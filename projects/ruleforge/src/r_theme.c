#include <nexus.h>
#include <logos.h>
#include <rf_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
A theme resolves once to styles: the properties of each key for the shown and the hidden state, evaluated and validated
against the manifest. `extends` copies the parent's styles; a key the parent defines must be redefined with `override`, which
replaces the properties it names and keeps the rest. `implements` is a contract: every key of the theme schema must exist
and no other key may.
*/

RuleforgeStyle *ruleforge_theme_style_get(const RuleforgeTheme *theme, const char *key) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(theme != NULL && key != NULL, "ruleforge_theme_style_get: NULL argument");

  for (i = 0; i < theme->count; i++) {
    if (nexus_strings_string_equals(theme->keys[i], key)) {
      return theme->styles[i];
    }
  }

  return NULL;
}

/* Summary: evaluates the properties of one state into a list; FALSE after reporting an error. */
static boolean r_theme_state_lower(REval *eval, RfThemeStateStmt state, NexusDataList *out) {
  uint32 i;
  uint32 j;

  for (i = 0; i < rf_theme_state_stmt_count_rule_style_property_stmt(state); i++) {
    RfStylePropertyStmt     property;
    const char             *property_name;
    RuleforgeSpan           span;
    RuleforgeValue          arguments[8];
    uint32                  count;
    int32                   alpha;
    RuleforgeStyleProperty  lowered;
    RuleforgeStyleProperty *stored;

    property      = rf_theme_state_stmt_rule_style_property_stmt(state, i);
    property_name = ruleforge_eval_text_get(eval, property.tree, rf_style_property_stmt_token_identifier(property, 0));
    span          = ruleforge_eval_span_get(eval, property.tree, property.node);
    count         = 0;
    alpha         = -1;

    if (rf_style_property_stmt_count_rule_color_override(property) > 0) {
      alpha = (int32)ruleforge_color_alpha_override_get(rf_style_property_stmt_rule_color_override(property, 0));
    }

    for (j = 0; j < rf_style_property_stmt_count_rule_expr(property); j++) {
      if (count >= 8) {
        ruleforge_eval_fail(eval, "RF0430", span, NULL, "style property `%s` has too many arguments", property_name);

        return FALSE;
      }

      eval->enum_fallback = TRUE;
      arguments[count]    = ruleforge_eval_expression(eval, rf_style_property_stmt_rule_expr(property, j));
      eval->enum_fallback = FALSE;

      if (eval->failed) {
        return FALSE;
      }

      count++;
    }

    if (!ruleforge_manifest_property_lower(eval->compilation, span, property_name, arguments, count, alpha, &lowered)) {
      eval->failed = TRUE;

      return FALSE;
    }

    /* A property named twice in one state is a mistake, not an override. */
    for (j = 0; j < nexus_data_list_length_get(out); j++) {
      if (nexus_strings_string_equals(((const RuleforgeStyleProperty *)nexus_data_list_pointer_at(out, j))->name, lowered.name)) {
        ruleforge_eval_fail(eval, "RF0431", span, NULL, "style property `%s` is set twice in one state", property_name);

        return FALSE;
      }
    }

    stored  = (RuleforgeStyleProperty *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*stored));
    *stored = lowered;
    (void)nexus_data_list_append_pointer(out, stored);
  }

  return TRUE;
}

