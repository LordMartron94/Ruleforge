#include <nexus.h>

#include "tool.h"

void logos_tool_text_initialize(LToolText *text) {
  text->builder = nexus_strings_string_builder_create_growable(1024);
}

void logos_tool_text_add(LToolText *text, const char *value) {
  NexusError *error;

  error = nexus_strings_string_builder_append(&text->builder, value);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }
}

void logos_tool_text_check(NexusError *error) {
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }
}

const char *logos_tool_text_data_get(const LToolText *text) {
  return nexus_strings_string_builder_string_get(&text->builder);
}

uint_large logos_tool_text_length_get(const LToolText *text) {
  return nexus_strings_string_builder_length_get(&text->builder);
}

void logos_tool_text_release(LToolText *text) {
  nexus_strings_string_builder_destroy(&text->builder);
}

const char *logos_tool_string_find_byte(const char *text, char value) {
  for (; *text != '\0'; text++) {
    if (*text == value) {
      return text;
    }
  }

  return NULL;
}

const char *logos_tool_string_find_last_byte(const char *text, char value) {
  const char *found;

  found = NULL;

  for (; *text != '\0'; text++) {
    if (*text == value) {
      found = text;
    }
  }

  return found;
}

const char *logos_tool_bytes_find(const char *bytes, char value, uint_large size) {
  uint_large i;

  for (i = 0; i < size; i++) {
    if (bytes[i] == value) {
      return bytes + i;
    }
  }

  return NULL;
}

static char *l_tool_identifier_map(const char *name, int mode) {
  uint_large length;
  uint_large i;
  char      *result;
  char       letter;

  length = nexus_strings_string_length_get(name);
  result = (char *)nexus_memory_allocate(length + 1);

  for (i = 0; i < length; i++) {
    letter = name[i];

    if ((letter >= 'a' && letter <= 'z') && mode == 1) {
      letter = (char)(letter - 'a' + 'A');
    } else if ((letter >= 'A' && letter <= 'Z') && mode == 0) {
      letter = (char)(letter - 'A' + 'a');
    } else if (!((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '_')) {
      letter = '_';
    }

    result[i] = letter;
  }

  result[length] = '\0';

  return result;
}

char *logos_tool_identifier_lower(const char *name) {
  return l_tool_identifier_map(name, 0);
}

char *logos_tool_identifier_upper(const char *name) {
  return l_tool_identifier_map(name, 1);
}

char *logos_tool_identifier_camel(const char *name) {
  uint_large length;
  uint_large i;
  uint_large out;
  char      *result;
  boolean    start;

  length = nexus_strings_string_length_get(name);
  result = (char *)nexus_memory_allocate(length + 1);
  out    = 0;
  start  = TRUE;

  for (i = 0; i < length; i++) {
    if (name[i] == '_') {
      start = TRUE;
      continue;
    }

    if (start && name[i] >= 'a' && name[i] <= 'z') {
      result[out++] = (char)(name[i] - 'a' + 'A');
    } else {
      result[out++] = name[i];
    }

    start = FALSE;
  }

  result[out] = '\0';

  return result;
}
