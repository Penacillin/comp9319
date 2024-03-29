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
#include <tmmintrin.h>
#include <immintrin.h>

#include "vector_utils.h"

#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define MIN(x, y) (((x) < (y)) ? (x) : (y))

#define LANGUAGE_SIZE 5
const unsigned LANGUAGE[] = {'\n', 'A', 'C', 'G', 'T', 'U'};
const unsigned SYMBOL_ARRAY_LANGUAGE[] = {'A', 'C', 'T', 'G'};
#define ENDING_CHAR '\n'
#define QUERY_MAX_SIZE 101

#define INPUT_BUF_SIZE 524288

#define MAX_INPUT_SIZE 104857600
#define SYMBOLS_PER_CHAR 4
#define BITS_PER_SYMBOL 2
#define PAGE_SIZE 256
#define UNCACHED_PAGE 65535
#define PAGE_CACHE_SIZE 32768
#define RANK_TABLE_SIZE (MAX_INPUT_SIZE/PAGE_SIZE)

#define FALSE 0
#define TRUE 1

typedef struct _PageEntry {
    union _SymbolArray {
        unsigned char char_array[PAGE_SIZE/SYMBOLS_PER_CHAR];
        u_int64_t int_array[PAGE_SIZE/SYMBOLS_PER_CHAR/8];
    } symbol_array;
} PageEntry;


#define RUN_COUNT_LOWER_MASK 0x00FFFFFF
typedef union _RankEntry {
    union Entries {
        struct __attribute__((__packed__)) _A_entry {
            u_int32_t val;
            u_int32_t x[3];
        } a_entry;
        struct __attribute__((__packed__)) _B_entry {
            u_int32_t _x[1];
            u_int32_t val;
            u_int32_t _y[2];
        } b_entry;
        struct __attribute__((__packed__)) _C_entry {
            u_int32_t _x[2];
            u_int32_t val;
            u_int32_t _y[1];
        } c_entry;
        struct __attribute__((__packed__)) _D_entry {
            u_int32_t _x[3];
            u_int32_t val;
        } d_entry;
    } entries;
    __m128i int_vector;
} RankEntry;


typedef struct _BWTSearch {
    u_int32_t rank_table_size;
    RankEntry __attribute__((aligned (16)))  rank_table[RANK_TABLE_SIZE+1];
    PageEntry symbol_pages[PAGE_CACHE_SIZE];

    u_int32_t ending_char_index;
    u_int32_t CTable[128];

    u_int16_t cache_clock;
    u_int32_t cache_to_page[PAGE_CACHE_SIZE];
    u_int16_t page_to_cache[RANK_TABLE_SIZE + 1];

    int bwt_file_fd;
} BWTSearch;

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

const size_t bwt_search_size = sizeof(BWTSearch);

off_t open_input_file(BWTSearch* search_info, char *bwt_file_path) {
    const int fd = open(bwt_file_path, O_RDONLY);
    if (fd < 0) {
        printf("Could not open BWT file %s\n", bwt_file_path);
        exit(1);
    }

    search_info->bwt_file_fd = fd;

    const off_t res = lseek(fd, 0, SEEK_END);
    if (res <= 0) {
        printf("BWT file %s is empty.\n", bwt_file_path);
        exit(1);
    }
    lseek(fd, 0, SEEK_SET);

    return res;
}

// 256/8 chars per 256 bit accumulator
#define CHAR_COUNT_STEP (256/8)
__m256i A_CHAR_MASK;
__m256i C_CHAR_MASK;
__m256i G_CHAR_MASK;
__m256i T_CHAR_MASK;
__m256i ENDING_CHAR_MASK;
__m256i ONE_CHAR_MASK;
const unsigned long long CHAR_EXTRACT8_2_MASK = 0x0606060606060606u;


