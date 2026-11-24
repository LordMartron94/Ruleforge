/* The Windows headers use memcpy/memset, which nexus.h poisons, so they come first. */
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#endif

#include "../nexus.h"

#include <stddef.h>

#if NEXUS_PLATFORM_WINDOWS
#elif NEXUS_PLATFORM_POSIX || NEXUS_PLATFORM_BSD
#  include <pthread.h>
#  include <unistd.h>
#endif

#define N_IDENTITY_UUID_BYTE_COUNT       16U
#define N_IDENTITY_UUID_VARIANT_MASK     0x3fU
#define N_IDENTITY_UUID_RFC_VARIANT      0x80U
#define N_IDENTITY_UUID_GREGORIAN_OFFSET 122192928000000000ULL
#define N_IDENTITY_UUID_V7_TIMESTAMP_MAX 0xffffffffffffULL

static uint32 n_identity_rotate_left(uint32 value, uint32 amount) {
  return (value << amount) | (value >> (32U - amount));
}

static uint32 n_identity_uint32_little_endian_read(const byte *bytes) {
  return (uint32)bytes[0] | ((uint32)bytes[1] << 8U) | ((uint32)bytes[2] << 16U) | ((uint32)bytes[3] << 24U);
}

static uint32 n_identity_uint32_big_endian_read(const byte *bytes) {
  return ((uint32)bytes[0] << 24U) | ((uint32)bytes[1] << 16U) | ((uint32)bytes[2] << 8U) | (uint32)bytes[3];
}

static void n_identity_uint32_little_endian_write(uint32 value, byte *bytes) {
  bytes[0] = (byte)value;
  bytes[1] = (byte)(value >> 8U);
  bytes[2] = (byte)(value >> 16U);
  bytes[3] = (byte)(value >> 24U);
}

static void n_identity_uint32_big_endian_write(uint32 value, byte *bytes) {
  bytes[0] = (byte)(value >> 24U);
  bytes[1] = (byte)(value >> 16U);
  bytes[2] = (byte)(value >> 8U);
  bytes[3] = (byte)value;
}

/* The UUID name algorithms are kept private: callers only need the UUID API,
   while this translation unit owns their fixed digest implementations. */
typedef struct NIdentityMD5 {
  uint32 state[4];
  uint64 byte_count;
  byte   block[64];
  uint32 block_size;
} NIdentityMD5;

