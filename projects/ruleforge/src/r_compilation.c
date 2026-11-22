#include <nexus.h>
#include <echo.h>

#include <logos.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/* ---------------------------------------------------------------------------- */
/* LIFECYCLE                                                                    */
/* ---------------------------------------------------------------------------- */

RuleforgeCompilation *ruleforge_compilation_create(RuleforgeRuntime *runtime) {
  RuleforgeCompilation *compilation;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "ruleforge_compilation_create: NULL runtime");

  compilation          = (RuleforgeCompilation *)nexus_memory_allocate(NEXUS_SIZEOF(*compilation));
  compilation->runtime = runtime;
  nexus_data_arena_initialize(&compilation->arena);
  compilation->files       = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  compilation->diagnostics = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  compilation->search      = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  compilation->modules     = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  compilation->error_count = 0;
  compilation->datasets    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

  return compilation;
}

void ruleforge_compilation_destroy(RuleforgeCompilation *compilation) {
  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_destroy: NULL compilation");

  nexus_data_list_destroy(compilation->files);
  nexus_data_list_destroy(compilation->diagnostics);
  nexus_data_list_destroy(compilation->search);
  nexus_data_list_destroy(compilation->modules);
  nexus_data_list_destroy(compilation->datasets);
  nexus_data_arena_release(&compilation->arena);
  nexus_memory_deallocate(compilation);
}

void ruleforge_compilation_search_folder_add(RuleforgeCompilation *compilation, const char *folder) {
  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && folder != NULL, "ruleforge_compilation_search_folder_add: NULL argument");

  (void)nexus_data_list_append_pointer(compilation->search,
                                       nexus_data_arena_text_copy(&compilation->arena, folder, nexus_strings_string_length_get(folder)));
}

RuleforgeModule *ruleforge_compilation_module_find(const RuleforgeCompilation *compilation, const char *name) {
  uint_large       i;
  RuleforgeModule *module;

  for (i = 0; i < nexus_data_list_length_get(compilation->modules); i++) {
    module = (RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, i);

    if (module->ast != NULL && module->ast->text != NULL && nexus_strings_string_equals(module->ast->text, name)) {
      return module;
    }
  }

  return NULL;
}

/* ---------------------------------------------------------------------------- */
/* LOADING                                                                      */
/* ---------------------------------------------------------------------------- */

