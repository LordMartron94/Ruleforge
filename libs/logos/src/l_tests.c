#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "../logos_tests.h"

#ifndef L_TESTS_NFA_INPUT_SHOWN_MAX
#  define L_TESTS_NFA_INPUT_SHOWN_MAX 40
#endif

typedef struct LTestsNfaInput {
  const char *text;
  uint_large  size; /* Explicit, so an input may hold NUL bytes. */
  boolean     expected;
} LTestsNfaInput;

typedef struct LTestsNfaTest {
  const char           *regexp;
  const LTestsNfaInput *inputs;
  uint_large            input_count;
  uint16                minimal_states; /* States of the minimal DFA, dead state included; 0 leaves it unchecked. */
} LTestsNfaTest;

#define L_TESTS_NFA_INPUT(text, expected) {(text), NEXUS_SIZEOF(text) - 1, (expected)}

static const LTestsNfaInput l_tests_nfa_inputs_empty_expression[] = {
    L_TESTS_NFA_INPUT("", TRUE),
    L_TESTS_NFA_INPUT("a", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_empty_class[] = {
    L_TESTS_NFA_INPUT("", FALSE),
    L_TESTS_NFA_INPUT("a", FALSE),
    L_TESTS_NFA_INPUT("\xFF", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_literal[] = {
    L_TESTS_NFA_INPUT("abc", TRUE), L_TESTS_NFA_INPUT("ab", FALSE),  L_TESTS_NFA_INPUT("abcd", FALSE),
    L_TESTS_NFA_INPUT("", FALSE),   L_TESTS_NFA_INPUT("abd", FALSE), L_TESTS_NFA_INPUT("xabc", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_star[] = {
    L_TESTS_NFA_INPUT("", TRUE),   L_TESTS_NFA_INPUT("a", TRUE),    L_TESTS_NFA_INPUT("aaaa", TRUE),
    L_TESTS_NFA_INPUT("b", FALSE), L_TESTS_NFA_INPUT("aab", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_plus[] = {
    L_TESTS_NFA_INPUT("ab", TRUE), L_TESTS_NFA_INPUT("aaab", TRUE), L_TESTS_NFA_INPUT("b", FALSE),
    L_TESTS_NFA_INPUT("a", FALSE), L_TESTS_NFA_INPUT("abb", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_suffix_abb[] = {
    L_TESTS_NFA_INPUT("abb", TRUE), L_TESTS_NFA_INPUT("aabb", TRUE),  L_TESTS_NFA_INPUT("babb", TRUE), L_TESTS_NFA_INPUT("ababb", TRUE),
    L_TESTS_NFA_INPUT("ab", FALSE), L_TESTS_NFA_INPUT("abba", FALSE), L_TESTS_NFA_INPUT("", FALSE),    L_TESTS_NFA_INPUT("bb", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_optional[] = {
    L_TESTS_NFA_INPUT("color", TRUE), L_TESTS_NFA_INPUT("colour", TRUE), L_TESTS_NFA_INPUT("colouur", FALSE),
    L_TESTS_NFA_INPUT("colr", FALSE), L_TESTS_NFA_INPUT("colou", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_phone[] = {
    L_TESTS_NFA_INPUT("555-1234", TRUE), L_TESTS_NFA_INPUT("55-1234", FALSE),  L_TESTS_NFA_INPUT("5555-1234", FALSE),
    L_TESTS_NFA_INPUT("555-123", FALSE), L_TESTS_NFA_INPUT("abc-defg", FALSE), L_TESTS_NFA_INPUT("555 1234", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_bounded[] = {
    L_TESTS_NFA_INPUT("", FALSE),   L_TESTS_NFA_INPUT("a", FALSE),   L_TESTS_NFA_INPUT("aa", TRUE),
    L_TESTS_NFA_INPUT("aaa", TRUE), L_TESTS_NFA_INPUT("aaaa", TRUE), L_TESTS_NFA_INPUT("aaaaa", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_group_star[] = {
    L_TESTS_NFA_INPUT("", TRUE),     L_TESTS_NFA_INPUT("ab", TRUE),  L_TESTS_NFA_INPUT("abab", TRUE),
    L_TESTS_NFA_INPUT("aba", FALSE), L_TESTS_NFA_INPUT("ba", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_decimal[] = {
    L_TESTS_NFA_INPUT("3.14", TRUE), L_TESTS_NFA_INPUT("10.5", TRUE),  L_TESTS_NFA_INPUT("0.001", TRUE),  L_TESTS_NFA_INPUT(".5", FALSE),
    L_TESTS_NFA_INPUT("3.", FALSE),  L_TESTS_NFA_INPUT("3x14", FALSE), L_TESTS_NFA_INPUT("3.1.4", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_negated_class[] = {
    L_TESTS_NFA_INPUT("xyz", TRUE),  L_TESTS_NFA_INPUT("d", TRUE),   L_TESTS_NFA_INPUT("\xFF", TRUE),
    L_TESTS_NFA_INPUT("abc", FALSE), L_TESTS_NFA_INPUT("xa", FALSE), L_TESTS_NFA_INPUT("", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_any_byte[] = {
    L_TESTS_NFA_INPUT("abc", TRUE),
    L_TESTS_NFA_INPUT("a\nc", TRUE),
    L_TESTS_NFA_INPUT("a\xFF"
                      "c",
                      TRUE),
    L_TESTS_NFA_INPUT("ac", FALSE),
    L_TESTS_NFA_INPUT("abbc", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_two_optionals[] = {
    L_TESTS_NFA_INPUT("", TRUE),   L_TESTS_NFA_INPUT("a", TRUE),   L_TESTS_NFA_INPUT("b", TRUE),
    L_TESTS_NFA_INPUT("ab", TRUE), L_TESTS_NFA_INPUT("ba", FALSE), L_TESTS_NFA_INPUT("aab", FALSE),
};

/* Several ways to split the same input: the NFA has to follow all of them at once. */
static const LTestsNfaInput l_tests_nfa_inputs_ambiguous_split[] = {
    L_TESTS_NFA_INPUT("ac", TRUE),     L_TESTS_NFA_INPUT("abc", TRUE),  L_TESTS_NFA_INPUT("abcd", TRUE), L_TESTS_NFA_INPUT("abd", FALSE),
    L_TESTS_NFA_INPUT("abcdd", FALSE), L_TESTS_NFA_INPUT("bcd", FALSE), L_TESTS_NFA_INPUT("", FALSE),
};

/* Accepting states that also have outgoing transitions. */
static const LTestsNfaInput l_tests_nfa_inputs_prefix_overlap[] = {
    L_TESTS_NFA_INPUT("", TRUE),     L_TESTS_NFA_INPUT("a", TRUE),    L_TESTS_NFA_INPUT("ab", TRUE), L_TESTS_NFA_INPUT("aba", TRUE),
    L_TESTS_NFA_INPUT("abab", TRUE), L_TESTS_NFA_INPUT("abb", FALSE), L_TESTS_NFA_INPUT("b", FALSE),
};

/* A nullable operand inside a star: first and last sets must still be right. */
static const LTestsNfaInput l_tests_nfa_inputs_nested_star[] = {
    L_TESTS_NFA_INPUT("b", TRUE),  L_TESTS_NFA_INPUT("ab", TRUE), L_TESTS_NFA_INPUT("aab", TRUE),
    L_TESTS_NFA_INPUT("a", FALSE), L_TESTS_NFA_INPUT("", FALSE),  L_TESTS_NFA_INPUT("bb", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_star_of_stars[] = {
    L_TESTS_NFA_INPUT("c", TRUE),   L_TESTS_NFA_INPUT("abc", TRUE), L_TESTS_NFA_INPUT("bac", TRUE), L_TESTS_NFA_INPUT("ababc", TRUE),
    L_TESTS_NFA_INPUT("cc", FALSE), L_TESTS_NFA_INPUT("ab", FALSE), L_TESTS_NFA_INPUT("", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_empty_alternative[] = {
    L_TESTS_NFA_INPUT("", TRUE),
    L_TESTS_NFA_INPUT("a", TRUE),
    L_TESTS_NFA_INPUT("aa", FALSE),
};

static const LTestsNfaInput l_tests_nfa_inputs_extreme_bytes[] = {
    L_TESTS_NFA_INPUT("\x00\xFF", TRUE),
    L_TESTS_NFA_INPUT("\x00", FALSE),
    L_TESTS_NFA_INPUT("\xFF\x00", FALSE),
};

static const LTestsNfaTest l_tests_nfa_tests[] = {
    {"", l_tests_nfa_inputs_empty_expression, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_empty_expression), 2},
    {"[]", l_tests_nfa_inputs_empty_class, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_empty_class), 1},
    {"abc", l_tests_nfa_inputs_literal, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_literal), 5},
    {"a*", l_tests_nfa_inputs_star, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_star), 2},
    {"a+b", l_tests_nfa_inputs_plus, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_plus), 4},
    {"(?:a|b)*abb", l_tests_nfa_inputs_suffix_abb, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_suffix_abb), 5},
    {"colou?r", l_tests_nfa_inputs_optional, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_optional), 8},
    {"[0-9]{3}-[0-9]{4}", l_tests_nfa_inputs_phone, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_phone), 0},
    {"a{2,4}", l_tests_nfa_inputs_bounded, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_bounded), 0},
    {"(?:ab)*", l_tests_nfa_inputs_group_star, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_group_star), 0},
    {"\\d+\\.\\d+", l_tests_nfa_inputs_decimal, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_decimal), 0},
    {"[^abc]+", l_tests_nfa_inputs_negated_class, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_negated_class), 0},
    {"a.c", l_tests_nfa_inputs_any_byte, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_any_byte), 0},
    {"a?b?", l_tests_nfa_inputs_two_optionals, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_two_optionals), 0},
    {"(?:a|ab)(?:c|bcd)", l_tests_nfa_inputs_ambiguous_split, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_ambiguous_split), 0},
    {"(?:a|ab)*", l_tests_nfa_inputs_prefix_overlap, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_prefix_overlap), 0},
    {"(?:a*)*b", l_tests_nfa_inputs_nested_star, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_nested_star), 0},
    {"(?:a*b*)*c", l_tests_nfa_inputs_star_of_stars, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_star_of_stars), 0},
    {"(?:)|a", l_tests_nfa_inputs_empty_alternative, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_empty_alternative), 3},
    {"\\x00\\xFF", l_tests_nfa_inputs_extreme_bytes, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_inputs_extreme_bytes), 0},
};

/* Matches source against nfa and logs the outcome; returns 1 if it differs from expected, else 0. */
static uint32 l_tests_nfa_expect(EchoContext *context, LNFA *nfa, const char *label, const char *text, uint_large size, boolean expected) {
  boolean matched;
  boolean as_expected;

  matched     = logos_automatons_nfa_match(nfa, (const byte *)text, size);
  as_expected = matched == expected;

  if (size <= L_TESTS_NFA_INPUT_SHOWN_MAX) {
    (void)echo_log(context, as_expected ? ELL_INFO : ELL_ERROR, "NFA %s /%s/: input=\"%s\" (%llu bytes), matched=%s, expected=%s",
                   as_expected ? "PASS" : "FAIL", label, text, (unsigned long long)size, matched ? "TRUE" : "FALSE", expected ? "TRUE" : "FALSE");
  } else {
    (void)echo_log(context, as_expected ? ELL_INFO : ELL_ERROR, "NFA %s /%s/: input of %llu bytes, matched=%s, expected=%s",
                   as_expected ? "PASS" : "FAIL", label, (unsigned long long)size, matched ? "TRUE" : "FALSE", expected ? "TRUE" : "FALSE");
  }

  return as_expected ? 0 : 1;
}

/* Compiles test->regexp and checks every input; returns the number of failures. A regexp that does not compile fails all its inputs. */
static uint32 l_tests_nfa_test_run(EchoContext *context, const LTestsNfaTest *test) {
  LRegula          *regula;
  LRegulaOperation *root_operation;
  LNFA             *nfa;
  NexusError       *error;
  uint32            failures;
  uint_large        i;

  regula = logos_languages_regula_regula_create(test->regexp, NEXUS_VERSION_PACK(0, 0, 1, 0));
  error  = logos_languages_regula_regula_operation_add_regexp("Root", regula, test->regexp, &root_operation);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)echo_error(context, "NFA FAIL /%s/: the regular expression did not compile: %s", test->regexp, error->message);
    logos_languages_regula_regula_destroy(regula);

    return (uint32)test->input_count;
  }

  nfa      = logos_automatons_nfa_from_regula(regula, context);
  failures = 0;

  for (i = 0; i < test->input_count; i++) {
    failures += l_tests_nfa_expect(context, nfa, test->regexp, test->inputs[i].text, test->inputs[i].size, test->inputs[i].expected);
  }

  logos_automatons_nfa_destroy(nfa);
  logos_languages_regula_regula_destroy(regula);

  return failures;
}

/* a{70} has 71 states, so its statesets span two 64-bit words. */
static uint32 l_tests_nfa_multiword_run(EchoContext *context) {
  static const uint_large lengths[] = {0, 1, 69, 70, 71, 140};

  LRegula          *regula;
  LRegulaOperation *root_operation;
  LNFA             *nfa;
  NexusError       *error;
  char              source[141];
  uint32            failures;
  uint_large        i;

  regula = logos_languages_regula_regula_create("a{70}", NEXUS_VERSION_PACK(0, 0, 1, 0));
  error  = logos_languages_regula_regula_operation_add_regexp("Root", regula, "a{70}", &root_operation);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)echo_error(context, "NFA FAIL /a{70}/: the regular expression did not compile: %s", error->message);
    logos_languages_regula_regula_destroy(regula);

    return (uint32)NEXUS_ARRAY_SIZE_ELEMENTS(lengths);
  }

  nfa      = logos_automatons_nfa_from_regula(regula, context);
  failures = 0;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(lengths); i++) {
    nexus_memory_bytes_set(source, NEXUS_SIZEOF(source), 'a');
    source[lengths[i]] = '\0';

    failures += l_tests_nfa_expect(context, nfa, "a{70}", source, lengths[i], lengths[i] == 70);
  }

  logos_automatons_nfa_destroy(nfa);
  logos_languages_regula_regula_destroy(regula);

  return failures;
}

/* A regula without operations is the empty language: its NFA is the initial state alone and accepts nothing. */
static uint32 l_tests_nfa_empty_regula_run(EchoContext *context) {
  LRegula *regula;
  LNFA    *nfa;
  uint32   failures;

  regula   = logos_languages_regula_regula_create("empty", NEXUS_VERSION_PACK(0, 0, 1, 0));
  nfa      = logos_automatons_nfa_from_regula(regula, context);
  failures = 0;

  failures += l_tests_nfa_expect(context, nfa, "(no operations)", "", 0, FALSE);
  failures += l_tests_nfa_expect(context, nfa, "(no operations)", "a", 1, FALSE);

  logos_automatons_nfa_destroy(nfa);
  logos_languages_regula_regula_destroy(regula);

  return failures;
}

void logos_tests_automatons_nfa_run(LogosRuntime *runtime) {
  EchoContext *context;
  uint32       failures;
  uint_large   i;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "runtime must not be NULL");

  context = logos_runtime_echo_context_get(runtime);

  (void)echo_info(context, "Testing Logos NFAs: %llu regular expressions plus the multiword and empty-language cases.",
                  (unsigned long long)NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_tests));

  failures = 0;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_tests); i++) {
    failures += l_tests_nfa_test_run(context, &l_tests_nfa_tests[i]);
  }

  failures += l_tests_nfa_multiword_run(context);
  failures += l_tests_nfa_empty_regula_run(context);

  if (failures == 0) {
    (void)echo_info(context, "Logos NFA tests: every input matched as expected.");
  } else {
    (void)echo_error(context, "Logos NFA tests: %u input(s) did not match as expected.", (unsigned int)failures);
  }
}

/* ---------------------------------------------------------------------------- */
/* DFA                                                                          */
/* ---------------------------------------------------------------------------- */

/* Checks one input against the DFA built from the NFA and against its minimization; returns the number of failures, 0 to 2. */
static uint32 l_tests_dfa_expect(EchoContext *context, const LDFA *dfa, const LDFA *minimal, const char *label, const char *text, uint_large size,
                                 boolean expected) {
  const LDFA *automatons[2];
  const char *names[2];
  uint32      failures;
  uint_large  i;

  automatons[0] = dfa;
  automatons[1] = minimal;
  names[0]      = "DFA";
  names[1]      = "MIN";
  failures      = 0;

  for (i = 0; i < 2; i++) {
    boolean matched;
    boolean as_expected;

    matched     = logos_automatons_dfa_match(automatons[i], (const byte *)text, size);
    as_expected = matched == expected;

    if (size <= L_TESTS_NFA_INPUT_SHOWN_MAX) {
      (void)echo_log(context, as_expected ? ELL_INFO : ELL_ERROR, "%s %s /%s/: input=\"%s\" (%llu bytes), matched=%s, expected=%s", names[i],
                     as_expected ? "PASS" : "FAIL", label, text, (unsigned long long)size, matched ? "TRUE" : "FALSE", expected ? "TRUE" : "FALSE");
    } else {
      (void)echo_log(context, as_expected ? ELL_INFO : ELL_ERROR, "%s %s /%s/: input of %llu bytes, matched=%s, expected=%s", names[i],
                     as_expected ? "PASS" : "FAIL", label, (unsigned long long)size, matched ? "TRUE" : "FALSE", expected ? "TRUE" : "FALSE");
    }

    failures += as_expected ? 0 : 1;
  }

  return failures;
}

/* The minimal DFA must not be larger than its source, must have the expected size when one is given, and must already be minimal. */
static uint32 l_tests_dfa_sizes_check(EchoContext *context, const LDFA *dfa, const LDFA *minimal, const char *label, uint16 expected_states) {
  LDFA   *again;
  uint32  failures;
  boolean not_larger;
  boolean as_expected;
  boolean idempotent;

  again       = logos_automatons_dfa_minimize(minimal, context);
  not_larger  = logos_automatons_dfa_state_count_get(minimal) <= logos_automatons_dfa_state_count_get(dfa);
  as_expected = expected_states == 0 || logos_automatons_dfa_state_count_get(minimal) == expected_states;
  idempotent  = logos_automatons_dfa_state_count_get(again) == logos_automatons_dfa_state_count_get(minimal);
  failures    = (not_larger ? 0 : 1) + (as_expected ? 0 : 1) + (idempotent ? 0 : 1);

  (void)echo_log(context, failures == 0 ? ELL_INFO : ELL_ERROR, "MIN %s /%s/: %u DFA states, %u minimal (expected %u), minimizing again gives %u.",
                 failures == 0 ? "PASS" : "FAIL", label, (unsigned int)logos_automatons_dfa_state_count_get(dfa),
                 (unsigned int)logos_automatons_dfa_state_count_get(minimal), (unsigned int)expected_states,
                 (unsigned int)logos_automatons_dfa_state_count_get(again));

  logos_automatons_dfa_destroy(again);

  return failures;
}

/* Compiles test->regexp, builds its DFA and minimal DFA, and checks every input on both; returns the number of failures. */
static uint32 l_tests_dfa_test_run(EchoContext *context, const LTestsNfaTest *test) {
  LRegula          *regula;
  LRegulaOperation *root_operation;
  LNFA             *nfa;
  LDFA             *dfa;
  LDFA             *minimal;
  NexusError       *error;
  uint32            failures;
  uint_large        i;

  regula = logos_languages_regula_regula_create(test->regexp, NEXUS_VERSION_PACK(0, 0, 1, 0));
  error  = logos_languages_regula_regula_operation_add_regexp("Root", regula, test->regexp, &root_operation);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)echo_error(context, "DFA FAIL /%s/: the regular expression did not compile: %s", test->regexp, error->message);
    logos_languages_regula_regula_destroy(regula);

    return (uint32)test->input_count;
  }

  nfa = logos_automatons_nfa_from_regula(regula, context);
  dfa = logos_automatons_dfa_from_nfa(nfa, context);

  if (dfa == NULL) {
    (void)echo_error(context, "DFA FAIL /%s/: the DFA was not built.", test->regexp);
    logos_automatons_nfa_destroy(nfa);
    logos_languages_regula_regula_destroy(regula);

    return (uint32)test->input_count;
  }

  minimal  = logos_automatons_dfa_minimize(dfa, context);
  failures = l_tests_dfa_sizes_check(context, dfa, minimal, test->regexp, test->minimal_states);

  for (i = 0; i < test->input_count; i++) {
    failures += l_tests_dfa_expect(context, dfa, minimal, test->regexp, test->inputs[i].text, test->inputs[i].size, test->inputs[i].expected);
  }

  logos_automatons_dfa_destroy(minimal);
  logos_automatons_dfa_destroy(dfa);
  logos_automatons_nfa_destroy(nfa);
  logos_languages_regula_regula_destroy(regula);

  return failures;
}