/* Summary: replaces or adds the properties of `over` in `base`, keeping the order of `base` for those it keeps. */
static void r_theme_properties_merge(REval *eval, const RuleforgeStyleProperty *base, uint32 base_count, NexusDataList *over,
                                     RuleforgeStyleProperty **out, uint32 *out_count) {
  RuleforgeStyleProperty *merged;
  uint32                  count;
  uint32                  i;
  uint32                  j;
  boolean                 replaced;

  merged = (RuleforgeStyleProperty *)nexus_data_arena_allocate(
      &eval->compilation->arena, ((uint_large)base_count + nexus_data_list_length_get(over) + 1U) * NEXUS_SIZEOF(*merged));
  count = 0;

  for (i = 0; i < base_count; i++) {
    replaced = FALSE;

    for (j = 0; j < nexus_data_list_length_get(over); j++) {
      const RuleforgeStyleProperty *candidate;

      candidate = (const RuleforgeStyleProperty *)nexus_data_list_pointer_at(over, j);

      if (nexus_strings_string_equals(candidate->name, base[i].name)) {
        merged[count++] = *candidate;
        replaced        = TRUE;
      }
    }

    if (!replaced) {
      merged[count++] = base[i];
    }
  }

  for (j = 0; j < nexus_data_list_length_get(over); j++) {
    const RuleforgeStyleProperty *candidate;
    boolean                       present;

    candidate = (const RuleforgeStyleProperty *)nexus_data_list_pointer_at(over, j);
    present   = FALSE;

    for (i = 0; i < base_count; i++) {
      if (nexus_strings_string_equals(candidate->name, base[i].name)) {
        present = TRUE;
      }
    }

    if (!present) {
      merged[count++] = *candidate;
    }
  }

  *out       = merged;
  *out_count = count;
}

