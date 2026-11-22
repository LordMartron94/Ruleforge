#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
Extended BNF to the neutral description, in the two notations that go by the name: ISO/IEC 14977 (`name = a, {b | c} ;`) and the one of the W3C
specifications (`Name ::= a (b | c)*`). A file is the W3C notation when it contains `::=`.

ISO: concatenation, alternatives (`|`, `/`, `!`), options, repetitions, groups, counts (`3 * x`), terminal strings and the exception
`x - y`. A special sequence `? ... ?` describes instead of defining and is rejected. W3C: sequence, alternatives, `? * +`, groups, strings,
`#x41` code points, character classes and the exception `A - B`; the constraint notes `[ wfc: ... ]` are comments.

The exception is carried over when both sides are sets of characters (a character, a class, alternatives of them, or a rule defined earlier
that is one); anything else cannot be written as a grammar without a negation and is rejected.
*/

#define LIEBNF_COUNT_MAX  512
#define LIEBNF_RANGES_MAX 4096

typedef struct LEbnf {
  LGenesisCompiler *compiler;
  LIGrammar        *grammar;
  LIParsed          parsed;
  boolean           w3c;
  boolean           ok;
  NexusDataList    *exceptions; /* LEbnfException, resolved once every rule is defined, so that an operand may be a rule that comes later */
} LEbnf;

typedef struct LEbnfException {
  LINode *placeholder;
  LINode *first;
  LINode *second;
} LEbnfException;

typedef struct LEbnfSet {
  uint32 pairs[LIEBNF_RANGES_MAX * 2];
  uint32 count;
} LEbnfSet;

/* ---------------------------------------------------------------------------- */
/* TEXT                                                                          */
/* ---------------------------------------------------------------------------- */

static boolean l_ebnf_is(LEbnf *ebnf, const LParserGreenNode *node, const char *name) {
  return nexus_strings_string_equals(logos_languages_interchange_rule_name(&ebnf->parsed, node), name);
}

