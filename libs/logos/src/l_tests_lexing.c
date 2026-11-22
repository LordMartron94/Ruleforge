#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "../logos_tests.h"

/* Token kinds of the test lexers. */
enum {
  LT_WS = L_LEXER_TOKEN_KIND_FIRST_USER,
  LT_COMMENT,
  LT_IF,
  LT_ID,
  LT_NUM,
  LT_EQEQ,
  LT_EQ,
  LT_SEMI,
  LT_OTHER,
  LT_WHILE,
  LT_QOPEN,
  LT_BODY,
  LT_ESC,
  LT_QCLOSE,
  LT_STR,
  LT_LP,
  LT_RBRACE,
  LT_TILDE,
  LT_P,
  LT_Q,
  LT_T,
  LT_R,
  LT_U,
  LT_ABC,
  LT_A,
  LT_AB,
  LT_B
};

typedef struct LTestsLexRule {
  const char                  *label;
  const char                  *regexp;
  LLexerTokenKind              kind;
  uint8                        priority;
  LLexerTokenContextActionKind action;
  uint32                       operand; /* A context index for push, set and replace; the amount for pop. */
} LTestsLexRule;

typedef struct LTestsLexContext {
  const char          *label;
  boolean              is_root;
  const LTestsLexRule *rules;
  uint_large           rule_count;
} LTestsLexContext;

/* What a run produced, in order. An error span is an event with its cause. */
typedef struct LTestsLexEvent {
  LLexerToken token;
  LLexerError error;
} LTestsLexEvent;

#define L_TESTS_LEX_EVENTS_MAX 600

typedef struct LTestsLexRun {
  LTestsLexEvent events[L_TESTS_LEX_EVENTS_MAX];
  uint_large     count;
  LLexerContext  final_context;
  uint_large     final_offset;
} LTestsLexRun;

typedef struct LTestsLexExpected {
  LLexerTokenKind kind;
  uint_large      offset;
  uint32          width;
  LLexerError     error;
} LTestsLexExpected;

#define LT_TOKEN(kind, offset, width)        {(kind), (offset), (width), LLE_NONE}
#define LT_ERROR(kind, offset, width, error) {(kind), (offset), (width), (error)}
#define LT_EOF(offset)                       {L_LEXER_TOKEN_KIND_EOF, (offset), 0, LLE_NONE}

#define LT_RULE(label, regexp, kind, priority)                         {(label), (regexp), (kind), (priority), LLTCAK_NONE, 0}
#define LT_RULE_ACTION(label, regexp, kind, priority, action, operand) {(label), (regexp), (kind), (priority), (action), (operand)}

static void l_tests_lexing_check(EchoContext *context, uint32 *failures, boolean ok, const char *name) {
  if (ok) {
    (void)echo_info(context, "Lexer PASS: %s", name);
  } else {
    (void)echo_error(context, "Lexer FAIL: %s", name);
    (*failures)++;
  }
}

/* Builds a lexer from a table. Context i of the table is the context with identifier i. Every language is destroyed once finalize returns. */
static LLexer *l_tests_lexing_build(LogosRuntime *runtime, const LTestsLexContext *contexts, uint_large context_count, uint32 limit,
                                    NexusError **out_error) {
  LLexer    *lexer;
  LRegula  **languages;
  uint_large language_count;
  uint_large capacity;
  uint_large i;
  uint_large j;

  capacity = 0;

  for (i = 0; i < context_count; i++) {
    capacity += contexts[i].rule_count;
  }

  languages      = (LRegula **)nexus_memory_allocate((capacity > 0 ? capacity : 1) * NEXUS_SIZEOF(LRegula *));
  language_count = 0;
  lexer          = logos_language_tooling_lexing_lexer_create(runtime);

  if (limit != 0) {
    logos_language_tooling_lexing_lexer_token_width_limit_set(lexer, limit);
  }

  for (i = 0; i < context_count; i++) {
    (void)logos_language_tooling_lexing_lexer_context_create(lexer, contexts[i].label, contexts[i].is_root);
  }

  for (i = 0; i < context_count; i++) {
    for (j = 0; j < contexts[i].rule_count; j++) {
      const LTestsLexRule *entry;
      LLexerTokenRule      rule;
      LRegulaOperation    *operation;
      NexusError          *error;

      entry                     = &contexts[i].rules[j];
      languages[language_count] = logos_languages_regula_regula_create(entry->label, NEXUS_VERSION_PACK(0, 0, 1, 0));
      error = logos_languages_regula_regula_operation_add_regexp(entry->label, languages[language_count], entry->regexp, &operation);

      NEXUS_PROGRAM_INVARIANT(error == NEXUS_PROGRAM_ERROR_SUCCESS(), "a test regexp must compile");

      rule.label          = entry->label;
      rule.token_language = languages[language_count];
      rule.token_kind     = entry->kind;
      rule.action_kind    = entry->action;
      rule.match_priority = entry->priority;

      rule.action_data.push_context = entry->operand;

      if (entry->action == LLTCAK_POP) {
        rule.action_data.pop_amount = (uint8)entry->operand;
      }

      logos_language_tooling_lexing_lexer_context_token_rule_add(lexer, (LLexerContext)i, rule);
      language_count++;
    }
  }

  *out_error = logos_language_tooling_lexing_lexer_finalize(lexer);

  for (i = 0; i < language_count; i++) {
    logos_languages_regula_regula_destroy(languages[i]);
  }

  nexus_memory_deallocate((void *)languages);

  if (*out_error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    logos_language_tooling_lexing_lexer_destroy(lexer);

    return NULL;
  }

  return lexer;
}

static uint32 l_tests_lexing_random(uint32 *state) {
  *state = (*state * 1664525u) + 1013904223u;

  return *state >> 8;
}

/* Lexes source in chunks of at most max_chunk bytes (random sizes, empty ones included; 0 means a single chunk). Every chunk lives in its own buffer
   that is scribbled over the moment the process has released it, so a lexer that still reads a released chunk fails. */
