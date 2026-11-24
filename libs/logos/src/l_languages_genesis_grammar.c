#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
The grammar description is the flat, expanded language rendered for tools: every definition as Genesis text and every
token as an Oniguruma regular expression, plus the references of each rule. It is built once when a language compiles.
*/

typedef struct LGText {
  NexusDataList *bytes; /* char */
} LGText;

static void l_gram_text_open(LGText *text) {
  text->bytes = nexus_data_list_create(NEXUS_SIZEOF(char), 128);
}

static void l_gram_text_char(LGText *text, char value) {
  (void)nexus_data_list_append(text->bytes, &value);
}

static void l_gram_text_add(LGText *text, const char *value) {
  uint_large i;

  for (i = 0; value[i] != '\0'; i++) {
    l_gram_text_char(text, value[i]);
  }
}

static void l_gram_text_format(LGText *text, const char *format, ...) {
  char    buffer[128];
  va_list arguments;

  va_start(arguments, format);
  (void)stbsp_vsnprintf(buffer, (int)sizeof(buffer), format, arguments);
  va_end(arguments);
  l_gram_text_add(text, buffer);
}

/* Summary: the operator of a repetition: ? * + or the counts {n}, {n,} and {n,m}, which Genesis and the regular expressions of Regula both write. */
static void l_gram_repeat_write(LGText *text, const LGExpr *expression) {
  if (expression->repeat_op != '{') {
    l_gram_text_char(text, expression->repeat_op);
  } else if (expression->hi == 0xFFFFFFFFU) {
    l_gram_text_format(text, "{%u,}", (unsigned)expression->lo);
  } else if (expression->hi == expression->lo) {
    l_gram_text_format(text, "{%u}", (unsigned)expression->lo);
  } else {
    l_gram_text_format(text, "{%u,%u}", (unsigned)expression->lo, (unsigned)expression->hi);
  }
}

/* Summary: the text so far, copied into the arena, and the buffer released. */
static const char *l_gram_text_close(LGText *text, NexusDataArena *arena) {
  const char *copy;

  l_gram_text_char(text, '\0');
  copy = nexus_data_arena_text_copy(arena, (const char *)nexus_data_list_data_get_const(text->bytes), nexus_data_list_length_get(text->bytes) - 1);
  nexus_data_list_destroy(text->bytes);
  text->bytes = NULL;

  return copy;
}

/* Summary: a name as a Genesis identifier; hygienic template names such as gap@3 become gap__i3. */
static void l_gram_name(LGText *text, const char *name) {
  uint_large i;

  for (i = 0; name[i] != '\0'; i++) {
    if (name[i] == '@') {
      l_gram_text_add(text, "__i");
    } else {
      l_gram_text_char(text, name[i]);
    }
  }
}

/* ---------------------------------------------------------------------------- */
/* GENESIS TEXT                                                                  */
/* ---------------------------------------------------------------------------- */

static void l_gram_codepoint_genesis(LGText *text, uint32 value) {
  switch (value) {
  case '\'':
    l_gram_text_add(text, "\\'");
    break;
  case '\\':
    l_gram_text_add(text, "\\\\");
    break;
  case '\n':
    l_gram_text_add(text, "\\n");
    break;
  case '\t':
    l_gram_text_add(text, "\\t");
    break;
  case '\r':
    l_gram_text_add(text, "\\r");
    break;
  default:
    if (value >= 0x20 && value < 0x7F) {
      l_gram_text_char(text, (char)value);
    } else {
      l_gram_text_format(text, "\\u{%X}", (unsigned)value);
    }
    break;
  }
}