RuleforgeTheme *ruleforge_theme_resolve(REval *eval, RfThemeStmt declaration, RuleforgeModule *owner) {
  RuleforgeTheme             *theme;
  RuleforgeTheme             *parent;
  RuleforgeModule            *saved_module;
  NexusDataList              *keys;
  NexusDataList              *styles;
  const char                 *schema_name;
  const char                 *theme_name;
  const RuleforgeDeclaration *schema_declaration;
  RuleforgeSpan               theme_span;
  uint32                      i;
  uint32                      j;
  uint32                      name_index;
  uint_large                  k;

  theme_name         = ruleforge_eval_text_get(eval, declaration.tree, rf_theme_stmt_token_identifier(declaration, 0));
  theme_span         = ruleforge_syntax_span_get(owner, declaration.tree, declaration.node);
  theme              = (RuleforgeTheme *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*theme));
  theme->name        = theme_name;
  parent             = NULL;
  schema_name        = NULL;
  schema_declaration = NULL;
  keys               = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  styles             = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  saved_module       = eval->module;
  eval->module       = owner;
  name_index         = 0;

  /* The qualified names after the name are the parent (when `extends` is written) and then the schema. */
  if (rf_theme_stmt_count_token_kw_extends(declaration) > 0) {
    RuleforgeModule            *parent_owner;
    const RuleforgeDeclaration *parent_declaration;
    const char                 *parent_name;

    parent_name = ruleforge_syntax_name_get(eval->compilation, rf_theme_stmt_rule_qualified_name(declaration, name_index));
    name_index++;
    parent_declaration = ruleforge_eval_symbol_find(eval, owner, parent_name, &parent_owner);

    if (parent_declaration == NULL || rf_declaration_count_rule_theme_stmt(parent_declaration->view) == 0) {
      ruleforge_eval_fail(eval, "RF0440", theme_span, NULL, "theme `%s` extends `%s`, which is not a theme", theme_name, parent_name);
    } else {
      parent = ruleforge_theme_resolve(eval, rf_declaration_rule_theme_stmt(parent_declaration->view, 0), parent_owner);

      if (parent != NULL) {
        schema_name        = parent->schema;
        schema_declaration = parent->schema_declaration;

        for (j = 0; j < parent->count; j++) {
          (void)nexus_data_list_append_pointer(keys, parent->keys[j]);
          (void)nexus_data_list_append_pointer(styles, parent->styles[j]);
        }
      }
    }
  }

  if (rf_theme_stmt_count_token_kw_implements(declaration) > 0 && !eval->failed) {
    RuleforgeModule *schema_owner;

    schema_name        = ruleforge_syntax_name_get(eval->compilation, rf_theme_stmt_rule_qualified_name(declaration, name_index));
    schema_declaration = ruleforge_eval_symbol_find(eval, owner, schema_name, &schema_owner);
  }

  for (i = 0; i < rf_theme_stmt_count_rule_theme_style_stmt(declaration) && !eval->failed; i++) {
    RfThemeStyleStmt child;
    const char      *key;
    RuleforgeSpan    child_span;
    boolean          override_flag;
    RuleforgeStyle  *style;
    RuleforgeStyle  *existing;
    NexusDataList   *show;
    NexusDataList   *hide;
    int64            position;

    child         = rf_theme_stmt_rule_theme_style_stmt(declaration, i);
    key           = ruleforge_eval_text_get(eval, child.tree, rf_theme_style_stmt_token_identifier(child, 0));
    child_span    = ruleforge_eval_span_get(eval, child.tree, child.node);
    override_flag = rf_theme_style_stmt_count_token_kw_override(child) > 0;
    show          = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
    hide          = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
    existing      = NULL;
    position      = -1;

    for (k = 0; k < nexus_data_list_length_get(keys); k++) {
      if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(keys, k), key)) {
        position = (int64)k;
        existing = (RuleforgeStyle *)nexus_data_list_pointer_at(styles, k);
      }
    }

    if (existing != NULL && !override_flag) {
      ruleforge_eval_fail(eval, "RF0441", child_span, "write `override` in front of the style to replace what the parent defines",
                          "style `%s` is already defined by the parent theme", key);
    } else if (existing == NULL && override_flag) {
      ruleforge_eval_fail(eval, "RF0442", child_span, "remove `override` to define a new style",
                          "`override %s` but the parent theme has no such style", key);
    }

    for (j = 0; j < rf_theme_style_stmt_count_rule_theme_state_stmt(child) && !eval->failed; j++) {
      RfThemeStateStmt state;

      state = rf_theme_style_stmt_rule_theme_state_stmt(child, j);
      (void)r_theme_state_lower(eval, state, rf_theme_state_stmt_count_token_kw_on_show(state) > 0 ? show : hide);
    }

    if (!eval->failed) {
      style        = (RuleforgeStyle *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*style));
      style->theme = theme_name;
      style->key   = key;

      if (existing != NULL) {
        void *removed;

        r_theme_properties_merge(eval, existing->show, existing->show_count, show, &style->show, &style->show_count);
        r_theme_properties_merge(eval, existing->hide, existing->hide_count, hide, &style->hide, &style->hide_count);
        nexus_data_list_remove_pointer(styles, (uint_large)position, &removed);
        nexus_data_list_insert_pointer(styles, (uint_large)position, style);
      } else {
        r_theme_properties_merge(eval, NULL, 0, show, &style->show, &style->show_count);
        r_theme_properties_merge(eval, NULL, 0, hide, &style->hide, &style->hide_count);
        (void)nexus_data_list_append_pointer(keys, key);
        (void)nexus_data_list_append_pointer(styles, style);
      }
    }

    nexus_data_list_destroy(show);
    nexus_data_list_destroy(hide);
  }

  /* The contract: every key of the theme schema, and no other. */
  if (!eval->failed && schema_name != NULL) {
    const RuleforgeDeclaration *schema;

    schema = schema_declaration;

    if (schema == NULL || rf_declaration_count_rule_theme_schema_stmt(schema->view) == 0) {
      ruleforge_eval_fail(eval, "RF0443", theme_span, NULL, "theme `%s` implements `%s`, which is not a theme schema", theme_name, schema_name);
    } else {
      RfThemeSchemaStmt contract;
      uint32            contract_count;

      contract       = rf_declaration_rule_theme_schema_stmt(schema->view, 0);
      contract_count = rf_theme_schema_stmt_count_rule_theme_schema_key(contract);

      for (j = 0; j < contract_count && !eval->failed; j++) {
        RfThemeSchemaKey schema_key;
        const char      *schema_key_name;
        boolean          found;

        schema_key      = rf_theme_schema_stmt_rule_theme_schema_key(contract, j);
        schema_key_name = ruleforge_eval_text_get(eval, schema_key.tree, rf_theme_schema_key_token_identifier(schema_key, 0));
        found           = FALSE;

        for (k = 0; k < nexus_data_list_length_get(keys); k++) {
          if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(keys, k), schema_key_name)) {
            found = TRUE;
          }
        }

        if (!found) {
          ruleforge_eval_fail(eval, "RF0444", theme_span, NULL, "theme `%s` does not implement the style `%s` of `%s`", theme_name, schema_key_name,
                              schema_name);
        }
      }

      for (k = 0; k < nexus_data_list_length_get(keys) && !eval->failed; k++) {
        boolean declared;

        declared = FALSE;

        for (j = 0; j < contract_count; j++) {
          RfThemeSchemaKey schema_key;

          schema_key = rf_theme_schema_stmt_rule_theme_schema_key(contract, j);

          if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(keys, k),
                                          ruleforge_eval_text_get(eval, schema_key.tree, rf_theme_schema_key_token_identifier(schema_key, 0)))) {
            declared = TRUE;
          }
        }

        if (!declared) {
          ruleforge_eval_fail(eval, "RF0445", theme_span, NULL, "theme `%s` defines the style `%s`, which `%s` does not declare", theme_name,
                              (const char *)nexus_data_list_pointer_at(keys, k), schema_name);
        }
      }
    }
  }

  eval->module = saved_module;

  if (eval->failed) {
    nexus_data_list_destroy(keys);
    nexus_data_list_destroy(styles);

    return NULL;
  }

  theme->schema             = schema_name;
  theme->schema_declaration = schema_declaration;
  theme->count              = (uint32)nexus_data_list_length_get(keys);
  theme->keys = (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)theme->count + 1U) * NEXUS_SIZEOF(const char *));
  theme->styles =
      (RuleforgeStyle **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)theme->count + 1U) * NEXUS_SIZEOF(RuleforgeStyle *));

  for (i = 0; i < theme->count; i++) {
    theme->keys[i]   = (const char *)nexus_data_list_pointer_at(keys, i);
    theme->styles[i] = (RuleforgeStyle *)nexus_data_list_pointer_at(styles, i);
  }

  nexus_data_list_destroy(keys);
  nexus_data_list_destroy(styles);

  return theme;
}

