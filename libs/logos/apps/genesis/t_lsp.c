#include <nexus.h>
#include <logos.h>

#include "lsp.h"

/* ---------------------------------------------------------------------------- */
/* HELPERS                                                                       */
/* ---------------------------------------------------------------------------- */

static char *l_ls_copy(const char *text) {
  uint_large length;
  char      *copy;

  if (text == NULL) {
    return NULL;
  }

  length = nexus_strings_string_length_get(text);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length + 1);

  return copy;
}

static char *l_ls_copy_bytes(const char *text, uint_large length) {
  char *copy;

  copy = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length);
  copy[length] = '\0';

  return copy;
}

/* Summary: frees what may be NULL. */
static void l_ls_free(void *memory) {
  if (memory != NULL) {
    nexus_memory_deallocate(memory);
  }
}

static void l_ls_name_add(PharosDocument *document, int32 kind, const char *text, uint_large length, uint_large start, const char *owner,
                          const char *scope, boolean declaration, int32 symbol_kind) {
  pharos_document_name_add(document, kind, text, length, start, owner, scope, declaration, symbol_kind);
}

static void l_ls_problem_add(PharosDocument *document, uint_large start, uint_large end, int32 severity, const char *code, const char *message) {
  pharos_document_problem_add(document, start, end, severity, code, message);
}

/* ---------------------------------------------------------------------------- */
/* GENESIS SOURCES                                                               */
/* ---------------------------------------------------------------------------- */

/* The LSP SymbolKind of a declaration of Genesis. */
static int32 l_ls_genesis_symbol_kind(LGenesisSymbolKind kind) {
  switch (kind) {
  case LGSK_UNIT:
    return 2;
  case LGSK_ALIAS:
    return 3;
  case LGSK_RULE:
    return 12;
  case LGSK_TOKEN:
    return 14;
  case LGSK_FRAGMENT:
    return 13;
  case LGSK_CONTEXT:
    return 3;
  case LGSK_TEMPLATE:
    return 6;
  case LGSK_PARAMETER:
    return 13;
  case LGSK_ENUM:
    return 10;
  default:
    return 22;
  }
}

static void l_ls_genesis_analyze(LToolLs *server, PharosDocument *document) {
  LGenesisCompiler *compiler;
  LGenesisIndex    *index;
  char             *directory;
  uint_large        i;

  compiler  = logos_languages_genesis_compiler_create(server->options->runtime);
  directory = logos_tool_files_directory_of(pharos_document_path_get(document));
  logos_languages_genesis_compiler_search_folder_add(compiler, directory);
  nexus_memory_deallocate(directory);

  for (i = 0; i < pharos_server_folder_count_get(server->pharos); i++) {
    logos_languages_genesis_compiler_search_folder_add(compiler, pharos_server_folder_at(server->pharos, i));
  }

  /* Syntax only: reading the source reports what does not parse; what the names mean is for the semantic analyzer. */
  index = logos_languages_genesis_index_create(compiler, pharos_document_path_get(document), (const byte *)pharos_document_text_get(document),
                                               pharos_document_size_get(document));

  for (i = 0; i < logos_languages_genesis_compiler_diagnostic_count_get(compiler); i++) {
    const PharosDiagnostic *diagnostic;
    NexusStringBuilder      message;
    uint_large              offset;
    uint_large              width;
    uint_large              line;
    uint_large              column;
    uint_large              at;

    diagnostic = logos_languages_genesis_compiler_diagnostic_at(compiler, i);

    if (!nexus_strings_string_equals(pharos_diagnostics_source_name_get(diagnostic), pharos_document_path_get(document)) ||
        pharos_diagnostics_severity_get(diagnostic) != PDS_ERROR) {
      continue;
    }

    pharos_diagnostics_span_get(diagnostic, &offset, &width, &line, &column);
    message = nexus_strings_string_builder_create_growable(128);
    logos_tool_text_check(nexus_strings_string_builder_append(&message, pharos_diagnostics_message_get(diagnostic)));

    for (at = 0; at < pharos_diagnostics_help_count_get(diagnostic); at++) {
      logos_tool_text_check(nexus_strings_string_builder_append_formatted(&message, "\nhelp: %s", pharos_diagnostics_help_at(diagnostic, at)));
    }

    for (at = 0; at < pharos_diagnostics_note_count_get(diagnostic); at++) {
      logos_tool_text_check(nexus_strings_string_builder_append_formatted(&message, "\nnote: %s", pharos_diagnostics_note_at(diagnostic, at)));
    }

    l_ls_problem_add(document, offset, offset + (width > 0 ? width : 1), 1, pharos_diagnostics_code_get(diagnostic),
                     nexus_strings_string_builder_string_get(&message));
    nexus_strings_string_builder_destroy(&message);
  }

  for (i = 0; i < logos_languages_genesis_index_symbol_count_get(index); i++) {
    const LGenesisSymbol *symbol;

    symbol = logos_languages_genesis_index_symbol_at(index, i);
    l_ls_name_add(document, (int32)symbol->kind, symbol->name, symbol->width, symbol->offset, symbol->owner, symbol->scope, symbol->declaration,
                  l_ls_genesis_symbol_kind(symbol->kind));
  }

  logos_languages_genesis_index_destroy(index);
  logos_languages_genesis_compiler_destroy(compiler);
}

