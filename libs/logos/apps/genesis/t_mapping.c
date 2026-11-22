#include <nexus.h>

#include "tool.h"

static char *l_tool_copy(const char *text, uint_large length) {
  char *copy;

  copy = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length);
  copy[length] = '\0';

  return copy;
}

void logos_tool_mapping_initialize(LToolMapping *mapping) {
  mapping->path       = NULL;
  mapping->settings   = nexus_data_list_create(NEXUS_SIZEOF(LToolMappingEntry), 8);
  mapping->scopes     = nexus_data_list_create(NEXUS_SIZEOF(LToolMappingEntry), 32);
  mapping->contexts   = nexus_data_list_create(NEXUS_SIZEOF(LToolMappingEntry), 8);
  mapping->symbols    = nexus_data_list_create(NEXUS_SIZEOF(LToolMappingEntry), 8);
  mapping->references = nexus_data_list_create(NEXUS_SIZEOF(LToolMappingEntry), 8);
}

static void l_tool_entries_release(NexusDataList *list) {
  uint_large         i;
  LToolMappingEntry *entry;

  for (i = 0; i < nexus_data_list_length_get(list); i++) {
    entry = (LToolMappingEntry *)nexus_data_list_at(list, i);
    nexus_memory_deallocate(entry->key);
    nexus_memory_deallocate(entry->value);
    nexus_memory_deallocate(entry->extra);
  }

  nexus_data_list_destroy(list);
}

void logos_tool_mapping_release(LToolMapping *mapping) {
  if (mapping->path != NULL) {
    nexus_memory_deallocate(mapping->path);
  }

  l_tool_entries_release(mapping->settings);
  l_tool_entries_release(mapping->scopes);
  l_tool_entries_release(mapping->contexts);
  l_tool_entries_release(mapping->symbols);
  l_tool_entries_release(mapping->references);
}

static void l_tool_entry_add(NexusDataList *list, const char *key, uint_large key_length, const char *value, uint_large value_length,
                             const char *extra, uint_large extra_length) {
  LToolMappingEntry entry;

  entry.key   = l_tool_copy(key, key_length);
  entry.value = l_tool_copy(value, value_length);
  entry.extra = l_tool_copy(extra, extra_length);
  (void)nexus_data_list_append(list, &entry);
}

static const char *l_tool_trim_start(const char *text) {
  while (*text == ' ' || *text == '\t') {
    text++;
  }

  return text;
}

/* Summary: the length of text without trailing blanks. */
static uint_large l_tool_trim_length(const char *text, uint_large length) {
  while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\t' || text[length - 1] == '\r')) {
    length--;
  }

  return length;
}

static boolean l_tool_fail(char *error, uint_large error_size, const char *path, uint32 line, const char *message) {
  (void)stbsp_snprintf(error, (int)(error_size), "%s:%u: %s", path, (unsigned)line, message);

  return FALSE;
}

