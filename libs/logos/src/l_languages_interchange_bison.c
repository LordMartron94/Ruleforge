#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
yacc and GNU Bison grammars (.y, .yy), with the flex scanner (.l) that belongs to them, to the neutral description.

What carries over: the rules with their alternatives; the tokens (declared with %token and named by the scanner, or written as character
and string literals); %start; and operator precedence. %left, %right, %nonassoc and %precedence give each token a level and an
associativity; a rule's precedence is that of its last token or of its %prec. A rule with a precedence that begins or ends with a
nonterminal becomes a rule of its own that is written with `@prec(associativity, level)`: the Earley engine then keeps, where such rules meet,
the grouping yacc would have chosen, so `a + b * c` has the tree yacc gives it and `a < b < c` is not a sentence when `<` is %nonassoc.

What is dropped with a note: actions (also mid-rule ones, which would be empty rules of their own), %union, %type and the other declarations
that do not change what is accepted, and the epilogue. What is ignored with a warning: semantic predicates (%?{ }), and alternatives that
use the `error` token (error recovery accepts more than the language; the alternatives are left out). What cannot be carried over, said
once as a warning: a precedence that settles a conflict between rules that are not operators (`%prec LOWER_THAN_ELSE` for a dangling else):
the Earley engine keeps both derivations, which accepts the same sentences.

A token is defined by the scanner. A token that no scanner defines becomes the literal of its string alias when it has one without a blank
(`%token EQ "=="`), and is an error otherwise.
*/

typedef struct LBisonToken {
  const char *name;
  const char *alias; /* the decoded string alias; NULL without one */
  boolean     used;
  boolean     defined; /* the scanner defines it */
  boolean     eof;     /* %token END 0: the end of the input, which no rule needs to match */
} LBisonToken;

typedef struct LBisonPrecedence {
  const char            *key; /* a token name, "'c" for a character literal, or "\"text" for a string literal */
  LContextaAssociativity associativity;
  int32                  level;
} LBisonPrecedence;

typedef struct LBisonAlternative {
  NexusDataList         *items;   /* LINode * */
  NexusDataList         *is_rule; /* boolean per item: a nonterminal */
  LContextaAssociativity associativity;
  int32                  level;
  boolean                error; /* uses the error token */
} LBisonAlternative;

typedef struct LBisonRule {
  const char    *name;
  NexusDataList *alternatives; /* LBisonAlternative * */
} LBisonRule;

typedef struct LBison {
  LGenesisCompiler *compiler;
  LIGrammar        *grammar;
  LIParsed          parsed;
  boolean           ok;
  NexusDataList    *tokens;      /* LBisonToken, by value */
  NexusDataList    *precedences; /* LBisonPrecedence, by value */
  NexusDataList    *rules;       /* LBisonRule * */
  const char       *start;
  int32             level;
  uint32            actions;
  uint32            mid_rule_actions;
  uint32            predicates;
  uint32            error_alternatives;
  uint32            inert_precedences;
  uint32            glr;
  uint32            ignored_declarations;
} LBison;

/* ---------------------------------------------------------------------------- */
/* TEXT                                                                          */
/* ---------------------------------------------------------------------------- */

static boolean l_bison_is(LBison *bison, const LParserGreenNode *node, const char *name) {
  return nexus_strings_string_equals(logos_languages_interchange_rule_name(&bison->parsed, node), name);
}

static const char *l_bison_copy(LBison *bison, const char *text, uint_large length) {
  return nexus_data_arena_text_copy(&bison->grammar->arena, text, length);
}

