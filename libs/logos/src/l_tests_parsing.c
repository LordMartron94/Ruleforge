#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "../logos_tests.h"

/* ---------------------------------------------------------------------------- */
/* LANGUAGE                                                                      */
/* ---------------------------------------------------------------------------- */

enum {
  PT_WS = L_LEXER_TOKEN_KIND_FIRST_USER,
  PT_LET,
  PT_NUM,
  PT_ID,
  PT_PLUS,
  PT_MINUS,
  PT_STAR,
  PT_CARET,
  PT_BANG,
  PT_LP,
  PT_RP,
  PT_LB,
  PT_RB,
  PT_COMMA,
  PT_QUESTION,
  PT_COLON,
  PT_SEMI,
  PT_LT,
  PT_ASSIGN
};

typedef struct LTestsParseRule {
  const char     *label;
  const char     *regexp;
  LLexerTokenKind kind;
  uint8           priority;
} LTestsParseRule;

static const LTestsParseRule l_tests_parsing_rules[] = {
    {"whitespace", "[ \\t\\n]+", PT_WS, 0},
    {"let", "let", PT_LET, 1},
    {"number", "[0-9]+", PT_NUM, 0},
    {"identifier", "[a-z]+", PT_ID, 0},
    {"plus", "\\+", PT_PLUS, 0},
    {"minus", "-", PT_MINUS, 0},
    {"star", "\\*", PT_STAR, 0},
    {"caret", "\\^", PT_CARET, 0},
    {"bang", "!", PT_BANG, 0},
    {"lparen", "\\(", PT_LP, 0},
    {"rparen", "\\)", PT_RP, 0},
    {"lbracket", "\\[", PT_LB, 0},
    {"rbracket", "\\]", PT_RB, 0},
    {"comma", ",", PT_COMMA, 0},
    {"question", "\\?", PT_QUESTION, 0},
    {"colon", ":", PT_COLON, 0},
    {"semicolon", ";", PT_SEMI, 0},
    {"less", "<", PT_LT, 0},
    {"assign", "=", PT_ASSIGN, 0},
};

/* The expression table: precedence 1 ternary, 2 non-associative <, 3 + -, 4 *, 5 prefix -, 6 ^ (right), 7 postfix !, 8 call and index. */
static const LContextaPrattEntry l_tests_parsing_table[] = {
    {LCPK_ATOM, "number", PT_NUM, LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 0, FALSE, FALSE},
    {LCPK_ATOM, "name", PT_ID, LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 0, FALSE, FALSE},
    {LCPK_PREFIX, "neg", PT_MINUS, LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 5, FALSE, FALSE},
    {LCPK_INFIX_LEFT, "add", PT_PLUS, LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 3, FALSE, FALSE},
    {LCPK_INFIX_LEFT, "sub", PT_MINUS, LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 3, FALSE, FALSE},
    {LCPK_INFIX_LEFT, "mul", PT_STAR, LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 4, FALSE, FALSE},
    {LCPK_INFIX_RIGHT, "pow", PT_CARET, LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 6, FALSE, FALSE},
    {LCPK_INFIX_NONE, "lt", PT_LT, LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 2, FALSE, FALSE},
    {LCPK_POSTFIX, "fact", PT_BANG, LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 7, FALSE, FALSE},
    {LCPK_ENCLOSED, "group", PT_LP, PT_RP, LCONTEXTA_PRATT_TOKEN_NONE, 0, FALSE, FALSE},
    {LCPK_ENCLOSED, "list", PT_LB, PT_RB, PT_COMMA, 0, TRUE, TRUE},
    {LCPK_APPLICATION, "call", PT_LP, PT_RP, PT_COMMA, 8, TRUE, FALSE},
    {LCPK_APPLICATION, "index", PT_LB, PT_RB, LCONTEXTA_PRATT_TOKEN_NONE, 8, FALSE, FALSE},
    {LCPK_TERNARY, "cond", PT_QUESTION, PT_COLON, LCONTEXTA_PRATT_TOKEN_NONE, 1, FALSE, FALSE},
};

typedef struct LTestsParsing {
  LLexer    *lexer;
  LContexta *contexta;
  LParser   *parser;
} LTestsParsing;

