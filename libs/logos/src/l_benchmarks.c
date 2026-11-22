#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "../logos_benchmarks.h"

/* ---------------------------------------------------------------------------- */
/* LANGUAGE AND WORKLOAD                                                         */
/* ---------------------------------------------------------------------------- */

enum {
  BT_WS = L_LEXER_TOKEN_KIND_FIRST_USER,
  BT_LET,
  BT_NUM,
  BT_ID,
  BT_PLUS,
  BT_MINUS,
  BT_STAR,
  BT_SLASH,
  BT_LP,
  BT_RP,
  BT_COMMA,
  BT_SEMI,
  BT_ASSIGN,
  BT_KEYWORD_FIRST
};

typedef struct LBenchmarksRule {
  const char     *label;
  const char     *regexp;
  LLexerTokenKind kind;
  uint8           priority;
} LBenchmarksRule;

static const LBenchmarksRule l_benchmarks_rules[] = {
    {"whitespace", "[ \\t\\n]+", BT_WS, 0},
    {"let", "let", BT_LET, 1},
    {"number", "[0-9]+", BT_NUM, 0},
    {"identifier", "[a-z_][a-z0-9_]*", BT_ID, 0},
    {"plus", "\\+", BT_PLUS, 0},
    {"minus", "-", BT_MINUS, 0},
    {"star", "\\*", BT_STAR, 0},
    {"slash", "/", BT_SLASH, 0},
    {"lparen", "\\(", BT_LP, 0},
    {"rparen", "\\)", BT_RP, 0},
    {"comma", ",", BT_COMMA, 0},
    {"semicolon", ";", BT_SEMI, 0},
    {"assign", "=", BT_ASSIGN, 0},
};

#define BT_NONE LCONTEXTA_PRATT_TOKEN_NONE

static const LContextaPrattEntry l_benchmarks_table[] = {
    {LCPK_ATOM, "number", BT_NUM, BT_NONE, BT_NONE, 0, FALSE, FALSE},      {LCPK_ATOM, "name", BT_ID, BT_NONE, BT_NONE, 0, FALSE, FALSE},
    {LCPK_PREFIX, "neg", BT_MINUS, BT_NONE, BT_NONE, 5, FALSE, FALSE},     {LCPK_INFIX_LEFT, "add", BT_PLUS, BT_NONE, BT_NONE, 3, FALSE, FALSE},
    {LCPK_INFIX_LEFT, "sub", BT_MINUS, BT_NONE, BT_NONE, 3, FALSE, FALSE}, {LCPK_INFIX_LEFT, "mul", BT_STAR, BT_NONE, BT_NONE, 4, FALSE, FALSE},
    {LCPK_INFIX_LEFT, "div", BT_SLASH, BT_NONE, BT_NONE, 4, FALSE, FALSE}, {LCPK_ENCLOSED, "group", BT_LP, BT_RP, BT_NONE, 0, FALSE, FALSE},
    {LCPK_APPLICATION, "call", BT_LP, BT_RP, BT_COMMA, 8, TRUE, FALSE},
};

static uint32 l_benchmarks_random(uint32 *state) {
  uint32 value;

  value = *state;
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  *state = value;

  return value;
}

typedef struct LBenchmarksBuffer {
  char      *data;
  uint_large size;
  uint_large capacity;
} LBenchmarksBuffer;

static void l_benchmarks_append(LBenchmarksBuffer *buffer, const char *text) {
  uint_large length;

  length = nexus_strings_string_length_get(text);

  if (buffer->size + length > buffer->capacity) {
    buffer->capacity = (buffer->capacity * 2) + length;
    buffer->data     = (char *)nexus_memory_reallocate(buffer->data, buffer->capacity);
  }

  nexus_memory_bytes_copy(text, buffer->data + buffer->size, length);
  buffer->size += length;
}

