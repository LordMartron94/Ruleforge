#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
ABNF (RFC 5234, RFC 7405) to the neutral description. ABNF does not tell lexical from syntactic rules and is case-insensitive in
rule names, so names are lower-cased and the emitter classifies. Prose values describe instead of define and are rejected;
the core rules of the RFC (ALPHA, DIGIT, ...) are supplied when a grammar uses them without defining them.
*/

#define LIABNF_CHILDREN_MAX 4096
#define LIABNF_REPEAT_MAX   64

static void l_abnf_lower(char *text) {
  for (; *text != '\0'; text++) {
    if (*text >= 'A' && *text <= 'Z') {
      *text = (char)(*text - 'A' + 'a');
    }
  }
}

static LINode *l_abnf_reference(LIGrammar *grammar, const char *name) {
  char   *copy;
  LINode *node;

  copy = (char *)nexus_memory_allocate(nexus_strings_string_length_get(name) + 1);
  nexus_memory_bytes_copy(name, copy, nexus_strings_string_length_get(name) + 1);
  l_abnf_lower(copy);
  node = logos_languages_interchange_node_reference(grammar, copy);
  nexus_memory_deallocate(copy);

  return node;
}

static uint_large l_abnf_encode(uint32 value, char *out) {
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

/* ---------------------------------------------------------------------------- */
/* NUMERIC VALUES                                                                */
/* ---------------------------------------------------------------------------- */

static uint32 l_abnf_digit(char letter) {
  if (letter >= '0' && letter <= '9') {
    return (uint32)(letter - '0');
  }

  if (letter >= 'a' && letter <= 'f') {
    return (uint32)(letter - 'a' + 10);
  }

  return (uint32)(letter - 'A' + 10);
}

/* Summary: %b, %d or %x values: a string of code points ("%x41.42"), or a range ("%x41-5A"). */
static LINode *l_abnf_numeric(LIGrammar *grammar, const char *text) {
  uint32     base;
  uint32     value;
  uint32     pair[2];
  char       buffer[512];
  uint_large length;
  uint_large i;
  boolean    range;

  if (text[1] == 'b') {
    base = 2;
  } else {
    base = text[1] == 'd' ? 10 : 16;
  }

  i = 2;

  value  = 0;
  length = 0;
  range  = FALSE;

  for (;; i++) {
    char letter;

    letter = text[i];

    if (letter == '.' || letter == '-' || letter == '\0') {
      if (range) {
        pair[1] = value;

        return logos_languages_interchange_node_charset(grammar, pair, 1, FALSE);
      }

      if (letter == '-') {
        range   = TRUE;
        pair[0] = value;
        value   = 0;
        continue;
      }

      if (length + 5 < sizeof(buffer)) {
        length += l_abnf_encode(value, buffer + length);
      }

      value = 0;

      if (letter == '\0') {
        break;
      }

      continue;
    }

    value = (value * base) + l_abnf_digit(letter);

    if (value > 0x10FFFF) {
      value = 0x10FFFF;
    }
  }

  return logos_languages_interchange_node_literal(grammar, buffer, length, FALSE);
}

/* ---------------------------------------------------------------------------- */
/* THE TREE                                                                      */
/* ---------------------------------------------------------------------------- */

typedef struct LAbnf {
  LGenesisCompiler *compiler;
  LIGrammar        *grammar;
  LIParsed          parsed;
  boolean           ok;
} LAbnf;

static LINode *l_abnf_alternation(LAbnf *abnf, const LParserGreenNode *node);

static LINode *l_abnf_element(LAbnf *abnf, const LParserGreenNode *node) {
  const LParserGreenNode *children[LIABNF_CHILDREN_MAX];
  uint32                  count;
  const char             *kind;
  char                   *text;
  LINode                 *result;
  uint_large              length;

  count = logos_languages_interchange_children(&abnf->parsed, node, children, LIABNF_CHILDREN_MAX);

  if (count == 0) {
    return logos_languages_interchange_node_new(abnf->grammar, LIK_EMPTY);
  }

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(children[0]) == LPGK_RULE) {
    const char *name;

    name = logos_languages_interchange_rule_name(&abnf->parsed, children[0]);

    if (nexus_strings_string_equals(name, "group")) {
      const LParserGreenNode *inner[LIABNF_CHILDREN_MAX];
      uint32                  inner_count;
      uint32                  i;

      inner_count = logos_languages_interchange_children(&abnf->parsed, children[0], inner, LIABNF_CHILDREN_MAX);

      for (i = 0; i < inner_count; i++) {
        if (nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf->parsed, inner[i]), "alternation")) {
          return l_abnf_alternation(abnf, inner[i]);
        }
      }
    } else if (nexus_strings_string_equals(name, "option")) {
      const LParserGreenNode *inner[LIABNF_CHILDREN_MAX];
      uint32                  inner_count;
      uint32                  i;

      inner_count = logos_languages_interchange_children(&abnf->parsed, children[0], inner, LIABNF_CHILDREN_MAX);

      for (i = 0; i < inner_count; i++) {
        if (nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf->parsed, inner[i]), "alternation")) {
          return logos_languages_interchange_node_wrap(abnf->grammar, LIK_OPT, l_abnf_alternation(abnf, inner[i]));
        }
      }
    }

    return logos_languages_interchange_node_new(abnf->grammar, LIK_EMPTY);
  }

  kind = logos_languages_interchange_token_name(&abnf->parsed, children[0]);
  text = logos_languages_interchange_token_text(&abnf->parsed, children[0]);

  if (nexus_strings_string_equals(kind, "rulename")) {
    result = l_abnf_reference(abnf->grammar, text);
  } else if (nexus_strings_string_equals(kind, "string") || nexus_strings_string_equals(kind, "string_sensitive") ||
             nexus_strings_string_equals(kind, "string_insensitive")) {
    uint_large start;
    boolean    insensitive;

    start       = nexus_strings_string_equals(kind, "string") ? 1 : 3;
    length      = nexus_strings_string_length_get(text);
    insensitive = !nexus_strings_string_equals(kind, "string_sensitive");
    result      = logos_languages_interchange_node_literal(abnf->grammar, text + start, length - start - 1, insensitive);
  } else if (nexus_strings_string_equals(kind, "prose")) {
    logos_languages_interchange_grammar_note(
        abnf->grammar, LIN_ERROR, "the prose value %s describes in words what a rule means and does not define it; write the rule it stands for",
        text);
    abnf->ok = FALSE;
    result   = logos_languages_interchange_node_new(abnf->grammar, LIK_EMPTY);
  } else {
    result = l_abnf_numeric(abnf->grammar, text);
  }

  nexus_memory_deallocate(text);

  return result;
}

