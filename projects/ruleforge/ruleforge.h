/*
Summary: Ruleforge, a compiler from the .rf rule language to Path of Exile item filters, with an Echo runtime: resolved or unresolved.
Layer: application core above Nexus and Echo.
See: DESIGN.md
*/
#pragma once

#include <nexus.h>
#include <echo.h>

/* ---------------------------------------------------------------------------- */
/* RUNTIME                                                                      */
/* ---------------------------------------------------------------------------- */

#ifndef RULEFORGE_ECHO_LABEL
#  define RULEFORGE_ECHO_LABEL "Ruleforge"
#endif

/*
Summary: owned Ruleforge context borrowing its caller's Echo runtime.
Ownership: destroy before the borrowed Echo runtime.
Thread-safety: synchronize runtime lifecycle and Echo configuration.
*/
typedef struct RuleforgeRuntime RuleforgeRuntime;

/*
Summary: creates a Ruleforge runtime with its Echo system and logging context.
Params:
  echo  borrowed; must outlive Ruleforge; started by its caller when logging
  parent_system  optional borrowed registered parent in echo
Returns: the owned runtime; Echo lifecycle failures panic instead of returning.
Ownership: release with ruleforge_runtime_destroy; Ruleforge never destroys echo.
Errors: NULL echo asserts. Echo registration/context errors panic with their original diagnostics; these are configuration or
  lifecycle programming errors. Nexus allocation follows its abort-on-exhaustion contract.
Thread-safety: exclusive lifecycle access to the borrowed Echo configuration.
*/
extern RuleforgeRuntime *ruleforge_runtime_create(EchoRuntime *echo, EchoSystem *parent_system);

/*
Summary: releases Ruleforge's Echo bindings and destroys its runtime.
Params: runtime  consumed; all borrowed context users must be released first
Ownership: consumes runtime; its handle and borrowed Echo bindings become invalid. Echo remains caller-owned.
Errors: NULL runtime asserts. Echo unregistration errors panic with their original diagnostics.
Thread-safety: exclusive lifecycle access; no borrowed users may remain.
*/
extern void ruleforge_runtime_destroy(RuleforgeRuntime *runtime);

/* Summary: the registered Echo system of the runtime; borrowed, valid until ruleforge_runtime_destroy. NULL runtime asserts. */
extern EchoSystem *ruleforge_runtime_echo_system_get(RuleforgeRuntime *runtime);

/* Summary: the Echo logging context of the runtime; borrowed, valid until ruleforge_runtime_destroy. NULL runtime asserts. */
extern EchoContext *ruleforge_runtime_echo_context_get(RuleforgeRuntime *runtime);

/* ---------------------------------------------------------------------------- */
/* COMPILATION                                                                  */
/* ---------------------------------------------------------------------------- */

/* Summary: how serious a diagnostic is; an error stops a compilation from producing a filter. */
typedef enum RuleforgeDiagnosticSeverity {
  RDS_ERROR,
  RDS_WARNING,
  RDS_NOTE
} RuleforgeDiagnosticSeverity;

/*
Summary: one Ruleforge compilation: source files, diagnostics and every product, owned together.
Description: add the project's sources, parse or compile, read the products, destroy. Everything a compilation returns
  by pointer is borrowed from it and expires when it is destroyed.
Ownership: borrows its runtime, which must outlive it.
Thread-safety: exclusive access per compilation; separate compilations are independent.
*/
typedef struct RuleforgeCompilation RuleforgeCompilation;

/*
Summary: creates an empty compilation.
Params: runtime  borrowed; must outlive the compilation
Ownership: release with ruleforge_compilation_destroy.
Errors: NULL runtime asserts.
*/
extern RuleforgeCompilation *ruleforge_compilation_create(RuleforgeRuntime *runtime);

/* Summary: destroys the compilation and everything it returned. NULL compilation asserts. */
extern void ruleforge_compilation_destroy(RuleforgeCompilation *compilation);

/*
Summary: adds a folder searched for the modules an import names.
Description: the module a.b.c is looked up as a/b/c.rf below every folder, in the order the folders were added.
Params: folder  copied
Errors: NULL arguments assert.
*/
extern void ruleforge_compilation_search_folder_add(RuleforgeCompilation *compilation, const char *folder);

