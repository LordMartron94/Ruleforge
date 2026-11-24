#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "../logos_tests.h"

/* ---------------------------------------------------------------------------- */
/* LANGUAGE                                                                      */
/* ---------------------------------------------------------------------------- */

enum {
  BK_WS = L_LEXER_TOKEN_KIND_FIRST_USER,
  BK_A,
  BK_B,
  BK_NUM,
  BK_ID,
  BK_PLUS,
  BK_STAR,
  BK_MINUS,
  BK_CARET,
  BK_BANG,
  BK_LP,
  BK_RP,
  BK_QUOTE,
  BK_STR,
  BK_QUOTE_END,
  BK_COMMA
};

typedef struct LTestsBkRule {
  const char                  *label;
  const char                  *regexp;
  LLexerTokenKind              kind;
  int32                        priority;
  LLexerTokenContextActionKind action;
  LLexerContext                operand;
  LLexerContext                context;
} LTestsBkRule;

static const LTestsBkRule l_tests_bk_rules[] = {
    {"ws", "[ \\t\\n]+", BK_WS, 0, LLTCAK_NONE, 0, 0},
    {"a", "a", BK_A, 1, LLTCAK_NONE, 0, 0},
    {"b", "b", BK_B, 1, LLTCAK_NONE, 0, 0},
    {"num", "[0-9]+", BK_NUM, 0, LLTCAK_NONE, 0, 0},
    {"id", "[c-z]+", BK_ID, 0, LLTCAK_NONE, 0, 0},
    {"plus", "\\+", BK_PLUS, 0, LLTCAK_NONE, 0, 0},
    {"star", "\\*", BK_STAR, 0, LLTCAK_NONE, 0, 0},
    {"minus", "-", BK_MINUS, 0, LLTCAK_NONE, 0, 0},
    {"caret", "\\^", BK_CARET, 0, LLTCAK_NONE, 0, 0},
    {"bang", "!", BK_BANG, 0, LLTCAK_NONE, 0, 0},
    {"lparen", "\\(", BK_LP, 0, LLTCAK_NONE, 0, 0},
    {"rparen", "\\)", BK_RP, 0, LLTCAK_NONE, 0, 0},
    {"comma", ",", BK_COMMA, 0, LLTCAK_NONE, 0, 0},
    {"quote", "\"", BK_QUOTE, 0, LLTCAK_PUSH, 1, 0},
    {"string", "[^\"]+", BK_STR, 0, LLTCAK_NONE, 0, 1},
    {"quote_end", "\"", BK_QUOTE_END, 0, LLTCAK_POP, 1, 1},
};

static void l_tests_bk_check(EchoContext *context, uint32 *failures, boolean ok, const char *name) {
  if (ok) {
    (void)echo_info(context, "Backtracking PASS: %s", name);
  } else {
    (void)echo_error(context, "Backtracking FAIL: %s", name);
    (*failures)++;
  }
}

static LLexer *l_tests_bk_lexer_build(LogosRuntime *runtime) {
  LLexer     *lexer;
  LRegula    *languages[NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_bk_rules)];
  NexusError *error;
  uint_large  i;

  lexer = logos_language_tooling_lexing_lexer_create(runtime);
  (void)logos_language_tooling_lexing_lexer_context_create(lexer, "root", TRUE);
  (void)logos_language_tooling_lexing_lexer_context_create(lexer, "string", FALSE);

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_bk_rules); i++) {
    LLexerTokenRule   rule;
    LRegulaOperation *operation;

    languages[i] = logos_languages_regula_regula_create(l_tests_bk_rules[i].label, NEXUS_VERSION_PACK(0, 0, 1, 0));
    error = logos_languages_regula_regula_operation_add_regexp(l_tests_bk_rules[i].label, languages[i], l_tests_bk_rules[i].regexp, &operation);

    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(error);
    }

    rule.label          = l_tests_bk_rules[i].label;
    rule.token_language = languages[i];
    rule.token_kind     = l_tests_bk_rules[i].kind;
    rule.action_kind    = l_tests_bk_rules[i].action;
    rule.match_priority = l_tests_bk_rules[i].priority;
    nexus_memory_bytes_clear(&rule.action_data, NEXUS_SIZEOF(rule.action_data));

    if (rule.action_kind == LLTCAK_PUSH) {
      rule.action_data.push_context = l_tests_bk_rules[i].operand;
    } else if (rule.action_kind == LLTCAK_POP) {
      rule.action_data.pop_amount = (uint8)l_tests_bk_rules[i].operand;
    }

    logos_language_tooling_lexing_lexer_context_token_rule_add(lexer, l_tests_bk_rules[i].context, rule);
  }

  error = logos_language_tooling_lexing_lexer_finalize(lexer);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_bk_rules); i++) {
    logos_languages_regula_regula_destroy(languages[i]);
  }

  return lexer;
}

