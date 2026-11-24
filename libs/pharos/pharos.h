/*
Summary: Pharos, a language server framework and diagnostics rendering with an Echo runtime: anchored or unanchored names.
Layer: editor tooling above Nexus and Echo.
See: DESIGN.md
*/
#pragma once

#include <nexus.h>
#include <echo.h>

/* ---------------------------------------------------------------------------- */
/* RUNTIME                                                                      */
/* ---------------------------------------------------------------------------- */

#ifndef PHAROS_ECHO_LABEL
#  define PHAROS_ECHO_LABEL "Pharos"
#endif

/*
Summary: owned Pharos context borrowing its caller's Echo runtime.
Ownership: destroy before the borrowed Echo runtime.
Thread-safety: synchronize runtime lifecycle and Echo configuration.
*/
typedef struct PharosRuntime PharosRuntime;

/*
Summary: creates a Pharos runtime with its Echo system and logging context.
Params:
  echo  borrowed; must outlive Pharos; started by its caller when logging
  parent_system  optional borrowed registered parent in echo
Returns: the owned runtime.
Errors: failing Echo registration panics, per the library Runtime construction policy.
*/
extern PharosRuntime *pharos_runtime_create(EchoRuntime *echo, EchoSystem *parent_system);

/* Summary: destroys the runtime and unregisters its Echo system. */
extern void pharos_runtime_destroy(PharosRuntime *runtime);

/* Summary: the registered Echo system of the runtime; borrowed. */
extern EchoSystem *pharos_runtime_echo_system_get(PharosRuntime *runtime);

/* Summary: the logging context of the runtime; borrowed. */
extern EchoContext *pharos_runtime_echo_context_get(PharosRuntime *runtime);

/* ---------------------------------------------------------------------------- */
/* LANGUAGE SERVER                                                              */
/* ---------------------------------------------------------------------------- */

/*
A language server over stdio for any language a provider can analyse. The provider turns one document into names (identifiers
with the declaration each means) and problems (syntax errors); Pharos does the rest: the protocol, the documents of the editor and
of the workspace on disk, exact positions, and the questions built on names. Pharos never judges what a name means: two names
are the same when their owner, kind, scope and text are, and everything it answers follows from that.
*/

typedef struct PharosServer   PharosServer;
typedef struct PharosDocument PharosDocument;

/*
Summary: one occurrence of a name in a document.
Fields:
  start,end    the identifier's byte range (without any sigil)
  kind         the provider's number for the kind of name (rule, token, ...)
  name         the text
  owner        the file (or other key) that declares the name; NULL when nothing is known, which leaves it unanchored
  scope        what the declaration lives in (a template, a context), or NULL
  declaration  TRUE when this occurrence declares the name
  symbol_kind  an LSP SymbolKind, for declarations
*/
typedef struct PharosName {
  uint_large  start;
  uint_large  end;
  int32       kind;
  const char *name;
  const char *owner;
  const char *scope;
  boolean     declaration;
  int32       symbol_kind;
} PharosName;

/*
Summary: what a language gives the server. Every callback is optional except document_kind and analyze.
Fields:
  user           passed back to every callback
  name           the server's name for the client
  document_kind  whether a file is served (by its normalised path), and which kind of document it is (a number of the provider's)
  analyze        reads the text of a document and reports its names and problems with pharos_document_name_add and
                 pharos_document_problem_add; called again whenever the text changes. Problems are syntax errors only.
  workspace_file whether a file of the workspace should be known to the server (NULL: every served file)
  kind_label     a word for a kind of name ("rule"), for hovers
  kind_sigil     the prefix a kind is written with ("#"), for hovers
  kind_text      FALSE for a kind whose occurrences may be written differently from the declared name (so text searches skip it)
  kind_rename    FALSE for a kind that cannot be renamed
  mention        whether a candidate name is a mention of a name that has no declaration (a way to still find where it is used)
  hover          extra hover text for a position that is not on a name; return FALSE for none
*/
typedef boolean PharosDocumentKindCallback(void *user, const char *path, uint32 *out_kind);
typedef void    PharosAnalyzeCallback(void *user, PharosServer *server, PharosDocument *document);
/* Summary: whether a file can contain occurrences of name; an optional, read-only prefilter before file I/O and analysis. */
typedef boolean PharosNameFileCallback(void *user, const PharosName *name, const char *path);

typedef boolean     PharosWorkspaceFileCallback(void *user, const char *path);
typedef const char *PharosKindTextCallback(void *user, int32 kind);
typedef boolean     PharosKindFlagCallback(void *user, int32 kind);
typedef boolean     PharosMentionCallback(void *user, const PharosName *name, const PharosName *candidate);
typedef boolean     PharosHoverCallback(void *user, PharosDocument *document, uint_large offset, NexusStringBuilder *markdown);
typedef void        PharosStateDestroyCallback(void *state);