/*
Summary: parses one added source and every module it imports, depth first, so a module precedes its importers.
Description: a module already loaded (by declared name) is not parsed again; a cycle of imports is an error.
*/
static boolean r_compilation_source_load(RuleforgeCompilation *compilation, uint32 file, const char *folder, uint32 depth) {
  LParserTree     *tree;
  RuleforgeNode   *ast;
  RuleforgeModule *module;
  uint32           i;
  boolean          ok;

  ok   = TRUE;
  tree = ruleforge_syntax_parse(compilation, file);

  if (tree == NULL) {
    return FALSE;
  }

  ast = ruleforge_ast_build(compilation, file, tree);
  logos_language_tooling_parsing_syntax_tree_destroy(tree);

  ruleforge_source_get(compilation, file)->module = ast->text;

  /* Imports first. A module is registered only after its imports so the list is a valid compile order. */
  for (i = 0; i < ast->child_count; i++) {
    const RuleforgeNode *import;
    char                *path;
    uint_large           j;
    byte                *bytes;
    uint_large           size;
    boolean              found;

    import = ast->children[i];

    if (import->kind != RNK_IMPORT) {
      continue;
    }

    if (ruleforge_compilation_module_find(compilation, import->text) != NULL || ruleforge_stdlib_module_exists(import->text)) {
      continue;
    }

    if (depth > 64) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0103", import->span, "imports are nested too deeply",
                                  "import chain of `%s` exceeds 64 modules", import->text);
      ok = FALSE;

      continue;
    }

    found = FALSE;

    /* a.b.c -> a/b/c.rf below the importing file's folder, then every search folder. */
    for (j = 0; !found && j <= nexus_data_list_length_get(compilation->search); j++) {
      const char        *base;
      NexusStringBuilder candidate;
      const char        *cursor;

      base      = j == 0 ? folder : (const char *)nexus_data_list_pointer_at(compilation->search, j - 1);
      candidate = nexus_strings_string_builder_create_growable(64);
      (void)nexus_strings_string_builder_append(&candidate, base);

      if (nexus_strings_string_length_get(base) > 0) {
        (void)nexus_strings_string_builder_append(&candidate, "/");
      }

      for (cursor = import->text; *cursor != '\0'; cursor++) {
        if (*cursor == '.') {
          (void)nexus_strings_string_builder_append(&candidate, "/");
        } else {
          char one[2];

          one[0] = *cursor;
          one[1] = '\0';
          (void)nexus_strings_string_builder_append(&candidate, one);
        }
      }

      (void)nexus_strings_string_builder_append(&candidate, ".rf");
      path = nexus_data_arena_text_copy(&compilation->arena, nexus_strings_string_builder_string_get(&candidate),
                                        nexus_strings_string_builder_length_get(&candidate));
      nexus_strings_string_builder_destroy(&candidate);

      if (ruleforge_source_file_read(path, &bytes, &size)) {
        uint32 imported;

        found    = TRUE;
        imported = ruleforge_source_add(compilation, path, NULL, bytes, size, FALSE);
        nexus_memory_deallocate(bytes);

        if (!r_compilation_source_load(compilation, imported, folder, depth + 1)) {
          ok = FALSE;
        }
      }
    }

    if (!found) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0101", import->span, "add the folder that holds it with a search folder",
                                  "cannot find module `%s`", import->text);
      ok = FALSE;
    }
  }

  module       = (RuleforgeModule *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*module));
  module->file = file;
  module->ast  = ast;
  (void)nexus_data_list_append_pointer(compilation->modules, module);

  return ok && compilation->error_count == 0;
}

boolean ruleforge_compilation_file_add(RuleforgeCompilation *compilation, const char *path) {
  byte      *bytes;
  uint_large size;
  uint32     file;
  char      *folder;
  uint_large length;
  uint_large i;
  boolean    ok;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && path != NULL, "ruleforge_compilation_file_add: NULL argument");

  if (!ruleforge_source_file_read(path, &bytes, &size)) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0100", ruleforge_span_make(0, 0, 0), NULL, "cannot read `%s`", path);

    return FALSE;
  }

  file = ruleforge_source_add(compilation, path, NULL, bytes, size, FALSE);
  nexus_memory_deallocate(bytes);

  length = nexus_strings_string_length_get(path);
  folder = nexus_data_arena_text_copy(&compilation->arena, path, length);

  for (i = length; i > 0; i--) {
    if (folder[i - 1] == '/' || folder[i - 1] == '\\') {
      break;
    }
  }

  /* The folder of the file, or "." for a bare file name. */
  if (i == 0) {
    folder[0] = '.';
    folder[1] = '\0';
  } else {
    folder[i - 1] = '\0';
  }

  ok = r_compilation_source_load(compilation, file, folder, 0);

  return ok;
}

boolean ruleforge_compilation_text_add(RuleforgeCompilation *compilation, const char *name, const char *text) {
  uint32 file;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && name != NULL && text != NULL, "ruleforge_compilation_text_add: NULL argument");

  file = ruleforge_source_add(compilation, name, NULL, (const byte *)text, nexus_strings_string_length_get(text), FALSE);

  return r_compilation_source_load(compilation, file, ".", 0);
}

/* ---------------------------------------------------------------------------- */
/* PRODUCTS                                                                     */
/* ---------------------------------------------------------------------------- */

