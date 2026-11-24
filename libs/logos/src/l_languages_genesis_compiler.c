#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/* ---------------------------------------------------------------------------- */
/* LIFECYCLE                                                                     */
/* ---------------------------------------------------------------------------- */

LGenesisCompiler *logos_languages_genesis_compiler_create(LogosRuntime *runtime) {
  LGenesisCompiler *compiler;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "runtime must not be NULL");

  compiler               = (LGenesisCompiler *)nexus_memory_allocate(NEXUS_SIZEOF(*compiler));
  compiler->runtime      = runtime;
  compiler->folders      = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  compiler->reporting    = pharos_diagnostics_create();
  compiler->registry     = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  compiler->chunk_open   = FALSE;
  compiler->chunk_name   = NULL;
  compiler->chunk_bytes  = nexus_data_list_create(NEXUS_SIZEOF(byte), 4096);
  compiler->import_depth = 0;
  compiler->check_only   = FALSE;

  return compiler;
}

static void l_genesis_imports_destroy(NexusDataList *imports) {
  uint_large i;
  LGImport  *import;

  for (i = 0; i < nexus_data_list_length_get(imports); i++) {
    import = (LGImport *)nexus_data_list_pointer_at(imports, i);
    nexus_memory_deallocate(import->name);

    if (import->alias != NULL) {
      nexus_memory_deallocate(import->alias);
    }

    nexus_memory_deallocate((void *)import->items);
    nexus_memory_deallocate(import);
  }

  nexus_data_list_destroy(imports);
}

static void l_genesis_registry_entry_destroy(LGRegistryEntry *entry) {
  if (entry->imports != NULL) {
    l_genesis_imports_destroy(entry->imports);
  }

  if (entry->engine != NULL) {
    logos_languages_genesis_engine_release(entry->engine);
  }

  if (entry->arena != NULL) {
    nexus_data_arena_release(entry->arena);
    nexus_memory_deallocate(entry->arena);
  }

  nexus_memory_deallocate(entry->name);

  if (entry->stem != NULL) {
    nexus_memory_deallocate(entry->stem);
  }

  if (entry->path != NULL) {
    nexus_memory_deallocate(entry->path);
  }

  nexus_memory_deallocate(entry);
}

void logos_languages_genesis_compiler_destroy(LGenesisCompiler *compiler) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(compiler != NULL, "compiler must not be NULL");

  for (i = 0; i < nexus_data_list_length_get(compiler->registry); i++) {
    l_genesis_registry_entry_destroy((LGRegistryEntry *)nexus_data_list_pointer_at(compiler->registry, i));
  }

  for (i = 0; i < nexus_data_list_length_get(compiler->folders); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(compiler->folders, i));
  }

  if (compiler->chunk_name != NULL) {
    nexus_memory_deallocate(compiler->chunk_name);
  }

  nexus_data_list_destroy(compiler->folders);
  pharos_diagnostics_destroy(compiler->reporting);
  nexus_data_list_destroy(compiler->registry);
  nexus_data_list_destroy(compiler->chunk_bytes);
  nexus_memory_deallocate(compiler);
}

static char *l_genesis_compiler_copy(const char *text) {
  uint_large length;
  char      *copy;

  length = nexus_strings_string_length_get(text);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length + 1);

  return copy;
}

void logos_languages_genesis_compiler_check_only_set(LGenesisCompiler *compiler, boolean check_only) {
  NEXUS_PROGRAM_PRECONDITION(compiler != NULL, "compiler must not be NULL");

  compiler->check_only = check_only;
}

void logos_languages_genesis_compiler_search_folder_add(LGenesisCompiler *compiler, const char *folder) {
  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && folder != NULL, "compiler and folder must not be NULL");

  (void)nexus_data_list_append_pointer(compiler->folders, l_genesis_compiler_copy(folder));
}

uint_large logos_languages_genesis_compiler_search_folder_count_get(const LGenesisCompiler *compiler) {
  NEXUS_PROGRAM_PRECONDITION(compiler != NULL, "compiler must not be NULL");

  return nexus_data_list_length_get(compiler->folders);
}

