#include "../nexus.h"

/* ---------------------------------------------------------------------------- */
/* GENERATOR PRIMITIVES                                                         */
/* ---------------------------------------------------------------------------- */

#define N_RANDOM_BUFFER_WORD_OFFSET 32U
#define N_RANDOM_BUFFER_BYTE_SIZE   64U

static uint32 n_random_rotate_left32(uint32 value, uint32 shift) {
  return (value << shift) | (value >> (32U - shift));
}

static uint64 n_random_rotate_left64(uint64 value, uint32 shift) {
  return (value << shift) | (value >> (64U - shift));
}

static uint64 n_random_splitmix64_next(uint64 *state) {
  uint64 value;

  *state += 0x9e3779b97f4a7c15ULL;
  value = *state;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

static uint32 n_random_pcg32_next(NexusRandomGenerator *generator) {
  uint64 old_state;
  uint32 xor_shifted;
  uint32 rotation;

  old_state           = generator->state[0];
  generator->state[0] = (old_state * 6364136223846793005ULL) + generator->state[1];
  xor_shifted         = (uint32)(((old_state >> 18U) ^ old_state) >> 27U);
  rotation            = (uint32)(old_state >> 59U);

  return (xor_shifted >> rotation) | (xor_shifted << ((0U - rotation) & 31U));
}

static void n_random_chacha_quarter_round(uint32 *a, uint32 *b, uint32 *c, uint32 *d) /* NOLINT(readability-identifier-length) */ {
  *a += *b;
  *d ^= *a;
  *d = n_random_rotate_left32(*d, 16U);

  *c += *d;
  *b ^= *c;
  *b = n_random_rotate_left32(*b, 12U);

  *a += *b;
  *d ^= *a;
  *d = n_random_rotate_left32(*d, 8U);

  *c += *d;
  *b ^= *c;
  *b = n_random_rotate_left32(*b, 7U);
}

static void n_random_chacha_block_generate(NexusRandomGenerator *generator) {
  uint32 output[16];
  byte  *buffer;
  uint32 round;
  uint32 word_index;

  buffer = (byte *)&generator->state[N_RANDOM_BUFFER_WORD_OFFSET];
  for (word_index = 0; word_index < 16U; word_index++) {
    output[word_index] = (uint32)generator->state[word_index];
  }
  for (round = 0; round < 20U; round += 2U) {
    n_random_chacha_quarter_round(&output[0], &output[4], &output[8], &output[12]);
    n_random_chacha_quarter_round(&output[1], &output[5], &output[9], &output[13]);
    n_random_chacha_quarter_round(&output[2], &output[6], &output[10], &output[14]);
    n_random_chacha_quarter_round(&output[3], &output[7], &output[11], &output[15]);
    n_random_chacha_quarter_round(&output[0], &output[5], &output[10], &output[15]);
    n_random_chacha_quarter_round(&output[1], &output[6], &output[11], &output[12]);
    n_random_chacha_quarter_round(&output[2], &output[7], &output[8], &output[13]);
    n_random_chacha_quarter_round(&output[3], &output[4], &output[9], &output[14]);
  }
  for (word_index = 0; word_index < 16U; word_index++) {
    uint32     value;
    uint_large offset;
    offset              = (uint_large)word_index * 4U;
    value               = output[word_index] + (uint32)generator->state[word_index];
    buffer[offset]      = (byte)value;
    buffer[offset + 1U] = (byte)(value >> 8U);
    buffer[offset + 2U] = (byte)(value >> 16U);
    buffer[offset + 3U] = (byte)(value >> 24U);
  }
  generator->state[12] = (uint32)(generator->state[12] + 1U);
  generator->state[16]--;
  generator->word_index = 0;
}

static uint64 n_random_generator_word_next(NexusRandomGenerator *generator) {
  uint64 state0;
  uint64 state1;
  uint64 result;
  uint32 low;
  uint32 high;

  switch (generator->algorithm) {
  case NRA_LCG64:
    generator->state[0] = (generator->state[0] * 6364136223846793005ULL) + 1442695040888963407ULL;
    return generator->state[0];

  case NRA_SPLITMIX64:
    return n_random_splitmix64_next(&generator->state[0]);

  case NRA_XOROSHIRO128:
    state0 = generator->state[0];
    state1 = generator->state[1];
    result = state0 + state1;
    state1 ^= state0;
    generator->state[0] = n_random_rotate_left64(state0, 55U) ^ state1 ^ (state1 << 14U);
    generator->state[1] = n_random_rotate_left64(state1, 36U);
    return result;

  case NRA_PCG32:
    low  = n_random_pcg32_next(generator);
    high = n_random_pcg32_next(generator);
    return ((uint64)high << 32U) | (uint64)low;

  case NRA_COUNTER:
    result = generator->state[0];
    generator->state[0] += generator->state[1];
    return result;

  default:
    NEXUS_PROGRAM_PANIC("random generator algorithm has no word primitive: %u", (uint32)generator->algorithm);
  }

  return 0;
}

static void n_random_word_little_endian_write(uint64 value, byte out_bytes[8]) {
  uint32 byte_index;

  for (byte_index = 0; byte_index < 8U; byte_index++) {
    out_bytes[byte_index] = (byte)(value >> (byte_index * 8U));
  }
}

/* ---------------------------------------------------------------------------- */
/* SOURCES                                                                      */
/* ---------------------------------------------------------------------------- */

NexusRandomSource nexus_random_system_source_get(void) {
  NexusRandomSource source;

  source.state = NULL;
  source.fill  = nexus_random_system_fill;
  return source;
}

NexusError *nexus_random_system_fill(void *state, byte *out_bytes, uint_large byte_count) {
  (void)state;
  return nexus_entropy_system_fill(out_bytes, byte_count);
}

NexusError *nexus_random_seed_fill(byte *out_seed, uint_large seed_size) {
  return nexus_random_system_fill(NULL, out_seed, seed_size);
}

NexusError *nexus_random_seed_uint32_get(uint32 *out_seed) {
  return nexus_random_source_uint32_get(nexus_random_system_source_get(), out_seed);
}

NexusError *nexus_random_seed_uint64_get(uint64 *out_seed) {
  return nexus_random_source_uint64_get(nexus_random_system_source_get(), out_seed);
}

NexusError *nexus_random_seed_uint128_get(uint128 *out_seed) {
  byte        bytes[16];
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_seed_uint128_get");
  NEXUS_PROGRAM_PRECONDITION(out_seed != NULL, "random seed output is NULL");

  error = nexus_random_seed_fill(bytes, NEXUS_SIZEOF(bytes));
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  out_seed->lo = nexus_bits_uint64_from_bytes_lsb(bytes);
  out_seed->hi = nexus_bits_uint64_from_bytes_lsb(bytes + 8);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_fill(NexusRandomSource source, byte *out_bytes, uint_large byte_count) {
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_fill");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random_source_fill: source.fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_bytes != NULL || byte_count == 0, "random_source_fill: out_bytes is NULL for nonzero byte_count");

  error = source.fill(source.state, out_bytes, byte_count);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return error;
}

/* ---------------------------------------------------------------------------- */
/* GENERATOR INITIALIZATION                                                     */
/* ---------------------------------------------------------------------------- */

static void n_random_generator_clear(NexusRandomGenerator *generator, NexusRandomAlgorithm algorithm) {
  NEXUS_PROGRAM_PRECONDITION(generator != NULL, "random generator is NULL");

  nexus_memory_bytes_clear(generator, NEXUS_SIZEOF(*generator));
  generator->algorithm  = algorithm;
  generator->word_index = N_RANDOM_BUFFER_BYTE_SIZE;
}

void nexus_random_generator_initialize_lcg64(NexusRandomGenerator *generator, uint64 seed) {
  n_random_generator_clear(generator, NRA_LCG64);
  generator->state[0] = seed;
}

void nexus_random_generator_initialize_splitmix64(NexusRandomGenerator *generator, uint64 seed) {
  n_random_generator_clear(generator, NRA_SPLITMIX64);
  generator->state[0] = seed;
}

void nexus_random_generator_initialize_xoroshiro128(NexusRandomGenerator *generator, uint128 seed) {
  n_random_generator_clear(generator, NRA_XOROSHIRO128);

  generator->state[0] = seed.lo;
  generator->state[1] = seed.hi;

  if (generator->state[0] == 0 && generator->state[1] == 0) {
    generator->state[0] = 0x9e3779b97f4a7c15ULL;
    generator->state[1] = 0x6a09e667f3bcc909ULL;
  }
}

void nexus_random_generator_initialize_pcg32(NexusRandomGenerator *generator, uint64 seed, uint64 sequence) {
  n_random_generator_clear(generator, NRA_PCG32);

  generator->state[0] = 0;
  generator->state[1] = (sequence << 1U) | 1U;
  (void)n_random_pcg32_next(generator);
  generator->state[0] += seed;
  (void)n_random_pcg32_next(generator);
}

void nexus_random_generator_initialize_chacha20(NexusRandomGenerator *generator, const byte key[32], const byte nonce[12], uint32 counter) {
  static const uint32 constants[4] = {0x61707865U, 0x3320646eU, 0x79622d32U, 0x6b206574U};
  uint64             *words;
  uint32              index;

  NEXUS_PROGRAM_PRECONDITION(key != NULL, "random chacha key is NULL");
  NEXUS_PROGRAM_PRECONDITION(nonce != NULL, "random chacha nonce is NULL");

  n_random_generator_clear(generator, NRA_CHACHA20);
  words = &generator->state[0];

  for (index = 0; index < 4U; index++) {
    words[index] = constants[index];
  }

  for (index = 0; index < 8U; index++) {
    words[4U + index] = ((uint32)key[(size_t)index * 4U]) | ((uint32)key[(index * 4U) + 1U] << 8U) | ((uint32)key[(index * 4U) + 2U] << 16U) |
                        ((uint32)key[(index * 4U) + 3U] << 24U);
  }

  words[12]            = counter;
  generator->state[16] = 0x100000000ULL - (uint64)counter;
  for (index = 0; index < 3U; index++) {
    words[13U + index] = ((uint32)nonce[(size_t)index * 4U]) | ((uint32)nonce[(index * 4U) + 1U] << 8U) | ((uint32)nonce[(index * 4U) + 2U] << 16U) |
                         ((uint32)nonce[(index * 4U) + 3U] << 24U);
  }
}

void nexus_random_generator_initialize_tape(NexusRandomGenerator *generator, const void *tape, uint_large tape_length) {
  NEXUS_PROGRAM_PRECONDITION(tape != NULL || tape_length == 0, "random tape is NULL for nonzero length");

  n_random_generator_clear(generator, NRA_TAPE);
  generator->tape        = (const byte *)tape;
  generator->tape_length = tape_length;
}

void nexus_random_generator_initialize_counter(NexusRandomGenerator *generator, uint64 start, uint64 step) {
  n_random_generator_clear(generator, NRA_COUNTER);
  generator->state[0] = start;
  generator->state[1] = step;
}

NexusRandomSource nexus_random_generator_source_get(NexusRandomGenerator *generator) {
  NexusRandomSource source;

  NEXUS_PROGRAM_PRECONDITION(generator != NULL, "random generator is NULL");

  source.state = generator;
  source.fill  = nexus_random_generator_fill;
  return source;
}

NexusError *nexus_random_generator_fill(void *generator_pointer, byte *out_bytes, uint_large byte_count) {
  NexusRandomGenerator *generator;
  byte                 *buffer;
  uint_large            output_offset;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_generator_fill");
  NEXUS_PROGRAM_PRECONDITION(generator_pointer != NULL, "random_generator_fill: generator is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_bytes != NULL || byte_count == 0, "random_generator_fill: out_bytes is NULL for nonzero byte_count");

  generator = (NexusRandomGenerator *)generator_pointer;

  NEXUS_PROGRAM_PRECONDITION(generator->algorithm >= NRA_LCG64 && generator->algorithm <= NRA_COUNTER, "random generator algorithm is invalid");
  if (generator->algorithm == NRA_CHACHA20) {
    uint64 remaining;
    remaining = (generator->state[16] * 64U) + (uint64)(N_RANDOM_BUFFER_BYTE_SIZE - generator->word_index);
    if ((uint64)byte_count > remaining) {
      NexusError *error;
      error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_RANDOM_ERROR_STREAM_EXHAUSTED, "ChaCha20 block counter exhausted");
      NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random stream failure returned success");
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
  }

  if (generator->algorithm == NRA_TAPE) {
    if (byte_count > generator->tape_length - generator->tape_offset) {
      NexusError *error;

      error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED, "random tape needs %llu bytes but only %llu remain", byte_count,
                                          generator->tape_length - generator->tape_offset);
      NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random tape failure returned success");
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }

    if (byte_count > 0) {
      nexus_memory_bytes_copy(generator->tape + generator->tape_offset, out_bytes, byte_count);
      generator->tape_offset += byte_count;
    }

    NEXUS_PROGRAM_CONTEXT_POP();
    return NEXUS_PROGRAM_ERROR_SUCCESS();
  }

  buffer        = (byte *)&generator->state[N_RANDOM_BUFFER_WORD_OFFSET];
  output_offset = 0;

  while (output_offset < byte_count) {
    uint_large available;
    uint_large copy_count;

    if ((generator->algorithm == NRA_CHACHA20 && generator->word_index >= N_RANDOM_BUFFER_BYTE_SIZE) ||
        (generator->algorithm != NRA_CHACHA20 && generator->word_index >= 8U)) {
      if (generator->algorithm == NRA_CHACHA20) {
        n_random_chacha_block_generate(generator);
      } else {
        uint64 word;

        word = n_random_generator_word_next(generator);
        n_random_word_little_endian_write(word, buffer);
        generator->word_index = 0;
      }
    }

    available  = generator->algorithm == NRA_CHACHA20 ? N_RANDOM_BUFFER_BYTE_SIZE - generator->word_index : 8U - generator->word_index;
    copy_count = byte_count - output_offset;
    if (copy_count > available) {
      copy_count = available;
    }

    nexus_memory_bytes_copy(buffer + generator->word_index, out_bytes + output_offset, copy_count);
    generator->word_index += (uint32)copy_count;
    output_offset += copy_count;
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_uint32_get(NexusRandomSource source, uint32 *out_value) {
  byte        bytes[4];
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_uint32_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random_source_uint32_get: out_value is NULL");

  error = nexus_random_source_fill(source, bytes, NEXUS_SIZEOF(bytes));
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_value = (uint32)bytes[0] | ((uint32)bytes[1] << 8U) | ((uint32)bytes[2] << 16U) | ((uint32)bytes[3] << 24U);

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_uint64_get(NexusRandomSource source, uint64 *out_value) {
  byte        bytes[8];
  uint32      byte_index;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_uint64_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random_source_uint64_get: out_value is NULL");

  error = nexus_random_source_fill(source, bytes, NEXUS_SIZEOF(bytes));
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  *out_value = 0;
  for (byte_index = 0; byte_index < 8U; byte_index++) {
    *out_value |= (uint64)bytes[byte_index] << (byte_index * 8U);
  }

  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_uint32_bounded_get(NexusRandomSource source, uint32 upper_bound, uint32 *out_value) {
  uint32      threshold;
  uint32      value;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_uint32_bounded_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random bounded output is NULL");
  NEXUS_PROGRAM_PRECONDITION(upper_bound > 0, "random upper bound must be positive");
  threshold = (uint32)(0U - upper_bound) % upper_bound;
  do {
    error = nexus_random_source_uint32_get(source, &value);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
      error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
      NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
  } while (value < threshold);
  *out_value = value % upper_bound;
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_uint64_bounded_get(NexusRandomSource source, uint64 upper_bound, uint64 *out_value) {
  uint64      threshold;
  uint64      value;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_uint64_bounded_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random bounded output is NULL");
  NEXUS_PROGRAM_PRECONDITION(upper_bound > 0, "random upper bound must be positive");
  threshold = (uint64)(0U - upper_bound) % upper_bound;
  do {
    error = nexus_random_source_uint64_get(source, &value);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
      error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
      NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
      NEXUS_PROGRAM_CONTEXT_POP();
      return error;
    }
  } while (value < threshold);
  *out_value = value % upper_bound;
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_uint32_range_get(NexusRandomSource source, uint32 minimum, uint32 maximum, uint32 *out_value) {
  uint32      offset;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_uint32_range_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random range output is NULL");
  NEXUS_PROGRAM_PRECONDITION(minimum < maximum, "random maximum must exceed minimum");
  error = nexus_random_source_uint32_bounded_get(source, maximum - minimum, &offset);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  *out_value = minimum + offset;
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_uint64_range_get(NexusRandomSource source, uint64 minimum, uint64 maximum, uint64 *out_value) {
  uint64      offset;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_uint64_range_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random range output is NULL");
  NEXUS_PROGRAM_PRECONDITION(minimum < maximum, "random maximum must exceed minimum");
  error = nexus_random_source_uint64_bounded_get(source, maximum - minimum, &offset);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  *out_value = minimum + offset;
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_int32_range_get(NexusRandomSource source, int32 minimum, int32 maximum, int32 *out_value) {
  uint32      offset;
  uint32      value;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_int32_range_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random range output is NULL");
  NEXUS_PROGRAM_PRECONDITION(minimum < maximum, "random maximum must exceed minimum");
  error = nexus_random_source_uint32_bounded_get(source, (uint32)maximum - (uint32)minimum, &offset);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  value = (uint32)minimum + offset;
  /* Decode negative values through their complement, keeping every cast in range. */
  if (value <= (uint32)INT32_MAX_VAL)
    *out_value = (int32)value;
  else
    *out_value = -1 - (int32)(UINT32_MAX_VAL - value);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_int64_range_get(NexusRandomSource source, int64 minimum, int64 maximum, int64 *out_value) {
  uint64      offset;
  uint64      value;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_int64_range_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random range output is NULL");
  NEXUS_PROGRAM_PRECONDITION(minimum < maximum, "random maximum must exceed minimum");
  error = nexus_random_source_uint64_bounded_get(source, (uint64)maximum - (uint64)minimum, &offset);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  value = (uint64)minimum + offset;
  /* Decode negative values through their complement, keeping every cast in range. */
  if (value <= (uint64)INT64_MAX_VAL)
    *out_value = (int64)value;
  else
    *out_value = -1 - (int64)(UINT64_MAX_VAL - value);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_real32_get(NexusRandomSource source, real32 *out_value) {
  uint32      value;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_real32_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random real output is NULL");
  error = nexus_random_source_uint32_get(source, &value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  *out_value = (real32)(value >> 8U) * (1.0F / 16777216.0F);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_source_real64_get(NexusRandomSource source, real64 *out_value) {
  uint64      value;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_random_source_real64_get");
  NEXUS_PROGRAM_PRECONDITION(source.fill != NULL, "random source fill is NULL");
  NEXUS_PROGRAM_PRECONDITION(out_value != NULL, "random real output is NULL");
  error = nexus_random_source_uint64_get(source, &value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    error = NEXUS_PROGRAM_ERRORS_FORWARD(error);
    NEXUS_PROGRAM_POSTCONDITION(error->code != GLOBAL_NEXUS_ERROR_SUCCESS, "random error forwarding returned success");
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }
  *out_value = (real64)(value >> 11U) * (1.0 / 9007199254740992.0);
  NEXUS_PROGRAM_CONTEXT_POP();
  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

NexusError *nexus_random_generator_uint32_get(NexusRandomGenerator *generator, uint32 *out_value) {
  return nexus_random_source_uint32_get(nexus_random_generator_source_get(generator), out_value);
}

NexusError *nexus_random_generator_uint64_get(NexusRandomGenerator *generator, uint64 *out_value) {
  return nexus_random_source_uint64_get(nexus_random_generator_source_get(generator), out_value);
}

NexusError *nexus_random_generator_real32_get(NexusRandomGenerator *generator, real32 *out_value) {
  return nexus_random_source_real32_get(nexus_random_generator_source_get(generator), out_value);
}

NexusError *nexus_random_generator_real64_get(NexusRandomGenerator *generator, real64 *out_value) {
  return nexus_random_source_real64_get(nexus_random_generator_source_get(generator), out_value);
}

NexusError *nexus_random_generator_uint32_bounded_get(NexusRandomGenerator *generator, uint32 upper_bound, uint32 *out_value) {
  return nexus_random_source_uint32_bounded_get(nexus_random_generator_source_get(generator), upper_bound, out_value);
}

NexusError *nexus_random_generator_uint64_bounded_get(NexusRandomGenerator *generator, uint64 upper_bound, uint64 *out_value) {
  return nexus_random_source_uint64_bounded_get(nexus_random_generator_source_get(generator), upper_bound, out_value);
}

NexusError *nexus_random_generator_uint32_range_get(NexusRandomGenerator *generator, uint32 minimum, uint32 maximum, uint32 *out_value) {
  return nexus_random_source_uint32_range_get(nexus_random_generator_source_get(generator), minimum, maximum, out_value);
}

NexusError *nexus_random_generator_uint64_range_get(NexusRandomGenerator *generator, uint64 minimum, uint64 maximum, uint64 *out_value) {
  return nexus_random_source_uint64_range_get(nexus_random_generator_source_get(generator), minimum, maximum, out_value);
}

NexusError *nexus_random_generator_int32_range_get(NexusRandomGenerator *generator, int32 minimum, int32 maximum, int32 *out_value) {
  return nexus_random_source_int32_range_get(nexus_random_generator_source_get(generator), minimum, maximum, out_value);
}

NexusError *nexus_random_generator_int64_range_get(NexusRandomGenerator *generator, int64 minimum, int64 maximum, int64 *out_value) {
  return nexus_random_source_int64_range_get(nexus_random_generator_source_get(generator), minimum, maximum, out_value);
}