/* One operand: an identifier, a number, a parenthesized sum, or a call. Depth-limited, so the text stays a realistic statement. */
static void l_benchmarks_operand(LBenchmarksBuffer *buffer, uint32 *state, uint32 depth) {
  static const char *const names[]   = {"alpha", "beta", "gamma", "delta", "x", "y", "count_1", "total", "index", "value"};
  static const char *const numbers[] = {"0", "1", "42", "1000", "3", "17"};
  uint32                   choice;

  choice = l_benchmarks_random(state) % (depth < 2 ? 8 : 4);

  if (choice < 2) {
    l_benchmarks_append(buffer, names[l_benchmarks_random(state) % 10]);
  } else if (choice < 4) {
    l_benchmarks_append(buffer, numbers[l_benchmarks_random(state) % 6]);
  } else if (choice < 6) {
    l_benchmarks_append(buffer, "(");
    l_benchmarks_operand(buffer, state, depth + 1);
    l_benchmarks_append(buffer, " + ");
    l_benchmarks_operand(buffer, state, depth + 1);
    l_benchmarks_append(buffer, ")");
  } else {
    l_benchmarks_append(buffer, "fn(");
    l_benchmarks_operand(buffer, state, depth + 1);
    l_benchmarks_append(buffer, ", ");
    l_benchmarks_operand(buffer, state, depth + 1);
    l_benchmarks_append(buffer, ")");
  }
}

/* Statements "let name = a op b op c ;" and "a op b ;" with spaces, one per line, until at least target bytes exist. */
static void l_benchmarks_source_build(LBenchmarksBuffer *buffer, uint_large target) {
  static const char *const operators[] = {" + ", " - ", " * ", " / "};
  uint32                   state;
  uint32                   terms;
  uint32                   i;

  state            = 12345;
  buffer->capacity = target + 4096;
  buffer->data     = (char *)nexus_memory_allocate(buffer->capacity);
  buffer->size     = 0;

  while (buffer->size < target) {
    if (l_benchmarks_random(&state) % 2 == 0) {
      l_benchmarks_append(buffer, "let result_");
      l_benchmarks_append(buffer, l_benchmarks_random(&state) % 2 == 0 ? "a" : "b");
      l_benchmarks_append(buffer, " = ");
    }

    terms = 2 + (l_benchmarks_random(&state) % 4);

    for (i = 0; i < terms; i++) {
      if (i > 0) {
        l_benchmarks_append(buffer, operators[l_benchmarks_random(&state) % 4]);
      }

      l_benchmarks_operand(buffer, &state, 0);
    }

    l_benchmarks_append(buffer, ";\n");
  }
}

/* ---------------------------------------------------------------------------- */
/* FIXTURES                                                                      */
/* ---------------------------------------------------------------------------- */

static LLexer *l_benchmarks_lexer_build(LogosRuntime *runtime, uint32 extra_keywords) {
  LLexer    *lexer;
  LRegula  **languages;
  uint_large count;
  uint_large i;
  char       regexp[32];

  count     = NEXUS_ARRAY_SIZE_ELEMENTS(l_benchmarks_rules) + extra_keywords;
  languages = (LRegula **)nexus_memory_allocate(count * NEXUS_SIZEOF(*languages));
  lexer     = logos_language_tooling_lexing_lexer_create(runtime);
  (void)logos_language_tooling_lexing_lexer_context_create(lexer, "root", TRUE);

  for (i = 0; i < count; i++) {
    LLexerTokenRule   rule;
    LRegulaOperation *operation;
    NexusError       *error;
    const char       *label;
    const char       *text;

    if (i < NEXUS_ARRAY_SIZE_ELEMENTS(l_benchmarks_rules)) {
      label = l_benchmarks_rules[i].label;
      text  = l_benchmarks_rules[i].regexp;
    } else {
      /* kw0000..: literal keywords that all share the identifier alphabet and prefix. */
      regexp[0] = 'k';
      regexp[1] = 'w';
      regexp[2] = (char)('a' + (char)(((i - NEXUS_ARRAY_SIZE_ELEMENTS(l_benchmarks_rules)) / 676) % 26));
      regexp[3] = (char)('a' + (char)(((i - NEXUS_ARRAY_SIZE_ELEMENTS(l_benchmarks_rules)) / 26) % 26));
      regexp[4] = (char)('a' + (char)((i - NEXUS_ARRAY_SIZE_ELEMENTS(l_benchmarks_rules)) % 26));
      regexp[5] = '\0';
      label     = "keyword";
      text      = regexp;
    }

    languages[i] = logos_languages_regula_regula_create(label, NEXUS_VERSION_PACK(0, 0, 1, 0));
    error        = logos_languages_regula_regula_operation_add_regexp(label, languages[i], text, &operation);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(error);
    }

    rule.label          = label;
    rule.token_language = languages[i];
    rule.token_kind     = i < NEXUS_ARRAY_SIZE_ELEMENTS(l_benchmarks_rules) ? l_benchmarks_rules[i].kind : BT_KEYWORD_FIRST + (LLexerTokenKind)i;
    rule.action_kind    = LLTCAK_NONE;
    rule.match_priority = i < NEXUS_ARRAY_SIZE_ELEMENTS(l_benchmarks_rules) ? l_benchmarks_rules[i].priority : 2;
    nexus_memory_bytes_clear(&rule.action_data, NEXUS_SIZEOF(rule.action_data));
    logos_language_tooling_lexing_lexer_context_token_rule_add(lexer, 0, rule);
  }

  {
    NexusError *finalize_error;

    finalize_error = logos_language_tooling_lexing_lexer_finalize(lexer);

    if (finalize_error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(finalize_error);
    }
  }

  for (i = 0; i < count; i++) {
    logos_languages_regula_regula_destroy(languages[i]);
  }

  nexus_memory_deallocate((void *)languages);

  return lexer;
}

