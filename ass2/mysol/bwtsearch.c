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

#define LANGUAGE_SIZE 5
const unsigned LANGUAGE[] = {'\n', 'A', 'C', 'G', 'T', 'U'};
#define ENDING_CHAR '\n'
#define QUERY_MAX_SIZE 101

#define INPUT_BUF_SIZE 4096

#define MAX_INPUT_SIZE 104857600
#define SYMBOLS_PER_CHAR 4
#define BITS_PER_SYMBOL 2
#define PAGE_SIZE 128
#define PAGE_CACHE_SIZE 64

typedef struct _PageEntry {
    char symbol_array[PAGE_SIZE/SYMBOLS_PER_CHAR];
} PageEntry;

#define RUN_COUNT_LOWER_MASK 0x00FFFFFF
typedef union _RankEntry {
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
} RankEntry;


typedef struct _BWTSearch {
    u_int32_t rank_table_size;
    RankEntry rank_table[MAX_INPUT_SIZE/PAGE_SIZE+1];
    PageEntry symbol_pages[PAGE_CACHE_SIZE];

    u_int32_t ending_char_index;
    u_int32_t CTable[128];

    unsigned char cache_clock;
    u_int32_t cache_to_page[PAGE_CACHE_SIZE];
    unsigned char page_to_cache[MAX_INPUT_SIZE/PAGE_SIZE + 1];

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

void prepare_bwt_search(BWTSearch *search_info) {
    ssize_t k;
    unsigned curr_index = 0;
    unsigned rank_index = 0;
    unsigned page_index = 0;
    unsigned tempRunCount[128] = {0};
    char in_buffer[INPUT_BUF_SIZE];

#ifdef DEBUG
    memset(search_info->page_to_cache, 255, sizeof(search_info->page_to_cache));
#endif

    // First snapshot starting state
    search_info->rank_table[page_index].a_entry.val = 0;
    search_info->rank_table[page_index].b_entry.val |= 0;
    search_info->rank_table[page_index].c_entry.val |= 0;
    search_info->rank_table[page_index].d_entry.val |= 0 << 8;
    search_info->cache_to_page[search_info->cache_clock] = page_index;
    search_info->page_to_cache[page_index] = search_info->cache_clock;
    ++page_index;

    while((k = read(search_info->bwt_file_fd, in_buffer, INPUT_BUF_SIZE)) > 0) {
        for (ssize_t i = 0; i < k;) {
            // Clear out page ready to input symbols
            search_info->symbol_pages[search_info->cache_clock].symbol_array[rank_index] = 0;
            for (unsigned j = 0; j < SYMBOLS_PER_CHAR && i < k; ++j, ++i) {
                const char c = in_buffer[i];
                if (__glibc_unlikely(c == '\n')) search_info->ending_char_index = curr_index;
                // Add to all CTable above char
                const unsigned char_val = get_char_index(c);
                for (unsigned k = char_val + 1; k < LANGUAGE_SIZE + 1; ++k) {
                    ++(search_info->CTable[LANGUAGE[k]]);
                }
                // 00000001 00000010 00000011 00000010
                // 00000000 00000000 00000000 01101110
                // Put symbol into symbol page
                search_info->symbol_pages[search_info->cache_clock].symbol_array[rank_index] |=
                    ((char_val - 1) & 0b11) << (j*BITS_PER_SYMBOL);

                // Run count for rank table
                ++tempRunCount[(unsigned)c];

                ++curr_index;
                // Snapshot rank table run counts
                if (__glibc_unlikely(curr_index % PAGE_SIZE == 0)) {
                    // assert(page_index < PAGE_TABLE_SIZE);
                    search_info->rank_table[page_index].a_entry.val = tempRunCount[LANGUAGE[1]];
                    search_info->rank_table[page_index].b_entry.val |= tempRunCount[LANGUAGE[2]];
                    search_info->rank_table[page_index].c_entry.val |= tempRunCount[LANGUAGE[3]];
                    search_info->rank_table[page_index].d_entry.val |= tempRunCount[LANGUAGE[4]] << 8;
                    
                    search_info->cache_to_page[search_info->cache_clock] = page_index;
                    search_info->page_to_cache[page_index] = search_info->cache_clock;
                    ++page_index;
                    search_info->cache_clock = (search_info->cache_clock + 1) % PAGE_CACHE_SIZE;
                    search_info->page_to_cache[search_info->cache_to_page[search_info->cache_clock]] = 255;
                    rank_index = 0;
                    break;
                }
            }
            ++rank_index;
        }
    }
    search_info->rank_table[page_index].a_entry.val = tempRunCount[LANGUAGE[1]];
    search_info->rank_table[page_index].b_entry.val |= tempRunCount[LANGUAGE[2]];
    search_info->rank_table[page_index].c_entry.val |= tempRunCount[LANGUAGE[3]];
    search_info->rank_table[page_index].d_entry.val |= tempRunCount[LANGUAGE[4]] << 8;

    // search_info->CTable[LANGUAGE[LANGUAGE_SIZE]] = 

    search_info->rank_table_size = curr_index;
}

unsigned char get_cache_for_index(BWTSearch *search_info, unsigned index) {
    const unsigned page_index = index / PAGE_SIZE;
    if (search_info->page_to_cache[page_index] != 255)
        return search_info->page_to_cache[page_index];
    
#ifdef DEBUG
    fprintf(stderr, "index %u, page %u not in cache", index, page_index);
    exit(1);
#endif

    return 0;
}

unsigned get_occurence(BWTSearch *search_info, const char c, const size_t index) {
    const unsigned char cache_index = get_cache_for_index(search_info, index);

    const unsigned snapshot_page_index = index / PAGE_SIZE + ((index % PAGE_SIZE > PAGE_SIZE / 2) ? 1 : 0);
    const unsigned page_index = index / PAGE_SIZE;
    const int direction =  (index % PAGE_SIZE > PAGE_SIZE / 2) ? -1 : 1;
    unsigned tempRunCount[128];
    tempRunCount['\n'] = 0;
    // Load in char counts until this page
    tempRunCount[LANGUAGE[1]] = search_info->rank_table[snapshot_page_index].a_entry.val & RUN_COUNT_LOWER_MASK;
    tempRunCount[LANGUAGE[2]] = search_info->rank_table[snapshot_page_index].b_entry.val & RUN_COUNT_LOWER_MASK;
    tempRunCount[LANGUAGE[3]] = search_info->rank_table[snapshot_page_index].c_entry.val & RUN_COUNT_LOWER_MASK;
    tempRunCount[LANGUAGE[4]] = search_info->rank_table[snapshot_page_index].d_entry.val >> 8;
    // clock_t t = clock();
    // Fill out char counts for this page
    unsigned char_index = snapshot_page_index * PAGE_SIZE - (direction == 1 ? 0 : 1);
    int rank_entry_index = (char_index & 0b11);
    unsigned rank_index = (char_index - page_index * PAGE_SIZE) / SYMBOLS_PER_CHAR;
    unsigned out_char;
    if (__glibc_unlikely(direction == 1 &&
         char_index <= search_info->ending_char_index && search_info->ending_char_index <= index))
        --tempRunCount['T'];
    else if(__glibc_unlikely(index <= search_info->ending_char_index && search_info->ending_char_index <= char_index)) {
        ++tempRunCount['T'];
    }

#ifdef DEBUG
    fprintf(stderr, "index %ld, Using page %d. char_index %d, rank_index %d, rank_entry %d\n",
             index, page_index, char_index, rank_index, rank_entry_index);
#endif
    if (direction == 1) {
        out_char =
            LANGUAGE[((search_info->symbol_pages[cache_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11) + 1];
        ++tempRunCount[out_char];
        while(__glibc_likely(char_index < index)) {
            ++rank_entry_index;
            if (rank_entry_index == SYMBOLS_PER_CHAR) {
                rank_entry_index = 0;
                ++rank_index;
            }
            out_char =
                LANGUAGE[((search_info->symbol_pages[cache_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11) + 1];
            ++tempRunCount[out_char];
            ++char_index;
        };
    } else {
        out_char =
            LANGUAGE[((search_info->symbol_pages[cache_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11) + 1];
        --tempRunCount[out_char];
        while(__glibc_likely(char_index > index)) {
            --rank_entry_index;
            if (rank_entry_index == -1) {
                rank_entry_index = SYMBOLS_PER_CHAR - 1;
                --rank_index;
            }
            out_char =
                LANGUAGE[((search_info->symbol_pages[cache_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11) + 1];
            --tempRunCount[out_char];
            --char_index;
#ifdef DEBUG
            fprintf(stderr, "< %d %c %d\n", char_index, out_char, tempRunCount[out_char]);
#endif
        };
    }
    // reader_timer += ((double)clock() - t)/CLOCKS_PER_SEC;
#ifdef DEBUG
    printf("%c %d %d\n", out_char, tempRunCount[out_char], search_info->CTable[out_char]);
#endif
    return tempRunCount[(unsigned)c];
}

int process_search_query(BWTSearch *search_info,
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
    char query_buf[QUERY_MAX_SIZE + 1];
    ssize_t query_size;
    while((query_size = read(STDIN_FILENO, query_buf, QUERY_MAX_SIZE)) > 0) {
        const int num_matches = process_search_query(search_info, query_buf, query_size - 1);
        printf("%d\n", num_matches);
    }
}

void print_ctable(const BWTSearch *search_info) {
    for (unsigned i = 0; i < LANGUAGE_SIZE; ++i) {
        printf("%c: %d\n", LANGUAGE[i], search_info->CTable[(unsigned)LANGUAGE[i]]);
    }
}

void print_rank_table(const BWTSearch *search_info) {
    for (unsigned i = 0; i < search_info->rank_table_size; ++i) {
        const unsigned rank_entry_index = i % SYMBOLS_PER_CHAR;
        const unsigned page_index = i / PAGE_SIZE;
        const unsigned rank_index = (i - page_index * PAGE_SIZE) / SYMBOLS_PER_CHAR;
        fprintf(stderr, "%d(page=%d,rank=%d,entry=%d) >%c<\n", i, page_index, rank_index, rank_entry_index,
            LANGUAGE[((search_info->symbol_pages[page_index].symbol_array[rank_index] >> (rank_entry_index*BITS_PER_SYMBOL)) & 0b11)+1]);
    }
    printf("%d is endchar\n", search_info->ending_char_index);
}

void print_cumtable(const BWTSearch *search_info) {
    for (unsigned i = 0; i < 3; ++i) {
        fprintf(stderr, "%u,", search_info->rank_table[i].a_entry.val & RUN_COUNT_LOWER_MASK);
        fprintf(stderr, "%u,", search_info->rank_table[i].b_entry.val & RUN_COUNT_LOWER_MASK);
        fprintf(stderr, "%u,", search_info->rank_table[i].c_entry.val & RUN_COUNT_LOWER_MASK);
        fprintf(stderr, "%u,", search_info->rank_table[i].d_entry.val >> 8);
        fprintf(stderr, "\n");
    }
}

BWTSearch bwt_search = {.cache_clock = 0};

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <BWT file path>\n", argv[0]);
    }

    const off_t bwt_file_size = open_input_file(&bwt_search, argv[1]);
    prepare_bwt_search(&bwt_search);

#ifdef DEBUG
    print_ctable(&bwt_search);
    print_rank_table(&bwt_search);
    print_cumtable(&bwt_search);
#endif

#ifdef DEBUG
    fprintf(stderr, "BWT file size %lu\n", bwt_file_size);
#else
    (void)bwt_file_size;
#endif

    read_search_queries(&bwt_search);

    return 0;
}