static void n_identity_md5_transform(NIdentityMD5 *context, const byte block[64]) {
  static const uint32 shifts[64]    = {7U,  12U, 17U, 22U, 7U,  12U, 17U, 22U, 7U,  12U, 17U, 22U, 7U,  12U, 17U, 22U, 5U,  9U,  14U, 20U, 5U,  9U,
                                       14U, 20U, 5U,  9U,  14U, 20U, 5U,  9U,  14U, 20U, 4U,  11U, 16U, 23U, 4U,  11U, 16U, 23U, 4U,  11U, 16U, 23U,
                                       4U,  11U, 16U, 23U, 6U,  10U, 15U, 21U, 6U,  10U, 15U, 21U, 6U,  10U, 15U, 21U, 6U,  10U, 15U, 21U};
  static const uint32 constants[64] = {
      0xd76aa478U, 0xe8c7b756U, 0x242070dbU, 0xc1bdceeeU, 0xf57c0fafU, 0x4787c62aU, 0xa8304613U, 0xfd469501U, 0x698098d8U, 0x8b44f7afU, 0xffff5bb1U,
      0x895cd7beU, 0x6b901122U, 0xfd987193U, 0xa679438eU, 0x49b40821U, 0xf61e2562U, 0xc040b340U, 0x265e5a51U, 0xe9b6c7aaU, 0xd62f105dU, 0x02441453U,
      0xd8a1e681U, 0xe7d3fbc8U, 0x21e1cde6U, 0xc33707d6U, 0xf4d50d87U, 0x455a14edU, 0xa9e3e905U, 0xfcefa3f8U, 0x676f02d9U, 0x8d2a4c8aU, 0xfffa3942U,
      0x8771f681U, 0x6d9d6122U, 0xfde5380cU, 0xa4beea44U, 0x4bdecfa9U, 0xf6bb4b60U, 0xbebfbc70U, 0x289b7ec6U, 0xeaa127faU, 0xd4ef3085U, 0x04881d05U,
      0xd9d4d039U, 0xe6db99e5U, 0x1fa27cf8U, 0xc4ac5665U, 0xf4292244U, 0x432aff97U, 0xab9423a7U, 0xfc93a039U, 0x655b59c3U, 0x8f0ccc92U, 0xffeff47dU,
      0x85845dd1U, 0x6fa87e4fU, 0xfe2ce6e0U, 0xa3014314U, 0x4e0811a1U, 0xf7537e82U, 0xbd3af235U, 0x2ad7d2bbU, 0xeb86d391U};
  uint32 words[16];
  uint32 a;
  uint32 b;
  uint32 c; /* NOLINT(readability-identifier-length) */
  uint32 d; /* NOLINT(readability-identifier-length) */
  uint32 f; /* NOLINT(readability-identifier-length) */
  uint32 g; /* NOLINT(readability-identifier-length) */
  uint32 temporary;
  uint32 index;

  for (index = 0; index < 16U; index++) {
    words[index] = n_identity_uint32_little_endian_read(&block[(size_t)index * 4U]);
  }

  a = context->state[0];
  b = context->state[1];
  c = context->state[2];
  d = context->state[3];

  for (index = 0; index < 64U; index++) {
    if (index < 16U) {
      f = (b & c) | (~b & d);
      g = index;
    } else if (index < 32U) {
      f = (d & b) | (~d & c);
      g = ((5U * index) + 1U) & 15U;
    } else if (index < 48U) {
      f = b ^ c ^ d;
      g = ((3U * index) + 5U) & 15U;
    } else {
      f = c ^ (b | ~d);
      g = (7U * index) & 15U;
    }
    temporary = d;
    d         = c;
    c         = b;
    b += n_identity_rotate_left(a + f + constants[index] + words[g], shifts[index]);
    a = temporary;
  }

  context->state[0] += a;
  context->state[1] += b;
  context->state[2] += c;
  context->state[3] += d;
}

static void n_identity_md5_begin(NIdentityMD5 *context) {
  context->state[0]   = 0x67452301U;
  context->state[1]   = 0xefcdab89U;
  context->state[2]   = 0x98badcfeU;
  context->state[3]   = 0x10325476U;
  context->byte_count = 0;
  context->block_size = 0;
}

static void n_identity_md5_update(NIdentityMD5 *context, const void *data, uint_large size) {
  const byte *bytes;
  uint_large  offset;
  uint_large  copy_count;

  bytes  = (const byte *)data;
  offset = 0;
  context->byte_count += size;
  while (offset < size) {
    copy_count = 64U - context->block_size;
    if (copy_count > size - offset) {
      copy_count = size - offset;
    }
    nexus_memory_bytes_copy(bytes + offset, context->block + context->block_size, copy_count);
    context->block_size += (uint32)copy_count;
    offset += copy_count;
    if (context->block_size == 64U) {
      n_identity_md5_transform(context, context->block);
      context->block_size = 0;
    }
  }
}

static void n_identity_md5_end(NIdentityMD5 *context, byte digest[16]) {
  uint64 bit_count;
  uint32 index;

  bit_count                             = context->byte_count * 8ULL;
  context->block[context->block_size++] = 0x80U;
  while (context->block_size != 56U) {
    if (context->block_size == 64U) {
      n_identity_md5_transform(context, context->block);
      context->block_size = 0;
    }
    context->block[context->block_size++] = 0;
  }
  for (index = 0; index < 8U; index++) {
    context->block[56U + index] = (byte)(bit_count >> (index * 8U));
  }
  n_identity_md5_transform(context, context->block);
  for (index = 0; index < 4U; index++) {
    n_identity_uint32_little_endian_write(context->state[index], &digest[(size_t)index * 4U]);
  }
}

typedef struct NIdentitySHA1 {
  uint32 state[5];
  uint64 byte_count;
  byte   block[64];
  uint32 block_size;
} NIdentitySHA1;

