#include <nexus.h>
#include <echo.h>

#include <logos.h>

#include <csv_bindings.h>
#include <json_bindings.h>
#include <rf_bindings.h>
#include <toml_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/* The grammars, embedded by cmake/RuleforgeEmbed.cmake: Ruleforge, its library, and the formats datasets and settings use. */
extern const unsigned char ruleforge_data_General[];
extern const unsigned long ruleforge_data_General_size;
extern const unsigned char ruleforge_data_ruleforge[];
extern const unsigned long ruleforge_data_ruleforge_size;
extern const unsigned char ruleforge_data_RuleforgeCsv[];
extern const unsigned long ruleforge_data_RuleforgeCsv_size;
extern const unsigned char ruleforge_data_JSON[];
extern const unsigned long ruleforge_data_JSON_size;
extern const unsigned char ruleforge_data_TOML[];
extern const unsigned long ruleforge_data_TOML_size;

/* ---------------------------------------------------------------------------- */
/* GRAMMARS                                                                     */
/* ---------------------------------------------------------------------------- */

/*
Summary: the compiled engine of a grammar, compiled on first use.
Description: a failure here is a build defect (an embedded grammar that does not compile, or generated bindings that no longer
  match it), so it panics with the diagnostics instead of returning.
*/
static LGenesisEngine *r_syntax_engine_get(RuleforgeRuntime *runtime, RuleforgeSyntaxKind kind) {
  LGenesisEngine *library;
  const byte     *text;
  uint_large      size;
  const char     *name;
  boolean         matches;
  char           *rendered;

  if (runtime->grammar_engines[kind] != NULL) {
    return runtime->grammar_engines[kind];
  }

  if (runtime->grammar_compiler == NULL) {
    runtime->grammar_compiler = logos_languages_genesis_compiler_create(runtime->logos);
    library                   = logos_languages_genesis_compiler_compile_string(runtime->grammar_compiler, "General.gen", ruleforge_data_General,
                                                                                (uint_large)ruleforge_data_General_size);

    if (library == NULL) {
      rendered = logos_languages_genesis_compiler_diagnostics_render(runtime->grammar_compiler, FALSE);
      NEXUS_PROGRAM_PANIC("ruleforge: embedded General.gen does not compile:\n%s", rendered);
    }

    /* The compiler keeps the library by name, so later grammars find their import without a search folder. */
    logos_languages_genesis_engine_release(library);
  }

  switch (kind) {
  case RSK_JSON:
    text = ruleforge_data_JSON;
    size = (uint_large)ruleforge_data_JSON_size;
    name = "JSON.gen";
    break;
  case RSK_CSV:
    text = ruleforge_data_RuleforgeCsv;
    size = (uint_large)ruleforge_data_RuleforgeCsv_size;
    name = "RuleforgeCsv.gen";
    break;
  case RSK_TOML:
    text = ruleforge_data_TOML;
    size = (uint_large)ruleforge_data_TOML_size;
    name = "TOML.gen";
    break;
  default:
    text = ruleforge_data_ruleforge;
    size = (uint_large)ruleforge_data_ruleforge_size;
    name = "ruleforge.gen";
    break;
  }

  runtime->grammar_engines[kind] = logos_languages_genesis_compiler_compile_string(runtime->grammar_compiler, name, text, size);

  if (runtime->grammar_engines[kind] == NULL) {
    rendered = logos_languages_genesis_compiler_diagnostics_render(runtime->grammar_compiler, FALSE);
    NEXUS_PROGRAM_PANIC("ruleforge: embedded %s does not compile:\n%s", name, rendered);
  }

  /* The generated bindings and the embedded grammar come from the same file; a mismatch is a stale build. */
  switch (kind) {
  case RSK_JSON:
    matches = json_grammar_matches(runtime->grammar_engines[kind]);
    break;
  case RSK_CSV:
    matches = csv_grammar_matches(runtime->grammar_engines[kind]);
    break;
  case RSK_TOML:
    matches = toml_grammar_matches(runtime->grammar_engines[kind]);
    break;
  default:
    matches = rf_grammar_matches(runtime->grammar_engines[kind]);
    break;
  }

  if (!matches) {
    NEXUS_PROGRAM_PANIC("ruleforge: the generated bindings of %s do not match the embedded grammar; rebuild", name);
  }

  return runtime->grammar_engines[kind];
}

/* ---------------------------------------------------------------------------- */
/* PARSING                                                                      */
/* ---------------------------------------------------------------------------- */

