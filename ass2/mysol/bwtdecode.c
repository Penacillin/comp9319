#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <time.h>
#include <aio.h>
#include <immintrin.h>


#include "vector_utils.h"

#define ENDING_CHAR '\n'
#define TABLE_SIZE 32
#define PAGE_TABLE_SIZE (15728640/TABLE_SIZE+1)
#define RANK_ENTRY_SIZE 4
#define RANK_ENTRY_MASK 0b11
#define BITS_PER_SYMBOL 2
#define rankTable_SIZE (15728640/RANK_ENTRY_SIZE) // 4 chars per 8 bits (2 bits per char)
#define INPUT_BUF_SIZE 524288
#define OUTPUT_BUF_SIZE 210000

// 256/8 chars per 256 bit accumulator
#define CHAR_COUNT_STEP (256/8)
__m256i A_CHAR_MASK;
__m256i C_CHAR_MASK;
__m256i G_CHAR_MASK;
__m256i T_CHAR_MASK;
__m256i ENDING_CHAR_MASK;
__m256i ONE_CHAR_MASK;
const __m128i A_CHAR_MASK_64 = {0x4141414141414141, 0x4141414141414141};
const __m128i C_CHAR_MASK_64 = {0x4343434343434343u, 0x4343434343434343u};
const __m128i G_CHAR_MASK_64 = {0x4747474747474747u, 0x4747474747474747u};
const __m128i T_CHAR_MASK_64 = {0x5454545454545454u, 0x5454545454545454u};

const unsigned long long CHAR_EXTRACT8_2_MASK = 0x0606060606060606u;
const unsigned long long CHAR_DEPOSIT2_8_MASK = 0x3333333333333333u;

#define FALSE 0
#define TRUE 1


// 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000 00000000
// AAAAAAAA AAAAAAAA AAAAAAAA BBBBBBBB BBBBBBBB BBBBBBBB CCCCCCCC CCCCCCCC CCCCCCCC DDDDDDDD DDDDDDDD DDDDDDDD  

#define RUN_COUNT_UPPER_MASK 0xFFFFFF00
#define RUN_COUNT_LOWER_MASK 0x00FFFFFF

union RunCountCumEntry {
    struct __attribute__((__packed__)) _A_entry {
        u_int32_t val;
        unsigned char x[8];
    } a_entry;
    struct __attribute__((__packed__)) _B_entry {
        unsigned char _x[3];
        u_int32_t val;
        unsigned char _y[5];
    } b_entry;
    struct __attribute__((__packed__)) _C_entry {
        unsigned char _x[6];
        u_int32_t val;
        unsigned char _y[2];
    } c_entry;
    struct __attribute__((__packed__)) _D_entry {
        unsigned char _x[8];
        u_int32_t val;
    } d_entry;
};

// Use pages of 32 symbols (8 chars (4 symbols per char))
// With a snapshot of all pages leading to this page
#if TABLE_SIZE/RANK_ENTRY_SIZE != 8
#warning "Rank Entry chars not 8, union may be wrong."
#endif
typedef struct __attribute__((__packed__))  _RankEntry {
    union RunCountCumEntry snapshot;
    union _SymbolArray {
        char char_array[(TABLE_SIZE/RANK_ENTRY_SIZE)];
        u_int64_t int_val;
    } symbol_array;
} RankEntry;


const size_t RunCountCumEntrySize = sizeof(RankEntry);

typedef struct _BWTDecode {
    RankEntry rankTable[PAGE_TABLE_SIZE]; // 9830400
    u_int32_t endingCharIndex; // 4
    u_int32_t CTable[128]; // 512
    int bwt_file_fd; // 4

    u_int32_t rankTableSize; // 4
} BWTDecode;


#define LANGUAGE_SIZE 5
const unsigned LANGUAGE[LANGUAGE_SIZE] = {'\n', 'A', 'C', 'G', 'T'};
const unsigned SYMBOL_ARRAY_LANGUAGE[] = {'A', 'C', 'T', 'G'};
__m128i SYMBOL_ARRAY_LANGUAGE_MASKS[4];