static void l_tests_parsing_check(EchoContext *context, uint32 *failures, boolean ok, const char *name) {
  if (ok) {
    (void)echo_info(context, "Parser PASS: %s", name);
  } else {
    (void)echo_error(context, "Parser FAIL: %s", name);
    (*failures)++;
  }
}

static LLexer *l_tests_parsing_lexer_build(LogosRuntime *runtime) {
  LLexer    *lexer;
  LRegula   *languages[NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_parsing_rules)];
  uint_large i;

  lexer = logos_language_tooling_lexing_lexer_create(runtime);
  (void)logos_language_tooling_lexing_lexer_context_create(lexer, "root", TRUE);

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_parsing_rules); i++) {
    LLexerTokenRule   rule;
    LRegulaOperation *operation;
    NexusError       *error;

    languages[i] = logos_languages_regula_regula_create(l_tests_parsing_rules[i].label, NEXUS_VERSION_PACK(0, 0, 1, 0));
    error =
        logos_languages_regula_regula_operation_add_regexp(l_tests_parsing_rules[i].label, languages[i], l_tests_parsing_rules[i].regexp, &operation);
    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(error);
    }

    rule.label          = l_tests_parsing_rules[i].label;
    rule.token_language = languages[i];
    rule.token_kind     = l_tests_parsing_rules[i].kind;
    rule.action_kind    = LLTCAK_NONE;
    rule.match_priority = l_tests_parsing_rules[i].priority;
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

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_parsing_rules); i++) {
    logos_languages_regula_regula_destroy(languages[i]);
  }

  return lexer;
}

/*
Grammar A is the expression table alone. Grammar B mixes it with ordinary rules:
  Program = Statement*   Statement = 'let' name '=' Expr ';' | Expr ';'
with whitespace admitted by default everywhere.
*/
static LContexta *l_tests_parsing_grammar_build(boolean mixed) {
  LContexta          *contexta;
  LContextaRule      *expression_rule;
  LContextaRule      *statement_rule;
  LContextaRule      *program_rule;
  LContextaOperation *expression;
  LContextaOperation *let_statement;
  LContextaOperation *expression_statement;
  LContextaOperation *tail;

  contexta = logos_languages_contexta_contexta_create("test", NEXUS_VERSION_PACK(0, 0, 1, 0));

  expression_rule = logos_languages_contexta_contexta_rule_add("Expression", contexta);
  expression =
      logos_languages_contexta_contexta_operation_add_pratt(NULL, contexta, l_tests_parsing_table, NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_parsing_table));
  logos_languages_contexta_contexta_rule_define(contexta, expression_rule, expression);

  if (!mixed) {
    logos_languages_contexta_contexta_start_rule_set(contexta, expression_rule);

    return contexta;
  }

  statement_rule = logos_languages_contexta_contexta_rule_add("Statement", contexta);
  program_rule   = logos_languages_contexta_contexta_rule_add("Program", contexta);

  /* 'let' name '=' Expression ';' */
  tail = logos_languages_contexta_contexta_operation_add_token("semicolon", contexta, PT_SEMI);
  tail = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_reference(NULL, contexta, expression_rule), tail);
  tail = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_token("assign", contexta, PT_ASSIGN), tail);
  tail = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_token("identifier", contexta, PT_ID), tail);
  let_statement = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_token("let", contexta, PT_LET), tail);

  /* Expression ';' */
  expression_statement = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_reference(NULL, contexta, expression_rule),
      logos_languages_contexta_contexta_operation_add_token("semicolon", contexta, PT_SEMI));

  logos_languages_contexta_contexta_rule_define(
      contexta, statement_rule, logos_languages_contexta_contexta_operation_add_unionize(NULL, contexta, let_statement, expression_statement));
  logos_languages_contexta_contexta_rule_define(
      contexta, program_rule,
      logos_languages_contexta_contexta_operation_add_star(
          NULL, contexta, logos_languages_contexta_contexta_operation_add_reference(NULL, contexta, statement_rule)));
  logos_languages_contexta_contexta_start_rule_set(contexta, program_rule);

  return contexta;
}

