#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "../logos_tests.h"

#ifndef LOGOS_GENESIS_GRAMMAR_DIR
#  define LOGOS_GENESIS_GRAMMAR_DIR "libs/logos/genesis"
#endif

/* ---------------------------------------------------------------------------- */
/* SUPPORT                                                                       */
/* ---------------------------------------------------------------------------- */

#define L_TESTS_GN_RENDER_MAX 4096

typedef struct LTestsGnRender {
  char       text[L_TESTS_GN_RENDER_MAX];
  uint_large length;
} LTestsGnRender;

static void l_tests_gn_check(EchoContext *context, uint32 *failures, boolean ok, const char *name) {
  if (ok) {
    (void)echo_info(context, "Genesis PASS: %s", name);
  } else {
    (void)echo_error(context, "Genesis FAIL: %s", name);
    (*failures)++;
  }
}

static void l_tests_gn_append(LTestsGnRender *render, const char *text, uint_large size) {
  uint_large i;

  for (i = 0; i < size && render->length + 1 < L_TESTS_GN_RENDER_MAX; i++) {
    render->text[render->length++] = text[i];
  }

  render->text[render->length] = '\0';
}

static boolean l_tests_gn_blank(const byte *text, uint_large size) {
  uint_large i;

  for (i = 0; i < size; i++) {
    if (text[i] != ' ' && text[i] != '\t' && text[i] != '\n' && text[i] != '\r') {
      return FALSE;
    }
  }

  return TRUE;
}

/* Tokens as text (blank tokens skipped), operator nodes as {label children}. */
static void l_tests_gn_render(const LParserTree *tree, const LParserGreenNode *node, LTestsGnRender *render) {
  const LParserGreenAlternative *alternative;
  const char                    *label;
  uint_large                     size;
  const byte                    *text;
  uint32                         i;
  boolean                        braces;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_TOKEN) {
    text = logos_language_tooling_parsing_syntax_green_node_text_get(tree, node, &size);

    if (size > 0 && !l_tests_gn_blank(text, size)) {
      l_tests_gn_append(render, " ", 1);
      l_tests_gn_append(render, (const char *)text, size);
    }

    return;
  }

  label  = logos_language_tooling_parsing_syntax_green_node_label_get(tree, node);
  braces = label != NULL && logos_language_tooling_parsing_syntax_green_node_operation_kind_get(tree, node) == LCOK_OPERATORS;

  if (braces) {
    l_tests_gn_append(render, " {", 2);
    l_tests_gn_append(render, label, nexus_strings_string_length_get(label));
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative != NULL) {
    for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
      l_tests_gn_render(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), render);
    }
  }

  if (braces) {
    l_tests_gn_append(render, "}", 1);
  }
}

typedef struct LTestsGnResult {
  LParserTree  *tree;
  LParserStatus status;
} LTestsGnResult;