static void n_identity_sha1_transform(NIdentitySHA1 *context, const byte block[64]) {
  uint32 words[80];
  uint32 a;
  uint32 b;
  uint32 c; /* NOLINT(readability-identifier-length) */
  uint32 d; /* NOLINT(readability-identifier-length) */
  uint32 e; /* NOLINT(readability-identifier-length) */
  uint32 f; /* NOLINT(readability-identifier-length) */
  uint32 k;
  uint32 temporary;
  uint32 index;

  for (index = 0; index < 16U; index++) {
    words[index] = n_identity_uint32_big_endian_read(&block[(size_t)index * 4U]);
  }
  for (index = 16U; index < 80U; index++) {
    words[index] = n_identity_rotate_left(words[index - 3U] ^ words[index - 8U] ^ words[index - 14U] ^ words[index - 16U], 1U);
  }
  a = context->state[0];
  b = context->state[1];
  c = context->state[2];
  d = context->state[3];
  e = context->state[4];
  for (index = 0; index < 80U; index++) {
    if (index < 20U) {
      f = (b & c) | (~b & d);
      k = 0x5a827999U;
    } else if (index < 40U) {
      f = b ^ c ^ d;
      k = 0x6ed9eba1U;
    } else if (index < 60U) {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8f1bbcdcU;
    } else {
      f = b ^ c ^ d;
      k = 0xca62c1d6U;
    }
    temporary = n_identity_rotate_left(a, 5U) + f + e + k + words[index];
    e         = d;
    d         = c;
    c         = n_identity_rotate_left(b, 30U);
    b         = a;
    a         = temporary;
  }
  context->state[0] += a;
  context->state[1] += b;
  context->state[2] += c;
  context->state[3] += d;
  context->state[4] += e;
}

static void n_identity_sha1_begin(NIdentitySHA1 *context) {
  context->state[0]   = 0x67452301U;
  context->state[1]   = 0xefcdab89U;
  context->state[2]   = 0x98badcfeU;
  context->state[3]   = 0x10325476U;
  context->state[4]   = 0xc3d2e1f0U;
  context->byte_count = 0;
  context->block_size = 0;
}

static void n_identity_sha1_update(NIdentitySHA1 *context, const void *data, uint_large size) {
  const byte *bytes;
  uint_large  offset;
  uint_large  copy_count;

  bytes  = (const byte *)data;
  offset = 0;
  context->byte_count += size;
  while (offset < size) {
    copy_count = 64U - context->block_size;
    if (copy_count > size - offset) {
      copy_count = size - offset;
    }
    nexus_memory_bytes_copy(bytes + offset, context->block + context->block_size, copy_count);
    context->block_size += (uint32)copy_count;
    offset += copy_count;
    if (context->block_size == 64U) {
      n_identity_sha1_transform(context, context->block);
      context->block_size = 0;
    }
  }
}

static void n_identity_sha1_end(NIdentitySHA1 *context, byte digest[20]) {
  uint64 bit_count;
  uint32 index;

  bit_count                             = context->byte_count * 8ULL;
  context->block[context->block_size++] = 0x80U;
  while (context->block_size != 56U) {
    if (context->block_size == 64U) {
      n_identity_sha1_transform(context, context->block);
      context->block_size = 0;
    }
    context->block[context->block_size++] = 0;
  }
  for (index = 0; index < 8U; index++) {
    context->block[56U + index] = (byte)(bit_count >> ((7U - index) * 8U));
  }
  n_identity_sha1_transform(context, context->block);
  for (index = 0; index < 5U; index++) {
    n_identity_uint32_big_endian_write(context->state[index], &digest[(size_t)index * 4U]);
  }
}

static void n_identity_uuid_digest(NexusUUID namespace_uuid, const void *name, uint_large name_size, byte digest[20], boolean sha1) {
  byte namespace_bytes[16];

  nexus_uint128_bytes_big_endian_write(namespace_uuid, namespace_bytes);
  if (sha1) {
    NIdentitySHA1 context;
    n_identity_sha1_begin(&context);
    n_identity_sha1_update(&context, namespace_bytes, 16U);
    n_identity_sha1_update(&context, name, name_size);
    n_identity_sha1_end(&context, digest);
  } else {
    NIdentityMD5 context;
    n_identity_md5_begin(&context);
    n_identity_md5_update(&context, namespace_bytes, 16U);
    n_identity_md5_update(&context, name, name_size);
    n_identity_md5_end(&context, digest);
  }
}