/* Binding strength: 1 union, 2 concatenation, 3 repetition and atoms. */
static void l_gram_genesis(LGText *text, const LGExpr *expression, uint32 context_strength) {
  uint32  i;
  boolean wrap;

  if (expression == NULL) {
    l_gram_text_add(text, "()");

    return;
  }

  switch (expression->kind) {
  case LGE_EMPTY:
  case LGE_NONE:
    l_gram_text_add(text, "()");
    break;
  case LGE_STRING:
    l_gram_text_char(text, '\'');

    for (i = 0; i < expression->codepoint_count; i++) {
      l_gram_codepoint_genesis(text, expression->codepoints[i]);
    }

    l_gram_text_char(text, '\'');
    break;
  case LGE_TEXT:
    l_gram_text_char(text, '\'');

    for (i = 0; expression->name[i] != '\0'; i++) {
      l_gram_codepoint_genesis(text, (byte)expression->name[i]);
    }

    l_gram_text_char(text, '\'');
    break;
  case LGE_ANY:
    l_gram_text_char(text, '.');
    break;
  case LGE_NOT:
    l_gram_text_char(text, '!');
    l_gram_genesis(text, expression->child, 4);
    break;
  case LGE_RANGE:
    l_gram_text_char(text, '\'');
    l_gram_codepoint_genesis(text, expression->lo);
    l_gram_text_add(text, "'..'");
    l_gram_codepoint_genesis(text, expression->hi);
    l_gram_text_char(text, '\'');
    break;
  case LGE_TOKEN:
  case LGE_FRAGMENT:
    l_gram_text_char(text, '$');
    l_gram_name(text, expression->name);
    break;
  case LGE_RULE:
    if (expression->qualifier != NULL) {
      l_gram_text_add(text, expression->qualifier);
      l_gram_text_char(text, '.');
    }

    l_gram_text_char(text, '#');
    l_gram_name(text, expression->name);
    break;
  case LGE_UNION:
  case LGE_CONCAT:
    wrap = (expression->kind == LGE_UNION ? 1U : 2U) < context_strength;

    if (wrap) {
      l_gram_text_char(text, '(');
    }

    for (i = 0; i < expression->item_count; i++) {
      if (i > 0) {
        l_gram_text_add(text, expression->kind == LGE_UNION ? " || " : " ");
      }

      l_gram_genesis(text, expression->items[i], expression->kind == LGE_UNION ? 2U : 3U);
    }

    if (wrap) {
      l_gram_text_char(text, ')');
    }
    break;
  case LGE_REPEAT:
    l_gram_genesis(text, expression->child, 4);
    l_gram_repeat_write(text, expression);
    break;
  case LGE_GROUP:
    l_gram_text_char(text, '(');
    l_gram_genesis(text, expression->child, 1);
    l_gram_text_char(text, ')');

    for (i = 0; i < expression->annotation_count; i++) {
      uint32 argument;

      l_gram_text_format(text, " @%s(", expression->annotations[i].name);

      for (argument = 0; argument < expression->annotations[i].argument_count; argument++) {
        if (argument > 0) {
          l_gram_text_add(text, ", ");
        }

        l_gram_genesis(text, expression->annotations[i].arguments[argument], 1);
      }

      l_gram_text_char(text, ')');
    }
    break;
  case LGE_ENUM:
    l_gram_text_add(text, expression->qualifier);
    l_gram_text_char(text, '.');
    l_gram_text_add(text, expression->name);
    break;
  case LGE_INT:
    l_gram_text_format(text, "%lld", (long long)expression->integer);
    break;
  case LGE_IDENT:
    l_gram_text_add(text, expression->name);
    break;
  case LGE_CALL:
    l_gram_text_add(text, expression->name);
    l_gram_text_char(text, '(');

    for (i = 0; i < expression->item_count; i++) {
      if (i > 0) {
        l_gram_text_add(text, ", ");
      }

      l_gram_genesis(text, expression->items[i], 1);
    }

    l_gram_text_char(text, ')');
    break;
  case LGE_PARAM:
  case LGE_IF:
  case LGE_COMPARE:
  default:
    l_gram_text_add(text, "''");
    break;
  }
}

/* ---------------------------------------------------------------------------- */
/* REGULAR EXPRESSIONS (ONIGURUMA)                                               */
/* ---------------------------------------------------------------------------- */

/* Summary: one character as a regexp atom or class member: letters, digits, underscore and space stay, other ASCII punctuation is
   backslash-escaped, everything else is \x{...}. */
