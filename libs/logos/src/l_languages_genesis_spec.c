#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
The words and punctuation of Genesis, in one place. The parser's lexer and parser read these tables, and the genesis tool
writes the grammar and the editor mapping of Genesis itself from them, so a new keyword or operator cannot exist in the
compiler without existing in the grammar.
*/

static const LGenesisSpecWord l_genesis_spec_words[LGSW_COUNT] = {
    {"Language", "module"},   {"Library", "module"},  {"Version", "module"},       {"IMPORT", "namespace"},   {"AS", "namespace"},
    {"ENUM", "enum"},         {"PARSER", "struct"},   {"CONTEXT", "struct"},       {"PRATT", "struct"},       {"EMBED", "struct"},
    {"TEMPLATE", "function"}, {"atom", "pratt"},      {"prefix", "pratt"},         {"infix", "pratt"},        {"postfix", "pratt"},
    {"push", "action"},       {"pop", "action"},      {"set", "action"},           {"replace", "action"},     {"none", "constant"},
    {"@if", "condition"},     {"@else", "condition"}, {"@priority", "annotation"}, {"@choice", "annotation"}, {"@exclude", "annotation"},
    {"@prec", "annotation"}};

static const LGenesisSpecPunctuation l_genesis_spec_punctuation[LGSP_COUNT] = {
    {"lbrace", "{", "open", "braces"},     {"rbrace", "}", "close", "braces"},     {"lparen", "(", "open", "parens"},
    {"rparen", ")", "close", "parens"},    {"langle", "<", "open", "angles"},      {"rangle", ">", "close", "angles"},
    {"lbracket", "[", "open", "brackets"}, {"rbracket", "]", "close", "brackets"}, {"colon", ":", "separator", ""},
    {"comma", ",", "separator", ""},       {"arrow", "->", "operator", ""},        {"equals_equals", "==", "operator", ""},
    {"not_equals", "!=", "operator", ""},  {"equals", "=", "operator", ""},        {"question", "?", "operator", ""},
    {"star", "*", "operator", ""},         {"plus", "+", "operator", ""},          {"minus", "-", "operator", ""},
    {"dot_dot", "..", "operator", ""},     {"dot", ".", "operator", ""},           {"union", "||", "operator", ""},
    {"bar", "|", "operator", ""},          {"bang", "!", "operator", ""}};

uint32 logos_languages_genesis_spec_word_count_get(void) {
  return LGSW_COUNT;
}

const LGenesisSpecWord *logos_languages_genesis_spec_word_at(uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(index < LGSW_COUNT, "word index must be in bounds");

  return &l_genesis_spec_words[index];
}

uint32 logos_languages_genesis_spec_punctuation_count_get(void) {
  return LGSP_COUNT;
}

const LGenesisSpecPunctuation *logos_languages_genesis_spec_punctuation_at(uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(index < LGSP_COUNT, "punctuation index must be in bounds");

  return &l_genesis_spec_punctuation[index];
}

const char *logos_languages_genesis_spec_word_text_get(LGSpecWordId word) {
  return l_genesis_spec_words[word].text;
}

uint32 logos_languages_genesis_spec_punctuation_match(const byte *text, uint_large size, LGSpecPunctuationId *out_id) {
  uint32 best_length;
  uint32 index;
  uint32 length;

  best_length = 0;

  for (index = 0; index < LGSP_COUNT; index++) {
    length = (uint32)nexus_strings_string_length_get(l_genesis_spec_punctuation[index].text);

    if (length > best_length && length <= size && nexus_memory_bytes_equal(text, (const byte *)l_genesis_spec_punctuation[index].text, length)) {
      best_length = length;
      *out_id     = (LGSpecPunctuationId)index;
    }
  }

  return best_length;
}

/* ---------------------------------------------------------------------------- */
/* THE LANGUAGE OF GENESIS                                                       */
/* ---------------------------------------------------------------------------- */

