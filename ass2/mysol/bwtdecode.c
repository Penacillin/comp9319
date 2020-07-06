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

typedef struct _rank_entry {
    char symbol;
    unsigned matching;
} RankEntry;

typedef struct _BWTDecode {
    RankEntry rankTable[1024];
    unsigned rankTableStart;
    unsigned rankTableEnd;
    unsigned rankTableSize;
    char in_buffer[1024];

    unsigned endingCharRankIndex;

    unsigned CTable[128];
    unsigned runCount[128];

    int bwt_file_fd;
} BWTDecode;

const char LANGUAGE[5] = {'\n', 'A', 'C', 'G', 'T'};

static inline unsigned get_char_index(const char c) {
    switch(c)  {
        case '\n': return 0;
        case 'A': return 1;
        case 'C': return 2;
        case 'G': return 3;
        case 'T': return 4;
    };
    fprintf(stderr, "nani tf >%d<\n", c);
    exit(1);
}

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
    while((k = read(decode_info->bwt_file_fd, decode_info->in_buffer, 1024)) > 0) {
        decode_info->rankTableStart = curr_index;
        for (ssize_t i = 0; i < k; ++i) {
            // Add to all CTable above char
            const char c = decode_info->in_buffer[i];
            if (c == 0) {
                fprintf(stderr, "unlucky %ld %ld %d\n", k, i, curr_index);
            }
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
    assert((decode_info->rankTableStart <= index && decode_info->rankTableEnd >= index));
    return;
}

int do_stuff2(BWTDecode *decode_info,
              off_t file_size,
              char* output_file_path,
              unsigned int a2) {
    int out_fd = open(output_file_path, O_CREAT | O_WRONLY);
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

    char output_buffer[1024];
    unsigned output_buffer_index = sizeof(output_buffer) - 1;
    output_buffer[output_buffer_index--] = ENDING_CHAR;
    --file_size;

    while (file_size > 0) {
        ensure_in_rank_table(decode_info, index);
        const char out_char = decode_info->rankTable[index].symbol;
        output_buffer[output_buffer_index--] = out_char;

        if (output_buffer_index == 0) {
            lseek(out_fd, file_size, SEEK_SET);
            write(out_fd, output_buffer, 1024);
            output_buffer_index = sizeof(output_buffer) - 1;
        }

        index = decode_info->rankTable[index].matching + decode_info->CTable[(unsigned)out_char];

        --file_size;
    }

    lseek(out_fd, file_size, SEEK_SET);
    write(out_fd, output_buffer + output_buffer_index, sizeof(output_buffer) - output_buffer_index);

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