/* a{70} needs 71 NFA states, so its state sets span two words; its minimal DFA is a chain of 71 states and the dead state. */
static uint32 l_tests_dfa_multiword_run(EchoContext *context) {
  static const uint_large lengths[] = {0, 1, 69, 70, 71, 140};

  LRegula          *regula;
  LRegulaOperation *root_operation;
  LNFA             *nfa;
  LDFA             *dfa;
  LDFA             *minimal;
  NexusError       *error;
  char              source[141];
  uint32            failures;
  uint_large        i;

  regula = logos_languages_regula_regula_create("a{70}", NEXUS_VERSION_PACK(0, 0, 1, 0));
  error  = logos_languages_regula_regula_operation_add_regexp("Root", regula, "a{70}", &root_operation);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)echo_error(context, "DFA FAIL /a{70}/: the regular expression did not compile: %s", error->message);
    logos_languages_regula_regula_destroy(regula);

    return (uint32)NEXUS_ARRAY_SIZE_ELEMENTS(lengths);
  }

  nfa      = logos_automatons_nfa_from_regula(regula, context);
  dfa      = logos_automatons_dfa_from_nfa(nfa, context);
  minimal  = logos_automatons_dfa_minimize(dfa, context);
  failures = l_tests_dfa_sizes_check(context, dfa, minimal, "a{70}", 72);

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(lengths); i++) {
    nexus_memory_bytes_set(source, NEXUS_SIZEOF(source), 'a');
    source[lengths[i]] = '\0';

    failures += l_tests_dfa_expect(context, dfa, minimal, "a{70}", source, lengths[i], lengths[i] == 70);
  }

  logos_automatons_dfa_destroy(minimal);
  logos_automatons_dfa_destroy(dfa);
  logos_automatons_nfa_destroy(nfa);
  logos_languages_regula_regula_destroy(regula);

  return failures;
}