static LContextaOperation *l_benchmarks_token(LContexta *contexta, LLexerTokenKind kind) {
  return logos_languages_contexta_contexta_operation_add_token(NULL, contexta, kind);
}

static LContextaOperation *l_benchmarks_concat(LContexta *contexta, LContextaOperation *left, LContextaOperation *right) {
  return logos_languages_contexta_contexta_operation_add_concatenate(NULL, contexta, left, right);
}

/*
Mixed: Program = Statement*, Statement = 'let' name '=' Expression ';' | Expression ';', Expression a Pratt operation.
Earley: Program = Statement*, Statement = 'let' name '=' name ('+' name)? ';' with tokens only, no Pratt.
*/
static LContexta *l_benchmarks_grammar_build(boolean pratt) {
  LContexta          *contexta;
  LContextaRule      *expression_rule;
  LContextaRule      *statement_rule;
  LContextaRule      *program_rule;
  LContextaOperation *expression;
  LContextaOperation *let_statement;
  LContextaOperation *tail;

  contexta       = logos_languages_contexta_contexta_create("bench", NEXUS_VERSION_PACK(0, 0, 1, 0));
  statement_rule = logos_languages_contexta_contexta_rule_add("Statement", contexta);
  program_rule   = logos_languages_contexta_contexta_rule_add("Program", contexta);

  if (pratt) {
    expression_rule = logos_languages_contexta_contexta_rule_add("Expression", contexta);
    expression =
        logos_languages_contexta_contexta_operation_add_pratt(NULL, contexta, l_benchmarks_table, NEXUS_ARRAY_SIZE_ELEMENTS(l_benchmarks_table));
    logos_languages_contexta_contexta_rule_define(contexta, expression_rule, expression);

    tail          = l_benchmarks_concat(contexta, logos_languages_contexta_contexta_operation_add_reference(NULL, contexta, expression_rule),
                                        l_benchmarks_token(contexta, BT_SEMI));
    let_statement = l_benchmarks_concat(contexta, l_benchmarks_token(contexta, BT_LET),
                                        l_benchmarks_concat(contexta, l_benchmarks_token(contexta, BT_ID),
                                                            l_benchmarks_concat(contexta, l_benchmarks_token(contexta, BT_ASSIGN), tail)));
    logos_languages_contexta_contexta_rule_define(
        contexta, statement_rule,
        logos_languages_contexta_contexta_operation_add_unionize(
            NULL, contexta, let_statement,
            l_benchmarks_concat(contexta, logos_languages_contexta_contexta_operation_add_reference(NULL, contexta, expression_rule),
                                l_benchmarks_token(contexta, BT_SEMI))));
  } else {
    /* Earley only: the same statement shapes with a fixed binary expression, ambiguity-free. */
    tail = l_benchmarks_concat(
        contexta, l_benchmarks_token(contexta, BT_ID),
        l_benchmarks_concat(
            contexta,
            logos_languages_contexta_contexta_operation_add_optional(
                NULL, contexta, l_benchmarks_concat(contexta, l_benchmarks_token(contexta, BT_PLUS), l_benchmarks_token(contexta, BT_ID))),
            l_benchmarks_token(contexta, BT_SEMI)));
    logos_languages_contexta_contexta_rule_define(
        contexta, statement_rule,
        l_benchmarks_concat(contexta, l_benchmarks_token(contexta, BT_LET),
                            l_benchmarks_concat(contexta, l_benchmarks_token(contexta, BT_ID),
                                                l_benchmarks_concat(contexta, l_benchmarks_token(contexta, BT_ASSIGN), tail))));
  }

  logos_languages_contexta_contexta_rule_define(
      contexta, program_rule,
      logos_languages_contexta_contexta_operation_add_star(
          NULL, contexta, logos_languages_contexta_contexta_operation_add_reference(NULL, contexta, statement_rule)));
  logos_languages_contexta_contexta_start_rule_set(contexta, program_rule);

  return contexta;
}