static uint_large l_ebnf_encode(uint32 value, char *out) {
  if (value < 0x80) {
    out[0] = (char)value;

    return 1;
  }

  if (value < 0x800) {
    out[0] = (char)(0xC0 | (value >> 6));
    out[1] = (char)(0x80 | (value & 0x3F));

    return 2;
  }

  if (value < 0x10000) {
    out[0] = (char)(0xE0 | (value >> 12));
    out[1] = (char)(0x80 | ((value >> 6) & 0x3F));
    out[2] = (char)(0x80 | (value & 0x3F));

    return 3;
  }

  out[0] = (char)(0xF0 | (value >> 18));
  out[1] = (char)(0x80 | ((value >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((value >> 6) & 0x3F));
  out[3] = (char)(0x80 | (value & 0x3F));

  return 4;
}

static uint32 l_ebnf_decode(const char *text, uint_large length, uint_large *position) {
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

static uint32 l_ebnf_hex_digit(char letter) {
  if (letter >= '0' && letter <= '9') {
    return (uint32)(letter - '0');
  }

  return (uint32)((letter >= 'a' ? letter - 'a' : letter - 'A') + 10);
}

/* Summary: the first token child's kind name and text of a node (text is Nexus-allocated). */
static const LParserGreenNode *l_ebnf_first(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode *inner[2];

  if (logos_languages_interchange_children(&ebnf->parsed, node, inner, 1) == 0) {
    return NULL;
  }

  return inner[0];
}

/* Summary: the quoted text of a terminal string without its quotes, as a literal (an empty string is the empty sequence). */
static LINode *l_ebnf_string(LEbnf *ebnf, const char *text) {
  uint_large length;

  length = nexus_strings_string_length_get(text);

  if (length <= 2) {
    return logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
  }

  return logos_languages_interchange_node_literal(ebnf->grammar, text + 1, length - 2, FALSE);
}

/* ---------------------------------------------------------------------------- */
/* SETS OF CHARACTERS AND THE EXCEPTION                                          */
/* ---------------------------------------------------------------------------- */

static void l_ebnf_set_add(LEbnfSet *set, uint32 low, uint32 high) {
  if (set->count < LIEBNF_RANGES_MAX) {
    set->pairs[(uint_large)set->count * 2]       = low;
    set->pairs[((uint_large)set->count * 2) + 1] = high;
    set->count++;
  }
}

/* Summary: the characters node matches, when it is a set of characters (a character, a plain class, alternatives of them, a rule that is one). */
static boolean l_ebnf_set_of(const LEbnf *ebnf, const LINode *node, LEbnfSet *set, uint32 depth) {
  uint32 i;

  if (node == NULL || depth > 16) {
    return FALSE;
  }

  switch (node->kind) {
  case LIK_LITERAL: {
    uint_large position;
    uint32     code;

    position = 0;
    code     = node->length > 0 ? l_ebnf_decode(node->text, node->length, &position) : 0;

    if (node->length == 0 || position != node->length) {
      return FALSE;
    }

    l_ebnf_set_add(set, code, code);

    return TRUE;
  }
  case LIK_CHARSET:
    if (node->negated) {
      return FALSE;
    }

    for (i = 0; i < node->range_count; i++) {
      l_ebnf_set_add(set, node->ranges[(uint_large)i * 2], node->ranges[((uint_large)i * 2) + 1]);
    }

    return TRUE;
  case LIK_ALT:
    for (i = 0; i < node->count; i++) {
      if (!l_ebnf_set_of(ebnf, node->items[i], set, depth + 1)) {
        return FALSE;
      }
    }

    return TRUE;
  case LIK_REF:
    for (i = 0; i < nexus_data_list_length_get(ebnf->grammar->definitions); i++) {
      const LIDefinition *definition;

      definition = (const LIDefinition *)nexus_data_list_pointer_at(ebnf->grammar->definitions, i);

      if (nexus_strings_string_equals(definition->name, node->text)) {
        return l_ebnf_set_of(ebnf, definition->expression, set, depth + 1);
      }
    }

    return FALSE;
  default:
    return FALSE;
  }
}

static int32 l_ebnf_pair_compare(const void *left, const void *right, void *user_data) {
  const uint32 *a;
  const uint32 *b;

  (void)user_data;
  a = (const uint32 *)left;
  b = (const uint32 *)right;

  return (a[0] > b[0]) - (a[0] < b[0]);
}

/* Summary: the node for first - second; NULL after an error note when a side is not a set of characters. */
static LINode *l_ebnf_difference_resolve(LEbnf *ebnf, const LINode *first, const LINode *second) {
  static LEbnfSet whole;
  static LEbnfSet removed;
  static LEbnfSet result;
  NexusDataArray  view;
  uint32          i;
  uint32          k;
  uint32          start;

  whole.count   = 0;
  removed.count = 0;
  result.count  = 0;

  if (!l_ebnf_set_of(ebnf, first, &whole, 0) || !l_ebnf_set_of(ebnf, second, &removed, 0)) {
    logos_languages_interchange_grammar_note(
        ebnf->grammar, LIN_ERROR, "an exception (x - y) is carried over only between sets of characters; this one has an operand that is not one");
    ebnf->ok = FALSE;

    return NULL;
  }

  view = nexus_data_array_view_create(whole.pairs, NEXUS_SIZEOF(uint32) * 2, whole.count);
  nexus_data_array_sort(&view, l_ebnf_pair_compare, NULL);

  for (i = 0; i < whole.count; i++) {
    uint32 low;
    uint32 high;

    low   = whole.pairs[(uint_large)i * 2];
    high  = whole.pairs[((uint_large)i * 2) + 1];
    start = low;

    /* Cut every removed range out of [low, high], left to right (the removed ranges are not sorted, so each pass takes the smallest start). */
    for (;;) {
      boolean cut;

      cut = FALSE;

      for (k = 0; k < removed.count; k++) {
        uint32 from;
        uint32 stop;

        from = removed.pairs[(uint_large)k * 2];
        stop = removed.pairs[((uint_large)k * 2) + 1];

        if (stop < start || from > high) {
          continue;
        }

        if (from > start) {
          l_ebnf_set_add(&result, start, from - 1);
        }

        if (stop >= high) {
          start = high + 1;
          cut   = TRUE;
          break;
        }

        start = stop + 1;
        cut   = TRUE;
      }

      if (!cut || start > high) {
        break;
      }
    }

    if (start <= high) {
      l_ebnf_set_add(&result, start, high);
    }
  }

  if (result.count == 0) {
    logos_languages_interchange_grammar_note(ebnf->grammar, LIN_ERROR, "an exception (x - y) removes every character of x");
    ebnf->ok = FALSE;

    return NULL;
  }

  return logos_languages_interchange_node_charset(ebnf->grammar, result.pairs, result.count, FALSE);
}

/* Summary: a node that becomes first - second when every rule is defined. */
static LINode *l_ebnf_difference(LEbnf *ebnf, LINode *first, LINode *second) {
  LEbnfException pending;

  pending.placeholder = logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
  pending.first       = first;
  pending.second      = second;
  (void)nexus_data_list_append(ebnf->exceptions, &pending);

  return pending.placeholder;
}

static void l_ebnf_exceptions_resolve(LEbnf *ebnf) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(ebnf->exceptions); i++) {
    const LEbnfException *pending;
    LINode               *result;

    pending = (const LEbnfException *)nexus_data_list_at_const(ebnf->exceptions, i);
    result  = l_ebnf_difference_resolve(ebnf, pending->first, pending->second);

    if (result != NULL) {
      *pending->placeholder = *result;
    }
  }
}

/* ---------------------------------------------------------------------------- */
/* ISO 14977                                                                     */
/* ---------------------------------------------------------------------------- */

static LINode *l_ebnf_iso_definitions(LEbnf *ebnf, const LParserGreenNode *node);

static LINode *l_ebnf_iso_primary(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode *inner;
  LINode                 *result;

  inner = l_ebnf_first(ebnf, node);

  if (inner == NULL) {
    return logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
  }

  if (l_ebnf_is(ebnf, inner, "optional")) {
    return logos_languages_interchange_node_wrap(ebnf->grammar, LIK_OPT, l_ebnf_iso_definitions(ebnf, inner));
  }

  if (l_ebnf_is(ebnf, inner, "repeated")) {
    return logos_languages_interchange_node_wrap(ebnf->grammar, LIK_STAR, l_ebnf_iso_definitions(ebnf, inner));
  }

  if (l_ebnf_is(ebnf, inner, "grouped")) {
    return l_ebnf_iso_definitions(ebnf, inner);
  }

  if (l_ebnf_is(ebnf, inner, "meta_identifier")) {
    const LParserGreenNode **words;
    uint32                   count;
    uint32                   i;
    NexusStringBuilder       name;

    words = logos_languages_interchange_children_get(&ebnf->parsed, inner, &count);
    name  = nexus_strings_string_builder_create_growable(32);

    for (i = 0; i < count; i++) {
      char *text;

      text = logos_languages_interchange_token_text(&ebnf->parsed, words[i]);
      (void)nexus_strings_string_builder_append_formatted(&name, "%s%s", i > 0 ? "_" : "", text);
      nexus_memory_deallocate(text);
    }

    result = logos_languages_interchange_node_reference(ebnf->grammar, nexus_strings_string_builder_string_get(&name));
    nexus_strings_string_builder_destroy(&name);
    nexus_memory_deallocate((void *)words);

    return result;
  }

  {
    const char *kind;
    char       *text;

    kind = logos_languages_interchange_token_name(&ebnf->parsed, inner);
    text = logos_languages_interchange_token_text(&ebnf->parsed, inner);

    if (nexus_strings_string_equals(kind, "special")) {
      logos_languages_interchange_grammar_note(ebnf->grammar, LIN_ERROR, "the special sequence %s describes a language instead of defining it", text);
      ebnf->ok = FALSE;
      result   = logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
    } else {
      result = l_ebnf_string(ebnf, text);
    }

    nexus_memory_deallocate(text);

    return result;
  }
}

static LINode *l_ebnf_iso_factor(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  uint32                   repeat;
  LINode                  *result;

  parts  = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);
  repeat = 1;
  result = NULL;

  for (i = 0; i < count; i++) {
    if (logos_language_tooling_parsing_syntax_green_node_kind_get(parts[i]) == LPGK_TOKEN &&
        nexus_strings_string_equals(logos_languages_interchange_token_name(&ebnf->parsed, parts[i]), "integer")) {
      char      *text;
      uint_large digit;

      text   = logos_languages_interchange_token_text(&ebnf->parsed, parts[i]);
      repeat = 0;

      for (digit = 0; text[digit] >= '0' && text[digit] <= '9' && repeat <= LIEBNF_COUNT_MAX; digit++) {
        repeat = (repeat * 10) + (uint32)(text[digit] - '0');
      }

      nexus_memory_deallocate(text);
    }
  }

  for (i = 0; i < count; i++) {
    if (l_ebnf_is(ebnf, parts[i], "primary")) {
      if (repeat > LIEBNF_COUNT_MAX) {
        logos_languages_interchange_grammar_note(ebnf->grammar, LIN_ERROR, "a repetition count above %u is not carried over",
                                                 (unsigned)LIEBNF_COUNT_MAX);
        ebnf->ok = FALSE;
        repeat   = 1;
      }

      if (repeat == 1) {
        result = l_ebnf_iso_primary(ebnf, parts[i]);
      } else if (repeat == 0) {
        result = logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
      } else {
        LINode **copies;
        uint32   k;

        copies = (LINode **)nexus_memory_allocate((uint_large)repeat * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

        for (k = 0; k < repeat; k++) {
          copies[k] = l_ebnf_iso_primary(ebnf, parts[i]);
        }

        result = logos_languages_interchange_node_group(ebnf->grammar, LIK_SEQ, copies, repeat);
        nexus_memory_deallocate((void *)copies);
      }
    }
  }

  nexus_memory_deallocate((void *)parts);

  return result != NULL ? result : logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
}

static LINode *l_ebnf_iso_term(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode *factors[2];
  uint32                  count;
  LINode                 *first;

  count = logos_languages_interchange_children(&ebnf->parsed, node, factors, 2);

  if (count == 0) {
    return logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
  }

  first = l_ebnf_iso_factor(ebnf, factors[0]);

  /* The children are the first factor, the minus token and the exception: two factors and a token make three. */
  {
    const LParserGreenNode **parts;
    uint32                   part_count;
    LINode                  *result;

    parts  = logos_languages_interchange_children_get(&ebnf->parsed, node, &part_count);
    result = first;

    if (part_count == 3) {
      LINode *removed;

      removed = l_ebnf_iso_factor(ebnf, parts[2]);
      result  = l_ebnf_difference(ebnf, first, removed);

      if (result == NULL) {
        result = first;
      }
    }

    nexus_memory_deallocate((void *)parts);

    return result;
  }
}

static LINode *l_ebnf_iso_single(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  uint32                   used;
  LINode                 **items;
  LINode                  *result;

  parts = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);
  items = (LINode **)nexus_memory_allocate(((uint_large)count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  used  = 0;

  for (i = 0; i < count; i++) {
    if (l_ebnf_is(ebnf, parts[i], "term")) {
      items[used++] = l_ebnf_iso_term(ebnf, parts[i]);
    }
  }

  result = used == 0 ? logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY)
                     : logos_languages_interchange_node_group(ebnf->grammar, LIK_SEQ, items, used);
  nexus_memory_deallocate((void *)items);
  nexus_memory_deallocate((void *)parts);

  return result;
}

/* Summary: the alternatives of a definitions list, or of the one inside an option, repetition or group. */
static LINode *l_ebnf_iso_definitions(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  uint32                   used;
  LINode                 **items;
  LINode                  *result;

  parts = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);
  items = (LINode **)nexus_memory_allocate(((uint_large)count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  used  = 0;

  for (i = 0; i < count; i++) {
    if (l_ebnf_is(ebnf, parts[i], "definitions_list")) {
      items[used++] = l_ebnf_iso_definitions(ebnf, parts[i]);
    } else if (l_ebnf_is(ebnf, parts[i], "single_definition")) {
      items[used++] = l_ebnf_iso_single(ebnf, parts[i]);
    }
  }

  result = used == 0 ? logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY)
                     : logos_languages_interchange_node_group(ebnf->grammar, LIK_ALT, items, used);
  nexus_memory_deallocate((void *)items);
  nexus_memory_deallocate((void *)parts);

  return result;
}

static void l_ebnf_iso_rule(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  char                    *name;
  LINode                  *expression;

  parts      = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);
  name       = NULL;
  expression = NULL;

  for (i = 0; i < count; i++) {
    if (l_ebnf_is(ebnf, parts[i], "meta_identifier") && name == NULL) {
      const LParserGreenNode **words;
      uint32                   word_count;
      uint32                   k;
      NexusStringBuilder       builder;

      words   = logos_languages_interchange_children_get(&ebnf->parsed, parts[i], &word_count);
      builder = nexus_strings_string_builder_create_growable(32);

      for (k = 0; k < word_count; k++) {
        char *text;

        text = logos_languages_interchange_token_text(&ebnf->parsed, words[k]);
        (void)nexus_strings_string_builder_append_formatted(&builder, "%s%s", k > 0 ? "_" : "", text);
        nexus_memory_deallocate(text);
      }

      name = (char *)nexus_memory_allocate(nexus_strings_string_builder_length_get(&builder) + 1);
      nexus_memory_bytes_copy(nexus_strings_string_builder_string_get(&builder), name, nexus_strings_string_builder_length_get(&builder) + 1);
      nexus_strings_string_builder_destroy(&builder);
      nexus_memory_deallocate((void *)words);
    } else if (l_ebnf_is(ebnf, parts[i], "definitions_list")) {
      expression = l_ebnf_iso_definitions(ebnf, parts[i]);
    }
  }

  if (name != NULL && expression != NULL) {
    (void)logos_languages_interchange_grammar_define(ebnf->grammar, name, LIC_UNKNOWN, expression);
  }

  if (name != NULL) {
    nexus_memory_deallocate(name);
  }

  nexus_memory_deallocate((void *)parts);
}

