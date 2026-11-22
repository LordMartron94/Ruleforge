#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
The evaluator runs Ruleforge's compile-time code: expressions, statements, functions, templates, rulesets and loops over
datasets. It is a tree walker over the AST under explicit budgets (steps, call depth, emitted rules) and records, for every
generated rule, where it came from. Errors are diagnostics; the first one marks the evaluation failed and unwinds it.
*/

static boolean        r_eval_block(REval *eval, const RuleforgeNode *block);
static RuleforgeValue r_eval_call(REval *eval, const RuleforgeNode *call);

/* ---------------------------------------------------------------------------- */
/* LIFECYCLE AND ERRORS                                                         */
/* ---------------------------------------------------------------------------- */

void ruleforge_eval_initialize(REval *eval, RuleforgeCompilation *compilation, const RuleforgeSettings *settings) {
  NEXUS_PROGRAM_PRECONDITION(eval != NULL && compilation != NULL, "ruleforge_eval_initialize: NULL argument");

  nexus_memory_bytes_set(eval, NEXUS_SIZEOF(*eval), 0);
  eval->compilation  = compilation;
  eval->settings     = settings;
  eval->variables    = nexus_data_list_create(NEXUS_SIZEOF(RuleforgeVariable), 64);
  eval->cores        = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  eval->datasets     = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  eval->cache_keys   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  eval->cache_values = nexus_data_list_create(NEXUS_SIZEOF(RuleforgeValue), 16);
  eval->result       = ruleforge_value_none();
}

void ruleforge_eval_release(REval *eval) {
  NEXUS_PROGRAM_PRECONDITION(eval != NULL, "ruleforge_eval_release: NULL eval");

  nexus_data_list_destroy(eval->variables);
  nexus_data_list_destroy(eval->cores);
  nexus_data_list_destroy(eval->datasets);
  nexus_data_list_destroy(eval->cache_keys);
  nexus_data_list_destroy(eval->cache_values);
}

void ruleforge_eval_fail(REval *eval, const char *code, RuleforgeSpan span, const char *note, const char *format, ...) {
  char    message[1024];
  va_list arguments;

  va_start(arguments, format);
  (void)stbsp_vsnprintf(message, (int)NEXUS_SIZEOF(message), format, arguments);
  va_end(arguments);

  /* Only the first error of an evaluation is reported: the rest would be consequences of it. */
  if (!eval->failed) {
    ruleforge_diagnostic_report(eval->compilation, RDS_ERROR, code, span, note, "%s", message);
  }

  eval->failed = TRUE;
}

/* Summary: counts one evaluation step against the budget; FALSE (and a failed evaluation) when it is spent. */
static boolean r_eval_step(REval *eval, RuleforgeSpan span) {
  eval->steps++;

  if (eval->settings != NULL && eval->steps > eval->settings->step_limit) {
    ruleforge_eval_fail(eval, "RF0390", span, "raise [limits] steps in the settings if the script is meant to be this large",
                        "the evaluation budget of %lu steps is exhausted", (unsigned long)eval->settings->step_limit);
  }

  return !eval->failed;
}

const RuleforgeProvenance *ruleforge_eval_provenance_push(REval *eval, RuleforgeProvenanceKind kind, RuleforgeSpan span, const char *label,
                                                          RuleforgeRecord *record) {
  RuleforgeProvenance *provenance;

  provenance         = (RuleforgeProvenance *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*provenance));
  provenance->kind   = kind;
  provenance->span   = span;
  provenance->label  = label;
  provenance->record = record;
  provenance->parent = eval->provenance;

  return provenance;
}

RuleforgeCondition *ruleforge_eval_condition_create(REval *eval, const RuleforgeConditionInfo *info, RuleforgeOperator op, uint32 count) {
  RuleforgeCondition *condition;

  condition          = (RuleforgeCondition *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*condition));
  condition->info    = info;
  condition->op      = op;
  condition->count   = count;
  condition->numbers = (int64 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)count + 1U) * NEXUS_SIZEOF(int64));
  condition->strings = (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)count + 1U) * NEXUS_SIZEOF(const char *));

  return condition;
}

/* ---------------------------------------------------------------------------- */
/* VARIABLES                                                                    */
/* ---------------------------------------------------------------------------- */

boolean ruleforge_eval_variable_find(REval *eval, const char *name, boolean parameter, RuleforgeValue *out_value) {
  uint_large         i;
  RuleforgeVariable *variable;

  for (i = nexus_data_list_length_get(eval->variables); i > eval->frame_base; i--) {
    variable = (RuleforgeVariable *)nexus_data_list_at(eval->variables, i - 1);

    if (variable->parameter == parameter && nexus_strings_string_equals(variable->name, name)) {
      *out_value = variable->value;

      return TRUE;
    }
  }

  return FALSE;
}

static void r_eval_variable_push(REval *eval, const char *name, RuleforgeValue value, boolean mutable_flag, boolean parameter) {
  RuleforgeVariable variable;

  variable.name         = name;
  variable.value        = value;
  variable.mutable_flag = mutable_flag;
  variable.parameter    = parameter;
  (void)nexus_data_list_append(eval->variables, &variable);
}

static void r_eval_variables_truncate(REval *eval, uint_large length) {
  while (nexus_data_list_length_get(eval->variables) > length) {
    (void)nexus_data_list_pop(eval->variables, NULL);
  }
}

/* ---------------------------------------------------------------------------- */
/* SYMBOLS                                                                      */
/* ---------------------------------------------------------------------------- */

static boolean r_eval_declaration_kind_is(RuleforgeNodeKind kind) {
  return kind == RNK_SCHEMA || kind == RNK_DATASET || kind == RNK_THEME_SCHEMA || kind == RNK_COLOR_SET || kind == RNK_THEME ||
         kind == RNK_TEMPLATE || kind == RNK_FUNCTION || kind == RNK_RULESET;
}

/* Summary: the alias an import goes by: the last segment of its module name. */
static const char *r_eval_alias_get(const char *module_name) {
  const char *last;

  last = nexus_strings_string_find_last(module_name, ".");

  return last == NULL ? module_name : last + 1;
}

/* Summary: the import of a module whose alias or full name is name; NULL when none. */
static const RuleforgeNode *r_eval_import_find(const RuleforgeModule *module, const char *name) {
  uint32 i;

  for (i = 0; i < module->ast->child_count; i++) {
    const RuleforgeNode *child;

    child = module->ast->children[i];

    if (child->kind == RNK_IMPORT &&
        (nexus_strings_string_equals(child->text, name) || nexus_strings_string_equals(r_eval_alias_get(child->text), name))) {
      return child;
    }
  }

  return NULL;
}

static const RuleforgeNode *r_eval_declaration_find(const RuleforgeModule *module, const char *name) {
  uint32 i;

  for (i = 0; i < module->ast->child_count; i++) {
    const RuleforgeNode *child;

    child = module->ast->children[i];

    if (r_eval_declaration_kind_is(child->kind) && nexus_strings_string_equals(child->text, name)) {
      return child;
    }
  }

  return NULL;
}

const RuleforgeNode *ruleforge_eval_symbol_find(REval *eval, RuleforgeModule *module, const char *name, RuleforgeModule **out_owner) {
  const RuleforgeNode *declaration;
  const char          *dot;
  char                 prefix[256];
  uint_large           length;
  uint32               i;
  RuleforgeModule     *imported;
  const RuleforgeNode *import;

  dot = nexus_strings_string_find_last(name, ".");

  if (dot != NULL) {
    length = (uint_large)(dot - name);

    if (length >= NEXUS_SIZEOF(prefix)) {
      return NULL;
    }

    nexus_memory_bytes_copy(name, prefix, length);
    prefix[length] = '\0';
    import         = r_eval_import_find(module, prefix);
    imported       = import == NULL ? NULL : ruleforge_compilation_module_find(eval->compilation, import->text);

    if (imported == NULL) {
      return NULL;
    }

    declaration = r_eval_declaration_find(imported, dot + 1);

    if (declaration != NULL && (declaration->flags & RNF_EXPORT) != 0) {
      *out_owner = imported;

      return declaration;
    }

    return NULL;
  }

  declaration = r_eval_declaration_find(module, name);

  if (declaration != NULL) {
    *out_owner = module;

    return declaration;
  }

  /* An imported export may be used unqualified when no other import exports the same name. */
  declaration = NULL;

  for (i = 0; i < module->ast->child_count; i++) {
    const RuleforgeNode *candidate;
    const RuleforgeNode *child;

    child = module->ast->children[i];

    if (child->kind != RNK_IMPORT) {
      continue;
    }

    imported = ruleforge_compilation_module_find(eval->compilation, child->text);

    if (imported == NULL) {
      continue;
    }

    candidate = r_eval_declaration_find(imported, name);

    if (candidate != NULL && (candidate->flags & RNF_EXPORT) != 0) {
      if (declaration != NULL) {
        ruleforge_eval_fail(eval, "RF0310", candidate->span, "qualify it with the module name, e.g. `module.name`",
                            "`%s` is exported by more than one import", name);

        return NULL;
      }

      declaration = candidate;
      *out_owner  = imported;
    }
  }

  return declaration;
}