static LContextaOperation *l_tests_bk_token(LContexta *contexta, LLexerTokenKind kind) {
  return logos_languages_contexta_contexta_operation_add_token(NULL, contexta, kind);
}

static LContextaOperation *l_tests_bk_cat(LContexta *contexta, LContextaOperation *left, LContextaOperation *right) {
  return logos_languages_contexta_contexta_operation_add_concatenate(NULL, contexta, left, right);
}

static LContextaOperation *l_tests_bk_ref(LContexta *contexta, const LContextaRule *rule) {
  return logos_languages_contexta_contexta_operation_add_reference(NULL, contexta, rule);
}

static LParser *l_tests_bk_parser(LLexer *lexer, LContexta *contexta, LParserConfiguration *configuration) {
  LParser    *parser;
  NexusError *error;

  configuration->engine = LPE_BACKTRACKING;
  error                 = logos_language_tooling_parsing_parser_create(lexer, contexta, configuration, &parser);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }

  return parser;
}

/* ---------------------------------------------------------------------------- */
/* RUNNING AND RENDERING                                                         */
/* ---------------------------------------------------------------------------- */

#define L_TESTS_BK_RENDER_MAX 2048

typedef struct LTestsBkRender {
  char       text[L_TESTS_BK_RENDER_MAX];
  uint_large length;
} LTestsBkRender;

static void l_tests_bk_append(LTestsBkRender *render, const char *text, uint_large size) {
  uint_large i;

  for (i = 0; i < size && render->length + 1 < L_TESTS_BK_RENDER_MAX; i++) {
    render->text[render->length++] = text[i];
  }

  render->text[render->length] = '\0';
}

/* Tokens as text (whitespace skipped), rules and operators as {label children}. */
static void l_tests_bk_render(const LParserTree *tree, const LParserGreenNode *node, LTestsBkRender *render, boolean show_rules) {
  const LParserGreenAlternative *alternative;
  const char                    *label;
  uint_large                     size;
  uint32                         i;
  boolean                        braces;
  LParserGreenKind               kind;

  kind = logos_language_tooling_parsing_syntax_green_node_kind_get(node);

  if (kind == LPGK_TOKEN) {
    if (logos_language_tooling_parsing_syntax_green_node_token_kind_get(tree, node) != BK_WS &&
        logos_language_tooling_parsing_syntax_green_node_token_kind_get(tree, node) != L_LEXER_TOKEN_KIND_EOF) {
      l_tests_bk_append(render, " ", 1);
      l_tests_bk_append(render, (const char *)logos_language_tooling_parsing_syntax_green_node_text_get(tree, node, &size), size);
    }

    return;
  }

  label  = logos_language_tooling_parsing_syntax_green_node_label_get(tree, node);
  braces = label != NULL &&
           ((kind == LPGK_RULE && show_rules) || logos_language_tooling_parsing_syntax_green_node_operation_kind_get(tree, node) == LCOK_OPERATORS);

  if (braces) {
    l_tests_bk_append(render, " {", 2);
    l_tests_bk_append(render, label, nexus_strings_string_length_get(label));
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative != NULL) {
    for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
      l_tests_bk_render(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), render, show_rules);
    }
  }

  if (braces) {
    l_tests_bk_append(render, "}", 1);
  }
}

