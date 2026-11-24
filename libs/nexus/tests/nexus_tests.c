#include <string.h>
#include <nexus.h>

#include <setjmp.h>
#include <stdio.h>

#if NEXUS_PLATFORM_LINUX
#  include <signal.h>
#  include <sys/types.h>
#  include <sys/wait.h>
#endif

static int nexus_test_compare_ints(const void *left, const void *right, void *user_data) {
  const int *left_int;
  const int *right_int;
  (void)user_data;

  left_int  = (const int *)left;
  right_int = (const int *)right;
  return *left_int - *right_int;
}

static int nexus_test_fail(int code) {
  (void)fprintf(stderr, "Nexus test failed: %d\n", code);
  return code;
}

static int nexus_test_errors_wrap(void) {
  NexusError    *cause;
  NexusError    *outer;
  NexusError    *plain;
  NexusError    *causes[1];
  NexusErrorCode codes[1];
  const char    *formats[1];
  uint32         line;
  uint32         cause_reads;
  uint32         code_reads;
  uint32         format_reads;
  uint32         argument_reads;
  uint64         slot;
  uint64         capacity;

  nexus_program_errors_pool_reset();
  cause = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_ERROR_FORWARD, "root cause");
  line  = __LINE__ + 1U;
  outer = NEXUS_PROGRAM_ERRORS_WRAP(cause, GLOBAL_NEXUS_ERROR_FORWARD, "opening %s: %u", "audio.flac", 7U);
  if (outer == cause || outer->code != GLOBAL_NEXUS_ERROR_FORWARD || outer->contained_error != cause || cause->contained_error != NULL ||
      !nexus_strings_string_equals(cause->message, "root cause") || !nexus_strings_string_equals(outer->message, "opening audio.flac: 7") ||
      !nexus_strings_string_equals(outer->file, __FILE__) || outer->line != line) {
    return nexus_test_fail(1300);
  }

  plain = NEXUS_PROGRAM_ERRORS_WRAP(outer, GLOBAL_NEXUS_ERROR_FORWARD, "plain message");
  if (plain->contained_error != outer || outer->contained_error != cause || !nexus_strings_string_equals(plain->message, "plain message")) {
    return nexus_test_fail(1301);
  }
  outer = NEXUS_PROGRAM_ERRORS_WRAP(plain, GLOBAL_NEXUS_ERROR_FORWARD, NULL);
  if (outer->message != NULL || outer->contained_error != plain) {
    return nexus_test_fail(1302);
  }

  causes[0]      = cause;
  codes[0]       = GLOBAL_NEXUS_ERROR_FORWARD;
  formats[0]     = "evaluation %u";
  cause_reads    = 0U;
  code_reads     = 0U;
  format_reads   = 0U;
  argument_reads = 0U;
  outer          = NEXUS_PROGRAM_ERRORS_WRAP(causes[cause_reads++], codes[code_reads++], formats[format_reads++], ++argument_reads);
  if (cause_reads != 1U || code_reads != 1U || format_reads != 1U || argument_reads != 1U || outer->contained_error != cause ||
      !nexus_strings_string_equals(outer->message, "evaluation 1")) {
    return nexus_test_fail(1303);
  }

  /* An ordinary pooled error may itself carry the pool-capacity code. */
  outer = NEXUS_PROGRAM_ERRORS_WRAP(cause, GLOBAL_NEXUS_ERROR_POOL_CAPACITY_REACHED, NULL);
  if (outer == &nexus_program_errors_pool_capacity_reached || outer->code != GLOBAL_NEXUS_ERROR_POOL_CAPACITY_REACHED ||
      outer->contained_error != cause) {
    return nexus_test_fail(1304);
  }

  nexus_program_errors_pool_reset();
  capacity = nexus_program_errors_pool_slabs_get() * NEXUS_PROGRAM_ERROR_POOL_SLAB_SIZE;
  cause    = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_ERROR_FORWARD, NULL);
  for (slot = 1U; slot < capacity; slot++) {
    outer = NEXUS_PROGRAM_ERRORS_WRAP(cause, GLOBAL_NEXUS_ERROR_FORWARD, NULL);
    if (outer == &nexus_program_errors_pool_capacity_reached || outer->contained_error != cause) {
      return nexus_test_fail(1305);
    }
  }

  /* Exhaustion must not attach a cause to the shared static sentinel. */
  outer = NEXUS_PROGRAM_ERRORS_WRAP(cause, GLOBAL_NEXUS_ERROR_FORWARD, "pool full");
  if (outer != &nexus_program_errors_pool_capacity_reached || outer->code != GLOBAL_NEXUS_ERROR_POOL_CAPACITY_REACHED ||
      outer->contained_error != NULL || cause->contained_error != NULL) {
    return nexus_test_fail(1306);
  }
  nexus_program_errors_pool_reset();

  return 0;
}

static int nexus_test_hashes(void) {
  static const char text[] = "hello";
  if (nexus_hash_fnv1a32(text, 5U) != 0x4f9f2cabU) {
    return nexus_test_fail(10);
  }
  if (nexus_hash_fnv1a64(text, 5U) != 0xa430d84680aabd0bULL) {
    return nexus_test_fail(11);
  }
  if (nexus_hash_fnv1a32_update(nexus_hash_fnv1a32_update(nexus_hash_fnv1a32_begin(), text, 2U), text + 2U, 3U) != nexus_hash_fnv1a32(text, 5U)) {
    return nexus_test_fail(12);
  }
  return 0;
}

static int nexus_test_json(void) {
  static const char  text[] = "{\"a\":[1,-2,true,null],\"s\":\"x\\ny\\u00e9\\ud83d\\ude00\",\"n\":{\"k\":7}} ";
  NexusDataArena     arena;
  const NexusJson   *root;
  NexusStringBuilder builder;
  const NexusJson   *array;

  nexus_data_arena_initialize(&arena);
  root = nexus_json_parse(&arena, text, sizeof(text) - 1U);

  if (root == NULL || root->kind != NJK_OBJECT || root->count != 3U) {
    return nexus_test_fail(9001);
  }

  array = nexus_json_member_get(root, "a");

  if (array == NULL || array->kind != NJK_ARRAY || array->count != 4U || array->items[1]->integer != -2 || !array->items[2]->boolean_value ||
      array->items[3]->kind != NJK_NULL) {
    return nexus_test_fail(9002);
  }

  if (!nexus_strings_string_equals(nexus_json_member_text_get(root, "s"), "x\ny\xc3\xa9\xf0\x9f\x98\x80") ||
      nexus_json_member_integer_get(nexus_json_member_get(root, "n"), "k", 0) != 7 || nexus_json_member_integer_get(root, "missing", 42) != 42) {
    return nexus_test_fail(9003);
  }

  if (nexus_json_parse(&arena, "{\"a\":}", 6U) != NULL || nexus_json_parse(&arena, "[1] x", 5U) != NULL ||
      nexus_json_parse(&arena, "[1,]", 4U) != NULL) {
    return nexus_test_fail(9004);
  }

  builder = nexus_strings_string_builder_create_growable(16U);

  if (nexus_json_quote_append(&builder, "a\"b\\c\n\x01\xc3\xa9") != NEXUS_PROGRAM_ERROR_SUCCESS() ||
      !nexus_strings_string_equals(nexus_strings_string_builder_string_get(&builder), "\"a\\\"b\\\\c\\n\\u0001\xc3\xa9\"")) {
    return nexus_test_fail(9005);
  }

  nexus_strings_string_builder_destroy(&builder);
  nexus_data_arena_release(&arena);

  return 0;
}

static boolean nexus_test_random_error_has_code(const NexusError *error, NexusErrorCode code) {
  while (error != NULL) {
    if (error->code == code)
      return TRUE;
    error = error->contained_error;
  }
  return FALSE;
}

static void nexus_test_random_initialize(NexusRandomGenerator *generator, NexusRandomAlgorithm algorithm, const byte *tape, uint_large length) {
  byte    key[32];
  byte    nonce[12];
  uint32  index;
  uint128 seed;

  for (index = 0; index < 32U; index++) {
    key[index] = (byte)index;
  }
  nexus_memory_bytes_clear(nonce, NEXUS_SIZEOF(nonce));
  seed.lo = 1U;
  seed.hi = 2U;
  switch (algorithm) {
  case NRA_LCG64:
    nexus_random_generator_initialize_lcg64(generator, 0U);
    break;
  case NRA_SPLITMIX64:
    nexus_random_generator_initialize_splitmix64(generator, 0U);
    break;
  case NRA_XOROSHIRO128:
    nexus_random_generator_initialize_xoroshiro128(generator, seed);
    break;
  case NRA_PCG32:
    nexus_random_generator_initialize_pcg32(generator, 42U, 54U);
    break;
  case NRA_CHACHA20:
    nexus_random_generator_initialize_chacha20(generator, key, nonce, 0U);
    break;
  case NRA_TAPE:
    nexus_random_generator_initialize_tape(generator, tape, length);
    break;
  case NRA_COUNTER:
    nexus_random_generator_initialize_counter(generator, 9U, 3U);
    break;
  }
}

static int nexus_test_random_vectors(void) {
  static const uint32 pcg[]      = {0xa15c02b7U, 0x7b47f409U, 0xba1d3330U, 0x83d2f293U, 0xbfa4784bU, 0xcbed606eU};
  static const uint64 splitmix[] = {0xe220a8397b1dcdafULL, 0x6e789e6aa1b965f4ULL, 0x06c45d188009454fULL};
  /* RFC 8439 section 2.3.2; key 00..1f, nonce 000000090000004a00000000, counter 1. */
  static const byte    chacha[64] = {0x10, 0xf1, 0xe7, 0xe4, 0xd1, 0x3b, 0x59, 0x15, 0x50, 0x0f, 0xdd, 0x1f, 0xa3, 0x20, 0x71, 0xc4,
                                     0xc7, 0xd1, 0xf4, 0xc7, 0x33, 0xc0, 0x68, 0x03, 0x04, 0x22, 0xaa, 0x9a, 0xc3, 0xd4, 0x6c, 0x4e,
                                     0xd2, 0x82, 0x64, 0x46, 0x07, 0x9f, 0xaa, 0x09, 0x14, 0xc2, 0xd7, 0x05, 0xd9, 0x8b, 0x02, 0xa2,
                                     0xb5, 0x12, 0x9c, 0xd1, 0xde, 0x16, 0x4e, 0xb9, 0xcb, 0xd0, 0x83, 0xe8, 0xa2, 0x50, 0x3c, 0x4e};
  NexusRandomGenerator generator;
  NexusRandomGenerator other;
  NexusError          *error;
  byte                 key[32];
  byte                 nonce[12];
  byte                 output[65];
  byte                 reference[64];
  uint32               index;
  uint32               value32;
  uint64               value64;
  uint128              seed;

  nexus_random_generator_initialize_pcg32(&generator, 42U, 54U);
  for (index = 0; index < 6U; index++) {
    error = nexus_random_generator_uint32_get(&generator, &value32);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value32 != pcg[index])
      return nexus_test_fail(1100);
  }
  nexus_random_generator_initialize_pcg32(&generator, 42U, 54U);
  nexus_random_generator_initialize_pcg32(&other, 42U, 54U | 0x8000000000000000ULL);
  error = nexus_random_generator_uint64_get(&generator, &value64);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != 0x7b47f409a15c02b7ULL)
    return nexus_test_fail(1101);
  {
    uint64 other_value;
    error = nexus_random_generator_uint64_get(&other, &other_value);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || other_value != value64)
      return nexus_test_fail(1102);
  }
  nexus_random_generator_initialize_splitmix64(&generator, 0U);
  for (index = 0; index < 3U; index++) {
    error = nexus_random_generator_uint64_get(&generator, &value64);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != splitmix[index])
      return nexus_test_fail(1103);
  }
  nexus_random_generator_initialize_lcg64(&generator, 0U);
  error = nexus_random_generator_uint64_get(&generator, &value64);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != 1442695040888963407ULL)
    return nexus_test_fail(1104);
  error = nexus_random_generator_uint64_get(&generator, &value64);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != 1876011003808476466ULL)
    return nexus_test_fail(1105);
  seed.lo = 1U;
  seed.hi = 2U;
  nexus_random_generator_initialize_xoroshiro128(&generator, seed);
  error = nexus_random_generator_uint64_get(&generator, &value64);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != 3U)
    return nexus_test_fail(1106);
  error = nexus_random_generator_uint64_get(&generator, &value64);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != 0x008000300000c003ULL)
    return nexus_test_fail(1107);
  seed.lo = 0U;
  seed.hi = 0U;
  nexus_random_generator_initialize_xoroshiro128(&generator, seed);
  seed.lo = 0x9e3779b97f4a7c15ULL;
  seed.hi = 0x6a09e667f3bcc909ULL;
  nexus_random_generator_initialize_xoroshiro128(&other, seed);
  error = nexus_random_generator_fill(&generator, output, 64U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1108);
  error = nexus_random_generator_fill(&other, reference, 64U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || memcmp(output, reference, 64U) != 0)
    return nexus_test_fail(1109);
  for (index = 0; index < 32U; index++)
    key[index] = (byte)index;
  nexus_memory_bytes_clear(nonce, NEXUS_SIZEOF(nonce));
  nonce[3] = 9U;
  nonce[7] = 0x4aU;
  nexus_random_generator_initialize_chacha20(&generator, key, nonce, 1U);
  error = nexus_random_generator_fill(&generator, output, 64U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || memcmp(output, chacha, 64U) != 0)
    return nexus_test_fail(1110);

  /* Exhaustion must preserve both the buffered final block and caller output. */
  nexus_random_generator_initialize_chacha20(&generator, key, nonce, 0xffffffffU);
  nexus_random_generator_initialize_chacha20(&other, key, nonce, 0xffffffffU);
  nexus_memory_bytes_set(output, 65U, 0xa5U);
  error = nexus_random_generator_fill(&generator, output, 65U);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_STREAM_EXHAUSTED))
    return nexus_test_fail(1111);
  for (index = 0; index < 65U; index++)
    if (output[index] != 0xa5U)
      return nexus_test_fail(1112);
  error = nexus_random_generator_fill(&generator, output, 1U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1113);
  error = nexus_random_generator_fill(&generator, reference, 64U);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_STREAM_EXHAUSTED))
    return nexus_test_fail(1114);
  error = nexus_random_generator_fill(&generator, output + 1, 63U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1115);
  error = nexus_random_generator_fill(&other, reference, 64U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || memcmp(output, reference, 64U) != 0)
    return nexus_test_fail(1116);
  value64 = 123U;
  error   = nexus_random_generator_uint64_get(&generator, &value64);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_STREAM_EXHAUSTED) || value64 != 123U)
    return nexus_test_fail(1117);
  error = nexus_random_generator_fill(&generator, NULL, 0U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1118);
  return 0;
}

static int nexus_test_random_chunking(void) {
  NexusRandomGenerator whole;
  NexusRandomGenerator chunked;
  NexusError          *error;
  byte                 tape[257];
  byte                 expected[257];
  byte                 actual[257];
  uint32               algorithm;
  uint32               index;
  uint32               value32;
  uint64               value64;

  for (index = 0; index < 257U; index++)
    tape[index] = (byte)index;
  for (algorithm = NRA_LCG64; algorithm <= NRA_COUNTER; algorithm++) {
    nexus_test_random_initialize(&whole, (NexusRandomAlgorithm)algorithm, tape, 257U);
    nexus_test_random_initialize(&chunked, (NexusRandomAlgorithm)algorithm, tape, 257U);
    error = nexus_random_generator_fill(&whole, expected, 257U);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
      return nexus_test_fail(1120);
    for (index = 0; index < 257U; index++) {
      error = nexus_random_source_fill(nexus_random_generator_source_get(&chunked), actual + index, 1U);
      if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
        return nexus_test_fail(1121);
    }
    if (memcmp(expected, actual, 257U) != 0)
      return nexus_test_fail(1122);
    nexus_test_random_initialize(&chunked, (NexusRandomAlgorithm)algorithm, tape, 257U);
    error = nexus_random_generator_fill(&chunked, actual, 1U);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || actual[0] != expected[0])
      return nexus_test_fail(1123);
    error = nexus_random_generator_uint32_get(&chunked, &value32);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value32 != nexus_bits_uint32_from_bytes_lsb(expected + 1))
      return nexus_test_fail(1124);
    error = nexus_random_generator_uint64_get(&chunked, &value64);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != nexus_bits_uint64_from_bytes_lsb(expected + 5))
      return nexus_test_fail(1125);
    error = nexus_random_generator_fill(&chunked, actual + 13, 244U);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || memcmp(expected + 13, actual + 13, 244U) != 0)
      return nexus_test_fail(1126);
  }
  return 0;
}

static int nexus_test_random_sampling(void) {
  static const uint32  bounds32[] = {1U, 16U, 10U, 0x80000001U, 0xffffffffU};
  static const uint64  bounds64[] = {1U, 16U, 10U, 0x8000000000000001ULL, 0xffffffffffffffffULL};
  NexusRandomGenerator generator;
  NexusRandomSource    source;
  NexusError          *error;
  byte                 tape[32];
  uint32               index;
  uint32               value32;
  uint32               threshold32;
  uint64               value64;
  uint64               threshold64;
  real32               float_value;
  real64               double_value;

  /* Each finite tape forces rejection below the threshold and acceptance at it. */
  for (index = 0; index < 5U; index++) {
    threshold32 = (uint32)(0U - bounds32[index]) % bounds32[index];
    nexus_bits_uint32_to_bytes_lsb(tape, threshold32 == 0U ? 0U : threshold32 - 1U);
    nexus_bits_uint32_to_bytes_lsb(tape + 4, threshold32);
    nexus_random_generator_initialize_tape(&generator, tape, threshold32 == 0U ? 4U : 8U);
    source = nexus_random_generator_source_get(&generator);
    error  = nexus_random_source_uint32_bounded_get(source, bounds32[index], &value32);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value32 != threshold32 % bounds32[index])
      return nexus_test_fail(1130);
    value32 = 123U;
    error   = nexus_random_generator_uint32_get(&generator, &value32);
    if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || value32 != 123U)
      return nexus_test_fail(1131);

    threshold64 = (uint64)(0U - bounds64[index]) % bounds64[index];
    nexus_bits_uint64_to_bytes_lsb(tape, threshold64 == 0U ? 0U : threshold64 - 1U);
    nexus_bits_uint64_to_bytes_lsb(tape + 8, threshold64);
    nexus_random_generator_initialize_tape(&generator, tape, threshold64 == 0U ? 8U : 16U);
    error = nexus_random_generator_uint64_bounded_get(&generator, bounds64[index], &value64);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != threshold64 % bounds64[index])
      return nexus_test_fail(1132);
    value64 = 123U;
    error   = nexus_random_source_uint64_get(nexus_random_generator_source_get(&generator), &value64);
    if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || value64 != 123U)
      return nexus_test_fail(1133);
  }
  nexus_memory_bytes_clear(tape, NEXUS_SIZEOF(tape));
  nexus_random_generator_initialize_tape(&generator, tape, 4U);
  value32 = 123U;
  error   = nexus_random_generator_uint32_bounded_get(&generator, 10U, &value32);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || value32 != 123U)
    return nexus_test_fail(1134);
  nexus_random_generator_initialize_tape(&generator, tape, 8U);
  value64 = 123U;
  error   = nexus_random_source_uint64_bounded_get(nexus_random_generator_source_get(&generator), 10U, &value64);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || value64 != 123U)
    return nexus_test_fail(1135);

  /* Zero, greatest value below one, and exactly one half, at each precision. */
  nexus_bits_uint32_to_bytes_lsb(tape, 0U);
  nexus_bits_uint32_to_bytes_lsb(tape + 4, 0xffffffffU);
  nexus_bits_uint32_to_bytes_lsb(tape + 8, 0x80000000U);
  nexus_random_generator_initialize_tape(&generator, tape, 12U);
  error = nexus_random_generator_real32_get(&generator, &float_value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || float_value != 0.0F)
    return nexus_test_fail(1136);
  error = nexus_random_source_real32_get(nexus_random_generator_source_get(&generator), &float_value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || float_value != 1.0F - (1.0F / 16777216.0F))
    return nexus_test_fail(1137);
  error = nexus_random_generator_real32_get(&generator, &float_value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || float_value != 0.5F)
    return nexus_test_fail(1138);
  error = nexus_random_generator_real32_get(&generator, &float_value);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || float_value != 0.5F)
    return nexus_test_fail(1139);
  nexus_bits_uint64_to_bytes_lsb(tape, 0U);
  nexus_bits_uint64_to_bytes_lsb(tape + 8, 0xffffffffffffffffULL);
  nexus_bits_uint64_to_bytes_lsb(tape + 16, 0x8000000000000000ULL);
  nexus_random_generator_initialize_tape(&generator, tape, 24U);
  error = nexus_random_source_real64_get(nexus_random_generator_source_get(&generator), &double_value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || double_value != 0.0)
    return nexus_test_fail(1140);
  error = nexus_random_generator_real64_get(&generator, &double_value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || double_value != 1.0 - (1.0 / 9007199254740992.0))
    return nexus_test_fail(1141);
  error = nexus_random_generator_real64_get(&generator, &double_value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || double_value != 0.5)
    return nexus_test_fail(1142);
  error = nexus_random_generator_real64_get(&generator, &double_value);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || double_value != 0.5)
    return nexus_test_fail(1143);
  return 0;
}