/* ---------------------------------------------------------------------------- */
/* TYPES                                                                        */
/* ---------------------------------------------------------------------------- */

/*
Summary: checks (and widens) a value against a declared type; reports a mismatch at span.
Description: int widens to float; none is accepted only by optional types; named schema types accept any record, named theme
  schemas any theme. Element types of list<T> and dataset<T> are checked.
*/
static boolean r_eval_type_check(REval *eval, RuleforgeValue *value, const RuleforgeNode *type, RuleforgeSpan span, const char *what) {
  uint32  i;
  boolean ok;

  if (value->kind == RVK_NONE) {
    if ((type->flags & RNF_OPTIONAL) != 0) {
      return TRUE;
    }

    ruleforge_eval_fail(eval, "RF0330", span, "declare the type optional (`T?`) if none is allowed", "%s is none but its type is `%s`", what,
                        type->text);

    return FALSE;
  }

  ok = FALSE;

  if (nexus_strings_string_equals(type->text, "int")) {
    ok = value->kind == RVK_INT;
  } else if (nexus_strings_string_equals(type->text, "float")) {
    if (value->kind == RVK_INT) {
      *value = ruleforge_value_real((f_real)value->as.integer);
    }

    ok = value->kind == RVK_FLOAT;
  } else if (nexus_strings_string_equals(type->text, "bool")) {
    ok = value->kind == RVK_BOOL;
  } else if (nexus_strings_string_equals(type->text, "string")) {
    ok = value->kind == RVK_STRING;
  } else if (nexus_strings_string_equals(type->text, "color")) {
    ok = value->kind == RVK_COLOR;
  } else if (nexus_strings_string_equals(type->text, "style")) {
    ok = value->kind == RVK_STYLE;
  } else if (nexus_strings_string_equals(type->text, "ruleset")) {
    ok = value->kind == RVK_RULESET;
  } else if (nexus_strings_string_equals(type->text, "void")) {
    ok = TRUE;
  } else if (nexus_strings_string_equals(type->text, "list")) {
    ok = value->kind == RVK_LIST;

    for (i = 0; ok && type->child_count > 0 && i < value->as.list->count; i++) {
      ok = r_eval_type_check(eval, &value->as.list->items[i], type->children[0], span, what);

      if (!ok) {
        return FALSE;
      }
    }
  } else if (nexus_strings_string_equals(type->text, "dataset")) {
    ok = value->kind == RVK_DATASET || value->kind == RVK_LIST;
  } else {
    /* A named schema or theme schema. */
    ok = value->kind == RVK_RECORD || value->kind == RVK_THEME;
  }

  if (!ok) {
    ruleforge_eval_fail(eval, "RF0331", span, NULL, "%s is a %s but its type is `%s`", what, ruleforge_value_type_name_get(value->kind), type->text);
  }

  return ok;
}

/* ---------------------------------------------------------------------------- */
/* VALUE OF A DECLARATION                                                       */
/* ---------------------------------------------------------------------------- */

static boolean r_eval_cache_get(REval *eval, const RuleforgeNode *node, RuleforgeValue *out) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(eval->cache_keys); i++) {
    if (nexus_data_list_pointer_at(eval->cache_keys, i) == (void *)node) {
      *out = *(const RuleforgeValue *)nexus_data_list_at_const(eval->cache_values, i);

      return TRUE;
    }
  }

  return FALSE;
}

static void r_eval_cache_put(REval *eval, const RuleforgeNode *node, RuleforgeValue value) {
  (void)nexus_data_list_append_pointer(eval->cache_keys, node);
  (void)nexus_data_list_append(eval->cache_values, &value);
}

/* Summary: evaluates a ruleset declaration: its core conditions, then its statements, into a fresh ruleset. */
static RuleforgeValue r_eval_ruleset_run(REval *eval, const RuleforgeNode *declaration, RuleforgeModule *owner, RuleforgeSpan include_span,
                                         boolean included) {
  RuleforgeRuleset          *saved_emit;
  RuleforgeRuleset          *ruleset;
  RuleforgeModule           *saved_module;
  const RuleforgeProvenance *saved_provenance;
  uint_large                 saved_cores;
  uint_large                 saved_variables;
  uint32                     saved_base;
  uint32                     i;
  uint32                     j;
  RuleforgeDnf               dnf;

  if (eval->depth >= eval->settings->depth_limit) {
    ruleforge_eval_fail(eval, "RF0391", declaration->span, NULL, "rulesets are nested more than %lu deep",
                        (unsigned long)eval->settings->depth_limit);

    return ruleforge_value_none();
  }

  saved_emit       = eval->emit;
  saved_module     = eval->module;
  saved_provenance = eval->provenance;
  saved_cores      = nexus_data_list_length_get(eval->cores);
  saved_variables  = nexus_data_list_length_get(eval->variables);
  saved_base       = eval->frame_base;

  ruleset          = ruleforge_value_ruleset_create(eval->compilation, declaration->text);
  eval->emit       = ruleset;
  eval->module     = owner;
  eval->frame_base = (uint32)saved_variables;
  eval->depth++;
  eval->provenance =
      ruleforge_eval_provenance_push(eval, included ? RPK_INCLUDE : RPK_CALL, included ? include_span : declaration->span, declaration->text, NULL);

  for (i = 0; i < declaration->child_count && !eval->failed; i++) {
    const RuleforgeNode *item;

    item = declaration->children[i];

    if (item->kind == RNK_DESCRIPTION) {
      continue;
    }

    if (item->kind == RNK_CORE_CONDITIONS) {
      for (j = 0; j < item->child_count; j++) {
        if (!ruleforge_condition_evaluate(eval, item->children[j], FALSE, &dnf)) {
          break;
        }

        if (!dnf.constant && dnf.count > 1) {
          ruleforge_eval_fail(eval, "RF0360", item->children[j]->span, "write one condition per line instead of `or`",
                              "coreConditions cannot contain `or`");
          break;
        }

        if (dnf.constant && !dnf.value) {
          ruleforge_eval_fail(eval, "RF0361", item->children[j]->span, NULL, "a core condition is always false, so the ruleset can never match");
          break;
        }

        if (!dnf.constant) {
          uint32 k;

          for (k = 0; k < dnf.clauses[0].count; k++) {
            (void)nexus_data_list_append_pointer(eval->cores, dnf.clauses[0].conditions[k]);
          }
        }
      }

      continue;
    }

    /* The remaining items are statements; run them as a one-statement block. */
    {
      RuleforgeNode block;

      block.kind        = RNK_BLOCK;
      block.span        = item->span;
      block.child_count = 1;
      block.children    = (RuleforgeNode **)&declaration->children[i];

      if (!r_eval_block(eval, &block)) {
        break;
      }
    }
  }

  eval->depth--;
  eval->emit       = saved_emit;
  eval->module     = saved_module;
  eval->provenance = saved_provenance;
  eval->frame_base = saved_base;
  r_eval_variables_truncate(eval, saved_variables);

  while (nexus_data_list_length_get(eval->cores) > saved_cores) {
    (void)nexus_data_list_pop_pointer(eval->cores, NULL);
  }

  return ruleforge_value_ruleset(ruleset);
}

static RuleforgeValue r_eval_declaration_value(REval *eval, const RuleforgeNode *declaration, RuleforgeModule *owner, RuleforgeSpan span) {
  RuleforgeValue value;

  switch (declaration->kind) {
  case RNK_THEME:
    if (!r_eval_cache_get(eval, declaration, &value)) {
      value.kind          = RVK_THEME;
      value.is_identifier = FALSE;
      value.as.theme      = ruleforge_theme_resolve(eval, declaration, owner);

      if (value.as.theme == NULL) {
        return ruleforge_value_none();
      }

      r_eval_cache_put(eval, declaration, value);
    }

    return value;
  case RNK_DATASET:
    if (!r_eval_cache_get(eval, declaration, &value)) {
      value.kind          = RVK_DATASET;
      value.is_identifier = FALSE;
      value.as.dataset    = ruleforge_dataset_load(eval, declaration, owner);

      if (value.as.dataset == NULL) {
        return ruleforge_value_none();
      }

      r_eval_cache_put(eval, declaration, value);
    }

    return value;
  case RNK_RULESET:
    return r_eval_ruleset_run(eval, declaration, owner, span, TRUE);
  case RNK_COLOR_SET:
    value.kind          = RVK_MODULE;
    value.is_identifier = FALSE;
    {
      RuleforgeNamespace *space;

      space           = (RuleforgeNamespace *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*space));
      space->node     = declaration;
      space->owner    = owner;
      value.as.module = space;
    }

    return value;
  default:
    ruleforge_eval_fail(eval, "RF0311", span, "call it with arguments, e.g. `Name(...)`", "`%s` is a %s, not a value", declaration->text,
                        ruleforge_ast_kind_name_get(declaration->kind));

    return ruleforge_value_none();
  }
}

