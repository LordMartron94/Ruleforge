#include <nexus.h>
#include <logos.h>

#include "tool.h"

/*
The grammars the tool knows by heart: the language of Genesis itself (from the tables of the compiler's lexer and parser)
and the language of mapping files (from the form table of the mapping reader). Both go through the .gen writer, and the
editor mappings that style them are written from the same tables, so nothing here can drift from the code it describes.
*/

static const LToolMappingForm l_tool_mapping_forms[] = {{LTOOL_FORM_CONTEXT, "context", "CONTEXT_LINE"},
                                                        {LTOOL_FORM_RULE, "rule", "RULE_LINE"},
                                                        {LTOOL_FORM_SYMBOL, "symbol", "SYMBOL_LINE"},
                                                        {LTOOL_FORM_REFERENCE, "reference", "REFERENCE_LINE"}};

uint32 logos_tool_mapping_form_count_get(void) {
  return NEXUS_ARRAY_SIZE_ELEMENTS(l_tool_mapping_forms);
}

const LToolMappingForm *logos_tool_mapping_form_at(uint32 index) {
  NEXUS_PROGRAM_PRECONDITION(index < NEXUS_ARRAY_SIZE_ELEMENTS(l_tool_mapping_forms), "form index must be in bounds");

  return &l_tool_mapping_forms[index];
}

const LToolMappingForm *logos_tool_mapping_form_find(const char *text, uint_large length) {
  uint32 i;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(l_tool_mapping_forms); i++) {
    if (nexus_strings_string_length_get(l_tool_mapping_forms[i].keyword) == length &&
        nexus_memory_bytes_equal(text, l_tool_mapping_forms[i].keyword, length)) {
      return &l_tool_mapping_forms[i];
    }
  }

  return NULL;
}

/* ---------------------------------------------------------------------------- */
/* THE LANGUAGE OF MAPPING FILES                                                 */
/* ---------------------------------------------------------------------------- */

#define LTOOL_NAME  "[A-Za-z_][A-Za-z0-9_.\\x2D]*"
#define LTOOL_BLANK "[ \\t\\r]+"

typedef struct LToolBuild {
  LGenesisDefinition *definition;
  LContexta          *contexta;
} LToolBuild;

static const LContextaOperation *l_tool_token(const LToolBuild *build, LLexerTokenKind kind) {
  return logos_languages_contexta_contexta_operation_add_token(NULL, build->contexta, kind);
}

static const LContextaOperation *l_tool_concatenate(const LToolBuild *build, const LContextaOperation *left, const LContextaOperation *right) {
  return logos_languages_contexta_contexta_operation_add_concatenate(NULL, build->contexta, left, right);
}

static const LContextaOperation *l_tool_union(const LToolBuild *build, const LContextaOperation *left, const LContextaOperation *right) {
  return logos_languages_contexta_contexta_operation_add_unionize(NULL, build->contexta, left, right);
}

static const LContextaOperation *l_tool_optional(const LToolBuild *build, const LContextaOperation *operation) {
  return logos_languages_contexta_contexta_operation_add_optional(NULL, build->contexta, operation);
}

static const LContextaOperation *l_tool_star(const LToolBuild *build, const LContextaOperation *operation) {
  return logos_languages_contexta_contexta_operation_add_star(NULL, build->contexta, operation);
}

/* Summary: tokens in order, concatenated; a leading NULL-free list of operations. */
static const LContextaOperation *l_tool_sequence(const LToolBuild *build, const LContextaOperation *const *items, uint32 count) {
  const LContextaOperation *result;
  uint32                    i;

  result = items[count - 1];

  for (i = count - 1; i > 0; i--) {
    result = l_tool_concatenate(build, items[i - 1], result);
  }

  return result;
}

/* Summary: a line context: blanks and the end of the line are in every one. */
static LLexerTokenKind l_tool_line_tokens(LGenesisDefinition *definition, const char *context) {
  (void)logos_languages_genesis_definition_token_add(definition, "blank", context, LTOOL_BLANK, 0, LGCA_NONE, NULL, 0);

  return logos_languages_genesis_definition_token_add(definition, "line_end", context, "\\n", 0, LGCA_POP, NULL, 1);
}

