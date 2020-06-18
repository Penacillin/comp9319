#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include <mpfr.h>
#include <string.h>
#include <locale>

#include "utilities.hpp"

#define MAX_LENGTH 2048

template<size_t Size>
char get_symbol_for_code(const mpfr_t ac_val,
                         const mpfr_t (&low_table)[Size],
                         const mpfr_t (&high_table)[Size]) {
    for (size_t i = 0; i < Size; ++i) {
        if (mpfr_greaterequal_p(ac_val, low_table[i]) && mpfr_less_p(ac_val, high_table[i])) {
            mpfr_fprintf(stderr, "%.9Rf <= %.70Rf < %.70Rf\n", low_table[i], ac_val, high_table[i]);
            return (char) i;
        }
    }

    fprintf(stderr, "ERROR: Could not find character in low/high table...\n");
    exit(1);
}

int main(void)
{
    std::locale loc;

    int count_table[256] = {0};
    size_t char_count = 0;
    mpfr_t low_table[256];
    mpfr_t high_table[256];
    mpfr_t ac_val;
    mpfr_init2(ac_val, 256);
    printf("adecode: %ld\n", mpfr_get_emin());
    char buffer[MAX_LENGTH];
    // count each char in input
    while (fgets(buffer, MAX_LENGTH, stdin) != NULL)
    {
        if (buffer[1] == '.') {
            fprintf(stderr, "adecode: ac_val buffer: %s\n", buffer);
            mpfr_strtofr(ac_val, buffer, nullptr, 10, rnd);
            break;
        }
        char c;
        sscanf(buffer, "%c", &c);
        count_table[(size_t)c] = strtol(buffer + 2, nullptr, 10);
        char_count += count_table[(size_t)c];
    }

    initialize_low_high_table(count_table, char_count, low_table, high_table);

#ifdef DEBUG
    for (int i = 0; i < 256; ++i) {
        if (count_table[i] > 0)
            mpfr_printf("adecode: %c %d %.9Rf %.9Rf\n", i, count_table[i], low_table[i], high_table[i]);
    }
    mpfr_fprintf(stderr, "adecode: ac_val: %.16Rf\n", ac_val);
#endif

    mpfr_t code_range;
    mpfr_init2(code_range, 256);
    for (size_t i = 0; i < char_count; ++i) {
        char symbol = get_symbol_for_code(ac_val, low_table, high_table);
        putchar(symbol);
        mpfr_fprintf(stderr, "adecode: ac_val: %.9Rf (%c)\n", ac_val, symbol);
        mpfr_sub(code_range, high_table[(size_t)symbol], low_table[(size_t)symbol], rnd);
        mpfr_sub(ac_val, ac_val, low_table[(size_t)symbol], rnd);
        mpfr_div(ac_val, ac_val, code_range, rnd);
    }
    mpfr_clear(code_range);

    return 0;
}