typedef struct LTestsBkResult {
  LParserTree  *tree;
  LParserStatus status;
} LTestsBkResult;

static LTestsBkResult l_tests_bk_run(LParserProcess *process, const char *source, uint_large chunk_size) {
  LTestsBkResult result;
  uint_large     size;
  uint_large     offset;
  uint_large     length;
  NexusError    *error;

  size = nexus_strings_string_length_get(source);
  logos_language_tooling_parsing_parser_process_reset(process);
  offset = 0;

  do {
    length = chunk_size == 0 || chunk_size > size - offset ? size - offset : chunk_size;
    error  = logos_language_tooling_parsing_parser_process_chunk_add(process, (const byte *)source + offset, length, offset + length >= size);

    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(error);
    }

    offset += length;

    do {
      result.status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
    } while (result.status == LPS_PROGRESS);
  } while (result.status == LPS_NEED_INPUT);

  result.tree = logos_language_tooling_parsing_parser_process_tree_take(process);

  return result;
}

/* Parses whole and in 1- and 3-byte chunks (which must agree), and compares the rendering and status with the expectation. */
static void l_tests_bk_case(EchoContext *context, uint32 *failures, LParserProcess *process, const char *source, const char *expected,
                            LParserStatus expected_status, boolean show_rules) {
  static const uint_large chunks[3] = {0, 1, 3};
  LTestsBkResult          result;
  LTestsBkRender          render;
  LTestsBkRender          reference;
  uint_large              i;
  boolean                 ok;

  for (i = 0; i < 3; i++) {
    result         = l_tests_bk_run(process, source, chunks[i]);
    render.length  = 0;
    render.text[0] = '\0';
    l_tests_bk_render(result.tree, logos_language_tooling_parsing_syntax_tree_root_get(result.tree), &render, show_rules);

    if (i == 0) {
      reference = render;
      ok        = result.status == expected_status &&
           (expected == NULL || nexus_strings_string_equals(render.length > 0 ? render.text + 1 : render.text, expected));
      l_tests_bk_check(context, failures, ok, source);

      if (!ok) {
        (void)echo_error(context, "  status %d (expected %d), rendered '%s', expected '%s'", (int)result.status, (int)expected_status, render.text,
                         expected != NULL ? expected : "-");
      }
    } else {
      ok = result.status == expected_status && nexus_strings_string_equals(render.text, reference.text);
      l_tests_bk_check(context, failures, ok, "chunked parse agrees with the whole parse");
    }

    logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  }
}

/* ---------------------------------------------------------------------------- */
/* CASES                                                                         */
/* ---------------------------------------------------------------------------- */

