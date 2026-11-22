#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/* ---------------------------------------------------------------------------- */
/* LIFECYCLE                                                                    */
/* ---------------------------------------------------------------------------- */

RuleforgeSettings *ruleforge_settings_create(void) {
  RuleforgeSettings *settings;

  settings = (RuleforgeSettings *)nexus_memory_allocate(NEXUS_SIZEOF(*settings));
  nexus_data_arena_initialize(&settings->arena);
  settings->values      = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  settings->datasets    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  settings->filter      = NULL;
  settings->step_limit  = 50000000ULL;
  settings->rule_limit  = 100000U;
  settings->depth_limit = 64U;

  return settings;
}

void ruleforge_settings_destroy(RuleforgeSettings *settings) {
  NEXUS_PROGRAM_PRECONDITION(settings != NULL, "ruleforge_settings_destroy: NULL settings");

  nexus_data_list_destroy(settings->values);
  nexus_data_list_destroy(settings->datasets);
  nexus_data_arena_release(&settings->arena);
  nexus_memory_deallocate(settings);
}

static char *r_settings_copy(RuleforgeSettings *settings, const char *text) {
  return nexus_data_arena_text_copy(&settings->arena, text, nexus_strings_string_length_get(text));
}

/* ---------------------------------------------------------------------------- */
/* VALUES                                                                       */
/* ---------------------------------------------------------------------------- */

/* Summary: the typed value of settings text: true/false, none, an integer, a number, else a string. */
static RuleforgeValue r_settings_value_parse(RuleforgeSettings *settings, const char *text) {
  uint_large length;
  uint_large i;
  boolean    digits;
  boolean    fraction;
  uint_large start;
  f_real     number;
  f_real     scale;
  int64      integer;

  length = nexus_strings_string_length_get(text);

  if (nexus_strings_string_equals(text, "true")) {
    return ruleforge_value_flag(TRUE);
  }

  if (nexus_strings_string_equals(text, "false")) {
    return ruleforge_value_flag(FALSE);
  }

  if (nexus_strings_string_equals(text, "none")) {
    return ruleforge_value_none();
  }

  start    = length > 0 && text[0] == '-' ? 1 : 0;
  digits   = FALSE;
  fraction = FALSE;
  number   = 0.0;
  scale    = 1.0;
  integer  = 0;

  for (i = start; i < length; i++) {
    if (text[i] >= '0' && text[i] <= '9') {
      digits = TRUE;

      if (fraction) {
        scale *= 0.1;
        number += (f_real)(text[i] - '0') * scale;
      } else {
        number  = (number * 10.0) + (f_real)(text[i] - '0');
        integer = (integer * 10) + (int64)(text[i] - '0');
      }
    } else if (text[i] == '.' && !fraction && digits) {
      fraction = TRUE;
    } else {
      return ruleforge_value_string(r_settings_copy(settings, text));
    }
  }

  if (!digits || (fraction && i > 0 && text[i - 1] == '.')) {
    return ruleforge_value_string(r_settings_copy(settings, text));
  }

  if (fraction) {
    return ruleforge_value_real(start == 1 ? -number : number);
  }

  return ruleforge_value_integer(start == 1 ? -integer : integer);
}

void ruleforge_settings_value_set(RuleforgeSettings *settings, const char *key, const char *text) {
  RuleforgeSettingValue *entry;
  uint_large             i;

  NEXUS_PROGRAM_PRECONDITION(settings != NULL && key != NULL && text != NULL, "ruleforge_settings_value_set: NULL argument");

  for (i = 0; i < nexus_data_list_length_get(settings->values); i++) {
    entry = (RuleforgeSettingValue *)nexus_data_list_pointer_at(settings->values, i);

    if (nexus_strings_string_equals(entry->key, key)) {
      entry->value = r_settings_value_parse(settings, text);

      return;
    }
  }

  entry        = (RuleforgeSettingValue *)nexus_data_arena_allocate(&settings->arena, NEXUS_SIZEOF(*entry));
  entry->key   = r_settings_copy(settings, key);
  entry->value = r_settings_value_parse(settings, text);
  (void)nexus_data_list_append_pointer(settings->values, entry);
}

