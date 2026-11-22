#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
The neutral grammar description of the interchange system and the writer that turns it into Genesis: lexical definitions
become fragments (and tokens where the grammar needs an emitting token), syntactic ones rules, literals of rules implicit
tokens, skipped tokens a global trivia rule, lexical modes contexts.
*/

#define LI_NAME_MAX 96

/* ---------------------------------------------------------------------------- */
/* BUILDING                                                                      */
/* ---------------------------------------------------------------------------- */

LIGrammar *logos_languages_interchange_grammar_create(const char *name) {
  LIGrammar *grammar;

  grammar = (LIGrammar *)nexus_memory_allocate(NEXUS_SIZEOF(*grammar));
  nexus_memory_bytes_clear(grammar, NEXUS_SIZEOF(*grammar));
  nexus_data_arena_initialize(&grammar->arena);
  grammar->name        = nexus_data_arena_text_copy(&grammar->arena, name, nexus_strings_string_length_get(name));
  grammar->definitions = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  grammar->modes       = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  grammar->notes       = nexus_data_list_create(NEXUS_SIZEOF(LINote), 8);

  return grammar;
}

void logos_languages_interchange_grammar_destroy(LIGrammar *grammar) {
  nexus_data_list_destroy(grammar->definitions);
  nexus_data_list_destroy(grammar->modes);
  nexus_data_list_destroy(grammar->notes);
  nexus_data_arena_release(&grammar->arena);
  nexus_memory_deallocate(grammar);
}

LINode *logos_languages_interchange_node_new(LIGrammar *grammar, LIKind kind) {
  LINode *node;

  node       = (LINode *)nexus_data_arena_allocate(&grammar->arena, NEXUS_SIZEOF(*node));
  node->kind = kind;

  return node;
}

LINode *logos_languages_interchange_node_literal(LIGrammar *grammar, const char *text, uint_large length, boolean insensitive) {
  LINode *node;

  node              = logos_languages_interchange_node_new(grammar, LIK_LITERAL);
  node->text        = nexus_data_arena_text_copy(&grammar->arena, text, length);
  node->length      = length;
  node->insensitive = insensitive;

  return node;
}

LINode *logos_languages_interchange_node_reference(LIGrammar *grammar, const char *name) {
  LINode *node;

  node         = logos_languages_interchange_node_new(grammar, LIK_REF);
  node->text   = nexus_data_arena_text_copy(&grammar->arena, name, nexus_strings_string_length_get(name));
  node->length = nexus_strings_string_length_get(name);

  return node;
}

LINode *logos_languages_interchange_node_charset(LIGrammar *grammar, const uint32 *ranges, uint32 range_count, boolean negated) {
  LINode *node;

  node              = logos_languages_interchange_node_new(grammar, LIK_CHARSET);
  node->ranges      = (uint32 *)nexus_data_arena_allocate(&grammar->arena, ((uint_large)range_count * 2 + 1) * NEXUS_SIZEOF(uint32));
  node->range_count = range_count;
  node->negated     = negated;

  if (range_count > 0) {
    nexus_memory_bytes_copy(ranges, node->ranges, (uint_large)range_count * 2 * NEXUS_SIZEOF(uint32));
  }

  return node;
}

LINode *logos_languages_interchange_node_group(LIGrammar *grammar, LIKind kind, LINode *const *items, uint32 count) {
  LINode *node;

  if (count == 1) {
    return items[0];
  }

  node        = logos_languages_interchange_node_new(grammar, kind);
  node->items = (LINode **)nexus_data_arena_allocate(&grammar->arena, ((uint_large)count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  node->count = count;

  if (count > 0) {
    nexus_memory_bytes_copy((const void *)items, (void *)node->items, (uint_large)count * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  }

  return node;
}

LINode *logos_languages_interchange_node_wrap(LIGrammar *grammar, LIKind kind, LINode *child) {
  LINode *node;

  node        = logos_languages_interchange_node_new(grammar, kind);
  node->child = child;

  return node;
}

LIDefinition *logos_languages_interchange_grammar_define(LIGrammar *grammar, const char *name, LIClass kind, LINode *expression) {
  LIDefinition *definition;

  definition             = (LIDefinition *)nexus_data_arena_allocate(&grammar->arena, NEXUS_SIZEOF(*definition));
  definition->name       = nexus_data_arena_text_copy(&grammar->arena, name, nexus_strings_string_length_get(name));
  definition->kind       = kind;
  definition->expression = expression;
  (void)nexus_data_list_append_pointer(grammar->definitions, definition);

  if (grammar->entry == NULL && kind != LIC_FRAGMENT) {
    grammar->entry = definition->name;
  }

  return definition;
}

void logos_languages_interchange_grammar_note(LIGrammar *grammar, LINoteSeverity severity, const char *format, ...) {
  LINote  note;
  char    buffer[512];
  va_list arguments;

  va_start(arguments, format);
  (void)stbsp_vsnprintf(buffer, (int)sizeof(buffer), format, arguments);
  va_end(arguments);

  /* A note that was already made is not made again (a hundred rules sending tokens to one channel are one note). */
  {
    uint_large i;

    for (i = 0; i < nexus_data_list_length_get(grammar->notes); i++) {
      const LINote *earlier;

      earlier = (const LINote *)nexus_data_list_at_const(grammar->notes, i);

      if (earlier->severity == severity && nexus_strings_string_equals(earlier->text, buffer)) {
        return;
      }
    }
  }

  note.severity = severity;
  note.text     = nexus_data_arena_text_copy(&grammar->arena, buffer, nexus_strings_string_length_get(buffer));
  (void)nexus_data_list_append(grammar->notes, &note);
}

const char *logos_languages_interchange_grammar_mode_add(LIGrammar *grammar, const char *name) {
  uint_large i;
  char      *copy;

  for (i = 0; i < nexus_data_list_length_get(grammar->modes); i++) {
    if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(grammar->modes, i), name)) {
      return (const char *)nexus_data_list_pointer_at(grammar->modes, i);
    }
  }

  copy = nexus_data_arena_text_copy(&grammar->arena, name, nexus_strings_string_length_get(name));
  (void)nexus_data_list_append_pointer(grammar->modes, copy);

  return copy;
}

/* ---------------------------------------------------------------------------- */
/* REPORT                                                                        */
/* ---------------------------------------------------------------------------- */

static const char *l_interchange_severity_text(LINoteSeverity severity) {
  if (severity == LIN_ERROR) {
    return "not supported";
  }

  return severity == LIN_WARNING ? "approximated" : "note";
}

char *logos_languages_interchange_grammar_report(const LIGrammar *grammar) {
  NexusStringBuilder out;
  uint_large         i;
  char              *result;
  uint_large         length;

  out = nexus_strings_string_builder_create_growable(256);

  for (i = 0; i < nexus_data_list_length_get(grammar->notes); i++) {
    const LINote *note;

    note = (const LINote *)nexus_data_list_at_const(grammar->notes, i);
    (void)nexus_strings_string_builder_append_formatted(&out, "%s: %s\n", l_interchange_severity_text(note->severity), note->text);
  }

  length = nexus_strings_string_builder_length_get(&out);
  result = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&out), result, length + 1);
  nexus_strings_string_builder_destroy(&out);

  return result;
}

