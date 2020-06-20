#pragma once

#include <gmp.h>
#include <mpfr.h>

#define AC_BITS 2048
#define LOW_HIGH_BITS 256


// This mode specifies round-to-nearest
mpfr_rnd_t rnd = MPFR_RNDN;

// initialize a low and high table from a count table
template<size_t Size>
void initialize_low_high_table(const int (&count_table)[Size],
                               const size_t char_count,
                               mpfr_t (&low_table)[Size],
                               mpfr_t (&high_table)[Size]) {
    // Initialize count/range table
    mpfr_t range_counter;
    mpfr_init2(range_counter, LOW_HIGH_BITS);
    mpfr_set_d(range_counter, 0.0, rnd);
    for (int i = 0; i < 256; ++i)
    {
        mpfr_init2(low_table[i], LOW_HIGH_BITS);
        mpfr_init2(high_table[i], LOW_HIGH_BITS);
        if (count_table[i] > 0)
        {
            mpfr_set(low_table[i], range_counter, rnd);
            mpfr_add_d(range_counter, range_counter, (double)count_table[i] / char_count, rnd);
            mpfr_set(high_table[i], range_counter, rnd);
        }
    }
    mpfr_clear(range_counter);
}