const char *logos_languages_genesis_compiler_search_folder_at(const LGenesisCompiler *compiler, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && index < nexus_data_list_length_get(compiler->folders), "folder index must be in bounds");

  return (const char *)nexus_data_list_pointer_at(compiler->folders, index);
}

/* ---------------------------------------------------------------------------- */
/* SOURCES                                                                       */
/* ---------------------------------------------------------------------------- */

uint32 logos_languages_genesis_compiler_source_add(LGenesisCompiler *compiler, const char *name, const byte *text, uint_large size) {
  return pharos_diagnostics_source_add(compiler->reporting, name, text, size);
}

const PharosSource *logos_languages_genesis_compiler_source_get(const LGenesisCompiler *compiler, uint32 source) {
  return pharos_diagnostics_source_get(compiler->reporting, source);
}

uint_large logos_languages_genesis_compiler_diagnostic_count_get(const LGenesisCompiler *compiler) {
  return pharos_diagnostics_count_get(compiler->reporting);
}

const PharosDiagnostic *logos_languages_genesis_compiler_diagnostic_at(const LGenesisCompiler *compiler, uint_large index) {
  return pharos_diagnostics_at(compiler->reporting, index);
}

boolean logos_languages_genesis_compiler_has_errors(const LGenesisCompiler *compiler) {
  return pharos_diagnostics_has_errors(compiler->reporting);
}

void logos_languages_genesis_compiler_diagnostics_clear(LGenesisCompiler *compiler) {
  pharos_diagnostics_clear(compiler->reporting);
}

char *logos_languages_genesis_compiler_diagnostics_render(const LGenesisCompiler *compiler, boolean color) {
  return pharos_diagnostics_render(compiler->reporting, color);
}

/* ---------------------------------------------------------------------------- */
/* REGISTRY AND IMPORTS                                                          */
/* ---------------------------------------------------------------------------- */

static LGRegistryEntry *l_genesis_registry_find(const LGenesisCompiler *compiler, const char *name) {
  LGRegistryEntry *entry;
  uint_large       i;

  for (i = nexus_data_list_length_get(compiler->registry); i > 0; i--) {
    entry = (LGRegistryEntry *)nexus_data_list_pointer_at(compiler->registry, i - 1);

    if (nexus_strings_string_equals(entry->name, name) || (entry->stem != NULL && nexus_strings_string_equals(entry->stem, name))) {
      return entry;
    }
  }

  return NULL;
}

/* Summary: the file name without folder and extension. */
static char *l_genesis_stem(const char *path) {
  uint_large length;
  uint_large start;
  uint_large end;
  uint_large i;
  char      *stem;

  length = nexus_strings_string_length_get(path);
  start  = 0;
  end    = length;

  for (i = 0; i < length; i++) {
    if (path[i] == '/') {
      start = i + 1;
    }
  }

  for (i = start; i < length; i++) {
    if (path[i] == '.') {
      end = i;
    }
  }

  stem = (char *)nexus_memory_allocate((end - start) + 1);

  if (end > start) {
    nexus_memory_bytes_copy(path + start, stem, end - start);
  }

  stem[end - start] = '\0';

  return stem;
}

/* Summary: reads a whole file into newly allocated memory; FALSE when it cannot be read. */
static boolean l_genesis_file_read(const char *path, byte **out_bytes, uint_large *out_size) {
  NexusPath        file_path;
  NexusFileHandle *handle;
  NexusError      *error;
  uint_large       size;
  uint_large       read;
  boolean          eof;
  byte            *bytes;

  file_path = nexus_filesystem_path_create(path);
  error     = nexus_filesystem_file_open(&file_path, NFM_READ_BINARY, &handle);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return FALSE;
  }

  error = nexus_filesystem_file_size_get(handle, &size);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)nexus_filesystem_file_close(handle);

    return FALSE;
  }

  bytes = (byte *)nexus_memory_allocate(size + 1);
  read  = 0;
  eof   = FALSE;

  while (read < size && !eof) {
    uint_large got;

    error = nexus_filesystem_file_read(handle, bytes + read, size - read, &got, &eof);

    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      nexus_memory_deallocate(bytes);
      (void)nexus_filesystem_file_close(handle);

      return FALSE;
    }

    read += got;
  }

  (void)nexus_filesystem_file_close(handle);
  *out_bytes = bytes;
  *out_size  = read;

  return TRUE;
}