LGenesisDefinition *logos_tool_spec_mapping_definition_create(void) {
  LToolBuild                build;
  LLexerTokenKind           blank;
  LLexerTokenKind           newline;
  LLexerTokenKind           comment;
  LLexerTokenKind           token_ref;
  LLexerTokenKind           setting_key;
  LLexerTokenKind           assign_scope;
  LLexerTokenKind           assign_text;
  LLexerTokenKind           scope;
  LLexerTokenKind           setting_value;
  LLexerTokenKind           context_name;
  LLexerTokenKind           rule_ref;
  LLexerTokenKind           greater;
  LLexerTokenKind           option_key;
  LLexerTokenKind           option_value;
  LLexerTokenKind           line_end;
  LLexerTokenKind           keywords[8];
  LContextaRule            *blank_rule;
  LContextaRule            *mapping;
  LContextaRule            *statement;
  LContextaRule            *token_scope;
  LContextaRule            *setting;
  LContextaRule            *symbol_option;
  LContextaRule            *forms[8];
  const LContextaOperation *alternatives;
  const LContextaOperation *items[8];
  uint32                    i;
  char                      name[64];
  char                      regexp[64];

  build.definition = logos_languages_genesis_definition_create("GenesisMapping", "0.1.0");
  build.contexta   = logos_languages_genesis_definition_contexta_get(build.definition);

  /* Where a line starts: the first word selects the context the rest of the line is read in. */
  blank   = logos_languages_genesis_definition_token_add(build.definition, "blank", "ROOT", LTOOL_BLANK, 0, LGCA_NONE, NULL, 0);
  newline = logos_languages_genesis_definition_token_add(build.definition, "newline", "ROOT", "\\n", 0, LGCA_NONE, NULL, 0);
  comment = logos_languages_genesis_definition_token_add(build.definition, "comment", "ROOT", "//[\\t\\x20-\\xFF]*", 0, LGCA_NONE, NULL, 0);

  for (i = 0; i < logos_tool_mapping_form_count_get(); i++) {
    const LToolMappingForm *form;

    form = logos_tool_mapping_form_at(i);
    (void)stbsp_snprintf(name, (int)sizeof(name), "kw_%s", form->keyword);
    (void)stbsp_snprintf(regexp, (int)sizeof(regexp), "%s", form->keyword);
    keywords[i] = logos_languages_genesis_definition_token_add(build.definition, name, "ROOT", regexp, 10, LGCA_PUSH, form->line_context, 0);
  }

  token_ref = logos_languages_genesis_definition_token_add(build.definition, "token_ref", "ROOT", "\\x24" LTOOL_NAME, 0, LGCA_PUSH, "SCOPE_LINE", 0);
  setting_key = logos_languages_genesis_definition_token_add(build.definition, "setting_key", "ROOT", LTOOL_NAME, 0, LGCA_PUSH, "SETTING_LINE", 0);

  /* $token = scope */
  line_end     = l_tool_line_tokens(build.definition, "SCOPE_LINE");
  assign_scope = logos_languages_genesis_definition_token_add(build.definition, "assign_scope", "SCOPE_LINE", "=", 0, LGCA_SET, "SCOPE_VALUE", 0);

  /* context NAME = scope */
  l_tool_line_tokens(build.definition, "CONTEXT_LINE");
  context_name = logos_languages_genesis_definition_token_add(build.definition, "context_name", "CONTEXT_LINE", LTOOL_NAME, 0, LGCA_NONE, NULL, 0);
  (void)logos_languages_genesis_definition_token_add(build.definition, "assign_scope", "CONTEXT_LINE", "=", 0, LGCA_SET, "SCOPE_VALUE", 0);

  /* rule #name = scope, rule #parent > #child = scope */
  l_tool_line_tokens(build.definition, "RULE_LINE");
  rule_ref = logos_languages_genesis_definition_token_add(build.definition, "rule_ref", "RULE_LINE", "#" LTOOL_NAME, 0, LGCA_NONE, NULL, 0);
  greater  = logos_languages_genesis_definition_token_add(build.definition, "greater", "RULE_LINE", ">", 0, LGCA_NONE, NULL, 0);
  (void)logos_languages_genesis_definition_token_add(build.definition, "assign_scope", "RULE_LINE", "=", 0, LGCA_SET, "SCOPE_VALUE", 0);

  /* symbol #rule name=$token kind=function */
  l_tool_line_tokens(build.definition, "SYMBOL_LINE");
  (void)logos_languages_genesis_definition_token_add(build.definition, "rule_ref", "SYMBOL_LINE", "#" LTOOL_NAME, 0, LGCA_NONE, NULL, 0);
  option_key = logos_languages_genesis_definition_token_add(build.definition, "option_key", "SYMBOL_LINE", LTOOL_NAME "=", 0, LGCA_NONE, NULL, 0);
  (void)logos_languages_genesis_definition_token_add(build.definition, "token_ref", "SYMBOL_LINE", "\\x24" LTOOL_NAME, 0, LGCA_NONE, NULL, 0);
  option_value = logos_languages_genesis_definition_token_add(build.definition, "option_value", "SYMBOL_LINE", LTOOL_NAME, 0, LGCA_NONE, NULL, 0);

  /* reference $token */
  l_tool_line_tokens(build.definition, "REFERENCE_LINE");
  (void)logos_languages_genesis_definition_token_add(build.definition, "token_ref", "REFERENCE_LINE", "\\x24" LTOOL_NAME, 0, LGCA_NONE, NULL, 0);

  /* key = value */
  l_tool_line_tokens(build.definition, "SETTING_LINE");
  assign_text = logos_languages_genesis_definition_token_add(build.definition, "assign_text", "SETTING_LINE", "=", 0, LGCA_SET, "TEXT_VALUE", 0);

  /* A scope assignment's value, and a setting's value: the rest of its line, whatever it contains. */
  l_tool_line_tokens(build.definition, "SCOPE_VALUE");
  scope = logos_languages_genesis_definition_token_add(build.definition, "scope", "SCOPE_VALUE", LTOOL_NAME, 0, LGCA_NONE, NULL, 0);
  l_tool_line_tokens(build.definition, "TEXT_VALUE");
  setting_value =
      logos_languages_genesis_definition_token_add(build.definition, "setting_value", "TEXT_VALUE", "[\\t\\x20-\\xFF]+", 0, LGCA_NONE, NULL, 0);

  /* Rules. */
  blank_rule    = logos_languages_genesis_definition_rule_add(build.definition, "blank");
  mapping       = logos_languages_genesis_definition_rule_add(build.definition, "mapping");
  statement     = logos_languages_genesis_definition_rule_add(build.definition, "statement");
  token_scope   = logos_languages_genesis_definition_rule_add(build.definition, "token_scope");
  setting       = logos_languages_genesis_definition_rule_add(build.definition, "setting");
  symbol_option = logos_languages_genesis_definition_rule_add(build.definition, "symbol_option");

  for (i = 0; i < logos_tool_mapping_form_count_get(); i++) {
    (void)stbsp_snprintf(name, (int)sizeof(name), "%s_form", logos_tool_mapping_form_at(i)->keyword);
    forms[i] = logos_languages_genesis_definition_rule_add(build.definition, name);
  }

  logos_languages_contexta_contexta_rule_define(build.contexta, blank_rule, l_tool_token(&build, blank));
  logos_languages_contexta_contexta_rule_define(
      build.contexta, mapping,
      l_tool_star(&build,
                  l_tool_union(&build,
                               l_tool_union(&build, logos_languages_contexta_contexta_operation_add_reference(NULL, build.contexta, statement),
                                            l_tool_token(&build, comment)),
                               l_tool_token(&build, newline))));

  items[0] = l_tool_token(&build, token_ref);
  items[1] = l_tool_token(&build, assign_scope);
  items[2] = l_tool_optional(&build, l_tool_token(&build, scope));
  items[3] = l_tool_optional(&build, l_tool_token(&build, line_end));
  logos_languages_contexta_contexta_rule_define(build.contexta, token_scope, l_tool_sequence(&build, items, 4));

  items[0] = l_tool_token(&build, setting_key);
  items[1] = l_tool_token(&build, assign_text);
  items[2] = l_tool_optional(&build, l_tool_token(&build, setting_value));
  items[3] = l_tool_optional(&build, l_tool_token(&build, line_end));
  logos_languages_contexta_contexta_rule_define(build.contexta, setting, l_tool_sequence(&build, items, 4));

  logos_languages_contexta_contexta_rule_define(
      build.contexta, symbol_option,
      l_tool_concatenate(&build, l_tool_token(&build, option_key),
                         l_tool_union(&build, l_tool_token(&build, token_ref), l_tool_token(&build, option_value))));

  for (i = 0; i < logos_tool_mapping_form_count_get(); i++) {
    LToolMappingFormId form_id;

    form_id  = logos_tool_mapping_form_at(i)->id;
    items[0] = l_tool_token(&build, keywords[i]);

    if (form_id == LTOOL_FORM_CONTEXT) {
      items[1] = l_tool_token(&build, context_name);
      items[2] = l_tool_token(&build, assign_scope);
      items[3] = l_tool_optional(&build, l_tool_token(&build, scope));
      items[4] = l_tool_optional(&build, l_tool_token(&build, line_end));
      logos_languages_contexta_contexta_rule_define(build.contexta, forms[i], l_tool_sequence(&build, items, 5));
    } else if (form_id == LTOOL_FORM_RULE) {
      items[1] = l_tool_token(&build, rule_ref);
      items[2] = l_tool_optional(&build, l_tool_concatenate(&build, l_tool_token(&build, greater), l_tool_token(&build, rule_ref)));
      items[3] = l_tool_token(&build, assign_scope);
      items[4] = l_tool_optional(&build, l_tool_token(&build, scope));
      items[5] = l_tool_optional(&build, l_tool_token(&build, line_end));
      logos_languages_contexta_contexta_rule_define(build.contexta, forms[i], l_tool_sequence(&build, items, 6));
    } else if (form_id == LTOOL_FORM_SYMBOL) {
      items[1] = l_tool_token(&build, rule_ref);
      items[2] = l_tool_star(&build, logos_languages_contexta_contexta_operation_add_reference(NULL, build.contexta, symbol_option));
      items[3] = l_tool_optional(&build, l_tool_token(&build, line_end));
      logos_languages_contexta_contexta_rule_define(build.contexta, forms[i], l_tool_sequence(&build, items, 4));
    } else {
      items[1] = l_tool_token(&build, token_ref);
      items[2] = l_tool_optional(&build, l_tool_token(&build, line_end));
      logos_languages_contexta_contexta_rule_define(build.contexta, forms[i], l_tool_sequence(&build, items, 3));
    }
  }

  alternatives = l_tool_union(&build, logos_languages_contexta_contexta_operation_add_reference(NULL, build.contexta, token_scope),
                              logos_languages_contexta_contexta_operation_add_reference(NULL, build.contexta, setting));

  for (i = 0; i < logos_tool_mapping_form_count_get(); i++) {
    alternatives = l_tool_union(&build, alternatives, logos_languages_contexta_contexta_operation_add_reference(NULL, build.contexta, forms[i]));
  }

  logos_languages_contexta_contexta_rule_define(build.contexta, statement, alternatives);
  logos_languages_genesis_definition_entry_set(build.definition, mapping);
  logos_languages_genesis_definition_global_add(build.definition, blank_rule);

  return build.definition;
}

