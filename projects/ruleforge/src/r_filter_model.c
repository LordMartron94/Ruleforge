#include <nexus.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
The model is the filter as data: every block with its structured conditions and style, the source location of the rule that
produced it, the chain of includes, calls, templates and dataset rows it came through, and the lines it occupies in the filter
text. It is written from the same blocks the text is, so the inspector can never disagree with the filter.
*/

static const char *r_filter_model_kind_name(RuleforgeProvenanceKind kind) {
  switch (kind) {
  case RPK_DECLARATION:
    return "declaration";
  case RPK_INCLUDE:
    return "include";
  case RPK_CALL:
    return "call";
  case RPK_ROW:
    return "row";
  case RPK_TEMPLATE:
    return "template";
  case RPK_TRANSFORM:
    return "transform";
  case RPK_COUNT:
    break;
  }

  return "declaration";
}

void ruleforge_filter_condition_json_write(const RuleforgeCondition *condition, NexusStringBuilder *out) {
  uint32 i;

  (void)nexus_strings_string_builder_append(out, "{\"name\":");
  (void)nexus_json_quote_append(out, condition->info->name);
  (void)nexus_strings_string_builder_append_formatted(out, ",\"keyword\":\"%s\",\"op\":\"%s\",\"values\":[", condition->info->keyword,
                                                      ruleforge_manifest_operator_text_get(condition->op));

  for (i = 0; i < condition->count; i++) {
    if (i > 0) {
      (void)nexus_strings_string_builder_append(out, ",");
    }

    if (condition->info->type == RCT_INTEGER) {
      (void)nexus_strings_string_builder_append_formatted(out, "%lld", (long long)condition->numbers[i]);
    } else if (condition->info->type == RCT_BOOLEAN) {
      (void)nexus_strings_string_builder_append(out, condition->numbers[i] != 0 ? "true" : "false");
    } else if (condition->info->type == RCT_RARITY) {
      (void)nexus_json_quote_append(out, ruleforge_manifest_rarity_name_get(condition->numbers[i]));
    } else {
      (void)nexus_json_quote_append(out, condition->strings[i]);
    }
  }

  (void)nexus_strings_string_builder_append(out, "]}");
}

void ruleforge_filter_style_json_write(const RuleforgeStyle *style, boolean show, NexusStringBuilder *out) {
  const RuleforgeStyleProperty *properties;
  uint32                        count;
  uint32                        i;

  if (style == NULL) {
    (void)nexus_strings_string_builder_append(out, "null");

    return;
  }

  properties = show ? style->show : style->hide;
  count      = show ? style->show_count : style->hide_count;

  (void)nexus_strings_string_builder_append(out, "{\"theme\":");
  (void)nexus_json_quote_append(out, style->theme == NULL ? "" : style->theme);
  (void)nexus_strings_string_builder_append(out, ",\"key\":");
  (void)nexus_json_quote_append(out, style->key == NULL ? "" : style->key);
  (void)nexus_strings_string_builder_append(out, ",\"properties\":[");

  for (i = 0; i < count; i++) {
    if (i > 0) {
      (void)nexus_strings_string_builder_append(out, ",");
    }

    (void)nexus_strings_string_builder_append(out, "{\"name\":");
    (void)nexus_json_quote_append(out, properties[i].name);
    (void)nexus_strings_string_builder_append_formatted(out, ",\"keyword\":\"%s\",\"value\":", properties[i].keyword);
    (void)nexus_json_quote_append(out, properties[i].text);
    (void)nexus_strings_string_builder_append(out, "}");
  }

  (void)nexus_strings_string_builder_append(out, "]}");
}

static void r_filter_model_span_write(const RuleforgeCompilation *compilation, RuleforgeSpan span, NexusStringBuilder *out) {
  const RuleforgeFile *file;
  uint32               line;
  uint32               column;
  uint32               end_line;
  uint32               end_column;

  file = ruleforge_source_get(compilation, span.file);
  ruleforge_text_position_get(file, span.start, &line, &column);
  ruleforge_text_position_get(file, span.end, &end_line, &end_column);

  (void)nexus_strings_string_builder_append(out, "\"file\":");
  (void)nexus_json_quote_append(out, file->name);
  (void)nexus_strings_string_builder_append(out, ",\"module\":");
  (void)nexus_json_quote_append(out, file->module == NULL ? "" : file->module);
  (void)nexus_strings_string_builder_append_formatted(out, ",\"line\":%lu,\"column\":%lu,\"endLine\":%lu,\"endColumn\":%lu", (unsigned long)line,
                                                      (unsigned long)column, (unsigned long)end_line, (unsigned long)end_column);
}