/* Summary: reads the declared name of a .gen file from its first lines: "Language: Name" or "Library: Name". */
static char *l_genesis_declared_name(const byte *text, uint_large size) {
  uint_large i;
  uint_large start;
  uint_large end;
  char      *name;

  i = 0;

  while (i < size) {
    while (i < size && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) {
      i++;
    }

    if (i + 1 < size && text[i] == '/' && text[i + 1] == '/') {
      while (i < size && text[i] != '\n') {
        i++;
      }

      continue;
    }

    if (i + 1 < size && text[i] == '/' && text[i + 1] == '*') {
      i += 2;

      while (i + 1 < size && !(text[i] == '*' && text[i + 1] == '/')) {
        i++;
      }

      i += 2;
      continue;
    }

    break;
  }

  if (size - i > 9 && nexus_memory_bytes_equal(text + i, (const byte *)"Language:", 9)) {
    i += 9;
  } else if (size - i > 8 && nexus_memory_bytes_equal(text + i, (const byte *)"Library:", 8)) {
    i += 8;
  } else {
    return NULL;
  }

  while (i < size && (text[i] == ' ' || text[i] == '\t')) {
    i++;
  }

  start = i;

  while (i < size &&
         ((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z') || (text[i] >= '0' && text[i] <= '9') || text[i] == '_')) {
    i++;
  }

  end = i;

  if (end == start) {
    return NULL;
  }

  name = (char *)nexus_memory_allocate((end - start) + 1);
  nexus_memory_bytes_copy(text + start, name, end - start);
  name[end - start] = '\0';

  return name;
}

/* Summary: finds the file that provides name in the folders, by file name first and then by declared name. Returns a newly allocated path. */
static char *l_genesis_folder_search(const LGenesisCompiler *compiler, const char *name) {
  NexusPath     candidate;
  NexusPathList list;
  NexusError   *error;
  boolean       exists;
  uint_large    folder_index;
  uint_large    i;
  const char   *folder;
  byte         *bytes;
  uint_large    size;
  char         *declared;
  char         *found;

  /* A file named after the import wins in the first folder that has one. */
  for (folder_index = 0; folder_index < nexus_data_list_length_get(compiler->folders); folder_index++) {
    folder    = (const char *)nexus_data_list_pointer_at(compiler->folders, folder_index);
    candidate = nexus_filesystem_path_create(folder);
    nexus_filesystem_path_join(&candidate, name);
    candidate.buffer[candidate.path_length] = '\0';
    exists                                  = FALSE;

    {
      char       full[NEXUS_FILESYSTEM_MAX_PATH_LENGTH + 8];
      uint_large length;

      length = (uint_large)candidate.path_length;
      nexus_memory_bytes_copy(candidate.buffer, full, length);
      full[length]     = '.';
      full[length + 1] = 'g';
      full[length + 2] = 'e';
      full[length + 3] = 'n';
      full[length + 4] = '\0';
      candidate        = nexus_filesystem_path_create(full);
    }

    error = nexus_filesystem_path_exists(&candidate, &exists, TRUE);

    if (error == NEXUS_PROGRAM_ERROR_SUCCESS() && exists) {
      return l_genesis_compiler_copy(candidate.buffer);
    }
  }

  /* Otherwise any .gen file whose header declares the name. */
  for (folder_index = 0; folder_index < nexus_data_list_length_get(compiler->folders); folder_index++) {
    folder    = (const char *)nexus_data_list_pointer_at(compiler->folders, folder_index);
    candidate = nexus_filesystem_path_create(folder);
    error     = nexus_filesystem_directory_collect_matching_allocated(&candidate, FALSE, "\\.gen$", &list);

    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      continue;
    }

    found = NULL;

    for (i = 0; i < list.count && found == NULL; i++) {
      if (!l_genesis_file_read(list.paths[i].buffer, &bytes, &size)) {
        continue;
      }

      declared = l_genesis_declared_name(bytes, size);

      if (declared != NULL && nexus_strings_string_equals(declared, name)) {
        found = l_genesis_compiler_copy(list.paths[i].buffer);
      }

      if (declared != NULL) {
        nexus_memory_deallocate(declared);
      }

      nexus_memory_deallocate(bytes);
    }

    nexus_filesystem_path_list_destroy(&list);

    if (found != NULL) {
      return found;
    }
  }

  return NULL;
}

LGRegistryEntry *logos_languages_genesis_compiler_import_resolve(LGenesisCompiler *compiler, const char *name, PharosSpan span) {
  LGRegistryEntry  *entry;
  PharosDiagnostic *diagnostic;
  char             *path;
  byte             *bytes;
  uint_large        size;
  uint32            source;
  LGenesisEngine   *engine;
  uint_large        i;

  entry = l_genesis_registry_find(compiler, name);

  if (entry != NULL) {
    if (entry->in_progress) {
      diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0113", span, "cyclic import of `%s`", name);
      pharos_diagnostics_label_add(diagnostic, span, TRUE, "`%s` is still being compiled here", name);
      pharos_diagnostics_note_add(diagnostic, "a source cannot import itself, directly or through other imports");
      pharos_diagnostics_help_add(diagnostic, "move what both sources need into a library that neither of them imports from the other");

      return NULL;
    }

    return entry;
  }

  if (compiler->import_depth >= LG_IMPORT_DEPTH_MAX) {
    diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0114", span, "imports are nested more than %u deep",
                                           (unsigned)LG_IMPORT_DEPTH_MAX);
    pharos_diagnostics_label_add(diagnostic, span, TRUE, "while importing `%s`", name);

    return NULL;
  }

  path = l_genesis_folder_search(compiler, name);

  if (path == NULL) {
    diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0100", span, "cannot find `%s` to import", name);
    pharos_diagnostics_label_add(diagnostic, span, TRUE, "no source named `%s`", name);

    if (nexus_data_list_length_get(compiler->folders) == 0) {
      pharos_diagnostics_note_add(diagnostic, "the compiler has no search folders; imports resolve only against sources compiled earlier");
      pharos_diagnostics_help_add(diagnostic, "add a folder with logos_languages_genesis_compiler_search_folder_add, or compile `%s` first", name);
    } else {
      for (i = 0; i < nexus_data_list_length_get(compiler->folders); i++) {
        pharos_diagnostics_note_add(diagnostic, "searched `%s` for `%s.gen` and for a header declaring `%s`",
                                    (const char *)nexus_data_list_pointer_at(compiler->folders, i), name, name);
      }
    }

    return NULL;
  }

  if (!l_genesis_file_read(path, &bytes, &size)) {
    diagnostic = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0102", span, "cannot read `%s`", path);
    pharos_diagnostics_label_add(diagnostic, span, TRUE, "imported here");
    nexus_memory_deallocate(path);

    return NULL;
  }

  source = logos_languages_genesis_compiler_source_add(compiler, path, bytes, size);
  nexus_memory_deallocate(bytes);

  compiler->import_depth++;
  engine = logos_languages_genesis_compiler_unit_compile(compiler, source, path);
  compiler->import_depth--;
  nexus_memory_deallocate(path);

  if (engine == NULL) {
    return NULL;
  }

  /* The registry now holds the unit; the caller's hold on the engine is not needed. */
  logos_languages_genesis_engine_release(engine);

  return l_genesis_registry_find(compiler, name);
}

