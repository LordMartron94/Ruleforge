#include <nexus.h>
#include <rf_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
A dataset is loaded once, normalized to typed records and fixed before evaluation reads it. Missing required fields, wrong
types and unreadable sources are errors at the `dataset` declaration; extra fields are ignored. CSV cells are text, so they
are converted to the declared field type (documented normalization: int, float, true/false, #hex color; an empty cell is a
missing value). JSON is strict: an int field needs an integer, a float field accepts any number, no other coercion.
*/

/* ---------------------------------------------------------------------------- */
/* HASHING                                                                      */
/* ---------------------------------------------------------------------------- */

static uint64 r_dataset_hash_text(uint64 hash, const char *text) {
  uint_large i;

  for (i = 0; text[i] != '\0'; i++) {
    hash ^= (uint64)(byte)text[i];
    hash *= 1099511628211ULL;
  }

  /* A separator so ("ab","c") and ("a","bc") differ. */
  hash ^= 0x1FU;
  hash *= 1099511628211ULL;

  return hash;
}

/* ---------------------------------------------------------------------------- */
/* CONVERSION                                                                   */
/* ---------------------------------------------------------------------------- */

static boolean r_dataset_hex_color(const char *text, uint32 *out_color) {
  uint_large length;
  uint_large i;
  uint32     value;
  uint32     digit;

  length = nexus_strings_string_length_get(text);

  if ((length != 7 && length != 9) || text[0] != '#') {
    return FALSE;
  }

  value = 0;

  for (i = 1; i < length; i++) {
    if (text[i] >= '0' && text[i] <= '9') {
      digit = (uint32)(text[i] - '0');
    } else if (text[i] >= 'a' && text[i] <= 'f') {
      digit = (uint32)(text[i] - 'a' + 10);
    } else if (text[i] >= 'A' && text[i] <= 'F') {
      digit = (uint32)(text[i] - 'A' + 10);
    } else {
      return FALSE;
    }

    value = (value << 4) | digit;
  }

  *out_color = length == 7 ? ((value << 8) | 0xFFU) : value;

  return TRUE;
}

/* Summary: converts CSV text to the declared field type; FALSE when the text is not of that type. */
static boolean r_dataset_text_convert(const char *text, RfType type, RuleforgeValue *out) {
  RuleforgeTypeKind kind;
  uint_large        length;
  uint_large        i;
  boolean           negative;
  int64             integer;
  f_real            real;
  f_real            scale;
  boolean           fraction;
  boolean           digits;
  uint32            color;

  length = nexus_strings_string_length_get(text);
  kind   = ruleforge_eval_type_kind_get(type);

  if (kind == RTK_STRING) {
    *out = ruleforge_value_string(text);

    return TRUE;
  }

  if (kind == RTK_BOOL) {
    if (nexus_strings_string_equals(text, "true")) {
      *out = ruleforge_value_flag(TRUE);

      return TRUE;
    }

    if (nexus_strings_string_equals(text, "false")) {
      *out = ruleforge_value_flag(FALSE);

      return TRUE;
    }

    return FALSE;
  }

  if (kind == RTK_COLOR) {
    if (!r_dataset_hex_color(text, &color)) {
      return FALSE;
    }

    *out = ruleforge_value_color(color);

    return TRUE;
  }

  if (kind != RTK_INT && kind != RTK_FLOAT) {
    return FALSE;
  }

  negative = length > 0 && (text[0] == '-' || text[0] == '+') ? text[0] == '-' : FALSE;
  i        = length > 0 && (text[0] == '-' || text[0] == '+') ? 1 : 0;
  integer  = 0;
  real     = 0.0;
  scale    = 1.0;
  fraction = FALSE;
  digits   = FALSE;

  for (; i < length; i++) {
    if (text[i] >= '0' && text[i] <= '9') {
      digits = TRUE;

      if (fraction) {
        scale *= 0.1;
        real += (f_real)(text[i] - '0') * scale;
      } else {
        integer = (integer * 10) + (int64)(text[i] - '0');
        real    = (real * 10.0) + (f_real)(text[i] - '0');
      }
    } else if (text[i] == '.' && !fraction && kind == RTK_FLOAT) {
      fraction = TRUE;
    } else {
      return FALSE;
    }
  }

  if (!digits) {
    return FALSE;
  }

  if (kind == RTK_INT) {
    *out = ruleforge_value_integer(negative ? -integer : integer);
  } else {
    *out = ruleforge_value_real(negative ? -real : real);
  }

  return TRUE;
}

