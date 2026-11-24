#pragma once

#include <nexus.h>
#include <echo.h>

#include <logos.h>
#include <pharos.h>

/*
The genesis command line tool: compiles Genesis grammars and turns them into other artifacts. Every artifact is a pure
function of the grammar, its mapping file and the tool itself, and is only written when its text changed.
*/

/* ---------------------------------------------------------------------------- */
/* FILES                                                                         */
/* ---------------------------------------------------------------------------- */

/* Summary: reads a whole file into Nexus-allocated memory with a NUL terminator; FALSE when it cannot be read. */
extern boolean logos_tool_files_read(const char *path, byte **out_bytes, uint_large *out_size);

/*
Summary: writes a file only when its content differs, creating missing parent directories.
Returns: FALSE when the file cannot be written; *out_changed tells whether it was (re)written.
*/
extern boolean logos_tool_files_write_if_changed(const char *path, const char *text, uint_large size, boolean *out_changed);

/* Summary: path joined with a name by a single slash, unless name is absolute (then name alone); Nexus-allocated. */
extern char *logos_tool_files_join(const char *directory, const char *name);

/* Summary: the directory part of a path ("." when there is none); Nexus-allocated. */
extern char *logos_tool_files_directory_of(const char *path);

/* Summary: the file name without folder and extension; Nexus-allocated. */
extern char *logos_tool_files_stem_of(const char *path);

/* ---------------------------------------------------------------------------- */
/* TEXT                                                                          */
/* ---------------------------------------------------------------------------- */

/* A growing text buffer for generators, over a Nexus string builder. */
typedef struct LToolText {
  NexusStringBuilder builder;
} LToolText;

extern void logos_tool_text_initialize(LToolText *text);
extern void logos_tool_text_add(LToolText *text, const char *value);
/* Summary: appends printf-style text; an allocation failure is fatal, as everywhere in Nexus. */
#define logos_tool_text_format(text, ...) logos_tool_text_check(nexus_strings_string_builder_append_formatted(&(text)->builder, __VA_ARGS__))
extern void        logos_tool_text_check(NexusError *error);
extern const char *logos_tool_text_data_get(const LToolText *text); /* NUL-terminated. */
extern uint_large  logos_tool_text_length_get(const LToolText *text);
extern void        logos_tool_text_release(LToolText *text);

/* Summary: the first byte equal to value in text, or NULL. */
extern const char *logos_tool_string_find_byte(const char *text, char value);

/* Summary: the last byte equal to value in text, or NULL. */
extern const char *logos_tool_string_find_last_byte(const char *text, char value);

/* Summary: the first byte equal to value among the first size bytes, or NULL. */
extern const char *logos_tool_bytes_find(const char *bytes, char value, uint_large size);

/* Summary: the text with every letter lower-cased (to_lower) or upper-cased, and non-alphanumerics replaced by '_'; Nexus-allocated. */
extern char *logos_tool_identifier_lower(const char *name);
extern char *logos_tool_identifier_upper(const char *name);

/* Summary: CamelCase of a snake_case or lower-case name: "json_value" -> "JsonValue"; Nexus-allocated. */
extern char *logos_tool_identifier_camel(const char *name);

/* ---------------------------------------------------------------------------- */
/* MAPPING                                                                       */
/* ---------------------------------------------------------------------------- */

/*
A mapping file tells the tools what a grammar cannot: which editor scopes the tokens get, which file extensions belong to
the language, where generated files go, and which rules declare and reference symbols. Lines are "key = value" settings,
"$token = scope" scope assignments, "context NAME = scope" context scopes, "rule #name = scope" scopes of delimited rules, and "symbol" and "reference" declarations;
a line starting with "//" is a comment.
*/
typedef struct LToolMappingEntry {
  char *key;
  char *value;
  char *extra; /* The remainder of a declaration line. */
} LToolMappingEntry;

typedef struct LToolMapping {
  char          *path;
  NexusDataList *settings;   /* LToolMappingEntry: key = value */
  NexusDataList *scopes;     /* LToolMappingEntry: token name (no $) = scope */
  NexusDataList *contexts;   /* LToolMappingEntry: context name = scope */
  NexusDataList *symbols;    /* LToolMappingEntry: key = rule name, value = name token, extra = kind */
  NexusDataList *references; /* LToolMappingEntry: key = token name */
} LToolMapping;

extern void logos_tool_mapping_initialize(LToolMapping *mapping);
extern void logos_tool_mapping_release(LToolMapping *mapping);

/* Summary: parses mapping text; returns FALSE and fills error with "path:line: message" on a malformed line. */
extern boolean logos_tool_mapping_parse(LToolMapping *mapping, const char *path, const char *text, char *error, uint_large error_size);

/* Summary: reads and parses a mapping file. A missing file is not an error: the mapping stays empty and *out_found is FALSE. */
extern boolean logos_tool_mapping_load(LToolMapping *mapping, const char *path, boolean *out_found, char *error, uint_large error_size);

extern const char *logos_tool_mapping_setting_get(const LToolMapping *mapping, const char *key);
extern const char *logos_tool_mapping_scope_get(const LToolMapping *mapping, const char *token);
extern const char *logos_tool_mapping_context_scope_get(const LToolMapping *mapping, const char *context);

/* ---------------------------------------------------------------------------- */
/* COMMON                                                                        */
/* ---------------------------------------------------------------------------- */