static LTestsGnResult l_tests_gn_run(LParserProcess *process, const char *source, uint_large chunk_size) {
  LTestsGnResult result;
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

/* Every byte of the source is owned by exactly one token, in order. */
static boolean l_tests_gn_lossless(const LParserTree *tree, const char *source) {
  uint_large  count;
  uint_large  i;
  uint_large  expected_offset;
  LLexerToken token;
  uint_large  size;
  const byte *retained;

  count           = logos_language_tooling_parsing_syntax_tree_token_count_get(tree);
  expected_offset = 0;

  for (i = 0; i < count; i++) {
    if (logos_language_tooling_parsing_syntax_tree_token_at(tree, i, &token) != LLE_NONE) {
      return FALSE;
    }

    if (token.lexeme_offset != expected_offset) {
      return FALSE;
    }

    expected_offset += token.lexeme_width;
  }

  retained = logos_language_tooling_parsing_syntax_tree_source_get(tree, &size);

  return expected_offset == nexus_strings_string_length_get(source) && size == expected_offset &&
         nexus_memory_bytes_equal(retained, (const byte *)source, size);
}

/* Parses whole and in 1- and 5-byte chunks; they must agree on status and rendering, and the tree must be lossless. */
static void l_tests_gn_case(EchoContext *context, uint32 *failures, LParserProcess *process, const char *name, const char *source,
                            LParserStatus expected_status, const char *expected_render) {
  static const uint_large chunks[3] = {0, 1, 5};
  LTestsGnResult          result;
  LTestsGnRender          render;
  LTestsGnRender          reference;
  uint_large              i;
  boolean                 ok;

  reference.length  = 0;
  reference.text[0] = '\0';

  for (i = 0; i < 3; i++) {
    result         = l_tests_gn_run(process, source, chunks[i]);
    render.length  = 0;
    render.text[0] = '\0';
    l_tests_gn_render(result.tree, logos_language_tooling_parsing_syntax_tree_root_get(result.tree), &render);

    if (i == 0) {
      reference = render;
      ok        = result.status == expected_status &&
           (expected_render == NULL || nexus_strings_string_equals(render.length > 0 ? render.text + 1 : render.text, expected_render));

      if (expected_status == LPS_ACCEPTED) {
        ok = ok && l_tests_gn_lossless(result.tree, source);
      }

      l_tests_gn_check(context, failures, ok, name);

      if (!ok) {
        (void)echo_error(context, "  status %d (expected %d), rendered '%s', expected '%s'", (int)result.status, (int)expected_status, render.text,
                         expected_render != NULL ? expected_render : "-");
      }
    } else {
      ok = result.status == expected_status && nexus_strings_string_equals(render.text, reference.text);
      l_tests_gn_check(context, failures, ok, "chunked parse agrees with the whole parse");
    }

    logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  }
}

typedef struct LTestsGnEngine {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
} LTestsGnEngine;

static void l_tests_gn_diagnostics_print(EchoContext *context, const LGenesisCompiler *compiler) {
  char *text;

  text = logos_languages_genesis_compiler_diagnostics_render(compiler, FALSE);
  (void)echo_error(context, "%s", text);
  nexus_memory_deallocate(text);
}

static LGenesisCompiler *l_tests_gn_compiler(LogosRuntime *runtime) {
  LGenesisCompiler *compiler;

  compiler = logos_languages_genesis_compiler_create(runtime);
  logos_languages_genesis_compiler_search_folder_add(compiler, LOGOS_GENESIS_GRAMMAR_DIR);
  logos_languages_genesis_compiler_search_folder_add(compiler, LOGOS_GENESIS_STDLIB_DIR);

  return compiler;
}

static boolean l_tests_gn_has_code(const LGenesisCompiler *compiler, const char *code) {
  uint_large i;

  for (i = 0; i < logos_languages_genesis_compiler_diagnostic_count_get(compiler); i++) {
    if (nexus_strings_string_equals(pharos_diagnostics_code_get(logos_languages_genesis_compiler_diagnostic_at(compiler, i)), code)) {
      return TRUE;
    }
  }

  return FALSE;
}

/* ---------------------------------------------------------------------------- */
/* LANGUAGES                                                                     */
/* ---------------------------------------------------------------------------- */

static void l_tests_gn_json_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LParserProcess   *process;
  char             *path;
  uint_large        length;

  compiler = logos_languages_genesis_compiler_create(runtime);
  length   = nexus_strings_string_length_get(LOGOS_GENESIS_STDLIB_DIR) + 16;
  path     = (char *)nexus_memory_allocate(length);
  (void)stbsp_snprintf(path, (int)length, "%s/JSON.gen", LOGOS_GENESIS_STDLIB_DIR);
  engine = logos_languages_genesis_compiler_compile_file(compiler, path);
  nexus_memory_deallocate(path);

  if (engine == NULL) {
    l_tests_gn_diagnostics_print(context, compiler);
  }

  l_tests_gn_check(context, failures, engine != NULL && !logos_languages_genesis_compiler_has_errors(compiler), "JSON.gen compiles from a file");

  if (engine == NULL) {
    logos_languages_genesis_compiler_destroy(compiler);

    return;
  }

  l_tests_gn_check(context, failures,
                   nexus_strings_string_equals(logos_languages_genesis_engine_name_get(engine), "JSON") &&
                       nexus_strings_string_equals(logos_languages_genesis_engine_version_get(engine), "1.0.0") &&
                       logos_languages_genesis_engine_lexer_get(engine) != NULL && logos_languages_genesis_engine_parser_get(engine) != NULL,
                   "the engine exposes name, version, raw lexer and raw parser");

  process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));

  l_tests_gn_case(context, failures, process, "JSON: scalar", "42", LPS_ACCEPTED, "42");
  l_tests_gn_case(context, failures, process, "JSON: object and array", "{\"a\": [1, 2.5e3, true, false, null, \"x\"], \"b\": {}}", LPS_ACCEPTED,
                  NULL);
  l_tests_gn_case(context, failures, process, "JSON: whitespace everywhere", " \n[ 1 ,\t2 ]\r\n", LPS_ACCEPTED, NULL);
  l_tests_gn_case(context, failures, process, "JSON: string escapes", "[\"a\\n\\u00e9\\\"b\"]", LPS_ACCEPTED, NULL);
  l_tests_gn_case(context, failures, process, "JSON: whitespace inside strings is content", "[\" a  b \"]", LPS_ACCEPTED, NULL);
  l_tests_gn_case(context, failures, process, "JSON: non-ASCII string content", "[\"\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80\"]", LPS_ACCEPTED, NULL);
  l_tests_gn_case(context, failures, process, "JSON: trailing comma is rejected", "[1,]", LPS_REJECTED, NULL);
  l_tests_gn_case(context, failures, process, "JSON: leading zero is rejected", "[01]", LPS_REJECTED, NULL);
  l_tests_gn_case(context, failures, process, "JSON: missing value is rejected", "{\"a\":}", LPS_REJECTED, NULL);
  l_tests_gn_case(context, failures, process, "JSON: a control character in a string is rejected", "[\"a\tb\"]", LPS_REJECTED, NULL);

  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_languages_genesis_engine_release(engine);
  logos_languages_genesis_compiler_destroy(compiler);
}

static void l_tests_gn_calculator_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LParserProcess   *process;

  compiler = l_tests_gn_compiler(runtime);
  engine   = logos_languages_genesis_compiler_compile_file(compiler, LOGOS_GENESIS_GRAMMAR_DIR "/Calculator.gen");

  if (engine == NULL) {
    l_tests_gn_diagnostics_print(context, compiler);
  }

  l_tests_gn_check(context, failures, engine != NULL, "Calculator.gen compiles, importing the General library through the search folder");

  if (engine == NULL) {
    logos_languages_genesis_compiler_destroy(compiler);

    return;
  }

  l_tests_gn_check(context, failures, logos_languages_genesis_engine_import_count_get(engine) == 1, "the engine records its import");
  process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));

  l_tests_gn_case(context, failures, process, "Calculator: precedence", "2 * 3 + 4", LPS_ACCEPTED, "{plus {multiply 2 * 3} + 4}");
  l_tests_gn_case(context, failures, process, "Calculator: power is right associative", "2 ^ 3 ^ 4", LPS_ACCEPTED, "{power 2 ^ {power 3 ^ 4}}");
  l_tests_gn_case(context, failures, process, "Calculator: prefix below power, postfix above", "-2 ^ 3!", LPS_ACCEPTED,
                  "{minus - {power 2 ^ {factorial 3 !}}}");
  l_tests_gn_case(context, failures, process, "Calculator: parentheses", "(1 + 2) * 3", LPS_ACCEPTED, NULL);
  l_tests_gn_case(context, failures, process, "Calculator: let binds an expression", "let x = 1 + 2", LPS_ACCEPTED, NULL);
  l_tests_gn_case(context, failures, process, "Calculator: delimited integer list with trailing comma", "[1, 2, 3,]", LPS_ACCEPTED, NULL);
  l_tests_gn_case(context, failures, process, "Calculator: empty list", "[]", LPS_ACCEPTED, NULL);
  l_tests_gn_case(context, failures, process, "Calculator: a dangling operator is rejected", "1 +", LPS_REJECTED, NULL);

  logos_language_tooling_parsing_parser_process_destroy(process);
  logos_languages_genesis_engine_release(engine);
  logos_languages_genesis_compiler_destroy(compiler);
}

