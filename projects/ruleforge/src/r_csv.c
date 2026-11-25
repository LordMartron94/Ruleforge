#include <nexus.h>

#include <logos.h>

#include <csv_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
CSV is parsed by Genesis (the Logos library's relaxed CSV grammar, CSVRelaxed.gen, through its generated views). This file turns the syntax tree
into cells: it removes the quotes of an escaped field and collapses its doubled quotes; it never interprets what a cell means.
*/

/* Walk a parsed field once. Repeated indexed generated-view lookups would
   traverse the same long quoted JSON cell for every token it contains. */
static void r_csv_field_append(const LParserTree *tree, const LParserGreenNode *node, NexusStringBuilder *out) {
  const LParserGreenAlternative *alternative;
  uint32                         i;
  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_TOKEN) {
    LLexerTokenKind kind;
    const byte     *text;
    uint_large      size;
    kind = logos_language_tooling_parsing_syntax_green_node_token_kind_get(tree, node);
    if (kind == CSV_TOKEN_QUOTE_OPEN || kind == CSV_TOKEN_QUOTE_CLOSE) {
      return;
    }
    if (kind == CSV_TOKEN_ESCAPED_QUOTE) {
      (void)nexus_strings_string_builder_append(out, "\"");
    } else {
      text = logos_language_tooling_parsing_syntax_green_node_text_get(tree, node, &size);
      (void)nexus_strings_string_builder_append_formatted(out, "%.*s", (int)size, (const char *)text);
    }
    return;
  }
  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);
  if (alternative == NULL) {
    return;
  }
  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    r_csv_field_append(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), out);
  }
}

static const char *r_csv_field_text(NexusDataArena *arena, CsvField field) {
  NexusStringBuilder out;
  char              *result;
  out = nexus_strings_string_builder_create_growable(16);
  r_csv_field_append(field.tree, field.node, &out);
  result = nexus_data_arena_text_copy(arena, nexus_strings_string_builder_string_get(&out), nexus_strings_string_builder_length_get(&out));
  nexus_strings_string_builder_destroy(&out);
  return result;
}

static RuleforgeCsv *r_csv_chunk_parse(RuleforgeRuntime *runtime, NexusDataArena *arena, const char *text, uint_large size, char *error,
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

  file = csv_root(tree);
  table = (RuleforgeCsv *)nexus_data_arena_allocate(arena, NEXUS_SIZEOF(*table));
  table->column_counts = (uint32 *)nexus_data_arena_allocate(arena, ((uint_large)csv_file_count_rule_record(file) + 1U) * NEXUS_SIZEOF(uint32));
  table->cells =
      (const char ***)nexus_data_arena_allocate(arena, ((uint_large)csv_file_count_rule_record(file) + 1U) * NEXUS_SIZEOF(const char **));
  kept = 0;

  for (row = 0; row < csv_file_count_rule_record(file); row++) {
    CsvRecord record;
    uint32    columns;

    record = csv_file_rule_record(file, row);
    columns = csv_record_count_rule_field(record);

    /* A blank line is a record with one empty field; it carries no data. */
    if (columns == 1 && csv_field_count_rule_escaped(csv_record_rule_field(record, 0)) == 0 &&
        csv_field_count_token_text(csv_record_rule_field(record, 0)) == 0) {
      continue;
    }

    table->column_counts[kept] = columns;
    table->cells[kept] = (const char **)nexus_data_arena_allocate(arena, ((uint_large)columns + 1U) * NEXUS_SIZEOF(const char *));

    for (column = 0; column < columns; column++) {
      table->cells[kept][column] = r_csv_field_text(arena, csv_record_rule_field(record, column));
    }

    kept++;
  }

  table->row_count = kept;
  logos_language_tooling_parsing_syntax_tree_destroy(tree);

  return table;
}

RuleforgeCsv *ruleforge_csv_parse(RuleforgeRuntime *runtime, NexusDataArena *arena, const char *text, uint_large size, char *error,
                                  uint_large error_size) {
  NexusDataList *chunks;
  RuleforgeCsv  *table;
  RuleforgeCsv  *chunk;
  uint_large     start;
  uint_large     offset;
  uint_large     i;
  uint32         row;
  uint32         total;
  boolean        quoted;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && arena != NULL && text != NULL && error != NULL, "ruleforge_csv_parse: NULL argument");

  /* Frame complete records, then let Genesis parse their cells. Releasing each
     syntax tree bounds parser memory independently of the snapshot size. */
  chunks = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  start = 0;
  total = 0;
  quoted = FALSE;
  for (offset = 0; offset <= size; offset++) {
    if (offset < size && text[offset] == '"') {
      if (quoted && offset + 1 < size && text[offset + 1] == '"') {
        offset++;
      } else {
        quoted = !quoted;
      }
    }
    if (offset != size && (quoted || (text[offset] != '\n' && text[offset] != '\r') || offset - start < 32768U)) {
      continue;
    }
    if (offset == size && start == size) {
      break;
    }
    if (offset < size && text[offset] == '\r' && offset + 1 < size && text[offset + 1] == '\n') {
      offset++;
    }
    chunk = r_csv_chunk_parse(runtime, arena, text + start, offset - start + (offset < size ? 1U : 0U), error, error_size);
    if (chunk == NULL) {
      nexus_data_list_destroy(chunks);
      return NULL;
    }
    if (chunk->row_count > ((uint32)0xFFFFFFFFU) - total) {
      (void)stbsp_snprintf(error, (int)error_size, "CSV has too many records");
      nexus_data_list_destroy(chunks);
      return NULL;
    }
    total += chunk->row_count;
    (void)nexus_data_list_append_pointer(chunks, chunk);
    start = offset + 1;
  }
  table = (RuleforgeCsv *)nexus_data_arena_allocate(arena, NEXUS_SIZEOF(*table));
  table->row_count = total;
  table->column_counts = (uint32 *)nexus_data_arena_allocate(arena, ((uint_large)total + 1U) * NEXUS_SIZEOF(uint32));
  table->cells = (const char ***)nexus_data_arena_allocate(arena, ((uint_large)total + 1U) * NEXUS_SIZEOF(const char **));
  total = 0;
  for (i = 0; i < nexus_data_list_length_get(chunks); i++) {
    chunk = (RuleforgeCsv *)nexus_data_list_pointer_at(chunks, i);
    for (row = 0; row < chunk->row_count; row++) {
      table->column_counts[total] = chunk->column_counts[row];
      table->cells[total++] = chunk->cells[row];
    }
  }
  nexus_data_list_destroy(chunks);
  return table;
}