/* A regula without operations is the empty language: its minimal DFA is the dead state alone. */
static uint32 l_tests_dfa_empty_regula_run(EchoContext *context) {
  LRegula *regula;
  LNFA    *nfa;
  LDFA    *dfa;
  LDFA    *minimal;
  uint32   failures;

  regula   = logos_languages_regula_regula_create("empty", NEXUS_VERSION_PACK(0, 0, 1, 0));
  nfa      = logos_automatons_nfa_from_regula(regula, context);
  dfa      = logos_automatons_dfa_from_nfa(nfa, context);
  minimal  = logos_automatons_dfa_minimize(dfa, context);
  failures = l_tests_dfa_sizes_check(context, dfa, minimal, "(no operations)", 1);

  failures += l_tests_dfa_expect(context, dfa, minimal, "(no operations)", "", 0, FALSE);
  failures += l_tests_dfa_expect(context, dfa, minimal, "(no operations)", "a", 1, FALSE);

  logos_automatons_dfa_destroy(minimal);
  logos_automatons_dfa_destroy(dfa);
  logos_automatons_nfa_destroy(nfa);
  logos_languages_regula_regula_destroy(regula);

  return failures;
}

void logos_tests_automatons_dfa_run(LogosRuntime *runtime) {
  EchoContext *context;
  uint32       failures;
  uint_large   i;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "runtime must not be NULL");

  context = logos_runtime_echo_context_get(runtime);

  (void)echo_info(context, "Testing Logos DFAs and their minimization: %llu regular expressions plus the multiword and empty-language cases.",
                  (unsigned long long)NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_tests));

  failures = 0;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_nfa_tests); i++) {
    failures += l_tests_dfa_test_run(context, &l_tests_nfa_tests[i]);
  }

  failures += l_tests_dfa_multiword_run(context);
  failures += l_tests_dfa_empty_regula_run(context);

  if (failures == 0) {
    (void)echo_info(context, "Logos DFA tests: every input matched as expected and every minimal DFA had the expected size.");
  } else {
    (void)echo_error(context, "Logos DFA tests: %u check(s) failed.", (unsigned int)failures);
  }
}
