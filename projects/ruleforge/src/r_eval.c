#include <nexus.h>
#include <echo.h>

#include <logos.h>
#include <rf_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
The evaluator runs Ruleforge's compile-time code: expressions, statements, functions, templates, rulesets and loops over datasets. It
reads the syntax only through the views `genesis bind` generates from spec/ruleforge.gen, so a grammar change that renames or removes
something the evaluator reads is a compile error here, never a silent misreading. It works under explicit budgets (steps, call depth,
emitted rules) and records, for every generated rule, where it came from. Errors are diagnostics; the first one marks the evaluation
failed and unwinds it.
*/

#define R_EVAL_SEGMENT_LIMIT 12

/* How a reference is prefixed: `$name` is a parameter, `@name` a compile-time value. */
enum {
  R_REF_PLAIN,
  R_REF_PARAMETER,
  R_REF_COMPILER
};

static boolean        r_eval_generator(REval *eval, RfGeneratorStmt view);
static RuleforgeValue r_eval_call(REval *eval, RfCall call);

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

RuleforgeSpan ruleforge_eval_span_get(const REval *eval, const LParserTree *tree, const LParserGreenNode *node) {
  return ruleforge_syntax_span_get(eval->module, tree, node);
}

const char *ruleforge_eval_text_get(REval *eval, const LParserTree *tree, const LParserGreenNode *token) {
  return ruleforge_syntax_text_get(eval->compilation, tree, token);
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

RuleforgeCondition *ruleforge_eval_condition_create(REval *eval, const RuleforgeConditionInfo *info, RuleforgeOperator operation, uint32 count) {
  RuleforgeCondition *condition;

  condition          = (RuleforgeCondition *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*condition));
  condition->info    = info;
  condition->op      = operation;
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

/* Summary: the alias an import goes by: the last segment of its module name. */
static const char *r_eval_alias_get(const char *module_name) {
  const char *last;

  last = nexus_strings_string_find_last(module_name, ".");

  return last == NULL ? module_name : last + 1;
}

/* Summary: the import of a module whose alias or full name is name; NULL when none. */
static const char *r_eval_import_find(const RuleforgeModule *module, const char *name) {
  uint32 i;

  for (i = 0; i < module->import_count; i++) {
    if (nexus_strings_string_equals(module->imports[i], name) || nexus_strings_string_equals(r_eval_alias_get(module->imports[i]), name)) {
      return module->imports[i];
    }
  }

  return NULL;
}

const RuleforgeDeclaration *ruleforge_eval_symbol_find(REval *eval, RuleforgeModule *module, const char *name, RuleforgeModule **out_owner) {
  const RuleforgeDeclaration *declaration;
  const RuleforgeDeclaration *candidate;
  const char                 *dot;
  char                        prefix[256];
  uint_large                  length;
  uint32                      i;
  RuleforgeModule            *imported;
  const char                 *import;

  dot = nexus_strings_string_find_last(name, ".");

  if (dot != NULL) {
    length = (uint_large)(dot - name);

    if (length >= NEXUS_SIZEOF(prefix)) {
      return NULL;
    }

    nexus_memory_bytes_copy(name, prefix, length);
    prefix[length] = '\0';
    import         = r_eval_import_find(module, prefix);
    imported       = import == NULL ? NULL : ruleforge_compilation_module_find(eval->compilation, import);

    if (imported == NULL) {
      return NULL;
    }

    declaration = ruleforge_compilation_declaration_find(imported, dot + 1);

    if (declaration != NULL && declaration->exported) {
      *out_owner = imported;

      return declaration;
    }

    return NULL;
  }

  declaration = ruleforge_compilation_declaration_find(module, name);

  if (declaration != NULL) {
    *out_owner = module;

    return declaration;
  }

  /* An imported export may be used unqualified when no other import exports the same name. */
  declaration = NULL;

  for (i = 0; i < module->import_count; i++) {
    imported = ruleforge_compilation_module_find(eval->compilation, module->imports[i]);

    if (imported == NULL) {
      continue;
    }

    candidate = ruleforge_compilation_declaration_find(imported, name);

    if (candidate != NULL && candidate->exported) {
      if (declaration != NULL) {
        ruleforge_eval_fail(eval, "RF0310", ruleforge_span_make(module->file, 0, 0), "qualify it with the module name, e.g. `module.name`",
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

RuleforgeTypeKind ruleforge_eval_type_kind_get(RfType type) {
  RfBaseType   base;
  RfScalarType scalar;

  base = rf_type_rule_base_type(type, 0);

  if (rf_base_type_count_token_kw_list(base) > 0) {
    return RTK_LIST;
  }

  if (rf_base_type_count_token_kw_dataset(base) > 0) {
    return RTK_DATASET;
  }

  if (rf_base_type_count_rule_qualified_name(base) > 0) {
    return RTK_NAMED;
  }

  scalar = rf_base_type_rule_scalar_type(base, 0);

  if (rf_scalar_type_count_token_kw_int(scalar) > 0) {
    return RTK_INT;
  }

  if (rf_scalar_type_count_token_kw_float(scalar) > 0) {
    return RTK_FLOAT;
  }

  if (rf_scalar_type_count_token_kw_bool(scalar) > 0) {
    return RTK_BOOL;
  }

  if (rf_scalar_type_count_token_kw_string(scalar) > 0) {
    return RTK_STRING;
  }

  if (rf_scalar_type_count_token_kw_color(scalar) > 0) {
    return RTK_COLOR;
  }

  if (rf_scalar_type_count_token_kw_style(scalar) > 0) {
    return RTK_STYLE;
  }

  if (rf_scalar_type_count_token_kw_ruleset(scalar) > 0) {
    return RTK_RULESET;
  }

  return RTK_VOID;
}

const char *ruleforge_eval_type_name_get(REval *eval, RfType type) {
  switch (ruleforge_eval_type_kind_get(type)) {
  case RTK_INT:
    return "int";
  case RTK_FLOAT:
    return "float";
  case RTK_BOOL:
    return "bool";
  case RTK_STRING:
    return "string";
  case RTK_COLOR:
    return "color";
  case RTK_STYLE:
    return "style";
  case RTK_VOID:
    return "void";
  case RTK_RULESET:
    return "ruleset";
  case RTK_LIST:
    return "list";
  case RTK_DATASET:
    return "dataset";
  case RTK_NAMED:
    return ruleforge_syntax_name_get(eval->compilation, rf_base_type_rule_qualified_name(rf_type_rule_base_type(type, 0), 0));
  }

  return "value";
}

/*
Summary: checks (and widens) a value against a declared type; reports a mismatch at span.
Description: int widens to float; none is accepted only by optional types; named schema types accept any record, named theme schemas
  any theme. Element types of list<T> are checked.
*/
boolean ruleforge_eval_type_check(REval *eval, RuleforgeValue *value, RfType type, RuleforgeSpan span, const char *what) {
  RuleforgeTypeKind kind;
  uint32            i;
  boolean           ok;

  if (value->kind == RVK_NONE) {
    if (rf_type_count_token_question(type) > 0) {
      return TRUE;
    }

    ruleforge_eval_fail(eval, "RF0330", span, "declare the type optional (`T?`) if none is allowed", "%s is none but its type is `%s`", what,
                        ruleforge_eval_type_name_get(eval, type));

    return FALSE;
  }

  kind = ruleforge_eval_type_kind_get(type);

  switch (kind) {
  case RTK_INT:
    ok = value->kind == RVK_INT;
    break;
  case RTK_FLOAT:
    if (value->kind == RVK_INT) {
      *value = ruleforge_value_real((f_real)value->as.integer);
    }

    ok = value->kind == RVK_FLOAT;
    break;
  case RTK_BOOL:
    ok = value->kind == RVK_BOOL;
    break;
  case RTK_STRING:
    ok = value->kind == RVK_STRING;
    break;
  case RTK_COLOR:
    ok = value->kind == RVK_COLOR;
    break;
  case RTK_STYLE:
    ok = value->kind == RVK_STYLE;
    break;
  case RTK_RULESET:
    ok = value->kind == RVK_RULESET;
    break;
  case RTK_VOID:
    ok = TRUE;
    break;
  case RTK_LIST:
    ok = value->kind == RVK_LIST;

    for (i = 0; ok && i < value->as.list->count; i++) {
      RfBaseType base;

      base = rf_type_rule_base_type(type, 0);
      ok   = ruleforge_eval_type_check(eval, &value->as.list->items[i], rf_base_type_rule_type(base, 0), span, what);

      if (!ok) {
        return FALSE;
      }
    }

    break;
  case RTK_DATASET:
    ok = value->kind == RVK_DATASET || value->kind == RVK_LIST;
    break;
  case RTK_NAMED:
    /* A named schema or theme schema. */
    ok = value->kind == RVK_RECORD || value->kind == RVK_THEME;
    break;
  }

  if (!ok) {
    ruleforge_eval_fail(eval, "RF0331", span, NULL, "%s is a %s but its type is `%s`", what, ruleforge_value_type_name_get(value->kind),
                        ruleforge_eval_type_name_get(eval, type));
  }

  return ok;
}

/* ---------------------------------------------------------------------------- */
/* VALUE OF A DECLARATION                                                       */
/* ---------------------------------------------------------------------------- */

static boolean r_eval_cache_get(REval *eval, const void *key, RuleforgeValue *out) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(eval->cache_keys); i++) {
    if (nexus_data_list_pointer_at(eval->cache_keys, i) == key) {
      *out = *(const RuleforgeValue *)nexus_data_list_at_const(eval->cache_values, i);

      return TRUE;
    }
  }

  return FALSE;
}

static void r_eval_cache_put(REval *eval, const void *key, RuleforgeValue value) {
  (void)nexus_data_list_append_pointer(eval->cache_keys, key);
  (void)nexus_data_list_append(eval->cache_values, &value);
}

/* Summary: evaluates a ruleset declaration: its core conditions, then its statements, into a fresh ruleset. */
static RuleforgeValue r_eval_ruleset_run(REval *eval, RfRulesetStmt declaration, RuleforgeModule *owner, RuleforgeSpan include_span,
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
  const char                *name;
  RuleforgeDnf               dnf;

  name = ruleforge_eval_text_get(eval, declaration.tree, rf_ruleset_stmt_token_identifier(declaration, 0));

  if (eval->depth >= eval->settings->depth_limit) {
    ruleforge_eval_fail(eval, "RF0391", ruleforge_eval_span_get(eval, declaration.tree, declaration.node), NULL,
                        "rulesets are nested more than %lu deep", (unsigned long)eval->settings->depth_limit);

    return ruleforge_value_none();
  }

  saved_emit       = eval->emit;
  saved_module     = eval->module;
  saved_provenance = eval->provenance;
  saved_cores      = nexus_data_list_length_get(eval->cores);
  saved_variables  = nexus_data_list_length_get(eval->variables);
  saved_base       = eval->frame_base;

  ruleset          = ruleforge_value_ruleset_create(eval->compilation, name);
  eval->emit       = ruleset;
  eval->module     = owner;
  eval->frame_base = (uint32)saved_variables;
  eval->depth++;
  eval->provenance =
      ruleforge_eval_provenance_push(eval, included ? RPK_INCLUDE : RPK_CALL,
                                     included ? include_span : ruleforge_eval_span_get(eval, declaration.tree, declaration.node), name, NULL);

  for (i = 0; i < rf_ruleset_stmt_count_rule_ruleset_item(declaration) && !eval->failed; i++) {
    RfRulesetItem item;

    item = rf_ruleset_stmt_rule_ruleset_item(declaration, i);

    if (rf_ruleset_item_count_rule_description_stmt(item) > 0) {
      continue;
    }

    if (rf_ruleset_item_count_rule_core_conditions_stmt(item) > 0) {
      RfCoreConditionsStmt conditions;

      conditions = rf_ruleset_item_rule_core_conditions_stmt(item, 0);

      for (j = 0; j < rf_core_conditions_stmt_count_rule_expr(conditions) && !eval->failed; j++) {
        RfExpr        expression;
        RuleforgeSpan span;

        expression = rf_core_conditions_stmt_rule_expr(conditions, j);
        span       = ruleforge_eval_span_get(eval, expression.tree, expression.node);

        if (!ruleforge_condition_evaluate(eval, expression, FALSE, &dnf)) {
          break;
        }

        if (!dnf.constant && dnf.count > 1) {
          ruleforge_eval_fail(eval, "RF0360", span, "write one condition per line instead of `or`", "coreConditions cannot contain `or`");
          break;
        }

        if (dnf.constant && !dnf.value) {
          ruleforge_eval_fail(eval, "RF0361", span, NULL, "a core condition is always false, so the ruleset can never match");
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

    (void)r_eval_generator(eval, rf_ruleset_item_rule_generator_stmt(item, 0));
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

static RuleforgeValue r_eval_declaration_value(REval *eval, const RuleforgeDeclaration *declaration, RuleforgeModule *owner, RuleforgeSpan span) {
  RuleforgeValue value;

  if (rf_declaration_count_rule_theme_stmt(declaration->view) > 0) {
    if (!r_eval_cache_get(eval, declaration, &value)) {
      value.kind          = RVK_THEME;
      value.is_identifier = FALSE;
      value.as.theme      = ruleforge_theme_resolve(eval, rf_declaration_rule_theme_stmt(declaration->view, 0), owner);

      if (value.as.theme == NULL) {
        return ruleforge_value_none();
      }

      r_eval_cache_put(eval, declaration, value);
    }

    return value;
  }

  if (rf_declaration_count_rule_dataset_stmt(declaration->view) > 0) {
    if (!r_eval_cache_get(eval, declaration, &value)) {
      value.kind          = RVK_DATASET;
      value.is_identifier = FALSE;
      value.as.dataset    = ruleforge_dataset_load(eval, rf_declaration_rule_dataset_stmt(declaration->view, 0), owner);

      if (value.as.dataset == NULL) {
        return ruleforge_value_none();
      }

      r_eval_cache_put(eval, declaration, value);
    }

    return value;
  }

  if (rf_declaration_count_rule_ruleset_stmt(declaration->view) > 0) {
    return r_eval_ruleset_run(eval, rf_declaration_rule_ruleset_stmt(declaration->view, 0), owner, span, TRUE);
  }

  if (rf_declaration_count_rule_color_set_stmt(declaration->view) > 0) {
    RuleforgeNamespace *space;

    space               = (RuleforgeNamespace *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*space));
    space->is_color_set = TRUE;
    space->color_set    = rf_declaration_rule_color_set_stmt(declaration->view, 0);
    space->owner        = owner;
    value.kind          = RVK_MODULE;
    value.is_identifier = FALSE;
    value.as.module     = space;

    return value;
  }

  ruleforge_eval_fail(eval, "RF0311", span, "call it with arguments, e.g. `Name(...)`", "`%s` is not a value", declaration->name);

  return ruleforge_value_none();
}

/* ---------------------------------------------------------------------------- */
/* REFERENCES                                                                   */
/* ---------------------------------------------------------------------------- */

/* Summary: the names a reference is made of, without the `$` or `@` prefix; returns how many. */
static uint32 r_eval_reference_segments(REval *eval, RfReference view, const char **segments, int64 *out_prefix) {
  uint32 count;
  uint32 i;

  count       = 0;
  *out_prefix = R_REF_PLAIN;

  if (rf_reference_count_token_parameter_reference(view) > 0) {
    segments[count++] = ruleforge_eval_text_get(eval, view.tree, rf_reference_token_parameter_reference(view, 0)) + 1;
    *out_prefix       = R_REF_PARAMETER;
  } else if (rf_reference_count_token_compiler_reference(view) > 0) {
    segments[count++] = ruleforge_eval_text_get(eval, view.tree, rf_reference_token_compiler_reference(view, 0)) + 1;
    *out_prefix       = R_REF_COMPILER;
  }

  for (i = 0; i < rf_reference_count_token_identifier(view) && count < R_EVAL_SEGMENT_LIMIT; i++) {
    segments[count++] = ruleforge_eval_text_get(eval, view.tree, rf_reference_token_identifier(view, i));
  }

  return count;
}

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

  if (base.kind == RVK_MODULE && ((const RuleforgeNamespace *)base.as.module)->is_color_set) {
    const RuleforgeNamespace *space;
    uint32                    i;

    space = (const RuleforgeNamespace *)base.as.module;

    for (i = 0; i < rf_color_set_stmt_count_rule_color_definition(space->color_set); i++) {
      RfColorDefinition definition;

      definition = rf_color_set_stmt_rule_color_definition(space->color_set, i);

      if (nexus_strings_string_equals(ruleforge_eval_text_get(eval, definition.tree, rf_color_definition_token_identifier(definition, 0)), name)) {
        RuleforgeModule *saved_module;
        RuleforgeValue   value;

        saved_module = eval->module;
        eval->module = space->owner;
        value        = ruleforge_eval_expression(eval, rf_color_definition_rule_expr(definition, 0));
        eval->module = saved_module;

        return value;
      }
    }

    ruleforge_eval_fail(eval, "RF0314", span, NULL, "the color set has no color `%s`", name);

    return ruleforge_value_none();
  }

  ruleforge_eval_fail(eval, "RF0315", span, NULL, "a %s has no member `%s`", ruleforge_value_type_name_get(base.kind), name);

  return ruleforge_value_none();
}

static boolean r_eval_compiler_value(REval *eval, const char **segments, uint32 count, RuleforgeSpan span, RuleforgeValue *out) {
  char       key[512];
  uint_large length;
  uint_large i;
  uint32     j;

  length = 0;

  for (j = 0; j < count; j++) {
    length += (uint_large)stbsp_snprintf(key + length, (int)(NEXUS_SIZEOF(key) - length), "%s%s", j > 0 ? "." : "", segments[j]);
  }

  if (eval->settings != NULL) {
    for (i = 0; i < nexus_data_list_length_get(eval->settings->values); i++) {
      const RuleforgeSettingValue *entry;

      entry = (const RuleforgeSettingValue *)nexus_data_list_pointer_at(eval->settings->values, i);

      if (nexus_strings_string_equals(entry->key, key)) {
        *out = entry->value;

        return TRUE;
      }
    }
  }

  /* The character and the atlas are optional by design: an absent value is none, so scripts guard it with `!= none`. */
  if (nexus_strings_string_starts_with(key, "character.") || nexus_strings_string_starts_with(key, "atlas.")) {
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
    ruleforge_eval_fail(eval, "RF0316", span, note, "the compile-time value @%s is not set", key);
  }

  return FALSE;
}

static boolean r_eval_reference_resolve(REval *eval, RfReference view, RuleforgeValue *out_value) {
  const char                 *segments[R_EVAL_SEGMENT_LIMIT];
  uint32                      count;
  uint32                      i;
  int64                       prefix;
  RuleforgeValue              value;
  const RuleforgeDeclaration *declaration;
  RuleforgeModule            *owner;
  const char                 *import;
  RuleforgeSpan               span;

  span  = ruleforge_eval_span_get(eval, view.tree, view.node);
  count = r_eval_reference_segments(eval, view, segments, &prefix);

  if (prefix == R_REF_COMPILER) {
    if (!r_eval_compiler_value(eval, segments, count, span, &value)) {
      return FALSE;
    }

    *out_value = value;

    return TRUE;
  }

  value = ruleforge_value_none();

  for (i = 0; i < count && !eval->failed; i++) {
    if (i == 0) {
      declaration = ruleforge_eval_symbol_find(eval, eval->module, segments[0], &owner);
      import      = r_eval_import_find(eval->module, segments[0]);

      if (prefix == R_REF_PARAMETER) {
        if (!ruleforge_eval_variable_find(eval, segments[0], TRUE, &value)) {
          ruleforge_eval_fail(eval, "RF0318", span, NULL, "`$%s` is not a parameter of the enclosing function or template", segments[0]);

          return FALSE;
        }
      } else if (ruleforge_eval_variable_find(eval, segments[0], FALSE, &value)) {
        /* a local */
      } else if (declaration != NULL) {
        value = r_eval_declaration_value(eval, declaration, owner, span);
      } else if (import != NULL && ruleforge_compilation_module_find(eval->compilation, import) != NULL) {
        RuleforgeNamespace *space;

        space               = (RuleforgeNamespace *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*space));
        space->module       = ruleforge_compilation_module_find(eval->compilation, import);
        value.kind          = RVK_MODULE;
        value.is_identifier = FALSE;
        value.as.module     = space;
      } else if (eval->enum_fallback && count == 1) {
        /* A style property argument such as Red or Circle: an enumerator the manifest validates. */
        value               = ruleforge_value_string(segments[0]);
        value.is_identifier = TRUE;
      } else {
        const char *note;

        note = ruleforge_manifest_condition_find(segments[0]) != NULL
                   ? "game conditions such as this one can only be used in `when`, `coreConditions` and `scope`"
                   : NULL;
        ruleforge_eval_fail(eval, "RF0319", span, note, "unknown name `%s`", segments[0]);

        return FALSE;
      }
    } else if (value.kind == RVK_MODULE && ((const RuleforgeNamespace *)value.as.module)->module != NULL) {
      const RuleforgeNamespace *space;

      space       = (const RuleforgeNamespace *)value.as.module;
      declaration = ruleforge_compilation_declaration_find(space->module, segments[i]);

      if (declaration == NULL || !declaration->exported) {
        ruleforge_eval_fail(eval, "RF0320", span, declaration == NULL ? NULL : "mark the declaration `export` to use it from another module",
                            "module `%s` does not export `%s`", space->module->name, segments[i]);

        return FALSE;
      }

      value = r_eval_declaration_value(eval, declaration, space->module, span);
    } else {
      value = r_eval_member(eval, value, segments[i], span);
    }

    if (eval->failed) {
      return FALSE;
    }
  }

  *out_value = value;

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* EXPRESSIONS                                                                  */
/* ---------------------------------------------------------------------------- */

static RuleforgeValue r_eval_binary(REval *eval, RfExpr view) {
  RuleforgeValue  left;
  RuleforgeValue  right;
  LLexerTokenKind operation;
  f_real          a;
  f_real          b;
  boolean         both_int;
  RuleforgeSpan   span;

  operation = rf_expr_operator_kind(view);
  span      = ruleforge_eval_span_get(eval, view.tree, view.node);
  left      = ruleforge_eval_expression(eval, rf_expr_operand(view, 0));

  if (eval->failed) {
    return ruleforge_value_none();
  }

  /* and / or short-circuit, so `x != none and x >= 80` is safe. */
  if (operation == RF_TOKEN_KW_AND || operation == RF_TOKEN_KW_OR) {
    boolean is_and;

    is_and = operation == RF_TOKEN_KW_AND;

    if (left.kind != RVK_BOOL) {
      ruleforge_eval_fail(eval, "RF0340", span, NULL, "`%s` needs bool operands, got %s", is_and ? "and" : "or",
                          ruleforge_value_type_name_get(left.kind));

      return ruleforge_value_none();
    }

    if ((is_and && !left.as.flag) || (!is_and && left.as.flag)) {
      return left;
    }

    right = ruleforge_eval_expression(eval, rf_expr_operand(view, 1));

    if (!eval->failed && right.kind != RVK_BOOL) {
      ruleforge_eval_fail(eval, "RF0340", span, NULL, "`%s` needs bool operands, got %s", is_and ? "and" : "or",
                          ruleforge_value_type_name_get(right.kind));

      return ruleforge_value_none();
    }

    return right;
  }

  right = ruleforge_eval_expression(eval, rf_expr_operand(view, 1));

  if (eval->failed) {
    return ruleforge_value_none();
  }

  if (operation == RF_TOKEN_EQUALS_EQUALS) {
    return ruleforge_value_flag(ruleforge_value_equals(&left, &right));
  }

  if (operation == RF_TOKEN_NOT_EQUALS) {
    return ruleforge_value_flag(!ruleforge_value_equals(&left, &right));
  }

  if (left.kind == RVK_STRING && right.kind == RVK_STRING) {
    if (operation == RF_TOKEN_PLUS) {
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
      const char *first;
      const char *second;
      int         order;

      first  = left.as.string;
      second = right.as.string;

      while (*first != '\0' && *first == *second) {
        first++;
        second++;
      }

      order = (int)(byte)*first - (int)(byte)*second;

      if (operation == RF_TOKEN_LESS) {
        return ruleforge_value_flag(order < 0);
      }

      if (operation == RF_TOKEN_LESS_EQUAL) {
        return ruleforge_value_flag(order <= 0);
      }

      if (operation == RF_TOKEN_GREATER) {
        return ruleforge_value_flag(order > 0);
      }

      if (operation == RF_TOKEN_GREATER_EQUAL) {
        return ruleforge_value_flag(order >= 0);
      }
    }
  }

  if (!ruleforge_value_number_get(&left, &a) || !ruleforge_value_number_get(&right, &b)) {
    ruleforge_eval_fail(
        eval, "RF0341", span, left.kind == RVK_NONE || right.kind == RVK_NONE ? "an optional value must be checked with `!= none` first" : NULL,
        "this operator cannot be applied to %s and %s", ruleforge_value_type_name_get(left.kind), ruleforge_value_type_name_get(right.kind));

    return ruleforge_value_none();
  }

  both_int = left.kind == RVK_INT && right.kind == RVK_INT;

  if (operation == RF_TOKEN_PLUS) {
    return both_int ? ruleforge_value_integer(left.as.integer + right.as.integer) : ruleforge_value_real(a + b);
  }

  if (operation == RF_TOKEN_MINUS) {
    return both_int ? ruleforge_value_integer(left.as.integer - right.as.integer) : ruleforge_value_real(a - b);
  }

  if (operation == RF_TOKEN_STAR) {
    return both_int ? ruleforge_value_integer(left.as.integer * right.as.integer) : ruleforge_value_real(a * b);
  }

  if (operation == RF_TOKEN_SLASH) {
    if (b == 0.0) {
      ruleforge_eval_fail(eval, "RF0342", span, NULL, "division by zero");

      return ruleforge_value_none();
    }

    return ruleforge_value_real(a / b);
  }

  if (operation == RF_TOKEN_LESS) {
    return ruleforge_value_flag(a < b);
  }

  if (operation == RF_TOKEN_LESS_EQUAL) {
    return ruleforge_value_flag(a <= b);
  }

  if (operation == RF_TOKEN_GREATER) {
    return ruleforge_value_flag(a > b);
  }

  return ruleforge_value_flag(a >= b);
}

/* Summary: the unescaped value of a string literal token ("..." with backslash escapes). */
static const char *r_eval_string_unescape(REval *eval, const LParserTree *tree, const LParserGreenNode *token) {
  const char *raw;
  uint_large  size;
  char       *result;
  uint_large  i;
  uint_large  length;

  raw    = ruleforge_eval_text_get(eval, tree, token);
  size   = nexus_strings_string_length_get(raw);
  result = (char *)nexus_data_arena_allocate(&eval->compilation->arena, size + 1);
  length = 0;

  for (i = 1; i + 1 < size; i++) {
    if (raw[i] == '\\' && i + 2 < size) {
      i++;

      if (raw[i] == 'n') {
        result[length++] = '\n';
      } else if (raw[i] == 't') {
        result[length++] = '\t';
      } else if (raw[i] == 'r') {
        result[length++] = '\r';
      } else {
        result[length++] = raw[i];
      }
    } else {
      result[length++] = raw[i];
    }
  }

  result[length] = '\0';

  return result;
}

/* Summary: the value of a string literal token; the unescaped text is made once and found again through the token. */
static const char *r_eval_string_value(REval *eval, const LParserTree *tree, const LParserGreenNode *token) {
  void       *found;
  const char *text;

  found = nexus_data_hashmap_get_pointer(eval->compilation->strings, (const void *)&token);

  if (found != NULL) {
    return (const char *)found;
  }

  text = r_eval_string_unescape(eval, tree, token);
  nexus_data_hashmap_put_pointer(eval->compilation->strings, (const void *)&token, text);

  return text;
}

static RuleforgeValue r_eval_arguments_list(REval *eval, RfArguments arguments) {
  RuleforgeList *list;
  uint32         i;

  list = ruleforge_value_list_create(eval->compilation, rf_arguments_count_rule_argument(arguments));

  for (i = 0; i < list->count && !eval->failed; i++) {
    list->items[i] = ruleforge_eval_expression(eval, rf_argument_rule_expr(rf_arguments_rule_argument(arguments, i), 0));
  }

  return ruleforge_value_list(list);
}

static RuleforgeValue r_eval_atom(REval *eval, RfAtom view) {
  RuleforgeValue value;
  RuleforgeSpan  span;
  uint_large     size;
  uint_large     i;

  span = ruleforge_eval_span_get(eval, view.tree, view.node);

  if (rf_atom_count_rule_expr(view) > 0) {
    return ruleforge_eval_expression(eval, rf_atom_rule_expr(view, 0));
  }

  if (rf_atom_count_rule_color_literal(view) > 0) {
    boolean has_alpha;
    uint32  color;

    color = ruleforge_color_value_get(rf_atom_rule_color_literal(view, 0), &has_alpha);

    return ruleforge_value_color(color);
  }

  if (rf_atom_count_rule_list_literal(view) > 0) {
    return r_eval_arguments_list(eval, rf_list_literal_rule_arguments(rf_atom_rule_list_literal(view, 0), 0));
  }

  if (rf_atom_count_rule_call(view) > 0) {
    return r_eval_call(eval, rf_atom_rule_call(view, 0));
  }

  if (rf_atom_count_rule_indexed_reference(view) > 0) {
    RfIndexedReference indexed;
    RuleforgeValue     operand;

    indexed = rf_atom_rule_indexed_reference(view, 0);

    if (!r_eval_reference_resolve(eval, rf_indexed_reference_rule_reference(indexed, 0), &value)) {
      return ruleforge_value_none();
    }

    for (i = 0; i < rf_indexed_reference_count_rule_expr(indexed) && !eval->failed; i++) {
      operand = ruleforge_eval_expression(eval, rf_indexed_reference_rule_expr(indexed, (uint32)i));

      if (eval->failed) {
        break;
      }

      if (value.kind != RVK_LIST || operand.kind != RVK_INT) {
        ruleforge_eval_fail(eval, "RF0343", span, NULL, "only a list can be indexed, with an int");

        return ruleforge_value_none();
      }

      if (operand.as.integer < 0 || (uint64)operand.as.integer >= value.as.list->count) {
        ruleforge_eval_fail(eval, "RF0344", span, NULL, "index %lld is outside the list of %lu", (long long)operand.as.integer,
                            (unsigned long)value.as.list->count);

        return ruleforge_value_none();
      }

      value = value.as.list->items[operand.as.integer];
    }

    return value;
  }

  if (rf_atom_count_token_string_literal(view) > 0) {
    return ruleforge_value_string(r_eval_string_value(eval, view.tree, rf_atom_token_string_literal(view, 0)));
  }

  if (rf_atom_count_token_integer_literal(view) > 0) {
    const char *digits;
    int64       integer;

    digits  = rf_token_text(view.tree, rf_atom_token_integer_literal(view, 0), &size);
    integer = 0;

    for (i = 0; i < size; i++) {
      integer = (integer * 10) + (int64)(digits[i] - '0');
    }

    return ruleforge_value_integer(integer);
  }

  if (rf_atom_count_token_float_literal(view) > 0) {
    const char *digits;
    f_real      number;
    f_real      scale;
    boolean     fraction;

    digits   = rf_token_text(view.tree, rf_atom_token_float_literal(view, 0), &size);
    number   = 0.0;
    scale    = 1.0;
    fraction = FALSE;

    for (i = 0; i < size; i++) {
      if (digits[i] == '.') {
        fraction = TRUE;
      } else if (fraction) {
        scale *= 0.1;
        number += (f_real)(digits[i] - '0') * scale;
      } else {
        number = (number * 10.0) + (f_real)(digits[i] - '0');
      }
    }

    return ruleforge_value_real(number);
  }

  if (rf_atom_count_token_kw_true(view) > 0) {
    return ruleforge_value_flag(TRUE);
  }

  if (rf_atom_count_token_kw_false(view) > 0) {
    return ruleforge_value_flag(FALSE);
  }

  return ruleforge_value_none();
}

RuleforgeValue ruleforge_eval_expression(REval *eval, RfExpr view) {
  RuleforgeValue  operand;
  LLexerTokenKind operation;
  uint32          count;

  if (!r_eval_step(eval, ruleforge_eval_span_get(eval, view.tree, view.node))) {
    return ruleforge_value_none();
  }

  count = rf_expr_operand_count(view);

  if (count == 0) {
    return r_eval_atom(eval, rf_expr_atom(view));
  }

  if (count == 2) {
    return r_eval_binary(eval, view);
  }

  /* A prefix operation: `not` or `-`. */
  operation = rf_expr_operator_kind(view);
  operand   = ruleforge_eval_expression(eval, rf_expr_operand(view, 0));

  if (eval->failed) {
    return ruleforge_value_none();
  }

  if (operation == RF_TOKEN_KW_NOT) {
    if (operand.kind != RVK_BOOL) {
      ruleforge_eval_fail(eval, "RF0340", ruleforge_eval_span_get(eval, view.tree, view.node), NULL, "`not` needs a bool, got %s",
                          ruleforge_value_type_name_get(operand.kind));

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

  ruleforge_eval_fail(eval, "RF0341", ruleforge_eval_span_get(eval, view.tree, view.node), NULL, "`-` needs a number, got %s",
                      ruleforge_value_type_name_get(operand.kind));

  return ruleforge_value_none();
}

boolean ruleforge_eval_boolean_get(REval *eval, RfExpr view, boolean *out_flag) {
  RuleforgeValue value;

  value = ruleforge_eval_expression(eval, view);

  if (eval->failed) {
    return FALSE;
  }

  if (value.kind != RVK_BOOL) {
    ruleforge_eval_fail(eval, "RF0332", ruleforge_eval_span_get(eval, view.tree, view.node), "optional values must be compared, e.g. `x != none`",
                        "a condition must be a bool, got %s", ruleforge_value_type_name_get(value.kind));

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

static boolean r_eval_arguments_collect(REval *eval, RfArguments arguments, RCallArguments *out) {
  uint32 i;

  out->count  = rf_arguments_count_rule_argument(arguments);
  out->values = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)out->count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
  out->names  = (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)out->count + 1U) * NEXUS_SIZEOF(const char *));

  for (i = 0; i < out->count; i++) {
    RfArgument argument;

    argument       = rf_arguments_rule_argument(arguments, i);
    out->names[i]  = rf_argument_count_token_identifier(argument) > 0
                         ? ruleforge_eval_text_get(eval, argument.tree, rf_argument_token_identifier(argument, 0))
                         : NULL;
    out->values[i] = ruleforge_eval_expression(eval, rf_argument_rule_expr(argument, 0));

    if (eval->failed) {
      return FALSE;
    }
  }

  return TRUE;
}

/* Summary: binds call arguments to a parameter list into `bound`; reports arity and name errors. */
static boolean r_eval_arguments_bind(REval *eval, RuleforgeSpan span, const char *callee, const char *const *parameter_names,
                                     const RfType *parameter_types, uint32 parameter_count, uint32 minimum, const RCallArguments *arguments,
                                     RuleforgeValue *bound, boolean *given) {
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

      if (parameter_types != NULL && rf_type_count_token_question(parameter_types[i]) > 0) {
        continue;
      }

      if (parameter_types == NULL && i >= minimum) {
        continue;
      }

      ruleforge_eval_fail(eval, "RF0325", span, NULL, "`%s` needs the argument `%s`", callee, parameter_names[i]);

      return FALSE;
    }

    if (parameter_types != NULL && !ruleforge_eval_type_check(eval, &bound[i], parameter_types[i], span, parameter_names[i])) {
      return FALSE;
    }
  }

  return TRUE;
}

/* Summary: the parameter names and types of a function or template, read through the views. */
static void r_eval_parameters_get(REval *eval, RfParameters parameters, const char ***out_names, RfType **out_types, uint32 *out_count) {
  uint32 i;

  *out_count = rf_parameters_count_rule_parameter(parameters);
  *out_names = (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)*out_count + 1U) * NEXUS_SIZEOF(const char *));
  *out_types = (RfType *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)*out_count + 1U) * NEXUS_SIZEOF(RfType));

  for (i = 0; i < *out_count; i++) {
    RfParameter parameter;

    parameter       = rf_parameters_rule_parameter(parameters, i);
    (*out_names)[i] = ruleforge_eval_text_get(eval, parameter.tree, rf_parameter_token_identifier(parameter, 0));
    (*out_types)[i] = rf_parameter_rule_type(parameter, 0);
  }
}

/* Summary: runs a function declaration with bound arguments and returns its value (the accumulated ruleset for `-> ruleset`). */
static RuleforgeValue r_eval_function_run(REval *eval, RfFunctionStmt function, RuleforgeModule *owner, const RuleforgeValue *bound,
                                          const char *const *names, uint32 parameter_count, RuleforgeSpan call_span) {
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
  const char                *function_name;
  boolean                    returns_ruleset;
  boolean                    returns_void;
  boolean                    has_return_type;

  if (eval->depth >= eval->settings->depth_limit) {
    ruleforge_eval_fail(eval, "RF0391", call_span, "recursion is bounded; raise [limits] depth if it is intended",
                        "calls are nested more than %lu deep", (unsigned long)eval->settings->depth_limit);

    return ruleforge_value_none();
  }

  function_name   = ruleforge_eval_text_get(eval, function.tree, rf_function_stmt_token_identifier(function, 0));
  has_return_type = rf_function_stmt_count_rule_type(function) > 0;
  returns_ruleset = has_return_type && ruleforge_eval_type_kind_get(rf_function_stmt_rule_type(function, 0)) == RTK_RULESET;
  returns_void    = has_return_type && ruleforge_eval_type_kind_get(rf_function_stmt_rule_type(function, 0)) == RTK_VOID;

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
  eval->provenance = ruleforge_eval_provenance_push(eval, RPK_CALL, call_span, function_name, NULL);

  ruleset    = NULL;
  eval->emit = NULL;

  if (returns_ruleset) {
    ruleset    = ruleforge_value_ruleset_create(eval->compilation, function_name);
    eval->emit = ruleset;
  }

  for (i = 0; i < parameter_count; i++) {
    r_eval_variable_push(eval, names[i], bound[i], FALSE, TRUE);
  }

  for (i = 0; i < rf_function_stmt_count_rule_generator_stmt(function) && !eval->failed && !eval->returning; i++) {
    (void)r_eval_generator(eval, rf_function_stmt_rule_generator_stmt(function, i));
  }

  result = ruleforge_value_none();

  if (!eval->failed) {
    if (ruleset != NULL) {
      result = eval->returning && eval->result.kind == RVK_RULESET ? eval->result : ruleforge_value_ruleset(ruleset);
    } else if (eval->returning) {
      result = eval->result;
    }

    if (has_return_type && !returns_void && !returns_ruleset) {
      if (!eval->returning) {
        ruleforge_eval_fail(eval, "RF0333", ruleforge_eval_span_get(eval, function.tree, function.node), NULL, "function `%s` must return a %s",
                            function_name, ruleforge_eval_type_name_get(eval, rf_function_stmt_rule_type(function, 0)));
      } else {
        (void)ruleforge_eval_type_check(eval, &result, rf_function_stmt_rule_type(function, 0),
                                        ruleforge_eval_span_get(eval, function.tree, function.node), "the returned value");
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

/* Summary: the dotted name of a reference written as segments, in a buffer. */
static uint_large r_eval_callee_write(const char **segments, uint32 count, char *buffer, uint_large size) {
  uint_large length;
  uint32     i;

  length = 0;

  for (i = 0; i < count; i++) {
    length += (uint_large)stbsp_snprintf(buffer + length, (int)(size - length), "%s%s", i > 0 ? "." : "", segments[i]);
  }

  return length;
}

static RuleforgeValue r_eval_call(REval *eval, RfCall call) {
  const char                 *segments[R_EVAL_SEGMENT_LIMIT];
  uint32                      segment_count;
  int64                       prefix;
  char                        callee[512];
  const RuleforgeNative      *native;
  const RuleforgeDeclaration *declaration;
  RuleforgeModule            *owner;
  RCallArguments              arguments;
  RuleforgeValue             *bound;
  boolean                    *given;
  RuleforgeSpan               span;
  uint_large                  length;
  const char                 *dot;
  char                        module_name[256];

  span          = ruleforge_eval_span_get(eval, call.tree, call.node);
  segment_count = r_eval_reference_segments(eval, rf_call_rule_reference(call, 0), segments, &prefix);
  native        = NULL;
  declaration   = NULL;
  owner         = NULL;
  (void)r_eval_callee_write(segments, segment_count, callee, NEXUS_SIZEOF(callee));

  if (!r_eval_arguments_collect(eval, rf_call_rule_arguments(call, 0), &arguments)) {
    return ruleforge_value_none();
  }

  dot = nexus_strings_string_find_last(callee, ".");

  if (dot != NULL) {
    const char *import;

    length = (uint_large)(dot - callee);

    if (length < NEXUS_SIZEOF(module_name)) {
      nexus_memory_bytes_copy(callee, module_name, length);
      module_name[length] = '\0';
      import              = r_eval_import_find(eval->module, module_name);

      if (import != NULL) {
        native = ruleforge_stdlib_native_find(import, dot + 1);
      } else if (ruleforge_stdlib_module_exists(module_name)) {
        native = ruleforge_stdlib_native_find(module_name, dot + 1);
      }
    }
  }

  if (native == NULL) {
    declaration = ruleforge_eval_symbol_find(eval, eval->module, callee, &owner);
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

    if (!r_eval_arguments_bind(eval, span, callee, names, NULL, parameter_count, native->minimum, &arguments, bound, given)) {
      return ruleforge_value_none();
    }

    return native->function(eval, span, bound, parameter_count);
  }

  if (declaration == NULL || rf_declaration_count_rule_function_stmt(declaration->view) == 0) {
    ruleforge_eval_fail(eval, "RF0326", span,
                        declaration != NULL && rf_declaration_count_rule_template_stmt(declaration->view) > 0
                            ? "a template is used as `rule name = Template(...);`"
                            : NULL,
                        "`%s` is not a function", callee);

    return ruleforge_value_none();
  }

  {
    RfFunctionStmt function;
    const char   **names;
    RfType        *types;
    uint32         parameter_count;

    function = rf_declaration_rule_function_stmt(declaration->view, 0);
    r_eval_parameters_get(eval, rf_function_stmt_rule_parameters(function, 0), &names, &types, &parameter_count);
    bound = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameter_count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
    given = (boolean *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameter_count + 1U) * NEXUS_SIZEOF(boolean));

    if (!r_eval_arguments_bind(eval, span, callee, names, types, parameter_count, parameter_count, &arguments, bound, given)) {
      return ruleforge_value_none();
    }

    return r_eval_function_run(eval, function, owner, bound, names, parameter_count, span);
  }
}

/* ---------------------------------------------------------------------------- */
/* STYLES                                                                       */
/* ---------------------------------------------------------------------------- */

RuleforgeStyle *ruleforge_eval_style_get(REval *eval, RfExpr view) {
  RuleforgeValue value;
  RfAtom         atom;

  /* A bare name that is not a variable or declaration is a style key of the default theme. */
  if (rf_expr_operand_count(view) == 0 && eval->default_theme != NULL) {
    atom = rf_expr_atom(view);

    if (rf_atom_count_rule_indexed_reference(atom) > 0) {
      RfReference reference;

      reference = rf_indexed_reference_rule_reference(rf_atom_rule_indexed_reference(atom, 0), 0);

      if (rf_reference_count_token_identifier(reference) == 1 && rf_reference_count_token_parameter_reference(reference) == 0 &&
          rf_reference_count_token_compiler_reference(reference) == 0) {
        const char      *name;
        RuleforgeModule *owner;

        name = ruleforge_eval_text_get(eval, reference.tree, rf_reference_token_identifier(reference, 0));

        if (!ruleforge_eval_variable_find(eval, name, FALSE, &value) && ruleforge_eval_symbol_find(eval, eval->module, name, &owner) == NULL) {
          RuleforgeStyle *style;

          style = ruleforge_theme_style_get(eval->default_theme, name);

          if (style == NULL) {
            ruleforge_eval_fail(eval, "RF0313", ruleforge_eval_span_get(eval, view.tree, view.node), NULL, "the default theme `%s` has no style `%s`",
                                eval->default_theme->name, name);
          }

          return style;
        }
      }
    }
  }

  value = ruleforge_eval_expression(eval, view);

  if (eval->failed) {
    return NULL;
  }

  if (value.kind != RVK_STYLE) {
    ruleforge_eval_fail(eval, "RF0334", ruleforge_eval_span_get(eval, view.tree, view.node), "a style is written `Theme.key`",
                        "`style` needs a style, got %s", ruleforge_value_type_name_get(value.kind));

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

static boolean r_eval_command_apply(REval *eval, RfRuleCommand command, boolean baseline, boolean *has_action, boolean *show, boolean *inherit,
                                    RuleforgeStyle **style) {
  if (rf_rule_command_count_rule_action_stmt(command) > 0) {
    *has_action = TRUE;
    *show       = rf_action_stmt_count_token_kw_show(rf_rule_command_rule_action_stmt(command, 0)) > 0;

    return TRUE;
  }

  if (rf_rule_command_count_rule_inherit_stmt(command) > 0) {
    if (baseline) {
      ruleforge_eval_fail(eval, "RF0370", ruleforge_eval_span_get(eval, command.tree, command.node),
                          "`inherit action` is for variants, which can inherit the baseline's action", "the baseline has nothing to inherit");

      return FALSE;
    }

    *inherit = TRUE;

    return TRUE;
  }

  *style = ruleforge_eval_style_get(eval, rf_style_stmt_rule_expr(rf_rule_command_rule_style_stmt(command, 0), 0));

  return *style != NULL;
}

/* Summary: applies one item of a rule body (scope, baseline, variants, a bare command) to the builder. */
static boolean r_eval_rule_item(REval *eval, RRuleBuilder *builder, RfRuleBodyItem item) {
  RuleforgeRule *rule;
  uint32         j;
  uint32         k;

  rule = builder->rule;

  if (rf_rule_body_item_count_rule_scope_stmt(item) > 0) {
    RfScopeStmt scope;

    scope = rf_rule_body_item_rule_scope_stmt(item, 0);

    for (j = 0; j < rf_scope_stmt_count_rule_scope_assignment(scope); j++) {
      RuleforgeCondition *condition;

      if (!ruleforge_condition_scope_build(eval, rf_scope_stmt_rule_scope_assignment(scope, j), &condition)) {
        return FALSE;
      }

      if (condition != NULL) {
        (void)nexus_data_list_append_pointer(builder->scope, condition);
      }
    }

    return TRUE;
  }

  if (rf_rule_body_item_count_rule_baseline_stmt(item) > 0) {
    RfBaselineStmt baseline;

    baseline = rf_rule_body_item_rule_baseline_stmt(item, 0);

    for (j = 0; j < rf_baseline_stmt_count_rule_rule_command(baseline); j++) {
      boolean inherit;

      inherit = FALSE;

      if (!r_eval_command_apply(eval, rf_baseline_stmt_rule_rule_command(baseline, j), TRUE, &rule->has_action, &rule->show, &inherit,
                                &rule->style)) {
        return FALSE;
      }
    }

    return TRUE;
  }

  if (rf_rule_body_item_count_rule_variants_stmt(item) > 0) {
    RfVariantsStmt variants;

    variants = rf_rule_body_item_rule_variants_stmt(item, 0);

    for (j = 0; j < rf_variants_stmt_count_rule_when_stmt(variants) && !eval->failed; j++) {
      RfWhenStmt       when;
      RuleforgeDnf     dnf;
      RuleforgeVariant variant;

      when = rf_variants_stmt_rule_when_stmt(variants, j);

      if (builder->stop) {
        rule->shadowed++;

        continue;
      }

      if (!ruleforge_condition_evaluate(eval, rf_when_stmt_rule_expr(when, 0), FALSE, &dnf)) {
        return FALSE;
      }

      /* A compile-time false condition removes the variant before emission. */
      if (dnf.constant && !dnf.value) {
        continue;
      }

      nexus_memory_bytes_set(&variant, NEXUS_SIZEOF(variant), 0);
      variant.span = ruleforge_eval_span_get(eval, when.tree, when.node);

      if (dnf.constant) {
        variant.terminal = TRUE;
      } else {
        variant.clause_count = dnf.count;
        variant.clauses      = dnf.clauses;
      }

      for (k = 0; k < rf_when_stmt_count_rule_rule_command(when); k++) {
        if (!r_eval_command_apply(eval, rf_when_stmt_rule_rule_command(when, k), FALSE, &variant.has_action, &variant.show, &variant.inherit_action,
                                  &variant.style)) {
          return FALSE;
        }
      }

      if (!variant.has_action && !variant.inherit_action) {
        ruleforge_eval_fail(eval, "RF0371", variant.span, "write `action show;`, `action hide;` or `inherit action;`",
                            "a variant must set the action or inherit it");

        return FALSE;
      }

      (void)nexus_data_list_append(builder->variants, &variant);

      if (variant.terminal) {
        builder->stop = TRUE;
      }
    }

    return TRUE;
  }

  {
    boolean inherit;

    inherit = FALSE;

    return r_eval_command_apply(eval, rf_rule_body_item_rule_rule_command(item, 0), TRUE, &rule->has_action, &rule->show, &inherit, &rule->style);
  }
}

/* Summary: builds the immutable rule of a `rule` statement (inline body or template use) and appends it to the current ruleset. */
static boolean r_eval_rule(REval *eval, RfRuleStmt view) {
  RRuleBuilder               builder;
  RuleforgeRule             *rule;
  uint32                     i;
  uint_large                 saved_variables;
  uint32                     saved_base;
  const RuleforgeProvenance *saved_provenance;
  RuleforgeSpan              span;
  const char                *name;

  span = ruleforge_eval_span_get(eval, view.tree, view.node);

  if (eval->emit == NULL) {
    ruleforge_eval_fail(eval, "RF0372", span, "rules can only be generated in a ruleset or in a function that returns `ruleset`",
                        "this `rule` is outside any ruleset");

    return FALSE;
  }

  if (eval->settings != NULL && eval->emitted >= eval->settings->rule_limit) {
    ruleforge_eval_fail(eval, "RF0392", span, "raise [limits] rules in the settings if this many rules are intended",
                        "the limit of %lu generated rules is reached", (unsigned long)eval->settings->rule_limit);

    return FALSE;
  }

  name = rf_rule_stmt_count_token_identifier(view) > 0 ? ruleforge_eval_text_get(eval, view.tree, rf_rule_stmt_token_identifier(view, 0)) : NULL;
  rule = (RuleforgeRule *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*rule));
  rule->name       = name;
  rule->span       = span;
  builder.rule     = rule;
  builder.scope    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  builder.variants = nexus_data_list_create(NEXUS_SIZEOF(RuleforgeVariant), 4);
  builder.stop     = FALSE;

  saved_provenance = eval->provenance;
  saved_variables  = nexus_data_list_length_get(eval->variables);
  saved_base       = eval->frame_base;

  if (rf_rule_stmt_count_rule_call(view) > 0) {
    /* rule Name = Template(args); */
    RfCall                      call;
    const RuleforgeDeclaration *declaration;
    RuleforgeModule            *owner;
    RCallArguments              arguments;
    RuleforgeValue             *bound;
    boolean                    *given;
    const char                **names;
    RfType                     *types;
    uint32                      parameter_count;
    RuleforgeModule            *saved_module;
    RfTemplateStmt              template_stmt;
    const char                 *callee_segments[R_EVAL_SEGMENT_LIMIT];
    int64                       prefix;
    char                        callee[512];
    uint32                      segment_count;
    const char                 *template_name;
    RuleforgeSpan               call_span;

    call          = rf_rule_stmt_rule_call(view, 0);
    call_span     = ruleforge_eval_span_get(eval, call.tree, call.node);
    segment_count = r_eval_reference_segments(eval, rf_call_rule_reference(call, 0), callee_segments, &prefix);
    (void)r_eval_callee_write(callee_segments, segment_count, callee, NEXUS_SIZEOF(callee));
    declaration = ruleforge_eval_symbol_find(eval, eval->module, callee, &owner);

    if (declaration == NULL || rf_declaration_count_rule_template_stmt(declaration->view) == 0) {
      ruleforge_eval_fail(eval, "RF0373", call_span, NULL, "`%s` is not a template", callee);
      nexus_data_list_destroy(builder.scope);
      nexus_data_list_destroy(builder.variants);

      return FALSE;
    }

    if (!r_eval_arguments_collect(eval, rf_call_rule_arguments(call, 0), &arguments)) {
      nexus_data_list_destroy(builder.scope);
      nexus_data_list_destroy(builder.variants);

      return FALSE;
    }

    template_stmt = rf_declaration_rule_template_stmt(declaration->view, 0);
    template_name = ruleforge_eval_text_get(eval, template_stmt.tree, rf_template_stmt_token_identifier(template_stmt, 0));
    r_eval_parameters_get(eval, rf_template_stmt_rule_parameters(template_stmt, 0), &names, &types, &parameter_count);
    bound = (RuleforgeValue *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameter_count + 1U) * NEXUS_SIZEOF(RuleforgeValue));
    given = (boolean *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)parameter_count + 1U) * NEXUS_SIZEOF(boolean));

    if (!r_eval_arguments_bind(eval, call_span, callee, names, types, parameter_count, parameter_count, &arguments, bound, given)) {
      nexus_data_list_destroy(builder.scope);
      nexus_data_list_destroy(builder.variants);

      return FALSE;
    }

    if (rule->name == NULL) {
      rule->name = template_name;
    }

    saved_module     = eval->module;
    eval->module     = owner;
    eval->frame_base = (uint32)saved_variables;
    eval->provenance = ruleforge_eval_provenance_push(eval, RPK_TEMPLATE, call_span, template_name, NULL);

    for (i = 0; i < parameter_count; i++) {
      r_eval_variable_push(eval, names[i], bound[i], FALSE, TRUE);
    }

    for (i = 0; i < rf_template_stmt_count_rule_rule_body_item(template_stmt) && !eval->failed; i++) {
      (void)r_eval_rule_item(eval, &builder, rf_template_stmt_rule_rule_body_item(template_stmt, i));
    }

    eval->module = saved_module;
  } else {
    eval->provenance = ruleforge_eval_provenance_push(eval, RPK_DECLARATION, span, name == NULL ? "" : name, NULL);

    for (i = 0; i < rf_rule_stmt_count_rule_rule_body_item(view) && !eval->failed; i++) {
      (void)r_eval_rule_item(eval, &builder, rf_rule_stmt_rule_rule_body_item(view, i));
    }
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
    ruleforge_eval_fail(eval, "RF0375", span, "add `action show;` or `action hide;`", "rule `%s` has no action",
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

static boolean r_eval_if(REval *eval, RfIfStmt view) {
  boolean    condition;
  uint_large else_start;
  uint_large else_end;
  uint_large start;
  uint_large end;
  uint32     i;
  uint_large saved_variables;
  boolean    has_else;

  if (!ruleforge_eval_boolean_get(eval, rf_if_stmt_rule_expr(view, 0), &condition)) {
    return FALSE;
  }

  has_else   = rf_if_stmt_count_token_kw_else(view) > 0;
  else_start = 0;
  else_end   = 0;

  if (has_else) {
    (void)rf_node_range(view.tree, rf_if_stmt_token_kw_else(view, 0), &else_start, &else_end);
  }

  saved_variables = nexus_data_list_length_get(eval->variables);

  /* The view lists the statements of both branches together; the `else` token tells them apart by position. */
  for (i = 0; i < rf_if_stmt_count_rule_generator_stmt(view) && !eval->failed && !eval->returning; i++) {
    RfGeneratorStmt statement;
    boolean         in_then;

    statement = rf_if_stmt_rule_generator_stmt(view, i);
    start     = 0;
    end       = 0;
    (void)rf_node_range(view.tree, statement.node, &start, &end);
    in_then = !has_else || start < else_start;

    if (in_then == condition) {
      (void)r_eval_generator(eval, statement);
    }
  }

  r_eval_variables_truncate(eval, saved_variables);

  if (!condition && rf_if_stmt_count_rule_if_stmt(view) > 0 && !eval->failed && !eval->returning) {
    return r_eval_if(eval, rf_if_stmt_rule_if_stmt(view, 0));
  }

  return !eval->failed;
}

static boolean r_eval_generator(REval *eval, RfGeneratorStmt view) {
  RuleforgeValue value;
  uint32         i;
  RuleforgeSpan  span;

  span = ruleforge_eval_span_get(eval, view.tree, view.node);

  if (!r_eval_step(eval, span)) {
    return FALSE;
  }

  if (rf_generator_stmt_count_rule_variable_stmt(view) > 0) {
    RfVariableStmt variable;
    const char    *name;
    RuleforgeValue existing;

    variable = rf_generator_stmt_rule_variable_stmt(view, 0);
    name     = ruleforge_eval_text_get(eval, variable.tree, rf_variable_stmt_token_identifier(variable, 0));
    value    = ruleforge_eval_expression(eval, rf_variable_stmt_rule_expr(variable, 0));

    if (eval->failed) {
      return FALSE;
    }

    if (rf_variable_stmt_count_rule_type(variable) > 0 &&
        !ruleforge_eval_type_check(eval, &value, rf_variable_stmt_rule_type(variable, 0), span, name)) {
      return FALSE;
    }

    if (ruleforge_eval_variable_find(eval, name, FALSE, &existing)) {
      ruleforge_eval_fail(eval, "RF0335", span, NULL, "`%s` is already declared in this function", name);

      return FALSE;
    }

    r_eval_variable_push(eval, name, value, rf_variable_stmt_count_token_kw_var(variable) > 0, FALSE);

    return TRUE;
  }

  if (rf_generator_stmt_count_rule_assignment_stmt(view) > 0) {
    RfAssignmentStmt   assignment;
    const char        *name;
    uint_large         index;
    RuleforgeVariable *variable;

    assignment = rf_generator_stmt_rule_assignment_stmt(view, 0);
    name       = ruleforge_eval_text_get(eval, assignment.tree, rf_assignment_stmt_token_identifier(assignment, 0));
    value      = ruleforge_eval_expression(eval, rf_assignment_stmt_rule_expr(assignment, 0));

    if (eval->failed) {
      return FALSE;
    }

    for (index = nexus_data_list_length_get(eval->variables); index > eval->frame_base; index--) {
      variable = (RuleforgeVariable *)nexus_data_list_at(eval->variables, index - 1);

      if (!variable->parameter && nexus_strings_string_equals(variable->name, name)) {
        if (!variable->mutable_flag) {
          ruleforge_eval_fail(eval, "RF0336", span, "declare it with `var` to reassign it", "`%s` is immutable", name);

          return FALSE;
        }

        if (value.kind == RVK_INT && variable->value.kind == RVK_FLOAT) {
          value = ruleforge_value_real((f_real)value.as.integer);
        }

        if (value.kind != variable->value.kind && variable->value.kind != RVK_NONE && value.kind != RVK_NONE) {
          ruleforge_eval_fail(eval, "RF0337", span, NULL, "`%s` holds a %s; it cannot become a %s", name,
                              ruleforge_value_type_name_get(variable->value.kind), ruleforge_value_type_name_get(value.kind));

          return FALSE;
        }

        variable->value = value;

        return TRUE;
      }
    }

    ruleforge_eval_fail(eval, "RF0338", span, NULL, "`%s` is not a variable", name);

    return FALSE;
  }

  if (rf_generator_stmt_count_rule_if_stmt(view) > 0) {
    return r_eval_if(eval, rf_generator_stmt_rule_if_stmt(view, 0));
  }

  if (rf_generator_stmt_count_rule_for_stmt(view) > 0) {
    RfForStmt                  loop;
    RuleforgeValue             iterable;
    uint32                     count;
    uint_large                 saved_variables;
    const RuleforgeProvenance *saved_provenance;
    const char                *variable_name;

    loop          = rf_generator_stmt_rule_for_stmt(view, 0);
    variable_name = ruleforge_eval_text_get(eval, loop.tree, rf_for_stmt_token_identifier(loop, 0));
    iterable      = ruleforge_eval_expression(eval, rf_for_stmt_rule_expr(loop, 0));

    if (eval->failed) {
      return FALSE;
    }

    if (iterable.kind != RVK_LIST && iterable.kind != RVK_DATASET) {
      ruleforge_eval_fail(eval, "RF0345", span, NULL, "`for` needs a list or a dataset, got %s", ruleforge_value_type_name_get(iterable.kind));

      return FALSE;
    }

    count            = iterable.kind == RVK_LIST ? iterable.as.list->count : iterable.as.dataset->count;
    saved_variables  = nexus_data_list_length_get(eval->variables);
    saved_provenance = eval->provenance;

    for (i = 0; i < count && !eval->failed && !eval->returning; i++) {
      RuleforgeValue item;
      boolean        keep;
      uint32         k;

      /* An empty body still costs a step, so no loop is free. */
      if (!r_eval_step(eval, span)) {
        break;
      }

      if (iterable.kind == RVK_LIST) {
        item = iterable.as.list->items[i];
      } else {
        item.kind          = RVK_RECORD;
        item.is_identifier = FALSE;
        item.as.record     = iterable.as.dataset->rows[i];
      }

      r_eval_variable_push(eval, variable_name, item, FALSE, FALSE);

      if (item.kind == RVK_RECORD && item.as.record->dataset != 0xFFFFFFFFU) {
        eval->provenance =
            ruleforge_eval_provenance_push(eval, RPK_ROW, span, iterable.kind == RVK_DATASET ? iterable.as.dataset->name : "", item.as.record);
      }

      keep = TRUE;

      /* `where` is the loop's second expression. */
      if (rf_for_stmt_count_token_kw_where(loop) > 0 && !ruleforge_eval_boolean_get(eval, rf_for_stmt_rule_expr(loop, 1), &keep)) {
        break;
      }

      if (keep) {
        uint_large body_variables;

        body_variables = nexus_data_list_length_get(eval->variables);

        for (k = 0; k < rf_for_stmt_count_rule_generator_stmt(loop) && !eval->failed && !eval->returning; k++) {
          (void)r_eval_generator(eval, rf_for_stmt_rule_generator_stmt(loop, k));
        }

        r_eval_variables_truncate(eval, body_variables);
      }

      eval->provenance = saved_provenance;
      r_eval_variables_truncate(eval, saved_variables);
    }

    eval->provenance = saved_provenance;
    r_eval_variables_truncate(eval, saved_variables);

    return !eval->failed;
  }

  if (rf_generator_stmt_count_rule_return_stmt(view) > 0) {
    RfReturnStmt result;

    result = rf_generator_stmt_rule_return_stmt(view, 0);

    if (rf_return_stmt_count_rule_expr(result) > 0) {
      eval->result = ruleforge_eval_expression(eval, rf_return_stmt_rule_expr(result, 0));
    } else {
      eval->result = ruleforge_value_none();
    }

    eval->returning = TRUE;

    return !eval->failed;
  }

  if (rf_generator_stmt_count_rule_include_stmt(view) > 0) {
    RfIncludeStmt   include;
    RfIncludeTarget target;
    RuleforgeTheme *saved_theme;
    RuleforgeValue  included;
    uint32          themes_set;

    if (eval->emit == NULL) {
      ruleforge_eval_fail(eval, "RF0372", span, "includes can only appear in a ruleset or in a function that returns `ruleset`",
                          "this `include` is outside any ruleset");

      return FALSE;
    }

    include     = rf_generator_stmt_rule_include_stmt(view, 0);
    target      = rf_include_stmt_rule_include_target(include, 0);
    saved_theme = eval->default_theme;
    themes_set  = 0;

    if (rf_include_stmt_count_rule_with_clause(include) > 0) {
      RfWithClause with;

      with = rf_include_stmt_rule_with_clause(include, 0);

      for (i = 0; i < rf_with_clause_count_rule_with_binding(with); i++) {
        RfWithBinding  binding;
        RuleforgeValue bound;

        binding = rf_with_clause_rule_with_binding(with, i);

        if (rf_with_binding_count_token_kw_theme(binding) > 0) {
          bound = ruleforge_eval_expression(eval, rf_with_binding_rule_expr(binding, 0));

          if (eval->failed) {
            return FALSE;
          }

          if (bound.kind != RVK_THEME) {
            ruleforge_eval_fail(eval, "RF0376", ruleforge_eval_span_get(eval, binding.tree, binding.node), NULL, "`theme:` needs a theme, got %s",
                                ruleforge_value_type_name_get(bound.kind));

            return FALSE;
          }

          eval->default_theme = bound.as.theme;
          eval->override_depth++;
          themes_set++;
        } else {
          ruleforge_diagnostic_report(eval->compilation, RDS_WARNING, "RF0377", ruleforge_eval_span_get(eval, binding.tree, binding.node),
                                      "only `theme:` bindings are applied in this version", "this `with` binding has no effect");
        }
      }
    }

    included = ruleforge_value_none();

    if (rf_include_target_count_rule_call(target) > 0) {
      included = r_eval_call(eval, rf_include_target_rule_call(target, 0));
    } else {
      (void)r_eval_reference_resolve(eval, rf_include_target_rule_reference(target, 0), &included);
    }

    eval->default_theme = saved_theme;
    eval->override_depth -= themes_set;

    if (eval->failed) {
      return FALSE;
    }

    if (included.kind != RVK_RULESET) {
      ruleforge_eval_fail(eval, "RF0378", span, NULL, "`include` needs a ruleset, got %s", ruleforge_value_type_name_get(included.kind));

      return FALSE;
    }

    r_eval_ruleset_append(eval, included.as.ruleset, span);

    return !eval->failed;
  }

  if (rf_generator_stmt_count_rule_emit_rule_stmt(view) > 0) {
    return r_eval_rule(eval, rf_emit_rule_stmt_rule_rule_stmt(rf_generator_stmt_rule_emit_rule_stmt(view, 0), 0));
  }

  if (rf_generator_stmt_count_rule_rule_stmt(view) > 0) {
    return r_eval_rule(eval, rf_generator_stmt_rule_rule_stmt(view, 0));
  }

  {
    RfNativeStmt   native;
    RuleforgeRule *rule;

    if (eval->emit == NULL) {
      ruleforge_eval_fail(eval, "RF0372", span, NULL, "this `native` is outside any ruleset");

      return FALSE;
    }

    native           = rf_generator_stmt_rule_native_stmt(view, 0);
    rule             = (RuleforgeRule *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*rule));
    rule->name       = "native";
    rule->span       = span;
    rule->native     = r_eval_string_value(eval, native.tree, rf_native_stmt_token_string_literal(native, 0));
    rule->provenance = ruleforge_eval_provenance_push(eval, RPK_DECLARATION, span, "native", NULL);
    ruleforge_value_ruleset_rule_add(eval->compilation, eval->emit, rule);
    eval->emitted++;

    return TRUE;
  }
}

/* ---------------------------------------------------------------------------- */
/* MAIN                                                                         */
/* ---------------------------------------------------------------------------- */

/* Summary: the text of a configuration key such as default-theme: its parts joined with '-'. */
static void r_eval_configuration_key_write(REval *eval, RfConfigurationKey key, NexusStringBuilder *out) {
  uint32 i;

  for (i = 0; i < rf_configuration_key_count_rule_configuration_key_part(key); i++) {
    RfConfigurationKeyPart  part;
    const LParserGreenNode *token;

    part = rf_configuration_key_rule_configuration_key_part(key, i);

    if (rf_configuration_key_part_count_token_identifier(part) > 0) {
      token = rf_configuration_key_part_token_identifier(part, 0);
    } else if (rf_configuration_key_part_count_token_kw_theme(part) > 0) {
      token = rf_configuration_key_part_token_kw_theme(part, 0);
    } else {
      token = rf_configuration_key_part_token_kw_schema(part, 0);
    }

    (void)nexus_strings_string_builder_append_formatted(out, "%s%s", i > 0 ? "-" : "", ruleforge_eval_text_get(eval, part.tree, token));
  }
}

boolean ruleforge_eval_main_run(REval *eval, RuleforgeRuleset **out_ruleset) {
  RuleforgeCompilation       *compilation;
  RuleforgeModule            *entry;
  const RuleforgeDeclaration *main_declaration;
  uint_large                  i;
  uint32                      j;
  uint_large                  k;
  RuleforgeModule            *candidate;
  RuleforgeValue              result;
  boolean                     imported;
  RuleforgeSpan               start_span;

  compilation      = eval->compilation;
  entry            = NULL;
  main_declaration = NULL;

  /* The entry module exports `func main` and is not imported by another module. */
  for (i = 0; i < nexus_data_list_length_get(compilation->modules); i++) {
    const RuleforgeDeclaration *declaration;

    candidate   = (RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, i);
    declaration = ruleforge_compilation_declaration_find(candidate, "main");

    if (declaration == NULL || !declaration->exported || rf_declaration_count_rule_function_stmt(declaration->view) == 0) {
      continue;
    }

    imported = FALSE;

    for (k = 0; k < nexus_data_list_length_get(compilation->modules); k++) {
      const RuleforgeModule *other;
      uint32                 m;

      other = (const RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, k);

      for (m = 0; other != candidate && m < other->import_count; m++) {
        if (nexus_strings_string_equals(other->imports[m], candidate->name)) {
          imported = TRUE;
        }
      }
    }

    if (!imported) {
      if (entry != NULL) {
        ruleforge_eval_fail(eval, "RF0380", ruleforge_span_make(candidate->file, 0, 0),
                            "keep one entry module; the others should be imported libraries", "more than one module exports `main`: `%s` and `%s`",
                            entry->name, candidate->name);

        return FALSE;
      }

      entry            = candidate;
      main_declaration = declaration;
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
  for (j = 0; j < rf_script_count_rule_declaration(entry->script) && !eval->failed; j++) {
    RfDeclaration declaration;

    declaration = rf_script_rule_declaration(entry->script, j);

    if (rf_declaration_count_rule_configuration_stmt(declaration) > 0) {
      RfConfigurationStmt configuration;
      uint32              m;

      configuration = rf_declaration_rule_configuration_stmt(declaration, 0);

      for (m = 0; m < rf_configuration_stmt_count_rule_configuration_item(configuration) && !eval->failed; m++) {
        RfConfigurationItem item;

        item = rf_configuration_stmt_rule_configuration_item(configuration, m);

        if (rf_configuration_item_count_rule_configuration_assignment(item) > 0) {
          RfConfigurationAssignment assignment;
          NexusStringBuilder        key;
          boolean                   is_theme;

          assignment = rf_configuration_item_rule_configuration_assignment(item, 0);
          key        = nexus_strings_string_builder_create_growable(24);
          r_eval_configuration_key_write(eval, rf_configuration_assignment_rule_configuration_key(assignment, 0), &key);
          is_theme = nexus_strings_string_equals(nexus_strings_string_builder_string_get(&key), "default-theme");
          nexus_strings_string_builder_destroy(&key);

          if (is_theme) {
            result = ruleforge_eval_expression(eval, rf_configuration_assignment_rule_expr(assignment, 0));

            if (!eval->failed && result.kind != RVK_THEME) {
              ruleforge_eval_fail(eval, "RF0382", ruleforge_eval_span_get(eval, assignment.tree, assignment.node), NULL,
                                  "`default-theme` needs a theme, got %s", ruleforge_value_type_name_get(result.kind));
            }

            if (!eval->failed) {
              eval->default_theme = result.as.theme;
            }
          }
        }
      }
    }
  }

  if (eval->failed) {
    return FALSE;
  }

  {
    RfFunctionStmt    function;
    RuleforgeRuleset *ruleset;

    function   = rf_declaration_rule_function_stmt(main_declaration->view, 0);
    start_span = ruleforge_syntax_span_get(entry, function.tree, function.node);

    if (rf_parameters_count_rule_parameter(rf_function_stmt_rule_parameters(function, 0)) != 0) {
      ruleforge_eval_fail(eval, "RF0383", start_span, NULL, "`main` takes no parameters");

      return FALSE;
    }

    /* `main` may be declared void: its includes still collect into one ruleset. */
    ruleset          = ruleforge_value_ruleset_create(compilation, "main");
    eval->emit       = ruleset;
    eval->provenance = ruleforge_eval_provenance_push(eval, RPK_CALL, start_span, "main", NULL);

    for (j = 0; j < rf_function_stmt_count_rule_generator_stmt(function) && !eval->failed && !eval->returning; j++) {
      (void)r_eval_generator(eval, rf_function_stmt_rule_generator_stmt(function, j));
    }

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