static NexusError *nexus_test_random_failing_fill(void *state, byte *out_bytes, uint_large byte_count) {
  (void)state;
  if (byte_count > 0U)
    out_bytes[0] = 0xa5U;
  return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED, "deliberate random source failure");
}

static int nexus_test_random_failures(void) {
  NexusRandomGenerator generator;
  NexusRandomSource    source;
  NexusError          *error;
  byte                 tape[4]   = {1U, 2U, 3U, 4U};
  byte                 output[4] = {0xa5U, 0xa5U, 0xa5U, 0xa5U};
  uint32               value32;
  uint64               value64;
  real32               float_value;
  real64               double_value;

  nexus_random_generator_initialize_tape(&generator, tape, 3U);
  error = nexus_random_generator_fill(&generator, output, 4U);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || output[0] != 0xa5U || output[3] != 0xa5U)
    return nexus_test_fail(1160);
  error = nexus_random_generator_fill(&generator, output, 3U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || memcmp(tape, output, 3U) != 0)
    return nexus_test_fail(1161);
  nexus_random_generator_initialize_tape(&generator, NULL, 0U);
  error = nexus_random_generator_fill(&generator, NULL, 0U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1162);
  nexus_random_generator_initialize_counter(&generator, 0xffffffffffffffffULL, 2U);
  error = nexus_random_generator_uint64_get(&generator, &value64);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != 0xffffffffffffffffULL)
    return nexus_test_fail(1163);
  error = nexus_random_generator_uint64_get(&generator, &value64);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != 1U)
    return nexus_test_fail(1164);

  source.state = NULL;
  source.fill  = nexus_test_random_failing_fill;
  value32      = 123U;
  error        = nexus_random_source_uint32_get(source, &value32);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || value32 != 123U)
    return nexus_test_fail(1165);
  value64 = 123U;
  error   = nexus_random_source_uint64_get(source, &value64);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || value64 != 123U)
    return nexus_test_fail(1166);
  error = nexus_random_source_uint32_bounded_get(source, 10U, &value32);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || value32 != 123U)
    return nexus_test_fail(1167);
  error = nexus_random_source_uint64_bounded_get(source, 10U, &value64);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || value64 != 123U)
    return nexus_test_fail(1168);
  float_value  = 0.5F;
  double_value = 0.5;
  error        = nexus_random_source_real32_get(source, &float_value);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || float_value != 0.5F)
    return nexus_test_fail(1169);
  error = nexus_random_source_real64_get(source, &double_value);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || double_value != 0.5)
    return nexus_test_fail(1170);
#if NEXUS_DEVELOPER_MODE
  {
    jmp_buf recovery_point;
    uint32  index;
    for (index = 0; index < 15U; index++) {
      nexus_random_generator_initialize_counter(&generator, 0U, 1U);
      source = nexus_random_generator_source_get(&generator);
      if (setjmp(recovery_point) == 0) {
        nexus_program_recovery_arm(&recovery_point);
        switch (index) {
        case 0:
          error = nexus_random_generator_uint32_bounded_get(&generator, 0U, &value32);
          break;
        case 1:
          error = nexus_random_generator_uint64_bounded_get(&generator, 0U, &value64);
          break;
        case 2:
          source.fill = NULL;
          error       = nexus_random_source_uint32_get(source, &value32);
          break;
        case 3:
          error = nexus_random_generator_fill(NULL, output, 1U);
          break;
        case 4:
          error = nexus_random_source_real64_get(source, NULL);
          break;
        case 5:
          error = nexus_random_seed_uint32_get(NULL);
          break;
        case 6:
          error = nexus_random_seed_uint64_get(NULL);
          break;
        case 7:
          error = nexus_random_seed_uint128_get(NULL);
          break;
        case 8:
          error = nexus_random_seed_fill(NULL, 1U);
          break;
        case 9:
          error = nexus_random_generator_uint32_range_get(&generator, 10U, 10U, &value32);
          break;
        case 10:
          error = nexus_random_generator_uint32_range_get(&generator, 11U, 10U, &value32);
          break;
        case 11:
          error = nexus_random_generator_uint64_range_get(&generator, 10U, 10U, &value64);
          break;
        case 12:
          error = nexus_random_generator_uint64_range_get(&generator, 11U, 10U, &value64);
          break;
        case 13:
          source.fill = NULL;
          error       = nexus_random_source_uint32_range_get(source, 0U, 1U, &value32);
          break;
        default:
          error = nexus_random_source_uint64_range_get(source, 0U, 1U, NULL);
          break;
        }
        (void)error;
        nexus_program_recovery_disarm();
        return nexus_test_fail(1171);
      }
      nexus_program_recovery_disarm();
      error = nexus_program_recovery_last_error_get();
      if (error->code != GLOBAL_NEXUS_PRECONDITION_VIOLATION)
        return nexus_test_fail(1172);
    }
  }
#endif
  return 0;
}

static int nexus_test_random_ranges(void) {
  static const uint32  cases32[][4] = {{0U, 16U, 15U, 15U},
                                       {100U, 110U, 10U, 100U},
                                       {100U, 110U, 9U, 109U},
                                       {100U, 101U, 0U, 100U},
                                       {0xfffffffeU, 0xffffffffU, 0xffffffffU, 0xfffffffeU},
                                       {5U, 0xffffffffU, 0xfffffff9U, 0xfffffffeU},
                                       {0U, 0xffffffffU, 0xfffffffeU, 0xfffffffeU}};
  static const uint64  cases64[][4] = {{0U, 16U, 15U, 15U},
                                       {100U, 110U, 10U, 100U},
                                       {100U, 110U, 9U, 109U},
                                       {100U, 101U, 0U, 100U},
                                       {0xfffffffffffffffeULL, 0xffffffffffffffffULL, 0xffffffffffffffffULL, 0xfffffffffffffffeULL},
                                       {5U, 0xffffffffffffffffULL, 0xfffffffffffffff9ULL, 0xfffffffffffffffeULL},
                                       {0U, 0xffffffffffffffffULL, 0xfffffffffffffffeULL, 0xfffffffffffffffeULL}};
  NexusRandomGenerator generator;
  NexusRandomSource    source;
  NexusError          *error;
  byte                 tape[24];
  uint_large           index;
  uint32               value32;
  uint64               value64;

  for (index = 0; index < NEXUS_SIZEOF(cases32) / NEXUS_SIZEOF(cases32[0]); index++) {
    nexus_bits_uint32_to_bytes_lsb(tape, cases32[index][2]);
    nexus_random_generator_initialize_tape(&generator, tape, 4U);
    source = nexus_random_generator_source_get(&generator);
    if (index % 2U == 0U)
      error = nexus_random_source_uint32_range_get(source, cases32[index][0], cases32[index][1], &value32);
    else
      error = nexus_random_generator_uint32_range_get(&generator, cases32[index][0], cases32[index][1], &value32);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value32 != cases32[index][3])
      return nexus_test_fail(1190);
  }
  for (index = 0; index < NEXUS_SIZEOF(cases64) / NEXUS_SIZEOF(cases64[0]); index++) {
    nexus_bits_uint64_to_bytes_lsb(tape, cases64[index][2]);
    nexus_random_generator_initialize_tape(&generator, tape, 8U);
    source = nexus_random_generator_source_get(&generator);
    if (index % 2U == 0U)
      error = nexus_random_source_uint64_range_get(source, cases64[index][0], cases64[index][1], &value64);
    else
      error = nexus_random_generator_uint64_range_get(&generator, cases64[index][0], cases64[index][1], &value64);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != cases64[index][3])
      return nexus_test_fail(1191);
  }

  /* Width ten rejects zero and five, then accepts six, translated to 106. */
  nexus_bits_uint32_to_bytes_lsb(tape, 0U);
  nexus_bits_uint32_to_bytes_lsb(tape + 4, 5U);
  nexus_bits_uint32_to_bytes_lsb(tape + 8, 6U);
  nexus_random_generator_initialize_tape(&generator, tape, 12U);
  error = nexus_random_generator_uint32_range_get(&generator, 100U, 110U, &value32);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value32 != 106U)
    return nexus_test_fail(1192);
  nexus_bits_uint64_to_bytes_lsb(tape, 0U);
  nexus_bits_uint64_to_bytes_lsb(tape + 8, 5U);
  nexus_bits_uint64_to_bytes_lsb(tape + 16, 6U);
  nexus_random_generator_initialize_tape(&generator, tape, 24U);
  error = nexus_random_generator_uint64_range_get(&generator, 100U, 110U, &value64);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value64 != 106U)
    return nexus_test_fail(1193);

  nexus_memory_bytes_clear(tape, NEXUS_SIZEOF(tape));
  nexus_random_generator_initialize_tape(&generator, tape, 4U);
  value32 = 123U;
  error   = nexus_random_generator_uint32_range_get(&generator, 100U, 110U, &value32);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || value32 != 123U)
    return nexus_test_fail(1194);
  nexus_random_generator_initialize_tape(&generator, tape, 8U);
  value64 = 123U;
  error   = nexus_random_generator_uint64_range_get(&generator, 100U, 110U, &value64);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || value64 != 123U)
    return nexus_test_fail(1195);
  source.state = NULL;
  source.fill  = nexus_test_random_failing_fill;
  error        = nexus_random_source_uint32_range_get(source, 100U, 110U, &value32);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || value32 != 123U)
    return nexus_test_fail(1196);
  error = nexus_random_source_uint64_range_get(source, 100U, 110U, &value64);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || value64 != 123U)
    return nexus_test_fail(1197);
  return 0;
}

static int nexus_test_random_signed32_ranges(void) {
  static const struct {
    int32  minimum;
    int32  maximum;
    uint32 draw;
    int32  expected;
  } cases[] = {{10, 20, 10U, 10},
               {-20, -10, 10U, -20},
               {-20, -10, 9U, -11},
               {-5, 5, 6U, 1},
               {-5, 5, 9U, 4},
               {-5, 5, 10U, -5},
               {-1, 1, 0U, -1},
               {-1, 1, 1U, 0},
               {INT32_MIN_VAL, INT32_MIN_VAL + 1, 0U, INT32_MIN_VAL},
               {INT32_MAX_VAL - 1, INT32_MAX_VAL, UINT32_MAX_VAL, INT32_MAX_VAL - 1},
               {INT32_MIN_VAL, 0, 0U, INT32_MIN_VAL},
               {INT32_MIN_VAL, 0, 0x7fffffffU, -1},
               {INT32_MIN_VAL, INT32_MAX_VAL, 1U, INT32_MIN_VAL + 1},
               {INT32_MIN_VAL, INT32_MAX_VAL, 0x80000000U, 0},
               {INT32_MIN_VAL, INT32_MAX_VAL, 0xfffffffeU, INT32_MAX_VAL - 1}};
  NexusRandomGenerator generator;
  NexusRandomSource    source;
  NexusError          *error;
  byte                 tape[12];
  uint_large           index;
  int32                value;

  for (index = 0; index < NEXUS_SIZEOF(cases) / NEXUS_SIZEOF(cases[0]); index++) {
    nexus_bits_uint32_to_bytes_lsb(tape, cases[index].draw);
    nexus_random_generator_initialize_tape(&generator, tape, 4U);
    source = nexus_random_generator_source_get(&generator);
    if (index % 2U == 0U)
      error = nexus_random_source_int32_range_get(source, cases[index].minimum, cases[index].maximum, &value);
    else
      error = nexus_random_generator_int32_range_get(&generator, cases[index].minimum, cases[index].maximum, &value);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value != cases[index].expected)
      return nexus_test_fail(1200);
  }
  nexus_bits_uint32_to_bytes_lsb(tape, 0U);
  nexus_bits_uint32_to_bytes_lsb(tape + 4, 5U);
  nexus_bits_uint32_to_bytes_lsb(tape + 8, 6U);
  nexus_random_generator_initialize_tape(&generator, tape, NEXUS_SIZEOF(tape));
  error = nexus_random_generator_int32_range_get(&generator, -5, 5, &value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value != 1)
    return nexus_test_fail(1201);
  nexus_random_generator_initialize_tape(&generator, tape, 4U);
  value = 123;
  error = nexus_random_generator_int32_range_get(&generator, -5, 5, &value);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || value != 123)
    return nexus_test_fail(1202);
  source.state = NULL;
  source.fill  = nexus_test_random_failing_fill;
  error        = nexus_random_source_int32_range_get(source, -5, 5, &value);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || value != 123)
    return nexus_test_fail(1203);
#if NEXUS_DEVELOPER_MODE
  {
    jmp_buf recovery_point;
    for (index = 0; index < 6U; index++) {
      nexus_random_generator_initialize_counter(&generator, 0U, 1U);
      source = nexus_random_generator_source_get(&generator);
      if (setjmp(recovery_point) == 0) {
        nexus_program_recovery_arm(&recovery_point);
        switch (index) {
        case 0:
          error = nexus_random_generator_int32_range_get(&generator, -1, -1, &value);
          break;
        case 1:
          error = nexus_random_generator_int32_range_get(&generator, 1, -1, &value);
          break;
        case 2:
          source.fill = NULL;
          error       = nexus_random_source_int32_range_get(source, -1, 1, &value);
          break;
        case 3:
          error = nexus_random_source_int32_range_get(source, -1, 1, NULL);
          break;
        case 4:
          error = nexus_random_generator_int32_range_get(NULL, -1, 1, &value);
          break;
        default:
          error = nexus_random_generator_int32_range_get(&generator, -1, 1, NULL);
          break;
        }
        (void)error;
        nexus_program_recovery_disarm();
        return nexus_test_fail(1204);
      }
      nexus_program_recovery_disarm();
      error = nexus_program_recovery_last_error_get();
      if (error->code != GLOBAL_NEXUS_PRECONDITION_VIOLATION)
        return nexus_test_fail(1205);
    }
  }
#endif
  return 0;
}

static int nexus_test_random_signed64_ranges(void) {
  static const struct {
    int64  minimum;
    int64  maximum;
    uint64 draw;
    int64  expected;
  } cases[] = {{10, 20, 10U, 10},
               {-20, -10, 10U, -20},
               {-20, -10, 9U, -11},
               {-5, 5, 6U, 1},
               {-5, 5, 9U, 4},
               {-5, 5, 10U, -5},
               {-1, 1, 0U, -1},
               {-1, 1, 1U, 0},
               {INT64_MIN_VAL, INT64_MIN_VAL + 1, 0U, INT64_MIN_VAL},
               {INT64_MAX_VAL - 1, INT64_MAX_VAL, UINT64_MAX_VAL, INT64_MAX_VAL - 1},
               {INT64_MIN_VAL, 0, 0U, INT64_MIN_VAL},
               {INT64_MIN_VAL, 0, 0x7fffffffffffffffULL, -1},
               {INT64_MIN_VAL, INT64_MAX_VAL, 1U, INT64_MIN_VAL + 1},
               {INT64_MIN_VAL, INT64_MAX_VAL, 0x8000000000000000ULL, 0},
               {INT64_MIN_VAL, INT64_MAX_VAL, 0xfffffffffffffffeULL, INT64_MAX_VAL - 1}};
  NexusRandomGenerator generator;
  NexusRandomSource    source;
  NexusError          *error;
  byte                 tape[24];
  uint_large           index;
  int64                value;

  for (index = 0; index < NEXUS_SIZEOF(cases) / NEXUS_SIZEOF(cases[0]); index++) {
    nexus_bits_uint64_to_bytes_lsb(tape, cases[index].draw);
    nexus_random_generator_initialize_tape(&generator, tape, 8U);
    source = nexus_random_generator_source_get(&generator);
    if (index % 2U == 0U)
      error = nexus_random_source_int64_range_get(source, cases[index].minimum, cases[index].maximum, &value);
    else
      error = nexus_random_generator_int64_range_get(&generator, cases[index].minimum, cases[index].maximum, &value);
    if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value != cases[index].expected)
      return nexus_test_fail(1210);
  }
  nexus_bits_uint64_to_bytes_lsb(tape, 0U);
  nexus_bits_uint64_to_bytes_lsb(tape + 8, 5U);
  nexus_bits_uint64_to_bytes_lsb(tape + 16, 6U);
  nexus_random_generator_initialize_tape(&generator, tape, NEXUS_SIZEOF(tape));
  error = nexus_random_generator_int64_range_get(&generator, -5, 5, &value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || value != 1)
    return nexus_test_fail(1211);
  nexus_random_generator_initialize_tape(&generator, tape, 8U);
  value = 123;
  error = nexus_random_generator_int64_range_get(&generator, -5, 5, &value);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_RANDOM_ERROR_TAPE_EXHAUSTED) || value != 123)
    return nexus_test_fail(1212);
  source.state = NULL;
  source.fill  = nexus_test_random_failing_fill;
  error        = nexus_random_source_int64_range_get(source, -5, 5, &value);
  if (!nexus_test_random_error_has_code(error, GLOBAL_NEXUS_ENTROPY_ERROR_SOURCE_FAILED) || value != 123)
    return nexus_test_fail(1213);
