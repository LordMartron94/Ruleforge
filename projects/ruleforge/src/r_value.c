#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

const char *ruleforge_value_type_name_get(RuleforgeValueKind kind) {
  switch (kind) {
  case RVK_NONE:
    return "none";
  case RVK_INT:
    return "int";
  case RVK_FLOAT:
    return "float";
  case RVK_BOOL:
    return "bool";
  case RVK_STRING:
    return "string";
  case RVK_COLOR:
    return "color";
  case RVK_STYLE:
    return "style";
  case RVK_RULESET:
    return "ruleset";
  case RVK_LIST:
    return "list";
  case RVK_DATASET:
    return "dataset";
  case RVK_RECORD:
    return "record";
  case RVK_THEME:
    return "theme";
  case RVK_MODULE:
    return "module";
  case RVK_COUNT:
    break;
  }

  return "value";
}

boolean ruleforge_value_number_get(const RuleforgeValue *value, f_real *out_number) {
  if (value->kind == RVK_INT) {
    *out_number = (f_real)value->as.integer;

    return TRUE;
  }

  if (value->kind == RVK_FLOAT) {
    *out_number = value->as.real;

    return TRUE;
  }

  return FALSE;
}

boolean ruleforge_value_equals(const RuleforgeValue *left, const RuleforgeValue *right) {
  f_real left_number;
  f_real right_number;
  uint32 i;

  if (ruleforge_value_number_get(left, &left_number) && ruleforge_value_number_get(right, &right_number)) {
    return left_number == right_number;
  }

  if (left->kind != right->kind) {
    return FALSE;
  }

  switch (left->kind) {
  case RVK_NONE:
    return TRUE;
  case RVK_BOOL:
    return left->as.flag == right->as.flag;
  case RVK_STRING:
    return nexus_strings_string_equals(left->as.string, right->as.string);
  case RVK_COLOR:
    return left->as.color == right->as.color;
  case RVK_LIST:
    if (left->as.list->count != right->as.list->count) {
      return FALSE;
    }

    for (i = 0; i < left->as.list->count; i++) {
      if (!ruleforge_value_equals(&left->as.list->items[i], &right->as.list->items[i])) {
        return FALSE;
      }
    }

    return TRUE;
  case RVK_STYLE:
    return left->as.style == right->as.style;
  case RVK_RECORD:
    return left->as.record == right->as.record;
  case RVK_DATASET:
    return left->as.dataset == right->as.dataset;
  case RVK_RULESET:
    return left->as.ruleset == right->as.ruleset;
  case RVK_THEME:
    return left->as.theme == right->as.theme;
  case RVK_MODULE:
    return left->as.module == right->as.module;
  case RVK_INT:
  case RVK_FLOAT:
  case RVK_COUNT:
    break;
  }

  return FALSE;
}

void ruleforge_value_text_write(const RuleforgeValue *value, NexusStringBuilder *out) {
  uint32 i;

  switch (value->kind) {
  case RVK_NONE:
    (void)nexus_strings_string_builder_append(out, "none");
    break;
  case RVK_INT:
    (void)nexus_strings_string_builder_append_formatted(out, "%lld", (long long)value->as.integer);
    break;
  case RVK_FLOAT:
    (void)nexus_strings_string_builder_append_formatted(out, "%.10g", (double)value->as.real);
    break;
  case RVK_BOOL:
    (void)nexus_strings_string_builder_append(out, value->as.flag ? "true" : "false");
    break;
  case RVK_STRING:
    (void)nexus_strings_string_builder_append(out, value->as.string);
    break;
  case RVK_COLOR:
    (void)nexus_strings_string_builder_append_formatted(out, "#%08lX", (unsigned long)value->as.color);
    break;
  case RVK_LIST:
    (void)nexus_strings_string_builder_append(out, "[");

    for (i = 0; i < value->as.list->count; i++) {
      if (i > 0) {
        (void)nexus_strings_string_builder_append(out, ", ");
      }

      ruleforge_value_text_write(&value->as.list->items[i], out);
    }

    (void)nexus_strings_string_builder_append(out, "]");
    break;
  default:
    (void)nexus_strings_string_builder_append(out, ruleforge_value_type_name_get(value->kind));
    break;
  }
}