/* ---------------------------------------------------------------------------- */
/* NAMES                                                                         */
/* ---------------------------------------------------------------------------- */

/* Summary: the Genesis name for a source name: lower case, letters digits and underscores, not starting with a digit. */
static void l_interchange_name(const char *source, char *out, uint_large capacity) {
  uint_large length;
  uint_large i;
  uint_large position;

  length   = nexus_strings_string_length_get(source);
  position = 0;

  for (i = 0; i < length && position + 2 < capacity; i++) {
    char letter;

    letter = source[i];

    if (letter >= 'A' && letter <= 'Z') {
      /* A camel-case boundary becomes an underscore: ArgList -> arg_list. */
      if (i > 0 && position > 0 && out[position - 1] != '_' && source[i - 1] >= 'a' && source[i - 1] <= 'z') {
        out[position++] = '_';
      }

      letter = (char)(letter - 'A' + 'a');
    } else if (!((letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '_')) {
      letter = '_';
    }

    out[position++] = letter;
  }

  if (position == 0 || (out[0] >= '0' && out[0] <= '9')) {
    nexus_memory_bytes_move(out, out + 1, position);
    out[0] = 'x';
    position++;
  }

  out[position] = '\0';
}

typedef struct LIEmit {
  LIGrammar          *grammar;
  NexusStringBuilder *target;   /* where expressions are written */
  NexusDataList      *names;    /* char *, parallel to the definitions: the Genesis name */
  NexusDataList      *tokens;   /* LIDefinition * that need an emitting token, in emission order */
  NexusDataList      *implicit; /* char * name, LINode * node pairs: implicit tokens for literals and sets of rules */
  boolean             ok;
} LIEmit;

static const LIDefinition *l_interchange_find(const LIGrammar *grammar, const char *name, uint_large *out_index) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(grammar->definitions); i++) {
    const LIDefinition *definition;

    definition = (const LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, i);

    if (nexus_strings_string_equals(definition->name, name)) {
      if (out_index != NULL) {
        *out_index = i;
      }

      return definition;
    }
  }

  return NULL;
}

/* ---------------------------------------------------------------------------- */
/* LEXICAL TEXT                                                                  */
/* ---------------------------------------------------------------------------- */

static void l_interchange_codepoint(NexusStringBuilder *out, uint32 value) {
  if (value == '\'') {
    (void)nexus_strings_string_builder_append(out, "'\\''");
  } else if (value == '\\') {
    (void)nexus_strings_string_builder_append(out, "'\\\\'");
  } else if (value == '\n') {
    (void)nexus_strings_string_builder_append(out, "'\\n'");
  } else if (value == '\r') {
    (void)nexus_strings_string_builder_append(out, "'\\r'");
  } else if (value == '\t') {
    (void)nexus_strings_string_builder_append(out, "'\\t'");
  } else if (value >= 0x20 && value < 0x7F) {
    (void)nexus_strings_string_builder_append_formatted(out, "'%c'", (char)value);
  } else {
    (void)nexus_strings_string_builder_append_formatted(out, "'\\u{%X}'", (unsigned)value);
  }
}

