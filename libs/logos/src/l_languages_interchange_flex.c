#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
The scanner of a flex (or lex) file to the neutral description, as the lexical half of a yacc grammar.

A flex file is three sections: definitions (named patterns, %option, %s and %x start conditions, code), rules (a pattern, optionally
under start conditions, then an action) and user code. The structure is line-oriented and is read here; the patterns are read with the
Genesis grammar FlexRegex.

What carries over: the definitions (as fragments), start conditions (as lexical modes; an inclusive %s state also holds the rules that name no
state), case-insensitivity, and every rule whose action returns a token by name (the token is the alternatives of its patterns, per mode).
A rule whose action returns nothing is matched and skipped, so white space, comments and the pieces of a string that a start condition
collects are trivia here. `BEGIN(state)`, `yy_push_state` and `yy_pop_state` become the token's action. `return yytext[0];` and
`return 'c';` are not tokens of their own: the characters a grammar names as literals are tokens already.

What is approximated, with a warning: `^` and `$` (dropped: the scanner then matches in more places), trailing context `r/s` (s is dropped),
`<<EOF>>` rules, and the actions `yymore`, `yyless`, `REJECT`, `unput` and `input` (ignored). What is left out, with a warning: a rule
whose action returns a computed token such as the result of a keyword lookup.

Flex picks the longest match and, among equal ones, the first rule; the priority of each token follows its first rule.
*/

#define LIFLEX_NAME_MAX          96
#define LIFLEX_REPEAT_COPIES_MAX 512

typedef struct LFlexState {
  const char *name;
  const char *mode; /* the Genesis mode of the state; NULL for INITIAL */
  boolean     exclusive;
} LFlexState;

typedef struct LFlexRule {
  const char **conditions; /* start condition names; NULL when the rule names none */
  uint32       condition_count;
  const char  *pattern;
  const char  *action;
  uint32       line;
} LFlexRule;

typedef struct LFlexPending {
  const char  *pattern;
  const char **conditions;
  uint32       condition_count;
} LFlexPending;

typedef struct LFlexPiece {
  const char    *token; /* NULL for a skipped piece */
  const char    *mode;
  LIAction       action;
  const char    *target;
  NexusDataList *patterns; /* LINode * */
  uint32         first_rule;
} LFlexPiece;

typedef struct LFlexAction {
  const char *token;     /* the name after `return` */
  boolean     character; /* returns a character: yytext[0] or a character constant */
  boolean     computed;  /* returns something else */
  boolean     several;   /* more than one distinct token returned */
  LIAction    action;
  const char *target;      /* state name for push, BEGIN */
  boolean     state_given; /* BEGIN, push or pop seen */
  boolean     begin;       /* the state change is a BEGIN (a switch), not a push or pop */
  boolean     conditional; /* the action has an if, else, while, for or switch */
  const char *unsupported; /* yymore, yyless, REJECT, unput, input */
} LFlexAction;

typedef struct LFlex {
  LGenesisCompiler *compiler;
  LIGrammar        *grammar;
  LGenesisEngine   *regex;
  boolean           ok;
  boolean           insensitive;
  boolean           dot_all;
  NexusDataList    *states;      /* LFlexState */
  NexusDataList    *definitions; /* const char *: the names of the definitions made */
  NexusDataList    *rules;       /* LFlexRule * */
  NexusDataList    *pieces;      /* LFlexPiece * */
  uint32            anchors;
  uint32            trailing;
  uint32            end_rules;
  uint32            unsupported;
  uint32            computed;
  uint32            characters;
  uint32            high_bytes;
  uint32            conditional_states;
  const char       *line_text; /* for messages: the pattern being converted */
} LFlex;

/* ---------------------------------------------------------------------------- */
/* TEXT                                                                          */
/* ---------------------------------------------------------------------------- */

static boolean l_flex_blank(char letter) {
  return letter == ' ' || letter == '\t' || letter == '\r' || letter == '\n';
}

static boolean l_flex_word_start(char letter) {
  return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || letter == '_';
}

static boolean l_flex_word_part(char letter) {
  return l_flex_word_start(letter) || (letter >= '0' && letter <= '9');
}

static void l_flex_bytes_append(NexusStringBuilder *builder, const char *text, uint_large length) {
  (void)nexus_strings_string_builder_append_formatted(builder, "%.*s", (int)length, text);
}

static const char *l_flex_copy(LFlex *flex, const char *text, uint_large length) {
  return nexus_data_arena_text_copy(&flex->grammar->arena, text, length);
}

static boolean l_flex_starts(const char *line, uint_large length, const char *prefix) {
  uint_large size;

  size = nexus_strings_string_length_get(prefix);

  return length >= size && nexus_memory_bytes_equal(line, prefix, size);
}

static uint32 l_flex_hex(char letter) {
  if (letter >= '0' && letter <= '9') {
    return (uint32)(letter - '0');
  }

  if (letter >= 'a' && letter <= 'f') {
    return (uint32)(letter - 'a' + 10);
  }

  return (uint32)(letter - 'A' + 10);
}

static boolean l_flex_is_hex(char letter) {
  return (letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'f') || (letter >= 'A' && letter <= 'F');
}

/* Summary: the fragment name of a definition of the scanner, kept apart from the names of the parser's symbols. */
static void l_flex_fragment_name(const char *name, char *out, uint_large capacity) {
  (void)stbsp_snprintf(out, (int)capacity, "lex_%s", name);
}