void ruleforge_settings_dataset_set(RuleforgeSettings *settings, const char *source, const char *provider, const char *path, const char *root) {
  RuleforgeSettingDataset *entry;
  uint_large               i;

  NEXUS_PROGRAM_PRECONDITION(settings != NULL && source != NULL && provider != NULL && path != NULL, "ruleforge_settings_dataset_set: NULL argument");

  entry = NULL;

  for (i = 0; i < nexus_data_list_length_get(settings->datasets); i++) {
    entry = (RuleforgeSettingDataset *)nexus_data_list_pointer_at(settings->datasets, i);

    if (nexus_strings_string_equals(entry->source, source)) {
      break;
    }

    entry = NULL;
  }

  if (entry == NULL) {
    entry         = (RuleforgeSettingDataset *)nexus_data_arena_allocate(&settings->arena, NEXUS_SIZEOF(*entry));
    entry->source = r_settings_copy(settings, source);
    (void)nexus_data_list_append_pointer(settings->datasets, entry);
  }

  entry->provider  = r_settings_copy(settings, provider);
  entry->path      = r_settings_copy(settings, path);
  entry->root      = r_settings_copy(settings, root == NULL ? "" : root);
  entry->delimiter = r_settings_copy(settings, ",");
}

void ruleforge_settings_filter_name_set(RuleforgeSettings *settings, const char *name) {
  NEXUS_PROGRAM_PRECONDITION(settings != NULL && name != NULL, "ruleforge_settings_filter_name_set: NULL argument");

  settings->filter = r_settings_copy(settings, name);
  ruleforge_settings_value_set(settings, "filter", name);
}

void ruleforge_settings_limits_set(RuleforgeSettings *settings, uint64 steps, uint32 rules, uint32 depth) {
  NEXUS_PROGRAM_PRECONDITION(settings != NULL, "ruleforge_settings_limits_set: NULL settings");

  if (steps != 0) {
    settings->step_limit = steps;
  }

  if (rules != 0) {
    settings->rule_limit = rules;
  }

  if (depth != 0) {
    settings->depth_limit = depth;
  }
}

/* ---------------------------------------------------------------------------- */
/* SETTINGS FILE                                                                */
/* ---------------------------------------------------------------------------- */

/* Summary: finds or creates the dataset source entry and sets one of its fields (provider, path, root). */
static void r_settings_dataset_field_set(RuleforgeSettings *settings, const char *source, const char *field, const char *value) {
  RuleforgeSettingDataset *entry;
  uint_large               i;

  entry = NULL;

  for (i = 0; i < nexus_data_list_length_get(settings->datasets); i++) {
    entry = (RuleforgeSettingDataset *)nexus_data_list_pointer_at(settings->datasets, i);

    if (nexus_strings_string_equals(entry->source, source)) {
      break;
    }

    entry = NULL;
  }

  if (entry == NULL) {
    entry            = (RuleforgeSettingDataset *)nexus_data_arena_allocate(&settings->arena, NEXUS_SIZEOF(*entry));
    entry->source    = r_settings_copy(settings, source);
    entry->provider  = r_settings_copy(settings, "");
    entry->path      = r_settings_copy(settings, "");
    entry->root      = r_settings_copy(settings, "");
    entry->delimiter = r_settings_copy(settings, ",");
    (void)nexus_data_list_append_pointer(settings->datasets, entry);
  }

  if (nexus_strings_string_equals(field, "provider")) {
    entry->provider = r_settings_copy(settings, value);
  } else if (nexus_strings_string_equals(field, "path")) {
    entry->path = r_settings_copy(settings, value);
  } else if (nexus_strings_string_equals(field, "root")) {
    entry->root = r_settings_copy(settings, value);
  }
}