/* The spec's choice example: the same bytes, three modes. */
static void l_tests_bk_choice_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LLexer              *lexer;
  LContexta           *contexta;
  LContextaRule       *ordered_rule;
  LContextaRule       *longest_rule;
  LContextaRule       *strict_rule;
  LContextaOperation  *first;
  LContextaOperation  *second;
  LParserConfiguration configuration;
  LParser             *parser;
  LParserProcess      *process;
  LTestsBkResult       result;
  uint32               modes;

  lexer    = l_tests_bk_lexer_build(runtime);
  contexta = logos_languages_contexta_contexta_create("choice", NEXUS_VERSION_PACK(0, 0, 1, 0));

  ordered_rule = logos_languages_contexta_contexta_rule_add("Ordered", contexta);
  longest_rule = logos_languages_contexta_contexta_rule_add("Longest", contexta);
  strict_rule  = logos_languages_contexta_contexta_rule_add("Strict", contexta);

  for (modes = 0; modes < 3; modes++) {
    LContextaRule      *target;
    LContextaChoiceMode mode;

    first  = l_tests_bk_token(contexta, BK_A);
    second = l_tests_bk_cat(contexta, l_tests_bk_token(contexta, BK_A), l_tests_bk_token(contexta, BK_B));
    target = ordered_rule;
    mode   = LCCM_ORDERED;

    if (modes == 1) {
      target = longest_rule;
      mode   = LCCM_LONGEST;
    } else if (modes == 2) {
      target = strict_rule;
      mode   = LCCM_UNAMBIGUOUS;
    }

    logos_languages_contexta_contexta_rule_define(contexta, target,
                                                  logos_languages_contexta_contexta_operation_add_choice(NULL, contexta, first, second, mode));
  }

  logos_languages_contexta_contexta_start_rule_set(contexta, longest_rule);

  /* Longest: "ab" is one complete document. */
  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  parser  = l_tests_bk_parser(lexer, contexta, &configuration);
  process = logos_language_tooling_parsing_parser_process_create(parser);
  l_tests_bk_case(context, failures, process, "ab", "a b", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "a", "a", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "b", NULL, LPS_REJECTED, FALSE);
  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);

  /* Ordered: the first branch wins on "ab", the rest of the input then fails full consumption. */
  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.entry = ordered_rule;
  parser              = l_tests_bk_parser(lexer, contexta, &configuration);
  process             = logos_language_tooling_parsing_parser_process_create(parser);
  l_tests_bk_case(context, failures, process, "ab", NULL, LPS_REJECTED, FALSE);
  l_tests_bk_case(context, failures, process, "a", "a", LPS_ACCEPTED, FALSE);
  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);

  /* Unambiguous: both branches succeed on "ab", which is an ambiguity, never a silent pick. */
  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.entry = strict_rule;
  parser              = l_tests_bk_parser(lexer, contexta, &configuration);
  process             = logos_language_tooling_parsing_parser_process_create(parser);
  result              = l_tests_bk_run(process, "ab", 0);
  l_tests_bk_check(context, failures,
                   result.status == LPS_REJECTED && logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(result.tree) >= 1 &&
                       logos_language_tooling_parsing_syntax_diagnostic_kind_get(
                           logos_language_tooling_parsing_syntax_tree_diagnostic_at(result.tree, 0)) == LPDK_AMBIGUOUS,
                   "an unambiguous choice reports ambiguity");
  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  l_tests_bk_case(context, failures, process, "a", "a", LPS_ACCEPTED, FALSE);
  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);

  /* The parser default mode applies to a plain union. */
  logos_languages_contexta_contexta_destroy(contexta);
  contexta     = logos_languages_contexta_contexta_create("default", NEXUS_VERSION_PACK(0, 0, 1, 0));
  ordered_rule = logos_languages_contexta_contexta_rule_add("Plain", contexta);
  logos_languages_contexta_contexta_rule_define(contexta, ordered_rule,
                                                logos_languages_contexta_contexta_operation_add_unionize(
                                                    NULL, contexta, l_tests_bk_token(contexta, BK_A),
                                                    l_tests_bk_cat(contexta, l_tests_bk_token(contexta, BK_A), l_tests_bk_token(contexta, BK_B))));
  logos_languages_contexta_contexta_start_rule_set(contexta, ordered_rule);
  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.choice = LCCM_LONGEST;
  parser               = l_tests_bk_parser(lexer, contexta, &configuration);
  process              = logos_language_tooling_parsing_parser_process_create(parser);
  l_tests_bk_case(context, failures, process, "ab", "a b", LPS_ACCEPTED, FALSE);
  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);

  logos_languages_contexta_contexta_destroy(contexta);
  logos_language_tooling_lexing_lexer_destroy(lexer);
}

