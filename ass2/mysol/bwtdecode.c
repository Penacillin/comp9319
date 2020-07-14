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

#define ENDING_CHAR '\n'
#define TABLE_SIZE 32
#define PAGE_TABLE_SIZE (15728640/TABLE_SIZE+1)
#define RANK_ENTRY_SIZE 4
#define RANK_ENTRY_MASK 0b11
#define BITS_PER_SYMBOL 2
#define RANK_TABLE_SIZE (15728640/RANK_ENTRY_SIZE) // 4 chars per 8 bits (2 bits per char)
#define INPUT_BUF_SIZE 4096
#define OUTPUT_BUF_SIZE 190000

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
typedef struct __attribute__((__packed__))  _RankEntry {
    union RunCountCumEntry snapshot;
    char symbol_array[TABLE_SIZE/RANK_ENTRY_SIZE];
} RankEntry;


const size_t RunCountCumEntrySize = sizeof(RankEntry);

typedef struct _BWTDecode {
    u_int32_t runCount[128]; // 512
    RankEntry rankTable[PAGE_TABLE_SIZE]; // 9830400
    u_int32_t endingCharIndex; // 4
    u_int32_t CTable[128]; // 512
    int bwt_file_fd; // 4

    u_int32_t rankTableSize; // 4
} BWTDecode;


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
static inline unsigned get_rank_entry_char_index(const char c) {
    switch(c)  {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        default: return 3;
    };

#ifdef DEBUG
    fprintf(stderr, "FATAL UNKOWN CHARACTER %d\n", c);
    exit(1);
#endif
}
// A      C      G     T
// 0      2      6     9
// 0000   0010   0110  1001 
// 0 1 2 3
// 0000   0001   0010  0011

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
            decode_info->rankTable[page_index].symbol_array[rank_index] = 0;
            for (unsigned j = 0; j < RANK_ENTRY_SIZE && i < k; ++j, ++i) {
                // Snapshot runCount
                if (curr_index % TABLE_SIZE == 0) {
                    assert(page_index < PAGE_TABLE_SIZE);
                    decode_info->rankTable[page_index].snapshot.a_entry.val = decode_info->runCount[LANGUAGE[1]];
                    decode_info->rankTable[page_index].snapshot.b_entry.val |= decode_info->runCount[LANGUAGE[2]];
                    decode_info->rankTable[page_index].snapshot.c_entry.val |= decode_info->runCount[LANGUAGE[3]];
                    decode_info->rankTable[page_index].snapshot.d_entry.val |= decode_info->runCount[LANGUAGE[4]] << 8;
                    ++page_index;
                    rank_index = 0;
                }

                const char c = in_buffer[i];
                if (c == '\n') decode_info->endingCharIndex = curr_index;
                // Add to all CTable above char
                for (unsigned k = get_char_index(c) + 1; k < LANGUAGE_SIZE; ++k) {
                    ++(decode_info->CTable[LANGUAGE[k]]);
                }
                // 00000001 00000010 00000011 00000010
                // 00000000 00000000 00000000 01101110
                // Put symbol into rank array
                decode_info->rankTable[page_index-1].symbol_array[rank_index] |=
                    get_rank_entry_char_index(c) << (j*BITS_PER_SYMBOL);

                // Run count for rank table
                ++decode_info->runCount[(unsigned)c];

                ++curr_index;
            }
            ++rank_index;
        }
    }
    decode_info->rankTable[page_index].snapshot.a_entry.val = decode_info->runCount[LANGUAGE[1]];
    decode_info->rankTable[page_index].snapshot.b_entry.val |= decode_info->runCount[LANGUAGE[2]];
    decode_info->rankTable[page_index].snapshot.c_entry.val |= decode_info->runCount[LANGUAGE[3]];
    decode_info->rankTable[page_index].snapshot.d_entry.val |= decode_info->runCount[LANGUAGE[4]] << 8;

    decode_info->rankTableSize = curr_index;
}

