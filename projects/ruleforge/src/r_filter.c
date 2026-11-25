#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
Lowering turns rules into the blocks the game evaluates: first match wins, so a rule's variants come before its baseline, and
a variant whose game-time condition is an `or` becomes one block per alternative. A variant that always matches (its
compile-time condition was true and nothing is left for the game to decide) ends its rule: nothing after it can be reached.
*/

/* ---------------------------------------------------------------------------- */
/* LOWERING                                                                     */
/* ---------------------------------------------------------------------------- */

static RuleforgeBlock *r_filter_block_create(RuleforgeCompilation *compilation, NexusDataList *blocks) {
  RuleforgeBlock *block;

  block = (RuleforgeBlock *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*block));
  block->id = (uint32)nexus_data_list_length_get(blocks) + 1;
  (void)nexus_data_list_append_pointer(blocks, block);

  return block;
}

/*
Summary: puts the conditions of a block in the order their declarations ask for: by `order`, and by where they were declared when equal.
Description: stable, and short lists only, so an insertion sort. The same filter written with the conditions in another order is the same
filter.
*/
static void r_filter_conditions_sort(RuleforgeCondition **conditions, uint32 count) {
  uint32 i;
  uint32 j;

  for (i = 1; i < count; i++) {
    RuleforgeCondition *moving;

    moving = conditions[i];

    for (j = i; j > 0; j--) {
      const RuleforgeConditionInfo *before;
      const RuleforgeConditionInfo *after;

      before = conditions[j - 1]->info;
      after = moving->info;

      if (before->order < after->order || (before->order == after->order && before->declaration <= after->declaration)) {
        break;
      }

      conditions[j] = conditions[j - 1];
    }

    conditions[j] = moving;
  }
}

RuleforgeFilter *ruleforge_filter_lower(RuleforgeCompilation *compilation, const RuleforgeRuleset *ruleset) {
  RuleforgeFilter *filter;
  NexusDataList   *blocks;
  uint32           i;
  uint32           j;
  uint32           k;
  uint32           m;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && ruleset != NULL, "ruleforge_filter_lower: NULL argument");

  blocks = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 64);

  for (i = 0; i < ruleset->count; i++) {
    const RuleforgeRule *rule;
    boolean              reached_baseline;
    RuleforgeBlock      *block;

    rule = ruleset->rules[i];
    block = NULL;

    if (rule->native != NULL) {
      block = r_filter_block_create(compilation, blocks);
      block->native = rule->native;
      block->rule = rule;
      block->role = "native";

      continue;
    }

    reached_baseline = TRUE;

    for (j = 0; j < rule->variant_count; j++) {
      const RuleforgeVariant *variant;
      uint32                  clauses;

      variant = &rule->variants[j];
      clauses = variant->terminal ? 1 : variant->clause_count;

      for (k = 0; k < clauses; k++) {
        char role[32];

        block = r_filter_block_create(compilation, blocks);
        (void)stbsp_snprintf(role, (int)NEXUS_SIZEOF(role), "variant %lu", (unsigned long)j + 1UL);
        block->role = nexus_data_arena_text_copy(&compilation->arena, role, nexus_strings_string_length_get(role));
        block->rule = rule;
        block->show = variant->has_action ? variant->show : rule->show;
        block->style = variant->style != NULL ? variant->style : rule->style;
        block->condition_count = rule->scope.count + (variant->terminal ? 0 : variant->clauses[k].count);
        block->conditions = (RuleforgeCondition **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)block->condition_count + 1U) *
                                                                                                      NEXUS_SIZEOF(RuleforgeCondition *));

        for (m = 0; m < rule->scope.count; m++) {
          block->conditions[m] = rule->scope.conditions[m];
        }

        for (m = 0; !variant->terminal && m < variant->clauses[k].count; m++) {
          block->conditions[rule->scope.count + m] = variant->clauses[k].conditions[m];
        }

        r_filter_conditions_sort(block->conditions, block->condition_count);
      }

      if (variant->terminal) {
        reached_baseline = FALSE;

        break;
      }
    }

    if (reached_baseline && rule->has_action) {
      block = r_filter_block_create(compilation, blocks);
      block->role = "baseline";
      block->rule = rule;
      block->show = rule->show;
      block->style = rule->style;
      block->condition_count = rule->scope.count;
      block->conditions = rule->scope.conditions;

      r_filter_conditions_sort(block->conditions, block->condition_count);
    }
  }

  filter = (RuleforgeFilter *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*filter));
  filter->count = (uint32)nexus_data_list_length_get(blocks);
  filter->blocks =
      (RuleforgeBlock **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)filter->count + 1U) * NEXUS_SIZEOF(RuleforgeBlock *));

  for (i = 0; i < filter->count; i++) {
    filter->blocks[i] = (RuleforgeBlock *)nexus_data_list_pointer_at(blocks, i);
  }

  nexus_data_list_destroy(blocks);

  ruleforge_filter_optimize(compilation, filter);
  return filter;
}