boolean ruleforge_settings_toml_load(RuleforgeRuntime *runtime, RuleforgeSettings *settings, const char *path, char *error, uint_large error_size) {
  byte          *bytes;
  uint_large     size;
  RuleforgeToml *toml;
  uint32         i;
  uint32         k;
  char           message[256];

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && settings != NULL && path != NULL && error != NULL, "ruleforge_settings_toml_load: NULL argument");

  error[0] = '\0';

  if (!ruleforge_source_file_read(path, &bytes, &size)) {
    (void)stbsp_snprintf(error, (int)error_size, "cannot read %s", path);

    return FALSE;
  }

  toml = ruleforge_toml_parse(runtime, &settings->arena, (const char *)bytes, size, message, NEXUS_SIZEOF(message));
  nexus_memory_deallocate(bytes);

  if (toml == NULL) {
    (void)stbsp_snprintf(error, (int)error_size, "%s: %s", path, message);

    return FALSE;
  }

  for (i = 0; i < toml->count; i++) {
    const RuleforgeTomlEntry *entry;
    const char               *group;
    RuleforgeValue            value;
    char                      joined[512];
    uint_large                length;

    entry = toml->entries[i];
    group = entry->path[0];

    if (nexus_strings_string_equals(group, "context") && entry->path_count >= 2) {
      length    = 0;
      joined[0] = '\0';

      for (k = 1; k < entry->path_count; k++) {
        length += (uint_large)stbsp_snprintf(joined + length, (int)(NEXUS_SIZEOF(joined) - length), "%s%s", k > 1 ? "." : "", entry->path[k]);
      }

      /* A TOML string stays a string even when it looks like a number; numbers and booleans take their natural type. */
      if (entry->kind == RVK_STRING) {
        value = ruleforge_value_string(r_settings_copy(settings, entry->text));
        {
          RuleforgeSettingValue *stored;
          uint_large             j;
          boolean                found;

          found = FALSE;

          for (j = 0; j < nexus_data_list_length_get(settings->values); j++) {
            stored = (RuleforgeSettingValue *)nexus_data_list_pointer_at(settings->values, j);

            if (nexus_strings_string_equals(stored->key, joined)) {
              stored->value = value;
              found         = TRUE;
            }
          }

          if (!found) {
            stored        = (RuleforgeSettingValue *)nexus_data_arena_allocate(&settings->arena, NEXUS_SIZEOF(*stored));
            stored->key   = r_settings_copy(settings, joined);
            stored->value = value;
            (void)nexus_data_list_append_pointer(settings->values, stored);
          }
        }
      } else {
        char       digits[64];
        uint_large out;

        out = 0;

        for (k = 0; entry->text[k] != '\0' && out + 1 < NEXUS_SIZEOF(digits); k++) {
          if (entry->text[k] != '_') {
            digits[out++] = entry->text[k];
          }
        }

        digits[out] = '\0';
        ruleforge_settings_value_set(settings, joined, digits);
      }
    } else if (nexus_strings_string_equals(group, "datasets") && entry->path_count == 3 && entry->kind == RVK_STRING) {
      r_settings_dataset_field_set(settings, entry->path[1], entry->path[2], entry->text);
    } else if (nexus_strings_string_equals(group, "limits") && entry->path_count == 2 && entry->kind == RVK_INT) {
      value = r_settings_value_parse(settings, entry->text);

      if (nexus_strings_string_equals(entry->path[1], "steps")) {
        ruleforge_settings_limits_set(settings, (uint64)value.as.integer, 0, 0);
      } else if (nexus_strings_string_equals(entry->path[1], "rules")) {
        ruleforge_settings_limits_set(settings, 0, (uint32)value.as.integer, 0);
      } else if (nexus_strings_string_equals(entry->path[1], "depth")) {
        ruleforge_settings_limits_set(settings, 0, 0, (uint32)value.as.integer);
      }
    } else if (nexus_strings_string_equals(group, "filter") && entry->path_count == 2 && nexus_strings_string_equals(entry->path[1], "name") &&
               entry->kind == RVK_STRING) {
      ruleforge_settings_filter_name_set(settings, entry->text);
    }
  }

  return TRUE;
}
