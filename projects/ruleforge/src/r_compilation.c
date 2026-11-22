#include <nexus.h>

#include <logos.h>
#include <rf_bindings.h>

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

  {
    NexusDataHashMapConfig configuration;
    NexusError            *error;

    nexus_data_hashmap_config_initialize(&configuration, NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
    error = nexus_data_hashmap_create(&configuration, &compilation->interned);

    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(error);
    }

    error = nexus_data_hashmap_create(&configuration, &compilation->strings);

    if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
      NEXUS_PROGRAM_PANIC_ERROR(error);
    }
  }

  compilation->error_count = 0;
  compilation->datasets    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

  return compilation;
}

void ruleforge_compilation_destroy(RuleforgeCompilation *compilation) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_destroy: NULL compilation");

  for (i = 0; i < nexus_data_list_length_get(compilation->modules); i++) {
    logos_language_tooling_parsing_syntax_tree_destroy(((RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, i))->tree);
  }

  nexus_data_hashmap_destroy(compilation->interned);
  nexus_data_hashmap_destroy(compilation->strings);
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

    if (module->name != NULL && nexus_strings_string_equals(module->name, name)) {
      return module;
    }
  }

  return NULL;
}

const RuleforgeDeclaration *ruleforge_compilation_declaration_find(const RuleforgeModule *module, const char *name) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(module != NULL && name != NULL, "ruleforge_compilation_declaration_find: NULL argument");

  for (i = 0; i < module->declaration_count; i++) {
    if (nexus_strings_string_equals(module->declarations[i].name, name)) {
      return &module->declarations[i];
    }
  }

  return NULL;
}

/* ---------------------------------------------------------------------------- */
/* LOADING                                                                      */
/* ---------------------------------------------------------------------------- */

/*
Summary: reads the name and the declarations of a parsed module through the generated views.
Description: the declarations are listed once, with the name and the export flag of each, so a name is found without reading the tree again.
*/
/*
Summary: checks a function declaration is whole: a body, or `intrinsic` and none; and that only the embedded library declares intrinsics.
Description: an intrinsic is bound to native code of the engine by module and name, so a script cannot be allowed to declare one: that would
  make every module able to name an engine function, and an unknown one would only fail when it was called.
*/
static void r_compilation_function_check(RuleforgeCompilation *compilation, const RuleforgeModule *module, RfFunctionStmt function) {
  boolean       intrinsic;
  boolean       has_body;
  RuleforgeSpan span;

  intrinsic = rf_function_stmt_count_token_kw_intrinsic(function) > 0;
  has_body  = rf_function_stmt_count_token_lbrace(function) > 0;
  span      = ruleforge_syntax_span_get(module, function.tree, function.node);

  if (intrinsic && !ruleforge_source_get(compilation, module->file)->embedded) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0120", span, "only the standard library embedded in the engine declares intrinsics",
                                "`intrinsic` is not allowed outside the standard library");
  } else if (intrinsic && has_body) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0122", span, "an intrinsic is implemented by the engine; remove the body",
                                "an intrinsic function has a body");
  } else if (!intrinsic && !has_body) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0121", span, "write a body, or declare it `intrinsic` in the standard library",
                                "this function has no body");
  }
}