/* ---------------------------------------------------------------------------- */
/* PRECEDENCE OF THE EARLEY ENGINE                                               */
/* ---------------------------------------------------------------------------- */

static const char l_tests_gn_precedence_head[] = "Language: Precedence\n"
                                                 "Version: 0.0.1\n"
                                                 "PARSER {\n"
                                                 "  entry = #expr\n"
                                                 "  engine = earley\n"
                                                 "}\n"
                                                 "CONTEXT <ROOT> {\n"
                                                 "  $num = ('0'..'9')+\n"
                                                 "  $plus = '+'\n"
                                                 "  $minus = '-'\n"
                                                 "  $star = '*'\n"
                                                 "  $caret = '^'\n"
                                                 "  $equals = '='\n"
                                                 "}\n";

static const char l_tests_gn_precedence_rules[] = "#expr = #add || #sub || #mul || #power || #negate || #compare || $num\n"
                                                  "#add @prec(left, 10) = #expr $plus #expr\n"
                                                  "#sub @prec(left, 10) = #expr $minus #expr\n"
                                                  "#mul @prec(left, 20) = #expr $star #expr\n"
                                                  "#power @prec(right, 30) = #expr $caret #expr\n"
                                                  "#negate @prec(left, 40) = $minus #expr\n"
                                                  "#compare @prec(nonassoc, 5) = #expr $equals #expr\n";

/* Renders the first derivation as nested parentheses led by the rule's name (the transparent `expr` is left out); *ambiguous becomes TRUE when a node keeps several. */
static void l_tests_gn_precedence_render(const LParserTree *tree, const LParserGreenNode *node, LTestsGnRender *render, boolean *ambiguous) {
  const LParserGreenAlternative *alternative;
  const char                    *label;
  uint_large                     size;
  const byte                    *text;
  uint32                         i;
  boolean                        named;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_TOKEN) {
    text = logos_language_tooling_parsing_syntax_green_node_text_get(tree, node, &size);

    if (size > 0) {
      l_tests_gn_append(render, render->length > 0 && render->text[render->length - 1] != '(' ? " " : "", render->length > 0 ? 1 : 0);
      l_tests_gn_append(render, (const char *)text, size);
    }

    return;
  }

  if (logos_language_tooling_parsing_syntax_green_node_alternative_count_get(node) > 1) {
    *ambiguous = TRUE;
  }

  label = logos_language_tooling_parsing_syntax_green_node_label_get(tree, node);
  named = logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_RULE && label != NULL &&
          !nexus_strings_string_equals(label, "expr") && !nexus_strings_string_equals(label, "source");

  if (named) {
    if (render->length > 0) {
      l_tests_gn_append(render, " ", 1);
    }

    l_tests_gn_append(render, "(", 1);
    l_tests_gn_append(render, label, nexus_strings_string_length_get(label));
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative != NULL) {
    for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
      l_tests_gn_precedence_render(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), render, ambiguous);
    }
  }

  if (named) {
    l_tests_gn_append(render, ")", 1);
  }
}

static void l_tests_gn_precedence_case(EchoContext *context, uint32 *failures, LParserProcess *process, const char *source, const char *expected) {
  LTestsGnResult result;
  LTestsGnRender render;
  boolean        ambiguous;
  boolean        ok;

  result         = l_tests_gn_run(process, source, 0);
  render.length  = 0;
  render.text[0] = '\0';
  ambiguous      = FALSE;

  if (expected == NULL) {
    ok = result.status != LPS_ACCEPTED;
  } else {
    l_tests_gn_precedence_render(result.tree, logos_language_tooling_parsing_syntax_tree_root_get(result.tree), &render, &ambiguous);
    ok = result.status == LPS_ACCEPTED && !ambiguous && nexus_strings_string_equals(render.text, expected);
  }

  l_tests_gn_check(context, failures, ok, source);

  if (!ok) {
    (void)echo_error(context, "  status %d, rendered '%s'%s, expected '%s'", (int)result.status, render.text, ambiguous ? " (ambiguous)" : "",
                     expected != NULL ? expected : "no parse");
  }

  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
}

static void l_tests_gn_precedence_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LParserProcess   *process;
  char             *exported;

  compiler = l_tests_gn_compiler(runtime);
  logos_languages_genesis_compiler_chunk_begin(compiler, "precedence.gen");
  logos_languages_genesis_compiler_chunk_add(compiler, (const byte *)l_tests_gn_precedence_head,
                                             nexus_strings_string_length_get(l_tests_gn_precedence_head));
  logos_languages_genesis_compiler_chunk_add(compiler, (const byte *)l_tests_gn_precedence_rules,
                                             nexus_strings_string_length_get(l_tests_gn_precedence_rules));
  engine = logos_languages_genesis_compiler_chunk_finish(compiler);

  if (engine == NULL) {
    l_tests_gn_diagnostics_print(context, compiler);
  }

  l_tests_gn_check(context, failures, engine != NULL, "a grammar with @prec on rules compiles with the Earley engine");

  if (engine == NULL) {
    logos_languages_genesis_compiler_destroy(compiler);

    return;
  }

  process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));
  l_tests_gn_precedence_case(context, failures, process, "1+2*3", "(add 1 + (mul 2 * 3))");
  l_tests_gn_precedence_case(context, failures, process, "1*2+3", "(add (mul 1 * 2) + 3)");
  l_tests_gn_precedence_case(context, failures, process, "1+2-3", "(sub (add 1 + 2) - 3)");
  l_tests_gn_precedence_case(context, failures, process, "1-2-3", "(sub (sub 1 - 2) - 3)");
  l_tests_gn_precedence_case(context, failures, process, "2^3^4", "(power 2 ^ (power 3 ^ 4))");
  l_tests_gn_precedence_case(context, failures, process, "-1+2", "(add (negate - 1) + 2)");
  l_tests_gn_precedence_case(context, failures, process, "-1^2", "(power (negate - 1) ^ 2)");
  l_tests_gn_precedence_case(context, failures, process, "1+-2*3", "(add 1 + (mul (negate - 2) * 3))");
  l_tests_gn_precedence_case(context, failures, process, "1=2", "(compare 1 = 2)");
  l_tests_gn_precedence_case(context, failures, process, "1+2=3*4", "(compare (add 1 + 2) = (mul 3 * 4))");
  l_tests_gn_precedence_case(context, failures, process, "1=2=3", NULL);
  logos_language_tooling_parsing_parser_process_destroy(process);

  /* The Genesis text exported from the grammar keeps the precedences. */
  exported = logos_languages_genesis_engine_export_text(engine);
  l_tests_gn_check(context, failures, exported != NULL && nexus_strings_string_contains(exported, "@prec(right, 30)"),
                   "an exported grammar keeps the precedence of its rules");

  if (exported != NULL) {
    nexus_memory_deallocate(exported);
  }

  logos_languages_genesis_engine_release(engine);
  logos_languages_genesis_compiler_destroy(compiler);
}