/* ---------------------------------------------------------------------------- */
/* THE NOTATION OF THE W3C                                                       */
/* ---------------------------------------------------------------------------- */

static LINode *l_ebnf_w3c_choice(LEbnf *ebnf, const LParserGreenNode *node);

/* Summary: a character class `[a-z#x30-#x39]` or `[^<&]`. */
static LINode *l_ebnf_w3c_class(LEbnf *ebnf, const char *text) {
  uint_large length;
  uint_large position;
  boolean    negated;
  uint32     pairs[LIEBNF_RANGES_MAX * 2];
  uint32     count;

  length   = nexus_strings_string_length_get(text);
  position = 1;
  negated  = FALSE;
  count    = 0;

  if (position < length && text[position] == '^') {
    negated = TRUE;
    position++;
  }

  while (position + 1 < length && count < LIEBNF_RANGES_MAX) {
    uint32 low;
    uint32 high;
    uint32 side;

    for (side = 0; side < 2; side++) {
      uint32 code;

      if (side == 1) {
        if (!(position + 2 < length && text[position] == '-')) {
          break;
        }

        position++;
      }

      if (text[position] == '#' && position + 2 < length && text[position + 1] == 'x') {
        position += 2;
        code = 0;

        while (position + 1 < length && ((text[position] >= '0' && text[position] <= '9') || (text[position] >= 'a' && text[position] <= 'f') ||
                                         (text[position] >= 'A' && text[position] <= 'F'))) {
          code = (code << 4) | l_ebnf_hex_digit(text[position]);
          position++;
        }
      } else {
        code = l_ebnf_decode(text, length - 1, &position);
      }

      if (side == 0) {
        low  = code;
        high = code;
      } else {
        high = code;
      }
    }

    pairs[(uint_large)count * 2]       = low;
    pairs[((uint_large)count * 2) + 1] = high;
    count++;
  }

  return logos_languages_interchange_node_charset(ebnf->grammar, pairs, count, negated);
}