/*
Summary: reads a `condition` declaration into the description the engine works with, and reports what is wrong with it.
Description: a condition needs the filter keyword it is written as (`keyword`) and what it accepts (`accepts`); `ordered` and `order` are
  optional. Each item may be given once, and only int and enum conditions can be ordered, because text and true/false have no order.
*/
static const RuleforgeConditionInfo *r_compilation_condition_describe(RuleforgeCompilation *compilation, const RuleforgeModule *module,
                                                                      RfConditionStmt stmt) {
  RuleforgeConditionInfo *info;
  RuleforgeSpan           span;
  boolean                 has_keyword;
  boolean                 has_accepts;
  boolean                 has_order;
  boolean                 has_ordered;
  uint32                  i;
  uint32                  j;

  info              = (RuleforgeConditionInfo *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*info));
  span              = ruleforge_syntax_span_get(module, stmt.tree, stmt.node);
  info->name        = ruleforge_syntax_text_get(compilation, stmt.tree, rf_condition_stmt_token_identifier(stmt, 0));
  info->module      = module->name;
  info->declaration = compilation->condition_serial++;
  has_keyword       = FALSE;
  has_accepts       = FALSE;
  has_order         = FALSE;
  has_ordered       = FALSE;

  for (i = 0; i < rf_condition_stmt_count_rule_condition_item(stmt); i++) {
    RfConditionItem item;

    item = rf_condition_stmt_rule_condition_item(stmt, i);

    if (rf_condition_item_count_rule_condition_keyword_item(item) > 0) {
      RfConditionKeywordItem keyword;
      const char            *raw;
      uint_large             length;

      keyword = rf_condition_item_rule_condition_keyword_item(item, 0);

      if (has_keyword) {
        ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0132", ruleforge_syntax_span_get(module, keyword.tree, keyword.node), NULL,
                                    "condition `%s` gives `keyword` twice", info->name);
      }

      /* The keyword is a string literal; its quotes are not part of it. */
      raw           = ruleforge_syntax_text_get(compilation, keyword.tree, rf_condition_keyword_item_token_string_literal(keyword, 0));
      length        = nexus_strings_string_length_get(raw);
      info->keyword = nexus_data_arena_text_copy(&compilation->arena, raw + 1, length >= 2 ? length - 2 : 0);
      has_keyword   = TRUE;
    } else if (rf_condition_item_count_rule_condition_accepts_item(item) > 0) {
      RfConditionAcceptsItem accepts_item;
      RfConditionAccepts     accepts;

      accepts_item = rf_condition_item_rule_condition_accepts_item(item, 0);
      accepts      = rf_condition_accepts_item_rule_condition_accepts(accepts_item, 0);

      if (has_accepts) {
        ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0132", ruleforge_syntax_span_get(module, accepts_item.tree, accepts_item.node), NULL,
                                    "condition `%s` gives `accepts` twice", info->name);
      }

      has_accepts = TRUE;

      if (rf_condition_accepts_count_token_kw_int(accepts) > 0) {
        info->type = RCT_INTEGER;
      } else if (rf_condition_accepts_count_token_kw_string(accepts) > 0) {
        info->type = RCT_STRING;
      } else if (rf_condition_accepts_count_token_kw_bool(accepts) > 0) {
        info->type = RCT_BOOLEAN;
      } else {
        info->type        = RCT_ENUM;
        info->value_count = rf_condition_accepts_count_token_identifier(accepts);
        info->values =
            (const char **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)info->value_count + 1U) * NEXUS_SIZEOF(const char *));

        for (j = 0; j < info->value_count; j++) {
          info->values[j] = ruleforge_syntax_text_get(compilation, accepts.tree, rf_condition_accepts_token_identifier(accepts, j));
        }
      }
    } else if (rf_condition_item_count_rule_condition_ordered_item(item) > 0) {
      has_ordered   = TRUE;
      info->ordered = TRUE;
    } else {
      RfConditionOrderItem order_item;
      const char          *digits;
      uint_large           k;

      order_item = rf_condition_item_rule_condition_order_item(item, 0);

      if (has_order) {
        ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0132", ruleforge_syntax_span_get(module, order_item.tree, order_item.node), NULL,
                                    "condition `%s` gives `order` twice", info->name);
      }

      digits      = ruleforge_syntax_text_get(compilation, order_item.tree, rf_condition_order_item_token_integer_literal(order_item, 0));
      info->order = 0;

      for (k = 0; digits[k] != '\0'; k++) {
        info->order = (info->order * 10) + (int64)(digits[k] - '0');
      }

      has_order = TRUE;
    }
  }

  if (!has_keyword) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0130", span, "write the filter keyword the condition is: `keyword \"AreaLevel\";`",
                                "condition `%s` has no `keyword`", info->name);
  }

  if (!has_accepts) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0131", span, "write what it accepts: `accepts int;`, `string`, `bool` or `enum { A, B }`",
                                "condition `%s` does not say what it `accepts`", info->name);
  }

  if (has_ordered && (info->type == RCT_STRING || info->type == RCT_BOOLEAN)) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0133", span, "only int and enum conditions have an order to compare by",
                                "condition `%s` is `ordered` but accepts text or true/false", info->name);
  }

  return info;
}