/* ---------------------------------------------------------------------------- */
/* BOUNDED REPETITION                                                            */
/* ---------------------------------------------------------------------------- */

static const char l_tests_gn_bounded[] = "Language: Bounded\n"
                                         "Version: 0.0.1\n"
                                         "PARSER { entry = #list }\n"
                                         "CONTEXT <ROOT> {\n"
                                         "  $sep = ','\n"
                                         "  $word = ('a'..'z'){2,4}\n"
                                         "  $hex = '0x' ('0'..'9' || 'a'..'f'){1,}\n"
                                         "  $exact = 'Q'{3}\n"
                                         "}\n"
                                         "#list = #item ($sep #item){1,2}\n"
                                         "#item = $word || $hex || $exact\n";

static void l_tests_gn_bounded_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LParserProcess   *process;
  char             *exported;

  compiler = l_tests_gn_compiler(runtime);
  engine   = logos_languages_genesis_compiler_compile_text(compiler, "bounded.gen", l_tests_gn_bounded);

  if (engine == NULL) {
    l_tests_gn_diagnostics_print(context, compiler);
  }

  l_tests_gn_check(context, failures, engine != NULL, "bounded repetition {n}, {n,} and {n,m} compiles in tokens and in rules");

  if (engine != NULL) {
    process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));
    l_tests_gn_case(context, failures, process, "Bounded: a word of the longest length", "abcd,ef", LPS_ACCEPTED, NULL);
    l_tests_gn_case(context, failures, process, "Bounded: an open count", "ab,0x1f9", LPS_ACCEPTED, NULL);
    l_tests_gn_case(context, failures, process, "Bounded: an exact count", "QQQ,ab,cd", LPS_ACCEPTED, NULL);
    l_tests_gn_case(context, failures, process, "Bounded: one letter is below the minimum", "a,bc", LPS_REJECTED, NULL);
    l_tests_gn_case(context, failures, process, "Bounded: a word of five letters is past the maximum", "abcde,fg", LPS_REJECTED, NULL);
    l_tests_gn_case(context, failures, process, "Bounded: two Q are below the exact count", "QQ,ab", LPS_REJECTED, NULL);
    l_tests_gn_case(context, failures, process, "Bounded: a rule repeated at most twice", "ab,cd,ef,gh", LPS_REJECTED, NULL);
    logos_language_tooling_parsing_parser_process_destroy(process);

    exported = logos_languages_genesis_engine_export_text(engine);
    l_tests_gn_check(context, failures,
                     exported != NULL && nexus_strings_string_contains(exported, "{2,4}") && nexus_strings_string_contains(exported, "{1,}") &&
                         nexus_strings_string_contains(exported, "'Q'{3}") && nexus_strings_string_contains(exported, "($sep #item){1,2}"),
                     "an exported grammar writes the counts back");

    if (exported != NULL) {
      nexus_memory_deallocate(exported);
    }

    logos_languages_genesis_engine_release(engine);
  }

  logos_languages_genesis_compiler_destroy(compiler);

  /* A count that runs backwards, and one that is too large, are errors with a code. */
  compiler = l_tests_gn_compiler(runtime);
  engine   = logos_languages_genesis_compiler_compile_text(
      compiler, "backwards.gen", "Language: Backwards\nVersion: 0.0.1\nPARSER { entry = #a }\nCONTEXT <ROOT> { $x = 'a'{3,2} }\n#a = $x\n");
  l_tests_gn_check(context, failures, engine == NULL && l_tests_gn_has_code(compiler, "G0263"), "a minimum above the maximum is refused (G0263)");
  logos_languages_genesis_compiler_destroy(compiler);

  compiler = l_tests_gn_compiler(runtime);
  engine   = logos_languages_genesis_compiler_compile_text(
      compiler, "huge.gen", "Language: Huge\nVersion: 0.0.1\nPARSER { entry = #a }\nCONTEXT <ROOT> { $x = 'a'{70000} }\n#a = $x\n");
  l_tests_gn_check(context, failures, engine == NULL && l_tests_gn_has_code(compiler, "G0263"), "a count beyond 65535 is refused (G0263)");
  logos_languages_genesis_compiler_destroy(compiler);
}

/* ---------------------------------------------------------------------------- */
/* NEGATION AND THE WILDCARD                                                     */
/* ---------------------------------------------------------------------------- */

static const char l_tests_gn_negation[] = "Language: Negation\n"
                                          "Version: 0.0.1\n"
                                          "PARSER { entry = #list }\n"
                                          "CONTEXT <ROOT> {\n"
                                          "  $_digit = '0'..'9'\n"
                                          "  $str = '\"' (!'\"')* '\"'\n"
                                          "  $comment = '//' (!'\\n')*\n"
                                          "  $block = '/*' (!'*' || '*'+ !('*' || '/'))* '*'+ '/'\n"
                                          "  $nondigit = !$_digit\n"
                                          "  $any3 = 'x' .{2}\n"
                                          "  $nl = '\\n'\n"
                                          "}\n"
                                          "#list = ($str || $comment || $block || $nondigit || $any3 || $nl)+\n";

