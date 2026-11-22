#include <nexus.h>

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

/* Summary: evaluates the properties of one state node into a list; FALSE after reporting an error. */
static boolean r_theme_state_lower(REval *eval, const RuleforgeNode *state, NexusDataList *out) {
  uint32 i;
  uint32 j;

  for (i = 0; i < state->child_count; i++) {
    const RuleforgeNode    *property;
    RuleforgeValue          arguments[8];
    uint32                  count;
    int32                   alpha;
    RuleforgeStyleProperty  lowered;
    RuleforgeStyleProperty *stored;

    property = state->children[i];
    count    = 0;
    alpha    = -1;

    for (j = 0; j < property->child_count; j++) {
      if (property->children[j]->kind == RNK_ALPHA_OVERRIDE) {
        alpha = (int32)property->children[j]->integer;

        continue;
      }

      if (count >= 8) {
        ruleforge_eval_fail(eval, "RF0430", property->span, NULL, "style property `%s` has too many arguments", property->text);

        return FALSE;
      }

      eval->enum_fallback = TRUE;
      arguments[count]    = ruleforge_eval_expression(eval, property->children[j]);
      eval->enum_fallback = FALSE;

      if (eval->failed) {
        return FALSE;
      }

      count++;
    }

    if (!ruleforge_manifest_property_lower(eval->compilation, property->span, property->text, arguments, count, alpha, &lowered)) {
      eval->failed = TRUE;

      return FALSE;
    }

    /* A property named twice in one state is a mistake, not an override. */
    for (j = 0; j < nexus_data_list_length_get(out); j++) {
      if (nexus_strings_string_equals(((const RuleforgeStyleProperty *)nexus_data_list_at_const(out, j))->name, lowered.name)) {
        ruleforge_eval_fail(eval, "RF0431", property->span, NULL, "style property `%s` is set twice in one state", property->text);

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

RuleforgeTheme *ruleforge_theme_resolve(REval *eval, const RuleforgeNode *declaration, RuleforgeModule *owner) {
  RuleforgeTheme  *theme;
  RuleforgeTheme  *parent;
  RuleforgeModule *saved_module;
  NexusDataList   *keys;
  NexusDataList   *styles;
  const char      *schema_name;
  uint32           i;
  uint32           j;
  uint_large       k;

  theme        = (RuleforgeTheme *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*theme));
  theme->name  = declaration->text;
  parent       = NULL;
  schema_name  = NULL;
  keys         = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  styles       = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  saved_module = eval->module;
  eval->module = owner;

  for (i = 0; i < declaration->child_count; i++) {
    const RuleforgeNode *child;

    child = declaration->children[i];

    if (child->kind == RNK_EXTENDS) {
      RuleforgeModule     *parent_owner;
      const RuleforgeNode *parent_declaration;

      parent_declaration = ruleforge_eval_symbol_find(eval, owner, child->text, &parent_owner);

      if (parent_declaration == NULL || parent_declaration->kind != RNK_THEME) {
        ruleforge_eval_fail(eval, "RF0440", child->span, NULL, "theme `%s` extends `%s`, which is not a theme", declaration->text, child->text);
        break;
      }

      parent = ruleforge_theme_resolve(eval, parent_declaration, parent_owner);

      if (parent == NULL) {
        break;
      }

      schema_name = parent->schema;

      for (j = 0; j < parent->count; j++) {
        (void)nexus_data_list_append_pointer(keys, parent->keys[j]);
        (void)nexus_data_list_append_pointer(styles, parent->styles[j]);
      }
    } else if (child->kind == RNK_IMPLEMENTS) {
      schema_name = child->text;
    } else if (child->kind == RNK_THEME_STYLE) {
      RuleforgeStyle *style;
      RuleforgeStyle *existing;
      NexusDataList  *show;
      NexusDataList  *hide;
      int64           position;

      show     = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
      hide     = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
      existing = NULL;
      position = -1;

      for (k = 0; k < nexus_data_list_length_get(keys); k++) {
        if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(keys, k), child->text)) {
          position = (int64)k;
          existing = (RuleforgeStyle *)nexus_data_list_pointer_at(styles, k);
        }
      }

      if (existing != NULL && (child->flags & RNF_OVERRIDE) == 0) {
        ruleforge_eval_fail(eval, "RF0441", child->span, "write `override` in front of the style to replace what the parent defines",
                            "style `%s` is already defined by the parent theme", child->text);
      } else if (existing == NULL && (child->flags & RNF_OVERRIDE) != 0) {
        ruleforge_eval_fail(eval, "RF0442", child->span, "remove `override` to define a new style",
                            "`override %s` but the parent theme has no such style", child->text);
      }

      for (j = 0; j < child->child_count && !eval->failed; j++) {
        const RuleforgeNode *state;

        state = child->children[j];
        (void)r_theme_state_lower(eval, state, nexus_strings_string_equals(state->text, "show") ? show : hide);
      }

      if (!eval->failed) {
        style        = (RuleforgeStyle *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*style));
        style->theme = declaration->text;
        style->key   = child->text;

        if (existing != NULL) {
          r_theme_properties_merge(eval, existing->show, existing->show_count, show, &style->show, &style->show_count);
          r_theme_properties_merge(eval, existing->hide, existing->hide_count, hide, &style->hide, &style->hide_count);
          {
            void *removed;

            nexus_data_list_remove_pointer(styles, (uint_large)position, &removed);
          }
          nexus_data_list_insert_pointer(styles, (uint_large)position, style);
        } else {
          r_theme_properties_merge(eval, NULL, 0, show, &style->show, &style->show_count);
          r_theme_properties_merge(eval, NULL, 0, hide, &style->hide, &style->hide_count);
          (void)nexus_data_list_append_pointer(keys, child->text);
          (void)nexus_data_list_append_pointer(styles, style);
        }
      }

      nexus_data_list_destroy(show);
      nexus_data_list_destroy(hide);
    }

    if (eval->failed) {
      break;
    }
  }

  /* The contract: every key of the theme schema, and no other. */
  if (!eval->failed && schema_name != NULL) {
    RuleforgeModule     *schema_owner;
    const RuleforgeNode *schema;

    schema = ruleforge_eval_symbol_find(eval, owner, schema_name, &schema_owner);

    if (schema == NULL || schema->kind != RNK_THEME_SCHEMA) {
      ruleforge_eval_fail(eval, "RF0443", declaration->span, NULL, "theme `%s` implements `%s`, which is not a theme schema", declaration->text,
                          schema_name);
    } else {
      for (j = 0; j < schema->child_count && !eval->failed; j++) {
        boolean found;

        found = FALSE;

        for (k = 0; k < nexus_data_list_length_get(keys); k++) {
          if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(keys, k), schema->children[j]->text)) {
            found = TRUE;
          }
        }

        if (!found) {
          ruleforge_eval_fail(eval, "RF0444", declaration->span, NULL, "theme `%s` does not implement the style `%s` of `%s`", declaration->text,
                              schema->children[j]->text, schema_name);
        }
      }

      for (k = 0; k < nexus_data_list_length_get(keys) && !eval->failed; k++) {
        boolean declared;

        declared = FALSE;

        for (j = 0; j < schema->child_count; j++) {
          if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(keys, k), schema->children[j]->text)) {
            declared = TRUE;
          }
        }

        if (!declared) {
          ruleforge_eval_fail(eval, "RF0445", declaration->span, NULL, "theme `%s` defines the style `%s`, which `%s` does not declare",
                              declaration->text, (const char *)nexus_data_list_pointer_at(keys, k), schema_name);
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

  theme->schema = schema_name;
  theme->count  = (uint32)nexus_data_list_length_get(keys);
  theme->keys   = (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)theme->count + 1U) * NEXUS_SIZEOF(const char *));
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