LParserTree *ruleforge_syntax_text_parse(RuleforgeRuntime *runtime, RuleforgeSyntaxKind kind, const byte *text, uint_large size, char *error,
                                         uint_large error_size) {
  LParserProcess *process;
  LParserTree    *tree;
  LParserStatus   status;
  NexusError     *chunk_error;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && text != NULL && error != NULL, "ruleforge_syntax_text_parse: NULL argument");

  error[0] = '\0';
  process  = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(r_syntax_engine_get(runtime, kind)));

  chunk_error = logos_language_tooling_parsing_parser_process_chunk_add(process, text, size, TRUE);

  if (chunk_error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    logos_language_tooling_parsing_parser_process_destroy(process);
    (void)stbsp_snprintf(error, (int)error_size, "the text is too large to parse");

    return NULL;
  }

  do {
    status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
  } while (status == LPS_PROGRESS);

  tree = logos_language_tooling_parsing_parser_process_tree_take(process);
  logos_language_tooling_parsing_parser_process_destroy(process);

  if (status != LPS_ACCEPTED) {
    if (logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(tree) > 0) {
      (void)logos_language_tooling_parsing_syntax_tree_diagnostic_message_write(tree, 0, error, error_size);
    } else {
      (void)stbsp_snprintf(error, (int)error_size, "the text could not be parsed");
    }

    logos_language_tooling_parsing_syntax_tree_destroy(tree);

    return NULL;
  }

  return tree;
}

LParserTree *ruleforge_syntax_parse(RuleforgeCompilation *compilation, uint32 file_index) {
  RuleforgeFile  *file;
  LParserProcess *process;
  LParserTree    *tree;
  LParserStatus   status;
  NexusError     *error;
  uint_large      diagnostic_count;
  uint_large      i;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_syntax_parse: NULL compilation");

  file    = ruleforge_source_get(compilation, file_index);
  process = logos_language_tooling_parsing_parser_process_create(
      logos_languages_genesis_engine_parser_get(r_syntax_engine_get(compilation->runtime, RSK_RULEFORGE)));
  error = logos_language_tooling_parsing_parser_process_chunk_add(process, file->text, file->size, TRUE);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    logos_language_tooling_parsing_parser_process_destroy(process);
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0002", ruleforge_span_make(file_index, 0, 0), NULL, "the source is too large to parse");

    return NULL;
  }

  do {
    status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
  } while (status == LPS_PROGRESS);

  tree             = logos_language_tooling_parsing_parser_process_tree_take(process);
  diagnostic_count = logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(tree);

  for (i = 0; i < diagnostic_count; i++) {
    const LParserDiagnostic *diagnostic;
    char                     message[512];
    uint_large               offset;
    uint_large               width;

    diagnostic = logos_language_tooling_parsing_syntax_tree_diagnostic_at(tree, i);
    offset     = logos_language_tooling_parsing_syntax_diagnostic_byte_offset_get(diagnostic);
    width      = logos_language_tooling_parsing_syntax_diagnostic_byte_width_get(diagnostic);
    message[0] = '\0';
    (void)logos_language_tooling_parsing_syntax_tree_diagnostic_message_write(tree, i, message, NEXUS_SIZEOF(message));

    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0001", ruleforge_span_make(file_index, (uint32)offset, (uint32)(offset + width)), NULL,
                                "%s", message);
  }

  logos_language_tooling_parsing_parser_process_destroy(process);

  /* Only an accepted derivation without diagnostics is a program; a recovered tree is kept for editors but never compiled. */
  if (status != LPS_ACCEPTED) {
    if (diagnostic_count == 0) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0001", ruleforge_span_make(file_index, 0, 0), NULL, "the source could not be parsed");
    }

    logos_language_tooling_parsing_syntax_tree_destroy(tree);

    return NULL;
  }

  return tree;
}

/* ---------------------------------------------------------------------------- */
/* TOKENS                                                                       */
/* ---------------------------------------------------------------------------- */

char *ruleforge_syntax_tokens_json_write(RuleforgeRuntime *runtime, const char *text, uint_large size) {
  LLexerProcess      *process;
  LLexerToken         token;
  LLexerStatus        status;
  NexusStringBuilder  out;
  char               *result;
  boolean             first;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && text != NULL, "ruleforge_syntax_tokens_json_write: NULL argument");

  process = logos_language_tooling_lexing_lexer_process_create(logos_languages_genesis_engine_lexer_get(r_syntax_engine_get(runtime, RSK_RULEFORGE)));
  logos_language_tooling_lexing_lexer_process_chunk_add(process, (const byte *)text, size, TRUE);
  out   = nexus_strings_string_builder_create_growable(1024);
  first = TRUE;
  (void)nexus_strings_string_builder_append(&out, "[");

  for (;;) {
    status = logos_language_tooling_lexing_lexer_process_lex(process, &token);

    if (status == LLS_DONE || status == LLS_NEED_INPUT) {
      break;
    }

    if (status == LLS_TOKEN && token.lexeme_width == 0) {
      continue;
    }

    (void)nexus_strings_string_builder_append_formatted(&out, "%s[%lu,%lu,\"%s\"]", first ? "" : ",", (unsigned long)token.lexeme_offset,
                                                        (unsigned long)(token.lexeme_offset + token.lexeme_width),
                                                        status == LLS_ERROR ? "error" : (rf_token_name(token.kind) == NULL ? "token" : rf_token_name(token.kind)));
    first = FALSE;
  }

  (void)nexus_strings_string_builder_append(&out, "]");
  logos_language_tooling_lexing_lexer_process_destroy(process);
  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}