uint_large ruleforge_compilation_diagnostic_count_get(const RuleforgeCompilation *compilation) {
  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_diagnostic_count_get: NULL compilation");

  return nexus_data_list_length_get(compilation->diagnostics);
}

uint_large ruleforge_compilation_error_count_get(const RuleforgeCompilation *compilation) {
  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_error_count_get: NULL compilation");

  return compilation->error_count;
}

char *ruleforge_compilation_diagnostics_json_write(const RuleforgeCompilation *compilation) {
  NexusStringBuilder out;
  char              *result;
  uint_large         i;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_diagnostics_json_write: NULL compilation");

  out = nexus_strings_string_builder_create_growable(256);
  (void)nexus_strings_string_builder_append(&out, "[");

  for (i = 0; i < nexus_data_list_length_get(compilation->diagnostics); i++) {
    const RuleforgeDiagnostic *diagnostic;
    const RuleforgeFile       *file;
    uint32                     line;
    uint32                     column;
    uint32                     end_line;
    uint32                     end_column;

    diagnostic = (const RuleforgeDiagnostic *)nexus_data_list_pointer_at(compilation->diagnostics, i);
    file       = nexus_data_list_length_get(compilation->files) > 0 ? ruleforge_source_get(compilation, diagnostic->span.file) : NULL;
    line       = 0;
    column     = 0;
    end_line   = 0;
    end_column = 0;

    if (file != NULL) {
      ruleforge_text_position_get(file, diagnostic->span.start, &line, &column);
      ruleforge_text_position_get(file, diagnostic->span.end, &end_line, &end_column);
    }

    if (i > 0) {
      (void)nexus_strings_string_builder_append(&out, ",");
    }

    (void)nexus_strings_string_builder_append_formatted(&out, "{\"severity\":\"%s\",\"code\":\"%s\",\"message\":",
                                                        diagnostic->severity == RDS_ERROR     ? "error"
                                                        : diagnostic->severity == RDS_WARNING ? "warning"
                                                                                              : "note",
                                                        diagnostic->code);
    (void)nexus_json_quote_append(&out, diagnostic->message);
    (void)nexus_strings_string_builder_append(&out, ",\"file\":");
    (void)nexus_json_quote_append(&out, file != NULL ? file->name : "");
    (void)nexus_strings_string_builder_append_formatted(&out, ",\"line\":%lu,\"column\":%lu,\"endLine\":%lu,\"endColumn\":%lu", (unsigned long)line,
                                                        (unsigned long)column, (unsigned long)end_line, (unsigned long)end_column);

    if (diagnostic->note != NULL) {
      (void)nexus_strings_string_builder_append(&out, ",\"note\":");
      (void)nexus_json_quote_append(&out, diagnostic->note);
    }

    (void)nexus_strings_string_builder_append(&out, "}");
  }

  (void)nexus_strings_string_builder_append(&out, "]");

  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}

