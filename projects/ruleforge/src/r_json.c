#include <nexus.h>

#include <logos.h>

#include <json_bindings.h>

#include "../ruleforge.h"
#include "./r_internal.h"

/*
JSON is parsed by Genesis (the Logos library's JSON grammar, JSON.gen, through its generated views). This file only turns the
syntax tree into values: it decodes the escapes of string tokens and the digits of number tokens, which the grammar
recognizes but does not interpret.
*/

typedef struct RJsonBuilder {
  NexusDataArena    *arena;
  const LParserTree *tree;
  char              *error;
  uint_large         error_size;
} RJsonBuilder;

static RuleforgeJson *r_json_element(RJsonBuilder *builder, JsonElement view);

/* ---------------------------------------------------------------------------- */
/* TOKENS                                                                       */
/* ---------------------------------------------------------------------------- */

static uint32 r_json_hex_digit(byte digit) {
  if (digit >= '0' && digit <= '9') {
    return (uint32)(digit - '0');
  }

  if (digit >= 'a' && digit <= 'f') {
    return (uint32)(digit - 'a' + 10);
  }

  return (uint32)(digit - 'A' + 10);
}

/* Summary: the value of a \uXXXX escape token text "uXXXX". */
static uint32 r_json_unicode_value(const byte *text) {
  uint32 value;
  uint32 i;

  value = 0;

  for (i = 1; i <= 4; i++) {
    value = (value << 4) | r_json_hex_digit(text[i]);
  }

  return value;
}

/* Summary: the decoded text of a string rule: its content tokens and escapes in order. */
static char *r_json_string_decode(RJsonBuilder *builder, JsonString view) {
  NexusStringBuilder out;
  char              *result;
  uint32             i;
  uint32             length;
  byte               encoded[8];
  uint32             k;
  char               one[2];

  out = nexus_strings_string_builder_create_growable(32);

  for (i = 0; i < json_string_count_rule_string_part(view); i++) {
    JsonStringPart part;

    part = json_string_rule_string_part(view, i);

    if (json_string_part_count_token_string_content(part) > 0) {
      const byte *text;
      uint_large  size;

      text = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, json_string_part_token_string_content(part, 0), &size);
      (void)nexus_strings_string_builder_append_formatted(&out, "%.*s", (int)size, (const char *)text);
    } else {
      JsonStringEscape escape;
      const byte      *text;
      uint_large       size;

      escape = json_string_part_rule_string_escape(part, 0);

      if (json_string_escape_count_token_escape_simple(escape) > 0) {
        text   = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, json_string_escape_token_escape_simple(escape, 0), &size);
        one[1] = '\0';

        switch (text[0]) {
        case 'n':
          one[0] = '\n';
          break;
        case 't':
          one[0] = '\t';
          break;
        case 'r':
          one[0] = '\r';
          break;
        case 'b':
          one[0] = '\b';
          break;
        case 'f':
          one[0] = '\f';
          break;
        default:
          one[0] = (char)text[0];
          break;
        }

        (void)nexus_strings_string_builder_append(&out, one);
      } else {
        uint32 code;

        text = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, json_string_escape_token_escape_unicode(escape, 0), &size);
        code = r_json_unicode_value(text);

        /* A high surrogate pairs with the \uXXXX escape that follows it. */
        if (code >= 0xD800U && code < 0xDC00U && i + 1 < json_string_count_rule_string_part(view)) {
          JsonStringPart next;

          next = json_string_rule_string_part(view, i + 1);

          if (json_string_part_count_rule_string_escape(next) > 0 &&
              json_string_escape_count_token_escape_unicode(json_string_part_rule_string_escape(next, 0)) > 0) {
            const byte *low_text;
            uint_large  low_size;
            uint32      low;

            low_text = logos_language_tooling_parsing_syntax_green_node_text_get(
                builder->tree, json_string_escape_token_escape_unicode(json_string_part_rule_string_escape(next, 0), 0), &low_size);
            low = r_json_unicode_value(low_text);

            if (low >= 0xDC00U && low < 0xE000U) {
              code = 0x10000U + ((code - 0xD800U) << 10) + (low - 0xDC00U);
              i++;
            }
          }
        }

        length = nexus_strings_utf8_encode(code, encoded);

        for (k = 0; k < length; k++) {
          one[0] = (char)encoded[k];
          one[1] = '\0';
          (void)nexus_strings_string_builder_append(&out, one);
        }
      }
    }
  }

  result = nexus_data_arena_text_copy(builder->arena, nexus_strings_string_builder_string_get(&out), nexus_strings_string_builder_length_get(&out));
  nexus_strings_string_builder_destroy(&out);

  return result;
}