static LINode *l_ebnf_w3c_primary(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  const LParserGreenNode  *inner;
  const char              *kind;
  char                    *text;
  LINode                  *result;

  parts = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);

  /* A group is `(`, a choice and `)`; anything else is one token. */
  for (i = 0; i < count; i++) {
    if (l_ebnf_is(ebnf, parts[i], "choice")) {
      result = l_ebnf_w3c_choice(ebnf, parts[i]);
      nexus_memory_deallocate((void *)parts);

      return result;
    }
  }

  inner = count > 0 ? parts[0] : NULL;

  if (inner == NULL) {
    nexus_memory_deallocate((void *)parts);

    return logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
  }

  kind = logos_languages_interchange_token_name(&ebnf->parsed, inner);
  text = logos_languages_interchange_token_text(&ebnf->parsed, inner);

  if (nexus_strings_string_equals(kind, "name")) {
    result = logos_languages_interchange_node_reference(ebnf->grammar, text);
  } else if (nexus_strings_string_equals(kind, "hex")) {
    uint32     code;
    uint_large k;
    char       bytes[8];

    code = 0;

    for (k = 2; text[k] != '\0'; k++) {
      code = (code << 4) | l_ebnf_hex_digit(text[k]);
    }

    result = logos_languages_interchange_node_literal(ebnf->grammar, bytes, l_ebnf_encode(code, bytes), FALSE);
  } else if (nexus_strings_string_equals(kind, "class")) {
    result = l_ebnf_w3c_class(ebnf, text);
  } else {
    result = l_ebnf_string(ebnf, text);
  }

  nexus_memory_deallocate(text);
  nexus_memory_deallocate((void *)parts);

  return result;
}