static void l_gram_codepoint_regexp(LGText *text, uint32 value) {
  if ((value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_' || value == ' ') {
    l_gram_text_char(text, (char)value);
  } else if (value > 0x20 && value < 0x7F) {
    l_gram_text_char(text, '\\');
    l_gram_text_char(text, (char)value);
  } else {
    l_gram_text_format(text, "\\x{%X}", (unsigned)value);
  }
}

/* Summary: whether the expression matches exactly one character from a fixed set, so a union of them can be a class. */
static boolean l_gram_classable(const LGContextModel *context, const LGExpr *expression, uint32 depth) {
  const LGToken *fragment;
  uint32         i;

  if (expression == NULL || depth > 32) {
    return FALSE;
  }

  switch (expression->kind) {
  case LGE_STRING:
    return expression->codepoint_count == 1;
  case LGE_RANGE:
    return TRUE;
  case LGE_GROUP:
    return l_gram_classable(context, expression->child, depth + 1);
  case LGE_FRAGMENT:
    fragment = logos_languages_genesis_analysis_fragment_find(context, expression->name);

    return fragment != NULL && l_gram_classable(context, fragment->expression, depth + 1);
  case LGE_UNION:
    for (i = 0; i < expression->item_count; i++) {
      if (!l_gram_classable(context, expression->items[i], depth + 1)) {
        return FALSE;
      }
    }

    return TRUE;
  default:
    return FALSE;
  }
}

static void l_gram_class_members(LGText *text, const LGContextModel *context, const LGExpr *expression) {
  uint32 i;

  switch (expression->kind) {
  case LGE_STRING:
    l_gram_codepoint_regexp(text, expression->codepoints[0]);
    break;
  case LGE_RANGE:
    l_gram_codepoint_regexp(text, expression->lo);

    if (expression->hi != expression->lo) {
      l_gram_text_char(text, '-');
      l_gram_codepoint_regexp(text, expression->hi);
    }
    break;
  case LGE_GROUP:
    l_gram_class_members(text, context, expression->child);
    break;
  case LGE_FRAGMENT:
    l_gram_class_members(text, context, logos_languages_genesis_analysis_fragment_find(context, expression->name)->expression);
    break;
  case LGE_UNION:
    for (i = 0; i < expression->item_count; i++) {
      l_gram_class_members(text, context, expression->items[i]);
    }
    break;
  default:
    break;
  }
}

/* Summary: 1 when the output is a single regexp atom (a repetition operator may follow directly), 0 otherwise. */
static void l_gram_regexp(LGText *text, const LGContextModel *context, const LGExpr *expression, uint32 depth) {
  uint32 i;
  uint32 length;
  byte   bytes[4];

  if (expression == NULL || depth > 64) {
    l_gram_text_add(text, "(?:)");

    return;
  }

  switch (expression->kind) {
  case LGE_STRING:
    if (expression->codepoint_count == 1) {
      l_gram_codepoint_regexp(text, expression->codepoints[0]);
    } else {
      l_gram_text_add(text, "(?:");

      for (i = 0; i < expression->codepoint_count; i++) {
        l_gram_codepoint_regexp(text, expression->codepoints[i]);
      }

      l_gram_text_char(text, ')');
    }
    break;
  case LGE_TEXT:
    l_gram_text_add(text, "(?:");

    for (i = 0; expression->name[i] != '\0';) {
      /* Text values are UTF-8; decode one scalar at a time. */
      length = 1;

      if (((byte)expression->name[i] & 0xF8) == 0xF0) {
        length = 4;
      } else if (((byte)expression->name[i] & 0xF0) == 0xE0) {
        length = 3;
      } else if (((byte)expression->name[i] & 0xE0) == 0xC0) {
        length = 2;
      }

      if (length == 1) {
        l_gram_codepoint_regexp(text, (byte)expression->name[i]);
      } else {
        uint32 value;
        uint32 part;

        nexus_memory_bytes_clear(bytes, NEXUS_SIZEOF(bytes));
        value = (byte)expression->name[i] & (0xFFU >> (length + 1));

        for (part = 1; part < length; part++) {
          value = (value << 6) | ((byte)expression->name[i + part] & 0x3FU);
        }

        l_gram_codepoint_regexp(text, value);
      }

      i += length;
    }

    l_gram_text_char(text, ')');
    break;
  case LGE_RANGE:
    l_gram_text_char(text, '[');
    l_gram_class_members(text, context, expression);
    l_gram_text_char(text, ']');
    break;
  case LGE_ANY:
    l_gram_text_add(text, "(?s:.)");
    break;
  case LGE_NOT:
    if (l_gram_classable(context, expression->child, 0)) {
      l_gram_text_add(text, "[^");
      l_gram_class_members(text, context, expression->child);
      l_gram_text_char(text, ']');
    } else {
      /* Any one character at which the operand does not match. */
      l_gram_text_add(text, "(?:(?!");
      l_gram_regexp(text, context, expression->child, depth + 1);
      l_gram_text_add(text, ")(?s:.))");
    }
    break;
  case LGE_FRAGMENT:
    l_gram_regexp(text, context, logos_languages_genesis_analysis_fragment_find(context, expression->name)->expression, depth + 1);
    break;
  case LGE_UNION:
    if (l_gram_classable(context, expression, 0)) {
      l_gram_text_char(text, '[');
      l_gram_class_members(text, context, expression);
      l_gram_text_char(text, ']');

      break;
    }

    l_gram_text_add(text, "(?:");

    for (i = 0; i < expression->item_count; i++) {
      if (i > 0) {
        l_gram_text_char(text, '|');
      }

      l_gram_regexp(text, context, expression->items[i], depth);
    }

    l_gram_text_char(text, ')');
    break;
  case LGE_CONCAT:
    l_gram_text_add(text, "(?:");

    for (i = 0; i < expression->item_count; i++) {
      l_gram_regexp(text, context, expression->items[i], depth);
    }

    l_gram_text_char(text, ')');
    break;
  case LGE_REPEAT:
    l_gram_regexp(text, context, expression->child, depth);
    l_gram_repeat_write(text, expression);
    break;
  case LGE_GROUP:
    l_gram_regexp(text, context, expression->child, depth);
    break;
  case LGE_EMPTY:
  default:
    l_gram_text_add(text, "(?:)");
    break;
  }
}

/* Summary: the fixed text a token matches, when it matches exactly one string; FALSE otherwise. */
static boolean l_gram_literal(LGText *text, const LGContextModel *context, const LGExpr *expression, uint32 depth) {
  byte   bytes[4];
  uint32 i;
  uint32 length;
  uint32 position;

  if (expression == NULL || depth > 32) {
    return FALSE;
  }

  switch (expression->kind) {
  case LGE_STRING:
    for (i = 0; i < expression->codepoint_count; i++) {
      length = nexus_strings_utf8_encode(expression->codepoints[i], bytes);

      for (position = 0; position < length; position++) {
        l_gram_text_char(text, (char)bytes[position]);
      }
    }

    return TRUE;
  case LGE_TEXT:
    l_gram_text_add(text, expression->name);

    return TRUE;
  case LGE_GROUP:
    return l_gram_literal(text, context, expression->child, depth + 1);
  case LGE_FRAGMENT:
    return l_gram_literal(text, context, logos_languages_genesis_analysis_fragment_find(context, expression->name)->expression, depth + 1);
  case LGE_CONCAT:
    for (i = 0; i < expression->item_count; i++) {
      if (!l_gram_literal(text, context, expression->items[i], depth + 1)) {
        return FALSE;
      }
    }

    return TRUE;
  default:
    return FALSE;
  }
}

/* ---------------------------------------------------------------------------- */
/* REFERENCES                                                                    */
/* ---------------------------------------------------------------------------- */

static const char *l_gram_sanitized(NexusDataArena *arena, const char *name);

static void l_gram_ref_add(NexusDataList *list, NexusDataArena *arena, const char *name) {
  uint_large  i;
  const char *copy;

  copy = l_gram_sanitized(arena, name);

  for (i = 0; i < nexus_data_list_length_get(list); i++) {
    if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(list, i), copy)) {
      return;
    }
  }

  (void)nexus_data_list_append_pointer(list, copy);
}