/* Summary: the next code point of UTF-8 text. */
static uint32 l_interchange_decode(const char *text, uint_large length, uint_large *position) {
  byte   lead;
  uint32 value;
  uint32 extra;
  uint32 i;

  lead = (byte)text[*position];
  (*position)++;

  if (lead < 0x80) {
    return lead;
  }

  if (lead >= 0xF0) {
    extra = 3;
    value = (uint32)(lead & 0x07);
  } else if (lead >= 0xE0) {
    extra = 2;
    value = (uint32)(lead & 0x0F);
  } else {
    extra = 1;
    value = (uint32)(lead & 0x1F);
  }

  for (i = 0; i < extra && *position < length; i++) {
    value = (value << 6) | (uint32)((byte)text[*position] & 0x3F);
    (*position)++;
  }

  return value;
}

static void l_interchange_literal(NexusStringBuilder *out, const LINode *node) {
  uint_large position;
  boolean    first;

  if (node->length == 0) {
    (void)nexus_strings_string_builder_append(out, "''");

    return;
  }

  first    = TRUE;
  position = 0;

  if (!node->insensitive) {
    /* One quoted string is the compact form; escapes inside it are the same as for a code point. */
    (void)nexus_strings_string_builder_append(out, "'");

    while (position < node->length) {
      uint32 value;

      value = l_interchange_decode(node->text, node->length, &position);

      if (value == '\'') {
        (void)nexus_strings_string_builder_append(out, "\\'");
      } else if (value == '\\') {
        (void)nexus_strings_string_builder_append(out, "\\\\");
      } else if (value == '\n') {
        (void)nexus_strings_string_builder_append(out, "\\n");
      } else if (value == '\r') {
        (void)nexus_strings_string_builder_append(out, "\\r");
      } else if (value == '\t') {
        (void)nexus_strings_string_builder_append(out, "\\t");
      } else if (value >= 0x20 && value < 0x7F) {
        (void)nexus_strings_string_builder_append_formatted(out, "%c", (char)value);
      } else {
        (void)nexus_strings_string_builder_append_formatted(out, "\\u{%X}", (unsigned)value);
      }
    }

    (void)nexus_strings_string_builder_append(out, "'");

    return;
  }

  while (position < node->length) {
    uint32 value;

    value = l_interchange_decode(node->text, node->length, &position);

    if (!first) {
      (void)nexus_strings_string_builder_append(out, " ");
    }

    first = FALSE;

    if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z')) {
      uint32 upper;
      uint32 lower;

      upper = value >= 'a' ? value - 'a' + 'A' : value;
      lower = upper - 'A' + 'a';
      (void)nexus_strings_string_builder_append(out, "(");
      l_interchange_codepoint(out, upper);
      (void)nexus_strings_string_builder_append(out, " || ");
      l_interchange_codepoint(out, lower);
      (void)nexus_strings_string_builder_append(out, ")");
    } else {
      l_interchange_codepoint(out, value);
    }
  }
}

/* Summary: the code point ranges of a set, complemented over the Unicode scalar values when negated. */
static uint32 l_interchange_ranges(const LINode *node, uint32 *out, uint32 capacity) {
  uint32     pairs[2048];
  uint_large count;
  uint_large i;
  uint_large j;
  uint_large merged;

  count = node->range_count < 1000 ? node->range_count : 1000;

  for (i = 0; i < count; i++) {
    pairs[(i * 2)]     = node->ranges[(i * 2)];
    pairs[(i * 2) + 1] = node->ranges[(i * 2) + 1];
  }

  /* Sort by start (insertion), then merge overlapping and adjacent ranges. */
  for (i = 1; i < count; i++) {
    uint32 low;
    uint32 high;

    low  = pairs[i * 2];
    high = pairs[(i * 2) + 1];

    for (j = i; j > 0 && pairs[(j - 1) * 2] > low; j--) {
      pairs[j * 2]       = pairs[(j - 1) * 2];
      pairs[(j * 2) + 1] = pairs[((j - 1) * 2) + 1];
    }

    pairs[j * 2]       = low;
    pairs[(j * 2) + 1] = high;
  }

  merged = 0;

  for (i = 0; i < count; i++) {
    if (merged > 0 && pairs[i * 2] <= pairs[((merged - 1) * 2) + 1] + 1) {
      if (pairs[(i * 2) + 1] > pairs[((merged - 1) * 2) + 1]) {
        pairs[((merged - 1) * 2) + 1] = pairs[(i * 2) + 1];
      }
    } else {
      pairs[merged * 2]       = pairs[i * 2];
      pairs[(merged * 2) + 1] = pairs[(i * 2) + 1];
      merged++;
    }
  }

  count = 0;

  if (!node->negated) {
    for (i = 0; i < merged && count + 1 < capacity; i++) {
      out[count * 2]       = pairs[i * 2];
      out[(count * 2) + 1] = pairs[(i * 2) + 1];
      count++;
    }

    return (uint32)count;
  }

  /* The gaps between the ranges, over 0..10FFFF without the surrogates D800..DFFF (not scalar values). */
  {
    uint32 next;

    next = 0;

    for (i = 0; i <= merged; i++) {
      uint32 gap_end;

      gap_end = i < merged ? pairs[i * 2] : 0x110000;

      if (gap_end > next) {
        uint32 low;
        uint32 high;

        low  = next;
        high = gap_end - 1;

        if (low <= 0xD7FF && high >= 0xD800) {
          if (count + 2 < capacity) {
            out[count * 2]       = low;
            out[(count * 2) + 1] = 0xD7FF;
            count++;
          }

          low = 0xE000;
        } else if (low >= 0xD800 && low <= 0xDFFF) {
          low = 0xE000;
        }

        if (low <= high && high <= 0x10FFFF && count + 1 < capacity) {
          out[count * 2]       = low;
          out[(count * 2) + 1] = high;
          count++;
        }
      }

      if (i < merged) {
        next = pairs[(i * 2) + 1] + 1;
      }
    }
  }

  return (uint32)count;
}