static void n_identity_uuid_variant_version_set(byte bytes[16], byte version) {
  bytes[6] = (byte)((bytes[6] & 0x0fU) | (version << 4U));
  bytes[8] = (byte)((bytes[8] & N_IDENTITY_UUID_VARIANT_MASK) | N_IDENTITY_UUID_RFC_VARIANT);
}

static byte n_identity_hex_value(char character) {
  if (character >= '0' && character <= '9') {
    return (byte)(character - '0');
  }
  if (character >= 'a' && character <= 'f') {
    return (byte)(character - 'a' + 10);
  }
  if (character >= 'A' && character <= 'F') {
    return (byte)(character - 'A' + 10);
  }
  return 0xffU;
}

NexusUUID nexus_identity_uuid_nil(void) {
  return nexus_uint128_zero();
}

NexusUUID nexus_identity_uuid_max(void) {
  return nexus_uint128_make(UINT64_MAX_VAL, UINT64_MAX_VAL);
}

void nexus_identity_uuid_bytes_write(NexusUUID uuid, byte out_bytes[16]) {
  NEXUS_PROGRAM_PRECONDITION(out_bytes != NULL, "uuid_bytes_write: out_bytes is NULL");
  nexus_uint128_bytes_big_endian_write(uuid, out_bytes);
}

NexusUUID nexus_identity_uuid_bytes_read(const byte bytes[16]) {
  NEXUS_PROGRAM_PRECONDITION(bytes != NULL, "uuid_bytes_read: bytes is NULL");
  return nexus_uint128_bytes_big_endian_read(bytes);
}

void nexus_identity_uuid_format(NexusUUID uuid, char out_string[NEXUS_UUID_STRING_BUFFER_SIZE]) {
  static const char hexadecimal[] = "0123456789abcdef";
  byte              bytes[16];
  uint32            byte_index;
  uint32            string_index;

  NEXUS_PROGRAM_PRECONDITION(out_string != NULL, "uuid_format: out_string is NULL");
  nexus_identity_uuid_bytes_write(uuid, bytes);
  string_index = 0;
  for (byte_index = 0; byte_index < 16U; byte_index++) {
    if (byte_index == 4U || byte_index == 6U || byte_index == 8U || byte_index == 10U) {
      out_string[string_index++] = '-';
    }
    out_string[string_index++] = hexadecimal[bytes[byte_index] >> 4U];
    out_string[string_index++] = hexadecimal[bytes[byte_index] & 0x0fU];
  }
  out_string[string_index] = '\0';
}

NexusError *nexus_identity_uuid_parse(const char *string, NexusUUID *out_uuid) {
  byte   bytes[16];
  byte   high;
  byte   low;
  uint32 byte_index;
  uint32 string_index;
  uint64 string_length;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_identity_uuid_parse");
  NEXUS_PROGRAM_PRECONDITION(string != NULL, "uuid_parse: string is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_parse: out_uuid is NULL");
  string_length = nexus_strings_string_length_get(string);
  if (string_length != NEXUS_UUID_STRING_LENGTH) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_IDENTITY_ERROR_INVALID_UUID, "UUID has an invalid length");
  }
  for (string_index = 0, byte_index = 0; string_index < 36U;) {
    if (string_index == 8U || string_index == 13U || string_index == 18U || string_index == 23U) {
      if (string[string_index] != '-') {
        NEXUS_PROGRAM_CONTEXT_POP();
        return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_IDENTITY_ERROR_INVALID_UUID, "UUID has an invalid separator");
      }
      string_index++;
      continue;
    }
    high = n_identity_hex_value(string[string_index]);
    low  = n_identity_hex_value(string[string_index + 1U]);
    if (high == 0xffU || low == 0xffU) {
      NEXUS_PROGRAM_CONTEXT_POP();
      return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_IDENTITY_ERROR_INVALID_UUID, "UUID has a non-hexadecimal digit");
    }
    bytes[byte_index++] = (byte)((high << 4U) | low);
    string_index += 2U;
  }
  *out_uuid = nexus_identity_uuid_bytes_read(bytes);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

uint8 nexus_identity_uuid_version_get(NexusUUID uuid) {
  byte bytes[16];
  nexus_identity_uuid_bytes_write(uuid, bytes);
  return (uint8)(bytes[6] >> 4U);
}

