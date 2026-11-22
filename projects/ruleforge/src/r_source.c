#include <nexus.h>

#include "./r_internal.h"

uint32 ruleforge_source_add(RuleforgeCompilation *compilation, const char *name, const char *module, const byte *text, uint_large size,
                            boolean embedded) {
  RuleforgeFile *file;
  uint_large     i;
  uint32         lines;
  uint32         line;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && name != NULL && text != NULL, "ruleforge_source_add: NULL argument");
  NEXUS_PROGRAM_PRECONDITION(nexus_data_list_length_get(compilation->files) < 0xFFFFU, "ruleforge_source_add: too many files");

  file           = (RuleforgeFile *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*file));
  file->name     = nexus_data_arena_text_copy(&compilation->arena, name, nexus_strings_string_length_get(name));
  file->module   = module == NULL ? NULL : nexus_data_arena_text_copy(&compilation->arena, module, nexus_strings_string_length_get(module));
  file->text     = (byte *)nexus_data_arena_text_copy(&compilation->arena, (const char *)text, size);
  file->size     = size;
  file->embedded = embedded;

  /* A byte-order mark is not part of the source. */
  if (size >= 3 && file->text[0] == 0xEF && file->text[1] == 0xBB && file->text[2] == 0xBF) {
    for (i = 3; i < size; i++) {
      file->text[i - 3] = file->text[i];
    }

    file->size -= 3;
    file->text[file->size] = '\0';
  }

  lines = 1;

  for (i = 0; i < file->size; i++) {
    if (file->text[i] == '\n') {
      lines++;
    }
  }

  file->line_starts    = (uint32 *)nexus_data_arena_allocate(&compilation->arena, (uint_large)lines * NEXUS_SIZEOF(uint32));
  file->line_count     = lines;
  file->line_starts[0] = 0;
  line                 = 1;

  for (i = 0; i < file->size; i++) {
    if (file->text[i] == '\n') {
      file->line_starts[line] = (uint32)(i + 1);
      line++;
    }
  }

  (void)nexus_data_list_append_pointer(compilation->files, file);

  return (uint32)(nexus_data_list_length_get(compilation->files) - 1);
}

RuleforgeFile *ruleforge_source_get(const RuleforgeCompilation *compilation, uint32 file) {
  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_source_get: NULL compilation");
  NEXUS_PROGRAM_PRECONDITION(file < nexus_data_list_length_get(compilation->files), "ruleforge_source_get: file out of range");

  return (RuleforgeFile *)nexus_data_list_pointer_at(compilation->files, file);
}

/* Summary: reads a whole file into freshly allocated memory (NUL-terminated); FALSE when it cannot be read. */
boolean ruleforge_source_file_read(const char *path, byte **out_bytes, uint_large *out_size) {
  NexusPath        file_path;
  NexusFileHandle *file;
  uint_large       size;
  uint_large       read_size;
  boolean          reached_eof;
  byte            *bytes;

  if (nexus_strings_string_length_get(path) > NEXUS_FILESYSTEM_MAX_PATH_LENGTH) {
    return FALSE;
  }

  file_path = nexus_filesystem_path_create(path);

  if (nexus_filesystem_file_open(&file_path, NFM_READ_BINARY, &file) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return FALSE;
  }

  if (nexus_filesystem_file_size_get(file, &size) != NEXUS_PROGRAM_ERROR_SUCCESS() || size > 0x10000000U) {
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