static void l_tests_lexing_run(LLexer *lexer, const byte *source, uint_large size, uint32 max_chunk, uint32 seed, LTestsLexRun *run) {
  LLexerProcess *process;
  byte          *buffer;
  uint_large     offset;
  boolean        done;

  process    = logos_language_tooling_lexing_lexer_process_create(lexer);
  buffer     = (byte *)nexus_memory_allocate(size + 1);
  offset     = 0;
  done       = FALSE;
  run->count = 0;

  while (!done) {
    uint_large length;
    boolean    last;
    boolean    need_input;

    length = size - offset;

    if (max_chunk != 0) {
      uint_large wanted;

      wanted = l_tests_lexing_random(&seed) % ((uint_large)max_chunk + 1);

      if (wanted < length) {
        length = wanted;
      }
    }

    last = offset + length == size;

    if (length > 0) {
      nexus_memory_bytes_copy(source + offset, buffer, length);
    }

    logos_language_tooling_lexing_lexer_process_chunk_add(process, buffer, length, last);

    need_input = FALSE;

    while (!need_input && !done) {
      LLexerToken  token;
      LLexerStatus status;

      status = logos_language_tooling_lexing_lexer_process_lex(process, &token);

      if (status == LLS_TOKEN || status == LLS_ERROR) {
        LTestsLexEvent *event;

        NEXUS_PROGRAM_INVARIANT(run->count < L_TESTS_LEX_EVENTS_MAX, "a test produced more events than the run holds");

        event        = &run->events[run->count++];
        event->token = token;
        event->error = status == LLS_ERROR ? logos_language_tooling_lexing_lexer_process_error_get(process, NULL) : LLE_NONE;
      } else if (status == LLS_NEED_INPUT) {
        need_input = TRUE;
        nexus_memory_bytes_set(buffer, size + 1, 0xEE);
      } else {
        done = TRUE;
      }
    }

    offset += length;
  }

  run->final_context = logos_language_tooling_lexing_lexer_process_context_get(process);
  run->final_offset  = logos_language_tooling_lexing_lexer_process_offset_get(process);

  nexus_memory_deallocate(buffer);
  logos_language_tooling_lexing_lexer_process_destroy(process);
}

static boolean l_tests_lexing_events_equal(const LTestsLexRun *left, const LTestsLexRun *right) {
  uint_large i;

  if (left->count != right->count) {
    return FALSE;
  }

  for (i = 0; i < left->count; i++) {
    if (left->events[i].token.kind != right->events[i].token.kind || left->events[i].token.lexeme_offset != right->events[i].token.lexeme_offset ||
        left->events[i].token.lexeme_width != right->events[i].token.lexeme_width || left->events[i].error != right->events[i].error) {
      return FALSE;
    }
  }

  return TRUE;
}

static boolean l_tests_lexing_matches(const LTestsLexRun *run, const LTestsLexExpected *expected, uint_large expected_count) {
  uint_large i;

  if (run->count != expected_count) {
    return FALSE;
  }

  for (i = 0; i < expected_count; i++) {
    if (run->events[i].token.kind != expected[i].kind || run->events[i].token.lexeme_offset != expected[i].offset ||
        run->events[i].token.lexeme_width != expected[i].width || run->events[i].error != expected[i].error) {
      return FALSE;
    }
  }

  return TRUE;
}

/* Lossless: the events, in order, tile the source from 0 to its length with no gap and no overlap, and the end token sits at the end. */
static boolean l_tests_lexing_tiles(const LTestsLexRun *run, uint_large size) {
  uint_large cursor;
  uint_large i;

  cursor = 0;

  if (run->count == 0 || run->events[run->count - 1].token.kind != L_LEXER_TOKEN_KIND_EOF || run->events[run->count - 1].error != LLE_NONE) {
    return FALSE;
  }

  for (i = 0; i < run->count; i++) {
    if (run->events[i].token.lexeme_offset != cursor) {
      return FALSE;
    }

    if (run->events[i].token.lexeme_width == 0 && run->events[i].token.kind != L_LEXER_TOKEN_KIND_EOF) {
      return FALSE;
    }

    cursor += run->events[i].token.lexeme_width;
  }

  return cursor == size;
}

/* One source, expected whole; then the same source cut every way (1 byte chunks and random cuts) must produce the identical events. */
static void l_tests_lexing_case(EchoContext *context, uint32 *failures, LLexer *lexer, const char *name, const char *source, uint_large size,
                                const LTestsLexExpected *expected, uint_large expected_count) {
  static LTestsLexRun whole;
  static LTestsLexRun cut;
  uint32              seed;
  boolean             chunked_equal;
  uint_large          max_chunk;

  l_tests_lexing_run(lexer, (const byte *)source, size, 0, 1, &whole);

  l_tests_lexing_check(context, failures, l_tests_lexing_matches(&whole, expected, expected_count), name);
  l_tests_lexing_check(context, failures, l_tests_lexing_tiles(&whole, size), "  tokens and error spans tile the source");

  chunked_equal = TRUE;
  seed          = 7;

  for (max_chunk = 1; max_chunk <= 9; max_chunk++) {
    uint32 attempt;

    for (attempt = 0; attempt < 20; attempt++) {
      l_tests_lexing_run(lexer, (const byte *)source, size, (uint32)max_chunk, seed++, &cut);

      if (!l_tests_lexing_events_equal(&whole, &cut)) {
        chunked_equal = FALSE;
      }
    }
  }

  l_tests_lexing_check(context, failures, chunked_equal, "  identical under 180 random chunkings, empty chunks included");
}

/* ---------------------------------------------------------------------------- */
/* LEXER TABLES                                                                  */
/* ---------------------------------------------------------------------------- */

static const LTestsLexRule l_tests_lexing_rules_basic[] = {
    LT_RULE("whitespace", "[ \\t\\r\\n]+", LT_WS, 0),
    LT_RULE("comment", "//[^\\n]*", LT_COMMENT, 0),
    LT_RULE("if", "if", LT_IF, 1),
    LT_RULE("identifier", "[a-z_][a-z0-9_]*", LT_ID, 0),
    LT_RULE("number", "[0-9]+", LT_NUM, 0),
    LT_RULE("equal-equal", "==", LT_EQEQ, 0),
    LT_RULE("equal", "=", LT_EQ, 0),
    LT_RULE("semicolon", ";", LT_SEMI, 0),
};