/*
Document = (Item ',')* Item? where Item = number | string, with whitespace as a global rule and strings excluding it:
  String = quote strchar quote_end
*/
static void l_tests_bk_globals_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LLexer              *lexer;
  LContexta           *contexta;
  LContextaRule       *document;
  LContextaRule       *item;
  LContextaRule       *string_rule;
  LContextaRule       *trivia;
  const LContextaRule *globals[1];
  const LContextaRule *excluded[1];
  LContextaOperation  *body;
  LParserConfiguration configuration;
  LParser             *parser;
  LParserProcess      *process;

  lexer    = l_tests_bk_lexer_build(runtime);
  contexta = logos_languages_contexta_contexta_create("globals", NEXUS_VERSION_PACK(0, 0, 1, 0));

  document    = logos_languages_contexta_contexta_rule_add("Document", contexta);
  item        = logos_languages_contexta_contexta_rule_add("Item", contexta);
  string_rule = logos_languages_contexta_contexta_rule_add("String", contexta);
  trivia      = logos_languages_contexta_contexta_rule_add("Trivia", contexta);

  logos_languages_contexta_contexta_rule_define(contexta, trivia, l_tests_bk_token(contexta, BK_WS));
  logos_languages_contexta_contexta_rule_define(contexta, item,
                                                logos_languages_contexta_contexta_operation_add_unionize(
                                                    NULL, contexta, l_tests_bk_token(contexta, BK_NUM), l_tests_bk_ref(contexta, string_rule)));

  globals[0]  = trivia;
  excluded[0] = trivia;
  body        = l_tests_bk_cat(contexta, l_tests_bk_token(contexta, BK_QUOTE),
                               l_tests_bk_cat(contexta, l_tests_bk_token(contexta, BK_STR), l_tests_bk_token(contexta, BK_QUOTE_END)));
  logos_languages_contexta_contexta_rule_define(contexta, string_rule,
                                                logos_languages_contexta_contexta_operation_add_exclude(NULL, contexta, body, excluded, 1));

  /* Item (',' Item)* */
  logos_languages_contexta_contexta_rule_define(
      contexta, document,
      l_tests_bk_cat(contexta, l_tests_bk_ref(contexta, item),
                     logos_languages_contexta_contexta_operation_add_star(
                         NULL, contexta, l_tests_bk_cat(contexta, l_tests_bk_token(contexta, BK_COMMA), l_tests_bk_ref(contexta, item)))));
  logos_languages_contexta_contexta_start_rule_set(contexta, document);

  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.global_rules      = globals;
  configuration.global_rule_count = 1;
  parser                          = l_tests_bk_parser(lexer, contexta, &configuration);
  process                         = logos_language_tooling_parsing_parser_process_create(parser);

  l_tests_bk_case(context, failures, process, "1,2,3", "1 , 2 , 3", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "  1 , 2 ,\n3  ", "1 , 2 , 3", LPS_ACCEPTED, FALSE);
  /* A pushed lexical context: the string's interior is one token, spaces included, and the closing quote pops it. */
  l_tests_bk_case(context, failures, process, "\"a b  c\" , 7", "\" a b  c \" , 7", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "1 2", NULL, LPS_REJECTED, FALSE);
  l_tests_bk_case(context, failures, process, "", NULL, LPS_REJECTED, FALSE);

  {
    LTestsBkResult           result;
    const LParserDiagnostic *diagnostic;
    uint_large               size;
    const byte              *text;
    uint_large               i;
    boolean                  expects_number;

    /* Lossless: the root covers every byte, whitespace included. */
    result = l_tests_bk_run(process, " 1 , \"x y\" ", 0);
    text   = logos_language_tooling_parsing_syntax_green_node_text_get(result.tree, logos_language_tooling_parsing_syntax_tree_root_get(result.tree),
                                                                       &size);
    (void)text;
    l_tests_bk_check(context, failures,
                     result.status == LPS_ACCEPTED && size == nexus_strings_string_length_get(" 1 , \"x y\" ") &&
                         logos_language_tooling_parsing_syntax_tree_token_count_get(result.tree) == 10,
                     "an accepted tree covers all bytes and tabulates its tokens");
    logos_language_tooling_parsing_syntax_tree_destroy(result.tree);

    /* The farthest failure names what the grammar allowed there. */
    result         = l_tests_bk_run(process, "1 , ,", 0);
    expects_number = FALSE;

    if (logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(result.tree) >= 1) {
      diagnostic = logos_language_tooling_parsing_syntax_tree_diagnostic_at(result.tree, 0);

      for (i = 0; i < logos_language_tooling_parsing_syntax_diagnostic_expectation_count_get(diagnostic); i++) {
        if (logos_language_tooling_parsing_syntax_expectation_token_kind_get(
                logos_language_tooling_parsing_syntax_tree_diagnostic_expectation_at(result.tree, 0, i)) == BK_NUM) {
          expects_number = TRUE;
        }
      }

      expects_number = expects_number && logos_language_tooling_parsing_syntax_diagnostic_byte_offset_get(diagnostic) == 4;
    }

    l_tests_bk_check(context, failures, result.status == LPS_REJECTED && expects_number, "the farthest failure lists the expected tokens");
    logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  }

  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);
  logos_languages_contexta_contexta_destroy(contexta);
  logos_language_tooling_lexing_lexer_destroy(lexer);
}