typedef struct LToolOptions {
  LogosRuntime  *runtime;
  PharosRuntime *pharos;
  NexusDataList *folders;        /* char *, search folders in order */
  const char    *grammar;        /* The grammar file. */
  const char    *mapping;        /* The mapping file, or NULL for "<grammar stem>.mapping" next to the grammar. */
  const char    *output;         /* An output file or directory, command specific. */
  const char    *tests;          /* A test output directory. */
  const char    *prefix;         /* An identifier prefix override. */
  const char    *builtin;        /* The name of a language the tool knows by heart: genesis or mapping. */
  uint32         iterations;     /* How often `bench` repeats a measurement. */
  const char    *install;        /* A directory the generated Sublime syntax is also written to (your Packages/User). */
  NexusDataList *files;          /* char *: further positional arguments, such as the files `check` parses. */
  const char    *mapping_output; /* Where `export --builtin` writes the editor mapping that styles it. */
  const char    *config;         /* The configuration file of --config, or NULL to look for genesis.toml. */
  boolean        quiet;
} LToolOptions;

/* ---------------------------------------------------------------------------- */
/* CONFIGURATION                                                                 */
/* ---------------------------------------------------------------------------- */

/*
genesis.toml in the root of a project (or the file of --config) holds the settings of the tool, in TOML. A key is "table.key"
(for example "lsp.cache"); a missing key leaves the caller's default. The file is read with the Genesis TOML grammar.
*/
typedef struct LToolConfig {
  char          *path;   /* The file that was read, or NULL when there is none. */
  NexusDataList *keys;   /* char *, "table.key" */
  NexusDataList *values; /* char *, the text of the value as written */
} LToolConfig;

extern void logos_tool_config_initialize(LToolConfig *config);
extern void logos_tool_config_release(LToolConfig *config);

/* Summary: parses TOML text into the configuration; FALSE with a message in error when it is not valid TOML. */
extern boolean logos_tool_config_parse(LToolConfig *config, LogosRuntime *runtime, const char *path, const byte *text, uint_large size, char *error,
                                       uint_large error_size);

/*
Summary: reads the file of --config, or else genesis.toml in root or the nearest folder above it. No file is not an error (the
  configuration stays empty); a file that cannot be read or is not TOML is, with the message in error.
*/
extern boolean logos_tool_config_load(LToolConfig *config, const LToolOptions *options, const char *root, char *error, uint_large error_size);

extern const char *logos_tool_config_raw_get(const LToolConfig *config, const char *key);
extern boolean     logos_tool_config_boolean_get(const LToolConfig *config, const char *key, boolean fallback);
extern int64       logos_tool_config_integer_get(const LToolConfig *config, const char *key, int64 fallback);

/* Summary: a string value (Nexus-allocated), or NULL when the key is missing. */
extern char *logos_tool_config_string_get(const LToolConfig *config, const char *key);

/* Summary: the strings of an array value, in a Nexus-allocated array to release with logos_tool_config_list_release; 0 when missing. */
extern uint32 logos_tool_config_list_get(const LToolConfig *config, const char *key, char ***out_items);
extern void   logos_tool_config_list_release(char **items, uint32 count);

/* Summary: `genesis config`: prints the configuration that applies here (the file found, and every key). */
extern int logos_tool_config_run(const LToolOptions *options);

/* Summary: compiles the grammar of the options; prints the diagnostics and returns NULL on errors. The compiler is returned for later release. */
extern LGenesisEngine *logos_tool_grammar_compile(const LToolOptions *options, LGenesisCompiler **out_compiler);

/* Summary: loads the mapping of the options (explicit, or next to the grammar). Errors are printed and FALSE is returned. */
extern boolean logos_tool_mapping_for(const LToolOptions *options, LToolMapping *mapping);

/* Summary: appends a JSON string literal for UTF-8 text (the language server protocol is JSON). */
#define logos_tool_json_quote(text, value) logos_tool_text_check(nexus_json_quote_append(&(text)->builder, value))

/* ---------------------------------------------------------------------------- */
/* IN-HOUSE SPECS                                                                */
/* ---------------------------------------------------------------------------- */

/*
The statement forms of a mapping file. The mapping reader dispatches on this table, and the grammar of mapping files is
written from it with the .gen writer, so a form cannot be added to one and not the other.
*/
typedef enum LToolMappingFormId {
  LTOOL_FORM_CONTEXT,
  LTOOL_FORM_RULE,
  LTOOL_FORM_SYMBOL,
  LTOOL_FORM_REFERENCE
} LToolMappingFormId;

typedef struct LToolMappingForm {
  LToolMappingFormId id;
  const char        *keyword;      /* The word that opens a line: "context". */
  const char        *line_context; /* The lexical context the rest of the line is read in. */
} LToolMappingForm;

extern uint32                  logos_tool_mapping_form_count_get(void);
extern const LToolMappingForm *logos_tool_mapping_form_at(uint32 index);

/* Summary: the form a line starts with, or NULL when the line starts a setting or a token scope. */
extern const LToolMappingForm *logos_tool_mapping_form_find(const char *text, uint_large length);

/* Summary: the language of mapping files as a language definition, built from the form table. */
extern LGenesisDefinition *logos_tool_spec_mapping_definition_create(void);

/* Summary: the mapping files (scopes for editors) of the two built-in grammars, from the same tables. */
extern void logos_tool_spec_genesis_mapping_write(LToolText *out);
extern void logos_tool_spec_mapping_mapping_write(LToolText *out);

extern int logos_tool_export_run(const LToolOptions *options);
extern int logos_tool_check_run(const LToolOptions *options);
extern int logos_tool_bench_run(const LToolOptions *options);
extern int logos_tool_import_run(const LToolOptions *options);
extern int logos_tool_bind_run(const LToolOptions *options);
extern int logos_tool_sublime_run(const LToolOptions *options);
extern int logos_tool_lsp_run(int argument_count, char **arguments, const LToolOptions *options);
