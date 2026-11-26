#include <nexus.h>
#include "./r_internal.h"

/* Coalescing is allowed only when moving a block earlier cannot cross a rule
   that could match one of its items. This preserves first-match semantics. */
boolean ruleforge_filter_text_equal(const char *a, const char *b) {
  uint32 i;
  for (i = 0; a[i] != '\0' || b[i] != '\0'; i++) {
    char left;
    char right;
    left = a[i] >= 'A' && a[i] <= 'Z' ? (char)(a[i] + ('a' - 'A')) : a[i];
    right = b[i] >= 'A' && b[i] <= 'Z' ? (char)(b[i] + ('a' - 'A')) : b[i];
    if (left != right) {
      return FALSE;
    }
  }
  return TRUE;
}
static boolean r_opt_condition_equal(const RuleforgeCondition *a, const RuleforgeCondition *b) {
  uint32 i;
  if (a->info != b->info || a->op != b->op || a->count != b->count || a->substring != b->substring || a->counted != b->counted ||
      a->threshold != b->threshold) {
    return FALSE;
  }
  for (i = 0; i < a->count; i++) {
    if (a->info->type == RCT_STRING ? !nexus_strings_string_equals(a->strings[i], b->strings[i]) : a->numbers[i] != b->numbers[i]) {
      return FALSE;
    }
  }
  return TRUE;
}
static boolean r_opt_style_equal(const RuleforgeBlock *a, const RuleforgeBlock *b) {
  const RuleforgeStyleProperty *left;
  const RuleforgeStyleProperty *right;
  uint32                        n;
  uint32                        i;
  if (a->show != b->show || (a->style == NULL) != (b->style == NULL)) {
    return FALSE;
  }
  if (a->style == NULL) {
    return TRUE;
  }
  n = a->show ? a->style->show_count : a->style->hide_count;
  if (n != (b->show ? b->style->show_count : b->style->hide_count)) {
    return FALSE;
  }
  left = a->show ? a->style->show : a->style->hide;
  right = b->show ? b->style->show : b->style->hide;
  for (i = 0; i < n; i++) {
    if (!nexus_strings_string_equals(left[i].keyword, right[i].keyword) || !nexus_strings_string_equals(left[i].text, right[i].text)) {
      return FALSE;
    }
  }
  return TRUE;
}
static int32 r_opt_base(const RuleforgeBlock *block) {
  uint32 i;
  int32  found;
  found = -1;
  for (i = 0; i < block->condition_count; i++) {
    const RuleforgeCondition *c;
    c = block->conditions[i];
    if (nexus_strings_string_equals(c->info->keyword, "BaseType")) {
      if (found >= 0 || c->op != ROP_EQ || c->substring || c->counted) {
        return -1;
      }
      found = (int32)i;
    }
  }
  return found;
}
boolean ruleforge_filter_blocks_disjoint(const RuleforgeBlock *a, const RuleforgeBlock *b) {
  uint32 i;
  uint32 j;
  uint32 x;
  uint32 y;
  if (a->native != NULL || b->native != NULL || a->has_continue || b->has_continue) {
    return FALSE;
  }
  for (i = 0; i < a->condition_count; i++) {
    const RuleforgeCondition *left;
    left = a->conditions[i];
    if (left->op != ROP_EQ || left->substring || left->counted) {
      continue;
    }
    /* Multi-valued item properties can satisfy disjoint-looking value lists
       simultaneously. Only scalar string properties prove disjointness. */
    if (left->info->type == RCT_STRING && !nexus_strings_string_equals(left->info->keyword, "BaseType") &&
        !nexus_strings_string_equals(left->info->keyword, "Class")) {
      continue;
    }
    for (j = 0; j < b->condition_count; j++) {
      const RuleforgeCondition *right;
      boolean                   overlap;
      right = b->conditions[j];
      if (left->info != right->info || right->op != ROP_EQ || right->substring || right->counted) {
        continue;
      }
      overlap = FALSE;
      for (x = 0; x < left->count; x++) {
        for (y = 0; y < right->count; y++) {
          overlap = overlap || (left->info->type == RCT_STRING ? ruleforge_filter_text_equal(left->strings[x], right->strings[y])
                                                               : left->numbers[x] == right->numbers[y]);
        }
      }
      if (!overlap) {
        return TRUE;
      }
    }
  }
  return FALSE;
}
void ruleforge_filter_optimize(RuleforgeCompilation *compilation, RuleforgeFilter *filter) {
  uint32 i;
  uint32 j;
  uint32 kept;
  for (i = 0; i < filter->count; i++) {
    RuleforgeBlock *current;
    int32           base;
    current = filter->blocks[i];
    base = r_opt_base(current);
    if (base < 0 || current->native != NULL || current->has_continue) {
      continue;
    }
    for (j = 0; j < i; j++) {
      RuleforgeBlock *candidate;
      int32           target;
      uint32          k;
      boolean         same;
      candidate = filter->blocks[j];
      if (candidate == NULL || candidate->native != NULL || candidate->has_continue || candidate->condition_count != current->condition_count) {
        continue;
      }
      if (!nexus_strings_string_equals(candidate->rule->section == NULL ? "" : candidate->rule->section,
                                       current->rule->section == NULL ? "" : current->rule->section)) {
        continue;
      }
      target = r_opt_base(candidate);
      if (target != base || !r_opt_style_equal(candidate, current)) {
        continue;
      }
      same = TRUE;
      for (k = 0; k < current->condition_count; k++) {
        if ((int32)k != base && !r_opt_condition_equal(candidate->conditions[k], current->conditions[k])) {
          same = FALSE;
          break;
        }
      }
      if (!same) {
        continue;
      }
      for (k = j + 1; k < i; k++) {
        if (filter->blocks[k] != NULL && !ruleforge_filter_blocks_disjoint(filter->blocks[k], current)) {
          same = FALSE;
          break;
        }
      }
      if (same) {
        RuleforgeCondition  *merged;
        RuleforgeCondition  *left;
        RuleforgeCondition  *right;
        RuleforgeRule       *rule;
        RuleforgeProvenance *trace;
        left = candidate->conditions[base];
        right = current->conditions[base];
        merged = (RuleforgeCondition *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*merged));
        *merged = *left;
        merged->count = left->count + right->count;
        merged->strings =
            (const char **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)merged->count + 1U) * NEXUS_SIZEOF(const char *));
        for (k = 0; k < left->count; k++) {
          merged->strings[k] = left->strings[k];
        }
        merged->count = left->count;
        for (k = 0; k < right->count; k++) {
          uint32  n;
          boolean present;
          present = FALSE;
          for (n = 0; n < merged->count; n++) {
            if (ruleforge_filter_text_equal(merged->strings[n], right->strings[k])) {
              present = TRUE;
              break;
            }
          }
          if (!present) {
            merged->strings[merged->count++] = right->strings[k];
          }
        }
        {
          RuleforgeCondition **conditions;
          conditions = (RuleforgeCondition **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)candidate->condition_count + 1U) *
                                                                                                 NEXUS_SIZEOF(RuleforgeCondition *));
          for (k = 0; k < candidate->condition_count; k++) {
            conditions[k] = candidate->conditions[k];
          }
          conditions[base] = merged;
          candidate->conditions = conditions;
        }
        rule = (RuleforgeRule *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*rule));
        *rule = *candidate->rule;
        trace = (RuleforgeProvenance *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*trace));
        trace->kind = RPK_TRANSFORM;
        trace->span = current->rule->span;
        trace->label = current->rule->name;
        trace->parent = rule->provenance;
        if (current->rule->provenance != NULL && current->rule->provenance->kind == RPK_ROW) {
          trace->kind = RPK_ROW;
          trace->record = current->rule->provenance->record;
        }
        rule->provenance = trace;
        candidate->rule = rule;
        filter->blocks[i] = NULL;
        break;
      }
    }
  }
  kept = 0;
  for (i = 0; i < filter->count; i++) {
    if (filter->blocks[i] != NULL) {
      filter->blocks[kept] = filter->blocks[i];
      filter->blocks[kept]->id = kept + 1;
      kept++;
    }
  }
  filter->count = kept;
}