static void l_tests_parsing_build(LogosRuntime *runtime, boolean mixed, LTestsParsing *out) {
  LParserConfiguration configuration;
  LLexerTokenKind      allowed[1];
  NexusError          *error;

  out->lexer    = l_tests_parsing_lexer_build(runtime);
  out->contexta = l_tests_parsing_grammar_build(mixed);
  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  allowed[0] = PT_WS;

  if (mixed) {
    configuration.allowed_tokens      = allowed;
    configuration.allowed_token_count = 1;
  }

  error = logos_language_tooling_parsing_parser_create(out->lexer, out->contexta, &configuration, &out->parser);
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }
}

static void l_tests_parsing_destroy(LTestsParsing *parsing) {
  logos_language_tooling_parsing_parser_destroy(parsing->parser);
  logos_languages_contexta_contexta_destroy(parsing->contexta);
  logos_language_tooling_lexing_lexer_destroy(parsing->lexer);
}

/* ---------------------------------------------------------------------------- */
/* RUNNING AND RENDERING                                                         */
/* ---------------------------------------------------------------------------- */

#define L_TESTS_PARSING_RENDER_MAX 4096

typedef struct LTestsParsingRender {
  char       text[L_TESTS_PARSING_RENDER_MAX];
  uint_large length;
} LTestsParsingRender;

static void l_tests_parsing_append(LTestsParsingRender *render, const char *text, uint_large size) {
  uint_large i;

  for (i = 0; i < size && render->length + 1 < L_TESTS_PARSING_RENDER_MAX; i++) {
    render->text[render->length++] = text[i];
  }

  render->text[render->length] = '\0';
}

/*
Renders the first alternative of every node: tokens as text (whitespace skipped), Pratt operators as {label children},
everything else flattened. A Pratt operator is a labelled LCOK_PRATT expression; the wrapper for the whole table is unlabelled.
*/
static void l_tests_parsing_render(const LParserTree *tree, const LParserGreenNode *node, LTestsParsingRender *render) {
  const LParserGreenAlternative *alternative;
  uint32                         i;
  uint_large                     size;
  LParserGreenKind               kind;
  boolean                        braces;

  kind = logos_language_tooling_parsing_syntax_green_node_kind_get(node);

  if (kind == LPGK_TOKEN) {
    if (logos_language_tooling_parsing_syntax_green_node_token_kind_get(tree, node) != PT_WS &&
        logos_language_tooling_parsing_syntax_green_node_token_kind_get(tree, node) != L_LEXER_TOKEN_KIND_EOF) {
      l_tests_parsing_append(render, " ", 1);
      l_tests_parsing_append(render, (const char *)logos_language_tooling_parsing_syntax_green_node_text_get(tree, node, &size), size);
    }

    return;
  }

  if (kind == LPGK_UNPARSED) {
    l_tests_parsing_append(render, " ?", 2);

    return;
  }

  braces = kind == LPGK_EXPRESSION && logos_language_tooling_parsing_syntax_green_node_operation_kind_get(tree, node) == LCOK_PRATT &&
           logos_language_tooling_parsing_syntax_green_node_label_get(tree, node) != NULL;

  if (braces) {
    l_tests_parsing_append(render, " {", 2);
    l_tests_parsing_append(render, logos_language_tooling_parsing_syntax_green_node_label_get(tree, node),
                           nexus_strings_string_length_get(logos_language_tooling_parsing_syntax_green_node_label_get(tree, node)));
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative != NULL) {
    for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
      l_tests_parsing_render(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), render);
    }
  }

  if (braces) {
    l_tests_parsing_append(render, "}", 1);
  }
}

typedef struct LTestsParsingResult {
  LParserTree  *tree;
  LParserStatus status;
} LTestsParsingResult;