#if NEXUS_DEVELOPER_MODE
  {
    jmp_buf recovery_point;
    for (index = 0; index < 6U; index++) {
      nexus_random_generator_initialize_counter(&generator, 0U, 1U);
      source = nexus_random_generator_source_get(&generator);
      if (setjmp(recovery_point) == 0) {
        nexus_program_recovery_arm(&recovery_point);
        switch (index) {
        case 0:
          error = nexus_random_generator_int64_range_get(&generator, -1, -1, &value);
          break;
        case 1:
          error = nexus_random_generator_int64_range_get(&generator, 1, -1, &value);
          break;
        case 2:
          source.fill = NULL;
          error       = nexus_random_source_int64_range_get(source, -1, 1, &value);
          break;
        case 3:
          error = nexus_random_source_int64_range_get(source, -1, 1, NULL);
          break;
        case 4:
          error = nexus_random_generator_int64_range_get(NULL, -1, 1, &value);
          break;
        default:
          error = nexus_random_generator_int64_range_get(&generator, -1, 1, NULL);
          break;
        }
        (void)error;
        nexus_program_recovery_disarm();
        return nexus_test_fail(1214);
      }
      nexus_program_recovery_disarm();
      error = nexus_program_recovery_last_error_get();
      if (error->code != GLOBAL_NEXUS_PRECONDITION_VIOLATION)
        return nexus_test_fail(1215);
    }
  }
#endif
  return 0;
}

static int nexus_test_random_seeds(void) {
  NexusRandomGenerator first;
  NexusRandomGenerator replay;
  NexusError          *error;
  byte                 bytes[34];
  uint32               seed32;
  uint64               seed64;
  uint128              seed128;
  uint64               first_value;
  uint64               replay_value;

  error = nexus_random_seed_fill(NULL, 0U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1180);
  bytes[0]  = 0xa5U;
  bytes[33] = 0x5aU;
  error     = nexus_random_seed_fill(bytes + 1, 32U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || bytes[0] != 0xa5U || bytes[33] != 0x5aU)
    return nexus_test_fail(1181);
  error = nexus_random_seed_uint32_get(&seed32);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1182);
  error = nexus_random_seed_uint64_get(&seed64);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1183);
  error = nexus_random_seed_uint128_get(&seed128);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1184);

  /* A newly drawn seed can be recorded and used to reproduce later draws. */
  nexus_random_generator_initialize_xoroshiro128(&first, seed128);
  nexus_random_generator_initialize_xoroshiro128(&replay, seed128);
  error = nexus_random_generator_uint64_get(&first, &first_value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1185);
  error = nexus_random_generator_uint64_get(&replay, &replay_value);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS || first_value != replay_value)
    return nexus_test_fail(1186);
  return 0;
}

static int nexus_test_random(void) {
  NexusError *error;
  byte        bytes[513];
  int         result;

  error = nexus_entropy_system_fill(NULL, 0U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1150);
  error = nexus_entropy_system_fill(bytes, NEXUS_SIZEOF(bytes));
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1151);
  error = nexus_random_system_fill(NULL, NULL, 0U);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1152);
  error = nexus_random_source_fill(nexus_random_system_source_get(), bytes, NEXUS_SIZEOF(bytes));
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS)
    return nexus_test_fail(1153);
  result = nexus_test_random_vectors();
  if (result != 0)
    return result;
  nexus_program_errors_pool_reset();
  result = nexus_test_random_chunking();
  if (result != 0)
    return result;
  result = nexus_test_random_sampling();
  if (result != 0)
    return result;
  nexus_program_errors_pool_reset();
  result = nexus_test_random_failures();
  nexus_program_errors_pool_reset();
  if (result != 0)
    return result;
  result = nexus_test_random_ranges();
  nexus_program_errors_pool_reset();
  if (result != 0)
    return result;
  result = nexus_test_random_signed32_ranges();
  nexus_program_errors_pool_reset();
  if (result != 0)
    return result;
  result = nexus_test_random_signed64_ranges();
  nexus_program_errors_pool_reset();
  if (result != 0)
    return result;
  return nexus_test_random_seeds();
}

static int nexus_test_identity(void) {
  NexusUUID            namespace_uuid;
  NexusUUID            uuid;
  NexusUUID            parsed;
  NexusError          *error;
  char                 string[NEXUS_UUID_STRING_BUFFER_SIZE];
  byte                 tape[16];
  NexusRandomGenerator generator;
  NexusRandomSource    source;
  uint32               index;

  error = nexus_identity_uuid_parse("6ba7b810-9dad-11d1-80b4-00c04fd430c8", &namespace_uuid);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(30);
  }
  nexus_identity_uuid_v3_generate(namespace_uuid, "www.example.com", 15U, &uuid);
  nexus_identity_uuid_format(uuid, string);
  if (!nexus_strings_string_equals(string, "5df41881-3aed-3515-88a7-2f4a814cf09e")) {
    return nexus_test_fail(31);
  }
  nexus_identity_uuid_v5_generate(namespace_uuid, "www.example.com", 15U, &uuid);
  nexus_identity_uuid_format(uuid, string);
  if (!nexus_strings_string_equals(string, "2ed6657d-e927-568b-95e1-2665a8aea6a2")) {
    return nexus_test_fail(32);
  }
  error = nexus_identity_uuid_parse(string, &parsed);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || parsed.lo != uuid.lo || parsed.hi != uuid.hi) {
    return nexus_test_fail(33);
  }
  for (index = 0; index < 16U; index++) {
    tape[index] = (byte)index;
  }
  nexus_random_generator_initialize_tape(&generator, tape, 16U);
  source = nexus_random_generator_source_get(&generator);
  error  = nexus_identity_uuid_v4_generate_with_random(source, &uuid);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || nexus_identity_uuid_version_get(uuid) != 4U) {
    return nexus_test_fail(34);
  }
  nexus_identity_uuid_format(uuid, string);
  if (string[14] != '4' || (string[19] != '8' && string[19] != '9' && string[19] != 'a' && string[19] != 'b')) {
    return nexus_test_fail(35);
  }
  return 0;
}

static int nexus_test_data_contiguous(void) {
  NexusDataArray       array;
  NexusDataList       *list;
  NexusDataStack      *stack;
  NexusDataQueue      *queue;
  NexusDataRingBuffer *ringbuffer;
  int                  values[3] = {1, 2, 3};
  NexusDataList       *pointer_list;
  const char *const   *names;
  int                  value;
  int                  out_value;
  const int           *items;
  uint_large           index;

  array = nexus_data_array_view_create(values, sizeof(int), 3U);
  if (nexus_data_array_data_get(&array) != (void *)values || nexus_data_array_data_get_const(&array) != (const void *)values) {
    return nexus_test_fail(70);
  }

  list = nexus_data_list_create(sizeof(int), 0U);
  if (nexus_data_list_data_get(list) != NULL) {
    return nexus_test_fail(71);
  }
  for (value = 1; value <= 3; value++) {
    nexus_data_list_append(list, &value);
  }
  items = (const int *)nexus_data_list_data_get_const(list);
  if (items != nexus_data_list_at(list, 0U) || items[0] != 1 || items[1] != 2 || items[2] != 3) {
    return nexus_test_fail(72);
  }
  nexus_data_list_destroy(list);

  pointer_list = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 0U);
  nexus_data_list_append_pointer(pointer_list, "first");
  nexus_data_list_append_pointer(pointer_list, "second");
  names = (const char *const *)nexus_data_list_data_get_const(pointer_list);
  if (names[0] != nexus_data_list_pointer_at(pointer_list, 0U) || names[1][0] != 's' || nexus_data_list_pointer_at(pointer_list, 1U) != names[1]) {
    return nexus_test_fail(73);
  }
  nexus_data_list_destroy(pointer_list);

  stack = nexus_data_stack_create(sizeof(int), 0U);
  for (value = 1; value <= 3; value++) {
    nexus_data_stack_push(stack, &value);
  }
  items = (const int *)nexus_data_stack_data_get_const(stack);
  if (items == NULL || items[0] != 1 || items[2] != 3) {
    return nexus_test_fail(74);
  }
  nexus_data_stack_destroy(stack);

  /* wrap the queue: fill 4 slots, drop 3, add 3 so head sits at slot 3 and the data straddles the end */
  queue = nexus_data_queue_create(sizeof(int), 4U);
  if (nexus_data_queue_linearize(queue) == NULL) {
    return nexus_test_fail(75);
  }
  for (value = 1; value <= 4; value++) {
    nexus_data_queue_push(queue, &value);
  }
  for (index = 0; index < 3U; index++) {
    nexus_data_queue_pop(queue, &out_value);
  }
  for (value = 5; value <= 7; value++) {
    nexus_data_queue_push(queue, &value);
  }
  items = (const int *)nexus_data_queue_linearize(queue);
  for (index = 0; index < 4U; index++) {
    if (items[index] != (int)(index + 4U)) {
      return nexus_test_fail(76);
    }
  }
  if (!nexus_data_queue_pop(queue, &out_value) || out_value != 4) {
    return nexus_test_fail(77);
  }
  nexus_data_queue_destroy(queue);

  /* overwrite-on-full ring: after 5 pushes into 3 slots the retained values are 3,4,5 with head mid-buffer */
  ringbuffer = nexus_data_ringbuffer_create(sizeof(int), 3U);
  for (value = 1; value <= 5; value++) {
    nexus_data_ringbuffer_push(ringbuffer, &value);
  }
  items = (const int *)nexus_data_ringbuffer_linearize(ringbuffer);
  for (index = 0; index < 3U; index++) {
    if (items[index] != (int)(index + 3U)) {
      return nexus_test_fail(78);
    }
  }
  if (*(int *)nexus_data_ringbuffer_at(ringbuffer, 0U) != 3) {
    return nexus_test_fail(79);
  }
  nexus_data_ringbuffer_destroy(ringbuffer);
  return 0;
}

static int nexus_test_bits(void) {
  byte   bytes[8];
  uint8  b[8];
  uint32 index;
  uint32 value32;
  uint64 value64;

  if (nexus_bits_uint32_popcount(0U) != 0U || nexus_bits_uint32_popcount(0xF0F0F0F0U) != 16U || nexus_bits_uint32_popcount(0xFFFFFFFFU) != 32U) {
    return nexus_test_fail(320);
  }
  if (nexus_bits_uint64_popcount(0ULL) != 0U || nexus_bits_uint64_popcount(0x8000000000000001ULL) != 2U ||
      nexus_bits_uint64_popcount(0xFFFFFFFFFFFFFFFFULL) != 64U) {
    return nexus_test_fail(321);
  }
  if (nexus_bits_uint32_trailing_zeros(1U) != 0U || nexus_bits_uint32_trailing_zeros(0x80000000U) != 31U ||
      nexus_bits_uint32_trailing_zeros(0x00000140U) != 6U) {
    return nexus_test_fail(322);
  }
  if (nexus_bits_uint64_trailing_zeros(1ULL) != 0U || nexus_bits_uint64_trailing_zeros(0x8000000000000000ULL) != 63U) {
    return nexus_test_fail(323);
  }
  if (nexus_bits_uint32_leading_zeros(0x80000000U) != 0U || nexus_bits_uint32_leading_zeros(1U) != 31U ||
      nexus_bits_uint32_leading_zeros(0x00010000U) != 15U) {
    return nexus_test_fail(324);
  }
  if (nexus_bits_uint64_leading_zeros(0x8000000000000000ULL) != 0U || nexus_bits_uint64_leading_zeros(1ULL) != 63U) {
    return nexus_test_fail(325);
  }
  if (nexus_bits_uint64_clear_lowest_set(0x68ULL) != 0x60ULL || nexus_bits_uint32_clear_lowest_set(0x68U) != 0x60U) {
    return nexus_test_fail(326);
  }

  if (!nexus_bits_uint32_get_lsb(0x00000100U, 8U) || nexus_bits_uint32_get_lsb(0x00000100U, 7U) ||
      nexus_bits_uint32_set_lsb(0U, 31U, TRUE) != 0x80000000U || nexus_bits_uint32_set_lsb(0xFFFFFFFFU, 0U, FALSE) != 0xFFFFFFFEU) {
    return nexus_test_fail(327);
  }

  for (index = 0U; index < 8U; index++) {
    const uint8  mask          = (uint8)((uint8)1U << index);
    const uint8  all_bits      = (uint8) ~(uint8)0U;
    const uint32 reverse_index = 8U - 1U - index;

    if (nexus_bits_uint8_set_lsb(0U, index, TRUE) != mask || nexus_bits_uint8_set_msb(0U, reverse_index, TRUE) != mask ||
        nexus_bits_uint8_set_lsb(all_bits, index, FALSE) != (uint8)(all_bits ^ mask) ||
        nexus_bits_uint8_set_msb(all_bits, reverse_index, FALSE) != (uint8)(all_bits ^ mask) || nexus_bits_uint8_set_lsb(mask, index, TRUE) != mask ||
        nexus_bits_uint8_set_msb(mask, reverse_index, TRUE) != mask || nexus_bits_uint8_set_lsb(0U, index, FALSE) != 0U ||
        nexus_bits_uint8_set_msb(0U, reverse_index, FALSE) != 0U || !nexus_bits_uint8_get_lsb(mask, index) ||
        !nexus_bits_uint8_get_msb(mask, reverse_index) || nexus_bits_uint8_get_lsb((uint8)(all_bits ^ mask), index) ||
        nexus_bits_uint8_get_msb((uint8)(all_bits ^ mask), reverse_index)) {
      return nexus_test_fail(350);
    }
  }

  for (index = 0U; index < 16U; index++) {
    const uint16 mask          = (uint16)((uint16)1U << index);
    const uint16 all_bits      = (uint16) ~(uint16)0U;
    const uint32 reverse_index = 16U - 1U - index;

    if (nexus_bits_uint16_set_lsb(0U, index, TRUE) != mask || nexus_bits_uint16_set_msb(0U, reverse_index, TRUE) != mask ||
        nexus_bits_uint16_set_lsb(all_bits, index, FALSE) != (uint16)(all_bits ^ mask) ||
        nexus_bits_uint16_set_msb(all_bits, reverse_index, FALSE) != (uint16)(all_bits ^ mask) ||
        nexus_bits_uint16_set_lsb(mask, index, TRUE) != mask || nexus_bits_uint16_set_msb(mask, reverse_index, TRUE) != mask ||
        nexus_bits_uint16_set_lsb(0U, index, FALSE) != 0U || nexus_bits_uint16_set_msb(0U, reverse_index, FALSE) != 0U ||
        !nexus_bits_uint16_get_lsb(mask, index) || !nexus_bits_uint16_get_msb(mask, reverse_index) ||
        nexus_bits_uint16_get_lsb((uint16)(all_bits ^ mask), index) || nexus_bits_uint16_get_msb((uint16)(all_bits ^ mask), reverse_index)) {
      return nexus_test_fail(351);
    }
  }

  for (index = 0U; index < 32U; index++) {
    const uint32 mask          = (uint32)1U << index;
    const uint32 all_bits      = ~(uint32)0U;
    const uint32 reverse_index = 32U - 1U - index;

    if (nexus_bits_uint32_set_lsb(0U, index, TRUE) != mask || nexus_bits_uint32_set_msb(0U, reverse_index, TRUE) != mask ||
        nexus_bits_uint32_set_lsb(all_bits, index, FALSE) != (uint32)(all_bits ^ mask) ||
        nexus_bits_uint32_set_msb(all_bits, reverse_index, FALSE) != (uint32)(all_bits ^ mask) ||
        nexus_bits_uint32_set_lsb(mask, index, TRUE) != mask || nexus_bits_uint32_set_msb(mask, reverse_index, TRUE) != mask ||
        nexus_bits_uint32_set_lsb(0U, index, FALSE) != 0U || nexus_bits_uint32_set_msb(0U, reverse_index, FALSE) != 0U ||
        !nexus_bits_uint32_get_lsb(mask, index) || !nexus_bits_uint32_get_msb(mask, reverse_index) ||
        nexus_bits_uint32_get_lsb((uint32)(all_bits ^ mask), index) || nexus_bits_uint32_get_msb((uint32)(all_bits ^ mask), reverse_index)) {
      return nexus_test_fail(352);
    }
  }

  for (index = 0U; index < 64U; index++) {
    const uint64 mask          = (uint64)1U << index;
    const uint64 all_bits      = ~(uint64)0U;
    const uint32 reverse_index = 64U - 1U - index;

    if (nexus_bits_uint64_set_lsb(0U, index, TRUE) != mask || nexus_bits_uint64_set_msb(0U, reverse_index, TRUE) != mask ||
        nexus_bits_uint64_set_lsb(all_bits, index, FALSE) != (uint64)(all_bits ^ mask) ||
        nexus_bits_uint64_set_msb(all_bits, reverse_index, FALSE) != (uint64)(all_bits ^ mask) ||
        nexus_bits_uint64_set_lsb(mask, index, TRUE) != mask || nexus_bits_uint64_set_msb(mask, reverse_index, TRUE) != mask ||
        nexus_bits_uint64_set_lsb(0U, index, FALSE) != 0U || nexus_bits_uint64_set_msb(0U, reverse_index, FALSE) != 0U ||
        !nexus_bits_uint64_get_lsb(mask, index) || !nexus_bits_uint64_get_msb(mask, reverse_index) ||
        nexus_bits_uint64_get_lsb((uint64)(all_bits ^ mask), index) || nexus_bits_uint64_get_msb((uint64)(all_bits ^ mask), reverse_index)) {
      return nexus_test_fail(353);
    }
  }

  for (index = 0U; index < (uint32)(NEXUS_SIZEOF(uint_large) * 8U); index++) {
    const uint_large mask          = (uint_large)1U << index;
    const uint_large all_bits      = ~(uint_large)0U;
    const uint32     reverse_index = (uint32)(NEXUS_SIZEOF(uint_large) * 8U) - 1U - index;

    if (nexus_bits_uint_large_set_lsb(0U, index, TRUE) != mask || nexus_bits_uint_large_set_msb(0U, reverse_index, TRUE) != mask ||
        nexus_bits_uint_large_set_lsb(all_bits, index, FALSE) != (uint_large)(all_bits ^ mask) ||
        nexus_bits_uint_large_set_msb(all_bits, reverse_index, FALSE) != (uint_large)(all_bits ^ mask) ||
        nexus_bits_uint_large_set_lsb(mask, index, TRUE) != mask || nexus_bits_uint_large_set_msb(mask, reverse_index, TRUE) != mask ||
        nexus_bits_uint_large_set_lsb(0U, index, FALSE) != 0U || nexus_bits_uint_large_set_msb(0U, reverse_index, FALSE) != 0U ||
        !nexus_bits_uint_large_get_lsb(mask, index) || !nexus_bits_uint_large_get_msb(mask, reverse_index) ||
        nexus_bits_uint_large_get_lsb((uint_large)(all_bits ^ mask), index) ||
        nexus_bits_uint_large_get_msb((uint_large)(all_bits ^ mask), reverse_index)) {
      return nexus_test_fail(354);
    }
  }

  nexus_bits_uint32_to_bytes_lsb(bytes, 0x01020304U);
  if (bytes[0] != 4U || bytes[3] != 1U || nexus_bits_uint32_from_bytes_lsb(bytes) != 0x01020304U) {
    return nexus_test_fail(328);
  }
  nexus_bits_uint32_to_bytes_msb(bytes, 0x01020304U);
  if (bytes[0] != 1U || bytes[3] != 4U || nexus_bits_uint32_from_bytes_msb(bytes) != 0x01020304U) {
    return nexus_test_fail(329);
  }
  nexus_bits_uint64_to_bytes_msb(bytes, 0x0102030405060708ULL);
  if (bytes[0] != 1U || bytes[7] != 8U || nexus_bits_uint64_from_bytes_msb(bytes) != 0x0102030405060708ULL) {
    return nexus_test_fail(330);
  }

  if (nexus_bits_uint32_from_real32(1.0F) != 0x3F800000U || nexus_bits_real32_from_uint32(0x3F800000U) != 1.0F ||
      nexus_bits_uint64_from_real64(1.0) != 0x3FF0000000000000ULL) {
    return nexus_test_fail(331);
  }

  value32 = nexus_bits_uint32_pack_4u8_lsb(1U, 2U, 3U, 4U);
  nexus_bits_uint32_unpack_4u8_msb(value32, &b[0], &b[1], &b[2], &b[3]);
  if (value32 != 0x04030201U || b[0] != 4U || b[3] != 1U) {
    return nexus_test_fail(332);
  }
  value64 = nexus_bits_uint64_pack_2u32_msb(0xAABBCCDDU, 0x11223344U);
  if (value64 != 0xAABBCCDD11223344ULL || nexus_bits_uint64_hi_u32(value64) != 0xAABBCCDDU || nexus_bits_uint64_lo_u32(value64) != 0x11223344U) {
    return nexus_test_fail(333);
  }

  return 0;
}