static void r_compilation_module_describe(RuleforgeCompilation *compilation, RuleforgeModule *module) {
  uint32 i;

  module->script = rf_root(module->tree);

  if (rf_script_count_rule_module_stmt(module->script) > 0) {
    module->name = ruleforge_syntax_name_get(compilation, rf_module_stmt_rule_qualified_name(rf_script_rule_module_stmt(module->script, 0), 0));
  }

  module->import_count = rf_script_count_rule_import_stmt(module->script);
  module->imports =
      (const char **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)module->import_count + 1U) * NEXUS_SIZEOF(const char *));

  for (i = 0; i < module->import_count; i++) {
    module->imports[i] = ruleforge_syntax_name_get(compilation, rf_import_stmt_rule_qualified_name(rf_script_rule_import_stmt(module->script, i), 0));
  }

  module->declaration_count = 0;
  module->declarations      = (RuleforgeDeclaration *)nexus_data_arena_allocate(
      &compilation->arena, ((uint_large)rf_script_count_rule_declaration(module->script) + 1U) * NEXUS_SIZEOF(RuleforgeDeclaration));

  for (i = 0; i < rf_script_count_rule_declaration(module->script); i++) {
    RfDeclaration                 view;
    RuleforgeDeclaration         *entry;
    const LParserGreenNode       *name;
    boolean                       exported;
    const RuleforgeConditionInfo *condition;

    view      = rf_script_rule_declaration(module->script, i);
    name      = NULL;
    exported  = FALSE;
    condition = NULL;

    if (rf_declaration_count_rule_schema_stmt(view) > 0) {
      RfSchemaStmt schema;

      schema   = rf_declaration_rule_schema_stmt(view, 0);
      name     = rf_schema_stmt_token_identifier(schema, 0);
      exported = rf_schema_stmt_count_token_kw_export(schema) > 0;
    } else if (rf_declaration_count_rule_dataset_stmt(view) > 0) {
      RfDatasetStmt dataset;

      dataset  = rf_declaration_rule_dataset_stmt(view, 0);
      name     = rf_dataset_stmt_token_identifier(dataset, 0);
      exported = rf_dataset_stmt_count_token_kw_export(dataset) > 0;
    } else if (rf_declaration_count_rule_theme_schema_stmt(view) > 0) {
      RfThemeSchemaStmt schema;

      schema   = rf_declaration_rule_theme_schema_stmt(view, 0);
      name     = rf_theme_schema_stmt_token_identifier(schema, 0);
      exported = rf_theme_schema_stmt_count_token_kw_export(schema) > 0;
    } else if (rf_declaration_count_rule_color_set_stmt(view) > 0) {
      RfColorSetStmt set;

      set      = rf_declaration_rule_color_set_stmt(view, 0);
      name     = rf_color_set_stmt_token_identifier(set, 0);
      exported = rf_color_set_stmt_count_token_kw_export(set) > 0;
    } else if (rf_declaration_count_rule_theme_stmt(view) > 0) {
      RfThemeStmt theme;

      theme    = rf_declaration_rule_theme_stmt(view, 0);
      name     = rf_theme_stmt_token_identifier(theme, 0);
      exported = rf_theme_stmt_count_token_kw_export(theme) > 0;
    } else if (rf_declaration_count_rule_template_stmt(view) > 0) {
      RfTemplateStmt template_stmt;

      template_stmt = rf_declaration_rule_template_stmt(view, 0);
      name          = rf_template_stmt_token_identifier(template_stmt, 0);
      exported      = rf_template_stmt_count_token_kw_export(template_stmt) > 0;
    } else if (rf_declaration_count_rule_function_stmt(view) > 0) {
      RfFunctionStmt function;

      function = rf_declaration_rule_function_stmt(view, 0);
      name     = rf_function_stmt_token_identifier(function, 0);
      exported = rf_function_stmt_count_token_kw_export(function) > 0;
      r_compilation_function_check(compilation, module, function);
    } else if (rf_declaration_count_rule_ruleset_stmt(view) > 0) {
      RfRulesetStmt ruleset;

      ruleset  = rf_declaration_rule_ruleset_stmt(view, 0);
      name     = rf_ruleset_stmt_token_identifier(ruleset, 0);
      exported = rf_ruleset_stmt_count_token_kw_export(ruleset) > 0;
    } else if (rf_declaration_count_rule_condition_stmt(view) > 0) {
      RfConditionStmt condition_stmt;

      condition_stmt = rf_declaration_rule_condition_stmt(view, 0);
      name           = rf_condition_stmt_token_identifier(condition_stmt, 0);
      exported       = rf_condition_stmt_count_token_kw_export(condition_stmt) > 0;
      condition      = r_compilation_condition_describe(compilation, module, condition_stmt);
    }

    /* The configuration block has no name; the entry module's run reads it from the tree. */
    if (name != NULL) {
      entry            = &module->declarations[module->declaration_count++];
      entry->name      = ruleforge_syntax_text_get(compilation, module->tree, name);
      entry->view      = view;
      entry->exported  = exported;
      entry->condition = condition;
    }
  }
}

