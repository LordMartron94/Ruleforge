#include <nexus.h>

#include <math.h>
#include <setjmp.h>
#include <stdio.h>

#include "../blaze.h"

#define BLAZE_TEST_REQUIRE(condition)                                                                                                                \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      (void)fprintf(stderr, "Blaze numerics test failed at line %d: %s\n", __LINE__, #condition);                                                    \
      return 1;                                                                                                                                      \
    }                                                                                                                                                \
  } while (0)

#define BLAZE_TEST_BOUNDS(min_fn, max_fn, clamp_fn, lower, upper)                                                                                    \
  do {                                                                                                                                               \
    BLAZE_TEST_REQUIRE(min_fn(lower, upper) == (lower));                                                                                             \
    BLAZE_TEST_REQUIRE(min_fn(upper, lower) == (lower));                                                                                             \
    BLAZE_TEST_REQUIRE(max_fn(lower, upper) == (upper));                                                                                             \
    BLAZE_TEST_REQUIRE(max_fn(upper, lower) == (upper));                                                                                             \
    BLAZE_TEST_REQUIRE(min_fn(upper, upper) == (upper));                                                                                             \
    BLAZE_TEST_REQUIRE(max_fn(lower, lower) == (lower));                                                                                             \
    BLAZE_TEST_REQUIRE(clamp_fn(lower, 2, 8) == 2);                                                                                                  \
    BLAZE_TEST_REQUIRE(clamp_fn(upper, 2, 8) == 8);                                                                                                  \
    BLAZE_TEST_REQUIRE(clamp_fn(5, 2, 8) == 5);                                                                                                      \
    BLAZE_TEST_REQUIRE(clamp_fn(2, 2, 8) == 2);                                                                                                      \
    BLAZE_TEST_REQUIRE(clamp_fn(8, 2, 8) == 8);                                                                                                      \
    BLAZE_TEST_REQUIRE(clamp_fn(lower, lower, upper) == (lower));                                                                                    \
    BLAZE_TEST_REQUIRE(clamp_fn(upper, lower, upper) == (upper));                                                                                    \
    BLAZE_TEST_REQUIRE(clamp_fn(lower, 5, 5) == 5);                                                                                                  \
    BLAZE_TEST_REQUIRE(clamp_fn(upper, 5, 5) == 5);                                                                                                  \
  } while (0)

#define BLAZE_TEST_FLOAT(min_fn, max_fn, clamp_fn, type)                                                                                             \
  do {                                                                                                                                               \
    type nan_value;                                                                                                                                  \
    nan_value = (type)NAN;                                                                                                                           \
    BLAZE_TEST_BOUNDS(min_fn, max_fn, clamp_fn, -INFINITY, INFINITY);                                                                                \
    BLAZE_TEST_REQUIRE(min_fn(nan_value, 3) == 3);                                                                                                   \
    BLAZE_TEST_REQUIRE(min_fn(3, nan_value) == 3);                                                                                                   \
    BLAZE_TEST_REQUIRE(max_fn(nan_value, 3) == 3);                                                                                                   \
    BLAZE_TEST_REQUIRE(max_fn(3, nan_value) == 3);                                                                                                   \
    BLAZE_TEST_REQUIRE(isnan(min_fn(nan_value, nan_value)));                                                                                         \
    BLAZE_TEST_REQUIRE(isnan(max_fn(nan_value, nan_value)));                                                                                         \
    BLAZE_TEST_REQUIRE(isnan(clamp_fn(nan_value, -1, 1)));                                                                                           \
    BLAZE_TEST_REQUIRE(signbit(clamp_fn((type) - 0.0, 0, 1)));                                                                                       \
    BLAZE_TEST_REQUIRE(!signbit(clamp_fn((type)0.0, -1, (type) - 0.0)));                                                                             \
    BLAZE_TEST_REQUIRE(clamp_fn((type) - 0.5, -1, 1) == (type) - 0.5);                                                                               \
    BLAZE_TEST_REQUIRE(min_fn((type) - 0.5, (type)0.25) == (type) - 0.5);                                                                            \
    BLAZE_TEST_REQUIRE(max_fn((type) - 0.5, (type)0.25) == (type)0.25);                                                                              \
  } while (0)

#if NEXUS_DEVELOPER_MODE
#  define BLAZE_TEST_PRECONDITION(expression)                                                                                                        \
    do {                                                                                                                                             \
      jmp_buf recovery_point;                                                                                                                        \
      if (setjmp(recovery_point) == 0) {                                                                                                             \
        nexus_program_recovery_arm(&recovery_point);                                                                                                 \
        (void)(expression);                                                                                                                          \
        nexus_program_recovery_disarm();                                                                                                             \
        BLAZE_TEST_REQUIRE(FALSE);                                                                                                                   \
      } else {                                                                                                                                       \
        BLAZE_TEST_REQUIRE(nexus_program_recovery_last_error_get()->code == GLOBAL_NEXUS_PRECONDITION_VIOLATION);                                    \
      }                                                                                                                                              \
    } while (0)
#endif