void prepare_bwt_search(BWTSearch *search_info) {
    ssize_t k;
    unsigned curr_index = 0;
    unsigned acc_base_page = 0;
    unsigned page_index = 0;
    unsigned tempRunCount[128] = {0};
    char end_char_found = FALSE;
    char __attribute__((aligned (32))) in_buffer[INPUT_BUF_SIZE];

    // memset(search_info->page_to_cache, UNCACHED_PAGE, sizeof(search_info->page_to_cache));

    // First snapshot starting state
    search_info->rank_table[page_index].entries.a_entry.val = 0;
    search_info->rank_table[page_index].entries.b_entry.val = 0;
    search_info->rank_table[page_index].entries.c_entry.val = 0;
    search_info->rank_table[page_index].entries.d_entry.val = 0;
    search_info->page_to_cache[page_index] = UNCACHED_PAGE;
    ++page_index;

    __m256i a_accum = _mm256_set1_epi8(0); 
    __m256i c_accum = _mm256_set1_epi8(0); 
    __m256i g_accum = _mm256_set1_epi8(0); 
    __m256i t_accum = _mm256_set1_epi8(0); 
    __m256i temp_256i_buf;

    ssize_t i = 0;
    while(__glibc_likely((k = read(search_info->bwt_file_fd, in_buffer, INPUT_BUF_SIZE)) > 0)) {
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
            curr_index += CHAR_COUNT_STEP;
            // Snapshot rank table run counts
            if (__glibc_unlikely(curr_index % PAGE_SIZE == 0)) {
#ifdef DEBUG
                assert(page_index < RANK_TABLE_SIZE);
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
                const u_int32_t a_accum_sum = (u_int32_t)_mm_extract_epi32(accum_sums, 0);
                const u_int32_t c_accum_sum = (u_int32_t)_mm_extract_epi32(accum_sums, 1);
                const u_int32_t g_accum_sum = (u_int32_t)_mm_extract_epi32(accum_sums, 2);
                const u_int32_t t_accum_sum = (u_int32_t)_mm_extract_epi32(accum_sums, 3);
                // const u_int32_t a_accum_sum = (u_int32_t)(accum_sums[0]);
                // const u_int32_t c_accum_sum = (u_int32_t)(accum_sums[0] >> 32);
                // const u_int32_t g_accum_sum = (u_int32_t)(accum_sums[1]);
                // const u_int32_t t_accum_sum = (u_int32_t)(accum_sums[1] >> 32);

                search_info->rank_table[page_index].int_vector = _mm_add_epi32(
                        search_info->rank_table[acc_base_page].int_vector, accum_sums);

                // search_info->rank_table[page_index].entries.a_entry.val =
                //     search_info->rank_table[acc_base_page].entries.a_entry.val + a_accum_sum;
                // search_info->rank_table[page_index].entries.b_entry.val =
                //     search_info->rank_table[acc_base_page].entries.b_entry.val + c_accum_sum;
                // search_info->rank_table[page_index].entries.c_entry.val =
                //     search_info->rank_table[acc_base_page].entries.c_entry.val + g_accum_sum;
                // search_info->rank_table[page_index].entries.d_entry.val =
                //     search_info->rank_table[acc_base_page].entries.d_entry.val + t_accum_sum;

#ifdef DEBUG
                fprintf(stderr, "accum sums %u %u %u %u\n",
                         a_accum_sum, c_accum_sum, g_accum_sum, t_accum_sum);
#endif

                if (__glibc_unlikely(!end_char_found && a_accum_sum + c_accum_sum + g_accum_sum + t_accum_sum !=
                                      (page_index - acc_base_page) * PAGE_SIZE)) {
#ifdef DEBUG
                    fprintf(stderr, "Found endchar around %u %d != %d\n", curr_index,
                        a_accum_sum + c_accum_sum + g_accum_sum + t_accum_sum, (page_index - acc_base_page) * PAGE_SIZE);
#endif
#ifdef PERF
                    clock_t t = clock();
#endif
                    // Find ending char
                    unsigned backward_iter = 0;
                    do {
                        temp_256i_buf = _mm256_cmpeq_epi8(*(const __m256i*)(in_buffer + i - backward_iter * CHAR_COUNT_STEP), ENDING_CHAR_MASK);
                        for (unsigned buffer_int_index = 0; buffer_int_index < 4; ++buffer_int_index) {
                            u_int32_t end_char_bit_offset = (u_int32_t)_lzcnt_u64((u_int64_t)temp_256i_buf[buffer_int_index]);
                            if (end_char_bit_offset != 64) {
                                search_info->ending_char_index =
                                    curr_index - (backward_iter+1) * CHAR_COUNT_STEP + buffer_int_index * 8 + (8 - (end_char_bit_offset/8 + 1));
                                end_char_found = TRUE;
                            }
                        }
                        ++backward_iter;
                        // if (backward_iter != PAGE_SIZE/CHAR_COUNT_STEP)
                        //     temp_256i_buf = _mm256_load_si256(
                        //         (const __m256i*)(in_buffer + i - backward_iter * CHAR_COUNT_STEP));
                    } while (backward_iter < PAGE_SIZE/CHAR_COUNT_STEP);
#ifdef PERF
                    fprintf(stderr, "find end char took %f seconds to execute %d \n",
                             (double)(clock() - t)/CLOCKS_PER_SEC, search_info->ending_char_index);
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
                search_info->page_to_cache[page_index] = UNCACHED_PAGE;
                ++page_index;
            }
        }
        for (;i < k; i += 1) {
            // Clear out page ready to input symbols
            const char c = in_buffer[i];
            if (__glibc_unlikely(c == '\n'))  {
#ifdef DEBUG
                fprintf(stderr, "Found end char in linear search %d %ld\n", curr_index, i);
#endif
                search_info->ending_char_index = curr_index;
                end_char_found = TRUE;
            }
            // Add to all CTable above char
            // const unsigned char_val = get_char_index(c);
            // Run count for rank table
            ++tempRunCount[(unsigned)c];

            ++curr_index;
            // Snapshot rank table run counts
            if (__glibc_unlikely(curr_index % PAGE_SIZE == 0)) {
                // assert(page_index < PAGE_TABLE_SIZE);
                search_info->rank_table[page_index].entries.a_entry.val = 
                    search_info->rank_table[acc_base_page].entries.a_entry.val + tempRunCount[LANGUAGE[1]];
                search_info->rank_table[page_index].entries.b_entry.val = 
                    search_info->rank_table[acc_base_page].entries.b_entry.val + tempRunCount[LANGUAGE[2]];
                search_info->rank_table[page_index].entries.c_entry.val = 
                    search_info->rank_table[acc_base_page].entries.c_entry.val + tempRunCount[LANGUAGE[3]];
                search_info->rank_table[page_index].entries.d_entry.val = 
                    search_info->rank_table[acc_base_page].entries.d_entry.val + tempRunCount[LANGUAGE[4]];
                tempRunCount[LANGUAGE[1]] = 0;
                tempRunCount[LANGUAGE[2]] = 0;
                tempRunCount[LANGUAGE[3]] = 0;
                tempRunCount[LANGUAGE[4]] = 0;
                ++page_index;
            }
        }
    }
#ifdef DEBUG
    printf("accums: a=%d,c=%d,g=%d,t=%d\n",
            mm256_hadd_epi8(a_accum),
            mm256_hadd_epi8(c_accum),
            mm256_hadd_epi8(g_accum),
            mm256_hadd_epi8(t_accum));
#endif
    u_int32_t a_accum_sum = (u_int32_t)mm256_hadd_epi8(a_accum);
    u_int32_t c_accum_sum = (u_int32_t)mm256_hadd_epi8(c_accum);
    u_int32_t g_accum_sum = (u_int32_t)mm256_hadd_epi8(g_accum);
    u_int32_t t_accum_sum = (u_int32_t)mm256_hadd_epi8(t_accum);
    search_info->rank_table[page_index].entries.a_entry.val =
        search_info->rank_table[acc_base_page].entries.a_entry.val + a_accum_sum;
    search_info->rank_table[page_index].entries.b_entry.val =
        search_info->rank_table[acc_base_page].entries.b_entry.val + c_accum_sum;
    search_info->rank_table[page_index].entries.c_entry.val =
        search_info->rank_table[acc_base_page].entries.c_entry.val + g_accum_sum;
    search_info->rank_table[page_index].entries.d_entry.val =
        search_info->rank_table[acc_base_page].entries.d_entry.val + t_accum_sum;
    search_info->rank_table[page_index].entries.a_entry.val += tempRunCount[LANGUAGE[1]];
    search_info->rank_table[page_index].entries.b_entry.val += tempRunCount[LANGUAGE[2]];
    search_info->rank_table[page_index].entries.c_entry.val += tempRunCount[LANGUAGE[3]];
    search_info->rank_table[page_index].entries.d_entry.val += tempRunCount[LANGUAGE[4]];
    if (__glibc_unlikely(!end_char_found)) {
        // Find ending char
        for (ssize_t backward_iter = i - 1; backward_iter >= 0; --backward_iter) {
            if (in_buffer[backward_iter] == ENDING_CHAR) {
                search_info->ending_char_index = curr_index - (u_int32_t)(i - backward_iter);
#ifdef DEBUG
                fprintf(stderr, "Found ending char index in remainder %ld\n", backward_iter);
#endif
                break;
            }
        }
    }
#ifdef DEBUG
    fprintf(stderr, "ending_char_index %d %d\n", end_char_found, search_info->ending_char_index);
#endif

    search_info->CTable[LANGUAGE[1]] = 1;
    search_info->CTable[LANGUAGE[2]] = 
        search_info->CTable[LANGUAGE[1]] + search_info->rank_table[page_index].entries.a_entry.val;
    search_info->CTable[LANGUAGE[3]] =
        search_info->CTable[LANGUAGE[2]] + search_info->rank_table[page_index].entries.b_entry.val;
    search_info->CTable[LANGUAGE[4]] =
        search_info->CTable[LANGUAGE[3]] + search_info->rank_table[page_index].entries.c_entry.val;
    search_info->CTable[LANGUAGE[5]] =
        search_info->CTable[LANGUAGE[4]] + search_info->rank_table[page_index].entries.d_entry.val;

    search_info->rank_table_size = curr_index;

    // Fix up last snapshot to count trailing 'A's
    search_info->rank_table[page_index].entries.a_entry.val +=
        (PAGE_SIZE - (curr_index % PAGE_SIZE));
}


