#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/* ---------------------------------------------------------------------------- */
/* READING A SOURCE WITH THE GRAMMAR OF ITS FORMAT                               */
/* ---------------------------------------------------------------------------- */

LGenesisEngine *logos_languages_interchange_reader_open(LGenesisCompiler *compiler, LIGrammar *grammar, const char *format_grammar) {
  char           *path;
  LGenesisEngine *engine;

  path = logos_languages_genesis_compiler_import_locate(compiler, format_grammar);

  if (path == NULL) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_ERROR, "cannot find the grammar `%s` of this format in the search folders; add the folder of the standard grammars with -I",
        format_grammar);

    return NULL;
  }

  engine = logos_languages_genesis_compiler_compile_file(compiler, path);
  nexus_memory_deallocate(path);

  if (engine == NULL) {
    char *report;

    report = logos_languages_genesis_compiler_diagnostics_render(compiler, FALSE);
    logos_languages_interchange_grammar_note(grammar, LIN_ERROR, "the grammar `%s` of this format does not compile:\n%s", format_grammar, report);
    nexus_memory_deallocate(report);
  }

  return engine;
}

boolean logos_languages_interchange_reader_parse(LGenesisEngine *engine, LIGrammar *grammar, const byte *text, uint_large size, LIParsed *out) {
  LParserProcess *process;
  LParserStatus   status;
  NexusError     *error;
  LParserTree    *tree;
  uint_large      i;

  nexus_memory_bytes_clear(out, NEXUS_SIZEOF(*out));
  process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));
  error   = logos_language_tooling_parsing_parser_process_chunk_add(process, text, size, TRUE);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    logos_language_tooling_parsing_parser_process_destroy(process);
    logos_languages_interchange_grammar_note(grammar, LIN_ERROR, "the input could not be read");

    return FALSE;
  }

  do {
    status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
  } while (status == LPS_PROGRESS);

  tree = logos_language_tooling_parsing_parser_process_tree_take(process);
  logos_language_tooling_parsing_parser_process_destroy(process);

  if (status != LPS_ACCEPTED || logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(tree) > 0) {
    for (i = 0; i < logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(tree) && i < 3; i++) {
      char message[256];

      (void)logos_language_tooling_parsing_syntax_tree_diagnostic_message_write(tree, i, message, sizeof(message));
      logos_languages_interchange_grammar_note(grammar, LIN_ERROR, "the input is not valid in this format: %s", message);
    }

    logos_language_tooling_parsing_syntax_tree_destroy(tree);

    return FALSE;
  }

  logos_languages_genesis_engine_retain(engine);
  out->engine = engine;
  out->tree   = tree;
  out->source = logos_language_tooling_parsing_syntax_tree_source_get(tree, &out->source_size);

  return TRUE;
}

boolean logos_languages_interchange_parse(LGenesisCompiler *compiler, LIGrammar *grammar, const char *format_grammar, const byte *text,
                                          uint_large size, LIParsed *out) {
  LGenesisEngine *engine;
  boolean         ok;

  nexus_memory_bytes_clear(out, NEXUS_SIZEOF(*out));
  engine = logos_languages_interchange_reader_open(compiler, grammar, format_grammar);

  if (engine == NULL) {
    return FALSE;
  }

  ok = logos_languages_interchange_reader_parse(engine, grammar, text, size, out);
  logos_languages_genesis_engine_release(engine);

  return ok;
}

void logos_languages_interchange_parsed_release(LIParsed *parsed) {
  if (parsed->tree != NULL) {
    logos_language_tooling_parsing_syntax_tree_destroy(parsed->tree);
  }

  if (parsed->engine != NULL) {
    logos_languages_genesis_engine_release(parsed->engine);
  }

  nexus_memory_bytes_clear(parsed, NEXUS_SIZEOF(*parsed));
}

/* ---------------------------------------------------------------------------- */
/* WALKING THE TREE                                                              */
/* ---------------------------------------------------------------------------- */

static void l_interchange_collect(const LParserTree *tree, const LParserGreenNode *node, const LParserGreenNode **out, uint32 capacity,
                                  uint32 *count) {
  const LParserGreenAlternative *alternative;
  uint32                         i;

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative == NULL) {
    return;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    const LParserGreenNode *child;
    LParserGreenKind        kind;

    child = logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i);
    kind  = logos_language_tooling_parsing_syntax_green_node_kind_get(child);

    /* Whitespace and comments (the global rule `trivia` of a format's grammar) are not part of what a converter reads. */
    if (kind == LPGK_RULE && nexus_strings_string_equals(logos_language_tooling_parsing_syntax_green_node_label_get(tree, child), "trivia")) {
      continue;
    }

    if (kind == LPGK_RULE || kind == LPGK_TOKEN) {
      if (*count < capacity) {
        out[*count] = child;
      }

      (*count)++;
    } else {
      l_interchange_collect(tree, child, out, capacity, count);
    }
  }
}

uint32 logos_languages_interchange_children_count(const LIParsed *parsed, const LParserGreenNode *node) {
  uint32 count;

  count = 0;
  l_interchange_collect(parsed->tree, node, NULL, 0, &count);

  return count;
}

uint32 logos_languages_interchange_children(const LIParsed *parsed, const LParserGreenNode *node, const LParserGreenNode **out, uint32 capacity) {
  uint32 count;

  count = 0;
  l_interchange_collect(parsed->tree, node, out, capacity, &count);

  return count < capacity ? count : capacity;
}