/* ---------------------------------------------------------------------------- */
/* MAPPING FILES                                                                 */
/* ---------------------------------------------------------------------------- */

static boolean l_ls_name_byte(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '_';
}

/* Summary: the grammar a mapping file maps: <stem>.gen next to it, or in a search folder. NULL when there is none. */
static char *l_ls_mapping_grammar(const LToolLs *server, const char *path) {
  char      *directory;
  char      *stem;
  char      *file;
  char      *candidate;
  uint_large i;
  byte      *bytes;
  uint_large size;

  directory = logos_tool_files_directory_of(path);
  stem      = logos_tool_files_stem_of(path);
  file      = (char *)nexus_memory_allocate(nexus_strings_string_length_get(stem) + 5);
  (void)stbsp_snprintf(file, (int)(nexus_strings_string_length_get(stem) + 5), "%s.gen", stem);
  candidate = logos_tool_files_join(directory, file);
  nexus_memory_deallocate(directory);

  if (logos_tool_files_read(candidate, &bytes, &size)) {
    nexus_memory_deallocate(bytes);
    nexus_memory_deallocate(stem);
    nexus_memory_deallocate(file);

    return candidate;
  }

  nexus_memory_deallocate(candidate);

  for (i = 0; i < pharos_server_folder_count_get(server->pharos); i++) {
    candidate = logos_tool_files_join(pharos_server_folder_at(server->pharos, i), file);

    if (logos_tool_files_read(candidate, &bytes, &size)) {
      nexus_memory_deallocate(bytes);
      nexus_memory_deallocate(stem);
      nexus_memory_deallocate(file);

      return candidate;
    }

    nexus_memory_deallocate(candidate);
  }

  nexus_memory_deallocate(stem);
  nexus_memory_deallocate(file);

  return NULL;
}

/* Summary: adds the `$name` or `#name` that starts at `at` (the sigil's position) as a reference to the grammar. */
static uint_large l_ls_mapping_reference(PharosDocument *document, uint_large at, const char *grammar) {
  uint_large end;

  end = at + 1;

  while (end < pharos_document_size_get(document) && l_ls_name_byte(pharos_document_text_get(document)[end])) {
    end++;
  }

  if (end > at + 1 && !(pharos_document_text_get(document)[at] == '$' && pharos_document_text_get(document)[at + 1] == '_')) {
    l_ls_name_add(document, pharos_document_text_get(document)[at] == '$' ? (int32)LGSK_TOKEN : (int32)LGSK_RULE,
                  pharos_document_text_get(document) + at + 1, end - at - 1, at + 1, grammar, NULL, FALSE, 0);
  }

  return end;
}

static boolean l_ls_word_is(const PharosDocument *document, uint_large at, uint_large end, const char *word) {
  uint_large length;

  length = nexus_strings_string_length_get(word);

  return end - at == length && nexus_memory_bytes_equal(pharos_document_text_get(document) + at, word, length);
}