char *logos_languages_genesis_compiler_import_locate(const LGenesisCompiler *compiler, const char *name) {
  return l_genesis_folder_search(compiler, name);
}

boolean logos_languages_genesis_compiler_file_parse(LGenesisCompiler *compiler, NexusDataArena *arena, const char *path, LGUnit **out_unit) {
  byte      *bytes;
  uint_large size;
  uint32     source;

  *out_unit = NULL;

  if (!l_genesis_file_read(path, &bytes, &size)) {
    return FALSE;
  }

  source = logos_languages_genesis_compiler_source_add(compiler, path, bytes, size);
  nexus_memory_deallocate(bytes);

  return logos_languages_genesis_syntax_parse(compiler, arena, source, out_unit);
}

/* ---------------------------------------------------------------------------- */
/* THE PIPELINE                                                                  */
/* ---------------------------------------------------------------------------- */

static void l_genesis_unit_register(LGenesisCompiler *compiler, LGRegistryEntry *entry) {
  /* Sources compiled again under one name all stay: other units hold pointers to the entries they imported. Lookups find the newest first. */
  (void)nexus_data_list_append_pointer(compiler->registry, entry);
}

LGenesisEngine *logos_languages_genesis_compiler_unit_compile(LGenesisCompiler *compiler, uint32 source, const char *path) {
  NexusDataArena     *arena;
  LGUnit             *unit;
  LGRegistryEntry    *entry;
  LGenesisEngine     *engine;
  LGModel            *model;
  NexusDataList      *imports;
  const PharosSource *text;
  boolean             ok;

  text  = logos_languages_genesis_compiler_source_get(compiler, source);
  arena = (NexusDataArena *)nexus_memory_allocate(NEXUS_SIZEOF(*arena));
  nexus_data_arena_initialize(arena);

  ok = logos_languages_genesis_syntax_parse(compiler, arena, source, &unit);

  if (unit == NULL) {
    nexus_data_arena_release(arena);
    nexus_memory_deallocate(arena);

    return NULL;
  }

  entry              = (LGRegistryEntry *)nexus_memory_allocate(NEXUS_SIZEOF(*entry));
  entry->name        = l_genesis_compiler_copy(unit->name);
  entry->stem        = l_genesis_stem(path != NULL ? path : text->name);
  entry->path        = path != NULL ? l_genesis_compiler_copy(path) : NULL;
  entry->kind        = unit->kind;
  entry->engine      = NULL;
  entry->unit        = unit;
  entry->arena       = arena;
  entry->imports     = NULL;
  entry->source      = source;
  entry->in_progress = TRUE;
  l_genesis_unit_register(compiler, entry);

  if (!ok) {
    uint_large index;

    for (index = 0; index < nexus_data_list_length_get(compiler->registry); index++) {
      if (nexus_data_list_pointer_at(compiler->registry, index) == entry) {
        nexus_data_list_remove_pointer(compiler->registry, index, NULL);

        break;
      }
    }

    l_genesis_registry_entry_destroy(entry);

    return NULL;
  }

  imports = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  ok      = logos_languages_genesis_resolver_imports_resolve(compiler, unit, imports);
  engine  = NULL;

  if (ok && unit->kind == LGUK_LIBRARY) {
    ok = logos_languages_genesis_analysis_library_check(compiler, unit, imports);

    if (ok) {
      engine = logos_languages_genesis_engine_create(unit->name, unit->version, text->name, LGUK_LIBRARY);
    }
  } else if (ok) {
    model = logos_languages_genesis_template_expand(compiler, arena, unit, imports);

    if (model != NULL && logos_languages_genesis_analysis_language_check(compiler, model)) {
      engine = logos_languages_genesis_engine_create(unit->name, unit->version, text->name, LGUK_LANGUAGE);

      /* An editor wants the diagnostics of the analysis; the tables (most of the time) it can leave to the build. */
      if (!(compiler->check_only && compiler->import_depth == 0) && !logos_languages_genesis_lowering_language_compile(compiler, model, engine)) {
        logos_languages_genesis_engine_release(engine);
        engine = NULL;
      }
    }

    if (model != NULL) {
      logos_languages_genesis_model_destroy(model);
    }
  }

  /* A library keeps its imports: its templates resolve names in its own scope when a language uses them. */
  if (unit->kind == LGUK_LIBRARY && engine != NULL) {
    entry->imports = imports;
  } else {
    l_genesis_imports_destroy(imports);
  }

  entry->in_progress = FALSE;

  if (engine == NULL) {
    /* A failed source is not registered: importing it again compiles and diagnoses it again. */
    {
      uint_large index;

      for (index = 0; index < nexus_data_list_length_get(compiler->registry); index++) {
        if (nexus_data_list_pointer_at(compiler->registry, index) == entry) {
          nexus_data_list_remove_pointer(compiler->registry, index, NULL);
          l_genesis_registry_entry_destroy(entry);

          break;
        }
      }
    }

    return NULL;
  }

  if (unit->kind == LGUK_LANGUAGE) {
    /* The registry keeps one hold; the arena is no longer needed once the engine exists. */
    entry->engine = engine;
    logos_languages_genesis_engine_retain(engine);
    entry->unit = NULL;
    nexus_data_arena_release(arena);
    nexus_memory_deallocate(arena);
    entry->arena = NULL;
  } else {
    entry->engine = engine;
    logos_languages_genesis_engine_retain(engine);
  }

  return engine;
}