/* ---------------------------------------------------------------------------- */
/* ADAPTERS                                                                     */
/* ---------------------------------------------------------------------------- */

boolean ruleforge_theme_adapter_find(REval *eval, const RuleforgeModule *module, const char *name, RfAdapterStmt *out_adapter) {
  uint32 i;
  uint32 j;

  for (i = 0; i < rf_script_count_rule_declaration(module->script); i++) {
    RfDeclaration declaration;

    declaration = rf_script_rule_declaration(module->script, i);

    if (rf_declaration_count_rule_configuration_stmt(declaration) == 0) {
      continue;
    }

    for (j = 0; j < rf_configuration_stmt_count_rule_configuration_item(rf_declaration_rule_configuration_stmt(declaration, 0)); j++) {
      RfConfigurationItem item;

      item = rf_configuration_stmt_rule_configuration_item(rf_declaration_rule_configuration_stmt(declaration, 0), j);

      if (rf_configuration_item_count_rule_adapter_stmt(item) > 0) {
        RfAdapterStmt adapter;

        adapter = rf_configuration_item_rule_adapter_stmt(item, 0);

        if (nexus_strings_string_equals(ruleforge_eval_text_get(eval, adapter.tree, rf_adapter_stmt_token_identifier(adapter, 0)), name)) {
          *out_adapter = adapter;

          return TRUE;
        }
      }
    }
  }

  return FALSE;
}

/* Summary: the position of a key in the contract of a theme schema, or -1. */
static int64 r_theme_contract_index(REval *eval, RfThemeSchemaStmt contract, const char *key) {
  uint32 i;

  for (i = 0; i < rf_theme_schema_stmt_count_rule_theme_schema_key(contract); i++) {
    RfThemeSchemaKey schema_key;

    schema_key = rf_theme_schema_stmt_rule_theme_schema_key(contract, i);

    if (nexus_strings_string_equals(ruleforge_eval_text_get(eval, schema_key.tree, rf_theme_schema_key_token_identifier(schema_key, 0)), key)) {
      return (int64)i;
    }
  }

  return -1;
}

