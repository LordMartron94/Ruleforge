#include <nexus.h>
#include "./r_internal.h"

/* Property markers are inserted at the winning Show block, never at the top of
   the filter. Flattening variants first preserves their action and precedence. */
static const RuleforgeStyleProperty *r_marker_property(const RuleforgeStyle *style, const char *key) {
  uint32 i;
  for (i = 0; style != NULL && i < style->show_count; i++) {
    if (nexus_strings_string_equals(style->show[i].keyword, key)) {
      return &style->show[i];
    }
  }
  return NULL;
}

static uint32 r_marker_font(const RuleforgeStyle *style) {
  const RuleforgeStyleProperty *font;
  const char *p;
  uint32 size;
  font = r_marker_property(style, "SetFontSize");
  if (font == NULL) {
    return 32;
  }
  size = 0;
  for (p = font->text; *p >= '0' && *p <= '9'; p++) {
    size = size * 10U + (uint32)(*p - '0');
  }
  return size;
}

static boolean r_marker_priority(const char *key) {
  return nexus_strings_string_equals(key, "SetFontSize") || nexus_strings_string_equals(key, "SetTextColor") ||
         nexus_strings_string_equals(key, "PlayEffect") || nexus_strings_string_equals(key, "MinimapIcon") ||
         nexus_strings_string_equals(key, "PlayAlertSound") || nexus_strings_string_equals(key, "PlayAlertSoundPositional") ||
         nexus_strings_string_equals(key, "CustomAlertSound") || nexus_strings_string_equals(key, "CustomAlertSoundOptional");
}

static RuleforgeStyle *r_marker_style(REval *eval, const RuleforgeStyle *base, const RuleforgeStyle *marker) {
  RuleforgeStyle *result;
  RuleforgeStyleProperty *properties;
  boolean stronger;
  uint32 i;
  uint32 n;
  uint32 base_count;
  RuleforgeStyle empty = {0};
  result = (RuleforgeStyle *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*result));
  *result = empty;
  if (base != NULL) {
    *result = *base;
  }
  base_count = base == NULL ? 0 : base->show_count;
  properties = (RuleforgeStyleProperty *)nexus_data_arena_allocate(&eval->compilation->arena,
      ((uint_large)base_count + marker->show_count + 1U) * NEXUS_SIZEOF(*properties));
  stronger = r_marker_font(marker) > r_marker_font(base);
  n = 0;
  for (i = 0; i < base_count; i++) {
    if ((nexus_strings_string_equals(base->show[i].keyword, "SetBorderColor") && r_marker_property(marker, "SetBorderColor") != NULL) ||
        (stronger && r_marker_priority(base->show[i].keyword))) {
      continue;
    }
    properties[n++] = base->show[i];
  }
  for (i = 0; i < marker->show_count; i++) {
    RuleforgeStyleProperty property;
    property = marker->show[i];
    if (!nexus_strings_string_equals(property.keyword, "SetBorderColor") && !(stronger && r_marker_priority(property.keyword))) {
      continue;
    }
    if (nexus_strings_string_equals(property.keyword, "MinimapIcon")) {
      const RuleforgeStyleProperty *icon;
      icon = r_marker_property(base, "MinimapIcon");
      if (icon != NULL) {
        const char *shape;
        const char *prefix;
        const char *p;
        char text[128];
        int length;
        shape = icon->text;
        prefix = NULL;
        for (p = icon->text; *p != '\0'; p++) {
          if (*p == ' ') { shape = p + 1; }
        }
        for (p = property.text; *p != '\0'; p++) {
          if (*p == ' ') { prefix = p; }
        }
        if (prefix != NULL) {
          length = stbsp_snprintf(text, (int)NEXUS_SIZEOF(text), "%.*s %s", (int)(prefix - property.text), property.text, shape);
          property.text = nexus_data_arena_text_copy(&eval->compilation->arena, text, (uint_large)length);
        }
      }
    }
    properties[n++] = property;
  }
  result->show = properties;
  result->show_count = n;
  return result;
}

static RuleforgeRule *r_marker_rule(REval *eval, RuleforgeSpan span, const RuleforgeBlock *block) {
  RuleforgeRule *rule;
  const RuleforgeProvenance *saved;
  rule = (RuleforgeRule *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*rule));
  *rule = *block->rule;
  rule->scope.count = block->condition_count;
  rule->scope.conditions = block->conditions;
  rule->variant_count = 0;
  rule->variants = NULL;
  rule->has_action = TRUE;
  rule->show = block->show;
  rule->style = NULL;
  if (block->style != NULL) {
    rule->style = (RuleforgeStyle *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*rule->style));
    *rule->style = *block->style;
  }
  saved = eval->provenance;
  eval->provenance = block->rule->provenance;
  rule->provenance = ruleforge_eval_provenance_push(eval, RPK_TRANSFORM, span, "rules.MarkShown", NULL);
  eval->provenance = saved;
  return rule;
}

