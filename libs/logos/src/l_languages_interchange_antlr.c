#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
ANTLR 4 grammars (.g4: lexer, parser, combined; one or several files) to the neutral description.

What carries over: lexer rules (tokens, fragments, modes, skip, channel as trivia, pushMode, popMode, mode), parser rules with
alternatives, blocks and the suffixes, character sets and ranges and negation, case-insensitivity options, non-greedy `.*?` in front of
literal text, literals that name a lexer rule. What is dropped with a note: actions, labels, rule arguments and returns, element
options such as <assoc=right>, token channels and types. What is approximated with a warning: semantic predicates (ignored, so
more is accepted), Unicode property classes (a table of common ranges), `more`. What is rejected: `import` of other grammars (pass
them as further files), a wildcard in a parser rule.

ANTLR resolves ambiguity by the order of alternatives and by precedence in left-recursive rules; the Earley engine keeps every derivation,
which accepts the same sentences.
*/

#define LIANTLR_RANGES_MAX 2048

typedef struct LAntlrLiteral {
  const char *text;
  uint_large  length;
  const char *token;
  boolean     insensitive;
} LAntlrLiteral;

typedef struct LAntlr {
  LGenesisCompiler *compiler;
  LIGrammar        *grammar;
  LIParsed          parsed;
  const char       *mode;
  boolean           insensitive;
  boolean           ok;
  NexusDataList    *literals; /* LAntlrLiteral: lexer rules that are exactly one literal */
  uint32            actions;
  uint32            predicates;
  uint32            arguments;
  uint32            element_options;
  uint32            labels;
  uint32            types;
  uint32            greedy;
  boolean           properties;
  boolean           parser_rules;
  const char       *rule; /* the lexer rule being read, for messages */
} LAntlr;

/* ---------------------------------------------------------------------------- */
/* TEXT AND SETS                                                                 */
/* ---------------------------------------------------------------------------- */

static uint32 l_antlr_hex(char letter) {
  if (letter >= '0' && letter <= '9') {
    return (uint32)(letter - '0');
  }

  if (letter >= 'a' && letter <= 'f') {
    return (uint32)(letter - 'a' + 10);
  }

  return (uint32)(letter - 'A' + 10);
}