static boolean r_compilation_source_load(RuleforgeCompilation *compilation, uint32 file, const char *folder, uint32 depth);

/* Summary: adds an embedded standard-library module (std.math is the file std/math.rf) and loads it with what it imports. */
static boolean r_compilation_library_module_load(RuleforgeCompilation *compilation, const char *module, const char *folder, uint32 depth) {
  const byte *text;
  uint_large  size;
  char        label[128];
  uint32      file;

  if (!ruleforge_stdlib_module_source_get(module, &text, &size)) {
    return FALSE;
  }

  (void)stbsp_snprintf(label, (int)NEXUS_SIZEOF(label), "std/%s.rf", module + 4);
  file = ruleforge_source_add(compilation, label, NULL, text, size, TRUE);

  return r_compilation_source_load(compilation, file, folder, depth);
}

boolean ruleforge_compilation_library_load(RuleforgeCompilation *compilation) {
  uint32  i;
  boolean ok;

  ok = TRUE;

  for (i = 0; i < ruleforge_stdlib_module_count_get(); i++) {
    if (ruleforge_compilation_module_find(compilation, ruleforge_stdlib_module_name_get(i)) == NULL &&
        !r_compilation_library_module_load(compilation, ruleforge_stdlib_module_name_get(i), ".", 0)) {
      ok = FALSE;
    }
  }

  return ok;
}

const char *ruleforge_compilation_documentation_get(RuleforgeCompilation *compilation, const RuleforgeModule *module,
                                                    const RuleforgeDeclaration *declaration) {
  const RuleforgeFile *file;
  RuleforgeSpan        span;
  uint_large           end;
  uint_large           start;

  file = ruleforge_source_get(compilation, module->file);
  span = ruleforge_syntax_span_get(module, declaration->view.tree, declaration->view.node);
  end  = span.start;

  /* Whitespace, then the closing of a block comment: the comment is the documentation of the declaration. */
  while (end > 0 && (file->text[end - 1] == ' ' || file->text[end - 1] == '\t' || file->text[end - 1] == '\n' || file->text[end - 1] == '\r')) {
    end--;
  }

  if (end < 2 || file->text[end - 1] != '/' || file->text[end - 2] != '*') {
    return "";
  }

  start = end - 2;

  while (start > 1 && !(file->text[start - 1] == '*' && file->text[start - 2] == '/')) {
    start--;
  }

  if (start <= 1) {
    return "";
  }

  /* The text between the opening and the closing marks, trimmed of surrounding blank lines. */
  {
    uint_large inner_start;
    uint_large inner_end;

    inner_start = start;
    inner_end   = end - 2;

    while (inner_start < inner_end && (file->text[inner_start] == '\n' || file->text[inner_start] == '\r' || file->text[inner_start] == ' ')) {
      inner_start++;
    }

    while (inner_end > inner_start && (file->text[inner_end - 1] == '\n' || file->text[inner_end - 1] == '\r' || file->text[inner_end - 1] == ' ')) {
      inner_end--;
    }

    return nexus_data_arena_text_copy(&compilation->arena, (const char *)file->text + inner_start, inner_end - inner_start);
  }
}

