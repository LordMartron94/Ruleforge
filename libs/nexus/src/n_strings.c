#include <string.h>
#include "../nexus.h"

boolean nexus_strings_string_starts_with(const char *string, const char *prefix) {
  return strncmp(string, prefix, strlen(prefix)) == 0;
}

boolean nexus_strings_string_ends_with(const char *string, const char *suffix) {
  uint64 string_length = nexus_strings_string_length_get(string);
  uint64 suffix_length = nexus_strings_string_length_get(suffix);

  if (suffix_length > string_length)
    return 0;

  return strcmp(string + (string_length - suffix_length), suffix) == 0;
}

boolean nexus_strings_string_contains(const char *string, const char *substring) {
  return strstr(string, substring) != NULL;
}

const char *nexus_strings_string_find(const char *string, const char *substring) {
  return strstr(string, substring);
}

const char *nexus_strings_string_find_last(const char *string, const char *substring) {
  const char *cursor     = string;
  const char *last_match = NULL;

  if (*substring == '\0')
    return string + nexus_strings_string_length_get(string);

  while ((cursor = strstr(cursor, substring)) != NULL) {
    last_match = cursor;
    ++cursor;
  }

  return last_match;
}

static NexusError *_nexus_strings_string_builder_ensure_capacity(NexusStringBuilder *builder, uint_large additional_length) {
  uint_large  required_capacity;
  uint_large  new_capacity;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("_nexus_strings_string_builder_ensure_capacity");

  required_capacity = builder->length + additional_length + 1; /* +1 for the null terminator. */

  /* A growable builder starts with no buffer and its capacity is only the hint for the first allocation, so a missing buffer always needs one. */
  if (builder->buffer != NULL && required_capacity <= builder->capacity) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return &nexus_program_errors_success;
  }

  if (builder->mode == NSBM_FIXED) {
    error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_NEXUS_STRINGS_ERROR_CAPACITY_EXCEEDED, "string builder capacity exceeded: need %llu, have %llu",
                                        (unsigned long long)required_capacity, (unsigned long long)builder->capacity);
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  new_capacity = (builder->capacity == 0) ? 64 : builder->capacity;
  while (new_capacity < required_capacity) {
    NEXUS_PROGRAM_PRECONDITION_ALWAYS(new_capacity <= UINT_LARGE_MAX_VAL / 2, "string builder capacity growth overflowed");
    new_capacity *= 2;
  }

  if (builder->buffer == NULL) {
    builder->buffer = (char *)nexus_memory_allocate(new_capacity);
  } else {
    builder->buffer = (char *)nexus_memory_reallocate(builder->buffer, new_capacity);
  }

  builder->capacity = new_capacity;

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_strings_string_builder_append(NexusStringBuilder *builder, const char *text) {
  uint_large  text_length;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_strings_string_builder_append");

  text_length = nexus_strings_string_length_get(text);

  error = _nexus_strings_string_builder_ensure_capacity(builder, text_length);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  nexus_memory_bytes_copy(text, builder->buffer + builder->length, text_length);
  builder->length += text_length;
  builder->buffer[builder->length] = '\0';

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

NexusError *nexus_strings_string_builder_append_formatted(NexusStringBuilder *builder, const char *format, ...) {
  va_list     args;
  int         required_length;
  NexusError *error;

  NEXUS_PROGRAM_CONTEXT_PUSH("nexus_strings_string_builder_append_formatted");

  /* Two independent va_start/va_end pairs on this function's own "..." -- not a reuse of one
     va_list, so no va_copy is needed (see nexus.h's STRINGS section note on why a shared
     va_list can't be walked twice under this project's -std=c89). */
  va_start(args, format);
  required_length = stbsp_vsnprintf(NULL, 0, format, args);
  va_end(args);

  NEXUS_PROGRAM_PRECONDITION(required_length >= 0, "string_builder_append_formatted: stbsp_vsnprintf reported a negative required length");

  error = _nexus_strings_string_builder_ensure_capacity(builder, (uint_large)required_length);
  if (error->code != GLOBAL_NEXUS_ERROR_SUCCESS) {
    NEXUS_PROGRAM_CONTEXT_POP();
    return error;
  }

  va_start(args, format);
  (void)stbsp_vsnprintf(builder->buffer + builder->length, (int)((uint_large)required_length + 1), format, args);
  va_end(args);

  builder->length += (uint_large)required_length;

  NEXUS_PROGRAM_CONTEXT_POP();
  return &nexus_program_errors_success;
}

static const char *const n_strings_bytes_units[]    = {"B", "KiB", "MiB", "GiB", "TiB"};
static const char *const n_strings_quantity_units[] = {"", "K", "M", "G", "T"};

/*
Summary: internal -- shared tiered-unit scaling behind bytes_format/quantity_format's f_real variants (dividing by base until magnitude fits one digit group, or the last unit is reached).
*/
static NexusStringFormatResult _nexus_strings_scaled_quantity_format(char *string, uint_large max_string_length, f_real value, uint32 decimal_places,
                                                                     real64 base, const char *const *unit_suffixes, uint32 unit_count) {
  real64  magnitude;
  uint32  tier;
  boolean is_negative;

  if (decimal_places > 9) {
    decimal_places = 9;
  }

  if (!nexus_real_is_finite(value)) {
    if (value != value) {
      return nexus_strings_string_format_with_truncation(string, max_string_length, "NaN");
    }
    return nexus_strings_string_format_with_truncation(string, max_string_length, (value > 0) ? "Inf" : "-Inf");
  }

  is_negative = value < 0;
  magnitude   = is_negative ? -(real64)value : (real64)value;

  tier = 0;
  while (magnitude >= base && tier + 1 < unit_count) {
    magnitude /= base;
    tier++;
  }

  return nexus_strings_string_format_with_truncation(string, max_string_length, "%s%.*f%s", is_negative ? "-" : "", (int)decimal_places, magnitude,
                                                     unit_suffixes[tier]);
}

NexusStringFormatResult nexus_strings_bytes_format(char *string, uint_large max_string_length, uint64 byte_count) {
  return _nexus_strings_scaled_quantity_format(string, max_string_length, (f_real)byte_count, 2, 1024.0, n_strings_bytes_units,
                                               NEXUS_SIZEOF(n_strings_bytes_units) / NEXUS_SIZEOF(n_strings_bytes_units[0]));
}

NexusStringFormatResult nexus_strings_bytes_format_f_real(char *string, uint_large max_string_length, f_real byte_count) {
  return _nexus_strings_scaled_quantity_format(string, max_string_length, byte_count, 2, 1024.0, n_strings_bytes_units,
                                               NEXUS_SIZEOF(n_strings_bytes_units) / NEXUS_SIZEOF(n_strings_bytes_units[0]));
}

NexusStringFormatResult nexus_strings_quantity_format(char *string, uint_large max_string_length, uint64 value) {
  return _nexus_strings_scaled_quantity_format(string, max_string_length, (f_real)value, 3, 1000.0, n_strings_quantity_units,
                                               NEXUS_SIZEOF(n_strings_quantity_units) / NEXUS_SIZEOF(n_strings_quantity_units[0]));
}

NexusStringFormatResult nexus_strings_quantity_format_f_real(char *string, uint_large max_string_length, f_real value, uint32 decimal_places) {
  return _nexus_strings_scaled_quantity_format(string, max_string_length, value, decimal_places, 1000.0, n_strings_quantity_units,
                                               NEXUS_SIZEOF(n_strings_quantity_units) / NEXUS_SIZEOF(n_strings_quantity_units[0]));
}

NexusStringFormatResult nexus_strings_percent_format_f_real(char *string, uint_large max_string_length, f_real percent_value, uint32 decimal_places) {
  if (decimal_places > 9) {
    decimal_places = 9;
  }

  if (!nexus_real_is_finite(percent_value)) {
    if (percent_value != percent_value) {
      return nexus_strings_string_format_with_truncation(string, max_string_length, "NaN%%");
    }
    return nexus_strings_string_format_with_truncation(string, max_string_length, (percent_value > 0) ? "Inf%%" : "-Inf%%");
  }

  return nexus_strings_string_format_with_truncation(string, max_string_length, "%.*f%%", (int)decimal_places, (real64)percent_value);
}

/* ---------------------------------------------------------------------------- */
/* EDIT DISTANCE AND SUGGESTIONS                                                 */
/* ---------------------------------------------------------------------------- */

#define NEXUS_STRINGS_DISTANCE_LENGTH_MAX 255

uint32 nexus_strings_string_edit_distance(const char *left, const char *right) {
  uint32    *matrix;
  uint_large width;
  uint_large left_length;
  uint_large right_length;
  uint_large i;
  uint_large j;
  uint32     cost;
  uint32     best;

  NEXUS_PROGRAM_PRECONDITION(left != NULL && right != NULL, "strings must not be NULL");

  left_length  = nexus_strings_string_length_get(left);
  right_length = nexus_strings_string_length_get(right);

  if (left_length > NEXUS_STRINGS_DISTANCE_LENGTH_MAX || right_length > NEXUS_STRINGS_DISTANCE_LENGTH_MAX) {
    return 0xFFFFFFFFU;
  }

  width  = right_length + 1;
  matrix = (uint32 *)nexus_memory_allocate((left_length + 1) * width * NEXUS_SIZEOF(uint32));

  for (i = 0; i <= left_length; i++) {
    matrix[i * width] = (uint32)i;
  }

  for (j = 0; j <= right_length; j++) {
    matrix[j] = (uint32)j;
  }

  for (i = 1; i <= left_length; i++) {
    for (j = 1; j <= right_length; j++) {
      cost = left[i - 1] == right[j - 1] ? 0 : 1;
      best = matrix[(i - 1) * width + (j - 1)] + cost;

      if (matrix[(i - 1) * width + (j)] + 1 < best) {
        best = matrix[(i - 1) * width + (j)] + 1;
      }

      if (matrix[(i)*width + (j - 1)] + 1 < best) {
        best = matrix[(i)*width + (j - 1)] + 1;
      }

      if (i > 1 && j > 1 && left[i - 1] == right[j - 2] && left[i - 2] == right[j - 1] && matrix[(i - 2) * width + (j - 2)] + 1 < best) {
        best = matrix[(i - 2) * width + (j - 2)] + 1;
      }

      matrix[(i)*width + (j)] = best;
    }
  }

  best = matrix[left_length * width + right_length];
  nexus_memory_deallocate(matrix);

  return best;
}

const char *nexus_strings_string_closest(const char *name, const char *const *candidates, uint_large count) {
  const char *best;
  uint32      best_distance;
  uint32      distance;
  uint32      limit;
  uint_large  length;
  uint_large  i;

  NEXUS_PROGRAM_PRECONDITION(name != NULL && (candidates != NULL || count == 0), "arguments must not be NULL");

  length = nexus_strings_string_length_get(name);
  limit  = (uint32)(length / 3);

  if (limit < 1) {
    limit = 1;
  } else if (limit > 3) {
    limit = 3;
  }

  best          = NULL;
  best_distance = limit + 1;

  for (i = 0; i < count; i++) {
    if (length >= 3 && nexus_strings_string_length_get(candidates[i]) > length && nexus_strings_string_starts_with(candidates[i], name)) {
      distance = 1;
    } else {
      distance = nexus_strings_string_edit_distance(name, candidates[i]);
    }

    if (distance <= limit && distance < best_distance) {
      best          = candidates[i];
      best_distance = distance;
    }
  }

  return best;
}

uint32 nexus_strings_utf8_encode(uint32 value, byte *out) {
  NEXUS_PROGRAM_PRECONDITION(out != NULL, "out must not be NULL");

  if (value < 0x80) {
    out[0] = (byte)value;

    return 1;
  }

  if (value < 0x800) {
    out[0] = (byte)(0xC0 | (value >> 6));
    out[1] = (byte)(0x80 | (value & 0x3F));

    return 2;
  }

  if (value < 0x10000) {
    out[0] = (byte)(0xE0 | (value >> 12));
    out[1] = (byte)(0x80 | ((value >> 6) & 0x3F));
    out[2] = (byte)(0x80 | (value & 0x3F));

    return 3;
  }

  out[0] = (byte)(0xF0 | (value >> 18));
  out[1] = (byte)(0x80 | ((value >> 12) & 0x3F));
  out[2] = (byte)(0x80 | ((value >> 6) & 0x3F));
  out[3] = (byte)(0x80 | (value & 0x3F));

  return 4;
}