static void l_ls_mapping_analyze(LToolLs *server, PharosDocument *document) {
  char        *grammar;
  uint_large   line_start;
  uint_large   line_end;
  LToolMapping mapping;
  char         error[512];

  grammar    = l_ls_mapping_grammar(server, pharos_document_path_get(document));
  line_start = 0;

  while (line_start <= pharos_document_size_get(document)) {
    uint_large at;
    uint_large word_end;

    line_end = line_start;

    while (line_end < pharos_document_size_get(document) && pharos_document_text_get(document)[line_end] != '\n') {
      line_end++;
    }

    at = line_start;

    while (at < line_end && (pharos_document_text_get(document)[at] == ' ' || pharos_document_text_get(document)[at] == '\t')) {
      at++;
    }

    if (at < line_end && !(pharos_document_text_get(document)[at] == '/' && at + 1 < line_end && pharos_document_text_get(document)[at + 1] == '/')) {
      if (pharos_document_text_get(document)[at] == '$') {
        (void)l_ls_mapping_reference(document, at, grammar);
      } else {
        word_end = at;

        while (word_end < line_end && l_ls_name_byte(pharos_document_text_get(document)[word_end])) {
          word_end++;
        }

        if (l_ls_word_is(document, at, word_end, "context")) {
          uint_large name;

          name = word_end;

          while (name < line_end && (pharos_document_text_get(document)[name] == ' ' || pharos_document_text_get(document)[name] == '\t')) {
            name++;
          }

          word_end = name;

          while (word_end < line_end && l_ls_name_byte(pharos_document_text_get(document)[word_end])) {
            word_end++;
          }

          if (word_end > name) {
            l_ls_name_add(document, (int32)LGSK_CONTEXT, pharos_document_text_get(document) + name, word_end - name, name, grammar, NULL, FALSE, 0);
          }
        } else if (l_ls_word_is(document, at, word_end, "rule") || l_ls_word_is(document, at, word_end, "symbol") ||
                   l_ls_word_is(document, at, word_end, "reference")) {
          boolean    stop_at_equals;
          uint_large cursor;

          stop_at_equals = l_ls_word_is(document, at, word_end, "rule");
          cursor         = word_end;

          while (cursor < line_end && !(stop_at_equals && pharos_document_text_get(document)[cursor] == '=')) {
            if (pharos_document_text_get(document)[cursor] == '$' || pharos_document_text_get(document)[cursor] == '#') {
              cursor = l_ls_mapping_reference(document, cursor, grammar);
            } else {
              cursor++;
            }
          }
        }
      }
    }

    if (line_end >= pharos_document_size_get(document)) {
      break;
    }

    line_start = line_end + 1;
  }

  logos_tool_mapping_initialize(&mapping);

  if (!logos_tool_mapping_parse(&mapping, pharos_document_path_get(document), pharos_document_text_get(document), error, sizeof(error))) {
    /* "path:line: message": the line locates the problem. */
    const char *cursor;
    uint_large  line;
    uint_large  offset;
    uint_large  end;
    const char *message;

    cursor  = error + nexus_strings_string_length_get(pharos_document_path_get(document));
    line    = 0;
    message = cursor;

    if (*cursor == ':') {
      cursor++;

      while (*cursor >= '0' && *cursor <= '9') {
        line = (line * 10) + (uint_large)(*cursor - '0');
        cursor++;
      }

      message = *cursor == ':' ? cursor + 1 : cursor;

      while (*message == ' ') {
        message++;
      }
    }

    offset = 0;

    for (end = 1; end < line && offset < pharos_document_size_get(document); offset++) {
      if (pharos_document_text_get(document)[offset] == '\n') {
        end++;
      }
    }

    end = offset;

    while (end < pharos_document_size_get(document) && pharos_document_text_get(document)[end] != '\n') {
      end++;
    }

    l_ls_problem_add(document, offset, end > offset ? end : offset + 1, 1, "mapping", message);
  }

  logos_tool_mapping_release(&mapping);
  l_ls_free(grammar);
}

/* ---------------------------------------------------------------------------- */
/* LANGUAGES DESCRIBED BY A GRAMMAR AND A MAPPING                                */
/* ---------------------------------------------------------------------------- */

typedef struct LToolLsDeclaration {
  char           *rule;      /* The rule that declares a symbol. */
  LLexerTokenKind name_kind; /* The token holding its name. */
  int32           lsp_kind;  /* The LSP SymbolKind. */
} LToolLsDeclaration;

static int32 l_ls_symbol_kind(const char *name) {
  static const char *const names[] = {"file",       "module",      "namespace", "package",   "class",        "method",   "property",
                                      "field",      "constructor", "enum",      "interface", "function",     "variable", "constant",
                                      "string",     "number",      "boolean",   "array",     "object",       "key",      "null",
                                      "enummember", "struct",      "event",     "operator",  "typeparameter"};
  uint32                   i;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(names); i++) {
    if (nexus_strings_string_equals(names[i], name)) {
      return (int32)i + 1;
    }
  }

  return 13;
}

static LLexerTokenKind l_ls_token_kind(const LGenesisGrammar *grammar, const char *name) {
  uint32 i;

  for (i = 0; i < logos_languages_genesis_grammar_token_count_get(grammar); i++) {
    const LGenesisGrammarToken *token;

    token = logos_languages_genesis_grammar_token_at(grammar, i);

    if (!token->fragment && nexus_strings_string_equals(token->name, name)) {
      return token->kind;
    }
  }

  return 0;
}