void ruleforge_filter_model_write(const RuleforgeCompilation *compilation, NexusStringBuilder *out) {
  uint32     i;
  uint32     j;
  uint_large k;

  NEXUS_PROGRAM_PRECONDITION(compilation != NULL && out != NULL && compilation->filter != NULL, "ruleforge_filter_model_write: nothing compiled");

  (void)nexus_strings_string_builder_append_formatted(
      out, "{\"format\":\"ruleforge-model\",\"version\":1,\"compiler\":\"ruleforge %s\",\"language\":\"1.0.0\",\"hash\":\"%016llx\",",
      ruleforge_version_get(), (unsigned long long)compilation->filter_hash);
  (void)nexus_strings_string_builder_append(out, "\"modules\":[");

  for (k = 0; k < nexus_data_list_length_get(compilation->modules); k++) {
    const RuleforgeModule *module;
    const RuleforgeFile   *file;

    module = (const RuleforgeModule *)nexus_data_list_pointer_at(compilation->modules, k);
    file   = ruleforge_source_get(compilation, module->file);

    if (k > 0) {
      (void)nexus_strings_string_builder_append(out, ",");
    }

    (void)nexus_strings_string_builder_append(out, "{\"name\":");
    (void)nexus_json_quote_append(out, module->name == NULL ? "" : module->name);
    (void)nexus_strings_string_builder_append(out, ",\"file\":");
    (void)nexus_json_quote_append(out, file->name);
    (void)nexus_strings_string_builder_append(out, "}");
  }

  (void)nexus_strings_string_builder_append(out, "],\"datasets\":[");

  for (k = 0; k < nexus_data_list_length_get(compilation->datasets); k++) {
    const RuleforgeDataset *dataset;

    dataset = (const RuleforgeDataset *)nexus_data_list_pointer_at(compilation->datasets, k);

    if (k > 0) {
      (void)nexus_strings_string_builder_append(out, ",");
    }

    (void)nexus_strings_string_builder_append(out, "{\"name\":");
    (void)nexus_json_quote_append(out, dataset->name);
    (void)nexus_strings_string_builder_append(out, ",\"source\":");
    (void)nexus_json_quote_append(out, dataset->source);
    (void)nexus_strings_string_builder_append_formatted(out, ",\"rows\":%lu,\"hash\":\"%016llx\"}", (unsigned long)dataset->count,
                                                        (unsigned long long)dataset->hash);
  }

  (void)nexus_strings_string_builder_append(out, "],\"blocks\":[");

  for (i = 0; i < compilation->filter->count; i++) {
    const RuleforgeBlock      *block;
    const RuleforgeProvenance *chain[64];
    uint32                     chain_count;
    const RuleforgeProvenance *link;

    block = compilation->filter->blocks[i];

    if (i > 0) {
      (void)nexus_strings_string_builder_append(out, ",");
    }

    (void)nexus_strings_string_builder_append_formatted(out, "{\"id\":%lu,\"action\":\"%s\",\"role\":\"%s\",\"lines\":[%lu,%lu],\"rule\":{\"name\":",
                                                        (unsigned long)block->id, block->native != NULL ? "native" : (block->show ? "show" : "hide"),
                                                        block->role, (unsigned long)block->first_line, (unsigned long)block->last_line);
    (void)nexus_json_quote_append(out, block->rule->name == NULL ? "" : block->rule->name);
    (void)nexus_strings_string_builder_append(out, ",");
    r_filter_model_span_write(compilation, block->rule->span, out);
    (void)nexus_strings_string_builder_append_formatted(out, ",\"shadowed\":%lu},", (unsigned long)block->rule->shadowed);

    if (block->native != NULL) {
      (void)nexus_strings_string_builder_append(out, "\"native\":");
      (void)nexus_json_quote_append(out, block->native);
      (void)nexus_strings_string_builder_append(out, ",");
    }

    (void)nexus_strings_string_builder_append(out, "\"conditions\":[");

    for (j = 0; j < block->condition_count; j++) {
      if (j > 0) {
        (void)nexus_strings_string_builder_append(out, ",");
      }

      ruleforge_filter_condition_json_write(block->conditions[j], out);
    }

    (void)nexus_strings_string_builder_append(out, "],\"style\":");
    ruleforge_filter_style_json_write(block->style, block->show, out);

    /* The trace is written outermost first: main, the includes and calls, then the rows and the rule itself. */
    chain_count = 0;

    for (link = block->rule->provenance; link != NULL && chain_count < 64; link = link->parent) {
      chain[chain_count++] = link;
    }

    (void)nexus_strings_string_builder_append(out, ",\"trace\":[");

    for (j = chain_count; j > 0; j--) {
      const RuleforgeProvenance *entry;

      entry = chain[j - 1];

      if (j < chain_count) {
        (void)nexus_strings_string_builder_append(out, ",");
      }

      (void)nexus_strings_string_builder_append_formatted(out, "{\"kind\":\"%s\",\"label\":", r_filter_model_kind_name(entry->kind));
      (void)nexus_json_quote_append(out,
                                    entry->record != NULL && entry->record->dataset < nexus_data_list_length_get(compilation->datasets) &&
                                            (entry->label == NULL || entry->label[0] == '\0')
                                        ? ((const RuleforgeDataset *)nexus_data_list_pointer_at(compilation->datasets, entry->record->dataset))->name
                                        : (entry->label == NULL ? "" : entry->label));
      (void)nexus_strings_string_builder_append(out, ",");
      r_filter_model_span_write(compilation, entry->span, out);

      if (entry->record != NULL) {
        RuleforgeValue record;

        record.kind          = RVK_RECORD;
        record.is_identifier = FALSE;
        record.as.record     = entry->record;
        (void)nexus_strings_string_builder_append_formatted(out, ",\"row\":%lu,\"record\":", (unsigned long)entry->record->row);
        ruleforge_value_json_write(&record, out);
      }

      (void)nexus_strings_string_builder_append(out, "}");
    }

    (void)nexus_strings_string_builder_append(out, "]}");
  }

  (void)nexus_strings_string_builder_append(out, "]}");
}