/* ---------------------------------------------------------------------------- */
/* EDITOR MAPPINGS OF THE TWO GRAMMARS                                           */
/* ---------------------------------------------------------------------------- */

/* Summary: the scope of a word of Genesis by its role. */
static const char *l_tool_word_scope(const char *role) {
  if (nexus_strings_string_equals(role, "module")) {
    return "keyword.declaration.module";
  }

  if (nexus_strings_string_equals(role, "namespace")) {
    return "keyword.declaration.namespace";
  }

  if (nexus_strings_string_equals(role, "enum")) {
    return "keyword.declaration.enum";
  }

  if (nexus_strings_string_equals(role, "struct")) {
    return "keyword.declaration.struct";
  }

  if (nexus_strings_string_equals(role, "function")) {
    return "keyword.declaration.function";
  }

  if (nexus_strings_string_equals(role, "pratt")) {
    return "keyword.other";
  }

  if (nexus_strings_string_equals(role, "action")) {
    return "keyword.control";
  }

  if (nexus_strings_string_equals(role, "constant")) {
    return "constant.language";
  }

  if (nexus_strings_string_equals(role, "condition")) {
    return "keyword.control.conditional";
  }

  return "entity.other.attribute-name";
}

/*
Summary: the scope of an operator by what it does, so that a theme can colour a repetition, an alternation or a negation each in its own way
  (the contexts of LangSpec scope operators the same way: keyword.operator.optional, .star, .plus, .assignment).
*/
static const char *l_tool_operator_scope(const char *name) {
  static const struct {
    const char *name;
    const char *scope;
  } scopes[] = {
      {"question", "keyword.operator.optional"},    {"star", "keyword.operator.star"},         {"plus", "keyword.operator.plus"},
      {"minus", "keyword.operator.minus"},          {"bang", "keyword.operator.negation"},     {"union", "keyword.operator.alternation"},
      {"bar", "keyword.operator.alternation"},      {"dot_dot", "keyword.operator.range"},     {"dot", "keyword.operator.wildcard"},
      {"arrow", "keyword.operator.arrow"},          {"equals", "keyword.operator.assignment"}, {"equals_equals", "keyword.operator.comparison"},
      {"not_equals", "keyword.operator.comparison"}};
  uint32 i;

  for (i = 0; i < NEXUS_ARRAY_SIZE_ELEMENTS(scopes); i++) {
    if (nexus_strings_string_equals(name, scopes[i].name)) {
      return scopes[i].scope;
    }
  }

  return "keyword.operator";
}

