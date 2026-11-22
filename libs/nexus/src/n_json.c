#include "../nexus.h"

typedef struct NexusJsonParser {
  NexusDataArena *arena;
  const char     *text;
  uint_large      size;
  uint_large      position;
  uint32          depth;
  boolean         ok;
} NexusJsonParser;

#define NEXUS_JSON_DEPTH_MAX 64

static void n_json_blank_skip(NexusJsonParser *parser) {
  while (parser->position < parser->size && (parser->text[parser->position] == ' ' || parser->text[parser->position] == '\t' ||
                                             parser->text[parser->position] == '\n' || parser->text[parser->position] == '\r')) {
    parser->position++;
  }
}

static NexusJson *n_json_new(NexusJsonParser *parser, NexusJsonKind kind) {
  NexusJson *value;

  value       = (NexusJson *)nexus_data_arena_allocate(parser->arena, NEXUS_SIZEOF(*value));
  value->kind = kind;

  return value;
}

static int32 n_json_hex(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }

  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }

  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }

  return -1;
}

/* Summary: a quoted string, decoded to UTF-8 into the arena. */
static const char *n_json_string(NexusJsonParser *parser) {
  char      *out;
  uint_large length;
  uint32     scalar;
  uint32     low;
  byte       encoded[4];
  uint32     encoded_length;
  uint32     i;

  out    = (char *)nexus_data_arena_allocate(parser->arena, parser->size - parser->position + 1);
  length = 0;
  parser->position++;

  while (parser->position < parser->size && parser->text[parser->position] != '"') {
    char value;

    value = parser->text[parser->position++];

    if (value != '\\') {
      out[length++] = value;
      continue;
    }

    if (parser->position >= parser->size) {
      parser->ok = FALSE;

      return out;
    }

    value = parser->text[parser->position++];

    switch (value) {
    case 'n':
      out[length++] = '\n';
      break;
    case 't':
      out[length++] = '\t';
      break;
    case 'r':
      out[length++] = '\r';
      break;
    case 'b':
      out[length++] = '\b';
      break;
    case 'f':
      out[length++] = '\f';
      break;
    case 'u':
      scalar = 0;

      for (i = 0; i < 4 && parser->position < parser->size; i++) {
        int32 digit;

        digit = n_json_hex(parser->text[parser->position++]);

        if (digit < 0) {
          parser->ok = FALSE;

          return out;
        }

        scalar = (scalar << 4) | (uint32)digit;
      }

      /* A high surrogate pairs with the escape that follows it. */
      if (scalar >= 0xD800 && scalar <= 0xDBFF && parser->position + 5 < parser->size && parser->text[parser->position] == '\\' &&
          parser->text[parser->position + 1] == 'u') {
        low = 0;
        parser->position += 2;

        for (i = 0; i < 4 && parser->position < parser->size; i++) {
          int32 digit;

          digit = n_json_hex(parser->text[parser->position++]);
          low   = (low << 4) | (uint32)(digit < 0 ? 0 : digit);
        }

        scalar = 0x10000 + ((scalar - 0xD800) << 10) + (low - 0xDC00);
      }

      encoded_length = nexus_strings_utf8_encode(scalar, encoded);

      for (i = 0; i < encoded_length; i++) {
        out[length++] = (char)encoded[i];
      }
      break;
    default:
      out[length++] = value;
      break;
    }
  }

  if (parser->position >= parser->size) {
    parser->ok = FALSE;

    return out;
  }

  parser->position++;
  out[length] = '\0';

  return out;
}

static NexusJson *n_json_value(NexusJsonParser *parser);

