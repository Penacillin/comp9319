// You can write your code in C or C++. Modify this file and the makefile
// accordingly. Please test your program on CSE linux machines before submitting
// your solution.
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h> 

typedef struct suff_element {
    int index;
    char *suff;
} SuffElement;
SuffElement suff_arr[32];
int sorted_suf_ix[32];


int strcmp_wrapper(const void *a, const void *b) {
    return strcmp(((SuffElement*)a)->suff, ((SuffElement*)b)->suff);
}

int main(int argc, const char *argv[])
{
    printf("Please modify the source of this program.\n");

    size_t input_len = strlen(argv[0]);
    const char *input = argv[0];
    for (int i = 0; i < input_len; ++i) {
        suff_arr[i] = { i, (char *)(input + i)};
    }

    qsort(suff_arr, input_len, sizeof(suff_arr), strcmp_wrapper);

    for (int i = 0; i < input_len; ++i) {
        sorted_suf_ix[i] = i;
    }

    char bwt_str[32] = {0};
    for (int i = 0; i < input_len; ++i) {
        const int input_ix = sorted_suf_ix[i] - 1 < 0 ? input_len - 1 : sorted_suf_ix[i] - 1;
        bwt_str[i] = input[input_ix];
    }


    printf("%s\n", bwt_str);

    return 0;
}