static int nexus_test_data_bitset(void) {
  /* 70 bits spans three words with a partial last word, so word boundaries and tail masking are both exercised. */
  uint32          storage_a[NEXUS_DATA_BITSET_WORD_COUNT(70U)];
  uint32          storage_b[NEXUS_DATA_BITSET_WORD_COUNT(70U)];
  NexusDataBitset a;
  NexusDataBitset b;

  a = nexus_data_bitset_view_create(storage_a, 70U);
  b = nexus_data_bitset_view_create(storage_b, 70U);
  nexus_data_bitset_clear_all(&a);
  nexus_data_bitset_clear_all(&b);

  if (nexus_data_bitset_count(&a) != 0U || !nexus_data_bitset_is_subset(&a, &b) || !nexus_data_bitset_equals(&a, &b)) {
    return nexus_test_fail(300);
  }

  nexus_data_bitset_set(&a, 0U);
  nexus_data_bitset_set(&a, 31U);
  nexus_data_bitset_set(&a, 32U);
  nexus_data_bitset_set(&a, 69U);
  if (nexus_data_bitset_count(&a) != 4U || !nexus_data_bitset_test(&a, 31U) || !nexus_data_bitset_test(&a, 32U) || !nexus_data_bitset_test(&a, 69U) ||
      nexus_data_bitset_test(&a, 1U) || nexus_data_bitset_test(&a, 68U)) {
    return nexus_test_fail(301);
  }

  nexus_data_bitset_clear(&a, 31U);
  if (nexus_data_bitset_test(&a, 31U) || nexus_data_bitset_count(&a) != 3U) {
    return nexus_test_fail(302);
  }

  /* a = {0, 32, 69}; b = {32, 69} -> b is a subset of a, not the reverse. */
  nexus_data_bitset_set(&b, 32U);
  nexus_data_bitset_set(&b, 69U);
  if (!nexus_data_bitset_is_subset(&b, &a) || nexus_data_bitset_is_subset(&a, &b) || nexus_data_bitset_equals(&a, &b)) {
    return nexus_test_fail(303);
  }

  nexus_data_bitset_difference_into(&a, &b);
  if (nexus_data_bitset_count(&a) != 1U || !nexus_data_bitset_test(&a, 0U)) {
    return nexus_test_fail(304);
  }

  nexus_data_bitset_union_into(&a, &b);
  if (nexus_data_bitset_count(&a) != 3U || !nexus_data_bitset_is_subset(&b, &a)) {
    return nexus_test_fail(305);
  }

  nexus_data_bitset_intersect_into(&a, &b);
  if (!nexus_data_bitset_equals(&a, &b) || nexus_data_bitset_count(&a) != 2U) {
    return nexus_test_fail(306);
  }

  /* Dirty tail bits (past bit_count) must not leak into any query. */
  storage_a[2] |= 0xFFFFFFC0U;
  if (nexus_data_bitset_count(&a) != 2U || !nexus_data_bitset_equals(&a, &b) || !nexus_data_bitset_is_subset(&a, &b)) {
    return nexus_test_fail(307);
  }

  return 0;
}

static int nexus_test_data(void) {
  NexusDataArray          array;
  NexusDataList          *list;
  NexusDataQueue         *queue;
  NexusDataRingBuffer    *ringbuffer;
  NexusDataPriorityQueue *priority_queue;
  NexusDataHashMapConfig  config;
  NexusDataHashMap       *hashmap;
  NexusError             *error;
  int                     values[4] = {4, 1, 3, 2};
  int                     value;
  int                     key;
  int                     out_value;
  boolean                 inserted;

  array = nexus_data_array_view_create(values, sizeof(int), 4U);
  nexus_data_array_sort(&array, nexus_test_compare_ints, NULL);
  if (values[0] != 1 || values[3] != 4 || !nexus_data_array_binary_search(&array, &values[2], nexus_test_compare_ints, NULL, NULL)) {
    return nexus_test_fail(40);
  }
  list  = nexus_data_list_create(sizeof(int), 0U);
  value = 7;
  nexus_data_list_append(list, &value);
  value = 8;
  nexus_data_list_append(list, &value);
  if (nexus_data_list_length_get(list) != 2U) {
    return nexus_test_fail(41);
  }
  nexus_data_list_destroy(list);
  queue = nexus_data_queue_create(sizeof(int), 1U);
  value = 9;
  nexus_data_queue_push(queue, &value);
  if (!nexus_data_queue_pop(queue, &out_value) || out_value != 9) {
    return nexus_test_fail(42);
  }
  nexus_data_queue_destroy(queue);
  ringbuffer = nexus_data_ringbuffer_create(sizeof(int), 2U);
  value      = 1;
  nexus_data_ringbuffer_push(ringbuffer, &value);
  value = 2;
  nexus_data_ringbuffer_push(ringbuffer, &value);
  value = 3;
  if (!nexus_data_ringbuffer_push(ringbuffer, &value) || *(int *)nexus_data_ringbuffer_at(ringbuffer, 0U) != 2) {
    return nexus_test_fail(43);
  }
  nexus_data_ringbuffer_destroy(ringbuffer);
  priority_queue = nexus_data_priority_queue_create(sizeof(int), 0U, nexus_test_compare_ints, NULL);
  value          = 4;
  nexus_data_priority_queue_push(priority_queue, &value);
  value = 1;
  nexus_data_priority_queue_push(priority_queue, &value);
  if (!nexus_data_priority_queue_pop(priority_queue, &out_value) || out_value != 1) {
    return nexus_test_fail(44);
  }
  nexus_data_priority_queue_destroy(priority_queue);
  nexus_data_hashmap_config_initialize(&config, sizeof(key), sizeof(value));
  config.seed_is_set = TRUE;
  config.seed        = 7U;
  error              = nexus_data_hashmap_create(&config, &hashmap);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(45);
  }
  key   = 5;
  value = 42;
  nexus_data_hashmap_put(hashmap, &key, &value);
  key = 5;
  if (!nexus_data_hashmap_get_copy(hashmap, &key, &out_value) || out_value != 42) {
    return nexus_test_fail(46);
  }
  key = 6;
  (void)nexus_data_hashmap_insert(hashmap, &key, &inserted);
  if (!inserted || !nexus_data_hashmap_remove(hashmap, &key, NULL)) {
    return nexus_test_fail(47);
  }
  nexus_data_hashmap_destroy(hashmap);
  return 0;
}

/* Deliberately fails a NEXUS_PROGRAM_PRECONDITION_ALWAYS (checked in every build, unlike the
   plain macro) while nexus_test_recovery has armed a recovery point -- exercises
   nexus_program_recovery_arm/_disarm/_last_error_get without needing NEXUS_DEVELOPER_MODE on. */
static void nexus_test_recovery_trigger(void) {
  NEXUS_PROGRAM_PRECONDITION_ALWAYS(FALSE, "deliberate test failure %d", 42);
}

static int nexus_test_recovery(void) {
  jmp_buf     recovery_point;
  NexusError *error;

  if (setjmp(recovery_point) == 0) {
    nexus_program_recovery_arm(&recovery_point);
    nexus_test_recovery_trigger();
    nexus_program_recovery_disarm();
    return nexus_test_fail(60); /* the trigger above must longjmp -- reaching here means it didn't */
  }
  nexus_program_recovery_disarm();

  error = nexus_program_recovery_last_error_get();
  if (error == NULL || error->code != GLOBAL_NEXUS_PRECONDITION_VIOLATION) {
    return nexus_test_fail(61);
  }
  if (error->message == NULL || !nexus_strings_string_equals(error->message, "deliberate test failure 42")) {
    return nexus_test_fail(62);
  }

  return 0;
}

#if NEXUS_PLATFORM_LINUX
static int nexus_test_process(void) {
  static const char *const true_argv[]       = {"/bin/true", NULL};
  static const char *const false_argv[]      = {"/bin/false", NULL};
  static const char *const segv_argv[]       = {"/bin/sh", "-c", "kill -SEGV $$", NULL};
  static const char *const echo_argv[]       = {"/bin/sh", "-c", "echo hello", NULL};
  static const char *const sleep_argv[]      = {"/bin/sh", "-c", "sleep 5", NULL};
  static const char        expected_stdout[] = "hello\n";

  NexusProcessSpawnOptions options;
  NexusProcess            *process;
  NexusProcessExitStatus   status;
  boolean                  has_exited;
  NexusError              *error;
  NexusDataList           *stdout_capture;
  int32                    sleeping_pid;
  uint_large               i;

  options.envp              = NULL;
  options.working_directory = NULL;
  options.stdin_mode        = NEXUS_PROCESS_STREAM_INHERIT;
  options.stdout_mode       = NEXUS_PROCESS_STREAM_INHERIT;
  options.stderr_mode       = NEXUS_PROCESS_STREAM_INHERIT;

  /* Normal exit, code 0. */
  options.argv = true_argv;
  error        = nexus_process_spawn(&options, &process);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(50);
  }
  error = nexus_process_wait(process, -1, &status, &has_exited);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || !has_exited || status.kind != NEXUS_PROCESS_EXIT_NORMAL || status.detail != 0) {
    return nexus_test_fail(51);
  }
  nexus_process_destroy(process);

  /* Normal exit, nonzero code. */
  options.argv = false_argv;
  error        = nexus_process_spawn(&options, &process);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(52);
  }
  error = nexus_process_wait(process, -1, &status, &has_exited);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || !has_exited || status.kind != NEXUS_PROCESS_EXIT_NORMAL || status.detail == 0) {
    return nexus_test_fail(53);
  }
  nexus_process_destroy(process);

  /* Killed by a signal -- reported as signaled, distinct from both a normal exit and an error. */
  options.argv = segv_argv;
  error        = nexus_process_spawn(&options, &process);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(54);
  }
  error = nexus_process_wait(process, -1, &status, &has_exited);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || !has_exited || status.kind != NEXUS_PROCESS_EXIT_SIGNALED || status.detail != SIGSEGV) {
    return nexus_test_fail(55);
  }
  nexus_process_destroy(process);

  /* communicate() captures piped stdout. */
  options.argv        = echo_argv;
  options.stdout_mode = NEXUS_PROCESS_STREAM_PIPE;
  error               = nexus_process_spawn(&options, &process);
  options.stdout_mode = NEXUS_PROCESS_STREAM_INHERIT;
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(56);
  }
  stdout_capture = nexus_data_list_create(sizeof(byte), 0U);
  error          = nexus_process_communicate(process, NULL, 0U, stdout_capture, NULL, -1, &status, &has_exited);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || !has_exited || status.kind != NEXUS_PROCESS_EXIT_NORMAL || status.detail != 0 ||
      nexus_data_list_length_get(stdout_capture) != (uint_large)(sizeof(expected_stdout) - 1U)) {
    return nexus_test_fail(57);
  }
  for (i = 0; i < sizeof(expected_stdout) - 1U; i++) {
    if (*(const byte *)nexus_data_list_at_const(stdout_capture, i) != (byte)expected_stdout[i]) {
      return nexus_test_fail(57);
    }
  }
  nexus_data_list_destroy(stdout_capture);
  nexus_process_destroy(process);

  /* destroy() must not kill a still-running child. */
  options.argv = sleep_argv;
  error        = nexus_process_spawn(&options, &process);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(58);
  }
  sleeping_pid = nexus_process_id_get(process);
  nexus_process_destroy(process);
  if (kill((pid_t)sleeping_pid, 0) != 0) {
    return nexus_test_fail(59);
  }
  (void)kill((pid_t)sleeping_pid, SIGKILL);
  (void)waitpid((pid_t)sleeping_pid, NULL, 0);

  return 0;
}
#endif

static int nexus_test_strings(void) {
  NexusStringBuilder builder;
  NexusError        *error;
  uint_large         hint;

  /* A growable builder's initial capacity is only a hint for the first allocation, so the first append must work whatever the hint is. */
  for (hint = 0; hint <= 1024U; hint += 1024U) {
    builder = nexus_strings_string_builder_create_growable(hint);
    error   = nexus_strings_string_builder_append(&builder, "hello");
    if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || nexus_strings_string_builder_length_get(&builder) != 5U) {
      nexus_strings_string_builder_destroy(&builder);
      return nexus_test_fail(600);
    }
    error = nexus_strings_string_builder_append_formatted(&builder, " %u", 42U);
    if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || nexus_strings_string_builder_length_get(&builder) != 8U) {
      nexus_strings_string_builder_destroy(&builder);
      return nexus_test_fail(601);
    }
    nexus_strings_string_builder_destroy(&builder);
  }

  /* A builder that is destroyed and then reused keeps its hint but has no buffer, which must not matter either. */
  builder = nexus_strings_string_builder_create_growable(1024U);
  (void)nexus_strings_string_builder_append(&builder, "a");
  nexus_strings_string_builder_destroy(&builder);
  error = nexus_strings_string_builder_append(&builder, "b");
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || nexus_strings_string_builder_length_get(&builder) != 1U) {
    nexus_strings_string_builder_destroy(&builder);
    return nexus_test_fail(602);
  }
  nexus_strings_string_builder_destroy(&builder);
  return 0;
}

#define NEXUS_TEST_MEMORY_THREADS    4U
#define NEXUS_TEST_MEMORY_ITERATIONS 20000U

static void nexus_test_memory_churn(void *user_data) {
  uint_large index;
  void      *block;
  (void)user_data;

  for (index = 0; index < NEXUS_TEST_MEMORY_ITERATIONS; index++) {
    block = nexus_memory_allocate(16U + (index % 48U));
    nexus_memory_deallocate(block);
  }
}

/* The header checksum covers the list links other threads rewrite while allocating and freeing, so marking and commenting a block while they do
   must not corrupt it (it used to trip "Allocation header got corrupted" at random). */
static int nexus_test_memory_threads(void) {
  NexusThread *threads[NEXUS_TEST_MEMORY_THREADS];
  uint_large   index;
  void        *block;

  for (index = 0; index < NEXUS_TEST_MEMORY_THREADS; index++) {
    if (nexus_threads_create(&threads[index], nexus_test_memory_churn, NULL)->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
      return nexus_test_fail(610);
    }
  }

  for (index = 0; index < NEXUS_TEST_MEMORY_ITERATIONS; index++) {
    block = nexus_memory_allocate(32U);
    nexus_memory_mark_persistent(block);
    nexus_memory_comment_set(block, "nexus test block");
    nexus_memory_deallocate(block);
  }

  for (index = 0; index < NEXUS_TEST_MEMORY_THREADS; index++) {
    if (nexus_threads_join(threads[index])->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
      return nexus_test_fail(611);
    }
  }
  return 0;
}

#define NEXUS_TEST_SORT_COUNT 1500U

/* Sorts keys of a given shape and checks order, stability (equal keys keep their original index order) and that every value still belongs to its key. */
static int nexus_test_keys_sort_case(uint64 modulus, uint64 multiplier, uint32 code) {
  static uint64 keys[NEXUS_TEST_SORT_COUNT];
  static uint64 original[NEXUS_TEST_SORT_COUNT];
  static uint64 scratch_keys[NEXUS_TEST_SORT_COUNT];
  static uint32 values[NEXUS_TEST_SORT_COUNT];
  static uint32 scratch_values[NEXUS_TEST_SORT_COUNT];
  uint64        state;
  uint32        index;

  state = 0x9E3779B97F4A7C15ULL;
  for (index = 0; index < NEXUS_TEST_SORT_COUNT; index++) {
    state           = (state * 6364136223846793005ULL) + 1442695040888963407ULL;
    keys[index]     = ((state >> 20) % modulus) * multiplier;
    original[index] = keys[index];
    values[index]   = index;
  }

  nexus_data_keys_radix_sort(keys, values, NEXUS_TEST_SORT_COUNT, scratch_keys, scratch_values);

  for (index = 0; index < NEXUS_TEST_SORT_COUNT; index++) {
    if (keys[index] != original[values[index]]) {
      return nexus_test_fail((int)code);
    }
    if (index > 0U && (keys[index - 1U] > keys[index] || (keys[index - 1U] == keys[index] && values[index - 1U] > values[index]))) {
      return nexus_test_fail((int)code + 1);
    }
  }
  return 0;
}

static int nexus_test_keys_sort(void) {
  uint64 empty_keys[1];
  uint32 empty_values[1];
  int    result;

  nexus_data_keys_radix_sort(empty_keys, empty_values, 0U, empty_keys, empty_values);

  result = nexus_test_keys_sort_case(0x7FFFFFFFFFFFFFULL, 1ULL, 620); /* wide keys: all eight bytes vary */
  if (result == 0) {
    result = nexus_test_keys_sort_case(40ULL, 1ULL, 622); /* one byte varies: an odd pass count */
  }
  if (result == 0) {
    result = nexus_test_keys_sort_case(300ULL, 0x0001000000000000ULL, 624); /* two bytes vary, far apart */
  }
  if (result == 0) {
    result = nexus_test_keys_sort_case(1ULL, 1ULL, 626); /* every key equal: every pass skipped */
  }
  return result;
}

/* ---------------------------------------------------------------------------- */
/* SLABBED DATA STRUCTURES                                                      */
/* ---------------------------------------------------------------------------- */

#define NEXUS_TEST_SLAB_COUNT 257U