static void l_tests_gn_negation_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LParserProcess   *process;
  char             *exported;

  compiler = l_tests_gn_compiler(runtime);
  engine   = logos_languages_genesis_compiler_compile_text(compiler, "negation.gen", l_tests_gn_negation);

  if (engine == NULL) {
    l_tests_gn_diagnostics_print(context, compiler);
  }

  l_tests_gn_check(context, failures, engine != NULL, "negation `!x` and the wildcard `.` compile in tokens");

  if (engine != NULL) {
    process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));
    l_tests_gn_case(context, failures, process, "Negation: a string of any other characters, beyond ASCII too", "\"h\xC3\xA9llo w\xC3\xB6rld\"",
                    LPS_ACCEPTED, NULL);
    l_tests_gn_case(context, failures, process, "Negation: a comment to the end of the line", "// to the end\nq", LPS_ACCEPTED, NULL);
    l_tests_gn_case(context, failures, process, "Negation: a block comment with stars inside", "/* a * b ** c */", LPS_ACCEPTED, NULL);
    l_tests_gn_case(context, failures, process, "Negation: a fragment negated", "q", LPS_ACCEPTED, NULL);
    l_tests_gn_case(context, failures, process, "Negation: the negated digit is no digit", "5", LPS_REJECTED, NULL);
    l_tests_gn_case(context, failures, process, "Wildcard: any two characters after x", "x\xC3\xA9z", LPS_ACCEPTED, NULL);
    logos_language_tooling_parsing_parser_process_destroy(process);

    exported = logos_languages_genesis_engine_export_text(engine);
    l_tests_gn_check(context, failures,
                     exported != NULL && nexus_strings_string_contains(exported, "(!'\"')*") && nexus_strings_string_contains(exported, "!$_digit") &&
                         nexus_strings_string_contains(exported, "'x' .{2}"),
                     "an exported grammar writes ! and . back");

    if (exported != NULL) {
      nexus_memory_deallocate(exported);
    }

    logos_languages_genesis_engine_release(engine);
  }

  logos_languages_genesis_compiler_destroy(compiler);

  compiler = l_tests_gn_compiler(runtime);
  engine   = logos_languages_genesis_compiler_compile_text(
      compiler, "badnegation.gen", "Language: Bad\nVersion: 0.0.1\nPARSER { entry = #a }\nCONTEXT <ROOT> { $x = !'ab' }\n#a = $x\n");
  l_tests_gn_check(context, failures, engine == NULL && l_tests_gn_has_code(compiler, "G0264"),
                   "a negated string of several characters is refused (G0264)");
  logos_languages_genesis_compiler_destroy(compiler);
}

static void l_tests_gn_choice_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LParserProcess   *process;

  compiler = l_tests_gn_compiler(runtime);
  engine   = logos_languages_genesis_compiler_compile_file(compiler, LOGOS_GENESIS_GRAMMAR_DIR "/ChoiceDemonstration.gen");

  if (engine == NULL) {
    l_tests_gn_diagnostics_print(context, compiler);
  }

  l_tests_gn_check(context, failures, engine != NULL, "ChoiceDemonstration.gen compiles");

  if (engine != NULL) {
    process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));
    l_tests_gn_case(context, failures, process, "Choice: LONGEST takes the whole input", "ab", LPS_ACCEPTED, "a b");
    l_tests_gn_case(context, failures, process, "Choice: a single token", "a", LPS_ACCEPTED, "a");
    logos_language_tooling_parsing_parser_process_destroy(process);
    logos_languages_genesis_engine_release(engine);
  }

  /* The unreachable alternatives warn, and warnings are not errors. */
  l_tests_gn_check(context, failures, l_tests_gn_has_code(compiler, "W0304") || l_tests_gn_has_code(compiler, "W0303"),
                   "ChoiceDemonstration warns about the unused or shadowed rules");
  logos_languages_genesis_compiler_destroy(compiler);
}

static void l_tests_gn_file_language_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures, const char *file, const char *label,
                                         const char *const *accepted, uint_large accepted_count, const char *const *rejected,
                                         uint_large rejected_count) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LParserProcess   *process;
  uint_large        i;

  compiler = l_tests_gn_compiler(runtime);
  engine   = logos_languages_genesis_compiler_compile_file(compiler, file);

  if (engine == NULL) {
    l_tests_gn_diagnostics_print(context, compiler);
  }

  l_tests_gn_check(context, failures, engine != NULL, label);

  if (engine != NULL) {
    process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));

    for (i = 0; i < accepted_count; i++) {
      l_tests_gn_case(context, failures, process, label, accepted[i], LPS_ACCEPTED, NULL);
    }

    for (i = 0; i < rejected_count; i++) {
      l_tests_gn_case(context, failures, process, label, rejected[i], LPS_REJECTED, NULL);
    }

    logos_language_tooling_parsing_parser_process_destroy(process);
    logos_languages_genesis_engine_release(engine);
  }

  logos_languages_genesis_compiler_destroy(compiler);
}

static void l_tests_gn_toml_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  static const char *const accepted[] = {"a = 1",
                                         "a = 1\nb = \"x\"\n",
                                         "# comment\n[table]\nk = [1, 2,\n 3,\n]\n",
                                         "x = 2024-01-01\ny = 2024-01-01T10:20:30Z\nz = 10:20:30",
                                         "a.b = { c = 1.5, d = \"q\", }",
                                         "key = true # trailing\n",
                                         "[[servers.alpha]]\nport = 0x1F\n",
                                         "1234 = 1234\n",
                                         "s = \"\"\"multi\nline\"\"\"\nt = 'lit'\n"};
  static const char *const rejected[] = {"a =", "= 1", "a = [1,, 2]", "[table", "a = 1 b = 2"};

  l_tests_gn_file_language_run(runtime, context, failures, LOGOS_GENESIS_STDLIB_DIR "/TOML.gen", "TOML (grammar_expected)", accepted,
                               NEXUS_ARRAY_SIZE_ELEMENTS(accepted), rejected, NEXUS_ARRAY_SIZE_ELEMENTS(rejected));
}

