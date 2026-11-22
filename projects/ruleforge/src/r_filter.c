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

  block     = (RuleforgeBlock *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*block));
  block->id = (uint32)nexus_data_list_length_get(blocks) + 1;
  (void)nexus_data_list_append_pointer(blocks, block);

  return block;
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

    rule  = ruleset->rules[i];
    block = NULL;

    if (rule->native != NULL) {
      block         = r_filter_block_create(compilation, blocks);
      block->native = rule->native;
      block->rule   = rule;
      block->role   = "native";

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
        block->role            = nexus_data_arena_text_copy(&compilation->arena, role, nexus_strings_string_length_get(role));
        block->rule            = rule;
        block->show            = variant->has_action ? variant->show : rule->show;
        block->style           = variant->style != NULL ? variant->style : rule->style;
        block->condition_count = rule->scope.count + (variant->terminal ? 0 : variant->clauses[k].count);
        block->conditions      = (RuleforgeCondition **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)block->condition_count + 1U) *
                                                                                                           NEXUS_SIZEOF(RuleforgeCondition *));

        for (m = 0; m < rule->scope.count; m++) {
          block->conditions[m] = rule->scope.conditions[m];
        }

        for (m = 0; !variant->terminal && m < variant->clauses[k].count; m++) {
          block->conditions[rule->scope.count + m] = variant->clauses[k].conditions[m];
        }
      }

      if (variant->terminal) {
        reached_baseline = FALSE;

        break;
      }
    }

    if (reached_baseline && rule->has_action) {
      block                  = r_filter_block_create(compilation, blocks);
      block->role            = "baseline";
      block->rule            = rule;
      block->show            = rule->show;
      block->style           = rule->style;
      block->condition_count = rule->scope.count;
      block->conditions      = rule->scope.conditions;
    }
  }

  filter        = (RuleforgeFilter *)nexus_data_arena_allocate(&compilation->arena, NEXUS_SIZEOF(*filter));
  filter->count = (uint32)nexus_data_list_length_get(blocks);
  filter->blocks =
      (RuleforgeBlock **)nexus_data_arena_allocate(&compilation->arena, ((uint_large)filter->count + 1U) * NEXUS_SIZEOF(RuleforgeBlock *));

  for (i = 0; i < filter->count; i++) {
    filter->blocks[i] = (RuleforgeBlock *)nexus_data_list_pointer_at(blocks, i);
  }

  nexus_data_list_destroy(blocks);

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

  (void)nexus_strings_string_builder_append_formatted(out, " %s", ruleforge_manifest_operator_text_get(condition->op));

  for (i = 0; i < condition->count; i++) {
    if (condition->info->type == RCT_INTEGER) {
      (void)nexus_strings_string_builder_append_formatted(out, " %lld", (long long)condition->numbers[i]);
    } else if (condition->info->type == RCT_RARITY) {
      (void)nexus_strings_string_builder_append_formatted(out, " %s", ruleforge_manifest_rarity_name_get(condition->numbers[i]));
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

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && filter != NULL, "ruleforge_filter_text_write: NULL argument");

  out = nexus_strings_string_builder_create_growable(4096);
  (void)nexus_strings_string_builder_append_formatted(
      &out, "# Generated by Ruleforge %s (language 1.0.0). Do not edit: change the .rf sources and compile again.\n", ruleforge_version_get());
  (void)nexus_strings_string_builder_append_formatted(&out, "# Filter: %s\n# Source hash: %016llx\n# Blocks: %lu\n\n",
                                                      name == NULL ? "ruleforge" : name, (unsigned long long)hash, (unsigned long)filter->count);
  line = 5;

  for (i = 0; i < filter->count; i++) {
    RuleforgeBlock               *block;
    const RuleforgeFile          *file;
    uint32                        rule_line;
    uint32                        rule_column;
    const RuleforgeStyleProperty *properties;
    uint32                        property_count;

    block = filter->blocks[i];
    file  = ruleforge_source_get(compilation, block->rule->span.file);
    ruleforge_text_position_get(file, block->rule->span.start, &rule_line, &rule_column);

    (void)nexus_strings_string_builder_append_formatted(&out, "# rf:%lu %s%s%s (%s:%lu)\n", (unsigned long)block->id,
                                                        block->rule->name == NULL ? "" : block->rule->name, block->rule->name == NULL ? "" : " ",
                                                        block->role, file->module == NULL ? "" : file->module, (unsigned long)rule_line);
    line++;
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
        properties     = block->show ? block->style->show : block->style->hide;
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

  result =
      nexus_data_arena_text_copy(&compilation->arena, nexus_strings_string_builder_string_get(&out), nexus_strings_string_builder_length_get(&out));
  nexus_strings_string_builder_destroy(&out);

  return result;
}

const char *ruleforge_filter_action_name_get(const RuleforgeBlock *block) {
  if (block->native != NULL) {
    return "native";
  }

  return block->show ? "show" : "hide";
}