typedef struct PharosSettings PharosSettings;

/*
Summary: how the server behaves; a provider fills it from its own configuration.
Fields:
  walk                look for the files of the workspace on a thread after initialize
  walk_max_files      files the walk takes at most
  walk_max_depth      folder levels the walk enters at most
  ignore, ignore_count  folder names the walk does not enter (NULL: build, .git, external, node_modules, ...)
  cache               keep what the analyses found on disk between sessions, so that a later session does not read and analyse the files again
  cache_directory     where (NULL: the user's cache folder, below pharos/); a relative path is taken from the project root
  cache_max_megabytes the cache is not written when it would be larger
  text_prefilter      analyse a file for a name only when its text contains the name (what clangd and Metals do with an index)
*/
struct PharosSettings {
  boolean            walk;
  uint_large         walk_max_files;
  uint32             walk_max_depth;
  const char *const *ignore;
  uint_large         ignore_count;
  boolean            cache;
  const char        *cache_directory;
  uint32             cache_max_megabytes;
  boolean            text_prefilter;
};

/* Summary: the defaults: walk on, 20000 files, 24 levels, the usual ignored folders, cache on in the user's cache folder, 128 MB, prefilter on. */
extern void pharos_settings_initialize(PharosSettings *settings);

/*
Summary: called once the client's workspace is known (at initialize): the provider reads its configuration for the project root (NULL when
  the client has no folder open) and changes the settings, whose strings and arrays must stay valid until the callback returns (they are copied).
*/
typedef void PharosConfigureCallback(void *user, const char *root, PharosSettings *settings);

/* Summary: a text that changes whenever the analyses of the provider would (its version, its grammars); cached results of another text are not used. */
typedef const char *PharosFingerprintCallback(void *user);

typedef struct PharosProvider {
  void                        *user;
  const char                  *name;
  PharosDocumentKindCallback  *document_kind;
  PharosAnalyzeCallback       *analyze;
  PharosWorkspaceFileCallback *workspace_file;
  PharosNameFileCallback      *name_file; /* NULL searches every workspace file. */
  PharosKindTextCallback      *kind_label;
  PharosKindTextCallback      *kind_sigil;
  PharosKindFlagCallback      *kind_text;
  PharosKindFlagCallback      *kind_rename;
  PharosMentionCallback       *mention;
  PharosHoverCallback         *hover;
  PharosConfigureCallback     *configure;
  PharosFingerprintCallback   *fingerprint;
} PharosProvider;

/*
Summary: creates a server for a provider.
Params:
  runtime   borrowed, for logging
  provider  copied
  folders   normalised search folders the provider may use (imports, companion files), copied
Returns: the server. Release with pharos_server_destroy.
*/
extern PharosServer *pharos_server_create(PharosRuntime *runtime, const PharosProvider *provider, const char *const *folders,
                                          uint_large folder_count);
extern void          pharos_server_destroy(PharosServer *server);

/* Summary: replaces the settings of a server (copied). Call before the workspace is read, or let the provider's configure callback do it. */
extern void pharos_server_settings_set(PharosServer *server, const PharosSettings *settings);

/* Summary: serves the protocol on standard input and output until the client exits or the input ends. Returns the process exit code. */
extern int pharos_server_run(PharosServer *server);

extern uint_large  pharos_server_folder_count_get(const PharosServer *server);
extern const char *pharos_server_folder_at(const PharosServer *server, uint_large index);

/* Summary: the normalised absolute path of a file URI (percent escapes decoded), and the file URI of a path; Nexus-allocated. */
extern char *pharos_paths_path_of_uri(const char *uri);
extern char *pharos_paths_uri_of_path(const char *path);

/* Summary: collapses "//", "/./" and "/x/../" in place. */
extern void pharos_paths_path_normalize(char *path);

extern const char *pharos_document_path_get(const PharosDocument *document);
extern const char *pharos_document_text_get(const PharosDocument *document);
extern uint_large  pharos_document_size_get(const PharosDocument *document);
extern uint32      pharos_document_kind_get(const PharosDocument *document);

/* Summary: the provider's private state for a document, released with destroy when the document is analysed again or dropped. */
extern void  pharos_document_state_set(PharosDocument *document, void *state, PharosStateDestroyCallback *destroy);
extern void *pharos_document_state_get(const PharosDocument *document);

/* Summary: reports a name while analysing; text is copied (length bytes), owner and scope may be NULL. */
extern void pharos_document_name_add(PharosDocument *document, int32 kind, const char *text, uint_large length, uint_large start, const char *owner,
                                     const char *scope, boolean declaration, int32 symbol_kind);