boolean logos_tool_mapping_parse(LToolMapping *mapping, const char *path, const char *text, char *error, uint_large error_size) {
  const char             *cursor;
  const char             *line_end;
  const char             *start;
  const char             *equals;
  uint_large              length;
  uint_large              word_length;
  uint32                  line;
  const LToolMappingForm *form;

  mapping->path = l_tool_copy(path, nexus_strings_string_length_get(path));
  cursor        = text;
  line          = 0;

  while (*cursor != '\0') {
    line++;
    line_end = logos_tool_string_find_byte(cursor, '\n');
    line_end = line_end == NULL ? cursor + nexus_strings_string_length_get(cursor) : line_end;
    start    = l_tool_trim_start(cursor);
    length   = (uint_large)(line_end - start);

    /* A comment is a whole line starting with //; a value may contain // (a comment prefix is a setting). */
    if (length >= 2 && start[0] == '/' && start[1] == '/') {
      length = 0;
    }

    length = l_tool_trim_length(start, length);
    cursor = *line_end == '\n' ? line_end + 1 : line_end;

    if (length == 0) {
      continue;
    }

    /* The first word of a line selects its form, from the table the grammar of mapping files is written from. */
    word_length = 0;

    while (word_length < length && start[word_length] != ' ' && start[word_length] != '\t') {
      word_length++;
    }

    form = word_length < length ? logos_tool_mapping_form_find(start, word_length) : NULL;

    if (start[0] == '$') {
      uint_large key_length;

      equals = logos_tool_bytes_find(start, '=', length);

      if (equals == NULL) {
        return l_tool_fail(error, error_size, path, line, "a token scope is written `$token = scope.name`");
      }

      key_length = l_tool_trim_length(start + 1, (uint_large)(equals - start - 1));
      l_tool_entry_add(mapping->scopes, start + 1, key_length, l_tool_trim_start(equals + 1),
                       l_tool_trim_length(l_tool_trim_start(equals + 1), (uint_large)(start + length - l_tool_trim_start(equals + 1))), "", 0);
    } else if (form != NULL && form->id == LTOOL_FORM_RULE && l_tool_trim_start(start + word_length)[0] == '#') {
      const char *name;
      uint_large  name_length;

      /* A rule scope is a context scope keyed by the rule's name with its #. */
      name   = l_tool_trim_start(start + word_length);
      equals = logos_tool_bytes_find(name, '=', (uint_large)(start + length - name));

      if (equals == NULL) {
        return l_tool_fail(error, error_size, path, line, "a rule scope is written `rule #name = scope.name`");
      }

      name_length = l_tool_trim_length(name, (uint_large)(equals - name));
      l_tool_entry_add(mapping->contexts, name, name_length, l_tool_trim_start(equals + 1),
                       l_tool_trim_length(l_tool_trim_start(equals + 1), (uint_large)(start + length - l_tool_trim_start(equals + 1))), "", 0);
    } else if (form != NULL && form->id == LTOOL_FORM_CONTEXT) {
      const char *name;
      uint_large  name_length;

      name   = l_tool_trim_start(start + word_length);
      equals = logos_tool_bytes_find(name, '=', (uint_large)(start + length - name));

      if (equals == NULL) {
        return l_tool_fail(error, error_size, path, line, "a context scope is written `context NAME = scope.name`");
      }

      name_length = l_tool_trim_length(name, (uint_large)(equals - name));
      l_tool_entry_add(mapping->contexts, name, name_length, l_tool_trim_start(equals + 1),
                       l_tool_trim_length(l_tool_trim_start(equals + 1), (uint_large)(start + length - l_tool_trim_start(equals + 1))), "", 0);
    } else if (form != NULL && form->id == LTOOL_FORM_SYMBOL) {
      const char *rule;
      const char *name_option;
      const char *kind_option;
      const char *end;
      const char *word_end;
      char        rule_name[128];
      char        name_token[128];
      char        kind[64];

      rule = l_tool_trim_start(start + word_length);
      end  = start + length;

      if (*rule != '#') {
        return l_tool_fail(error, error_size, path, line, "a symbol names its rule first: `symbol #rule name=$token kind=function`");
      }

      word_end = rule;

      while (word_end < end && *word_end != ' ' && *word_end != '\t') {
        word_end++;
      }

      if ((uint_large)(word_end - rule - 1) >= sizeof(rule_name)) {
        return l_tool_fail(error, error_size, path, line, "the rule name is too long");
      }

      nexus_memory_bytes_copy(rule + 1, rule_name, (uint_large)(word_end - rule - 1));
      rule_name[word_end - rule - 1] = '\0';
      name_token[0]                  = '\0';
      kind[0]                        = '\0';
      name_option                    = nexus_strings_string_find(word_end, "name=$");
      kind_option                    = nexus_strings_string_find(word_end, "kind=");

      if (name_option == NULL || name_option >= end) {
        return l_tool_fail(error, error_size, path, line, "a symbol needs `name=$token`, the token holding its name");
      }

      {
        const char *value;
        uint_large  value_length;

        value        = name_option + 6;
        value_length = 0;

        while (value + value_length < end && value[value_length] != ' ' && value[value_length] != '\t' && value_length + 1 < sizeof(name_token)) {
          value_length++;
        }

        nexus_memory_bytes_copy(value, name_token, value_length);
        name_token[value_length] = '\0';
      }

      if (kind_option != NULL && kind_option < end) {
        const char *value;
        uint_large  value_length;

        value        = kind_option + 5;
        value_length = 0;

        while (value + value_length < end && value[value_length] != ' ' && value[value_length] != '\t' && value_length + 1 < sizeof(kind)) {
          value_length++;
        }

        nexus_memory_bytes_copy(value, kind, value_length);
        kind[value_length] = '\0';
      }

      l_tool_entry_add(mapping->symbols, rule_name, nexus_strings_string_length_get(rule_name), name_token,
                       nexus_strings_string_length_get(name_token), kind, nexus_strings_string_length_get(kind));
    } else if (form != NULL && form->id == LTOOL_FORM_REFERENCE) {
      const char *token;

      token = l_tool_trim_start(start + word_length);

      if (*token != '$') {
        return l_tool_fail(error, error_size, path, line, "a reference names a token: `reference $identifier`");
      }

      l_tool_entry_add(mapping->references, token + 1, l_tool_trim_length(token + 1, (uint_large)(start + length - token - 1)), "", 0, "", 0);
    } else {
      uint_large key_length;

      equals = logos_tool_bytes_find(start, '=', length);

      if (equals == NULL) {
        return l_tool_fail(error, error_size, path, line,
                           "expected `key = value`, `$token = scope`, `context NAME = scope`, `symbol ...` or `reference ...`");
      }

      key_length = l_tool_trim_length(start, (uint_large)(equals - start));
      l_tool_entry_add(mapping->settings, start, key_length, l_tool_trim_start(equals + 1),
                       l_tool_trim_length(l_tool_trim_start(equals + 1), (uint_large)(start + length - l_tool_trim_start(equals + 1))), "", 0);
    }
  }

  return TRUE;
}

boolean logos_tool_mapping_load(LToolMapping *mapping, const char *path, boolean *out_found, char *error, uint_large error_size) {
  byte      *bytes;
  uint_large size;
  boolean    ok;

  logos_tool_mapping_initialize(mapping);
  *out_found = FALSE;

  if (!logos_tool_files_read(path, &bytes, &size)) {
    return TRUE;
  }

  *out_found = TRUE;
  ok         = logos_tool_mapping_parse(mapping, path, (const char *)bytes, error, error_size);
  nexus_memory_deallocate(bytes);

  return ok;
}

static const char *l_tool_entries_find(const NexusDataList *list, const char *key) {
  uint_large               i;
  const LToolMappingEntry *entry;

  for (i = 0; i < nexus_data_list_length_get(list); i++) {
    entry = (const LToolMappingEntry *)nexus_data_list_at_const(list, i);

    if (nexus_strings_string_equals(entry->key, key)) {
      return entry->value;
    }
  }

  return NULL;
}

const char *logos_tool_mapping_setting_get(const LToolMapping *mapping, const char *key) {
  return l_tool_entries_find(mapping->settings, key);
}

const char *logos_tool_mapping_scope_get(const LToolMapping *mapping, const char *token) {
  return l_tool_entries_find(mapping->scopes, token);
}

const char *logos_tool_mapping_context_scope_get(const LToolMapping *mapping, const char *context) {
  return l_tool_entries_find(mapping->contexts, context);
}