/*
Summary: reads and parses a .rf file into the compilation, then every module it imports.
Params: path  the file; its folder is searched for imports as well
Returns: TRUE when every file read and parsed without an error diagnostic.
Errors: NULL arguments assert. Unreadable files and syntax errors are diagnostics.
*/
extern boolean ruleforge_compilation_file_add(RuleforgeCompilation *compilation, const char *path);

/*
Summary: parses source text given directly, then the modules it imports.
Params: name  shown in diagnostics; copied. text  copied
Returns: TRUE when no error diagnostic was reported.
*/
extern boolean ruleforge_compilation_text_add(RuleforgeCompilation *compilation, const char *name, const char *text);

/* Summary: the number of diagnostics reported so far, warnings included. */
extern uint_large ruleforge_compilation_diagnostic_count_get(const RuleforgeCompilation *compilation);

/* Summary: the number of error diagnostics reported so far. */
extern uint_large ruleforge_compilation_error_count_get(const RuleforgeCompilation *compilation);

/*
Summary: the diagnostics as a JSON array of {severity, code, message, file, line, column, endLine, endColumn, note}.
Returns: a NUL-terminated string owned by the caller; release with nexus_memory_deallocate.
*/
extern char *ruleforge_compilation_diagnostics_json_write(const RuleforgeCompilation *compilation);

/*
Summary: the diagnostics as human-readable text, one block per diagnostic with the source line and a caret.
Returns: a NUL-terminated string owned by the caller; release with nexus_memory_deallocate.
*/
extern char *ruleforge_compilation_diagnostics_text_write(const RuleforgeCompilation *compilation);

/*
Summary: the syntax trees of every parsed module as one JSON document, the interchange form of the parser.
Description: {"format":"ruleforge-syntax","version":1,"files":[{"name","module","embedded","tree"}]}. The tree is written by the generated
  bindings of spec/ruleforge.gen: every node is {"rule":<name>,"start","end","children"} or {"token":<name>,"start","end","text"}, and the
  rule and token names are the grammar's own. Any consumer reads this without parsing .rf again; for interpretations (themes, styles) use
  ruleforge_compilation_themes_json_write instead of walking the tree by hand.
Returns: a NUL-terminated string owned by the caller; release with nexus_memory_deallocate.
*/
extern char *ruleforge_compilation_ast_json_write(const RuleforgeCompilation *compilation);

/*
Summary: the themes and color sets of the parsed modules (not the standard library), as the editor needs them.
Description: {"format":"ruleforge-themes","version":1,"themes":[{"name","module","file","extends","styles":[{"override","key","states":[{"state",
  "properties":[{"name","arguments":[{"kind":"color"|"reference"|"expression","start","end","text",["value","alpha"]}],["alphaOverride"]}]}]}]}],
  "colorSets":[{"name","file","colors":[{"name","value":<argument>}]}]}. A color's value is 0xRRGGBBAA; start/end are byte offsets in file, so an
  editor can rewrite the color in place.
Returns: a NUL-terminated string owned by the caller; release with nexus_memory_deallocate.
*/
extern char *ruleforge_compilation_themes_json_write(RuleforgeCompilation *compilation);

/* ---------------------------------------------------------------------------- */
/* SETTINGS                                                                     */
/* ---------------------------------------------------------------------------- */

/*
Summary: the engine's inputs besides the source: compile-time values (@name), dataset sources and limits.
Description: scripts read values as @key (nested keys as @a.b) and datasets by logical source name. Source configuration
  belongs to the engine, not to the script, so every snapshot is fixed before evaluation starts.
Ownership: owned by the caller; a compilation only borrows it while compiling.
*/
typedef struct RuleforgeSettings RuleforgeSettings;

/* Summary: creates settings with the default limits (50 million steps, 100000 rules, call depth 64). */
extern RuleforgeSettings *ruleforge_settings_create(void);

/* Summary: destroys the settings. NULL settings assert. */
extern void ruleforge_settings_destroy(RuleforgeSettings *settings);

/*
Summary: sets a compile-time value readable as @key.
Params: key  dotted, e.g. "character.level". text  true/false, none, an integer, a number, else a string; copied
*/
extern void ruleforge_settings_value_set(RuleforgeSettings *settings, const char *key, const char *text);