/* ---------------------------------------------------------------------------- */
/* MEASUREMENT                                                                   */
/* ---------------------------------------------------------------------------- */

/* A fixed count, so instruction and cycle counters compare between builds; the report is the best run. */
#define L_BENCHMARKS_ITERATIONS 5

static int64 l_benchmarks_now(void) {
  return (int64)nexus_time_get_monotonic().time;
}

static void l_benchmarks_report(EchoContext *context, const char *name, uint_large bytes, uint_large tokens, int64 best_nanoseconds,
                                uint32 iterations) {
  f_real seconds;

  seconds = (f_real)best_nanoseconds / 1.0e9;
  (void)echo_info(context, "Bench %-34s %8.2f ms  %8.1f MiB/s  %7.2f Mtok/s  %6.1f ns/tok  (%u runs)", name, seconds * 1000.0,
                  (f_real)bytes / 1048576.0 / seconds, (f_real)tokens / 1.0e6 / seconds, tokens > 0 ? (f_real)best_nanoseconds / (f_real)tokens : 0.0,
                  (unsigned)iterations);
}

/* ---------------------------------------------------------------------------- */
/* LEXER                                                                         */
/* ---------------------------------------------------------------------------- */

static uint_large l_benchmarks_lex_whole(LLexerProcess *process, const byte *source, uint_large size, uint_large chunk_size, NexusDataList *batch,
                                         boolean many) {
  LLexerToken  token;
  LLexerStatus status;
  uint_large   offset;
  uint_large   length;
  uint_large   count;

  logos_language_tooling_lexing_lexer_process_reset(process);
  offset = 0;
  count  = 0;

  do {
    length = chunk_size == 0 || chunk_size > size - offset ? size - offset : chunk_size;
    logos_language_tooling_lexing_lexer_process_chunk_add(process, source + offset, length, offset + length >= size);
    offset += length;

    for (;;) {
      if (many) {
        status = logos_language_tooling_lexing_lexer_process_lex_many(process, batch, LLEXER_TOKEN_LIMIT_UNBOUNDED);
        count += nexus_data_list_length_get(batch);

        if (status == LLS_NEED_INPUT || status == LLS_DONE) {
          break;
        }

        /* LLS_ERROR: the span is not in the batch; the next call continues behind it. */
        count++;
        continue;
      }

      status = logos_language_tooling_lexing_lexer_process_lex(process, &token);

      if (status == LLS_NEED_INPUT) {
        break;
      }

      if (status == LLS_DONE) {
        break;
      }

      count++;

      if (token.kind == L_LEXER_TOKEN_KIND_EOF) {
        break;
      }
    }

    if (status == LLS_DONE || (!many && status != LLS_NEED_INPUT)) {
      break;
    }
  } while (offset < size);

  return count;
}

/* Identifiers and numbers of 6 to 24 bytes separated by one space: the byte loop matters more than the per-token work. */
static void l_benchmarks_long_source_build(LBenchmarksBuffer *buffer, uint_large target) {
  uint32  state;
  uint32  length;
  uint32  i;
  char    word[32];
  boolean number;

  state            = 777;
  buffer->capacity = target + 64;
  buffer->data     = (char *)nexus_memory_allocate(buffer->capacity);
  buffer->size     = 0;

  while (buffer->size < target) {
    length = 6 + (l_benchmarks_random(&state) % 19);
    number = l_benchmarks_random(&state) % 4 == 0;

    for (i = 0; i < length; i++) {
      if (number) {
        word[i] = (char)('0' + (l_benchmarks_random(&state) % 10));
      } else {
        word[i] = (char)('a' + (l_benchmarks_random(&state) % 26));
      }
    }

    word[length]     = ' ';
    word[length + 1] = '\0';
    l_benchmarks_append(buffer, word);
  }
}