/* Summary: a repetition: the optional repeat prefix and the element. */
static LINode *l_abnf_repetition(LAbnf *abnf, const LParserGreenNode *node) {
  const LParserGreenNode *children[LIABNF_CHILDREN_MAX];
  uint32                  count;
  uint32                  i;
  const LParserGreenNode *element_node;
  LINode                 *element;
  uint32                  minimum;
  int64                   maximum; /* -1: unbounded */
  boolean                 has_repeat;

  count        = logos_languages_interchange_children(&abnf->parsed, node, children, LIABNF_CHILDREN_MAX);
  element      = NULL;
  element_node = NULL;
  has_repeat   = FALSE;
  minimum      = 0;
  maximum      = -1;

  for (i = 0; i < count; i++) {
    if (nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf->parsed, children[i]), "repeat")) {
      const LParserGreenNode *parts[16];
      uint32                  part_count;
      uint32                  part;
      boolean                 seen_star;
      boolean                 before;
      uint32                  numbers[2];

      part_count = logos_languages_interchange_children(&abnf->parsed, children[i], parts, 16);
      seen_star  = FALSE;
      before     = TRUE;
      numbers[0] = 0;
      numbers[1] = 0;
      has_repeat = TRUE;

      {
        boolean have_min;
        boolean have_max;

        have_min = FALSE;
        have_max = FALSE;

        for (part = 0; part < part_count; part++) {
          const char *kind;
          char       *text;

          kind = logos_languages_interchange_token_name(&abnf->parsed, parts[part]);
          text = logos_languages_interchange_token_text(&abnf->parsed, parts[part]);

          if (nexus_strings_string_equals(kind, "star")) {
            seen_star = TRUE;
            before    = FALSE;
          } else {
            uint32     value;
            uint_large k;

            value = 0;

            for (k = 0; text[k] != '\0' && value < 100000; k++) {
              value = (value * 10) + (uint32)(text[k] - '0');
            }

            if (before) {
              numbers[0] = value;
              have_min   = TRUE;
            } else {
              numbers[1] = value;
              have_max   = TRUE;
            }
          }

          nexus_memory_deallocate(text);
        }

        if (!seen_star) {
          minimum = numbers[0];
          maximum = (int64)numbers[0];
        } else {
          minimum = have_min ? numbers[0] : 0;
          maximum = have_max ? (int64)numbers[1] : -1;
        }
      }
    } else if (nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf->parsed, children[i]), "element")) {
      element_node = children[i];
    }
  }

  if (element_node == NULL || (has_repeat && maximum == 0)) {
    /* 0<x> and 0*0x: none of it, so what it is does not matter (RFC 3986 writes path-empty = 0<pchar>). */
    return logos_languages_interchange_node_new(abnf->grammar, LIK_EMPTY);
  }

  element = l_abnf_element(abnf, element_node);

  if (!has_repeat) {
    return element;
  }

  if ((maximum >= 0 && maximum > LIABNF_REPEAT_MAX) || minimum > LIABNF_REPEAT_MAX) {
    logos_languages_interchange_grammar_note(abnf->grammar, LIN_ERROR, "a repetition count above %d is not carried over", LIABNF_REPEAT_MAX);
    abnf->ok = FALSE;

    return element;
  }

  if (maximum == -1) {
    if (minimum == 0) {
      return logos_languages_interchange_node_wrap(abnf->grammar, LIK_STAR, element);
    }

    if (minimum == 1) {
      return logos_languages_interchange_node_wrap(abnf->grammar, LIK_PLUS, element);
    }

    {
      LINode *items[LIABNF_REPEAT_MAX + 2];
      uint32  k;

      for (k = 0; k + 1 < minimum; k++) {
        items[k] = element;
      }

      items[minimum - 1] = logos_languages_interchange_node_wrap(abnf->grammar, LIK_PLUS, element);

      return logos_languages_interchange_node_group(abnf->grammar, LIK_SEQ, items, minimum);
    }
  }

  if (maximum == 0) {
    return logos_languages_interchange_node_new(abnf->grammar, LIK_EMPTY);
  }

  {
    LINode *items[LIABNF_REPEAT_MAX + 2];
    uint32  count_items;
    uint32  k;
    LINode *tail;

    count_items = 0;

    for (k = 0; k < minimum; k++) {
      items[count_items++] = element;
    }

    /* The optional part: x (x (x)?)? */
    tail = NULL;

    for (k = minimum; k < (uint32)maximum; k++) {
      if (tail == NULL) {
        tail = logos_languages_interchange_node_wrap(abnf->grammar, LIK_OPT, element);
      } else {
        LINode *pair[2];

        pair[0] = element;
        pair[1] = tail;
        tail = logos_languages_interchange_node_wrap(abnf->grammar, LIK_OPT, logos_languages_interchange_node_group(abnf->grammar, LIK_SEQ, pair, 2));
      }
    }

    if (tail != NULL) {
      items[count_items++] = tail;
    }

    return count_items == 0 ? logos_languages_interchange_node_new(abnf->grammar, LIK_EMPTY)
                            : logos_languages_interchange_node_group(abnf->grammar, LIK_SEQ, items, count_items);
  }
}

