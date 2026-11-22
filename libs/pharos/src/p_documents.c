#include <nexus.h>

#include "./p_internal.h"

/* ---------------------------------------------------------------------------- */
/* FILES                                                                         */
/* ---------------------------------------------------------------------------- */

/* Summary: reads a whole file into Nexus memory with a NUL terminator; FALSE when it cannot be read. Reports the file's stamp. */
static boolean p_documents_file_read(const char *path, char **out_text, uint_large *out_size, timestamp *out_time, uint64 *out_stamp_size) {
  NexusPath        file_path;
  NexusFileHandle *file;
  NexusPathInfo    info;
  uint_large       size;
  uint_large       read_size;
  boolean          reached_eof;
  char            *bytes;

  file_path = nexus_filesystem_path_create(path);
  nexus_memory_bytes_clear(&info, NEXUS_SIZEOF(info));

  if (nexus_filesystem_path_info_get(&file_path, &info) != NEXUS_PROGRAM_ERROR_SUCCESS() || info.type != NPT_FILE || info.size > 0x40000000U) {
    return FALSE;
  }

  if (nexus_filesystem_file_open(&file_path, NFM_READ_BINARY, &file) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return FALSE;
  }

  size        = (uint_large)info.size;
  bytes       = (char *)nexus_memory_allocate(size + 1);
  read_size   = 0;
  reached_eof = FALSE;

  if (size > 0 && nexus_filesystem_file_read(file, (byte *)bytes, size, &read_size, &reached_eof) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    read_size = size + 1;
  }

  (void)nexus_filesystem_file_close(file);

  if (read_size != size) {
    nexus_memory_deallocate(bytes);

    return FALSE;
  }

  bytes[size]     = '\0';
  *out_text       = bytes;
  *out_size       = size;
  *out_time       = info.modification_time.time;
  *out_stamp_size = info.size;

  return TRUE;
}

/* Summary: whether the file on disk differs from what was read. */
static boolean p_documents_stale(const PharosDocument *document) {
  NexusPath     path;
  NexusPathInfo info;

  path = nexus_filesystem_path_create(document->path);
  nexus_memory_bytes_clear(&info, NEXUS_SIZEOF(info));

  if (nexus_filesystem_path_info_get(&path, &info) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return FALSE;
  }

  return info.modification_time.time != document->stamp_time || info.size != document->stamp_size;
}

/* ---------------------------------------------------------------------------- */
/* POSITIONS                                                                     */
/* ---------------------------------------------------------------------------- */

static void p_documents_lines_build(PharosDocument *document) {
  uint_large i;
  uint_large start;

  nexus_data_list_clear(document->line_starts);
  start = 0;
  (void)nexus_data_list_append(document->line_starts, &start);

  for (i = 0; i < document->size; i++) {
    if (document->text[i] == '\n') {
      start = i + 1;
      (void)nexus_data_list_append(document->line_starts, &start);
    }
  }

  document->lines_built = TRUE;
}

uint32 pharos_documents_sequence_length_get(byte lead) {
  if (lead >= 0xF0) {
    return 4;
  }

  if (lead >= 0xE0) {
    return 3;
  }

  return lead >= 0xC0 ? 2 : 1;
}

/* Summary: a sequence's length in UTF-16 units. */
static uint32 p_documents_units(byte lead) {
  return lead >= 0xF0 ? 2 : 1;
}

void pharos_documents_position_get(PharosDocument *document, uint_large offset, uint32 *out_line, uint32 *out_character) {
  uint_large low;
  uint_large high;
  uint_large line_start;
  uint_large i;
  uint32     units;

  if (!document->lines_built) {
    p_documents_lines_build(document);
  }

  low  = 0;
  high = nexus_data_list_length_get(document->line_starts);

  while (high - low > 1) {
    uint_large middle;

    middle = (low + high) / 2;

    if (*(const uint_large *)nexus_data_list_at_const(document->line_starts, middle) <= offset) {
      low = middle;
    } else {
      high = middle;
    }
  }

  line_start = *(const uint_large *)nexus_data_list_at_const(document->line_starts, low);
  units      = 0;

  for (i = line_start; i < offset && i < document->size;) {
    units += p_documents_units((byte)document->text[i]);
    i += pharos_documents_sequence_length_get((byte)document->text[i]);
  }

  *out_line      = (uint32)low;
  *out_character = units;
}

uint_large pharos_documents_offset_get(PharosDocument *document, uint32 line, uint32 character) {
  uint_large offset;
  uint32     units;

  if (!document->lines_built) {
    p_documents_lines_build(document);
  }

  if (line >= nexus_data_list_length_get(document->line_starts)) {
    return document->size;
  }

  offset = *(const uint_large *)nexus_data_list_at_const(document->line_starts, line);
  units  = 0;

  while (offset < document->size && document->text[offset] != '\n' && units < character) {
    units += p_documents_units((byte)document->text[offset]);
    offset += pharos_documents_sequence_length_get((byte)document->text[offset]);
  }

  return offset;
}

