#include <nexus.h>

#include "./p_internal.h"

/*
The protocol: JSON-RPC 2.0 over stdio with Content-Length framing, and the requests that reduce to documents and names:
diagnostics, document symbols, hover, go to definition, references, highlights, prepare-rename and rename.
*/

#define P_FORMAT(out, ...) p_server_check(nexus_strings_string_builder_append_formatted(out, __VA_ARGS__))

static void p_server_check(NexusError *error) {
  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }
}

static void p_add(NexusStringBuilder *out, const char *text) {
  p_server_check(nexus_strings_string_builder_append(out, text));
}

static void p_quote(NexusStringBuilder *out, const char *text) {
  p_server_check(nexus_json_quote_append(out, text));
}

/* ---------------------------------------------------------------------------- */
/* TRANSPORT                                                                     */
/* ---------------------------------------------------------------------------- */

static void p_send(const NexusStringBuilder *body) {
  char       header[64];
  uint_large written;
  uint_large length;

  length = nexus_strings_string_builder_length_get(body);
  (void)nexus_strings_string_format_with_truncation(header, sizeof(header), "Content-Length: %u\r\n\r\n", (unsigned)length);
  (void)nexus_stdio_stdout_write_cstring(header);
  (void)nexus_stdio_stdout_write((const byte *)nexus_strings_string_builder_string_get(body), length, &written);
  (void)nexus_stdio_stdout_flush();
}

/* Summary: reads one framed message into Nexus memory; NULL at the end of input. */
static char *p_receive(uint_large *out_size) {
  char       line[512];
  uint_large length;
  uint_large read;
  boolean    eof;
  char      *body;

  length = 0;

  for (;;) {
    eof = FALSE;

    if (nexus_stdio_stdin_read_line(line, sizeof(line), &eof) != NEXUS_PROGRAM_ERROR_SUCCESS() || eof) {
      return NULL;
    }

    if (line[0] == '\0') {
      break;
    }

    if (nexus_strings_string_starts_with(line, "Content-Length:")) {
      const char *digits;

      digits = line + 15;

      while (*digits == ' ') {
        digits++;
      }

      length = 0;

      while (*digits >= '0' && *digits <= '9') {
        length = (length * 10) + (uint_large)(*digits - '0');
        digits++;
      }
    }
  }

  if (length == 0 || length > 0x10000000U) {
    return NULL;
  }

  body = (char *)nexus_memory_allocate(length + 1);
  read = 0;

  if (nexus_stdio_stdin_read((byte *)body, length, &read) != NEXUS_PROGRAM_ERROR_SUCCESS() || read != length) {
    nexus_memory_deallocate(body);

    return NULL;
  }

  body[length] = '\0';
  *out_size    = length;

  return body;
}

static void p_id(NexusStringBuilder *out, const NexusJson *request) {
  const NexusJson *id;

  id = nexus_json_member_get(request, "id");

  if (id == NULL || id->kind == NJK_NULL) {
    p_add(out, "null");
  } else if (id->kind == NJK_STRING) {
    p_quote(out, id->text);
  } else {
    P_FORMAT(out, "%lld", (long long)id->integer);
  }
}

static void p_reply_begin(NexusStringBuilder *out, const NexusJson *request) {
  p_add(out, "{\"jsonrpc\":\"2.0\",\"id\":");
  p_id(out, request);
  p_add(out, ",\"result\":");
}

static void p_error_send(const NexusJson *request, int code, const char *message) {
  NexusStringBuilder out;

  out = nexus_strings_string_builder_create_growable(128);
  p_add(&out, "{\"jsonrpc\":\"2.0\",\"id\":");
  p_id(&out, request);
  P_FORMAT(&out, ",\"error\":{\"code\":%d,\"message\":", code);
  p_quote(&out, message);
  p_add(&out, "}}");
  p_send(&out);
  nexus_strings_string_builder_destroy(&out);
}

/* ---------------------------------------------------------------------------- */
/* QUERIES OVER NAMES                                                            */
/* ---------------------------------------------------------------------------- */