char *ruleforge_compilation_diagnostics_text_write(const RuleforgeCompilation *compilation) {
  NexusStringBuilder out;
  char              *result;
  uint_large         i;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_diagnostics_text_write: NULL compilation");

  out = nexus_strings_string_builder_create_growable(256);

  for (i = 0; i < nexus_data_list_length_get(compilation->diagnostics); i++) {
    const RuleforgeDiagnostic *diagnostic;
    const RuleforgeFile       *file;
    uint32                     line;
    uint32                     column;
    uint32                     end_line;
    uint32                     end_column;
    uint32                     width;
    uint32                     c;
    uint_large                 line_start;
    uint_large                 line_end;

    diagnostic = (const RuleforgeDiagnostic *)nexus_data_list_pointer_at(compilation->diagnostics, i);
    file       = nexus_data_list_length_get(compilation->files) > 0 ? ruleforge_source_get(compilation, diagnostic->span.file) : NULL;

    (void)nexus_strings_string_builder_append_formatted(&out, "%s[%s]: %s\n",
                                                        diagnostic->severity == RDS_ERROR     ? "error"
                                                        : diagnostic->severity == RDS_WARNING ? "warning"
                                                                                              : "note",
                                                        diagnostic->code, diagnostic->message);

    if (file != NULL) {
      ruleforge_text_position_get(file, diagnostic->span.start, &line, &column);
      ruleforge_text_position_get(file, diagnostic->span.end, &end_line, &end_column);
      (void)nexus_strings_string_builder_append_formatted(&out, "  --> %s:%lu:%lu\n", file->name, (unsigned long)line, (unsigned long)column);

      line_start = file->line_starts[line - 1];
      line_end   = line_start;

      while (line_end < file->size && file->text[line_end] != '\n' && file->text[line_end] != '\r') {
        line_end++;
      }

      (void)nexus_strings_string_builder_append_formatted(&out, "   | %.*s\n   | ", (int)(line_end - line_start),
                                                          (const char *)file->text + line_start);

      width = end_line == line && end_column > column ? end_column - column : 1;

      for (c = 1; c < column; c++) {
        (void)nexus_strings_string_builder_append(&out, " ");
      }

      for (c = 0; c < width; c++) {
        (void)nexus_strings_string_builder_append(&out, "^");
      }

      (void)nexus_strings_string_builder_append(&out, "\n");
    }

    if (diagnostic->note != NULL) {
      (void)nexus_strings_string_builder_append_formatted(&out, "  = note: %s\n", diagnostic->note);
    }
  }

  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}

char *ruleforge_compilation_ast_json_write(const RuleforgeCompilation *compilation) {
  NexusStringBuilder out;
  char              *result;
  uint_large         i;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_ast_json_write: NULL compilation");

  out = nexus_strings_string_builder_create_growable(1024);
  (void)nexus_strings_string_builder_append(&out, "{\"format\":\"ruleforge-ast\",\"version\":1,\"files\":[");

  for (i = 0; i < nexus_data_list_length_get(compilation->modules); i++) {
    const RuleforgeModule *module;
    const RuleforgeFile   *file;

    module = (const RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, i);
    file   = ruleforge_source_get(compilation, module->file);

    if (i > 0) {
      (void)nexus_strings_string_builder_append(&out, ",");
    }

    (void)nexus_strings_string_builder_append(&out, "{\"name\":");
    (void)nexus_json_quote_append(&out, file->name);
    (void)nexus_strings_string_builder_append(&out, ",\"module\":");
    (void)nexus_json_quote_append(&out, module->ast->text == NULL ? "" : module->ast->text);
    (void)nexus_strings_string_builder_append_formatted(&out, ",\"embedded\":%s,\"ast\":", file->embedded ? "true" : "false");
    ruleforge_ast_json_write(compilation, module->ast, &out);
    (void)nexus_strings_string_builder_append(&out, "}");
  }

  (void)nexus_strings_string_builder_append(&out, "]}");

  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}

/* ---------------------------------------------------------------------------- */
/* COMPILING                                                                    */
/* ---------------------------------------------------------------------------- */

static uint64 r_compilation_hash_bytes(uint64 hash, const byte *bytes, uint_large size) {
  uint_large i;

  for (i = 0; i < size; i++) {
    hash ^= (uint64)bytes[i];
    hash *= 1099511628211ULL;
  }

  return hash;
}