static int nexus_test_data_slabbed_list(void) {
  NexusDataSlabbedList *list;
  NexusDataSlabbedList *pointer_list;
  NexusDataSlabbedList *odd_list;
  void                 *saved[NEXUS_TEST_SLAB_COUNT];
  uint_large            index;
  uint_large            logical_index;
  uint_large            capacity;
  int                   value;
  int                   output;
  void                 *slot;
  byte                  odd_value[3] = {3U, 5U, 7U};
  const byte           *odd_slot;

  list = nexus_data_slabbed_list_create(NEXUS_SIZEOF(int), 0, 3U);
  if (nexus_data_slabbed_list_length_get(list) != 0 || nexus_data_slabbed_list_capacity_get(list) != 0 || nexus_data_slabbed_list_pop(list, NULL)) {
    return nexus_test_fail(700);
  }
  /* Reverse input makes sorting move every ordering reference. */
  for (index = NEXUS_TEST_SLAB_COUNT; index > 0; index--) {
    value             = (int)(index - 1U);
    saved[index - 1U] = nexus_data_slabbed_list_append(list, &value);
  }
  value = -1;
  nexus_data_slabbed_list_insert(list, 0, &value);
  if (nexus_data_slabbed_list_at(list, 1U) != saved[NEXUS_TEST_SLAB_COUNT - 1U]) {
    return nexus_test_fail(701);
  }
  nexus_data_slabbed_list_remove(list, 0, &output);
  if (output != -1) {
    return nexus_test_fail(702);
  }
  /* Remove one interior payload, then reuse exactly that dead slot. */
  nexus_data_slabbed_list_remove(list, 10U, &output);
  if (output != (int)(NEXUS_TEST_SLAB_COUNT - 11U)) {
    return nexus_test_fail(703);
  }
  slot = nexus_data_slabbed_list_append(list, saved[0]);
  if (slot != saved[NEXUS_TEST_SLAB_COUNT - 11U] || *(int *)slot != 0) {
    return nexus_test_fail(704);
  }
  saved[NEXUS_TEST_SLAB_COUNT - 11U] = NULL;
  if (!nexus_data_slabbed_list_pop(list, &output) || output != 0) {
    return nexus_test_fail(705);
  }
  slot         = nexus_data_slabbed_list_append_uninitialized(list);
  *(int *)slot = -2;
  nexus_data_slabbed_list_pop(list, &output);
  if (output != -2) {
    return nexus_test_fail(706);
  }
  nexus_data_slabbed_list_sort(list, nexus_test_compare_ints, NULL);
  nexus_data_slabbed_list_reserve(list, 1024U);
  logical_index = 0;
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index++) {
    if (saved[index] != NULL) {
      if (*(int *)saved[index] != (int)index || nexus_data_slabbed_list_at_const(list, logical_index) != saved[index]) {
        return nexus_test_fail(707);
      }
      logical_index++;
    }
  }
  capacity = nexus_data_slabbed_list_capacity_get(list);
  if (capacity < 1024U || nexus_data_slabbed_list_length_get(list) != NEXUS_TEST_SLAB_COUNT - 1U) {
    return nexus_test_fail(708);
  }
  nexus_data_slabbed_list_clear(list);
  nexus_data_slabbed_list_sort(list, nexus_test_compare_ints, NULL);
  for (index = 0; index < capacity; index++) {
    value = (int)index;
    nexus_data_slabbed_list_append(list, &value);
  }
  if (nexus_data_slabbed_list_capacity_get(list) != capacity) {
    return nexus_test_fail(709);
  }
  nexus_data_slabbed_list_destroy(list);

  pointer_list = nexus_data_slabbed_list_create(NEXUS_SIZEOF(void *), 0, 0);
  nexus_data_slabbed_list_append_pointer(pointer_list, odd_value);
  nexus_data_slabbed_list_append_pointer(pointer_list, NULL);
  if (nexus_data_slabbed_list_pointer_at(pointer_list, 0) != odd_value || nexus_data_slabbed_list_pointer_at(pointer_list, 1U) != NULL) {
    return nexus_test_fail(710);
  }
  nexus_data_slabbed_list_destroy(pointer_list);
  /* Odd-sized records still receive maximally aligned, non-overlapping slots. */
  odd_list = nexus_data_slabbed_list_create(3U, 0, 1U);
  for (index = 0; index < 19U; index++) {
    odd_slot = (const byte *)nexus_data_slabbed_list_append(odd_list, odd_value);
    if ((uint_large)odd_slot % NEXUS_ALIGNOF(long double) != 0 || odd_slot[0] != 3U || odd_slot[2] != 7U) {
      return nexus_test_fail(711);
    }
  }
  nexus_data_slabbed_list_destroy(odd_list);
  nexus_data_slabbed_list_destroy(NULL);
  return 0;
}

static int nexus_test_data_slabbed_stack(void) {
  NexusDataSlabbedStack *stack;
  void                  *saved[NEXUS_TEST_SLAB_COUNT];
  uint_large             index;
  uint_large             capacity;
  int                    value;
  int                    output;

  stack = nexus_data_slabbed_stack_create(NEXUS_SIZEOF(int), 1U, 7U);
  if (nexus_data_slabbed_stack_peek(stack) != NULL || nexus_data_slabbed_stack_pop(stack, NULL)) {
    return nexus_test_fail(720);
  }
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index++) {
    value        = (int)index;
    saved[index] = nexus_data_slabbed_stack_push(stack, &value);
  }
  nexus_data_slabbed_stack_reserve(stack, 512U);
  capacity = nexus_data_slabbed_stack_capacity_get(stack);
  if (capacity < 512U || nexus_data_slabbed_stack_length_get(stack) != NEXUS_TEST_SLAB_COUNT) {
    return nexus_test_fail(721);
  }
  for (index = NEXUS_TEST_SLAB_COUNT; index > 0; index--) {
    if (nexus_data_slabbed_stack_peek(stack) != saved[index - 1U] || *(int *)saved[0] != 0 || !nexus_data_slabbed_stack_pop(stack, &output) ||
        output != (int)(index - 1U)) {
      return nexus_test_fail(722);
    }
  }
  value = 42;
  nexus_data_slabbed_stack_push(stack, &value);
  nexus_data_slabbed_stack_clear(stack);
  if (nexus_data_slabbed_stack_peek(stack) != NULL || nexus_data_slabbed_stack_capacity_get(stack) != capacity) {
    return nexus_test_fail(723);
  }
  nexus_data_slabbed_stack_push(stack, &value);
  if (*(int *)nexus_data_slabbed_stack_peek(stack) != 42) {
    return nexus_test_fail(724);
  }
  nexus_data_slabbed_stack_destroy(stack);
  nexus_data_slabbed_stack_destroy(NULL);
  return 0;
}

static int nexus_test_data_slabbed_queue(void) {
  NexusDataSlabbedQueue *queue;
  void                  *saved[NEXUS_TEST_SLAB_COUNT];
  uint_large             index;
  uint_large             capacity;
  int                    value;
  int                    output;

  queue = nexus_data_slabbed_queue_create(NEXUS_SIZEOF(int), 4U, 3U);
  if (nexus_data_slabbed_queue_peek(queue) != NULL || nexus_data_slabbed_queue_pop(queue, NULL)) {
    return nexus_test_fail(730);
  }
  for (index = 0; index < 4U; index++) {
    value        = (int)index;
    saved[index] = nexus_data_slabbed_queue_push(queue, &value);
  }
  for (index = 0; index < 3U; index++) {
    if (!nexus_data_slabbed_queue_pop(queue, &output) || output != (int)index) {
      return nexus_test_fail(731);
    }
  }
  /* Wrap the directory before forcing both slab growth and directory growth. */
  for (index = 4U; index < NEXUS_TEST_SLAB_COUNT; index++) {
    value        = (int)index;
    saved[index] = nexus_data_slabbed_queue_push(queue, &value);
    if (nexus_data_slabbed_queue_peek(queue) != saved[3] || *(int *)saved[3] != 3) {
      return nexus_test_fail(732);
    }
  }
  nexus_data_slabbed_queue_reserve(queue, 512U);
  capacity = nexus_data_slabbed_queue_capacity_get(queue);
  if (capacity < 512U || nexus_data_slabbed_queue_length_get(queue) != NEXUS_TEST_SLAB_COUNT - 3U) {
    return nexus_test_fail(733);
  }
  for (index = 3U; index < NEXUS_TEST_SLAB_COUNT; index++) {
    if (nexus_data_slabbed_queue_peek(queue) != saved[index] || !nexus_data_slabbed_queue_pop(queue, &output) || output != (int)index) {
      return nexus_test_fail(734);
    }
  }
  value = 100;
  for (index = 0; index < capacity; index++) {
    nexus_data_slabbed_queue_push(queue, &value);
  }
  for (index = 0; index < capacity / 2U; index++) {
    nexus_data_slabbed_queue_pop(queue, NULL);
    nexus_data_slabbed_queue_push(queue, &value);
  }
  nexus_data_slabbed_queue_clear(queue);
  if (nexus_data_slabbed_queue_length_get(queue) != 0 || nexus_data_slabbed_queue_peek(queue) != NULL ||
      nexus_data_slabbed_queue_capacity_get(queue) != capacity) {
    return nexus_test_fail(735);
  }
  nexus_data_slabbed_queue_push(queue, &value);
  if (!nexus_data_slabbed_queue_pop(queue, &output) || output != 100) {
    return nexus_test_fail(736);
  }
  nexus_data_slabbed_queue_destroy(queue);
  nexus_data_slabbed_queue_destroy(NULL);
  return 0;
}

static int nexus_test_data_slabbed_priority_queue(void) {
  NexusDataSlabbedPriorityQueue *queue;
  void                          *saved[NEXUS_TEST_SLAB_COUNT];
  uint_large                     index;
  uint_large                     live;
  uint_large                     capacity;
  int                            value;
  int                            output;

  queue = nexus_data_slabbed_priority_queue_create(NEXUS_SIZEOF(int), 0, 5U, nexus_test_compare_ints, NULL);
  if (nexus_data_slabbed_priority_queue_peek(queue) != NULL || nexus_data_slabbed_priority_queue_pop(queue, NULL)) {
    return nexus_test_fail(740);
  }
  for (index = NEXUS_TEST_SLAB_COUNT; index > 0; index--) {
    value             = (int)(index - 1U);
    saved[index - 1U] = nexus_data_slabbed_priority_queue_push(queue, &value);
    for (live = index - 1U; live < NEXUS_TEST_SLAB_COUNT; live++) {
      if (*(int *)saved[live] != (int)live) {
        return nexus_test_fail(741);
      }
    }
  }
  nexus_data_slabbed_priority_queue_reserve(queue, 512U);
  capacity = nexus_data_slabbed_priority_queue_capacity_get(queue);
  if (capacity < 512U || nexus_data_slabbed_priority_queue_length_get(queue) != NEXUS_TEST_SLAB_COUNT) {
    return nexus_test_fail(742);
  }
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index++) {
    if (nexus_data_slabbed_priority_queue_peek(queue) != saved[index] || !nexus_data_slabbed_priority_queue_pop(queue, &output) ||
        output != (int)index) {
      return nexus_test_fail(743);
    }
    for (live = index + 1U; live < NEXUS_TEST_SLAB_COUNT; live++) {
      if (*(int *)saved[live] != (int)live) {
        return nexus_test_fail(744);
      }
    }
  }
  value = 42;
  nexus_data_slabbed_priority_queue_push(queue, &value);
  nexus_data_slabbed_priority_queue_clear(queue);
  if (nexus_data_slabbed_priority_queue_peek(queue) != NULL || nexus_data_slabbed_priority_queue_capacity_get(queue) != capacity) {
    return nexus_test_fail(745);
  }
  nexus_data_slabbed_priority_queue_push(queue, &value);
  if (!nexus_data_slabbed_priority_queue_pop(queue, &output) || output != 42) {
    return nexus_test_fail(746);
  }
  nexus_data_slabbed_priority_queue_destroy(queue);
  nexus_data_slabbed_priority_queue_destroy(NULL);
  return 0;
}

static uint64 nexus_test_slabbed_hash_collision(const void *key, uint_large key_size, uint64 seed, void *user_data) {
  (void)key;
  (void)key_size;
  (void)seed;
  (void)user_data;
  return 0;
}

static int nexus_test_data_slabbed_hashmap(boolean force_collisions) {
  NexusDataHashMapConfig            config;
  NexusDataSlabbedHashMap          *hashmap;
  NexusDataSlabbedHashMapEnumerator enumerator;
  NexusError                       *error;
  void                             *saved_values[NEXUS_TEST_SLAB_COUNT];
  const void                       *saved_keys[NEXUS_TEST_SLAB_COUNT];
  const void                       *key_pointer;
  void                             *value_pointer;
  uint_large                        index;
  uint_large                        count;
  uint_large                        capacity;
  int                               key;
  int                               value;
  int                               output;
  boolean                           inserted;

  nexus_data_hashmap_config_initialize(&config, NEXUS_SIZEOF(int), NEXUS_SIZEOF(int));
  config.seed_is_set = TRUE;
  config.seed        = 123U;
  if (force_collisions) {
    config.hash = nexus_test_slabbed_hash_collision;
  }
  error = nexus_data_slabbed_hashmap_create(&config, 3U, &hashmap);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code || hashmap == NULL || nexus_data_slabbed_hashmap_length_get(hashmap) != 0) {
    return nexus_test_fail(750);
  }
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index++) {
    key           = (int)index;
    value         = key + 1000;
    value_pointer = nexus_data_slabbed_hashmap_insert(hashmap, &key, &inserted);
    if (!inserted) {
      return nexus_test_fail(751);
    }
    *(int *)value_pointer = value;
    saved_values[index]   = value_pointer;
    saved_keys[index]     = NULL;
  }
  count = 0;
  nexus_data_slabbed_hashmap_enumerator_initialize(hashmap, &enumerator);
  while (nexus_data_slabbed_hashmap_enumerator_next(&enumerator, &key_pointer, &value_pointer)) {
    key = *(const int *)key_pointer;
    if (key < 0 || key >= (int)NEXUS_TEST_SLAB_COUNT || value_pointer != saved_values[key] || *(int *)value_pointer != key + 1000 ||
        saved_keys[key] != NULL) {
      return nexus_test_fail(752);
    }
    saved_keys[key] = key_pointer;
    count++;
  }
  if (count != NEXUS_TEST_SLAB_COUNT || nexus_data_slabbed_hashmap_enumerator_next(&enumerator, NULL, NULL)) {
    return nexus_test_fail(753);
  }
  nexus_data_slabbed_hashmap_reserve(hashmap, 2048U);
  capacity = nexus_data_slabbed_hashmap_capacity_get(hashmap);
  if (capacity < 2048U) {
    return nexus_test_fail(754);
  }
  /* Same-key put must replace in place; source key/value may be live map slots. */
  key = 100;
  nexus_data_slabbed_hashmap_put(hashmap, saved_keys[key], saved_values[key]);
  value = -10;
  nexus_data_slabbed_hashmap_put(hashmap, saved_keys[key], &value);
  if (nexus_data_slabbed_hashmap_insert(hashmap, saved_keys[key], &inserted) != saved_values[key] || inserted || *(int *)saved_values[key] != -10) {
    return nexus_test_fail(755);
  }
  value = 1100;
  nexus_data_slabbed_hashmap_put(hashmap, saved_keys[key], &value);
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index++) {
    key = (int)index;
    if (!nexus_data_slabbed_hashmap_contains(hashmap, &key) || nexus_data_slabbed_hashmap_get(hashmap, &key) != saved_values[index] ||
        nexus_data_slabbed_hashmap_get_const(hashmap, saved_keys[index]) != saved_values[index] ||
        !nexus_data_slabbed_hashmap_get_copy(hashmap, &key, &output) || output != key + 1000) {
      return nexus_test_fail(756);
    }
  }
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index += 2U) {
    key = (int)index;
    if (!nexus_data_slabbed_hashmap_remove(hashmap, saved_keys[index], &output) || output != key + 1000 ||
        nexus_data_slabbed_hashmap_contains(hashmap, &key)) {
      return nexus_test_fail(757);
    }
    saved_values[index] = NULL;
    saved_keys[index]   = NULL;
  }
  /* Reinsert through tombstones and ensure every surviving key/value stayed put. */
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index += 2U) {
    key   = (int)index;
    value = key + 1000;
    nexus_data_slabbed_hashmap_put(hashmap, &key, &value);
    saved_values[index] = nexus_data_slabbed_hashmap_get(hashmap, &key);
  }
  nexus_data_slabbed_hashmap_enumerator_initialize(hashmap, &enumerator);
  count = 0;
  while (nexus_data_slabbed_hashmap_enumerator_next(&enumerator, &key_pointer, &value_pointer)) {
    key = *(const int *)key_pointer;
    if (value_pointer != saved_values[key] || *(int *)value_pointer != key + 1000 || (saved_keys[key] != NULL && key_pointer != saved_keys[key])) {
      return nexus_test_fail(758);
    }
    count++;
  }
  if (count != NEXUS_TEST_SLAB_COUNT) {
    return nexus_test_fail(759);
  }
  key    = -1;
  output = 999;
  if (nexus_data_slabbed_hashmap_get(hashmap, &key) != NULL || nexus_data_slabbed_hashmap_get_copy(hashmap, &key, &output) || output != 999 ||
      nexus_data_slabbed_hashmap_remove(hashmap, &key, NULL)) {
    return nexus_test_fail(760);
  }
  nexus_data_slabbed_hashmap_clear(hashmap);
  if (nexus_data_slabbed_hashmap_length_get(hashmap) != 0 || nexus_data_slabbed_hashmap_capacity_get(hashmap) != capacity) {
    return nexus_test_fail(761);
  }
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index++) {
    key   = (int)index;
    value = key;
    nexus_data_slabbed_hashmap_put(hashmap, &key, &value);
  }
  nexus_data_slabbed_hashmap_destroy(hashmap);
  nexus_data_slabbed_hashmap_destroy(NULL);
  return 0;
}

static int nexus_test_data_slabbed_hashmap_rebuild(void) {
  NexusDataHashMapConfig            config;
  NexusDataSlabbedHashMap          *hashmap;
  NexusDataSlabbedHashMapEnumerator enumerator;
  NexusError                       *error;
  const void                       *saved_key;
  const void                       *key_pointer;
  void                             *saved_value;
  void                             *value_pointer;
  uint_large                        index;
  int                               key;
  int                               value;
  jmp_buf                           recovery_point;

  nexus_data_hashmap_config_initialize(&config, NEXUS_SIZEOF(int), NEXUS_SIZEOF(int));
  config.initial_capacity = 14U;
  config.seed_is_set      = TRUE;
  config.seed             = 1U;
  config.hash             = nexus_test_slabbed_hash_collision;
  error                   = nexus_data_slabbed_hashmap_create(&config, 2U, &hashmap);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(770);
  }
  for (index = 0; index < 14U; index++) {
    key   = (int)index;
    value = key + 100;
    nexus_data_slabbed_hashmap_put(hashmap, &key, &value);
  }
  saved_key   = NULL;
  saved_value = NULL;
  nexus_data_slabbed_hashmap_enumerator_initialize(hashmap, &enumerator);
  while (nexus_data_slabbed_hashmap_enumerator_next(&enumerator, &key_pointer, &value_pointer)) {
    if (*(const int *)key_pointer == 13) {
      saved_key   = key_pointer;
      saved_value = value_pointer;
    }
  }
  if (saved_key == NULL || saved_value == NULL) {
    return nexus_test_fail(771);
  }
  for (index = 0; index < 12U; index++) {
    key = (int)index;
    nexus_data_slabbed_hashmap_remove(hashmap, &key, NULL);
  }
  /* Exceed occupied+tombstone load while mostly dead: same-size cleanup. */
  key   = 50;
  value = 150;
  nexus_data_slabbed_hashmap_put(hashmap, &key, &value);
  if (*(const int *)saved_key != 13 || nexus_data_slabbed_hashmap_get(hashmap, saved_key) != saved_value || *(int *)saved_value != 113) {
    return nexus_test_fail(772);
  }
  /* Fill the cleaned table and trigger an automatic growth rehash. */
  for (index = 51U; index < 80U; index++) {
    key   = (int)index;
    value = key + 100;
    nexus_data_slabbed_hashmap_put(hashmap, &key, &value);
  }
  nexus_data_slabbed_hashmap_enumerator_initialize(hashmap, &enumerator);
  while (nexus_data_slabbed_hashmap_enumerator_next(&enumerator, &key_pointer, &value_pointer)) {
    if (*(const int *)key_pointer == 13 && (key_pointer != saved_key || value_pointer != saved_value)) {
      return nexus_test_fail(773);
    }
  }