/* Parses source in chunks of chunk_size (0 means one chunk) and takes the tree. */
static LTestsParsingResult l_tests_parsing_run(LParserProcess *process, const char *source, uint_large size, uint_large chunk_size) {
  LTestsParsingResult result;
  uint_large          offset;
  uint_large          length;
  boolean             last;
  NexusError         *error;

  logos_language_tooling_parsing_parser_process_reset(process);
  offset = 0;

  do {
    length = chunk_size == 0 || chunk_size > size - offset ? size - offset : chunk_size;
    last   = offset + length >= size;
    error  = logos_language_tooling_parsing_parser_process_chunk_add(process, (const byte *)source + offset, length, last);
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

#define L_TESTS_PARSING_CHUNKS_MAX 4

/* Checks the rendered shape and the status, then repeats under byte-sized and odd chunkings, which must agree exactly. */
static void l_tests_parsing_case(EchoContext *context, uint32 *failures, LParserProcess *process, const char *source, const char *expected,
                                 LParserStatus expected_status) {
  static const uint_large chunks[L_TESTS_PARSING_CHUNKS_MAX] = {0, 1, 2, 3};
  LTestsParsingResult     result;
  LTestsParsingRender     render;
  LTestsParsingRender     reference;
  const LParserGreenNode *root;
  uint_large              source_size;
  uint_large              i;
  boolean                 ok;

  source_size = nexus_strings_string_length_get(source);

  for (i = 0; i < L_TESTS_PARSING_CHUNKS_MAX; i++) {
    result         = l_tests_parsing_run(process, source, source_size, chunks[i]);
    render.length  = 0;
    render.text[0] = '\0';
    root           = logos_language_tooling_parsing_syntax_tree_root_get(result.tree);
    l_tests_parsing_render(result.tree, root, &render);

    if (i == 0) {
      reference = render;
      ok        = result.status == expected_status &&
           (expected == NULL || nexus_strings_string_equals(render.length > 0 ? render.text + 1 : render.text, expected));
      l_tests_parsing_check(context, failures, ok, source);

      if (!ok) {
        (void)echo_error(context, "  status %d (expected %d), rendered '%s', expected '%s'", (int)result.status, (int)expected_status, render.text,
                         expected != NULL ? expected : "-");
      }
    } else {
      ok = result.status == expected_status && nexus_strings_string_equals(render.text, reference.text);
      l_tests_parsing_check(context, failures, ok, "chunked parse agrees with the whole parse");
    }

    logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  }
}

/* ---------------------------------------------------------------------------- */
/* CASES                                                                         */
/* ---------------------------------------------------------------------------- */

static void l_tests_parsing_expression_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LTestsParsing   parsing;
  LParserProcess *process;

  l_tests_parsing_build(runtime, FALSE, &parsing);
  process = logos_language_tooling_parsing_parser_process_create(parsing.parser);

  l_tests_parsing_case(context, failures, process, "1", "1", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a+b*c", "{add a + {mul b * c}}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a*b+c", "{add {mul a * b} + c}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a-b-c", "{sub {sub a - b} - c}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a^b^c", "{pow a ^ {pow b ^ c}}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "-a^b", "{neg - {pow a ^ b}}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "-a*b", "{mul {neg - a} * b}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a--b", "{sub a - {neg - b}}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a!", "{fact a !}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a!!", "{fact {fact a !} !}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a+b!", "{add a + {fact b !}}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "(a+b)*c", "{mul {group ( {add a + b} )} * c}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "((a))", "{group ( {group ( a )} )}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "f(a,b)", "{call f ( a , b )}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "f()", "{call f ( )}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "f(a)(b)", "{call {call f ( a )} ( b )}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a[1][2]", "{index {index a [ 1 ]} [ 2 ]}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "-f(a)", "{neg - {call f ( a )}}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "f(a+b,c*d)", "{call f ( {add a + b} , {mul c * d} )}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a?b:c?d:e", "{cond a ? b : {cond c ? d : e}}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a?b+c:d+e", "{cond a ? {add b + c} : {add d + e}}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a?(b?c:d):e", "{cond a ? {group ( {cond b ? c : d} )} : e}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a<b", "{lt a < b}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a+b<c+d", "{lt {add a + b} < {add c + d}}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "[1,2,3]", "{list [ 1 , 2 , 3 ]}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "[]", "{list [ ]}", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "[1,2,]", "{list [ 1 , 2 , ]}", LPS_ACCEPTED);

  /* Errors: the status is never ACCEPTED, and the tree still covers the whole source. */
  l_tests_parsing_case(context, failures, process, "a<b<c", NULL, LPS_RECOVERED);
  l_tests_parsing_case(context, failures, process, "a+", NULL, LPS_REJECTED);
  l_tests_parsing_case(context, failures, process, "(a", NULL, LPS_REJECTED);
  l_tests_parsing_case(context, failures, process, "f(a,)", NULL, LPS_REJECTED);
  l_tests_parsing_case(context, failures, process, "()", NULL, LPS_REJECTED);
  l_tests_parsing_case(context, failures, process, "", NULL, LPS_REJECTED);

  logos_language_tooling_parsing_parser_process_destroy(process);
  l_tests_parsing_destroy(&parsing);
}

static void l_tests_parsing_mixed_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LTestsParsing             parsing;
  LParserProcess           *process;
  LTestsParsingResult       result;
  const LParserDiagnostic  *diagnostic;
  const LParserExpectation *expectation;
  uint_large                size;
  uint_large                i;
  boolean                   found_number;
  char                      message[256];

  l_tests_parsing_build(runtime, TRUE, &parsing);
  process = logos_language_tooling_parsing_parser_process_create(parsing.parser);

  l_tests_parsing_case(context, failures, process, "", "", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "let x = 1 + 2 * 3;", "let x = {add 1 + {mul 2 * 3}} ;", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "a+b;c*d;", "{add a + b} ; {mul c * d} ;", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "let   x =   a + b ;\n  f ( x , y ) ;  ", "let x = {add a + b} ; {call f ( x , y )} ;",
                       LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "  a ? b : c ;", "{cond a ? b : c} ;", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "let x = ( 1 + 2 ) * 3 ;", "let x = {mul {group ( {add 1 + 2} )} * 3} ;", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "let x = - a ! ;", "let x = {neg - {fact a !}} ;", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "let x = ;", NULL, LPS_REJECTED);
  l_tests_parsing_case(context, failures, process, "a + ;", NULL, LPS_REJECTED);
  l_tests_parsing_case(context, failures, process, "a +", NULL, LPS_REJECTED);

  /* The tree of an accepted source is lossless: the root covers every byte, and the Pratt part leaves no diagnostics. */
  {
    const char             *source;
    const LParserGreenNode *root;
    uint_large              root_width;

    source     = "let x = 1 + 2 ;\n  y * 3 ;";
    size       = nexus_strings_string_length_get(source);
    result     = l_tests_parsing_run(process, source, size, 0);
    root       = logos_language_tooling_parsing_syntax_tree_root_get(result.tree);
    root_width = logos_language_tooling_parsing_syntax_green_node_width_get(root);
    l_tests_parsing_check(context, failures,
                          result.status == LPS_ACCEPTED && root_width == size &&
                              logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(result.tree) == 0,
                          "accepted mixed source is lossless and diagnostic-free");
    logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  }

  /* A diagnostic inside a Pratt expression lists the terminals the table allowed there. */
  size   = nexus_strings_string_length_get("let x = ;");
  result = l_tests_parsing_run(process, "let x = ;", size, 0);
  l_tests_parsing_check(context, failures, logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(result.tree) >= 1,
                        "operand missing is diagnosed");

  if (logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(result.tree) >= 1) {
    diagnostic   = logos_language_tooling_parsing_syntax_tree_diagnostic_at(result.tree, 0);
    found_number = FALSE;

    for (i = 0; i < logos_language_tooling_parsing_syntax_diagnostic_expectation_count_get(diagnostic); i++) {
      expectation = logos_language_tooling_parsing_syntax_tree_diagnostic_expectation_at(result.tree, 0, i);

      if (logos_language_tooling_parsing_syntax_expectation_token_kind_get(expectation) == PT_NUM) {
        found_number = TRUE;
      }
    }

    l_tests_parsing_check(context, failures, found_number && logos_language_tooling_parsing_syntax_diagnostic_byte_offset_get(diagnostic) == 8,
                          "the diagnostic at ';' expects an operand such as a number");
    (void)logos_language_tooling_parsing_syntax_tree_diagnostic_message_write(result.tree, 0, message, NEXUS_SIZEOF(message));
    (void)echo_info(context, "  message: %s", message);
  }

  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);

  logos_language_tooling_parsing_parser_process_destroy(process);
  l_tests_parsing_destroy(&parsing);
}