static void l_benchmarks_lexer_run(LogosRuntime *runtime, EchoContext *context, const LBenchmarksBuffer *source) {
  LLexer                  *lexer;
  LLexerProcess           *process;
  NexusDataList           *batch;
  int64                    start;
  int64                    best;
  uint32                   iterations;
  uint_large               tokens;
  uint_large               mode;
  static const uint_large  chunks[] = {0, 0, 4096, 64};
  static const char *const names[]  = {"lexer: whole buffer, lex", "lexer: whole buffer, lex_many", "lexer: 4 KiB chunks, lex",
                                       "lexer: 64 byte chunks, lex"};

  lexer   = l_benchmarks_lexer_build(runtime, 0);
  process = logos_language_tooling_lexing_lexer_process_create(lexer);
  batch   = nexus_data_list_create(NEXUS_SIZEOF(LLexerToken), 4096);

  for (mode = 0; mode < 4; mode++) {
    best       = 0;
    iterations = 0;
    tokens     = 0;

    while (iterations < L_BENCHMARKS_ITERATIONS) {
      start  = l_benchmarks_now();
      tokens = l_benchmarks_lex_whole(process, (const byte *)source->data, source->size, chunks[mode], batch, mode == 1);
      start  = l_benchmarks_now() - start;
      iterations++;

      if (best == 0 || start < best) {
        best = start;
      }
    }

    l_benchmarks_report(context, names[mode], source->size, tokens, best, iterations);
  }

  {
    LBenchmarksBuffer long_source;

    l_benchmarks_long_source_build(&long_source, (uint_large)8 * 1024 * 1024);
    best       = 0;
    iterations = 0;
    tokens     = 0;

    while (iterations < L_BENCHMARKS_ITERATIONS) {
      start  = l_benchmarks_now();
      tokens = l_benchmarks_lex_whole(process, (const byte *)long_source.data, long_source.size, 0, batch, FALSE);
      start  = l_benchmarks_now() - start;
      iterations++;

      if (best == 0 || start < best) {
        best = start;
      }
    }

    l_benchmarks_report(context, "lexer: long tokens (6-24 bytes), lex", long_source.size, tokens, best, iterations);
    nexus_memory_deallocate(long_source.data);
  }

  nexus_data_list_destroy(batch);
  logos_language_tooling_lexing_lexer_process_destroy(process);
  logos_language_tooling_lexing_lexer_destroy(lexer);
}

/* Compiling the rule set (NFA, DFA, minimization, union) is not a hot path, but it grows with the rule count. */
static void l_benchmarks_compile_run(LogosRuntime *runtime, EchoContext *context) {
  static const uint32 counts[] = {0, 100, 1000};
  LLexer             *lexer;
  int64               start;
  uint32              i;
  char                name[64];

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(counts); i++) {
    start = l_benchmarks_now();
    lexer = l_benchmarks_lexer_build(runtime, counts[i]);
    start = l_benchmarks_now() - start;
    logos_language_tooling_lexing_lexer_destroy(lexer);
    nexus_strings_string_format_with_truncation(name, NEXUS_SIZEOF(name), "lexer compile: %u + 13 rules", (unsigned)counts[i]);
    l_benchmarks_report(context, name, 0, 0, start, 1);
  }
}

/* ---------------------------------------------------------------------------- */
/* PARSER                                                                        */
/* ---------------------------------------------------------------------------- */

static LParserStatus l_benchmarks_parse(LParserProcess *process, const byte *source, uint_large size, uint_large chunk_size) {
  LParserStatus status;
  LParserTree  *tree;
  uint_large    offset;
  uint_large    length;
  NexusError   *error;

  logos_language_tooling_parsing_parser_process_reset(process);
  offset = 0;

  do {
    length = chunk_size == 0 || chunk_size > size - offset ? size - offset : chunk_size;
    error  = logos_language_tooling_parsing_parser_process_chunk_add(process, source + offset, length, offset + length >= size);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(error);
    }
    offset += length;

    do {
      status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
    } while (status == LPS_PROGRESS);
  } while (status == LPS_NEED_INPUT);

  tree = logos_language_tooling_parsing_parser_process_tree_take(process);
  logos_language_tooling_parsing_syntax_tree_destroy(tree);

  return status;
}

