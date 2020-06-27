#include <stdio.h>
#include <gmp.h>
#include <mpfr.h>
#include <string.h>

#include "utilities.hpp"

#define MAX_LENGTH 1024

template <size_t Size>
void print_fraction_with_radix(const char (&fraction)[Size],
                               mpfr_exp_t exp) {
    if (exp <= 0) {
        printf("0.");
        for (mpfr_exp_t i = 0; i > exp; --i) {
            putchar('0');
        }
        printf("%s", fraction);
    } else {
        for (mpfr_exp_t i = 0; i < exp; ++i) {
            putchar(fraction[i]);
        }
        putchar('.');
        printf("%s", fraction + exp);
    }
}

int main(void)
{
    // read input
    char input[MAX_LENGTH + 2] = {0};
    const size_t char_count = fread(input, sizeof(char), MAX_LENGTH, stdin);
    // fprintf(stderr, "char_count: %lu\n", char_count);
    int count_table[256] = {0};
    mpfr_t low_table[256];
    mpfr_t high_table[256];

    // count each char in input
    for (size_t i = 0; i < char_count; ++i) {
        count_table[(int)input[i]] += 1;
    }

    // Initialize count/range table
    initialize_low_high_table(count_table, char_count, low_table, high_table);

    for (int i = 0; i < 256; ++i) {
        if (count_table[i] > 0) {
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

    for (size_t i = 0; i < char_count; ++i) {
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

    char output_buffer_real_low[2048] = {0};
    char output_buffer_real_high[2048] = {0};
    mpfr_exp_t expptr_low;
    mpfr_exp_t expptr_high;
    char *output_buffer_low = mpfr_get_str(nullptr, &expptr_low, 10, 0, low, rnd);
    char *output_buffer_high = mpfr_get_str(nullptr, &expptr_high, 10, 0, high, rnd);

    // Take as many digits as need for there to be a gap between low and high
    // Then add 1 to the least significant digit of low so that it guaranteed to be at
    // least as big as actual low (including all later ignored digits)
    size_t output_length = 0;
    bool low_high_has_diff = false;
    for (; output_length < strlen(output_buffer_low); ++output_length) {
        output_buffer_real_low[output_length] = output_buffer_low[output_length];
        output_buffer_real_high[output_length] = output_buffer_high[output_length];
        if (!low_high_has_diff) {
            if (output_buffer_low[output_length] < output_buffer_high[output_length] - 1) {
                output_buffer_real_low[output_length]++;
                break;
            } else if (output_buffer_low[output_length] < output_buffer_high[output_length]) {
                low_high_has_diff = true;
            }
            continue;
        } else if (output_buffer_low[output_length] < '9') {
            output_buffer_real_low[output_length]++;
            break;
        }
    }

    print_fraction_with_radix(output_buffer_real_low, expptr_low);
    putchar(' ');
    print_fraction_with_radix(output_buffer_real_high, expptr_high);
    putchar('\n');

#ifdef DEBUG
    mpfr_fprintf(stderr, "aencode: %d\n0.%s\n0.%s\n0.%s\n0.%s\n", expptr_low,
        output_buffer_real_low, output_buffer_real_high, output_buffer_low, output_buffer_high);
#endif

    // Release memory
    mpfr_clear(low);
    mpfr_clear(high);
    mpfr_clear(code_range);
    mpfr_clear(temp);
    clear_mpfr_array(low_table);
    clear_mpfr_array(high_table);
    mpfr_free_str(output_buffer_low);
    mpfr_free_str(output_buffer_high);
    mpfr_free_cache();
    return 0;
}
