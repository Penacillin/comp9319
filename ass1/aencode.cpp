#include <stdio.h>
#include <gmp.h>
#include <mpfr.h>
#include <string.h>

#include "utilities.hpp"

#define MAX_LENGTH 1024


int main(void)
{
    // read input
    char input[MAX_LENGTH + 1] = {0};
    fgets(input, MAX_LENGTH, stdin);

    int count_table[256] = {0};
    mpfr_t low_table[256];
    mpfr_t high_table[256];

    // count each char in input
    const size_t char_count = strlen(input);
    for (size_t i = 0; i < char_count; ++i)
    {
        count_table[(int)input[i]] += 1;
    }

    // Initialize count/range table
    initialize_low_high_table(count_table, char_count, low_table, high_table);

    for (int i = 0; i < 256; ++i)
    {
        if (count_table[i] > 0)
        {
#ifdef DEBUG
            mpfr_fprintf(stderr, "aencode: %c %d %.9Rf %.9Rf\n", i, count_table[i],
                low_table[i], high_table[i]);
#endif
            printf("%c %d\n", i, count_table[i]);
        }
    }

    // Create and Initialize algo counters/temps
    mpfr_t low, high, code_range, temp;
    mpfr_init2(low, AC_BITS);
    mpfr_init2(high, AC_BITS);
    mpfr_init2(code_range, AC_BITS);
    mpfr_init2(temp, AC_BITS);
    mpfr_set_d(low, 0, rnd);
    mpfr_set_d(high, 1.0, rnd);

    for (size_t i = 0; i < char_count; ++i)
    {
        size_t c = (size_t)input[i];
        mpfr_sub(code_range, high, low, rnd);

        // high = low + range*high_range(symbol)
        mpfr_mul(temp, code_range, high_table[c], rnd);
        mpfr_add(high, low, temp, rnd);

        // low = low + range*low_range(symbol)
        mpfr_mul(temp, code_range, low_table[c], rnd);
        mpfr_add(low, low, temp, rnd);
    }

    char output_buffer[1024] = {0};
    mpfr_exp_t expptr;
    mpfr_get_str(output_buffer, &expptr, 10, 0, low, rnd);
    mpfr_printf("0.%s\n", output_buffer);
    puts(output_buffer);
#ifdef DEBUG
    char output_buffer_high[1024] = {0};
    mpfr_get_str(output_buffer_high, &expptr, 10, 0, high, rnd);
    mpfr_fprintf(stderr, "aencode: 0.%s 0.%s %RNe\n", output_buffer, output_buffer_high, low);
#endif

    // Release memory
    mpfr_clear(low);
    mpfr_clear(high);

    return 0;
}