static void l_gram_refs_collect(const LGExpr *expression, NexusDataList *tokens, NexusDataList *rules, NexusDataArena *arena) {
  uint32     i;
  char      *qualified;
  uint_large length;

  if (expression == NULL) {
    return;
  }

  switch (expression->kind) {
  case LGE_TOKEN:
    l_gram_ref_add(tokens, arena, expression->name);
    break;
  case LGE_RULE:
    if (expression->qualifier != NULL) {
      length    = nexus_strings_string_length_get(expression->qualifier) + nexus_strings_string_length_get(expression->name) + 2;
      qualified = (char *)nexus_data_arena_allocate(arena, length);
      (void)stbsp_snprintf(qualified, (int)length, "%s.%s", expression->qualifier, expression->name);
      l_gram_ref_add(rules, arena, qualified);
    } else {
      l_gram_ref_add(rules, arena, expression->name);
    }
    break;
  case LGE_UNION:
  case LGE_CONCAT:
  case LGE_CALL:
    for (i = 0; i < expression->item_count; i++) {
      l_gram_refs_collect(expression->items[i], tokens, rules, arena);
    }
    break;
  case LGE_REPEAT:
    l_gram_refs_collect(expression->child, tokens, rules, arena);
    break;
  case LGE_GROUP:
    l_gram_refs_collect(expression->child, tokens, rules, arena);

    for (i = 0; i < expression->annotation_count; i++) {
      uint32 argument;

      for (argument = 0; argument < expression->annotations[i].argument_count; argument++) {
        l_gram_refs_collect(expression->annotations[i].arguments[argument], tokens, rules, arena);
      }
    }
    break;
  default:
    break;
  }
}