static inline unsigned get_char_index(const char c) {
#ifdef DEBUG
    switch(c)  {
        case 'A': return 1;
        case 'C': return 2;
        case 'G': return 3;
        case 'T': return 4;
        case '\n': return 0;
    };
    fprintf(stderr, "FATAL UNKOWN CHARACTER %d\n", c);
    exit(1);
#else
    switch(c)  {
        case 'A': return 1;
        case 'C': return 2;
        case 'G': return 3;
        case 'T': return 4;
        case '\n': return 0;
    };
    exit(1);
#endif
}

static inline unsigned get_rank_entry_char_index(const char c) {
#ifdef DEBUG
    switch(c)  {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 3;
        case 'T': return 2;
        case '\n': return 1;
    };
    fprintf(stderr, "FATAL UNKOWN CHARACTER %d\n", c);
    exit(1);
#else
    switch(c)  {
        case 'A': return 0;
        case 'T': return 2;
        case 'G': return 3;
        default: return 1;
    };
#endif
}

// A      C      G     T                 \n
// 0      2      6     9          
// 0000   0010   0110  1001
// 01000001  01000011  01000111  01010100  00001010
// compressed mask: 0x6 = 0b00000110
// 0 1 2 3
// 0000   0001   0010  0011
// A + B + C + D == T <= 15mil . 8 bytes (64 bits). naive = 9 bytes (72 bits).
//     A
//   B   C
// 
//


off_t open_input_file(BWTDecode* decode_info, char *bwt_file_path) {
    int fd = open(bwt_file_path, O_RDONLY);
    if (fd < 0) {
        printf("Could not open BWT file %s\n", bwt_file_path);
        exit(1);
    }

    decode_info->bwt_file_fd = fd;

    off_t res = lseek(fd, 0, SEEK_END);
    if (res <= 0) {
        printf("BWT file %s is empty.\n", bwt_file_path);
        exit(1);
    }
    lseek(fd, 0, SEEK_SET);

    return res;
}