void print_ctable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < 5; ++i) {
        printf("%c: %d\n", LANGUAGE[i], decode_info->CTable[(unsigned)LANGUAGE[i]]);
    }
}

void print_ranktable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < decode_info->rankTableSize; ++i) {
        const unsigned rank_entry_index = i % RANK_ENTRY_SIZE;
        const unsigned page_index = i / TABLE_SIZE;
        const unsigned rank_index = (i - page_index * TABLE_SIZE) / RANK_ENTRY_SIZE;
        fprintf(stderr, "%d(page=%d,rank=%d,entry=%d) >%c<\n", i, page_index, rank_index, rank_entry_index,
            LANGUAGE[((decode_info->rankTable[page_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)+1]);
    }
    printf("%d is endchar\n", decode_info->endingCharIndex);
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

char get_char_rank(const unsigned index, BWTDecode *decode_info, unsigned *next_index) {
    const unsigned snapshot_page_index = index / TABLE_SIZE + ((index % TABLE_SIZE > TABLE_SIZE / 2) ? 1 : 0);
    const unsigned page_index = index / TABLE_SIZE;
    int direction = 1;
    if (index % TABLE_SIZE > TABLE_SIZE / 2) {
        direction = -1;
    }
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
    if (direction == 1 &&
         char_index <= decode_info->endingCharIndex && decode_info->endingCharIndex <= index)
        --tempRunCount['T'];
    else if(index <= decode_info->endingCharIndex && decode_info->endingCharIndex <= char_index) {
        ++tempRunCount['T'];
    }

#ifdef DEBUG
    fprintf(stderr, "Using page %d. char_index %d, rank_index %d, rank_entry %d\n",
             page_index, char_index, rank_index, rank_entry_index);
#endif
    if (direction == 1) {
        out_char =
            LANGUAGE[((decode_info->rankTable[page_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11) + 1];
        ++tempRunCount[out_char];
        while(char_index < index) {
            ++rank_entry_index;
            if (rank_entry_index == RANK_ENTRY_SIZE) {
                rank_entry_index = 0;
                ++rank_index;
            }
            out_char =
                LANGUAGE[((decode_info->rankTable[page_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11) + 1];
            ++tempRunCount[out_char];
            ++char_index;
        };
        --tempRunCount[out_char];
    } else {
        out_char =
            LANGUAGE[((decode_info->rankTable[page_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11) + 1];
        --tempRunCount[out_char];
        while(char_index > index) {
            --rank_entry_index;
            if (rank_entry_index == -1) {
                rank_entry_index = RANK_ENTRY_SIZE - 1;
                --rank_index;
            }
            out_char =
                LANGUAGE[((decode_info->rankTable[page_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11) + 1];
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
    printf("%c %d %d\n", out_char, tempRunCount[out_char], decode_info->CTable[out_char]);
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

    clock_t t = clock();
    build_tables(decode_info);
    t = clock() - t;
    printf("build_tables() took %f seconds to execute \n", ((double)t)/CLOCKS_PER_SEC);

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

BWTDecode bwtDecode = {.runCount = {0}, .CTable = {0}};

int main(int argc, char** argv) {
    if (argc != 3) {
        printf("Usage: %s <BWT file path> <Reversed file path>\n", argv[0]);
        exit(1);
    }
    fprintf(stderr, "BWTDecode size %ld RunCountCumEntrySize: %ld\n", sizeof(struct _BWTDecode), RunCountCumEntrySize);
    off_t file_size = open_input_file(&bwtDecode, argv[1]);
#ifdef DEBUG
    fprintf(stderr, "BWT file file_size: %ld\n", file_size);
#endif
    const int out_fd = setup_BWT(&bwtDecode, argv[2]);
    do_stuff2(&bwtDecode, file_size, out_fd);

    close(bwtDecode.bwt_file_fd);

    printf("reader timer %lf\n", reader_timer);
    printf("busy waits   %lu\n", busy_waits);

    return 0;
}