static LINode *l_abnf_concatenation(LAbnf *abnf, const LParserGreenNode *node) {
  const LParserGreenNode *children[LIABNF_CHILDREN_MAX];
  LINode                 *items[LIABNF_CHILDREN_MAX];
  uint32                  count;
  uint32                  used;
  uint32                  i;

  count = logos_languages_interchange_children(&abnf->parsed, node, children, LIABNF_CHILDREN_MAX);
  used  = 0;

  for (i = 0; i < count; i++) {
    if (nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf->parsed, children[i]), "repetition")) {
      items[used++] = l_abnf_repetition(abnf, children[i]);
    }
  }

  return used == 0 ? logos_languages_interchange_node_new(abnf->grammar, LIK_EMPTY)
                   : logos_languages_interchange_node_group(abnf->grammar, LIK_SEQ, items, used);
}

static LINode *l_abnf_alternation(LAbnf *abnf, const LParserGreenNode *node) {
  const LParserGreenNode *children[LIABNF_CHILDREN_MAX];
  LINode                 *items[LIABNF_CHILDREN_MAX];
  uint32                  count;
  uint32                  used;
  uint32                  i;

  count = logos_languages_interchange_children(&abnf->parsed, node, children, LIABNF_CHILDREN_MAX);
  used  = 0;

  for (i = 0; i < count; i++) {
    if (nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf->parsed, children[i]), "concatenation")) {
      items[used++] = l_abnf_concatenation(abnf, children[i]);
    }
  }

  return used == 0 ? logos_languages_interchange_node_new(abnf->grammar, LIK_EMPTY)
                   : logos_languages_interchange_node_group(abnf->grammar, LIK_ALT, items, used);
}