void prepare_bwt_decode(BWTDecode *decode_info) {
    ssize_t k;
    unsigned curr_index = 0;
    unsigned acc_base_page = 0;
    unsigned page_index = 0;
    unsigned tempRunCount[128] = {0};
    char end_char_found = FALSE;
    char __attribute__((aligned (32))) in_buffer[INPUT_BUF_SIZE];


    // First snapshot starting state
    decode_info->rankTable[page_index].snapshot.a_entry.val = 0;
    decode_info->rankTable[page_index].snapshot.b_entry.val = 0;
    decode_info->rankTable[page_index].snapshot.c_entry.val = 0;
    decode_info->rankTable[page_index].snapshot.d_entry.val = 0;
    ++page_index;

    __m256i a_accum = _mm256_set1_epi8(0); 
    __m256i c_accum = _mm256_set1_epi8(0); 
    __m256i g_accum = _mm256_set1_epi8(0); 
    __m256i t_accum = _mm256_set1_epi8(0); 
    __m256i temp_256i_buf;

    ssize_t i = 0;
    while(__glibc_likely((k = read(decode_info->bwt_file_fd, in_buffer, INPUT_BUF_SIZE)) > 0)) {
        i = 0;
#ifdef DEBUG
        fprintf(stderr, "Read %ld bytes\n", k);
#endif
        for (; i < k - CHAR_COUNT_STEP + 1; i += CHAR_COUNT_STEP) {
            // temp_256i_buf = _mm256_stream_load_si256((const __m256i*)(in_buffer + i));
            a_accum = _mm256_sub_epi8(a_accum, _mm256_cmpeq_epi8(*(const __m256i*)(in_buffer + i), A_CHAR_MASK));
            c_accum = _mm256_sub_epi8(c_accum, _mm256_cmpeq_epi8(*(const __m256i*)(in_buffer + i), C_CHAR_MASK));
            g_accum = _mm256_sub_epi8(g_accum, _mm256_cmpeq_epi8(*(const __m256i*)(in_buffer + i), G_CHAR_MASK));
            t_accum = _mm256_sub_epi8(t_accum, _mm256_cmpeq_epi8(*(const __m256i*)(in_buffer + i), T_CHAR_MASK));
            // _mm_prefetch((void*)(in_buffer + i + CHAR_COUNT_STEP), _MM_HINT_T2);
            const u_int64_t string_2bit_chars = _pext_u64(
                    *(unsigned long long*)(in_buffer+i), CHAR_EXTRACT8_2_MASK) | 
                    (_pext_u64(*(unsigned long long*)(in_buffer+i+8), CHAR_EXTRACT8_2_MASK) << 16) | 
                    (_pext_u64(*(unsigned long long*)(in_buffer+i+16), CHAR_EXTRACT8_2_MASK) << 32) |
                    (_pext_u64(*(unsigned long long*)(in_buffer+i+24), CHAR_EXTRACT8_2_MASK) << 48);

            decode_info->rankTable[page_index-1].symbol_array.int_val = string_2bit_chars;

            curr_index += CHAR_COUNT_STEP;
            // Snapshot rank table run counts
            if (__glibc_unlikely(curr_index % TABLE_SIZE == 0)) {
#ifdef DEBUG
                assert(page_index < rankTable_SIZE);
#endif
                // u_int32_t a_accum_sum = mm256_hadd_epi8(a_accum);
                // u_int32_t c_accum_sum = mm256_hadd_epi8(c_accum);
                // u_int32_t g_accum_sum = mm256_hadd_epi8(g_accum);
                // u_int32_t t_accum_sum = mm256_hadd_epi8(t_accum);
                // const __m256i accum_sums = mm256_hadd_epi8_4(a_accum, c_accum, g_accum, t_accum);
                // const u_int32_t a_accum_sum = (accum_sums[2] & 0xFFFF);
                // const u_int32_t c_accum_sum = (accum_sums[0] & 0xFFFF);
                // const u_int32_t g_accum_sum = (accum_sums[2] & 0xFFFF0000) >> 16;
                // const u_int32_t t_accum_sum = (accum_sums[0] & 0xFFFF0000) >> 16;
                const __m128i accum_sums = mm256_hadd_epi8_4_128(a_accum, c_accum, g_accum, t_accum);
                // const u_int32_t a_accum_sum = _mm_extract_epi32(accum_sums, 0);
                // const u_int32_t c_accum_sum = _mm_extract_epi32(accum_sums, 1);
                // const u_int32_t g_accum_sum = _mm_extract_epi32(accum_sums, 2);
                // const u_int32_t t_accum_sum = _mm_extract_epi32(accum_sums, 3);
                const u_int32_t a_accum_sum = accum_sums[0];
                const u_int32_t c_accum_sum = accum_sums[0] >> 32;
                const u_int32_t g_accum_sum = accum_sums[1];
                const u_int32_t t_accum_sum = accum_sums[1] >> 32;


                decode_info->rankTable[page_index].snapshot.a_entry.val =
                    (decode_info->rankTable[acc_base_page].snapshot.a_entry.val & RUN_COUNT_LOWER_MASK) + a_accum_sum;
                decode_info->rankTable[page_index].snapshot.b_entry.val |=
                    (decode_info->rankTable[acc_base_page].snapshot.b_entry.val & RUN_COUNT_LOWER_MASK) + c_accum_sum;
                decode_info->rankTable[page_index].snapshot.c_entry.val |=
                    (decode_info->rankTable[acc_base_page].snapshot.c_entry.val & RUN_COUNT_LOWER_MASK) + g_accum_sum;
                decode_info->rankTable[page_index].snapshot.d_entry.val |=
                    (((decode_info->rankTable[acc_base_page].snapshot.d_entry.val & RUN_COUNT_UPPER_MASK) >> 8) + t_accum_sum) << 8;

#ifdef DEBUG
                fprintf(stderr, "accum sums %u %u %u %u\n",
                         a_accum_sum, c_accum_sum, g_accum_sum, t_accum_sum);
#endif

                if (__glibc_unlikely(!end_char_found && a_accum_sum + c_accum_sum + g_accum_sum + t_accum_sum !=
                                      (page_index - acc_base_page) * TABLE_SIZE)) {
#ifdef DEBUG
                    fprintf(stderr, "Found endchar around %u %d != %d\n", curr_index,
                        a_accum_sum + c_accum_sum + g_accum_sum + t_accum_sum, (page_index - acc_base_page) * TABLE_SIZE);
#endif
#ifdef PERF
                    clock_t t = clock();
#endif
                    // Find ending char
                    unsigned backward_iter = 0;
                    do {
                        temp_256i_buf = _mm256_cmpeq_epi8(*(const __m256i*)(in_buffer + i - backward_iter * CHAR_COUNT_STEP), ENDING_CHAR_MASK);
                        for (unsigned buffer_int_index = 0; buffer_int_index < 4; ++buffer_int_index) {
                            u_int64_t end_char_bit_offset = _lzcnt_u64(temp_256i_buf[buffer_int_index]);
                            if (end_char_bit_offset != 64) {
                                decode_info->endingCharIndex =
                                    curr_index - (backward_iter+1) * CHAR_COUNT_STEP + buffer_int_index * 8 + (8 - (end_char_bit_offset/8 + 1));
                                end_char_found = TRUE;
                            }
                        }
                        ++backward_iter;
                        // if (backward_iter != TABLE_SIZE/CHAR_COUNT_STEP)
                        //     temp_256i_buf = _mm256_load_si256(
                        //         (const __m256i*)(in_buffer + i - backward_iter * CHAR_COUNT_STEP));
                    } while (backward_iter < TABLE_SIZE/CHAR_COUNT_STEP);
#ifdef PERF
                    fprintf(stderr, "find end char took %f seconds to execute %d \n",
                             ((double)clock() - t)/CLOCKS_PER_SEC, decode_info->endingCharIndex);
#endif
                }
#ifdef DEBUG
                // printf("accums (%d): a=%d,c=%d,g=%d,t=%d\n",
                //     curr_index,
                //     mm256_hadd_epi8(a_accum),
                //     mm256_hadd_epi8(c_accum),
                //     mm256_hadd_epi8(g_accum),
                //     mm256_hadd_epi8(t_accum));
#endif
                // TODO: only need to do this when 255 per position max (32 accumulators, 8 per page, 256/8=32 pages)
                // only have to set every 32 pages
                if (__glibc_unlikely(page_index > acc_base_page + 7)) {
                    a_accum = _mm256_set1_epi8(0); 
                    c_accum = _mm256_set1_epi8(0); 
                    g_accum = _mm256_set1_epi8(0); 
                    t_accum = _mm256_set1_epi8(0);
                    acc_base_page = page_index;
                }

                ++page_index;
            }
        }
        unsigned rank_index = 0;
        for (;i < k; ++rank_index) {
            // Clear out page ready to input symbols
            decode_info->rankTable[page_index-1].symbol_array.char_array[rank_index] = 0;
            for (unsigned j = 0; j < RANK_ENTRY_SIZE && i < k; ++j, ++i) {
                const char c = in_buffer[i];
                if (__glibc_unlikely(c == '\n')) decode_info->endingCharIndex = curr_index;
                const unsigned char_val = get_rank_entry_char_index(c);
                // Put symbol into rank array
                decode_info->rankTable[page_index-1].symbol_array.char_array[rank_index] |=
                    char_val << (j*BITS_PER_SYMBOL);

                // Run count for rank table
                ++tempRunCount[(unsigned)c];

                ++curr_index;
                // Snapshot runCount
                if (__glibc_unlikely(curr_index % TABLE_SIZE == 0)) {
                    // assert(page_index < PAGE_TABLE_SIZE);
                    fprintf(stderr, "WARNING: Incorrectly snapshoting\n");
                    const __m128i accum_sums = mm256_hadd_epi8_4_128(a_accum, c_accum, g_accum, t_accum);
                    const u_int32_t a_accum_sum = accum_sums[0];
                    const u_int32_t c_accum_sum = accum_sums[0] >> 32;
                    const u_int32_t g_accum_sum = accum_sums[1];
                    const u_int32_t t_accum_sum = accum_sums[1] >> 32;
                    decode_info->rankTable[page_index].snapshot.a_entry.val = 
                        (decode_info->rankTable[acc_base_page].snapshot.a_entry.val & RUN_COUNT_LOWER_MASK) + a_accum_sum + tempRunCount[LANGUAGE[1]];
                    decode_info->rankTable[page_index].snapshot.b_entry.val |=
                         (decode_info->rankTable[acc_base_page].snapshot.b_entry.val & RUN_COUNT_LOWER_MASK) + c_accum_sum + tempRunCount[LANGUAGE[2]];
                    decode_info->rankTable[page_index].snapshot.c_entry.val |=
                         (decode_info->rankTable[acc_base_page].snapshot.c_entry.val & RUN_COUNT_LOWER_MASK) + g_accum_sum + tempRunCount[LANGUAGE[3]];
                    decode_info->rankTable[page_index].snapshot.d_entry.val |=
                         (((decode_info->rankTable[acc_base_page].snapshot.d_entry.val & RUN_COUNT_UPPER_MASK) >> 8) + t_accum_sum + tempRunCount[LANGUAGE[4]]) << 8;
                    tempRunCount[LANGUAGE[1]] = 0;
                    tempRunCount[LANGUAGE[2]] = 0;
                    tempRunCount[LANGUAGE[3]] = 0;
                    tempRunCount[LANGUAGE[4]] = 0;

                    if (__glibc_unlikely(page_index > acc_base_page + 7)) {
                        a_accum = _mm256_set1_epi8(0); 
                        c_accum = _mm256_set1_epi8(0); 
                        g_accum = _mm256_set1_epi8(0); 
                        t_accum = _mm256_set1_epi8(0);
                        acc_base_page = page_index;
                    }
                    ++page_index;
                    rank_index = 0;
                }
            }
        }
    }
#ifdef DEBUG
    fprintf(stderr, "accums: a=%d,c=%d,g=%d,t=%d\n",
            mm256_hadd_epi8(a_accum),
            mm256_hadd_epi8(c_accum),
            mm256_hadd_epi8(g_accum),
            mm256_hadd_epi8(t_accum));
#endif
    u_int32_t a_accum_sum = mm256_hadd_epi8(a_accum);
    u_int32_t c_accum_sum = mm256_hadd_epi8(c_accum);
    u_int32_t g_accum_sum = mm256_hadd_epi8(g_accum);
    u_int32_t t_accum_sum = mm256_hadd_epi8(t_accum);
    decode_info->rankTable[page_index].snapshot.a_entry.val =
        (decode_info->rankTable[acc_base_page].snapshot.a_entry.val & RUN_COUNT_LOWER_MASK) +
            a_accum_sum + tempRunCount[LANGUAGE[1]];
    decode_info->rankTable[page_index].snapshot.b_entry.val |=
        (decode_info->rankTable[acc_base_page].snapshot.b_entry.val & RUN_COUNT_LOWER_MASK) +
            c_accum_sum + tempRunCount[LANGUAGE[2]];
    decode_info->rankTable[page_index].snapshot.c_entry.val |=
        (decode_info->rankTable[acc_base_page].snapshot.c_entry.val & RUN_COUNT_LOWER_MASK) +
            g_accum_sum + tempRunCount[LANGUAGE[3]];
    decode_info->rankTable[page_index].snapshot.d_entry.val |=
        (((decode_info->rankTable[acc_base_page].snapshot.d_entry.val & RUN_COUNT_UPPER_MASK) >> 8) +
            t_accum_sum + tempRunCount[LANGUAGE[4]]) << 8;
    if (__glibc_unlikely(!end_char_found)) {
        // Find ending char
        for (int backward_iter = i - 1; backward_iter >= 0; --backward_iter) {
            if (in_buffer[backward_iter] == ENDING_CHAR) {
                decode_info->endingCharIndex = curr_index - (i - backward_iter);
#ifdef DEBUG
                fprintf(stderr, "Found ending char index in remainder %d\n", backward_iter);
#endif
                break;
            }
        }
    }
#ifdef DEBUG
    fprintf(stderr, "endingCharIndex %d %d\n", end_char_found, decode_info->endingCharIndex);
#endif

    decode_info->CTable[LANGUAGE[1]] = 1;
    decode_info->CTable[LANGUAGE[2]] = 
        decode_info->CTable[LANGUAGE[1]] + (decode_info->rankTable[page_index].snapshot.a_entry.val & RUN_COUNT_LOWER_MASK);
    decode_info->CTable[LANGUAGE[3]] =
        decode_info->CTable[LANGUAGE[2]] + (decode_info->rankTable[page_index].snapshot.b_entry.val & RUN_COUNT_LOWER_MASK);
    decode_info->CTable[LANGUAGE[4]] =
        decode_info->CTable[LANGUAGE[3]] + (decode_info->rankTable[page_index].snapshot.c_entry.val & RUN_COUNT_LOWER_MASK);

    decode_info->rankTableSize = curr_index;
}



