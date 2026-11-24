#include <nexus.h>

#include <logos.h>

#include <csv_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
CSV is parsed by Genesis (the Logos library's relaxed CSV grammar, CSVRelaxed.gen, through its generated views). This file turns the syntax tree into cells: it
removes the quotes of an escaped field and collapses its doubled quotes; it never interprets what a cell means.
*/

/* The five token kinds that can follow one another inside a quoted field, in the order the grammar lists them. */
#define R_CSV_PART_KINDS 5

/* Summary: the token of kind `kind` at `index` inside an escaped field, or NULL past the last one. */
static const LParserGreenNode *r_csv_part_get(CsvEscaped escaped, uint32 kind, uint32 index) {
  switch (kind) {
  case 0:
    return index < csv_escaped_count_token_quoted_text(escaped) ? csv_escaped_token_quoted_text(escaped, index) : NULL;
  case 1:
    return index < csv_escaped_count_token_quoted_comma(escaped) ? csv_escaped_token_quoted_comma(escaped, index) : NULL;
  case 2:
    return index < csv_escaped_count_token_quoted_cr(escaped) ? csv_escaped_token_quoted_cr(escaped, index) : NULL;
  case 3:
    return index < csv_escaped_count_token_quoted_lf(escaped) ? csv_escaped_token_quoted_lf(escaped, index) : NULL;
  default:
    return index < csv_escaped_count_token_escaped_quote(escaped) ? csv_escaped_token_escaped_quote(escaped, index) : NULL;
  }
}

static const char *r_csv_field_text(NexusDataArena *arena, CsvField field) {
  NexusStringBuilder out;
  char              *result;

  out = nexus_strings_string_builder_create_growable(16);

  if (csv_field_count_rule_escaped(field) > 0) {
    CsvEscaped escaped;
    uint32     index[R_CSV_PART_KINDS];
    uint32     kind;

    escaped = csv_field_rule_escaped(field, 0);

    for (kind = 0; kind < R_CSV_PART_KINDS; kind++) {
      index[kind] = 0;
    }

    /* The views list the token kinds apart; their source positions restore the order they were written in. */
    for (;;) {
      const byte *best_text;
      uint_large  best_size;
      uint32      best_kind;

      best_text = NULL;
      best_size = 0;
      best_kind = R_CSV_PART_KINDS;

      for (kind = 0; kind < R_CSV_PART_KINDS; kind++) {
        const LParserGreenNode *token;
        const byte             *text;
        uint_large              size;

        token = r_csv_part_get(escaped, kind, index[kind]);

        if (token == NULL) {
          continue;
        }

        text = logos_language_tooling_parsing_syntax_green_node_text_get(escaped.tree, token, &size);

        if (best_text == NULL || text < best_text) {
          best_text = text;
          best_size = size;
          best_kind = kind;
        }
      }

      if (best_kind == R_CSV_PART_KINDS) {
        break;
      }

      index[best_kind]++;

      /* A doubled quote stands for one quote; everything else is the text as written. */
      if (best_kind == R_CSV_PART_KINDS - 1) {
        (void)nexus_strings_string_builder_append(&out, "\"");
      } else {
        (void)nexus_strings_string_builder_append_formatted(&out, "%.*s", (int)best_size, (const char *)best_text);
      }
    }
  } else if (csv_field_count_token_text(field) > 0) {
    const byte *text;
    uint_large  size;

    text = logos_language_tooling_parsing_syntax_green_node_text_get(field.tree, csv_field_token_text(field, 0), &size);
    (void)nexus_strings_string_builder_append_formatted(&out, "%.*s", (int)size, (const char *)text);
  }

  result = nexus_data_arena_text_copy(arena, nexus_strings_string_builder_string_get(&out), nexus_strings_string_builder_length_get(&out));
  nexus_strings_string_builder_destroy(&out);

  return result;
}

RuleforgeCsv *ruleforge_csv_parse(RuleforgeRuntime *runtime, NexusDataArena *arena, const char *text, uint_large size, char *error,
                                  uint_large error_size) {
  LParserTree  *tree;
  CsvFile       file;
  RuleforgeCsv *table;
  uint32        row;
  uint32        column;
  uint32        kept;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && arena != NULL && text != NULL && error != NULL, "ruleforge_csv_parse: NULL argument");

  /* A byte-order mark is not part of the first cell. */
  if (size >= 3 && (byte)text[0] == 0xEF && (byte)text[1] == 0xBB && (byte)text[2] == 0xBF) {
    text += 3;
    size -= 3;
  }

  tree = ruleforge_syntax_text_parse(runtime, RSK_CSV, (const byte *)text, size, error, error_size);

  if (tree == NULL) {
    return NULL;
  }

  file                 = csv_root(tree);
  table                = (RuleforgeCsv *)nexus_data_arena_allocate(arena, NEXUS_SIZEOF(*table));
  table->column_counts = (uint32 *)nexus_data_arena_allocate(arena, ((uint_large)csv_file_count_rule_record(file) + 1U) * NEXUS_SIZEOF(uint32));
  table->cells = (const char ***)nexus_data_arena_allocate(arena, ((uint_large)csv_file_count_rule_record(file) + 1U) * NEXUS_SIZEOF(const char **));
  kept         = 0;

  for (row = 0; row < csv_file_count_rule_record(file); row++) {
    CsvRecord record;
    uint32    columns;

    record  = csv_file_rule_record(file, row);
    columns = csv_record_count_rule_field(record);

    /* A blank line is a record with one empty field; it carries no data. */
    if (columns == 1 && csv_field_count_rule_escaped(csv_record_rule_field(record, 0)) == 0 &&
        csv_field_count_token_text(csv_record_rule_field(record, 0)) == 0) {
      continue;
    }

    table->column_counts[kept] = columns;
    table->cells[kept]         = (const char **)nexus_data_arena_allocate(arena, ((uint_large)columns + 1U) * NEXUS_SIZEOF(const char *));

    for (column = 0; column < columns; column++) {
      table->cells[kept][column] = r_csv_field_text(arena, csv_record_rule_field(record, column));
    }

    kept++;
  }

  table->row_count = kept;
  logos_language_tooling_parsing_syntax_tree_destroy(tree);

  return table;
}