/* Rules without any Pratt operation keep their behavior: ambiguity stays packed and a missing terminal is repaired. */
static void l_tests_parsing_earley_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LParserConfiguration     configuration;
  LLexer                  *lexer;
  LContexta               *contexta;
  LParser                 *parser;
  LParserProcess          *process;
  LContextaRule           *sum_rule;
  LContextaRule           *declaration_rule;
  LContextaOperation      *plus_branch;
  LContextaOperation      *tail;
  LTestsParsingResult      result;
  const LParserDiagnostic *diagnostic;
  LLexerTokenKind          whitespace;

  whitespace = PT_WS;
  lexer      = l_tests_parsing_lexer_build(runtime);
  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.allowed_tokens      = &whitespace;
  configuration.allowed_token_count = 1;

  /* Sum = Sum '+' Sum | number: ambiguous. Start rule chosen per case below. */
  contexta    = logos_languages_contexta_contexta_create("earley", NEXUS_VERSION_PACK(0, 0, 1, 0));
  sum_rule    = logos_languages_contexta_contexta_rule_add("Sum", contexta);
  plus_branch = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_reference(NULL, contexta, sum_rule),
      logos_languages_contexta_contexta_operation_add_concatenate(
          NULL, contexta, logos_languages_contexta_contexta_operation_add_token("plus", contexta, PT_PLUS),
          logos_languages_contexta_contexta_operation_add_reference(NULL, contexta, sum_rule)));
  logos_languages_contexta_contexta_rule_define(
      contexta, sum_rule,
      logos_languages_contexta_contexta_operation_add_unionize(NULL, contexta, plus_branch,
                                                               logos_languages_contexta_contexta_operation_add_token("number", contexta, PT_NUM)));
  logos_languages_contexta_contexta_start_rule_set(contexta, sum_rule);
  {
    NexusError *create_error;

    create_error = logos_language_tooling_parsing_parser_create(lexer, contexta, &configuration, &parser);

    if (create_error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(create_error);
    }
  }
  process = logos_language_tooling_parsing_parser_process_create(parser);

  l_tests_parsing_case(context, failures, process, "1+2+3", "1 + 2 + 3", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "1+", NULL, LPS_RECOVERED);

  result = l_tests_parsing_run(process, "1+2+3", 5, 0);
  l_tests_parsing_check(
      context, failures,
      logos_language_tooling_parsing_syntax_green_node_alternative_count_get(logos_language_tooling_parsing_syntax_alternative_child_get(
          result.tree,
          logos_language_tooling_parsing_syntax_green_node_alternative_first_get(result.tree,
                                                                                 logos_language_tooling_parsing_syntax_tree_root_get(result.tree)),
          0)) >= 1,
      "the Earley forest of an ambiguous sum keeps its structure");
  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);
  logos_languages_contexta_contexta_destroy(contexta);

  /* Declaration = 'let' name '=' number ';': "let x = ;" is repaired by inserting the missing number. */
  contexta         = logos_languages_contexta_contexta_create("repair", NEXUS_VERSION_PACK(0, 0, 1, 0));
  declaration_rule = logos_languages_contexta_contexta_rule_add("Declaration", contexta);
  tail             = logos_languages_contexta_contexta_operation_add_token("semicolon", contexta, PT_SEMI);
  tail             = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_token("number", contexta, PT_NUM), tail);
  tail = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_token("assign", contexta, PT_ASSIGN), tail);
  tail = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_token("identifier", contexta, PT_ID), tail);
  tail = logos_languages_contexta_contexta_operation_add_concatenate(
      NULL, contexta, logos_languages_contexta_contexta_operation_add_token("let", contexta, PT_LET), tail);
  logos_languages_contexta_contexta_rule_define(contexta, declaration_rule, tail);
  logos_languages_contexta_contexta_start_rule_set(contexta, declaration_rule);
  {
    NexusError *create_error;

    create_error = logos_language_tooling_parsing_parser_create(lexer, contexta, &configuration, &parser);

    if (create_error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(create_error);
    }
  }
  process = logos_language_tooling_parsing_parser_process_create(parser);

  l_tests_parsing_case(context, failures, process, "let x=1;", "let x = 1 ;", LPS_ACCEPTED);
  l_tests_parsing_case(context, failures, process, "let x=;", "let x = ;", LPS_RECOVERED);
  result     = l_tests_parsing_run(process, "let x=;", 7, 0);
  diagnostic = logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(result.tree) > 0
                   ? logos_language_tooling_parsing_syntax_tree_diagnostic_at(result.tree, 0)
                   : NULL;
  l_tests_parsing_check(context, failures,
                        diagnostic != NULL && logos_language_tooling_parsing_syntax_diagnostic_kind_get(diagnostic) == LPDK_MISSING_TOKEN,
                        "a missing terminal is diagnosed and inserted");
  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_language_tooling_parsing_parser_destroy(parser);
  logos_languages_contexta_contexta_destroy(contexta);
  logos_language_tooling_lexing_lexer_destroy(lexer);
}