int main(void) {
  uint128 zero;
  uint128 word_end;
  uint128 next_word;
  uint128 all_bits;
  uint128 next_word_plus_one;
  uint64  large;
  int     evaluations;
  int32   evaluated_result;

  BLAZE_TEST_FLOAT(blaze_numerics_min, blaze_numerics_max, blaze_numerics_clamp, f_real);
  BLAZE_TEST_FLOAT(blaze_numerics_minf32, blaze_numerics_maxf32, blaze_numerics_clampf32, real32);
  BLAZE_TEST_FLOAT(blaze_numerics_minf64, blaze_numerics_maxf64, blaze_numerics_clampf64, real64);
  BLAZE_TEST_BOUNDS(blaze_numerics_mini8, blaze_numerics_maxi8, blaze_numerics_clampi8, INT8_MIN_VAL, INT8_MAX_VAL);
  BLAZE_TEST_BOUNDS(blaze_numerics_mini16, blaze_numerics_maxi16, blaze_numerics_clampi16, INT16_MIN_VAL, INT16_MAX_VAL);
  BLAZE_TEST_BOUNDS(blaze_numerics_mini32, blaze_numerics_maxi32, blaze_numerics_clampi32, INT32_MIN_VAL, INT32_MAX_VAL);
  BLAZE_TEST_BOUNDS(blaze_numerics_mini64, blaze_numerics_maxi64, blaze_numerics_clampi64, INT64_MIN_VAL, INT64_MAX_VAL);
  BLAZE_TEST_BOUNDS(blaze_numerics_minu8, blaze_numerics_maxu8, blaze_numerics_clampu8, 0, UINT8_MAX_VAL);
  BLAZE_TEST_BOUNDS(blaze_numerics_minu16, blaze_numerics_maxu16, blaze_numerics_clampu16, 0, UINT16_MAX_VAL);
  BLAZE_TEST_BOUNDS(blaze_numerics_minu32, blaze_numerics_maxu32, blaze_numerics_clampu32, 0, UINT32_MAX_VAL);
  BLAZE_TEST_BOUNDS(blaze_numerics_minu64, blaze_numerics_maxu64, blaze_numerics_clampu64, 0, UINT64_MAX_VAL);
  BLAZE_TEST_BOUNDS(blaze_numerics_minilarge, blaze_numerics_maxilarge, blaze_numerics_clampilarge, INT_LARGE_MIN_VAL, INT_LARGE_MAX_VAL);
  BLAZE_TEST_BOUNDS(blaze_numerics_minularge, blaze_numerics_maxularge, blaze_numerics_clampularge, 0, UINT_LARGE_MAX_VAL);

  /* Adjacent large integers must never pass through floating-point conversion. */
  large = ((uint64)1 << 63);
  BLAZE_TEST_REQUIRE(blaze_numerics_minu64(large + 1, large + 2) == large + 1);
  BLAZE_TEST_REQUIRE(blaze_numerics_maxu64(large + 1, large + 2) == large + 2);
  BLAZE_TEST_REQUIRE(blaze_numerics_clampu64(large + 2, large + 1, large + 3) == large + 2);
  BLAZE_TEST_REQUIRE(blaze_numerics_mini64(INT64_MIN_VAL, INT64_MIN_VAL + 1) == INT64_MIN_VAL);
  BLAZE_TEST_REQUIRE(blaze_numerics_clampi64(INT64_MIN_VAL, INT64_MIN_VAL + 1, INT64_MAX_VAL) == INT64_MIN_VAL + 1);

  /* High words take precedence, then word_end words break ties. */
  zero               = nexus_uint128_zero();
  word_end           = nexus_uint128_make(UINT64_MAX_VAL, 0);
  next_word          = nexus_uint128_make(0, 1);
  next_word_plus_one = nexus_uint128_make(1, 1);
  all_bits           = nexus_uint128_make(UINT64_MAX_VAL, UINT64_MAX_VAL);
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_minu128(word_end, next_word), word_end));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_minu128(next_word, word_end), word_end));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_maxu128(word_end, next_word), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_maxu128(next_word, word_end), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_minu128(next_word_plus_one, next_word), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_maxu128(next_word, next_word_plus_one), next_word_plus_one));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_minu128(all_bits, all_bits), all_bits));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_maxu128(zero, zero), zero));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(zero, word_end, next_word), word_end));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(all_bits, word_end, next_word), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(next_word, word_end, next_word_plus_one), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(next_word, next_word, next_word_plus_one), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(next_word_plus_one, next_word, next_word_plus_one), next_word_plus_one));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(zero, next_word, next_word), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(all_bits, next_word, next_word), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(word_end, next_word, next_word_plus_one), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(next_word_plus_one, word_end, next_word), next_word));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(all_bits, zero, all_bits), all_bits));
  BLAZE_TEST_REQUIRE(nexus_uint128_compare(blaze_numerics_clampu128(zero, zero, all_bits), zero));

  evaluations      = 0;
  evaluated_result = blaze_numerics_mini32(++evaluations, 10);
  BLAZE_TEST_REQUIRE(evaluated_result == 1);
  BLAZE_TEST_REQUIRE(evaluations == 1);
  evaluated_result = blaze_numerics_maxi32(++evaluations, 0);
  BLAZE_TEST_REQUIRE(evaluated_result == 2);
  BLAZE_TEST_REQUIRE(evaluations == 2);
  evaluated_result = blaze_numerics_clampi32(++evaluations, 0, 10);
  BLAZE_TEST_REQUIRE(evaluated_result == 3);
  BLAZE_TEST_REQUIRE(evaluations == 3);

#if NEXUS_DEVELOPER_MODE
  BLAZE_TEST_PRECONDITION(blaze_numerics_clamp(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampf32(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampf64(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampi8(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampi16(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampi32(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampi64(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampu8(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampu16(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampu32(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampu64(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampilarge(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampularge(0, 8, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clamp(0, NAN, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clamp(0, 0, NAN));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampf32(0, NAN, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampf32(0, 0, NAN));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampf64(0, NAN, 2));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampf64(0, 0, NAN));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampu128(zero, next_word, word_end));
  BLAZE_TEST_PRECONDITION(blaze_numerics_clampu128(zero, next_word_plus_one, next_word));
#endif

  (void)puts("Blaze numerics tests passed.");

  return 0;
}