static void l_tests_gn_embedding_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  static const char *const markdown[]     = {"Notes:\n\n```json\n{\"enabled\": true, \"count\": 3}\n```\n\n```sql\nSELECT id FROM users;\n```\n",
                                             "plain\n", "```json\n```\n"};
  static const char *const markdown_bad[] = {"```json\nnever closed\n"};
  static const char *const python[]       = {"sql(r\"\"\"SELECT 1\"\"\")\njson(r\"\"\"{\"x\":1}\"\"\")", "sql(r\"\"\"\"\"\")"};
  static const char *const python_bad[]   = {"sql(\"SELECT 1\")"};

  l_tests_gn_file_language_run(runtime, context, failures, LOGOS_GENESIS_GRAMMAR_DIR "/MarkdownEmbedded.gen",
                               "Markdown with embedded JSON and SQL regions", markdown, NEXUS_ARRAY_SIZE_ELEMENTS(markdown), markdown_bad,
                               NEXUS_ARRAY_SIZE_ELEMENTS(markdown_bad));
  l_tests_gn_file_language_run(runtime, context, failures, LOGOS_GENESIS_GRAMMAR_DIR "/PythonEmbeddedSubset.gen",
                               "Python subset with embedded token content", python, NEXUS_ARRAY_SIZE_ELEMENTS(python), python_bad,
                               NEXUS_ARRAY_SIZE_ELEMENTS(python_bad));
}

/* ---------------------------------------------------------------------------- */
/* COMPILER API                                                                  */
/* ---------------------------------------------------------------------------- */

static const char l_tests_gn_tiny[] = "Language: Tiny\n"
                                      "Version: 0.0.1\n"
                                      "PARSER { entry = #list }\n"
                                      "CONTEXT <ROOT> {\n"
                                      "  $ws = (' ')+\n"
                                      "  $word = ('a'..'z')+\n"
                                      "}\n"
                                      "#list = $word ($ws $word)*\n";

static void l_tests_gn_api_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *text_engine;
  LGenesisEngine   *string_engine;
  LGenesisEngine   *chunk_engine;
  LParserProcess   *process;
  uint_large        i;
  uint_large        length;

  compiler = logos_languages_genesis_compiler_create(runtime);
  l_tests_gn_check(context, failures, logos_languages_genesis_compiler_search_folder_count_get(compiler) == 0,
                   "a new compiler has no search folders");
  logos_languages_genesis_compiler_search_folder_add(compiler, "first");
  logos_languages_genesis_compiler_search_folder_add(compiler, LOGOS_GENESIS_GRAMMAR_DIR);
  l_tests_gn_check(context, failures,
                   logos_languages_genesis_compiler_search_folder_count_get(compiler) == 2 &&
                       nexus_strings_string_equals(logos_languages_genesis_compiler_search_folder_at(compiler, 0), "first"),
                   "search folders keep their order");

  text_engine = logos_languages_genesis_compiler_compile_text(compiler, "tiny_text.gen", l_tests_gn_tiny);
  l_tests_gn_check(context, failures, text_engine != NULL, "compile_text compiles a NUL-terminated source");

  length        = nexus_strings_string_length_get(l_tests_gn_tiny);
  string_engine = logos_languages_genesis_compiler_compile_string(compiler, "tiny_string.gen", (const byte *)l_tests_gn_tiny, length);
  l_tests_gn_check(context, failures, string_engine != NULL, "compile_string compiles bytes");

  logos_languages_genesis_compiler_chunk_begin(compiler, "tiny_chunks.gen");

  for (i = 0; i < length; i += 7) {
    logos_languages_genesis_compiler_chunk_add(compiler, (const byte *)l_tests_gn_tiny + i, length - i < 7 ? length - i : 7);
  }

  chunk_engine = logos_languages_genesis_compiler_chunk_finish(compiler);
  l_tests_gn_check(context, failures, chunk_engine != NULL, "a chunked source compiles");

  if (text_engine != NULL && string_engine != NULL && chunk_engine != NULL) {
    LGenesisEngine *engines[3];
    uint_large      index;

    engines[0] = text_engine;
    engines[1] = string_engine;
    engines[2] = chunk_engine;

    for (index = 0; index < 3; index++) {
      process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engines[index]));
      l_tests_gn_case(context, failures, process, "the three entry points build the same language", "ab cd e", LPS_ACCEPTED, "ab cd e");
      logos_language_tooling_parsing_parser_process_destroy(process);
    }
  }

  if (text_engine != NULL) {
    logos_languages_genesis_engine_release(text_engine);
  }

  if (string_engine != NULL) {
    logos_languages_genesis_engine_release(string_engine);
  }

  if (chunk_engine != NULL) {
    logos_languages_genesis_engine_release(chunk_engine);
  }

  /* A source compiled earlier is found by its file stem and by its declared name, without searching folders. */
  {
    LGenesisEngine *by_stem;
    LGenesisEngine *by_name;

    by_stem = logos_languages_genesis_compiler_compile_text(
        compiler, "user_of_stem.gen", "Language: ByStem\nPARSER { entry = #a }\nIMPORT tiny_text { #list }\nCONTEXT <ROOT> { $x = 'x' }\n#a = $x\n");
    if (by_stem == NULL) {
      l_tests_gn_diagnostics_print(context, compiler);
    }

    l_tests_gn_check(context, failures, by_stem != NULL, "an import resolves by the file stem of an earlier source");

    by_name = logos_languages_genesis_compiler_compile_text(
        compiler, "user_of_name.gen", "Language: ByName\nPARSER { entry = #a }\nIMPORT Tiny { #list }\nCONTEXT <ROOT> { $x = 'x' }\n#a = $x\n");
    l_tests_gn_check(context, failures, by_name != NULL, "an import resolves by the declared name of an earlier source");

    if (by_stem != NULL) {
      logos_languages_genesis_engine_release(by_stem);
    }

    if (by_name != NULL) {
      logos_languages_genesis_engine_release(by_name);
    }
  }

  logos_languages_genesis_compiler_destroy(compiler);
}

/* ---------------------------------------------------------------------------- */
/* DIAGNOSTICS                                                                   */
/* ---------------------------------------------------------------------------- */