#if NEXUS_DEVELOPER_MODE
  nexus_data_slabbed_hashmap_enumerator_initialize(hashmap, &enumerator);
  nexus_data_slabbed_hashmap_reserve(hashmap, 512U);
  if (setjmp(recovery_point) == 0) {
    nexus_program_recovery_arm(&recovery_point);
    (void)nexus_data_slabbed_hashmap_enumerator_next(&enumerator, NULL, NULL);
    nexus_program_recovery_disarm();
    return nexus_test_fail(774);
  }
  nexus_program_recovery_disarm();
  error = nexus_program_recovery_last_error_get();
  if (error == NULL || error->code != GLOBAL_NEXUS_PRECONDITION_VIOLATION) {
    return nexus_test_fail(775);
  }
#else
  (void)recovery_point;
#endif
  nexus_data_slabbed_hashmap_destroy(hashmap);
  return 0;
}

static int nexus_test_data_slabbed_overflow(void) {
  NexusDataSlabbedList *list;
  NexusError           *error;
  jmp_buf               recovery_point;

  list = nexus_data_slabbed_list_create(1U, 0, 3U);
  if (setjmp(recovery_point) == 0) {
    nexus_program_recovery_arm(&recovery_point);
    nexus_data_slabbed_list_reserve(list, UINT_LARGE_MAX_VAL);
    nexus_program_recovery_disarm();
    return nexus_test_fail(780);
  }
  nexus_program_recovery_disarm();
  error = nexus_program_recovery_last_error_get();
  if (error == NULL || error->code != GLOBAL_NEXUS_PRECONDITION_VIOLATION || nexus_data_slabbed_list_capacity_get(list) != 0) {
    return nexus_test_fail(781);
  }
  nexus_data_slabbed_list_destroy(list);
  return 0;
}

#if NEXUS_MEMORY_TRACING
static uint_large nexus_test_slabbed_storage_operations;

static void nexus_test_slabbed_allocation_trace(const char *message, ...) {
  if (message[0] == 'A' || message[0] == 'R' || message[0] == 'D') {
    nexus_test_slabbed_storage_operations++;
  }
}

static int nexus_test_data_slabbed_reserved(void) {
  NexusDataSlabbedList          *list;
  NexusDataSlabbedStack         *stack;
  NexusDataSlabbedQueue         *queue;
  NexusDataSlabbedPriorityQueue *priority_queue;
  NexusDataSlabbedHashMap       *hashmap;
  NexusDataHashMapConfig         config;
  NexusError                    *error;
  uint_large                     index;
  uint_large                     pass;
  int                            value;

  list           = nexus_data_slabbed_list_create(NEXUS_SIZEOF(int), 128U, 7U);
  stack          = nexus_data_slabbed_stack_create(NEXUS_SIZEOF(int), 128U, 7U);
  queue          = nexus_data_slabbed_queue_create(NEXUS_SIZEOF(int), 128U, 7U);
  priority_queue = nexus_data_slabbed_priority_queue_create(NEXUS_SIZEOF(int), 128U, 7U, nexus_test_compare_ints, NULL);
  nexus_data_hashmap_config_initialize(&config, NEXUS_SIZEOF(int), NEXUS_SIZEOF(int));
  config.initial_capacity = 128U;
  config.seed_is_set      = TRUE;
  error                   = nexus_data_slabbed_hashmap_create(&config, 7U, &hashmap);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(790);
  }
  nexus_test_slabbed_storage_operations = 0;
  nexus_memory_trace_function_set(nexus_test_slabbed_allocation_trace);
  /* Reserve pays for slabs and directories; filling, draining, sorting and
     refilling all five containers must issue no allocator operations. */
  for (pass = 0; pass < 2U; pass++) {
    for (index = 0; index < 128U; index++) {
      value = (int)index;
      nexus_data_slabbed_list_append(list, &value);
      nexus_data_slabbed_stack_push(stack, &value);
      nexus_data_slabbed_queue_push(queue, &value);
      nexus_data_slabbed_priority_queue_push(priority_queue, &value);
      nexus_data_slabbed_hashmap_put(hashmap, &value, &value);
    }
    nexus_data_slabbed_list_sort(list, nexus_test_compare_ints, NULL);
    for (index = 0; index < 64U; index++) {
      value = (int)index;
      nexus_data_slabbed_list_pop(list, NULL);
      nexus_data_slabbed_stack_pop(stack, NULL);
      nexus_data_slabbed_queue_pop(queue, NULL);
      nexus_data_slabbed_queue_push(queue, &value);
      nexus_data_slabbed_priority_queue_pop(priority_queue, NULL);
    }
    nexus_data_slabbed_list_clear(list);
    nexus_data_slabbed_stack_clear(stack);
    nexus_data_slabbed_queue_clear(queue);
    nexus_data_slabbed_priority_queue_clear(priority_queue);
    nexus_data_slabbed_hashmap_clear(hashmap);
  }
  nexus_memory_trace_function_set(NULL);
  nexus_data_slabbed_list_destroy(list);
  nexus_data_slabbed_stack_destroy(stack);
  nexus_data_slabbed_queue_destroy(queue);
  nexus_data_slabbed_priority_queue_destroy(priority_queue);
  nexus_data_slabbed_hashmap_destroy(hashmap);
  if (nexus_test_slabbed_storage_operations != 0) {
    return nexus_test_fail(791);
  }
  return 0;
}
#endif

static int nexus_test_data_reverse_sequences(void) {
  NexusDataList           *list;
  NexusDataStack          *stack;
  NexusDataSlabbedList    *slabbed_list;
  NexusDataSlabbedStack   *slabbed_stack;
  NexusDataLinkedList      linked_list;
  NexusDataLinkedListNode  nodes[9];
  NexusDataLinkedListNode *node;
  byte                     values[9][3];
  byte                     output[3];
  byte                    *element;
  void                    *saved_list[9];
  void                    *saved_stack[9];
  void                    *list_storage;
  void                    *stack_storage;
  uint_large               count;
  uint_large               index;
  uint_large               expected;
  uint_large               pass;
  uint_large               list_capacity;
  uint_large               slabbed_capacity;

  /* Odd-width payloads catch swaps that accidentally assume word alignment. */
  for (index = 0; index < 9U; index++) {
    values[index][0] = (byte)index;
    values[index][1] = (byte)(index + 10U);
    values[index][2] = (byte)(index + 20U);
  }
  for (count = 0; count <= 9U; count++) {
    list          = nexus_data_list_create(3U, 0);
    stack         = nexus_data_stack_create(3U, 0);
    slabbed_list  = nexus_data_slabbed_list_create(3U, 0, 3U);
    slabbed_stack = nexus_data_slabbed_stack_create(3U, 0, 3U);
    nexus_data_linked_list_initialize(&linked_list);
    for (index = 0; index < count; index++) {
      nexus_data_list_append(list, values[index]);
      nexus_data_stack_push(stack, values[index]);
      saved_list[index]  = nexus_data_slabbed_list_append(slabbed_list, values[index]);
      saved_stack[index] = nexus_data_slabbed_stack_push(slabbed_stack, values[index]);
      nexus_data_linked_list_node_initialize(&nodes[index]);
      nexus_data_linked_list_push_back(&linked_list, &nodes[index]);
    }
    list_storage     = nexus_data_list_data_get(list);
    stack_storage    = nexus_data_stack_data_get(stack);
    list_capacity    = nexus_data_list_capacity_get(list);
    slabbed_capacity = nexus_data_slabbed_list_capacity_get(slabbed_list);
    for (pass = 0; pass < 3U; pass++) {
#if NEXUS_MEMORY_TRACING
      nexus_test_slabbed_storage_operations = 0;
      nexus_memory_trace_function_set(nexus_test_slabbed_allocation_trace);
#endif
      nexus_data_list_reverse(list);
      nexus_data_stack_reverse(stack);
      nexus_data_slabbed_list_reverse(slabbed_list);
      nexus_data_slabbed_stack_reverse(slabbed_stack);
      nexus_data_linked_list_reverse(&linked_list);
#if NEXUS_MEMORY_TRACING
      nexus_memory_trace_function_set(NULL);
      if (nexus_test_slabbed_storage_operations != 0) {
        return nexus_test_fail(1100);
      }
#endif
      if (nexus_data_list_length_get(list) != count || nexus_data_stack_length_get(stack) != count ||
          nexus_data_slabbed_list_length_get(slabbed_list) != count || nexus_data_slabbed_stack_length_get(slabbed_stack) != count ||
          linked_list.length != count || nexus_data_list_capacity_get(list) != list_capacity ||
          nexus_data_slabbed_list_capacity_get(slabbed_list) != slabbed_capacity || nexus_data_list_data_get(list) != list_storage ||
          nexus_data_stack_data_get(stack) != stack_storage) {
        return nexus_test_fail(1101);
      }
      node = linked_list.first;
      for (index = 0; index < count; index++) {
        expected = pass == 1U ? index : count - 1U - index;
        element  = (byte *)nexus_data_list_at(list, index);
        if (element[0] != values[expected][0] || element[1] != values[expected][1] || element[2] != values[expected][2] ||
            nexus_data_slabbed_list_at(slabbed_list, index) != saved_list[expected]) {
          return nexus_test_fail(1102);
        }
        element = (byte *)saved_list[index];
        if (element[0] != values[index][0] || element[1] != values[index][1] || element[2] != values[index][2]) {
          return nexus_test_fail(1103);
        }
        element = (byte *)saved_stack[index];
        if (element[0] != values[index][0] || element[1] != values[index][1] || element[2] != values[index][2]) {
          return nexus_test_fail(1104);
        }
        if (node != &nodes[expected] || node->owner != &linked_list ||
            node->previous != (index == 0 ? NULL : &nodes[pass == 1U ? index - 1U : expected + 1U])) {
          return nexus_test_fail(1105);
        }
        node = node->next;
      }
      if (node != NULL || (count == 0 && (linked_list.first != NULL || linked_list.last != NULL)) ||
          (count > 0 && linked_list.last != &nodes[pass == 1U ? count - 1U : 0])) {
        return nexus_test_fail(1106);
      }
    }
    /* Pop order reverses too, and every slabbed pointer keeps its identity. */
    for (index = 0; index < count; index++) {
      if (nexus_data_slabbed_stack_peek(slabbed_stack) != saved_stack[index] || !nexus_data_stack_pop(stack, output) ||
          output[0] != values[index][0] || output[1] != values[index][1] || output[2] != values[index][2] ||
          !nexus_data_slabbed_stack_pop(slabbed_stack, output) || output[0] != values[index][0] || output[1] != values[index][1] ||
          output[2] != values[index][2] || nexus_data_linked_list_pop_front(&linked_list) != &nodes[count - 1U - index]) {
        return nexus_test_fail(1107);
      }
    }
    nexus_data_list_append(list, values[0]);
    nexus_data_slabbed_list_append(slabbed_list, values[0]);
    nexus_data_list_reverse(list);
    nexus_data_slabbed_list_reverse(slabbed_list);
    if (*(byte *)nexus_data_list_at(list, 0) != 0 || *(byte *)nexus_data_slabbed_list_at(slabbed_list, 0) != 0) {
      return nexus_test_fail(1108);
    }
    nexus_data_linked_list_clear(&linked_list);
    nexus_data_list_destroy(list);
    nexus_data_stack_destroy(stack);
    nexus_data_slabbed_list_destroy(slabbed_list);
    nexus_data_slabbed_stack_destroy(slabbed_stack);
  }
  return 0;
}

static int nexus_test_data_reverse_circular(void) {
  NexusDataQueue        *queue;
  NexusDataSlabbedQueue *slabbed_queue;
  NexusDataRingBuffer   *ringbuffer;
  void                  *saved[16];
  uint_large             capacity;
  uint_large             offset;
  uint_large             count;
  uint_large             index;
  uint_large             pass;
  uint_large             slabbed_capacity;
  int                    value;
  int                    output;
  int                    first;
  int                    expected;

  queue         = nexus_data_queue_create(NEXUS_SIZEOF(int), 0);
  slabbed_queue = nexus_data_slabbed_queue_create(NEXUS_SIZEOF(int), 0, 3U);
  nexus_data_queue_reverse(queue);
  nexus_data_slabbed_queue_reverse(slabbed_queue);
  value = 42;
  nexus_data_queue_push(queue, &value);
  nexus_data_slabbed_queue_push(slabbed_queue, &value);
  nexus_data_queue_reverse(queue);
  nexus_data_slabbed_queue_reverse(slabbed_queue);
  if (!nexus_data_queue_pop(queue, &output) || output != 42 || !nexus_data_slabbed_queue_pop(slabbed_queue, &output) || output != 42) {
    return nexus_test_fail(1109);
  }
  nexus_data_queue_destroy(queue);
  nexus_data_slabbed_queue_destroy(slabbed_queue);

  /* Exercise every occupancy and head offset, including partial wraps. */
  for (capacity = 1U; capacity <= 8U; capacity *= 2U) {
    for (offset = 0; offset < capacity; offset++) {
      for (count = 0; count <= capacity; count++) {
        queue         = nexus_data_queue_create(NEXUS_SIZEOF(int), capacity);
        slabbed_queue = nexus_data_slabbed_queue_create(NEXUS_SIZEOF(int), capacity, 3U);
        ringbuffer    = nexus_data_ringbuffer_create(NEXUS_SIZEOF(int), capacity);
        for (index = 0; index < capacity + offset; index++) {
          if (index >= capacity) {
            nexus_data_queue_pop(queue, NULL);
            nexus_data_slabbed_queue_pop(slabbed_queue, NULL);
          }
          value = (int)index;
          nexus_data_queue_push(queue, &value);
          saved[index] = nexus_data_slabbed_queue_push(slabbed_queue, &value);
          nexus_data_ringbuffer_push(ringbuffer, &value);
        }
        for (index = 0; index < capacity - count; index++) {
          nexus_data_queue_pop(queue, NULL);
          nexus_data_slabbed_queue_pop(slabbed_queue, NULL);
          nexus_data_ringbuffer_pop(ringbuffer, NULL);
        }
        first            = (int)(capacity + offset - count);
        slabbed_capacity = nexus_data_slabbed_queue_capacity_get(slabbed_queue);
        for (pass = 0; pass < 3U; pass++) {
#if NEXUS_MEMORY_TRACING
          nexus_test_slabbed_storage_operations = 0;
          nexus_memory_trace_function_set(nexus_test_slabbed_allocation_trace);
#endif
          nexus_data_queue_reverse(queue);
          nexus_data_slabbed_queue_reverse(slabbed_queue);
          nexus_data_ringbuffer_reverse(ringbuffer);
#if NEXUS_MEMORY_TRACING
          nexus_memory_trace_function_set(NULL);
          if (nexus_test_slabbed_storage_operations != 0) {
            return nexus_test_fail(1110);
          }
#endif
          if (nexus_data_queue_length_get(queue) != count || nexus_data_slabbed_queue_length_get(slabbed_queue) != count ||
              nexus_data_ringbuffer_length_get(ringbuffer) != count || nexus_data_ringbuffer_capacity_get(ringbuffer) != capacity ||
              nexus_data_slabbed_queue_capacity_get(slabbed_queue) != slabbed_capacity) {
            return nexus_test_fail(1111);
          }
          if (count > 0) {
            expected = first + (pass == 1U ? 0 : (int)count - 1);
            if (*(int *)nexus_data_queue_peek(queue) != expected || nexus_data_slabbed_queue_peek(slabbed_queue) != saved[expected]) {
              return nexus_test_fail(1112);
            }
          }
          for (index = 0; index < count; index++) {
            expected = first + (int)(pass == 1U ? index : count - 1U - index);
            if (*(int *)nexus_data_ringbuffer_at(ringbuffer, index) != expected || *(int *)saved[first + (int)index] != first + (int)index) {
              return nexus_test_fail(1113);
            }
          }
        }
        /* Append after reversal, also verifying the ring's overwrite policy. */
        value = 99;
        nexus_data_queue_push(queue, &value);
        nexus_data_slabbed_queue_push(slabbed_queue, &value);
        if (nexus_data_ringbuffer_push(ringbuffer, &value) != (count == capacity)) {
          return nexus_test_fail(1114);
        }
        for (index = 0; index < count; index++) {
          expected = first + (int)(count - 1U - index);
          if (!nexus_data_queue_pop(queue, &output) || output != expected || nexus_data_slabbed_queue_peek(slabbed_queue) != saved[expected] ||
              !nexus_data_slabbed_queue_pop(slabbed_queue, &output) || output != expected) {
            return nexus_test_fail(1115);
          }
          if (!(count == capacity && index == 0) && (!nexus_data_ringbuffer_pop(ringbuffer, &output) || output != expected)) {
            return nexus_test_fail(1116);
          }
        }
        if (!nexus_data_queue_pop(queue, &output) || output != 99 || !nexus_data_slabbed_queue_pop(slabbed_queue, &output) || output != 99 ||
            !nexus_data_ringbuffer_pop(ringbuffer, &output) || output != 99 || nexus_data_queue_pop(queue, NULL) ||
            nexus_data_slabbed_queue_pop(slabbed_queue, NULL) || nexus_data_ringbuffer_pop(ringbuffer, NULL)) {
          return nexus_test_fail(1117);
        }
        nexus_data_queue_destroy(queue);
        nexus_data_slabbed_queue_destroy(slabbed_queue);
        nexus_data_ringbuffer_destroy(ringbuffer);
      }
    }
  }
  return 0;
}

static int nexus_test_data_hashmap_rehash(boolean force_collisions) {
  NexusDataHashMapConfig     config;
  NexusDataHashMap          *hashmap;
  NexusDataHashMapEnumerator enumerator;
  NexusError                *error;
  void                      *key_pointer;
  void                      *value_pointer;
  uint_large                 index;
  uint_large                 count;
  int                        key;
  int                        value;
  int                        output;

  nexus_data_hashmap_config_initialize(&config, NEXUS_SIZEOF(int), NEXUS_SIZEOF(int));
  config.seed_is_set = TRUE;
  config.seed        = 7U;
  if (force_collisions) {
    config.hash = nexus_test_slabbed_hash_collision;
  }
  error = nexus_data_hashmap_create(&config, &hashmap);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(795);
  }
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index++) {
    key   = (int)index;
    value = key + 100;
    nexus_data_hashmap_put(hashmap, &key, &value);
  }
  nexus_data_hashmap_reserve(hashmap, 1024U);
  for (index = 0; index < NEXUS_TEST_SLAB_COUNT; index++) {
    key = (int)index;
    if (!nexus_data_hashmap_get_copy(hashmap, &key, &output) || output != key + 100) {
      return nexus_test_fail(796);
    }
    if (index % 2U == 0) {
      nexus_data_hashmap_remove(hashmap, &key, NULL);
    }
  }
  count = 0;
  nexus_data_hashmap_enumerator_initialize(hashmap, &enumerator);
  while (nexus_data_hashmap_enumerator_next(&enumerator, &key_pointer, &value_pointer)) {
    key = *(int *)key_pointer;
    if (key % 2 == 0 || *(int *)value_pointer != key + 100) {
      return nexus_test_fail(797);
    }
    count++;
  }
  if (count != NEXUS_TEST_SLAB_COUNT / 2U) {
    return nexus_test_fail(798);
  }
  nexus_data_hashmap_clear(hashmap);
  if (nexus_data_hashmap_length_get(hashmap) != 0) {
    return nexus_test_fail(799);
  }
  key   = 2;
  value = 42;
  nexus_data_hashmap_put(hashmap, &key, &value);
  nexus_data_hashmap_destroy(hashmap);
  return 0;
}

