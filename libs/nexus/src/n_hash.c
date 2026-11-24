#include "../nexus.h"

#include <xxhash.h>

#define N_HASH_FNV1A32_OFFSET_BASIS 2166136261U
#define N_HASH_FNV1A32_PRIME        16777619U

#define N_HASH_FNV1A64_OFFSET_BASIS 14695981039346656037ULL
#define N_HASH_FNV1A64_PRIME        1099511628211ULL

static uint64 n_hash_rotate_left64(uint64 value, uint32 shift) {
  return (value << shift) | (value >> (64U - shift));
}

static uint64 n_hash_uint64_little_endian_read(const byte *bytes) {
  uint64 value;
  uint32 index;

  value = 0;
  for (index = 0; index < 8U; index++) {
    value |= (uint64)bytes[index] << (index * 8U);
  }
  return value;
}

uint32 nexus_hash_fnv1a32_begin(void) {
  return N_HASH_FNV1A32_OFFSET_BASIS;
}

uint32 nexus_hash_fnv1a32_update(uint32 hash, const void *data, uint_large byte_count) {
  const byte *bytes;
  uint_large  index;

  NEXUS_PROGRAM_PRECONDITION(data != NULL || byte_count == 0, "hash_fnv1a32_update: data is NULL for nonzero byte_count");

  bytes = (const byte *)data;
  for (index = 0; index < byte_count; index++) {
    hash ^= (uint32)bytes[index];
    hash *= N_HASH_FNV1A32_PRIME;
  }
  return hash;
}

uint32 nexus_hash_fnv1a32(const void *data, uint_large byte_count) {
  return nexus_hash_fnv1a32_update(nexus_hash_fnv1a32_begin(), data, byte_count);
}

uint64 nexus_hash_fnv1a64_begin(void) {
  return N_HASH_FNV1A64_OFFSET_BASIS;
}

uint64 nexus_hash_fnv1a64_update(uint64 hash, const void *data, uint_large byte_count) {
  const byte *bytes;
  uint_large  index;

  NEXUS_PROGRAM_PRECONDITION(data != NULL || byte_count == 0, "hash_fnv1a64_update: data is NULL for nonzero byte_count");

  bytes = (const byte *)data;
  for (index = 0; index < byte_count; index++) {
    hash ^= (uint64)bytes[index];
    hash *= N_HASH_FNV1A64_PRIME;
  }
  return hash;
}

uint64 nexus_hash_fnv1a64(const void *data, uint_large byte_count) {
  return nexus_hash_fnv1a64_update(nexus_hash_fnv1a64_begin(), data, byte_count);
}

uint128 nexus_hash_fnv1a128_begin(void) {
  return nexus_uint128_make(0x62b821756295c58dULL, 0x6c62272e07bb0142ULL);
}

uint128 nexus_hash_fnv1a128_update(uint128 hash, const void *data, uint_large byte_count) {
  const byte *bytes;
  uint128     prime;
  uint_large  index;

  NEXUS_PROGRAM_PRECONDITION(data != NULL || byte_count == 0, "hash_fnv1a128_update: data is NULL for nonzero byte_count");

  bytes = (const byte *)data;
  prime = nexus_uint128_make(0x13bULL, 0x0000000001000000ULL);

  for (index = 0; index < byte_count; index++) {
    hash.lo ^= (uint64)bytes[index];
    hash = nexus_uint128_mul(hash, prime);
  }
  return hash;
}

uint128 nexus_hash_fnv1a128(const void *data, uint_large byte_count) {
  return nexus_hash_fnv1a128_update(nexus_hash_fnv1a128_begin(), data, byte_count);
}

#define N_HASH_SIP_ROUND()                                                                                                                           \
  do {                                                                                                                                               \
    v0 += v1;                                                                                                                                        \
    v1 = n_hash_rotate_left64(v1, 13U);                                                                                                              \
    v1 ^= v0;                                                                                                                                        \
    v0 = n_hash_rotate_left64(v0, 32U);                                                                                                              \
    v2 += v3;                                                                                                                                        \
    v3 = n_hash_rotate_left64(v3, 16U);                                                                                                              \
    v3 ^= v2;                                                                                                                                        \
    v0 += v3;                                                                                                                                        \
    v3 = n_hash_rotate_left64(v3, 21U);                                                                                                              \
    v3 ^= v0;                                                                                                                                        \
    v2 += v1;                                                                                                                                        \
    v1 = n_hash_rotate_left64(v1, 17U);                                                                                                              \
    v1 ^= v2;                                                                                                                                        \
    v2 = n_hash_rotate_left64(v2, 32U);                                                                                                              \
  } while (0)