static uint_large l_flex_encode(uint32 value, char *out) {
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

static uint32 l_flex_decode(const char *text, uint_large length, uint_large *position) {
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

/* Summary: one escape sequence at text[*position] (the backslash already passed): \n \t \xHH \101 or the character itself. */
static uint32 l_flex_escape(const char *text, uint_large length, uint_large *position) {
  char   letter;
  uint32 value;

  if (*position >= length) {
    return '\\';
  }

  letter = text[*position];

  switch (letter) {
  case 'n':
    (*position)++;

    return '\n';
  case 't':
    (*position)++;

    return '\t';
  case 'r':
    (*position)++;

    return '\r';
  case 'f':
    (*position)++;

    return 0x0C;
  case 'v':
    (*position)++;

    return 0x0B;
  case 'a':
    (*position)++;

    return 0x07;
  case 'b':
    (*position)++;

    return 0x08;
  default:
    break;
  }

  if (letter == 'x' && *position + 1 < length && l_flex_is_hex(text[*position + 1])) {
    (*position)++;
    value = l_flex_hex(text[*position]);
    (*position)++;

    if (*position < length && l_flex_is_hex(text[*position])) {
      value = (value << 4) | l_flex_hex(text[*position]);
      (*position)++;
    }

    return value;
  }

  if (letter >= '0' && letter <= '7') {
    uint32 digits;

    value  = 0;
    digits = 0;

    while (digits < 3 && *position < length && text[*position] >= '0' && text[*position] <= '7') {
      value = (value * 8) + (uint32)(text[*position] - '0');
      (*position)++;
      digits++;
    }

    return value;
  }

  return l_flex_decode(text, length, position);
}

/* ---------------------------------------------------------------------------- */
/* CHARACTER CLASSES                                                             */
/* ---------------------------------------------------------------------------- */

typedef struct LFlexRanges {
  uint32 *pairs;
  uint32  count;
  uint32  capacity;
} LFlexRanges;

static void l_flex_ranges_add(LFlexRanges *ranges, uint32 low, uint32 high) {
  if (ranges->count >= ranges->capacity) {
    if (ranges->pairs == NULL) {
      ranges->capacity = 32;
      ranges->pairs    = (uint32 *)nexus_memory_allocate((uint_large)ranges->capacity * 2 * NEXUS_SIZEOF(uint32));
    } else {
      ranges->capacity *= 2;
      ranges->pairs = (uint32 *)nexus_memory_reallocate(ranges->pairs, (uint_large)ranges->capacity * 2 * NEXUS_SIZEOF(uint32));
    }
  }

  ranges->pairs[(uint_large)ranges->count * 2]       = low;
  ranges->pairs[((uint_large)ranges->count * 2) + 1] = high;
  ranges->count++;
}

/* Summary: adds the other-case counterparts of the ASCII letters in [low, high]. */
static void l_flex_ranges_case_add(LFlexRanges *ranges, uint32 low, uint32 high) {
  uint32 first;
  uint32 last;

  first = low > 'a' ? low : 'a';
  last  = high < 'z' ? high : 'z';

  if (first <= last) {
    l_flex_ranges_add(ranges, first - 'a' + 'A', last - 'a' + 'A');
  }

  first = low > 'A' ? low : 'A';
  last  = high < 'Z' ? high : 'Z';

  if (first <= last) {
    l_flex_ranges_add(ranges, first - 'A' + 'a', last - 'A' + 'a');
  }
}

static boolean l_flex_class_name_add(LFlexRanges *ranges, const char *name, uint_large length) {
  if (length == 5 && nexus_memory_bytes_equal(name, "alpha", 5)) {
    l_flex_ranges_add(ranges, 'a', 'z');
    l_flex_ranges_add(ranges, 'A', 'Z');
  } else if (length == 5 && nexus_memory_bytes_equal(name, "digit", 5)) {
    l_flex_ranges_add(ranges, '0', '9');
  } else if (length == 5 && nexus_memory_bytes_equal(name, "alnum", 5)) {
    l_flex_ranges_add(ranges, 'a', 'z');
    l_flex_ranges_add(ranges, 'A', 'Z');
    l_flex_ranges_add(ranges, '0', '9');
  } else if (length == 5 && nexus_memory_bytes_equal(name, "lower", 5)) {
    l_flex_ranges_add(ranges, 'a', 'z');
  } else if (length == 5 && nexus_memory_bytes_equal(name, "upper", 5)) {
    l_flex_ranges_add(ranges, 'A', 'Z');
  } else if (length == 6 && nexus_memory_bytes_equal(name, "xdigit", 6)) {
    l_flex_ranges_add(ranges, '0', '9');
    l_flex_ranges_add(ranges, 'a', 'f');
    l_flex_ranges_add(ranges, 'A', 'F');
  } else if (length == 5 && nexus_memory_bytes_equal(name, "space", 5)) {
    l_flex_ranges_add(ranges, ' ', ' ');
    l_flex_ranges_add(ranges, '\t', '\r');
  } else if (length == 5 && nexus_memory_bytes_equal(name, "blank", 5)) {
    l_flex_ranges_add(ranges, ' ', ' ');
    l_flex_ranges_add(ranges, '\t', '\t');
  } else if (length == 5 && nexus_memory_bytes_equal(name, "cntrl", 5)) {
    l_flex_ranges_add(ranges, 0, 0x1F);
    l_flex_ranges_add(ranges, 0x7F, 0x7F);
  } else if (length == 5 && nexus_memory_bytes_equal(name, "graph", 5)) {
    l_flex_ranges_add(ranges, '!', '~');
  } else if (length == 5 && nexus_memory_bytes_equal(name, "print", 5)) {
    l_flex_ranges_add(ranges, ' ', '~');
  } else if (length == 5 && nexus_memory_bytes_equal(name, "punct", 5)) {
    l_flex_ranges_add(ranges, '!', '/');
    l_flex_ranges_add(ranges, ':', '@');
    l_flex_ranges_add(ranges, '[', '`');
    l_flex_ranges_add(ranges, '{', '~');
  } else {
    return FALSE;
  }

  return TRUE;
}

/* Summary: the node of the class token `[...]` (text includes the brackets). */
static LINode *l_flex_class(LFlex *flex, const char *text, uint_large length, boolean insensitive) {
  LFlexRanges ranges;
  uint_large  position;
  boolean     negated;
  boolean     first;
  LINode     *node;

  ranges.pairs    = NULL;
  ranges.count    = 0;
  ranges.capacity = 0;
  position        = 1;
  negated         = FALSE;
  first           = TRUE;

  if (position < length && text[position] == '^') {
    negated = TRUE;
    position++;
  }

  while (position + 1 < length || (position < length && text[position] != ']')) {
    uint32  low;
    boolean low_numeric;

    if (text[position] == ']' && !first) {
      break;
    }

    low_numeric =
        text[position] == '\\' && position + 1 < length && ((text[position + 1] >= '0' && text[position + 1] <= '7') || text[position + 1] == 'x');

    first = FALSE;

    if (text[position] == '[' && position + 1 < length && text[position + 1] == ':') {
      uint_large end;

      end = position + 2;

      while (end + 1 < length && !(text[end] == ':' && text[end + 1] == ']')) {
        end++;
      }

      if (end + 1 < length && l_flex_class_name_add(&ranges, text + position + 2, end - position - 2)) {
        position = end + 2;
        continue;
      }
    }

    if (text[position] == '\\') {
      position++;
      low = l_flex_escape(text, length, &position);
    } else {
      low = l_flex_decode(text, length, &position);
    }

    if (position + 1 < length && text[position] == '-' && text[position + 1] != ']') {
      uint32  high;
      boolean high_numeric;

      position++;
      high_numeric =
          text[position] == '\\' && position + 1 < length && ((text[position + 1] >= '0' && text[position + 1] <= '7') || text[position + 1] == 'x');

      if (text[position] == '\\') {
        position++;
        high = l_flex_escape(text, length, &position);
      } else {
        high = l_flex_decode(text, length, &position);
      }

      /* A scanner of bytes writes the characters beyond ASCII as [\200-\377]; for text that is UTF-8 it means every character beyond ASCII. */
      if (low_numeric && high_numeric && low >= 0x80 && high == 0xFF) {
        high = 0x10FFFF;
        flex->high_bytes++;
      }

      if (high < low) {
        logos_languages_interchange_grammar_note(flex->grammar, LIN_ERROR, "the character range %u-%u in `%s` runs backwards", (unsigned)low,
                                                 (unsigned)high, flex->line_text);
        flex->ok = FALSE;
        high     = low;
      }

      l_flex_ranges_add(&ranges, low, high);

      if (insensitive) {
        l_flex_ranges_case_add(&ranges, low, high);
      }
    } else {
      l_flex_ranges_add(&ranges, low, low);

      if (insensitive) {
        l_flex_ranges_case_add(&ranges, low, low);
      }
    }
  }

  node = logos_languages_interchange_node_charset(flex->grammar, ranges.pairs, ranges.count, negated);

  if (ranges.pairs != NULL) {
    nexus_memory_deallocate(ranges.pairs);
  }

  return node;
}

/* ---------------------------------------------------------------------------- */
/* PATTERNS                                                                      */
/* ---------------------------------------------------------------------------- */

typedef struct LFlexContext {
  const LIParsed *parsed;
  boolean         insensitive;
  boolean         dot_all;
} LFlexContext;

static boolean l_flex_is(const LFlexContext *context, const LParserGreenNode *node, const char *name) {
  return nexus_strings_string_equals(logos_languages_interchange_rule_name(context->parsed, node), name);
}

static LINode *l_flex_alternation(LFlex *flex, const LFlexContext *context, const LParserGreenNode *node);

static LINode *l_flex_literal_code(LFlex *flex, const LFlexContext *context, uint32 code) {
  char bytes[8];

  return logos_languages_interchange_node_literal(flex->grammar, bytes, l_flex_encode(code, bytes), context->insensitive);
}

static LINode *l_flex_atom(LFlex *flex, const LFlexContext *context, const LParserGreenNode *node) {
  const LParserGreenNode *inner[2];
  const char             *kind;
  char                   *text;
  uint_large              length;
  LINode                 *result;

  if (logos_languages_interchange_children(context->parsed, node, inner, 2) == 0) {
    return logos_languages_interchange_node_new(flex->grammar, LIK_EMPTY);
  }

  if (l_flex_is(context, inner[0], "group")) {
    const LParserGreenNode **parts;
    uint32                   count;
    uint32                   i;
    LFlexContext             nested;

    nested = *context;
    parts  = logos_languages_interchange_children_get(context->parsed, inner[0], &count);
    result = NULL;

    for (i = 0; i < count; i++) {
      const char *token;

      token = logos_language_tooling_parsing_syntax_green_node_kind_get(parts[i]) == LPGK_TOKEN
                  ? logos_languages_interchange_token_name(context->parsed, parts[i])
                  : "";

      if (nexus_strings_string_equals(token, "lparen_options")) {
        char      *options;
        uint_large k;
        boolean    negative;

        options  = logos_languages_interchange_token_text(context->parsed, parts[i]);
        negative = FALSE;

        for (k = 2; options[k] != '\0' && options[k] != ':'; k++) {
          if (options[k] == '-') {
            negative = TRUE;
          } else if (options[k] == 'i') {
            nested.insensitive = !negative;
          } else if (options[k] == 's') {
            nested.dot_all = !negative;
          } else if (options[k] != 'x') {
            logos_languages_interchange_grammar_note(flex->grammar, LIN_WARNING, "the group option `%c` of `%s` is ignored", options[k],
                                                     flex->line_text);
          }
        }

        nexus_memory_deallocate(options);
      } else if (l_flex_is(context, parts[i], "alternation")) {
        result = l_flex_alternation(flex, &nested, parts[i]);
      }
    }

    nexus_memory_deallocate((void *)parts);

    return result != NULL ? result : logos_languages_interchange_node_new(flex->grammar, LIK_EMPTY);
  }

  kind   = logos_languages_interchange_token_name(context->parsed, inner[0]);
  text   = logos_languages_interchange_token_text(context->parsed, inner[0]);
  length = nexus_strings_string_length_get(text);

  if (nexus_strings_string_equals(kind, "string")) {
    char      *bytes;
    uint_large out;
    uint_large position;

    bytes    = (char *)nexus_memory_allocate(length + 1);
    out      = 0;
    position = 1;

    while (position + 1 < length) {
      uint32     code;
      char       encoded[8];
      uint_large size;
      boolean    numeric;

      numeric = FALSE;

      if (text[position] == '\\') {
        numeric = position + 1 < length && ((text[position + 1] >= '0' && text[position + 1] <= '7') || text[position + 1] == 'x');
        position++;
        code = l_flex_escape(text, length - 1, &position);
      } else {
        code = l_flex_decode(text, length - 1, &position);
      }

      /* \xEF\xBB\xBF names the bytes of a character in the scanner's text, which is UTF-8 here. */
      if (numeric && code >= 0x80 && code <= 0xFF) {
        bytes[out++] = (char)code;
        continue;
      }

      size = l_flex_encode(code, encoded);
      nexus_memory_bytes_copy(encoded, bytes + out, size);
      out += size;
    }

    result = out == 0 ? logos_languages_interchange_node_new(flex->grammar, LIK_EMPTY)
                      : logos_languages_interchange_node_literal(flex->grammar, bytes, out, context->insensitive);
    nexus_memory_deallocate(bytes);
  } else if (nexus_strings_string_equals(kind, "class")) {
    result = l_flex_class(flex, text, length, context->insensitive);
  } else if (nexus_strings_string_equals(kind, "dot")) {
    uint32 newline[2];

    newline[0] = '\n';
    newline[1] = '\n';
    result     = context->dot_all ? logos_languages_interchange_node_charset(flex->grammar, newline, 0, TRUE)
                                  : logos_languages_interchange_node_charset(flex->grammar, newline, 1, TRUE);
  } else if (nexus_strings_string_equals(kind, "escape")) {
    uint_large position;

    position = 1;
    result   = l_flex_literal_code(flex, context, l_flex_escape(text, length, &position));
  } else if (nexus_strings_string_equals(kind, "name")) {
    char       name[LIFLEX_NAME_MAX];
    char       fragment[LIFLEX_NAME_MAX + 8];
    uint_large k;
    boolean    found;

    (void)stbsp_snprintf(name, (int)sizeof(name), "%.*s", (int)(length > 2 ? length - 2 : 0), text + 1);
    l_flex_fragment_name(name, fragment, sizeof(fragment));
    found = FALSE;

    for (k = 0; k < nexus_data_list_length_get(flex->definitions); k++) {
      found = found || nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(flex->definitions, k), name);
    }

    if (!found) {
      logos_languages_interchange_grammar_note(flex->grammar, LIN_ERROR, "`%s` uses the name `{%s}`, which no definition gives", flex->line_text,
                                               name);
      flex->ok = FALSE;
    }

    result = logos_languages_interchange_node_reference(flex->grammar, fragment);
  } else if (nexus_strings_string_equals(kind, "caret") || nexus_strings_string_equals(kind, "dollar")) {
    flex->anchors++;
    result = logos_languages_interchange_node_new(flex->grammar, LIK_EMPTY);
  } else {
    /* A plain character stands for itself. */
    uint_large position;

    position = 0;
    result   = l_flex_literal_code(flex, context, l_flex_decode(text, length, &position));
  }

  nexus_memory_deallocate(text);

  return result;
}

static LINode *l_flex_repetition(LFlex *flex, const LFlexContext *context, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  LINode                  *result;

  parts = logos_languages_interchange_children_get(context->parsed, node, &count);

  if (count == 0) {
    nexus_memory_deallocate((void *)parts);

    return logos_languages_interchange_node_new(flex->grammar, LIK_EMPTY);
  }

  result = l_flex_atom(flex, context, parts[0]);

  for (i = 1; i < count; i++) {
    const LParserGreenNode *quantifier[2];
    const char             *kind;

    if (logos_languages_interchange_children(context->parsed, parts[i], quantifier, 2) == 0) {
      continue;
    }

    kind = logos_languages_interchange_token_name(context->parsed, quantifier[0]);

    if (nexus_strings_string_equals(kind, "star")) {
      result = logos_languages_interchange_node_wrap(flex->grammar, LIK_STAR, result);
    } else if (nexus_strings_string_equals(kind, "plus")) {
      result = logos_languages_interchange_node_wrap(flex->grammar, LIK_PLUS, result);
    } else if (nexus_strings_string_equals(kind, "question")) {
      result = logos_languages_interchange_node_wrap(flex->grammar, LIK_OPT, result);
    } else {
      /* {n}, {n,} or {n,m}: copies of the operand, so it is read again for each. */
      char      *text;
      uint32     low;
      uint32     high;
      boolean    open;
      uint_large k;
      LINode    *copies[LIFLEX_REPEAT_COPIES_MAX];
      uint32     used;

      text = logos_languages_interchange_token_text(context->parsed, quantifier[0]);
      low  = 0;
      k    = 1;

      while (text[k] >= '0' && text[k] <= '9') {
        low = (low * 10) + (uint32)(text[k] - '0');
        k++;
      }

      high = low;
      open = FALSE;

      if (text[k] == ',') {
        k++;
        high = 0;
        open = text[k] == '}';

        while (text[k] >= '0' && text[k] <= '9') {
          high = (high * 10) + (uint32)(text[k] - '0');
          k++;
        }
      }

      nexus_memory_deallocate(text);

      if (low > LIFLEX_REPEAT_COPIES_MAX / 2 || (!open && high > LIFLEX_REPEAT_COPIES_MAX / 2) || (!open && high < low)) {
        logos_languages_interchange_grammar_note(flex->grammar, LIN_ERROR, "the repetition in `%s` is too large or runs backwards", flex->line_text);
        flex->ok = FALSE;
        continue;
      }

      used = 0;

      for (k = 0; k < low; k++) {
        copies[used++] = k == 0 ? result : l_flex_atom(flex, context, parts[0]);
      }

      if (open) {
        LINode *tail;

        tail           = low == 0 ? result : l_flex_atom(flex, context, parts[0]);
        copies[used++] = logos_languages_interchange_node_wrap(flex->grammar, LIK_STAR, tail);
      } else if (high > low) {
        LINode *nested;

        /* x{2,4} is x x (x (x)?)?: the optional copies nest. */
        nested = NULL;

        for (k = high - low; k > 0; k--) {
          LINode *copy;
          LINode *pair[2];

          copy = (low == 0 && k == high - low && nested == NULL && used == 0) ? result : l_flex_atom(flex, context, parts[0]);

          if (nested == NULL) {
            nested = logos_languages_interchange_node_wrap(flex->grammar, LIK_OPT, copy);
          } else {
            pair[0] = copy;
            pair[1] = nested;
            nested  = logos_languages_interchange_node_wrap(flex->grammar, LIK_OPT,
                                                            logos_languages_interchange_node_group(flex->grammar, LIK_SEQ, pair, 2));
          }
        }

        copies[used++] = nested;
      }

      result = used == 0 ? logos_languages_interchange_node_new(flex->grammar, LIK_EMPTY)
                         : logos_languages_interchange_node_group(flex->grammar, LIK_SEQ, copies, used);
    }
  }

  nexus_memory_deallocate((void *)parts);

  return result;
}

static LINode *l_flex_concatenation(LFlex *flex, const LFlexContext *context, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  LINode                 **items;
  LINode                  *result;

  parts = logos_languages_interchange_children_get(context->parsed, node, &count);

  if (count == 0) {
    nexus_memory_deallocate((void *)parts);

    return logos_languages_interchange_node_new(flex->grammar, LIK_EMPTY);
  }

  items = (LINode **)nexus_memory_allocate((uint_large)count * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  for (i = 0; i < count; i++) {
    items[i] = l_flex_repetition(flex, context, parts[i]);
  }

  result = logos_languages_interchange_node_group(flex->grammar, LIK_SEQ, items, count);
  nexus_memory_deallocate((void *)items);
  nexus_memory_deallocate((void *)parts);

  return result;
}

static LINode *l_flex_alternation(LFlex *flex, const LFlexContext *context, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  uint32                   used;
  LINode                 **items;
  LINode                  *result;

  parts = logos_languages_interchange_children_get(context->parsed, node, &count);
  items = (LINode **)nexus_memory_allocate(((uint_large)count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  used  = 0;

  for (i = 0; i < count; i++) {
    if (l_flex_is(context, parts[i], "concatenation")) {
      items[used++] = l_flex_concatenation(flex, context, parts[i]);
    }
  }

  /* An alternation with an empty branch (`a|`) reaches the tree as a branch that is a concatenation of nothing. */
  if (used == 0) {
    items[used++] = logos_languages_interchange_node_new(flex->grammar, LIK_EMPTY);
  }

  result = logos_languages_interchange_node_group(flex->grammar, LIK_ALT, items, used);
  nexus_memory_deallocate((void *)items);
  nexus_memory_deallocate((void *)parts);

  return result;
}

/* Summary: the node of a whole pattern text; trailing context is dropped with a warning. */
static LINode *l_flex_pattern(LFlex *flex, const char *text, uint_large length) {
  LIParsed                 parsed;
  LFlexContext             context;
  const LParserGreenNode  *roots[2];
  const LParserGreenNode  *pattern[2];
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  LINode                  *result;

  flex->line_text = text;

  if (!logos_languages_interchange_reader_parse(flex->regex, flex->grammar, (const byte *)text, length, &parsed)) {
    logos_languages_interchange_grammar_note(flex->grammar, LIN_NOTE, "in the pattern `%s`", text);
    flex->ok = FALSE;

    return NULL;
  }

  context.parsed      = &parsed;
  context.insensitive = flex->insensitive;
  context.dot_all     = FALSE;
  result              = NULL;

  if (logos_languages_interchange_children(&parsed, logos_language_tooling_parsing_syntax_tree_root_get(parsed.tree), roots, 2) > 0 &&
      logos_languages_interchange_children(&parsed, roots[0], pattern, 1) >= 0) {
    parts = logos_languages_interchange_children_get(&parsed, roots[0], &count);

    for (i = 0; i < count; i++) {
      if (l_flex_is(&context, parts[i], "alternation") && result == NULL) {
        result = l_flex_alternation(flex, &context, parts[i]);
      } else if (logos_language_tooling_parsing_syntax_green_node_kind_get(parts[i]) == LPGK_TOKEN) {
        flex->trailing++;
      }
    }

    nexus_memory_deallocate((void *)parts);
  }

  logos_languages_interchange_parsed_release(&parsed);

  return result;
}

/* ---------------------------------------------------------------------------- */
/* THE FILE                                                                      */
/* ---------------------------------------------------------------------------- */

typedef struct LFlexLines {
  const char *text;
  uint_large  size;
  uint_large  position;
} LFlexLines;

/* Summary: the next line, without its line end; FALSE at the end of the text. */
static boolean l_flex_line_next(LFlexLines *lines, const char **out_line, uint_large *out_length) {
  uint_large start;
  uint_large end;

  if (lines->position >= lines->size) {
    return FALSE;
  }

  start = lines->position;
  end   = start;

  while (end < lines->size && lines->text[end] != '\n') {
    end++;
  }

  lines->position = end < lines->size ? end + 1 : end;

  if (end > start && lines->text[end - 1] == '\r') {
    end--;
  }

  *out_line   = lines->text + start;
  *out_length = end - start;

  return TRUE;
}

/* Summary: skips past the end of a C comment, string or character constant that starts at text[i]; returns the index after it. */
static uint_large l_flex_c_skip(const char *text, uint_large length, uint_large i) {
  char quote;

  if (text[i] == '/' && i + 1 < length && text[i + 1] == '*') {
    i += 2;

    while (i + 1 < length && !(text[i] == '*' && text[i + 1] == '/')) {
      i++;
    }

    return i + 2 < length ? i + 2 : length;
  }

  if (text[i] == '/' && i + 1 < length && text[i + 1] == '/') {
    while (i < length && text[i] != '\n') {
      i++;
    }

    return i;
  }

  quote = text[i];
  i++;

  while (i < length && text[i] != quote) {
    if (text[i] == '\\') {
      i++;
    }

    i++;
  }

  return i < length ? i + 1 : length;
}

/* Summary: the end of a pattern that starts at line[start]: the first white space outside quotes, classes and braces. */
static uint_large l_flex_pattern_end(const char *line, uint_large length, uint_large start) {
  uint_large i;

  i = start;

  while (i < length && line[i] != ' ' && line[i] != '\t') {
    if (line[i] == '\\') {
      i += 2;
    } else if (line[i] == '"') {
      i++;

      while (i < length && line[i] != '"') {
        if (line[i] == '\\') {
          i++;
        }

        i++;
      }

      i++;
    } else if (line[i] == '[') {
      i++;

      if (i < length && line[i] == '^') {
        i++;
      }

      if (i < length && line[i] == ']') {
        i++;
      }

      while (i < length && line[i] != ']') {
        if (line[i] == '\\') {
          i++;
        } else if (line[i] == '[' && i + 1 < length && line[i + 1] == ':') {
          i += 2;

          while (i + 1 < length && !(line[i] == ':' && line[i + 1] == ']')) {
            i++;
          }

          i++;
        }

        i++;
      }

      i++;
    } else if (line[i] == '{') {
      while (i < length && line[i] != '}') {
        i++;
      }

      i++;
    } else {
      i++;
    }
  }

  return i < length ? i : length;
}

static const LFlexState *l_flex_state_find(const LFlex *flex, const char *name, uint_large length) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(flex->states); i++) {
    const LFlexState *state;

    state = (const LFlexState *)nexus_data_list_at_const(flex->states, i);

    if (nexus_strings_string_length_get(state->name) == length && nexus_memory_bytes_equal(state->name, name, length)) {
      return state;
    }
  }

  return NULL;
}

static void l_flex_state_add(LFlex *flex, const char *name, uint_large length, boolean exclusive) {
  LFlexState state;

  if (l_flex_state_find(flex, name, length) != NULL) {
    return;
  }

  state.name      = l_flex_copy(flex, name, length);
  state.exclusive = exclusive;
  state.mode      = logos_languages_interchange_grammar_mode_add(flex->grammar, state.name);
  (void)nexus_data_list_append(flex->states, &state);
}

/* Summary: one definition `name pattern` of the first section. */
static void l_flex_definition(LFlex *flex, const char *line, uint_large length) {
  uint_large name_end;
  uint_large start;
  uint_large end;
  char       name[LIFLEX_NAME_MAX];
  char       fragment[LIFLEX_NAME_MAX + 8];
  char      *pattern;
  LINode    *node;

  name_end = 0;

  while (name_end < length && (l_flex_word_part(line[name_end]) || line[name_end] == '-')) {
    name_end++;
  }

  start = name_end;

  while (start < length && (line[start] == ' ' || line[start] == '\t')) {
    start++;
  }

  end = length;

  while (end > start && (line[end - 1] == ' ' || line[end - 1] == '\t')) {
    end--;
  }

  if (name_end == 0 || start >= end || name_end >= LIFLEX_NAME_MAX) {
    return;
  }

  (void)stbsp_snprintf(name, (int)sizeof(name), "%.*s", (int)name_end, line);
  pattern = (char *)nexus_memory_allocate(end - start + 1);
  nexus_memory_bytes_copy(line + start, pattern, end - start);
  pattern[end - start] = '\0';

  /* Registered before it is read, so that a definition may refer to itself in an error message and to later ones by order of reading. */
  {
    const char *copy;

    copy = l_flex_copy(flex, name, name_end);
    (void)nexus_data_list_append(flex->definitions, (const void *)&copy);
  }
  node = l_flex_pattern(flex, pattern, end - start);
  nexus_memory_deallocate(pattern);

  if (node != NULL) {
    l_flex_fragment_name(name, fragment, sizeof(fragment));
    (void)logos_languages_interchange_grammar_define(flex->grammar, fragment, LIC_FRAGMENT, node);
  }
}

static void l_flex_words(LFlex *flex, const char *line, uint_large length, uint_large from, boolean exclusive) {
  uint_large i;

  i = from;

  while (i < length) {
    uint_large start;

    while (i < length && (line[i] == ' ' || line[i] == '\t' || line[i] == ',')) {
      i++;
    }

    start = i;

    while (i < length && l_flex_word_part(line[i])) {
      i++;
    }

    if (i > start) {
      l_flex_state_add(flex, line + start, i - start, exclusive);
    } else if (i < length) {
      i++;
    }
  }
}

static void l_flex_option(LFlex *flex, const char *line, uint_large length) {
  uint_large i;

  for (i = 0; i + 7 <= length; i++) {
    if (nexus_memory_bytes_equal(line + i, "caseless", 8 <= length - i ? 8 : 0) ||
        (i + 16 <= length && nexus_memory_bytes_equal(line + i, "case-insensitive", 16))) {
      flex->insensitive = TRUE;
    }
  }
}

/* Summary: the action text that starts at line[start] and may go on over later lines when it opens a brace. */
static const char *l_flex_action_read(LFlex *flex, LFlexLines *lines, const char *line, uint_large length, uint_large start) {
  NexusStringBuilder builder;
  uint_large         depth;
  uint_large         i;
  const char        *result;

  while (start < length && (line[start] == ' ' || line[start] == '\t')) {
    start++;
  }

  if (start >= length || line[start] != '{') {
    return l_flex_copy(flex, line + start, length - start);
  }

  builder = nexus_strings_string_builder_create_growable(256);
  depth   = 0;

  for (;;) {
    for (i = start; i < length; i++) {
      char letter;

      letter = line[i];

      if (letter == '"' || letter == '\'' || (letter == '/' && i + 1 < length && (line[i + 1] == '*' || line[i + 1] == '/'))) {
        uint_large skip;

        skip = l_flex_c_skip(line, length, i);
        l_flex_bytes_append(&builder, line + i, skip - i);
        i = skip - 1;
        continue;
      }

      l_flex_bytes_append(&builder, &line[i], 1);

      if (letter == '{') {
        depth++;
      } else if (letter == '}' && depth > 0) {
        depth--;

        if (depth == 0) {
          result = l_flex_copy(flex, nexus_strings_string_builder_string_get(&builder), nexus_strings_string_builder_length_get(&builder));
          nexus_strings_string_builder_destroy(&builder);

          return result;
        }
      }
    }

    l_flex_bytes_append(&builder, "\n", 1);

    if (!l_flex_line_next(lines, &line, &length)) {
      break;
    }

    start = 0;
  }

  result = l_flex_copy(flex, nexus_strings_string_builder_string_get(&builder), nexus_strings_string_builder_length_get(&builder));
  nexus_strings_string_builder_destroy(&builder);

  return result;
}

static void l_flex_rule_add(LFlex *flex, const char *pattern, uint_large pattern_length, const char **conditions, uint32 condition_count,
                            const char *action, uint32 line_number) {
  LFlexRule *rule;

  rule                  = (LFlexRule *)nexus_data_arena_allocate(&flex->grammar->arena, NEXUS_SIZEOF(*rule));
  rule->pattern         = l_flex_copy(flex, pattern, pattern_length);
  rule->conditions      = conditions;
  rule->condition_count = condition_count;
  rule->action          = action;
  rule->line            = line_number;
  (void)nexus_data_list_append_pointer(flex->rules, rule);
}

/* Summary: reads the rules section line by line. */
static void l_flex_rules_read(LFlex *flex, LFlexLines *lines, uint32 *line_number) {
  const char    *line;
  uint_large     length;
  const char   **scope;
  uint32         scope_count;
  NexusDataList *pending;

  scope       = NULL;
  scope_count = 0;
  pending     = nexus_data_list_create(NEXUS_SIZEOF(LFlexPending), 8);

  while (l_flex_line_next(lines, &line, &length)) {
    const char **conditions;
    uint32       condition_count;
    uint_large   position;
    uint_large   pattern_end;

    (*line_number)++;

    if (length == 0 || l_flex_blank(line[0])) {
      continue;
    }

    if (l_flex_starts(line, length, "%%")) {
      break;
    }

    if (l_flex_starts(line, length, "%{")) {
      const char *skipped;
      uint_large  skipped_length;

      while (l_flex_line_next(lines, &skipped, &skipped_length)) {
        (*line_number)++;

        if (l_flex_starts(skipped, skipped_length, "%}")) {
          break;
        }
      }

      continue;
    }

    if (line[0] == '}' && scope != NULL) {
      scope       = NULL;
      scope_count = 0;
      continue;
    }

    conditions      = scope;
    condition_count = scope_count;
    position        = 0;

    if (l_flex_starts(line, length, "<<EOF>>")) {
      flex->end_rules++;
      (void)l_flex_action_read(flex, lines, line, length, 7);
      continue;
    }

    if (line[0] == '<') {
      uint_large  end;
      uint_large  i;
      const char *names[64];
      uint32      used;

      end = 1;

      while (end < length && line[end] != '>') {
        end++;
      }

      used = 0;
      i    = 1;

      while (i < end && used < 64) {
        uint_large start;

        start = i;

        while (i < end && line[i] != ',') {
          i++;
        }

        names[used++] = l_flex_copy(flex, line + start, i - start);
        i++;
      }

      conditions = (const char **)nexus_data_arena_allocate(&flex->grammar->arena, ((uint_large)used + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
      nexus_memory_bytes_copy((const void *)names, (void *)conditions, (uint_large)used * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
      condition_count = used;
      position        = end + 1;

      /* <state>{ opens a group of rules that share the start condition. */
      if (position < length && line[position] == '{') {
        uint_large rest;

        rest = position + 1;

        while (rest < length && l_flex_blank(line[rest])) {
          rest++;
        }

        if (rest >= length) {
          scope       = conditions;
          scope_count = condition_count;
          continue;
        }
      }
    }

    pattern_end = l_flex_pattern_end(line, length, position);

    if (pattern_end > position) {
      const char *action;
      uint_large  action_start;

      action_start = pattern_end;

      while (action_start < length && (line[action_start] == ' ' || line[action_start] == '\t')) {
        action_start++;
      }

      /* `|` as the action: the pattern shares the action of the next rule. */
      if (action_start + 1 == length && line[action_start] == '|') {
        LFlexPending entry;

        entry.pattern         = l_flex_copy(flex, line + position, pattern_end - position);
        entry.conditions      = conditions;
        entry.condition_count = condition_count;
        (void)nexus_data_list_append(pending, &entry);
        continue;
      }

      action = l_flex_action_read(flex, lines, line, length, action_start);

      {
        uint_large k;

        for (k = 0; k < nexus_data_list_length_get(pending); k++) {
          const LFlexPending *entry;

          entry = (const LFlexPending *)nexus_data_list_at_const(pending, k);
          l_flex_rule_add(flex, entry->pattern, nexus_strings_string_length_get(entry->pattern), entry->conditions, entry->condition_count, action,
                          *line_number);
        }

        nexus_data_list_clear(pending);
      }

      l_flex_rule_add(flex, line + position, pattern_end - position, conditions, condition_count, action, *line_number);
    }
  }

  nexus_data_list_destroy(pending);
}

/* ---------------------------------------------------------------------------- */
/* ACTIONS                                                                       */
/* ---------------------------------------------------------------------------- */

/* Summary: the identifier that follows at text[i] after white space and an optional parenthesis; its end is returned in *end. */
static boolean l_flex_next_word(const char *text, uint_large length, uint_large i, boolean allow_paren, uint_large *start, uint_large *end) {
  while (i < length && (text[i] == ' ' || text[i] == '\t' || text[i] == '\n' || text[i] == '\r' || (allow_paren && text[i] == '('))) {
    i++;
  }

  if (i >= length || !l_flex_word_start(text[i])) {
    return FALSE;
  }

  *start = i;

  while (i < length && l_flex_word_part(text[i])) {
    i++;
  }

  *end = i;

  return TRUE;
}

static void l_flex_action_analyse(LFlex *flex, const char *text, LFlexAction *out) {
  uint_large length;
  uint_large i;

  nexus_memory_bytes_clear(out, NEXUS_SIZEOF(*out));
  out->action = LIA_NONE;
  length      = nexus_strings_string_length_get(text);
  i           = 0;

  while (i < length) {
    uint_large start;
    uint_large end;

    if (text[i] == '"' || text[i] == '\'' || (text[i] == '/' && i + 1 < length && (text[i + 1] == '*' || text[i + 1] == '/'))) {
      i = l_flex_c_skip(text, length, i);
      continue;
    }

    if (!l_flex_word_start(text[i])) {
      i++;
      continue;
    }

    start = i;

    while (i < length && l_flex_word_part(text[i])) {
      i++;
    }

    end = i;

    if (i - start == 6 && nexus_memory_bytes_equal(text + start, "return", 6)) {
      uint_large word_start;
      uint_large word_end;
      uint_large k;

      k = end;

      while (k < length && (text[k] == ' ' || text[k] == '\t' || text[k] == '(')) {
        k++;
      }

      if (k < length && text[k] == '\'') {
        out->character = TRUE;
      } else if (l_flex_next_word(text, length, end, TRUE, &word_start, &word_end)) {
        uint_large after;

        after = word_end;

        while (after < length && (text[after] == ' ' || text[after] == '\t')) {
          after++;
        }

        if (word_end - word_start == 6 && nexus_memory_bytes_equal(text + word_start, "yytext", 6)) {
          out->character = TRUE;
        } else if (after < length && (text[after] == '(' || text[after] == '[' || text[after] == '.' || text[after] == '-')) {
          out->computed = TRUE;
        } else if (out->token == NULL) {
          out->token = l_flex_copy(flex, text + word_start, word_end - word_start);
        } else if (!(nexus_strings_string_length_get(out->token) == word_end - word_start &&
                     nexus_memory_bytes_equal(out->token, text + word_start, word_end - word_start))) {
          out->several = TRUE;
        }
      } else {
        out->computed = TRUE;
      }

      continue;
    }

    if ((end - start == 2 && nexus_memory_bytes_equal(text + start, "if", 2)) ||
        (end - start == 4 && nexus_memory_bytes_equal(text + start, "else", 4)) ||
        (end - start == 5 && nexus_memory_bytes_equal(text + start, "while", 5)) ||
        (end - start == 3 && nexus_memory_bytes_equal(text + start, "for", 3)) ||
        (end - start == 6 && nexus_memory_bytes_equal(text + start, "switch", 6))) {
      out->conditional = TRUE;
      continue;
    }

    if ((end - start == 5 && nexus_memory_bytes_equal(text + start, "BEGIN", 5)) ||
        (end - start == 13 && nexus_memory_bytes_equal(text + start, "yy_push_state", 13))) {
      uint_large word_start;
      uint_large word_end;

      if (l_flex_next_word(text, length, end, TRUE, &word_start, &word_end) && !out->state_given) {
        out->state_given = TRUE;
        out->begin       = text[start] == 'B';
        out->action      = out->begin ? LIA_REPLACE : LIA_PUSH;
        out->target      = l_flex_copy(flex, text + word_start, word_end - word_start);
      }

      continue;
    }

    if (end - start == 12 && nexus_memory_bytes_equal(text + start, "yy_pop_state", 12) && !out->state_given) {
      out->state_given = TRUE;
      out->action      = LIA_POP;
      continue;
    }

    if ((end - start == 6 && nexus_memory_bytes_equal(text + start, "yymore", 6)) ||
        (end - start == 6 && nexus_memory_bytes_equal(text + start, "yyless", 6)) ||
        (end - start == 6 && nexus_memory_bytes_equal(text + start, "REJECT", 6)) ||
        (end - start == 5 && nexus_memory_bytes_equal(text + start, "unput", 5)) ||
        (end - start == 5 && nexus_memory_bytes_equal(text + start, "input", 5))) {
      out->unsupported = l_flex_copy(flex, text + start, end - start);
    }
  }
}

/* ---------------------------------------------------------------------------- */
/* TOKENS                                                                        */
/* ---------------------------------------------------------------------------- */

static LFlexPiece *l_flex_piece_get(LFlex *flex, const char *token, const char *mode, LIAction action, const char *target, uint32 rule_index) {
  uint_large  i;
  LFlexPiece *piece;

  for (i = 0; i < nexus_data_list_length_get(flex->pieces); i++) {
    piece = (LFlexPiece *)nexus_data_list_pointer_at(flex->pieces, i);

    if (piece->action == action && ((piece->token == NULL) == (token == NULL)) &&
        (token == NULL || nexus_strings_string_equals(piece->token, token)) && ((piece->mode == NULL) == (mode == NULL)) &&
        (mode == NULL || nexus_strings_string_equals(piece->mode, mode)) && ((piece->target == NULL) == (target == NULL)) &&
        (target == NULL || nexus_strings_string_equals(piece->target, target))) {
      return piece;
    }
  }

  piece             = (LFlexPiece *)nexus_data_arena_allocate(&flex->grammar->arena, NEXUS_SIZEOF(*piece));
  piece->token      = token;
  piece->mode       = mode;
  piece->action     = action;
  piece->target     = target;
  piece->patterns   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  piece->first_rule = rule_index;
  (void)nexus_data_list_append_pointer(flex->pieces, piece);

  return piece;
}

/* Summary: the Genesis mode name of a flex state name (NULL for INITIAL). */
static const char *l_flex_mode_of(LFlex *flex, const char *name) {
  const LFlexState *state;

  if (nexus_strings_string_equals(name, "INITIAL")) {
    return NULL;
  }

  state = l_flex_state_find(flex, name, nexus_strings_string_length_get(name));

  if (state == NULL) {
    logos_languages_interchange_grammar_note(flex->grammar, LIN_ERROR, "the start condition `%s` is not declared with %%s or %%x", name);
    flex->ok = FALSE;

    return NULL;
  }

  return state->mode;
}

static void l_flex_rule_convert(LFlex *flex, const LFlexRule *rule, uint32 rule_index) {
  LFlexAction analysis;
  LINode     *pattern;
  const char *modes[128];
  uint32      mode_count;
  uint32      i;
  uint_large  k;
  const char *token;
  const char *target;
  const char *target_mode;
  LIAction    action;
  boolean     skip;

  l_flex_action_analyse(flex, rule->action, &analysis);

  if (analysis.unsupported != NULL) {
    flex->unsupported++;
    logos_languages_interchange_grammar_note(flex->grammar, LIN_WARNING,
                                             "the action `%s` is not carried over (line %u); the pattern is matched without it", analysis.unsupported,
                                             (unsigned)rule->line);
  }

  if (analysis.state_given && analysis.conditional) {
    flex->conditional_states++;
  }

  if (analysis.several) {
    logos_languages_interchange_grammar_note(flex->grammar, LIN_WARNING, "the action of line %u returns several tokens; the first, `%s`, is used",
                                             (unsigned)rule->line, analysis.token);
  }

  if (analysis.token == NULL && analysis.character && !analysis.state_given) {
    flex->characters++;

    return;
  }

  if (analysis.token == NULL && analysis.computed) {
    flex->computed++;
    logos_languages_interchange_grammar_note(flex->grammar, LIN_WARNING,
                                             "line %u returns a token that its action computes; the pattern `%s` is left out", (unsigned)rule->line,
                                             rule->pattern);

    return;
  }

  pattern = l_flex_pattern(flex, rule->pattern, nexus_strings_string_length_get(rule->pattern));

  if (pattern == NULL) {
    return;
  }

  /* The modes the rule is active in. */
  mode_count = 0;

  if (rule->conditions == NULL) {
    modes[mode_count++] = NULL;

    for (k = 0; k < nexus_data_list_length_get(flex->states) && mode_count < 128; k++) {
      const LFlexState *state;

      state = (const LFlexState *)nexus_data_list_at_const(flex->states, k);

      if (!state->exclusive) {
        modes[mode_count++] = state->mode;
      }
    }
  } else {
    for (i = 0; i < rule->condition_count && mode_count < 128; i++) {
      if (nexus_strings_string_equals(rule->conditions[i], "*")) {
        modes[mode_count++] = NULL;

        for (k = 0; k < nexus_data_list_length_get(flex->states) && mode_count < 128; k++) {
          modes[mode_count++] = ((const LFlexState *)nexus_data_list_at_const(flex->states, k))->mode;
        }
      } else {
        modes[mode_count++] = l_flex_mode_of(flex, rule->conditions[i]);
      }
    }
  }

  token  = analysis.token;
  action = analysis.action;
  target = NULL;
  skip   = token == NULL;

  /* The state a push or BEGIN names (NULL: INITIAL, the root context). */
  target_mode = NULL;

  if (action == LIA_PUSH || action == LIA_REPLACE) {
    target_mode = l_flex_mode_of(flex, analysis.target);
  }

  for (i = 0; i < mode_count; i++) {
    LFlexPiece *piece;

    action = analysis.action;
    target = NULL;

    if (analysis.begin) {
      /* flex has one current state; the lexer has a stack. The root is the bottom, so a switch from INITIAL pushes, one back to it pops. */
      if (modes[i] == target_mode || (modes[i] != NULL && target_mode != NULL && nexus_strings_string_equals(modes[i], target_mode))) {
        action = LIA_NONE;
      } else if (modes[i] == NULL) {
        action = LIA_PUSH;
        target = target_mode;
      } else if (target_mode == NULL) {
        action = LIA_POP;
      } else {
        action = LIA_REPLACE;
        target = target_mode;
      }
    } else if (action == LIA_PUSH) {
      target = target_mode != NULL ? target_mode : "ROOT";
    }

    piece = l_flex_piece_get(flex, token, modes[i], action, target, rule_index);
    (void)nexus_data_list_append_pointer(piece->patterns, pattern);
  }

  (void)skip;
}

/* ---------------------------------------------------------------------------- */
/* PATTERNS THAT CAN MATCH NOTHING                                               */
/* ---------------------------------------------------------------------------- */

/*
A flex pattern such as `[^*\n]*` may match the empty string; flex only takes such a match when nothing longer matches, so it never makes a
token. A Genesis token cannot be empty, so the pattern is replaced by the one that matches what it matches except the empty string.
*/
static boolean l_flex_nullable(const LFlex *flex, const LINode *node, uint32 depth) {
  uint32 i;

  if (node == NULL || depth > 24) {
    return FALSE;
  }

  switch (node->kind) {
  case LIK_EMPTY:
  case LIK_STAR:
  case LIK_OPT:
    return TRUE;
  case LIK_LITERAL:
    return node->length == 0;
  case LIK_CHARSET:
    return FALSE;
  case LIK_PLUS:
    return l_flex_nullable(flex, node->child, depth + 1);
  case LIK_SEQ:
    for (i = 0; i < node->count; i++) {
      if (!l_flex_nullable(flex, node->items[i], depth + 1)) {
        return FALSE;
      }
    }

    return TRUE;
  case LIK_ALT:
    for (i = 0; i < node->count; i++) {
      if (l_flex_nullable(flex, node->items[i], depth + 1)) {
        return TRUE;
      }
    }

    return FALSE;
  case LIK_REF:
    for (i = 0; i < nexus_data_list_length_get(flex->grammar->definitions); i++) {
      const LIDefinition *definition;

      definition = (const LIDefinition *)nexus_data_list_pointer_at(flex->grammar->definitions, i);

      if (nexus_strings_string_equals(definition->name, node->text)) {
        return l_flex_nullable(flex, definition->expression, depth + 1);
      }
    }

    return FALSE;
  default:
    return FALSE;
  }
}

/* Summary: the node that matches what node matches except the empty string; NULL when it matches nothing else. */
static LINode *l_flex_nonempty(LFlex *flex, LINode *node) {
  uint32   i;
  uint32   used;
  LINode **alternatives;
  LINode  *result;

  if (!l_flex_nullable(flex, node, 0)) {
    return node;
  }

  switch (node->kind) {
  case LIK_STAR:
    return logos_languages_interchange_node_wrap(flex->grammar, LIK_PLUS, node->child);
  case LIK_OPT:
  case LIK_PLUS:
    return l_flex_nonempty(flex, node->child);
  case LIK_ALT:
    alternatives = (LINode **)nexus_memory_allocate(((uint_large)node->count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
    used         = 0;

    for (i = 0; i < node->count; i++) {
      LINode *branch;

      branch = l_flex_nonempty(flex, node->items[i]);

      if (branch != NULL) {
        alternatives[used++] = branch;
      }
    }

    result = used == 0 ? NULL : logos_languages_interchange_node_group(flex->grammar, LIK_ALT, alternatives, used);
    nexus_memory_deallocate((void *)alternatives);

    return result;
  case LIK_SEQ:
    /* The first element that matches something is at position k, the ones before it matched nothing. */
    alternatives = (LINode **)nexus_memory_allocate(((uint_large)node->count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
    used         = 0;

    for (i = 0; i < node->count; i++) {
      LINode  *first;
      LINode **parts;
      uint32   rest;

      first = l_flex_nonempty(flex, node->items[i]);

      if (first != NULL) {
        rest     = node->count - i;
        parts    = (LINode **)nexus_memory_allocate(((uint_large)rest + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
        parts[0] = first;
        nexus_memory_bytes_copy((const void *)(node->items + i + 1), (void *)(parts + 1),
                                ((uint_large)rest - 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
        alternatives[used++] = logos_languages_interchange_node_group(flex->grammar, LIK_SEQ, parts, rest);
        nexus_memory_deallocate((void *)parts);
      }

      if (!l_flex_nullable(flex, node->items[i], 0)) {
        break;
      }
    }

    result = used == 0 ? NULL : logos_languages_interchange_node_group(flex->grammar, LIK_ALT, alternatives, used);
    nexus_memory_deallocate((void *)alternatives);

    return result;
  case LIK_REF:
  case LIK_LITERAL:
  case LIK_EMPTY:
  case LIK_CHARSET:
  default:
    /* A reference to a fragment that can match nothing is left to the emitter, which says so. */
    return node->kind == LIK_EMPTY ? NULL : node;
  }
}

static void l_flex_pieces_define(LFlex *flex, NexusDataList *named_tokens) {
  uint_large i;
  uint_large j;
  uint32     skips;

  skips = 0;

  for (i = 0; i < nexus_data_list_length_get(flex->pieces); i++) {
    LFlexPiece   *piece;
    LIDefinition *definition;
    LINode       *expression;
    char          name[LIFLEX_NAME_MAX + 16];
    uint_large    siblings;
    boolean       first_of_token;

    piece = (LFlexPiece *)nexus_data_list_pointer_at(flex->pieces, i);

    {
      LINode   **items;
      uint_large count;

      count = nexus_data_list_length_get(piece->patterns);
      items = (LINode **)nexus_memory_allocate((count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

      for (j = 0; j < count; j++) {
        items[j] = (LINode *)nexus_data_list_pointer_at(piece->patterns, j);
      }

      expression = logos_languages_interchange_node_group(flex->grammar, LIK_ALT, items, (uint32)count);
      nexus_memory_deallocate((void *)items);
      expression = l_flex_nonempty(flex, expression);

      if (expression == NULL) {
        logos_languages_interchange_grammar_note(flex->grammar, LIN_WARNING,
                                                 "the patterns of the token `%s` match only the empty string and are left out",
                                                 piece->token != NULL ? piece->token : "(skipped)");
        continue;
      }
    }

    siblings       = 0;
    first_of_token = TRUE;

    if (piece->token != NULL) {
      for (j = 0; j < nexus_data_list_length_get(flex->pieces); j++) {
        const LFlexPiece *other;

        other = (const LFlexPiece *)nexus_data_list_pointer_at(flex->pieces, j);

        if (other->token != NULL && nexus_strings_string_equals(other->token, piece->token)) {
          siblings++;

          if (j < i) {
            first_of_token = FALSE;
          }
        }
      }
    }

    if (piece->token == NULL) {
      skips++;
      (void)stbsp_snprintf(name, (int)sizeof(name), "skip_%u", (unsigned)skips);
    } else if (siblings == 1) {
      (void)stbsp_snprintf(name, (int)sizeof(name), "%s", piece->token);
    } else {
      (void)stbsp_snprintf(name, (int)sizeof(name), "%s_piece_%u", piece->token, (unsigned)(i + 1));
    }

    definition                = logos_languages_interchange_grammar_define(flex->grammar, name, LIC_TOKEN, expression);
    definition->mode          = piece->mode;
    definition->action        = piece->action;
    definition->action_target = piece->target;
    definition->skip          = piece->token == NULL;
    definition->priority      = (int64)100000 - (int64)piece->first_rule;

    if (piece->token != NULL && first_of_token) {
      (void)nexus_data_list_append(named_tokens, (const void *)&piece->token);

      /* A token returned from several modes or with several actions is a rule over its pieces. */
      if (siblings > 1) {
        LINode   **refs;
        uint_large used;

        refs = (LINode **)nexus_memory_allocate((siblings + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
        used = 0;

        for (j = 0; j < nexus_data_list_length_get(flex->pieces); j++) {
          const LFlexPiece *other;

          other = (const LFlexPiece *)nexus_data_list_pointer_at(flex->pieces, j);

          if (other->token != NULL && nexus_strings_string_equals(other->token, piece->token)) {
            char piece_name[LIFLEX_NAME_MAX + 16];

            (void)stbsp_snprintf(piece_name, (int)sizeof(piece_name), "%s_piece_%u", other->token, (unsigned)(j + 1));
            refs[used++] = logos_languages_interchange_node_reference(flex->grammar, piece_name);
          }
        }

        (void)logos_languages_interchange_grammar_define(flex->grammar, piece->token, LIC_RULE,
                                                         logos_languages_interchange_node_group(flex->grammar, LIK_ALT, refs, (uint32)used));
        nexus_memory_deallocate((void *)refs);
      }
    }
  }
}

/* ---------------------------------------------------------------------------- */
/* ENTRY                                                                         */
/* ---------------------------------------------------------------------------- */

boolean logos_languages_interchange_flex_read(LGenesisCompiler *compiler, LIGrammar *grammar, const LInterchangeInput *input,
                                              NexusDataList *named_tokens) {
  LFlex       flex;
  LFlexLines  lines;
  const char *line;
  uint_large  length;
  uint32      line_number;
  uint_large  i;

  flex.compiler           = compiler;
  flex.grammar            = grammar;
  flex.ok                 = TRUE;
  flex.insensitive        = FALSE;
  flex.dot_all            = FALSE;
  flex.states             = nexus_data_list_create(NEXUS_SIZEOF(LFlexState), 8);
  flex.definitions        = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  flex.rules              = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  flex.pieces             = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  flex.anchors            = 0;
  flex.trailing           = 0;
  flex.end_rules          = 0;
  flex.unsupported        = 0;
  flex.computed           = 0;
  flex.characters         = 0;
  flex.high_bytes         = 0;
  flex.conditional_states = 0;
  flex.line_text          = "";
  flex.regex              = logos_languages_interchange_reader_open(compiler, grammar, "FlexRegex");
  lines.text              = (const char *)input->text;
  lines.size              = input->size;
  lines.position          = 0;
  line_number             = 0;

  if (flex.regex == NULL) {
    nexus_data_list_destroy(flex.states);
    nexus_data_list_destroy(flex.definitions);
    nexus_data_list_destroy(flex.rules);
    nexus_data_list_destroy(flex.pieces);

    return FALSE;
  }

  /* The definitions section: start conditions, options and named patterns, until the first %%. */
  while (l_flex_line_next(&lines, &line, &length)) {
    line_number++;

    if (length == 0 || l_flex_blank(line[0])) {
      continue;
    }

    if (l_flex_starts(line, length, "%%")) {
      break;
    }

    if (l_flex_starts(line, length, "%{") || l_flex_starts(line, length, "%top{")) {
      const char *skipped;
      uint_large  skipped_length;

      while (l_flex_line_next(&lines, &skipped, &skipped_length)) {
        line_number++;

        if (l_flex_starts(skipped, skipped_length, "%}") || (skipped_length > 0 && skipped[0] == '}')) {
          break;
        }
      }
    } else if (l_flex_starts(line, length, "/*")) {
      uint_large start;

      start = 0;

      while (start + 1 < length && !(line[start] == '*' && line[start + 1] == '/')) {
        start++;
      }

      if (start + 1 >= length) {
        const char *skipped;
        uint_large  skipped_length;

        while (l_flex_line_next(&lines, &skipped, &skipped_length)) {
          line_number++;

          for (i = 0; i + 1 < skipped_length; i++) {
            if (skipped[i] == '*' && skipped[i + 1] == '/') {
              break;
            }
          }

          if (i + 1 < skipped_length) {
            break;
          }
        }
      }
    } else if (l_flex_starts(line, length, "%option")) {
      l_flex_option(&flex, line, length);
    } else if (l_flex_starts(line, length, "%s ") || l_flex_starts(line, length, "%s\t")) {
      l_flex_words(&flex, line, length, 2, FALSE);
    } else if (l_flex_starts(line, length, "%x ") || l_flex_starts(line, length, "%x\t")) {
      l_flex_words(&flex, line, length, 2, TRUE);
    } else if (l_flex_starts(line, length, "%start") || l_flex_starts(line, length, "%array") || l_flex_starts(line, length, "%pointer")) {
      if (l_flex_starts(line, length, "%start")) {
        l_flex_words(&flex, line, length, 6, FALSE);
      }
    } else if (l_flex_word_start(line[0])) {
      l_flex_definition(&flex, line, length);
    }
  }

  l_flex_rules_read(&flex, &lines, &line_number);

  for (i = 0; i < nexus_data_list_length_get(flex.rules) && flex.ok; i++) {
    l_flex_rule_convert(&flex, (const LFlexRule *)nexus_data_list_pointer_at(flex.rules, i), (uint32)i);
  }

  if (flex.ok) {
    l_flex_pieces_define(&flex, named_tokens);
  }

  if (flex.anchors > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_WARNING, "%u line-start or line-end anchors (^ and $) are dropped, so the scanner matches in more places than flex does",
        (unsigned)flex.anchors);
  }

  if (flex.trailing > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_WARNING, "%u trailing contexts (r/s) are dropped: the scanner matches r wherever s follows or not", (unsigned)flex.trailing);
  }

  if (flex.conditional_states > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_WARNING,
        "%u actions change the start condition only under a condition (a nesting depth, for example); the scanner here "
        "always changes it, so what depends on the counter is not carried over",
        (unsigned)flex.conditional_states);
  }

  if (flex.high_bytes > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_NOTE, "%u byte ranges 0x80-0xFF are taken as every character beyond ASCII, as for text in UTF-8", (unsigned)flex.high_bytes);
  }

  if (flex.end_rules > 0) {
    logos_languages_interchange_grammar_note(grammar, LIN_NOTE, "%u <<EOF>> rules are dropped", (unsigned)flex.end_rules);
  }

  if (flex.characters > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_NOTE, "%u rules return single characters; the characters the parser grammar names as literals are tokens of their own",
        (unsigned)flex.characters);
  }

  for (i = 0; i < nexus_data_list_length_get(flex.pieces); i++) {
    nexus_data_list_destroy(((LFlexPiece *)nexus_data_list_pointer_at(flex.pieces, i))->patterns);
  }

  logos_languages_genesis_engine_release(flex.regex);
  nexus_data_list_destroy(flex.states);
  nexus_data_list_destroy(flex.definitions);
  nexus_data_list_destroy(flex.rules);
  nexus_data_list_destroy(flex.pieces);

  return flex.ok;
}