static LINode *l_ebnf_w3c_item(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  LINode                  *result;

  parts  = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);
  result = NULL;

  for (i = 0; i < count; i++) {
    if (l_ebnf_is(ebnf, parts[i], "primary")) {
      result = l_ebnf_w3c_primary(ebnf, parts[i]);
    } else if (result != NULL) {
      const char *kind;

      kind = logos_languages_interchange_token_name(&ebnf->parsed, parts[i]);

      if (nexus_strings_string_equals(kind, "question")) {
        result = logos_languages_interchange_node_wrap(ebnf->grammar, LIK_OPT, result);
      } else if (nexus_strings_string_equals(kind, "star")) {
        result = logos_languages_interchange_node_wrap(ebnf->grammar, LIK_STAR, result);
      } else if (nexus_strings_string_equals(kind, "plus")) {
        result = logos_languages_interchange_node_wrap(ebnf->grammar, LIK_PLUS, result);
      }
    }
  }

  nexus_memory_deallocate((void *)parts);

  return result != NULL ? result : logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
}

static LINode *l_ebnf_w3c_difference(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  LINode                  *result;

  parts = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);

  if (count == 0) {
    nexus_memory_deallocate((void *)parts);

    return logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY);
  }

  result = l_ebnf_w3c_item(ebnf, parts[0]);

  if (count == 3) {
    LINode *removed;
    LINode *difference;

    removed    = l_ebnf_w3c_item(ebnf, parts[2]);
    difference = l_ebnf_difference(ebnf, result, removed);
    result     = difference != NULL ? difference : result;
  }

  nexus_memory_deallocate((void *)parts);

  return result;
}