#ifdef PERF
unsigned long long cache_hits = 0;
unsigned long long cache_misses = 0;
#endif

void print_ranktable_cache(const BWTSearch *search_info, const u_int16_t cache) {
    for (unsigned i = 0; i < PAGE_SIZE; ++i) {
        const unsigned rank_entry_index = i % SYMBOLS_PER_CHAR;
        const unsigned rank_index = i / SYMBOLS_PER_CHAR;
        fprintf(stderr, "%d(page=%d,rank=%d,entry=%d) >%c<\n", i, cache, rank_index, rank_entry_index,
            SYMBOL_ARRAY_LANGUAGE[((search_info->symbol_pages[cache].symbol_array.char_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)]);
    }
    fprintf(stderr, "%d is endchar\n", search_info->ending_char_index);
}

u_int16_t get_cache_for_index(BWTSearch *search_info, const size_t page_index) {
    if (search_info->page_to_cache[page_index] != UNCACHED_PAGE) {
#ifdef PERF
        ++cache_hits;
#endif
        return search_info->page_to_cache[page_index];
    }
#ifdef PERF
    ++cache_misses;
#endif

#ifdef DEBUG
    fprintf(stderr, "page %lu not in cache\n", page_index);
#endif
    // invalidate page at cache clock that we're about to write over
    search_info->page_to_cache[search_info->cache_to_page[search_info->cache_clock]] = UNCACHED_PAGE;
    memset(search_info->symbol_pages[search_info->cache_clock].symbol_array.char_array, 0, PAGE_SIZE/SYMBOLS_PER_CHAR);

    char in_buffer[PAGE_SIZE];
    lseek(search_info->bwt_file_fd, (off_t)(page_index * PAGE_SIZE), SEEK_SET);
    ssize_t k = read(search_info->bwt_file_fd, in_buffer, PAGE_SIZE);
    unsigned rank_index = 0;

    // START READING INTO PAGE
    ssize_t i = 0;
    for (; i < k - CHAR_COUNT_STEP + 1; i += CHAR_COUNT_STEP) {
        const u_int64_t string_2bit_chars =
            _pext_u64(*(unsigned long long*)(in_buffer+i), CHAR_EXTRACT8_2_MASK) | 
            (_pext_u64(*(unsigned long long*)(in_buffer+i+8), CHAR_EXTRACT8_2_MASK) << 16) | 
            (_pext_u64(*(unsigned long long*)(in_buffer+i+16), CHAR_EXTRACT8_2_MASK) << 32) |
            (_pext_u64(*(unsigned long long*)(in_buffer+i+24), CHAR_EXTRACT8_2_MASK) << 48);

        search_info->symbol_pages[search_info->cache_clock].symbol_array.int_array[rank_index] = string_2bit_chars;
        ++rank_index;
    }
    rank_index *= 8;
    for (;i<k; ++rank_index) {
        for (unsigned j = 0; j < SYMBOLS_PER_CHAR && i < k; ++j, ++i) {
            const unsigned char_val = get_rank_entry_char_index(in_buffer[i]);
            search_info->symbol_pages[search_info->cache_clock].symbol_array.char_array[rank_index] |=
                (unsigned char)(char_val << (j*BITS_PER_SYMBOL));
        }
    }
    // END READING
#ifdef DEBUG
    print_ranktable_cache(search_info, search_info->cache_clock);
#endif

    // record newly cached page
    search_info->cache_to_page[search_info->cache_clock] = (u_int32_t)page_index;
    search_info->page_to_cache[page_index] = search_info->cache_clock;
    // move cache clock forward
    const u_int16_t cache_index = search_info->cache_clock;
    search_info->cache_clock = (u_int16_t)(search_info->cache_clock + 1) % PAGE_CACHE_SIZE;

    return cache_index;
}

unsigned get_occurence(BWTSearch *search_info, const char c, const size_t index) {
    const size_t page_index = index / PAGE_SIZE;
    const u_int16_t cache_index = get_cache_for_index(search_info, page_index);

    // const unsigned snapshot_page_index = index / PAGE_SIZE + ((index % PAGE_SIZE > PAGE_SIZE / 2) ? 1 : 0);
    // const int direction =  (index % PAGE_SIZE > PAGE_SIZE / 2) ? -1 : 1;
    const size_t snapshot_page_index = index / PAGE_SIZE;
    const int direction =  1;

    unsigned tempRunCount[128];
    tempRunCount['\n'] = 0;
    // Load in char counts until this page
    tempRunCount[LANGUAGE[1]] = search_info->rank_table[snapshot_page_index].entries.a_entry.val;
    tempRunCount[LANGUAGE[2]] = search_info->rank_table[snapshot_page_index].entries.b_entry.val;
    tempRunCount[LANGUAGE[3]] = search_info->rank_table[snapshot_page_index].entries.c_entry.val;
    tempRunCount[LANGUAGE[4]] = search_info->rank_table[snapshot_page_index].entries.d_entry.val;
    // clock_t t = clock();
    // Fill out char counts for this page
    size_t char_index = snapshot_page_index * PAGE_SIZE - (direction == 1 ? 0 : 1);
    int rank_entry_index = (char_index & 0b11);
    size_t rank_index = (char_index - page_index * PAGE_SIZE) / SYMBOLS_PER_CHAR;
    unsigned out_char;
    if (__glibc_unlikely(direction == 1 &&
         char_index <= search_info->ending_char_index && search_info->ending_char_index < index))
        --tempRunCount['C'];
    else if(__glibc_unlikely(index <= search_info->ending_char_index && search_info->ending_char_index <= char_index)) {
        ++tempRunCount['C'];
    }

#ifdef DEBUG
    fprintf(stderr, "index %ld, Using page %ld snapshot page %ld. char_index %ld, rank_index %ld, rank_entry %d\n",
             index, page_index, snapshot_page_index, char_index, rank_index, rank_entry_index);
#endif
    if (direction == 1) {
        while(__glibc_likely(char_index <= index)) {
            out_char =
                SYMBOL_ARRAY_LANGUAGE[((search_info->symbol_pages[cache_index].symbol_array.char_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)];
            ++tempRunCount[out_char];
            ++rank_entry_index;
            if (rank_entry_index == SYMBOLS_PER_CHAR) {
                rank_entry_index = 0;
                ++rank_index;
            }
            ++char_index;
        };
    } else {
        out_char =
            SYMBOL_ARRAY_LANGUAGE[((search_info->symbol_pages[cache_index].symbol_array.char_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)];
        --tempRunCount[out_char];
        while(__glibc_likely(char_index > index + 1)) {
            --rank_entry_index;
            if (rank_entry_index == -1) {
                rank_entry_index = SYMBOLS_PER_CHAR - 1;
                --rank_index;
            }
            out_char =
                SYMBOL_ARRAY_LANGUAGE[((search_info->symbol_pages[cache_index].symbol_array.char_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)];
            --tempRunCount[out_char];
            --char_index;
#ifdef DEBUG
            fprintf(stderr, "< %ld %c %d\n", char_index, out_char, tempRunCount[out_char]);
#endif
        };
    }
    // reader_timer += ((double)clock() - t)/CLOCKS_PER_SEC;
#ifdef DEBUG
    printf("%c %d %d\n", out_char, tempRunCount[out_char], search_info->CTable[out_char]);
#endif
    return tempRunCount[(unsigned)c];
}

unsigned process_search_query(BWTSearch *search_info,
                         const char *search_query,
                         const size_t search_query_size) {
#ifdef DEBUG
    fprintf(stderr, "querying: '");
    for (size_t i = 0; i < search_query_size; ++i)
        putc(search_query[i], stderr);
    fprintf(stderr, "'\n");
#endif
    size_t i = search_query_size - 1;
    // Begin algo
    char c = search_query[i];
    unsigned first = search_info->CTable[(unsigned)c];
    unsigned last = search_info->CTable[LANGUAGE[get_char_index(c)+1]] - 1;
#ifdef DEBUG
    fprintf(stderr, "first=%u,last=%u\n", first, last);
#endif
    while (first <= last && i >= 1) {
        c = search_query[i-1];
        first = search_info->CTable[(unsigned)c] + get_occurence(search_info, c, first-1);
        last = search_info->CTable[(unsigned)c] + get_occurence(search_info, c, last) - 1;
#ifdef DEBUG
        fprintf(stderr, "first=%u,last=%u\n", first, last);
#endif
        --i;
    }
    if (last < first) return 0;
    return last - first + 1;
}

void read_search_queries(BWTSearch *search_info) {
    char query_buf[QUERY_MAX_SIZE + 2] = {0};
    size_t query_size;
    while(fgets(query_buf, QUERY_MAX_SIZE+1, stdin) != NULL) {
        query_size = strlen(query_buf);
        const unsigned num_matches = process_search_query(search_info, query_buf, query_size - 1);
        printf("%d\n", num_matches);
        memset(query_buf, 0, QUERY_MAX_SIZE + 2);
    }
}

void print_ctable(const BWTSearch *search_info) {
    for (unsigned i = 0; i < LANGUAGE_SIZE+1; ++i) {
        printf("%c: %d\n", LANGUAGE[i], search_info->CTable[(unsigned)LANGUAGE[i]]);
    }
}

void print_rank_table(const BWTSearch *search_info) {
    // for (unsigned i = 0; i < MIN(search_info->rank_table_size, PAGE_CACHE_SIZE * PAGE_SIZE); ++i) {
    for (unsigned i = 0; i < 256; ++i) {
        const unsigned rank_entry_index = i % SYMBOLS_PER_CHAR;
        const unsigned page_index = i / PAGE_SIZE;
        const unsigned rank_index = (i - page_index * PAGE_SIZE) / SYMBOLS_PER_CHAR;
        fprintf(stderr, "%d(page=%d,rank=%d,entry=%d) >%c<\n", i, page_index, rank_index, rank_entry_index,
            LANGUAGE[((search_info->symbol_pages[page_index].symbol_array.char_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)+1]);
    }
    printf("%d is endchar\n", search_info->ending_char_index);
}

void print_cumtable(const BWTSearch *search_info) {
    // for (unsigned i = 0; i < search_info->rank_table_size / PAGE_SIZE + 1; ++i) {
    for (unsigned i = 0; i < 4; ++i) {
        fprintf(stderr, "%u,", search_info->rank_table[i].entries.a_entry.val);
        fprintf(stderr, "%u,", search_info->rank_table[i].entries.b_entry.val);
        fprintf(stderr, "%u,", search_info->rank_table[i].entries.c_entry.val);
        fprintf(stderr, "%u,", search_info->rank_table[i].entries.d_entry.val);
        fprintf(stderr, "\n");
    }
}

BWTSearch bwt_search = {.cache_clock = 0, .CTable ={0}};

int main(int argc, char *argv[]) {
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

#ifdef DEBUG
    printf("%ld \n", sizeof(A_CHAR_MASK[0]));
    printf("%c %c %c %c\n",
        (char)(A_CHAR_MASK[0] & 0xFF), (char)(C_CHAR_MASK[0] & 0xFF), (char)(T_CHAR_MASK[0] & 0xFF), (char)(G_CHAR_MASK[0] & 0xFF));
    __m256i debug_temp = _mm256_set1_epi8(1);
    debug_temp = _mm256_cmpeq_epi8(A_CHAR_MASK, ONE_CHAR_MASK);
    printf("cmpeq: %lld\n", debug_temp[0]);
    debug_temp = _mm256_and_si256(A_CHAR_MASK, ONE_CHAR_MASK);
    printf("and: %lld\n", debug_temp[0]);
    debug_temp = _mm256_add_epi8(A_CHAR_MASK, ONE_CHAR_MASK);
    printf("add: %lld\n", debug_temp[0]);
#endif

    if (argc != 2) {
        printf("Usage: %s <BWT file path>\n", argv[0]);
    }

    const off_t bwt_file_size = open_input_file(&bwt_search, argv[1]);
#ifdef PERF
    clock_t t = clock();
#endif
    prepare_bwt_search(&bwt_search);
#ifdef PERF
    fprintf(stderr, "build_tables() took %f seconds to execute \n", (double)(clock() - t)/CLOCKS_PER_SEC);
#endif

#ifdef DEBUG
    fprintf(stderr, "RankEntry size %ld\n", sizeof(RankEntry));
    print_ctable(&bwt_search);
    // print_rank_table(&bwt_search);
    print_cumtable(&bwt_search);
#endif

#ifdef DEBUG
    fprintf(stderr, "BWT file size %lu\n", bwt_file_size);
#else
    (void)bwt_file_size;
#endif

    read_search_queries(&bwt_search);

#ifdef PERF
    fprintf(stderr, "Cache hits %lld\n", cache_hits);
    fprintf(stderr, "Cache misses %lld\n", cache_misses);
#endif

    return 0;
}