static int nexus_test_compare_pointer_ints(const void *left, const void *right, void *user_data) {
  const int *left_value;
  const int *right_value;
  (void)user_data;

  left_value  = *(const int *const *)left;
  right_value = *(const int *const *)right;
  if (left_value == NULL || right_value == NULL) {
    return (left_value != NULL) - (right_value != NULL);
  }
  return (*left_value > *right_value) - (*left_value < *right_value);
}

static int nexus_test_data_pointers(void) {
  NexusDataArray                 array;
  NexusDataList                 *list;
  NexusDataSlabbedList          *slabbed_list;
  NexusDataStack                *stack;
  NexusDataSlabbedStack         *slabbed_stack;
  NexusDataQueue                *queue;
  NexusDataSlabbedQueue         *slabbed_queue;
  NexusDataPriorityQueue        *priority_queue;
  NexusDataSlabbedPriorityQueue *slabbed_priority_queue;
  NexusDataRingBuffer           *ringbuffer;
  NexusDataHashMap              *hashmap;
  NexusDataSlabbedHashMap       *slabbed_hashmap;
  NexusDataHashMapConfig         config;
  NexusError                    *error;
  byte                           unaligned[(2U * sizeof(void *)) + 1U];
  int                            values[64];
  int                            index;
  int                            key;
  int                            expected_index;
  void                          *expected;
  void                          *output;
  void                          *saved_slot;

  for (index = 0; index < 64; index++) {
    values[index] = index;
  }
  array = nexus_data_array_view_create(unaligned + 1U, NEXUS_SIZEOF(void *), 2U);
  nexus_data_array_set_pointer(&array, 0, &values[0]);
  nexus_data_array_set_pointer(&array, 1U, NULL);
  if (nexus_data_array_pointer_at(&array, 0) != &values[0] || nexus_data_array_pointer_at(&array, 1U) != NULL) {
    return nexus_test_fail(900);
  }
  list = nexus_data_list_create(NEXUS_SIZEOF(void *), 0);
  for (index = 0; index < 64; index++) {
    nexus_data_list_append_pointer(list, &values[index]);
  }
  nexus_data_list_insert_pointer(list, 1U, NULL);
  if (nexus_data_list_pointer_at(list, 1U) != NULL || nexus_data_list_pointer_at(list, 2U) != &values[1]) {
    return nexus_test_fail(901);
  }
  output = &values[0];
  nexus_data_list_remove_pointer(list, 1U, &output);
  if (output != NULL) {
    return nexus_test_fail(902);
  }
  for (index = 63; index >= 0; index--) {
    if (nexus_data_list_pointer_at(list, (uint_large)index) != &values[index] || !nexus_data_list_pop_pointer(list, &output) ||
        output != &values[index]) {
      return nexus_test_fail(903);
    }
  }
  nexus_data_list_insert_pointer(list, 0, NULL);
  if (!nexus_data_list_pop_pointer(list, &output) || output != NULL) {
    return nexus_test_fail(904);
  }
  output = &values[0];
  if (nexus_data_list_pop_pointer(list, &output) || output != &values[0]) {
    return nexus_test_fail(905);
  }
  nexus_data_list_append_pointer(list, NULL);
  nexus_data_list_remove_pointer(list, 0, NULL);
  nexus_data_list_append_pointer(list, NULL);
  if (!nexus_data_list_pop_pointer(list, NULL)) {
    return nexus_test_fail(906);
  }
  nexus_data_list_destroy(list);
  slabbed_list = nexus_data_slabbed_list_create(NEXUS_SIZEOF(void *), 0, 3U);
  for (index = 0; index < 64; index++) {
    nexus_data_slabbed_list_append_pointer(slabbed_list, &values[index]);
  }
  nexus_data_slabbed_list_insert_pointer(slabbed_list, 1U, NULL);
  if (nexus_data_slabbed_list_pointer_at(slabbed_list, 1U) != NULL || nexus_data_slabbed_list_pointer_at(slabbed_list, 2U) != &values[1]) {
    return nexus_test_fail(907);
  }
  output = &values[0];
  nexus_data_slabbed_list_remove_pointer(slabbed_list, 1U, &output);
  if (output != NULL) {
    return nexus_test_fail(908);
  }
  for (index = 63; index >= 0; index--) {
    if (nexus_data_slabbed_list_pointer_at(slabbed_list, (uint_large)index) != &values[index] ||
        !nexus_data_slabbed_list_pop_pointer(slabbed_list, &output) || output != &values[index]) {
      return nexus_test_fail(909);
    }
  }
  nexus_data_slabbed_list_insert_pointer(slabbed_list, 0, NULL);
  if (!nexus_data_slabbed_list_pop_pointer(slabbed_list, &output) || output != NULL) {
    return nexus_test_fail(910);
  }
  output = &values[0];
  if (nexus_data_slabbed_list_pop_pointer(slabbed_list, &output) || output != &values[0]) {
    return nexus_test_fail(911);
  }
  nexus_data_slabbed_list_append_pointer(slabbed_list, NULL);
  nexus_data_slabbed_list_remove_pointer(slabbed_list, 0, NULL);
  nexus_data_slabbed_list_append_pointer(slabbed_list, NULL);
  if (!nexus_data_slabbed_list_pop_pointer(slabbed_list, NULL)) {
    return nexus_test_fail(912);
  }
  nexus_data_slabbed_list_destroy(slabbed_list);
  stack  = nexus_data_stack_create(NEXUS_SIZEOF(void *), 0);
  output = &values[0];
  if (nexus_data_stack_peek_pointer(stack) != NULL || nexus_data_stack_pop_pointer(stack, &output) || output != &values[0]) {
    return nexus_test_fail(913);
  }
  saved_slot = nexus_data_stack_push_pointer(stack, &values[63]);
  if (*(void **)saved_slot != &values[63]) {
    return nexus_test_fail(914);
  }
  for (index = 62; index >= 0; index--) {
    nexus_data_stack_push_pointer(stack, index == 17 ? NULL : &values[index]);
  }
  for (index = 0; index < 64; index++) {
    expected_index = index;
    expected       = expected_index == 17 ? NULL : &values[expected_index];
    if (nexus_data_stack_peek_pointer(stack) != expected || !nexus_data_stack_pop_pointer(stack, &output) || output != expected) {
      return nexus_test_fail(915);
    }
  }
  output = &values[0];
  if (nexus_data_stack_pop_pointer(stack, &output) || output != &values[0] || nexus_data_stack_peek_pointer(stack) != NULL) {
    return nexus_test_fail(916);
  }
  nexus_data_stack_push_pointer(stack, NULL);
  if (!nexus_data_stack_pop_pointer(stack, NULL)) {
    return nexus_test_fail(917);
  }
  nexus_data_stack_destroy(stack);
  slabbed_stack = nexus_data_slabbed_stack_create(NEXUS_SIZEOF(void *), 0, 3U);
  output        = &values[0];
  if (nexus_data_slabbed_stack_peek_pointer(slabbed_stack) != NULL || nexus_data_slabbed_stack_pop_pointer(slabbed_stack, &output) ||
      output != &values[0]) {
    return nexus_test_fail(918);
  }
  saved_slot = nexus_data_slabbed_stack_push_pointer(slabbed_stack, &values[63]);
  if (*(void **)saved_slot != &values[63]) {
    return nexus_test_fail(919);
  }
  for (index = 62; index >= 0; index--) {
    nexus_data_slabbed_stack_push_pointer(slabbed_stack, index == 17 ? NULL : &values[index]);
  }
  if (*(void **)saved_slot != &values[63]) {
    return nexus_test_fail(920);
  }
  for (index = 0; index < 64; index++) {
    expected_index = index;
    expected       = expected_index == 17 ? NULL : &values[expected_index];
    if (nexus_data_slabbed_stack_peek_pointer(slabbed_stack) != expected || !nexus_data_slabbed_stack_pop_pointer(slabbed_stack, &output) ||
        output != expected) {
      return nexus_test_fail(921);
    }
  }
  output = &values[0];
  if (nexus_data_slabbed_stack_pop_pointer(slabbed_stack, &output) || output != &values[0] ||
      nexus_data_slabbed_stack_peek_pointer(slabbed_stack) != NULL) {
    return nexus_test_fail(922);
  }
  nexus_data_slabbed_stack_push_pointer(slabbed_stack, NULL);
  if (!nexus_data_slabbed_stack_pop_pointer(slabbed_stack, NULL)) {
    return nexus_test_fail(923);
  }
  nexus_data_slabbed_stack_destroy(slabbed_stack);
  queue  = nexus_data_queue_create(NEXUS_SIZEOF(void *), 0);
  output = &values[0];
  if (nexus_data_queue_peek_pointer(queue) != NULL || nexus_data_queue_pop_pointer(queue, &output) || output != &values[0]) {
    return nexus_test_fail(924);
  }
  saved_slot = nexus_data_queue_push_pointer(queue, &values[63]);
  if (*(void **)saved_slot != &values[63]) {
    return nexus_test_fail(925);
  }
  for (index = 62; index >= 0; index--) {
    nexus_data_queue_push_pointer(queue, index == 17 ? NULL : &values[index]);
  }
  for (index = 0; index < 64; index++) {
    expected_index = 63 - index;
    expected       = expected_index == 17 ? NULL : &values[expected_index];
    if (nexus_data_queue_peek_pointer(queue) != expected || !nexus_data_queue_pop_pointer(queue, &output) || output != expected) {
      return nexus_test_fail(926);
    }
  }
  output = &values[0];
  if (nexus_data_queue_pop_pointer(queue, &output) || output != &values[0] || nexus_data_queue_peek_pointer(queue) != NULL) {
    return nexus_test_fail(927);
  }
  nexus_data_queue_push_pointer(queue, NULL);
  if (!nexus_data_queue_pop_pointer(queue, NULL)) {
    return nexus_test_fail(928);
  }
  nexus_data_queue_destroy(queue);
  slabbed_queue = nexus_data_slabbed_queue_create(NEXUS_SIZEOF(void *), 0, 3U);
  output        = &values[0];
  if (nexus_data_slabbed_queue_peek_pointer(slabbed_queue) != NULL || nexus_data_slabbed_queue_pop_pointer(slabbed_queue, &output) ||
      output != &values[0]) {
    return nexus_test_fail(929);
  }
  saved_slot = nexus_data_slabbed_queue_push_pointer(slabbed_queue, &values[63]);
  if (*(void **)saved_slot != &values[63]) {
    return nexus_test_fail(930);
  }
  for (index = 62; index >= 0; index--) {
    nexus_data_slabbed_queue_push_pointer(slabbed_queue, index == 17 ? NULL : &values[index]);
  }
  if (*(void **)saved_slot != &values[63]) {
    return nexus_test_fail(931);
  }
  for (index = 0; index < 64; index++) {
    expected_index = 63 - index;
    expected       = expected_index == 17 ? NULL : &values[expected_index];
    if (nexus_data_slabbed_queue_peek_pointer(slabbed_queue) != expected || !nexus_data_slabbed_queue_pop_pointer(slabbed_queue, &output) ||
        output != expected) {
      return nexus_test_fail(932);
    }
  }
  output = &values[0];
  if (nexus_data_slabbed_queue_pop_pointer(slabbed_queue, &output) || output != &values[0] ||
      nexus_data_slabbed_queue_peek_pointer(slabbed_queue) != NULL) {
    return nexus_test_fail(933);
  }
  nexus_data_slabbed_queue_push_pointer(slabbed_queue, NULL);
  if (!nexus_data_slabbed_queue_pop_pointer(slabbed_queue, NULL)) {
    return nexus_test_fail(934);
  }
  nexus_data_slabbed_queue_destroy(slabbed_queue);
  priority_queue = nexus_data_priority_queue_create(NEXUS_SIZEOF(void *), 0, nexus_test_compare_pointer_ints, NULL);
  output         = &values[0];
  if (nexus_data_priority_queue_peek_pointer(priority_queue) != NULL || nexus_data_priority_queue_pop_pointer(priority_queue, &output) ||
      output != &values[0]) {
    return nexus_test_fail(935);
  }
  saved_slot = nexus_data_priority_queue_push_pointer(priority_queue, &values[63]);
  if (*(void **)saved_slot != &values[63]) {
    return nexus_test_fail(936);
  }
  for (index = 62; index >= 0; index--) {
    nexus_data_priority_queue_push_pointer(priority_queue, index == 17 ? NULL : &values[index]);
  }
  for (index = 0; index < 64; index++) {
    expected_index = index - 1;
    if (expected_index >= 17) {
      expected_index++;
    }
    expected = index == 0 ? NULL : &values[expected_index];
    if (nexus_data_priority_queue_peek_pointer(priority_queue) != expected || !nexus_data_priority_queue_pop_pointer(priority_queue, &output) ||
        output != expected) {
      return nexus_test_fail(937);
    }
  }
  output = &values[0];
  if (nexus_data_priority_queue_pop_pointer(priority_queue, &output) || output != &values[0] ||
      nexus_data_priority_queue_peek_pointer(priority_queue) != NULL) {
    return nexus_test_fail(938);
  }
  nexus_data_priority_queue_push_pointer(priority_queue, NULL);
  if (!nexus_data_priority_queue_pop_pointer(priority_queue, NULL)) {
    return nexus_test_fail(939);
  }
  nexus_data_priority_queue_destroy(priority_queue);
  slabbed_priority_queue = nexus_data_slabbed_priority_queue_create(NEXUS_SIZEOF(void *), 0, 3U, nexus_test_compare_pointer_ints, NULL);
  output                 = &values[0];
  if (nexus_data_slabbed_priority_queue_peek_pointer(slabbed_priority_queue) != NULL ||
      nexus_data_slabbed_priority_queue_pop_pointer(slabbed_priority_queue, &output) || output != &values[0]) {
    return nexus_test_fail(940);
  }
  saved_slot = nexus_data_slabbed_priority_queue_push_pointer(slabbed_priority_queue, &values[63]);
  if (*(void **)saved_slot != &values[63]) {
    return nexus_test_fail(941);
  }
  for (index = 62; index >= 0; index--) {
    nexus_data_slabbed_priority_queue_push_pointer(slabbed_priority_queue, index == 17 ? NULL : &values[index]);
  }
  if (*(void **)saved_slot != &values[63]) {
    return nexus_test_fail(942);
  }
  for (index = 0; index < 64; index++) {
    expected_index = index - 1;
    if (expected_index >= 17) {
      expected_index++;
    }
    expected = index == 0 ? NULL : &values[expected_index];
    if (nexus_data_slabbed_priority_queue_peek_pointer(slabbed_priority_queue) != expected ||
        !nexus_data_slabbed_priority_queue_pop_pointer(slabbed_priority_queue, &output) || output != expected) {
      return nexus_test_fail(943);
    }
  }
  output = &values[0];
  if (nexus_data_slabbed_priority_queue_pop_pointer(slabbed_priority_queue, &output) || output != &values[0] ||
      nexus_data_slabbed_priority_queue_peek_pointer(slabbed_priority_queue) != NULL) {
    return nexus_test_fail(944);
  }
  nexus_data_slabbed_priority_queue_push_pointer(slabbed_priority_queue, NULL);
  if (!nexus_data_slabbed_priority_queue_pop_pointer(slabbed_priority_queue, NULL)) {
    return nexus_test_fail(945);
  }
  nexus_data_slabbed_priority_queue_destroy(slabbed_priority_queue);
  ringbuffer = nexus_data_ringbuffer_create(NEXUS_SIZEOF(void *), 3U);
  output     = &values[0];
  if (nexus_data_ringbuffer_pop_pointer(ringbuffer, &output) || output != &values[0]) {
    return nexus_test_fail(946);
  }
  if (nexus_data_ringbuffer_push_pointer(ringbuffer, &values[0]) || nexus_data_ringbuffer_push_pointer(ringbuffer, NULL) ||
      nexus_data_ringbuffer_push_pointer(ringbuffer, &values[2])) {
    return nexus_test_fail(947);
  }
  if (!nexus_data_ringbuffer_push_pointer(ringbuffer, &values[3]) || nexus_data_ringbuffer_pointer_at(ringbuffer, 0) != NULL ||
      nexus_data_ringbuffer_pointer_at(ringbuffer, 1U) != &values[2]) {
    return nexus_test_fail(948);
  }
  nexus_data_ringbuffer_linearize(ringbuffer);
  if (!nexus_data_ringbuffer_pop_pointer(ringbuffer, &output) || output != NULL) {
    return nexus_test_fail(949);
  }
  if (nexus_data_ringbuffer_push_pointer(ringbuffer, &values[4])) {
    return nexus_test_fail(950);
  }
  for (index = 2; index <= 4; index++) {
    if (!nexus_data_ringbuffer_pop_pointer(ringbuffer, &output) || output != &values[index]) {
      return nexus_test_fail(960);
    }
  }
  nexus_data_ringbuffer_push_pointer(ringbuffer, NULL);
  if (!nexus_data_ringbuffer_pop_pointer(ringbuffer, NULL)) {
    return nexus_test_fail(951);
  }
  output = &values[0];
  if (nexus_data_ringbuffer_pop_pointer(ringbuffer, &output) || output != &values[0]) {
    return nexus_test_fail(952);
  }
  nexus_data_ringbuffer_destroy(ringbuffer);
  nexus_data_hashmap_config_initialize(&config, NEXUS_SIZEOF(int), NEXUS_SIZEOF(void *));
  config.seed_is_set = TRUE;
  config.seed        = 7U;
  error              = nexus_data_hashmap_create(&config, &hashmap);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(953);
  }
  for (key = 0; key < 64; key++) {
    nexus_data_hashmap_put_pointer(hashmap, &key, key == 17 ? NULL : &values[key]);
  }
  nexus_data_hashmap_reserve(hashmap, 512U);
  for (key = 0; key < 64; key++) {
    expected = key == 17 ? NULL : &values[key];
    output   = &values[0];
    if (nexus_data_hashmap_get_pointer(hashmap, &key) != expected || !nexus_data_hashmap_get_copy_pointer(hashmap, &key, &output) ||
        output != expected) {
      return nexus_test_fail(954);
    }
  }
  key = 3;
  nexus_data_hashmap_put_pointer(hashmap, &key, NULL);
  if (nexus_data_hashmap_length_get(hashmap) != 64U || !nexus_data_hashmap_remove_pointer(hashmap, &key, &output) || output != NULL) {
    return nexus_test_fail(955);
  }
  key = 17;
  if (!nexus_data_hashmap_remove_pointer(hashmap, &key, NULL)) {
    return nexus_test_fail(956);
  }
  key = 63;
  if (!nexus_data_hashmap_remove_pointer(hashmap, &key, &output) || output != &values[63]) {
    return nexus_test_fail(957);
  }
  key    = 99;
  output = &values[0];
  if (nexus_data_hashmap_get_pointer(hashmap, &key) != NULL || nexus_data_hashmap_get_copy_pointer(hashmap, &key, &output) ||
      nexus_data_hashmap_remove_pointer(hashmap, &key, &output) || output != &values[0]) {
    return nexus_test_fail(958);
  }
  nexus_data_hashmap_destroy(hashmap);
  nexus_data_hashmap_config_initialize(&config, NEXUS_SIZEOF(int), NEXUS_SIZEOF(void *));
  config.seed_is_set = TRUE;
  config.seed        = 7U;
  error              = nexus_data_slabbed_hashmap_create(&config, 3U, &slabbed_hashmap);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(959);
  }
  for (key = 0; key < 64; key++) {
    nexus_data_slabbed_hashmap_put_pointer(slabbed_hashmap, &key, key == 17 ? NULL : &values[key]);
  }
  nexus_data_slabbed_hashmap_reserve(slabbed_hashmap, 512U);
  for (key = 0; key < 64; key++) {
    expected = key == 17 ? NULL : &values[key];
    output   = &values[0];
    if (nexus_data_slabbed_hashmap_get_pointer(slabbed_hashmap, &key) != expected ||
        !nexus_data_slabbed_hashmap_get_copy_pointer(slabbed_hashmap, &key, &output) || output != expected) {
      return nexus_test_fail(960);
    }
  }
  key = 3;
  nexus_data_slabbed_hashmap_put_pointer(slabbed_hashmap, &key, NULL);
  if (nexus_data_slabbed_hashmap_length_get(slabbed_hashmap) != 64U || !nexus_data_slabbed_hashmap_remove_pointer(slabbed_hashmap, &key, &output) ||
      output != NULL) {
    return nexus_test_fail(961);
  }
  key = 17;
  if (!nexus_data_slabbed_hashmap_remove_pointer(slabbed_hashmap, &key, NULL)) {
    return nexus_test_fail(962);
  }
  key = 63;
  if (!nexus_data_slabbed_hashmap_remove_pointer(slabbed_hashmap, &key, &output) || output != &values[63]) {
    return nexus_test_fail(963);
  }
  key    = 99;
  output = &values[0];
  if (nexus_data_slabbed_hashmap_get_pointer(slabbed_hashmap, &key) != NULL ||
      nexus_data_slabbed_hashmap_get_copy_pointer(slabbed_hashmap, &key, &output) ||
      nexus_data_slabbed_hashmap_remove_pointer(slabbed_hashmap, &key, &output) || output != &values[0]) {
    return nexus_test_fail(964);
  }
  nexus_data_slabbed_hashmap_destroy(slabbed_hashmap);
  return 0;
}