/* ---------------------------------------------------------------------------- */
/* REFERENCES                                                                   */
/* ---------------------------------------------------------------------------- */

/* Summary: the member `name` of a value: a record field, a theme style, a color of a color set, or an export of a module. */
static RuleforgeValue r_eval_member(REval *eval, RuleforgeValue base, const char *name, RuleforgeSpan span) {
  const RuleforgeValue *field;
  RuleforgeStyle       *style;

  if (base.kind == RVK_RECORD) {
    field = ruleforge_value_record_field_get(base.as.record, name);

    if (field == NULL) {
      ruleforge_eval_fail(eval, "RF0312", span, NULL, "the record has no field `%s`", name);

      return ruleforge_value_none();
    }

    return *field;
  }

  if (base.kind == RVK_THEME) {
    style = ruleforge_theme_style_get(base.as.theme, name);

    if (style == NULL) {
      ruleforge_eval_fail(eval, "RF0313", span, NULL, "theme `%s` has no style `%s`", base.as.theme->name, name);

      return ruleforge_value_none();
    }

    base.kind          = RVK_STYLE;
    base.is_identifier = FALSE;
    base.as.style      = style;

    return base;
  }

  if (base.kind == RVK_MODULE) {
    const RuleforgeNamespace *space;

    space = (const RuleforgeNamespace *)base.as.module;

    if (space->node != NULL) {
      uint32 i;

      for (i = 0; i < space->node->child_count; i++) {
        if (nexus_strings_string_equals(space->node->children[i]->text, name)) {
          RuleforgeModule *saved_module;
          RuleforgeValue   value;

          saved_module = eval->module;
          eval->module = space->owner;
          value        = ruleforge_eval_expression(eval, space->node->children[i]->children[0]);
          eval->module = saved_module;

          return value;
        }
      }

      ruleforge_eval_fail(eval, "RF0314", span, NULL, "color set `%s` has no color `%s`", space->node->text, name);

      return ruleforge_value_none();
    }
  }

  ruleforge_eval_fail(eval, "RF0315", span, NULL, "a %s has no member `%s`", ruleforge_value_type_name_get(base.kind), name);

  return ruleforge_value_none();
}

static boolean r_eval_compiler_value(REval *eval, const RuleforgeNode *node, RuleforgeValue *out) {
  uint_large i;

  if (eval->settings != NULL) {
    for (i = 0; i < nexus_data_list_length_get(eval->settings->values); i++) {
      const RuleforgeSettingValue *entry;

      entry = (const RuleforgeSettingValue *)nexus_data_list_pointer_at(eval->settings->values, i);

      if (nexus_strings_string_equals(entry->key, node->text)) {
        *out = entry->value;

        return TRUE;
      }
    }
  }

  /* The character and the atlas are optional by design: an absent value is none, so scripts guard it with `!= none`. */
  if (nexus_strings_string_starts_with(node->text, "character.") || nexus_strings_string_starts_with(node->text, "atlas.")) {
    *out = ruleforge_value_none();

    return TRUE;
  }

  {
    NexusStringBuilder known;
    char              *note;

    known = nexus_strings_string_builder_create_growable(64);
    (void)nexus_strings_string_builder_append(&known, "set it with `--set <key>=<value>` or in the settings file's [context] table; known keys:");

    for (i = 0; eval->settings != NULL && i < nexus_data_list_length_get(eval->settings->values); i++) {
      (void)nexus_strings_string_builder_append_formatted(
          &known, " %s", ((const RuleforgeSettingValue *)nexus_data_list_pointer_at(eval->settings->values, i))->key);
    }

    note = nexus_data_arena_text_copy(&eval->compilation->arena, nexus_strings_string_builder_string_get(&known),
                                      nexus_strings_string_builder_length_get(&known));
    nexus_strings_string_builder_destroy(&known);
    ruleforge_eval_fail(eval, "RF0316", node->span, note, "the compile-time value @%s is not set", node->text);
  }

  return FALSE;
}

boolean ruleforge_eval_reference_resolve(REval *eval, const RuleforgeNode *node, RuleforgeValue *out_value) {
  char                 segment[256];
  const char          *cursor;
  const char          *dot;
  uint_large           length;
  RuleforgeValue       value;
  const RuleforgeNode *declaration;
  RuleforgeModule     *owner;
  const RuleforgeNode *import;
  boolean              first;

  if (node->integer == RNK_REF_COMPILER) {
    if (!r_eval_compiler_value(eval, node, &value)) {
      return FALSE;
    }

    *out_value = value;

    return TRUE;
  }

  cursor = node->text;
  first  = TRUE;
  value  = ruleforge_value_none();

  while (*cursor != '\0' && !eval->failed) {
    dot    = nexus_strings_string_find(cursor, ".");
    length = dot == NULL ? nexus_strings_string_length_get(cursor) : (uint_large)(dot - cursor);

    if (length >= NEXUS_SIZEOF(segment)) {
      ruleforge_eval_fail(eval, "RF0317", node->span, NULL, "a name in this reference is too long");

      return FALSE;
    }

    nexus_memory_bytes_copy(cursor, segment, length);
    segment[length] = '\0';

    if (first) {
      first = FALSE;

      if (node->integer == RNK_REF_PARAMETER) {
        if (!ruleforge_eval_variable_find(eval, segment, TRUE, &value)) {
          ruleforge_eval_fail(eval, "RF0318", node->span, NULL, "`$%s` is not a parameter of the enclosing function or template", segment);

          return FALSE;
        }
      } else if (ruleforge_eval_variable_find(eval, segment, FALSE, &value)) {
        /* a local */
      } else if ((declaration = ruleforge_eval_symbol_find(eval, eval->module, segment, &owner)) != NULL) {
        value = r_eval_declaration_value(eval, declaration, owner, node->span);
      } else if ((import = r_eval_import_find(eval->module, segment)) != NULL &&
                 ruleforge_compilation_module_find(eval->compilation, import->text) != NULL) {
        RuleforgeNamespace *space;

        space               = (RuleforgeNamespace *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*space));
        space->module       = ruleforge_compilation_module_find(eval->compilation, import->text);
        value.kind          = RVK_MODULE;
        value.is_identifier = FALSE;
        value.as.module     = space;
      } else if (eval->enum_fallback && dot == NULL) {
        /* A style property argument such as Red or Circle: an enumerator the manifest validates. */
        value               = ruleforge_value_string(nexus_data_arena_text_copy(&eval->compilation->arena, segment, length));
        value.is_identifier = TRUE;
      } else {
        const char *note;

        note = ruleforge_manifest_condition_find(segment) != NULL
                   ? "game conditions such as this one can only be used in `when`, `coreConditions` and `scope`"
                   : NULL;
        ruleforge_eval_fail(eval, "RF0319", node->span, note, "unknown name `%s`", segment);

        return FALSE;
      }
    } else if (value.kind == RVK_MODULE && ((const RuleforgeNamespace *)value.as.module)->module != NULL) {
      const RuleforgeNamespace *space;

      space       = (const RuleforgeNamespace *)value.as.module;
      declaration = r_eval_declaration_find(space->module, segment);

      if (declaration == NULL || (declaration->flags & RNF_EXPORT) == 0) {
        ruleforge_eval_fail(eval, "RF0320", node->span, declaration == NULL ? NULL : "mark the declaration `export` to use it from another module",
                            "module `%s` does not export `%s`", space->module->ast->text, segment);

        return FALSE;
      }

      value = r_eval_declaration_value(eval, declaration, space->module, node->span);
    } else {
      value = r_eval_member(eval, value, segment, node->span);
    }

    if (eval->failed) {
      return FALSE;
    }

    cursor = dot == NULL ? cursor + length : dot + 1;
  }

  *out_value = value;

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* EXPRESSIONS                                                                  */
/* ---------------------------------------------------------------------------- */