/*
Summary: a theme seen through an adapter: the keys of the adapter's target schema, each served by the theme's style the adapter maps it to.
Description: a key the adapter does not mention is served by the style of the same name. Every mapping and every key of the schema is checked,
  and the first problem is reported at the mapping that causes it.
*/
RuleforgeTheme *ruleforge_theme_adapt(REval *eval, const RuleforgeTheme *theme, RfAdapterStmt adapter, RuleforgeModule *owner, RuleforgeSpan span) {
  const char                 *adapter_name;
  const char                 *target_name;
  const RuleforgeDeclaration *target;
  RuleforgeModule            *target_owner;
  RfThemeSchemaStmt           contract;
  RuleforgeTheme             *adapted;
  const char                **mapped;
  uint32                      key_count;
  uint32                      i;
  uint32                      j;

  adapter_name = ruleforge_eval_text_get(eval, adapter.tree, rf_adapter_stmt_token_identifier(adapter, 0));
  target_name  = ruleforge_syntax_name_get(eval->compilation, rf_adapter_stmt_rule_qualified_name(adapter, 0));
  target       = ruleforge_eval_symbol_find(eval, owner, target_name, &target_owner);

  if (target == NULL || rf_declaration_count_rule_theme_schema_stmt(target->view) == 0) {
    ruleforge_eval_fail(eval, "RF0446", ruleforge_syntax_span_get(owner, adapter.tree, adapter.node), NULL,
                        "adapter `%s` targets `%s`, which is not a theme schema", adapter_name, target_name);

    return NULL;
  }

  contract  = rf_declaration_rule_theme_schema_stmt(target->view, 0);
  key_count = rf_theme_schema_stmt_count_rule_theme_schema_key(contract);
  mapped    = (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)key_count + 1U) * NEXUS_SIZEOF(const char *));

  for (i = 0; i < key_count; i++) {
    mapped[i] = NULL;
  }

  for (i = 0; i < rf_adapter_stmt_count_rule_adapter_mapping(adapter); i++) {
    RfAdapterMapping mapping;
    const char      *schema_key;
    const char      *theme_key;
    RuleforgeSpan    mapping_span;
    int64            index;

    mapping      = rf_adapter_stmt_rule_adapter_mapping(adapter, i);
    schema_key   = ruleforge_eval_text_get(eval, mapping.tree, rf_adapter_mapping_token_identifier(mapping, 0));
    theme_key    = ruleforge_eval_text_get(eval, mapping.tree, rf_adapter_mapping_token_identifier(mapping, 1));
    mapping_span = ruleforge_syntax_span_get(owner, mapping.tree, mapping.node);
    index        = r_theme_contract_index(eval, contract, schema_key);

    if (index < 0) {
      ruleforge_eval_fail(eval, "RF0447", mapping_span,
                          "the left side of a mapping is a key of the target schema, the right side a style of the theme",
                          "adapter `%s` maps `%s`, which schema `%s` does not declare", adapter_name, schema_key, target_name);

      return NULL;
    }

    if (mapped[index] != NULL) {
      ruleforge_eval_fail(eval, "RF0448", mapping_span, NULL, "adapter `%s` maps `%s` twice", adapter_name, schema_key);

      return NULL;
    }

    if (ruleforge_theme_style_get(theme, theme_key) == NULL) {
      ruleforge_eval_fail(eval, "RF0449", mapping_span, NULL, "theme `%s` has no style `%s`, which adapter `%s` maps to `%s`", theme->name, theme_key,
                          adapter_name, schema_key);

      return NULL;
    }

    mapped[index] = theme_key;
  }

  adapted                     = (RuleforgeTheme *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*adapted));
  adapted->name               = theme->name;
  adapted->schema             = target_name;
  adapted->schema_declaration = target;
  adapted->count              = key_count;
  adapted->keys = (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)key_count + 1U) * NEXUS_SIZEOF(const char *));
  adapted->styles =
      (RuleforgeStyle **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)key_count + 1U) * NEXUS_SIZEOF(RuleforgeStyle *));

  for (j = 0; j < key_count; j++) {
    RfThemeSchemaKey schema_key;
    const char      *key;
    const char      *source;

    schema_key = rf_theme_schema_stmt_rule_theme_schema_key(contract, j);
    key        = ruleforge_eval_text_get(eval, schema_key.tree, rf_theme_schema_key_token_identifier(schema_key, 0));
    source     = mapped[j] != NULL ? mapped[j] : key;

    adapted->keys[j]   = key;
    adapted->styles[j] = ruleforge_theme_style_get(theme, source);

    if (adapted->styles[j] == NULL) {
      ruleforge_eval_fail(eval, "RF0450", span, "map the key in the adapter (`key = style;`) or add the style to the theme",
                          "theme `%s` has no style `%s` for the key `%s` of schema `%s` (adapter `%s`)", theme->name, source, key, target_name,
                          adapter_name);

      return NULL;
    }
  }

  return adapted;
}