/*
Genesis as a Logos language definition: its tokens as regexps (the words and punctuation from the tables above plus the
lexical shapes the lexer hard-codes) and its rules as Contexta operations. The grammar is tolerant: it knows the words,
tokens, literals and groups of Genesis, not the precedence of expressions or where a definition ends, which an editor
does not need. The genesis tool exports this definition to Genesis.gen.
*/

#define LG_SPEC_IDENT_START "[A-Za-z_]"
#define LG_SPEC_IDENT       "[A-Za-z_][A-Za-z0-9_]*"

typedef struct LGSpecBuild {
  LGenesisDefinition *definition;
  LContexta          *contexta;
} LGSpecBuild;

static const LContextaOperation *l_spec_token(const LGSpecBuild *build, LLexerTokenKind kind) {
  return logos_languages_contexta_contexta_operation_add_token(NULL, build->contexta, kind);
}

static const LContextaOperation *l_spec_reference(const LGSpecBuild *build, const LContextaRule *rule) {
  return logos_languages_contexta_contexta_operation_add_reference(NULL, build->contexta, rule);
}

static const LContextaOperation *l_spec_concatenate(const LGSpecBuild *build, const LContextaOperation *left, const LContextaOperation *right) {
  return logos_languages_contexta_contexta_operation_add_concatenate(NULL, build->contexta, left, right);
}

static const LContextaOperation *l_spec_union(const LGSpecBuild *build, const LContextaOperation *left, const LContextaOperation *right) {
  return logos_languages_contexta_contexta_operation_add_unionize(NULL, build->contexta, left, right);
}

static const LContextaOperation *l_spec_star(const LGSpecBuild *build, const LContextaOperation *operation) {
  return logos_languages_contexta_contexta_operation_add_star(NULL, build->contexta, operation);
}

/* Summary: the union of tokens. */
static const LContextaOperation *l_spec_kinds(const LGSpecBuild *build, const LLexerTokenKind *kinds, uint32 count) {
  const LContextaOperation *result;
  uint32                    i;

  result = l_spec_token(build, kinds[0]);

  for (i = 1; i < count; i++) {
    result = l_spec_union(build, result, l_spec_token(build, kinds[i]));
  }

  return result;
}