static const char *const *l_gram_refs_close(NexusDataList *list, NexusDataArena *arena, uint32 *out_count) {
  const char **array;
  uint_large   i;

  *out_count = (uint32)nexus_data_list_length_get(list);
  array      = (const char **)nexus_data_arena_allocate(arena, (nexus_data_list_length_get(list) + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  for (i = 0; i < nexus_data_list_length_get(list); i++) {
    array[i] = (const char *)nexus_data_list_pointer_at(list, i);
  }

  nexus_data_list_destroy(list);

  return array;
}

/* ---------------------------------------------------------------------------- */
/* FRAMES                                                                        */
/* ---------------------------------------------------------------------------- */

typedef struct LGFrameBuild {
  const char    *open;
  const char    *close;
  const char    *owner;
  NexusDataList *tokens;
  NexusDataList *rules;
} LGFrameBuild;

/* Summary: finds or creates the frame between two tokens; frames with the same pair are one. */
static LGFrameBuild *l_gram_frame_get(NexusDataList *frames, NexusDataArena *arena, const char *open, const char *close, const char *owner) {
  LGFrameBuild  entry;
  LGFrameBuild *existing;
  uint_large    i;

  for (i = 0; i < nexus_data_list_length_get(frames); i++) {
    existing = (LGFrameBuild *)nexus_data_list_at(frames, i);

    if (nexus_strings_string_equals(existing->open, open) && nexus_strings_string_equals(existing->close, close)) {
      return existing;
    }
  }

  entry.open   = l_gram_sanitized(arena, open);
  entry.close  = l_gram_sanitized(arena, close);
  entry.owner  = owner;
  entry.tokens = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  entry.rules  = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
  (void)nexus_data_list_append(frames, &entry);

  return (LGFrameBuild *)nexus_data_list_at(frames, nexus_data_list_length_get(frames) - 1);
}

/*
Summary: collects what an expression references, outside the interior of frames into tokens and rules, and registers each
  frame it finds. A frame is a concatenation that starts with one token and ends with a different one.
*/
static void l_gram_scan(NexusDataArena *arena, const LGExpr *expression, NexusDataList *tokens, NexusDataList *rules, NexusDataList *frames,
                        NexusDataList *plain, const char *owner) {
  uint32 i;

  if (expression == NULL) {
    return;
  }

  switch (expression->kind) {
  case LGE_TOKEN:
    l_gram_ref_add(tokens, arena, expression->name);
    l_gram_ref_add(plain, arena, expression->name);
    break;
  case LGE_RULE:
    if (expression->qualifier == NULL) {
      l_gram_ref_add(rules, arena, expression->name);
    }
    break;
  case LGE_CONCAT:
    if (expression->item_count >= 2 && expression->items[0]->kind == LGE_TOKEN && expression->items[expression->item_count - 1]->kind == LGE_TOKEN &&
        !nexus_strings_string_equals(expression->items[0]->name, expression->items[expression->item_count - 1]->name)) {
      LGFrameBuild *frame;

      frame = l_gram_frame_get(frames, arena, expression->items[0]->name, expression->items[expression->item_count - 1]->name, owner);
      l_gram_ref_add(tokens, arena, expression->items[0]->name);

      for (i = 1; i + 1 < expression->item_count; i++) {
        l_gram_scan(arena, expression->items[i], frame->tokens, frame->rules, frames, plain, owner);
      }

      break;
    }

    for (i = 0; i < expression->item_count; i++) {
      l_gram_scan(arena, expression->items[i], tokens, rules, frames, plain, owner);
    }
    break;
  case LGE_UNION:
    for (i = 0; i < expression->item_count; i++) {
      l_gram_scan(arena, expression->items[i], tokens, rules, frames, plain, owner);
    }
    break;
  case LGE_REPEAT:
  case LGE_GROUP:
    l_gram_scan(arena, expression->child, tokens, rules, frames, plain, owner);
    break;
  default:
    break;
  }
}

/* ---------------------------------------------------------------------------- */
/* BUILD                                                                         */
/* ---------------------------------------------------------------------------- */

static const char *l_gram_sanitized(NexusDataArena *arena, const char *name) {
  LGText text;

  l_gram_text_open(&text);
  l_gram_name(&text, name);

  return l_gram_text_close(&text, arena);
}

static void l_gram_rule_annotations(LGText *text, const LGAnnotation *annotations, uint32 count) {
  uint32 i;
  uint32 argument;

  for (i = 0; i < count; i++) {
    if (i > 0) {
      l_gram_text_char(text, ' ');
    }

    l_gram_text_format(text, "@%s(", annotations[i].name);

    for (argument = 0; argument < annotations[i].argument_count; argument++) {
      if (argument > 0) {
        l_gram_text_add(text, ", ");
      }

      l_gram_genesis(text, annotations[i].arguments[argument], 1);
    }

    l_gram_text_char(text, ')');
  }
}

void logos_languages_genesis_grammar_build(LGenesisEngine *engine, const LGModel *model) {
  NexusDataArena  *arena;
  LGenesisGrammar *grammar;
  NexusDataList   *tokens;
  NexusDataList   *rules;
  NexusDataList   *frames;
  NexusDataList   *plain; /* Tokens referenced other than as the opening or closing token of a frame. */
  NexusDataList   *token_refs;
  NexusDataList   *rule_refs;
  const char     **names;
  uint_large       i;
  uint_large       j;
  uint32           k;
  LGText           text;

  arena   = engine->labels;
  grammar = (LGenesisGrammar *)nexus_data_arena_allocate(arena, NEXUS_SIZEOF(*grammar));
  tokens  = nexus_data_list_create(NEXUS_SIZEOF(LGenesisGrammarToken), 64);
  rules   = nexus_data_list_create(NEXUS_SIZEOF(LGenesisGrammarRule), 32);
  frames  = nexus_data_list_create(NEXUS_SIZEOF(LGFrameBuild), 8);
  plain   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);

  grammar->name    = nexus_data_arena_text_copy(arena, model->unit->name, nexus_strings_string_length_get(model->unit->name));
  grammar->version = model->unit->version != NULL
                         ? nexus_data_arena_text_copy(arena, model->unit->version, nexus_strings_string_length_get(model->unit->version))
                         : NULL;

  names = (const char **)nexus_data_arena_allocate(arena, (nexus_data_list_length_get(model->contexts) + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  grammar->context_count = (uint32)nexus_data_list_length_get(model->contexts);

  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    const LGContextModel *context;

    context  = (const LGContextModel *)nexus_data_list_pointer_at(model->contexts, i);
    names[i] = nexus_data_arena_text_copy(arena, context->name, nexus_strings_string_length_get(context->name));

    for (j = 0; j < nexus_data_list_length_get(context->tokens); j++) {
      const LGToken       *token;
      LGenesisGrammarToken entry;

      token = (const LGToken *)nexus_data_list_pointer_at(context->tokens, j);
      nexus_memory_bytes_clear(&entry, NEXUS_SIZEOF(entry));
      entry.name     = l_gram_sanitized(arena, token->name);
      entry.context  = names[i];
      entry.fragment = token->fragment;
      entry.kind     = token->fragment ? 0 : token->kind;
      entry.priority = token->priority;

      switch (token->action) {
      case LGA_PUSH:
        entry.action = LGCA_PUSH;
        break;
      case LGA_POP:
        entry.action = LGCA_POP;
        break;
      case LGA_SET:
        entry.action = LGCA_SET;
        break;
      case LGA_REPLACE:
        entry.action = LGCA_REPLACE;
        break;
      case LGA_NONE:
      default:
        entry.action = LGCA_NONE;
        break;
      }

      entry.action_target =
          token->target != NULL ? nexus_data_arena_text_copy(arena, token->target, nexus_strings_string_length_get(token->target)) : NULL;
      entry.action_amount = token->amount;

      l_gram_text_open(&text);
      l_gram_genesis(&text, token->expression, 1);
      entry.definition = l_gram_text_close(&text, arena);

      l_gram_text_open(&text);
      l_gram_regexp(&text, context, token->expression, 0);
      entry.regexp = l_gram_text_close(&text, arena);

      l_gram_text_open(&text);

      if (l_gram_literal(&text, context, token->expression, 0)) {
        entry.literal = l_gram_text_close(&text, arena);
      } else {
        (void)l_gram_text_close(&text, arena);
      }

      (void)nexus_data_list_append(tokens, &entry);
    }
  }

  grammar->contexts = names;

  for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
    const LGRuleModel  *rule;
    LGenesisGrammarRule entry;

    rule = (const LGRuleModel *)nexus_data_list_pointer_at(model->rules, i);
    nexus_memory_bytes_clear(&entry, NEXUS_SIZEOF(entry));
    entry.name = l_gram_sanitized(arena, rule->name);
    entry.kind = LGRK_RULE;

    l_gram_text_open(&text);
    l_gram_rule_annotations(&text, rule->annotations, rule->annotation_count);
    entry.annotations = l_gram_text_close(&text, arena);

    l_gram_text_open(&text);
    l_gram_genesis(&text, rule->expression, 1);
    entry.definition = l_gram_text_close(&text, arena);

    token_refs = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    rule_refs  = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    l_gram_refs_collect(rule->expression, token_refs, rule_refs, arena);

    for (k = 0; k < rule->annotation_count; k++) {
      uint32 argument;

      for (argument = 0; argument < rule->annotations[k].argument_count; argument++) {
        l_gram_refs_collect(rule->annotations[k].arguments[argument], token_refs, rule_refs, arena);
      }
    }

    entry.token_refs = l_gram_refs_close(token_refs, arena, &entry.token_ref_count);
    entry.rule_refs  = l_gram_refs_close(rule_refs, arena, &entry.rule_ref_count);

    token_refs = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    rule_refs  = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    l_gram_scan(arena, rule->expression, token_refs, rule_refs, frames, plain, entry.name);
    entry.top_token_refs = l_gram_refs_close(token_refs, arena, &entry.top_token_ref_count);
    entry.top_rule_refs  = l_gram_refs_close(rule_refs, arena, &entry.top_rule_ref_count);
    (void)nexus_data_list_append(rules, &entry);
  }

  for (i = 0; i < nexus_data_list_length_get(model->pratts); i++) {
    const LGPrattModel *pratt;
    LGenesisGrammarRule entry;

    pratt = (const LGPrattModel *)nexus_data_list_pointer_at(model->pratts, i);
    nexus_memory_bytes_clear(&entry, NEXUS_SIZEOF(entry));
    entry.name        = l_gram_sanitized(arena, pratt->name);
    entry.kind        = LGRK_PRATT;
    entry.annotations = "";
    token_refs        = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    rule_refs         = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

    l_gram_text_open(&text);
    l_gram_text_add(&text, "atom = ");
    l_gram_genesis(&text, pratt->atom, 1);
    l_gram_refs_collect(pratt->atom, token_refs, rule_refs, arena);

    for (j = 0; j < nexus_data_list_length_get(pratt->entries); j++) {
      const LGEntryModel *operator_entry;

      operator_entry = (const LGEntryModel *)nexus_data_list_pointer_at(pratt->entries, j);
      l_gram_text_format(&text, "\n\t%s ", operator_entry->kind);
      l_gram_genesis(&text, operator_entry->token, 4);
      l_gram_text_add(&text, " (");

      if (nexus_strings_string_equals(operator_entry->kind, "prefix")) {
        l_gram_text_format(&text, "right_bp: %lld", (long long)operator_entry->right_bp);
      } else if (nexus_strings_string_equals(operator_entry->kind, "postfix")) {
        l_gram_text_format(&text, "left_bp: %lld", (long long)operator_entry->left_bp);
      } else {
        l_gram_text_format(&text, "left_bp: %lld, right_bp: %lld", (long long)operator_entry->left_bp, (long long)operator_entry->right_bp);
      }

      l_gram_text_char(&text, ')');
      l_gram_refs_collect(operator_entry->token, token_refs, rule_refs, arena);
    }

    entry.definition          = l_gram_text_close(&text, arena);
    entry.token_refs          = l_gram_refs_close(token_refs, arena, &entry.token_ref_count);
    entry.rule_refs           = l_gram_refs_close(rule_refs, arena, &entry.rule_ref_count);
    entry.top_token_refs      = entry.token_refs;
    entry.top_token_ref_count = entry.token_ref_count;
    entry.top_rule_refs       = entry.rule_refs;
    entry.top_rule_ref_count  = entry.rule_ref_count;
    (void)nexus_data_list_append(rules, &entry);
  }

  for (i = 0; i < nexus_data_list_length_get(model->embeds); i++) {
    const LGEmbedModel *embed;
    LGenesisGrammarRule entry;

    embed = (const LGEmbedModel *)nexus_data_list_pointer_at(model->embeds, i);
    nexus_memory_bytes_clear(&entry, NEXUS_SIZEOF(entry));
    entry.name        = l_gram_sanitized(arena, embed->name);
    entry.kind        = LGRK_EMBED;
    entry.annotations = "";
    token_refs        = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    rule_refs         = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

    l_gram_text_open(&text);

    for (k = 0; k < embed->count; k++) {
      if (k > 0) {
        l_gram_text_add(&text, "\n\t");
      }

      l_gram_text_format(&text, "%s = ", embed->keys[k]);
      l_gram_genesis(&text, embed->values[k], 1);
      l_gram_refs_collect(embed->values[k], token_refs, rule_refs, arena);
    }

    entry.definition          = l_gram_text_close(&text, arena);
    entry.token_refs          = l_gram_refs_close(token_refs, arena, &entry.token_ref_count);
    entry.rule_refs           = l_gram_refs_close(rule_refs, arena, &entry.rule_ref_count);
    entry.top_token_refs      = entry.token_refs;
    entry.top_token_ref_count = entry.token_ref_count;
    entry.top_rule_refs       = entry.rule_refs;
    entry.top_rule_ref_count  = entry.rule_ref_count;
    (void)nexus_data_list_append(rules, &entry);
  }

  grammar->token_count = (uint32)nexus_data_list_length_get(tokens);
  grammar->tokens      = (LGenesisGrammarToken *)nexus_data_arena_allocate(arena, (grammar->token_count + 1) * NEXUS_SIZEOF(LGenesisGrammarToken));

  if (grammar->token_count > 0) {
    nexus_memory_bytes_copy(nexus_data_list_data_get_const(tokens), grammar->tokens,
                            (uint_large)grammar->token_count * NEXUS_SIZEOF(LGenesisGrammarToken));
  }

  grammar->rule_count = (uint32)nexus_data_list_length_get(rules);
  grammar->rules      = (LGenesisGrammarRule *)nexus_data_arena_allocate(arena, (grammar->rule_count + 1) * NEXUS_SIZEOF(LGenesisGrammarRule));

  if (grammar->rule_count > 0) {
    nexus_memory_bytes_copy(nexus_data_list_data_get_const(rules), grammar->rules,
                            (uint_large)grammar->rule_count * NEXUS_SIZEOF(LGenesisGrammarRule));
  }

  /*
  A pair of tokens only delimits a frame when neither is also used on its own elsewhere: a token that appears both as an
  opener and as a plain word (a name followed by an equals sign, say) cannot tell an editor when a frame begins.
  */
  grammar->frame_count = 0;
  grammar->frames =
      (LGenesisGrammarFrame *)nexus_data_arena_allocate(arena, (nexus_data_list_length_get(frames) + 1) * NEXUS_SIZEOF(LGenesisGrammarFrame));

  for (i = 0; i < nexus_data_list_length_get(frames); i++) {
    LGFrameBuild *build;
    uint_large    plain_index;
    boolean       ambiguous;

    build     = (LGFrameBuild *)nexus_data_list_at(frames, i);
    ambiguous = FALSE;

    for (plain_index = 0; plain_index < nexus_data_list_length_get(plain); plain_index++) {
      const char *name;

      name      = (const char *)nexus_data_list_pointer_at(plain, plain_index);
      ambiguous = ambiguous || nexus_strings_string_equals(name, build->open) || nexus_strings_string_equals(name, build->close);
    }

    if (ambiguous) {
      continue;
    }

    grammar->frames[grammar->frame_count].rule  = build->owner;
    grammar->frames[grammar->frame_count].open  = build->open;
    grammar->frames[grammar->frame_count].close = build->close;
    grammar->frames[grammar->frame_count].token_refs =
        l_gram_refs_close(build->tokens, arena, &grammar->frames[grammar->frame_count].token_ref_count);
    grammar->frames[grammar->frame_count].rule_refs = l_gram_refs_close(build->rules, arena, &grammar->frames[grammar->frame_count].rule_ref_count);
    grammar->frame_count++;
  }

  nexus_data_list_destroy(plain);
  nexus_data_list_destroy(frames);
  nexus_data_list_destroy(tokens);
  nexus_data_list_destroy(rules);

  /* The PARSER block. */
  {
    const char **globals;
    uint32       global_count;

    globals      = (const char **)nexus_data_arena_allocate(arena, (model->parser_count + nexus_data_list_length_get(model->rules) + 1) *
                                                                       NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
    global_count = 0;

    for (k = 0; k < model->parser_count; k++) {
      const LGExpr *value;

      value = model->parser_values[k];

      if (nexus_strings_string_equals(model->parser_keys[k], "entry")) {
        grammar->entry = l_gram_sanitized(arena, value->name);
      } else if (nexus_strings_string_equals(model->parser_keys[k], "global")) {
        uint32 count;
        uint32 item;

        count = value->kind == LGE_CONCAT ? value->item_count : 1;

        for (item = 0; item < count; item++) {
          globals[global_count++] = l_gram_sanitized(arena, (value->kind == LGE_CONCAT ? value->items[item] : value)->name);
        }
      } else if (nexus_strings_string_equals(model->parser_keys[k], "choice")) {
        l_gram_text_open(&text);
        l_gram_genesis(&text, value, 1);
        grammar->choice = l_gram_text_close(&text, arena);
      } else if (nexus_strings_string_equals(model->parser_keys[k], "lexical_selection")) {
        grammar->grammar_expected = nexus_strings_string_equals(value->name, "grammar_expected");
      }
    }

    grammar->globals      = globals;
    grammar->global_count = global_count;
  }

  /* IMPORT declarations of embedded languages. */
  l_gram_text_open(&text);

  for (i = 0; i < nexus_data_list_length_get(model->imports); i++) {
    const LGImport *import;

    import = (const LGImport *)nexus_data_list_pointer_at(model->imports, i);

    if (import->entry->kind != LGUK_LANGUAGE) {
      continue;
    }

    l_gram_text_format(&text, "IMPORT %s", import->name);

    if (import->alias != NULL) {
      l_gram_text_format(&text, " AS %s", import->alias);
    }

    l_gram_text_add(&text, " {");

    for (k = 0; k < import->item_count; k++) {
      l_gram_text_format(&text, "%s %s", k > 0 ? "," : "", import->items[k]);
    }

    l_gram_text_add(&text, " }\n");
  }

  grammar->imports = l_gram_text_close(&text, arena);
  engine->grammar  = grammar;
}

/* ---------------------------------------------------------------------------- */
/* GETTERS                                                                       */
/* ---------------------------------------------------------------------------- */

uint32 logos_languages_genesis_grammar_frame_count_get(const LGenesisGrammar *grammar) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL, "grammar must not be NULL");

  return grammar->frame_count;
}