/* ---------------------------------------------------------------------------- */
/* THEMES AS JSON                                                               */
/* ---------------------------------------------------------------------------- */

/* Summary: source spans used by form editors for an entire construct and, optionally, its name. */
static void r_theme_edit_span_json_write(RuleforgeCompilation *compilation, const RuleforgeModule *module, const LParserTree *tree,
                                         const LParserGreenNode *node, const LParserGreenNode *name, NexusStringBuilder *out) {
  RuleforgeSpan span;

  span = ruleforge_syntax_span_get(module, tree, node);
  (void)nexus_strings_string_builder_append_formatted(out, ",\"start\":%lu,\"end\":%lu", (unsigned long)span.start, (unsigned long)span.end);

  if (name != NULL) {
    span = ruleforge_syntax_span_get(module, tree, name);
    (void)nexus_strings_string_builder_append_formatted(out, ",\"nameStart\":%lu,\"nameEnd\":%lu", (unsigned long)span.start,
                                                        (unsigned long)span.end);
  }

  (void)compilation;
}

/* Summary: appends the source text of a node, quoted. */
static void r_theme_source_json_write(RuleforgeCompilation *compilation, const RuleforgeModule *module, const LParserTree *tree,
                                      const LParserGreenNode *node, NexusStringBuilder *out) {
  RuleforgeSpan span;
  const char   *text;

  span = ruleforge_syntax_span_get(module, tree, node);
  text = nexus_data_arena_text_copy(&compilation->arena, (const char *)ruleforge_source_get(compilation, module->file)->text + span.start,
                                    span.end - span.start);
  (void)nexus_json_quote_append(out, text);
}

/* Summary: one argument of a style property or one color of a color set: what it is, where it is written, and for a color its value. */
static void r_theme_expression_json_write(RuleforgeCompilation *compilation, const RuleforgeModule *module, RfExpr expression,
                                          NexusStringBuilder *out) {
  RuleforgeSpan span;
  RfAtom        atom;
  const char   *kind;

  span = ruleforge_syntax_span_get(module, expression.tree, expression.node);
  kind = "expression";

  if (rf_expr_operand_count(expression) == 0) {
    atom = rf_expr_atom(expression);

    if (rf_atom_count_rule_color_literal(atom) > 0) {
      boolean has_alpha;
      uint32  value;

      value = ruleforge_color_value_get(rf_atom_rule_color_literal(atom, 0), &has_alpha);
      (void)nexus_strings_string_builder_append_formatted(
          out, "{\"kind\":\"color\",\"value\":%lu,\"alpha\":%s,\"start\":%lu,\"end\":%lu,\"text\":", (unsigned long)value,
          has_alpha ? "true" : "false", (unsigned long)span.start, (unsigned long)span.end);
      r_theme_source_json_write(compilation, module, expression.tree, expression.node, out);
      (void)nexus_strings_string_builder_append(out, "}");

      return;
    }

    if (rf_atom_count_rule_indexed_reference(atom) > 0) {
      kind = "reference";
    }
  }

  (void)nexus_strings_string_builder_append_formatted(out, "{\"kind\":\"%s\",\"start\":%lu,\"end\":%lu,\"text\":", kind, (unsigned long)span.start,
                                                      (unsigned long)span.end);
  r_theme_source_json_write(compilation, module, expression.tree, expression.node, out);
  (void)nexus_strings_string_builder_append(out, "}");
}