static void l_interchange_charset(NexusStringBuilder *out, const LINode *node) {
  uint32     ranges[2048];
  uint_large count;
  uint_large i;

  count = l_interchange_ranges(node, ranges, 1000);

  if (count == 0) {
    /* An empty set matches nothing; the closest honest text is a range no input has. */
    (void)nexus_strings_string_builder_append(out, "'\\u{10FFFF}'..'\\u{10FFFF}'");

    return;
  }

  if (count > 1) {
    (void)nexus_strings_string_builder_append(out, "(");
  }

  for (i = 0; i < count; i++) {
    if (i > 0) {
      (void)nexus_strings_string_builder_append(out, " || ");
    }

    l_interchange_codepoint(out, ranges[i * 2]);

    if (ranges[(i * 2) + 1] != ranges[i * 2]) {
      (void)nexus_strings_string_builder_append(out, "..");
      l_interchange_codepoint(out, ranges[(i * 2) + 1]);
    }
  }

  if (count > 1) {
    (void)nexus_strings_string_builder_append(out, ")");
  }
}

/* ---------------------------------------------------------------------------- */
/* CLASSIFICATION                                                                */
/* ---------------------------------------------------------------------------- */

/* Summary: whether an expression refers (transitively) only to lexical definitions. */
static boolean l_interchange_lexical(const LIGrammar *grammar, const LINode *node, uint32 depth, const LIDefinition **stack) {
  uint32 i;

  if (node == NULL) {
    return TRUE;
  }

  switch (node->kind) {
  case LIK_REF: {
    const LIDefinition *target;

    target = l_interchange_find(grammar, node->text, NULL);

    if (target == NULL || target->kind == LIC_RULE) {
      return FALSE;
    }

    if (target->kind == LIC_TOKEN || target->kind == LIC_FRAGMENT) {
      return TRUE;
    }

    /* A definition that reaches itself is not a regular language: it must be a rule. */
    for (i = 0; i < depth; i++) {
      if (stack[i] == target) {
        return FALSE;
      }
    }

    if (depth >= 64) {
      return FALSE;
    }

    stack[depth] = target;

    return l_interchange_lexical(grammar, target->expression, depth + 1, stack);
  }
  case LIK_SEQ:
  case LIK_ALT:
    for (i = 0; i < node->count; i++) {
      if (!l_interchange_lexical(grammar, node->items[i], depth, stack)) {
        return FALSE;
      }
    }

    return TRUE;
  case LIK_STAR:
  case LIK_PLUS:
  case LIK_OPT:
    return l_interchange_lexical(grammar, node->child, depth, stack);
  default:
    return TRUE;
  }
}

/* Summary: whether an expression can match the empty string. */
static boolean l_interchange_nullable(const LIGrammar *grammar, const LINode *node, uint32 depth) {
  uint32 i;

  if (node == NULL || depth > 64) {
    return FALSE;
  }

  switch (node->kind) {
  case LIK_EMPTY:
  case LIK_STAR:
  case LIK_OPT:
    return TRUE;
  case LIK_LITERAL:
    return node->length == 0;
  case LIK_REF: {
    const LIDefinition *target;

    target = l_interchange_find(grammar, node->text, NULL);

    return target != NULL && l_interchange_nullable(grammar, target->expression, depth + 1);
  }
  case LIK_SEQ:
    for (i = 0; i < node->count; i++) {
      if (!l_interchange_nullable(grammar, node->items[i], depth)) {
        return FALSE;
      }
    }

    return TRUE;
  case LIK_ALT:
    for (i = 0; i < node->count; i++) {
      if (l_interchange_nullable(grammar, node->items[i], depth)) {
        return TRUE;
      }
    }

    return FALSE;
  case LIK_PLUS:
    return l_interchange_nullable(grammar, node->child, depth);
  default:
    return FALSE;
  }
}

/* ---------------------------------------------------------------------------- */
/* EMISSION                                                                      */
/* ---------------------------------------------------------------------------- */

static const char *l_interchange_name_of(const LIEmit *emit, uint_large index) {
  return (const char *)nexus_data_list_pointer_at(emit->names, index);
}

/*
Summary: the expression without what matches nothing: empty items of a sequence are dropped, an alternative that can be empty
  makes the alternatives optional, and a repetition of nothing is nothing.
*/
static LINode *l_interchange_simplify(LIGrammar *grammar, LINode *node) {
  LINode *items[1024];
  uint32  used;
  uint32  i;
  boolean has_empty;

  if (node == NULL) {
    return node;
  }

  switch (node->kind) {
  case LIK_SEQ:
    used = 0;

    for (i = 0; i < node->count && used < 1000; i++) {
      LINode *item;

      item = l_interchange_simplify(grammar, node->items[i]);

      if (item->kind != LIK_EMPTY) {
        items[used++] = item;
      }
    }

    if (used == 0) {
      return logos_languages_interchange_node_new(grammar, LIK_EMPTY);
    }

    return logos_languages_interchange_node_group(grammar, LIK_SEQ, items, used);
  case LIK_ALT:
    used      = 0;
    has_empty = FALSE;

    for (i = 0; i < node->count && used < 1000; i++) {
      LINode *item;

      item = l_interchange_simplify(grammar, node->items[i]);

      if (item->kind == LIK_EMPTY) {
        has_empty = TRUE;
      } else {
        items[used++] = item;
      }
    }

    if (used == 0) {
      return logos_languages_interchange_node_new(grammar, LIK_EMPTY);
    }

    {
      LINode *alternatives;

      alternatives = logos_languages_interchange_node_group(grammar, LIK_ALT, items, used);

      return has_empty ? logos_languages_interchange_node_wrap(grammar, LIK_OPT, alternatives) : alternatives;
    }
  case LIK_STAR:
  case LIK_PLUS:
  case LIK_OPT: {
    LINode *child;

    child = l_interchange_simplify(grammar, node->child);

    if (child->kind == LIK_EMPTY) {
      return child;
    }

    return logos_languages_interchange_node_wrap(grammar, node->kind, child);
  }
  default:
    return node;
  }
}