static boolean l_ls_language_add(LToolLs *server, const char *argument) {
  LToolOptions           own;
  LToolMapping           mapping;
  LToolLsLanguage       *language;
  char                  *path;
  const char            *colon;
  const char            *extensions;
  const LGenesisGrammar *grammar;
  uint_large             path_length;
  uint32                 i;

  colon       = logos_tool_string_find_byte(argument, ':');
  path_length = colon != NULL ? (uint_large)(colon - argument) : nexus_strings_string_length_get(argument);
  path        = l_ls_copy_bytes(argument, path_length);

  own         = *server->options;
  own.grammar = path;
  own.mapping = colon != NULL ? colon + 1 : NULL;
  language    = (LToolLsLanguage *)nexus_memory_allocate(NEXUS_SIZEOF(*language));
  nexus_memory_bytes_clear(language, NEXUS_SIZEOF(*language));
  language->engine = logos_tool_grammar_compile(&own, &language->compiler);

  if (language->engine == NULL) {
    logos_languages_genesis_compiler_destroy(language->compiler);
    nexus_memory_deallocate(language);
    nexus_memory_deallocate(path);

    return FALSE;
  }

  if (!logos_tool_mapping_for(&own, &mapping)) {
    logos_languages_genesis_engine_release(language->engine);
    logos_languages_genesis_compiler_destroy(language->compiler);
    nexus_memory_deallocate(language);
    nexus_memory_deallocate(path);

    return FALSE;
  }

  grammar        = logos_languages_genesis_engine_grammar_get(language->engine);
  language->name = l_ls_copy(logos_languages_genesis_engine_name_get(language->engine));

  {
    char      *exported;
    byte      *mapping_bytes;
    uint_large mapping_size;

    exported              = logos_languages_genesis_engine_export_text(language->engine);
    language->source_hash = exported != NULL ? nexus_hash_xxh3_64(exported, nexus_strings_string_length_get(exported)) : 0;

    if (exported != NULL) {
      nexus_memory_deallocate(exported);
    }

    if (mapping.path != NULL && logos_tool_files_read(mapping.path, &mapping_bytes, &mapping_size)) {
      language->source_hash = nexus_hash_xxh3_64_seeded(mapping_bytes, mapping_size, language->source_hash);
      nexus_memory_deallocate(mapping_bytes);
    }
  }

  language->extensions   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  language->declarations = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  language->references   = nexus_data_list_create(NEXUS_SIZEOF(uint32), 4);
  extensions             = logos_tool_mapping_setting_get(&mapping, "extensions");

  if (extensions == NULL || extensions[0] == '\0') {
    (void)nexus_data_list_append_pointer(language->extensions, logos_tool_identifier_lower(language->name));
  } else {
    const char *cursor;

    cursor = extensions;

    while (*cursor != '\0') {
      const char *end;

      while (*cursor == ' ' || *cursor == ',') {
        cursor++;
      }

      end = cursor;

      while (*end != '\0' && *end != ',' && *end != ' ') {
        end++;
      }

      if (end > cursor) {
        (void)nexus_data_list_append_pointer(language->extensions, l_ls_copy_bytes(cursor, (uint_large)(end - cursor)));
      }

      cursor = end;
    }
  }

  for (i = 0; i < nexus_data_list_length_get(mapping.symbols); i++) {
    const LToolMappingEntry *entry;
    LToolLsDeclaration      *declaration;

    entry                  = (const LToolMappingEntry *)nexus_data_list_at_const(mapping.symbols, i);
    declaration            = (LToolLsDeclaration *)nexus_memory_allocate(NEXUS_SIZEOF(*declaration));
    declaration->rule      = l_ls_copy(entry->key);
    declaration->name_kind = l_ls_token_kind(grammar, entry->value);
    declaration->lsp_kind  = l_ls_symbol_kind(entry->extra);
    (void)nexus_data_list_append_pointer(language->declarations, declaration);
  }

  for (i = 0; i < nexus_data_list_length_get(mapping.references); i++) {
    uint32 kind;

    kind = (uint32)l_ls_token_kind(grammar, ((const LToolMappingEntry *)nexus_data_list_at_const(mapping.references, i))->key);
    (void)nexus_data_list_append(language->references, &kind);
  }

  logos_tool_mapping_release(&mapping);
  nexus_memory_deallocate(path);
  (void)nexus_data_list_append_pointer(server->languages, language);

  return TRUE;
}

static void l_ls_language_destroy(LToolLsLanguage *language) {
  uint_large j;

  for (j = 0; j < nexus_data_list_length_get(language->extensions); j++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(language->extensions, j));
  }

  for (j = 0; j < nexus_data_list_length_get(language->declarations); j++) {
    LToolLsDeclaration *declaration;

    declaration = (LToolLsDeclaration *)nexus_data_list_pointer_at(language->declarations, j);
    nexus_memory_deallocate(declaration->rule);
    nexus_memory_deallocate(declaration);
  }

  nexus_data_list_destroy(language->extensions);
  nexus_data_list_destroy(language->declarations);
  nexus_data_list_destroy(language->references);
  logos_languages_genesis_engine_release(language->engine);
  logos_languages_genesis_compiler_destroy(language->compiler);
  nexus_memory_deallocate(language->name);
  nexus_memory_deallocate(language);
}

static LToolLsLanguage *l_ls_language_for(const LToolLs *server, const char *path) {
  const char *dot;
  uint_large  i;
  uint_large  j;
  uint_large  length;

  dot = NULL;

  for (i = 0; path[i] != '\0'; i++) {
    if (path[i] == '.') {
      dot = path + i + 1;
    } else if (path[i] == '/') {
      dot = NULL;
    }
  }

  if (dot == NULL) {
    return NULL;
  }

  length = nexus_strings_string_length_get(dot);

  for (i = 0; i < nexus_data_list_length_get(server->languages); i++) {
    LToolLsLanguage *language;

    language = (LToolLsLanguage *)nexus_data_list_pointer_at(server->languages, i);

    for (j = 0; j < nexus_data_list_length_get(language->extensions); j++) {
      const char *extension;

      extension = (const char *)nexus_data_list_pointer_at(language->extensions, j);

      if (nexus_strings_string_length_get(extension) == length && nexus_memory_bytes_equal(extension, dot, length)) {
        return language;
      }
    }
  }

  return NULL;
}

/* ---- syntax tree helpers ---- */