static void r_theme_style_json_write(RuleforgeCompilation *compilation, const RuleforgeModule *module, RfThemeStyleStmt style,
                                     NexusStringBuilder *out) {
  uint32 i;
  uint32 j;
  uint32 k;

  (void)nexus_strings_string_builder_append_formatted(
      out, "{\"override\":%s,\"key\":", rf_theme_style_stmt_count_token_kw_override(style) > 0 ? "true" : "false");
  (void)nexus_json_quote_append(out, ruleforge_syntax_text_get(compilation, style.tree, rf_theme_style_stmt_token_identifier(style, 0)));
  r_theme_edit_span_json_write(compilation, module, style.tree, style.node, rf_theme_style_stmt_token_identifier(style, 0), out);
  (void)nexus_strings_string_builder_append(out, ",\"states\":[");

  for (i = 0; i < rf_theme_style_stmt_count_rule_theme_state_stmt(style); i++) {
    RfThemeStateStmt state;

    state = rf_theme_style_stmt_rule_theme_state_stmt(style, i);
    (void)nexus_strings_string_builder_append_formatted(out, "%s{\"state\":\"%s\"", i > 0 ? "," : "",
                                                        rf_theme_state_stmt_count_token_kw_on_show(state) > 0 ? "show" : "hide");
    r_theme_edit_span_json_write(compilation, module, state.tree, state.node, NULL, out);
    (void)nexus_strings_string_builder_append(out, ",\"properties\":[");

    for (j = 0; j < rf_theme_state_stmt_count_rule_style_property_stmt(state); j++) {
      RfStylePropertyStmt property;

      property = rf_theme_state_stmt_rule_style_property_stmt(state, j);
      (void)nexus_strings_string_builder_append_formatted(out, "%s{\"name\":", j > 0 ? "," : "");
      (void)nexus_json_quote_append(out, ruleforge_syntax_text_get(compilation, property.tree, rf_style_property_stmt_token_identifier(property, 0)));
      r_theme_edit_span_json_write(compilation, module, property.tree, property.node, NULL, out);
      (void)nexus_strings_string_builder_append(out, ",\"arguments\":[");

      for (k = 0; k < rf_style_property_stmt_count_rule_expr(property); k++) {
        if (k > 0) {
          (void)nexus_strings_string_builder_append(out, ",");
        }

        r_theme_expression_json_write(compilation, module, rf_style_property_stmt_rule_expr(property, k), out);
      }

      (void)nexus_strings_string_builder_append(out, "]");

      if (rf_style_property_stmt_count_rule_color_override(property) > 0) {
        (void)nexus_strings_string_builder_append_formatted(
            out, ",\"alphaOverride\":%lu",
            (unsigned long)ruleforge_color_alpha_override_get(rf_style_property_stmt_rule_color_override(property, 0)));
      }

      (void)nexus_strings_string_builder_append(out, "}");
    }

    (void)nexus_strings_string_builder_append(out, "]}");
  }

  (void)nexus_strings_string_builder_append(out, "]}");
}