static NexusJson *n_json_container(NexusJsonParser *parser, boolean is_object) {
  NexusJson     *value;
  NexusDataList *items;
  NexusDataList *keys;
  char           close;
  uint_large     i;
  NexusJson    **item_array;
  const char   **key_array;

  value = n_json_new(parser, is_object ? NJK_OBJECT : NJK_ARRAY);
  items = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  keys  = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  close = is_object ? '}' : ']';
  parser->position++;
  parser->depth++;
  n_json_blank_skip(parser);

  if (parser->depth > NEXUS_JSON_DEPTH_MAX) {
    parser->ok = FALSE;
  }

  if (parser->ok && parser->position < parser->size && parser->text[parser->position] == close) {
    parser->position++;
  } else {
    while (parser->ok) {
      const char *key;

      n_json_blank_skip(parser);
      key = NULL;

      if (is_object) {
        if (parser->position >= parser->size || parser->text[parser->position] != '"') {
          parser->ok = FALSE;
          break;
        }

        key = n_json_string(parser);
        n_json_blank_skip(parser);

        if (parser->position >= parser->size || parser->text[parser->position] != ':') {
          parser->ok = FALSE;
          break;
        }

        parser->position++;
      }

      (void)nexus_data_list_append_pointer(keys, key);
      (void)nexus_data_list_append_pointer(items, n_json_value(parser));
      n_json_blank_skip(parser);

      if (parser->position < parser->size && parser->text[parser->position] == ',') {
        parser->position++;
        continue;
      }

      if (parser->position < parser->size && parser->text[parser->position] == close) {
        parser->position++;
        break;
      }

      parser->ok = FALSE;
    }
  }

  parser->depth--;
  value->count = (uint32)nexus_data_list_length_get(items);
  item_array   = (NexusJson **)nexus_data_arena_allocate(parser->arena, (value->count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  key_array    = (const char **)nexus_data_arena_allocate(parser->arena, (value->count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  for (i = 0; i < nexus_data_list_length_get(items); i++) {
    item_array[i] = (NexusJson *)nexus_data_list_pointer_at(items, i);
    key_array[i]  = (const char *)nexus_data_list_pointer_at(keys, i);
  }

  value->items = item_array;
  value->keys  = key_array;
  nexus_data_list_destroy(items);
  nexus_data_list_destroy(keys);

  return value;
}

static NexusJson *n_json_word(NexusJsonParser *parser, const char *word, NexusJsonKind kind, boolean boolean_value) {
  uint_large length;
  NexusJson *value;

  length = nexus_strings_string_length_get(word);

  if (parser->position + length > parser->size || !nexus_memory_bytes_equal(parser->text + parser->position, word, length)) {
    parser->ok = FALSE;

    return n_json_new(parser, NJK_NULL);
  }

  parser->position += length;
  value                = n_json_new(parser, kind);
  value->boolean_value = boolean_value;

  return value;
}

static NexusJson *n_json_value(NexusJsonParser *parser) {
  NexusJson *value;
  char       first;

  n_json_blank_skip(parser);

  if (!parser->ok || parser->position >= parser->size) {
    parser->ok = FALSE;

    return n_json_new(parser, NJK_NULL);
  }

  first = parser->text[parser->position];

  if (first == '{' || first == '[') {
    return n_json_container(parser, first == '{');
  }

  if (first == '"') {
    value       = n_json_new(parser, NJK_STRING);
    value->text = n_json_string(parser);

    return value;
  }

  if (first == 't') {
    return n_json_word(parser, "true", NJK_BOOLEAN, TRUE);
  }

  if (first == 'f') {
    return n_json_word(parser, "false", NJK_BOOLEAN, FALSE);
  }

  if (first == 'n') {
    return n_json_word(parser, "null", NJK_NULL, FALSE);
  }

  if (first == '-' || (first >= '0' && first <= '9')) {
    boolean negative;

    value    = n_json_new(parser, NJK_NUMBER);
    negative = first == '-';

    if (negative) {
      parser->position++;
    }

    while (parser->position < parser->size && parser->text[parser->position] >= '0' && parser->text[parser->position] <= '9') {
      value->integer = (value->integer * 10) + (parser->text[parser->position] - '0');
      parser->position++;
    }

    /* A fraction or an exponent is skipped: the protocol's numbers are integers. */
    while (parser->position < parser->size &&
           (parser->text[parser->position] == '.' || parser->text[parser->position] == 'e' || parser->text[parser->position] == 'E' ||
            parser->text[parser->position] == '+' || parser->text[parser->position] == '-' ||
            (parser->text[parser->position] >= '0' && parser->text[parser->position] <= '9'))) {
      parser->position++;
    }

    if (negative) {
      value->integer = -value->integer;
    }

    return value;
  }

  parser->ok = FALSE;

  return n_json_new(parser, NJK_NULL);
}

const NexusJson *nexus_json_parse(NexusDataArena *arena, const char *text, uint_large size) {
  NexusJsonParser parser;
  NexusJson      *value;

  parser.arena    = arena;
  parser.text     = text;
  parser.size     = size;
  parser.position = 0;
  parser.depth    = 0;
  parser.ok       = TRUE;
  value           = n_json_value(&parser);
  n_json_blank_skip(&parser);

  return parser.ok && parser.position == parser.size ? value : NULL;
}

const NexusJson *nexus_json_member_get(const NexusJson *object, const char *key) {
  uint32 i;

  if (object == NULL || object->kind != NJK_OBJECT) {
    return NULL;
  }

  for (i = 0; i < object->count; i++) {
    if (nexus_strings_string_equals(object->keys[i], key)) {
      return object->items[i];
    }
  }

  return NULL;
}

const char *nexus_json_member_text_get(const NexusJson *object, const char *key) {
  const NexusJson *value;

  value = nexus_json_member_get(object, key);

  return value != NULL && value->kind == NJK_STRING ? value->text : NULL;
}

int64 nexus_json_member_integer_get(const NexusJson *object, const char *key, int64 fallback) {
  const NexusJson *value;

  value = nexus_json_member_get(object, key);

  return value != NULL && value->kind == NJK_NUMBER ? value->integer : fallback;
}

NexusError *nexus_json_quote_append(NexusStringBuilder *builder, const char *text) {
  NexusError *error;
  char        one[2];

  one[1] = '\0';
  error  = nexus_strings_string_builder_append(builder, "\"");

  for (; error == NEXUS_PROGRAM_ERROR_SUCCESS() && *text != '\0'; text++) {
    switch (*text) {
    case '"':
      error = nexus_strings_string_builder_append(builder, "\\\"");
      break;
    case '\\':
      error = nexus_strings_string_builder_append(builder, "\\\\");
      break;
    case '\n':
      error = nexus_strings_string_builder_append(builder, "\\n");
      break;
    case '\r':
      error = nexus_strings_string_builder_append(builder, "\\r");
      break;
    case '\t':
      error = nexus_strings_string_builder_append(builder, "\\t");
      break;
    default:
      if ((byte)*text < 0x20) {
        error = nexus_strings_string_builder_append_formatted(builder, "\\u%04x", (unsigned)(byte)*text);
      } else {
        one[0] = *text;
        error  = nexus_strings_string_builder_append(builder, one);
      }
      break;
    }
  }

  return error == NEXUS_PROGRAM_ERROR_SUCCESS() ? nexus_strings_string_builder_append(builder, "\"") : error;
}
