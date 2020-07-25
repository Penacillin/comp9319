#ifndef _VECTOR_UTILS_H
#define _VECTOR_UTILS_H

#include <immintrin.h>


__m128i SHUFFLE_16_DOWN;
__m256i SHUFFLE_16_DOWN_256;
__m128i SHUFFLE_2x2x16_4x16;

static inline u_int32_t mm256_hadd_epi8(__m256i x) {
    const __m256i quad_sum_256i_64 = _mm256_sad_epu8(x, _mm256_setzero_si256()); // 4 * 16 bit ints in lows of 4 64 bit int
    const __m256i quad_sum_256i_64_grouped = _mm256_shuffle_epi8(quad_sum_256i_64, SHUFFLE_16_DOWN_256);
    const __m128i quad_sum_hi = _mm256_extracti128_si256(quad_sum_256i_64_grouped, 1);
    const __m128i quad_sum_lo = _mm256_extracti128_si256(quad_sum_256i_64_grouped, 0);

    const __m128i pair_sum = _mm_add_epi16(quad_sum_hi, quad_sum_lo); // 2 * 16 bits sum

    return _mm_extract_epi16(pair_sum, 0) + _mm_extract_epi16(pair_sum, 1); // TODO: use 64 bit int?
}

static inline u_int32_t mm256_hadd_epi8_2(__m256i x) {
    const __m256i quad_sum_256i_64 = _mm256_sad_epu8(x, _mm256_setzero_si256()); // 4 * 16 bit ints in lows of 4 64 bit int
    const __m128i quad_sum_hi = _mm256_extracti128_si256(quad_sum_256i_64, 1);
    const __m128i quad_sum_lo = _mm256_extracti128_si256(quad_sum_256i_64, 0);

    const __m128i hi_pair = _mm_shuffle_epi8(quad_sum_hi, SHUFFLE_16_DOWN); // 2 * 16 bits
    const __m128i lo_pair = _mm_shuffle_epi8(quad_sum_lo, SHUFFLE_16_DOWN); // 2 * 16 bits
    const __m128i pair_sum = _mm_add_epi16(hi_pair, lo_pair); // 2 * 16 bits sum

    return _mm_extract_epi16(pair_sum, 0) + _mm_extract_epi16(pair_sum, 1); // TODO: use 64 bit int?
}

static inline __m256i mm256_hadd_epi8_4(__m256i a, __m256i b, __m256i c, __m256i d) {
    const __m256i a_quad_sum256i = _mm256_sad_epu8(a, _mm256_setzero_si256()); // ---A4---A3---A2---A1
    const __m256i b_quad_sum256i = _mm256_sad_epu8(b, _mm256_setzero_si256()); // ---B4---B3---B2---B1
    const __m256i c_quad_sum256i = _mm256_slli_epi64(_mm256_sad_epu8(c, _mm256_setzero_si256()), 16); // --C4---C3---C2---C1-
    const __m256i d_quad_sum256i = _mm256_slli_epi64(_mm256_sad_epu8(d, _mm256_setzero_si256()), 16); // --D4---D3---D2---D1-

    __m256i ab_lo = _mm256_permute2x128_si256(a_quad_sum256i, b_quad_sum256i, 0x02); // ---B2---B1---A2---A1
    __m256i ab_hi = _mm256_permute2x128_si256(a_quad_sum256i, b_quad_sum256i, 0x13); // ---B4---B3---A4---A3
    ab_lo = _mm256_or_si256(_mm256_permute2x128_si256(c_quad_sum256i, d_quad_sum256i, 0x02), ab_lo); // --D2B2--D1B1--C2A2--C1A1
    ab_hi = _mm256_or_si256(_mm256_permute2x128_si256(c_quad_sum256i, d_quad_sum256i, 0x13), ab_hi); // --D4B4--D3B3--C4A4--C3A3

    __m256i quad_sum = _mm256_add_epi16(ab_lo, ab_hi); // (-,-,D2+D4,B2+B4, -,-,D1+D3,B1+B3, -,-,C2+C4,A2+A4, -,-,C1+C3,A1+A3)
    ab_hi = _mm256_permute4x64_epi64(quad_sum, 0b11110101); // -,-,D2+D4,B2+B4, -,-,D2+D4,B2+B4, -,-,C2+C4,A2+A4, -,-,C2+C4,A2+A4)
    quad_sum = _mm256_add_epi16(quad_sum, ab_hi); //-,-,2*(D2+D4),2*(B2+B4), -,-,D1+D3+D2+D4,B1+B3+B2+B4, -,-,2*(C2+C4),2*(A2+A4), -,-,C1+C3+C2+C4,A1+A3+A2+A4)
    return quad_sum;
}