/* ---------------------------------------------------------------------------- */
/* THE CORE RULES OF THE RFC (APPENDIX B)                                        */
/* ---------------------------------------------------------------------------- */

static LINode *l_abnf_range(LIGrammar *grammar, uint32 low, uint32 high) {
  uint32 pair[2];

  pair[0] = low;
  pair[1] = high;

  return logos_languages_interchange_node_charset(grammar, pair, 1, FALSE);
}

static LINode *l_abnf_char(LIGrammar *grammar, uint32 value) {
  return l_abnf_range(grammar, value, value);
}

/* Summary: defines a core rule by name; FALSE when the name is not one. */
static boolean l_abnf_core(LIGrammar *grammar, const char *name) {
  LINode *items[8];
  LINode *node;

  if (nexus_strings_string_equals(name, "alpha")) {
    uint32 ranges[4];

    ranges[0] = 0x41;
    ranges[1] = 0x5A;
    ranges[2] = 0x61;
    ranges[3] = 0x7A;
    node      = logos_languages_interchange_node_charset(grammar, ranges, 2, FALSE);
  } else if (nexus_strings_string_equals(name, "bit")) {
    node = l_abnf_range(grammar, '0', '1');
  } else if (nexus_strings_string_equals(name, "char")) {
    node = l_abnf_range(grammar, 0x01, 0x7F);
  } else if (nexus_strings_string_equals(name, "cr")) {
    node = l_abnf_char(grammar, 0x0D);
  } else if (nexus_strings_string_equals(name, "crlf")) {
    items[0] = logos_languages_interchange_node_reference(grammar, "cr");
    items[1] = logos_languages_interchange_node_reference(grammar, "lf");
    node     = logos_languages_interchange_node_group(grammar, LIK_SEQ, items, 2);
  } else if (nexus_strings_string_equals(name, "ctl")) {
    uint32 ranges[4];

    ranges[0] = 0x00;
    ranges[1] = 0x1F;
    ranges[2] = 0x7F;
    ranges[3] = 0x7F;
    node      = logos_languages_interchange_node_charset(grammar, ranges, 2, FALSE);
  } else if (nexus_strings_string_equals(name, "digit")) {
    node = l_abnf_range(grammar, 0x30, 0x39);
  } else if (nexus_strings_string_equals(name, "dquote")) {
    node = l_abnf_char(grammar, 0x22);
  } else if (nexus_strings_string_equals(name, "hexdig")) {
    uint32 ranges[6];

    ranges[0] = '0';
    ranges[1] = '9';
    ranges[2] = 'A';
    ranges[3] = 'F';
    ranges[4] = 'a';
    ranges[5] = 'f';
    node      = logos_languages_interchange_node_charset(grammar, ranges, 3, FALSE);
  } else if (nexus_strings_string_equals(name, "htab")) {
    node = l_abnf_char(grammar, 0x09);
  } else if (nexus_strings_string_equals(name, "lf")) {
    node = l_abnf_char(grammar, 0x0A);
  } else if (nexus_strings_string_equals(name, "lwsp")) {
    LINode *pair[2];

    pair[0]  = logos_languages_interchange_node_reference(grammar, "crlf");
    pair[1]  = logos_languages_interchange_node_reference(grammar, "wsp");
    items[0] = logos_languages_interchange_node_reference(grammar, "wsp");
    items[1] = logos_languages_interchange_node_group(grammar, LIK_SEQ, pair, 2);
    node     = logos_languages_interchange_node_wrap(grammar, LIK_STAR, logos_languages_interchange_node_group(grammar, LIK_ALT, items, 2));
  } else if (nexus_strings_string_equals(name, "octet")) {
    node = l_abnf_range(grammar, 0x00, 0xFF);
  } else if (nexus_strings_string_equals(name, "sp")) {
    node = l_abnf_char(grammar, 0x20);
  } else if (nexus_strings_string_equals(name, "vchar")) {
    node = l_abnf_range(grammar, 0x21, 0x7E);
  } else if (nexus_strings_string_equals(name, "wsp")) {
    items[0] = logos_languages_interchange_node_reference(grammar, "sp");
    items[1] = logos_languages_interchange_node_reference(grammar, "htab");
    node     = logos_languages_interchange_node_group(grammar, LIK_ALT, items, 2);
  } else {
    return FALSE;
  }

  (void)logos_languages_interchange_grammar_define(grammar, name, LIC_UNKNOWN, node);

  return TRUE;
}