void logos_tool_spec_genesis_mapping_write(LToolText *out) {
  uint32 i;

  logos_tool_text_add(out, "// Generated by genesis from the tables of the compiler's lexer and parser. Do not edit.\n");
  logos_tool_text_add(out, "//\n// Editor mapping for Genesis sources.\n");
  logos_tool_text_add(out, "extensions = gen\nscope = source.genesis\ntest.comment = //\nsublime.package = User\n\n");
  logos_tool_text_add(out, "$line_comment = comment.line.double-slash\n$block_comment_open = comment.block\n$block_comment_close = comment.block\n");
  logos_tool_text_add(out, "$block_comment_text = comment.block\n$block_comment_star = comment.block\ncontext BLOCK_COMMENT = comment.block\n\n");

  for (i = 0; i < logos_languages_genesis_spec_word_count_get(); i++) {
    const LGenesisSpecWord *word;
    const char             *text;
    uint_large              j;
    char                    one[2];

    word = logos_languages_genesis_spec_word_at(i);
    text = word->text[0] == '@' ? word->text + 1 : word->text;
    logos_tool_text_add(out, "$kw_");
    one[1] = '\0';

    for (j = 0; text[j] != '\0'; j++) {
      one[0] = text[j];

      if (text[j] >= 'A' && text[j] <= 'Z') {
        one[0] = (char)(text[j] - 'A' + 'a');
      }

      logos_tool_text_add(out, one);
    }

    logos_tool_text_format(out, " = %s\n", l_tool_word_scope(word->role));
  }

  logos_tool_text_add(out,
                      "\n$annotation = entity.other.attribute-name\n$param_ref = variable.parameter\n$fragment_ref = support.constant.fragment\n");
  logos_tool_text_add(out,
                      "$token_ref = support.constant\n$rule_ref = entity.name.function\n$identifier = variable.other\n$integer = constant.numeric\n");
  logos_tool_text_add(out, "$single_quoted = string.quoted.single\n$double_quoted = string.quoted.double\n\n");

  for (i = 0; i < logos_languages_genesis_spec_punctuation_count_get(); i++) {
    const LGenesisSpecPunctuation *punctuation;

    punctuation = logos_languages_genesis_spec_punctuation_at(i);

    if (nexus_strings_string_equals(punctuation->role, "open")) {
      logos_tool_text_format(out, "$%s = punctuation.section.%s.begin\n", punctuation->name, punctuation->group);
    } else if (nexus_strings_string_equals(punctuation->role, "close")) {
      logos_tool_text_format(out, "$%s = punctuation.section.%s.end\n", punctuation->name, punctuation->group);
    } else if (nexus_strings_string_equals(punctuation->role, "separator")) {
      logos_tool_text_format(out, "$%s = punctuation.separator\n", punctuation->name);
    } else {
      logos_tool_text_format(out, "$%s = %s\n", punctuation->name, l_tool_operator_scope(punctuation->name));
    }
  }

  logos_tool_text_add(out, "\n// Groups are scoped as frames.\n");

  for (i = 0; i < logos_languages_genesis_spec_punctuation_count_get(); i++) {
    const LGenesisSpecPunctuation *punctuation;

    punctuation = logos_languages_genesis_spec_punctuation_at(i);

    if (nexus_strings_string_equals(punctuation->role, "open")) {
      logos_tool_text_format(out, "rule #%s = meta.%s\n", punctuation->group, punctuation->group);
    }
  }
}