static const LTestsLexContext l_tests_lexing_basic[] = {
    {"root", TRUE, l_tests_lexing_rules_basic, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_basic)}};

/* Equal length, equal priority: the earlier rule wins. A higher priority beats an earlier rule. Longest still beats priority. */
static const LTestsLexRule l_tests_lexing_rules_priority[] = {
    LT_RULE("identifier", "[a-z]+", LT_ID, 0),
    LT_RULE("if (shadowed)", "if", LT_IF, 0),
    LT_RULE("other", "[a-z]+", LT_OTHER, 0),
    LT_RULE("while", "while", LT_WHILE, 5),
};

static const LTestsLexContext l_tests_lexing_priority[] = {
    {"root", TRUE, l_tests_lexing_rules_priority, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_priority)}};

static const LTestsLexRule l_tests_lexing_rules_root[] = {
    LT_RULE("whitespace", "[ ]+", LT_WS, 0),
    LT_RULE("identifier", "[a-z]+", LT_ID, 0),
    LT_RULE_ACTION("quote open", "\"", LT_QOPEN, 0, LLTCAK_PUSH, 1),
    LT_RULE_ACTION("left parenthesis", "\\(", LT_LP, 0, LLTCAK_PUSH, 0),
    LT_RULE_ACTION("right brace", "\\}", LT_RBRACE, 0, LLTCAK_POP, 1),
    LT_RULE_ACTION("tilde", "~", LT_TILDE, 0, LLTCAK_REPLACE, 2),
    LT_RULE_ACTION("P", "P", LT_P, 0, LLTCAK_PUSH, 2),
};

static const LTestsLexRule l_tests_lexing_rules_string[] = {
    LT_RULE("body", "[^\"\\\\]+", LT_BODY, 0),
    LT_RULE("escape", "\\\\.", LT_ESC, 0),
    LT_RULE_ACTION("quote close", "\"", LT_QCLOSE, 0, LLTCAK_POP, 1),
};

static const LTestsLexRule l_tests_lexing_rules_a[] = {
    LT_RULE_ACTION("Q", "Q", LT_Q, 0, LLTCAK_REPLACE, 3),
    LT_RULE_ACTION("T", "T", LT_T, 0, LLTCAK_SET, 3),
};

static const LTestsLexRule l_tests_lexing_rules_b[] = {
    LT_RULE_ACTION("R", "R", LT_R, 0, LLTCAK_SET, 0),
    LT_RULE_ACTION("U", "U", LT_U, 0, LLTCAK_POP, 1),
};

static const LTestsLexContext l_tests_lexing_contexts[] = {
    {"root", TRUE, l_tests_lexing_rules_root, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_root)},
    {"string", FALSE, l_tests_lexing_rules_string, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_string)},
    {"a", FALSE, l_tests_lexing_rules_a, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_a)},
    {"b", FALSE, l_tests_lexing_rules_b, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_b)},
};

static const LTestsLexRule l_tests_lexing_rules_string_whole[] = {
    LT_RULE("whitespace", "[ ]+", LT_WS, 0),
    LT_RULE("identifier", "[a-z]+", LT_ID, 0),
    LT_RULE("string", "\"[^\"]*\"", LT_STR, 0),
};

static const LTestsLexContext l_tests_lexing_string_whole[] = {
    {"root", TRUE, l_tests_lexing_rules_string_whole, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_string_whole)}};

static const LTestsLexRule l_tests_lexing_rules_abc[] = {
    LT_RULE("abc", "abc", LT_ABC, 0),
    LT_RULE("whitespace", "[ ]+", LT_WS, 0),
};

static const LTestsLexContext l_tests_lexing_abc[] = {{"root", TRUE, l_tests_lexing_rules_abc, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_abc)}};

/* "a" and "a+b": after "aa" the lexer cannot tell until it sees what follows, and may have to give the extra bytes back. */
static const LTestsLexRule l_tests_lexing_rules_backtrack[] = {
    LT_RULE("a", "a", LT_A, 0),
    LT_RULE("a+b", "a+b", LT_AB, 0),
    LT_RULE("b", "b", LT_B, 0),
    LT_RULE("whitespace", "[ ]+", LT_WS, 0),
    LT_RULE("string", "\"[^\"]*\"", LT_STR, 0),
};

static const LTestsLexContext l_tests_lexing_backtrack[] = {
    {"root", TRUE, l_tests_lexing_rules_backtrack, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_backtrack)}};

static const LTestsLexRule l_tests_lexing_rules_a_star[] = {LT_RULE("a-star", "a*", LT_A, 0)};

static const LTestsLexContext l_tests_lexing_empty_match[] = {
    {"root", TRUE, l_tests_lexing_rules_a_star, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_a_star)}};

static const LTestsLexRule l_tests_lexing_rules_single_a[] = {LT_RULE("a", "a+", LT_A, 0)};

static const LTestsLexContext l_tests_lexing_single_a[] = {
    {"root", TRUE, l_tests_lexing_rules_single_a, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_rules_single_a)}};

/* ---------------------------------------------------------------------------- */
/* TESTS                                                                         */
/* ---------------------------------------------------------------------------- */

