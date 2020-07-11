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
#define TABLE_SIZE 32
#define PAGE_TABLE_SIZE (15728640/TABLE_SIZE+1)
#define RANK_ENTRY_SIZE 4
#define BITS_PER_SYMBOL 2
#define RANK_TABLE_SIZE (15728640/RANK_ENTRY_SIZE) // 4 chars per 8 bits (2 bits per char)
#define INPUT_BUF_SIZE 4096
#define OUTPUT_BUF_SIZE 999999

#define FALSE 0
#define TRUE 1

// Use 2 bits per symbol. 4 bits per char/RankEntry
typedef char RankEntry;


typedef struct _BWTDecode {
    u_int32_t runCount[128]; // 512
    u_int32_t runCountCum[PAGE_TABLE_SIZE][4]; // 245776 // using 4*32=128 bits per snapshot. Actually needs 96 bits
    RankEntry rankTable[RANK_TABLE_SIZE]; // 3932160
    u_int32_t rankTableSize;
    u_int32_t CTable[128]; // 512

    int bwt_file_fd; // 4
} BWTDecode;

const size_t BWTDECODE_SIZE = sizeof(struct _BWTDecode);

#define LANGUAGE_SIZE 5
const unsigned LANGUAGE[LANGUAGE_SIZE] = {'\n', 'A', 'C', 'G', 'T'};

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
    unsigned rank_index = 0;
    unsigned page_index = 0;
    char in_buffer[INPUT_BUF_SIZE];
    while((k = read(decode_info->bwt_file_fd, in_buffer, INPUT_BUF_SIZE)) > 0) {
        for (ssize_t i = 0; i < k;) {
            // Clear out page ready to input symbols
            decode_info->rankTable[rank_index] = 0;
            for (unsigned j = 0; j < RANK_ENTRY_SIZE && i < k; ++j, ++i) {
                // Snapshot runCount
                if (curr_index % TABLE_SIZE == 0) {
                    for (unsigned k = 0; k < 4; ++k)
                        decode_info->runCountCum[page_index][k] = decode_info->runCount[LANGUAGE[k+1]];
                    ++page_index;
                    assert(page_index < PAGE_TABLE_SIZE);
                }

                const char c = in_buffer[i];
                // Add to all CTable above char
                for (unsigned k = get_char_index(c) + 1; k < LANGUAGE_SIZE; ++k) {
                    ++(decode_info->CTable[LANGUAGE[k]]);
                }

                // Put symbol into rank array
                decode_info->rankTable[rank_index] |=
                    ((get_char_index(c)-1) & 0b11) << (j*BITS_PER_SYMBOL);

                ++curr_index;
            }
            ++rank_index;
        }
    }
    decode_info->rankTableSize = curr_index;
}

void print_ctable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < 5; ++i) {
        printf("%c: %d\n", LANGUAGE[i], decode_info->CTable[(unsigned)LANGUAGE[i]]);
    }
}

void print_ranktable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < decode_info->rankTableSize; ++i) {
        const unsigned rank_index = i / RANK_ENTRY_SIZE;
        const unsigned rank_entry_index = i % RANK_ENTRY_SIZE;
        fprintf(stderr, "%d >%c<\n",
            i, LANGUAGE[((decode_info->rankTable[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)+1]);
    }
}

double reader_timer = 0;

char get_char_rank(const unsigned index, BWTDecode *decode_info, unsigned *next_index) {
    const unsigned page_index = index / TABLE_SIZE;
    unsigned tempRunCount[128];
    tempRunCount['\n'] = 0;
    // Load in char counts until this page
    for (unsigned i = 0; i < 4; ++i)
        tempRunCount[LANGUAGE[i+1]] = decode_info->runCountCum[page_index][i];
    clock_t t = clock();
    // Fill out char counts for this page
    unsigned char_index = page_index * TABLE_SIZE;
    unsigned rank_entry_index = char_index % RANK_ENTRY_SIZE;
    unsigned rank_index = char_index / RANK_ENTRY_SIZE;
    unsigned out_char;
#ifdef DEBUG
    fprintf(stderr, "Using page %d. rank_index %d\n", page_index, rank_index);
#endif
    for (; char_index <= index; ++char_index) {
        out_char =
            LANGUAGE[((decode_info->rankTable[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11) + 1];
        ++rank_entry_index;
        if (rank_entry_index == RANK_ENTRY_SIZE) {
            rank_entry_index = 0;
            ++rank_index;
        }
        ++tempRunCount[out_char];
    }reader_timer
     += ((double)clock() - t)/CLOCKS_PER_SEC;
    *next_index = tempRunCount[out_char] + decode_info->CTable[out_char];
    return (char)out_char;
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
        unsigned next_index;
        const char out_char = get_char_rank(index, decode_info, &next_index);
        index = next_index;
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

BWTDecode bwtDecode = {.runCount = {0}, .CTable = {0}};

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

    return 0;
}