typedef struct NIdentityTimeState {
  boolean initialized;
  uint64  last_timestamp;
  uint16  clock_sequence;
  uint64  node;
} NIdentityTimeState;

static NIdentityTimeState n_identity_time_state = {FALSE, 0, 0, 0};

#if NEXUS_PLATFORM_WINDOWS
static SRWLOCK n_identity_time_mutex = SRWLOCK_INIT;
#  define N_IDENTITY_LOCK()   AcquireSRWLockExclusive(&n_identity_time_mutex)
#  define N_IDENTITY_UNLOCK() ReleaseSRWLockExclusive(&n_identity_time_mutex)
#elif NEXUS_PLATFORM_POSIX || NEXUS_PLATFORM_BSD
static pthread_mutex_t n_identity_time_mutex = PTHREAD_MUTEX_INITIALIZER;
#  define N_IDENTITY_LOCK()   pthread_mutex_lock(&n_identity_time_mutex)
#  define N_IDENTITY_UNLOCK() pthread_mutex_unlock(&n_identity_time_mutex)
#else
#  define N_IDENTITY_LOCK()   ((void)0)
#  define N_IDENTITY_UNLOCK() ((void)0)
#endif

static NexusError *n_identity_time_next(uint64 *out_timestamp, uint16 *out_sequence, uint64 *out_node) {
  byte              random_bytes[8];
  NexusError       *error;
  NexusRandomSource source;
  uint64            now;

  NEXUS_PROGRAM_CONTEXT_PUSH("n_identity_time_next");
  NEXUS_PROGRAM_PRECONDITION(out_timestamp != NULL, "time_next: out_timestamp is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_sequence != NULL, "time_next: out_sequence is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_node != NULL, "time_next: out_node is NULL");

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_time_get_real");
  now = nexus_time_get_real().time;
  NEXUS_PROGRAM_CONTEXT_POP();
  if (now > UINT64_MAX_VAL - N_IDENTITY_UUID_GREGORIAN_OFFSET || (now / 100U) + N_IDENTITY_UUID_GREGORIAN_OFFSET > UINT64_MAX_VAL) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_IDENTITY_ERROR_TIMESTAMP_RANGE, "UUID timestamp is out of range");
  }
  now = (now / 100U) + N_IDENTITY_UUID_GREGORIAN_OFFSET;

  N_IDENTITY_LOCK();
  if (!n_identity_time_state.initialized) {
    source = nexus_random_system_source_get();
    error  = nexus_random_source_fill(source, random_bytes, sizeof(random_bytes));
    if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
      N_IDENTITY_UNLOCK();
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
    n_identity_time_state.clock_sequence = (uint16)(n_identity_uint32_little_endian_read(random_bytes) & 0x3fffU);
    n_identity_time_state.node           = (((uint64)random_bytes[2] << 40U) | ((uint64)random_bytes[3] << 32U) | ((uint64)random_bytes[4] << 24U) |
                                  ((uint64)random_bytes[5] << 16U) | ((uint64)random_bytes[6] << 8U) | (uint64)random_bytes[7]) &
                                 0xffffffffffffULL;
    n_identity_time_state.node |= 0x010000000000ULL;
    n_identity_time_state.initialized = TRUE;
  }
  if (now <= n_identity_time_state.last_timestamp) {
    if (n_identity_time_state.last_timestamp == UINT64_MAX_VAL) {
      N_IDENTITY_UNLOCK();
      NEXUS_PROGRAM_CONTEXT_POP();
      return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_IDENTITY_ERROR_SEQUENCE_EXHAUSTED, "UUID timestamp sequence exhausted");
    }
    n_identity_time_state.clock_sequence = (uint16)((n_identity_time_state.clock_sequence + 1U) & 0x3fffU);
    now                                  = n_identity_time_state.last_timestamp + 1U;
  }
  n_identity_time_state.last_timestamp = now;
  *out_timestamp                       = now;
  *out_sequence                        = n_identity_time_state.clock_sequence;
  *out_node                            = n_identity_time_state.node;
  N_IDENTITY_UNLOCK();
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void nexus_identity_uuid_v1_build(uint64 timestamp_value, uint16 clock_sequence, uint64 node, NexusUUID *out_uuid) {
  byte bytes[16];
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_v1_build: out_uuid is NULL");
  NEXUS_PROGRAM_PRECONDITION((clock_sequence & 0xc000U) == 0, "uuid_v1_build: clock_sequence exceeds 14 bits");
  NEXUS_PROGRAM_PRECONDITION((node & ~0xffffffffffffULL) == 0, "uuid_v1_build: node exceeds 48 bits");
  bytes[0]  = (byte)(timestamp_value >> 24U);
  bytes[1]  = (byte)(timestamp_value >> 16U);
  bytes[2]  = (byte)(timestamp_value >> 8U);
  bytes[3]  = (byte)timestamp_value;
  bytes[4]  = (byte)(timestamp_value >> 40U);
  bytes[5]  = (byte)(timestamp_value >> 32U);
  bytes[6]  = (byte)(0x10U | ((timestamp_value >> 56U) & 0x0fU));
  bytes[7]  = (byte)(timestamp_value >> 48U);
  bytes[8]  = (byte)((clock_sequence >> 8U) | 0x80U);
  bytes[9]  = (byte)clock_sequence;
  bytes[10] = (byte)(node >> 40U);
  bytes[11] = (byte)(node >> 32U);
  bytes[12] = (byte)(node >> 24U);
  bytes[13] = (byte)(node >> 16U);
  bytes[14] = (byte)(node >> 8U);
  bytes[15] = (byte)node;
  *out_uuid = nexus_identity_uuid_bytes_read(bytes);
}