const LParserGreenNode **logos_languages_interchange_children_get(const LIParsed *parsed, const LParserGreenNode *node, uint32 *out_count) {
  const LParserGreenNode **children;
  uint32                   count;

  count      = logos_languages_interchange_children_count(parsed, node);
  children   = (const LParserGreenNode **)nexus_memory_allocate(((uint_large)count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  *out_count = logos_languages_interchange_children(parsed, node, children, count);

  return children;
}

const char *logos_languages_interchange_rule_name(const LIParsed *parsed, const LParserGreenNode *node) {
  const char *label;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) != LPGK_RULE) {
    return "";
  }

  label = logos_language_tooling_parsing_syntax_green_node_label_get(parsed->tree, node);

  return label != NULL ? label : "";
}

const char *logos_languages_interchange_token_name(const LIParsed *parsed, const LParserGreenNode *node) {
  const LGenesisGrammar *grammar;
  LLexerTokenKind        kind;
  uint32                 i;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) != LPGK_TOKEN) {
    return "";
  }

  grammar = logos_languages_genesis_engine_grammar_get(parsed->engine);
  kind    = logos_language_tooling_parsing_syntax_green_node_token_kind_get(parsed->tree, node);

  for (i = 0; i < logos_languages_genesis_grammar_token_count_get(grammar); i++) {
    const LGenesisGrammarToken *token;

    token = logos_languages_genesis_grammar_token_at(grammar, i);

    if (!token->fragment && token->kind == kind) {
      return token->name;
    }
  }

  return "";
}

char *logos_languages_interchange_token_text(const LIParsed *parsed, const LParserGreenNode *node) {
  uint_large  size;
  const byte *text;
  char       *copy;

  size = 0;
  text = logos_language_tooling_parsing_syntax_green_node_text_get(parsed->tree, node, &size);

  if (text == NULL) {
    size = 0;
  }

  copy = (char *)nexus_memory_allocate(size + 1);

  if (size > 0) {
    nexus_memory_bytes_copy(text, copy, size);
  }

  copy[size] = '\0';

  return copy;
}

/* ---------------------------------------------------------------------------- */
/* THE FORMATS                                                                   */
/* ---------------------------------------------------------------------------- */

typedef struct LIFormat {
  const char            *name;
  const char            *description;
  LInterchangeConverter *convert;
} LIFormat;

static const LIFormat l_interchange_formats[] = {
    {"abnf", "ABNF (RFC 5234 with the RFC 7405 case-sensitive strings), the grammar notation of Internet standards",
     logos_languages_interchange_abnf_convert},
    {"ebnf", "Extended BNF: ISO/IEC 14977 (`a = b, {c} ;`) or the W3C notation of the XML specification (`A ::= b (c)*`), told apart by `::=`",
     logos_languages_interchange_ebnf_convert},
    {"antlr", "ANTLR 4 grammars (.g4): a lexer grammar and its parser grammar, or a combined one; give every file",
     logos_languages_interchange_antlr_convert},
    {"bison", "yacc and GNU Bison grammars (.y) with the flex scanner (.l) that returns their tokens; give both files",
     logos_languages_interchange_bison_convert},
};

uint32 logos_languages_interchange_format_count_get(void) {
  return (uint32)NEXUS_ARRAY_SIZE_ELEMENTS(l_interchange_formats);
}

const char *logos_languages_interchange_format_name_at(uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(index < NEXUS_ARRAY_SIZE_ELEMENTS(l_interchange_formats), "format index must be in bounds");

  return l_interchange_formats[index].name;
}

const char *logos_languages_interchange_format_description_at(uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(index < NEXUS_ARRAY_SIZE_ELEMENTS(l_interchange_formats), "format index must be in bounds");

  return l_interchange_formats[index].description;
}

boolean logos_languages_interchange_convert(LGenesisCompiler *compiler, const char *format, const char *name, const LInterchangeInput *inputs,
                                            uint32 input_count, char **out_genesis, char **out_report) {
  uint32     i;
  LIGrammar *grammar;
  boolean    ok;
  boolean    errors;

  *out_genesis = NULL;
  *out_report  = NULL;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(l_interchange_formats); i++) {
    if (nexus_strings_string_equals(l_interchange_formats[i].name, format)) {
      break;
    }
  }

  if (i == NEXUS_ARRAY_SIZE_ELEMENTS(l_interchange_formats)) {
    return FALSE;
  }

  grammar = logos_languages_interchange_grammar_create(name);
  ok      = l_interchange_formats[i].convert(compiler, grammar, inputs, input_count);

  if (ok) {
    *out_genesis = logos_languages_interchange_grammar_emit(grammar);
  }

  *out_report = logos_languages_interchange_grammar_report(grammar);
  errors      = FALSE;

  for (i = 0; i < nexus_data_list_length_get(grammar->notes); i++) {
    errors = errors || ((const LINote *)nexus_data_list_at_const(grammar->notes, i))->severity == LIN_ERROR;
  }

  if (errors && *out_genesis != NULL) {
    nexus_memory_deallocate(*out_genesis);
    *out_genesis = NULL;
  }

  logos_languages_interchange_grammar_destroy(grammar);

  return *out_genesis != NULL;
}
