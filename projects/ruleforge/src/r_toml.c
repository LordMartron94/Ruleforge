#include <nexus.h>
#include <echo.h>

#include <logos.h>

#include <toml_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
TOML is parsed by Genesis (the Logos library's TOML 1.1 grammar, TOML.gen, through its generated views). This file reads the
scalar entries a settings file needs: it decodes quoted keys and strings and keeps numbers and booleans as written. Arrays,
inline tables and dates carry no setting and are skipped.
*/

typedef struct RTomlReader {
  NexusDataArena    *arena;
  const LParserTree *tree;
  NexusDataList     *entries; /* RuleforgeTomlEntry * */
} RTomlReader;

/* Summary: the decoded text of a basic ("...") or literal ('...') string token, without its quotes. */
static const char *r_toml_string_decode(RTomlReader *reader, const LParserGreenNode *token) {
  const byte        *text;
  uint_large         size;
  NexusStringBuilder out;
  const char        *result;
  uint_large         i;
  boolean            basic;
  uint_large         quote;
  char               one[2];

  text  = logos_language_tooling_parsing_syntax_green_node_text_get(reader->tree, token, &size);
  basic = size > 0 && text[0] == '"';
  quote = size >= 6 && text[0] == text[1] && text[1] == text[2] ? 3 : 1;
  out   = nexus_strings_string_builder_create_growable(32);

  /* A multi-line string drops the line break that directly follows its opening quotes. */
  i = quote;

  if (quote == 3 && i < size && text[i] == '\n') {
    i++;
  }

  for (; i + quote < size; i++) {
    if (basic && text[i] == '\\' && i + 1 + quote < size) {
      i++;

      if (text[i] == 'n') {
        one[0] = '\n';
      } else if (text[i] == 't') {
        one[0] = '\t';
      } else if (text[i] == 'r') {
        one[0] = '\r';
      } else {
        one[0] = (char)text[i];
      }

      one[1] = '\0';
      (void)nexus_strings_string_builder_append(&out, one);

      continue;
    }

    one[0] = (char)text[i];
    one[1] = '\0';
    (void)nexus_strings_string_builder_append(&out, one);
  }

  result = nexus_data_arena_text_copy(reader->arena, nexus_strings_string_builder_string_get(&out), nexus_strings_string_builder_length_get(&out));
  nexus_strings_string_builder_destroy(&out);

  return result;
}

static const char *r_toml_token_text(RTomlReader *reader, const LParserGreenNode *token) {
  const byte *text;
  uint_large  size;

  text = logos_language_tooling_parsing_syntax_green_node_text_get(reader->tree, token, &size);

  return nexus_data_arena_text_copy(reader->arena, (const char *)text, size);
}

/* Summary: appends the segments of a key to a path list. */
static void r_toml_key_append(RTomlReader *reader, TomlKey key, NexusDataList *path) {
  uint32 i;

  for (i = 0; i < toml_key_count_rule_simple_key(key); i++) {
    TomlSimpleKey segment;

    segment = toml_key_rule_simple_key(key, i);

    if (toml_simple_key_count_token_bare_key(segment) > 0) {
      (void)nexus_data_list_append_pointer(path, r_toml_token_text(reader, toml_simple_key_token_bare_key(segment, 0)));
    } else if (toml_simple_key_count_token_basic_string(segment) > 0) {
      (void)nexus_data_list_append_pointer(path, r_toml_string_decode(reader, toml_simple_key_token_basic_string(segment, 0)));
    } else {
      (void)nexus_data_list_append_pointer(path, r_toml_string_decode(reader, toml_simple_key_token_literal_string(segment, 0)));
    }
  }
}

RuleforgeToml *ruleforge_toml_parse(RuleforgeRuntime *runtime, NexusDataArena *arena, const char *text, uint_large size, char *error,
                                    uint_large error_size) {
  RTomlReader    reader;
  LParserTree   *tree;
  TomlDocument   document;
  RuleforgeToml *result;
  NexusDataList *table;
  uint32         i;
  uint_large     k;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && arena != NULL && text != NULL && error != NULL, "ruleforge_toml_parse: NULL argument");

  tree = ruleforge_syntax_text_parse(runtime, RSK_TOML, (const byte *)text, size, error, error_size);

  if (tree == NULL) {
    return NULL;
  }

  reader.arena   = arena;
  reader.tree    = tree;
  reader.entries = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  table          = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  document       = toml_root(tree);

  for (i = 0; i < toml_document_count_rule_line(document); i++) {
    TomlLine line;

    line = toml_document_rule_line(document, i);

    if (toml_line_count_rule_statement(line) == 0) {
      continue;
    }

    {
      TomlStatement statement;

      statement = toml_line_rule_statement(line, 0);

      if (toml_statement_count_rule_table(statement) > 0) {
        nexus_data_list_clear(table);
        r_toml_key_append(&reader, toml_table_rule_key(toml_statement_rule_table(statement, 0), 0), table);
      } else if (toml_statement_count_rule_array_of_tables(statement) > 0) {
        /* Arrays of tables hold no settings; their keys are skipped by leaving the path on a name no setting uses. */
        nexus_data_list_clear(table);
        (void)nexus_data_list_append_pointer(table, "[[array]]");
      } else if (toml_statement_count_rule_key_value(statement) > 0) {
        TomlKeyValue        pair;
        TomlValue           value;
        RuleforgeTomlEntry *entry;
        NexusDataList      *path;

        pair  = toml_statement_rule_key_value(statement, 0);
        value = toml_key_value_rule_value(pair, 0);
        entry = (RuleforgeTomlEntry *)nexus_data_arena_allocate(arena, NEXUS_SIZEOF(*entry));

        if (toml_value_count_rule_string(value) > 0) {
          TomlString string;

          string      = toml_value_rule_string(value, 0);
          entry->kind = RVK_STRING;

          if (toml_string_count_token_basic_string(string) > 0) {
            entry->text = r_toml_string_decode(&reader, toml_string_token_basic_string(string, 0));
          } else if (toml_string_count_token_literal_string(string) > 0) {
            entry->text = r_toml_string_decode(&reader, toml_string_token_literal_string(string, 0));
          } else if (toml_string_count_token_multiline_basic_string(string) > 0) {
            entry->text = r_toml_string_decode(&reader, toml_string_token_multiline_basic_string(string, 0));
          } else {
            entry->text = r_toml_string_decode(&reader, toml_string_token_multiline_literal_string(string, 0));
          }
        } else if (toml_value_count_rule_number(value) > 0) {
          TomlNumber number;

          number = toml_value_rule_number(value, 0);

          if (toml_number_count_token_integer(number) > 0) {
            entry->kind = RVK_INT;
            entry->text = r_toml_token_text(&reader, toml_number_token_integer(number, 0));
          } else {
            entry->kind = RVK_FLOAT;
            entry->text = r_toml_token_text(&reader, toml_number_token_float(number, 0));
          }
        } else if (toml_value_count_token_boolean(value) > 0) {
          entry->kind = RVK_BOOL;
          entry->text = r_toml_token_text(&reader, toml_value_token_boolean(value, 0));
        } else {
          continue;
        }

        path = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

        for (k = 0; k < nexus_data_list_length_get(table); k++) {
          (void)nexus_data_list_append_pointer(path, nexus_data_list_pointer_at(table, k));
        }

        r_toml_key_append(&reader, toml_key_value_rule_key(pair, 0), path);
        entry->path_count = (uint32)nexus_data_list_length_get(path);
        entry->path       = (const char **)nexus_data_arena_allocate(arena, ((uint_large)entry->path_count + 1U) * NEXUS_SIZEOF(const char *));

        for (k = 0; k < entry->path_count; k++) {
          entry->path[k] = (const char *)nexus_data_list_pointer_at(path, k);
        }

        nexus_data_list_destroy(path);
        (void)nexus_data_list_append_pointer(reader.entries, entry);
      }
    }
  }

  result          = (RuleforgeToml *)nexus_data_arena_allocate(arena, NEXUS_SIZEOF(*result));
  result->count   = (uint32)nexus_data_list_length_get(reader.entries);
  result->entries = (RuleforgeTomlEntry **)nexus_data_arena_allocate(arena, ((uint_large)result->count + 1U) * NEXUS_SIZEOF(RuleforgeTomlEntry *));

  for (i = 0; i < result->count; i++) {
    result->entries[i] = (RuleforgeTomlEntry *)nexus_data_list_pointer_at(reader.entries, i);
  }

  nexus_data_list_destroy(reader.entries);
  nexus_data_list_destroy(table);
  logos_language_tooling_parsing_syntax_tree_destroy(tree);

  return result;
}