void logos_tool_spec_mapping_mapping_write(LToolText *out) {
  uint32 i;

  logos_tool_text_add(out, "// Generated by genesis from the statement forms of the mapping reader. Do not edit.\n");
  logos_tool_text_add(out, "//\n// Editor mapping for mapping files.\n");
  logos_tool_text_add(out, "extensions = mapping\nscope = source.genesis-mapping\ntest.comment = //\nsublime.package = User\n\n");
  logos_tool_text_add(out, "$comment = comment.line.double-slash\n");

  for (i = 0; i < logos_tool_mapping_form_count_get(); i++) {
    logos_tool_text_format(out, "$kw_%s = keyword.declaration\n", logos_tool_mapping_form_at(i)->keyword);
  }

  logos_tool_text_add(
      out,
      "$token_ref = support.constant\n$rule_ref = entity.name.function\n$setting_key = variable.other.member\n$context_name = entity.name.type\n");
  logos_tool_text_add(out, "$option_key = entity.other.attribute-name\n$option_value = string.unquoted\n$scope = "
                           "string.unquoted.scope\n$setting_value = string.unquoted\n");
  logos_tool_text_add(out, "$assign_scope = keyword.operator.assignment\n$assign_text = keyword.operator.assignment\n$greater = keyword.operator\n");
}