static uint32 l_antlr_decode(const char *text, uint_large length, uint_large *position) {
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

static uint_large l_antlr_encode(uint32 value, char *out) {
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

/*
Summary: reads one character of an ANTLR literal or set: a backslash escape (\n, \uXXXX, \u{X}, \-, ...) or a plain character.
Returns: the code point; *kind is 0 for a character, 1 for a property class \p{..}, with the name in property.
*/
static uint32 l_antlr_character(const char *text, uint_large length, uint_large *position, uint32 *kind, char *property, uint_large property_size) {
  uint32 value;

  *kind = 0;

  if (text[*position] != '\\' || *position + 1 >= length) {
    return l_antlr_decode(text, length, position);
  }

  (*position)++;

  switch (text[*position]) {
  case 'n':
    (*position)++;

    return '\n';
  case 'r':
    (*position)++;

    return '\r';
  case 't':
    (*position)++;

    return '\t';
  case 'b':
    (*position)++;

    return '\b';
  case 'f':
    (*position)++;

    return '\f';
  case 'u':
    (*position)++;
    value = 0;

    if (text[*position] == '{') {
      (*position)++;

      while (*position < length && text[*position] != '}') {
        value = (value << 4) | l_antlr_hex(text[*position]);
        (*position)++;
      }

      (*position)++;
    } else {
      uint32 i;

      for (i = 0; i < 4 && *position < length; i++) {
        value = (value << 4) | l_antlr_hex(text[*position]);
        (*position)++;
      }
    }

    return value;
  case 'p':
  case 'P': {
    uint_large out;

    *kind = text[*position] == 'p' ? 1 : 2;
    (*position)++;
    out = 0;

    if (text[*position] == '{') {
      (*position)++;

      while (*position < length && text[*position] != '}' && out + 1 < property_size) {
        property[out++] = text[*position];
        (*position)++;
      }

      (*position)++;
    }

    property[out] = '\0';

    return 0;
  }
  default:
    return l_antlr_decode(text, length, position);
  }
}

/* The ranges of the common Unicode properties; a table, so an approximation of the real ones. */
static const uint32 l_antlr_letter_ranges[] = {
    0x41,   0x5A,   0x61,   0x7A,   0xAA,   0xAA,   0xB5,   0xB5,   0xBA,   0xBA,   0xC0,   0xD6,   0xD8,   0xF6,    0xF8,   0x2C1,  0x370,
    0x373,  0x376,  0x377,  0x37B,  0x37D,  0x386,  0x386,  0x388,  0x3FF,  0x400,  0x481,  0x48A,  0x52F,  0x531,   0x556,  0x561,  0x587,
    0x5D0,  0x5EA,  0x620,  0x64A,  0x671,  0x6D3,  0x904,  0x939,  0xE01,  0xE30,  0x10A0, 0x10FF, 0x1E00, 0x1FFF,  0x3041, 0x3096, 0x30A1,
    0x30FA, 0x3400, 0x4DBF, 0x4E00, 0x9FFF, 0xAC00, 0xD7A3, 0xF900, 0xFAFF, 0xFF21, 0xFF3A, 0xFF41, 0xFF5A, 0x20000, 0x2FA1F};
static const uint32 l_antlr_upper_ranges[] = {0x41, 0x5A, 0xC0, 0xD6, 0xD8, 0xDE, 0x391, 0x3A9, 0x410, 0x42F};
static const uint32 l_antlr_lower_ranges[] = {0x61, 0x7A, 0xDF, 0xF6, 0xF8, 0xFF, 0x3B1, 0x3C9, 0x430, 0x44F};
static const uint32 l_antlr_digit_ranges[] = {0x30, 0x39, 0x660, 0x669, 0x6F0, 0x6F9, 0x966, 0x96F, 0xFF10, 0xFF19};
static const uint32 l_antlr_space_ranges[] = {0x09,   0x0D,   0x20,   0x20,   0x85,   0x85,   0xA0,   0xA0,   0x2000,
                                              0x200A, 0x2028, 0x2029, 0x202F, 0x202F, 0x205F, 0x205F, 0x3000, 0x3000};

/* Summary: appends the ranges of a property; FALSE when it is not one of the table. */
static boolean l_antlr_property(const char *name, uint32 *ranges, uint_large *count, uint_large capacity) {
  const uint32 *table;
  uint_large    pairs;
  uint_large    i;

  table = NULL;

  if (nexus_strings_string_equals(name, "L") || nexus_strings_string_equals(name, "Letter") || nexus_strings_string_equals(name, "Alphabetic") ||
      nexus_strings_string_equals(name, "Alpha") || nexus_strings_string_equals(name, "IsAlphabetic")) {
    table = l_antlr_letter_ranges;
    pairs = (uint_large)(NEXUS_ARRAY_SIZE_ELEMENTS(l_antlr_letter_ranges) / 2);
  } else if (nexus_strings_string_equals(name, "Lu") || nexus_strings_string_equals(name, "Uppercase_Letter")) {
    table = l_antlr_upper_ranges;
    pairs = (uint_large)(NEXUS_ARRAY_SIZE_ELEMENTS(l_antlr_upper_ranges) / 2);
  } else if (nexus_strings_string_equals(name, "Ll") || nexus_strings_string_equals(name, "Lowercase_Letter")) {
    table = l_antlr_lower_ranges;
    pairs = (uint_large)(NEXUS_ARRAY_SIZE_ELEMENTS(l_antlr_lower_ranges) / 2);
  } else if (nexus_strings_string_equals(name, "N") || nexus_strings_string_equals(name, "Nd") || nexus_strings_string_equals(name, "Digit") ||
             nexus_strings_string_equals(name, "Number") || nexus_strings_string_equals(name, "Decimal_Number")) {
    table = l_antlr_digit_ranges;
    pairs = (uint_large)(NEXUS_ARRAY_SIZE_ELEMENTS(l_antlr_digit_ranges) / 2);
  } else if (nexus_strings_string_equals(name, "White_Space") || nexus_strings_string_equals(name, "Z") || nexus_strings_string_equals(name, "Zs") ||
             nexus_strings_string_equals(name, "Space_Separator")) {
    table = l_antlr_space_ranges;
    pairs = (uint_large)(NEXUS_ARRAY_SIZE_ELEMENTS(l_antlr_space_ranges) / 2);
  } else {
    return FALSE;
  }

  for (i = 0; i < pairs && *count < capacity; i++) {
    ranges[*count * 2]       = table[i * 2];
    ranges[(*count * 2) + 1] = table[(i * 2) + 1];
    (*count)++;
  }

  return TRUE;
}

/* Summary: adds a code point range, and for case-insensitive grammars the other case of ASCII letters in it. */
static void l_antlr_range_add(uint32 *ranges, uint_large *count, uint_large capacity, uint32 low, uint32 high, boolean insensitive) {
  if (*count >= capacity) {
    return;
  }

  ranges[*count * 2]       = low;
  ranges[(*count * 2) + 1] = high;
  (*count)++;

  if (insensitive) {
    uint32 start;
    uint32 end;

    start = low > 'a' ? low : 'a';
    end   = high < 'z' ? high : 'z';

    if (start <= end && *count < capacity) {
      ranges[*count * 2]       = start - 'a' + 'A';
      ranges[(*count * 2) + 1] = end - 'a' + 'A';
      (*count)++;
    }

    start = low > 'A' ? low : 'A';
    end   = high < 'Z' ? high : 'Z';

    if (start <= end && *count < capacity) {
      ranges[*count * 2]       = start - 'A' + 'a';
      ranges[(*count * 2) + 1] = end - 'A' + 'a';
      (*count)++;
    }
  }
}

/* Summary: the ranges of the text of a lexer character set, without its brackets. */
static void l_antlr_set_parse(LAntlr *antlr, const char *text, uint_large length, uint32 *ranges, uint_large *count, uint_large capacity) {
  uint_large position;

  position = 0;

  while (position < length) {
    uint32 kind;
    char   property[64];
    uint32 low;

    low = l_antlr_character(text, length, &position, &kind, property, sizeof(property));

    if (kind != 0) {
      antlr->properties = TRUE;

      if (kind == 1) {
        if (!l_antlr_property(property, ranges, count, capacity)) {
          logos_languages_interchange_grammar_note(antlr->grammar, LIN_ERROR, "the Unicode property \\p{%s} is not carried over", property);
          antlr->ok = FALSE;
        }
      } else {
        /* \P{..}: the complement of the property, over the code points. */
        uint32     inner[512];
        uint_large inner_count;
        uint32     next;
        uint_large i;
        uint_large k;

        inner_count = 0;

        if (!l_antlr_property(property, inner, &inner_count, 200)) {
          logos_languages_interchange_grammar_note(antlr->grammar, LIN_ERROR, "the Unicode property \\P{%s} is not carried over", property);
          antlr->ok = FALSE;
        } else {
          /* Insertion sort by start, then the gaps. */
          for (i = 1; i < inner_count; i++) {
            uint32 start;
            uint32 end;

            start = inner[i * 2];
            end   = inner[(i * 2) + 1];

            for (k = i; k > 0 && inner[(k - 1) * 2] > start; k--) {
              inner[k * 2]       = inner[(k - 1) * 2];
              inner[(k * 2) + 1] = inner[((k - 1) * 2) + 1];
            }

            inner[k * 2]       = start;
            inner[(k * 2) + 1] = end;
          }

          next = 0;

          for (i = 0; i < inner_count; i++) {
            if (inner[i * 2] > next) {
              l_antlr_range_add(ranges, count, capacity, next, inner[i * 2] - 1, FALSE);
            }

            if (inner[(i * 2) + 1] + 1 > next) {
              next = inner[(i * 2) + 1] + 1;
            }
          }

          l_antlr_range_add(ranges, count, capacity, next, 0x10FFFF, FALSE);
        }
      }

      continue;
    }

    if (position < length && text[position] == '-' && position + 1 < length) {
      uint_large after;
      uint32     high;
      uint32     high_kind;

      after = position + 1;
      high  = l_antlr_character(text, length, &after, &high_kind, property, sizeof(property));

      if (high_kind == 0) {
        position = after;
        l_antlr_range_add(ranges, count, capacity, low, high, antlr->insensitive);

        continue;
      }
    }

    l_antlr_range_add(ranges, count, capacity, low, low, antlr->insensitive);
  }
}

/* ---------------------------------------------------------------------------- */
/* "UP TO THE FIRST OCCURRENCE": .*? LITERAL                                      */
/* ---------------------------------------------------------------------------- */

#define LIANTLR_UNTIL_MAX 6

/*
Summary: the language of `.*? T` as an expression: the strings that end with the first occurrence of T. T is run through the
  matching automaton of its characters, and the automaton is turned into an expression by eliminating its states.
*/
static LINode *l_antlr_until(LIGrammar *grammar, const uint32 *text, uint32 length) {
  LINode *edges[LIANTLR_UNTIL_MAX + 1][LIANTLR_UNTIL_MAX + 1];
  uint32  alphabet[LIANTLR_UNTIL_MAX];
  uint32  alphabet_count;
  uint32  state;
  uint32  i;
  uint32  j;
  uint32  k;

  alphabet_count = 0;

  for (i = 0; i < length; i++) {
    boolean seen;

    seen = FALSE;

    for (j = 0; j < alphabet_count; j++) {
      seen = seen || alphabet[j] == text[i];
    }

    if (!seen) {
      alphabet[alphabet_count++] = text[i];
    }
  }

  for (i = 0; i <= length; i++) {
    for (j = 0; j <= length; j++) {
      edges[i][j] = NULL;
    }
  }

  /* The edges of the automaton: from `state` characters of T matched, on each character of the alphabet and on any other. */
  for (state = 0; state < length; state++) {
    for (i = 0; i <= alphabet_count; i++) {
      uint32  next;
      uint32  character;
      LINode *label;

      if (i == alphabet_count) {
        /* Any other character falls back to the start. */
        next = 0;
        {
          uint32     pairs[LIANTLR_UNTIL_MAX * 2];
          uint_large at;

          for (j = 0; j < alphabet_count; j++) {
            at            = (uint_large)j * 2;
            pairs[at]     = alphabet[j];
            pairs[at + 1] = alphabet[j];
          }

          label = logos_languages_interchange_node_charset(grammar, pairs, alphabet_count, TRUE);
        }
      } else {
        uint32 pair[2];
        uint32 candidate;

        character = alphabet[i];
        pair[0]   = character;
        pair[1]   = character;
        label     = logos_languages_interchange_node_charset(grammar, pair, 1, FALSE);

        /* The longest prefix of T that ends the text matched so far plus this character. */
        next = 0;

        for (candidate = state + 1; candidate > 0; candidate--) {
          boolean match;

          match = text[candidate - 1] == character;

          for (k = 0; match && k + 1 < candidate; k++) {
            match = text[k] == text[state + 1 - candidate + k];
          }

          if (match) {
            next = candidate;
            break;
          }
        }
      }

      if (edges[state][next] == NULL) {
        edges[state][next] = label;
      } else {
        LINode *both[2];

        both[0]            = edges[state][next];
        both[1]            = label;
        edges[state][next] = logos_languages_interchange_node_group(grammar, LIK_ALT, both, 2);
      }
    }
  }

  /* Eliminate the states between the start (0) and the end (length). */
  for (state = 1; state < length; state++) {
    LINode *loop;

    loop = edges[state][state];

    for (i = 0; i <= length; i++) {
      if (i == state || edges[i][state] == NULL) {
        continue;
      }

      for (j = 0; j <= length; j++) {
        LINode *path[3];
        LINode *through;
        uint32  used;

        if (j == state || edges[state][j] == NULL) {
          continue;
        }

        used         = 0;
        path[used++] = edges[i][state];

        if (loop != NULL) {
          path[used++] = logos_languages_interchange_node_wrap(grammar, LIK_STAR, loop);
        }

        path[used++] = edges[state][j];
        through      = logos_languages_interchange_node_group(grammar, LIK_SEQ, path, used);

        if (edges[i][j] == NULL) {
          edges[i][j] = through;
        } else {
          LINode *both[2];

          both[0]     = edges[i][j];
          both[1]     = through;
          edges[i][j] = logos_languages_interchange_node_group(grammar, LIK_ALT, both, 2);
        }
      }
    }
  }

  {
    LINode *parts[2];
    uint32  used;

    used = 0;

    if (edges[0][0] != NULL) {
      parts[used++] = logos_languages_interchange_node_wrap(grammar, LIK_STAR, edges[0][0]);
    }

    parts[used++] = edges[0][length];

    return logos_languages_interchange_node_group(grammar, LIK_SEQ, parts, used);
  }
}

/* ---------------------------------------------------------------------------- */
/* EXPRESSIONS                                                                   */
/* ---------------------------------------------------------------------------- */

static const char *l_antlr_rule(LAntlr *antlr, const LParserGreenNode *node) {
  return logos_languages_interchange_rule_name(&antlr->parsed, node);
}

static boolean l_antlr_is(LAntlr *antlr, const LParserGreenNode *node, const char *name) {
  return nexus_strings_string_equals(l_antlr_rule(antlr, node), name);
}

static LINode *l_antlr_empty(LAntlr *antlr) {
  return logos_languages_interchange_node_new(antlr->grammar, LIK_EMPTY);
}

static LINode *l_antlr_alternatives(LAntlr *antlr, const LParserGreenNode *node, boolean lexer, LIDefinition *definition);

/* Summary: a string literal token's value (the text between the quotes, unescaped), as UTF-8. */
static char *l_antlr_literal_text(const char *token, uint_large *out_length) {
  uint_large length;
  uint_large position;
  uint_large out;
  char      *result;

  length   = nexus_strings_string_length_get(token);
  position = 1;
  out      = 0;
  result   = (char *)nexus_memory_allocate((length * 4) + 1);

  while (position + 1 < length) {
    uint32 kind;
    char   property[8];
    uint32 value;

    value = l_antlr_character(token, length - 1, &position, &kind, property, sizeof(property));
    out += l_antlr_encode(value, result + out);
  }

  result[out] = '\0';
  *out_length = out;

  return result;
}

/* Summary: whether a literal is read without regard to case: the grammar says so and the literal has letters. */
static boolean l_antlr_literal_insensitive(const LAntlr *antlr, const char *text, uint_large length) {
  uint_large i;

  for (i = 0; antlr->insensitive && i < length; i++) {
    if ((text[i] >= 'a' && text[i] <= 'z') || (text[i] >= 'A' && text[i] <= 'Z')) {
      return TRUE;
    }
  }

  return FALSE;
}

static const char *l_antlr_token_for_literal(const LAntlr *antlr, const char *text, uint_large length) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(antlr->literals); i++) {
    const LAntlrLiteral *entry;

    entry = (const LAntlrLiteral *)nexus_data_list_at_const(antlr->literals, i);

    if (entry->length != length) {
      continue;
    }

    if (nexus_memory_bytes_equal(entry->text, text, length)) {
      return entry->token;
    }

    if (entry->insensitive) {
      uint_large k;
      boolean    same;

      same = TRUE;

      for (k = 0; k < length && same; k++) {
        char left;
        char right;

        left  = entry->text[k];
        right = text[k];
        left  = (char)((left >= 'A' && left <= 'Z') ? left - 'A' + 'a' : left);
        right = (char)((right >= 'A' && right <= 'Z') ? right - 'A' + 'a' : right);
        same  = left == right;
      }

      if (same) {
        return entry->token;
      }
    }
  }

  return NULL;
}