static boolean l_ls_node_range(const LParserTree *tree, const LParserGreenNode *node, const byte *base, uint_large *out_start, uint_large *out_end) {
  const LParserGreenAlternative *alternative;
  uint_large                     size;
  uint32                         i;
  boolean                        found;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_TOKEN) {
    const byte *text;

    text = logos_language_tooling_parsing_syntax_green_node_text_get(tree, node, &size);

    if (size == 0) {
      return FALSE;
    }

    *out_start = (uint_large)(text - base);
    *out_end   = *out_start + size;

    return TRUE;
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);
  found       = FALSE;

  if (alternative == NULL) {
    return FALSE;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    uint_large start;
    uint_large end;

    if (l_ls_node_range(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), base, &start, &end)) {
      if (!found || start < *out_start) {
        *out_start = start;
      }

      if (!found || end > *out_end) {
        *out_end = end;
      }

      found = TRUE;
    }
  }

  return found;
}

/* Summary: the first token of a kind below a node. */
static boolean l_ls_token_find(const LParserTree *tree, const LParserGreenNode *node, LLexerTokenKind kind, const byte *base, uint_large *out_start,
                               uint_large *out_end) {
  const LParserGreenAlternative *alternative;
  uint32                         i;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_TOKEN) {
    uint_large  size;
    const byte *text;

    if (logos_language_tooling_parsing_syntax_green_node_token_kind_get(tree, node) != kind) {
      return FALSE;
    }

    text       = logos_language_tooling_parsing_syntax_green_node_text_get(tree, node, &size);
    *out_start = (uint_large)(text - base);
    *out_end   = *out_start + size;

    return TRUE;
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative == NULL) {
    return FALSE;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    if (l_ls_token_find(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), kind, base, out_start, out_end)) {
      return TRUE;
    }
  }

  return FALSE;
}

typedef struct LToolLsHit {
  uint_large start;
  int32      lsp_kind;
} LToolLsHit;

static void l_ls_hits_collect(const LParserTree *tree, const LToolLsLanguage *language, const LParserGreenNode *node, const byte *base,
                              NexusDataList *hits) {
  const LParserGreenAlternative *alternative;
  uint32                         i;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_RULE) {
    const char *label;

    label = logos_language_tooling_parsing_syntax_green_node_label_get(tree, node);

    for (i = 0; label != NULL && i < nexus_data_list_length_get(language->declarations); i++) {
      const LToolLsDeclaration *declaration;
      LToolLsHit                hit;
      uint_large                end;

      declaration = (const LToolLsDeclaration *)nexus_data_list_pointer_at(language->declarations, i);

      if (nexus_strings_string_equals(declaration->rule, label) && l_ls_token_find(tree, node, declaration->name_kind, base, &hit.start, &end)) {
        hit.lsp_kind = declaration->lsp_kind;
        (void)nexus_data_list_append(hits, &hit);
      }
    }
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative == NULL) {
    return;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    l_ls_hits_collect(tree, language, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), base, hits);
  }
}

static void l_ls_rule_path_write(const LToolLsGeneric *generic, uint_large offset, NexusStringBuilder *path, uint32 *out_depth) {
  const LParserGreenNode *node;
  uint_large              size;
  const byte             *base;

  *out_depth = 0;

  if (generic->tree == NULL) {
    return;
  }

  node = logos_language_tooling_parsing_syntax_tree_root_get(generic->tree);
  base = logos_language_tooling_parsing_syntax_tree_source_get(generic->tree, &size);

  for (;;) {
    const LParserGreenAlternative *alternative;
    uint_large                     start;
    uint_large                     end;
    uint32                         i;
    const LParserGreenNode        *next;

    if (!l_ls_node_range(generic->tree, node, base, &start, &end) || offset < start || offset >= end) {
      return;
    }

    if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_RULE) {
      const char *label;

      label = logos_language_tooling_parsing_syntax_green_node_label_get(generic->tree, node);

      if (label != NULL) {
        logos_tool_text_check(nexus_strings_string_builder_append_formatted(path, "%s#%s", *out_depth > 0 ? " > " : "", label));
        (*out_depth)++;
      }
    }

    alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(generic->tree, node);
    next        = NULL;

    if (alternative != NULL && logos_language_tooling_parsing_syntax_green_node_kind_get(node) != LPGK_TOKEN) {
      for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative) && next == NULL; i++) {
        const LParserGreenNode *child;
        uint_large              child_start;
        uint_large              child_end;

        child = logos_language_tooling_parsing_syntax_alternative_child_get(generic->tree, alternative, i);

        if (l_ls_node_range(generic->tree, child, base, &child_start, &child_end) && offset >= child_start && offset < child_end) {
          next = child;
        }
      }
    }

    if (next == NULL) {
      return;
    }

    node = next;
  }
}

static boolean l_ls_kind_listed(const NexusDataList *kinds, LLexerTokenKind kind) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(kinds); i++) {
    if (*(const uint32 *)nexus_data_list_at_const(kinds, i) == kind) {
      return TRUE;
    }
  }

  return FALSE;
}

static boolean l_ls_name_kind(const LToolLsLanguage *language, LLexerTokenKind kind) {
  uint_large i;

  if (l_ls_kind_listed(language->references, kind)) {
    return TRUE;
  }

  for (i = 0; i < nexus_data_list_length_get(language->declarations); i++) {
    if (((const LToolLsDeclaration *)nexus_data_list_pointer_at(language->declarations, i))->name_kind == kind) {
      return TRUE;
    }
  }

  return FALSE;
}