static uint_large l_bison_encode(uint32 value, char *out) {
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

static boolean l_bison_is_hex(char letter) {
  return (letter >= '0' && letter <= '9') || (letter >= 'a' && letter <= 'f') || (letter >= 'A' && letter <= 'F');
}

static uint32 l_bison_hex(char letter) {
  if (letter >= '0' && letter <= '9') {
    return (uint32)(letter - '0');
  }

  return (uint32)((letter >= 'a' ? letter - 'a' : letter - 'A') + 10);
}

/* Summary: the text between the quotes of a character or string literal, with C escapes decoded, as UTF-8 bytes (Nexus-allocated). */
static char *l_bison_unquote(const char *text, uint_large *out_length) {
  uint_large length;
  uint_large position;
  uint_large out;
  char      *bytes;

  length   = nexus_strings_string_length_get(text);
  bytes    = (char *)nexus_memory_allocate((length * 4) + 1);
  position = 1;
  out      = 0;

  while (position + 1 < length) {
    uint32 code;

    if (text[position] != '\\') {
      bytes[out++] = text[position++];
      continue;
    }

    position++;
    code = (byte)text[position];

    switch (text[position]) {
    case 'n':
      code = '\n';
      position++;
      break;
    case 't':
      code = '\t';
      position++;
      break;
    case 'r':
      code = '\r';
      position++;
      break;
    case 'f':
      code = 0x0C;
      position++;
      break;
    case 'v':
      code = 0x0B;
      position++;
      break;
    case 'a':
      code = 0x07;
      position++;
      break;
    case 'b':
      code = 0x08;
      position++;
      break;
    case 'x':
      position++;
      code = 0;

      while (position + 1 < length && l_bison_is_hex(text[position])) {
        code = (code << 4) | l_bison_hex(text[position]);
        position++;
      }

      break;
    default:
      if (text[position] >= '0' && text[position] <= '7') {
        uint32 digits;

        code   = 0;
        digits = 0;

        while (digits < 3 && position + 1 < length && text[position] >= '0' && text[position] <= '7') {
          code = (code * 8) + (uint32)(text[position] - '0');
          position++;
          digits++;
        }
      } else {
        position++;
      }

      break;
    }

    out += l_bison_encode(code, bytes + out);
  }

  bytes[out]  = '\0';
  *out_length = out;

  return bytes;
}

/* ---------------------------------------------------------------------------- */
/* SYMBOLS                                                                       */
/* ---------------------------------------------------------------------------- */

static LBisonToken *l_bison_token_find(const LBison *bison, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(bison->tokens); i++) {
    LBisonToken *token;

    token = (LBisonToken *)nexus_data_list_at(bison->tokens, i);

    if (nexus_strings_string_equals(token->name, name)) {
      return token;
    }
  }

  return NULL;
}

static LBisonToken *l_bison_token_alias_find(const LBison *bison, const char *alias, uint_large length) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(bison->tokens); i++) {
    LBisonToken *token;

    token = (LBisonToken *)nexus_data_list_at(bison->tokens, i);

    if (token->alias != NULL && nexus_strings_string_length_get(token->alias) == length && nexus_memory_bytes_equal(token->alias, alias, length)) {
      return token;
    }
  }

  return NULL;
}

static LBisonToken *l_bison_token_declare(LBison *bison, const char *name) {
  LBisonToken *found;
  LBisonToken  token;

  found = l_bison_token_find(bison, name);

  if (found != NULL) {
    return found;
  }

  token.name    = l_bison_copy(bison, name, nexus_strings_string_length_get(name));
  token.alias   = NULL;
  token.used    = FALSE;
  token.defined = FALSE;
  token.eof     = FALSE;
  (void)nexus_data_list_append(bison->tokens, &token);

  return (LBisonToken *)nexus_data_list_at(bison->tokens, nexus_data_list_length_get(bison->tokens) - 1);
}

static void l_bison_precedence_set(LBison *bison, const char *key, LContextaAssociativity associativity, int32 level) {
  LBisonPrecedence precedence;

  precedence.key           = l_bison_copy(bison, key, nexus_strings_string_length_get(key));
  precedence.associativity = associativity;
  precedence.level         = level;
  (void)nexus_data_list_append(bison->precedences, &precedence);
}

static const LBisonPrecedence *l_bison_precedence_find(const LBison *bison, const char *key) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(bison->precedences); i++) {
    const LBisonPrecedence *precedence;

    precedence = (const LBisonPrecedence *)nexus_data_list_at_const(bison->precedences, i);

    if (nexus_strings_string_equals(precedence->key, key)) {
      return precedence;
    }
  }

  return NULL;
}

/* Summary: the precedence key of a symbol node (`symbol`): a token name, "'c" or "\"text". */
static char *l_bison_symbol_key(LBison *bison, const LParserGreenNode *symbol) {
  const LParserGreenNode *inner[2];
  const char             *kind;
  char                   *text;

  if (logos_languages_interchange_children(&bison->parsed, symbol, inner, 2) == 0) {
    return NULL;
  }

  kind = logos_languages_interchange_token_name(&bison->parsed, inner[0]);
  text = logos_languages_interchange_token_text(&bison->parsed, inner[0]);

  if (nexus_strings_string_equals(kind, "identifier")) {
    return text;
  }

  {
    uint_large length;
    char      *decoded;
    char      *key;

    decoded = l_bison_unquote(text, &length);
    key     = (char *)nexus_memory_allocate(length + 2);
    key[0]  = nexus_strings_string_equals(kind, "char_literal") ? '\'' : '"';
    nexus_memory_bytes_copy(decoded, key + 1, length + 1);

    /* A string that is the alias of a token has the precedence of the token. */
    if (key[0] == '"') {
      const LBisonToken *token;

      token = l_bison_token_alias_find(bison, decoded, length);

      if (token != NULL) {
        nexus_memory_deallocate(key);
        key = (char *)nexus_memory_allocate(nexus_strings_string_length_get(token->name) + 1);
        nexus_memory_bytes_copy(token->name, key, nexus_strings_string_length_get(token->name) + 1);
      }
    }

    nexus_memory_deallocate(decoded);
    nexus_memory_deallocate(text);

    return key;
  }
}