/*
Summary: parses one added source and every module it imports, depth first, so a module precedes its importers.
Description: a module already loaded (by declared name) is not parsed again; a cycle of imports is an error.
*/
static boolean r_compilation_source_load(RuleforgeCompilation *compilation, uint32 file, const char *folder, uint32 depth) {
  LParserTree     *tree;
  RuleforgeModule *module;
  uint32           i;
  boolean          ok;

  ok   = TRUE;
  tree = ruleforge_syntax_parse(compilation, file);

  if (tree == NULL) {
    return FALSE;
  }

  module       = (RuleforgeModule *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*module));
  module->file = file;
  module->tree = tree;
  r_compilation_module_describe(compilation, module);
  ruleforge_source_get(compilation, file)->module = module->name;

  if (module->name != NULL && !ruleforge_source_get(compilation, file)->embedded && nexus_strings_string_starts_with(module->name, "std.")) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0123", ruleforge_span_make(file, 0, 0), "choose another module name",
                                "module names starting with `std.` belong to the standard library");
    ok = FALSE;
  }

  /* Imports first. A module is registered only after its imports so the list is a valid compile order. */
  for (i = 0; i < module->import_count; i++) {
    const char   *import;
    char         *path;
    const byte   *library_text;
    uint_large    library_size;
    uint_large    j;
    byte         *bytes;
    uint_large    size;
    boolean       found;
    RuleforgeSpan span;

    import = module->imports[i];
    span   = ruleforge_syntax_span_get(module, tree, rf_import_stmt_rule_qualified_name(rf_script_rule_import_stmt(module->script, i), 0).node);

    if (ruleforge_compilation_module_find(compilation, import) != NULL) {
      continue;
    }

    /* The standard library is embedded in the engine: std.math is the file std/math.rf, and no file on disk can replace it. */
    if (ruleforge_stdlib_module_source_get(import, &library_text, &library_size)) {
      if (!r_compilation_library_module_load(compilation, import, folder, depth + 1)) {
        ok = FALSE;
      }

      continue;
    }

    if (depth > 64) {
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0103", span, "imports are nested too deeply", "import chain of `%s` exceeds 64 modules",
                                  import);
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

      for (cursor = import; *cursor != '\0'; cursor++) {
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
      ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0101", span, "add the folder that holds it with a search folder",
                                  "cannot find module `%s`", import);
      ok = FALSE;
    }
  }

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

    (void)nexus_strings_string_builder_append_formatted(
        &out, "{\"severity\":\"%s\",\"code\":\"%s\",\"message\":", ruleforge_diagnostic_severity_name_get(diagnostic->severity), diagnostic->code);
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

    (void)nexus_strings_string_builder_append_formatted(&out, "%s[%s]: %s\n", ruleforge_diagnostic_severity_name_get(diagnostic->severity),
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
  (void)nexus_strings_string_builder_append(&out, "{\"format\":\"ruleforge-syntax\",\"version\":1,\"files\":[");

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
    (void)nexus_json_quote_append(&out, module->name == NULL ? "" : module->name);
    (void)nexus_strings_string_builder_append_formatted(&out, ",\"embedded\":%s,\"tree\":", file->embedded ? "true" : "false");
    ruleforge_syntax_tree_json_write(module->tree, &out);
    (void)nexus_strings_string_builder_append(&out, "}");
  }

  (void)nexus_strings_string_builder_append(&out, "]}");

  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}

char *ruleforge_compilation_themes_json_write(RuleforgeCompilation *compilation) {
  NexusStringBuilder out;
  char              *result;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL, "ruleforge_compilation_themes_json_write: NULL compilation");

  out = nexus_strings_string_builder_create_growable(1024);
  ruleforge_theme_declarations_json_write(compilation, &out);
  result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}