static void l_ls_generic_destroy(void *state) {
  LToolLsGeneric *generic;

  generic = (LToolLsGeneric *)state;

  if (generic->tree != NULL) {
    logos_language_tooling_parsing_syntax_tree_destroy(generic->tree);
  }

  nexus_memory_deallocate(generic);
}

static void l_ls_generic_analyze(PharosDocument *document, LToolLsLanguage *language) {
  LToolLsGeneric *generic;
  LParserProcess *process;
  LParserStatus   status;
  NexusError     *error;
  uint_large      count;
  uint_large      i;
  NexusDataList  *hits;
  char           *owner;

  generic           = (LToolLsGeneric *)nexus_memory_allocate(NEXUS_SIZEOF(*generic));
  generic->language = language;
  generic->tree     = NULL;
  pharos_document_state_set(document, generic, l_ls_generic_destroy);

  process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(language->engine));
  error   = logos_language_tooling_parsing_parser_process_chunk_add(process, (const byte *)pharos_document_text_get(document),
                                                                    pharos_document_size_get(document), TRUE);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    logos_language_tooling_parsing_parser_process_destroy(process);

    return;
  }

  do {
    status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
  } while (status == LPS_PROGRESS);

  generic->tree = logos_language_tooling_parsing_parser_process_tree_take(process);
  logos_language_tooling_parsing_parser_process_destroy(process);

  count = logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(generic->tree);

  for (i = 0; i < count; i++) {
    const LParserDiagnostic *diagnostic;
    char                     message[512];
    uint_large               start;
    uint_large               end;

    diagnostic = logos_language_tooling_parsing_syntax_tree_diagnostic_at(generic->tree, i);
    start      = logos_language_tooling_parsing_syntax_diagnostic_byte_offset_get(diagnostic);
    end        = start + logos_language_tooling_parsing_syntax_diagnostic_byte_width_get(diagnostic);

    if (end <= start) {
      end = start < pharos_document_size_get(document) ? start + pharos_documents_sequence_length_get((byte)pharos_document_text_get(document)[start])
                                                       : start;
    }

    (void)logos_language_tooling_parsing_syntax_tree_diagnostic_message_write(generic->tree, i, message, sizeof(message));
    l_ls_problem_add(document, start, end, 1, language->name, message);
  }

  if (status != LPS_ACCEPTED && status != LPS_RECOVERED) {
    return;
  }

  hits = nexus_data_list_create(NEXUS_SIZEOF(LToolLsHit), 16);
  {
    uint_large  source_size;
    const byte *base;

    base = logos_language_tooling_parsing_syntax_tree_source_get(generic->tree, &source_size);
    l_ls_hits_collect(generic->tree, language, logos_language_tooling_parsing_syntax_tree_root_get(generic->tree), base, hits);
  }

  owner = (char *)nexus_memory_allocate(nexus_strings_string_length_get(language->name) + 9);
  (void)stbsp_snprintf(owner, (int)(nexus_strings_string_length_get(language->name) + 9), "generic:%s", language->name);
  count = logos_language_tooling_parsing_syntax_tree_token_count_get(generic->tree);

  for (i = 0; i < count; i++) {
    LLexerToken token;
    uint_large  hit_index;
    boolean     declaration;
    int32       lsp_kind;

    if (logos_language_tooling_parsing_syntax_tree_token_at(generic->tree, i, &token) != LLE_NONE || token.lexeme_width == 0 ||
        !l_ls_name_kind(language, token.kind)) {
      continue;
    }

    declaration = FALSE;
    lsp_kind    = 13;

    for (hit_index = 0; hit_index < nexus_data_list_length_get(hits); hit_index++) {
      const LToolLsHit *hit;

      hit = (const LToolLsHit *)nexus_data_list_at_const(hits, hit_index);

      if (hit->start == token.lexeme_offset) {
        declaration = TRUE;
        lsp_kind    = hit->lsp_kind;
      }
    }

    l_ls_name_add(document, LTOOL_LS_KIND_GENERIC, pharos_document_text_get(document) + token.lexeme_offset, token.lexeme_width, token.lexeme_offset,
                  owner, NULL, declaration, lsp_kind);
  }

  nexus_memory_deallocate(owner);
  nexus_data_list_destroy(hits);
}

/* ---------------------------------------------------------------------------- */
/* DISPATCH                                                                      */
/* ---------------------------------------------------------------------------- */

/* ---------------------------------------------------------------------------- */
/* THE PROVIDER                                                                  */
/* ---------------------------------------------------------------------------- */

static boolean l_ls_suffix(const char *path, const char *suffix) {
  uint_large length;
  uint_large suffix_length;

  length        = nexus_strings_string_length_get(path);
  suffix_length = nexus_strings_string_length_get(suffix);

  return length > suffix_length && nexus_memory_bytes_equal(path + length - suffix_length, suffix, suffix_length);
}