/* ---------------------------------------------------------------------------- */
/* DECLARATIONS                                                                  */
/* ---------------------------------------------------------------------------- */

static boolean l_bison_directive_is(const char *directive, const char *name) {
  return nexus_strings_string_equals(directive, name);
}

static void l_bison_declaration(LBison *bison, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  char                    *directive;
  boolean                  precedence_directive;
  LContextaAssociativity   associativity;
  int32                    level;
  LBisonToken             *pending;

  parts = logos_languages_interchange_children_get(&bison->parsed, node, &count);

  if (count == 0) {
    nexus_memory_deallocate((void *)parts);

    return;
  }

  directive     = logos_languages_interchange_token_text(&bison->parsed, parts[0]);
  associativity = LCPA_NONE;
  level         = 0;
  pending       = NULL;

  if (l_bison_directive_is(directive, "%left")) {
    associativity = LCPA_LEFT;
  } else if (l_bison_directive_is(directive, "%right")) {
    associativity = LCPA_RIGHT;
  } else if (l_bison_directive_is(directive, "%nonassoc") || l_bison_directive_is(directive, "%precedence")) {
    associativity = LCPA_NONASSOC;
  }

  precedence_directive = associativity != LCPA_NONE;

  if (precedence_directive) {
    bison->level++;
    level = bison->level;
  }

  if (l_bison_directive_is(directive, "%start")) {
    for (i = 1; i < count; i++) {
      const LParserGreenNode *inner[2];

      if (logos_languages_interchange_children(&bison->parsed, parts[i], inner, 2) > 0 &&
          nexus_strings_string_equals(logos_languages_interchange_token_name(&bison->parsed, inner[0]), "identifier")) {
        char *name;

        name         = logos_languages_interchange_token_text(&bison->parsed, inner[0]);
        bison->start = l_bison_copy(bison, name, nexus_strings_string_length_get(name));
        nexus_memory_deallocate(name);
        break;
      }
    }
  } else if (l_bison_directive_is(directive, "%token") || precedence_directive) {
    for (i = 1; i < count; i++) {
      const LParserGreenNode *inner[2];
      const char             *kind;
      char                   *text;

      if (logos_languages_interchange_children(&bison->parsed, parts[i], inner, 2) == 0) {
        continue;
      }

      if (logos_language_tooling_parsing_syntax_green_node_kind_get(inner[0]) != LPGK_TOKEN) {
        pending = NULL;
        continue;
      }

      kind = logos_languages_interchange_token_name(&bison->parsed, inner[0]);
      text = logos_languages_interchange_token_text(&bison->parsed, inner[0]);

      if (nexus_strings_string_equals(kind, "identifier")) {
        pending = l_bison_token_declare(bison, text);

        if (precedence_directive) {
          l_bison_precedence_set(bison, text, associativity, level);
        }
      } else if (nexus_strings_string_equals(kind, "integer")) {
        if (pending != NULL && nexus_strings_string_equals(text, "0")) {
          pending->eof = TRUE;
        }
      } else if (nexus_strings_string_equals(kind, "string_literal")) {
        uint_large length;
        char      *decoded;

        decoded = l_bison_unquote(text, &length);

        if (pending != NULL && pending->alias == NULL) {
          pending->alias = l_bison_copy(bison, decoded, length);
        } else if (precedence_directive) {
          char *key;

          key    = (char *)nexus_memory_allocate(length + 2);
          key[0] = '"';
          nexus_memory_bytes_copy(decoded, key + 1, length + 1);
          l_bison_precedence_set(bison, key, associativity, level);
          nexus_memory_deallocate(key);
        }

        nexus_memory_deallocate(decoded);
        pending = NULL;
      } else if (nexus_strings_string_equals(kind, "char_literal")) {
        uint_large length;
        char      *decoded;
        char      *key;

        decoded = l_bison_unquote(text, &length);
        key     = (char *)nexus_memory_allocate(length + 2);
        key[0]  = '\'';
        nexus_memory_bytes_copy(decoded, key + 1, length + 1);

        if (precedence_directive) {
          l_bison_precedence_set(bison, key, associativity, level);
        }

        nexus_memory_deallocate(key);
        nexus_memory_deallocate(decoded);
        pending = NULL;
      }

      nexus_memory_deallocate(text);
    }
  } else if (l_bison_directive_is(directive, "%glr-parser") || l_bison_directive_is(directive, "%dprec")) {
    bison->glr++;
  } else if (!l_bison_directive_is(directive, "%type") && !l_bison_directive_is(directive, "%union") && !l_bison_directive_is(directive, "%expect") &&
             !l_bison_directive_is(directive, "%expect-rr") && !l_bison_directive_is(directive, "%define") &&
             !l_bison_directive_is(directive, "%code")) {
    bison->ignored_declarations++;
  }

  nexus_memory_deallocate(directive);
  nexus_memory_deallocate((void *)parts);
}