uint64 nexus_hash_siphash24(const void *data, uint_large byte_count, uint128 key) {
  const byte *bytes;
  uint_large  offset;
  uint64      final_block;
  uint64      message;
  uint64      v0; /* NOLINT(readability-identifier-length) */
  uint64      v1; /* NOLINT(readability-identifier-length) */
  uint64      v2; /* NOLINT(readability-identifier-length) */
  uint64      v3; /* NOLINT(readability-identifier-length) */

  NEXUS_PROGRAM_PRECONDITION(data != NULL || byte_count == 0, "hash_siphash24: data is NULL for nonzero byte_count");

  bytes = (const byte *)data;
  v0    = 0x736f6d6570736575ULL ^ key.lo;
  v1    = 0x646f72616e646f6dULL ^ key.hi;
  v2    = 0x6c7967656e657261ULL ^ key.lo;
  v3    = 0x7465646279746573ULL ^ key.hi;

  offset = 0;
  while (byte_count - offset >= 8U) {
    message = n_hash_uint64_little_endian_read(bytes + offset);
    v3 ^= message;
    N_HASH_SIP_ROUND();
    N_HASH_SIP_ROUND();
    v0 ^= message;
    offset += 8U;
  }

  final_block = (uint64)byte_count << 56U;
  switch (byte_count - offset) {
  case 7:
    final_block |= (uint64)bytes[offset + 6U] << 48U;
  case 6: /* fall through */
    final_block |= (uint64)bytes[offset + 5U] << 40U;
  case 5: /* fall through */
    final_block |= (uint64)bytes[offset + 4U] << 32U;
  case 4: /* fall through */
    final_block |= (uint64)bytes[offset + 3U] << 24U;
  case 3: /* fall through */
    final_block |= (uint64)bytes[offset + 2U] << 16U;
  case 2: /* fall through */
    final_block |= (uint64)bytes[offset + 1U] << 8U;
  case 1: /* fall through */
    final_block |= (uint64)bytes[offset];
  default:
    break;
  }

  v3 ^= final_block;
  N_HASH_SIP_ROUND();
  N_HASH_SIP_ROUND();
  v0 ^= final_block;
  v2 ^= 0xffU;
  N_HASH_SIP_ROUND();
  N_HASH_SIP_ROUND();
  N_HASH_SIP_ROUND();
  N_HASH_SIP_ROUND();

  return v0 ^ v1 ^ v2 ^ v3;
}

#undef N_HASH_SIP_ROUND

uint64 nexus_hash_xxh3_64(const void *data, uint_large byte_count) {
  static const byte empty = 0;

  NEXUS_PROGRAM_PRECONDITION(data != NULL || byte_count == 0, "hash_xxh3_64: data is NULL for nonzero byte_count");
  return (uint64)XXH3_64bits(data != NULL ? data : &empty, (size_t)byte_count);
}

uint64 nexus_hash_xxh3_64_seeded(const void *data, uint_large byte_count, uint64 seed) {
  static const byte empty = 0;

  NEXUS_PROGRAM_PRECONDITION(data != NULL || byte_count == 0, "hash_xxh3_64_seeded: data is NULL for nonzero byte_count");
  return (uint64)XXH3_64bits_withSeed(data != NULL ? data : &empty, (size_t)byte_count, (XXH64_hash_t)seed);
}

uint128 nexus_hash_xxh3_128(const void *data, uint_large byte_count) {
  static const byte empty = 0;
  XXH128_hash_t     hash;

  NEXUS_PROGRAM_PRECONDITION(data != NULL || byte_count == 0, "hash_xxh3_128: data is NULL for nonzero byte_count");

  hash = XXH3_128bits(data != NULL ? data : &empty, (size_t)byte_count);
  return nexus_uint128_make((uint64)hash.low64, (uint64)hash.high64);
}

uint128 nexus_hash_xxh3_128_seeded(const void *data, uint_large byte_count, uint64 seed) {
  static const byte empty = 0;
  XXH128_hash_t     hash;

  NEXUS_PROGRAM_PRECONDITION(data != NULL || byte_count == 0, "hash_xxh3_128_seeded: data is NULL for nonzero byte_count");

  hash = XXH3_128bits_withSeed(data != NULL ? data : &empty, (size_t)byte_count, (XXH64_hash_t)seed);
  return nexus_uint128_make((uint64)hash.low64, (uint64)hash.high64);
}
