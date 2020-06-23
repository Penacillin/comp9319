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
    const size_t char_count = fread(input, sizeof(char), MAX_LENGTH, stdin);
    // fprintf(stderr, "char_count: %lu\n", char_count);
    int count_table[256] = {0};
    mpfr_t low_table[256];
    mpfr_t high_table[256];

    // count each char in input
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
        char c = input[i];
        int res = mpfr_sub(code_range, high, low, rnd);
        if (res != 0) mpfr_fprintf(stderr, "aencode [WARNING]: high-low inexact\n");

        // high = low + range*high_range(symbol)
        mpfr_mul(temp, code_range, high_table[(size_t)c], rnd);
        mpfr_add(high, low, temp, rnd);

        // low = low + range*low_range(symbol)
        mpfr_mul(temp, code_range, low_table[(size_t)c], rnd);
        mpfr_add(low, low, temp, rnd);
    }

    char output_buffer_low[2048] = {0};
    char output_buffer_high[2048] = {0};
    char output_buffer_real_low[2048] = {0};
    char output_buffer_real_high[2048] = {0};
    size_t output_exp_offset = 0;
    mpfr_exp_t expptr;
    mpfr_get_str(output_buffer_low, &expptr, 10, 0, low, rnd);
    mpfr_get_str(output_buffer_high, &expptr, 10, 0, high, rnd);

    output_exp_offset = (size_t)-1*expptr;
    for (size_t i = 0; i < output_exp_offset; ++i) {
        output_buffer_real_low[i] = '0';
        output_buffer_real_high[i] = '0';
    }

    size_t output_length = 0;
    bool low_high_has_diff = false;
    for (; output_length < strlen(output_buffer_low); ++output_length) {
        output_buffer_real_low[output_length+output_exp_offset] = output_buffer_low[output_length];
        output_buffer_real_high[output_length+output_exp_offset] = output_buffer_high[output_length];
        if (!low_high_has_diff) {
            if (output_buffer_low[output_length] < output_buffer_high[output_length] - 1) {
                output_buffer_real_low[output_length+output_exp_offset]++;
                break;
            } else if (output_buffer_low[output_length] < output_buffer_high[output_length]) {
                low_high_has_diff = true;
            }
            continue;
        } else if (output_buffer_low[output_length] < '9') {
            output_buffer_real_low[output_length+output_exp_offset]++;
            break;
        }
    }

    printf("0.%s 0.%s\n", output_buffer_real_low, output_buffer_real_high);
#ifdef DEBUG
    mpfr_fprintf(stderr, "aencode: %d(%d)\n0.%s\n0.%s\n0.%s\n0.%s\n", expptr, output_exp_offset,
        output_buffer_real_low, output_buffer_real_high, output_buffer_low, output_buffer_high);
#endif

    // Release memory
    mpfr_clear(low);
    mpfr_clear(high);
    clear_mpfr_array(low_table);
    clear_mpfr_array(high_table);

    return 0;
}