/* Summary: reads the digits of a number token: an integer when it has no fraction or exponent. */
static void r_json_number_read(const byte *text, uint_large size, RuleforgeJson *node) {
  uint_large i;
  boolean    negative;
  f_real     whole;
  f_real     scale;
  int64      integer;
  boolean    fraction;
  int32      exponent;
  boolean    exponent_negative;
  int32      step;

  negative       = size > 0 && text[0] == '-';
  i              = negative ? 1 : 0;
  whole          = 0.0;
  scale          = 1.0;
  integer        = 0;
  fraction       = FALSE;
  node->integral = TRUE;

  for (; i < size && text[i] != 'e' && text[i] != 'E'; i++) {
    if (text[i] == '.') {
      fraction       = TRUE;
      node->integral = FALSE;
    } else if (fraction) {
      scale *= 0.1;
      whole += (f_real)(text[i] - '0') * scale;
    } else {
      whole   = (whole * 10.0) + (f_real)(text[i] - '0');
      integer = (integer * 10) + (int64)(text[i] - '0');
    }
  }

  if (i < size) {
    node->integral    = FALSE;
    exponent          = 0;
    exponent_negative = FALSE;
    i++;

    if (i < size && (text[i] == '-' || text[i] == '+')) {
      exponent_negative = text[i] == '-';
      i++;
    }

    for (; i < size; i++) {
      exponent = (exponent * 10) + (int32)(text[i] - '0');

      if (exponent > 400) {
        exponent = 400;
      }
    }

    for (step = 0; step < exponent; step++) {
      whole = exponent_negative ? whole / 10.0 : whole * 10.0;
    }
  }

  node->real    = negative ? -whole : whole;
  node->integer = negative ? -integer : integer;
}

/* ---------------------------------------------------------------------------- */
/* TREE                                                                         */
/* ---------------------------------------------------------------------------- */

static RuleforgeJson *r_json_node(RJsonBuilder *builder, RuleforgeJsonKind kind) {
  RuleforgeJson *node;

  node       = (RuleforgeJson *)nexus_data_arena_allocate(builder->arena, NEXUS_SIZEOF(*node));
  node->kind = kind;

  return node;
}

static RuleforgeJson *r_json_element(RJsonBuilder *builder, JsonElement view) {
  RuleforgeJson *node;
  uint32         i;

  if (json_element_count_rule_object(view) > 0) {
    JsonObject object;

    object      = json_element_rule_object(view, 0);
    node        = r_json_node(builder, RJK_OBJECT);
    node->count = json_object_count_rule_key_value_pair(object);
    node->items = (RuleforgeJson **)nexus_data_arena_allocate(builder->arena, ((uint_large)node->count + 1U) * NEXUS_SIZEOF(RuleforgeJson *));
    node->keys  = (const char **)nexus_data_arena_allocate(builder->arena, ((uint_large)node->count + 1U) * NEXUS_SIZEOF(const char *));

    for (i = 0; i < node->count; i++) {
      JsonKeyValuePair pair;

      pair           = json_object_rule_key_value_pair(object, i);
      node->keys[i]  = r_json_string_decode(builder, json_key_value_pair_rule_string(pair, 0));
      node->items[i] = r_json_element(builder, json_key_value_pair_rule_element(pair, 0));
    }

    return node;
  }

  if (json_element_count_rule_array(view) > 0) {
    JsonArray array;

    array       = json_element_rule_array(view, 0);
    node        = r_json_node(builder, RJK_ARRAY);
    node->count = json_array_count_rule_element(array);
    node->items = (RuleforgeJson **)nexus_data_arena_allocate(builder->arena, ((uint_large)node->count + 1U) * NEXUS_SIZEOF(RuleforgeJson *));

    for (i = 0; i < node->count; i++) {
      node->items[i] = r_json_element(builder, json_array_rule_element(array, i));
    }

    return node;
  }

  if (json_element_count_rule_string(view) > 0) {
    node       = r_json_node(builder, RJK_STRING);
    node->text = r_json_string_decode(builder, json_element_rule_string(view, 0));

    return node;
  }

  if (json_element_count_rule_number(view) > 0) {
    const byte *text;
    uint_large  size;

    node = r_json_node(builder, RJK_NUMBER);
    text = logos_language_tooling_parsing_syntax_green_node_text_get(builder->tree, json_number_token_number(json_element_rule_number(view, 0), 0),
                                                                     &size);
    r_json_number_read(text, size, node);

    return node;
  }

  if (json_element_count_token_true(view) > 0) {
    node       = r_json_node(builder, RJK_BOOL);
    node->flag = TRUE;

    return node;
  }

  if (json_element_count_token_false(view) > 0) {
    return r_json_node(builder, RJK_BOOL);
  }

  return r_json_node(builder, RJK_NULL);
}

RuleforgeJson *ruleforge_json_parse(RuleforgeRuntime *runtime, NexusDataArena *arena, const char *text, uint_large size, char *error,
                                    uint_large error_size) {
  RJsonBuilder   builder;
  LParserTree   *tree;
  RuleforgeJson *root;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL && arena != NULL && text != NULL && error != NULL, "ruleforge_json_parse: NULL argument");

  tree = ruleforge_syntax_text_parse(runtime, RSK_JSON, (const byte *)text, size, error, error_size);

  if (tree == NULL) {
    return NULL;
  }

  builder.arena      = arena;
  builder.tree       = tree;
  builder.error      = error;
  builder.error_size = error_size;
  root               = r_json_element(&builder, json_document_rule_element(json_root(tree), 0));
  logos_language_tooling_parsing_syntax_tree_destroy(tree);

  return root;
}

const RuleforgeJson *ruleforge_json_member_get(const RuleforgeJson *object, const char *key) {
  uint32 i;

  NEXUS_PROGRAM_PRECONDITION(key != NULL, "ruleforge_json_member_get: NULL key");

  if (object == NULL || object->kind != RJK_OBJECT) {
    return NULL;
  }

  for (i = 0; i < object->count; i++) {
    if (nexus_strings_string_equals(object->keys[i], key)) {
      return object->items[i];
    }
  }

  return NULL;
}
