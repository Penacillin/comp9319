#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include <mpfr.h>
#include <string.h>

#define MAX_LENGTH 2048

int main(void)
{
    // This mode specifies round-to-nearest
    mpfr_rnd_t rnd = MPFR_RNDN;

    int count_table[256] = {0};
    mpfr_t low_table[256];
    mpfr_t high_table[256];

    for (int i = 0; i < 256; ++i) {
        mpfr_init2(low_table[i], 256);
        mpfr_init2(high_table[i], 256);
    }

    char buffer[MAX_LENGTH];
    char *buffer_end;
    // count each char in input
    while (fgets(buffer, MAX_LENGTH, stdin) != NULL)
    {
        // puts(buffer);
        count_table[(size_t)buffer[0]] = strtol(buffer + 2, &buffer_end, 10);
        mpfr_inp_str(low_table[(size_t)buffer[0]], buffer_end, 10, rnd);
    }

    for (int i = 0; i < 256; ++i) {
        if (count_table[i] > 0) {
            mpfr_printf("%c %d %.6Rf\n", i, count_table[i], low_table[i]);
        }
    }

    return 0;
}