/* Summary: the single code point of a one-character literal, or FALSE. */
static boolean l_antlr_single(const char *text, uint_large length, uint32 *out) {
  uint_large position;

  position = 0;
  *out     = l_antlr_decode(text, length, &position);

  return length > 0 && position == length;
}

static LINode *l_antlr_terminal(LAntlr *antlr, const LParserGreenNode *node, boolean lexer) {
  const LParserGreenNode **children;
  uint32                   count;
  LINode                  *result;
  char                    *text;
  const char              *kind;

  result   = NULL;
  children = logos_languages_interchange_children_get(&antlr->parsed, node, &count);

  if (count > 0) {
    kind = logos_languages_interchange_token_name(&antlr->parsed, children[0]);
    text = logos_languages_interchange_token_text(&antlr->parsed, children[0]);

    if (nexus_strings_string_equals(kind, "token_name")) {
      if (nexus_strings_string_equals(text, "EOF")) {
        result = l_antlr_empty(antlr);
      } else {
        result = logos_languages_interchange_node_reference(antlr->grammar, text);
      }
    } else {
      uint_large  length;
      char       *value;
      const char *token;

      value = l_antlr_literal_text(text, &length);
      token = lexer ? NULL : l_antlr_token_for_literal(antlr, value, length);

      if (token != NULL) {
        result = logos_languages_interchange_node_reference(antlr->grammar, token);
      } else {
        result = logos_languages_interchange_node_literal(antlr->grammar, value, length, l_antlr_literal_insensitive(antlr, value, length));
      }

      nexus_memory_deallocate(value);
    }

    nexus_memory_deallocate(text);
  }

  nexus_memory_deallocate((void *)children);

  return result != NULL ? result : l_antlr_empty(antlr);
}

/* Summary: the ranges of one set element: a literal character, a range, a [set], or a token that is a one-character literal. */
static void l_antlr_set_element(LAntlr *antlr, const LParserGreenNode *node, uint32 *ranges, uint_large *count, uint_large capacity) {
  const LParserGreenNode **children;
  uint32                   child_count;
  uint32                   i;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_TOKEN) {
    const char *kind;
    char       *text;

    kind = logos_languages_interchange_token_name(&antlr->parsed, node);

    /* The punctuation of ~('a' | 'b'). */
    if (nexus_strings_string_equals(kind, "lparen") || nexus_strings_string_equals(kind, "rparen") || nexus_strings_string_equals(kind, "pipe") ||
        nexus_strings_string_equals(kind, "not")) {
      return;
    }

    text = logos_languages_interchange_token_text(&antlr->parsed, node);

    if (nexus_strings_string_equals(kind, "char_set")) {
      l_antlr_set_parse(antlr, text + 1, nexus_strings_string_length_get(text) - 2, ranges, count, capacity);
    } else if (nexus_strings_string_equals(kind, "string_literal")) {
      uint_large length;
      char      *value;
      uint32     code;

      value = l_antlr_literal_text(text, &length);

      if (l_antlr_single(value, length, &code)) {
        l_antlr_range_add(ranges, count, capacity, code, code, antlr->insensitive);
      } else {
        logos_languages_interchange_grammar_note(antlr->grammar, LIN_ERROR, "a set element must be one character, not '%s'", value);
        antlr->ok = FALSE;
      }

      nexus_memory_deallocate(value);
    } else {
      logos_languages_interchange_grammar_note(antlr->grammar, LIN_ERROR, "the token `%s` in a set is not carried over; use characters", text);
      antlr->ok = FALSE;
    }

    nexus_memory_deallocate(text);

    return;
  }

  if (l_antlr_is(antlr, node, "character_range")) {
    uint_large length_low;
    uint_large length_high;
    char      *low_text;
    char      *high_text;
    char      *low_value;
    char      *high_value;
    uint32     low;
    uint32     high;

    children = logos_languages_interchange_children_get(&antlr->parsed, node, &child_count);

    if (child_count >= 3) {
      low_text   = logos_languages_interchange_token_text(&antlr->parsed, children[0]);
      high_text  = logos_languages_interchange_token_text(&antlr->parsed, children[2]);
      low_value  = l_antlr_literal_text(low_text, &length_low);
      high_value = l_antlr_literal_text(high_text, &length_high);

      if (l_antlr_single(low_value, length_low, &low) && l_antlr_single(high_value, length_high, &high)) {
        l_antlr_range_add(ranges, count, capacity, low, high, antlr->insensitive);
      } else {
        logos_languages_interchange_grammar_note(antlr->grammar, LIN_ERROR, "a character range needs single characters");
        antlr->ok = FALSE;
      }

      nexus_memory_deallocate(low_text);
      nexus_memory_deallocate(high_text);
      nexus_memory_deallocate(low_value);
      nexus_memory_deallocate(high_value);
    }

    nexus_memory_deallocate((void *)children);

    return;
  }

  children = logos_languages_interchange_children_get(&antlr->parsed, node, &child_count);

  for (i = 0; i < child_count; i++) {
    l_antlr_set_element(antlr, children[i], ranges, count, capacity);
  }

  nexus_memory_deallocate((void *)children);
}

static LINode *l_antlr_not_set(LAntlr *antlr, const LParserGreenNode *node) {
  uint32                   ranges[LIANTLR_RANGES_MAX];
  uint_large               count;
  const LParserGreenNode **children;
  uint32                   element_count;
  uint32                   i;

  count    = 0;
  children = logos_languages_interchange_children_get(&antlr->parsed, node, &element_count);

  for (i = 0; i < element_count; i++) {
    if (logos_language_tooling_parsing_syntax_green_node_kind_get(children[i]) == LPGK_TOKEN &&
        nexus_strings_string_equals(logos_languages_interchange_token_name(&antlr->parsed, children[i]), "not")) {
      continue;
    }

    l_antlr_set_element(antlr, children[i], ranges, &count, (LIANTLR_RANGES_MAX / 2) - 8);
  }

  nexus_memory_deallocate((void *)children);

  return logos_languages_interchange_node_charset(antlr->grammar, ranges, (uint32)count, TRUE);
}