/* ---------------------------------------------------------------------------- */
/* RULES                                                                         */
/* ---------------------------------------------------------------------------- */

static LBisonRule *l_bison_rule_get(LBison *bison, const char *name) {
  uint_large  i;
  LBisonRule *rule;

  for (i = 0; i < nexus_data_list_length_get(bison->rules); i++) {
    rule = (LBisonRule *)nexus_data_list_pointer_at(bison->rules, i);

    if (nexus_strings_string_equals(rule->name, name)) {
      return rule;
    }
  }

  rule               = (LBisonRule *)nexus_data_arena_allocate(&bison->grammar->arena, NEXUS_SIZEOF(*rule));
  rule->name         = l_bison_copy(bison, name, nexus_strings_string_length_get(name));
  rule->alternatives = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  (void)nexus_data_list_append_pointer(bison->rules, rule);

  return rule;
}

static void l_bison_item_add(LBisonAlternative *alternative, LINode *node, boolean is_rule) {
  (void)nexus_data_list_append_pointer(alternative->items, node);
  (void)nexus_data_list_append(alternative->is_rule, &is_rule);
}

static void l_bison_alternative_read(LBison *bison, LBisonRule *rule, const LParserGreenNode *node) {
  const LParserGreenNode **elements;
  uint32                   count;
  uint32                   i;
  LBisonAlternative       *alternative;
  char                    *last_key;
  char                    *prec_key;
  boolean                  has_prec_clause;
  uint32                   last_action;
  elements                   = logos_languages_interchange_children_get(&bison->parsed, node, &count);
  alternative                = (LBisonAlternative *)nexus_data_arena_allocate(&bison->grammar->arena, NEXUS_SIZEOF(*alternative));
  alternative->items         = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  alternative->is_rule       = nexus_data_list_create(NEXUS_SIZEOF(boolean), 4);
  alternative->associativity = LCPA_NONE;
  alternative->level         = 0;
  alternative->error         = FALSE;
  last_key                   = NULL;
  prec_key                   = NULL;
  has_prec_clause            = FALSE;
  last_action                = 0;

  /* The index of the last element, so that an action at the end is told from a mid-rule one. */
  for (i = 0; i < count; i++) {
    if (l_bison_is(bison, elements[i], "element")) {
      last_action = i;
    }
  }

  for (i = 0; i < count; i++) {
    const LParserGreenNode *parts[4];
    uint32                  part_count;
    uint32                  k;

    if (!l_bison_is(bison, elements[i], "element")) {
      continue;
    }

    part_count = logos_languages_interchange_children(&bison->parsed, elements[i], parts, 4);

    for (k = 0; k < part_count; k++) {
      if (l_bison_is(bison, parts[k], "symbol")) {
        const LParserGreenNode *inner[2];
        const char             *kind;
        char                   *text;

        if (logos_languages_interchange_children(&bison->parsed, parts[k], inner, 2) == 0) {
          continue;
        }

        kind = logos_languages_interchange_token_name(&bison->parsed, inner[0]);
        text = logos_languages_interchange_token_text(&bison->parsed, inner[0]);

        if (nexus_strings_string_equals(kind, "identifier")) {
          LBisonToken *token;

          if (nexus_strings_string_equals(text, "error")) {
            alternative->error = TRUE;
          } else {
            token = l_bison_token_find(bison, text);

            if (token != NULL && token->eof) {
              /* The end of the input needs no rule to match it. */
            } else if (token != NULL) {
              token->used = TRUE;
              l_bison_item_add(alternative, logos_languages_interchange_node_reference(bison->grammar, text), FALSE);

              if (last_key != NULL) {
                nexus_memory_deallocate(last_key);
              }

              last_key = l_bison_symbol_key(bison, parts[k]);
            } else {
              l_bison_item_add(alternative, logos_languages_interchange_node_reference(bison->grammar, text), TRUE);
            }
          }
        } else {
          uint_large   length;
          char        *decoded;
          LBisonToken *token;

          decoded = l_bison_unquote(text, &length);
          token   = nexus_strings_string_equals(kind, "string_literal") ? l_bison_token_alias_find(bison, decoded, length) : NULL;

          if (token != NULL) {
            token->used = TRUE;
            l_bison_item_add(alternative, logos_languages_interchange_node_reference(bison->grammar, token->name), FALSE);
          } else {
            l_bison_item_add(alternative, logos_languages_interchange_node_literal(bison->grammar, decoded, length, FALSE), FALSE);
          }

          if (last_key != NULL) {
            nexus_memory_deallocate(last_key);
          }

          last_key = l_bison_symbol_key(bison, parts[k]);
          nexus_memory_deallocate(decoded);
        }

        nexus_memory_deallocate(text);
      } else if (l_bison_is(bison, parts[k], "action_block")) {
        if (i == last_action) {
          bison->actions++;
        } else {
          bison->mid_rule_actions++;
        }
      } else if (l_bison_is(bison, parts[k], "predicate_block")) {
        bison->predicates++;
      } else if (l_bison_is(bison, parts[k], "precedence_clause")) {
        const LParserGreenNode *clause[3];

        if (logos_languages_interchange_children(&bison->parsed, parts[k], clause, 3) >= 2 && l_bison_is(bison, clause[1], "symbol")) {
          prec_key        = l_bison_symbol_key(bison, clause[1]);
          has_prec_clause = TRUE;
        }
      } else if (l_bison_is(bison, parts[k], "dprec_clause") || l_bison_is(bison, parts[k], "merge_clause")) {
        bison->glr++;
      }
    }
  }

  {
    const char *key;

    key = has_prec_clause ? prec_key : last_key;

    if (key != NULL) {
      const LBisonPrecedence *precedence;

      precedence = l_bison_precedence_find(bison, key);

      if (precedence != NULL) {
        alternative->associativity = precedence->associativity;
        alternative->level         = precedence->level;
      }
    }
  }

  if (last_key != NULL) {
    nexus_memory_deallocate(last_key);
  }

  if (prec_key != NULL) {
    nexus_memory_deallocate(prec_key);
  }

  nexus_memory_deallocate((void *)elements);

  (void)nexus_data_list_append_pointer(rule->alternatives, alternative);
}