typedef struct PCollect {
  NexusStringBuilder   *out;
  uint32                count;
  boolean               declarations_only;
  boolean               skip_declarations;
  const PharosDocument *only;
  const char           *new_text;
  const char           *current_uri;
  boolean               open_group;
  PharosDocument       *first_document;
  const PharosName     *first_name;
} PCollect;

static void p_collect_location(PharosDocument *document, const PharosName *name, void *user) {
  PCollect *collect;

  collect = (PCollect *)user;

  if ((collect->declarations_only && !name->declaration) || (collect->skip_declarations && name->declaration) ||
      (collect->only != NULL && collect->only != document)) {
    return;
  }

  P_FORMAT(collect->out, "%s{\"uri\":", collect->count > 0 ? "," : "");
  p_quote(collect->out, document->uri);
  p_add(collect->out, ",\"range\":");
  pharos_documents_range_write(collect->out, document, name->start, name->end);
  p_add(collect->out, "}");
  collect->count++;
}

static void p_collect_edit(PharosDocument *document, const PharosName *name, void *user) {
  PCollect *collect;

  collect = (PCollect *)user;

  if (collect->current_uri == NULL || !nexus_strings_string_equals(collect->current_uri, document->uri)) {
    if (collect->open_group) {
      p_add(collect->out, "],");
    }

    p_quote(collect->out, document->uri);
    p_add(collect->out, ":[");
    collect->current_uri = document->uri;
    collect->open_group  = TRUE;
    collect->count       = 0;
  }

  P_FORMAT(collect->out, "%s{\"range\":", collect->count > 0 ? "," : "");
  pharos_documents_range_write(collect->out, document, name->start, name->end);
  p_add(collect->out, ",\"newText\":");
  p_quote(collect->out, collect->new_text);
  p_add(collect->out, "}");
  collect->count++;
}

static void p_collect_first(PharosDocument *document, const PharosName *name, void *user) {
  PCollect *collect;

  collect = (PCollect *)user;

  if (name->declaration && collect->first_name == NULL) {
    collect->first_document = document;
    collect->first_name     = name;
  }

  if (name->declaration) {
    collect->count++;
  }
}

static const char *p_file_name(const char *path) {
  const char *name;
  const char *cursor;

  name = path;

  for (cursor = path; *cursor != '\0'; cursor++) {
    if (*cursor == '/') {
      name = cursor + 1;
    }
  }

  return name;
}

static const char *p_kind_label(const PharosServer *server, const PharosName *name) {
  const char *label;

  label = server->provider.kind_label != NULL ? server->provider.kind_label(server->provider.user, name->kind) : NULL;

  return label != NULL ? label : "name";
}

static const char *p_kind_sigil(const PharosServer *server, const PharosName *name) {
  const char *sigil;

  sigil = server->provider.kind_sigil != NULL ? server->provider.kind_sigil(server->provider.user, name->kind) : NULL;

  return sigil != NULL ? sigil : "";
}

/* ---------------------------------------------------------------------------- */
/* DIAGNOSTICS                                                                   */
/* ---------------------------------------------------------------------------- */