void ruleforge_value_json_write(const RuleforgeValue *value, NexusStringBuilder *out) {
  uint32 i;

  switch (value->kind) {
  case RVK_NONE:
    (void)nexus_strings_string_builder_append(out, "null");
    break;
  case RVK_INT:
    (void)nexus_strings_string_builder_append_formatted(out, "%lld", (long long)value->as.integer);
    break;
  case RVK_FLOAT:
    (void)nexus_strings_string_builder_append_formatted(out, "%.10g", (double)value->as.real);
    break;
  case RVK_BOOL:
    (void)nexus_strings_string_builder_append(out, value->as.flag ? "true" : "false");
    break;
  case RVK_STRING:
    (void)nexus_json_quote_append(out, value->as.string);
    break;
  case RVK_COLOR:
    (void)nexus_strings_string_builder_append_formatted(out, "\"#%08lX\"", (unsigned long)value->as.color);
    break;
  case RVK_LIST:
    (void)nexus_strings_string_builder_append(out, "[");

    for (i = 0; i < value->as.list->count; i++) {
      if (i > 0) {
        (void)nexus_strings_string_builder_append(out, ",");
      }

      ruleforge_value_json_write(&value->as.list->items[i], out);
    }

    (void)nexus_strings_string_builder_append(out, "]");
    break;
  case RVK_RECORD:
    (void)nexus_strings_string_builder_append(out, "{");

    for (i = 0; i < value->as.record->count; i++) {
      if (i > 0) {
        (void)nexus_strings_string_builder_append(out, ",");
      }

      (void)nexus_json_quote_append(out, value->as.record->names[i]);
      (void)nexus_strings_string_builder_append(out, ":");
      ruleforge_value_json_write(&value->as.record->values[i], out);
    }

    (void)nexus_strings_string_builder_append(out, "}");
    break;
  default:
    (void)nexus_json_quote_append(out, ruleforge_value_type_name_get(value->kind));
    break;
  }
}

RuleforgeList *ruleforge_value_list_create(RuleforgeCompilation *compilation, uint32 count) {
  RuleforgeList *list;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_value_list_create: NULL compilation");

  list        = (RuleforgeList *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*list));
  list->count = count;
  list->items = (RuleforgeValue *)nexus_data_arena_allocate(&compilation->arena, ((uint_large)count + 1U) * NEXUS_SIZEOF(RuleforgeValue));

  return list;
}

RuleforgeRecord *ruleforge_value_record_create(RuleforgeCompilation *compilation, uint32 count) {
  RuleforgeRecord *record;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_value_record_create: NULL compilation");

  record          = (RuleforgeRecord *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*record));
  record->count   = count;
  record->names   = (const char **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)count + 1U) * NEXUS_SIZEOF(const char *));
  record->values  = (RuleforgeValue *)nexus_data_arena_allocate(&compilation->arena, ((uint_large)count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
  record->dataset = 0xFFFFFFFFU;
  record->row     = 0;

  return record;
}

const RuleforgeValue *ruleforge_value_record_field_get(const RuleforgeRecord *record, const char *name) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(record != NULL && name != NULL, "ruleforge_value_record_field_get: NULL argument");

  for (i = 0; i < record->count; i++) {
    if (nexus_strings_string_equals(record->names[i], name)) {
      return &record->values[i];
    }
  }

  return NULL;
}

RuleforgeRuleset *ruleforge_value_ruleset_create(RuleforgeCompilation *compilation, const char *name) {
  RuleforgeRuleset *ruleset;

  ruleset       = (RuleforgeRuleset *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*ruleset));
  ruleset->name = name;

  return ruleset;
}

void ruleforge_value_ruleset_rule_add(RuleforgeCompilation *compilation, RuleforgeRuleset *ruleset, RuleforgeRule *rule) {
  RuleforgeRule **grown;
  uint32          capacity;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && ruleset != NULL && rule != NULL, "ruleforge_value_ruleset_rule_add: NULL argument");

  if (ruleset->count == ruleset->capacity) {
    capacity = ruleset->capacity == 0 ? 8 : ruleset->capacity * 2;
    grown    = (RuleforgeRule **)nexus_data_arena_allocate(&compilation->arena, (uint_large)capacity * NEXUS_SIZEOF(RuleforgeRule *));

    if (ruleset->count > 0) {
      nexus_memory_bytes_copy((const void *)ruleset->rules, (void *)grown, (uint_large)ruleset->count * NEXUS_SIZEOF(RuleforgeRule *));
    }

    ruleset->rules    = grown;
    ruleset->capacity = capacity;
  }

  ruleset->rules[ruleset->count] = rule;
  ruleset->count++;
}
