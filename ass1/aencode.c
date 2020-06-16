#include <stdio.h>
#include <gmp.h>
#include <mpfr.h>
#include <string.h>
#include <string>

int main(void)
{
    // This mode specifies round-to-nearest
    mpfr_rnd_t rnd = MPFR_RNDN;

    mpfr_t p, t;

    // allocate unitialized memory for p as 256-bit numbers
    mpfr_init2(p, 256);
    mpfr_init2(t, 256);

    mpfr_set_d(p, 355, rnd);
    mpfr_set_d(t, 113, rnd);

    // a good approx of PI = 355/113
    mpfr_div(p, p, t, rnd);

    // Print Pi to standard out in base 10
    // printf("pi = ");
    // mpfr_out_str(stdout, 10, 0, p, rnd);
    // putchar('\n');

    size_t BUFFER_SIZE = 1024;
    char input[2015] = {0};

    fgets(input, BUFFER_SIZE, stdin);

    int count_table[256] = {0};
    mpfr_t low_table[256];
    mpfr_t high_table[256];

    const size_t char_count = strlen(input);
    for (int i = 0; i < char_count; ++i)
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
        if (count_table[i] > 0) {
            // printf("%c %d\n", i, count_table[i]);
            mpfr_set(low_table[i], range_counter, rnd);
            // mpfr_div(temp, )
            mpfr_add_d(range_counter, range_counter, (double)count_table[i]/char_count, rnd);
            mpfr_set(high_table[i], range_counter, rnd);
        }
    }

    for (int i = 0; i < 256; ++i) {
        if (count_table[i] > 0) {
            mpfr_printf("%c %d %.6Rf %.6Rf\n", i, count_table[i], low_table[i], high_table[i]);
        }
    }

    // Create and Initialize algo counters/temps
    mpfr_t low, high, code_range;
    mpfr_init2(low, 256);
    mpfr_init2(high, 256);
    mpfr_init2(code_range, 256);
    mpfr_set_d(low, 0, rnd);
    mpfr_set_d(high, 1.0, rnd);
    mpfr_set_d(code_range, 0, rnd);

    for (int i = 0; i < char_count; ++i)
    {
        char c = input[i];
        // mpfr_sub(code_range, )
    }

    // Release memory
    mpfr_clear(p);
    mpfr_clear(t);

    return 0;
}
