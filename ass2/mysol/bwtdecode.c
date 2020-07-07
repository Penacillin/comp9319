#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>

#define ENDING_CHAR '\n'
#define TABLE_SIZE 4096
#define PAGE_TABLE_SIZE 100000000/TABLE_SIZE


typedef struct _rank_entry {
    char symbol;
    unsigned matching;
} RankEntry;


typedef struct _BWTDecode {
    u_int32_t runCount[128];
    u_int32_t runCountCum[PAGE_TABLE_SIZE][4];
    RankEntry rankTable[TABLE_SIZE];
    u_int32_t rankTableStart;
    u_int32_t rankTableEnd;
    u_int32_t rankTableSize;

    char in_buffer[TABLE_SIZE];

    u_int32_t CTable[128];

    int bwt_file_fd;
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
#endif
}

// A C G T
// 0 2 6 9
// 0 1 2 3

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
    while((k = read(decode_info->bwt_file_fd, decode_info->in_buffer, TABLE_SIZE)) > 0) {
        decode_info->rankTableStart = curr_index;
        assert(page_index < PAGE_TABLE_SIZE);
        for (unsigned i = 0; i < 4; ++i)
            decode_info->runCountCum[page_index][i] = decode_info->runCount[LANGUAGE[i]];
        ++page_index;
        for (ssize_t i = 0; i < k; ++i) {
            // Add to all CTable above char
            const char c = decode_info->in_buffer[i];
            for (unsigned j = get_char_index(c) + 1; j < 5; ++j) {
                ++(decode_info->CTable[(unsigned)LANGUAGE[j]]);
            }
            decode_info->rankTable[i] = (RankEntry){c, decode_info->runCount[(unsigned)c]++};
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
            i, decode_info->rankTable[i].symbol, decode_info->rankTable[i].matching);
    }
}

void ensure_in_rank_table(BWTDecode *decode_info, const unsigned index) {
    if (decode_info->rankTableStart <= index && decode_info->rankTableEnd >= index) return;

    const unsigned desired_page_index = index / TABLE_SIZE;
#ifdef DEBUG
    fprintf(stderr, "Moving page to %d\n", desired_page_index);
#endif
    decode_info->rankTableStart =  desired_page_index * TABLE_SIZE;
    lseek(decode_info->bwt_file_fd, decode_info->rankTableStart, SEEK_SET);
    const ssize_t read_bytes = read(decode_info->bwt_file_fd, decode_info->in_buffer, TABLE_SIZE);
    decode_info->rankTableEnd = decode_info->rankTableStart + read_bytes;
    decode_info->rankTableSize = read_bytes;

    unsigned tempRunCount[128] = {0};
    // Load in char counts until this page
    for (unsigned i = 0; i < 4; ++i)
        tempRunCount[LANGUAGE[i]] = decode_info->runCountCum[desired_page_index][i];
    // Fill out char counts for this page
    for (unsigned i = 0; i < read_bytes; ++i) {
        const char c = decode_info->in_buffer[i];
        decode_info->rankTable[i] = (RankEntry){c,  tempRunCount[(unsigned)c]++};
    }

    assert((decode_info->rankTableStart <= index && decode_info->rankTableEnd >= index));
    return;
}

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

    build_tables(decode_info);
    print_ctable(decode_info);
    print_ranktable(decode_info);

    unsigned index = decode_info->CTable[ENDING_CHAR];
    printf("starting index %d\n", index);

    char output_buffer[TABLE_SIZE];
    unsigned output_buffer_index = sizeof(output_buffer);
    output_buffer[--output_buffer_index] = ENDING_CHAR;
    --file_size;

    while (file_size > 0) {
        ensure_in_rank_table(decode_info, index);
        const char out_char = decode_info->rankTable[index % TABLE_SIZE].symbol;
        output_buffer[--output_buffer_index] = out_char;
        --file_size;

        if (output_buffer_index == 0) {
            lseek(out_fd, file_size, SEEK_SET);
            printf("File Size %ld, index %d\n", file_size, index);
            ssize_t res = write(out_fd, output_buffer, sizeof(output_buffer));
            if (res != sizeof(output_buffer)) exit(1);
            output_buffer_index = sizeof(output_buffer);
        }

        index = decode_info->rankTable[index % TABLE_SIZE].matching + decode_info->CTable[(unsigned)out_char];
        fprintf(stderr, "index: %d\n", index);
    }

    lseek(out_fd, file_size, SEEK_SET);
    ssize_t res = write(
        out_fd,
        output_buffer + output_buffer_index,
        sizeof(output_buffer) - output_buffer_index);
    if ((unsigned)res != sizeof(output_buffer) - output_buffer_index) {
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

    off_t file_size = read_file(&bwtDecode, argv[1]);
#ifdef DEBUG
    fprintf(stderr, "BWT file file_size: %ld\n", file_size);
#endif
    (void)file_size;

    do_stuff2(&bwtDecode, file_size, argv[2], 0);


    return 0;
}