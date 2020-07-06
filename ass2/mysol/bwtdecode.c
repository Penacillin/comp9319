#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

typedef struct _rank_entry {
    char symbol;
    unsigned matching;
} RankEntry;

typedef struct _BWTDecode {
    RankEntry rankTable[1024];
    char in_buffer[1024];

    unsigned CTable[128];
    unsigned runCount[128];

    int bwt_file_fd;
} BWTDecode;

char LANGUAGE_INDEXES [5] = {'\n', 'A', 'C', 'G', 'T'};

static inline unsigned get_char_index(const char c) {
    switch(c)  {
        case '\n': return 0;
        case 'A': return 1;
        case 'C': return 2;
        case 'G': return 3;
        case 'T': return 4;
    };
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
        for (ssize_t i = 0; i < k; ++i) {
            // Add to all CTable above char
            const char c = decode_info->in_buffer[i];
            for (unsigned j = get_char_index(c) + 1; j < 5; ++j) {
                ++(decode_info->CTable[(unsigned)LANGUAGE_INDEXES[j]]);
            }
            decode_info->rankTable[curr_index] = (RankEntry){c, 0};
        }
        ++curr_index;
    }
}

void print_ctable(const BWTDecode *decode_info) {
    for (unsigned i = 0; i < 5; ++i) {
        printf("%c: %d\n", LANGUAGE_INDEXES[i], decode_info->CTable[(unsigned)LANGUAGE_INDEXES[i]]);
    }
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

    (void)out_fd;
    (void)file_size;
    (void)a2;

    build_tables(decode_info);
    print_ctable(decode_info);

    return 0;
}

BWTDecode bwtDecode;

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