static RuleforgeValue r_eval_binary(REval *eval, const RuleforgeNode *node) {
  RuleforgeValue left;
  RuleforgeValue right;
  const char    *op;
  f_real         a;
  f_real         b;
  boolean        both_int;

  op   = node->text;
  left = ruleforge_eval_expression(eval, node->children[0]);

  if (eval->failed) {
    return ruleforge_value_none();
  }

  /* and / or short-circuit, so `x != none and x >= 80` is safe. */
  if (nexus_strings_string_equals(op, "and") || nexus_strings_string_equals(op, "or")) {
    boolean is_and;

    is_and = nexus_strings_string_equals(op, "and");

    if (left.kind != RVK_BOOL) {
      ruleforge_eval_fail(eval, "RF0340", node->children[0]->span, NULL, "`%s` needs bool operands, got %s", op,
                          ruleforge_value_type_name_get(left.kind));

      return ruleforge_value_none();
    }

    if ((is_and && !left.as.flag) || (!is_and && left.as.flag)) {
      return left;
    }

    right = ruleforge_eval_expression(eval, node->children[1]);

    if (!eval->failed && right.kind != RVK_BOOL) {
      ruleforge_eval_fail(eval, "RF0340", node->children[1]->span, NULL, "`%s` needs bool operands, got %s", op,
                          ruleforge_value_type_name_get(right.kind));

      return ruleforge_value_none();
    }

    return right;
  }

  right = ruleforge_eval_expression(eval, node->children[1]);

  if (eval->failed) {
    return ruleforge_value_none();
  }

  if (nexus_strings_string_equals(op, "==")) {
    return ruleforge_value_flag(ruleforge_value_equals(&left, &right));
  }

  if (nexus_strings_string_equals(op, "!=")) {
    return ruleforge_value_flag(!ruleforge_value_equals(&left, &right));
  }

  if (left.kind == RVK_STRING && right.kind == RVK_STRING) {
    if (nexus_strings_string_equals(op, "+")) {
      NexusStringBuilder joined;
      char              *text;

      joined = nexus_strings_string_builder_create_growable(32);
      (void)nexus_strings_string_builder_append(&joined, left.as.string);
      (void)nexus_strings_string_builder_append(&joined, right.as.string);
      text = nexus_data_arena_text_copy(&eval->compilation->arena, nexus_strings_string_builder_string_get(&joined),
                                        nexus_strings_string_builder_length_get(&joined));
      nexus_strings_string_builder_destroy(&joined);

      return ruleforge_value_string(text);
    }

    {
      int order;

      order = 0;

      {
        const char *l;
        const char *r;

        l = left.as.string;
        r = right.as.string;

        while (*l != '\0' && *l == *r) {
          l++;
          r++;
        }

        order = (int)(byte)*l - (int)(byte)*r;
      }

      if (nexus_strings_string_equals(op, "<")) {
        return ruleforge_value_flag(order < 0);
      }

      if (nexus_strings_string_equals(op, "<=")) {
        return ruleforge_value_flag(order <= 0);
      }

      if (nexus_strings_string_equals(op, ">")) {
        return ruleforge_value_flag(order > 0);
      }

      if (nexus_strings_string_equals(op, ">=")) {
        return ruleforge_value_flag(order >= 0);
      }
    }
  }

  if (!ruleforge_value_number_get(&left, &a) || !ruleforge_value_number_get(&right, &b)) {
    ruleforge_eval_fail(
        eval, "RF0341", node->span, left.kind == RVK_NONE || right.kind == RVK_NONE ? "an optional value must be checked with `!= none` first" : NULL,
        "`%s` cannot be applied to %s and %s", op, ruleforge_value_type_name_get(left.kind), ruleforge_value_type_name_get(right.kind));

    return ruleforge_value_none();
  }

  both_int = left.kind == RVK_INT && right.kind == RVK_INT;

  if (nexus_strings_string_equals(op, "+")) {
    return both_int ? ruleforge_value_integer(left.as.integer + right.as.integer) : ruleforge_value_real(a + b);
  }

  if (nexus_strings_string_equals(op, "-")) {
    return both_int ? ruleforge_value_integer(left.as.integer - right.as.integer) : ruleforge_value_real(a - b);
  }

  if (nexus_strings_string_equals(op, "*")) {
    return both_int ? ruleforge_value_integer(left.as.integer * right.as.integer) : ruleforge_value_real(a * b);
  }

  if (nexus_strings_string_equals(op, "/")) {
    if (b == 0.0) {
      ruleforge_eval_fail(eval, "RF0342", node->span, NULL, "division by zero");

      return ruleforge_value_none();
    }

    return ruleforge_value_real(a / b);
  }

  if (nexus_strings_string_equals(op, "<")) {
    return ruleforge_value_flag(a < b);
  }

  if (nexus_strings_string_equals(op, "<=")) {
    return ruleforge_value_flag(a <= b);
  }

  if (nexus_strings_string_equals(op, ">")) {
    return ruleforge_value_flag(a > b);
  }

  return ruleforge_value_flag(a >= b);
}

RuleforgeValue ruleforge_eval_expression(REval *eval, const RuleforgeNode *node) {
  RuleforgeValue value;
  RuleforgeValue operand;
  RuleforgeList *list;
  uint32         i;

  if (!r_eval_step(eval, node->span)) {
    return ruleforge_value_none();
  }

  switch (node->kind) {
  case RNK_INT:
    return ruleforge_value_integer(node->integer);
  case RNK_FLOAT:
    return ruleforge_value_real(node->real);
  case RNK_STRING:
    return ruleforge_value_string(node->text);
  case RNK_BOOL:
    return ruleforge_value_flag(node->integer != 0);
  case RNK_NONE:
    return ruleforge_value_none();
  case RNK_COLOR:
    return ruleforge_value_color((uint32)node->integer);
  case RNK_LIST:
    list = ruleforge_value_list_create(eval->compilation, node->child_count);

    for (i = 0; i < node->child_count && !eval->failed; i++) {
      list->items[i] = ruleforge_eval_expression(eval, node->children[i]->children[0]);
    }

    return ruleforge_value_list(list);
  case RNK_ARG:
    return ruleforge_eval_expression(eval, node->children[0]);
  case RNK_REF:
    if (!ruleforge_eval_reference_resolve(eval, node, &value)) {
      return ruleforge_value_none();
    }

    return value;
  case RNK_CALL:
    return r_eval_call(eval, node);
  case RNK_INDEX:
    value   = ruleforge_eval_expression(eval, node->children[0]);
    operand = ruleforge_eval_expression(eval, node->children[1]);

    if (eval->failed) {
      return ruleforge_value_none();
    }

    if (value.kind != RVK_LIST || operand.kind != RVK_INT) {
      ruleforge_eval_fail(eval, "RF0343", node->span, NULL, "only a list can be indexed, with an int");

      return ruleforge_value_none();
    }

    if (operand.as.integer < 0 || (uint64)operand.as.integer >= value.as.list->count) {
      ruleforge_eval_fail(eval, "RF0344", node->span, NULL, "index %lld is outside the list of %lu", (long long)operand.as.integer,
                          (unsigned long)value.as.list->count);

      return ruleforge_value_none();
    }

    return value.as.list->items[operand.as.integer];
  case RNK_UNARY:
    operand = ruleforge_eval_expression(eval, node->children[0]);

    if (eval->failed) {
      return ruleforge_value_none();
    }

    if (nexus_strings_string_equals(node->text, "not")) {
      if (operand.kind != RVK_BOOL) {
        ruleforge_eval_fail(eval, "RF0340", node->span, NULL, "`not` needs a bool, got %s", ruleforge_value_type_name_get(operand.kind));

        return ruleforge_value_none();
      }

      return ruleforge_value_flag(!operand.as.flag);
    }

    if (operand.kind == RVK_INT) {
      return ruleforge_value_integer(-operand.as.integer);
    }

    if (operand.kind == RVK_FLOAT) {
      return ruleforge_value_real(-operand.as.real);
    }

    ruleforge_eval_fail(eval, "RF0341", node->span, NULL, "`-` needs a number, got %s", ruleforge_value_type_name_get(operand.kind));

    return ruleforge_value_none();
  case RNK_BINARY:
    return r_eval_binary(eval, node);
  default:
    ruleforge_eval_fail(eval, "RF0399", node->span, NULL, "%s is not an expression", ruleforge_ast_kind_name_get(node->kind));

    return ruleforge_value_none();
  }
}

boolean ruleforge_eval_boolean_get(REval *eval, const RuleforgeNode *node, boolean *out_flag) {
  RuleforgeValue value;

  value = ruleforge_eval_expression(eval, node);

  if (eval->failed) {
    return FALSE;
  }

  if (value.kind != RVK_BOOL) {
    ruleforge_eval_fail(eval, "RF0332", node->span, "optional values must be compared, e.g. `x != none`", "a condition must be a bool, got %s",
                        ruleforge_value_type_name_get(value.kind));

    return FALSE;
  }

  *out_flag = value.as.flag;

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* CALLS                                                                        */
/* ---------------------------------------------------------------------------- */

typedef struct RCallArguments {
  uint32          count;
  RuleforgeValue *values;
  const char    **names;
} RCallArguments;

static boolean r_eval_arguments_collect(REval *eval, const RuleforgeNode *call, RCallArguments *out) {
  uint32 i;

  out->count  = call->child_count - 1;
  out->values = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)out->count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
  out->names  = (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)out->count + 1U) * NEXUS_SIZEOF(const char *));

  for (i = 0; i < out->count; i++) {
    out->names[i]  = call->children[i + 1]->text;
    out->values[i] = ruleforge_eval_expression(eval, call->children[i + 1]->children[0]);

    if (eval->failed) {
      return FALSE;
    }
  }

  return TRUE;
}