/* ---------------------------------------------------------------------------- */
/* TEXT                                                                         */
/* ---------------------------------------------------------------------------- */

/* Summary: the condition as a filter line: Keyword [operator] values. */
static void r_filter_condition_write(const RuleforgeCondition *condition, NexusStringBuilder *out) {
  uint32 i;

  (void)nexus_strings_string_builder_append_formatted(out, "\t%s", condition->info->keyword);

  if (condition->info->type == RCT_BOOLEAN) {
    (void)nexus_strings_string_builder_append(out, condition->numbers[0] != 0 ? " True\n" : " False\n");

    return;
  }

  if (!condition->substring) {
    (void)nexus_strings_string_builder_append_formatted(out, " %s", ruleforge_manifest_operator_text_get(condition->op));
  }
  if (condition->counted) {
    (void)nexus_strings_string_builder_append_formatted(out, " %lld", (long long)condition->threshold);
  }

  for (i = 0; i < condition->count; i++) {
    if (condition->info->type == RCT_INTEGER) {
      (void)nexus_strings_string_builder_append_formatted(out, " %lld", (long long)condition->numbers[i]);
    } else if (condition->info->type == RCT_ENUM) {
      (void)nexus_strings_string_builder_append_formatted(out, " %s",
                                                          ruleforge_condition_value_name_get(condition->info, condition->numbers[i]));
    } else {
      (void)nexus_strings_string_builder_append_formatted(out, " \"%s\"", condition->strings[i]);
    }
  }

  (void)nexus_strings_string_builder_append(out, "\n");
}

char *ruleforge_filter_text_write(RuleforgeCompilation *compilation, RuleforgeFilter *filter, const char *name, uint64 hash) {
  NexusStringBuilder out;
  char              *result;
  uint32             i;
  uint32             j;
  uint32             line;
  uint_large         k;
  uint_large         length;
  const char        *section;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && filter != NULL, "ruleforge_filter_text_write: NULL argument");

  out = nexus_strings_string_builder_create_growable(4096);
  (void)nexus_strings_string_builder_append_formatted(
      &out, "# Generated by Ruleforge %s (language 1.0.0). Do not edit: change the .rf sources and compile again.\n", ruleforge_version_get());
  (void)nexus_strings_string_builder_append_formatted(&out, "# Filter: %s\n# Source hash: %016llx\n# Blocks: %lu\n\n",
                                                      name == NULL ? "ruleforge" : name, (unsigned long long)hash,
                                                      (unsigned long)filter->count);
  line = 6;
  section = NULL;

  for (i = 0; i < filter->count; i++) {
    RuleforgeBlock               *block;
    const RuleforgeStyleProperty *properties;
    uint32                        property_count;

    block = filter->blocks[i];

    if (block->rule->section != NULL && (section == NULL || !nexus_strings_string_equals(section, block->rule->section))) {
      const char *title;
      section = block->rule->section;
      (void)nexus_strings_string_builder_append(&out, "# ========================================================================\n# ");
      for (title = section; *title != '\0'; title++) {
        char one[2];
        one[0] = *title == '\n' || *title == '\r' ? ' ' : *title;
        one[1] = '\0';
        (void)nexus_strings_string_builder_append(&out, one);
      }
      (void)nexus_strings_string_builder_append(&out, "\n# ========================================================================\n\n");
      line += 4;
    }
    block->first_line = line;

    if (block->native != NULL) {
      (void)nexus_strings_string_builder_append_formatted(&out, "%s\n", block->native);
      length = nexus_strings_string_length_get(block->native);

      line++;

      for (k = 0; k < length; k++) {
        if (block->native[k] == '\n') {
          line++;
        }
      }
    } else {
      (void)nexus_strings_string_builder_append(&out, block->show ? "Show\n" : "Hide\n");
      line++;

      for (j = 0; j < block->condition_count; j++) {
        r_filter_condition_write(block->conditions[j], &out);
        line++;
      }

      if (block->style != NULL) {
        properties = block->show ? block->style->show : block->style->hide;
        property_count = block->show ? block->style->show_count : block->style->hide_count;

        for (j = 0; j < property_count; j++) {
          (void)nexus_strings_string_builder_append_formatted(&out, "\t%s %s\n", properties[j].keyword, properties[j].text);
          line++;
        }
      }
    }

    block->last_line = line - 1;
    (void)nexus_strings_string_builder_append(&out, "\n");
    line++;
  }

  result = nexus_data_arena_text_copy(&compilation->arena, nexus_strings_string_builder_string_get(&out),
                                      nexus_strings_string_builder_length_get(&out));
  nexus_strings_string_builder_destroy(&out);

  return result;
}

const char *ruleforge_filter_action_name_get(const RuleforgeBlock *block) {
  if (block->native != NULL) {
    return "native";
  }

  return block->show ? "show" : "hide";
}