static void l_tests_lexing_basic_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  static const char              source_text[]   = "if x==10; // hi\nfoo = iff";
  static const char              source_error[]  = "a $ b";
  static const LTestsLexExpected expected_text[] = {
      LT_TOKEN(LT_IF, 0, 2),   LT_TOKEN(LT_WS, 2, 1),  LT_TOKEN(LT_ID, 3, 1),       LT_TOKEN(LT_EQEQ, 4, 2), LT_TOKEN(LT_NUM, 6, 2),
      LT_TOKEN(LT_SEMI, 8, 1), LT_TOKEN(LT_WS, 9, 1),  LT_TOKEN(LT_COMMENT, 10, 5), LT_TOKEN(LT_WS, 15, 1),  LT_TOKEN(LT_ID, 16, 3),
      LT_TOKEN(LT_WS, 19, 1),  LT_TOKEN(LT_EQ, 20, 1), LT_TOKEN(LT_WS, 21, 1),      LT_TOKEN(LT_ID, 22, 3),  LT_EOF(25),
  };
  static const LTestsLexExpected expected_error[] = {
      LT_TOKEN(LT_ID, 0, 1), LT_TOKEN(LT_WS, 1, 1), LT_ERROR(L_LEXER_TOKEN_KIND_ERROR, 2, 1, LLE_UNRECOGNIZED),
      LT_TOKEN(LT_WS, 3, 1), LT_TOKEN(LT_ID, 4, 1), LT_EOF(5),
  };
  static const char              source_empty[]   = "";
  static const LTestsLexExpected expected_empty[] = {LT_EOF(0)};

  LLexer     *lexer;
  NexusError *error;

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_basic, 1, 0, &error);

  l_tests_lexing_check(context, failures, lexer != NULL, "basic lexer finalizes");

  if (lexer == NULL) {
    return;
  }

  l_tests_lexing_case(context, failures, lexer, "whitespace and comments are tokens; keyword by priority, longest match, == before =", source_text,
                      NEXUS_SIZEOF(source_text) - 1, expected_text, NEXUS_ARRAY_SIZE_ELEMENTS(expected_text));
  l_tests_lexing_case(context, failures, lexer, "an unmatched byte is an error span and lexing continues", source_error,
                      NEXUS_SIZEOF(source_error) - 1, expected_error, NEXUS_ARRAY_SIZE_ELEMENTS(expected_error));
  l_tests_lexing_case(context, failures, lexer, "the empty source is just the end token", source_empty, 0, expected_empty,
                      NEXUS_ARRAY_SIZE_ELEMENTS(expected_empty));

  logos_language_tooling_lexing_lexer_destroy(lexer);
}

/* The three outcomes, with exact statuses: a token, "more input needed", and an error. */
static void l_tests_lexing_status_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LLexer        *lexer;
  LLexerProcess *process;
  LLexerToken    token;
  NexusError    *error;
  boolean        ok;

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_basic, 1, 0, &error);

  if (lexer == NULL) {
    l_tests_lexing_check(context, failures, FALSE, "status tests need the basic lexer");

    return;
  }

  /* "=" then "=": the first chunk cannot decide, because "==" is also a token. */
  process = logos_language_tooling_lexing_lexer_process_create(lexer);
  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)"=", 1, FALSE);
  ok = logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_NEED_INPUT;
  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)"=", 1, TRUE);
  ok = ok && logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_TOKEN && token.kind == LT_EQEQ && token.lexeme_offset == 0 &&
       token.lexeme_width == 2;
  ok = ok && logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_TOKEN && token.kind == L_LEXER_TOKEN_KIND_EOF &&
       token.lexeme_offset == 2;
  ok = ok && logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_DONE;
  l_tests_lexing_check(context, failures, ok,
                       "\"=\" at a chunk end asks for input; the next \"=\" completes \"==\"; the end token comes once, then DONE");

  /* "=" then "x": the "=" is decided only once "x" shows it cannot grow. */
  logos_language_tooling_lexing_lexer_process_reset(process);
  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)"=", 1, FALSE);
  ok = logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_NEED_INPUT;
  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)"x", 1, TRUE);
  ok = ok && logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_TOKEN && token.kind == LT_EQ && token.lexeme_width == 1;
  ok = ok && logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_TOKEN && token.kind == LT_ID && token.lexeme_offset == 1;
  l_tests_lexing_check(context, failures, ok, "\"=\" then \"x\" yields \"=\" and \"x\" with the second chunk's bytes at absolute offset 1");

  /* ";" cannot be extended, so it is decided without waiting for the next byte. */
  logos_language_tooling_lexing_lexer_process_reset(process);
  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)";", 1, FALSE);
  ok = logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_TOKEN && token.kind == LT_SEMI;
  ok = ok && logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_NEED_INPUT;
  l_tests_lexing_check(context, failures, ok, "a token nothing can extend is produced before its chunk ends");

  /* An error's span is consumed and the cause is reported. */
  logos_language_tooling_lexing_lexer_process_reset(process);
  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)"$x", 2, TRUE);
  ok = logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_ERROR && token.kind == L_LEXER_TOKEN_KIND_ERROR &&
       token.lexeme_width == 1;
  ok = ok && logos_language_tooling_lexing_lexer_process_error_get(process, &token) == LLE_UNRECOGNIZED && token.lexeme_offset == 0;
  ok = ok && logos_language_tooling_lexing_lexer_process_offset_get(process) == 1;
  ok = ok && logos_language_tooling_lexing_lexer_process_lex(process, &token) == LLS_TOKEN && token.kind == LT_ID;
  l_tests_lexing_check(context, failures, ok, "an error reports its cause and span, consumes it, and lexing resumes behind it");

  logos_language_tooling_lexing_lexer_process_destroy(process);
  logos_language_tooling_lexing_lexer_destroy(lexer);
}

static void l_tests_lexing_lex_many_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LLexer        *lexer;
  LLexerProcess *process;
  NexusDataList *tokens;
  NexusError    *error;
  boolean        ok;

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_basic, 1, 0, &error);

  if (lexer == NULL) {
    l_tests_lexing_check(context, failures, FALSE, "lex_many tests need the basic lexer");

    return;
  }

  process = logos_language_tooling_lexing_lexer_process_create(lexer);
  tokens  = nexus_data_list_create(NEXUS_SIZEOF(LLexerToken), 4);

  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)"x=1;", 4, TRUE);
  ok = logos_language_tooling_lexing_lexer_process_lex_many(process, tokens, LLEXER_TOKEN_LIMIT_UNBOUNDED) == LLS_DONE;
  ok = ok && nexus_data_list_length_get(tokens) == 5;
  ok = ok && ((const LLexerToken *)nexus_data_list_at_const(tokens, 4))->kind == L_LEXER_TOKEN_KIND_EOF;
  l_tests_lexing_check(context, failures, ok, "lex_many without a limit runs to the end token and DONE");

  logos_language_tooling_lexing_lexer_process_reset(process);
  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)"x=1;", 4, TRUE);
  ok = logos_language_tooling_lexing_lexer_process_lex_many(process, tokens, 2) == LLS_TOKEN && nexus_data_list_length_get(tokens) == 2;
  ok = ok && logos_language_tooling_lexing_lexer_process_lex_many(process, tokens, 2) == LLS_TOKEN && nexus_data_list_length_get(tokens) == 2;
  ok = ok && logos_language_tooling_lexing_lexer_process_lex_many(process, tokens, 2) == LLS_DONE && nexus_data_list_length_get(tokens) == 1;
  l_tests_lexing_check(context, failures, ok, "lex_many with a limit stops at it with LLS_TOKEN, clears the list each call, and finishes with DONE");

  logos_language_tooling_lexing_lexer_process_reset(process);
  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)"x=", 2, FALSE);
  ok = logos_language_tooling_lexing_lexer_process_lex_many(process, tokens, LLEXER_TOKEN_LIMIT_UNBOUNDED) == LLS_NEED_INPUT;
  ok = ok && nexus_data_list_length_get(tokens) == 1;
  l_tests_lexing_check(context, failures, ok, "lex_many stops at NEED_INPUT with the tokens decided so far");

  nexus_data_list_destroy(tokens);
  logos_language_tooling_lexing_lexer_process_destroy(process);
  logos_language_tooling_lexing_lexer_destroy(lexer);
}