static void p_diagnostics_publish(PharosServer *server, PharosDocument *document) {
  NexusStringBuilder out;
  uint_large         i;

  (void)pharos_documents_ensure(server, document);
  out = nexus_strings_string_builder_create_growable(512);
  p_add(&out, "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":");
  p_quote(&out, document->uri);
  p_add(&out, ",\"diagnostics\":[");

  for (i = 0; i < nexus_data_list_length_get(document->problems); i++) {
    const PharosProblem *problem;

    problem = (const PharosProblem *)nexus_data_list_at_const(document->problems, i);
    P_FORMAT(&out, "%s{\"range\":", i > 0 ? "," : "");
    pharos_documents_range_write(&out, document, problem->start, problem->end);
    P_FORMAT(&out, ",\"severity\":%d,\"source\":", (int)problem->severity);
    p_quote(&out, server->provider.name != NULL ? server->provider.name : "pharos");
    p_add(&out, ",\"code\":");
    p_quote(&out, problem->code != NULL ? problem->code : "");
    p_add(&out, ",\"message\":");
    p_quote(&out, problem->message);
    p_add(&out, "}");
  }

  p_add(&out, "]}}");
  p_send(&out);
  nexus_strings_string_builder_destroy(&out);
}

/* ---------------------------------------------------------------------------- */
/* REQUESTS                                                                      */
/* ---------------------------------------------------------------------------- */

static PharosDocument *p_request_document(PharosServer *server, const NexusJson *params, uint_large *out_offset) {
  const NexusJson *text_document;
  const NexusJson *position;
  PharosDocument  *document;
  const char      *uri;
  char            *path;

  text_document = nexus_json_member_get(params, "textDocument");
  position      = nexus_json_member_get(params, "position");
  uri           = text_document != NULL ? nexus_json_member_text_get(text_document, "uri") : NULL;
  document      = NULL;

  if (uri != NULL) {
    path     = pharos_paths_path_of_uri(uri);
    document = pharos_documents_document_find(server, path);
    nexus_memory_deallocate(path);
  }

  if (document != NULL) {
    (void)pharos_documents_ensure(server, document);
  }

  if (document != NULL && position != NULL && out_offset != NULL) {
    *out_offset = pharos_documents_offset_get(document, (uint32)nexus_json_member_integer_get(position, "line", 0),
                                              (uint32)nexus_json_member_integer_get(position, "character", 0));
  }

  return document;
}

static boolean p_identifier(const char *text) {
  const char *cursor;

  if (text == NULL || text[0] == '\0' || (text[0] >= '0' && text[0] <= '9')) {
    return FALSE;
  }

  for (cursor = text; *cursor != '\0'; cursor++) {
    if (!((*cursor >= 'a' && *cursor <= 'z') || (*cursor >= 'A' && *cursor <= 'Z') || (*cursor >= '0' && *cursor <= '9') || *cursor == '_')) {
      return FALSE;
    }
  }

  return TRUE;
}

static boolean p_renameable(const PharosServer *server, const PharosName *name) {
  return name != NULL && name->owner != NULL &&
         (server->provider.kind_rename == NULL || server->provider.kind_rename(server->provider.user, name->kind));
}

static void p_hover(PharosServer *server, PharosDocument *document, uint_large offset, const NexusJson *message, NexusStringBuilder *out) {
  const PharosName  *name;
  NexusStringBuilder markup;

  p_reply_begin(out, message);
  name = pharos_names_name_at(document, offset);

  if (name != NULL) {
    PCollect collect;
    uint32   line;
    uint32   character;

    collect.count          = 0;
    collect.first_document = NULL;
    collect.first_name     = NULL;
    pharos_names_occurrences_visit(server, name, FALSE, p_collect_first, &collect);
    markup = nexus_strings_string_builder_create_growable(128);

    if (collect.first_name != NULL) {
      pharos_documents_position_get(collect.first_document, collect.first_name->start, &line, &character);
      P_FORMAT(&markup, "%s `%s%s`\n\nDeclared in `%s`, line %u%s", p_kind_label(server, name), p_kind_sigil(server, name), name->name,
               p_file_name(collect.first_document->path), (unsigned)line + 1, collect.count > 1 ? " (and in other contexts)" : "");
    } else {
      P_FORMAT(&markup, "%s `%s%s`\n\nNo declaration found", p_kind_label(server, name), p_kind_sigil(server, name), name->name);
    }

    p_add(out, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
    p_quote(out, nexus_strings_string_builder_string_get(&markup));
    p_add(out, "},\"range\":");
    pharos_documents_range_write(out, document, name->start, name->end);
    p_add(out, "}}");
    nexus_strings_string_builder_destroy(&markup);

    return;
  }

  markup = nexus_strings_string_builder_create_growable(128);

  if (server->provider.hover != NULL && offset < document->size && server->provider.hover(server->provider.user, document, offset, &markup)) {
    p_add(out, "{\"contents\":{\"kind\":\"markdown\",\"value\":");
    p_quote(out, nexus_strings_string_builder_string_get(&markup));
    p_add(out, "}}}");
  } else {
    p_add(out, "null}");
  }

  nexus_strings_string_builder_destroy(&markup);
}

static void p_handle(PharosServer *server, const NexusJson *message) {
  const char        *method;
  const NexusJson   *params;
  const NexusJson   *id;
  NexusStringBuilder out;
  PharosDocument    *document;
  uint_large         offset;

  method = nexus_json_member_text_get(message, "method");
  params = nexus_json_member_get(message, "params");
  id     = nexus_json_member_get(message, "id");

  if (method == NULL) {
    return;
  }

  out    = nexus_strings_string_builder_create_growable(256);
  offset = 0;

  if (nexus_strings_string_equals(method, "initialize")) {
    const NexusJson *folders;
    uint32           i;

    if (params != NULL) {
      const char *root;

      root = nexus_json_member_text_get(params, "rootUri");

      if (root != NULL) {
        (void)nexus_data_list_append_pointer(server->roots, pharos_paths_path_of_uri(root));
      }

      folders = nexus_json_member_get(params, "workspaceFolders");

      for (i = 0; folders != NULL && folders->kind == NJK_ARRAY && i < folders->count; i++) {
        const char *uri;

        uri = nexus_json_member_text_get(folders->items[i], "uri");

        if (uri != NULL) {
          (void)nexus_data_list_append_pointer(server->roots, pharos_paths_path_of_uri(uri));
        }
      }
    }

    /* The provider reads its configuration for the root of the project, and the cache of this workspace is opened. */
    {
      const char *first_root;

      first_root = nexus_data_list_length_get(server->roots) > 0 ? (const char *)nexus_data_list_pointer_at(server->roots, 0) : NULL;

      if (server->provider.configure != NULL) {
        PharosSettings settings;

        settings = server->settings.values;
        server->provider.configure(server->provider.user, first_root, &settings);
        pharos_settings_apply(server, &settings);
      }

      pharos_cache_open(server, first_root);
    }

    p_reply_begin(&out, message);
    p_add(&out, "{\"capabilities\":{\"textDocumentSync\":{\"openClose\":true,\"change\":1},\"hoverProvider\":true,\"definitionProvider\":true,"
                "\"referencesProvider\":true,\"documentHighlightProvider\":true,\"documentSymbolProvider\":true,"
                "\"renameProvider\":{\"prepareProvider\":true}},\"serverInfo\":{\"name\":");
    p_quote(&out, server->provider.name != NULL ? server->provider.name : "pharos");
    p_add(&out, "}}}");
    p_send(&out);

    /* The files of the workspace are found while the editor settles. */
    pharos_workspace_start(server);
  } else if (nexus_strings_string_equals(method, "shutdown")) {
    server->shutdown = TRUE;
    p_reply_begin(&out, message);
    p_add(&out, "null}");
    p_send(&out);
  } else if (nexus_strings_string_equals(method, "exit")) {
    server->exit = TRUE;
  } else if (nexus_strings_string_equals(method, "textDocument/didOpen") || nexus_strings_string_equals(method, "textDocument/didChange")) {
    const NexusJson *text_document;
    const char      *uri;
    const char      *text;

    text_document = params != NULL ? nexus_json_member_get(params, "textDocument") : NULL;
    uri           = text_document != NULL ? nexus_json_member_text_get(text_document, "uri") : NULL;
    text          = NULL;

    if (nexus_strings_string_equals(method, "textDocument/didOpen")) {
      text = text_document != NULL ? nexus_json_member_text_get(text_document, "text") : NULL;
    } else {
      const NexusJson *changes;

      changes = nexus_json_member_get(params, "contentChanges");

      if (changes != NULL && changes->kind == NJK_ARRAY && changes->count > 0) {
        text = nexus_json_member_text_get(changes->items[changes->count - 1], "text");
      }
    }

    if (uri != NULL && text != NULL) {
      document = pharos_documents_open_set(server, uri, text, nexus_json_member_integer_get(text_document, "version", 0));

      if (document != NULL) {
        p_diagnostics_publish(server, document);
      }
    }
  } else if (nexus_strings_string_equals(method, "textDocument/didClose")) {
    const NexusJson *text_document;
    const char      *uri;

    text_document = params != NULL ? nexus_json_member_get(params, "textDocument") : NULL;
    uri           = text_document != NULL ? nexus_json_member_text_get(text_document, "uri") : NULL;

    if (uri != NULL) {
      pharos_documents_close(server, uri);
    }
  } else if (id == NULL) {
    /* A notification this server does not know is ignored. */
  } else if (nexus_strings_string_equals(method, "textDocument/documentSymbol")) {
    uint_large i;
    uint32     count;

    document = p_request_document(server, params, NULL);
    p_reply_begin(&out, message);
    p_add(&out, "[");
    count = 0;

    for (i = 0; document != NULL && i < nexus_data_list_length_get(document->names); i++) {
      const PharosName *name;

      name = (const PharosName *)nexus_data_list_at_const(document->names, i);

      if (!name->declaration) {
        continue;
      }

      P_FORMAT(&out, "%s{\"name\":", count > 0 ? "," : "");
      p_quote(&out, name->name);
      P_FORMAT(&out, ",\"kind\":%d,\"range\":", (int)name->symbol_kind);
      pharos_documents_range_write(&out, document, name->start, name->end);
      p_add(&out, ",\"selectionRange\":");
      pharos_documents_range_write(&out, document, name->start, name->end);
      p_add(&out, "}");
      count++;
    }

    p_add(&out, "]}");
    p_send(&out);
  } else if (nexus_strings_string_equals(method, "textDocument/hover")) {
    document = p_request_document(server, params, &offset);

    if (document == NULL) {
      p_reply_begin(&out, message);
      p_add(&out, "null}");
    } else {
      p_hover(server, document, offset, message, &out);
    }

    p_send(&out);
  } else if (nexus_strings_string_equals(method, "textDocument/definition") || nexus_strings_string_equals(method, "textDocument/references") ||
             nexus_strings_string_equals(method, "textDocument/documentHighlight")) {
    const PharosName *name;
    PCollect          collect;
    boolean           highlight;
    boolean           definition;
    const NexusJson  *context;
    const NexusJson  *include;

    document   = p_request_document(server, params, &offset);
    highlight  = nexus_strings_string_equals(method, "textDocument/documentHighlight");
    definition = nexus_strings_string_equals(method, "textDocument/definition");
    name       = document != NULL ? pharos_names_name_at(document, offset) : NULL;
    context    = params != NULL ? nexus_json_member_get(params, "context") : NULL;
    include    = context != NULL ? nexus_json_member_get(context, "includeDeclaration") : NULL;
    p_reply_begin(&out, message);
    p_add(&out, "[");

    if (name != NULL) {
      collect.out               = &out;
      collect.count             = 0;
      collect.declarations_only = definition;
      collect.skip_declarations = !definition && !highlight && include != NULL && include->kind == NJK_BOOLEAN && !include->boolean_value;
      collect.only              = highlight ? document : NULL;
      pharos_names_occurrences_visit(server, name, !definition && !highlight, p_collect_location, &collect);

      /* A name no declaration anchors: go where the grammar mentions it. */
      if (definition && collect.count == 0) {
        collect.declarations_only = FALSE;
        pharos_names_mentions_visit(server, name, p_collect_location, &collect);
      }
    }

    p_add(&out, "]}");
    p_send(&out);
  } else if (nexus_strings_string_equals(method, "textDocument/prepareRename")) {
    const PharosName *name;

    document = p_request_document(server, params, &offset);
    name     = document != NULL ? pharos_names_name_at(document, offset) : NULL;
    p_reply_begin(&out, message);

    if (p_renameable(server, name)) {
      pharos_documents_range_write(&out, document, name->start, name->end);
      p_add(&out, "}");
    } else {
      p_add(&out, "null}");
    }

    p_send(&out);
  } else if (nexus_strings_string_equals(method, "textDocument/rename")) {
    const PharosName *name;
    const char       *new_name;

    document = p_request_document(server, params, &offset);
    name     = document != NULL ? pharos_names_name_at(document, offset) : NULL;
    new_name = params != NULL ? nexus_json_member_text_get(params, "newName") : NULL;

    if (!p_renameable(server, name)) {
      p_reply_begin(&out, message);
      p_add(&out, "null}");
      p_send(&out);
    } else if (!p_identifier(new_name)) {
      p_error_send(message, -32602, "a name is letters, digits and underscores, and does not start with a digit");
    } else {
      PCollect collect;

      collect.out         = &out;
      collect.count       = 0;
      collect.new_text    = new_name;
      collect.current_uri = NULL;
      collect.open_group  = FALSE;
      p_reply_begin(&out, message);
      p_add(&out, "{\"changes\":{");
      pharos_names_occurrences_visit(server, name, TRUE, p_collect_edit, &collect);

      if (collect.open_group) {
        p_add(&out, "]");
      }

      p_add(&out, "}}}");
      p_send(&out);
    }
  } else {
    p_error_send(message, -32601, "method not found");
  }

  nexus_strings_string_builder_destroy(&out);
}

/* ---------------------------------------------------------------------------- */
/* THE SERVER                                                                    */
/* ---------------------------------------------------------------------------- */

PharosServer *pharos_server_create(PharosRuntime *runtime, const PharosProvider *provider, const char *const *folders, uint_large folder_count) {
  PharosServer *server;
  uint_large    i;

  NEXUS_PROGRAM_PRECONDITION(provider != NULL && provider->document_kind != NULL && provider->analyze != NULL,
                             "a provider must serve and analyse documents");

  server            = (PharosServer *)nexus_memory_allocate(NEXUS_SIZEOF(*server));
  server->runtime   = runtime;
  server->provider  = *provider;
  server->documents = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  server->index     = nexus_data_word_map_create(1, 1, 256);
  nexus_memory_bytes_clear(&server->settings, NEXUS_SIZEOF(server->settings));
  nexus_memory_bytes_clear(&server->cache, NEXUS_SIZEOF(server->cache));
  pharos_settings_default_apply(server);
  server->folders      = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  server->roots        = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  server->scanned      = FALSE;
  server->scan_started = FALSE;
  server->scan_thread  = NULL;
  server->scan_paths   = NULL;
  server->shutdown     = FALSE;
  server->exit         = FALSE;

  for (i = 0; i < folder_count; i++) {
    char *folder;

    folder = pharos_paths_text_copy(folders[i]);
    pharos_paths_path_normalize(folder);
    (void)nexus_data_list_append_pointer(server->folders, folder);
  }

  return server;
}

void pharos_server_destroy(PharosServer *server) {
  uint_large i;

  if (server->scan_thread != NULL) {
    (void)nexus_threads_join(server->scan_thread);
    server->scan_thread = NULL;
  }

  /* What this session analysed is kept for the next one. */
  pharos_cache_save(server);
  pharos_cache_release(&server->cache);
  pharos_settings_release(&server->settings);
  nexus_data_word_map_destroy(server->index);

  if (server->scan_paths != NULL) {
    for (i = 0; i < nexus_data_list_length_get(server->scan_paths); i++) {
      nexus_memory_deallocate(nexus_data_list_pointer_at(server->scan_paths, i));
    }

    nexus_data_list_destroy(server->scan_paths);
  }

  for (i = 0; i < nexus_data_list_length_get(server->documents); i++) {
    pharos_documents_destroy((PharosDocument *)nexus_data_list_pointer_at(server->documents, i));
  }

  for (i = 0; i < nexus_data_list_length_get(server->folders); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(server->folders, i));
  }

  for (i = 0; i < nexus_data_list_length_get(server->roots); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(server->roots, i));
  }

  nexus_data_list_destroy(server->documents);
  nexus_data_list_destroy(server->folders);
  nexus_data_list_destroy(server->roots);
  nexus_memory_deallocate(server);
}

uint_large pharos_server_folder_count_get(const PharosServer *server) {
  return nexus_data_list_length_get(server->folders);
}

const char *pharos_server_folder_at(const PharosServer *server, uint_large index) {
  return (const char *)nexus_data_list_pointer_at(server->folders, index);
}

int pharos_server_run(PharosServer *server) {
  while (!server->exit) {
    NexusDataArena   arena;
    const NexusJson *message;
    char            *body;
    uint_large       size;

    body = p_receive(&size);

    if (body == NULL) {
      break;
    }

    nexus_data_arena_initialize(&arena);
    message = nexus_json_parse(&arena, body, size);

    if (message != NULL) {
      p_handle(server, message);
    }

    nexus_data_arena_release(&arena);
    nexus_memory_deallocate(body);
  }

  return 0;
}