static boolean l_ls_document_kind(void *user, const char *path, uint32 *out_kind) {
  if (l_ls_suffix(path, ".gen")) {
    *out_kind = (uint32)LTLD_GENESIS;
  } else if (l_ls_suffix(path, ".mapping")) {
    *out_kind = (uint32)LTLD_MAPPING;
  } else if (l_ls_language_for((const LToolLs *)user, path) != NULL) {
    *out_kind = (uint32)LTLD_GENERIC;
  } else {
    return FALSE;
  }

  return TRUE;
}

static void l_ls_analyze(void *user, PharosServer *server, PharosDocument *document) {
  LToolLs *provider;

  (void)server;
  provider = (LToolLs *)user;

  if (pharos_document_kind_get(document) == (uint32)LTLD_GENESIS) {
    l_ls_genesis_analyze(provider, document);
  } else if (pharos_document_kind_get(document) == (uint32)LTLD_MAPPING) {
    l_ls_mapping_analyze(provider, document);
  } else {
    LToolLsLanguage *language;

    language = l_ls_language_for(provider, pharos_document_path_get(document));

    if (language != NULL) {
      l_ls_generic_analyze(document, language);
    }
  }
}

static boolean l_ls_workspace_file(void *user, const char *path) {
  return l_ls_suffix(path, ".gen") || l_ls_suffix(path, ".mapping") || l_ls_language_for((const LToolLs *)user, path) != NULL;
}

static const char *l_ls_kind_label(void *user, int32 kind) {
  (void)user;

  switch (kind) {
  case LGSK_UNIT:
    return "language or library";
  case LGSK_ALIAS:
    return "import name";
  case LGSK_RULE:
    return "rule";
  case LGSK_TOKEN:
    return "token";
  case LGSK_FRAGMENT:
    return "fragment";
  case LGSK_CONTEXT:
    return "context";
  case LGSK_TEMPLATE:
    return "template";
  case LGSK_PARAMETER:
    return "parameter";
  case LGSK_ENUM:
    return "enumeration";
  case LGSK_ENUM_MEMBER:
    return "enumeration member";
  default:
    return "symbol";
  }
}

static const char *l_ls_kind_sigil(void *user, int32 kind) {
  (void)user;

  if (kind == (int32)LGSK_RULE) {
    return "#";
  }

  if (kind == (int32)LGSK_TOKEN || kind == (int32)LGSK_FRAGMENT) {
    return "$";
  }

  return kind == (int32)LGSK_PARAMETER ? "$$" : "";
}

static boolean l_ls_kind_text(void *user, int32 kind) {
  (void)user;

  return kind != (int32)LGSK_UNIT;
}

static boolean l_ls_kind_rename(void *user, int32 kind) {
  (void)user;

  return kind != (int32)LGSK_UNIT;
}

static boolean l_ls_mention(void *user, const PharosName *name, const PharosName *candidate) {
  (void)user;

  return candidate->kind != (int32)LGSK_UNIT && candidate->kind != (int32)LGSK_ALIAS && candidate->kind != (int32)LGSK_PARAMETER &&
         nexus_strings_string_equals(candidate->name, name->name);
}

static boolean l_ls_hover(void *user, PharosDocument *document, uint_large offset, NexusStringBuilder *markdown) {
  const LToolLsGeneric *generic;
  uint32                depth;
  NexusStringBuilder    path;
  boolean               shown;

  (void)user;
  generic = (const LToolLsGeneric *)pharos_document_state_get(document);

  if (generic == NULL || generic->tree == NULL) {
    return FALSE;
  }

  path = nexus_strings_string_builder_create_growable(64);
  l_ls_rule_path_write(generic, offset, &path, &depth);
  shown = depth > 0;

  if (shown) {
    logos_tool_text_check(nexus_strings_string_builder_append_formatted(markdown, "`%s`", nexus_strings_string_builder_string_get(&path)));
  }

  nexus_strings_string_builder_destroy(&path);

  return shown;
}