/* A hostile or enormous expression must neither overflow the stack nor run out of structure. */
static void l_tests_parsing_scale_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LTestsParsing       parsing;
  LParserProcess     *process;
  LTestsParsingResult result;
  char               *source;
  uint_large          depth;
  uint_large          size;
  uint_large          i;

  l_tests_parsing_build(runtime, FALSE, &parsing);
  process = logos_language_tooling_parsing_parser_process_create(parsing.parser);

  depth  = 50000;
  size   = (depth * 2) + 1;
  source = (char *)nexus_memory_allocate(size + 1);

  for (i = 0; i < depth; i++) {
    source[i]            = '(';
    source[size - 1 - i] = ')';
  }

  source[depth] = 'a';
  source[size]  = '\0';
  result        = l_tests_parsing_run(process, source, size, 4096);
  l_tests_parsing_check(context, failures,
                        result.status == LPS_ACCEPTED && logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(result.tree) == 0,
                        "50000 nested groups parse without recursion");
  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  nexus_memory_deallocate(source);

  /* a+a+...+a, and the right-leaning a^a^...^a, whose reductions all wait until the end. */
  size   = 100001;
  source = (char *)nexus_memory_allocate(size + 1);

  for (i = 0; i < size; i++) {
    source[i] = i % 2 == 0 ? 'a' : '+';
  }

  source[size] = '\0';
  result       = l_tests_parsing_run(process, source, size, 0);
  l_tests_parsing_check(context, failures, result.status == LPS_ACCEPTED, "50001-term left-associative chain parses");
  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);

  for (i = 1; i < size; i += 2) {
    source[i] = '^';
  }

  result = l_tests_parsing_run(process, source, size, 0);
  l_tests_parsing_check(context, failures, result.status == LPS_ACCEPTED, "50001-term right-associative chain parses");
  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  nexus_memory_deallocate(source);

  logos_language_tooling_parsing_parser_process_destroy(process);
  l_tests_parsing_destroy(&parsing);
}