/* Pratt with a rule atom: Atom = number | id | '(' Expression ')', power, factorial and prefix minus as in the spec's Calculator. */
static void l_tests_bk_pratt_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  static const LContextaOperatorEntry table[] = {{LCOP_PREFIX, "neg", BK_MINUS, 0, 65},
                                                 {LCOP_INFIX, "add", BK_PLUS, 30, 31},
                                                 {LCOP_INFIX, "mul", BK_STAR, 50, 51},
                                                 {LCOP_INFIX, "pow", BK_CARET, 80, 80},
                                                 {LCOP_POSTFIX, "fact", BK_BANG, 90, 0}};
  LLexer                             *lexer;
  LContexta                          *contexta;
  LContextaRule                      *expression;
  LContextaRule                      *atom;
  LContextaRule                      *trivia;
  const LContextaRule                *globals[1];
  LParserConfiguration                configuration;
  LParser                            *parser;
  LParserProcess                     *process;

  lexer      = l_tests_bk_lexer_build(runtime);
  contexta   = logos_languages_contexta_contexta_create("pratt", NEXUS_VERSION_PACK(0, 0, 1, 0));
  expression = logos_languages_contexta_contexta_rule_add("Expression", contexta);
  atom       = logos_languages_contexta_contexta_rule_add("Atom", contexta);
  trivia     = logos_languages_contexta_contexta_rule_add("Trivia", contexta);

  logos_languages_contexta_contexta_rule_define(contexta, trivia, l_tests_bk_token(contexta, BK_WS));
  logos_languages_contexta_contexta_rule_define(
      contexta, atom,
      logos_languages_contexta_contexta_operation_add_unionize(
          NULL, contexta, l_tests_bk_token(contexta, BK_NUM),
          logos_languages_contexta_contexta_operation_add_unionize(
              NULL, contexta, l_tests_bk_token(contexta, BK_ID),
              l_tests_bk_cat(contexta, l_tests_bk_token(contexta, BK_LP),
                             l_tests_bk_cat(contexta, l_tests_bk_ref(contexta, expression), l_tests_bk_token(contexta, BK_RP))))));
  logos_languages_contexta_contexta_rule_define(contexta, expression,
                                                logos_languages_contexta_contexta_operation_add_operators(
                                                    NULL, contexta, l_tests_bk_ref(contexta, atom), table, NEXUS_ARRAY_SIZE_ELEMENTS(table)));
  logos_languages_contexta_contexta_start_rule_set(contexta, expression);

  globals[0] = trivia;
  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.global_rules      = globals;
  configuration.global_rule_count = 1;
  parser                          = l_tests_bk_parser(lexer, contexta, &configuration);
  process                         = logos_language_tooling_parsing_parser_process_create(parser);

  l_tests_bk_case(context, failures, process, "1", "1", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "2*3+4", "{add {mul 2 * 3} + 4}", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "2+3*4", "{add 2 + {mul 3 * 4}}", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "1+2+3", "{add {add 1 + 2} + 3}", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "2^3^4", "{pow 2 ^ {pow 3 ^ 4}}", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "-2^3!", "{neg - {pow 2 ^ {fact 3 !}}}", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, " ( 1 + 2 ) * x ", "{mul ( {add 1 + 2} ) * x}", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "((x))", "( ( x ) )", LPS_ACCEPTED, FALSE);
  l_tests_bk_case(context, failures, process, "1+", NULL, LPS_REJECTED, FALSE);
  l_tests_bk_case(context, failures, process, "(1", NULL, LPS_REJECTED, FALSE);

  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);
  logos_languages_contexta_contexta_destroy(contexta);
  logos_language_tooling_lexing_lexer_destroy(lexer);
}