typedef struct LTestsGnBad {
  const char *name;
  const char *source;
  const char *code;
  const char *snippet; /* Must occur in the rendered text. */
} LTestsGnBad;

static const LTestsGnBad l_tests_gn_bad[] = {
    {"missing header", "CONTEXT <ROOT> { }\n", "G0005", "a source must begin with"},
    {"unterminated string", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $x = 'abc\n}\n#a = $x\n", "G0002", "unterminated string"},
    {"unknown token", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w' }\n#a = $wrod\n", "G0240", "did you mean `$word`?"},
    {"unknown rule", "Language: L\nPARSER { entry = #doc }\nCONTEXT <ROOT> { $word = 'w' }\n#document = $word\n", "G0222",
     "did you mean `#document`?"},
    {"missing parser", "Language: L\nCONTEXT <ROOT> { $word = 'w' }\n#a = $word\n", "G0220", "PARSER { entry"},
    {"missing entry", "Language: L\nPARSER { global = #a }\nCONTEXT <ROOT> { $word = 'w' }\n#a = $word\n", "G0221", "entry = #document"},
    {"unknown parser setting", "Language: L\nPARSER { entry = #a  entyr = #a }\nCONTEXT <ROOT> { $word = 'w' }\n#a = $word\n", "G0226",
     "did you mean `entry`?"},
    {"no root context", "Language: L\nPARSER { entry = #a }\nCONTEXT <OTHER> { $word = 'w' }\n#a = $word\n", "G0230", "ROOT"},
    {"unknown context action", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w' -> pusj(X) }\n#a = $word\n", "G0003",
     "did you mean `push`?"},
    {"unknown context",
     "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w' -> push(STRIGN) }\nCONTEXT <STRING> { $q = 'q' }\n#a = $word $q\n", "G0234",
     "did you mean `STRING`?"},
    {"empty token", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w'* }\n#a = $word\n", "G0233", "empty string"},
    {"token in lexical definition", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $one = '1'  $two = $one '2' }\n#a = $two\n", "G0237",
     "$_one"},
    {"fragment cycle", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $_x = 'a' $_y  $_y = 'b' $_x  $t = $_x }\n#a = $t\n", "G0232", "itself"},
    {"literal in a rule", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w' }\n#a = $word 'x'\n", "G0243", "declare a token"},
    {"left recursion", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w'  $plus = '+' }\n#a = #a $plus $word || $word\n", "G0244",
     "left-recursive"},
    {"nullable repetition", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w' }\n#a = ($word?)*\n", "G0245", "never end"},
    {"nullable global", "Language: L\nPARSER { entry = #a  global = #ws }\nCONTEXT <ROOT> { $word = 'w'  $sp = ' ' }\n#ws = $sp*\n#a = $word\n",
     "G0223", "non-nullable"},
    {"unknown annotation", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w' }\n#a @choise(ChoiceMode.LONGEST) = $word\n", "G0249",
     "did you mean `@choice`?"},
    {"unknown choice mode", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w' }\n#a @choice(ChoiceMode.LONGST) = $word\n", "G0224",
     "did you mean `ChoiceMode.LONGEST`?"},
    {"duplicate token", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w'  $word = 'v' }\n#a = $word\n", "G0201",
     "first defined here"},
    {"template not found", "Language: L\nPARSER { entry = #a }\nCONTEXT <ROOT> { $word = 'w' }\n#a = #nothing(x: $word)\n", "G0301",
     "import it from a library"},
    {"import not found", "Language: L\nPARSER { entry = #a }\nIMPORT NoSuchLibrary { #x }\nCONTEXT <ROOT> { $word = 'w' }\n#a = $word\n", "G0100",
     "NoSuchLibrary"},
    {"library with a rule", "Library: Lib\n#a = $b\n", "G0101", "library cannot declare"},
};

static void l_tests_gn_diagnostics_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  uint_large index;

  for (index = 0; index < NEXUS_ARRAY_SIZE_ELEMENTS(l_tests_gn_bad); index++) {
    LGenesisCompiler *compiler;
    LGenesisEngine   *engine;
    char             *rendered;
    boolean           ok;

    compiler = l_tests_gn_compiler(runtime);
    engine   = logos_languages_genesis_compiler_compile_text(compiler, "bad.gen", l_tests_gn_bad[index].source);
    rendered = logos_languages_genesis_compiler_diagnostics_render(compiler, FALSE);
    ok       = engine == NULL && l_tests_gn_has_code(compiler, l_tests_gn_bad[index].code) &&
         nexus_strings_string_contains(rendered, l_tests_gn_bad[index].snippet);
    l_tests_gn_check(context, failures, ok, l_tests_gn_bad[index].name);

    if (!ok) {
      (void)echo_error(context, "  expected %s containing '%s'; got:\n%s", l_tests_gn_bad[index].code, l_tests_gn_bad[index].snippet, rendered);
    }

    nexus_memory_deallocate(rendered);

    if (engine != NULL) {
      logos_languages_genesis_engine_release(engine);
    }

    logos_languages_genesis_compiler_destroy(compiler);
  }
}

/* ---------------------------------------------------------------------------- */
/* GRAMMAR DESCRIPTION AND EXPORT                                                */
/* ---------------------------------------------------------------------------- */

static boolean l_tests_gn_status_of(LGenesisEngine *engine, const char *source) {
  LParserProcess *process;
  LTestsGnResult  result;
  boolean         accepted;

  process  = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));
  result   = l_tests_gn_run(process, source, 0);
  accepted = result.status == LPS_ACCEPTED;
  logos_language_tooling_parsing_syntax_tree_destroy(result.tree);
  logos_language_tooling_parsing_parser_process_destroy(process);

  return accepted;
}

static void l_tests_gn_export_one(LogosRuntime *runtime, EchoContext *context, uint32 *failures, const char *file, const char *const *samples,
                                  uint_large sample_count) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *original;
  LGenesisEngine   *again;
  char             *text;
  uint_large        i;
  boolean           ok;

  compiler = l_tests_gn_compiler(runtime);
  original = logos_languages_genesis_compiler_compile_file(compiler, file);
  ok       = original != NULL;
  again    = NULL;
  text     = NULL;

  if (ok) {
    text  = logos_languages_genesis_engine_export_text(original);
    again = logos_languages_genesis_compiler_compile_text(compiler, "exported.gen", text);
    ok    = again != NULL;

    if (!ok) {
      (void)echo_error(context, "exported text of %s does not compile:\n%s", file, text);
      l_tests_gn_diagnostics_print(context, compiler);
    }
  }

  for (i = 0; ok && i < sample_count; i++) {
    ok = l_tests_gn_status_of(original, samples[i]) == l_tests_gn_status_of(again, samples[i]);
  }

  l_tests_gn_check(context, failures, ok, file);

  if (text != NULL) {
    nexus_memory_deallocate(text);
  }

  if (again != NULL) {
    logos_languages_genesis_engine_release(again);
  }

  if (original != NULL) {
    logos_languages_genesis_engine_release(original);
  }

  logos_languages_genesis_compiler_destroy(compiler);
}