typedef struct LAntlrElement {
  LINode *node;
  boolean greedy_dot; /* `.*?` or `.+?` */
  boolean plus;
} LAntlrElement;

static LINode *l_antlr_suffix(LAntlr *antlr, LINode *node, const char *kind) {
  if (nexus_strings_string_equals(kind, "question") || nexus_strings_string_equals(kind, "question_greedy")) {
    antlr->greedy += nexus_strings_string_equals(kind, "question_greedy") ? 1 : 0;

    return logos_languages_interchange_node_wrap(antlr->grammar, LIK_OPT, node);
  }

  if (nexus_strings_string_equals(kind, "star") || nexus_strings_string_equals(kind, "star_greedy")) {
    antlr->greedy += nexus_strings_string_equals(kind, "star_greedy") ? 1 : 0;

    return logos_languages_interchange_node_wrap(antlr->grammar, LIK_STAR, node);
  }

  antlr->greedy += nexus_strings_string_equals(kind, "plus_greedy") ? 1 : 0;

  return logos_languages_interchange_node_wrap(antlr->grammar, LIK_PLUS, node);
}

/* Summary: one lexer element and its suffix; the node is NULL for an action or predicate, which carries over to nothing. */
static void l_antlr_lexer_element(LAntlr *antlr, const LParserGreenNode *node, LAntlrElement *out) {
  const LParserGreenNode **children;
  uint32                   count;
  uint32                   i;
  const char              *suffix;
  char                    *suffix_text;
  LINode                  *atom;
  boolean                  dot;

  out->node       = NULL;
  out->greedy_dot = FALSE;
  out->plus       = FALSE;
  children        = logos_languages_interchange_children_get(&antlr->parsed, node, &count);
  suffix          = NULL;
  suffix_text     = NULL;
  atom            = NULL;
  dot             = FALSE;

  for (i = 0; i < count; i++) {
    const char *rule;

    rule = l_antlr_rule(antlr, children[i]);

    if (nexus_strings_string_equals(rule, "ebnf_suffix")) {
      const LParserGreenNode *suffix_children[2];

      if (logos_languages_interchange_children(&antlr->parsed, children[i], suffix_children, 2) > 0) {
        suffix_text = logos_languages_interchange_token_text(&antlr->parsed, suffix_children[0]);
        suffix      = logos_languages_interchange_token_name(&antlr->parsed, suffix_children[0]);
      }
    } else if (nexus_strings_string_equals(rule, "action_or_predicate")) {
      const LParserGreenNode **inner;
      uint32                   inner_count;
      boolean                  predicate;

      inner     = logos_languages_interchange_children_get(&antlr->parsed, children[i], &inner_count);
      predicate = inner_count > 1;
      nexus_memory_deallocate((void *)inner);

      if (predicate) {
        antlr->predicates++;
      } else {
        antlr->actions++;
      }

      atom = NULL;
    } else if (nexus_strings_string_equals(rule, "lexer_atom")) {
      const LParserGreenNode **inner;
      uint32                   inner_count;

      inner = logos_languages_interchange_children_get(&antlr->parsed, children[i], &inner_count);

      if (inner_count == 1 && logos_language_tooling_parsing_syntax_green_node_kind_get(inner[0]) == LPGK_TOKEN) {
        const char *kind;
        char       *text;

        kind = logos_languages_interchange_token_name(&antlr->parsed, inner[0]);
        text = logos_languages_interchange_token_text(&antlr->parsed, inner[0]);

        if (nexus_strings_string_equals(kind, "dot")) {
          atom = logos_languages_interchange_node_charset(antlr->grammar, NULL, 0, TRUE);
          dot  = TRUE;
        } else if (nexus_strings_string_equals(kind, "char_set")) {
          uint32     ranges[LIANTLR_RANGES_MAX];
          uint_large range_count;

          range_count = 0;
          l_antlr_set_parse(antlr, text + 1, nexus_strings_string_length_get(text) - 2, ranges, &range_count, (LIANTLR_RANGES_MAX / 2) - 8);
          atom = logos_languages_interchange_node_charset(antlr->grammar, ranges, (uint32)range_count, FALSE);
        }

        nexus_memory_deallocate(text);
      } else if (inner_count == 1) {
        const char *name;

        name = l_antlr_rule(antlr, inner[0]);

        if (nexus_strings_string_equals(name, "terminal")) {
          atom = l_antlr_terminal(antlr, inner[0], TRUE);
        } else if (nexus_strings_string_equals(name, "lexer_not_set")) {
          atom = l_antlr_not_set(antlr, inner[0]);
        } else if (nexus_strings_string_equals(name, "character_range")) {
          uint32     ranges[8];
          uint_large range_count;

          range_count = 0;
          l_antlr_set_element(antlr, inner[0], ranges, &range_count, 3);
          atom = logos_languages_interchange_node_charset(antlr->grammar, ranges, (uint32)range_count, FALSE);
        }
      }

      nexus_memory_deallocate((void *)inner);
    } else if (nexus_strings_string_equals(rule, "lexer_block")) {
      const LParserGreenNode **inner;
      uint32                   inner_count;
      uint32                   k;

      inner = logos_languages_interchange_children_get(&antlr->parsed, children[i], &inner_count);

      for (k = 0; k < inner_count; k++) {
        if (l_antlr_is(antlr, inner[k], "lexer_alternatives")) {
          atom = l_antlr_alternatives(antlr, inner[k], TRUE, NULL);
        }
      }

      nexus_memory_deallocate((void *)inner);
    }
  }

  if (atom == NULL) {
    out->node = NULL;
  } else if (suffix != NULL) {
    if (dot && (nexus_strings_string_equals(suffix, "star_greedy") || nexus_strings_string_equals(suffix, "plus_greedy"))) {
      out->node       = atom;
      out->greedy_dot = TRUE;
      out->plus       = nexus_strings_string_equals(suffix, "plus_greedy");
    } else {
      out->node = l_antlr_suffix(antlr, atom, suffix);
    }
  } else {
    out->node = atom;
  }

  if (suffix_text != NULL) {
    nexus_memory_deallocate(suffix_text);
  }

  nexus_memory_deallocate((void *)children);
}

/* Summary: the text of a node when it is a literal or a one-character set, as code points; FALSE otherwise. */
static boolean l_antlr_terminator(const LINode *node, uint32 *out, uint32 *length) {
  if (node == NULL) {
    return FALSE;
  }

  if (node->kind == LIK_LITERAL && node->length > 0) {
    uint_large position;
    uint_large k;

    /* Case does not matter for text without letters. */
    for (k = 0; k < node->length; k++) {
      if (node->insensitive && ((node->text[k] >= 'a' && node->text[k] <= 'z') || (node->text[k] >= 'A' && node->text[k] <= 'Z'))) {
        return FALSE;
      }
    }

    position = 0;

    while (position < node->length && *length < LIANTLR_UNTIL_MAX) {
      out[(*length)++] = l_antlr_decode(node->text, node->length, &position);
    }

    return TRUE;
  }

  /* (literal | EOF): the end of input cannot be said, so the literal alone ends it; an unterminated comment is not recognized. */
  if (node->kind == LIK_ALT) {
    uint32  i;
    LINode *only;

    only = NULL;

    for (i = 0; i < node->count; i++) {
      if (node->items[i]->kind == LIK_EMPTY) {
        continue;
      }

      if (only != NULL) {
        return FALSE;
      }

      only = node->items[i];
    }

    return only != NULL && l_antlr_terminator(only, out, length);
  }

  if (node->kind == LIK_CHARSET && !node->negated && node->range_count == 1 && node->ranges[0] == node->ranges[1] && *length < LIANTLR_UNTIL_MAX) {
    out[(*length)++] = node->ranges[0];

    return TRUE;
  }

  return FALSE;
}