static LINode *l_ebnf_w3c_sequence(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  uint32                   used;
  LINode                 **items;
  LINode                  *result;

  parts = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);
  items = (LINode **)nexus_memory_allocate(((uint_large)count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  used  = 0;

  for (i = 0; i < count; i++) {
    if (l_ebnf_is(ebnf, parts[i], "difference")) {
      items[used++] = l_ebnf_w3c_difference(ebnf, parts[i]);
    }
  }

  result = used == 0 ? logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY)
                     : logos_languages_interchange_node_group(ebnf->grammar, LIK_SEQ, items, used);
  nexus_memory_deallocate((void *)items);
  nexus_memory_deallocate((void *)parts);

  return result;
}

static LINode *l_ebnf_w3c_choice(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  uint32                   used;
  LINode                 **items;
  LINode                  *result;

  parts = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);
  items = (LINode **)nexus_memory_allocate(((uint_large)count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  used  = 0;

  for (i = 0; i < count; i++) {
    if (l_ebnf_is(ebnf, parts[i], "concatenation")) {
      items[used++] = l_ebnf_w3c_sequence(ebnf, parts[i]);
    }
  }

  result = used == 0 ? logos_languages_interchange_node_new(ebnf->grammar, LIK_EMPTY)
                     : logos_languages_interchange_node_group(ebnf->grammar, LIK_ALT, items, used);
  nexus_memory_deallocate((void *)items);
  nexus_memory_deallocate((void *)parts);

  return result;
}

static void l_ebnf_w3c_rule(LEbnf *ebnf, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  char                    *name;
  LINode                  *expression;

  parts      = logos_languages_interchange_children_get(&ebnf->parsed, node, &count);
  name       = NULL;
  expression = NULL;

  for (i = 0; i < count; i++) {
    if (name == NULL && logos_language_tooling_parsing_syntax_green_node_kind_get(parts[i]) == LPGK_TOKEN &&
        nexus_strings_string_equals(logos_languages_interchange_token_name(&ebnf->parsed, parts[i]), "rule_head")) {
      uint_large end;

      /* The token is `Name ::=`, with white space between the two. */
      name = logos_languages_interchange_token_text(&ebnf->parsed, parts[i]);

      for (end = 0; name[end] != '\0' && name[end] != ' ' && name[end] != '\t' && name[end] != '\r' && name[end] != '\n' && name[end] != ':'; end++) {
      }

      name[end] = '\0';
    } else if (l_ebnf_is(ebnf, parts[i], "choice")) {
      expression = l_ebnf_w3c_choice(ebnf, parts[i]);
    }
  }

  if (name != NULL && expression != NULL) {
    (void)logos_languages_interchange_grammar_define(ebnf->grammar, name, LIC_UNKNOWN, expression);
  }

  if (name != NULL) {
    nexus_memory_deallocate(name);
  }

  nexus_memory_deallocate((void *)parts);
}

/* ---------------------------------------------------------------------------- */
/* ENTRY                                                                         */
/* ---------------------------------------------------------------------------- */

boolean logos_languages_interchange_ebnf_convert(LGenesisCompiler *compiler, LIGrammar *grammar, const LInterchangeInput *inputs,
                                                 uint32 input_count) {
  LEbnf      ebnf;
  uint32     i;
  uint_large k;

  ebnf.compiler   = compiler;
  ebnf.grammar    = grammar;
  ebnf.ok         = TRUE;
  ebnf.exceptions = nexus_data_list_create(NEXUS_SIZEOF(LEbnfException), 8);

  for (i = 0; i < input_count && ebnf.ok; i++) {
    const LParserGreenNode **roots;
    const LParserGreenNode **rules;
    uint32                   root_count;
    uint32                   rule_count;
    uint32                   index;
    boolean                  w3c;

    w3c = FALSE;

    for (k = 0; k + 3 <= inputs[i].size; k++) {
      if (nexus_memory_bytes_equal(inputs[i].text + k, "::=", 3)) {
        w3c = TRUE;
        break;
      }
    }

    ebnf.w3c = w3c;

    if (!logos_languages_interchange_parse(compiler, grammar, w3c ? "W3CEBNF" : "EBNF", inputs[i].text, inputs[i].size, &ebnf.parsed)) {
      logos_languages_interchange_grammar_note(grammar, LIN_NOTE, "in %s", inputs[i].name);
      ebnf.ok = FALSE;
      break;
    }

    roots =
        logos_languages_interchange_children_get(&ebnf.parsed, logos_language_tooling_parsing_syntax_tree_root_get(ebnf.parsed.tree), &root_count);
    rules = logos_languages_interchange_children_get(
        &ebnf.parsed, root_count > 0 ? roots[0] : logos_language_tooling_parsing_syntax_tree_root_get(ebnf.parsed.tree), &rule_count);

    for (index = 0; index < rule_count; index++) {
      if (l_ebnf_is(&ebnf, rules[index], "rule")) {
        if (w3c) {
          l_ebnf_w3c_rule(&ebnf, rules[index]);
        } else {
          l_ebnf_iso_rule(&ebnf, rules[index]);
        }
      }
    }

    nexus_memory_deallocate((void *)rules);
    nexus_memory_deallocate((void *)roots);
    logos_languages_interchange_parsed_release(&ebnf.parsed);
    logos_languages_interchange_grammar_note(grammar, LIN_NOTE,
                                             "%s notation; names are kept, and the rules that no rule uses and that match text are the tokens",
                                             w3c ? "the W3C" : "ISO 14977");
  }

  if (ebnf.ok) {
    l_ebnf_exceptions_resolve(&ebnf);
  }

  nexus_data_list_destroy(ebnf.exceptions);

  return ebnf.ok;
}