/* Summary: reports a problem while analysing. Severity: 1 error, 2 warning, 3 information. */
extern void pharos_document_problem_add(PharosDocument *document, uint_large start, uint_large end, int32 severity, const char *code,
                                        const char *message);

/* Summary: the number of bytes of the UTF-8 sequence a lead byte starts. */
extern uint32 pharos_documents_sequence_length_get(byte lead);

/* ---------------------------------------------------------------------------- */
/* DIAGNOSTICS                                                                  */
/* ---------------------------------------------------------------------------- */

/*
Problems found in sources, reported with spans, labels, notes and helps, and rendered the way a compiler shows them:
the offending lines with carets under the span, the labels beside them, then notes and helps.
*/

/* A byte span of one source of a set. */
typedef struct PharosSpan {
  uint32 source;
  uint32 offset;
  uint32 width;
} PharosSpan;

typedef enum PharosSeverity {
  PDS_ERROR,
  PDS_WARNING
} PharosSeverity;

/*
Summary: a source's name and text, as added to a set.
Fields:
  name         the file name or a label for diagnostics
  text         the bytes, NUL-terminated
  size         the length without the terminator
  line_starts  uint_large offsets of every line start; internal
*/
typedef struct PharosSource {
  char          *name;
  byte          *text;
  uint_large     size;
  NexusDataList *line_starts;
} PharosSource;

typedef struct PharosDiagnostics PharosDiagnostics;
typedef struct PharosDiagnostic  PharosDiagnostic;

/* Summary: an empty set of sources and diagnostics; release with pharos_diagnostics_destroy. */
extern PharosDiagnostics *pharos_diagnostics_create(void);
extern void               pharos_diagnostics_destroy(PharosDiagnostics *diagnostics);

/* Summary: adds a source (name and text copied) and returns its index, for the spans reported against it. */
extern uint32              pharos_diagnostics_source_add(PharosDiagnostics *diagnostics, const char *name, const byte *text, uint_large size);
extern const PharosSource *pharos_diagnostics_source_get(const PharosDiagnostics *diagnostics, uint32 source);

/*
Summary: reports a diagnostic with a printf-style message (at most 1023 bytes); chain label, note and help on the result.
Returns: the diagnostic, owned by the set.
*/
extern PharosDiagnostic *pharos_diagnostics_report(PharosDiagnostics *diagnostics, PharosSeverity severity, const char *code, PharosSpan span,
                                                   const char *format, ...);
extern void              pharos_diagnostics_label_add(PharosDiagnostic *diagnostic, PharosSpan span, boolean primary, const char *format, ...);
extern void              pharos_diagnostics_note_add(PharosDiagnostic *diagnostic, const char *format, ...);
extern void              pharos_diagnostics_help_add(PharosDiagnostic *diagnostic, const char *format, ...);

extern uint_large              pharos_diagnostics_count_get(const PharosDiagnostics *diagnostics);
extern const PharosDiagnostic *pharos_diagnostics_at(const PharosDiagnostics *diagnostics, uint_large index);
extern boolean                 pharos_diagnostics_has_errors(const PharosDiagnostics *diagnostics);

/* Summary: the diagnostic reported last, to add a note to; NULL when there is none. */
extern PharosDiagnostic *pharos_diagnostics_last_get(PharosDiagnostics *diagnostics);
extern void              pharos_diagnostics_clear(PharosDiagnostics *diagnostics);

extern PharosSeverity pharos_diagnostics_severity_get(const PharosDiagnostic *diagnostic);
extern const char    *pharos_diagnostics_code_get(const PharosDiagnostic *diagnostic);
extern const char    *pharos_diagnostics_message_get(const PharosDiagnostic *diagnostic);
extern const char    *pharos_diagnostics_source_name_get(const PharosDiagnostic *diagnostic);
extern uint_large     pharos_diagnostics_note_count_get(const PharosDiagnostic *diagnostic);
extern const char    *pharos_diagnostics_note_at(const PharosDiagnostic *diagnostic, uint_large index);
extern uint_large     pharos_diagnostics_help_count_get(const PharosDiagnostic *diagnostic);
extern const char    *pharos_diagnostics_help_at(const PharosDiagnostic *diagnostic, uint_large index);

/* Summary: the primary span: byte offset and width, and the 1-based line and column of its start. */
extern void pharos_diagnostics_span_get(const PharosDiagnostic *diagnostic, uint_large *out_offset, uint_large *out_width, uint_large *out_line,
                                        uint_large *out_column);

/* Summary: every diagnostic rendered, then a count line; Nexus-allocated text, with ANSI colour when asked. */
extern char *pharos_diagnostics_render(const PharosDiagnostics *diagnostics, boolean color);
