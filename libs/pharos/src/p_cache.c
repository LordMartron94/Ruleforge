#include <nexus.h>

#include "./p_internal.h"

/*
The cache keeps, per file of the workspace, what analysing it found (names and problems) together with the size and time of the file it
was made from. A later session that finds the same file unchanged takes the names from here and does not read or analyse the file; the
text is read when a position in it is needed. The file is read whole when the workspace is known and written when the server ends.

Layout (integers little-endian): "PHAROSC1", a string with the provider name, a string with its fingerprint, the number of records,
then the records. A record is its length in bytes (u32), the path (string), the size (u64), the modification time (u64), the kind (u32), the
names and the problems. A string is a u32 length and the bytes.
*/

#define P_CACHE_MAGIC "PHAROSC1"

/* ---------------------------------------------------------------------------- */
/* WRITING AND READING VALUES                                                    */
/* ---------------------------------------------------------------------------- */

static void p_cache_u32(NexusDataList *out, uint32 value) {
  uint32 i;

  for (i = 0; i < 4; i++) {
    byte part;

    part = (byte)((value >> (i * 8)) & 0xFF);
    (void)nexus_data_list_append(out, &part);
  }
}

static void p_cache_u64(NexusDataList *out, uint64 value) {
  uint32 i;

  for (i = 0; i < 8; i++) {
    byte part;

    part = (byte)((value >> (i * 8)) & 0xFF);
    (void)nexus_data_list_append(out, &part);
  }
}

static void p_cache_bytes(NexusDataList *out, const byte *bytes, uint_large size) {
  uint_large i;

  for (i = 0; i < size; i++) {
    (void)nexus_data_list_append(out, &bytes[i]);
  }
}

static void p_cache_string(NexusDataList *out, const char *text) {
  uint_large length;

  length = text == NULL ? 0 : nexus_strings_string_length_get(text);
  p_cache_u32(out, (uint32)length);
  p_cache_bytes(out, (const byte *)text, length);
}

/* A name's owner and scope may be absent, which is not the same as empty. */
static void p_cache_optional(NexusDataList *out, const char *text) {
  byte present;

  present = text != NULL ? 1 : 0;
  (void)nexus_data_list_append(out, &present);

  if (text != NULL) {
    p_cache_string(out, text);
  }
}

typedef struct PCacheReader {
  const byte *bytes;
  uint_large  size;
  uint_large  position;
  boolean     ok;
} PCacheReader;

static uint32 p_cache_read_u32(PCacheReader *reader) {
  uint32 value;
  uint32 i;

  value = 0;

  if (reader->position + 4 > reader->size) {
    reader->ok = FALSE;

    return 0;
  }

  for (i = 0; i < 4; i++) {
    value |= (uint32)reader->bytes[reader->position + i] << (i * 8);
  }

  reader->position += 4;

  return value;
}

static byte p_cache_read_byte(PCacheReader *reader) {
  byte value;

  if (reader->position >= reader->size) {
    reader->ok = FALSE;

    return 0;
  }

  value = reader->bytes[reader->position];
  reader->position++;

  return value;
}

static uint64 p_cache_read_u64(PCacheReader *reader) {
  uint64 value;
  uint32 i;

  value = 0;

  if (reader->position + 8 > reader->size) {
    reader->ok = FALSE;

    return 0;
  }

  for (i = 0; i < 8; i++) {
    value |= (uint64)reader->bytes[reader->position + i] << (i * 8);
  }

  reader->position += 8;

  return value;
}

/* Summary: a string as a view into the reader's bytes (not NUL-terminated); *length is its size. */
static const char *p_cache_read_view(PCacheReader *reader, uint_large *length) {
  uint32      size;
  const char *view;

  size = p_cache_read_u32(reader);

  if (!reader->ok || reader->position + size > reader->size) {
    reader->ok = FALSE;
    *length    = 0;

    return NULL;
  }

  view = (const char *)reader->bytes + reader->position;
  reader->position += size;
  *length = size;

  return view;
}

static char *p_cache_read_string(PCacheReader *reader) {
  uint_large  length;
  const char *view;

  view = p_cache_read_view(reader, &length);

  return view == NULL ? NULL : pharos_paths_bytes_copy(view, length);
}

static char *p_cache_read_optional(PCacheReader *reader) {
  if (p_cache_read_byte(reader) == 0) {
    return NULL;
  }

  return p_cache_read_string(reader);
}