static inline __m128i mm256_hadd_epi8_4_128(__m256i a, __m256i b, __m256i c, __m256i d) {
    const __m256i a_quad_sum256i = _mm256_sad_epu8(a, _mm256_setzero_si256()); // ---A4---A3---A2---A1
    const __m256i b_quad_sum256i = _mm256_sad_epu8(b, _mm256_setzero_si256()); // ---B4---B3---B2---B1
    const __m256i c_quad_sum256i = _mm256_slli_epi64(_mm256_sad_epu8(c, _mm256_setzero_si256()), 16); // --C4---C3---C2---C1-
    const __m256i d_quad_sum256i = _mm256_slli_epi64(_mm256_sad_epu8(d, _mm256_setzero_si256()), 16); // --D4---D3---D2---D1-

    __m256i ab_lo = _mm256_permute2x128_si256(a_quad_sum256i, b_quad_sum256i, 0x02); // ---B2---B1---A2---A1
    __m256i ab_hi = _mm256_permute2x128_si256(a_quad_sum256i, b_quad_sum256i, 0x13); // ---B4---B3---A4---A3
    ab_lo = _mm256_or_si256(_mm256_permute2x128_si256(c_quad_sum256i, d_quad_sum256i, 0x02), ab_lo); // --D2B2--D1B1--C2A2--C1A1
    ab_hi = _mm256_or_si256(_mm256_permute2x128_si256(c_quad_sum256i, d_quad_sum256i, 0x13), ab_hi); // --D4B4--D3B3--C4A4--C3A3

    __m256i quad_sum = _mm256_add_epi16(ab_lo, ab_hi); // (-,-,D2+D4,B2+B4, -,-,D1+D3,B1+B3, -,-,C2+C4,A2+A4, -,-,C1+C3,A1+A3)
    ab_hi = _mm256_permute4x64_epi64(quad_sum, 0b11110101); // -,-,D2+D4,B2+B4, -,-,D2+D4,B2+B4, -,-,C2+C4,A2+A4, -,-,C2+C4,A2+A4)
    quad_sum = _mm256_add_epi16(quad_sum, ab_hi); //-,-,2*(D2+D4),2*(B2+B4), -,-,D1+D3+D2+D4,B1+B3+B2+B4, -,-,2*(C2+C4),2*(A2+A4), -,-,C1+C3+C2+C4,A1+A3+A2+A4)
    quad_sum[1] = quad_sum[2]; //-,-,2*(D2+D4),2*(B2+B4), -,-,D1+D3+D2+D4,B1+B3+B2+B4, -,-,D1+D3+D2+D4,B1+B3+B2+B4, -,-,C1+C3+C2+C4,A1+A3+A2+A4)
    __m128i quad_sum_128 = _mm256_castsi256_si128(quad_sum); // -,-,D1+D3+D2+D4,B1+B3+B2+B4, -,-,C1+C3+C2+C4,A1+A3+A2+A4
    quad_sum_128 = _mm_shuffle_epi8 (quad_sum_128, SHUFFLE_2x2x16_4x16); // -,D1+D3+D2+D4,-,B1+B3+B2+B4, -,C1+C3+C2+C4,-,A1+A3+A2+A4
    return quad_sum_128;
}

#endif
