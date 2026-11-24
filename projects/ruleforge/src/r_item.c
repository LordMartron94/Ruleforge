#include <nexus.h>
#include <logos.h>
#include <poe_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

char *ruleforge_item_json_write(RuleforgeRuntime *runtime, const char *text, char *error, uint_large error_size) {
  NexusDataArena     arena;
  NexusStringBuilder source;
  NexusStringBuilder fields;
  NexusStringBuilder unknown;
  NexusStringBuilder out;
  LParserTree       *tree;
  PoeDocument        document;
  const char        *item_class;
  const char        *rarity;
  const char        *names[2];
  uint32             name_count;
  uint32             unknown_count;
  uint32             i;
  uint32             section;
  boolean            identified;
  boolean            corrupted;
  boolean            mirrored;
  boolean            fractured;
  boolean            synthesised;
  char              *result;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && text != NULL && error != NULL && error_size > 0, "Item arguments must be non-NULL");

  source = nexus_strings_string_builder_create_growable(512);
  (void)nexus_strings_string_builder_append(&source, text);
  (void)nexus_strings_string_builder_append(&source, "\n");
  tree = ruleforge_syntax_text_parse(runtime, RSK_ITEM, (const byte *)nexus_strings_string_builder_string_get(&source),
                                     nexus_strings_string_builder_length_get(&source), error, error_size);
  nexus_strings_string_builder_destroy(&source);

  if (tree == NULL) {
    return NULL;
  }

  nexus_data_arena_initialize(&arena);
  fields        = nexus_strings_string_builder_create_growable(256);
  unknown       = nexus_strings_string_builder_create_growable(256);
  document      = poe_root(tree);
  item_class    = NULL;
  rarity        = NULL;
  name_count    = 0;
  unknown_count = 0;
  section       = 0;
  identified    = TRUE;
  corrupted     = FALSE;
  mirrored      = FALSE;
  fractured     = FALSE;
  synthesised   = FALSE;

  for (i = 0; i < poe_document_count_rule_line(document); i++) {
    PoeLine     line;
    const char *value;
    uint_large  size;
    char       *copy;

    line = poe_document_rule_line(document, i);

    if (poe_line_count_token_item_class(line) > 0) {
      value      = poe_token_text(tree, poe_line_token_item_class(line, 0), &size);
      item_class = nexus_data_arena_text_copy(&arena, value + 12, size - 12);
    } else if (poe_line_count_token_rarity(line) > 0) {
      value  = poe_token_text(tree, poe_line_token_rarity(line, 0), &size);
      rarity = nexus_data_arena_text_copy(&arena, value + 8, size - 8);
    } else if (poe_line_count_token_separator(line) > 0) {
      section++;
    } else if (poe_line_count_token_text(line) > 0) {
      value = poe_token_text(tree, poe_line_token_text(line, 0), &size);
      copy  = nexus_data_arena_text_copy(&arena, value, size);

      if (section == 0 && rarity != NULL && name_count < 2) {
        names[name_count++] = copy;
      } else if (nexus_strings_string_equals(copy, "Unidentified")) {
        identified = FALSE;
      } else if (nexus_strings_string_equals(copy, "Corrupted")) {
        corrupted = TRUE;
      } else if (nexus_strings_string_equals(copy, "Mirrored")) {
        mirrored = TRUE;
      } else if (nexus_strings_string_equals(copy, "Fractured Item")) {
        fractured = TRUE;
      } else if (nexus_strings_string_equals(copy, "Synthesised Item")) {
        synthesised = TRUE;
      } else if (nexus_strings_string_starts_with(copy, "Sockets: ")) {
        uint32      sockets;
        uint32      linked;
        uint32      group;
        const char *cursor;

        sockets = 0;
        linked  = 0;
        group   = 0;

        for (cursor = copy + 9; *cursor != '\0'; cursor++) {
          if (*cursor == ' ') {
            group = 0;
          } else if (*cursor == 'R' || *cursor == 'G' || *cursor == 'B' || *cursor == 'W' || *cursor == 'A' || *cursor == 'D') {
            sockets++;
            group++;

            if (group > linked) {
              linked = group;
            }
          }
        }

        (void)nexus_strings_string_builder_append_formatted(&fields, ",\"sockets\":%u,\"linkedSockets\":%u", (unsigned)sockets, (unsigned)linked);
      } else {
        const char *field;
        const char *digits;
        uint32      number;

        field  = NULL;
        digits = NULL;

        if (nexus_strings_string_starts_with(copy, "Item Level: ")) {
          field  = "itemLevel";
          digits = copy + 12;
        } else if (nexus_strings_string_starts_with(copy, "Quality: ")) {
          field  = "quality";
          digits = copy + 9;
        } else if (nexus_strings_string_starts_with(copy, "Stack Size: ")) {
          field  = "stackSize";
          digits = copy + 12;
        } else if (nexus_strings_string_starts_with(copy, "Map Tier: ")) {
          field  = "mapTier";
          digits = copy + 10;
        } else if (nexus_strings_string_starts_with(copy, "Level: ") && item_class != NULL && nexus_strings_string_contains(item_class, "Gems")) {
          field  = "gemLevel";
          digits = copy + 7;
        }

        if (digits != NULL && *digits == '+') {
          digits++;
        }

        if (digits != NULL && *digits >= '0' && *digits <= '9') {
          number = 0;

          while (*digits >= '0' && *digits <= '9' && number <= 1000000U) {
            number = (number * 10U) + (uint32)(*digits++ - '0');
          }

          if (number <= 1000000U) {
            (void)nexus_strings_string_builder_append_formatted(&fields, ",\"%s\":%u", field, (unsigned)number);
          }
        } else {
          if (unknown_count++ > 0) {
            (void)nexus_strings_string_builder_append(&unknown, ",");
          }
          (void)nexus_json_quote_append(&unknown, copy);
        }
      }
    }
  }

  result = NULL;

  if (item_class == NULL || rarity == NULL || name_count == 0) {
    (void)stbsp_snprintf(error, (int)error_size, "expected English PoE copied item text with Item Class, Rarity and item name");
  } else {
    out = nexus_strings_string_builder_create_growable(512);
    (void)nexus_strings_string_builder_append(&out, "{\"name\":");
    (void)nexus_json_quote_append(&out, names[0]);
    (void)nexus_strings_string_builder_append(&out, ",\"baseType\":");
    (void)nexus_json_quote_append(&out, names[name_count - 1]);
    (void)nexus_strings_string_builder_append(&out, ",\"class\":");
    (void)nexus_json_quote_append(&out, item_class);
    (void)nexus_strings_string_builder_append(&out, ",\"rarity\":");
    (void)nexus_json_quote_append(&out, rarity);
    (void)nexus_strings_string_builder_append_formatted(
        &out, ",\"identified\":%s,\"corrupted\":%s,\"mirrored\":%s,\"fractured\":%s,\"synthesised\":%s%s,\"_unparsed\":[%s]}",
        identified ? "true" : "false", corrupted ? "true" : "false", mirrored ? "true" : "false", fractured ? "true" : "false",
        synthesised ? "true" : "false", nexus_strings_string_builder_string_get(&fields), nexus_strings_string_builder_string_get(&unknown));
    result = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&out) + 1);
    nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, nexus_strings_string_builder_length_get(&out) + 1);
    nexus_strings_string_builder_destroy(&out);
  }

  nexus_strings_string_builder_destroy(&fields);
  nexus_strings_string_builder_destroy(&unknown);
  nexus_data_arena_release(&arena);
  logos_language_tooling_parsing_syntax_tree_destroy(tree);

  return result;
}