/* Summary: the settings of the language server from genesis.toml of the project (or --config): [lsp] walk, workspace_max_files, workspace_max_depth, ignore, cache, cache_directory, cache_max_megabytes, text_prefilter. */
static void l_ls_configure(void *user, const char *root, PharosSettings *settings) {
  LToolLs    *server;
  LToolConfig config;
  char        error[512];
  int64       number;

  server = (LToolLs *)user;
  logos_tool_config_initialize(&config);

  if (!logos_tool_config_load(&config, server->options, root, error, sizeof(error))) {
    (void)nexus_stdio_stderr_write_formatted("genesis: %s\n", error);
    logos_tool_config_release(&config);

    return;
  }

  settings->walk                = logos_tool_config_boolean_get(&config, "lsp.walk", settings->walk);
  settings->cache               = logos_tool_config_boolean_get(&config, "lsp.cache", settings->cache);
  settings->text_prefilter      = logos_tool_config_boolean_get(&config, "lsp.text_prefilter", settings->text_prefilter);
  number                        = logos_tool_config_integer_get(&config, "lsp.workspace_max_files", (int64)settings->walk_max_files);
  settings->walk_max_files      = number < 0 ? 0 : (uint_large)number;
  number                        = logos_tool_config_integer_get(&config, "lsp.workspace_max_depth", (int64)settings->walk_max_depth);
  settings->walk_max_depth      = number < 0 ? 0 : (uint32)number;
  number                        = logos_tool_config_integer_get(&config, "lsp.cache_max_megabytes", (int64)settings->cache_max_megabytes);
  settings->cache_max_megabytes = number < 0 ? 0 : (uint32)number;

  if (server->cache_directory != NULL) {
    nexus_memory_deallocate(server->cache_directory);
  }

  server->cache_directory = logos_tool_config_string_get(&config, "lsp.cache_directory");

  if (server->cache_directory != NULL) {
    settings->cache_directory = server->cache_directory;
  }

  logos_tool_config_list_release(server->ignore, server->ignore_count);
  server->ignore_count = logos_tool_config_list_get(&config, "lsp.ignore", &server->ignore);

  if (logos_tool_config_raw_get(&config, "lsp.ignore") != NULL) {
    settings->ignore       = (const char *const *)server->ignore;
    settings->ignore_count = server->ignore_count;
  }

  logos_tool_config_release(&config);
}

/* Summary: the text cached analyses are tied to: the analysis version and the content of every grammar and mapping that is served. */
static const char *l_ls_fingerprint(void *user) {
  LToolLs           *server;
  NexusStringBuilder text;
  uint_large         i;

  server = (LToolLs *)user;

  if (server->fingerprint != NULL) {
    return server->fingerprint;
  }

  text = nexus_strings_string_builder_create_growable(128);
  (void)nexus_strings_string_builder_append_formatted(&text, "genesis-lsp-%d", (int)LTOOL_LS_ANALYSIS_VERSION);

  for (i = 0; i < nexus_data_list_length_get(server->languages); i++) {
    const LToolLsLanguage *language;

    language = (const LToolLsLanguage *)nexus_data_list_pointer_at(server->languages, i);
    (void)nexus_strings_string_builder_append_formatted(&text, ";%s:%llx", language->name, (unsigned long long)language->source_hash);
  }

  server->fingerprint = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&text) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&text), server->fingerprint, nexus_strings_string_builder_length_get(&text) + 1);
  nexus_strings_string_builder_destroy(&text);

  return server->fingerprint;
}

int logos_tool_lsp_run(int argument_count, char **arguments, const LToolOptions *options) {
  LToolLs        provider;
  PharosProvider callbacks;
  const char   **folders;
  uint_large     i;
  int            result;

  (void)argument_count;
  (void)arguments;

  provider.options         = options;
  provider.languages       = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  provider.pharos          = NULL;
  provider.cache_directory = NULL;
  provider.ignore          = NULL;
  provider.ignore_count    = 0;
  provider.fingerprint     = NULL;
  result                   = 0;

  /* Genesis sources and mapping files are always served; the grammars named here add their own languages. */
  if (options->grammar != NULL && !l_ls_language_add(&provider, options->grammar)) {
    result = 1;
  }

  for (i = 0; i < nexus_data_list_length_get(options->files); i++) {
    if (!l_ls_language_add(&provider, (const char *)nexus_data_list_pointer_at(options->files, i))) {
      result = 1;
    }
  }

  if (result == 0) {
    callbacks.user           = &provider;
    callbacks.name           = "genesis";
    callbacks.document_kind  = l_ls_document_kind;
    callbacks.analyze        = l_ls_analyze;
    callbacks.workspace_file = l_ls_workspace_file;
    callbacks.kind_label     = l_ls_kind_label;
    callbacks.kind_sigil     = l_ls_kind_sigil;
    callbacks.kind_text      = l_ls_kind_text;
    callbacks.kind_rename    = l_ls_kind_rename;
    callbacks.mention        = l_ls_mention;
    callbacks.hover          = l_ls_hover;
    callbacks.configure      = l_ls_configure;
    callbacks.fingerprint    = l_ls_fingerprint;
    folders = (const char **)nexus_memory_allocate((nexus_data_list_length_get(options->folders) + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

    for (i = 0; i < nexus_data_list_length_get(options->folders); i++) {
      folders[i] = (const char *)nexus_data_list_pointer_at(options->folders, i);
    }

    provider.pharos = pharos_server_create(options->pharos, &callbacks, folders, nexus_data_list_length_get(options->folders));
    nexus_memory_deallocate((void *)folders);
    result = pharos_server_run(provider.pharos);
    pharos_server_destroy(provider.pharos);
  }

  for (i = 0; i < nexus_data_list_length_get(provider.languages); i++) {
    l_ls_language_destroy((LToolLsLanguage *)nexus_data_list_pointer_at(provider.languages, i));
  }

  nexus_data_list_destroy(provider.languages);

  if (provider.cache_directory != NULL) {
    nexus_memory_deallocate(provider.cache_directory);
  }

  logos_tool_config_list_release(provider.ignore, provider.ignore_count);

  if (provider.fingerprint != NULL) {
    nexus_memory_deallocate(provider.fingerprint);
  }

  return result;
}