/*
Summary: declares where a dataset's logical source reads its snapshot from.
Params: source  the name a script's `dataset X: S from "source"` uses. provider  "csv" or "json".
  path  the snapshot file. root  JSON only: the member that holds the array, or NULL for a top-level array
*/
extern void ruleforge_settings_dataset_set(RuleforgeSettings *settings, const char *source, const char *provider, const char *path, const char *root);

/*
Summary: reads a settings file (TOML subset: [context] values, [datasets."name"] sources, [limits]).
Params: runtime  borrowed; the file is parsed with the TOML grammar through Logos. error  a buffer for the message when the
  file is unusable. error_size  its size
Returns: TRUE on success.
*/
extern boolean ruleforge_settings_toml_load(RuleforgeRuntime *runtime, RuleforgeSettings *settings, const char *path, char *error,
                                            uint_large error_size);

/* Summary: sets the output filter name (shown in the header and readable as @filter). */
extern void ruleforge_settings_filter_name_set(RuleforgeSettings *settings, const char *name);

/*
Summary: sets the evaluation budgets; zero keeps the current value.
Params: steps  evaluation steps. rules  emitted rules. depth  nested calls
*/
extern void ruleforge_settings_limits_set(RuleforgeSettings *settings, uint64 steps, uint32 rules, uint32 depth);

/* ---------------------------------------------------------------------------- */
/* COMPILING                                                                    */
/* ---------------------------------------------------------------------------- */

/*
Summary: runs the entry module's `main` and lowers the result to filter blocks.
Description: the sources must already be added. Evaluation is deterministic: the same sources, datasets and settings give the
  same filter. Errors are diagnostics; on any error no filter is produced.
Params: settings  borrowed for the call; NULL uses empty settings
Returns: TRUE when a filter was produced.
Errors: NULL compilation asserts.
*/
extern boolean ruleforge_compilation_compile(RuleforgeCompilation *compilation, const RuleforgeSettings *settings);

/*
Summary: the filter text produced by ruleforge_compilation_compile.
Returns: a string borrowed from the compilation, or NULL when no filter was produced.
*/
extern const char *ruleforge_compilation_filter_get(const RuleforgeCompilation *compilation);

/*
Summary: the structured model of the last compile as JSON: blocks with conditions, styles, source spans, traces and dataset rows.
Description: this is what the inspector and simulator read; see docs/model.md. It is produced from the same blocks the filter text is.
Returns: a NUL-terminated string owned by the caller; release with nexus_memory_deallocate. NULL when no filter was produced.
*/
extern char *ruleforge_compilation_model_json_write(const RuleforgeCompilation *compilation);

/*
Summary: runs items through the compiled filter and explains the result.
Params: items_json  a JSON array of items, or one item object: {"baseType","class","rarity","itemLevel","areaLevel","quality",
  "stackSize","sockets","linkedSockets","identified","corrupted",...}
Returns: a JSON array with, per item, the block that wins, its action and style, why every earlier block did not match, and
  the later blocks that would also match. Owned by the caller; release with nexus_memory_deallocate. NULL when no filter was
  produced or the JSON is invalid.
*/
extern char *ruleforge_compilation_simulate_json_write(const RuleforgeCompilation *compilation, const char *items_json);

/* ---------------------------------------------------------------------------- */
/* TOOLING                                                                      */
/* ---------------------------------------------------------------------------- */

/* Summary: the engine's version, e.g. "0.1.0"; static text. */
extern const char *ruleforge_version_get(void);

/*
Summary: the lexical tokens of Ruleforge source text as JSON, for editor highlighting: [[start, end, "kind"], ...].
Description: lexed by the Genesis grammar's lexer, so it works on text that does not parse yet; a byte that is no token is
  reported with kind "error". Offsets are bytes. Trivia (whitespace, comments) is included.
Params: runtime  borrowed. text  the source; size  its length in bytes
Returns: a NUL-terminated string owned by the caller; release with nexus_memory_deallocate.
Errors: NULL arguments assert.
*/
extern char *ruleforge_syntax_tokens_json_write(RuleforgeRuntime *runtime, const char *text, uint_large size);

/*
Summary: everything the engine can express, as JSON: game conditions, style properties, rarities, enumerators and the
  standard library's functions with their parameters.
Description: the editor builds completion and hover help from this, so it can never list something the compiler rejects.
Returns: a NUL-terminated string owned by the caller; release with nexus_memory_deallocate.
*/
extern char *ruleforge_manifest_json_write(void);