/* ---------------------------------------------------------------------------- */
/* THE FILE                                                                      */
/* ---------------------------------------------------------------------------- */

static uint64 p_cache_path_hash(const char *path) {
  return nexus_hash_xxh3_64(path, nexus_strings_string_length_get(path));
}

/* Summary: the folder the cache files live in: the setting, else the user's cache folder; Nexus-allocated, or NULL when there is none. */
static char *p_cache_directory(const PharosServer *server, const char *root) {
  char    buffer[1024];
  boolean found;

  if (server->settings.cache_directory != NULL) {
    const char *directory;

    directory = server->settings.cache_directory;

    if (directory[0] == '/' || root == NULL) {
      return pharos_paths_text_copy(directory);
    }

    (void)stbsp_snprintf(buffer, (int)sizeof(buffer), "%s/%s", root, directory);

    return pharos_paths_text_copy(buffer);
  }

  if (nexus_environment_variable_get("XDG_CACHE_HOME", buffer, sizeof(buffer), &found) == NEXUS_PROGRAM_ERROR_SUCCESS() && found &&
      buffer[0] == '/') {
    char full[1100];

    (void)stbsp_snprintf(full, (int)sizeof(full), "%s/pharos", buffer);

    return pharos_paths_text_copy(full);
  }

  if (nexus_environment_variable_get("HOME", buffer, sizeof(buffer), &found) == NEXUS_PROGRAM_ERROR_SUCCESS() && found && buffer[0] == '/') {
    char full[1100];

    (void)stbsp_snprintf(full, (int)sizeof(full), "%s/.cache/pharos", buffer);

    return pharos_paths_text_copy(full);
  }

  return NULL;
}

static const char *p_cache_fingerprint(const PharosServer *server) {
  return server->provider.fingerprint != NULL ? server->provider.fingerprint(server->provider.user) : "";
}

void pharos_cache_open(PharosServer *server, const char *root) {
  PharosCache     *cache;
  char            *directory;
  char             name[160];
  NexusPath        path;
  NexusFileHandle *file;
  NexusPathInfo    info;
  uint_large       read_size;
  boolean          reached_eof;
  PCacheReader     reader;
  uint64           identity;
  uint64           count;
  uint64           i;

  cache = &server->cache;

  if (cache->loaded || !server->settings.values.cache) {
    return;
  }

  cache->loaded = TRUE;
  directory     = p_cache_directory(server, root);

  if (directory == NULL) {
    return;
  }

  /* One file per provider, fingerprint and root. */
  {
    NexusStringBuilder key;

    key = nexus_strings_string_builder_create_growable(128);
    (void)nexus_strings_string_builder_append_formatted(&key, "%s|%s|%s", server->provider.name != NULL ? server->provider.name : "",
                                                        p_cache_fingerprint(server), root != NULL ? root : "");
    identity = nexus_hash_xxh3_64(nexus_strings_string_builder_string_get(&key), nexus_strings_string_builder_length_get(&key));
    nexus_strings_string_builder_destroy(&key);
  }

  (void)stbsp_snprintf(name, (int)sizeof(name), "/%016llx.pcache", (unsigned long long)identity);
  cache->path = (char *)nexus_memory_allocate(nexus_strings_string_length_get(directory) + nexus_strings_string_length_get(name) + 1);
  nexus_memory_bytes_copy(directory, cache->path, nexus_strings_string_length_get(directory));
  nexus_memory_bytes_copy(name, cache->path + nexus_strings_string_length_get(directory), nexus_strings_string_length_get(name) + 1);
  nexus_memory_deallocate(directory);

  path = nexus_filesystem_path_create(cache->path);
  nexus_memory_bytes_clear(&info, NEXUS_SIZEOF(info));

  if (nexus_filesystem_path_info_get(&path, &info) != NEXUS_PROGRAM_ERROR_SUCCESS() || info.type != NPT_FILE || info.size < 16 ||
      info.size > (uint64)server->settings.values.cache_max_megabytes * 1024 * 1024 * 2) {
    return;
  }

  if (nexus_filesystem_file_open(&path, NFM_READ_BINARY, &file) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return;
  }

  cache->size = (uint_large)info.size;
  cache->blob = (byte *)nexus_memory_allocate(cache->size + 1);
  read_size   = 0;
  reached_eof = FALSE;

  if (nexus_filesystem_file_read(file, cache->blob, cache->size, &read_size, &reached_eof) != NEXUS_PROGRAM_ERROR_SUCCESS() ||
      read_size != cache->size) {
    nexus_memory_deallocate(cache->blob);
    cache->blob = NULL;
    (void)nexus_filesystem_file_close(file);

    return;
  }

  (void)nexus_filesystem_file_close(file);

  reader.bytes    = cache->blob;
  reader.size     = cache->size;
  reader.position = 0;
  reader.ok       = TRUE;

  if (cache->size < 8 || !nexus_memory_bytes_equal(cache->blob, P_CACHE_MAGIC, 8)) {
    nexus_memory_deallocate(cache->blob);
    cache->blob = NULL;

    return;
  }

  reader.position = 8;

  {
    char   *provider;
    char   *fingerprint;
    boolean match;

    provider    = p_cache_read_string(&reader);
    fingerprint = p_cache_read_string(&reader);
    match       = reader.ok && provider != NULL && fingerprint != NULL &&
            nexus_strings_string_equals(provider, server->provider.name != NULL ? server->provider.name : "") &&
            nexus_strings_string_equals(fingerprint, p_cache_fingerprint(server));

    if (provider != NULL) {
      nexus_memory_deallocate(provider);
    }

    if (fingerprint != NULL) {
      nexus_memory_deallocate(fingerprint);
    }

    if (!match) {
      nexus_memory_deallocate(cache->blob);
      cache->blob = NULL;

      return;
    }
  }

  count          = p_cache_read_u64(&reader);
  cache->records = nexus_data_word_map_create(1, 1, (uint_large)count + 8);

  for (i = 0; i < count && reader.ok; i++) {
    uint_large  record;
    uint32      length;
    uint_large  path_length;
    const char *record_path;
    uint_large  key[1];
    uint_large *slot;

    record      = reader.position;
    length      = p_cache_read_u32(&reader);
    record_path = p_cache_read_view(&reader, &path_length);

    if (!reader.ok || record_path == NULL || record + length > cache->size) {
      break;
    }

    key[0] = (uint_large)nexus_hash_xxh3_64(record_path, path_length);
    slot   = nexus_data_word_map_get(cache->records, key);

    if (slot == NULL) {
      slot    = nexus_data_word_map_insert(cache->records, key);
      slot[0] = record;
    }

    reader.position = record + length;
  }
}

