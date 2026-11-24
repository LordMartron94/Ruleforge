#pragma once

#include <nexus.h>
#include <echo.h>

#include "../pharos.h"

struct PharosRuntime {
  EchoRuntime *echo;
  EchoSystem  *log_system;
  EchoContext *log_context;
};

typedef struct PharosProblem {
  uint_large start;
  uint_large end;
  int32      severity;
  char      *code;
  char      *message;
} PharosProblem;

struct PharosDocument {
  char                       *uri;
  char                       *path;
  char                       *text; /* NUL-terminated; NULL while a file found on disk has not been read yet. */
  uint_large                  size;
  int64                       version;
  boolean                     open; /* Sent by the client; otherwise what is on disk. */
  boolean                     analyzed;
  uint32                      kind;
  timestamp                   stamp_time; /* Of the file on disk when it was read. */
  uint64                      stamp_size;
  boolean                     from_cache; /* The names were read from the cache and the text has not been read; reading it keeps them when the stamp matches. */
  boolean                     lines_built;
  NexusDataList              *line_starts; /* uint_large */
  NexusDataList              *names;       /* PharosName, strings owned */
  NexusDataList              *problems;    /* PharosProblem */
  void                       *state;
  PharosStateDestroyCallback *state_destroy;
};

/* The settings of a server, with the strings and the list it owns. */
typedef struct PharosOwnedSettings {
  PharosSettings values;
  char          *cache_directory;
  char         **ignore;
  uint_large     ignore_count;
} PharosOwnedSettings;

/* The analyses kept on disk: a file read whole, and where each document's record is in it. */
typedef struct PharosCache {
  char             *path;        /* The cache file of this workspace; NULL until the root is known. */
  byte             *blob;        /* What was read from it; NULL when there was none or it did not match. */
  uint_large        size;
  NexusDataWordMap *records;     /* hash of a path -> offset of its record in blob */
  boolean           loaded;
  uint_large        hits;        /* Documents whose analysis came from here. */
  uint_large        misses;
} PharosCache;

struct PharosServer {
  PharosRuntime *runtime;
  PharosProvider provider;
  PharosOwnedSettings settings;
  PharosCache         cache;
  NexusDataWordMap   *index; /* hash of a path -> index into documents (collisions are resolved by comparing paths) */
  NexusDataList *documents; /* PharosDocument * */
  NexusDataList *folders;   /* char * */
  NexusDataList *roots;     /* char *: the folders of the client's workspace */
  boolean        scanned;
  boolean        scan_started;
  NexusThread   *scan_thread;
  NexusDataList *scan_paths; /* char *: what the background walk found, until the main thread takes it. */
  boolean        shutdown;
  boolean        exit;
};

/* ---- p_documents.c ---- */

extern PharosDocument *pharos_documents_document_find(const PharosServer *server, const char *path);

/* Summary: a text the client sent: creates or replaces the document and analyses it. */
extern PharosDocument *pharos_documents_open_set(PharosServer *server, const char *uri, const char *text, int64 version);
extern void            pharos_documents_close(PharosServer *server, const char *uri);

/* Summary: a file of disk as a document, not read yet. Returns NULL when it is not served or already known. */
extern PharosDocument *pharos_documents_file_add(PharosServer *server, const char *path);

/* Summary: the document of a path, known or read from disk; NULL when it cannot be read or is not served. Not analysed. */
extern PharosDocument *pharos_documents_load(PharosServer *server, const char *path);

/*
Summary: brings a document up to date: a file on disk is read when it was never read or its stamp changed, and the text is
  analysed when the analysis is missing or stale. Returns FALSE when the text is unavailable.
*/
extern boolean pharos_documents_ensure(PharosServer *server, PharosDocument *document);

/* Summary: reads the text of a file document if it has none. Does not analyse. */
extern boolean pharos_documents_text_ensure(PharosServer *server, PharosDocument *document);

extern void       pharos_documents_destroy(PharosDocument *document);
extern void       pharos_documents_names_clear(PharosDocument *document);
extern void       pharos_documents_range_write(NexusStringBuilder *out, PharosDocument *document, uint_large start, uint_large end);
extern uint_large pharos_documents_offset_get(PharosDocument *document, uint32 line, uint32 character);
extern void       pharos_documents_position_get(PharosDocument *document, uint_large offset, uint32 *out_line, uint32 *out_character);

/* ---- p_workspace.c ---- */

/* Summary: starts looking for the files of the workspace on another thread (idempotent). */
extern void pharos_workspace_start(PharosServer *server);

/* Summary: waits for that walk if it is still running and makes a document of each file found (idempotent). */
extern void pharos_workspace_scan(PharosServer *server);

/* ---- p_names.c ---- */

extern const PharosName *pharos_names_name_at(const PharosDocument *document, uint_large offset);
extern boolean           pharos_names_same(const PharosServer *server, const PharosName *left, const PharosName *right);

typedef void PharosNamesVisitCallback(PharosDocument *document, const PharosName *name, void *user);

/*
Summary: calls back for every occurrence, in every document, that means the same as a name. The document that declares it
  is read first; with everything set the whole workspace is searched (only files whose text contains the name are analysed).
*/
extern void pharos_names_occurrences_visit(PharosServer *server, const PharosName *name, boolean everything, PharosNamesVisitCallback *callback,
                                           void *user);

/* Summary: calls back for every name of the owner's document that the provider calls a mention of the name. */
extern void pharos_names_mentions_visit(PharosServer *server, const PharosName *name, PharosNamesVisitCallback *callback, void *user);

/* ---- p_paths.c ---- */

/* Summary: Nexus-allocated copies of text, and of length bytes of it as text. */
extern char *pharos_paths_text_copy(const char *text);
extern char *pharos_paths_bytes_copy(const char *text, uint_large length);

/* ---- p_diagnostics.c ---- */

typedef struct PharosLabel {
  PharosSpan span;
  char      *text; /* May be NULL. */
  boolean    primary;
} PharosLabel;

struct PharosDiagnostic {
  PharosSeverity severity;
  char          *code;
  char          *message;
  PharosSpan     span;
  const char    *source_name; /* Borrowed from the source table. */
  uint_large     line;        /* 1-based line and column of the primary span's start. */
  uint_large     column;
  NexusDataList *labels; /* PharosLabel */
  NexusDataList *notes;  /* char *, as pointers */
  NexusDataList *helps;  /* char *, as pointers */
};

struct PharosDiagnostics {
  NexusDataList *sources;     /* PharosSource * */
  NexusDataList *diagnostics; /* PharosDiagnostic * */
};

/* ---- p_settings.c ---- */

/* Summary: copies settings into the server, replacing what it had. */
extern void pharos_settings_apply(PharosServer *server, const PharosSettings *settings);
extern void pharos_settings_release(PharosOwnedSettings *owned);
extern void pharos_settings_default_apply(PharosServer *server);

/* ---- p_cache.c ---- */

/*
Summary: finds the cache file of the workspace (named by the provider, its fingerprint and the root) and reads it once. Safe to call again.
*/
extern void pharos_cache_open(PharosServer *server, const char *root);

/* Summary: gives a file document the names and problems of the cache when the file on disk is the one they were made from. TRUE on a hit. */
extern boolean pharos_cache_attach(PharosServer *server, PharosDocument *document);

/* Summary: writes the cache: the analyses of this session, and the records of files that were not touched and still exist. */
extern void pharos_cache_save(PharosServer *server);

extern void pharos_cache_release(PharosCache *cache);
