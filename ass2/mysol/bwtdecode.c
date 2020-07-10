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

#define ENDING_CHAR '\n'
#define TABLE_SIZE 1024
#define PAGE_TABLE_SIZE (15728640/TABLE_SIZE+1)
#define CACHE_SIZE (2621440/TABLE_SIZE)
#define OUTPUT_BUF_SIZE 999999

#define FALSE 0
#define TRUE 1

// Use top 3 bits for symbol, rest for 29 bit unsigned integer
#define SYMBOL_MASK   0xE0000000
#define MATCHING_MASK 0x1FFFFFFF
typedef u_int32_t RankEntry;


typedef struct _BWTDecode {
    u_int32_t runCount[128]; // 512
    u_int32_t runCountCum[PAGE_TABLE_SIZE][4]; // 245776
    RankEntry rankTable[CACHE_SIZE][TABLE_SIZE]; // 10485760
    int16_t cache_clock; // 2
    int16_t cache_table[PAGE_TABLE_SIZE]; // 30772
    u_int16_t cache_to_page[CACHE_SIZE]; // 2624
    u_int32_t rankTableStart; // 4
    u_int32_t rankTableEnd; // 4
    u_int32_t rankTableSize; // 4


    u_int32_t CTable[128]; // 512

    int bwt_file_fd; // 4
} BWTDecode;

const size_t BWTDECODE_SIZE = sizeof(struct _BWTDecode);

const unsigned LANGUAGE[5] = {'\n', 'A', 'C', 'G', 'T'};

static inline unsigned get_char_index(const char c) {
    switch(c)  {
        case 'A': return 1;
        case 'C': return 2;
        case 'G': return 3;
        case 'T': return 4;
        case '\n': return 0;
    };
#ifdef DEBUG
    fprintf(stderr, "FATAL UNKOWN CHARACTER %d\n", c);
    exit(1);
#else
    return 0;
#endif
}
// A      C      G     T
// 0      2      6     9
// 0000   0010   0110  1001 
// 0 1 2 3
// 0000   0001   0010  0011

off_t read_file(BWTDecode* decode_info, char *bwt_file_path) {
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

// '\n'ACGT
void build_tables(BWTDecode *decode_info) {
    ssize_t k;
    unsigned curr_index = 0;
    unsigned page_index = 0;
    char replacing_caches = FALSE;
    char in_buffer[TABLE_SIZE];
    while((k = read(decode_info->bwt_file_fd, in_buffer, TABLE_SIZE)) > 0) {
        assert(page_index < PAGE_TABLE_SIZE);
        // Snapshot runCount
        for (unsigned i = 0; i < 4; ++i)
            decode_info->runCountCum[page_index][i] = decode_info->runCount[LANGUAGE[i+1]];

        decode_info->cache_clock = decode_info->cache_clock + 1;
        if (decode_info->cache_clock == CACHE_SIZE) {
            decode_info->cache_clock = 0;
            replacing_caches = TRUE;
        }
        decode_info->cache_table[page_index] = decode_info->cache_clock; // page_index about to get cache
        if (replacing_caches)
            decode_info->cache_table[decode_info->cache_to_page[decode_info->cache_clock]] = -1; // update previous page that it's out of cache
        decode_info->cache_to_page[decode_info->cache_clock] = page_index;
        ++page_index;

        decode_info->rankTableStart = curr_index;
        for (ssize_t i = 0; i < k; ++i) {
            // Add to all CTable above char
            const char c = in_buffer[i];
            for (unsigned j = get_char_index(c) + 1; j < 5; ++j) {
                ++(decode_info->CTable[(unsigned)LANGUAGE[j]]);
            }
            decode_info->rankTable[decode_info->cache_clock][i] =
                (get_char_index(c) << 29) | decode_info->runCount[(unsigned)c]++;
            ++curr_index;
        }
        decode_info->rankTableEnd = curr_index;
        decode_info->rankTableSize = k;
    }
}

void print_ctable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < 5; ++i) {
        printf("%c: %d\n", LANGUAGE[i], decode_info->CTable[(unsigned)LANGUAGE[i]]);
    }
}

void print_ranktable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < decode_info->rankTableSize; ++i) {
        fprintf(stderr, "%d %c %d\n",
            i, (decode_info->rankTable[0][i] & SYMBOL_MASK) >> 29, decode_info->rankTable[0][i] & MATCHING_MASK);
    }
}

double reader_timer = 0;
unsigned cache_misses = 0;