/* Rules that nest deeply must hit the depth limit and report it instead of overflowing the stack. */
static void l_tests_bk_limits_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LLexer              *lexer;
  LContexta           *contexta;
  LContextaRule       *nest;
  LParserConfiguration configuration;
  LParser             *parser;
  LParserProcess      *process;
  LTestsBkResult       result;
  char                *source;
  uint_large           depth;
  uint_large           i;

  lexer    = l_tests_bk_lexer_build(runtime);
  contexta = logos_languages_contexta_contexta_create("nest", NEXUS_VERSION_PACK(0, 0, 1, 0));
  nest     = logos_languages_contexta_contexta_rule_add("Nest", contexta);

  /* Nest = '(' Nest ')' | 'a' */
  logos_languages_contexta_contexta_rule_define(
      contexta, nest,
      logos_languages_contexta_contexta_operation_add_unionize(
          NULL, contexta,
          l_tests_bk_cat(contexta, l_tests_bk_token(contexta, BK_LP),
                         l_tests_bk_cat(contexta, l_tests_bk_ref(contexta, nest), l_tests_bk_token(contexta, BK_RP))),
          l_tests_bk_token(contexta, BK_A)));
  logos_languages_contexta_contexta_start_rule_set(contexta, nest);
  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.depth_limit = 100;
  parser                    = l_tests_bk_parser(lexer, contexta, &configuration);
  process                   = logos_language_tooling_parsing_parser_process_create(parser);

  depth  = 5000;
  source = (char *)nexus_memory_allocate((depth * 2) + 2);

  for (i = 0; i < depth; i++) {
    source[i]               = '(';
    source[(depth * 2) - i] = ')';
  }

  source[depth]           = 'a';
  source[(depth * 2) + 1] = '\0';
  result                  = l_tests_bk_run(process, source, 0);
  l_tests_bk_check(context, failures, result.status == LPS_LIMIT, "nesting beyond the depth limit reports LIMIT");
  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  nexus_memory_deallocate(source);

  l_tests_bk_case(context, failures, process, "((a))", NULL, LPS_ACCEPTED, FALSE);

  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);
  logos_languages_contexta_contexta_destroy(contexta);
  logos_language_tooling_lexing_lexer_destroy(lexer);
}

void logos_tests_backtracking_run(LogosRuntime *runtime) {
  EchoContext *context;
  uint32       failures;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "runtime must not be NULL");

  context  = logos_runtime_echo_context_get(runtime);
  failures = 0;

  l_tests_bk_choice_run(runtime, context, &failures);
  l_tests_bk_globals_run(runtime, context, &failures);
  l_tests_bk_pratt_run(runtime, context, &failures);
  l_tests_bk_limits_run(runtime, context, &failures);

  if (failures == 0) {
    (void)echo_info(context, "Backtracking: all checks passed.");
  } else {
    (void)echo_error(context, "Backtracking: %u check(s) FAILED.", (unsigned)failures);
  }
}