/* Contexta contract: a Pratt operation is not a byte language. */
static void l_tests_parsing_contexta_run(EchoContext *context, uint32 *failures) {
  LContexta           *contexta;
  const LContextaRule *rule;
  LRegula             *regula;
  NexusError          *error;

  contexta = l_tests_parsing_grammar_build(FALSE);
  rule     = logos_languages_contexta_contexta_start_rule_get(contexta);
  error    = logos_languages_contexta_contexta_to_regula(contexta, &regula);
  l_tests_parsing_check(context, failures, rule != NULL && error != NEXUS_PROGRAM_ERROR_SUCCESS(),
                        "a Pratt grammar does not convert to a byte Regula");
  logos_languages_contexta_contexta_destroy(contexta);
}

void logos_tests_parsing_run(LogosRuntime *runtime) {
  EchoContext *context;
  uint32       failures;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "runtime must not be NULL");

  context  = logos_runtime_echo_context_get(runtime);
  failures = 0;

  l_tests_parsing_expression_run(runtime, context, &failures);
  l_tests_parsing_earley_run(runtime, context, &failures);
  l_tests_parsing_mixed_run(runtime, context, &failures);
  l_tests_parsing_scale_run(runtime, context, &failures);
  l_tests_parsing_contexta_run(context, &failures);

  if (failures == 0) {
    (void)echo_info(context, "Parser: all checks passed.");
  } else {
    (void)echo_error(context, "Parser: %u check(s) FAILED.", (unsigned)failures);
  }
}