/* Summary: text as a regexp that matches exactly it: letters, digits and underscore stay, every other byte is \xHH. */
static void l_spec_literal(const char *text, char *out, uint_large size) {
  static const char digits[] = "0123456789ABCDEF";
  uint_large        position;
  uint_large        i;

  position = 0;

  for (i = 0; text[i] != '\0' && position + 5 < size; i++) {
    byte value;

    value = (byte)text[i];

    if ((value >= '0' && value <= '9') || (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || value == '_') {
      out[position++] = (char)value;
    } else {
      out[position++] = '\\';
      out[position++] = 'x';
      out[position++] = digits[value >> 4];
      out[position++] = digits[value & 0x0F];
    }
  }

  out[position] = '\0';
}

LGenesisDefinition *logos_languages_genesis_spec_definition_create(void) {
  static const char *const  roles[] = {"module", "namespace", "enum", "struct", "function", "pratt", "action", "constant", "condition", "annotation"};
  static const char *const  groups[] = {"braces", "parens", "angles", "brackets"};
  LGSpecBuild               build;
  LLexerTokenKind           word_kinds[LGSW_COUNT];
  LLexerTokenKind           punctuation_kinds[LGSP_COUNT];
  LLexerTokenKind           kinds[LGSW_COUNT + LGSP_COUNT];
  LLexerTokenKind           whitespace;
  LLexerTokenKind           line_comment;
  LLexerTokenKind           comment_open;
  LLexerTokenKind           comment_close;
  LLexerTokenKind           comment_text;
  LLexerTokenKind           comment_star;
  LLexerTokenKind           annotation;
  LLexerTokenKind           param_ref;
  LLexerTokenKind           fragment_ref;
  LLexerTokenKind           token_ref;
  LLexerTokenKind           rule_ref;
  LLexerTokenKind           identifier;
  LLexerTokenKind           integer;
  LLexerTokenKind           single_quoted;
  LLexerTokenKind           double_quoted;
  LContextaRule            *trivia;
  LContextaRule            *block_comment;
  LContextaRule            *source;
  LContextaRule            *item;
  LContextaRule            *group_rules[4];
  LContextaRule            *word;
  LContextaRule            *role_rules[10];
  LContextaRule            *operator_rule;
  LContextaRule            *separator_rule;
  LContextaRule            *reference;
  LContextaRule            *literal;
  const LContextaOperation *alternatives;
  uint32                    i;
  uint32                    j;
  uint32                    count;
  char                      name[96];
  char                      regexp[160];

  build.definition = logos_languages_genesis_definition_create("Genesis", "0.1.0");
  build.contexta   = logos_languages_genesis_definition_contexta_get(build.definition);

  /* Tokens: the lexical shapes the lexer hard-codes. */
  whitespace   = logos_languages_genesis_definition_token_add(build.definition, "whitespace", "ROOT", "[ \\t\\r\\n]+", 0, LGCA_NONE, NULL, 0);
  line_comment = logos_languages_genesis_definition_token_add(build.definition, "line_comment", "ROOT", "//[\\t\\x20-\\xFF]*", 0, LGCA_NONE, NULL, 0);
  comment_open =
      logos_languages_genesis_definition_token_add(build.definition, "block_comment_open", "ROOT", "/\\*", 0, LGCA_PUSH, "BLOCK_COMMENT", 0);

  /* Every word is a keyword that wins ties with an identifier. */
  for (i = 0; i < LGSW_COUNT; i++) {
    char        literal_text[64];
    char        lower[48];
    uint_large  length;
    const char *text;

    text   = l_genesis_spec_words[i].text[0] == '@' ? l_genesis_spec_words[i].text + 1 : l_genesis_spec_words[i].text;
    length = 0;

    for (j = 0; text[j] != '\0' && length + 1 < sizeof(lower); j++) {
      lower[length] = text[j];

      if (text[j] >= 'A' && text[j] <= 'Z') {
        lower[length] = (char)(text[j] - 'A' + 'a');
      }

      length++;
    }

    lower[length] = '\0';
    (void)stbsp_snprintf(name, (int)sizeof(name), "kw_%s", lower);
    l_spec_literal(l_genesis_spec_words[i].text, literal_text, sizeof(literal_text));
    word_kinds[i] = logos_languages_genesis_definition_token_add(build.definition, name, "ROOT", literal_text, 10, LGCA_NONE, NULL, 0);
  }

  annotation = logos_languages_genesis_definition_token_add(build.definition, "annotation", "ROOT", "@" LG_SPEC_IDENT, 0, LGCA_NONE, NULL, 0);
  param_ref  = logos_languages_genesis_definition_token_add(build.definition, "param_ref", "ROOT", "\\x24\\x24" LG_SPEC_IDENT, 0, LGCA_NONE, NULL, 0);
  fragment_ref = logos_languages_genesis_definition_token_add(build.definition, "fragment_ref", "ROOT", "\\x24_[A-Za-z0-9_]*", 0, LGCA_NONE, NULL, 0);
  token_ref    = logos_languages_genesis_definition_token_add(build.definition, "token_ref", "ROOT", "\\x24" LG_SPEC_IDENT, 0, LGCA_NONE, NULL, 0);
  rule_ref     = logos_languages_genesis_definition_token_add(build.definition, "rule_ref", "ROOT", "#" LG_SPEC_IDENT, 0, LGCA_NONE, NULL, 0);
  identifier   = logos_languages_genesis_definition_token_add(build.definition, "identifier", "ROOT", LG_SPEC_IDENT, 0, LGCA_NONE, NULL, 0);
  integer      = logos_languages_genesis_definition_token_add(build.definition, "integer", "ROOT", "[0-9]+", 0, LGCA_NONE, NULL, 0);
  single_quoted = logos_languages_genesis_definition_token_add(
      build.definition, "single_quoted", "ROOT", "\\x27([\\x09\\x20-\\x26\\x28-\\x5B\\x5D-\\xFF]|\\x5C[\\x20-\\xFF])*\\x27", 0, LGCA_NONE, NULL, 0);
  double_quoted = logos_languages_genesis_definition_token_add(
      build.definition, "double_quoted", "ROOT", "\\x22([\\x09\\x20\\x21\\x23-\\x5B\\x5D-\\xFF]|\\x5C[\\x20-\\xFF])*\\x22", 0, LGCA_NONE, NULL, 0);

  for (i = 0; i < LGSP_COUNT; i++) {
    l_spec_literal(l_genesis_spec_punctuation[i].text, regexp, sizeof(regexp));
    punctuation_kinds[i] =
        logos_languages_genesis_definition_token_add(build.definition, l_genesis_spec_punctuation[i].name, "ROOT", regexp, 0, LGCA_NONE, NULL, 0);
  }

  comment_close =
      logos_languages_genesis_definition_token_add(build.definition, "block_comment_close", "BLOCK_COMMENT", "\\*/", 0, LGCA_POP, NULL, 1);
  comment_text =
      logos_languages_genesis_definition_token_add(build.definition, "block_comment_text", "BLOCK_COMMENT", "[^*]+", 0, LGCA_NONE, NULL, 0);
  comment_star = logos_languages_genesis_definition_token_add(build.definition, "block_comment_star", "BLOCK_COMMENT", "\\*", 0, LGCA_NONE, NULL, 0);

  /* Rules, declared first so they can refer to each other. */
  trivia        = logos_languages_genesis_definition_rule_add(build.definition, "trivia");
  block_comment = logos_languages_genesis_definition_rule_add(build.definition, "block_comment");
  source        = logos_languages_genesis_definition_rule_add(build.definition, "source");

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(groups); i++) {
    group_rules[i] = logos_languages_genesis_definition_rule_add(build.definition, groups[i]);
  }

  item = logos_languages_genesis_definition_rule_add(build.definition, "item");

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(roles); i++) {
    (void)stbsp_snprintf(name, (int)sizeof(name), "word_%s", roles[i]);
    role_rules[i] = logos_languages_genesis_definition_rule_add(build.definition, name);
  }

  operator_rule  = logos_languages_genesis_definition_rule_add(build.definition, "operator");
  separator_rule = logos_languages_genesis_definition_rule_add(build.definition, "separator");
  reference      = logos_languages_genesis_definition_rule_add(build.definition, "reference");
  literal        = logos_languages_genesis_definition_rule_add(build.definition, "literal");
  word           = logos_languages_genesis_definition_rule_add(build.definition, "word");

  /* Trivia is kept in the syntax tree like everything else. */
  kinds[0]     = whitespace;
  kinds[1]     = line_comment;
  alternatives = l_spec_union(&build, l_spec_kinds(&build, kinds, 2), l_spec_reference(&build, block_comment));
  logos_languages_contexta_contexta_rule_define(build.contexta, trivia, alternatives);

  kinds[0] = comment_text;
  kinds[1] = comment_star;
  logos_languages_contexta_contexta_rule_define(
      build.contexta, block_comment,
      l_spec_concatenate(&build, l_spec_token(&build, comment_open),
                         l_spec_concatenate(&build, l_spec_star(&build, l_spec_kinds(&build, kinds, 2)), l_spec_token(&build, comment_close))));
  logos_languages_contexta_contexta_rule_define(build.contexta, source, l_spec_star(&build, l_spec_reference(&build, item)));

  /* A group is the name its open and close punctuation share. */
  alternatives = NULL;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(groups); i++) {
    LLexerTokenKind open;
    LLexerTokenKind close;

    open  = (LLexerTokenKind)0;
    close = (LLexerTokenKind)0;

    for (j = 0; j < LGSP_COUNT; j++) {
      if (nexus_strings_string_equals(l_genesis_spec_punctuation[j].group, groups[i])) {
        if (nexus_strings_string_equals(l_genesis_spec_punctuation[j].role, "open")) {
          open = punctuation_kinds[j];
        } else {
          close = punctuation_kinds[j];
        }
      }
    }

    logos_languages_contexta_contexta_rule_define(
        build.contexta, group_rules[i],
        l_spec_concatenate(&build, l_spec_token(&build, open),
                           l_spec_concatenate(&build, l_spec_star(&build, l_spec_reference(&build, item)), l_spec_token(&build, close))));
    alternatives = alternatives == NULL ? l_spec_reference(&build, group_rules[i])
                                        : l_spec_union(&build, alternatives, l_spec_reference(&build, group_rules[i]));
  }

  logos_languages_contexta_contexta_rule_define(build.contexta, item, l_spec_union(&build, alternatives, l_spec_reference(&build, word)));

  /* One rule per role of words, one for operators and one for separators. */
  alternatives = NULL;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(roles); i++) {
    count = 0;

    for (j = 0; j < LGSW_COUNT; j++) {
      if (nexus_strings_string_equals(l_genesis_spec_words[j].role, roles[i])) {
        kinds[count++] = word_kinds[j];
      }
    }

    if (count == 0) {
      logos_languages_contexta_contexta_rule_define(build.contexta, role_rules[i],
                                                    logos_languages_contexta_contexta_operation_add_empty_language(NULL, build.contexta));
      continue;
    }

    logos_languages_contexta_contexta_rule_define(build.contexta, role_rules[i], l_spec_kinds(&build, kinds, count));
    alternatives =
        alternatives == NULL ? l_spec_reference(&build, role_rules[i]) : l_spec_union(&build, alternatives, l_spec_reference(&build, role_rules[i]));
  }

  {
    static const char *const kinds_names[] = {"operator", "separator"};
    LContextaRule           *rules[2];

    rules[0] = operator_rule;
    rules[1] = separator_rule;

    for (i = 0; i < 2; i++) {
      count = 0;

      for (j = 0; j < LGSP_COUNT; j++) {
        if (nexus_strings_string_equals(l_genesis_spec_punctuation[j].role, kinds_names[i])) {
          kinds[count++] = punctuation_kinds[j];
        }
      }

      logos_languages_contexta_contexta_rule_define(build.contexta, rules[i], l_spec_kinds(&build, kinds, count));
      alternatives = l_spec_union(&build, alternatives, l_spec_reference(&build, rules[i]));
    }
  }

  kinds[0] = param_ref;
  kinds[1] = fragment_ref;
  kinds[2] = token_ref;
  kinds[3] = rule_ref;
  logos_languages_contexta_contexta_rule_define(build.contexta, reference, l_spec_kinds(&build, kinds, 4));
  kinds[0] = single_quoted;
  kinds[1] = double_quoted;
  kinds[2] = integer;
  logos_languages_contexta_contexta_rule_define(build.contexta, literal, l_spec_kinds(&build, kinds, 3));
  alternatives = l_spec_union(&build, alternatives, l_spec_reference(&build, reference));
  alternatives = l_spec_union(&build, alternatives, l_spec_reference(&build, literal));
  alternatives = l_spec_union(&build, alternatives, l_spec_token(&build, annotation));
  alternatives = l_spec_union(&build, alternatives, l_spec_token(&build, identifier));
  logos_languages_contexta_contexta_rule_define(build.contexta, word, alternatives);

  logos_languages_genesis_definition_entry_set(build.definition, source);
  logos_languages_genesis_definition_global_add(build.definition, trivia);

  return build.definition;
}