u_int32_t ensure_in_rank_table(BWTDecode *decode_info, const unsigned index) {
    const unsigned desired_page_index = index / TABLE_SIZE;
    if (decode_info->cache_table[desired_page_index] != -1) {
        return decode_info->cache_table[desired_page_index];
    }
    ++cache_misses;
#ifdef DEBUG
    fprintf(stderr, "Moving page to %d\n", desired_page_index);
#endif
    decode_info->rankTableStart =  desired_page_index * TABLE_SIZE;
    off_t seek_res = lseek(decode_info->bwt_file_fd, decode_info->rankTableStart, SEEK_SET);
    if (__glibc_unlikely(seek_res == -1)) exit(1);
    char in_buffer[TABLE_SIZE];
    const ssize_t read_bytes = read(decode_info->bwt_file_fd, in_buffer, TABLE_SIZE);
    decode_info->rankTableEnd = decode_info->rankTableStart + read_bytes;
    decode_info->rankTableSize = read_bytes;

    // Pick page to be replaced
    decode_info->cache_clock = (decode_info->cache_clock + 1) % CACHE_SIZE;

    // Replace page
    decode_info->cache_table[desired_page_index] = decode_info->cache_clock; // page_index about to get cache
    decode_info->cache_table[decode_info->cache_to_page[decode_info->cache_clock]] = -1; // update previous page that it's out of cache
    decode_info->cache_to_page[decode_info->cache_clock] = desired_page_index;

    clock_t t = clock();
    unsigned tempRunCount[128];
    tempRunCount['\n'] = 0;
    // Load in char counts until this page
    for (unsigned i = 0; i < 4; ++i)
        tempRunCount[LANGUAGE[i+1]] = decode_info->runCountCum[desired_page_index][i];
    // Fill out char counts for this page
    for (unsigned i = 0; i < read_bytes; ++i) {
        const char c = in_buffer[i];
        unsigned j = 0;
        switch(c)  {
            case 'A': j = 1; goto LABEL_BREAK_SWITCH;
            case 'C': j = 2; goto LABEL_BREAK_SWITCH;
            case 'G': j = 3; goto LABEL_BREAK_SWITCH;
            case 'T': j = 4; goto LABEL_BREAK_SWITCH;
        };
LABEL_BREAK_SWITCH:
        decode_info->rankTable[decode_info->cache_clock][i] = (j << 29) | tempRunCount[(unsigned)c]++;
    }
    reader_timer += ((double)clock() - t)/CLOCKS_PER_SEC;

#ifdef DEBUG
    assert((decode_info->rankTableStart <= index && index < decode_info->rankTableEnd));
    // printf("cache miss (page %d) took %f seconds to execute \n",
    //     desired_page_index, ((double)t)/CLOCKS_PER_SEC);
#endif

    return decode_info->cache_clock;
}

// The main BWTDecode running algorithm
int do_stuff2(BWTDecode *decode_info,
              off_t file_size,
              char* output_file_path,
              unsigned int a2) {
    int out_fd = open(output_file_path, O_CREAT | O_WRONLY, 0666);
    if (out_fd < 0) {
        char *err = strerror(errno);
        printf("Could not create the reversed file %s - %s\n", output_file_path, err);
        exit(1);
    }

    (void)a2;

    clock_t t = clock();
    build_tables(decode_info);
    t = clock() - t;
    printf("build_tables() took %f seconds to execute \n", ((double)t)/CLOCKS_PER_SEC);

#ifdef DEBUG
    print_ctable(decode_info);
    print_ranktable(decode_info);
#endif

    unsigned index = decode_info->CTable[ENDING_CHAR];

    char output_buffer[OUTPUT_BUF_SIZE];
    unsigned output_buffer_index = sizeof(output_buffer);
    output_buffer[--output_buffer_index] = ENDING_CHAR;
    --file_size;

    while (file_size > 0) {
        const u_int32_t page_index = ensure_in_rank_table(decode_info, index);
        const char out_char = LANGUAGE[((decode_info->rankTable[page_index][index % TABLE_SIZE] & SYMBOL_MASK) >> 29)];
        output_buffer[--output_buffer_index] = out_char;
        --file_size;

        if (output_buffer_index == 0) {
            lseek(out_fd, file_size, SEEK_SET);
#ifdef DEBUG
            printf("File Size %ld, index %d\n", file_size, index);
#endif
            ssize_t res = write(out_fd, output_buffer, sizeof(output_buffer));
            if (res != sizeof(output_buffer)) exit(1);
            output_buffer_index = sizeof(output_buffer);
        }

        index =
            (decode_info->rankTable[page_index][index % TABLE_SIZE] & MATCHING_MASK) +
            decode_info->CTable[(unsigned)out_char];
#ifdef DEBUG
        fprintf(stderr, "index: %d\n", index);
#endif
    }

    lseek(out_fd, file_size, SEEK_SET);
    ssize_t res = write(
        out_fd,
        output_buffer + output_buffer_index,
        sizeof(output_buffer) - output_buffer_index);
    if (res == -1 || (unsigned)res != sizeof(output_buffer) - output_buffer_index) {
        fprintf(stderr, "Error writing to file. Wrote %ld instead of %d\n", res, TABLE_SIZE);
        exit(1);  
    }

    close(out_fd);

    return 0;
}

BWTDecode bwtDecode = {.runCount = {0}, .CTable = {0}, .cache_clock = 0};

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage: %s <BWT file path> <Reversed file path>\n", argv[0]);
        exit(1);
    }
    fprintf(stderr, "BWTDecode size %ld\n", BWTDECODE_SIZE);
    off_t file_size = read_file(&bwtDecode, argv[1]);
#ifdef DEBUG
    fprintf(stderr, "BWT file file_size: %ld\n", file_size);
#endif
    (void)file_size;

    do_stuff2(&bwtDecode, file_size, argv[2], 0);

    close(bwtDecode.bwt_file_fd);

    printf("reader timer %lf\n", reader_timer);
    printf("cache misses %d\n", cache_misses);

    return 0;
}