static void l_tests_lexing_priority_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  static const LTestsLexExpected expected_if[]     = {LT_TOKEN(LT_ID, 0, 2), LT_EOF(2)};
  static const LTestsLexExpected expected_while[]  = {LT_TOKEN(LT_WHILE, 0, 5), LT_EOF(5)};
  static const LTestsLexExpected expected_whilex[] = {LT_TOKEN(LT_ID, 0, 6), LT_EOF(6)};

  LLexer     *lexer;
  NexusError *error;

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_priority, 1, 0, &error);

  l_tests_lexing_check(context, failures, lexer != NULL, "priority lexer finalizes (a shadowed rule is a warning, not an error)");

  if (lexer == NULL) {
    return;
  }

  l_tests_lexing_case(context, failures, lexer, "equal length, equal priority: the earlier rule wins", "if", 2, expected_if,
                      NEXUS_ARRAY_SIZE_ELEMENTS(expected_if));
  l_tests_lexing_case(context, failures, lexer, "equal length: the higher priority wins over earlier rules", "while", 5, expected_while,
                      NEXUS_ARRAY_SIZE_ELEMENTS(expected_while));
  l_tests_lexing_case(context, failures, lexer, "a longer match beats a higher priority", "whilex", 6, expected_whilex,
                      NEXUS_ARRAY_SIZE_ELEMENTS(expected_whilex));

  logos_language_tooling_lexing_lexer_destroy(lexer);
}

static void l_tests_lexing_context_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  static const LTestsLexExpected expected_string[] = {
      LT_TOKEN(LT_ID, 0, 1),   LT_TOKEN(LT_WS, 1, 1),     LT_TOKEN(LT_QOPEN, 2, 1), LT_TOKEN(LT_BODY, 3, 1), LT_TOKEN(LT_ESC, 4, 2),
      LT_TOKEN(LT_BODY, 6, 1), LT_TOKEN(LT_QCLOSE, 7, 1), LT_TOKEN(LT_WS, 8, 1),    LT_TOKEN(LT_ID, 9, 1),   LT_EOF(10),
  };
  static const LTestsLexExpected expected_underflow[] = {
      LT_TOKEN(LT_ID, 0, 1),
      LT_ERROR(LT_RBRACE, 1, 1, LLE_CONTEXT_UNDERFLOW),
      LT_TOKEN(LT_ID, 2, 1),
      LT_EOF(3),
  };
  static const LTestsLexExpected expected_replace_root[] = {
      LT_ERROR(LT_TILDE, 0, 1, LLE_CONTEXT_UNDERFLOW),
      LT_EOF(1),
  };

  static LTestsLexRun run;
  LLexer             *lexer;
  NexusError         *error;
  boolean             ok;
  byte                parentheses[256];

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_contexts, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_lexing_contexts), 0, &error);

  l_tests_lexing_check(context, failures, lexer != NULL, "context lexer finalizes");

  if (lexer == NULL) {
    return;
  }

  l_tests_lexing_case(context, failures, lexer, "push and pop: a string context lexes its own tokens, the pop returns to the root", "a \"b\\\"c\" d",
                      10, expected_string, NEXUS_ARRAY_SIZE_ELEMENTS(expected_string));
  l_tests_lexing_case(context, failures, lexer, "pop at the root fails: the token is reported with the error and the stack is unchanged", "a}b", 3,
                      expected_underflow, NEXUS_ARRAY_SIZE_ELEMENTS(expected_underflow));
  l_tests_lexing_case(context, failures, lexer, "replace at the root fails the same way", "~", 1, expected_replace_root,
                      NEXUS_ARRAY_SIZE_ELEMENTS(expected_replace_root));

  /* The stack after each action. Context 2 is "a", 3 is "b". */
  l_tests_lexing_run(lexer, (const byte *)"P", 1, 0, 1, &run);
  ok = run.final_context == 2;
  l_tests_lexing_run(lexer, (const byte *)"PQ", 2, 0, 1, &run);
  ok = ok && run.final_context == 3; /* replace: [root, a] -> [root, b] */
  l_tests_lexing_run(lexer, (const byte *)"PT", 2, 0, 1, &run);
  ok = ok && run.final_context == 3; /* set: [root, a] -> [root, b] */
  l_tests_lexing_run(lexer, (const byte *)"PTR", 3, 0, 1, &run);
  ok = ok && run.final_context == 0; /* set to the root: [root] */
  l_tests_lexing_run(lexer, (const byte *)"PTU", 3, 0, 1, &run);
  ok = ok && run.final_context == 0; /* pop 1: [root, b] -> [root] */
  l_tests_lexing_check(context, failures, ok, "push, replace, set (to a context and to the root) and pop leave the expected top of the stack");

  /* The root counts toward the depth limit: 255 pushes fit, the 256th is an overflow, and it does not change the stack. */
  nexus_memory_bytes_set(parentheses, NEXUS_SIZEOF(parentheses), '(');
  l_tests_lexing_run(lexer, parentheses, 256, 0, 1, &run);
  ok = run.count == 257 && run.events[254].error == LLE_NONE && run.events[255].error == LLE_CONTEXT_OVERFLOW &&
       run.events[255].token.kind == LT_LP && run.events[255].token.lexeme_offset == 255;
  l_tests_lexing_check(context, failures, ok, "the 256th stacked context overflows; the matched token is still reported");

  logos_language_tooling_lexing_lexer_destroy(lexer);
}