/* Summary: reads one rule node; its alternatives are added to the rule of its left-hand side. */
static void l_bison_rule_read(LBison *bison, const LParserGreenNode *node) {
  const LParserGreenNode **parts;
  uint32                   count;
  uint32                   i;
  LBisonRule              *rule;

  parts = logos_languages_interchange_children_get(&bison->parsed, node, &count);
  rule  = NULL;

  for (i = 0; i < count; i++) {
    if (rule == NULL && logos_language_tooling_parsing_syntax_green_node_kind_get(parts[i]) == LPGK_TOKEN &&
        nexus_strings_string_equals(logos_languages_interchange_token_name(&bison->parsed, parts[i]), "identifier")) {
      char *name;

      name = logos_languages_interchange_token_text(&bison->parsed, parts[i]);
      rule = l_bison_rule_get(bison, name);
      nexus_memory_deallocate(name);
    } else if (l_bison_is(bison, parts[i], "alternatives") && rule != NULL) {
      const LParserGreenNode **alternatives;
      uint32                   alternative_count;
      uint32                   k;

      alternatives = logos_languages_interchange_children_get(&bison->parsed, parts[i], &alternative_count);

      for (k = 0; k < alternative_count; k++) {
        if (l_bison_is(bison, alternatives[k], "alternative")) {
          l_bison_alternative_read(bison, rule, alternatives[k]);
        }
      }

      nexus_memory_deallocate((void *)alternatives);
    }
  }

  nexus_memory_deallocate((void *)parts);
}

/* ---------------------------------------------------------------------------- */
/* DEFINITIONS                                                                   */
/* ---------------------------------------------------------------------------- */

static boolean l_bison_rule_exists(const LBison *bison, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(bison->rules); i++) {
    if (nexus_strings_string_equals(((const LBisonRule *)nexus_data_list_pointer_at(bison->rules, i))->name, name)) {
      return TRUE;
    }
  }

  return FALSE;
}

static boolean l_bison_name_taken(const LBison *bison, const char *name) {
  uint_large i;

  if (l_bison_rule_exists(bison, name) || l_bison_token_find(bison, name) != NULL) {
    return TRUE;
  }

  for (i = 0; i < nexus_data_list_length_get(bison->grammar->definitions); i++) {
    if (nexus_strings_string_equals(((const LIDefinition *)nexus_data_list_pointer_at(bison->grammar->definitions, i))->name, name)) {
      return TRUE;
    }
  }

  return FALSE;
}