/* Summary: converts one JSON value to the declared type; FALSE on a mismatch. */
static boolean r_dataset_json_convert(RuleforgeCompilation *compilation, const RuleforgeJson *json, RfType type, RuleforgeValue *out) {
  RuleforgeTypeKind kind;
  uint32            color;
  uint32            i;

  kind = ruleforge_eval_type_kind_get(type);

  if (kind == RTK_LIST) {
    RuleforgeList *list;
    RfType         element;

    if (json->kind != RJK_ARRAY) {
      return FALSE;
    }

    element = rf_base_type_rule_type(rf_type_rule_base_type(type, 0), 0);
    list    = ruleforge_value_list_create(compilation, json->count);

    for (i = 0; i < json->count; i++) {
      if (!r_dataset_json_convert(compilation, json->items[i], element, &list->items[i])) {
        return FALSE;
      }
    }

    *out = ruleforge_value_list(list);

    return TRUE;
  }

  if (kind == RTK_STRING) {
    if (json->kind != RJK_STRING) {
      return FALSE;
    }

    *out = ruleforge_value_string(json->text);

    return TRUE;
  }

  if (kind == RTK_BOOL) {
    if (json->kind != RJK_BOOL) {
      return FALSE;
    }

    *out = ruleforge_value_flag(json->flag);

    return TRUE;
  }

  if (kind == RTK_INT) {
    if (json->kind != RJK_NUMBER || !json->integral) {
      return FALSE;
    }

    *out = ruleforge_value_integer(json->integer);

    return TRUE;
  }

  if (kind == RTK_FLOAT) {
    if (json->kind != RJK_NUMBER) {
      return FALSE;
    }

    *out = ruleforge_value_real(json->real);

    return TRUE;
  }

  if (kind == RTK_COLOR) {
    if (json->kind != RJK_STRING || !r_dataset_hex_color(json->text, &color)) {
      return FALSE;
    }

    *out = ruleforge_value_color(color);

    return TRUE;
  }

  return FALSE;
}

/* Summary: the logical source name of a dataset declaration: its string literal without the quotes. */
static const char *r_dataset_string_literal(REval *eval, RfDatasetStmt declaration) {
  const char *raw;
  uint_large  length;

  raw    = ruleforge_eval_text_get(eval, declaration.tree, rf_dataset_stmt_token_string_literal(declaration, 0));
  length = nexus_strings_string_length_get(raw);

  return nexus_data_arena_text_copy(&eval->compilation->arena, raw + 1, length >= 2 ? length - 2 : 0);
}

/* ---------------------------------------------------------------------------- */
/* LOADING                                                                      */
/* ---------------------------------------------------------------------------- */