static void l_tests_lexing_errors_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  static const LTestsLexExpected expected_unterminated[] = {
      LT_TOKEN(LT_ID, 0, 1),
      LT_TOKEN(LT_WS, 1, 1),
      LT_ERROR(L_LEXER_TOKEN_KIND_ERROR, 2, 4, LLE_UNRECOGNIZED),
      LT_EOF(6),
  };
  static const LTestsLexExpected expected_prefix[] = {
      LT_ERROR(L_LEXER_TOKEN_KIND_ERROR, 0, 2, LLE_UNRECOGNIZED),
      LT_ERROR(L_LEXER_TOKEN_KIND_ERROR, 2, 1, LLE_UNRECOGNIZED),
      LT_TOKEN(LT_ABC, 3, 3),
      LT_EOF(6),
  };
  static const LTestsLexExpected expected_backtrack[] = {
      LT_TOKEN(LT_A, 0, 1),  LT_TOKEN(LT_A, 1, 1),  LT_ERROR(L_LEXER_TOKEN_KIND_ERROR, 2, 1, LLE_UNRECOGNIZED),
      LT_TOKEN(LT_WS, 3, 1), LT_TOKEN(LT_AB, 4, 4), LT_EOF(8),
  };
  static const LTestsLexExpected expected_too_long[] = {
      LT_ERROR(L_LEXER_TOKEN_KIND_ERROR, 0, 4, LLE_TOKEN_TOO_LONG),
      LT_ERROR(L_LEXER_TOKEN_KIND_ERROR, 4, 4, LLE_TOKEN_TOO_LONG),
      LT_TOKEN(LT_A, 8, 2),
      LT_EOF(10),
  };

  LLexer     *lexer;
  NexusError *error;

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_string_whole, 1, 0, &error);

  if (lexer != NULL) {
    l_tests_lexing_case(context, failures, lexer, "an unterminated string is an error span over the whole viable prefix, not a byte-by-byte retry",
                        "x \"abc", 6, expected_unterminated, NEXUS_ARRAY_SIZE_ELEMENTS(expected_unterminated));
    logos_language_tooling_lexing_lexer_destroy(lexer);
  } else {
    l_tests_lexing_check(context, failures, FALSE, "string lexer finalizes");
  }

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_abc, 1, 0, &error);

  if (lexer != NULL) {
    l_tests_lexing_case(context, failures, lexer, "no accepting prefix: the error covers the viable prefix, then the killing byte restarts the scan",
                        "abxabc", 6, expected_prefix, NEXUS_ARRAY_SIZE_ELEMENTS(expected_prefix));
    logos_language_tooling_lexing_lexer_destroy(lexer);
  } else {
    l_tests_lexing_check(context, failures, FALSE, "abc lexer finalizes");
  }

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_backtrack, 1, 0, &error);

  if (lexer != NULL) {
    l_tests_lexing_case(context, failures, lexer, "an abandoned longer match gives its bytes back to the next token", "aac aaab", 8,
                        expected_backtrack, NEXUS_ARRAY_SIZE_ELEMENTS(expected_backtrack));
    logos_language_tooling_lexing_lexer_destroy(lexer);
  } else {
    l_tests_lexing_check(context, failures, FALSE, "backtracking lexer finalizes");
  }

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_single_a, 1, 4, &error);

  if (lexer != NULL) {
    l_tests_lexing_case(context, failures, lexer, "a token still undecided after the width limit is an error span of limit bytes", "aaaaaaaaaa", 10,
                        expected_too_long, NEXUS_ARRAY_SIZE_ELEMENTS(expected_too_long));
    logos_language_tooling_lexing_lexer_destroy(lexer);
  } else {
    l_tests_lexing_check(context, failures, FALSE, "limited lexer finalizes");
  }
}

static void l_tests_lexing_compile_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LLexer     *lexer;
  NexusError *error;

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_empty_match, 1, 0, &error);

  l_tests_lexing_check(context, failures, lexer == NULL && error->code == GLOBAL_LOGOS_LEXER_RULE_EMPTY_MATCH,
                       "a rule that accepts the empty string is rejected by finalize");

  if (lexer != NULL) {
    logos_language_tooling_lexing_lexer_destroy(lexer);
  }
}

/* Random sources over a small alphabet, lexed whole and then cut at random places: the events must be identical, and always tile the source. This
   is the streaming contract: where the source is cut never changes what is lexed, including through backtracking, unterminated strings and the
   width limit. */
static void l_tests_lexing_fuzz_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures, uint32 limit, const char *name) {
  static const char   alphabet[] = "aab \"x";
  static LTestsLexRun whole;
  static LTestsLexRun cut;

  LLexer     *lexer;
  NexusError *error;
  byte        source[96];
  uint32      state;
  uint32      round;
  uint32      mismatches;
  uint32      tiling_failures;

  lexer = l_tests_lexing_build(runtime, l_tests_lexing_backtrack, 1, limit, &error);

  if (lexer == NULL) {
    l_tests_lexing_check(context, failures, FALSE, name);

    return;
  }

  state           = 12345;
  mismatches      = 0;
  tiling_failures = 0;

  for (round = 0; round < 400; round++) {
    uint_large size;
    uint_large i;
    uint32     attempt;

    size = l_tests_lexing_random(&state) % NEXUS_SIZEOF(source);

    for (i = 0; i < size; i++) {
      source[i] = (byte)alphabet[l_tests_lexing_random(&state) % (NEXUS_SIZEOF(alphabet) - 1)];
    }

    l_tests_lexing_run(lexer, source, size, 0, 1, &whole);

    if (!l_tests_lexing_tiles(&whole, size)) {
      tiling_failures++;
    }

    for (attempt = 0; attempt < 8; attempt++) {
      l_tests_lexing_run(lexer, source, size, 1 + (attempt % 6), l_tests_lexing_random(&state), &cut);

      if (!l_tests_lexing_events_equal(&whole, &cut)) {
        mismatches++;
      }
    }
  }

  if (mismatches != 0 || tiling_failures != 0) {
    (void)echo_error(context, "Lexer fuzz (%s): %u chunking mismatches, %u tiling failures.", name, (unsigned int)mismatches,
                     (unsigned int)tiling_failures);
  }

  l_tests_lexing_check(context, failures, mismatches == 0 && tiling_failures == 0, name);

  logos_language_tooling_lexing_lexer_destroy(lexer);
}