/* Summary: a sequence of elements into one node, turning `.*? literal` into "up to the first literal". */
static LINode *l_antlr_sequence(LAntlr *antlr, LAntlrElement *elements, uint32 count) {
  LINode *items[1024];
  uint32  used;
  uint32  i;

  used = 0;

  for (i = 0; i < count && used < 1000; i++) {
    if (elements[i].node == NULL) {
      continue;
    }

    if (elements[i].greedy_dot) {
      uint32 text[LIANTLR_UNTIL_MAX];
      uint32 length;
      uint32 k;

      length = 0;
      k      = i + 1;

      while (k < count && elements[k].node == NULL) {
        k++;
      }

      while (k < count && !elements[k].greedy_dot && l_antlr_terminator(elements[k].node, text, &length)) {
        k++;

        while (k < count && elements[k].node == NULL) {
          k++;
        }
      }

      if (length == 0) {
        logos_languages_interchange_grammar_note(
            antlr->grammar, LIN_ERROR,
            "in the rule %s a non-greedy `.*?` must be followed by the literal text that ends it; it is not carried over otherwise", antlr->rule);
        antlr->ok     = FALSE;
        items[used++] = logos_languages_interchange_node_wrap(antlr->grammar, LIK_STAR, elements[i].node);
      } else {
        if (elements[i].plus) {
          items[used++] = elements[i].node;
        }

        items[used++] = l_antlr_until(antlr->grammar, text, length);
        i             = k - 1;
      }

      continue;
    }

    items[used++] = elements[i].node;
  }

  return used == 0 ? l_antlr_empty(antlr) : logos_languages_interchange_node_group(antlr->grammar, LIK_SEQ, items, used);
}

/* Summary: the lexer commands of an alternative into the definition's action (the first one that has any). */
static void l_antlr_commands(LAntlr *antlr, const LParserGreenNode *node, LIDefinition *definition) {
  const LParserGreenNode **children;
  uint32                   count;
  uint32                   i;

  children = logos_languages_interchange_children_get(&antlr->parsed, node, &count);

  for (i = 0; i < count; i++) {
    const LParserGreenNode **parts;
    uint32                   part_count;
    char                    *name;
    char                    *argument;

    if (!l_antlr_is(antlr, children[i], "lexer_command")) {
      continue;
    }

    parts    = logos_languages_interchange_children_get(&antlr->parsed, children[i], &part_count);
    name     = NULL;
    argument = NULL;

    {
      uint32 k;

      for (k = 0; k < part_count; k++) {
        if (l_antlr_is(antlr, parts[k], "identifier")) {
          const LParserGreenNode *idents[2];

          if (logos_languages_interchange_children(&antlr->parsed, parts[k], idents, 2) > 0) {
            if (name == NULL) {
              name = logos_languages_interchange_token_text(&antlr->parsed, idents[0]);
            } else if (argument == NULL) {
              argument = logos_languages_interchange_token_text(&antlr->parsed, idents[0]);
            }
          }
        } else if (name != NULL && argument == NULL &&
                   nexus_strings_string_equals(logos_languages_interchange_token_name(&antlr->parsed, parts[k]), "integer")) {
          argument = logos_languages_interchange_token_text(&antlr->parsed, parts[k]);
        }
      }
    }

    if (name != NULL && definition != NULL) {
      if (nexus_strings_string_equals(name, "skip")) {
        definition->skip = TRUE;
      } else if (nexus_strings_string_equals(name, "channel")) {
        definition->skip = TRUE;
        logos_languages_interchange_grammar_note(antlr->grammar, LIN_NOTE, "a token sent to the channel `%s` is skipped as trivia here",
                                                 argument != NULL ? argument : "?");
      } else if (nexus_strings_string_equals(name, "pushMode") && argument != NULL) {
        definition->action        = LIA_PUSH;
        definition->action_target = logos_languages_interchange_grammar_mode_add(antlr->grammar, argument);
      } else if (nexus_strings_string_equals(name, "popMode")) {
        definition->action = LIA_POP;
      } else if (nexus_strings_string_equals(name, "mode") && argument != NULL) {
        definition->action        = LIA_REPLACE;
        definition->action_target = logos_languages_interchange_grammar_mode_add(antlr->grammar, argument);
      } else if (nexus_strings_string_equals(name, "more")) {
        logos_languages_interchange_grammar_note(antlr->grammar, LIN_ERROR,
                                                 "the lexer command `more` (the next token continues this one) is not carried over");
        antlr->ok = FALSE;
      } else if (nexus_strings_string_equals(name, "type")) {
        antlr->types++;
      }
    }

    if (name != NULL) {
      nexus_memory_deallocate(name);
    }

    if (argument != NULL) {
      nexus_memory_deallocate(argument);
    }

    nexus_memory_deallocate((void *)parts);
  }

  nexus_memory_deallocate((void *)children);
}

/* ---- parser elements ---- */

/* Summary: any token the lexer produces for the parser (not skipped, not a fragment), except the named ones: the wildcard and ~ of a parser rule. */
static LINode *l_antlr_any_token(LAntlr *antlr, const char *const *except, uint32 except_count) {
  LINode    *items[1024];
  uint32     used;
  uint_large i;

  used = 0;

  for (i = 0; i < nexus_data_list_length_get(antlr->grammar->definitions) && used < 1000; i++) {
    const LIDefinition *definition;
    uint32              k;
    boolean             excluded;

    definition = (const LIDefinition *)nexus_data_list_pointer_at(antlr->grammar->definitions, i);

    if (definition->kind != LIC_TOKEN || definition->skip) {
      continue;
    }

    excluded = FALSE;

    for (k = 0; k < except_count; k++) {
      excluded = excluded || nexus_strings_string_equals(except[k], definition->name);
    }

    if (!excluded) {
      items[used++] = logos_languages_interchange_node_reference(antlr->grammar, definition->name);
    }
  }

  logos_languages_interchange_grammar_note(antlr->grammar, LIN_WARNING,
                                           "a wildcard or ~ in a parser rule stands for the tokens the lexer defines, one alternative each");

  return used == 0 ? l_antlr_empty(antlr) : logos_languages_interchange_node_group(antlr->grammar, LIK_ALT, items, used);
}

static LINode *l_antlr_parser_alternatives(LAntlr *antlr, const LParserGreenNode *node);

/* Summary: a parser atom (a terminal, a rule reference, ~ or the wildcard) as a node. */
static LINode *l_antlr_parser_atom(LAntlr *antlr, const LParserGreenNode *atom_node) {
  LINode *atom;

  atom = NULL;
  {
    const LParserGreenNode **inner;
    uint32                   inner_count;
    uint32                   k;

    inner = logos_languages_interchange_children_get(&antlr->parsed, atom_node, &inner_count);

    for (k = 0; k < inner_count; k++) {
      const char *name;

      name = l_antlr_rule(antlr, inner[k]);

      if (nexus_strings_string_equals(name, "terminal")) {
        atom = l_antlr_terminal(antlr, inner[k], FALSE);
      } else if (nexus_strings_string_equals(name, "parser_rule_reference")) {
        const LParserGreenNode **ref;
        uint32                   ref_count;
        uint32                   ref_index;

        ref = logos_languages_interchange_children_get(&antlr->parsed, inner[k], &ref_count);

        for (ref_index = 0; ref_index < ref_count; ref_index++) {
          if (logos_language_tooling_parsing_syntax_green_node_kind_get(ref[ref_index]) == LPGK_TOKEN &&
              nexus_strings_string_equals(logos_languages_interchange_token_name(&antlr->parsed, ref[ref_index]), "rule_name")) {
            char *text;

            text = logos_languages_interchange_token_text(&antlr->parsed, ref[ref_index]);
            atom = logos_languages_interchange_node_reference(antlr->grammar, text);
            nexus_memory_deallocate(text);
          } else if (l_antlr_is(antlr, ref[ref_index], "argument_block")) {
            antlr->arguments++;
          } else if (l_antlr_is(antlr, ref[ref_index], "element_options")) {
            antlr->element_options++;
          }
        }

        nexus_memory_deallocate((void *)ref);
      } else if (nexus_strings_string_equals(name, "parser_not_set")) {
        const char              *except[256];
        uint32                   except_count;
        char                    *texts[256];
        const LParserGreenNode **elements;
        uint32                   element_count;
        uint32                   elem;

        except_count = 0;
        elements     = logos_languages_interchange_children_get(&antlr->parsed, inner[k], &element_count);

        for (elem = 0; elem < element_count; elem++) {
          const LParserGreenNode **leaves;
          uint32                   leaf_count;
          uint32                   leaf_index;

          leaves = logos_languages_interchange_children_get(&antlr->parsed, elements[elem], &leaf_count);

          for (leaf_index = 0; leaf_index < leaf_count && except_count < 255; leaf_index++) {
            if (logos_language_tooling_parsing_syntax_green_node_kind_get(leaves[leaf_index]) == LPGK_TOKEN) {
              const char *kind;
              char       *text;

              kind = logos_languages_interchange_token_name(&antlr->parsed, leaves[leaf_index]);
              text = logos_languages_interchange_token_text(&antlr->parsed, leaves[leaf_index]);

              if (nexus_strings_string_equals(kind, "token_name")) {
                texts[except_count]  = text;
                except[except_count] = text;
                except_count++;
              } else if (nexus_strings_string_equals(kind, "string_literal")) {
                uint_large  length;
                char       *value;
                const char *token;

                value = l_antlr_literal_text(text, &length);
                token = l_antlr_token_for_literal(antlr, value, length);

                if (token != NULL) {
                  texts[except_count]  = NULL;
                  except[except_count] = token;
                  except_count++;
                }

                nexus_memory_deallocate(value);
                nexus_memory_deallocate(text);
              } else {
                nexus_memory_deallocate(text);
              }
            }
          }

          nexus_memory_deallocate((void *)leaves);
        }

        atom = l_antlr_any_token(antlr, except, except_count);

        for (elem = 0; elem < except_count; elem++) {
          if (texts[elem] != NULL) {
            nexus_memory_deallocate(texts[elem]);
          }
        }

        nexus_memory_deallocate((void *)elements);
      } else if (logos_language_tooling_parsing_syntax_green_node_kind_get(inner[k]) == LPGK_TOKEN &&
                 nexus_strings_string_equals(logos_languages_interchange_token_name(&antlr->parsed, inner[k]), "dot")) {
        atom = l_antlr_any_token(antlr, NULL, 0);
      }
    }

    nexus_memory_deallocate((void *)inner);
  }

  return atom;
}

