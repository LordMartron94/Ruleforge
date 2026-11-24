#include <nexus.h>

#include <setjmp.h>
#include <stdio.h>

#include "../blaze.h"

#define BLAZE_TEST_REQUIRE(condition)                                                                                                                \
  do {                                                                                                                                               \
    if (!(condition)) {                                                                                                                              \
      (void)fprintf(stderr, "Blaze vector test failed at line %d: %s\n", __LINE__, #condition);                                                      \
      return 1;                                                                                                                                      \
    }                                                                                                                                                \
  } while (0)

/* The same public contract must hold independently for every component type. */
#define BLAZE_TEST_VECTOR2(type, prefix)                                                                                                             \
  do {                                                                                                                                               \
    type left;                                                                                                                                       \
    type right;                                                                                                                                      \
    type result;                                                                                                                                     \
    left  = prefix##_make(12, 24);                                                                                                                   \
    right = prefix##_make(3, 4);                                                                                                                     \
    BLAZE_TEST_REQUIRE(left.x == 12 && left.y == 24);                                                                                                \
    result = prefix##_add(left, right);                                                                                                              \
    BLAZE_TEST_REQUIRE(result.x == 15 && result.y == 28);                                                                                            \
    result = prefix##_subtract(left, right);                                                                                                         \
    BLAZE_TEST_REQUIRE(result.x == 9 && result.y == 20);                                                                                             \
    result = prefix##_multiply(left, right);                                                                                                         \
    BLAZE_TEST_REQUIRE(result.x == 36 && result.y == 96);                                                                                            \
    result = prefix##_divide(left, right);                                                                                                           \
    BLAZE_TEST_REQUIRE(result.x == 4 && result.y == 6);                                                                                              \
    result = prefix##_add_scalar(left, 2);                                                                                                           \
    BLAZE_TEST_REQUIRE(result.x == 14 && result.y == 26);                                                                                            \
    result = prefix##_subtract_scalar(left, 2);                                                                                                      \
    BLAZE_TEST_REQUIRE(result.x == 10 && result.y == 22);                                                                                            \
    result = prefix##_multiply_scalar(left, 2);                                                                                                      \
    BLAZE_TEST_REQUIRE(result.x == 24 && result.y == 48);                                                                                            \
    result = prefix##_divide_scalar(left, 2);                                                                                                        \
    BLAZE_TEST_REQUIRE(result.x == 6 && result.y == 12);                                                                                             \
    BLAZE_TEST_REQUIRE(prefix##_dot(left, right) == 132);                                                                                            \
    BLAZE_TEST_REQUIRE(prefix##_dot(right, right) == 25);                                                                                            \
    BLAZE_TEST_REQUIRE(prefix##_dot(left, right) == prefix##_dot(right, left));                                                                      \
    BLAZE_TEST_REQUIRE(prefix##_dot(prefix##_make(1, 0), prefix##_make(0, 1)) == 0);                                                                 \
    BLAZE_TEST_REQUIRE(prefix##_dot(left, prefix##_make(0, 0)) == 0);                                                                                \
    BLAZE_TEST_REQUIRE(prefix##_magnitude(right) == 5);                                                                                              \
    BLAZE_TEST_REQUIRE(prefix##_magnitude(prefix##_make(0, 0)) == 0);                                                                                \
    BLAZE_TEST_REQUIRE(left.x == 12 && left.y == 24);                                                                                                \
  } while (0)

#define BLAZE_TEST_VECTOR3(type, prefix)                                                                                                             \
  do {                                                                                                                                               \
    type left;                                                                                                                                       \
    type right;                                                                                                                                      \
    type result;                                                                                                                                     \
    left  = prefix##_make(12, 24, 40);                                                                                                               \
    right = prefix##_make(3, 4, 5);                                                                                                                  \
    BLAZE_TEST_REQUIRE(left.x == 12 && left.y == 24 && left.z == 40);                                                                                \
    result = prefix##_add(left, right);                                                                                                              \
    BLAZE_TEST_REQUIRE(result.x == 15 && result.y == 28 && result.z == 45);                                                                          \
    result = prefix##_subtract(left, right);                                                                                                         \
    BLAZE_TEST_REQUIRE(result.x == 9 && result.y == 20 && result.z == 35);                                                                           \
    result = prefix##_multiply(left, right);                                                                                                         \
    BLAZE_TEST_REQUIRE(result.x == 36 && result.y == 96 && result.z == 200);                                                                         \
    result = prefix##_divide(left, right);                                                                                                           \
    BLAZE_TEST_REQUIRE(result.x == 4 && result.y == 6 && result.z == 8);                                                                             \
    result = prefix##_add_scalar(left, 2);                                                                                                           \
    BLAZE_TEST_REQUIRE(result.x == 14 && result.y == 26 && result.z == 42);                                                                          \
    result = prefix##_subtract_scalar(left, 2);                                                                                                      \
    BLAZE_TEST_REQUIRE(result.x == 10 && result.y == 22 && result.z == 38);                                                                          \
    result = prefix##_multiply_scalar(left, 2);                                                                                                      \
    BLAZE_TEST_REQUIRE(result.x == 24 && result.y == 48 && result.z == 80);                                                                          \
    result = prefix##_divide_scalar(left, 2);                                                                                                        \
    BLAZE_TEST_REQUIRE(result.x == 6 && result.y == 12 && result.z == 20);                                                                           \
    BLAZE_TEST_REQUIRE(prefix##_dot(left, right) == 332);                                                                                            \
    BLAZE_TEST_REQUIRE(prefix##_dot(right, right) == 50);                                                                                            \
    BLAZE_TEST_REQUIRE(prefix##_dot(left, right) == prefix##_dot(right, left));                                                                      \
    BLAZE_TEST_REQUIRE(prefix##_dot(prefix##_make(1, 2, 0), prefix##_make(0, 0, 3)) == 0);                                                           \
    BLAZE_TEST_REQUIRE(prefix##_dot(left, prefix##_make(0, 0, 0)) == 0);                                                                             \
    BLAZE_TEST_REQUIRE(prefix##_magnitude(prefix##_make(2, 3, 6)) == 7);                                                                             \
    BLAZE_TEST_REQUIRE(prefix##_magnitude(prefix##_make(0, 0, 0)) == 0);                                                                             \
    BLAZE_TEST_REQUIRE(left.x == 12 && left.y == 24 && left.z == 40);                                                                                \
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
  BVector2i32 signed32;
  BVector3i64 signed64;
  BVector2u32 unsigned32;
  BVector3u64 unsigned64;
  BVector2f32 float32;
  BVector3f64 float64;
  real64      magnitude;

  BLAZE_TEST_VECTOR2(BVector2f, blaze_vector2f);
  BLAZE_TEST_VECTOR2(BVector2f32, blaze_vector2f32);
  BLAZE_TEST_VECTOR2(BVector2f64, blaze_vector2f64);
  BLAZE_TEST_VECTOR2(BVector2i32, blaze_vector2i32);
  BLAZE_TEST_VECTOR2(BVector2i64, blaze_vector2i64);
  BLAZE_TEST_VECTOR2(BVector2u32, blaze_vector2u32);
  BLAZE_TEST_VECTOR2(BVector2u64, blaze_vector2u64);
  BLAZE_TEST_VECTOR3(BVector3f, blaze_vector3f);
  BLAZE_TEST_VECTOR3(BVector3f32, blaze_vector3f32);
  BLAZE_TEST_VECTOR3(BVector3f64, blaze_vector3f64);
  BLAZE_TEST_VECTOR3(BVector3i32, blaze_vector3i32);
  BLAZE_TEST_VECTOR3(BVector3i64, blaze_vector3i64);
  BLAZE_TEST_VECTOR3(BVector3u32, blaze_vector3u32);
  BLAZE_TEST_VECTOR3(BVector3u64, blaze_vector3u64);

  /* Negative, fractional, and boundary dot products retain component arithmetic. */
  BLAZE_TEST_REQUIRE(blaze_vector2f_dot(blaze_vector2f_make(1.5, -2), blaze_vector2f_make(2, 0.5)) == 2);
  BLAZE_TEST_REQUIRE(blaze_vector3f_dot(blaze_vector3f_make(1.5, -2, 0.25), blaze_vector3f_make(2, 0.5, 4)) == 3);
  BLAZE_TEST_REQUIRE(blaze_vector2f32_dot(blaze_vector2f32_make(1.5f, -2), blaze_vector2f32_make(2, 0.5f)) == 2);
  BLAZE_TEST_REQUIRE(blaze_vector3f64_dot(blaze_vector3f64_make(1.5, -2, 0.25), blaze_vector3f64_make(2, 0.5, 4)) == 3);
  BLAZE_TEST_REQUIRE(blaze_vector2i32_dot(blaze_vector2i32_make(-3, 4), blaze_vector2i32_make(2, -5)) == -26);
  BLAZE_TEST_REQUIRE(blaze_vector3i64_dot(blaze_vector3i64_make(1, 2, 3), blaze_vector3i64_make(4, -5, 6)) == 12);
  BLAZE_TEST_REQUIRE(blaze_vector2i32_dot(blaze_vector2i32_make(INT32_MIN_VAL, 0), blaze_vector2i32_make(1, 1)) == INT32_MIN_VAL);
  BLAZE_TEST_REQUIRE(blaze_vector3i64_dot(blaze_vector3i64_make(0, INT64_MAX_VAL, 0), blaze_vector3i64_make(1, 1, 1)) == INT64_MAX_VAL);
  BLAZE_TEST_REQUIRE(blaze_vector2u32_dot(blaze_vector2u32_make(UINT32_MAX_VAL, 1), blaze_vector2u32_make(1, 1)) == 0);
  BLAZE_TEST_REQUIRE(blaze_vector3u64_dot(blaze_vector3u64_make(UINT64_MAX_VAL, 1, 1), blaze_vector3u64_make(2, 1, 1)) == 0);

  /* Integer division truncates toward zero, including negative components. */
  signed32 = blaze_vector2i32_divide_scalar(blaze_vector2i32_make(-7, 7), 2);
  BLAZE_TEST_REQUIRE(signed32.x == -3 && signed32.y == 3);
  signed64 = blaze_vector3i64_divide(blaze_vector3i64_make(-7, 7, -11), blaze_vector3i64_make(2, -2, -3));
  BLAZE_TEST_REQUIRE(signed64.x == -3 && signed64.y == -3 && signed64.z == 3);
  signed32 = blaze_vector2i32_divide_scalar(blaze_vector2i32_make(INT32_MIN_VAL, 4), 1);
  BLAZE_TEST_REQUIRE(signed32.x == INT32_MIN_VAL && signed32.y == 4);
  signed64 = blaze_vector3i64_divide_scalar(blaze_vector3i64_make(INT64_MIN_VAL, 4, 6), 1);
  BLAZE_TEST_REQUIRE(signed64.x == INT64_MIN_VAL && signed64.y == 4 && signed64.z == 6);

  /* Unsigned operations retain the underlying type's modular arithmetic. */
  unsigned32 = blaze_vector2u32_add_scalar(blaze_vector2u32_make(UINT32_MAX_VAL, 0), 1);
  BLAZE_TEST_REQUIRE(unsigned32.x == 0 && unsigned32.y == 1);
  unsigned32 = blaze_vector2u32_subtract_scalar(blaze_vector2u32_make(0, 1), 1);
  BLAZE_TEST_REQUIRE(unsigned32.x == UINT32_MAX_VAL && unsigned32.y == 0);
  unsigned64 = blaze_vector3u64_add_scalar(blaze_vector3u64_make(UINT64_MAX_VAL, 0, 1), 1);
  BLAZE_TEST_REQUIRE(unsigned64.x == 0 && unsigned64.y == 1 && unsigned64.z == 2);
  unsigned64 = blaze_vector3u64_subtract_scalar(blaze_vector3u64_make(0, 1, 2), 1);
  BLAZE_TEST_REQUIRE(unsigned64.x == UINT64_MAX_VAL && unsigned64.y == 0 && unsigned64.z == 1);

  /* The old sqrt(x + y + z) bug also fails for negative components. */
  BLAZE_TEST_REQUIRE(blaze_vector2f_magnitude(blaze_vector2f_make(-3, 4)) == 5);
  BLAZE_TEST_REQUIRE(blaze_vector3f_magnitude(blaze_vector3f_make(-2, -3, -6)) == 7);
  BLAZE_TEST_REQUIRE(blaze_vector2f32_magnitude(blaze_vector2f32_make(-3, 4)) == 5);
  BLAZE_TEST_REQUIRE(blaze_vector3f64_magnitude(blaze_vector3f64_make(-2, -3, -6)) == 7);
  BLAZE_TEST_REQUIRE(blaze_vector2i32_magnitude(blaze_vector2i32_make(INT32_MIN_VAL, 0)) == 2147483648.0);
  BLAZE_TEST_REQUIRE(blaze_vector3i64_magnitude(blaze_vector3i64_make(INT64_MIN_VAL, 0, 0)) == 9223372036854775808.0);
  BLAZE_TEST_REQUIRE(blaze_vector2u32_magnitude(blaze_vector2u32_make(UINT32_MAX_VAL, 0)) == 4294967295.0);
  BLAZE_TEST_REQUIRE(blaze_vector3u64_magnitude(blaze_vector3u64_make(UINT64_MAX_VAL, 0, 0)) == 18446744073709551616.0);
  magnitude = blaze_vector2i64_magnitude(blaze_vector2i64_make(1, 1));
  BLAZE_TEST_REQUIRE(blaze_numerics_fabsf64(magnitude - 1.4142135623730951) < 1e-15);

  /* Fractional scalar arithmetic must not take the integer path. */
  float32 = blaze_vector2f32_divide_scalar(blaze_vector2f32_make(-3, 5), 2);
  BLAZE_TEST_REQUIRE(float32.x == -1.5f && float32.y == 2.5f);
  float64 = blaze_vector3f64_multiply_scalar(blaze_vector3f64_make(-3, 5, 7), 0.5);
  BLAZE_TEST_REQUIRE(float64.x == -1.5 && float64.y == 2.5 && float64.z == 3.5);

  /* Squaring these components directly would overflow or underflow. */
  magnitude = blaze_vector2f32_magnitude(blaze_vector2f32_make(3e30f, 4e30f));
  BLAZE_TEST_REQUIRE(blaze_numerics_fabsf64((magnitude / 5e30) - 1) < 1e-6);
  magnitude = blaze_vector2f32_magnitude(blaze_vector2f32_make(3e-30f, 4e-30f));
  BLAZE_TEST_REQUIRE(blaze_numerics_fabsf64((magnitude / 5e-30) - 1) < 1e-6);
  magnitude = blaze_vector3f64_magnitude(blaze_vector3f64_make(2e300, 3e300, 6e300));
  BLAZE_TEST_REQUIRE(blaze_numerics_fabsf64((magnitude / 7e300) - 1) < 1e-15);
  magnitude = blaze_vector3f64_magnitude(blaze_vector3f64_make(2e-300, 3e-300, 6e-300));
  BLAZE_TEST_REQUIRE(blaze_numerics_fabsf64((magnitude / 7e-300) - 1) < 1e-15);

#if NEXUS_DEVELOPER_MODE
  BLAZE_TEST_PRECONDITION(blaze_vector2f_divide_scalar(blaze_vector2f_make(1, 2), 0));
  BLAZE_TEST_PRECONDITION(blaze_vector3f64_divide(blaze_vector3f64_make(1, 2, 3), blaze_vector3f64_make(1, 1, 0)));
  BLAZE_TEST_PRECONDITION(blaze_vector2i32_divide(blaze_vector2i32_make(1, INT32_MIN_VAL), blaze_vector2i32_make(1, -1)));
  BLAZE_TEST_PRECONDITION(blaze_vector3i64_divide_scalar(blaze_vector3i64_make(1, 2, INT64_MIN_VAL), -1));
  BLAZE_TEST_PRECONDITION(blaze_vector2u32_divide_scalar(blaze_vector2u32_make(1, 2), 0));
  BLAZE_TEST_PRECONDITION(blaze_vector3u64_divide(blaze_vector3u64_make(1, 2, 3), blaze_vector3u64_make(1, 0, 1)));
#endif

  (void)puts("Blaze vector tests passed.");
  return 0;
}