/* ---------------------------------------------------------------------------- */
/* PUBLIC COMPILATION ENTRY POINTS                                               */
/* ---------------------------------------------------------------------------- */

LGenesisEngine *logos_languages_genesis_compiler_compile_string(LGenesisCompiler *compiler, const char *source_name, const byte *text,
                                                                uint_large size) {
  uint32 source;

  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && source_name != NULL && (text != NULL || size == 0), "compile arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(!compiler->chunk_open, "a chunked compilation is open: finish it first");

  source = logos_languages_genesis_compiler_source_add(compiler, source_name, text, size);

  return logos_languages_genesis_compiler_unit_compile(compiler, source, NULL);
}

LGenesisEngine *logos_languages_genesis_compiler_compile_text(LGenesisCompiler *compiler, const char *source_name, const char *text) {
  NEXUS_PROGRAM_PRECONDITION(text != NULL, "text must not be NULL");

  return logos_languages_genesis_compiler_compile_string(compiler, source_name, (const byte *)text, nexus_strings_string_length_get(text));
}

void logos_languages_genesis_compiler_chunk_begin(LGenesisCompiler *compiler, const char *source_name) {
  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && source_name != NULL, "compiler and source name must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(!compiler->chunk_open, "a chunked compilation is already open");

  compiler->chunk_open = TRUE;
  compiler->chunk_name = l_genesis_compiler_copy(source_name);
  nexus_data_list_clear(compiler->chunk_bytes);
}