/* Summary: binds call arguments to a parameter list (declared params or a native's names) into `bound`; reports arity and name errors. */
static boolean r_eval_arguments_bind(REval *eval, RuleforgeSpan span, const char *callee, const char *const *parameter_names,
                                     const RuleforgeNode *const *parameter_types, uint32 parameter_count, uint32 minimum,
                                     const RCallArguments *arguments, RuleforgeValue *bound, boolean *given) {
  uint32  i;
  uint32  j;
  uint32  position;
  boolean named_seen;

  named_seen = FALSE;
  position   = 0;

  for (i = 0; i < parameter_count; i++) {
    given[i] = FALSE;
  }

  for (i = 0; i < arguments->count; i++) {
    if (arguments->names[i] != NULL) {
      named_seen = TRUE;

      for (j = 0; j < parameter_count; j++) {
        if (nexus_strings_string_equals(parameter_names[j], arguments->names[i])) {
          break;
        }
      }

      if (j == parameter_count) {
        ruleforge_eval_fail(eval, "RF0321", span, NULL, "`%s` has no parameter named `%s`", callee, arguments->names[i]);

        return FALSE;
      }

      if (given[j]) {
        ruleforge_eval_fail(eval, "RF0322", span, NULL, "parameter `%s` of `%s` is given twice", arguments->names[i], callee);

        return FALSE;
      }

      bound[j] = arguments->values[i];
      given[j] = TRUE;
    } else {
      if (named_seen) {
        ruleforge_eval_fail(eval, "RF0323", span, "put positional arguments before named ones", "a positional argument follows a named argument");

        return FALSE;
      }

      if (position >= parameter_count) {
        ruleforge_eval_fail(eval, "RF0324", span, NULL, "`%s` takes %lu argument(s), got %lu", callee, (unsigned long)parameter_count,
                            (unsigned long)arguments->count);

        return FALSE;
      }

      bound[position] = arguments->values[i];
      given[position] = TRUE;
      position++;
    }
  }

  for (i = 0; i < parameter_count; i++) {
    if (!given[i]) {
      bound[i] = ruleforge_value_none();

      if (parameter_types != NULL && (parameter_types[i]->flags & RNF_OPTIONAL) != 0) {
        continue;
      }

      if (parameter_types == NULL && i >= minimum) {
        continue;
      }

      ruleforge_eval_fail(eval, "RF0325", span, NULL, "`%s` needs the argument `%s`", callee, parameter_names[i]);

      return FALSE;
    }

    if (parameter_types != NULL && !r_eval_type_check(eval, &bound[i], parameter_types[i], span, parameter_names[i])) {
      return FALSE;
    }
  }

  return TRUE;
}

/* Summary: runs a function declaration with bound arguments and returns its value (the accumulated ruleset for `-> ruleset`). */
static RuleforgeValue r_eval_function_run(REval *eval, const RuleforgeNode *function, RuleforgeModule *owner, const RuleforgeValue *bound,
                                          RuleforgeSpan call_span) {
  const RuleforgeNode       *parameters;
  const RuleforgeNode       *return_type;
  const RuleforgeNode       *body;
  RuleforgeRuleset          *saved_emit;
  RuleforgeModule           *saved_module;
  const RuleforgeProvenance *saved_provenance;
  uint_large                 saved_variables;
  uint32                     saved_base;
  boolean                    saved_returning;
  RuleforgeValue             saved_result;
  RuleforgeRuleset          *ruleset;
  RuleforgeValue             result;
  uint32                     i;

  if (eval->depth >= eval->settings->depth_limit) {
    ruleforge_eval_fail(eval, "RF0391", call_span, "recursion is bounded; raise [limits] depth if it is intended",
                        "calls are nested more than %lu deep", (unsigned long)eval->settings->depth_limit);

    return ruleforge_value_none();
  }

  parameters  = function->children[0];
  return_type = function->child_count == 3 ? function->children[1] : NULL;
  body        = function->children[function->child_count - 1];

  saved_emit       = eval->emit;
  saved_module     = eval->module;
  saved_provenance = eval->provenance;
  saved_variables  = nexus_data_list_length_get(eval->variables);
  saved_base       = eval->frame_base;
  saved_returning  = eval->returning;
  saved_result     = eval->result;

  eval->frame_base = (uint32)saved_variables;
  eval->module     = owner;
  eval->returning  = FALSE;
  eval->result     = ruleforge_value_none();
  eval->depth++;
  eval->provenance = ruleforge_eval_provenance_push(eval, RPK_CALL, call_span, function->text, NULL);

  ruleset    = NULL;
  eval->emit = NULL;

  if (return_type != NULL && nexus_strings_string_equals(return_type->text, "ruleset")) {
    ruleset    = ruleforge_value_ruleset_create(eval->compilation, function->text);
    eval->emit = ruleset;
  }

  for (i = 0; i < parameters->child_count; i++) {
    r_eval_variable_push(eval, parameters->children[i]->text, bound[i], FALSE, TRUE);
  }

  (void)r_eval_block(eval, body);

  result = ruleforge_value_none();

  if (!eval->failed) {
    if (ruleset != NULL) {
      result = eval->returning && eval->result.kind == RVK_RULESET ? eval->result : ruleforge_value_ruleset(ruleset);
    } else if (eval->returning) {
      result = eval->result;
    }

    if (return_type != NULL && !nexus_strings_string_equals(return_type->text, "void") &&
        !nexus_strings_string_equals(return_type->text, "ruleset")) {
      if (!eval->returning) {
        ruleforge_eval_fail(eval, "RF0333", function->span, NULL, "function `%s` must return a %s", function->text, return_type->text);
      } else {
        (void)r_eval_type_check(eval, &result, return_type, function->span, "the returned value");
      }
    }
  }

  eval->depth--;
  eval->emit       = saved_emit;
  eval->module     = saved_module;
  eval->provenance = saved_provenance;
  eval->frame_base = saved_base;
  eval->returning  = saved_returning;
  eval->result     = saved_result;
  r_eval_variables_truncate(eval, saved_variables);

  return result;
}

static RuleforgeValue r_eval_call(REval *eval, const RuleforgeNode *call) {
  const RuleforgeNode   *callee;
  const RuleforgeNode   *declaration;
  const RuleforgeNative *native;
  RuleforgeModule       *owner;
  RCallArguments         arguments;
  RuleforgeValue        *bound;
  boolean               *given;
  const char            *dot;
  char                   prefix[256];
  uint_large             length;
  const RuleforgeNode   *import;
  uint32                 i;

  callee      = call->children[0];
  native      = NULL;
  declaration = NULL;
  owner       = NULL;

  if (!r_eval_arguments_collect(eval, call, &arguments)) {
    return ruleforge_value_none();
  }

  dot = nexus_strings_string_find_last(callee->text, ".");

  if (dot != NULL) {
    length = (uint_large)(dot - callee->text);

    if (length < NEXUS_SIZEOF(prefix)) {
      nexus_memory_bytes_copy(callee->text, prefix, length);
      prefix[length] = '\0';
      import         = r_eval_import_find(eval->module, prefix);

      if (import != NULL) {
        native = ruleforge_stdlib_native_find(import->text, dot + 1);
      } else if (ruleforge_stdlib_module_exists(prefix)) {
        native = ruleforge_stdlib_native_find(prefix, dot + 1);
      }
    }
  }

  if (native == NULL) {
    declaration = ruleforge_eval_symbol_find(eval, eval->module, callee->text, &owner);
  }

  if (eval->failed) {
    return ruleforge_value_none();
  }

  if (native != NULL) {
    uint32      parameter_count;
    const char *names[16];
    char       *copy;
    char       *cursor;

    /* The native's parameter names are space separated in one string; split a copy into the array. */
    copy            = nexus_data_arena_text_copy(&eval->compilation->arena, native->parameters, nexus_strings_string_length_get(native->parameters));
    parameter_count = 0;
    cursor          = copy;

    while (*cursor != '\0' && parameter_count < 16) {
      names[parameter_count++] = cursor;

      while (*cursor != '\0' && *cursor != ' ') {
        cursor++;
      }

      if (*cursor == ' ') {
        *cursor = '\0';
        cursor++;
      }
    }

    bound = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameter_count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
    given = (boolean *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameter_count + 1U) * NEXUS_SIZEOF(boolean));

    if (!r_eval_arguments_bind(eval, call->span, callee->text, names, NULL, parameter_count, native->minimum, &arguments, bound, given)) {
      return ruleforge_value_none();
    }

    return native->function(eval, call->span, bound, parameter_count);
  }

  if (declaration == NULL || declaration->kind != RNK_FUNCTION) {
    ruleforge_eval_fail(eval, "RF0326", call->span,
                        declaration != NULL && declaration->kind == RNK_TEMPLATE ? "a template is used as `rule name = Template(...);`" : NULL,
                        "`%s` is not a function", callee->text);

    return ruleforge_value_none();
  }

  {
    const RuleforgeNode  *parameters;
    const char          **names;
    const RuleforgeNode **types;

    parameters = declaration->children[0];
    names =
        (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameters->child_count + 1U) * NEXUS_SIZEOF(const char *));
    types = (const RuleforgeNode **)nexus_data_arena_allocate(&eval->compilation->arena,
                                                              ((uint_large)parameters->child_count + 1U) * NEXUS_SIZEOF(const RuleforgeNode *));
    bound = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena,
                                                        ((uint_large)parameters->child_count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
    given = (boolean *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameters->child_count + 1U) * NEXUS_SIZEOF(boolean));

    for (i = 0; i < parameters->child_count; i++) {
      names[i] = parameters->children[i]->text;
      types[i] = parameters->children[i]->children[0];
    }

    if (!r_eval_arguments_bind(eval, call->span, callee->text, names, types, parameters->child_count, parameters->child_count, &arguments, bound,
                               given)) {
      return ruleforge_value_none();
    }
  }

  return r_eval_function_run(eval, declaration, owner, bound, call->span);
}