RuleforgeDataset *ruleforge_dataset_load(REval *eval, RfDatasetStmt declaration, RuleforgeModule *owner) {
  RuleforgeCompilation          *compilation;
  const RuleforgeSettingDataset *source;
  const RuleforgeDeclaration    *schema;
  RfSchemaStmt                   fields;
  const char                    *dataset_name;
  const char                    *schema_name;
  RuleforgeSpan                  span;
  uint32                         field_count;
  RuleforgeModule               *schema_owner;
  RuleforgeDataset              *dataset;
  const char                    *logical;
  const char                    *provider;
  byte                          *bytes;
  uint_large                     size;
  uint_large                     i;
  uint32                         row;
  uint32                         field;
  uint32                        *columns;
  RuleforgeJson                 *json;
  const RuleforgeJson           *array;
  RuleforgeCsv                  *csv;
  boolean                        is_json;
  char                           error[256];
  uint64                         hash;

  compilation  = eval->compilation;
  dataset_name = ruleforge_eval_text_get(eval, declaration.tree, rf_dataset_stmt_token_identifier(declaration, 0));
  schema_name  = ruleforge_syntax_name_get(compilation, rf_dataset_stmt_rule_qualified_name(declaration, 0));
  span         = ruleforge_syntax_span_get(owner, declaration.tree, declaration.node);
  logical      = r_dataset_string_literal(eval, declaration);
  source       = NULL;
  json         = NULL;
  array        = NULL;
  csv          = NULL;

  for (i = 0; eval->settings != NULL && i < nexus_data_list_length_get(eval->settings->datasets); i++) {
    const RuleforgeSettingDataset *candidate;

    candidate = (const RuleforgeSettingDataset *)nexus_data_list_pointer_at(eval->settings->datasets, i);

    if (nexus_strings_string_equals(candidate->source, logical)) {
      source = candidate;
    }
  }

  if (source == NULL) {
    ruleforge_eval_fail(eval, "RF0300", span, "configure it in the settings file: [datasets.\"<source>\"] provider = \"csv\", path = \"...\"",
                        "dataset `%s` reads the source \"%s\", which has no configured snapshot", dataset_name, logical);

    return NULL;
  }

  schema = ruleforge_eval_symbol_find(eval, owner, schema_name, &schema_owner);

  if (schema == NULL || rf_declaration_count_rule_schema_stmt(schema->view) == 0) {
    ruleforge_eval_fail(eval, "RF0301", span, NULL, "dataset `%s` names the schema `%s`, which is not declared", dataset_name, schema_name);

    return NULL;
  }

  fields      = rf_declaration_rule_schema_stmt(schema->view, 0);
  field_count = rf_schema_stmt_count_rule_schema_field(fields);

  if (!ruleforge_source_file_read(source->path, &bytes, &size)) {
    ruleforge_eval_fail(eval, "RF0302", span, NULL, "cannot read the snapshot %s of dataset `%s`", source->path, dataset_name);

    return NULL;
  }

  provider = nexus_strings_string_ends_with(source->path, ".json") ? "json" : "csv";

  if (source->provider[0] != '\0') {
    provider = source->provider;
  }

  is_json = nexus_strings_string_equals(provider, "json");

  if (is_json) {
    json = ruleforge_json_parse(compilation->runtime, &compilation->arena, (const char *)bytes, size, error, NEXUS_SIZEOF(error));

    if (json == NULL) {
      ruleforge_eval_fail(eval, "RF0303", span, NULL, "the snapshot %s of dataset `%s` is not valid JSON: %s", source->path, dataset_name, error);
      nexus_memory_deallocate(bytes);

      return NULL;
    }

    array = json;

    if (source->root[0] != '\0') {
      array = ruleforge_json_member_get(json, source->root);
    }

    if (array == NULL || array->kind != RJK_ARRAY) {
      ruleforge_eval_fail(eval, "RF0304", span, "set `root` in the dataset's settings to the member that holds the array",
                          "the snapshot %s of dataset `%s` has no array to read", source->path, dataset_name);
      nexus_memory_deallocate(bytes);

      return NULL;
    }
  } else if (nexus_strings_string_equals(provider, "csv")) {
    csv = ruleforge_csv_parse(compilation->runtime, &compilation->arena, (const char *)bytes, size, error, NEXUS_SIZEOF(error));

    if (csv == NULL || csv->row_count == 0) {
      ruleforge_eval_fail(eval, "RF0305", span, NULL, "the snapshot %s of dataset `%s` is not usable CSV: %s", source->path, dataset_name,
                          csv == NULL ? error : "it has no header row");
      nexus_memory_deallocate(bytes);

      return NULL;
    }
  } else {
    ruleforge_eval_fail(eval, "RF0306", span, "providers: csv, json", "dataset `%s` uses the unknown provider `%s`", dataset_name, provider);
    nexus_memory_deallocate(bytes);

    return NULL;
  }

  nexus_memory_deallocate(bytes);

  /* Map schema fields to CSV columns once. */
  columns = (uint32 *)nexus_data_arena_allocate(&compilation->arena, ((uint_large)field_count + 1U) * NEXUS_SIZEOF(uint32));

  for (field = 0; field < field_count; field++) {
    RfSchemaField schema_field;
    const char   *field_name;

    schema_field   = rf_schema_stmt_rule_schema_field(fields, field);
    field_name     = ruleforge_eval_text_get(eval, schema_field.tree, rf_schema_field_token_identifier(schema_field, 0));
    columns[field] = 0xFFFFFFFFU;

    if (!is_json) {
      for (i = 0; i < csv->column_counts[0]; i++) {
        if (nexus_strings_string_equals(csv->cells[0][i], field_name)) {
          columns[field] = (uint32)i;
        }
      }

      if (columns[field] == 0xFFFFFFFFU && rf_type_count_token_question(rf_schema_field_rule_type(schema_field, 0)) == 0) {
        ruleforge_eval_fail(eval, "RF0307", span, NULL, "the snapshot %s lacks the column `%s` that schema `%s` requires", source->path, field_name,
                            schema_name);

        return NULL;
      }
    }
  }

  dataset         = (RuleforgeDataset *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*dataset));
  dataset->name   = dataset_name;
  dataset->source = logical;
  dataset->schema = schema_name;
  dataset->index  = (uint32)nexus_data_list_length_get(eval->datasets);
  dataset->count  = is_json ? array->count : csv->row_count - 1;
  dataset->rows =
      (RuleforgeRecord **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)dataset->count + 1U) * NEXUS_SIZEOF(RuleforgeRecord *));
  hash = 14695981039346656037ULL;

  for (row = 0; row < dataset->count; row++) {
    RuleforgeRecord *record;

    record          = ruleforge_value_record_create(compilation, field_count);
    record->dataset = dataset->index;
    record->row     = row;

    for (field = 0; field < field_count; field++) {
      RfSchemaField        schema_field;
      const char          *field_name;
      RfType               type;
      const char          *text;
      const RuleforgeJson *member;
      boolean              missing;
      boolean              ok;
      NexusStringBuilder   canonical;

      schema_field          = rf_schema_stmt_rule_schema_field(fields, field);
      field_name            = ruleforge_eval_text_get(eval, schema_field.tree, rf_schema_field_token_identifier(schema_field, 0));
      type                  = rf_schema_field_rule_type(schema_field, 0);
      record->names[field]  = field_name;
      record->values[field] = ruleforge_value_none();
      text                  = "";
      member                = NULL;

      if (is_json) {
        member  = ruleforge_json_member_get(array->items[row], field_name);
        missing = member == NULL || member->kind == RJK_NULL;
      } else {
        text    = columns[field] != 0xFFFFFFFFU && columns[field] < csv->column_counts[row + 1] ? csv->cells[row + 1][columns[field]] : "";
        missing = text[0] == '\0';
      }

      if (missing) {
        if (rf_type_count_token_question(type) == 0) {
          ruleforge_eval_fail(eval, "RF0308", span, "mark the field optional (`T?`) when values can be absent",
                              "row %lu of dataset `%s` has no value for the required field `%s`", (unsigned long)row + 1UL, dataset_name, field_name);

          return NULL;
        }

        hash = r_dataset_hash_text(hash, "\x01");

        continue;
      }

      ok = is_json ? r_dataset_json_convert(compilation, member, type, &record->values[field])
                   : r_dataset_text_convert(text, type, &record->values[field]);

      if (!ok) {
        ruleforge_eval_fail(eval, "RF0309", span,
                            is_json ? "JSON values are not coerced; an int field needs an integer"
                                    : "CSV cells convert to int, float, true/false or #hex colors",
                            "row %lu of dataset `%s`: field `%s` is not a valid %s", (unsigned long)row + 1UL, dataset_name, field_name,
                            ruleforge_eval_type_name_get(eval, type));

        return NULL;
      }

      canonical = nexus_strings_string_builder_create_growable(16);
      ruleforge_value_json_write(&record->values[field], &canonical);
      hash = r_dataset_hash_text(hash, nexus_strings_string_builder_string_get(&canonical));
      nexus_strings_string_builder_destroy(&canonical);
    }

    dataset->rows[row] = record;
  }

  dataset->hash = hash;
  (void)nexus_data_list_append_pointer(eval->datasets, dataset);

  return dataset;
}