void pharos_documents_range_write(NexusStringBuilder *out, PharosDocument *document, uint_large start, uint_large end) {
  uint32 start_line;
  uint32 start_character;
  uint32 end_line;
  uint32 end_character;

  pharos_documents_position_get(document, start, &start_line, &start_character);
  pharos_documents_position_get(document, end, &end_line, &end_character);
  (void)nexus_strings_string_builder_append_formatted(out, "{\"start\":{\"line\":%u,\"character\":%u},\"end\":{\"line\":%u,\"character\":%u}}",
                                                      (unsigned)start_line, (unsigned)start_character, (unsigned)end_line, (unsigned)end_character);
}

/* ---------------------------------------------------------------------------- */
/* THE DOCUMENTS                                                                 */
/* ---------------------------------------------------------------------------- */

static void p_documents_free_maybe(void *memory) {
  if (memory != NULL) {
    nexus_memory_deallocate(memory);
  }
}

/* Summary: frees a string the document owns but exposes as const. */
static void p_documents_free_text(const char *text) {
  void *memory;

  nexus_memory_bytes_copy((const void *)&text, (void *)&memory, NEXUS_SIZEOF(memory));
  p_documents_free_maybe(memory);
}

void pharos_documents_names_clear(PharosDocument *document) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(document->names); i++) {
    PharosName *name;

    name = (PharosName *)nexus_data_list_at(document->names, i);
    p_documents_free_text(name->name);
    if (name->owner != NULL) {
      p_documents_free_text(name->owner);
    }
    if (name->scope != NULL) {
      p_documents_free_text(name->scope);
    }
  }

  nexus_data_list_clear(document->names);

  for (i = 0; i < nexus_data_list_length_get(document->problems); i++) {
    PharosProblem *problem;

    problem = (PharosProblem *)nexus_data_list_at(document->problems, i);
    p_documents_free_maybe(problem->code);
    p_documents_free_maybe(problem->message);
  }

  nexus_data_list_clear(document->problems);

  if (document->state != NULL && document->state_destroy != NULL) {
    document->state_destroy(document->state);
  }

  document->state      = NULL;
  document->from_cache = FALSE;
}

void pharos_documents_destroy(PharosDocument *document) {
  pharos_documents_names_clear(document);
  nexus_data_list_destroy(document->names);
  nexus_data_list_destroy(document->problems);
  nexus_data_list_destroy(document->line_starts);
  p_documents_free_maybe(document->uri);
  p_documents_free_maybe(document->path);
  p_documents_free_maybe(document->text);
  nexus_memory_deallocate(document);
}

PharosDocument *pharos_documents_document_find(const PharosServer *server, const char *path) {
  uint_large  i;
  uint_large  key[1];
  uint_large *slot;

  key[0] = (uint_large)nexus_hash_xxh3_64(path, nexus_strings_string_length_get(path));
  slot   = nexus_data_word_map_get(server->index, key);

  if (slot != NULL) {
    PharosDocument *candidate;

    candidate = (PharosDocument *)nexus_data_list_pointer_at(server->documents, slot[0]);

    if (nexus_strings_string_equals(candidate->path, path)) {
      return candidate;
    }
  }

  /* Another path with the same hash is not in the index; the documents are searched. */
  for (i = 0; i < nexus_data_list_length_get(server->documents); i++) {
    PharosDocument *document;

    document = (PharosDocument *)nexus_data_list_pointer_at(server->documents, i);

    if (nexus_strings_string_equals(document->path, path)) {
      return document;
    }
  }

  return NULL;
}

/* Summary: a new document for a served path; NULL when no kind serves it. The text may be absent (a file not read yet). */
static PharosDocument *p_documents_create(PharosServer *server, const char *path, const char *uri) {
  PharosDocument *document;
  uint32          kind;

  if (!server->provider.document_kind(server->provider.user, path, &kind)) {
    return NULL;
  }

  document = (PharosDocument *)nexus_memory_allocate(NEXUS_SIZEOF(*document));
  nexus_memory_bytes_clear(document, NEXUS_SIZEOF(*document));
  document->path        = pharos_paths_text_copy(path);
  document->uri         = uri != NULL ? pharos_paths_text_copy(uri) : pharos_paths_uri_of_path(path);
  document->kind        = kind;
  document->line_starts = nexus_data_list_create(NEXUS_SIZEOF(uint_large), 64);
  document->names       = nexus_data_list_create(NEXUS_SIZEOF(PharosName), 64);
  document->problems    = nexus_data_list_create(NEXUS_SIZEOF(PharosProblem), 8);
  {
    uint_large key[1];

    key[0] = (uint_large)nexus_hash_xxh3_64(document->path, nexus_strings_string_length_get(document->path));

    if (nexus_data_word_map_get(server->index, key) == NULL) {
      *nexus_data_word_map_insert(server->index, key) = nexus_data_list_length_get(server->documents);
    }
  }

  (void)nexus_data_list_append_pointer(server->documents, document);

  return document;
}

PharosDocument *pharos_documents_file_add(PharosServer *server, const char *path) {
  if (pharos_documents_document_find(server, path) != NULL) {
    return NULL;
  }

  return p_documents_create(server, path, NULL);
}

