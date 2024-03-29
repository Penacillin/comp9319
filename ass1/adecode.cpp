#include <stdio.h>
#include <stdlib.h>
#include <gmp.h>
#include <mpfr.h>
#include <string.h>
#include <locale>

#include "utilities.hpp"

#define MAX_LENGTH 2048

// check if val_1 < val_2 outside by outside a margin of epsilon
bool my_mpfr_less(const mpfr_t val_1, const mpfr_t val_2, const mpfr_t epsilon) {
    mpfr_t temp;
    mpfr_init2(temp, AC_BITS);
    mpfr_sub(temp, val_2, val_1, rnd);
    if (mpfr_greater_p(temp, epsilon)) {
        mpfr_clear(temp);
        return true;
    }
    mpfr_clear(temp);
    return false;
}

// checks if val_1 >= val_2 within a margin of epsilon
bool my_mpfr_greaterequal_p(const mpfr_t val_1, const mpfr_t val_2, const mpfr_t epsilon) {
    mpfr_t temp;
    mpfr_init2(temp, AC_BITS);
    mpfr_sub(temp, val_2, val_1, rnd);
    if (mpfr_lessequal_p(temp, epsilon)) {
        mpfr_clear(temp);
        return true;
    }
    mpfr_clear(temp);
    return false;
}

template<size_t Size>
char get_symbol_for_code(const mpfr_t ac_val,
                         const int (&count_table)[Size],
                         const mpfr_t (&low_table)[Size],
                         const mpfr_t (&high_table)[Size],
                         const mpfr_t epsilon) {
    for (size_t i = 0; i < Size; ++i) {
        if (count_table[i] > 0 &&
                my_mpfr_greaterequal_p(ac_val, low_table[i], epsilon) &&
                my_mpfr_less(ac_val, high_table[i], epsilon)) {
            return (char) i;
        }
    }

    fprintf(stderr, "ERROR: Could not find character in low/high table...\n");
    exit(1);
}

int main(void)
{
    mpfr_t MPFR_EPSILON;
    mpfr_init2(MPFR_EPSILON, AC_BITS);
    mpfr_set_str(MPFR_EPSILON, "0.000000000001", 10, rnd);
    std::locale loc;

    int count_table[256] = {0};
    size_t char_count = 0;
    mpfr_t low_table[256];
    mpfr_t high_table[256];
    mpfr_t ac_val;
    mpfr_init2(ac_val, AC_BITS);

    char buffer[MAX_LENGTH];
    char c;
    int count_temp;
    while (scanf("%c %d\n", &c, &count_temp) >= 2) {
        char_count += count_table[(size_t)c] = count_temp;
    }
    fgets(buffer+1, MAX_LENGTH, stdin);
    buffer[0] = c;
    mpfr_strtofr(ac_val, buffer, nullptr, 10, rnd);

    initialize_low_high_table(count_table, char_count, low_table, high_table);

#ifdef DEBUG
    for (int i = 0; i < 256; ++i) {
        if (count_table[i] > 0)
            mpfr_fprintf(stderr, "adecode: %c %d %.9Rf %.9Rf\n", i, count_table[i], low_table[i], high_table[i]);
    }
    mpfr_fprintf(stderr, "adecode: ac_val: %.16Rf\n", ac_val);
#endif

    mpfr_t code_range;
    mpfr_init2(code_range, LOW_HIGH_BITS);
    for (size_t i = 0; i < char_count; ++i) {
        const char symbol = get_symbol_for_code(
            ac_val, count_table, low_table, high_table, MPFR_EPSILON);
        putchar(symbol);

        int res = mpfr_sub(code_range, high_table[(size_t)symbol], low_table[(size_t)symbol], rnd);
        if (res != 0) mpfr_fprintf(stderr, "adecode [WARNING]: high-low inexact\n");

        res = mpfr_sub(ac_val, ac_val, low_table[(size_t)symbol], rnd);
        if (res != 0) mpfr_fprintf(stderr, "adecode [WARNING]: high-low inexact\n");

        res = mpfr_div(ac_val, ac_val, code_range, rnd);
#ifdef DEBUG
        if (res != 0) {
            mpfr_fprintf(stderr, "adecode: %c %d /%.9Rf=%.36Rf\n", symbol, res, code_range, ac_val);
        }
#endif
    }

    mpfr_clear(code_range);
    mpfr_clear(ac_val);
    mpfr_clear(MPFR_EPSILON);
    clear_mpfr_array(low_table);
    clear_mpfr_array(high_table);
    mpfr_free_cache();
    return 0;
}
