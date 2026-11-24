#pragma once

#include <nexus.h>
#include <math.h>

/* All angles are in radians. C99 <math.h> functions are required. */

/* -------------------------------------------------------------------------- */
/* NUMERICS                                                                   */
/* -------------------------------------------------------------------------- */

/* --- Unary functions --- */

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_fabs(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return fabs(value);
#else
  return fabsf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_fabsf32(real32 value) {
  return fabsf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_fabsf64(real64 value) {
  return fabs(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_sqrt(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return sqrt(value);
#else
  return sqrtf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_sqrtf32(real32 value) {
  return sqrtf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_sqrtf64(real64 value) {
  return sqrt(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_cbrt(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return cbrt(value);
#else
  return cbrtf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_cbrtf32(real32 value) {
  return cbrtf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_cbrtf64(real64 value) {
  return cbrt(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_ceil(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return ceil(value);
#else
  return ceilf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_ceilf32(real32 value) {
  return ceilf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_ceilf64(real64 value) {
  return ceil(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_floor(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return floor(value);
#else
  return floorf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_floorf32(real32 value) {
  return floorf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_floorf64(real64 value) {
  return floor(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_trunc(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return trunc(value);
#else
  return truncf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_truncf32(real32 value) {
  return truncf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_truncf64(real64 value) {
  return trunc(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_round(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return round(value);
#else
  return roundf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_roundf32(real32 value) {
  return roundf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_roundf64(real64 value) {
  return round(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_nearbyint(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return nearbyint(value);
#else
  return nearbyintf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_nearbyintf32(real32 value) {
  return nearbyintf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_nearbyintf64(real64 value) {
  return nearbyint(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_rint(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return rint(value);
#else
  return rintf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_rintf32(real32 value) {
  return rintf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_rintf64(real64 value) {
  return rint(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_exp(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return exp(value);
#else
  return expf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_expf32(real32 value) {
  return expf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_expf64(real64 value) {
  return exp(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_exp2(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return exp2(value);
#else
  return exp2f(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_exp2f32(real32 value) {
  return exp2f(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_exp2f64(real64 value) {
  return exp2(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_expm1(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return expm1(value);
#else
  return expm1f(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_expm1f32(real32 value) {
  return expm1f(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_expm1f64(real64 value) {
  return expm1(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_log(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return log(value);
#else
  return logf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_logf32(real32 value) {
  return logf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_logf64(real64 value) {
  return log(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_log2(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return log2(value);
#else
  return log2f(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_log2f32(real32 value) {
  return log2f(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_log2f64(real64 value) {
  return log2(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_log10(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return log10(value);
#else
  return log10f(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_log10f32(real32 value) {
  return log10f(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_log10f64(real64 value) {
  return log10(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_log1p(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return log1p(value);
#else
  return log1pf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_log1pf32(real32 value) {
  return log1pf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_log1pf64(real64 value) {
  return log1p(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_sin(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return sin(value);
#else
  return sinf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_sinf32(real32 value) {
  return sinf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_sinf64(real64 value) {
  return sin(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_cos(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return cos(value);
#else
  return cosf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_cosf32(real32 value) {
  return cosf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_cosf64(real64 value) {
  return cos(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_tan(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return tan(value);
#else
  return tanf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_tanf32(real32 value) {
  return tanf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_tanf64(real64 value) {
  return tan(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_asin(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return asin(value);
#else
  return asinf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_asinf32(real32 value) {
  return asinf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_asinf64(real64 value) {
  return asin(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_acos(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return acos(value);
#else
  return acosf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_acosf32(real32 value) {
  return acosf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_acosf64(real64 value) {
  return acos(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_atan(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return atan(value);
#else
  return atanf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_atanf32(real32 value) {
  return atanf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_atanf64(real64 value) {
  return atan(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_sinh(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return sinh(value);
#else
  return sinhf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_sinhf32(real32 value) {
  return sinhf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_sinhf64(real64 value) {
  return sinh(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_cosh(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return cosh(value);
#else
  return coshf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_coshf32(real32 value) {
  return coshf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_coshf64(real64 value) {
  return cosh(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_tanh(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return tanh(value);
#else
  return tanhf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_tanhf32(real32 value) {
  return tanhf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_tanhf64(real64 value) {
  return tanh(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_asinh(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return asinh(value);
#else
  return asinhf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_asinhf32(real32 value) {
  return asinhf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_asinhf64(real64 value) {
  return asinh(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_acosh(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return acosh(value);
#else
  return acoshf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_acoshf32(real32 value) {
  return acoshf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_acoshf64(real64 value) {
  return acosh(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_atanh(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return atanh(value);
#else
  return atanhf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_atanhf32(real32 value) {
  return atanhf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_atanhf64(real64 value) {
  return atanh(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_erf(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return erf(value);
#else
  return erff(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_erff32(real32 value) {
  return erff(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_erff64(real64 value) {
  return erf(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_erfc(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return erfc(value);
#else
  return erfcf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_erfcf32(real32 value) {
  return erfcf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_erfcf64(real64 value) {
  return erfc(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_tgamma(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return tgamma(value);
#else
  return tgammaf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_tgammaf32(real32 value) {
  return tgammaf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_tgammaf64(real64 value) {
  return tgamma(value);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_lgamma(f_real value) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return lgamma(value);
#else
  return lgammaf(value);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_lgammaf32(real32 value) {
  return lgammaf(value);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_lgammaf64(real64 value) {
  return lgamma(value);
}

/* --- Binary functions --- */

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_atan2(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return atan2(left, right);
#else
  return atan2f(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_atan2f32(real32 left, real32 right) {
  return atan2f(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_atan2f64(real64 left, real64 right) {
  return atan2(left, right);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_pow(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return pow(left, right);
#else
  return powf(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_powf32(real32 left, real32 right) {
  return powf(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_powf64(real64 left, real64 right) {
  return pow(left, right);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_hypot(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return hypot(left, right);
#else
  return hypotf(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_hypotf32(real32 left, real32 right) {
  return hypotf(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_hypotf64(real64 left, real64 right) {
  return hypot(left, right);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_fmod(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return fmod(left, right);
#else
  return fmodf(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_fmodf32(real32 left, real32 right) {
  return fmodf(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_fmodf64(real64 left, real64 right) {
  return fmod(left, right);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_remainder(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return remainder(left, right);
#else
  return remainderf(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_remainderf32(real32 left, real32 right) {
  return remainderf(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_remainderf64(real64 left, real64 right) {
  return remainder(left, right);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_fmin(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return fmin(left, right);
#else
  return fminf(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_fminf32(real32 left, real32 right) {
  return fminf(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_fminf64(real64 left, real64 right) {
  return fmin(left, right);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_fmax(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return fmax(left, right);
#else
  return fmaxf(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_fmaxf32(real32 left, real32 right) {
  return fmaxf(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_fmaxf64(real64 left, real64 right) {
  return fmax(left, right);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_fdim(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return fdim(left, right);
#else
  return fdimf(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_fdimf32(real32 left, real32 right) {
  return fdimf(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_fdimf64(real64 left, real64 right) {
  return fdim(left, right);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_copysign(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return copysign(left, right);
#else
  return copysignf(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_copysignf32(real32 left, real32 right) {
  return copysignf(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_copysignf64(real64 left, real64 right) {
  return copysign(left, right);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_nextafter(f_real left, f_real right) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return nextafter(left, right);
#else
  return nextafterf(left, right);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_nextafterf32(real32 left, real32 right) {
  return nextafterf(left, right);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_nextafterf64(real64 left, real64 right) {
  return nextafter(left, right);
}

/* --- Ternary functions --- */

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_fma(f_real a, f_real b, f_real c) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return fma(a, b, c);
#else
  return fmaf(a, b, c);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_fmaf32(real32 a, real32 b, real32 c) {
  return fmaf(a, b, c);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_fmaf64(real64 a, real64 b, real64 c) {
  return fma(a, b, c);
}

/* --- Minimum, maximum, and clamp --- */

/*
Summary: blaze_numerics_min returns the floating minimum with fmin semantics.
*/
static NEXUS_MAYBE_UNUSED f_real blaze_numerics_min(f_real left, f_real right) {
  return blaze_numerics_fmin(left, right);
}

/*
Summary: blaze_numerics_max returns the floating maximum with fmax semantics.
*/
static NEXUS_MAYBE_UNUSED f_real blaze_numerics_max(f_real left, f_real right) {
  return blaze_numerics_fmax(left, right);
}

/*
Summary: blaze_numerics_clamp bounds a value inclusively, preserving NaN and signed zero.
Errors: unordered or NaN bounds are programmer errors (asserts).
*/
static NEXUS_MAYBE_UNUSED f_real blaze_numerics_clamp(f_real value, f_real minimum, f_real maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered and not NaN");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_minf32 returns the floating minimum with fmin semantics.
*/
static NEXUS_MAYBE_UNUSED real32 blaze_numerics_minf32(real32 left, real32 right) {
  return blaze_numerics_fminf32(left, right);
}

/*
Summary: blaze_numerics_maxf32 returns the floating maximum with fmax semantics.
*/
static NEXUS_MAYBE_UNUSED real32 blaze_numerics_maxf32(real32 left, real32 right) {
  return blaze_numerics_fmaxf32(left, right);
}

/*
Summary: blaze_numerics_clampf32 bounds a value inclusively, preserving NaN and signed zero.
Errors: unordered or NaN bounds are programmer errors (asserts).
*/
static NEXUS_MAYBE_UNUSED real32 blaze_numerics_clampf32(real32 value, real32 minimum, real32 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered and not NaN");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_minf64 returns the floating minimum with fmin semantics.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_numerics_minf64(real64 left, real64 right) {
  return blaze_numerics_fminf64(left, right);
}

/*
Summary: blaze_numerics_maxf64 returns the floating maximum with fmax semantics.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_numerics_maxf64(real64 left, real64 right) {
  return blaze_numerics_fmaxf64(left, right);
}

/*
Summary: blaze_numerics_clampf64 bounds a value inclusively, preserving NaN and signed zero.
Errors: unordered or NaN bounds are programmer errors (asserts).
*/
static NEXUS_MAYBE_UNUSED real64 blaze_numerics_clampf64(real64 value, real64 minimum, real64 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered and not NaN");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_mini8 returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int8 blaze_numerics_mini8(int8 left, int8 right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxi8 returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int8 blaze_numerics_maxi8(int8 left, int8 right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampi8 bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED int8 blaze_numerics_clampi8(int8 value, int8 minimum, int8 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_mini16 returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int16 blaze_numerics_mini16(int16 left, int16 right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxi16 returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int16 blaze_numerics_maxi16(int16 left, int16 right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampi16 bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED int16 blaze_numerics_clampi16(int16 value, int16 minimum, int16 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_mini32 returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int32 blaze_numerics_mini32(int32 left, int32 right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxi32 returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int32 blaze_numerics_maxi32(int32 left, int32 right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampi32 bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED int32 blaze_numerics_clampi32(int32 value, int32 minimum, int32 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_mini64 returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int64 blaze_numerics_mini64(int64 left, int64 right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxi64 returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int64 blaze_numerics_maxi64(int64 left, int64 right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampi64 bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED int64 blaze_numerics_clampi64(int64 value, int64 minimum, int64 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_minu8 returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint8 blaze_numerics_minu8(uint8 left, uint8 right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxu8 returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint8 blaze_numerics_maxu8(uint8 left, uint8 right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampu8 bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED uint8 blaze_numerics_clampu8(uint8 value, uint8 minimum, uint8 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_minu16 returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint16 blaze_numerics_minu16(uint16 left, uint16 right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxu16 returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint16 blaze_numerics_maxu16(uint16 left, uint16 right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampu16 bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED uint16 blaze_numerics_clampu16(uint16 value, uint16 minimum, uint16 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_minu32 returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint32 blaze_numerics_minu32(uint32 left, uint32 right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxu32 returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint32 blaze_numerics_maxu32(uint32 left, uint32 right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampu32 bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED uint32 blaze_numerics_clampu32(uint32 value, uint32 minimum, uint32 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_minu64 returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint64 blaze_numerics_minu64(uint64 left, uint64 right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxu64 returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint64 blaze_numerics_maxu64(uint64 left, uint64 right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampu64 bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED uint64 blaze_numerics_clampu64(uint64 value, uint64 minimum, uint64 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_minilarge returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int_large blaze_numerics_minilarge(int_large left, int_large right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxilarge returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED int_large blaze_numerics_maxilarge(int_large left, int_large right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampilarge bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED int_large blaze_numerics_clampilarge(int_large value, int_large minimum, int_large maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_minularge returns the smaller integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint_large blaze_numerics_minularge(uint_large left, uint_large right) {
  if (left < right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_maxularge returns the larger integer without conversion.
*/
static NEXUS_MAYBE_UNUSED uint_large blaze_numerics_maxularge(uint_large left, uint_large right) {
  if (left > right) {
    return left;
  }

  return right;
}

/*
Summary: blaze_numerics_clampularge bounds an integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED uint_large blaze_numerics_clampularge(uint_large value, uint_large minimum, uint_large maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum <= maximum, "Clamp bounds must be ordered");

  if (value < minimum) {
    return minimum;
  }

  if (value > maximum) {
    return maximum;
  }

  return value;
}

/*
Summary: blaze_numerics_minu128 returns the smaller unsigned 128-bit integer.
*/
static NEXUS_MAYBE_UNUSED uint128 blaze_numerics_minu128(uint128 left, uint128 right) {
  return left.hi < right.hi || (left.hi == right.hi && left.lo < right.lo) ? left : right;
}

/*
Summary: blaze_numerics_maxu128 returns the larger unsigned 128-bit integer.
*/
static NEXUS_MAYBE_UNUSED uint128 blaze_numerics_maxu128(uint128 left, uint128 right) {
  return left.hi > right.hi || (left.hi == right.hi && left.lo > right.lo) ? left : right;
}

/*
Summary: blaze_numerics_clampu128 bounds an unsigned 128-bit integer inclusively.
Errors: minimum greater than maximum is a programmer error (asserts).
*/
static NEXUS_MAYBE_UNUSED uint128 blaze_numerics_clampu128(uint128 value, uint128 minimum, uint128 maximum) {
  NEXUS_PROGRAM_PRECONDITION(minimum.hi < maximum.hi || (minimum.hi == maximum.hi && minimum.lo <= maximum.lo), "Clamp bounds must be ordered");

  if (value.hi < minimum.hi || (value.hi == minimum.hi && value.lo < minimum.lo)) {
    return minimum;
  }

  if (value.hi > maximum.hi || (value.hi == maximum.hi && value.lo > maximum.lo)) {
    return maximum;
  }

  return value;
}

/* --- Integer exponent functions --- */

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_ldexp(f_real value, int32 exponent) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return ldexp(value, exponent);
#else
  return ldexpf(value, exponent);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_ldexpf32(real32 value, int32 exponent) {
  return ldexpf(value, exponent);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_ldexpf64(real64 value, int32 exponent) {
  return ldexp(value, exponent);
}

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_scalbn(f_real value, int32 exponent) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return scalbn(value, exponent);
#else
  return scalbnf(value, exponent);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_scalbnf32(real32 value, int32 exponent) {
  return scalbnf(value, exponent);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_scalbnf64(real64 value, int32 exponent) {
  return scalbn(value, exponent);
}

/* --- Long integer exponent functions --- */

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_scalbln(f_real value, long exponent) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return scalbln(value, exponent);
#else
  return scalblnf(value, exponent);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_scalblnf32(real32 value, long exponent) {
  return scalblnf(value, exponent);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_scalblnf64(real64 value, long exponent) {
  return scalbln(value, exponent);
}

/* --- Decomposition functions --- */

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_frexp(f_real value, int32 *exponent) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return frexp(value, exponent);
#else
  return frexpf(value, exponent);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_frexpf32(real32 value, int32 *exponent) {
  return frexpf(value, exponent);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_frexpf64(real64 value, int32 *exponent) {
  return frexp(value, exponent);
}

/* --- Fraction decomposition functions --- */

static NEXUS_MAYBE_UNUSED f_real blaze_numerics_modf(f_real value, f_real *integer_part) {
#if NEXUS_FLOAT_DOUBLE_PRECISION
  return modf(value, integer_part);
#else
  return modff(value, integer_part);
#endif
}

static NEXUS_MAYBE_UNUSED real32 blaze_numerics_modff32(real32 value, real32 *integer_part) {
  return modff(value, integer_part);
}

static NEXUS_MAYBE_UNUSED real64 blaze_numerics_modff64(real64 value, real64 *integer_part) {
  return modf(value, integer_part);
}

/* -------------------------------------------------------------------------- */
/* COMPLEX                                                                    */
/* -------------------------------------------------------------------------- */

typedef struct BComplex32 {
  real32 real;
  real32 imaginary;
} BComplex32;

typedef struct BComplex64 {
  real64 real;
  real64 imaginary;
} BComplex64;

/* --- 32-bit Complex Arithmetic --- */

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_make(real32 real, real32 imaginary) {
  BComplex32 result;
  result.real      = real;
  result.imaginary = imaginary;
  return result;
}

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_add(BComplex32 a, BComplex32 b) {
  return blaze_complex32_make(a.real + b.real, a.imaginary + b.imaginary);
}

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_subtract(BComplex32 a, BComplex32 b) {
  return blaze_complex32_make(a.real - b.real, a.imaginary - b.imaginary);
}

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_multiply(BComplex32 a, BComplex32 b) {
  return blaze_complex32_make((a.real * b.real) - (a.imaginary * b.imaginary), (a.real * b.imaginary) + (a.imaginary * b.real));
}

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_divide(BComplex32 a, BComplex32 b) {
  real64 denominator;

  denominator = ((real64)b.real * b.real) + ((real64)b.imaginary * b.imaginary);
  NEXUS_PROGRAM_PRECONDITION(denominator != 0.0, "Complex division by zero");

  return blaze_complex32_make((real32)((((real64)a.real * b.real) + ((real64)a.imaginary * b.imaginary)) / denominator),
                              (real32)((((real64)a.imaginary * b.real) - ((real64)a.real * b.imaginary)) / denominator));
}

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_scale(BComplex32 value, real32 scalar) {
  return blaze_complex32_make(value.real * scalar, value.imaginary * scalar);
}

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_negate(BComplex32 value) {
  return blaze_complex32_make(-value.real, -value.imaginary);
}

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_conjugate(BComplex32 value) {
  return blaze_complex32_make(value.real, -value.imaginary);
}

static NEXUS_MAYBE_UNUSED real32 blaze_complex32_magnitude_squared(BComplex32 value) {
  return (value.real * value.real) + (value.imaginary * value.imaginary);
}

static NEXUS_MAYBE_UNUSED real32 blaze_complex32_magnitude(BComplex32 value) {
  return (real32)hypot((real64)value.real, (real64)value.imaginary);
}

static NEXUS_MAYBE_UNUSED real32 blaze_complex32_phase(BComplex32 value) {
  return (real32)atan2((real64)value.imaginary, (real64)value.real);
}

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_from_polar(real32 magnitude, real32 angle) {
  return blaze_complex32_make(magnitude * (real32)cos((real64)angle), magnitude * (real32)sin((real64)angle));
}

static NEXUS_MAYBE_UNUSED BComplex32 blaze_complex32_from_angle(real32 angle) {
  return blaze_complex32_from_polar(1.0f, angle);
}

/* --- 64-bit Complex Arithmetic --- */

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_make(real64 real, real64 imaginary) {
  BComplex64 result;
  result.real      = real;
  result.imaginary = imaginary;
  return result;
}

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_add(BComplex64 a, BComplex64 b) {
  return blaze_complex64_make(a.real + b.real, a.imaginary + b.imaginary);
}

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_subtract(BComplex64 a, BComplex64 b) {
  return blaze_complex64_make(a.real - b.real, a.imaginary - b.imaginary);
}

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_multiply(BComplex64 a, BComplex64 b) {
  return blaze_complex64_make((a.real * b.real) - (a.imaginary * b.imaginary), (a.real * b.imaginary) + (a.imaginary * b.real));
}

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_divide(BComplex64 a, BComplex64 b) {
  real64 ratio;
  real64 denominator;

  NEXUS_PROGRAM_PRECONDITION(b.real != 0.0 || b.imaginary != 0.0, "Complex division by zero");

  if (fabs(b.real) >= fabs(b.imaginary)) {
    ratio       = b.imaginary / b.real;
    denominator = b.real + (b.imaginary * ratio);

    return blaze_complex64_make((a.real + (a.imaginary * ratio)) / denominator, (a.imaginary - (a.real * ratio)) / denominator);
  }

  ratio       = b.real / b.imaginary;
  denominator = b.imaginary + (b.real * ratio);

  return blaze_complex64_make(((a.real * ratio) + a.imaginary) / denominator, ((a.imaginary * ratio) - a.real) / denominator);
}

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_scale(BComplex64 value, real64 scalar) {
  return blaze_complex64_make(value.real * scalar, value.imaginary * scalar);
}

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_negate(BComplex64 value) {
  return blaze_complex64_make(-value.real, -value.imaginary);
}

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_conjugate(BComplex64 value) {
  return blaze_complex64_make(value.real, -value.imaginary);
}

static NEXUS_MAYBE_UNUSED real64 blaze_complex64_magnitude_squared(BComplex64 value) {
  return (value.real * value.real) + (value.imaginary * value.imaginary);
}

static NEXUS_MAYBE_UNUSED real64 blaze_complex64_magnitude(BComplex64 value) {
  return hypot(value.real, value.imaginary);
}

static NEXUS_MAYBE_UNUSED real64 blaze_complex64_phase(BComplex64 value) {
  return atan2(value.imaginary, value.real);
}

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_from_polar(real64 magnitude, real64 angle) {
  return blaze_complex64_make(magnitude * cos(angle), magnitude * sin(angle));
}

static NEXUS_MAYBE_UNUSED BComplex64 blaze_complex64_from_angle(real64 angle) {
  return blaze_complex64_from_polar(1.0, angle);
}

/* -------------------------------------------------------------------------- */
/* VECTORS                                                                    */
/* -------------------------------------------------------------------------- */

/*
Componentwise arithmetic; multiply is the Hadamard product, not a dot product.
Scalar operations apply the right-hand scalar to every component.
Signed integer add/subtract/multiply and dot products require representable
products and partial sums; unsigned arithmetic wraps modulo the component
width. Dot products return the component type. Integer division truncates toward
zero. Division asserts nonzero divisors and rejects signed MIN / -1 in
NEXUS_DEVELOPER_MODE; these remain caller preconditions in release builds.
Integer magnitudes convert to real64 before arithmetic; 64-bit components may
round during that conversion. Floating magnitudes use hypot to avoid needless
intermediate overflow and underflow. Values own no resources or shared state.
*/

/*
Summary: 2D vector with f_real components.
*/
typedef struct BVector2f {
  f_real x;
  f_real y;
} BVector2f;

/*
Summary: construct a BVector2f from its components.
*/
static NEXUS_MAYBE_UNUSED BVector2f blaze_vector2f_make(f_real x_value, f_real y_value) {
  BVector2f result;

  result.x = x_value;
  result.y = y_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2f blaze_vector2f_add(BVector2f a, BVector2f b) {
  BVector2f result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2f blaze_vector2f_subtract(BVector2f a, BVector2f b) {
  BVector2f result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2f blaze_vector2f_multiply(BVector2f a, BVector2f b) {
  BVector2f result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2f blaze_vector2f_divide(BVector2f a, BVector2f b) {
  BVector2f result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2f blaze_vector2f_add_scalar(BVector2f vector, f_real scalar) {
  BVector2f result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector2f blaze_vector2f_subtract_scalar(BVector2f vector, f_real scalar) {
  BVector2f result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2f blaze_vector2f_multiply_scalar(BVector2f vector, f_real scalar) {
  BVector2f result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2f blaze_vector2f_divide_scalar(BVector2f vector, f_real scalar) {
  BVector2f result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector2f values.
*/
static NEXUS_MAYBE_UNUSED f_real blaze_vector2f_dot(BVector2f a, BVector2f b) {
  return (a.x * b.x) + (a.y * b.y);
}

/*
Summary: compute the Euclidean length of a BVector2f.
*/
static NEXUS_MAYBE_UNUSED f_real blaze_vector2f_magnitude(BVector2f vector) {
  return blaze_numerics_hypot(vector.x, vector.y);
}

/*
Summary: 2D vector with real32 components.
*/
typedef struct BVector2f32 {
  real32 x;
  real32 y;
} BVector2f32;

/*
Summary: construct a BVector2f32 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector2f32 blaze_vector2f32_make(real32 x_value, real32 y_value) {
  BVector2f32 result;

  result.x = x_value;
  result.y = y_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2f32 blaze_vector2f32_add(BVector2f32 a, BVector2f32 b) {
  BVector2f32 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2f32 blaze_vector2f32_subtract(BVector2f32 a, BVector2f32 b) {
  BVector2f32 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2f32 blaze_vector2f32_multiply(BVector2f32 a, BVector2f32 b) {
  BVector2f32 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2f32 blaze_vector2f32_divide(BVector2f32 a, BVector2f32 b) {
  BVector2f32 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2f32 blaze_vector2f32_add_scalar(BVector2f32 vector, real32 scalar) {
  BVector2f32 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector2f32 blaze_vector2f32_subtract_scalar(BVector2f32 vector, real32 scalar) {
  BVector2f32 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2f32 blaze_vector2f32_multiply_scalar(BVector2f32 vector, real32 scalar) {
  BVector2f32 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2f32 blaze_vector2f32_divide_scalar(BVector2f32 vector, real32 scalar) {
  BVector2f32 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector2f32 values.
*/
static NEXUS_MAYBE_UNUSED real32 blaze_vector2f32_dot(BVector2f32 a, BVector2f32 b) {
  return (a.x * b.x) + (a.y * b.y);
}

/*
Summary: compute the Euclidean length of a BVector2f32.
*/
static NEXUS_MAYBE_UNUSED real32 blaze_vector2f32_magnitude(BVector2f32 vector) {
  return blaze_numerics_hypotf32(vector.x, vector.y);
}

/*
Summary: 2D vector with real64 components.
*/
typedef struct BVector2f64 {
  real64 x;
  real64 y;
} BVector2f64;

/*
Summary: construct a BVector2f64 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector2f64 blaze_vector2f64_make(real64 x_value, real64 y_value) {
  BVector2f64 result;

  result.x = x_value;
  result.y = y_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2f64 blaze_vector2f64_add(BVector2f64 a, BVector2f64 b) {
  BVector2f64 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2f64 blaze_vector2f64_subtract(BVector2f64 a, BVector2f64 b) {
  BVector2f64 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2f64 blaze_vector2f64_multiply(BVector2f64 a, BVector2f64 b) {
  BVector2f64 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2f64 blaze_vector2f64_divide(BVector2f64 a, BVector2f64 b) {
  BVector2f64 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2f64 blaze_vector2f64_add_scalar(BVector2f64 vector, real64 scalar) {
  BVector2f64 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector2f64 blaze_vector2f64_subtract_scalar(BVector2f64 vector, real64 scalar) {
  BVector2f64 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2f64 blaze_vector2f64_multiply_scalar(BVector2f64 vector, real64 scalar) {
  BVector2f64 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2f64 blaze_vector2f64_divide_scalar(BVector2f64 vector, real64 scalar) {
  BVector2f64 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector2f64 values.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector2f64_dot(BVector2f64 a, BVector2f64 b) {
  return (a.x * b.x) + (a.y * b.y);
}

/*
Summary: compute the Euclidean length of a BVector2f64.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector2f64_magnitude(BVector2f64 vector) {
  return blaze_numerics_hypotf64(vector.x, vector.y);
}

/*
Summary: 2D vector with int32 components.
*/
typedef struct BVector2i32 {
  int32 x;
  int32 y;
} BVector2i32;

/*
Summary: construct a BVector2i32 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector2i32 blaze_vector2i32_make(int32 x_value, int32 y_value) {
  BVector2i32 result;

  result.x = x_value;
  result.y = y_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2i32 blaze_vector2i32_add(BVector2i32 a, BVector2i32 b) {
  BVector2i32 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2i32 blaze_vector2i32_subtract(BVector2i32 a, BVector2i32 b) {
  BVector2i32 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2i32 blaze_vector2i32_multiply(BVector2i32 a, BVector2i32 b) {
  BVector2i32 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
  A minimum signed component divided by -1 also asserts in developer mode.
*/
static NEXUS_MAYBE_UNUSED BVector2i32 blaze_vector2i32_divide(BVector2i32 a, BVector2i32 b) {
  BVector2i32 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0, "Vector division by zero");
  NEXUS_PROGRAM_PRECONDITION(!(a.x == INT32_MIN_VAL && b.x == -1) && !(a.y == INT32_MIN_VAL && b.y == -1), "Signed vector division overflow");

  result.x = a.x / b.x;
  result.y = a.y / b.y;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2i32 blaze_vector2i32_add_scalar(BVector2i32 vector, int32 scalar) {
  BVector2i32 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector2i32 blaze_vector2i32_subtract_scalar(BVector2i32 vector, int32 scalar) {
  BVector2i32 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2i32 blaze_vector2i32_multiply_scalar(BVector2i32 vector, int32 scalar) {
  BVector2i32 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
  A minimum signed component divided by -1 also asserts in developer mode.
*/
static NEXUS_MAYBE_UNUSED BVector2i32 blaze_vector2i32_divide_scalar(BVector2i32 vector, int32 scalar) {
  BVector2i32 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");
  NEXUS_PROGRAM_PRECONDITION(!(vector.x == INT32_MIN_VAL && scalar == -1) && !(vector.y == INT32_MIN_VAL && scalar == -1),
                             "Signed vector division overflow");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector2i32 values.
*/
static NEXUS_MAYBE_UNUSED int32 blaze_vector2i32_dot(BVector2i32 a, BVector2i32 b) {
  return (a.x * b.x) + (a.y * b.y);
}

/*
Summary: compute the Euclidean length of a BVector2i32.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector2i32_magnitude(BVector2i32 vector) {
  return blaze_numerics_hypotf64((real64)vector.x, (real64)vector.y);
}

/*
Summary: 2D vector with int64 components.
*/
typedef struct BVector2i64 {
  int64 x;
  int64 y;
} BVector2i64;

/*
Summary: construct a BVector2i64 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector2i64 blaze_vector2i64_make(int64 x_value, int64 y_value) {
  BVector2i64 result;

  result.x = x_value;
  result.y = y_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2i64 blaze_vector2i64_add(BVector2i64 a, BVector2i64 b) {
  BVector2i64 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2i64 blaze_vector2i64_subtract(BVector2i64 a, BVector2i64 b) {
  BVector2i64 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2i64 blaze_vector2i64_multiply(BVector2i64 a, BVector2i64 b) {
  BVector2i64 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
  A minimum signed component divided by -1 also asserts in developer mode.
*/
static NEXUS_MAYBE_UNUSED BVector2i64 blaze_vector2i64_divide(BVector2i64 a, BVector2i64 b) {
  BVector2i64 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0, "Vector division by zero");
  NEXUS_PROGRAM_PRECONDITION(!(a.x == INT64_MIN_VAL && b.x == -1) && !(a.y == INT64_MIN_VAL && b.y == -1), "Signed vector division overflow");

  result.x = a.x / b.x;
  result.y = a.y / b.y;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2i64 blaze_vector2i64_add_scalar(BVector2i64 vector, int64 scalar) {
  BVector2i64 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector2i64 blaze_vector2i64_subtract_scalar(BVector2i64 vector, int64 scalar) {
  BVector2i64 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2i64 blaze_vector2i64_multiply_scalar(BVector2i64 vector, int64 scalar) {
  BVector2i64 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
  A minimum signed component divided by -1 also asserts in developer mode.
*/
static NEXUS_MAYBE_UNUSED BVector2i64 blaze_vector2i64_divide_scalar(BVector2i64 vector, int64 scalar) {
  BVector2i64 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");
  NEXUS_PROGRAM_PRECONDITION(!(vector.x == INT64_MIN_VAL && scalar == -1) && !(vector.y == INT64_MIN_VAL && scalar == -1),
                             "Signed vector division overflow");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector2i64 values.
*/
static NEXUS_MAYBE_UNUSED int64 blaze_vector2i64_dot(BVector2i64 a, BVector2i64 b) {
  return (a.x * b.x) + (a.y * b.y);
}

/*
Summary: compute the Euclidean length of a BVector2i64.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector2i64_magnitude(BVector2i64 vector) {
  return blaze_numerics_hypotf64((real64)vector.x, (real64)vector.y);
}

/*
Summary: 2D vector with uint32 components.
*/
typedef struct BVector2u32 {
  uint32 x;
  uint32 y;
} BVector2u32;

/*
Summary: construct a BVector2u32 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector2u32 blaze_vector2u32_make(uint32 x_value, uint32 y_value) {
  BVector2u32 result;

  result.x = x_value;
  result.y = y_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2u32 blaze_vector2u32_add(BVector2u32 a, BVector2u32 b) {
  BVector2u32 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2u32 blaze_vector2u32_subtract(BVector2u32 a, BVector2u32 b) {
  BVector2u32 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2u32 blaze_vector2u32_multiply(BVector2u32 a, BVector2u32 b) {
  BVector2u32 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2u32 blaze_vector2u32_divide(BVector2u32 a, BVector2u32 b) {
  BVector2u32 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2u32 blaze_vector2u32_add_scalar(BVector2u32 vector, uint32 scalar) {
  BVector2u32 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector2u32 blaze_vector2u32_subtract_scalar(BVector2u32 vector, uint32 scalar) {
  BVector2u32 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2u32 blaze_vector2u32_multiply_scalar(BVector2u32 vector, uint32 scalar) {
  BVector2u32 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2u32 blaze_vector2u32_divide_scalar(BVector2u32 vector, uint32 scalar) {
  BVector2u32 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector2u32 values.
*/
static NEXUS_MAYBE_UNUSED uint32 blaze_vector2u32_dot(BVector2u32 a, BVector2u32 b) {
  return (a.x * b.x) + (a.y * b.y);
}

/*
Summary: compute the Euclidean length of a BVector2u32.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector2u32_magnitude(BVector2u32 vector) {
  return blaze_numerics_hypotf64((real64)vector.x, (real64)vector.y);
}

/*
Summary: 2D vector with uint64 components.
*/
typedef struct BVector2u64 {
  uint64 x;
  uint64 y;
} BVector2u64;

/*
Summary: construct a BVector2u64 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector2u64 blaze_vector2u64_make(uint64 x_value, uint64 y_value) {
  BVector2u64 result;

  result.x = x_value;
  result.y = y_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2u64 blaze_vector2u64_add(BVector2u64 a, BVector2u64 b) {
  BVector2u64 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2u64 blaze_vector2u64_subtract(BVector2u64 a, BVector2u64 b) {
  BVector2u64 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector2u64 blaze_vector2u64_multiply(BVector2u64 a, BVector2u64 b) {
  BVector2u64 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2u64 blaze_vector2u64_divide(BVector2u64 a, BVector2u64 b) {
  BVector2u64 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2u64 blaze_vector2u64_add_scalar(BVector2u64 vector, uint64 scalar) {
  BVector2u64 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector2u64 blaze_vector2u64_subtract_scalar(BVector2u64 vector, uint64 scalar) {
  BVector2u64 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector2u64 blaze_vector2u64_multiply_scalar(BVector2u64 vector, uint64 scalar) {
  BVector2u64 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector2u64 blaze_vector2u64_divide_scalar(BVector2u64 vector, uint64 scalar) {
  BVector2u64 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector2u64 values.
*/
static NEXUS_MAYBE_UNUSED uint64 blaze_vector2u64_dot(BVector2u64 a, BVector2u64 b) {
  return (a.x * b.x) + (a.y * b.y);
}

/*
Summary: compute the Euclidean length of a BVector2u64.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector2u64_magnitude(BVector2u64 vector) {
  return blaze_numerics_hypotf64((real64)vector.x, (real64)vector.y);
}

/*
Summary: 3D vector with f_real components.
*/
typedef struct BVector3f {
  f_real x;
  f_real y;
  f_real z;
} BVector3f;

/*
Summary: construct a BVector3f from its components.
*/
static NEXUS_MAYBE_UNUSED BVector3f blaze_vector3f_make(f_real x_value, f_real y_value, f_real z_value) {
  BVector3f result;

  result.x = x_value;
  result.y = y_value;
  result.z = z_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3f blaze_vector3f_add(BVector3f a, BVector3f b) {
  BVector3f result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;
  result.z = a.z + b.z;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3f blaze_vector3f_subtract(BVector3f a, BVector3f b) {
  BVector3f result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;
  result.z = a.z - b.z;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3f blaze_vector3f_multiply(BVector3f a, BVector3f b) {
  BVector3f result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;
  result.z = a.z * b.z;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3f blaze_vector3f_divide(BVector3f a, BVector3f b) {
  BVector3f result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0 && b.z != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;
  result.z = a.z / b.z;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3f blaze_vector3f_add_scalar(BVector3f vector, f_real scalar) {
  BVector3f result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;
  result.z = vector.z + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector3f blaze_vector3f_subtract_scalar(BVector3f vector, f_real scalar) {
  BVector3f result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;
  result.z = vector.z - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3f blaze_vector3f_multiply_scalar(BVector3f vector, f_real scalar) {
  BVector3f result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;
  result.z = vector.z * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3f blaze_vector3f_divide_scalar(BVector3f vector, f_real scalar) {
  BVector3f result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;
  result.z = vector.z / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector3f values.
*/
static NEXUS_MAYBE_UNUSED f_real blaze_vector3f_dot(BVector3f a, BVector3f b) {
  return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z);
}

/*
Summary: compute the Euclidean length of a BVector3f.
*/
static NEXUS_MAYBE_UNUSED f_real blaze_vector3f_magnitude(BVector3f vector) {
  return blaze_numerics_hypot(blaze_numerics_hypot(vector.x, vector.y), vector.z);
}

/*
Summary: 3D vector with real32 components.
*/
typedef struct BVector3f32 {
  real32 x;
  real32 y;
  real32 z;
} BVector3f32;

/*
Summary: construct a BVector3f32 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector3f32 blaze_vector3f32_make(real32 x_value, real32 y_value, real32 z_value) {
  BVector3f32 result;

  result.x = x_value;
  result.y = y_value;
  result.z = z_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3f32 blaze_vector3f32_add(BVector3f32 a, BVector3f32 b) {
  BVector3f32 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;
  result.z = a.z + b.z;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3f32 blaze_vector3f32_subtract(BVector3f32 a, BVector3f32 b) {
  BVector3f32 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;
  result.z = a.z - b.z;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3f32 blaze_vector3f32_multiply(BVector3f32 a, BVector3f32 b) {
  BVector3f32 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;
  result.z = a.z * b.z;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3f32 blaze_vector3f32_divide(BVector3f32 a, BVector3f32 b) {
  BVector3f32 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0 && b.z != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;
  result.z = a.z / b.z;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3f32 blaze_vector3f32_add_scalar(BVector3f32 vector, real32 scalar) {
  BVector3f32 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;
  result.z = vector.z + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector3f32 blaze_vector3f32_subtract_scalar(BVector3f32 vector, real32 scalar) {
  BVector3f32 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;
  result.z = vector.z - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3f32 blaze_vector3f32_multiply_scalar(BVector3f32 vector, real32 scalar) {
  BVector3f32 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;
  result.z = vector.z * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3f32 blaze_vector3f32_divide_scalar(BVector3f32 vector, real32 scalar) {
  BVector3f32 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;
  result.z = vector.z / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector3f32 values.
*/
static NEXUS_MAYBE_UNUSED real32 blaze_vector3f32_dot(BVector3f32 a, BVector3f32 b) {
  return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z);
}

/*
Summary: compute the Euclidean length of a BVector3f32.
*/
static NEXUS_MAYBE_UNUSED real32 blaze_vector3f32_magnitude(BVector3f32 vector) {
  return blaze_numerics_hypotf32(blaze_numerics_hypotf32(vector.x, vector.y), vector.z);
}

/*
Summary: 3D vector with real64 components.
*/
typedef struct BVector3f64 {
  real64 x;
  real64 y;
  real64 z;
} BVector3f64;

/*
Summary: construct a BVector3f64 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_vector3f64_make(real64 x_value, real64 y_value, real64 z_value) {
  BVector3f64 result;

  result.x = x_value;
  result.y = y_value;
  result.z = z_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_vector3f64_add(BVector3f64 a, BVector3f64 b) {
  BVector3f64 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;
  result.z = a.z + b.z;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_vector3f64_subtract(BVector3f64 a, BVector3f64 b) {
  BVector3f64 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;
  result.z = a.z - b.z;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_vector3f64_multiply(BVector3f64 a, BVector3f64 b) {
  BVector3f64 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;
  result.z = a.z * b.z;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_vector3f64_divide(BVector3f64 a, BVector3f64 b) {
  BVector3f64 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0 && b.z != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;
  result.z = a.z / b.z;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_vector3f64_add_scalar(BVector3f64 vector, real64 scalar) {
  BVector3f64 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;
  result.z = vector.z + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_vector3f64_subtract_scalar(BVector3f64 vector, real64 scalar) {
  BVector3f64 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;
  result.z = vector.z - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_vector3f64_multiply_scalar(BVector3f64 vector, real64 scalar) {
  BVector3f64 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;
  result.z = vector.z * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_vector3f64_divide_scalar(BVector3f64 vector, real64 scalar) {
  BVector3f64 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;
  result.z = vector.z / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector3f64 values.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector3f64_dot(BVector3f64 a, BVector3f64 b) {
  return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z);
}

/*
Summary: compute the Euclidean length of a BVector3f64.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector3f64_magnitude(BVector3f64 vector) {
  return blaze_numerics_hypotf64(blaze_numerics_hypotf64(vector.x, vector.y), vector.z);
}

/*
Summary: 3D vector with int32 components.
*/
typedef struct BVector3i32 {
  int32 x;
  int32 y;
  int32 z;
} BVector3i32;

/*
Summary: construct a BVector3i32 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector3i32 blaze_vector3i32_make(int32 x_value, int32 y_value, int32 z_value) {
  BVector3i32 result;

  result.x = x_value;
  result.y = y_value;
  result.z = z_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3i32 blaze_vector3i32_add(BVector3i32 a, BVector3i32 b) {
  BVector3i32 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;
  result.z = a.z + b.z;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3i32 blaze_vector3i32_subtract(BVector3i32 a, BVector3i32 b) {
  BVector3i32 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;
  result.z = a.z - b.z;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3i32 blaze_vector3i32_multiply(BVector3i32 a, BVector3i32 b) {
  BVector3i32 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;
  result.z = a.z * b.z;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
  A minimum signed component divided by -1 also asserts in developer mode.
*/
static NEXUS_MAYBE_UNUSED BVector3i32 blaze_vector3i32_divide(BVector3i32 a, BVector3i32 b) {
  BVector3i32 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0 && b.z != 0, "Vector division by zero");
  NEXUS_PROGRAM_PRECONDITION(!(a.x == INT32_MIN_VAL && b.x == -1) && !(a.y == INT32_MIN_VAL && b.y == -1) && !(a.z == INT32_MIN_VAL && b.z == -1),
                             "Signed vector division overflow");

  result.x = a.x / b.x;
  result.y = a.y / b.y;
  result.z = a.z / b.z;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3i32 blaze_vector3i32_add_scalar(BVector3i32 vector, int32 scalar) {
  BVector3i32 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;
  result.z = vector.z + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector3i32 blaze_vector3i32_subtract_scalar(BVector3i32 vector, int32 scalar) {
  BVector3i32 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;
  result.z = vector.z - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3i32 blaze_vector3i32_multiply_scalar(BVector3i32 vector, int32 scalar) {
  BVector3i32 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;
  result.z = vector.z * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
  A minimum signed component divided by -1 also asserts in developer mode.
*/
static NEXUS_MAYBE_UNUSED BVector3i32 blaze_vector3i32_divide_scalar(BVector3i32 vector, int32 scalar) {
  BVector3i32 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");
  NEXUS_PROGRAM_PRECONDITION(!(vector.x == INT32_MIN_VAL && scalar == -1) && !(vector.y == INT32_MIN_VAL && scalar == -1) &&
                                 !(vector.z == INT32_MIN_VAL && scalar == -1),
                             "Signed vector division overflow");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;
  result.z = vector.z / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector3i32 values.
*/
static NEXUS_MAYBE_UNUSED int32 blaze_vector3i32_dot(BVector3i32 a, BVector3i32 b) {
  return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z);
}

/*
Summary: compute the Euclidean length of a BVector3i32.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector3i32_magnitude(BVector3i32 vector) {
  return blaze_numerics_hypotf64(blaze_numerics_hypotf64((real64)vector.x, (real64)vector.y), (real64)vector.z);
}

/*
Summary: 3D vector with int64 components.
*/
typedef struct BVector3i64 {
  int64 x;
  int64 y;
  int64 z;
} BVector3i64;

/*
Summary: construct a BVector3i64 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector3i64 blaze_vector3i64_make(int64 x_value, int64 y_value, int64 z_value) {
  BVector3i64 result;

  result.x = x_value;
  result.y = y_value;
  result.z = z_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3i64 blaze_vector3i64_add(BVector3i64 a, BVector3i64 b) {
  BVector3i64 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;
  result.z = a.z + b.z;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3i64 blaze_vector3i64_subtract(BVector3i64 a, BVector3i64 b) {
  BVector3i64 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;
  result.z = a.z - b.z;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3i64 blaze_vector3i64_multiply(BVector3i64 a, BVector3i64 b) {
  BVector3i64 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;
  result.z = a.z * b.z;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
  A minimum signed component divided by -1 also asserts in developer mode.
*/
static NEXUS_MAYBE_UNUSED BVector3i64 blaze_vector3i64_divide(BVector3i64 a, BVector3i64 b) {
  BVector3i64 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0 && b.z != 0, "Vector division by zero");
  NEXUS_PROGRAM_PRECONDITION(!(a.x == INT64_MIN_VAL && b.x == -1) && !(a.y == INT64_MIN_VAL && b.y == -1) && !(a.z == INT64_MIN_VAL && b.z == -1),
                             "Signed vector division overflow");

  result.x = a.x / b.x;
  result.y = a.y / b.y;
  result.z = a.z / b.z;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3i64 blaze_vector3i64_add_scalar(BVector3i64 vector, int64 scalar) {
  BVector3i64 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;
  result.z = vector.z + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector3i64 blaze_vector3i64_subtract_scalar(BVector3i64 vector, int64 scalar) {
  BVector3i64 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;
  result.z = vector.z - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3i64 blaze_vector3i64_multiply_scalar(BVector3i64 vector, int64 scalar) {
  BVector3i64 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;
  result.z = vector.z * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
  A minimum signed component divided by -1 also asserts in developer mode.
*/
static NEXUS_MAYBE_UNUSED BVector3i64 blaze_vector3i64_divide_scalar(BVector3i64 vector, int64 scalar) {
  BVector3i64 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");
  NEXUS_PROGRAM_PRECONDITION(!(vector.x == INT64_MIN_VAL && scalar == -1) && !(vector.y == INT64_MIN_VAL && scalar == -1) &&
                                 !(vector.z == INT64_MIN_VAL && scalar == -1),
                             "Signed vector division overflow");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;
  result.z = vector.z / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector3i64 values.
*/
static NEXUS_MAYBE_UNUSED int64 blaze_vector3i64_dot(BVector3i64 a, BVector3i64 b) {
  return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z);
}

/*
Summary: compute the Euclidean length of a BVector3i64.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector3i64_magnitude(BVector3i64 vector) {
  return blaze_numerics_hypotf64(blaze_numerics_hypotf64((real64)vector.x, (real64)vector.y), (real64)vector.z);
}

/*
Summary: 3D vector with uint32 components.
*/
typedef struct BVector3u32 {
  uint32 x;
  uint32 y;
  uint32 z;
} BVector3u32;

/*
Summary: construct a BVector3u32 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector3u32 blaze_vector3u32_make(uint32 x_value, uint32 y_value, uint32 z_value) {
  BVector3u32 result;

  result.x = x_value;
  result.y = y_value;
  result.z = z_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3u32 blaze_vector3u32_add(BVector3u32 a, BVector3u32 b) {
  BVector3u32 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;
  result.z = a.z + b.z;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3u32 blaze_vector3u32_subtract(BVector3u32 a, BVector3u32 b) {
  BVector3u32 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;
  result.z = a.z - b.z;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3u32 blaze_vector3u32_multiply(BVector3u32 a, BVector3u32 b) {
  BVector3u32 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;
  result.z = a.z * b.z;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3u32 blaze_vector3u32_divide(BVector3u32 a, BVector3u32 b) {
  BVector3u32 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0 && b.z != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;
  result.z = a.z / b.z;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3u32 blaze_vector3u32_add_scalar(BVector3u32 vector, uint32 scalar) {
  BVector3u32 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;
  result.z = vector.z + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector3u32 blaze_vector3u32_subtract_scalar(BVector3u32 vector, uint32 scalar) {
  BVector3u32 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;
  result.z = vector.z - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3u32 blaze_vector3u32_multiply_scalar(BVector3u32 vector, uint32 scalar) {
  BVector3u32 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;
  result.z = vector.z * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3u32 blaze_vector3u32_divide_scalar(BVector3u32 vector, uint32 scalar) {
  BVector3u32 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;
  result.z = vector.z / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector3u32 values.
*/
static NEXUS_MAYBE_UNUSED uint32 blaze_vector3u32_dot(BVector3u32 a, BVector3u32 b) {
  return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z);
}

/*
Summary: compute the Euclidean length of a BVector3u32.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector3u32_magnitude(BVector3u32 vector) {
  return blaze_numerics_hypotf64(blaze_numerics_hypotf64((real64)vector.x, (real64)vector.y), (real64)vector.z);
}

/*
Summary: 3D vector with uint64 components.
*/
typedef struct BVector3u64 {
  uint64 x;
  uint64 y;
  uint64 z;
} BVector3u64;

/*
Summary: construct a BVector3u64 from its components.
*/
static NEXUS_MAYBE_UNUSED BVector3u64 blaze_vector3u64_make(uint64 x_value, uint64 y_value, uint64 z_value) {
  BVector3u64 result;

  result.x = x_value;
  result.y = y_value;
  result.z = z_value;

  return result;
}

/*
Summary: add two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3u64 blaze_vector3u64_add(BVector3u64 a, BVector3u64 b) {
  BVector3u64 result;

  result.x = a.x + b.x;
  result.y = a.y + b.y;
  result.z = a.z + b.z;

  return result;
}

/*
Summary: subtract two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3u64 blaze_vector3u64_subtract(BVector3u64 a, BVector3u64 b) {
  BVector3u64 result;

  result.x = a.x - b.x;
  result.y = a.y - b.y;
  result.z = a.z - b.z;

  return result;
}

/*
Summary: multiply two vectors componentwise.
*/
static NEXUS_MAYBE_UNUSED BVector3u64 blaze_vector3u64_multiply(BVector3u64 a, BVector3u64 b) {
  BVector3u64 result;

  result.x = a.x * b.x;
  result.y = a.y * b.y;
  result.z = a.z * b.z;

  return result;
}

/*
Summary: divide two vectors componentwise.
Errors: zero divisor components are programmer errors (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3u64 blaze_vector3u64_divide(BVector3u64 a, BVector3u64 b) {
  BVector3u64 result;

  NEXUS_PROGRAM_PRECONDITION(b.x != 0 && b.y != 0 && b.z != 0, "Vector division by zero");

  result.x = a.x / b.x;
  result.y = a.y / b.y;
  result.z = a.z / b.z;

  return result;
}

/*
Summary: add a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3u64 blaze_vector3u64_add_scalar(BVector3u64 vector, uint64 scalar) {
  BVector3u64 result;

  result.x = vector.x + scalar;
  result.y = vector.y + scalar;
  result.z = vector.z + scalar;

  return result;
}

/*
Summary: subtract a scalar from every vector component.
*/
static NEXUS_MAYBE_UNUSED BVector3u64 blaze_vector3u64_subtract_scalar(BVector3u64 vector, uint64 scalar) {
  BVector3u64 result;

  result.x = vector.x - scalar;
  result.y = vector.y - scalar;
  result.z = vector.z - scalar;

  return result;
}

/*
Summary: multiply a scalar componentwise with a vector.
*/
static NEXUS_MAYBE_UNUSED BVector3u64 blaze_vector3u64_multiply_scalar(BVector3u64 vector, uint64 scalar) {
  BVector3u64 result;

  result.x = vector.x * scalar;
  result.y = vector.y * scalar;
  result.z = vector.z * scalar;

  return result;
}

/*
Summary: divide every vector component by a scalar.
Errors: a zero scalar is a programmer error (asserts in developer mode).
*/
static NEXUS_MAYBE_UNUSED BVector3u64 blaze_vector3u64_divide_scalar(BVector3u64 vector, uint64 scalar) {
  BVector3u64 result;

  NEXUS_PROGRAM_PRECONDITION(scalar != 0, "Vector division by zero");

  result.x = vector.x / scalar;
  result.y = vector.y / scalar;
  result.z = vector.z / scalar;

  return result;
}

/*
Summary: compute the scalar dot product of two BVector3u64 values.
*/
static NEXUS_MAYBE_UNUSED uint64 blaze_vector3u64_dot(BVector3u64 a, BVector3u64 b) {
  return ((a.x * b.x) + (a.y * b.y)) + (a.z * b.z);
}

/*
Summary: compute the Euclidean length of a BVector3u64.
*/
static NEXUS_MAYBE_UNUSED real64 blaze_vector3u64_magnitude(BVector3u64 vector) {
  return blaze_numerics_hypotf64(blaze_numerics_hypotf64((real64)vector.x, (real64)vector.y), (real64)vector.z);
}

/* -------------------------------------------------------------------------- */
/* GEOMETRY                                                                   */
/* -------------------------------------------------------------------------- */

/*
Summary: a 3D position in a unit grid, split into an integer tile and local offset.
Description:
  Each offset is normalized to [0, 1). Absolute tile coordinates never pass
  through floating point, preserving local precision far from the origin.
*/
typedef struct BGridPosition3 {
  BVector3i64 tile;
  BVector3f64 offset;
} BGridPosition3;

/*
Summary: subtract split positions without rounding their absolute coordinates.
Description: nearby displacements retain their local fractional precision.
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_geometry_position3_displacement(const BGridPosition3 *source, const BGridPosition3 *destination) {
  BVector3f64 result;

  result.x = destination->tile.x >= source->tile.x ? (real64)((uint64)destination->tile.x - (uint64)source->tile.x)
                                                   : -(real64)((uint64)source->tile.x - (uint64)destination->tile.x);
  result.y = destination->tile.y >= source->tile.y ? (real64)((uint64)destination->tile.y - (uint64)source->tile.y)
                                                   : -(real64)((uint64)source->tile.y - (uint64)destination->tile.y);
  result.z = destination->tile.z >= source->tile.z ? (real64)((uint64)destination->tile.z - (uint64)source->tile.z)
                                                   : -(real64)((uint64)source->tile.z - (uint64)destination->tile.z);
  result.x += destination->offset.x - source->offset.x;
  result.y += destination->offset.y - source->offset.y;
  result.z += destination->offset.z - source->offset.z;
  return result;
}

/*
Summary: signed axis gaps between two boxes; touching axes have zero gap.
*/
static NEXUS_MAYBE_UNUSED BVector3f64 blaze_geometry_box3_gap(const BGridPosition3 *source, const BVector3i32 *source_size,
                                                              const BGridPosition3 *destination, const BVector3i32 *destination_size) {
  BVector3f64 gap;

  gap = blaze_geometry_position3_displacement(source, destination);
  if (gap.x > source_size->x) {
    gap.x -= source_size->x;
  } else if (gap.x < -(real64)destination_size->x) {
    gap.x += destination_size->x;
  } else {
    gap.x = 0;
  }
  if (gap.y > source_size->y) {
    gap.y -= source_size->y;
  } else if (gap.y < -(real64)destination_size->y) {
    gap.y += destination_size->y;
  } else {
    gap.y = 0;
  }
  if (gap.z > source_size->z) {
    gap.z -= source_size->z;
  } else if (gap.z < -(real64)destination_size->z) {
    gap.z += destination_size->z;
  } else {
    gap.z = 0;
  }
  return gap;
}

/*
Summary: displace a BGridPosition3 and normalize its local offsets.
Description:
  Whole-tile displacement and local fractions are processed separately.
  Negative local coordinates borrow a tile, giving floor-based indexing.
  No allocation or absolute floating-point position conversion is performed.
Params:
  result  output; may alias position; unchanged when FALSE is returned
Returns:
  TRUE on success; FALSE for invalid input offsets, non-finite displacement,
  a displacement outside [-2^63, 2^63), or tile-coordinate overflow.
Ownership: all pointers are borrowed for the duration of the call.
Errors: NULL position, movement, or result is a programmer error (asserts).
Thread-safety: safe concurrently with independent result storage.
*/
extern boolean blaze_geometry_position3_move(const BGridPosition3 *position, const BVector3f64 *movement, BGridPosition3 *result);

/*
Summary: find every unit tile intersected by a half-open 3D box.
Description:
  position is the minimum corner; size gives positive integer dimensions.
  An offset greater than zero adds the partially covered tile at the far face.
  Exactly touching a face does not occupy the tile beyond it.
Params:
  minimum/maximum  distinct outputs; inclusive tile bounds
  tile_count  output; product of the three inclusive tile spans
Returns:
  TRUE on success; FALSE for invalid offsets/dimensions, int64 tile-bound
  overflow, or uint64 count overflow. Outputs remain unchanged on failure.
Ownership: all pointers are borrowed for the duration of the call.
Errors: NULL pointers or aliased minimum/maximum are programmer errors (asserts).
Thread-safety: safe concurrently with independent output storage.
*/
extern boolean blaze_geometry_box3_tiles_get(const BGridPosition3 *position, const BVector3i32 *size, BVector3i64 *minimum, BVector3i64 *maximum,
                                             uint64 *tile_count);

/*
Summary: advance an inclusive integer tile range in X, then Y, then Z order.
Description: initialize tile to minimum; FALSE means the range is exhausted.
Returns: TRUE when tile was advanced; FALSE after the final coordinate.
Ownership: pointers are borrowed; tile must not alias either bound.
Errors: NULL/aliased pointers or a tile outside the bounds assert.
*/
static NEXUS_MAYBE_UNUSED boolean blaze_geometry_tile3_next(BVector3i64 *tile, const BVector3i64 *minimum, const BVector3i64 *maximum) {
  NEXUS_PROGRAM_PRECONDITION(tile != NULL && minimum != NULL && maximum != NULL, "Tile range pointers must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(tile != minimum && tile != maximum, "Tile must not alias its bounds");
  NEXUS_PROGRAM_PRECONDITION(tile->x >= minimum->x && tile->x <= maximum->x && tile->y >= minimum->y && tile->y <= maximum->y &&
                                 tile->z >= minimum->z && tile->z <= maximum->z,
                             "Tile must be inside its inclusive bounds");

  if (tile->x != maximum->x) {
    tile->x++;
    return TRUE;
  }

  tile->x = minimum->x;

  if (tile->y != maximum->y) {
    tile->y++;
    return TRUE;
  }

  tile->y = minimum->y;

  if (tile->z != maximum->z) {
    tile->z++;
    return TRUE;
  }

  return FALSE;
}

/*
Summary: test half-open intervals using integer origins and local offsets.
Description: touching ends are allowed; integer gaps avoid floating absolute coordinates.
Returns: TRUE when the intervals overlap.
Errors: nonpositive sizes or offsets outside [0, 1) assert.
*/
static NEXUS_MAYBE_UNUSED boolean blaze_geometry_interval_overlap(int64 origin, real64 offset, int32 size, int64 other_origin, real64 other_offset,
                                                                  int32 other_size) {
  uint64 gap;

  NEXUS_PROGRAM_PRECONDITION(size > 0 && other_size > 0, "Interval sizes must be positive");
  NEXUS_PROGRAM_PRECONDITION(offset >= 0 && offset < 1 && other_offset >= 0 && other_offset < 1, "Interval offsets must be in [0, 1)");

  if (origin <= other_origin) {
    gap = (uint64)other_origin - (uint64)origin;

    if (gap != (uint64)size) {
      return gap < (uint64)size;
    }

    return other_offset < offset;
  }

  gap = (uint64)origin - (uint64)other_origin;

  if (gap != (uint64)other_size) {
    return gap < (uint64)other_size;
  }

  return offset < other_offset;
}

/*
Summary: test two half-open 3D boxes for overlap.
Description: positions are minimum corners; touching faces are allowed.
Returns: TRUE when all three interval pairs overlap.
Ownership: all pointers are borrowed for the duration of the call.
Errors: NULL pointers, nonpositive dimensions, or invalid offsets assert.
*/
static NEXUS_MAYBE_UNUSED boolean blaze_geometry_box3_overlap(const BGridPosition3 *position, const BVector3i32 *size,
                                                              const BGridPosition3 *other_position, const BVector3i32 *other_size) {
  NEXUS_PROGRAM_PRECONDITION(position != NULL && size != NULL && other_position != NULL && other_size != NULL, "Box pointers must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(size->x > 0 && size->y > 0 && size->z > 0, "Box dimensions must be positive");
  NEXUS_PROGRAM_PRECONDITION(other_size->x > 0 && other_size->y > 0 && other_size->z > 0, "Other box dimensions must be positive");
  NEXUS_PROGRAM_PRECONDITION(position->offset.x >= 0 && position->offset.x < 1 && position->offset.y >= 0 && position->offset.y < 1 &&
                                 position->offset.z >= 0 && position->offset.z < 1,
                             "Box offsets must be in [0, 1)");
  NEXUS_PROGRAM_PRECONDITION(other_position->offset.x >= 0 && other_position->offset.x < 1 && other_position->offset.y >= 0 &&
                                 other_position->offset.y < 1 && other_position->offset.z >= 0 && other_position->offset.z < 1,
                             "Other box offsets must be in [0, 1)");

  return blaze_geometry_interval_overlap(position->tile.x, position->offset.x, size->x, other_position->tile.x, other_position->offset.x,
                                         other_size->x) &&
         blaze_geometry_interval_overlap(position->tile.y, position->offset.y, size->y, other_position->tile.y, other_position->offset.y,
                                         other_size->y) &&
         blaze_geometry_interval_overlap(position->tile.z, position->offset.z, size->z, other_position->tile.z, other_position->offset.z,
                                         other_size->z);
}

/* -------------------------------------------------------------------------- */
/* COLOR                                                                      */
/* -------------------------------------------------------------------------- */

/*
Summary: a CIELAB colour with a caller-selected reference white.
Description: L is lightness, a is the green/red axis, b is the blue/yellow axis. Fixed real64 fields keep the ABI independent of f_real.
Ownership: a plain value owned by the caller.
*/
typedef struct BColorLab {
  real64 l;
  real64 a;
  real64 b;
} BColorLab;

/*
Summary: composites a packed 0xRRGGBBAA sRGB foreground over an opaque background.
Description: source-over in encoded sRGB, rounded to the nearest channel byte; the result is opaque.
Returns: the packed opaque result.
Errors: a non-opaque background asserts.
*/
extern uint32 blaze_color_srgb_composite(uint32 foreground, uint32 background);

/*
Summary: the WCAG relative luminance of packed opaque 0xRRGGBBAA sRGB, in 0..1.
Returns: relative luminance, with the WCAG 2.2 0.04045 transfer breakpoint.
Errors: a non-opaque colour asserts; composite it onto the intended backdrop first.
*/
extern real64 blaze_color_srgb_luminance_get(uint32 color);

/*
Summary: the WCAG contrast ratio of two packed opaque sRGB colours, in 1..21.
Returns: the luminance contrast ratio, independent of argument order.
Errors: a non-opaque colour asserts; composite it onto the intended backdrop first.
*/
extern real64 blaze_color_srgb_wcag_contrast_get(uint32 first, uint32 second);

/*
Summary: APCA 0.0.98G-4g contrast of opaque packed sRGB text over background.
Description: follows the Myndex soft-black clamp, polarity exponents, low clip and offsets. This is a contrast metric, not a font-size conformance test.
Returns: signed Lc, positive for dark text on light, negative for light text on dark.
Errors: a non-opaque colour asserts; composite it onto the intended backdrop first.
*/
extern real64 blaze_color_srgb_apca_contrast_get(uint32 text, uint32 background);

/*
Summary: converts packed opaque sRGB to CIELAB using the D65 reference white.
Params: out  receives the converted colour
Ownership: out is borrowed for the call.
Errors: NULL out or a non-opaque colour asserts.
*/
extern void blaze_color_srgb_lab_get(uint32 color, BColorLab *out);

/*
Summary: CIEDE2000 difference of two CIELAB colours under the same reference white, kL=kC=kH=1.
Description: colour separation, not a measure of text legibility. Implements the zero-chroma and hue-wrap cases from Sharma, Wu and Dalal (2005).
Returns: the nonnegative colour difference.
Ownership: inputs are borrowed for the call.
Errors: NULL inputs or non-finite or out-of-range components assert; L must be 0..100 and a/b -200..200.
*/
extern real64 blaze_color_lab_delta_e_2000_get(const BColorLab *first, const BColorLab *second);

/*
Summary: CIEDE2000 difference of two opaque packed sRGB colours, using D65 and unit weighting factors.
Returns: nonnegative colour difference.
Errors: a non-opaque colour asserts.
*/
extern real64 blaze_color_srgb_delta_e_2000_get(uint32 first, uint32 second);