typedef enum LIContext {
  LIX_LEXICAL,
  LIX_SYNTAX
} LIContext;

static void l_interchange_expression(LIEmit *emit, const LINode *node, LIContext context, uint32 precedence);

/* Summary: a readable name for a short run of punctuation ("{" is lbrace, "->" is minus_greater); FALSE when a character has none. */
static boolean l_interchange_punctuation(const char *text, uint_large length, char *out, uint_large capacity) {
  static const char *const names[] = {
      "!", "bang",      "\"", "quote", "#", "hash",       "$", "dollar",   "%", "percent",  "&", "ampersand", "'", "apostrophe", "(",  "lparen",
      ")", "rparen",    "*",  "star",  "+", "plus",       ",", "comma",    "-", "minus",    ".", "dot",       "/", "slash",      ":",  "colon",
      ";", "semicolon", "<",  "less",  "=", "equals",     ">", "greater",  "?", "question", "@", "at",        "[", "lbracket",   "\\", "backslash",
      "]", "rbracket",  "^",  "caret", "_", "underscore", "`", "backtick", "{", "lbrace",   "|", "pipe",      "}", "rbrace",     "~",  "tilde"};
  uint_large i;
  uint_large k;
  uint_large used;

  (void)stbsp_snprintf(out, (int)capacity, "p");
  used = 1;

  for (i = 0; i < length; i++) {
    boolean found;

    found = FALSE;

    for (k = 0; k + 1 < NEXUS_ARRAY_SIZE_ELEMENTS(names); k += 2) {
      if (names[k][0] == text[i] && names[k][1] == '\0') {
        used += (uint_large)stbsp_snprintf(out + used, (int)(capacity - used), "_%s", names[k + 1]);
        found = TRUE;
        break;
      }
    }

    if (!found) {
      return FALSE;
    }
  }

  return TRUE;
}