/* Exact scalar constraints are intersected, so the output never needs repeated
   Class/BaseType directives and unrelated item families get no marker copy. */
static boolean r_marker_scope(REval *eval, RuleforgeRule *rule, const RuleforgeBlock *marker) {
  RuleforgeCondition **conditions;
  uint32 n;
  uint32 i;
  uint32 j;
  conditions = (RuleforgeCondition **)nexus_data_arena_allocate(&eval->compilation->arena,
      ((uint_large)rule->scope.count + marker->condition_count + 1U) * NEXUS_SIZEOF(*conditions));
  n = rule->scope.count;
  for (i = 0; i < n; i++) { conditions[i] = rule->scope.conditions[i]; }
  for (i = 0; i < marker->condition_count; i++) {
    const RuleforgeCondition *right;
    right = marker->conditions[i];
    for (j = 0; j < n; j++) {
      RuleforgeCondition *left;
      left = conditions[j];
      if (left->info == right->info && left->op == ROP_EQ && right->op == ROP_EQ && !left->substring && !right->substring &&
          !left->counted && !right->counted && (left->info->type != RCT_STRING ||
          nexus_strings_string_equals(left->info->keyword, "Class") || nexus_strings_string_equals(left->info->keyword, "BaseType"))) {
        RuleforgeCondition *merged;
        uint32 x;
        uint32 y;
        merged = (RuleforgeCondition *)nexus_data_arena_allocate(&eval->compilation->arena, NEXUS_SIZEOF(*merged));
        *merged = *left;
        merged->count = 0;
        merged->strings = (const char **)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)left->count + 1U) * NEXUS_SIZEOF(const char *));
        merged->numbers = (int64 *)nexus_data_arena_allocate(&eval->compilation->arena, ((uint_large)left->count + 1U) * NEXUS_SIZEOF(int64));
        for (x = 0; x < left->count; x++) {
          for (y = 0; y < right->count; y++) {
            if (left->info->type == RCT_STRING ? ruleforge_filter_text_equal(left->strings[x], right->strings[y]) :
                left->numbers[x] == right->numbers[y]) {
              if (left->info->type == RCT_STRING) { merged->strings[merged->count++] = left->strings[x]; }
              else { merged->numbers[merged->count++] = left->numbers[x]; }
              break;
            }
          }
        }
        if (merged->count == 0) { return FALSE; }
        conditions[j] = merged;
        break;
      }
    }
    if (j == n) { conditions[n++] = marker->conditions[i]; }
  }
  rule->scope.count = n;
  rule->scope.conditions = conditions;
  return TRUE;
}

RuleforgeValue ruleforge_rules_mark_shown(REval *eval, RuleforgeSpan span, const RuleforgeValue *arguments, uint32 count) {
  RuleforgeFilter *source;
  RuleforgeFilter *markers;
  RuleforgeRuleset *result;
  uint32 i;
  uint32 j;
  (void)count;
  if (arguments[0].kind != RVK_RULESET || arguments[1].kind != RVK_RULESET) {
    ruleforge_eval_fail(eval, "RF0525", span, NULL, "rules.MarkShown needs source and marker rulesets");
    return ruleforge_value_none();
  }
  source = ruleforge_filter_lower(eval->compilation, arguments[0].as.ruleset);
  markers = ruleforge_filter_lower(eval->compilation, arguments[1].as.ruleset);
  for (j = 0; j < markers->count; j++) {
    if (markers->blocks[j]->native != NULL || !markers->blocks[j]->show || markers->blocks[j]->style == NULL) {
      ruleforge_eval_fail(eval, "RF0525", span, NULL, "rules.MarkShown markers must be structured Show rules with a style");
      return ruleforge_value_none();
    }
  }
  result = ruleforge_value_ruleset_create(eval->compilation, "markShown");
  for (i = 0; i < source->count; i++) {
    const RuleforgeBlock *block;
    block = source->blocks[i];
    for (j = 0; block->show && block->native == NULL && j < markers->count; j++) {
      RuleforgeRule *marked;
      if (ruleforge_filter_blocks_disjoint(block, markers->blocks[j])) { continue; }
      marked = r_marker_rule(eval, span, block);
      if (!r_marker_scope(eval, marked, markers->blocks[j])) { continue; }
      marked->style = r_marker_style(eval, block->style, markers->blocks[j]->style);
      marked->property_marker = TRUE;
      ruleforge_value_ruleset_rule_add(eval->compilation, result, marked);
    }
    ruleforge_value_ruleset_rule_add(eval->compilation, result, r_marker_rule(eval, span, block));
  }
  return ruleforge_value_ruleset(result);
}