#if NEXUS_DEVELOPER_MODE
static int nexus_test_data_pointer_size_assertions(void) {
  NexusDataArray                 array;
  NexusDataList                 *list;
  NexusDataSlabbedList          *slabbed_list;
  NexusDataStack                *stack;
  NexusDataSlabbedStack         *slabbed_stack;
  NexusDataQueue                *queue;
  NexusDataSlabbedQueue         *slabbed_queue;
  NexusDataRingBuffer           *ringbuffer;
  NexusDataPriorityQueue        *priority_queue;
  NexusDataSlabbedPriorityQueue *slabbed_priority_queue;
  NexusDataHashMap              *hashmap;
  NexusDataSlabbedHashMap       *slabbed_hashmap;
  NexusDataHashMapConfig         config;
  NexusError                    *error;
  jmp_buf                        recovery_point;
  byte                           storage;
  int                            index;
  int                            key;
  void                          *output;

  key                    = 1;
  output                 = NULL;
  array                  = nexus_data_array_view_create(&storage, 1U, 1U);
  list                   = nexus_data_list_create(1U, 0);
  slabbed_list           = nexus_data_slabbed_list_create(1U, 0, 3U);
  stack                  = nexus_data_stack_create(1U, 0);
  slabbed_stack          = nexus_data_slabbed_stack_create(1U, 0, 3U);
  queue                  = nexus_data_queue_create(1U, 0);
  slabbed_queue          = nexus_data_slabbed_queue_create(1U, 0, 3U);
  ringbuffer             = nexus_data_ringbuffer_create(1U, 1U);
  priority_queue         = nexus_data_priority_queue_create(1U, 0, nexus_test_compare_pointer_ints, NULL);
  slabbed_priority_queue = nexus_data_slabbed_priority_queue_create(1U, 0, 3U, nexus_test_compare_pointer_ints, NULL);
  nexus_data_hashmap_config_initialize(&config, NEXUS_SIZEOF(int), 1U);
  config.seed_is_set = TRUE;
  config.seed        = 0;
  error              = nexus_data_hashmap_create(&config, &hashmap);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(980);
  }
  nexus_data_hashmap_config_initialize(&config, NEXUS_SIZEOF(int), 1U);
  config.seed_is_set = TRUE;
  config.seed        = 0;
  error              = nexus_data_slabbed_hashmap_create(&config, 3U, &slabbed_hashmap);
  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return nexus_test_fail(980);
  }
  for (index = 0; index < 41; index++) {
    if (setjmp(recovery_point) == 0) {
      nexus_program_recovery_arm(&recovery_point);
      switch (index) {
      case 0:
        nexus_data_array_set_pointer(&array, 0, NULL);
        break;
      case 1:
        (void)nexus_data_array_pointer_at(&array, 0);
        break;
      case 2:
        (void)nexus_data_list_append_pointer(list, NULL);
        break;
      case 3:
        (void)nexus_data_list_pointer_at(list, 0);
        break;
      case 4:
        nexus_data_list_insert_pointer(list, 0, NULL);
        break;
      case 5:
        nexus_data_list_remove_pointer(list, 0, &output);
        break;
      case 6:
        (void)nexus_data_list_pop_pointer(list, &output);
        break;
      case 7:
        (void)nexus_data_stack_push_pointer(stack, NULL);
        break;
      case 8:
        (void)nexus_data_stack_pop_pointer(stack, &output);
        break;
      case 9:
        (void)nexus_data_stack_peek_pointer(stack);
        break;
      case 10:
        (void)nexus_data_queue_push_pointer(queue, NULL);
        break;
      case 11:
        (void)nexus_data_queue_pop_pointer(queue, &output);
        break;
      case 12:
        (void)nexus_data_queue_peek_pointer(queue);
        break;
      case 13:
        (void)nexus_data_ringbuffer_push_pointer(ringbuffer, NULL);
        break;
      case 14:
        (void)nexus_data_ringbuffer_pop_pointer(ringbuffer, &output);
        break;
      case 15:
        (void)nexus_data_ringbuffer_pointer_at(ringbuffer, 0);
        break;
      case 16:
        (void)nexus_data_priority_queue_push_pointer(priority_queue, NULL);
        break;
      case 17:
        (void)nexus_data_priority_queue_pop_pointer(priority_queue, &output);
        break;
      case 18:
        (void)nexus_data_priority_queue_peek_pointer(priority_queue);
        break;
      case 19:
        (void)nexus_data_slabbed_list_append_pointer(slabbed_list, NULL);
        break;
      case 20:
        (void)nexus_data_slabbed_list_pointer_at(slabbed_list, 0);
        break;
      case 21:
        nexus_data_slabbed_list_insert_pointer(slabbed_list, 0, NULL);
        break;
      case 22:
        nexus_data_slabbed_list_remove_pointer(slabbed_list, 0, &output);
        break;
      case 23:
        (void)nexus_data_slabbed_list_pop_pointer(slabbed_list, &output);
        break;
      case 24:
        (void)nexus_data_slabbed_stack_push_pointer(slabbed_stack, NULL);
        break;
      case 25:
        (void)nexus_data_slabbed_stack_pop_pointer(slabbed_stack, &output);
        break;
      case 26:
        (void)nexus_data_slabbed_stack_peek_pointer(slabbed_stack);
        break;
      case 27:
        (void)nexus_data_slabbed_queue_push_pointer(slabbed_queue, NULL);
        break;
      case 28:
        (void)nexus_data_slabbed_queue_pop_pointer(slabbed_queue, &output);
        break;
      case 29:
        (void)nexus_data_slabbed_queue_peek_pointer(slabbed_queue);
        break;
      case 30:
        (void)nexus_data_slabbed_priority_queue_push_pointer(slabbed_priority_queue, NULL);
        break;
      case 31:
        (void)nexus_data_slabbed_priority_queue_pop_pointer(slabbed_priority_queue, &output);
        break;
      case 32:
        (void)nexus_data_slabbed_priority_queue_peek_pointer(slabbed_priority_queue);
        break;
      case 33:
        nexus_data_hashmap_put_pointer(hashmap, &key, NULL);
        break;
      case 34:
        (void)nexus_data_hashmap_get_pointer(hashmap, &key);
        break;
      case 35:
        (void)nexus_data_hashmap_get_copy_pointer(hashmap, &key, &output);
        break;
      case 36:
        (void)nexus_data_hashmap_remove_pointer(hashmap, &key, &output);
        break;
      case 37:
        nexus_data_slabbed_hashmap_put_pointer(slabbed_hashmap, &key, NULL);
        break;
      case 38:
        (void)nexus_data_slabbed_hashmap_get_pointer(slabbed_hashmap, &key);
        break;
      case 39:
        (void)nexus_data_slabbed_hashmap_get_copy_pointer(slabbed_hashmap, &key, &output);
        break;
      case 40:
        (void)nexus_data_slabbed_hashmap_remove_pointer(slabbed_hashmap, &key, &output);
        break;
      default:
        return nexus_test_fail(981);
      }
      nexus_program_recovery_disarm();
      return nexus_test_fail(982);
    }
    nexus_program_recovery_disarm();
    error = nexus_program_recovery_last_error_get();
    if (error == NULL || error->code != GLOBAL_NEXUS_PRECONDITION_VIOLATION) {
      return nexus_test_fail(983);
    }
  }
  nexus_data_list_destroy(list);
  nexus_data_slabbed_list_destroy(slabbed_list);
  nexus_data_stack_destroy(stack);
  nexus_data_slabbed_stack_destroy(slabbed_stack);
  nexus_data_queue_destroy(queue);
  nexus_data_slabbed_queue_destroy(slabbed_queue);
  nexus_data_ringbuffer_destroy(ringbuffer);
  nexus_data_priority_queue_destroy(priority_queue);
  nexus_data_slabbed_priority_queue_destroy(slabbed_priority_queue);
  nexus_data_hashmap_destroy(hashmap);
  nexus_data_slabbed_hashmap_destroy(slabbed_hashmap);
  return 0;
}
#endif

static int nexus_test_memory_bytes_equal(void) {
  const byte left[]     = {0x00U, 0x80U, 0xffU, 0x42U};
  byte       right[]    = {0x00U, 0x80U, 0xffU, 0x42U};
  const byte repeated[] = {0x80U, 0x80U, 0x80U, 0x80U};
  uint_large index;

  if (!nexus_memory_bytes_equal(left, right, NEXUS_SIZEOF(left)) || !nexus_memory_bytes_equal(left, left, NEXUS_SIZEOF(left)) ||
      !nexus_memory_bytes_equal(left + 1, right + 1, NEXUS_SIZEOF(left) - 1U) ||
      !nexus_memory_bytes_equal(repeated, repeated + 1, NEXUS_SIZEOF(repeated) - 1U)) {
    return nexus_test_fail(1401);
  }

  for (index = 0; index < NEXUS_SIZEOF(left); index++) {
    right[index] ^= 1U;

    if (nexus_memory_bytes_equal(left, right, NEXUS_SIZEOF(left)) || nexus_memory_bytes_equal(right, left, NEXUS_SIZEOF(left)) ||
        !nexus_memory_bytes_equal(left, right, index)) {
      return nexus_test_fail(1402);
    }

    right[index] = left[index];
  }

  return 0;
}

static int nexus_test_memory_bytes_move(void) {
  byte       bytes[]       = {0, 1, 2, 3, 4, 5};
  const byte moved_right[] = {0, 0, 1, 2, 3, 4};
  const byte moved_left[]  = {0, 1, 2, 3, 4, 4};

  nexus_memory_bytes_move(bytes, bytes + 1, 5);

  if (!nexus_memory_bytes_equal(bytes, moved_right, NEXUS_SIZEOF(bytes)))
    return nexus_test_fail(1403);

  nexus_memory_bytes_move(bytes + 1, bytes, 5);
  nexus_memory_bytes_move(bytes, bytes, NEXUS_SIZEOF(bytes));
  nexus_memory_bytes_move(bytes, bytes + 1, 0);

  if (!nexus_memory_bytes_equal(bytes, moved_left, NEXUS_SIZEOF(bytes)))
    return nexus_test_fail(1404);

  return 0;
}

static int nexus_test_data_chunked_list(void) {
  NexusDataChunkedList list;
  uint64              *saved[300];
  uint64              *slot;
  uint64               value;
  uint_large           index;

  nexus_data_chunked_list_initialize(&list, NEXUS_SIZEOF(uint64), 3U);
  {
    NexusDataList *sized;

    sized = nexus_data_list_create(NEXUS_SIZEOF(uint64), 0);

    if (nexus_data_list_element_size_get(sized) != NEXUS_SIZEOF(uint64)) {
      return nexus_test_fail(2000);
    }

    nexus_data_list_destroy(sized);
  }

  if (nexus_data_chunked_list_length_get(&list) != 0) {
    return nexus_test_fail(2001);
  }

  /* 300 elements span 38 chunks of 8, so the chunk table doubles twice; addresses must not move. */
  for (index = 0; index < 300; index++) {
    value        = (uint64)index * 7U + 1U;
    saved[index] = (uint64 *)nexus_data_chunked_list_append(&list, &value);
  }

  if (nexus_data_chunked_list_length_get(&list) != 300) {
    return nexus_test_fail(2002);
  }

  for (index = 0; index < 300; index++) {
    slot = (uint64 *)nexus_data_chunked_list_at(&list, index);

    if (slot != saved[index] || *slot != (uint64)index * 7U + 1U) {
      return nexus_test_fail(2003);
    }
  }

  slot  = (uint64 *)nexus_data_chunked_list_append_uninitialized(&list);
  *slot = 99U;

  if (*(uint64 *)nexus_data_chunked_list_at(&list, 300) != 99U) {
    return nexus_test_fail(2004);
  }

  /* Clear keeps the chunks: refilling reuses the same storage. */
  nexus_data_chunked_list_clear(&list);

  if (nexus_data_chunked_list_length_get(&list) != 0) {
    return nexus_test_fail(2005);
  }

  value = 5U;
  slot  = (uint64 *)nexus_data_chunked_list_append(&list, &value);

  if (slot != saved[0] || *slot != 5U) {
    return nexus_test_fail(2006);
  }

  nexus_data_chunked_list_release(&list);

  return 0;
}

static int nexus_test_data_word_map(void) {
  NexusDataWordMap *map;
  uint_large        key[4];
  uint_large        reference[4096];
  uint_large       *value;
  uint_large        state;
  uint_large        index;
  uint_large        round;

  /* Four-word keys, two-word values: insert past several growths, then verify against a reference array. */
  map = nexus_data_word_map_create(4U, 2U, 4);

  for (round = 0; round < 2; round++) {
    state = 12345U + round;

    for (index = 0; index < 4096; index++) {
      key[0] = index;
      key[1] = index * 3U;
      key[2] = round;
      key[3] = 0;

      if (nexus_data_word_map_get(map, key) != NULL) {
        return nexus_test_fail(2101);
      }

      state            = (state * 6364136223846793005UL) + 1442695040888963407UL;
      reference[index] = state;
      value            = nexus_data_word_map_insert(map, key);
      value[0]         = state;
      value[1]         = ~state;
    }

    if (nexus_data_word_map_length_get(map) != 4096) {
      return nexus_test_fail(2102);
    }

    for (index = 0; index < 4096; index++) {
      key[0] = index;
      key[1] = index * 3U;
      key[2] = round;
      key[3] = 0;
      value  = nexus_data_word_map_get(map, key);

      if (value == NULL || value[0] != reference[index] || value[1] != ~reference[index]) {
        return nexus_test_fail(2103);
      }

      /* A key differing in any one word is absent. */
      key[3] = 1;

      if (nexus_data_word_map_get(map, key) != NULL) {
        return nexus_test_fail(2104);
      }
    }

    /* Clear is O(1) and forgets everything, but the next round reuses the same table. */
    nexus_data_word_map_clear(map);

    if (nexus_data_word_map_length_get(map) != 0) {
      return nexus_test_fail(2105);
    }

    key[0] = 0;
    key[1] = 0;
    key[2] = round;
    key[3] = 0;

    if (nexus_data_word_map_get(map, key) != NULL) {
      return nexus_test_fail(2106);
    }
  }

  nexus_data_word_map_destroy(map);

  /* One-word keys, one-word values. */
  map    = nexus_data_word_map_create(1U, 1U, 0);
  key[0] = 42U;
  value  = nexus_data_word_map_insert(map, key);
  *value = 7U;
  value  = nexus_data_word_map_get(map, key);

  if (value == NULL || *value != 7U) {
    return nexus_test_fail(2107);
  }

  nexus_data_word_map_destroy(map);
  nexus_data_word_map_destroy(NULL);

  return 0;
}

int main(void) {
  int result;

  result = nexus_test_memory_bytes_move();
  if (result != 0) {
    return result;
  }

  result = nexus_test_memory_bytes_equal();
  if (result != 0) {
    return result;
  }

  result = nexus_test_data_chunked_list();
  if (result != 0) {
    return result;
  }

  result = nexus_test_data_word_map();
  if (result != 0) {
    return result;
  }

  result = nexus_test_errors_wrap();
  if (result != 0) {
    return result;
  }
  result = nexus_test_hashes();
  if (result != 0) {
    return result;
  }
  result = nexus_test_json();
  if (result != 0) {
    return result;
  }
  result = nexus_test_random();
  if (result != 0) {
    return result;
  }
  result = nexus_test_identity();
  if (result != 0) {
    return result;
  }
#if NEXUS_DEVELOPER_MODE
  result = nexus_test_data_pointer_size_assertions();
  if (result != 0) {
    return result;
  }
#endif
  result = nexus_test_data_pointers();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_slabbed_list();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_slabbed_stack();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_slabbed_queue();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_slabbed_priority_queue();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_slabbed_hashmap(FALSE);
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_slabbed_hashmap(TRUE);
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_slabbed_hashmap_rebuild();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_slabbed_overflow();
  if (result != 0) {
    return result;
  }
#if NEXUS_MEMORY_TRACING
  result = nexus_test_data_slabbed_reserved();
  if (result != 0) {
    return result;
  }
#endif
  result = nexus_test_data_hashmap_rehash(FALSE);
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_hashmap_rehash(TRUE);
  if (result != 0) {
    return result;
  }
  result = nexus_test_bits();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_bitset();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_reverse_sequences();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_reverse_circular();
  if (result != 0) {
    return result;
  }
  result = nexus_test_data_contiguous();
  if (result != 0) {
    return result;
  }
  result = nexus_test_recovery();
  if (result != 0) {
    return result;
  }
  result = nexus_test_keys_sort();
  if (result != 0) {
    return result;
  }
  result = nexus_test_strings();
  if (result != 0) {
    return result;
  }
  result = nexus_test_memory_threads();
  if (result != 0) {
    return result;
  }
#if NEXUS_PLATFORM_LINUX
  result = nexus_test_process();
  if (result != 0) {
    return result;
  }
#endif
  return 0;
}