static void l_tests_gn_grammar_run(LogosRuntime *runtime, EchoContext *context, uint32 *failures) {
  static const char *const json[]       = {"[1, 2.5, true]", "{\"a\": null}", "[1,]", "{"};
  static const char *const toml[]       = {"a = 1\n", "[t]\nk = [1, 2,\n]\n", "a =", "x = 2024-01-01"};
  static const char *const calculator[] = {"2 * 3 + 4", "let x = 1", "[1, 2,]", "1 +"};
  static const char *const markdown[]   = {"a\n```json\n[1]\n```\n", "```json\nx\n"};
  static const char *const choice[]     = {"ab", "a"};
  static const char *const python[]     = {"sql(r\"\"\"SELECT 1\"\"\")", "sql(1)"};
  LGenesisCompiler        *compiler;
  LGenesisEngine          *engine;
  const LGenesisGrammar   *grammar;
  uint32                   i;
  boolean                  found_true;
  boolean                  found_number;

  compiler = l_tests_gn_compiler(runtime);
  engine   = logos_languages_genesis_compiler_compile_file(compiler, LOGOS_GENESIS_STDLIB_DIR "/JSON.gen");
  grammar  = engine != NULL ? logos_languages_genesis_engine_grammar_get(engine) : NULL;

  l_tests_gn_check(context, failures,
                   grammar != NULL && nexus_strings_string_equals(logos_languages_genesis_grammar_entry_get(grammar), "document") &&
                       logos_languages_genesis_grammar_global_count_get(grammar) == 1 &&
                       logos_languages_genesis_grammar_context_count_get(grammar) == 3,
                   "the grammar description names the entry, the globals and the contexts");

  found_true   = FALSE;
  found_number = FALSE;

  for (i = 0; grammar != NULL && i < logos_languages_genesis_grammar_token_count_get(grammar); i++) {
    const LGenesisGrammarToken *token;

    token = logos_languages_genesis_grammar_token_at(grammar, i);

    if (nexus_strings_string_equals(token->name, "true")) {
      found_true = token->literal != NULL && nexus_strings_string_equals(token->literal, "true") && token->priority == 10;
    }

    if (nexus_strings_string_equals(token->name, "number")) {
      found_number = token->literal == NULL && nexus_strings_string_contains(token->regexp, "[0-9]") && !token->fragment;
    }
  }

  l_tests_gn_check(context, failures, found_true && found_number, "tokens carry priority, literal text and an Oniguruma regexp");

  if (engine != NULL) {
    logos_languages_genesis_engine_release(engine);
  }

  logos_languages_genesis_compiler_destroy(compiler);

  l_tests_gn_export_one(runtime, context, failures, LOGOS_GENESIS_STDLIB_DIR "/JSON.gen", json, NEXUS_ARRAY_SIZE_ELEMENTS(json));
  l_tests_gn_export_one(runtime, context, failures, LOGOS_GENESIS_STDLIB_DIR "/TOML.gen", toml, NEXUS_ARRAY_SIZE_ELEMENTS(toml));
  l_tests_gn_export_one(runtime, context, failures, LOGOS_GENESIS_GRAMMAR_DIR "/Calculator.gen", calculator, NEXUS_ARRAY_SIZE_ELEMENTS(calculator));
  l_tests_gn_export_one(runtime, context, failures, LOGOS_GENESIS_GRAMMAR_DIR "/MarkdownEmbedded.gen", markdown, NEXUS_ARRAY_SIZE_ELEMENTS(markdown));
  l_tests_gn_export_one(runtime, context, failures, LOGOS_GENESIS_GRAMMAR_DIR "/ChoiceDemonstration.gen", choice, NEXUS_ARRAY_SIZE_ELEMENTS(choice));
  l_tests_gn_export_one(runtime, context, failures, LOGOS_GENESIS_GRAMMAR_DIR "/PythonEmbeddedSubset.gen", python, NEXUS_ARRAY_SIZE_ELEMENTS(python));
}

/* ---------------------------------------------------------------------------- */
/* ENTRY                                                                         */
/* ---------------------------------------------------------------------------- */

void logos_tests_genesis_run(LogosRuntime *runtime) {
  EchoContext *context;
  uint32       failures;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "runtime must not be NULL");

  context  = logos_runtime_echo_context_get(runtime);
  failures = 0;

  l_tests_gn_api_run(runtime, context, &failures);
  l_tests_gn_json_run(runtime, context, &failures);
  l_tests_gn_calculator_run(runtime, context, &failures);
  l_tests_gn_choice_run(runtime, context, &failures);
  l_tests_gn_precedence_run(runtime, context, &failures);
  l_tests_gn_bounded_run(runtime, context, &failures);
  l_tests_gn_negation_run(runtime, context, &failures);
  l_tests_gn_toml_run(runtime, context, &failures);
  l_tests_gn_embedding_run(runtime, context, &failures);
  l_tests_gn_diagnostics_run(runtime, context, &failures);
  l_tests_gn_grammar_run(runtime, context, &failures);

  if (failures == 0) {
    (void)echo_info(context, "Genesis: all checks passed.");
  } else {
    (void)echo_error(context, "Genesis: %u check(s) FAILED.", (unsigned)failures);
  }
}
