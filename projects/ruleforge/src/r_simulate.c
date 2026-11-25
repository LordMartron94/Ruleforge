#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
The simulator evaluates items against the compiled blocks exactly as the game would: top to bottom, the first block whose
conditions all hold wins. For every item it reports the winner, why each earlier block did not match (the first condition
that failed and the item's actual value) and the later blocks that would also have matched, which shows what a rule shadows.
*/

/* Summary: an ASCII letter in lower case; every other byte as it is. */
static char r_simulate_lower(char ch) {
  if (ch >= 'A' && ch <= 'Z') {
    return (char)(ch + 32);
  }

  return ch;
}

static boolean r_simulate_equals_text(const char *left, const char *right) {
  uint_large i;

  for (i = 0; left[i] != '\0' && right[i] != '\0'; i++) {
    char a;
    char b;

    a = r_simulate_lower(left[i]);
    b = r_simulate_lower(right[i]);

    if (a != b) {
      return FALSE;
    }
  }

  return left[i] == right[i];
}

static boolean r_simulate_compare(int64 actual, RuleforgeOperator operation, int64 expected) {
  switch (operation) {
  case ROP_EQ:
    return actual == expected;
  case ROP_NE:
    return actual != expected;
  case ROP_LT:
    return actual < expected;
  case ROP_LE:
    return actual <= expected;
  case ROP_GT:
    return actual > expected;
  case ROP_GE:
    return actual >= expected;
  }

  return FALSE;
}

/* Summary: case-insensitive fragments, used only when a rule explicitly asks for contains/count matching. */
static boolean r_simulate_contains_text(const char *text, const char *fragment) {
  uint32 i;
  uint32 j;
  for (i = 0; text[i] != '\0'; i++) {
    for (j = 0; fragment[j] != '\0' && text[i + j] != '\0' && r_simulate_lower(text[i + j]) == r_simulate_lower(fragment[j]); j++) {
    }
    if (fragment[j] == '\0') {
      return TRUE;
    }
  }
  return fragment[0] == '\0';
}

/* Summary: socket patterns compare group size and colour multiplicities, rather than the order of socket letters. */
static boolean r_simulate_socket_pattern(const char *actual, const char *pattern, RuleforgeOperator op) {
  int64  total;
  int64  required;
  int64  present[256];
  int64  wanted[256];
  uint32 i;
  uint32 start;
  total = 0;
  required = 0;
  for (i = 0; i < 256; i++) {
    present[i] = 0;
    wanted[i] = 0;
  }
  for (i = 0; actual[i] != '\0'; i++) {
    byte c;
    c = (byte)r_simulate_lower(actual[i]);
    if (c == 'r' || c == 'g' || c == 'b' || c == 'w' || c == 'a' || c == 'd') {
      present[c]++;
      total++;
    }
  }
  for (start = 0; pattern[start] >= '0' && pattern[start] <= '9'; start++) {
    required = required * 10 + pattern[start] - '0';
  }
  for (i = start; pattern[i] != '\0'; i++) {
    byte c;
    c = (byte)r_simulate_lower(pattern[i]);
    wanted[c]++;
  }
  if (required > 0 && !r_simulate_compare(total, op, required)) {
    return FALSE;
  }
  for (i = 0; i < 256; i++) {
    if (wanted[i] > 0 && present[i] < wanted[i]) {
      return FALSE;
    }
  }
  return TRUE;
}

/* Summary: whether one condition holds for the item; writes the item's actual value as JSON into `actual` for the explanation. */
static boolean r_simulate_condition_holds(const RuleforgeCondition *condition, const RuleforgeJson *item, NexusStringBuilder *actual) {
  const RuleforgeJson *value;
  uint32               i;
  uint32               j;
  boolean              any;

  value = ruleforge_json_member_get(item, condition->info->name);

  if (condition->info->type == RCT_BOOLEAN) {
    boolean flag;

    flag = value != NULL && value->kind == RJK_BOOL && value->flag;
    (void)nexus_strings_string_builder_append(actual, flag ? "true" : "false");

    return (flag ? 1 : 0) == condition->numbers[0];
  }

  if (condition->info->type == RCT_INTEGER || condition->info->type == RCT_ENUM) {
    int64 number;

    if (condition->info->type == RCT_ENUM) {
      int32  named;
      uint32 position;

      named = -1;

      for (position = 0; value != NULL && value->kind == RJK_STRING && position < condition->info->value_count; position++) {
        if (nexus_strings_string_equals(condition->info->values[position], value->text)) {
          named = (int32)position;
        }
      }

      if (named < 0) {
        (void)nexus_strings_string_builder_append(actual, "null");

        return FALSE;
      }

      number = named;
      (void)nexus_json_quote_append(actual, ruleforge_condition_value_name_get(condition->info, number));
    } else if (value != NULL && value->kind == RJK_NUMBER) {
      number = value->integer;
      (void)nexus_strings_string_builder_append_formatted(actual, "%lld", (long long)number);
    } else if (value == NULL && (nexus_strings_string_equals(condition->info->name, "quality") ||
                                 nexus_strings_string_equals(condition->info->name, "sockets") ||
                                 nexus_strings_string_equals(condition->info->name, "linkedSockets") ||
                                 nexus_strings_string_equals(condition->info->name, "corruptedMods"))) {
      /* The game counts these as zero on an item that has none. */
      number = 0;
      (void)nexus_strings_string_builder_append(actual, "0");
    } else {
      (void)nexus_strings_string_builder_append(actual, "null");

      return FALSE;
    }

    if (condition->info->type == RCT_ENUM && condition->count > 1) {
      boolean matched;
      matched = FALSE;
      for (i = 0; i < condition->count; i++) {
        matched = matched || number == condition->numbers[i];
      }
      return condition->op == ROP_NE ? !matched : matched;
    }
    return r_simulate_compare(number, condition->op, condition->numbers[0]);
  }

  /* String conditions: the item has one string or a list of them (mods, influences). */
  if (value == NULL) {
    (void)nexus_strings_string_builder_append(actual, "null");

    return condition->op == ROP_NE;
  }

  if (value->kind == RJK_STRING) {
    (void)nexus_json_quote_append(actual, value->text);
  } else if (value->kind == RJK_ARRAY) {
    (void)nexus_strings_string_builder_append(actual, "[");

    for (i = 0; i < value->count; i++) {
      if (i > 0) {
        (void)nexus_strings_string_builder_append(actual, ",");
      }

      (void)nexus_json_quote_append(actual, value->items[i]->kind == RJK_STRING ? value->items[i]->text : "");
    }

    (void)nexus_strings_string_builder_append(actual, "]");
  } else {
    (void)nexus_strings_string_builder_append(actual, "null");

    return FALSE;
  }

  any = FALSE;
  {
    int64  matches;
    uint32 actual_count;
    matches = 0;
    actual_count = value->kind == RJK_ARRAY ? value->count : 1;
    for (j = 0; j < actual_count; j++) {
      const RuleforgeJson *entry;
      boolean              matched;
      entry = value->kind == RJK_ARRAY ? value->items[j] : value;
      matched = FALSE;
      if (entry->kind != RJK_STRING) {
        continue;
      }
      for (i = 0; i < condition->count; i++) {
        if (nexus_strings_string_equals(condition->info->keyword, "SocketGroup")) {
          matched = matched || r_simulate_socket_pattern(entry->text, condition->strings[i], condition->op);
        } else {
          matched = matched || ((condition->substring || condition->counted) ? r_simulate_contains_text(entry->text, condition->strings[i])
                                                                             : r_simulate_equals_text(entry->text, condition->strings[i]));
        }
      }
      any = any || matched;
      if (matched) {
        matches++;
      }
    }
    if (condition->counted) {
      return r_simulate_compare(matches, condition->op, condition->threshold);
    }
  }

  return condition->op == ROP_NE ? !any : any;
}

/* Summary: whether every condition of the block holds; fills `failed` (a JSON array body) with the first one that does not. */
static boolean r_simulate_block_matches(const RuleforgeBlock *block, const RuleforgeJson *item, NexusStringBuilder *failed) {
  uint32 i;

  if (block->native != NULL) {
    /* A native block is raw filter text the engine cannot evaluate. */
    (void)nexus_strings_string_builder_append(failed, "{\"native\":true}");

    return FALSE;
  }

  for (i = 0; i < block->condition_count; i++) {
    NexusStringBuilder actual;
    boolean            holds;

    actual = nexus_strings_string_builder_create_growable(32);
    holds = r_simulate_condition_holds(block->conditions[i], item, &actual);

    if (!holds) {
      (void)nexus_strings_string_builder_append(failed, "{\"condition\":");
      ruleforge_filter_condition_json_write(block->conditions[i], failed);
      (void)nexus_strings_string_builder_append_formatted(failed, ",\"actual\":%s}", nexus_strings_string_builder_string_get(&actual));
      nexus_strings_string_builder_destroy(&actual);

      return FALSE;
    }

    nexus_strings_string_builder_destroy(&actual);
  }

  return TRUE;
}

boolean ruleforge_simulate_json_write(const RuleforgeCompilation *compilation, const char *items_json, NexusStringBuilder *out) {
  NexusDataArena arena;
  RuleforgeJson *items;
  char           error[256];
  uint32         item_count;
  uint32         index;
  uint32         i;
  uint32         matched_id;
  boolean        first_item;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && items_json != NULL && out != NULL, "ruleforge_simulate_json_write: NULL argument");

  nexus_data_arena_initialize(&arena);
  items =
      ruleforge_json_parse(compilation->runtime, &arena, items_json, nexus_strings_string_length_get(items_json), error, NEXUS_SIZEOF(error));

  if (items == NULL || (items->kind != RJK_ARRAY && items->kind != RJK_OBJECT)) {
    nexus_data_arena_release(&arena);

    return FALSE;
  }

  item_count = items->kind == RJK_ARRAY ? items->count : 1;
  first_item = TRUE;
  (void)nexus_strings_string_builder_append(out, "[");

  for (index = 0; index < item_count; index++) {
    const RuleforgeJson  *item;
    const RuleforgeBlock *winner;

    item = items->kind == RJK_ARRAY ? items->items[index] : items;
    winner = NULL;
    matched_id = 0;

    if (!first_item) {
      (void)nexus_strings_string_builder_append(out, ",");
    }

    first_item = FALSE;
    (void)nexus_strings_string_builder_append_formatted(out, "{\"index\":%lu,\"considered\":[", (unsigned long)index);

    for (i = 0; i < compilation->filter->count; i++) {
      NexusStringBuilder    failed;
      const RuleforgeBlock *block;
      boolean               matches;

      block = compilation->filter->blocks[i];
      failed = nexus_strings_string_builder_create_growable(64);
      matches = r_simulate_block_matches(block, item, &failed);

      if (matches) {
        winner = block;
        matched_id = block->id;
        nexus_strings_string_builder_destroy(&failed);

        break;
      }

      if (i > 0) {
        (void)nexus_strings_string_builder_append(out, ",");
      }

      (void)nexus_strings_string_builder_append_formatted(out, "{\"id\":%lu,\"failed\":%s}", (unsigned long)block->id,
                                                          nexus_strings_string_builder_string_get(&failed));
      nexus_strings_string_builder_destroy(&failed);
    }

    (void)nexus_strings_string_builder_append(out, "]");

    if (winner == NULL) {
      /* No block matches: the game shows the item with its defaults. */
      (void)nexus_strings_string_builder_append(out, ",\"matched\":null,\"action\":\"show\",\"default\":true,\"style\":null,\"shadowed\":[]}");

      continue;
    }

    (void)nexus_strings_string_builder_append_formatted(out, ",\"matched\":%lu,\"action\":\"%s\",\"default\":false,\"style\":",
                                                        (unsigned long)matched_id, ruleforge_filter_action_name_get(winner));
    ruleforge_filter_style_json_write(winner->style, winner->show, out);
    (void)nexus_strings_string_builder_append(out, ",\"shadowed\":[");

    {
      boolean first_shadow;

      first_shadow = TRUE;

      for (i = matched_id; i < compilation->filter->count; i++) {
        NexusStringBuilder scratch;

        scratch = nexus_strings_string_builder_create_growable(32);

        if (r_simulate_block_matches(compilation->filter->blocks[i], item, &scratch)) {
          if (!first_shadow) {
            (void)nexus_strings_string_builder_append(out, ",");
          }

          first_shadow = FALSE;
          (void)nexus_strings_string_builder_append_formatted(out, "%lu", (unsigned long)compilation->filter->blocks[i]->id);
        }

        nexus_strings_string_builder_destroy(&scratch);
      }
    }

    (void)nexus_strings_string_builder_append(out, "]}");
  }

  (void)nexus_strings_string_builder_append(out, "]");
  nexus_data_arena_release(&arena);

  return TRUE;
}
