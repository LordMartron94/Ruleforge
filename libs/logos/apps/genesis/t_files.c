#include <nexus.h>

#include "tool.h"

boolean logos_tool_files_read(const char *path, byte **out_bytes, uint_large *out_size) {
  NexusPath        file_path;
  NexusFileHandle *file;
  uint_large       size;
  uint_large       read_size;
  boolean          reached_eof;
  byte            *bytes;

  file_path = nexus_filesystem_path_create(path);

  if (nexus_filesystem_file_open(&file_path, NFM_READ_BINARY, &file) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return FALSE;
  }

  if (nexus_filesystem_file_size_get(file, &size) != NEXUS_PROGRAM_ERROR_SUCCESS() || size > 0x40000000U) {
    (void)nexus_filesystem_file_close(file);

    return FALSE;
  }

  bytes       = (byte *)nexus_memory_allocate(size + 1);
  read_size   = 0;
  reached_eof = FALSE;

  if (size > 0 && nexus_filesystem_file_read(file, bytes, size, &read_size, &reached_eof) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    read_size = size + 1;
  }

  (void)nexus_filesystem_file_close(file);

  if (read_size != size) {
    nexus_memory_deallocate(bytes);

    return FALSE;
  }

  bytes[size] = '\0';
  *out_bytes  = bytes;
  *out_size   = size;

  return TRUE;
}

/* Summary: creates every directory of a path, ignoring those that exist. */
static void l_tool_directories_make(const char *directory) {
  char      *copy;
  uint_large length;
  uint_large i;
  NexusPath  path;

  length = nexus_strings_string_length_get(directory);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(directory, copy, length + 1);

  for (i = 1; i <= length; i++) {
    if (i == length || copy[i] == '/') {
      char saved;

      saved   = copy[i];
      copy[i] = '\0';
      path    = nexus_filesystem_path_create(copy);
      (void)nexus_filesystem_directory_create(&path);
      copy[i] = saved;
    }
  }

  nexus_memory_deallocate(copy);
}

boolean logos_tool_files_write_if_changed(const char *path, const char *text, uint_large size, boolean *out_changed) {
  byte            *existing;
  uint_large       existing_size;
  char            *directory;
  NexusPath        file_path;
  NexusFileHandle *file;
  uint_large       written;

  *out_changed = FALSE;

  if (logos_tool_files_read(path, &existing, &existing_size)) {
    if (existing_size == size && nexus_memory_bytes_equal(existing, text, size)) {
      nexus_memory_deallocate(existing);

      return TRUE;
    }

    nexus_memory_deallocate(existing);
  }

  directory = logos_tool_files_directory_of(path);
  l_tool_directories_make(directory);
  nexus_memory_deallocate(directory);

  file_path = nexus_filesystem_path_create(path);

  if (nexus_filesystem_file_open(&file_path, NFM_WRITE_BINARY, &file) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return FALSE;
  }

  if (size > 0 && (nexus_filesystem_file_write(file, (const byte *)text, size, &written) != NEXUS_PROGRAM_ERROR_SUCCESS() || written != size)) {
    (void)nexus_filesystem_file_close(file);

    return FALSE;
  }

  if (nexus_filesystem_file_close(file) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return FALSE;
  }

  *out_changed = TRUE;

  return TRUE;
}

char *logos_tool_files_join(const char *directory, const char *name) {
  uint_large directory_length;
  uint_large name_length;
  char      *result;

  if (name[0] == '/' || directory == NULL || directory[0] == '\0') {
    name_length = nexus_strings_string_length_get(name);
    result      = (char *)nexus_memory_allocate(name_length + 1);
    nexus_memory_bytes_copy(name, result, name_length + 1);

    return result;
  }

  directory_length = nexus_strings_string_length_get(directory);
  name_length      = nexus_strings_string_length_get(name);
  result           = (char *)nexus_memory_allocate(directory_length + name_length + 2);
  nexus_memory_bytes_copy(directory, result, directory_length);
  result[directory_length] = '/';
  nexus_memory_bytes_copy(name, result + directory_length + 1, name_length + 1);

  return result;
}

char *logos_tool_files_directory_of(const char *path) {
  const char *slash;
  char       *result;
  uint_large  length;

  slash = logos_tool_string_find_last_byte(path, '/');

  if (slash == NULL) {
    result = (char *)nexus_memory_allocate(2);
    nexus_memory_bytes_copy(".", result, 2);

    return result;
  }

  length = slash == path ? 1 : (uint_large)(slash - path);
  result = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(path, result, length);
  result[length] = '\0';

  return result;
}

char *logos_tool_files_stem_of(const char *path) {
  const char *slash;
  const char *dot;
  char       *result;
  uint_large  length;

  slash  = logos_tool_string_find_last_byte(path, '/');
  slash  = slash == NULL ? path : slash + 1;
  dot    = logos_tool_string_find_last_byte(slash, '.');
  length = dot == NULL || dot == slash ? nexus_strings_string_length_get(slash) : (uint_large)(dot - slash);
  result = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(slash, result, length);
  result[length] = '\0';

  return result;
}