const LGenesisGrammarFrame *logos_languages_genesis_grammar_frame_at(const LGenesisGrammar *grammar, uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL && index < grammar->frame_count, "frame index must be in bounds");

  return &grammar->frames[index];
}

uint32 logos_languages_genesis_grammar_context_count_get(const LGenesisGrammar *grammar) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL, "grammar must not be NULL");

  return grammar->context_count;
}

const char *logos_languages_genesis_grammar_context_name_at(const LGenesisGrammar *grammar, uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL && index < grammar->context_count, "context index must be in bounds");

  return grammar->contexts[index];
}

uint32 logos_languages_genesis_grammar_token_count_get(const LGenesisGrammar *grammar) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL, "grammar must not be NULL");

  return grammar->token_count;
}

const LGenesisGrammarToken *logos_languages_genesis_grammar_token_at(const LGenesisGrammar *grammar, uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL && index < grammar->token_count, "token index must be in bounds");

  return &grammar->tokens[index];
}

uint32 logos_languages_genesis_grammar_rule_count_get(const LGenesisGrammar *grammar) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL, "grammar must not be NULL");

  return grammar->rule_count;
}

const LGenesisGrammarRule *logos_languages_genesis_grammar_rule_at(const LGenesisGrammar *grammar, uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL && index < grammar->rule_count, "rule index must be in bounds");

  return &grammar->rules[index];
}

const char *logos_languages_genesis_grammar_entry_get(const LGenesisGrammar *grammar) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL, "grammar must not be NULL");

  return grammar->entry;
}

uint32 logos_languages_genesis_grammar_global_count_get(const LGenesisGrammar *grammar) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL, "grammar must not be NULL");

  return grammar->global_count;
}

const char *logos_languages_genesis_grammar_global_at(const LGenesisGrammar *grammar, uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL && index < grammar->global_count, "global index must be in bounds");

  return grammar->globals[index];
}

boolean logos_languages_genesis_grammar_grammar_expected_get(const LGenesisGrammar *grammar) {
  NEXUS_PROGRAM_PRECONDITION(grammar != NULL, "grammar must not be NULL");

  return grammar->grammar_expected;
}