/* Summary: the name of the implicit token that stands for a literal or a set inside a rule. */
static const char *l_interchange_implicit(LIEmit *emit, const LINode *node) {
  uint_large i;
  char       name[LI_NAME_MAX];
  char       raw[LI_NAME_MAX];

  for (i = 0; i + 1 < nexus_data_list_length_get(emit->implicit); i += 2) {
    const LINode *other;

    other = (const LINode *)nexus_data_list_pointer_at(emit->implicit, i + 1);

    if (other->kind == node->kind && other->length == node->length && other->insensitive == node->insensitive && other->negated == node->negated &&
        other->range_count == node->range_count && (node->kind != LIK_LITERAL || nexus_memory_bytes_equal(other->text, node->text, node->length)) &&
        (node->kind != LIK_CHARSET ||
         nexus_memory_bytes_equal(other->ranges, node->ranges, (uint_large)node->range_count * 2 * NEXUS_SIZEOF(uint32)))) {
      return (const char *)nexus_data_list_pointer_at(emit->implicit, i);
    }
  }

  if (node->kind == LIK_LITERAL) {
    uint_large k;
    boolean    plain;

    plain = TRUE;

    for (k = 0; k < node->length; k++) {
      char letter;

      letter = node->text[k];
      plain  = plain && ((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9') || letter == '_');
    }

    if (plain && node->length > 0 && node->length < 40) {
      (void)stbsp_snprintf(raw, (int)sizeof(raw), "kw_%.*s", (int)node->length, node->text);
    } else if (node->length > 0 && node->length <= 3 && l_interchange_punctuation(node->text, node->length, raw, sizeof(raw))) {
      /* A symbol token is named for what it looks like: lbrace, arrow. */
    } else {
      (void)stbsp_snprintf(raw, (int)sizeof(raw), "lit_%u", (unsigned)(nexus_data_list_length_get(emit->implicit) / 2));
    }
  } else {
    (void)stbsp_snprintf(raw, (int)sizeof(raw), "set_%u", (unsigned)(nexus_data_list_length_get(emit->implicit) / 2));
  }

  l_interchange_name(raw, name, sizeof(name));
  (void)nexus_data_list_append_pointer(emit->implicit,
                                       nexus_data_arena_text_copy(&emit->grammar->arena, name, nexus_strings_string_length_get(name)));
  (void)nexus_data_list_append(emit->implicit, (const void *)&node);

  return (const char *)nexus_data_list_pointer_at(emit->implicit, nexus_data_list_length_get(emit->implicit) - 2);
}

/* precedence: 0 alternatives, 1 sequence, 2 postfix operand (needs parentheses around anything compound). */
static void l_interchange_expression(LIEmit *emit, const LINode *node, LIContext context, uint32 precedence) {
  uint32 i;

  switch (node->kind) {
  case LIK_EMPTY:
    (void)nexus_strings_string_builder_append(emit->target, "''");
    break;
  case LIK_LITERAL:
    if (context == LIX_SYNTAX) {
      (void)nexus_strings_string_builder_append_formatted(emit->target, "$%s", l_interchange_implicit(emit, node));
    } else {
      if (precedence > 1 && (node->length > 1 || node->insensitive)) {
        (void)nexus_strings_string_builder_append(emit->target, "(");
        l_interchange_literal(emit->target, node);
        (void)nexus_strings_string_builder_append(emit->target, ")");
      } else {
        l_interchange_literal(emit->target, node);
      }
    }

    break;
  case LIK_CHARSET:
    if (context == LIX_SYNTAX) {
      (void)nexus_strings_string_builder_append_formatted(emit->target, "$%s", l_interchange_implicit(emit, node));
    } else {
      l_interchange_charset(emit->target, node);
    }

    break;
  case LIK_REF: {
    uint_large          index;
    const LIDefinition *target;

    target = l_interchange_find(emit->grammar, node->text, &index);

    if (target == NULL) {
      logos_languages_interchange_grammar_note(emit->grammar, LIN_ERROR, "`%s` is used but never defined", node->text);
      (void)nexus_strings_string_builder_append(emit->target, "''");
      emit->ok = FALSE;
    } else if (context == LIX_LEXICAL) {
      (void)nexus_strings_string_builder_append_formatted(emit->target, "$_%s", l_interchange_name_of(emit, index));
    } else if (target->kind == LIC_RULE) {
      (void)nexus_strings_string_builder_append_formatted(emit->target, "#%s", l_interchange_name_of(emit, index));
    } else {
      (void)nexus_strings_string_builder_append_formatted(emit->target, "$%s", l_interchange_name_of(emit, index));
    }

    break;
  }
  case LIK_SEQ:
    if (precedence > 1) {
      (void)nexus_strings_string_builder_append(emit->target, "(");
    }

    for (i = 0; i < node->count; i++) {
      if (i > 0) {
        (void)nexus_strings_string_builder_append(emit->target, " ");
      }

      l_interchange_expression(emit, node->items[i], context, 1);
    }

    if (precedence > 1) {
      (void)nexus_strings_string_builder_append(emit->target, ")");
    }

    break;
  case LIK_ALT:
    if (precedence > 0) {
      (void)nexus_strings_string_builder_append(emit->target, "(");
    }

    for (i = 0; i < node->count; i++) {
      if (i > 0) {
        (void)nexus_strings_string_builder_append(emit->target, " || ");
      }

      l_interchange_expression(emit, node->items[i], context, 0);
    }

    if (precedence > 0) {
      (void)nexus_strings_string_builder_append(emit->target, ")");
    }

    break;
  case LIK_STAR:
  case LIK_PLUS:
  case LIK_OPT:
    /* x+? would read as a lazy repetition: a postfix on a postfix is parenthesized. */
    if (node->child->kind == LIK_STAR || node->child->kind == LIK_PLUS || node->child->kind == LIK_OPT) {
      (void)nexus_strings_string_builder_append(emit->target, "(");
      l_interchange_expression(emit, node->child, context, 0);
      (void)nexus_strings_string_builder_append(emit->target, ")");
    } else {
      l_interchange_expression(emit, node->child, context, 2);
    }

    if (node->kind == LIK_STAR) {
      (void)nexus_strings_string_builder_append(emit->target, "*");
    } else {
      (void)nexus_strings_string_builder_append(emit->target, node->kind == LIK_PLUS ? "+" : "?");
    }
    break;
  default:
    break;
  }
}

static void l_interchange_upper(const char *source, char *out, uint_large capacity) {
  uint_large i;

  l_interchange_name(source, out, capacity);

  for (i = 0; out[i] != '\0'; i++) {
    if (out[i] >= 'a' && out[i] <= 'z') {
      out[i] = (char)(out[i] - 'a' + 'A');
    }
  }
}

/* Summary: whether `$name` stands in the text as a whole name. */
static boolean l_interchange_mentions(const char *text, const char *name) {
  char       needle[LI_NAME_MAX + 4];
  uint_large needle_length;
  uint_large offset;

  (void)stbsp_snprintf(needle, (int)sizeof(needle), "$%s", name);
  needle_length = nexus_strings_string_length_get(needle);

  for (offset = 0; text[offset] != '\0'; offset++) {
    char after;

    if (!nexus_memory_bytes_equal(text + offset, needle, needle_length)) {
      continue;
    }

    after = text[offset + needle_length];

    if (!((after >= 'a' && after <= 'z') || (after >= '0' && after <= '9') || after == '_')) {
      return TRUE;
    }
  }

  return FALSE;
}

/* Summary: gives every definition a unique Genesis name (a token and a rule may share one, being told apart by their sigils). */
static void l_interchange_names_assign(LIEmit *emit) {
  LIGrammar *grammar;
  uint_large i;
  uint_large j;
  char       name[LI_NAME_MAX];

  grammar = emit->grammar;

  for (i = 0; i < nexus_data_list_length_get(grammar->definitions); i++) {
    const LIDefinition *definition;
    char                candidate[LI_NAME_MAX];
    uint32              attempt;
    boolean             taken;

    definition = (const LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, i);
    l_interchange_name(definition->name, name, sizeof(name));
    attempt = 1;

    do {
      taken = FALSE;

      if (attempt == 1) {
        (void)stbsp_snprintf(candidate, (int)sizeof(candidate), "%s", name);
      } else {
        (void)stbsp_snprintf(candidate, (int)sizeof(candidate), "%s_%u", name, (unsigned)attempt);
      }

      for (j = 0; j < i; j++) {
        const LIDefinition *other;

        other = (const LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, j);

        if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(emit->names, j), candidate) &&
            ((other->kind == LIC_RULE) == (definition->kind == LIC_RULE))) {
          taken = TRUE;
        }
      }

      attempt++;
    } while (taken);

    (void)nexus_data_list_append_pointer(emit->names,
                                         nexus_data_arena_text_copy(&grammar->arena, candidate, nexus_strings_string_length_get(candidate)));
  }
}