boolean pharos_documents_text_ensure(PharosServer *server, PharosDocument *document) {
  char      *text;
  uint_large size;
  timestamp  time;
  uint64     stamp_size;

  (void)server;

  if (document->open) {
    return TRUE;
  }

  /* A file already read is read again when it changed. */
  if (document->text != NULL && !p_documents_stale(document)) {
    return TRUE;
  }

  if (!p_documents_file_read(document->path, &text, &size, &time, &stamp_size)) {
    return document->text != NULL;
  }

  /* Names that came from the cache stay when the file read is the one they were made from. */
  if (document->from_cache && document->text == NULL && time == document->stamp_time && stamp_size == document->stamp_size) {
    document->text        = text;
    document->size        = size;
    document->lines_built = FALSE;

    return TRUE;
  }

  document->from_cache = FALSE;

  p_documents_free_maybe(document->text);
  document->text        = text;
  document->size        = size;
  document->stamp_time  = time;
  document->stamp_size  = stamp_size;
  document->lines_built = FALSE;
  document->analyzed    = FALSE;

  return TRUE;
}

static void p_documents_analyze(PharosServer *server, PharosDocument *document) {
  pharos_documents_names_clear(document);
  server->provider.analyze(server->provider.user, server, document);
  document->analyzed = TRUE;
}

boolean pharos_documents_ensure(PharosServer *server, PharosDocument *document) {
  if (!pharos_documents_text_ensure(server, document)) {
    return FALSE;
  }

  if (!document->analyzed) {
    p_documents_analyze(server, document);
  }

  return TRUE;
}

PharosDocument *pharos_documents_load(PharosServer *server, const char *path) {
  PharosDocument *document;

  document = pharos_documents_document_find(server, path);

  if (document == NULL) {
    document = p_documents_create(server, path, NULL);
  }

  if (document == NULL || !pharos_documents_text_ensure(server, document)) {
    return NULL;
  }

  return document;
}

PharosDocument *pharos_documents_open_set(PharosServer *server, const char *uri, const char *text, int64 version) {
  char           *path;
  PharosDocument *document;
  uint_large      size;

  path     = pharos_paths_path_of_uri(uri);
  document = pharos_documents_document_find(server, path);

  if (document == NULL) {
    document = p_documents_create(server, path, uri);
  }

  nexus_memory_deallocate(path);

  if (document == NULL) {
    return NULL;
  }

  size = nexus_strings_string_length_get(text);
  p_documents_free_maybe(document->text);
  p_documents_free_maybe(document->uri);
  document->uri         = pharos_paths_text_copy(uri);
  document->text        = pharos_paths_bytes_copy(text, size);
  document->size        = size;
  document->open        = TRUE;
  document->version     = version;
  document->lines_built = FALSE;
  p_documents_analyze(server, document);

  return document;
}

void pharos_documents_close(PharosServer *server, const char *uri) {
  char           *path;
  PharosDocument *document;

  path     = pharos_paths_path_of_uri(uri);
  document = pharos_documents_document_find(server, path);
  nexus_memory_deallocate(path);

  if (document == NULL) {
    return;
  }

  /* A closed document is what is on disk again. */
  document->open     = FALSE;
  document->analyzed = FALSE;
  p_documents_free_maybe(document->text);
  document->text = NULL;
  document->size = 0;
}

void pharos_document_state_set(PharosDocument *document, void *state, PharosStateDestroyCallback *destroy) {
  document->state         = state;
  document->state_destroy = destroy;
}

void *pharos_document_state_get(const PharosDocument *document) {
  return document->state;
}

const char *pharos_document_path_get(const PharosDocument *document) {
  return document->path;
}

const char *pharos_document_text_get(const PharosDocument *document) {
  return document->text != NULL ? document->text : "";
}

uint_large pharos_document_size_get(const PharosDocument *document) {
  return document->size;
}

uint32 pharos_document_kind_get(const PharosDocument *document) {
  return document->kind;
}

void pharos_document_name_add(PharosDocument *document, int32 kind, const char *text, uint_large length, uint_large start, const char *owner,
                              const char *scope, boolean declaration, int32 symbol_kind) {
  PharosName name;

  name.start       = start;
  name.end         = start + length;
  name.kind        = kind;
  name.name        = pharos_paths_bytes_copy(text, length);
  name.owner       = owner != NULL ? pharos_paths_text_copy(owner) : NULL;
  name.scope       = scope != NULL ? pharos_paths_text_copy(scope) : NULL;
  name.declaration = declaration;
  name.symbol_kind = symbol_kind;
  (void)nexus_data_list_append(document->names, &name);
}

void pharos_document_problem_add(PharosDocument *document, uint_large start, uint_large end, int32 severity, const char *code, const char *message) {
  PharosProblem problem;

  problem.start    = start;
  problem.end      = end;
  problem.severity = severity;
  problem.code     = code != NULL ? pharos_paths_text_copy(code) : NULL;
  problem.message  = pharos_paths_text_copy(message);
  (void)nexus_data_list_append(document->problems, &problem);
}