/* ---------------------------------------------------------------------------- */
/* STYLES                                                                       */
/* ---------------------------------------------------------------------------- */

RuleforgeStyle *ruleforge_eval_style_get(REval *eval, const RuleforgeNode *node) {
  RuleforgeValue  value;
  RuleforgeStyle *style;

  /* A bare name that is not a variable or declaration is a style key of the default theme. */
  if (node->kind == RNK_REF && node->integer == RNK_REF_PLAIN && nexus_strings_string_find(node->text, ".") == NULL && eval->default_theme != NULL) {
    RuleforgeModule *owner;

    if (!ruleforge_eval_variable_find(eval, node->text, FALSE, &value) &&
        ruleforge_eval_symbol_find(eval, eval->module, node->text, &owner) == NULL) {
      style = ruleforge_theme_style_get(eval->default_theme, node->text);

      if (style == NULL) {
        ruleforge_eval_fail(eval, "RF0313", node->span, NULL, "the default theme `%s` has no style `%s`", eval->default_theme->name, node->text);
      }

      return style;
    }
  }

  value = ruleforge_eval_expression(eval, node);

  if (eval->failed) {
    return NULL;
  }

  if (value.kind != RVK_STYLE) {
    ruleforge_eval_fail(eval, "RF0334", node->span, "a style is written `Theme.key`", "`style` needs a style, got %s",
                        ruleforge_value_type_name_get(value.kind));

    return NULL;
  }

  return value.as.style;
}

/* ---------------------------------------------------------------------------- */
/* RULES                                                                        */
/* ---------------------------------------------------------------------------- */

typedef struct RRuleBuilder {
  RuleforgeRule *rule;
  NexusDataList *scope;    /* RuleforgeCondition * */
  NexusDataList *variants; /* RuleforgeVariant */
  boolean        stop;     /* a terminal variant was added: later variants never match */
} RRuleBuilder;

static boolean r_eval_command_apply(REval *eval, const RuleforgeNode *command, boolean baseline, boolean *has_action, boolean *show, boolean *inherit,
                                    RuleforgeStyle **style) {
  if (command->kind == RNK_ACTION) {
    *has_action = TRUE;
    *show       = nexus_strings_string_equals(command->text, "show");

    return TRUE;
  }

  if (command->kind == RNK_INHERIT) {
    if (baseline) {
      ruleforge_eval_fail(eval, "RF0370", command->span, "`inherit action` is for variants, which can inherit the baseline's action",
                          "the baseline has nothing to inherit");

      return FALSE;
    }

    *inherit = TRUE;

    return TRUE;
  }

  *style = ruleforge_eval_style_get(eval, command->children[0]);

  return *style != NULL;
}

/* Summary: applies the items of a rule body (scope, baseline, variants, bare commands) to the builder. */
static boolean r_eval_rule_items(REval *eval, RRuleBuilder *builder, const RuleforgeNode *const *items, uint32 count) {
  uint32         i;
  uint32         j;
  uint32         k;
  RuleforgeRule *rule;

  rule = builder->rule;

  for (i = 0; i < count && !eval->failed; i++) {
    const RuleforgeNode *item;

    item = items[i];

    if (item->kind == RNK_SCOPE) {
      for (j = 0; j < item->child_count; j++) {
        RuleforgeCondition *condition;

        if (!ruleforge_condition_scope_build(eval, item->children[j], &condition)) {
          return FALSE;
        }

        if (condition != NULL) {
          (void)nexus_data_list_append_pointer(builder->scope, condition);
        }
      }
    } else if (item->kind == RNK_BASELINE) {
      for (j = 0; j < item->child_count; j++) {
        boolean inherit;

        inherit = FALSE;

        if (!r_eval_command_apply(eval, item->children[j], TRUE, &rule->has_action, &rule->show, &inherit, &rule->style)) {
          return FALSE;
        }
      }
    } else if (item->kind == RNK_VARIANTS) {
      for (j = 0; j < item->child_count && !eval->failed; j++) {
        const RuleforgeNode *when;
        RuleforgeDnf         dnf;
        RuleforgeVariant     variant;

        when = item->children[j];

        if (builder->stop) {
          rule->shadowed++;

          continue;
        }

        if (!ruleforge_condition_evaluate(eval, when->children[0], FALSE, &dnf)) {
          return FALSE;
        }

        /* A compile-time false condition removes the variant before emission. */
        if (dnf.constant && !dnf.value) {
          continue;
        }

        nexus_memory_bytes_set(&variant, NEXUS_SIZEOF(variant), 0);
        variant.span = when->span;

        if (dnf.constant) {
          variant.terminal = TRUE;
        } else {
          variant.clause_count = dnf.count;
          variant.clauses      = dnf.clauses;
        }

        for (k = 1; k < when->child_count; k++) {
          if (!r_eval_command_apply(eval, when->children[k], FALSE, &variant.has_action, &variant.show, &variant.inherit_action, &variant.style)) {
            return FALSE;
          }
        }

        if (!variant.has_action && !variant.inherit_action) {
          ruleforge_eval_fail(eval, "RF0371", when->span, "write `action show;`, `action hide;` or `inherit action;`",
                              "a variant must set the action or inherit it");

          return FALSE;
        }

        (void)nexus_data_list_append(builder->variants, &variant);

        if (variant.terminal) {
          builder->stop = TRUE;
        }
      }
    } else if (item->kind == RNK_ACTION || item->kind == RNK_STYLE || item->kind == RNK_INHERIT) {
      boolean inherit;

      inherit = FALSE;

      if (!r_eval_command_apply(eval, item, TRUE, &rule->has_action, &rule->show, &inherit, &rule->style)) {
        return FALSE;
      }
    }
  }

  return !eval->failed;
}

