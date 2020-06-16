#include <stdio.h>
#include <gmp.h>
#include <mpfr.h>
#include <string.h>

#define MAX_LENGTH 1024

int main(void)
{
    // This mode specifies round-to-nearest
    mpfr_rnd_t rnd = MPFR_RNDN;

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
    mpfr_t range_counter, temp;
    mpfr_init2(range_counter, 256);
    mpfr_init2(temp, 256);
    mpfr_set_d(range_counter, 0.0, rnd);
    mpfr_set_d(temp, 0.0, rnd);
    for (int i = 0; i < 256; ++i)
    {
        mpfr_init2(low_table[i], 256);
        mpfr_init2(high_table[i], 256);
        if (count_table[i] > 0)
        {
            mpfr_set(low_table[i], range_counter, rnd);
            mpfr_add_d(range_counter, range_counter, (double)count_table[i] / char_count, rnd);
            mpfr_set(high_table[i], range_counter, rnd);
        }
    }

    for (int i = 0; i < 256; ++i)
    {
        if (count_table[i] > 0)
        {
#ifdef DEBUG
            mpfr_printf("%c %d %.9Rf %.9Rf\n", i, count_table[i], low_table[i], high_table[i]);
#else
            printf("%c %d\n", i, count_table[i]);
#endif
        }
    }

    // Create and Initialize algo counters/temps
    mpfr_t low, high, code_range;
    mpfr_init2(low, 256);
    mpfr_init2(high, 256);
    mpfr_init2(code_range, 256);
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
    mpfr_printf("0.%s\n", output_buffer, expptr, low, high);

    // Release memory
    mpfr_clear(low);
    mpfr_clear(high);

    return 0;
}