static LINode *l_antlr_parser_element(LAntlr *antlr, const LParserGreenNode *node) {
  const LParserGreenNode **children;
  uint32                   count;
  uint32                   i;
  LINode                  *atom;
  char                    *suffix_text;
  const char              *suffix;

  children    = logos_languages_interchange_children_get(&antlr->parsed, node, &count);
  atom        = NULL;
  suffix_text = NULL;
  suffix      = NULL;

  for (i = 0; i < count; i++) {
    const char *rule;

    rule = l_antlr_rule(antlr, children[i]);

    if (nexus_strings_string_equals(rule, "ebnf_suffix")) {
      const LParserGreenNode *suffix_children[2];

      if (logos_languages_interchange_children(&antlr->parsed, children[i], suffix_children, 2) > 0) {
        suffix_text = logos_languages_interchange_token_text(&antlr->parsed, suffix_children[0]);
        suffix      = logos_languages_interchange_token_name(&antlr->parsed, suffix_children[0]);
      }
    } else if (nexus_strings_string_equals(rule, "action_or_predicate")) {
      const LParserGreenNode **inner;
      uint32                   inner_count;

      inner = logos_languages_interchange_children_get(&antlr->parsed, children[i], &inner_count);

      if (inner_count > 1) {
        antlr->predicates++;
      } else {
        antlr->actions++;
      }

      nexus_memory_deallocate((void *)inner);
    } else if (nexus_strings_string_equals(rule, "parser_labeled_element")) {
      const LParserGreenNode **inner;
      uint32                   inner_count;
      uint32                   k;

      antlr->labels++;
      inner = logos_languages_interchange_children_get(&antlr->parsed, children[i], &inner_count);

      for (k = 0; k < inner_count; k++) {
        const char *name;

        name = l_antlr_rule(antlr, inner[k]);

        if (nexus_strings_string_equals(name, "parser_atom")) {
          atom = l_antlr_parser_atom(antlr, inner[k]);
        } else if (nexus_strings_string_equals(name, "parser_block")) {
          const LParserGreenNode **block;
          uint32                   block_count;
          uint32                   b2;

          block = logos_languages_interchange_children_get(&antlr->parsed, inner[k], &block_count);

          for (b2 = 0; b2 < block_count; b2++) {
            if (l_antlr_is(antlr, block[b2], "parser_alternatives")) {
              atom = l_antlr_parser_alternatives(antlr, block[b2]);
            }
          }

          nexus_memory_deallocate((void *)block);
        }
      }

      nexus_memory_deallocate((void *)inner);
    } else if (nexus_strings_string_equals(rule, "parser_atom")) {
      atom = l_antlr_parser_atom(antlr, children[i]);
    } else if (nexus_strings_string_equals(rule, "parser_block")) {
      const LParserGreenNode **inner;
      uint32                   inner_count;
      uint32                   k;

      inner = logos_languages_interchange_children_get(&antlr->parsed, children[i], &inner_count);

      for (k = 0; k < inner_count; k++) {
        if (l_antlr_is(antlr, inner[k], "parser_alternatives")) {
          atom = l_antlr_parser_alternatives(antlr, inner[k]);
        }
      }

      nexus_memory_deallocate((void *)inner);
    }
  }

  if (atom != NULL && suffix != NULL) {
    atom = l_antlr_suffix(antlr, atom, suffix);
  }

  if (suffix_text != NULL) {
    nexus_memory_deallocate(suffix_text);
  }

  nexus_memory_deallocate((void *)children);

  return atom;
}

static LINode *l_antlr_parser_alternatives(LAntlr *antlr, const LParserGreenNode *node) {
  const LParserGreenNode **labeled;
  uint32                   labeled_count;
  LINode                  *alternatives[1024];
  uint32                   used;
  uint32                   i;

  labeled = logos_languages_interchange_children_get(&antlr->parsed, node, &labeled_count);
  used    = 0;

  for (i = 0; i < labeled_count && used < 1000; i++) {
    const LParserGreenNode **parts;
    uint32                   part_count;
    uint32                   k;

    if (!l_antlr_is(antlr, labeled[i], "parser_labeled_alternative")) {
      continue;
    }

    parts = logos_languages_interchange_children_get(&antlr->parsed, labeled[i], &part_count);

    for (k = 0; k < part_count; k++) {
      if (l_antlr_is(antlr, parts[k], "parser_alternative")) {
        const LParserGreenNode **elements;
        uint32                   element_count;
        LINode                  *items[1024];
        uint32                   item_count;
        uint32                   element_index;

        elements   = logos_languages_interchange_children_get(&antlr->parsed, parts[k], &element_count);
        item_count = 0;

        for (element_index = 0; element_index < element_count && item_count < 1000; element_index++) {
          if (l_antlr_is(antlr, elements[element_index], "parser_element")) {
            LINode *item;

            item = l_antlr_parser_element(antlr, elements[element_index]);

            if (item != NULL) {
              items[item_count++] = item;
            }
          } else if (l_antlr_is(antlr, elements[element_index], "element_options")) {
            antlr->element_options++;
          }
        }

        alternatives[used++] =
            item_count == 0 ? l_antlr_empty(antlr) : logos_languages_interchange_node_group(antlr->grammar, LIK_SEQ, items, item_count);
        nexus_memory_deallocate((void *)elements);
      }
    }

    nexus_memory_deallocate((void *)parts);
  }

  nexus_memory_deallocate((void *)labeled);

  return used == 0 ? l_antlr_empty(antlr) : logos_languages_interchange_node_group(antlr->grammar, LIK_ALT, alternatives, used);
}

static LINode *l_antlr_alternatives(LAntlr *antlr, const LParserGreenNode *node, boolean lexer, LIDefinition *definition) {
  const LParserGreenNode **alts;
  uint32                   alt_count;
  LINode                  *alternatives[1024];
  uint32                   used;
  uint32                   i;

  (void)lexer;
  alts = logos_languages_interchange_children_get(&antlr->parsed, node, &alt_count);
  used = 0;

  for (i = 0; i < alt_count && used < 1000; i++) {
    const LParserGreenNode **parts;
    uint32                   part_count;
    uint32                   k;
    LAntlrElement            elements[512];
    uint32                   element_count;

    if (!l_antlr_is(antlr, alts[i], "lexer_alternative")) {
      continue;
    }

    parts         = logos_languages_interchange_children_get(&antlr->parsed, alts[i], &part_count);
    element_count = 0;

    for (k = 0; k < part_count; k++) {
      if (l_antlr_is(antlr, parts[k], "lexer_element") && element_count < 500) {
        l_antlr_lexer_element(antlr, parts[k], &elements[element_count++]);
      } else if (l_antlr_is(antlr, parts[k], "lexer_commands")) {
        l_antlr_commands(antlr, parts[k], definition);
      }
    }

    alternatives[used++] = l_antlr_sequence(antlr, elements, element_count);
    nexus_memory_deallocate((void *)parts);
  }

  nexus_memory_deallocate((void *)alts);

  return used == 0 ? l_antlr_empty(antlr) : logos_languages_interchange_node_group(antlr->grammar, LIK_ALT, alternatives, used);
}