void pharos_cache_release(PharosCache *cache) {
  if (cache->blob != NULL) {
    nexus_memory_deallocate(cache->blob);
  }

  if (cache->records != NULL) {
    nexus_data_word_map_destroy(cache->records);
  }

  if (cache->path != NULL) {
    nexus_memory_deallocate(cache->path);
  }

  nexus_memory_bytes_clear(cache, NEXUS_SIZEOF(*cache));
}

/* ---------------------------------------------------------------------------- */
/* ATTACHING A RECORD TO A DOCUMENT                                              */
/* ---------------------------------------------------------------------------- */

boolean pharos_cache_attach(PharosServer *server, PharosDocument *document) {
  PharosCache  *cache;
  uint_large    key[1];
  uint_large   *slot;
  PCacheReader  reader;
  uint32        length;
  uint_large    path_length;
  const char   *record_path;
  uint64        size;
  uint64        time;
  uint32        kind;
  uint32        count;
  uint32        i;
  NexusPath     path;
  NexusPathInfo info;

  cache = &server->cache;

  if (cache->blob == NULL || cache->records == NULL || document->open) {
    return FALSE;
  }

  key[0] = (uint_large)p_cache_path_hash(document->path);
  slot   = nexus_data_word_map_get(cache->records, key);

  if (slot == NULL) {
    cache->misses++;

    return FALSE;
  }

  reader.bytes    = cache->blob;
  reader.size     = cache->size;
  reader.position = slot[0];
  reader.ok       = TRUE;
  length          = p_cache_read_u32(&reader);
  record_path     = p_cache_read_view(&reader, &path_length);
  (void)length;

  if (!reader.ok || record_path == NULL || path_length != nexus_strings_string_length_get(document->path) ||
      !nexus_memory_bytes_equal(record_path, document->path, path_length)) {
    cache->misses++;

    return FALSE;
  }

  size = p_cache_read_u64(&reader);
  time = p_cache_read_u64(&reader);
  kind = p_cache_read_u32(&reader);

  /* The file must be the one the record was made from. */
  path = nexus_filesystem_path_create(document->path);
  nexus_memory_bytes_clear(&info, NEXUS_SIZEOF(info));

  if (!reader.ok || kind != document->kind || nexus_filesystem_path_info_get(&path, &info) != NEXUS_PROGRAM_ERROR_SUCCESS() ||
      info.type != NPT_FILE || info.size != size || (uint64)info.modification_time.time != time) {
    cache->misses++;

    return FALSE;
  }

  pharos_documents_names_clear(document);
  count = p_cache_read_u32(&reader);

  for (i = 0; i < count && reader.ok; i++) {
    PharosName name;
    uint_large start;
    uint_large end;

    name.kind        = (int32)p_cache_read_u32(&reader);
    name.symbol_kind = (int32)p_cache_read_u32(&reader);
    name.declaration = p_cache_read_byte(&reader) != 0;
    start            = (uint_large)p_cache_read_u64(&reader);
    end              = (uint_large)p_cache_read_u64(&reader);
    name.start       = start;
    name.end         = end;
    name.name        = p_cache_read_string(&reader);
    name.owner       = p_cache_read_optional(&reader);
    name.scope       = p_cache_read_optional(&reader);

    if (reader.ok && name.name != NULL) {
      (void)nexus_data_list_append(document->names, &name);
    }
  }

  count = reader.ok ? p_cache_read_u32(&reader) : 0;

  for (i = 0; i < count && reader.ok; i++) {
    PharosProblem problem;

    problem.start    = (uint_large)p_cache_read_u64(&reader);
    problem.end      = (uint_large)p_cache_read_u64(&reader);
    problem.severity = (int32)p_cache_read_u32(&reader);
    problem.code     = p_cache_read_string(&reader);
    problem.message  = p_cache_read_string(&reader);

    if (reader.ok) {
      (void)nexus_data_list_append(document->problems, &problem);
    }
  }

  if (!reader.ok) {
    pharos_documents_names_clear(document);
    cache->misses++;

    return FALSE;
  }

  document->stamp_time = (timestamp)time;
  document->stamp_size = size;
  document->analyzed   = TRUE;
  document->from_cache = TRUE;
  cache->hits++;

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* SAVING                                                                        */
/* ---------------------------------------------------------------------------- */

static void p_cache_record_write(NexusDataList *out, const PharosDocument *document) {
  NexusDataList *record;
  uint_large     i;
  uint_large     length;
  uint_large     offset;

  record = nexus_data_list_create(NEXUS_SIZEOF(byte), 512);
  p_cache_string(record, document->path);
  p_cache_u64(record, document->stamp_size);
  p_cache_u64(record, (uint64)document->stamp_time);
  p_cache_u32(record, document->kind);
  p_cache_u32(record, (uint32)nexus_data_list_length_get(document->names));

  for (i = 0; i < nexus_data_list_length_get(document->names); i++) {
    const PharosName *name;
    byte              declaration;

    name        = (const PharosName *)nexus_data_list_at_const(document->names, i);
    declaration = name->declaration ? 1 : 0;
    p_cache_u32(record, (uint32)name->kind);
    p_cache_u32(record, (uint32)name->symbol_kind);
    (void)nexus_data_list_append(record, &declaration);
    p_cache_u64(record, name->start);
    p_cache_u64(record, name->end);
    p_cache_string(record, name->name);
    p_cache_optional(record, name->owner);
    p_cache_optional(record, name->scope);
  }

  p_cache_u32(record, (uint32)nexus_data_list_length_get(document->problems));

  for (i = 0; i < nexus_data_list_length_get(document->problems); i++) {
    const PharosProblem *problem;

    problem = (const PharosProblem *)nexus_data_list_at_const(document->problems, i);
    p_cache_u64(record, problem->start);
    p_cache_u64(record, problem->end);
    p_cache_u32(record, (uint32)problem->severity);
    p_cache_string(record, problem->code);
    p_cache_string(record, problem->message);
  }

  length = nexus_data_list_length_get(record) + 4;
  p_cache_u32(out, (uint32)length);

  for (offset = 0; offset < nexus_data_list_length_get(record); offset++) {
    (void)nexus_data_list_append(out, nexus_data_list_at_const(record, offset));
  }

  nexus_data_list_destroy(record);
}

void pharos_cache_save(PharosServer *server) {
  PharosCache     *cache;
  NexusDataList   *out;
  NexusDataList   *records;
  uint_large       count;
  uint_large       i;
  NexusPath        final_path;
  NexusPath        temporary;
  char            *temporary_text;
  NexusFileHandle *file;
  uint_large       written;
  uint_large       limit;

  cache = &server->cache;

  if (!server->settings.values.cache || cache->path == NULL) {
    return;
  }

  records = nexus_data_list_create(NEXUS_SIZEOF(byte), 4096);
  count   = 0;

  /* This session's analyses first; documents that were never analysed keep the record they had. */
  for (i = 0; i < nexus_data_list_length_get(server->documents); i++) {
    const PharosDocument *document;

    document = (const PharosDocument *)nexus_data_list_pointer_at(server->documents, i);

    if (document->open || !document->analyzed || (document->stamp_size == 0 && document->stamp_time == 0)) {
      continue;
    }

    p_cache_record_write(records, document);
    count++;
  }

  if (cache->blob != NULL && cache->records != NULL) {
    for (i = 0; i < nexus_data_list_length_get(server->documents); i++) {
      const PharosDocument *document;
      uint_large            key[1];
      uint_large           *slot;

      document = (const PharosDocument *)nexus_data_list_pointer_at(server->documents, i);

      if (document->open || document->analyzed) {
        continue;
      }

      key[0] = (uint_large)p_cache_path_hash(document->path);
      slot   = nexus_data_word_map_get(cache->records, key);

      if (slot != NULL) {
        PCacheReader reader;
        uint32       length;
        uint_large   start;

        reader.bytes    = cache->blob;
        reader.size     = cache->size;
        reader.position = slot[0];
        reader.ok       = TRUE;
        start           = slot[0];
        length          = p_cache_read_u32(&reader);

        if (reader.ok && start + length <= cache->size) {
          p_cache_bytes(records, cache->blob + start, length);
          count++;
        }
      }
    }
  }

  limit = (uint_large)server->settings.values.cache_max_megabytes * 1024 * 1024;

  if (count == 0 || nexus_data_list_length_get(records) > limit) {
    nexus_data_list_destroy(records);

    return;
  }

  out = nexus_data_list_create(NEXUS_SIZEOF(byte), nexus_data_list_length_get(records) + 256);
  p_cache_bytes(out, (const byte *)P_CACHE_MAGIC, 8);
  p_cache_string(out, server->provider.name != NULL ? server->provider.name : "");
  p_cache_string(out, p_cache_fingerprint(server));
  p_cache_u64(out, count);

  for (i = 0; i < nexus_data_list_length_get(records); i++) {
    (void)nexus_data_list_append(out, nexus_data_list_at_const(records, i));
  }

  nexus_data_list_destroy(records);

  temporary_text = (char *)nexus_memory_allocate(nexus_strings_string_length_get(cache->path) + 5);
  (void)stbsp_snprintf(temporary_text, (int)nexus_strings_string_length_get(cache->path) + 5, "%s.tmp", cache->path);
  final_path = nexus_filesystem_path_create(cache->path);
  temporary  = nexus_filesystem_path_create(temporary_text);

  if (nexus_filesystem_file_ensure_parent_directory(&temporary) == NEXUS_PROGRAM_ERROR_SUCCESS() &&
      nexus_filesystem_file_open(&temporary, NFM_WRITE_BINARY, &file) == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    written = 0;

    {
      /* The list is contiguous chunks; write it in pieces. */
      uint_large position;

      position = 0;

      while (position < nexus_data_list_length_get(out)) {
        uint_large piece;

        piece = nexus_data_list_length_get(out) - position;

        if (piece > 65536) {
          piece = 65536;
        }

        if (nexus_filesystem_file_write(file, (const byte *)nexus_data_list_at_const(out, position), piece, &written) !=
            NEXUS_PROGRAM_ERROR_SUCCESS()) {
          break;
        }

        position += piece;
      }

      (void)nexus_filesystem_file_close(file);

      if (position == nexus_data_list_length_get(out)) {
        (void)nexus_filesystem_path_rename(&temporary, &final_path);
      }
    }
  }

  nexus_memory_deallocate(temporary_text);
  nexus_data_list_destroy(out);
}