void print_ctable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < 5; ++i) {
        fprintf(stderr, "%c: %d\n", LANGUAGE[i], decode_info->CTable[(unsigned)LANGUAGE[i]]);
    }
}

void print_ranktable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < decode_info->rankTableSize; ++i) {
        const unsigned rank_entry_index = i % RANK_ENTRY_SIZE;
        const unsigned page_index = i / TABLE_SIZE;
        const unsigned rank_index = (i - page_index * TABLE_SIZE) / RANK_ENTRY_SIZE;
        fprintf(stderr, "%d(page=%d,rank=%d,entry=%d) >%c<\n", i, page_index, rank_index, rank_entry_index,
            SYMBOL_ARRAY_LANGUAGE[((decode_info->rankTable[page_index].symbol_array.char_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)]);
    }
    fprintf(stderr, "%d is endchar\n", decode_info->endingCharIndex);
}

void print_cumtable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < 3; ++i) {
        fprintf(stderr, "%u,", decode_info->rankTable[i].snapshot.a_entry.val & RUN_COUNT_LOWER_MASK);
        fprintf(stderr, "%u,", decode_info->rankTable[i].snapshot.b_entry.val & RUN_COUNT_LOWER_MASK);
        fprintf(stderr, "%u,", decode_info->rankTable[i].snapshot.c_entry.val & RUN_COUNT_LOWER_MASK);
        fprintf(stderr, "%u,", decode_info->rankTable[i].snapshot.d_entry.val >> 8);
        fprintf(stderr, "\n");
    }
}