void logos_languages_genesis_compiler_chunk_add(LGenesisCompiler *compiler, const byte *chunk, uint_large chunk_size) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && compiler->chunk_open && (chunk != NULL || chunk_size == 0), "a chunked compilation must be open");

  for (i = 0; i < chunk_size; i++) {
    (void)nexus_data_list_append(compiler->chunk_bytes, &chunk[i]);
  }
}

LGenesisEngine *logos_languages_genesis_compiler_chunk_finish(LGenesisCompiler *compiler) {
  LGenesisEngine *engine;
  uint32          source;
  char           *name;

  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && compiler->chunk_open, "a chunked compilation must be open");

  name                 = compiler->chunk_name;
  compiler->chunk_name = NULL;
  compiler->chunk_open = FALSE;
  source = logos_languages_genesis_compiler_source_add(compiler, name, (const byte *)nexus_data_list_data_get_const(compiler->chunk_bytes),
                                                       nexus_data_list_length_get(compiler->chunk_bytes));
  nexus_memory_deallocate(name);
  nexus_data_list_clear(compiler->chunk_bytes);
  engine = logos_languages_genesis_compiler_unit_compile(compiler, source, NULL);

  return engine;
}

LGenesisEngine *logos_languages_genesis_compiler_compile_file(LGenesisCompiler *compiler, const char *path) {
  byte             *bytes;
  uint_large        size;
  uint32            source;
  PharosSpan        span;
  LGenesisEngine   *engine;
  PharosDiagnostic *diagnostic;

  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && path != NULL, "compiler and path must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(!compiler->chunk_open, "a chunked compilation is open: finish it first");

  if (!l_genesis_file_read(path, &bytes, &size)) {
    source      = logos_languages_genesis_compiler_source_add(compiler, path, (const byte *)"", 0);
    span.source = source;
    span.offset = 0;
    span.width  = 0;
    diagnostic  = pharos_diagnostics_report((compiler)->reporting, PDS_ERROR, "G0102", span, "cannot read `%s`", path);
    pharos_diagnostics_label_add(diagnostic, span, TRUE, "the file does not exist or cannot be read");

    return NULL;
  }

  source = logos_languages_genesis_compiler_source_add(compiler, path, bytes, size);
  nexus_memory_deallocate(bytes);
  engine = logos_languages_genesis_compiler_unit_compile(compiler, source, path);

  return engine;
}