void nexus_identity_uuid_v6_build(uint64 timestamp_value, uint16 clock_sequence, uint64 node, NexusUUID *out_uuid) {
  byte bytes[16];
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_v6_build: out_uuid is NULL");
  NEXUS_PROGRAM_PRECONDITION((clock_sequence & 0xc000U) == 0, "uuid_v6_build: clock_sequence exceeds 14 bits");
  NEXUS_PROGRAM_PRECONDITION((node & ~0xffffffffffffULL) == 0, "uuid_v6_build: node exceeds 48 bits");
  bytes[0]  = (byte)(timestamp_value >> 52U);
  bytes[1]  = (byte)(timestamp_value >> 44U);
  bytes[2]  = (byte)(timestamp_value >> 36U);
  bytes[3]  = (byte)(timestamp_value >> 28U);
  bytes[4]  = (byte)(timestamp_value >> 20U);
  bytes[5]  = (byte)(timestamp_value >> 12U);
  bytes[6]  = (byte)(0x60U | (timestamp_value >> 8U & 0x0fU));
  bytes[7]  = (byte)timestamp_value;
  bytes[8]  = (byte)((clock_sequence >> 8U) | 0x80U);
  bytes[9]  = (byte)clock_sequence;
  bytes[10] = (byte)(node >> 40U);
  bytes[11] = (byte)(node >> 32U);
  bytes[12] = (byte)(node >> 24U);
  bytes[13] = (byte)(node >> 16U);
  bytes[14] = (byte)(node >> 8U);
  bytes[15] = (byte)node;
  *out_uuid = nexus_identity_uuid_bytes_read(bytes);
}