boolean ruleforge_compilation_expression_evaluate(RuleforgeCompilation *compilation, const RuleforgeSettings *settings, const char *const *imports,
                                                  uint32 import_count, const char *expression, char **out_json) {
  NexusStringBuilder          text;
  char                        name[64];
  uint32                      file;
  uint32                      i;
  RuleforgeModule            *module;
  const RuleforgeDeclaration *declaration;
  RuleforgeSettings          *fallback;
  REval                       eval;
  RuleforgeValue              value;
  RfFunctionStmt              function;
  RfReturnStmt                result_statement;
  boolean                     ok;
  NexusStringBuilder          json;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && expression != NULL && out_json != NULL,
                             "ruleforge_compilation_expression_evaluate: NULL argument");

  *out_json = NULL;

  /* The expression is wrapped in a module of its own, so it is parsed by the language's grammar and sees exactly what a script sees. */
  (void)stbsp_snprintf(name, (int)NEXUS_SIZEOF(name), "eval.e%lu", (unsigned long)nexus_data_list_length_get(compilation->modules));
  text = nexus_strings_string_builder_create_growable(128);
  (void)nexus_strings_string_builder_append_formatted(&text, "version 1.0.0;\nmodule %s;\n", name);

  for (i = 0; i < import_count; i++) {
    (void)nexus_strings_string_builder_append_formatted(&text, "import %s;\n", imports[i]);
  }

  (void)nexus_strings_string_builder_append_formatted(&text, "export func Evaluate() -> any {\n    return %s;\n}\n", expression);
  file = ruleforge_source_add(compilation, "<expression>", NULL, (const byte *)nexus_strings_string_builder_string_get(&text),
                              nexus_strings_string_builder_length_get(&text), FALSE);
  nexus_strings_string_builder_destroy(&text);

  if (!r_compilation_source_load(compilation, file, ".", 0)) {
    return FALSE;
  }

  module      = ruleforge_compilation_module_find(compilation, name);
  declaration = module == NULL ? NULL : ruleforge_compilation_declaration_find(module, "Evaluate");

  if (declaration == NULL) {
    return FALSE;
  }

  fallback = NULL;

  if (settings == NULL) {
    fallback = ruleforge_settings_create();
    settings = fallback;
  }

  ruleforge_eval_initialize(&eval, compilation, settings);
  eval.module = module;
  function    = rf_declaration_rule_function_stmt(declaration->view, 0);
  value       = ruleforge_value_none();

  /* The one statement of the function is the `return <expression>;` written above. */
  result_statement = rf_generator_stmt_rule_return_stmt(rf_function_stmt_rule_generator_stmt(function, 0), 0);
  value            = ruleforge_eval_expression(&eval, rf_return_stmt_rule_expr(result_statement, 0));
  ok               = !eval.failed;

  ruleforge_eval_release(&eval);

  if (fallback != NULL) {
    ruleforge_settings_destroy(fallback);
  }

  if (!ok) {
    return FALSE;
  }

  json = nexus_strings_string_builder_create_growable(64);
  ruleforge_value_json_write(&value, &json);
  *out_json = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&json) + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&json), *out_json, nexus_strings_string_builder_length_get(&json) + 1);
  nexus_strings_string_builder_destroy(&json);

  return TRUE;
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

/* Summary: hashes a NUL-terminated string. */
static uint64 r_compilation_hash_string(uint64 hash, const char *text) {
  uint_large i;

  for (i = 0; text[i] != '\0'; i++) {
    hash ^= (uint64)(byte)text[i];
    hash *= 1099511628211ULL;
  }

  return hash;
}

/* Summary: hashes source text with CRLF read as LF, so a checkout with Windows line endings yields the same filter hash. */
static uint64 r_compilation_hash_text(uint64 hash, const byte *bytes, uint_large size) {
  uint_large i;

  for (i = 0; i < size; i++) {
    if (bytes[i] == (byte)'\r' && i + 1 < size && bytes[i + 1] == (byte)'\n') {
      continue;
    }

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

    /* The embedded library is part of the engine, which the header of the filter names by version; the hash is of what the user supplied. */
    if (!file->embedded) {
      hash = r_compilation_hash_text(hash, file->text, file->size);
    }
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
      hash = r_compilation_hash_string(hash, nexus_strings_string_builder_string_get(&text));
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