typedef struct LTestsPosition {
  uint_large offset;
  uint_large line;
  uint_large column_byte;
  uint_large column_character;
  uint_large column_visual;
} LTestsPosition;

static boolean l_tests_lexing_position_is(const LLexerSourceIndex *index, const LTestsPosition *expected) {
  LLexerSourcePosition position;

  logos_language_tooling_lexing_source_index_position_get(index, expected->offset, &position);

  return position.line == expected->line && position.column_byte == expected->column_byte &&
         position.column_character == expected->column_character && position.column_visual == expected->column_visual;
}

static boolean l_tests_lexing_positions_are(EchoContext *context, const LLexerSourceIndex *index, const LTestsPosition *expected, uint_large count) {
  uint_large i;
  boolean    ok;

  ok = TRUE;

  for (i = 0; i < count; i++) {
    if (!l_tests_lexing_position_is(index, &expected[i])) {
      (void)echo_error(context, "Lexer source index: offset %llu has the wrong position.", (unsigned long long)expected[i].offset);
      ok = FALSE;
    }
  }

  return ok;
}

/* Feeds text to a fresh index of the given tab size in chunks of chunk bytes (0: all at once). */
static LLexerSourceIndex *l_tests_lexing_index_build(const byte *text, uint_large size, uint8 tab_size, uint_large chunk) {
  LLexerSourceIndex *index;
  uint_large         offset;

  index  = logos_language_tooling_lexing_source_index_create(tab_size);
  offset = 0;

  while (offset < size) {
    uint_large length;

    length = chunk == 0 || chunk > size - offset ? size - offset : chunk;
    logos_language_tooling_lexing_source_index_chunk_add(index, text + offset, length);
    offset += length;
  }

  return index;
}

static boolean l_tests_lexing_indexes_agree(const LLexerSourceIndex *left, const LLexerSourceIndex *right) {
  uint_large length;
  uint_large offset;

  length = logos_language_tooling_lexing_source_index_length_get(left);

  if (length != logos_language_tooling_lexing_source_index_length_get(right) ||
      logos_language_tooling_lexing_source_index_line_count_get(left) != logos_language_tooling_lexing_source_index_line_count_get(right)) {
    return FALSE;
  }

  for (offset = 0; offset <= length; offset++) {
    LLexerSourcePosition a;
    LLexerSourcePosition b;

    logos_language_tooling_lexing_source_index_position_get(left, offset, &a);
    logos_language_tooling_lexing_source_index_position_get(right, offset, &b);

    if (a.line != b.line || a.column_byte != b.column_byte || a.column_character != b.column_character || a.column_visual != b.column_visual) {
      return FALSE;
    }
  }

  return TRUE;
}