/* Summary: the word `@prec` writes for an associativity. */
static const char *l_interchange_associativity_name(LContextaAssociativity associativity) {
  switch (associativity) {
  case LCPA_LEFT:
    return "left";
  case LCPA_RIGHT:
    return "right";
  case LCPA_NONE:
  case LCPA_NONASSOC:
  default:
    return "nonassoc";
  }
}

/* Summary: writes the action of a token. */
static void l_interchange_action_write(NexusStringBuilder *out, const LIDefinition *token) {
  char target[LI_NAME_MAX];

  if ((token->action == LIA_PUSH || token->action == LIA_REPLACE) && token->action_target != NULL) {
    l_interchange_upper(token->action_target, target, sizeof(target));
    (void)nexus_strings_string_builder_append_formatted(out, " -> %s(%s)", token->action == LIA_PUSH ? "push" : "replace", target);
  } else if (token->action == LIA_POP) {
    (void)nexus_strings_string_builder_append(out, " -> pop(1)");
  }
}

char *logos_languages_interchange_grammar_emit(LIGrammar *grammar) {
  LIEmit             emit;
  uint_large         count;
  uint_large         i;
  uint_large         mode;
  uint_large         mode_count;
  boolean            any_skip;
  char              *result;
  uint_large         length;
  NexusStringBuilder rules;
  NexusStringBuilder main_out;
  const char        *rules_text;

  count         = nexus_data_list_length_get(grammar->definitions);
  emit.grammar  = grammar;
  emit.names    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  emit.tokens   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  emit.implicit = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  emit.ok       = TRUE;
  any_skip      = FALSE;
  rules         = nexus_strings_string_builder_create_growable(1024);
  main_out      = nexus_strings_string_builder_create_growable(2048);

  /* Definitions a format does not classify are lexical when they reach no rule; the rest are rules. */
  for (i = 0; i < count; i++) {
    LIDefinition *definition;

    definition = (LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, i);

    if (definition->kind == LIC_UNKNOWN) {
      const LIDefinition *stack[65];

      stack[0]         = definition;
      definition->kind = l_interchange_lexical(grammar, definition->expression, 1, stack) ? LIC_FRAGMENT : LIC_RULE;
    }
  }

  l_interchange_names_assign(&emit);

  /* The rules, so that the implicit tokens of their literals and sets are known before the contexts are written. */
  emit.target = &rules;

  for (i = 0; i < count; i++) {
    const LIDefinition *definition;

    definition = (const LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, i);

    if (definition->kind != LIC_RULE) {
      continue;
    }

    (void)nexus_strings_string_builder_append_formatted(&rules, "#%s ", l_interchange_name_of(&emit, i));

    if (definition->associativity != LCPA_NONE) {
      (void)nexus_strings_string_builder_append_formatted(&rules, "@prec(%s, %d) ", l_interchange_associativity_name(definition->associativity),
                                                          (int)definition->level);
    }

    (void)nexus_strings_string_builder_append(&rules, "= ");
    l_interchange_expression(&emit, l_interchange_simplify(grammar, definition->expression), LIX_SYNTAX, 0);
    (void)nexus_strings_string_builder_append(&rules, "\n");
  }

  rules_text = nexus_strings_string_builder_string_get(&rules);

  /* The lexical definitions that need an emitting token: the ones the format calls tokens, the entry, and those a rule names. */
  for (i = 0; i < count; i++) {
    const LIDefinition *definition;
    boolean             needed;

    definition = (const LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, i);

    if (definition->kind == LIC_RULE) {
      continue;
    }

    needed = definition->kind == LIC_TOKEN || (grammar->entry != NULL && nexus_strings_string_equals(definition->name, grammar->entry)) ||
             l_interchange_mentions(rules_text, l_interchange_name_of(&emit, i));

    if (needed && !l_interchange_nullable(grammar, definition->expression, 0)) {
      (void)nexus_data_list_append(emit.tokens, (const void *)&definition);
      any_skip = any_skip || definition->skip;
    } else if (needed) {
      logos_languages_interchange_grammar_note(
          grammar, LIN_WARNING,
          "`%s` can match nothing and is used as a token; Genesis tokens are not empty, so places that use it are not carried over",
          definition->name);
    }
  }

  (void)nexus_strings_string_builder_append_formatted(&main_out, "Language: %s\nVersion: 1.0.0\n\nPARSER {\n", grammar->name);

  if (grammar->entry != NULL) {
    uint_large          entry_index;
    const LIDefinition *entry;

    entry = l_interchange_find(grammar, grammar->entry, &entry_index);

    if (entry != NULL) {
      (void)nexus_strings_string_builder_append_formatted(&main_out, "\tentry = #%s%s\n", l_interchange_name_of(&emit, entry_index),
                                                          entry->kind == LIC_RULE ? "" : "_entry");
    }
  }

  if (any_skip) {
    (void)nexus_strings_string_builder_append(&main_out, "\tglobal = #trivia\n");
  }

  (void)nexus_strings_string_builder_append(&main_out, "\tengine = earley\n}\n\n");

  mode_count  = nexus_data_list_length_get(grammar->modes) + 1;
  emit.target = &main_out;

  for (mode = 0; mode < mode_count; mode++) {
    const char *mode_name;
    char        context[LI_NAME_MAX];

    mode_name = mode == 0 ? NULL : (const char *)nexus_data_list_pointer_at(grammar->modes, mode - 1);

    if (mode_name == NULL) {
      (void)stbsp_snprintf(context, (int)sizeof(context), "ROOT");
    } else {
      l_interchange_upper(mode_name, context, sizeof(context));
    }

    (void)nexus_strings_string_builder_append_formatted(&main_out, "CONTEXT <%s> {\n", context);

    /* Every lexical definition is a fragment, visible in every context. */
    for (i = 0; i < count; i++) {
      const LIDefinition *definition;

      definition = (const LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, i);

      if (definition->kind == LIC_RULE) {
        continue;
      }

      (void)nexus_strings_string_builder_append_formatted(&main_out, "\t$_%s = ", l_interchange_name_of(&emit, i));
      l_interchange_expression(&emit, l_interchange_simplify(grammar, definition->expression), LIX_LEXICAL, 0);
      (void)nexus_strings_string_builder_append(&main_out, "\n");
    }

    /* The implicit tokens of the literals and sets that rules use come first in the default context, so they win equal-length ties. */
    for (i = 0; mode == 0 && i + 1 < nexus_data_list_length_get(emit.implicit); i += 2) {
      (void)nexus_strings_string_builder_append_formatted(&main_out, "\t$%s = ", (const char *)nexus_data_list_pointer_at(emit.implicit, i));
      l_interchange_expression(&emit, (const LINode *)nexus_data_list_pointer_at(emit.implicit, i + 1), LIX_LEXICAL, 0);
      (void)nexus_strings_string_builder_append(&main_out, " @priority(10)\n");
    }

    for (i = 0; i < nexus_data_list_length_get(emit.tokens); i++) {
      const LIDefinition *token;
      uint_large          index;

      token = (const LIDefinition *)nexus_data_list_pointer_at(emit.tokens, i);

      if (!((mode_name == NULL && token->mode == NULL) ||
            (mode_name != NULL && token->mode != NULL && nexus_strings_string_equals(mode_name, token->mode)))) {
        continue;
      }

      index = 0;
      (void)l_interchange_find(grammar, token->name, &index);
      (void)nexus_strings_string_builder_append_formatted(&main_out, "\t$%s = $_%s", l_interchange_name_of(&emit, index),
                                                          l_interchange_name_of(&emit, index));

      if (token->priority != 0) {
        (void)nexus_strings_string_builder_append_formatted(&main_out, " @priority(%lld)", (long long)token->priority);
      }

      l_interchange_action_write(&main_out, token);
      (void)nexus_strings_string_builder_append(&main_out, "\n");
    }

    (void)nexus_strings_string_builder_append(&main_out, "}\n\n");
  }

  if (any_skip) {
    boolean first;

    first = TRUE;
    (void)nexus_strings_string_builder_append(&main_out, "#trivia = ");

    for (i = 0; i < nexus_data_list_length_get(emit.tokens); i++) {
      const LIDefinition *token;
      uint_large          index;

      token = (const LIDefinition *)nexus_data_list_pointer_at(emit.tokens, i);

      if (token->skip) {
        index = 0;
        (void)l_interchange_find(grammar, token->name, &index);
        (void)nexus_strings_string_builder_append_formatted(&main_out, "%s$%s", first ? "" : " || ", l_interchange_name_of(&emit, index));
        first = FALSE;
      }
    }

    (void)nexus_strings_string_builder_append(&main_out, "\n");
  }

  (void)nexus_strings_string_builder_append(&main_out, nexus_strings_string_builder_string_get(&rules));

  if (grammar->entry != NULL) {
    uint_large          entry_index;
    const LIDefinition *entry;

    entry = l_interchange_find(grammar, grammar->entry, &entry_index);

    if (entry != NULL && entry->kind != LIC_RULE) {
      (void)nexus_strings_string_builder_append_formatted(&main_out, "#%s_entry = $%s\n", l_interchange_name_of(&emit, entry_index),
                                                          l_interchange_name_of(&emit, entry_index));
    }
  }

  length = nexus_strings_string_builder_length_get(&main_out);
  result = NULL;

  if (emit.ok) {
    result = (char *)nexus_memory_allocate(length + 1);
    nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&main_out), result, length + 1);
  }

  nexus_strings_string_builder_destroy(&rules);
  nexus_strings_string_builder_destroy(&main_out);
  nexus_data_list_destroy(emit.names);
  nexus_data_list_destroy(emit.tokens);
  nexus_data_list_destroy(emit.implicit);

  return result;
}