/* ---------------------------------------------------------------------------- */
/* RULES                                                                         */
/* ---------------------------------------------------------------------------- */

/* Summary: reads `options { name = value; }` for caseInsensitive. */
static boolean l_antlr_options_insensitive(LAntlr *antlr, const LParserGreenNode *options, boolean current) {
  const LParserGreenNode **children;
  uint32                   count;
  uint32                   i;
  boolean                  result;

  result   = current;
  children = logos_languages_interchange_children_get(&antlr->parsed, options, &count);

  for (i = 0; i < count; i++) {
    if (l_antlr_is(antlr, children[i], "option")) {
      const LParserGreenNode **parts;
      uint32                   part_count;
      char                    *name;
      char                    *value;
      uint32                   k;

      parts = logos_languages_interchange_children_get(&antlr->parsed, children[i], &part_count);
      name  = NULL;
      value = NULL;

      for (k = 0; k < part_count; k++) {
        if (l_antlr_is(antlr, parts[k], "identifier") && name == NULL) {
          const LParserGreenNode *ident[2];

          if (logos_languages_interchange_children(&antlr->parsed, parts[k], ident, 2) > 0) {
            name = logos_languages_interchange_token_text(&antlr->parsed, ident[0]);
          }
        } else if (l_antlr_is(antlr, parts[k], "option_value") && value == NULL) {
          const LParserGreenNode *ident[2];

          if (logos_languages_interchange_children(&antlr->parsed, parts[k], ident, 2) > 0 &&
              logos_language_tooling_parsing_syntax_green_node_kind_get(ident[0]) == LPGK_RULE) {
            const LParserGreenNode *inner[2];

            if (logos_languages_interchange_children(&antlr->parsed, ident[0], inner, 2) > 0) {
              value = logos_languages_interchange_token_text(&antlr->parsed, inner[0]);
            }
          } else if (logos_languages_interchange_children(&antlr->parsed, parts[k], ident, 2) > 0) {
            value = logos_languages_interchange_token_text(&antlr->parsed, ident[0]);
          }
        }
      }

      if (name != NULL && value != NULL && nexus_strings_string_equals(name, "caseInsensitive")) {
        result = nexus_strings_string_equals(value, "true");
      }

      if (name != NULL) {
        nexus_memory_deallocate(name);
      }

      if (value != NULL) {
        nexus_memory_deallocate(value);
      }

      nexus_memory_deallocate((void *)parts);
    }
  }

  nexus_memory_deallocate((void *)children);

  return result;
}

static boolean l_antlr_refers(const LINode *node, const char *name) {
  uint32 i;

  if (node == NULL) {
    return FALSE;
  }

  if (node->kind == LIK_REF && nexus_strings_string_equals(node->text, name)) {
    return TRUE;
  }

  for (i = 0; i < node->count; i++) {
    if (l_antlr_refers(node->items[i], name)) {
      return TRUE;
    }
  }

  return l_antlr_refers(node->child, name);
}

/*
Summary: a lexer rule that contains itself can only be a nested region (a comment inside a comment): open, anything or the rule itself
  repeated, close. Genesis lexers are regular, so such a region becomes a lexical mode that is entered at the opening text, entered
  again at a nested opening, and left at the closing text; all of it is trivia.
*/
static boolean l_antlr_nested(LAntlr *antlr, LIDefinition *definition) {
  const LINode *expression;
  const LINode *open;
  const LINode *body;
  const LINode *close;
  char          name[160];
  const char   *mode;
  LIDefinition *token;
  uint32        any[2];

  expression = definition->expression;

  if (expression->kind != LIK_SEQ || expression->count != 3 || expression->items[0]->kind != LIK_LITERAL ||
      expression->items[2]->kind != LIK_LITERAL || expression->items[1]->kind != LIK_STAR) {
    return FALSE;
  }

  open  = expression->items[0];
  body  = expression->items[1];
  close = expression->items[2];

  if (!l_antlr_refers(body, definition->name)) {
    return FALSE;
  }

  (void)stbsp_snprintf(name, (int)sizeof(name), "%s_nested", definition->name);
  mode = logos_languages_interchange_grammar_mode_add(antlr->grammar, name);

  definition->expression    = (LINode *)open;
  definition->action        = LIA_PUSH;
  definition->action_target = mode;

  (void)stbsp_snprintf(name, (int)sizeof(name), "%s_nested_open", definition->name);
  token                = logos_languages_interchange_grammar_define(antlr->grammar, name, LIC_TOKEN, (LINode *)open);
  token->mode          = mode;
  token->skip          = TRUE;
  token->action        = LIA_PUSH;
  token->action_target = mode;

  (void)stbsp_snprintf(name, (int)sizeof(name), "%s_nested_close", definition->name);
  token         = logos_languages_interchange_grammar_define(antlr->grammar, name, LIC_TOKEN, (LINode *)close);
  token->mode   = mode;
  token->skip   = TRUE;
  token->action = LIA_POP;

  any[0] = 0;
  any[1] = 0;
  (void)stbsp_snprintf(name, (int)sizeof(name), "%s_nested_character", definition->name);
  token       = logos_languages_interchange_grammar_define(antlr->grammar, name, LIC_TOKEN,
                                                           logos_languages_interchange_node_charset(antlr->grammar, any, 0, TRUE));
  token->mode = mode;
  token->skip = TRUE;
  logos_languages_interchange_grammar_note(antlr->grammar, LIN_NOTE,
                                           "the nested rule %s became a lexical mode that is entered at each opening and left at each closing",
                                           definition->name);

  return TRUE;
}

static void l_antlr_lexer_rule(LAntlr *antlr, const LParserGreenNode *node) {
  const LParserGreenNode **children;
  uint32                   count;
  uint32                   i;
  boolean                  fragment;
  boolean                  saved;
  char                    *name;
  LIDefinition            *definition;
  LINode                  *expression;

  children   = logos_languages_interchange_children_get(&antlr->parsed, node, &count);
  fragment   = FALSE;
  saved      = antlr->insensitive;
  name       = NULL;
  expression = NULL;
  definition = NULL;

  /* The name and fragment first, so the definition exists for the commands of the alternatives. */
  for (i = 0; i < count; i++) {
    if (logos_language_tooling_parsing_syntax_green_node_kind_get(children[i]) == LPGK_TOKEN) {
      const char *kind;

      kind = logos_languages_interchange_token_name(&antlr->parsed, children[i]);

      if (nexus_strings_string_equals(kind, "kw_fragment")) {
        fragment = TRUE;
      } else if (nexus_strings_string_equals(kind, "token_name")) {
        name = logos_languages_interchange_token_text(&antlr->parsed, children[i]);
      }
    } else if (l_antlr_is(antlr, children[i], "options_specification")) {
      antlr->insensitive = l_antlr_options_insensitive(antlr, children[i], antlr->insensitive);
    }
  }

  if (name == NULL) {
    nexus_memory_deallocate((void *)children);
    antlr->insensitive = saved;

    return;
  }

  definition       = logos_languages_interchange_grammar_define(antlr->grammar, name, fragment ? LIC_FRAGMENT : LIC_TOKEN, l_antlr_empty(antlr));
  antlr->rule      = definition->name;
  definition->mode = antlr->mode;

  for (i = 0; i < count; i++) {
    if (l_antlr_is(antlr, children[i], "lexer_alternatives")) {
      expression = l_antlr_alternatives(antlr, children[i], TRUE, definition);
    }
  }

  if (expression != NULL) {
    definition->expression = expression;

    if (l_antlr_refers(expression, definition->name) && !l_antlr_nested(antlr, definition)) {
      logos_languages_interchange_grammar_note(antlr->grammar, LIN_ERROR,
                                               "the lexer rule %s contains itself in a way that is not a nested region; Genesis lexers are regular",
                                               definition->name);
      antlr->ok = FALSE;
    }

    /* A rule that is one literal lets the parser's literals find this token. */
    if (!fragment && expression->kind == LIK_LITERAL) {
      LAntlrLiteral entry;

      entry.insensitive = expression->insensitive;
      entry.text        = expression->text;
      entry.length      = expression->length;
      entry.token       = definition->name;
      (void)nexus_data_list_append(antlr->literals, &entry);
    }
  }

  nexus_memory_deallocate(name);
  nexus_memory_deallocate((void *)children);
  antlr->insensitive = saved;
}