static boolean l_abnf_defined(const LIGrammar *grammar, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(grammar->definitions); i++) {
    if (nexus_strings_string_equals(((const LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, i))->name, name)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void l_abnf_references_collect(const LINode *node, NexusDataList *out) {
  uint32 i;

  if (node == NULL) {
    return;
  }

  if (node->kind == LIK_REF) {
    (void)nexus_data_list_append(out, (const void *)&node->text);
  }

  for (i = 0; i < node->count; i++) {
    l_abnf_references_collect(node->items[i], out);
  }

  l_abnf_references_collect(node->child, out);
}

/* Summary: defines the core rules that the grammar uses and does not define, until nothing more is missing. */
static void l_abnf_core_supply(LIGrammar *grammar) {
  boolean added;

  do {
    NexusDataList *used;
    uint_large     i;

    used  = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
    added = FALSE;

    for (i = 0; i < nexus_data_list_length_get(grammar->definitions); i++) {
      l_abnf_references_collect(((const LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, i))->expression, used);
    }

    for (i = 0; i < nexus_data_list_length_get(used); i++) {
      const char *name;

      name = (const char *)nexus_data_list_pointer_at(used, i);

      if (!l_abnf_defined(grammar, name) && l_abnf_core(grammar, name)) {
        added = TRUE;
        break;
      }
    }

    nexus_data_list_destroy(used);
  } while (added);
}

/* ---------------------------------------------------------------------------- */
/* ENTRY                                                                         */
/* ---------------------------------------------------------------------------- */

boolean logos_languages_interchange_abnf_convert(LGenesisCompiler *compiler, LIGrammar *grammar, const LInterchangeInput *inputs,
                                                 uint32 input_count) {
  LAbnf                   abnf;
  const LParserGreenNode *children[LIABNF_CHILDREN_MAX];
  uint32                  count;
  uint32                  i;

  abnf.compiler = compiler;
  abnf.grammar  = grammar;
  abnf.ok       = TRUE;

  if (input_count != 1) {
    logos_languages_interchange_grammar_note(grammar, LIN_ERROR, "an ABNF grammar is one file");

    return FALSE;
  }

  if (!logos_languages_interchange_parse(compiler, grammar, "ABNF", inputs[0].text, inputs[0].size, &abnf.parsed)) {
    return FALSE;
  }

  count = logos_languages_interchange_children(&abnf.parsed, logos_language_tooling_parsing_syntax_tree_root_get(abnf.parsed.tree), children,
                                               LIABNF_CHILDREN_MAX);

  for (i = 0; i < count; i++) {
    const LParserGreenNode *rule_children[LIABNF_CHILDREN_MAX];
    uint32                  rule_count;
    uint32                  k;
    char                   *name;
    boolean                 incremental;
    LINode                 *expression;

    if (!nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf.parsed, children[i]), "rulelist")) {
      continue;
    }

    rule_count = logos_languages_interchange_children(&abnf.parsed, children[i], rule_children, LIABNF_CHILDREN_MAX);

    for (k = 0; k < rule_count; k++) {
      const LParserGreenNode *parts[LIABNF_CHILDREN_MAX];
      uint32                  part_count;
      uint32                  part;

      if (!nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf.parsed, rule_children[k]), "rule")) {
        continue;
      }

      part_count  = logos_languages_interchange_children(&abnf.parsed, rule_children[k], parts, LIABNF_CHILDREN_MAX);
      name        = NULL;
      incremental = FALSE;
      expression  = NULL;

      for (part = 0; part < part_count; part++) {
        if (name == NULL && nexus_strings_string_equals(logos_languages_interchange_token_name(&abnf.parsed, parts[part]), "rulename")) {
          name = logos_languages_interchange_token_text(&abnf.parsed, parts[part]);
          l_abnf_lower(name);
        } else if (nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf.parsed, parts[part]), "defined_as")) {
          const LParserGreenNode *operator_node[2];

          if (logos_languages_interchange_children(&abnf.parsed, parts[part], operator_node, 2) > 0) {
            incremental = nexus_strings_string_equals(logos_languages_interchange_token_name(&abnf.parsed, operator_node[0]), "equals_slash");
          }
        } else if (nexus_strings_string_equals(logos_languages_interchange_rule_name(&abnf.parsed, parts[part]), "alternation")) {
          expression = l_abnf_alternation(&abnf, parts[part]);
        }
      }

      if (name == NULL || expression == NULL) {
        continue;
      }

      if (incremental && l_abnf_defined(grammar, name)) {
        uint_large index;

        for (index = 0; index < nexus_data_list_length_get(grammar->definitions); index++) {
          LIDefinition *existing;

          existing = (LIDefinition *)nexus_data_list_pointer_at(grammar->definitions, index);

          if (nexus_strings_string_equals(existing->name, name)) {
            LINode *both[2];

            both[0]              = existing->expression;
            both[1]              = expression;
            existing->expression = logos_languages_interchange_node_group(grammar, LIK_ALT, both, 2);
          }
        }
      } else if (l_abnf_defined(grammar, name)) {
        logos_languages_interchange_grammar_note(grammar, LIN_ERROR, "the rule `%s` is defined twice (use =/ to add alternatives)", name);
        abnf.ok = FALSE;
      } else {
        (void)logos_languages_interchange_grammar_define(grammar, name, LIC_UNKNOWN, expression);
      }

      nexus_memory_deallocate(name);
    }
  }

  l_abnf_core_supply(grammar);
  logos_languages_interchange_grammar_note(
      grammar, LIN_NOTE,
      "ABNF has no lexical rules: definitions that reach only characters became tokens and fragments, the others rules, and the Earley "
      "engine reads the result; a token boundary is the longest match, which ABNF, working on characters, does not say");
  logos_languages_interchange_parsed_release(&abnf.parsed);

  return abnf.ok;
}
