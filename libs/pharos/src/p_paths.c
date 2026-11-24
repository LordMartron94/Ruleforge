#include <nexus.h>

#include "./p_internal.h"

char *pharos_paths_text_copy(const char *text) {
  uint_large length;
  char      *copy;

  length = nexus_strings_string_length_get(text);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length + 1);

  return copy;
}

static int p_paths_hex(char digit) {
  if (digit >= '0' && digit <= '9') {
    return digit - '0';
  }

  if (digit >= 'a' && digit <= 'f') {
    return digit - 'a' + 10;
  }

  if (digit >= 'A' && digit <= 'F') {
    return digit - 'A' + 10;
  }

  return -1;
}

/* Summary: collapses "//", "/./" and "/x/../" in place, keeping a leading "/" and any leading "../" of a relative path. */
void pharos_paths_path_normalize(char *path) {
  uint_large length;
  uint_large read;
  uint_large write;
  boolean    absolute;

  length   = nexus_strings_string_length_get(path);
  absolute = length > 0 && path[0] == '/';
  read     = 0;
  write    = 0;

  while (read < length) {
    uint_large end;

    while (read < length && path[read] == '/') {
      read++;
    }

    end = read;

    while (end < length && path[end] != '/') {
      end++;
    }

    if (end == read) {
      break;
    }

    if ((end - read == 1 && path[read] == '.') || (end - read == 2 && path[read] == '.' && path[read + 1] == '.' && absolute && write == 0)) {
      /* "." and a ".." above the root are dropped. */
    } else if (end - read == 2 && path[read] == '.' && path[read + 1] == '.' && write > 0 &&
               !(write >= 2 && path[write - 1] == '.' && path[write - 2] == '.') && !(write == 2 && path[0] == '.' && path[1] == '.')) {
      /* Drop the previous segment. */
      while (write > 0 && path[write - 1] != '/') {
        write--;
      }

      if (write > 0) {
        write--;
      }
    } else {
      if (write > 0 || absolute) {
        path[write++] = '/';
      }

      while (read < end) {
        path[write++] = path[read++];
      }
    }

    read = end;
  }

  if (write == 0 && absolute) {
    path[write++] = '/';
  }

  path[write] = '\0';
}

char *pharos_paths_path_of_uri(const char *uri) {
  const char *cursor;
  char       *path;
  uint_large  out;

  cursor = uri;

  if (nexus_strings_string_starts_with(uri, "file://")) {
    cursor = uri + 7;
  }

  path = (char *)nexus_memory_allocate(nexus_strings_string_length_get(cursor) + 1);
  out  = 0;

  while (*cursor != '\0') {
    if (*cursor == '%' && p_paths_hex(cursor[1]) >= 0 && p_paths_hex(cursor[2]) >= 0) {
      path[out++] = (char)((p_paths_hex(cursor[1]) * 16) + p_paths_hex(cursor[2]));
      cursor += 3;
    } else {
      path[out++] = *cursor++;
    }
  }

  path[out] = '\0';
  pharos_paths_path_normalize(path);

  return path;
}

char *pharos_paths_uri_of_path(const char *path) {
  NexusStringBuilder out;
  char              *result;
  const char        *cursor;

  out = nexus_strings_string_builder_create_growable(64);
  (void)nexus_strings_string_builder_append(&out, "file://");

  for (cursor = path; *cursor != '\0'; cursor++) {
    char letter;

    letter = *cursor;

    if ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '/' || letter == '-' ||
        letter == '_' || letter == '.' || letter == '~') {
      (void)nexus_strings_string_builder_append_formatted(&out, "%c", letter);
    } else {
      (void)nexus_strings_string_builder_append_formatted(&out, "%%%02X", (unsigned)(byte)letter);
    }
  }

  result = pharos_paths_text_copy(nexus_strings_string_builder_string_get(&out));
  nexus_strings_string_builder_destroy(&out);

  return result;
}

char *pharos_paths_bytes_copy(const char *text, uint_large length) {
  char *copy;

  copy = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length);
  copy[length] = '\0';

  return copy;
}
