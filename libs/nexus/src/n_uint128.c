#include "../nexus.h"

uint128 nexus_uint128_make(uint64 lo, uint64 hi) {
  uint128 value;

  value.lo = lo;
  value.hi = hi;
  return value;
}

uint128 nexus_uint128_zero(void) {
  return nexus_uint128_make(0, 0);
}

uint128 nexus_uint128_add(uint128 left, uint128 right) {
  uint128 result;
  uint64  carry_lo;

  result.lo = left.lo + right.lo;
  carry_lo  = (result.lo < left.lo) ? 1ULL : 0ULL;
  result.hi = left.hi + right.hi + carry_lo;
  return result;
}

uint128 nexus_uint128_xor(uint128 left, uint128 right) {
  uint128 result;

  result.lo = left.lo ^ right.lo;
  result.hi = left.hi ^ right.hi;
  return result;
}

static void p_uint128_mul64_to128(uint64 left, uint64 right, uint64 *out_hi, uint64 *out_lo) {
  uint64 left_lo  = left & 0xffffffffULL;
  uint64 left_hi  = left >> 32;
  uint64 right_lo = right & 0xffffffffULL;
  uint64 right_hi = right >> 32;

  uint64 product0 = left_lo * right_lo;
  uint64 product1 = left_lo * right_hi;
  uint64 product2 = left_hi * right_lo;
  uint64 product3 = left_hi * right_hi;

  uint64 middle = (product0 >> 32) + (product1 & 0xffffffffULL) + (product2 & 0xffffffffULL);

  *out_lo = (product0 & 0xffffffffULL) | (middle << 32);
  *out_hi = product3 + (product1 >> 32) + (product2 >> 32) + (middle >> 32);
}

uint128 nexus_uint128_mul(uint128 left, uint128 right) {
  uint128 result;
  uint64  term_hi;
  uint64  term_lo;

  p_uint128_mul64_to128(left.lo, right.lo, &result.hi, &result.lo);

  p_uint128_mul64_to128(left.lo, right.hi, &term_hi, &term_lo);
  result.hi = result.hi + term_lo;
  (void)term_hi;

  p_uint128_mul64_to128(left.hi, right.lo, &term_hi, &term_lo);
  result.hi = result.hi + term_lo;
  (void)term_hi;

  return result;
}

uint128 nexus_uint128_shift_right(uint128 value, uint32 shift) {
  uint128 result;

  if (shift == 0U) {
    return value;
  }

  if (shift < 64U) {
    result.lo = (value.lo >> shift) | (value.hi << (64U - shift));
    result.hi = value.hi >> shift;
    return result;
  }

  if (shift < 128U) {
    result.lo = value.hi >> (shift - 64U);
    result.hi = 0;
    return result;
  }

  return nexus_uint128_zero();
}

uint64 nexus_uint128_fold_to_uint64(uint128 value) {
  return value.hi ^ value.lo;
}

void nexus_uint128_bytes_little_endian_write(uint128 value, byte out_bytes[16]) {
  uint32 byte_index;

  for (byte_index = 0; byte_index < 8U; byte_index++) {
    out_bytes[byte_index] = (byte)(value.lo >> (byte_index * 8U));
  }
  for (byte_index = 0; byte_index < 8U; byte_index++) {
    out_bytes[byte_index + 8U] = (byte)(value.hi >> (byte_index * 8U));
  }
}

uint128 nexus_uint128_bytes_little_endian_read(const byte in_bytes[16]) {
  uint128 value;
  uint32  byte_index;

  value.lo = 0;
  value.hi = 0;
  for (byte_index = 0; byte_index < 8U; byte_index++) {
    value.lo |= ((uint64)in_bytes[byte_index]) << (byte_index * 8U);
  }
  for (byte_index = 0; byte_index < 8U; byte_index++) {
    value.hi |= ((uint64)in_bytes[byte_index + 8U]) << (byte_index * 8U);
  }
  return value;
}

void nexus_uint128_bytes_big_endian_write(uint128 value, byte out_bytes[16]) {
  uint32 byte_index;

  /* Big-endian: the most significant word (hi) occupies the low byte
     indices, mirroring nexus_uint128_bytes_big_endian_read. */
  for (byte_index = 0; byte_index < 8U; byte_index++) {
    out_bytes[7U - byte_index] = (byte)(value.hi >> (byte_index * 8U));
  }
  for (byte_index = 0; byte_index < 8U; byte_index++) {
    out_bytes[15U - byte_index] = (byte)(value.lo >> (byte_index * 8U));
  }
}

uint128 nexus_uint128_bytes_big_endian_read(const byte in_bytes[16]) {
  uint128 value;
  uint32  byte_index;

  value.lo = 0;
  value.hi = 0;
  for (byte_index = 0; byte_index < 8U; byte_index++) {
    value.hi |= ((uint64)in_bytes[byte_index]) << ((7U - byte_index) * 8U);
  }
  for (byte_index = 0; byte_index < 8U; byte_index++) {
    value.lo |= ((uint64)in_bytes[byte_index + 8U]) << ((7U - byte_index) * 8U);
  }
  return value;
}