NexusError *nexus_identity_uuid_v1_generate(NexusUUID *out_uuid) {
  uint64      timestamp_value;
  uint16      clock_sequence;
  uint64      node;
  NexusError *error;
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_identity_uuid_v1_generate");
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_v1_generate: out_uuid is NULL");
  error = n_identity_time_next(&timestamp_value, &clock_sequence, &node);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  nexus_identity_uuid_v1_build(timestamp_value, clock_sequence, node, out_uuid);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_identity_uuid_v6_generate(NexusUUID *out_uuid) {
  uint64      timestamp_value;
  uint16      clock_sequence;
  uint64      node;
  NexusError *error;
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_identity_uuid_v6_generate");
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_v6_generate: out_uuid is NULL");
  error = n_identity_time_next(&timestamp_value, &clock_sequence, &node);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  nexus_identity_uuid_v6_build(timestamp_value, clock_sequence, node, out_uuid);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void nexus_identity_uuid_v3_generate(NexusUUID namespace_uuid, const void *name, uint_large name_size, NexusUUID *out_uuid) {
  byte digest[20];
  byte bytes[16];
  NEXUS_PROGRAM_PRECONDITION(name != NULL || name_size == 0, "uuid_v3_generate: name is NULL for nonzero size");
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_v3_generate: out_uuid is NULL");
  n_identity_uuid_digest(namespace_uuid, name, name_size, digest, FALSE);
  nexus_memory_bytes_copy(digest, bytes, 16U);
  n_identity_uuid_variant_version_set(bytes, 3U);
  *out_uuid = nexus_identity_uuid_bytes_read(bytes);
}

void nexus_identity_uuid_v5_generate(NexusUUID namespace_uuid, const void *name, uint_large name_size, NexusUUID *out_uuid) {
  byte digest[20];
  byte bytes[16];
  NEXUS_PROGRAM_PRECONDITION(name != NULL || name_size == 0, "uuid_v5_generate: name is NULL for nonzero size");
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_v5_generate: out_uuid is NULL");
  n_identity_uuid_digest(namespace_uuid, name, name_size, digest, TRUE);
  nexus_memory_bytes_copy(digest, bytes, 16U);
  n_identity_uuid_variant_version_set(bytes, 5U);
  *out_uuid = nexus_identity_uuid_bytes_read(bytes);
}

NexusError *nexus_identity_uuid_v4_generate_with_random(NexusRandomSource source, NexusUUID *out_uuid) {
  byte        bytes[16];
  NexusError *error;
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_identity_uuid_v4_generate_with_random");
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_v4_generate: out_uuid is NULL");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "uuid_v4_generate: source.fill is NULL");
  error = nexus_random_source_fill(source, bytes, 16U);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  n_identity_uuid_variant_version_set(bytes, 4U);
  *out_uuid = nexus_identity_uuid_bytes_read(bytes);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_identity_uuid_v4_generate(NexusUUID *out_uuid) {
  return nexus_identity_uuid_v4_generate_with_random(nexus_random_system_source_get(), out_uuid);
}

static NexusError *n_identity_uuid_v7_timestamp_get(uint64 *out_milliseconds) {
  uint64 milliseconds;
  NEXUS_PROGRAM_PRECONDITION(out_milliseconds != NULL, "uuid_v7_timestamp: out_milliseconds is NULL");
  milliseconds = nexus_time_get_real().time / NEXUS_NANOSECONDS_PER_MILLISECOND;
  if (milliseconds > N_IDENTITY_UUID_V7_TIMESTAMP_MAX) {
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_IDENTITY_ERROR_TIMESTAMP_RANGE, "UUIDv7 timestamp exceeds 48 bits");
  }
  *out_milliseconds = milliseconds;
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

static void n_identity_uuid_v7_timestamp_write(byte bytes[16], uint64 milliseconds) {
  bytes[0] = (byte)(milliseconds >> 40U);
  bytes[1] = (byte)(milliseconds >> 32U);
  bytes[2] = (byte)(milliseconds >> 24U);
  bytes[3] = (byte)(milliseconds >> 16U);
  bytes[4] = (byte)(milliseconds >> 8U);
  bytes[5] = (byte)milliseconds;
}

NexusError *nexus_identity_uuid_v7_generate_with_random(NexusRandomSource source, NexusUUID *out_uuid) {
  byte        bytes[16];
  uint64      milliseconds;
  NexusError *error;
  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_identity_uuid_v7_generate_with_random");
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_v7_generate: out_uuid is NULL");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "uuid_v7_generate: source.fill is NULL");
  error = n_identity_uuid_v7_timestamp_get(&milliseconds);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  error = nexus_random_source_fill(source, &bytes[6], 10U);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  n_identity_uuid_v7_timestamp_write(bytes, milliseconds);
  n_identity_uuid_variant_version_set(bytes, 7U);
  *out_uuid = nexus_identity_uuid_bytes_read(bytes);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_identity_uuid_v7_generate_random(NexusUUID *out_uuid) {
  return nexus_identity_uuid_v7_generate_with_random(nexus_random_system_source_get(), out_uuid);
}

typedef struct NIdentityV7State {
  boolean initialized;
  uint64  milliseconds;
  byte    random_bytes[10];
#if NEXUS_PLATFORM_WINDOWS
  uint32 process_id;
#elif NEXUS_PLATFORM_POSIX || NEXUS_PLATFORM_BSD
  pid_t process_id;
#endif
} NIdentityV7State;

static NIdentityV7State n_identity_v7_state = {FALSE, 0, {0}, 0};

static uint64 n_identity_process_id_get(void) {
#if NEXUS_PLATFORM_WINDOWS
  return (uint64)GetCurrentProcessId();
#elif NEXUS_PLATFORM_POSIX || NEXUS_PLATFORM_BSD
  return (uint64)getpid();
#else
  return 0;
#endif
}

static boolean n_identity_v7_random_increment(byte random_bytes[10]) {
  int32 index;
  byte  maximum;

  for (index = 9; index >= 0; index--) {
    if (index == 0) {
      maximum = 0x0fU;
    } else if (index == 2) {
      maximum = 0x3fU;
    } else {
      maximum = 0xffU;
    }

    if (random_bytes[index] == maximum) {
      random_bytes[index] = 0;
    } else {
      random_bytes[index]++;
      return TRUE;
    }
  }
  return FALSE;
}

NexusError *nexus_identity_uuid_v7_generate(NexusUUID *out_uuid) {
  NexusRandomSource source;
  NexusError       *error;
  byte              bytes[16];
  uint64            milliseconds;
  uint64            process_id;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_identity_uuid_v7_generate");
  NEXUS_PROGRAM_PRECONDITION(out_uuid != NULL, "uuid_v7_generate: out_uuid is NULL");
  source = nexus_random_system_source_get();
  error  = n_identity_uuid_v7_timestamp_get(&milliseconds);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  process_id = n_identity_process_id_get();
  N_IDENTITY_LOCK();
  if (!n_identity_v7_state.initialized || (uint64)n_identity_v7_state.process_id != process_id || milliseconds > n_identity_v7_state.milliseconds) {
    error = nexus_random_source_fill(source, n_identity_v7_state.random_bytes, 10U);
    if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
      N_IDENTITY_UNLOCK();
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
    n_identity_v7_state.initialized = TRUE;
#if NEXUS_PLATFORM_WINDOWS
    n_identity_v7_state.process_id = (uint32)process_id;
#elif NEXUS_PLATFORM_POSIX || NEXUS_PLATFORM_BSD
    n_identity_v7_state.process_id = (pid_t)process_id;
#endif
    n_identity_v7_state.milliseconds = milliseconds;
  } else if (milliseconds == n_identity_v7_state.milliseconds) {
    if (!n_identity_v7_random_increment(n_identity_v7_state.random_bytes)) {
      N_IDENTITY_UNLOCK();
      NEXUS_PROGRAM_CONTEXT_POP();
      return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_IDENTITY_ERROR_SEQUENCE_EXHAUSTED, "UUIDv7 random sequence exhausted");
    }
  } else {
    if (!n_identity_v7_random_increment(n_identity_v7_state.random_bytes)) {
      N_IDENTITY_UNLOCK();
      NEXUS_PROGRAM_CONTEXT_POP();
      return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_IDENTITY_ERROR_SEQUENCE_EXHAUSTED, "UUIDv7 random sequence exhausted");
    }
  }
  n_identity_v7_state.random_bytes[0] &= 0x0fU;
  n_identity_v7_state.random_bytes[2] &= 0x3fU;
  n_identity_uuid_v7_timestamp_write(bytes, n_identity_v7_state.milliseconds);
  nexus_memory_bytes_copy(n_identity_v7_state.random_bytes, &bytes[6], 10U);
  n_identity_uuid_variant_version_set(bytes, 7U);
  N_IDENTITY_UNLOCK();
  *out_uuid = nexus_identity_uuid_bytes_read(bytes);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusUUID nexus_identity_uuid_v8_build(const byte custom_bytes[16]) {
  byte bytes[16];
  NEXUS_PROGRAM_PRECONDITION(custom_bytes != NULL, "uuid_v8_build: custom_bytes is NULL");
  nexus_memory_bytes_copy(custom_bytes, bytes, 16U);
  n_identity_uuid_variant_version_set(bytes, 8U);
  return nexus_identity_uuid_bytes_read(bytes);
}