static void l_antlr_parser_rule(LAntlr *antlr, const LParserGreenNode *node) {
  const LParserGreenNode **children;
  uint32                   count;
  uint32                   i;
  char                    *name;
  LINode                  *expression;

  children   = logos_languages_interchange_children_get(&antlr->parsed, node, &count);
  name       = NULL;
  expression = NULL;

  for (i = 0; i < count; i++) {
    if (logos_language_tooling_parsing_syntax_green_node_kind_get(children[i]) == LPGK_TOKEN &&
        nexus_strings_string_equals(logos_languages_interchange_token_name(&antlr->parsed, children[i]), "rule_name") && name == NULL) {
      name = logos_languages_interchange_token_text(&antlr->parsed, children[i]);
    } else if (l_antlr_is(antlr, children[i], "argument_block") || l_antlr_is(antlr, children[i], "returns_clause") ||
               l_antlr_is(antlr, children[i], "locals_clause")) {
      antlr->arguments++;
    } else if (l_antlr_is(antlr, children[i], "parser_alternatives")) {
      expression = l_antlr_parser_alternatives(antlr, children[i]);
    }
  }

  if (name != NULL && expression != NULL) {
    (void)logos_languages_interchange_grammar_define(antlr->grammar, name, LIC_RULE, expression);
    antlr->parser_rules = TRUE;
  }

  if (name != NULL) {
    nexus_memory_deallocate(name);
  }

  nexus_memory_deallocate((void *)children);
}

/* Summary: the rules of one parsed file: lexer rules and modes first (only lexer rules when `lexer_pass`), parser rules otherwise. */
static void l_antlr_file(LAntlr *antlr, const LParserGreenNode *top, boolean lexer_pass) {
  const LParserGreenNode **children;
  const LParserGreenNode **roots;
  const LParserGreenNode  *root;
  uint32                   count;
  uint32                   root_count;
  uint32                   i;

  /* The tree's root holds the rule `grammar_spec`. */
  roots       = logos_languages_interchange_children_get(&antlr->parsed, top, &root_count);
  root        = root_count > 0 ? roots[0] : top;
  children    = logos_languages_interchange_children_get(&antlr->parsed, root, &count);
  antlr->mode = NULL;

  for (i = 0; i < count; i++) {
    const char *rule;

    rule = l_antlr_rule(antlr, children[i]);

    if (nexus_strings_string_equals(rule, "prequel")) {
      const LParserGreenNode *inner[2];

      if (logos_languages_interchange_children(&antlr->parsed, children[i], inner, 2) > 0) {
        const char *kind;

        kind = l_antlr_rule(antlr, inner[0]);

        if (lexer_pass && nexus_strings_string_equals(kind, "options_specification")) {
          antlr->insensitive = l_antlr_options_insensitive(antlr, inner[0], antlr->insensitive);
        } else if (lexer_pass && nexus_strings_string_equals(kind, "import_declaration")) {
          logos_languages_interchange_grammar_note(antlr->grammar, LIN_ERROR,
                                                   "`import` of other grammars is not followed; give the imported grammars as further files");
          antlr->ok = FALSE;
        } else if (lexer_pass && nexus_strings_string_equals(kind, "named_action")) {
          antlr->actions++;
        }
      }
    } else if (nexus_strings_string_equals(rule, "rule_specification")) {
      const LParserGreenNode *inner[2];

      if (logos_languages_interchange_children(&antlr->parsed, children[i], inner, 2) > 0) {
        if (l_antlr_is(antlr, inner[0], "lexer_rule") && lexer_pass) {
          l_antlr_lexer_rule(antlr, inner[0]);
        } else if (l_antlr_is(antlr, inner[0], "parser_rule") && !lexer_pass) {
          l_antlr_parser_rule(antlr, inner[0]);
        }
      }
    } else if (nexus_strings_string_equals(rule, "mode_specification")) {
      const LParserGreenNode **parts;
      uint32                   part_count;
      uint32                   k;

      parts = logos_languages_interchange_children_get(&antlr->parsed, children[i], &part_count);

      for (k = 0; k < part_count; k++) {
        if (l_antlr_is(antlr, parts[k], "identifier")) {
          const LParserGreenNode *ident[2];

          if (logos_languages_interchange_children(&antlr->parsed, parts[k], ident, 2) > 0) {
            char *name;

            name        = logos_languages_interchange_token_text(&antlr->parsed, ident[0]);
            antlr->mode = logos_languages_interchange_grammar_mode_add(antlr->grammar, name);
            nexus_memory_deallocate(name);
          }
        } else if (l_antlr_is(antlr, parts[k], "lexer_rule") && lexer_pass) {
          l_antlr_lexer_rule(antlr, parts[k]);
        }
      }

      nexus_memory_deallocate((void *)parts);
    }
  }

  nexus_memory_deallocate((void *)children);
  nexus_memory_deallocate((void *)roots);
}

/* ---------------------------------------------------------------------------- */
/* ENTRY                                                                         */
/* ---------------------------------------------------------------------------- */

boolean logos_languages_interchange_antlr_convert(LGenesisCompiler *compiler, LIGrammar *grammar, const LInterchangeInput *inputs,
                                                  uint32 input_count) {
  LAntlr     antlr;
  LIParsed  *parsed;
  uint32     i;
  uint_large k;

  antlr.compiler        = compiler;
  antlr.grammar         = grammar;
  antlr.mode            = NULL;
  antlr.insensitive     = FALSE;
  antlr.ok              = TRUE;
  antlr.literals        = nexus_data_list_create(NEXUS_SIZEOF(LAntlrLiteral), 64);
  antlr.actions         = 0;
  antlr.predicates      = 0;
  antlr.arguments       = 0;
  antlr.element_options = 0;
  antlr.labels          = 0;
  antlr.types           = 0;
  antlr.greedy          = 0;
  antlr.properties      = FALSE;
  antlr.parser_rules    = FALSE;
  parsed                = (LIParsed *)nexus_memory_allocate(((uint_large)input_count + 1) * NEXUS_SIZEOF(*parsed));
  nexus_memory_bytes_clear(parsed, ((uint_large)input_count + 1) * NEXUS_SIZEOF(*parsed));

  for (i = 0; i < input_count && antlr.ok; i++) {
    if (!logos_languages_interchange_parse(compiler, grammar, "ANTLR4", inputs[i].text, inputs[i].size, &parsed[i])) {
      logos_languages_interchange_grammar_note(grammar, LIN_NOTE, "in %s", inputs[i].name);
      antlr.ok = FALSE;
    }
  }

  /* The lexer rules of every file first (the parser's literals look them up), then the parser rules. */
  for (i = 0; i < input_count && antlr.ok; i++) {
    antlr.parsed      = parsed[i];
    antlr.insensitive = FALSE;
    l_antlr_file(&antlr, logos_language_tooling_parsing_syntax_tree_root_get(parsed[i].tree), TRUE);
  }

  for (i = 0; i < input_count && antlr.ok; i++) {
    antlr.parsed = parsed[i];
    l_antlr_file(&antlr, logos_language_tooling_parsing_syntax_tree_root_get(parsed[i].tree), FALSE);
  }

  /* The entry is the first parser rule, as in ANTLR; a lexer-only grammar starts at its first token. */
  for (k = 0; k < nexus_data_list_length_get(grammar->definitions); k++) {
    const LIDefinition *definition;

    definition = (const LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, k);

    if (definition->kind == LIC_RULE) {
      grammar->entry = definition->name;
      break;
    }
  }

  if (antlr.actions > 0) {
    logos_languages_interchange_grammar_note(grammar, LIN_NOTE, "%u actions (target-language code) are dropped", (unsigned)antlr.actions);
  }

  if (antlr.predicates > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_WARNING, "%u semantic predicates are ignored, so the language accepts what they would have refused", (unsigned)antlr.predicates);
  }

  if (antlr.arguments > 0 || antlr.labels > 0 || antlr.element_options > 0 || antlr.types > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_NOTE, "rule arguments, returns, labels, element options and token types are dropped (they do not change what is accepted)");
  }

  if (antlr.properties) {
    logos_languages_interchange_grammar_note(grammar, LIN_WARNING, "Unicode property classes (\\p{..}) are approximated by tables of common ranges");
  }

  logos_languages_interchange_grammar_note(grammar, LIN_NOTE,
                                           "ANTLR picks among ambiguous alternatives by their order and by precedence; the Earley engine keeps every "
                                           "derivation, which accepts the same sentences");

  for (i = 0; i < input_count; i++) {
    logos_languages_interchange_parsed_release(&parsed[i]);
  }

  nexus_memory_deallocate(parsed);
  nexus_data_list_destroy(antlr.literals);

  return antlr.ok;
}