/* Summary: builds the immutable rule of a `rule` statement (inline body or template use) and appends it to the current ruleset. */
static boolean r_eval_rule(REval *eval, const RuleforgeNode *node) {
  RRuleBuilder               builder;
  RuleforgeRule             *rule;
  uint32                     i;
  uint_large                 saved_variables;
  uint32                     saved_base;
  const RuleforgeProvenance *saved_provenance;

  if (eval->emit == NULL) {
    ruleforge_eval_fail(eval, "RF0372", node->span, "rules can only be generated in a ruleset or in a function that returns `ruleset`",
                        "this `rule` is outside any ruleset");

    return FALSE;
  }

  if (eval->settings != NULL && eval->emitted >= eval->settings->rule_limit) {
    ruleforge_eval_fail(eval, "RF0392", node->span, "raise [limits] rules in the settings if this many rules are intended",
                        "the limit of %lu generated rules is reached", (unsigned long)eval->settings->rule_limit);

    return FALSE;
  }

  rule             = (RuleforgeRule *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*rule));
  rule->name       = node->text;
  rule->span       = node->span;
  builder.rule     = rule;
  builder.scope    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  builder.variants = nexus_data_list_create(NEXUS_SIZEOF(RuleforgeVariant), 4);
  builder.stop     = FALSE;

  saved_provenance = eval->provenance;
  saved_variables  = nexus_data_list_length_get(eval->variables);
  saved_base       = eval->frame_base;

  if (node->child_count == 1 && node->children[0]->kind == RNK_CALL) {
    /* rule Name = Template(args); */
    const RuleforgeNode  *call;
    const RuleforgeNode  *template_node;
    RuleforgeModule      *owner;
    RCallArguments        arguments;
    RuleforgeValue       *bound;
    boolean              *given;
    const char          **names;
    const RuleforgeNode **types;
    const RuleforgeNode  *parameters;
    RuleforgeModule      *saved_module;

    call          = node->children[0];
    template_node = ruleforge_eval_symbol_find(eval, eval->module, call->children[0]->text, &owner);

    if (template_node == NULL || template_node->kind != RNK_TEMPLATE) {
      ruleforge_eval_fail(eval, "RF0373", call->span, NULL, "`%s` is not a template", call->children[0]->text);
      nexus_data_list_destroy(builder.scope);
      nexus_data_list_destroy(builder.variants);

      return FALSE;
    }

    if (!r_eval_arguments_collect(eval, call, &arguments)) {
      nexus_data_list_destroy(builder.scope);
      nexus_data_list_destroy(builder.variants);

      return FALSE;
    }

    parameters = template_node->children[0];
    names =
        (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameters->child_count + 1U) * NEXUS_SIZEOF(const char *));
    types = (const RuleforgeNode **)nexus_data_arena_allocate(&eval->compilation->arena,
                                                              ((uint_large)parameters->child_count + 1U) * NEXUS_SIZEOF(const RuleforgeNode *));
    bound = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena,
                                                        ((uint_large)parameters->child_count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
    given = (boolean *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameters->child_count + 1U) * NEXUS_SIZEOF(boolean));

    for (i = 0; i < parameters->child_count; i++) {
      names[i] = parameters->children[i]->text;
      types[i] = parameters->children[i]->children[0];
    }

    if (!r_eval_arguments_bind(eval, call->span, call->children[0]->text, names, types, parameters->child_count, parameters->child_count, &arguments,
                               bound, given)) {
      nexus_data_list_destroy(builder.scope);
      nexus_data_list_destroy(builder.variants);

      return FALSE;
    }

    if (rule->name == NULL) {
      rule->name = template_node->text;
    }

    saved_module     = eval->module;
    eval->module     = owner;
    eval->frame_base = (uint32)saved_variables;
    eval->provenance = ruleforge_eval_provenance_push(eval, RPK_TEMPLATE, call->span, template_node->text, NULL);

    for (i = 0; i < parameters->child_count; i++) {
      r_eval_variable_push(eval, names[i], bound[i], FALSE, TRUE);
    }

    (void)r_eval_rule_items(eval, &builder, (const RuleforgeNode *const *)&template_node->children[1], template_node->child_count - 1);
    eval->module = saved_module;
  } else {
    eval->provenance = ruleforge_eval_provenance_push(eval, RPK_DECLARATION, node->span, node->text == NULL ? "" : node->text, NULL);
    (void)r_eval_rule_items(eval, &builder, (const RuleforgeNode *const *)node->children, node->child_count);
  }

  rule->provenance = eval->provenance;
  eval->provenance = saved_provenance;
  eval->frame_base = saved_base;
  r_eval_variables_truncate(eval, saved_variables);

  if (eval->failed) {
    nexus_data_list_destroy(builder.scope);
    nexus_data_list_destroy(builder.variants);

    return FALSE;
  }

  /* Core conditions of every enclosing ruleset join the rule's own scope. */
  for (i = 0; i < nexus_data_list_length_get(eval->cores); i++) {
    (void)nexus_data_list_append_pointer(builder.scope, nexus_data_list_pointer_at(eval->cores, i));
  }

  rule->scope.count      = (uint32)nexus_data_list_length_get(builder.scope);
  rule->scope.conditions = (RuleforgeCondition **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)rule->scope.count + 1U) *
                                                                                                           NEXUS_SIZEOF(RuleforgeCondition *));

  for (i = 0; i < rule->scope.count; i++) {
    rule->scope.conditions[i] = (RuleforgeCondition *)nexus_data_list_pointer_at(builder.scope, i);
  }

  rule->variant_count = (uint32)nexus_data_list_length_get(builder.variants);
  rule->variants      = (RuleforgeVariant *)nexus_data_arena_allocate(&eval->compilation->arena,
                                                                      ((uint_large)rule->variant_count + 1U) * NEXUS_SIZEOF(RuleforgeVariant));

  for (i = 0; i < rule->variant_count; i++) {
    rule->variants[i] = *(const RuleforgeVariant *)nexus_data_list_at_const(builder.variants, i);

    if (rule->variants[i].inherit_action && !rule->has_action) {
      ruleforge_eval_fail(eval, "RF0374", rule->variants[i].span, "give the rule's baseline an `action`",
                          "`inherit action` but the rule has no baseline action");
      nexus_data_list_destroy(builder.scope);
      nexus_data_list_destroy(builder.variants);

      return FALSE;
    }
  }

  if (builder.stop && rule->has_action) {
    /* The baseline can never be reached after a variant that always matches. */
    rule->shadowed++;
  }

  nexus_data_list_destroy(builder.scope);
  nexus_data_list_destroy(builder.variants);

  if (!rule->has_action && rule->variant_count == 0) {
    ruleforge_eval_fail(eval, "RF0375", node->span, "add `action show;` or `action hide;`", "rule `%s` has no action",
                        rule->name == NULL ? "" : rule->name);

    return FALSE;
  }

  ruleforge_value_ruleset_rule_add(eval->compilation, eval->emit, rule);
  eval->emitted++;

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* STATEMENTS                                                                   */
/* ---------------------------------------------------------------------------- */

static void r_eval_ruleset_append(REval *eval, const RuleforgeRuleset *source, RuleforgeSpan span) {
  uint32 i;

  for (i = 0; i < source->count; i++) {
    ruleforge_value_ruleset_rule_add(eval->compilation, eval->emit, source->rules[i]);
    eval->emitted++;
  }

  if (eval->settings != NULL && eval->emitted > eval->settings->rule_limit) {
    ruleforge_eval_fail(eval, "RF0392", span, "raise [limits] rules in the settings if this many rules are intended",
                        "the limit of %lu generated rules is reached", (unsigned long)eval->settings->rule_limit);
  }
}