static void l_benchmarks_parser_run(LogosRuntime *runtime, EchoContext *context, const LBenchmarksBuffer *source, boolean pratt) {
  LParserConfiguration configuration;
  LLexer              *lexer;
  LContexta           *contexta;
  LParser             *parser;
  LParserProcess      *process;
  LLexerProcess       *counter;
  LLexerTokenKind      whitespace;
  LParserStatus        status;
  int64                start;
  int64                best;
  uint32               iterations;
  uint_large           tokens;
  LBenchmarksBuffer    earley_source;

  whitespace = BT_WS;
  lexer      = l_benchmarks_lexer_build(runtime, 0);
  contexta   = l_benchmarks_grammar_build(pratt);
  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.allowed_tokens      = &whitespace;
  configuration.allowed_token_count = 1;
  {
    NexusError *create_error;

    create_error = logos_language_tooling_parsing_parser_create(lexer, contexta, &configuration, &parser);

    if (create_error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(create_error);
    }
  }
  process = logos_language_tooling_parsing_parser_process_create(parser);

  /* The Earley-only grammar accepts only its own statement shape. */
  earley_source.data     = NULL;
  earley_source.size     = 0;
  earley_source.capacity = 0;

  if (!pratt) {
    uint_large i;

    earley_source.capacity = source->size;
    earley_source.data     = (char *)nexus_memory_allocate(earley_source.capacity + 64);

    for (i = 0; earley_source.size + 40 < source->size; i++) {
      l_benchmarks_append(&earley_source, i % 2 == 0 ? "let result = alpha + beta;\n" : "let other = gamma;\n");
    }
  }

  counter = logos_language_tooling_lexing_lexer_process_create(lexer);
  tokens  = l_benchmarks_lex_whole(counter, !pratt ? (const byte *)earley_source.data : (const byte *)source->data,
                                  !pratt ? earley_source.size : source->size, 0, NULL, FALSE);
  logos_language_tooling_lexing_lexer_process_destroy(counter);

  best       = 0;
  iterations = 0;
  status     = LPS_REJECTED;

  while (iterations < L_BENCHMARKS_ITERATIONS) {
    start  = l_benchmarks_now();
    status = l_benchmarks_parse(process, !pratt ? (const byte *)earley_source.data : (const byte *)source->data,
                                !pratt ? earley_source.size : source->size, 0);
    start  = l_benchmarks_now() - start;
    iterations++;

    if (best == 0 || start < best) {
      best = start;
    }
  }

  l_benchmarks_report(context, pratt ? "parser: Pratt expressions in Earley" : "parser: Earley rules only",
                      !pratt ? earley_source.size : source->size, tokens, best, iterations);

  if (status != LPS_ACCEPTED) {
    (void)echo_error(context, "Bench parser input was not accepted (status %d).", (int)status);
  }

  if (earley_source.data != NULL) {
    nexus_memory_deallocate(earley_source.data);
  }

  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);
  logos_languages_contexta_contexta_destroy(contexta);
  logos_language_tooling_lexing_lexer_destroy(lexer);
}

void logos_benchmarks_run(LogosRuntime *runtime, uint32 selection) {
  EchoContext      *context;
  LBenchmarksBuffer source;
  LBenchmarksBuffer parse_source;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "runtime must not be NULL");

  context = logos_runtime_echo_context_get(runtime);
  l_benchmarks_source_build(&source, (uint_large)8 * 1024 * 1024);
  l_benchmarks_source_build(&parse_source, (uint_large)1024 * 1024);
  (void)echo_info(context, "Bench sources: lexer %llu bytes, parser %llu bytes of statements.", (unsigned long long)source.size,
                  (unsigned long long)parse_source.size);

  if ((selection & LOGOS_BENCHMARKS_LEXER) != 0) {
    l_benchmarks_lexer_run(runtime, context, &source);
  }

  if ((selection & LOGOS_BENCHMARKS_PARSER_PRATT) != 0) {
    l_benchmarks_parser_run(runtime, context, &parse_source, TRUE);
  }

  if ((selection & LOGOS_BENCHMARKS_PARSER_EARLEY) != 0) {
    l_benchmarks_parser_run(runtime, context, &parse_source, FALSE);
  }

  if ((selection & LOGOS_BENCHMARKS_COMPILE) != 0) {
    l_benchmarks_compile_run(runtime, context);
  }

  nexus_memory_deallocate(source.data);
  nexus_memory_deallocate(parse_source.data);
}