uint64 ruleforge_compilation_hash_get(const RuleforgeCompilation *compilation) {
  uint64     hash;
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_hash_get: NULL compilation");

  hash = 14695981039346656037ULL;

  /* Everything that can change the filter: the sources in module order, the dataset snapshots and the settings. */
  for (i = 0; i < nexus_data_list_length_get(compilation->modules); i++) {
    const RuleforgeFile *file;

    file = ruleforge_source_get(compilation, ((const RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, i))->file);
    hash = r_compilation_hash_bytes(hash, file->text, file->size);
  }

  for (i = 0; i < nexus_data_list_length_get(compilation->datasets); i++) {
    uint64 dataset_hash;

    dataset_hash = ((const RuleforgeDataset *)nexus_data_list_pointer_at(compilation->datasets, i))->hash;
    hash         = r_compilation_hash_bytes(hash, (const byte *)&dataset_hash, NEXUS_SIZEOF(dataset_hash));
  }

  if (compilation->settings != NULL) {
    for (i = 0; i < nexus_data_list_length_get(compilation->settings->values); i++) {
      const RuleforgeSettingValue *entry;
      NexusStringBuilder           text;

      entry = (const RuleforgeSettingValue *)nexus_data_list_pointer_at(compilation->settings->values, i);
      text  = nexus_strings_string_builder_create_growable(16);
      (void)nexus_strings_string_builder_append_formatted(&text, "%s=", entry->key);
      ruleforge_value_json_write(&entry->value, &text);
      hash = r_compilation_hash_bytes(hash, (const byte *)nexus_strings_string_builder_string_get(&text),
                                      nexus_strings_string_builder_length_get(&text));
      nexus_strings_string_builder_destroy(&text);
    }
  }

  return hash;
}

boolean ruleforge_compilation_compile(RuleforgeCompilation *compilation, const RuleforgeSettings *settings) {
  REval              eval;
  RuleforgeSettings *fallback;
  RuleforgeRuleset  *result;
  boolean            ok;
  uint_large         i;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_compile: NULL compilation");

  fallback = NULL;

  if (compilation->error_count > 0) {
    return FALSE;
  }

  if (settings == NULL) {
    fallback = ruleforge_settings_create();
    settings = fallback;
  }

  compilation->settings    = settings;
  compilation->result      = NULL;
  compilation->filter      = NULL;
  compilation->filter_text = NULL;

  ruleforge_eval_initialize(&eval, compilation, settings);
  result = NULL;
  ok     = ruleforge_eval_main_run(&eval, &result);

  for (i = 0; i < nexus_data_list_length_get(eval.datasets); i++) {
    (void)nexus_data_list_append_pointer(compilation->datasets, nexus_data_list_pointer_at(eval.datasets, i));
  }

  ruleforge_eval_release(&eval);

  if (ok && compilation->error_count == 0) {
    compilation->result      = result;
    compilation->filter      = ruleforge_filter_lower(compilation, result);
    compilation->filter_hash = ruleforge_compilation_hash_get(compilation);
    compilation->filter_text = ruleforge_filter_text_write(compilation, compilation->filter, settings->filter, compilation->filter_hash);
  } else {
    ok = FALSE;
  }

  /* The settings are only borrowed for the call: nothing the products point at may live in them. */
  compilation->settings = NULL;

  if (fallback != NULL) {
    ruleforge_settings_destroy(fallback);
  }

  return ok;
}

const char *ruleforge_compilation_filter_get(const RuleforgeCompilation *compilation) {
  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_filter_get: NULL compilation");

  return compilation->filter_text;
}

char *ruleforge_compilation_model_json_write(const RuleforgeCompilation *compilation) {
  NexusStringBuilder out;
  char              *result;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_model_json_write: NULL compilation");

  if (compilation->filter == NULL) {
    return NULL;
  }

  out = nexus_strings_string_builder_create_growable(4096);
  ruleforge_filter_model_write(compilation, &out);
  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}

char *ruleforge_compilation_simulate_json_write(const RuleforgeCompilation *compilation, const char *items_json) {
  NexusStringBuilder out;
  char              *result;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && items_json != NULL, "ruleforge_compilation_simulate_json_write: NULL argument");

  if (compilation->filter == NULL) {
    return NULL;
  }

  out = nexus_strings_string_builder_create_growable(1024);

  if (!ruleforge_simulate_json_write(compilation, items_json, &out)) {
    nexus_strings_string_builder_destroy(&out);

    return NULL;
  }

  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}