static boolean r_eval_statement(REval *eval, const RuleforgeNode *node) {
  RuleforgeValue value;
  uint32         i;

  if (!r_eval_step(eval, node->span)) {
    return FALSE;
  }

  switch (node->kind) {
  case RNK_LET:
    value = ruleforge_eval_expression(eval, node->children[node->child_count - 1]);

    if (eval->failed) {
      return FALSE;
    }

    if (node->child_count == 2 && !r_eval_type_check(eval, &value, node->children[0], node->span, node->text)) {
      return FALSE;
    }

    {
      RuleforgeValue existing;

      if (ruleforge_eval_variable_find(eval, node->text, FALSE, &existing)) {
        ruleforge_eval_fail(eval, "RF0335", node->span, NULL, "`%s` is already declared in this function", node->text);

        return FALSE;
      }
    }

    r_eval_variable_push(eval, node->text, value, (node->flags & RNF_MUTABLE) != 0, FALSE);

    return TRUE;
  case RNK_ASSIGN: {
    uint_large         index;
    RuleforgeVariable *variable;

    value = ruleforge_eval_expression(eval, node->children[0]);

    if (eval->failed) {
      return FALSE;
    }

    for (index = nexus_data_list_length_get(eval->variables); index > eval->frame_base; index--) {
      variable = (RuleforgeVariable *)nexus_data_list_at(eval->variables, index - 1);

      if (!variable->parameter && nexus_strings_string_equals(variable->name, node->text)) {
        if (!variable->mutable_flag) {
          ruleforge_eval_fail(eval, "RF0336", node->span, "declare it with `var` to reassign it", "`%s` is immutable", node->text);

          return FALSE;
        }

        if (value.kind == RVK_INT && variable->value.kind == RVK_FLOAT) {
          value = ruleforge_value_real((f_real)value.as.integer);
        }

        if (value.kind != variable->value.kind && variable->value.kind != RVK_NONE && value.kind != RVK_NONE) {
          ruleforge_eval_fail(eval, "RF0337", node->span, NULL, "`%s` holds a %s; it cannot become a %s", node->text,
                              ruleforge_value_type_name_get(variable->value.kind), ruleforge_value_type_name_get(value.kind));

          return FALSE;
        }

        variable->value = value;

        return TRUE;
      }
    }

    ruleforge_eval_fail(eval, "RF0338", node->span, NULL, "`%s` is not a variable", node->text);

    return FALSE;
  }
  case RNK_IF: {
    boolean condition;

    if (!ruleforge_eval_boolean_get(eval, node->children[0], &condition)) {
      return FALSE;
    }

    if (condition) {
      return r_eval_block(eval, node->children[1]);
    }

    if (node->child_count == 3) {
      return node->children[2]->kind == RNK_IF ? r_eval_statement(eval, node->children[2]) : r_eval_block(eval, node->children[2]);
    }

    return TRUE;
  }
  case RNK_FOR: {
    RuleforgeValue             iterable;
    const RuleforgeNode       *where;
    const RuleforgeNode       *body;
    uint32                     count;
    uint_large                 saved_variables;
    const RuleforgeProvenance *saved_provenance;

    iterable = ruleforge_eval_expression(eval, node->children[0]);

    if (eval->failed) {
      return FALSE;
    }

    if (iterable.kind != RVK_LIST && iterable.kind != RVK_DATASET) {
      ruleforge_eval_fail(eval, "RF0345", node->children[0]->span, NULL, "`for` needs a list or a dataset, got %s",
                          ruleforge_value_type_name_get(iterable.kind));

      return FALSE;
    }

    where = node->child_count == 3 ? node->children[1] : NULL;
    body  = node->children[node->child_count - 1];
    count = iterable.kind == RVK_LIST ? iterable.as.list->count : iterable.as.dataset->count;

    saved_variables  = nexus_data_list_length_get(eval->variables);
    saved_provenance = eval->provenance;

    for (i = 0; i < count && !eval->failed && !eval->returning; i++) {
      RuleforgeValue item;
      boolean        keep;

      /* An empty body still costs a step, so no loop is free. */
      if (!r_eval_step(eval, node->span)) {
        break;
      }

      if (iterable.kind == RVK_LIST) {
        item = iterable.as.list->items[i];
      } else {
        item.kind          = RVK_RECORD;
        item.is_identifier = FALSE;
        item.as.record     = iterable.as.dataset->rows[i];
      }

      r_eval_variable_push(eval, node->text, item, FALSE, FALSE);

      if (item.kind == RVK_RECORD && item.as.record->dataset != 0xFFFFFFFFU) {
        eval->provenance =
            ruleforge_eval_provenance_push(eval, RPK_ROW, node->span, iterable.kind == RVK_DATASET ? iterable.as.dataset->name : "", item.as.record);
      }

      keep = TRUE;

      if (where != NULL && !ruleforge_eval_boolean_get(eval, where->children[0], &keep)) {
        break;
      }

      if (keep) {
        (void)r_eval_block(eval, body);
      }

      eval->provenance = saved_provenance;
      r_eval_variables_truncate(eval, saved_variables);
    }

    eval->provenance = saved_provenance;
    r_eval_variables_truncate(eval, saved_variables);

    return !eval->failed;
  }
  case RNK_RETURN:
    if (node->child_count == 1) {
      eval->result = ruleforge_eval_expression(eval, node->children[0]);
    } else {
      eval->result = ruleforge_value_none();
    }

    eval->returning = TRUE;

    return !eval->failed;
  case RNK_INCLUDE: {
    const RuleforgeNode *target;
    const RuleforgeNode *with;
    RuleforgeTheme      *saved_theme;
    RuleforgeValue       included;

    if (eval->emit == NULL) {
      ruleforge_eval_fail(eval, "RF0372", node->span, "includes can only appear in a ruleset or in a function that returns `ruleset`",
                          "this `include` is outside any ruleset");

      return FALSE;
    }

    target      = node->children[0];
    with        = node->child_count == 2 ? node->children[1] : NULL;
    saved_theme = eval->default_theme;

    for (i = 0; with != NULL && i < with->child_count; i++) {
      const RuleforgeNode *binding;
      RuleforgeValue       bound;

      binding = with->children[i];

      if (nexus_strings_string_equals(binding->text, "theme")) {
        bound = ruleforge_eval_expression(eval, binding->children[0]);

        if (eval->failed) {
          return FALSE;
        }

        if (bound.kind != RVK_THEME) {
          ruleforge_eval_fail(eval, "RF0376", binding->span, NULL, "`theme:` needs a theme, got %s", ruleforge_value_type_name_get(bound.kind));

          return FALSE;
        }

        eval->default_theme = bound.as.theme;
        eval->override_depth++;
      } else {
        ruleforge_diagnostic_report(eval->compilation, RDS_WARNING, "RF0377", binding->span, "only `theme:` bindings are applied in this version",
                                    "`with { %s: ... }` has no effect", binding->text);
      }
    }

    included = ruleforge_eval_expression(eval, target);

    eval->default_theme = saved_theme;

    for (i = 0; with != NULL && i < with->child_count; i++) {
      if (nexus_strings_string_equals(with->children[i]->text, "theme")) {
        eval->override_depth--;
      }
    }

    if (eval->failed) {
      return FALSE;
    }

    if (included.kind != RVK_RULESET) {
      ruleforge_eval_fail(eval, "RF0378", target->span, NULL, "`include` needs a ruleset, got %s", ruleforge_value_type_name_get(included.kind));

      return FALSE;
    }

    r_eval_ruleset_append(eval, included.as.ruleset, node->span);

    return !eval->failed;
  }
  case RNK_EMIT:
    return r_eval_rule(eval, node->children[0]);
  case RNK_RULE:
    return r_eval_rule(eval, node);
  case RNK_NATIVE: {
    RuleforgeRule *rule;

    if (eval->emit == NULL) {
      ruleforge_eval_fail(eval, "RF0372", node->span, NULL, "this `native` is outside any ruleset");

      return FALSE;
    }

    rule             = (RuleforgeRule *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*rule));
    rule->name       = "native";
    rule->span       = node->span;
    rule->native     = node->text;
    rule->provenance = ruleforge_eval_provenance_push(eval, RPK_DECLARATION, node->span, "native", NULL);
    ruleforge_value_ruleset_rule_add(eval->compilation, eval->emit, rule);
    eval->emitted++;

    return TRUE;
  }
  default:
    ruleforge_eval_fail(eval, "RF0399", node->span, NULL, "%s cannot appear here", ruleforge_ast_kind_name_get(node->kind));

    return FALSE;
  }
}

static boolean r_eval_block(REval *eval, const RuleforgeNode *block) {
  uint_large saved_variables;
  uint32     i;

  saved_variables = nexus_data_list_length_get(eval->variables);

  for (i = 0; i < block->child_count && !eval->failed && !eval->returning; i++) {
    (void)r_eval_statement(eval, block->children[i]);
  }

  r_eval_variables_truncate(eval, saved_variables);

  return !eval->failed;
}

/* ---------------------------------------------------------------------------- */
/* MAIN                                                                         */
/* ---------------------------------------------------------------------------- */

boolean ruleforge_eval_main_run(REval *eval, RuleforgeRuleset **out_ruleset) {
  RuleforgeCompilation *compilation;
  RuleforgeModule      *entry;
  const RuleforgeNode  *main_function;
  uint_large            i;
  uint32                j;
  uint_large            k;
  RuleforgeModule      *candidate;
  RuleforgeValue        result;
  RuleforgeValue        none;
  boolean               imported;

  compilation   = eval->compilation;
  entry         = NULL;
  main_function = NULL;

  /* The entry module exports `func main` and is not imported by another module. */
  for (i = 0; i < nexus_data_list_length_get(compilation->modules); i++) {
    candidate = (RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, i);

    for (j = 0; j < candidate->ast->child_count; j++) {
      const RuleforgeNode *child;

      child = candidate->ast->children[j];

      if (child->kind != RNK_FUNCTION || !nexus_strings_string_equals(child->text, "main") || (child->flags & RNF_EXPORT) == 0) {
        continue;
      }

      imported = FALSE;

      for (k = 0; k < nexus_data_list_length_get(compilation->modules); k++) {
        const RuleforgeModule *other;
        uint32                 m;

        other = (const RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, k);

        for (m = 0; other != candidate && m < other->ast->child_count; m++) {
          if (other->ast->children[m]->kind == RNK_IMPORT && nexus_strings_string_equals(other->ast->children[m]->text, candidate->ast->text)) {
            imported = TRUE;
          }
        }
      }

      if (!imported) {
        if (entry != NULL) {
          ruleforge_eval_fail(eval, "RF0380", child->span, "keep one entry module; the others should be imported libraries",
                              "more than one module exports `main`: `%s` and `%s`", entry->ast->text, candidate->ast->text);

          return FALSE;
        }

        entry         = candidate;
        main_function = child;
      }
    }
  }

  if (entry == NULL) {
    ruleforge_diagnostic_report(compilation, RDS_ERROR, "RF0381", ruleforge_span_make(0, 0, 0),
                                "write `export func main() { include ...; }` in the entry module", "no module exports `func main`");
    eval->failed = TRUE;

    return FALSE;
  }

  eval->entry  = entry;
  eval->module = entry;

  /* configuration { default-theme = ...; } */
  for (j = 0; j < entry->ast->child_count && !eval->failed; j++) {
    const RuleforgeNode *declaration;
    uint32               m;

    declaration = entry->ast->children[j];

    if (declaration->kind != RNK_CONFIGURATION) {
      continue;
    }

    for (m = 0; m < declaration->child_count; m++) {
      if (declaration->children[m]->kind == RNK_CONFIG_ASSIGN && nexus_strings_string_equals(declaration->children[m]->text, "default-theme")) {
        result = ruleforge_eval_expression(eval, declaration->children[m]->children[0]);

        if (!eval->failed && result.kind != RVK_THEME) {
          ruleforge_eval_fail(eval, "RF0382", declaration->children[m]->span, NULL, "`default-theme` needs a theme, got %s",
                              ruleforge_value_type_name_get(result.kind));
        }

        if (!eval->failed) {
          eval->default_theme = result.as.theme;
        }
      }
    }
  }

  if (eval->failed) {
    return FALSE;
  }

  if (main_function->children[0]->child_count != 0) {
    ruleforge_eval_fail(eval, "RF0383", main_function->span, NULL, "`main` takes no parameters");

    return FALSE;
  }

  /* `main` may be declared void: its includes still collect into one ruleset. */
  {
    RuleforgeRuleset *ruleset;

    ruleset    = ruleforge_value_ruleset_create(compilation, "main");
    eval->emit = ruleset;
    none       = ruleforge_value_none();
    (void)none;
    eval->provenance = ruleforge_eval_provenance_push(eval, RPK_CALL, main_function->span, "main", NULL);

    (void)r_eval_block(eval, main_function->children[main_function->child_count - 1]);

    if (eval->returning && eval->result.kind == RVK_RULESET) {
      ruleset = eval->result.as.ruleset;
    }

    if (eval->failed) {
      return FALSE;
    }

    *out_ruleset = ruleset;
  }

  return TRUE;
}