double reader_timer = 0;
u_int64_t busy_waits = 0;
BWTDecode bwtDecode = {.CTable = {0}};

static inline char get_char_rank(const unsigned index, BWTDecode *decode_info, unsigned *next_index) {
    const unsigned snapshot_page_index = index / TABLE_SIZE + ((index % TABLE_SIZE > TABLE_SIZE / 2) ? 1 : 0);
    const unsigned page_index = index / TABLE_SIZE;
    const int direction =  (index % TABLE_SIZE > TABLE_SIZE / 2) ? -1 : 1;
    unsigned tempRunCount[128];
    tempRunCount['\n'] = 0;
    // Load in char counts until this page
    tempRunCount[LANGUAGE[1]] = decode_info->rankTable[snapshot_page_index].snapshot.a_entry.val & RUN_COUNT_LOWER_MASK;
    tempRunCount[LANGUAGE[2]] = decode_info->rankTable[snapshot_page_index].snapshot.b_entry.val & RUN_COUNT_LOWER_MASK;
    tempRunCount[LANGUAGE[3]] = decode_info->rankTable[snapshot_page_index].snapshot.c_entry.val & RUN_COUNT_LOWER_MASK;
    tempRunCount[LANGUAGE[4]] = decode_info->rankTable[snapshot_page_index].snapshot.d_entry.val >> 8;
    // clock_t t = clock();
    // Fill out char counts for this page
    unsigned char_index = snapshot_page_index * TABLE_SIZE - (direction == 1 ? 0 : 1);
    int rank_entry_index = (char_index & 0b11);
    unsigned rank_index = (char_index - page_index * TABLE_SIZE) / RANK_ENTRY_SIZE;
    unsigned out_char;
    if (__glibc_unlikely(direction == 1 &&
         char_index <= decode_info->endingCharIndex && decode_info->endingCharIndex <= index))
        --tempRunCount['C'];
    else if(__glibc_unlikely(index <= decode_info->endingCharIndex && decode_info->endingCharIndex <= char_index)) {
        ++tempRunCount['C'];
    }

#ifdef DEBUG
    fprintf(stderr, "Using page %d. char_index %d, rank_index %d, rank_entry %d\n",
             page_index, char_index, rank_index, rank_entry_index);
#endif
    if (direction == 1) {
        const unsigned char_page_index = index - page_index * TABLE_SIZE;
        const __m64 string_lo = _mm_cvtsi64_m64(
                _pdep_u64(decode_info->rankTable[page_index].symbol_array.int_val, CHAR_DEPOSIT2_8_MASK));
        const __m64 string_hi = _mm_cvtsi64_m64(
                _pdep_u64(decode_info->rankTable[page_index].symbol_array.int_val >> 16, CHAR_DEPOSIT2_8_MASK));
        __m128i symbol_string_16 = _mm_setr_epi64(string_lo, string_hi);
        const char symbol_language_char = _bextr_u64(
                decode_info->rankTable[page_index].symbol_array.int_val, char_page_index*2, 2);
        out_char = SYMBOL_ARRAY_LANGUAGE[(unsigned)symbol_language_char];        

        __m128i symbol_out_char_masked = _mm_cmpeq_epi8(
                symbol_string_16, SYMBOL_ARRAY_LANGUAGE_MASKS[(unsigned)symbol_language_char]);

        _mm_bslli_si128(symbol_out_char_masked, TABLE_SIZE - char_page_index);
        tempRunCount[out_char] +=
            (_mm_popcnt_u64(symbol_string_16[0]) + _mm_popcnt_u64(symbol_string_16[1])) / 8;
    } else {
        out_char =
            SYMBOL_ARRAY_LANGUAGE[((decode_info->rankTable[page_index].symbol_array.char_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)];
        --tempRunCount[out_char];
        while(__glibc_likely(char_index > index)) {
            --rank_entry_index;
            if (rank_entry_index == -1) {
                rank_entry_index = RANK_ENTRY_SIZE - 1;
                --rank_index;
            }
            out_char =
                SYMBOL_ARRAY_LANGUAGE[((decode_info->rankTable[page_index].symbol_array.char_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)];
            --tempRunCount[out_char];
            --char_index;
#ifdef DEBUG
            fprintf(stderr, "< %d %c %d\n", char_index, out_char, tempRunCount[out_char]);
#endif
        };
    }
    // reader_timer += ((double)clock() - t)/CLOCKS_PER_SEC;
    *next_index = tempRunCount[out_char] + decode_info->CTable[out_char];
#ifdef DEBUG
    fprintf(stderr, "%c %d %d\n", out_char, tempRunCount[out_char], decode_info->CTable[out_char]);
#endif
    return (char)out_char;
}

int setup_BWT(BWTDecode *decode_info, char* output_file_path) {
    int out_fd = open(output_file_path, O_CREAT | O_WRONLY, 0666);
    if (out_fd < 0) {
        char *err = strerror(errno);
        printf("Could not create the reversed file %s - %s\n", output_file_path, err);
        exit(1);
    }

#ifdef PERF
    clock_t t = clock();
#endif
    prepare_bwt_decode(decode_info);
#ifdef PERF
    t = clock() - t;
    printf("build_tables() took %f seconds to execute \n", ((double)t)/CLOCKS_PER_SEC);
#endif

    return out_fd;
}

// The main BWTDecode running algorithm
int do_stuff2(BWTDecode *decode_info,
              off_t file_size,
              int out_fd) {

#ifdef DEBUG
    print_ctable(decode_info);
    print_ranktable(decode_info);
    print_cumtable(decode_info);
#endif

    unsigned index = 0;

    char output_buffer[OUTPUT_BUF_SIZE];
    char output_buffer2[OUTPUT_BUF_SIZE];
    struct aiocb aiocbp;
    memset(&aiocbp, 0, sizeof(struct aiocb));
    char first_write = FALSE;
    char aiocbp_curr = 0;
    int res;
    aiocbp.aio_fildes = out_fd;
    aiocbp.aio_buf = output_buffer;
    aiocbp.aio_nbytes = OUTPUT_BUF_SIZE;

    unsigned output_buffer_index = sizeof(output_buffer);
    output_buffer[--output_buffer_index] = ENDING_CHAR;
    --file_size;
    while (file_size > 0) {
#ifdef DEBUG
        fprintf(stderr, "index: %d\n", index);
#endif
        unsigned next_index;
        const char out_char = get_char_rank(index, decode_info, &next_index);
        index = next_index;
        if (aiocbp_curr == 0) {
            output_buffer[--output_buffer_index] = out_char;
        } else {
            output_buffer2[--output_buffer_index] = out_char;
        }
        --file_size;

        if (output_buffer_index == 0) {
            while((res = aio_error(&aiocbp)) == EINPROGRESS && first_write) ++busy_waits;
            if (res != 0) {
#ifdef DEBUG
                fprintf(stderr, "index=%d,res=%d,error=%s\n", index, res, strerror(res));
#endif
                exit(1);
            }
#ifdef DEBUG
            printf("File Size %ld, index %d\n", file_size, index);
#endif
            if (aiocbp_curr == 0) {
                aiocbp.aio_offset = file_size;
                aiocbp.aio_buf = output_buffer;
                res = aio_write(&aiocbp);
                aiocbp_curr = 1;
            } else {
                aiocbp.aio_offset = file_size;
                aiocbp.aio_buf = output_buffer2;
                res = aio_write(&aiocbp);
                aiocbp_curr = 0;
            }
            if (res != 0) {
#ifdef DEBUG
                perror("AIO write instruction failed");
#endif
                exit(1);
            }
            first_write = TRUE;

            output_buffer_index = sizeof(output_buffer);
        }
    }

    while((res = aio_error(&aiocbp)) == EINPROGRESS && first_write) ++busy_waits;
    if (res != 0) exit(1);
    if (aiocbp_curr == 0) {
        aiocbp.aio_offset = file_size;
        aiocbp.aio_buf = output_buffer + output_buffer_index;
        aiocbp.aio_nbytes = sizeof(output_buffer) - output_buffer_index;
        res = aio_write(&aiocbp);
        aiocbp_curr = 1;
    } else {
        aiocbp.aio_offset = file_size;
        aiocbp.aio_buf = output_buffer2 + output_buffer_index;
        aiocbp.aio_nbytes = sizeof(output_buffer) - output_buffer_index;
        res = aio_write(&aiocbp);
        aiocbp_curr = 0;
    }
    first_write = TRUE;
    if (res != 0) exit(1);
    while((res = aio_error(&aiocbp)) == EINPROGRESS && first_write) ++busy_waits;
    if (res != 0) exit(1);
    close(out_fd);

    return 0;
}


int main(int argc, char** argv) {
    // Set constants
    A_CHAR_MASK = _mm256_set1_epi8('A');
    C_CHAR_MASK = _mm256_set1_epi8('C');
    G_CHAR_MASK = _mm256_set1_epi8('G');
    T_CHAR_MASK = _mm256_set1_epi8('T');
    ENDING_CHAR_MASK = _mm256_set1_epi8(ENDING_CHAR);
    ONE_CHAR_MASK = _mm256_set1_epi8(1);
    SHUFFLE_16_DOWN = _mm_setr_epi8(0, 1, 8, 9, 4, 5, 6, 7, 2, 3, 10, 11, 12, 13, 14, 15);
    SHUFFLE_16_DOWN_256 = _mm256_setr_epi8(0, 1, 8, 9, 4, 5, 6, 7, 2, 3, 10, 11, 12, 13, 14, 15,
                                            0, 1, 8, 9, 4, 5, 6, 7, 2, 3, 10, 11, 12, 13, 14, 15);
    SHUFFLE_2x2x16_4x16 = _mm_setr_epi8(8, 9, 6, 7, 0, 1, 6, 7, 10, 11, 14, 15, 2, 3, 14, 15);

    if (argc != 3) {
        printf("Usage: %s <BWT file path> <Reversed file path>\n", argv[0]);
        exit(1);
    }
#ifdef PERF
    fprintf(stderr, "BWTDecode size %ld RunCountCumEntrySize: %ld\n", sizeof(struct _BWTDecode), RunCountCumEntrySize);
#endif   
    off_t file_size = open_input_file(&bwtDecode, argv[1]);
#ifdef DEBUG
    fprintf(stderr, "BWT file file_size: %ld\n", file_size);
#endif
    const int out_fd = setup_BWT(&bwtDecode, argv[2]);
    do_stuff2(&bwtDecode, file_size, out_fd);

    close(bwtDecode.bwt_file_fd);

#ifdef PERF
    printf("reader timer %lf\n", reader_timer);
    printf("write busy waits   %lu\n", busy_waits);
#endif

    return 0;
}