/* Summary: the node of an alternative: its items in sequence, or a reference to a rule of its own when it carries a precedence the engine can apply. */
static LINode *l_bison_alternative_node(LBison *bison, const LBisonRule *rule, const LBisonAlternative *alternative, uint32 *counter) {
  uint_large count;
  uint_large i;
  LINode   **items;
  LINode    *sequence;
  boolean    left_open;
  boolean    right_open;

  count = nexus_data_list_length_get(alternative->items);

  if (count == 0) {
    return logos_languages_interchange_node_new(bison->grammar, LIK_EMPTY);
  }

  items = (LINode **)nexus_memory_allocate(count * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  for (i = 0; i < count; i++) {
    items[i] = (LINode *)nexus_data_list_pointer_at(alternative->items, i);
  }

  sequence = logos_languages_interchange_node_group(bison->grammar, LIK_SEQ, items, (uint32)count);
  nexus_memory_deallocate((void *)items);

  if (alternative->associativity == LCPA_NONE) {
    return sequence;
  }

  left_open  = count >= 2 && *(const boolean *)nexus_data_list_at_const(alternative->is_rule, 0);
  right_open = count >= 2 && *(const boolean *)nexus_data_list_at_const(alternative->is_rule, count - 1);

  if (!left_open && !right_open) {
    bison->inert_precedences++;

    return sequence;
  }

  {
    char          name[160];
    LIDefinition *definition;

    do {
      (*counter)++;
      (void)stbsp_snprintf(name, (int)sizeof(name), "%s_alt%u", rule->name, (unsigned)*counter);
    } while (l_bison_name_taken(bison, name));

    definition                = logos_languages_interchange_grammar_define(bison->grammar, name, LIC_RULE, sequence);
    definition->associativity = alternative->associativity;
    definition->level         = alternative->level;

    return logos_languages_interchange_node_reference(bison->grammar, definition->name);
  }
}

static boolean l_bison_scanner_defines(const NexusDataList *scanner_tokens, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(scanner_tokens); i++) {
    if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(scanner_tokens, i), name)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void l_bison_define(LBison *bison, const NexusDataList *scanner_tokens) {
  uint_large i;
  uint_large k;

  for (i = 0; i < nexus_data_list_length_get(bison->rules); i++) {
    const LBisonRule *rule;
    LINode          **nodes;
    uint32            used;
    uint32            counter;

    rule    = (const LBisonRule *)nexus_data_list_pointer_at(bison->rules, i);
    nodes   = (LINode **)nexus_memory_allocate(((uint_large)nexus_data_list_length_get(rule->alternatives) + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
    used    = 0;
    counter = 0;

    for (k = 0; k < nexus_data_list_length_get(rule->alternatives); k++) {
      const LBisonAlternative *alternative;
      uint_large               item;

      alternative = (const LBisonAlternative *)nexus_data_list_pointer_at(rule->alternatives, k);

      if (alternative->error) {
        bison->error_alternatives++;
        continue;
      }

      /* Every nonterminal an alternative names must have rules. */
      for (item = 0; item < nexus_data_list_length_get(alternative->items); item++) {
        if (*(const boolean *)nexus_data_list_at_const(alternative->is_rule, item)) {
          const LINode *node;

          node = (const LINode *)nexus_data_list_pointer_at(alternative->items, item);

          if (!l_bison_rule_exists(bison, node->text)) {
            logos_languages_interchange_grammar_note(bison->grammar, LIN_ERROR, "`%s` is used in the rule `%s` but is neither a token nor defined",
                                                     node->text, rule->name);
            bison->ok = FALSE;
          }
        }
      }

      nodes[used++] = l_bison_alternative_node(bison, rule, alternative, &counter);
    }

    (void)logos_languages_interchange_grammar_define(bison->grammar, rule->name, LIC_RULE,
                                                     used == 0 ? logos_languages_interchange_node_new(bison->grammar, LIK_EMPTY)
                                                               : logos_languages_interchange_node_group(bison->grammar, LIK_ALT, nodes, used));
    nexus_memory_deallocate((void *)nodes);
  }

  /* The tokens the rules use that the scanner does not define. */
  for (i = 0; i < nexus_data_list_length_get(bison->tokens); i++) {
    const LBisonToken *token;

    token = (const LBisonToken *)nexus_data_list_at_const(bison->tokens, i);

    if (!token->used || token->eof || l_bison_scanner_defines(scanner_tokens, token->name)) {
      continue;
    }

    if (token->alias != NULL && !nexus_strings_string_contains(token->alias, " ") && token->alias[0] != '\0') {
      LINode *literal;

      literal = logos_languages_interchange_node_literal(bison->grammar, token->alias, nexus_strings_string_length_get(token->alias), FALSE);
      (void)logos_languages_interchange_grammar_define(bison->grammar, token->name, LIC_TOKEN, literal);
      logos_languages_interchange_grammar_note(
          bison->grammar, LIN_WARNING, "no scanner defines the token `%s`; its alias \"%s\" is taken for its text", token->name, token->alias);
    } else {
      logos_languages_interchange_grammar_note(
          bison->grammar, LIN_ERROR, "the token `%s` is used by the grammar but no scanner defines it; give the flex file (.l) as well", token->name);
      bison->ok = FALSE;
    }
  }
}

/* ---------------------------------------------------------------------------- */
/* ENTRY                                                                         */
/* ---------------------------------------------------------------------------- */

static boolean l_bison_is_scanner(const char *name) {
  uint_large length;

  length = nexus_strings_string_length_get(name);

  return (length > 2 && nexus_strings_string_equals(name + length - 2, ".l")) ||
         (length > 3 && nexus_strings_string_equals(name + length - 3, ".ll")) ||
         (length > 4 && nexus_strings_string_equals(name + length - 4, ".lex")) ||
         (length > 5 && nexus_strings_string_equals(name + length - 5, ".flex"));
}

static void l_bison_file_read(LBison *bison, const LParserGreenNode *top, boolean declarations_pass) {
  const LParserGreenNode **roots;
  const LParserGreenNode **children;
  uint32                   root_count;
  uint32                   count;
  uint32                   i;

  roots    = logos_languages_interchange_children_get(&bison->parsed, top, &root_count);
  children = logos_languages_interchange_children_get(&bison->parsed, root_count > 0 ? roots[0] : top, &count);

  for (i = 0; i < count; i++) {
    if (declarations_pass && l_bison_is(bison, children[i], "declaration")) {
      const LParserGreenNode *inner[2];

      if (logos_languages_interchange_children(&bison->parsed, children[i], inner, 2) > 0 && l_bison_is(bison, inner[0], "directive_declaration")) {
        l_bison_declaration(bison, inner[0]);
      }
    } else if (!declarations_pass && l_bison_is(bison, children[i], "rule")) {
      l_bison_rule_read(bison, children[i]);
    }
  }

  nexus_memory_deallocate((void *)children);
  nexus_memory_deallocate((void *)roots);
}

boolean logos_languages_interchange_bison_convert(LGenesisCompiler *compiler, LIGrammar *grammar, const LInterchangeInput *inputs,
                                                  uint32 input_count) {
  LBison          bison;
  LIParsed       *parsed;
  LGenesisEngine *engine;
  NexusDataList  *scanner_tokens;
  uint32          i;
  uint32          grammars;

  bison.compiler             = compiler;
  bison.grammar              = grammar;
  bison.ok                   = TRUE;
  bison.tokens               = nexus_data_list_create(NEXUS_SIZEOF(LBisonToken), 32);
  bison.precedences          = nexus_data_list_create(NEXUS_SIZEOF(LBisonPrecedence), 16);
  bison.rules                = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  bison.start                = NULL;
  bison.level                = 0;
  bison.actions              = 0;
  bison.mid_rule_actions     = 0;
  bison.predicates           = 0;
  bison.error_alternatives   = 0;
  bison.inert_precedences    = 0;
  bison.glr                  = 0;
  bison.ignored_declarations = 0;
  scanner_tokens             = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  parsed                     = (LIParsed *)nexus_memory_allocate(((uint_large)input_count + 1) * NEXUS_SIZEOF(*parsed));
  nexus_memory_bytes_clear(parsed, ((uint_large)input_count + 1) * NEXUS_SIZEOF(*parsed));
  grammars = 0;

  for (i = 0; i < input_count; i++) {
    if (!l_bison_is_scanner(inputs[i].name)) {
      grammars++;
    }
  }

  if (grammars == 0) {
    logos_languages_interchange_grammar_note(grammar, LIN_ERROR, "give the yacc or Bison grammar (.y) as well as the scanner");
    bison.ok = FALSE;
  }

  /* The scanners first: the tokens they define and the fragments they name exist before the rules are read. */
  for (i = 0; i < input_count && bison.ok; i++) {
    if (l_bison_is_scanner(inputs[i].name) && !logos_languages_interchange_flex_read(compiler, grammar, &inputs[i], scanner_tokens)) {
      logos_languages_interchange_grammar_note(grammar, LIN_NOTE, "in %s", inputs[i].name);
      bison.ok = FALSE;
    }
  }

  engine = bison.ok ? logos_languages_interchange_reader_open(compiler, grammar, "Bison") : NULL;

  for (i = 0; i < input_count && bison.ok && engine != NULL; i++) {
    if (!l_bison_is_scanner(inputs[i].name) &&
        !logos_languages_interchange_reader_parse(engine, grammar, inputs[i].text, inputs[i].size, &parsed[i])) {
      logos_languages_interchange_grammar_note(grammar, LIN_NOTE, "in %s", inputs[i].name);
      bison.ok = FALSE;
    }
  }

  if (engine == NULL) {
    bison.ok = FALSE;
  }

  /* The declarations of every file first (the rules look tokens and precedences up), then the rules. */
  for (i = 0; i < input_count && bison.ok; i++) {
    if (parsed[i].tree != NULL) {
      bison.parsed = parsed[i];
      l_bison_file_read(&bison, logos_language_tooling_parsing_syntax_tree_root_get(parsed[i].tree), TRUE);
    }
  }

  for (i = 0; i < input_count && bison.ok; i++) {
    if (parsed[i].tree != NULL) {
      bison.parsed = parsed[i];
      l_bison_file_read(&bison, logos_language_tooling_parsing_syntax_tree_root_get(parsed[i].tree), FALSE);
    }
  }

  if (bison.ok) {
    l_bison_define(&bison, scanner_tokens);
  }

  if (bison.ok) {
    if (bison.start != NULL) {
      if (l_bison_rule_exists(&bison, bison.start)) {
        grammar->entry = bison.start;
      } else {
        logos_languages_interchange_grammar_note(grammar, LIN_ERROR, "%%start names `%s`, which has no rules", bison.start);
        bison.ok = FALSE;
      }
    } else if (nexus_data_list_length_get(bison.rules) > 0) {
      grammar->entry = ((const LBisonRule *)nexus_data_list_pointer_at(bison.rules, 0))->name;
    }
  }

  if (bison.actions > 0 || bison.mid_rule_actions > 0) {
    logos_languages_interchange_grammar_note(grammar, LIN_NOTE, "%u actions (C code) are dropped, %u of them in the middle of a rule",
                                             (unsigned)(bison.actions + bison.mid_rule_actions), (unsigned)bison.mid_rule_actions);
  }

  if (bison.predicates > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_WARNING, "%u semantic predicates are ignored, so the language accepts what they would have refused", (unsigned)bison.predicates);
  }

  if (bison.error_alternatives > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_WARNING, "%u alternatives that use the `error` token (error recovery) are left out; the language is that of correct input",
        (unsigned)bison.error_alternatives);
  }

  if (bison.inert_precedences > 0) {
    logos_languages_interchange_grammar_note(grammar, LIN_WARNING,
                                             "%u rules have a precedence that settles a conflict between rules that are not operators (a dangling "
                                             "else, for example); the Earley engine keeps "
                                             "both derivations, which accepts the same sentences",
                                             (unsigned)bison.inert_precedences);
  }

  if (bison.glr > 0) {
    logos_languages_interchange_grammar_note(grammar, LIN_NOTE,
                                             "%%dprec, %%merge and %%glr-parser only choose among derivations; the Earley engine keeps all");
  }

  if (bison.ok && nexus_data_list_length_get(bison.precedences) > 0) {
    logos_languages_interchange_grammar_note(
        grammar, LIN_NOTE, "operator precedence is applied by the Earley engine to the rules written with @prec, as yacc resolves its conflicts");
  }

  for (i = 0; i < input_count; i++) {
    logos_languages_interchange_parsed_release(&parsed[i]);
  }

  if (engine != NULL) {
    logos_languages_genesis_engine_release(engine);
  }

  for (i = 0; i < nexus_data_list_length_get(bison.rules); i++) {
    const LBisonRule *rule;
    uint_large        k;

    rule = (const LBisonRule *)nexus_data_list_pointer_at(bison.rules, i);

    for (k = 0; k < nexus_data_list_length_get(rule->alternatives); k++) {
      const LBisonAlternative *alternative;

      alternative = (const LBisonAlternative *)nexus_data_list_pointer_at(rule->alternatives, k);
      nexus_data_list_destroy(alternative->items);
      nexus_data_list_destroy(alternative->is_rule);
    }

    nexus_data_list_destroy(rule->alternatives);
  }

  nexus_memory_deallocate(parsed);
  nexus_data_list_destroy(bison.tokens);
  nexus_data_list_destroy(bison.precedences);
  nexus_data_list_destroy(bison.rules);
  nexus_data_list_destroy(scanner_tokens);

  return bison.ok;
}