static void l_tests_lexing_source_index_run(EchoContext *context, uint32 *failures) {
  /* Line 0 "a<tab>b", line 1 "é x" with a CRLF, line 2 "€" "😀" "z", line 3 an invalid byte and "q" ended by a lone CR, line 4 "last". */
  static const byte           text[]     = {'a',  '\t', 'b',  '\n', 0xC3, 0xA9, 'x', '\r', '\n', 0xE2, 0x82, 0xAC, 0xF0,
                                            0x9F, 0x98, 0x80, 'z',  '\n', 0xFF, 'q', '\r', 'l',  'a',  's',  't'};
  static const LTestsPosition expected[] = {
      /* offset line byte char visual */
      {0, 0, 0, 0, 0},  {1, 0, 1, 1, 1},  {2, 0, 2, 2, 4},  {3, 0, 3, 3, 5}, /* a, the tab, b after a tab stop, the LF */
      {4, 1, 0, 0, 0},  {5, 1, 1, 0, 0},  {6, 1, 2, 1, 1},                   /* é; inside é reads as its start; x */
      {7, 1, 3, 2, 2},  {8, 1, 4, 3, 3},  {9, 2, 0, 0, 0},                   /* the CR, the LF of the CRLF on its own line, € */
      {12, 2, 3, 1, 1}, {16, 2, 7, 2, 2}, {17, 2, 8, 3, 3},                  /* the emoji is one character; z; the LF */
      {18, 3, 0, 0, 0}, {19, 3, 1, 1, 1}, {20, 3, 2, 2, 2},                  /* an invalid byte is one column; q; the lone CR */
      {21, 4, 0, 0, 0}, {25, 4, 4, 4, 4},                                    /* last; the end of the source */
  };
  static const uint_large round_trip_offsets[] = {0, 1, 2, 3, 4, 6, 7, 9, 12, 16, 17, 18, 19, 20, 21, 25};
  static const byte       bom_text[]           = {0xEF, 0xBB, 0xBF, 'a', 'b'};
  static const byte       broken_text[]        = {0xE2, 0x82, 'a', 0xC0, 0x80, 0xED, 0xA0, 0x80, 'b'}; /* Cut off, overlong, surrogate. */

  LLexerSourceIndex   *index;
  LLexerSourceIndex   *pieces;
  LLexerSourcePosition position;
  LLexerSourceSpan     span;
  LLexerToken          token;
  uint_large           start;
  uint_large           end;
  uint_large           i;
  uint_large           size;
  boolean              ok;

  size  = NEXUS_SIZEOF(text);
  index = l_tests_lexing_index_build(text, size, 4, 0);

  ok = logos_language_tooling_lexing_source_index_length_get(index) == size && logos_language_tooling_lexing_source_index_line_count_get(index) == 5;
  ok = ok && l_tests_lexing_positions_are(context, index, expected, NEXUS_ARRAY_SIZE_ELEMENTS(expected));
  l_tests_lexing_check(context, failures, ok, "source index: byte, character and tab-expanded columns; LF, CRLF and CR breaks; UTF-8; invalid bytes");

  /* Any cut of the source gives the same index, one byte at a time included, so a UTF-8 sequence or a CRLF split between chunks changes nothing. */
  ok = TRUE;

  for (i = 1; i <= 5; i++) {
    pieces = l_tests_lexing_index_build(text, size, 4, i);
    ok     = ok && l_tests_lexing_indexes_agree(index, pieces);
    logos_language_tooling_lexing_source_index_destroy(pieces);
  }

  l_tests_lexing_check(context, failures, ok, "source index: identical whatever the chunking, at every offset");

  ok = TRUE;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(round_trip_offsets); i++) {
    logos_language_tooling_lexing_source_index_position_get(index, round_trip_offsets[i], &position);
    ok = ok && logos_language_tooling_lexing_source_index_offset_get(index, position.line, position.column_character) == round_trip_offsets[i];
  }

  /* A column past the text stops at the end of the text, never inside the break. */
  ok = ok && logos_language_tooling_lexing_source_index_offset_get(index, 1, 99) == 7;
  ok = ok && logos_language_tooling_lexing_source_index_offset_get(index, 4, 99) == 25;
  logos_language_tooling_lexing_source_index_line_range_get(index, 1, &start, &end);
  ok = ok && start == 4 && end == 7;
  logos_language_tooling_lexing_source_index_line_range_get(index, 4, &start, &end);
  ok = ok && start == 21 && end == 25;
  l_tests_lexing_check(context, failures, ok, "source index: offset_get inverts position_get, clamps to the text, and line ranges exclude the break");

  token.kind          = LT_ID;
  token.lexeme_offset = 4;
  token.lexeme_width  = 5;
  logos_language_tooling_lexing_source_index_span_get(index, token, &span);
  ok = span.start.line == 1 && span.start.column_character == 0 && span.end.line == 2 && span.end.column_character == 0; /* Half-open. */
  token.lexeme_offset = 25;
  token.lexeme_width  = 0;
  logos_language_tooling_lexing_source_index_span_get(index, token, &span);
  ok = ok && span.start.line == 4 && span.start.column_visual == 4 && span.end.line == 4 && span.end.column_visual == 4; /* Empty. */
  l_tests_lexing_check(context, failures, ok, "source index: spans are half-open, and empty at the end");

  logos_language_tooling_lexing_source_index_destroy(index);

  /* The tab size moves tab stops: with 8, "a<tab>b" puts b at visual column 8. */
  index = l_tests_lexing_index_build(text, size, 8, 0);
  logos_language_tooling_lexing_source_index_position_get(index, 2, &position);
  ok = position.column_visual == 8 && position.column_character == 2;
  logos_language_tooling_lexing_source_index_destroy(index);
  l_tests_lexing_check(context, failures, ok, "source index: the tab size sets the tab stops");

  /* A byte order mark takes no column; invalid sequences (cut off, overlong, surrogate) take one column per byte. */
  index = l_tests_lexing_index_build(bom_text, NEXUS_SIZEOF(bom_text), 4, 0);
  logos_language_tooling_lexing_source_index_position_get(index, 3, &position);
  ok = position.column_byte == 3 && position.column_character == 0 && position.column_visual == 0;
  logos_language_tooling_lexing_source_index_position_get(index, 5, &position);
  ok     = ok && position.column_byte == 5 && position.column_character == 2 && position.column_visual == 2;
  ok     = ok && logos_language_tooling_lexing_source_index_offset_get(index, 0, 0) == 3;
  pieces = l_tests_lexing_index_build(bom_text, NEXUS_SIZEOF(bom_text), 4, 1);
  ok     = ok && l_tests_lexing_indexes_agree(index, pieces);
  logos_language_tooling_lexing_source_index_destroy(pieces);
  logos_language_tooling_lexing_source_index_destroy(index);

  index = l_tests_lexing_index_build(broken_text, NEXUS_SIZEOF(broken_text), 4, 0);
  logos_language_tooling_lexing_source_index_position_get(index, NEXUS_SIZEOF(broken_text), &position);
  ok     = ok && position.column_character == NEXUS_SIZEOF(broken_text);
  pieces = l_tests_lexing_index_build(broken_text, NEXUS_SIZEOF(broken_text), 4, 1);
  ok     = ok && l_tests_lexing_indexes_agree(index, pieces);
  logos_language_tooling_lexing_source_index_destroy(pieces);
  logos_language_tooling_lexing_source_index_destroy(index);
  l_tests_lexing_check(context, failures, ok, "source index: the byte order mark is no column; invalid UTF-8 is one column per byte");
}

void logos_tests_lexing_run(LogosRuntime *runtime) {
  EchoContext *context;
  uint32       failures;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "runtime must not be NULL");

  context  = logos_runtime_echo_context_get(runtime);
  failures = 0;

  (void)echo_info(context, "Testing the Logos lexer: matching, streaming, context actions, errors, limits and source locations.");

  l_tests_lexing_basic_run(runtime, context, &failures);
  l_tests_lexing_status_run(runtime, context, &failures);
  l_tests_lexing_lex_many_run(runtime, context, &failures);
  l_tests_lexing_priority_run(runtime, context, &failures);
  l_tests_lexing_context_run(runtime, context, &failures);
  l_tests_lexing_errors_run(runtime, context, &failures);
  l_tests_lexing_compile_run(runtime, context, &failures);
  l_tests_lexing_fuzz_run(runtime, context, &failures, 0, "fuzz: random chunking never changes the tokens");
  l_tests_lexing_fuzz_run(runtime, context, &failures, 5, "fuzz with a width limit of 5: random chunking never changes the tokens");
  l_tests_lexing_source_index_run(context, &failures);

  if (failures == 0) {
    (void)echo_info(context, "Logos lexer tests: every check passed.");
  } else {
    (void)echo_error(context, "Logos lexer tests: %u check(s) failed.", (unsigned int)failures);
  }
}