void ruleforge_theme_declarations_json_write(RuleforgeCompilation *compilation, NexusStringBuilder *out) {
  uint_large themes;
  uint_large sets;
  uint_large m;
  uint32     i;
  uint32     j;

  themes = 0;
  sets   = 0;
  (void)nexus_strings_string_builder_append(out, "{\"format\":\"ruleforge-themes\",\"version\":1,\"themes\":[");

  for (m = 0; m < nexus_data_list_length_get(compilation->modules); m++) {
    const RuleforgeModule *module;
    const RuleforgeFile   *file;

    module = (const RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, m);
    file   = ruleforge_source_get(compilation, module->file);

    if (file->embedded) {
      continue;
    }

    for (i = 0; i < rf_script_count_rule_declaration(module->script); i++) {
      RfDeclaration declaration;
      RfThemeStmt   theme;

      declaration = rf_script_rule_declaration(module->script, i);

      if (rf_declaration_count_rule_theme_stmt(declaration) == 0) {
        continue;
      }

      theme = rf_declaration_rule_theme_stmt(declaration, 0);
      (void)nexus_strings_string_builder_append_formatted(out, "%s{\"name\":", themes > 0 ? "," : "");
      (void)nexus_json_quote_append(out, ruleforge_syntax_text_get(compilation, theme.tree, rf_theme_stmt_token_identifier(theme, 0)));
      r_theme_edit_span_json_write(compilation, module, theme.tree, declaration.node, rf_theme_stmt_token_identifier(theme, 0), out);
      (void)nexus_strings_string_builder_append(out, ",\"module\":");
      (void)nexus_json_quote_append(out, module->name == NULL ? "" : module->name);
      (void)nexus_strings_string_builder_append(out, ",\"file\":");
      (void)nexus_json_quote_append(out, file->name);
      (void)nexus_strings_string_builder_append(out, ",\"extends\":");

      if (rf_theme_stmt_count_token_kw_extends(theme) > 0) {
        (void)nexus_json_quote_append(out, ruleforge_syntax_name_get(compilation, rf_theme_stmt_rule_qualified_name(theme, 0)));
      } else {
        (void)nexus_strings_string_builder_append(out, "null");
      }

      (void)nexus_strings_string_builder_append(out, ",\"styles\":[");

      for (j = 0; j < rf_theme_stmt_count_rule_theme_style_stmt(theme); j++) {
        if (j > 0) {
          (void)nexus_strings_string_builder_append(out, ",");
        }

        r_theme_style_json_write(compilation, module, rf_theme_stmt_rule_theme_style_stmt(theme, j), out);
      }

      (void)nexus_strings_string_builder_append(out, "]}");
      themes++;
    }
  }

  (void)nexus_strings_string_builder_append(out, "],\"colorSets\":[");

  for (m = 0; m < nexus_data_list_length_get(compilation->modules); m++) {
    const RuleforgeModule *module;
    const RuleforgeFile   *file;

    module = (const RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, m);
    file   = ruleforge_source_get(compilation, module->file);

    if (file->embedded) {
      continue;
    }

    for (i = 0; i < rf_script_count_rule_declaration(module->script); i++) {
      RfDeclaration  declaration;
      RfColorSetStmt set;

      declaration = rf_script_rule_declaration(module->script, i);

      if (rf_declaration_count_rule_color_set_stmt(declaration) == 0) {
        continue;
      }

      set = rf_declaration_rule_color_set_stmt(declaration, 0);
      (void)nexus_strings_string_builder_append_formatted(out, "%s{\"name\":", sets > 0 ? "," : "");
      (void)nexus_json_quote_append(out, ruleforge_syntax_text_get(compilation, set.tree, rf_color_set_stmt_token_identifier(set, 0)));
      (void)nexus_strings_string_builder_append(out, ",\"file\":");
      (void)nexus_json_quote_append(out, file->name);
      r_theme_edit_span_json_write(compilation, module, set.tree, declaration.node, rf_color_set_stmt_token_identifier(set, 0), out);
      (void)nexus_strings_string_builder_append(out, ",\"colors\":[");

      for (j = 0; j < rf_color_set_stmt_count_rule_color_definition(set); j++) {
        RfColorDefinition definition;

        definition = rf_color_set_stmt_rule_color_definition(set, j);
        (void)nexus_strings_string_builder_append_formatted(out, "%s{\"name\":", j > 0 ? "," : "");
        (void)nexus_json_quote_append(out,
                                      ruleforge_syntax_text_get(compilation, definition.tree, rf_color_definition_token_identifier(definition, 0)));
        r_theme_edit_span_json_write(compilation, module, definition.tree, definition.node, rf_color_definition_token_identifier(definition, 0), out);
        (void)nexus_strings_string_builder_append(out, ",\"value\":");
        r_theme_expression_json_write(compilation, module, rf_color_definition_rule_expr(definition, 0), out);
        (void)nexus_strings_string_builder_append(out, "}");
      }

      (void)nexus_strings_string_builder_append(out, "]}");
      sets++;
    }
  }

  (void)nexus_strings_string_builder_append(out, "]}");
}
