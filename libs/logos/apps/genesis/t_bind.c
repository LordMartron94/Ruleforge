#include <nexus.h>
#include <logos.h>

#include "tool.h"

/*
The binding generator writes one self-contained C header per grammar: the token kinds and rule names as enumerations,
and for every rule a small typed view over its syntax node with accessors for the tokens and rules the rule refers to.
Everything is static, so the header costs nothing for what a program does not use. The identifier prefix is the namespace.
*/

typedef struct LToolBind {
  const LGenesisGrammar *grammar;
  const char            *prefix; /* lower-case, e.g. "json" */
  char                  *upper;  /* "JSON" */
  char                  *camel;  /* "Json" */
  LToolText              out;
} LToolBind;

static boolean l_tool_bind_private(const char *name) {
  return nexus_strings_string_find(name, "__i") != NULL;
}

/* Summary: two 32-bit FNV-1a hashes of the text with different seeds; the header's generated check computes the same. */
static void l_tool_bind_fingerprint(const char *text, uint32 *out_high, uint32 *out_low) {
  uint32     high;
  uint32     low;
  uint_large i;

  high = 0x811C9DC5U;
  low  = 0x9E3779B9U;

  for (i = 0; text[i] != '\0'; i++) {
    high = (high ^ (byte)text[i]) * 0x01000193U;
    low  = (low ^ (byte)text[i]) * 0x01000193U;
  }

  *out_high = high;
  *out_low  = low;
}

/* Summary: adds template lines, replacing @P@ with the lower-case prefix and @U@ with the upper-case prefix. */
static void l_tool_bind_emit(LToolBind *bind, const char *const *lines) {
  uint32      i;
  const char *cursor;
  char        one[2];

  one[1] = '\0';

  for (i = 0; lines[i] != NULL; i++) {
    for (cursor = lines[i]; *cursor != '\0'; cursor++) {
      if (cursor[0] == '@' && cursor[1] == 'P' && cursor[2] == '@') {
        logos_tool_text_add(&bind->out, bind->prefix);
        cursor += 2;
      } else if (cursor[0] == '@' && cursor[1] == 'U' && cursor[2] == '@') {
        logos_tool_text_add(&bind->out, bind->upper);
        cursor += 2;
      } else {
        one[0] = *cursor;
        logos_tool_text_add(&bind->out, one);
      }
    }

    logos_tool_text_add(&bind->out, "\n");
  }
}

static void l_tool_bind_helpers(LToolBind *bind) {
  static const char *const lines[] = {
      "/* Helpers shared by the views below. */",
      "#define @U@_NOT_FOUND ((uint32)0xFFFFFFFFu)",
      "",
      "static NEXUS_MAYBE_UNUSED boolean @P@_node_is_rule(const LParserTree *tree, const LParserGreenNode *node, const char *label) {",
      "  const char *own;",
      "",
      "  if (node == NULL || (logos_language_tooling_parsing_syntax_green_node_kind_get(node) != LPGK_RULE &&",
      "                       logos_language_tooling_parsing_syntax_green_node_kind_get(node) != LPGK_EXPRESSION)) {",
      "    return FALSE;",
      "  }",
      "",
      "  own = logos_language_tooling_parsing_syntax_green_node_label_get(tree, node);",
      "",
      "  return own != NULL && nexus_strings_string_equals(own, label);",
      "}",
      "",
      "/* Visits what a node holds directly: it looks through expressions and sequences and stops at rules. */",
      "static NEXUS_MAYBE_UNUSED void @P@_walk(const LParserTree *tree, const LParserGreenNode *node, LLexerTokenKind token_kind, const char "
      "*rule_label,",
      "                                       uint32 wanted, uint32 *counter, const LParserGreenNode **out) {",
      "  const LParserGreenAlternative *alternative;",
      "  const LParserGreenNode        *child;",
      "  LParserGreenKind               kind;",
      "  uint32                         i;",
      "",
      "  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);",
      "",
      "  if (alternative == NULL) {",
      "    return;",
      "  }",
      "",
      "  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {",
      "    child = logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i);",
      "    kind  = logos_language_tooling_parsing_syntax_green_node_kind_get(child);",
      "",
      "    if (kind == LPGK_TOKEN) {",
      "      if (rule_label == NULL && logos_language_tooling_parsing_syntax_green_node_token_kind_get(tree, child) == token_kind) {",
      "        if (*counter == wanted && out != NULL) {",
      "          *out = child;",
      "        }",
      "",
      "        (*counter)++;",
      "      }",
      "    } else if (kind == LPGK_RULE) {",
      "      if (rule_label != NULL && @P@_node_is_rule(tree, child, rule_label)) {",
      "        if (*counter == wanted && out != NULL) {",
      "          *out = child;",
      "        }",
      "",
      "        (*counter)++;",
      "      }",
      "    } else {",
      "      @P@_walk(tree, child, token_kind, rule_label, wanted, counter, out);",
      "    }",
      "  }",
      "}",
      "",
      "static NEXUS_MAYBE_UNUSED uint32 @P@_count_tokens(const LParserTree *tree, const LParserGreenNode *node, LLexerTokenKind kind) {",
      "  uint32 counter;",
      "",
      "  counter = 0;",
      "  @P@_walk(tree, node, kind, NULL, @U@_NOT_FOUND, &counter, NULL);",
      "",
      "  return counter;",
      "}",
      "",
      "static NEXUS_MAYBE_UNUSED const LParserGreenNode *@P@_token_at(const LParserTree *tree, const LParserGreenNode *node, LLexerTokenKind kind, "
      "uint32 index) {",
      "  const LParserGreenNode *found;",
      "  uint32                  counter;",
      "",
      "  found   = NULL;",
      "  counter = 0;",
      "  @P@_walk(tree, node, kind, NULL, index, &counter, &found);",
      "",
      "  return found;",
      "}",
      "",
      "static NEXUS_MAYBE_UNUSED uint32 @P@_count_rules(const LParserTree *tree, const LParserGreenNode *node, const char *label) {",
      "  uint32 counter;",
      "",
      "  counter = 0;",
      "  @P@_walk(tree, node, 0, label, @U@_NOT_FOUND, &counter, NULL);",
      "",
      "  return counter;",
      "}",
      "",
      "static NEXUS_MAYBE_UNUSED const LParserGreenNode *@P@_rule_at(const LParserTree *tree, const LParserGreenNode *node, const char *label, "
      "uint32 index) {",
      "  const LParserGreenNode *found;",
      "  uint32                  counter;",
      "",
      "  found   = NULL;",
      "  counter = 0;",
      "  @P@_walk(tree, node, 0, label, index, &counter, &found);",
      "",
      "  return found;",
      "}",
      "",
      NULL};

  l_tool_bind_emit(bind, lines);
}

/* Summary: the generated helpers every grammar gets: which rules are global (trivia), the byte range of a node, rule names and a JSON dump. */
static void l_tool_bind_generic(LToolBind *bind) {
  LToolText             *out;
  const LGenesisGrammar *grammar;
  uint32                 i;

  out     = &bind->out;
  grammar = bind->grammar;

  logos_tool_text_format(out,
                         "/* Tells whether a syntax node is one of the global rules of the grammar (its PARSER block's trivia). */\n"
                         "static NEXUS_MAYBE_UNUSED boolean %s_node_is_global(const LParserTree *tree, const LParserGreenNode *node) {\n"
                         "  const char *label;\n\n"
                         "  if (node == NULL || logos_language_tooling_parsing_syntax_green_node_kind_get(node) != LPGK_RULE) {\n"
                         "    return FALSE;\n"
                         "  }\n\n"
                         "  label = logos_language_tooling_parsing_syntax_green_node_label_get(tree, node);\n\n"
                         "  if (label == NULL) {\n"
                         "    return FALSE;\n"
                         "  }\n\n",
                         bind->prefix);

  for (i = 0; i < logos_languages_genesis_grammar_global_count_get(grammar); i++) {
    logos_tool_text_format(out, "  if (nexus_strings_string_equals(label, \"%s\")) {\n    return TRUE;\n  }\n\n",
                           logos_languages_genesis_grammar_global_at(grammar, i));
  }

  logos_tool_text_add(out, "  return FALSE;\n}\n\n");

  {
    static const char *const lines[] = {
        "/* The byte range of the tokens below a node, leaving out the global rules (trivia) before the first and after the last token. */",
        "static NEXUS_MAYBE_UNUSED boolean @P@_node_range(const LParserTree *tree, const LParserGreenNode *node, uint_large *out_start, uint_large "
        "*out_end) {",
        "  const LParserGreenAlternative *alternative;",
        "  const byte                    *text;",
        "  const byte                    *base;",
        "  uint_large                     size;",
        "  uint_large                     start;",
        "  uint_large                     end;",
        "  uint_large                     tree_size;",
        "  uint32                         i;",
        "  boolean                        found;",
        "",
        "  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_TOKEN) {",
        "    text = logos_language_tooling_parsing_syntax_green_node_text_get(tree, node, &size);",
        "    base = logos_language_tooling_parsing_syntax_tree_source_get(tree, &tree_size);",
        "",
        "    if (size == 0) {",
        "      return FALSE;",
        "    }",
        "",
        "    *out_start = (uint_large)(text - base);",
        "    *out_end   = *out_start + size;",
        "",
        "    return TRUE;",
        "  }",
        "",
        "  if (@P@_node_is_global(tree, node)) {",
        "    return FALSE;",
        "  }",
        "",
        "  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);",
        "  found       = FALSE;",
        "",
        "  if (alternative == NULL) {",
        "    return FALSE;",
        "  }",
        "",
        "  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {",
        "    if (@P@_node_range(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), &start, &end)) {",
        "      if (!found || start < *out_start) {",
        "        *out_start = start;",
        "      }",
        "",
        "      if (!found || end > *out_end) {",
        "        *out_end = end;",
        "      }",
        "",
        "      found = TRUE;",
        "    }",
        "  }",
        "",
        "  return found;",
        "}",
        "",
        "/* The exact source text of a token, and its length, or NULL for a node that is not a token. */",
        "static NEXUS_MAYBE_UNUSED const char *@P@_token_text(const LParserTree *tree, const LParserGreenNode *token, uint_large *out_size) {",
        "  return (const char *)logos_language_tooling_parsing_syntax_green_node_text_get(tree, token, out_size);",
        "}",
        "",
        NULL};

    l_tool_bind_emit(bind, lines);
  }

  logos_tool_text_format(
      out, "/* The name of a rule of the grammar, or NULL. */\nstatic NEXUS_MAYBE_UNUSED const char *%s_rule_name(int32 rule) {\n  switch (rule) {\n",
      bind->prefix);

  for (i = 0; i < logos_languages_genesis_grammar_rule_count_get(grammar); i++) {
    const LGenesisGrammarRule *rule;
    char                      *upper;

    rule = logos_languages_genesis_grammar_rule_at(grammar, i);

    if (l_tool_bind_private(rule->name)) {
      continue;
    }

    upper = logos_tool_identifier_upper(rule->name);
    logos_tool_text_format(out, "  case %s_RULE_%s:\n    return \"%s\";\n", bind->upper, upper, rule->name);
    nexus_memory_deallocate(upper);
  }

  logos_tool_text_add(out, "  default:\n    return NULL;\n  }\n}\n\n");

  {
    static const char *const lines[] = {
        "/*",
        "Summary: writes a syntax tree as JSON with the names the grammar gives its rules and tokens.",
        "Description: a rule is {\"rule\":name,\"start\":byte,\"end\":byte,\"children\":[...]} and a token "
        "{\"token\":name,\"start\":byte,\"end\":byte,\"text\":string},",
        "  in source order. The global rules (trivia) are left out and the grammar's private helper rules are flattened into their parent.",
        "  The names come from the grammar, so the JSON cannot drift from it. The text of a token is its exact source.",
        "*/",
        "static NEXUS_MAYBE_UNUSED void @P@_node_json_write(const LParserTree *tree, const LParserGreenNode *node, NexusStringBuilder *out, boolean "
        "*first) {",
        "  const LParserGreenAlternative *alternative;",
        "  LParserGreenKind               kind;",
        "  uint_large                     start;",
        "  uint_large                     end;",
        "  uint_large                     size;",
        "  const char                    *label;",
        "  const char                    *text;",
        "  char                          *copy;",
        "  uint32                         i;",
        "  boolean                        children_first;",
        "",
        "  kind = logos_language_tooling_parsing_syntax_green_node_kind_get(node);",
        "",
        "  if (kind == LPGK_TOKEN) {",
        "    if (!@P@_node_range(tree, node, &start, &end)) {",
        "      return;",
        "    }",
        "",
        "    label = @P@_token_name(logos_language_tooling_parsing_syntax_green_node_token_kind_get(tree, node));",
        "    text  = @P@_token_text(tree, node, &size);",
        "    copy  = (char *)nexus_memory_allocate(size + 1);",
        "    nexus_memory_bytes_copy(text, copy, size);",
        "    copy[size] = '\\0';",
        "    (void)nexus_strings_string_builder_append_formatted(out, "
        "\"%s{\\\"token\\\":\\\"%s\\\",\\\"start\\\":%lu,\\\"end\\\":%lu,\\\"text\\\":\", *first ? \"\" : \",\",",
        "                                                        label == NULL ? \"\" : label, (unsigned long)start, (unsigned long)end);",
        "    (void)nexus_json_quote_append(out, copy);",
        "    (void)nexus_strings_string_builder_append(out, \"}\");",
        "    nexus_memory_deallocate(copy);",
        "    *first = FALSE;",
        "",
        "    return;",
        "  }",
        "",
        "  if (@P@_node_is_global(tree, node)) {",
        "    return;",
        "  }",
        "",
        "  label       = logos_language_tooling_parsing_syntax_green_node_label_get(tree, node);",
        "  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);",
        "",
        "  if (alternative == NULL) {",
        "    return;",
        "  }",
        "",
        "  /* Sequences, the source and private helper rules (their names contain \"__i\" or \"@\") add nothing of their own. */",
        "  if (kind == LPGK_SEQUENCE || kind == LPGK_SOURCE || label == NULL || nexus_strings_string_contains(label, \"__i\") ||",
        "      nexus_strings_string_contains(label, \"@\")) {",
        "    for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {",
        "      @P@_node_json_write(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), out, first);",
        "    }",
        "",
        "    return;",
        "  }",
        "",
        "  if (!@P@_node_range(tree, node, &start, &end)) {",
        "    return;",
        "  }",
        "",
        "  (void)nexus_strings_string_builder_append_formatted(out, "
        "\"%s{\\\"rule\\\":\\\"%s\\\",\\\"start\\\":%lu,\\\"end\\\":%lu,\\\"children\\\":[\", *first ? \"\" : \",\", label,",
        "                                                      (unsigned long)start, (unsigned long)end);",
        "  children_first = TRUE;",
        "",
        "  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {",
        "    @P@_node_json_write(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), out, &children_first);",
        "  }",
        "",
        "  (void)nexus_strings_string_builder_append(out, \"]}\");",
        "  *first = FALSE;",
        "}",
        "",
        "/* Summary: a whole parsed tree as one JSON object (see @P@_node_json_write); the root of the tree is the entry rule. */",
        "static NEXUS_MAYBE_UNUSED void @P@_tree_json_write(const LParserTree *tree, NexusStringBuilder *out) {",
        "  boolean first;",
        "",
        "  first = TRUE;",
        "  @P@_node_json_write(tree, logos_language_tooling_parsing_syntax_tree_root_get(tree), out, &first);",
        "}",
        "",
        NULL};

    l_tool_bind_emit(bind, lines);
  }
}

static void l_tool_bind_typedef(LToolBind *bind, const LGenesisGrammarRule *rule) {
  char *rule_camel;

  rule_camel = logos_tool_identifier_camel(rule->name);
  logos_tool_text_format(&bind->out, "/* A view of the rule #%s. */\n", rule->name);
  logos_tool_text_format(&bind->out,
                         "typedef struct %s%s {\n  const LParserTree      *tree; /* The tree the node belongs to. */\n  const LParserGreenNode "
                         "*node; /* NULL when absent. */\n} %s%s;\n\n",
                         bind->camel, rule_camel, bind->camel, rule_camel);
  nexus_memory_deallocate(rule_camel);
}

static void l_tool_bind_view(LToolBind *bind, const LGenesisGrammarRule *rule) {
  LToolText *out;
  char      *rule_camel;
  char      *rule_lower;
  uint32     i;

  out        = &bind->out;
  rule_camel = logos_tool_identifier_camel(rule->name);
  rule_lower = logos_tool_identifier_lower(rule->name);

  logos_tool_text_format(out, "/* Accessors of the rule #%s. */\n", rule->name);
  logos_tool_text_format(out,
                         "static NEXUS_MAYBE_UNUSED %s%s %s_%s_cast(const LParserTree *tree, const LParserGreenNode *node) {\n"
                         "  %s%s view;\n\n"
                         "  view.tree = tree;\n"
                         "  view.node = %s_node_is_rule(tree, node, \"%s\") ? node : NULL;\n\n"
                         "  return view;\n"
                         "}\n\n",
                         bind->camel, rule_camel, bind->prefix, rule_lower, bind->camel, rule_camel, bind->prefix, rule->name);

  for (i = 0; i < rule->token_ref_count; i++) {
    char *token_lower;
    char *token_upper;

    if (l_tool_bind_private(rule->token_refs[i])) {
      continue;
    }

    token_lower = logos_tool_identifier_lower(rule->token_refs[i]);
    token_upper = logos_tool_identifier_upper(rule->token_refs[i]);
    logos_tool_text_format(out,
                           "static NEXUS_MAYBE_UNUSED uint32 %s_%s_count_token_%s(%s%s view) {\n"
                           "  return view.node == NULL ? 0 : %s_count_tokens(view.tree, view.node, %s_TOKEN_%s);\n"
                           "}\n\n",
                           bind->prefix, rule_lower, token_lower, bind->camel, rule_camel, bind->prefix, bind->upper, token_upper);
    logos_tool_text_format(out,
                           "static NEXUS_MAYBE_UNUSED const LParserGreenNode *%s_%s_token_%s(%s%s view, uint32 index) {\n"
                           "  return view.node == NULL ? NULL : %s_token_at(view.tree, view.node, %s_TOKEN_%s, index);\n"
                           "}\n\n",
                           bind->prefix, rule_lower, token_lower, bind->camel, rule_camel, bind->prefix, bind->upper, token_upper);
    nexus_memory_deallocate(token_lower);
    nexus_memory_deallocate(token_upper);
  }

  for (i = 0; i < rule->rule_ref_count; i++) {
    char *child_lower;
    char *child_camel;

    if (l_tool_bind_private(rule->rule_refs[i]) || logos_tool_string_find_byte(rule->rule_refs[i], '.') != NULL) {
      continue;
    }

    child_lower = logos_tool_identifier_lower(rule->rule_refs[i]);
    child_camel = logos_tool_identifier_camel(rule->rule_refs[i]);
    logos_tool_text_format(out,
                           "static NEXUS_MAYBE_UNUSED uint32 %s_%s_count_rule_%s(%s%s view) {\n"
                           "  return view.node == NULL ? 0 : %s_count_rules(view.tree, view.node, \"%s\");\n"
                           "}\n\n",
                           bind->prefix, rule_lower, child_lower, bind->camel, rule_camel, bind->prefix, rule->rule_refs[i]);
    logos_tool_text_format(out,
                           "static NEXUS_MAYBE_UNUSED %s%s %s_%s_rule_%s(%s%s view, uint32 index) {\n"
                           "  %s%s child;\n\n"
                           "  child.tree = view.tree;\n"
                           "  child.node = view.node == NULL ? NULL : %s_rule_at(view.tree, view.node, \"%s\", index);\n\n"
                           "  return child;\n"
                           "}\n\n",
                           bind->camel, child_camel, bind->prefix, rule_lower, child_lower, bind->camel, rule_camel, bind->camel, child_camel,
                           bind->prefix, rule->rule_refs[i]);
    nexus_memory_deallocate(child_lower);
    nexus_memory_deallocate(child_camel);
  }

  nexus_memory_deallocate(rule_camel);
  nexus_memory_deallocate(rule_lower);
}

/*
Summary: the operator structure of a binding-power (PRATT) rule.
Description: the views of a rule cannot say which operator applies to which
operands, because operators are not rules, so this is generated from the PRATT
block itself. For every PRATT rule R whose block names its atom rule A (`atom =
#A`): operand_count  0 when the node is an atom, 1 for a prefix operation, 2 for
an infix one operator_kind  the lexer token kind of the operator, or
L_LEXER_TOKEN_KIND_ERROR for an atom operator_token the operator's token node
  operand        a view of an operand, itself an operation or an atom
  atom           the atom, when operand_count is 0
*/
static void l_tool_bind_pratt(LToolBind *bind, const LGenesisGrammarRule *rule) {
  LToolText  *out;
  char       *rule_camel;
  char       *rule_lower;
  char       *atom_camel;
  char       *atom_name;
  const char *marker;
  uint_large  length;

  marker = nexus_strings_string_find(rule->definition, "atom = #");

  if (marker == NULL) {
    return;
  }

  marker += 8;
  length = 0;

  while ((marker[length] >= 'a' && marker[length] <= 'z') || (marker[length] >= '0' && marker[length] <= '9') || marker[length] == '_') {
    length++;
  }

  atom_name = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(marker, atom_name, length);
  atom_name[length] = '\0';

  out        = &bind->out;
  rule_camel = logos_tool_identifier_camel(rule->name);
  rule_lower = logos_tool_identifier_lower(rule->name);
  atom_camel = logos_tool_identifier_camel(atom_name);

  /* The spine: the significant children of the node, looking through sequences and leaving out the global rules. */
  logos_tool_text_format(out,
                         "/* The significant children of a node of the binding-power rule #%s: for an operation the operator and its operands. */\n"
                         "static NEXUS_MAYBE_UNUSED uint32 %s_%s_spine(%s%s view, const LParserGreenNode *spine[4]) {\n"
                         "  const LParserGreenNode        *node;\n"
                         "  const LParserGreenAlternative *alternative;\n"
                         "  uint32                         count;\n"
                         "  uint32                         i;\n"
                         "  uint32                         k;\n\n"
                         "  node  = view.node;\n"
                         "  count = 0;\n\n",
                         rule->name, bind->prefix, rule_lower, bind->camel, rule_camel);
  logos_tool_text_format(out,
                         "  /* The rule node itself holds one child: an atom or an operation. */\n"
                         "  while (node != NULL && logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_RULE &&\n"
                         "         %s_node_is_rule(view.tree, node, \"%s\")) {\n"
                         "    const LParserGreenNode *next;\n\n"
                         "    alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(view.tree, node);\n"
                         "    next        = NULL;\n\n",
                         bind->prefix, rule->name);
  logos_tool_text_add(out, "    for (i = 0; next == NULL && alternative != NULL &&\n"
                           "                i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative);\n"
                           "         i++) {\n"
                           "      const LParserGreenNode *child;\n\n"
                           "      child = logos_language_tooling_parsing_syntax_alternative_child_get(view.tree, alternative, i);\n\n");
  logos_tool_text_add(out, "      if (logos_language_tooling_parsing_syntax_green_node_kind_get(child) == LPGK_SEQUENCE) {\n"
                           "        const LParserGreenAlternative *inner;\n\n"
                           "        inner = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(view.tree, child);\n\n");
  logos_tool_text_format(out,
                         "        for (k = 0; next == NULL && inner != NULL &&\n"
                         "                    k < logos_language_tooling_parsing_syntax_alternative_child_count_get(inner);\n"
                         "             k++) {\n"
                         "          const LParserGreenNode *part;\n\n"
                         "          part = logos_language_tooling_parsing_syntax_alternative_child_get(view.tree, inner, k);\n\n"
                         "          if (!%s_node_is_global(view.tree, part)) {\n"
                         "            next = part;\n"
                         "          }\n"
                         "        }\n",
                         bind->prefix);
  logos_tool_text_format(out,
                         "      } else if (!%s_node_is_global(view.tree, child)) {\n"
                         "        next = child;\n"
                         "      }\n"
                         "    }\n\n"
                         "    node = next;\n"
                         "  }\n\n",
                         bind->prefix);
  logos_tool_text_add(out, "  if (node == NULL) {\n"
                           "    return 0;\n"
                           "  }\n\n"
                           "  spine[0] = node;\n\n"
                           "  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) != LPGK_EXPRESSION) {\n"
                           "    return 1;\n"
                           "  }\n\n"
                           "  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(view.tree, node);\n\n");
  logos_tool_text_add(out, "  for (i = 0; alternative != NULL &&\n"
                           "              i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative);\n"
                           "       i++) {\n"
                           "    const LParserGreenNode *child;\n\n"
                           "    child = logos_language_tooling_parsing_syntax_alternative_child_get(view.tree, alternative, i);\n\n");
  logos_tool_text_add(out, "    if (logos_language_tooling_parsing_syntax_green_node_kind_get(child) == LPGK_SEQUENCE) {\n"
                           "      const LParserGreenAlternative *inner;\n\n"
                           "      inner = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(view.tree, child);\n\n");
  logos_tool_text_format(out,
                         "      for (k = 0; inner != NULL &&\n"
                         "                  k < logos_language_tooling_parsing_syntax_alternative_child_count_get(inner);\n"
                         "           k++) {\n"
                         "        const LParserGreenNode *part;\n\n"
                         "        part = logos_language_tooling_parsing_syntax_alternative_child_get(view.tree, inner, k);\n\n"
                         "        if (!%s_node_is_global(view.tree, part) && count < 4) {\n"
                         "          spine[count++] = part;\n"
                         "        }\n"
                         "      }\n",
                         bind->prefix);
  logos_tool_text_format(out,
                         "    } else if (!%s_node_is_global(view.tree, child) && count < 4) {\n"
                         "      spine[count++] = child;\n"
                         "    }\n"
                         "  }\n\n"
                         "  return count;\n"
                         "}\n\n",
                         bind->prefix);

  logos_tool_text_format(out,
                         "/* How many operands the node of the rule #%s has: 0 for an atom, 1 for "
                         "a prefix operation, 2 for an infix one. */\n"
                         "static NEXUS_MAYBE_UNUSED uint32 %s_%s_operand_count(%s%s view) {\n"
                         "  const LParserGreenNode *spine[4];\n"
                         "  uint32                  count;\n\n"
                         "  count = view.node == NULL ? 0 : %s_%s_spine(view, spine);\n\n"
                         "  return count < 2 ? 0 : count - 1;\n"
                         "}\n\n",
                         rule->name, bind->prefix, rule_lower, bind->camel, rule_camel, bind->prefix, rule_lower);
  logos_tool_text_format(out,
                         "/* The operator of an operation: its token node, whose kind says which "
                         "operator it is. */\n"
                         "static NEXUS_MAYBE_UNUSED const LParserGreenNode "
                         "*%s_%s_operator_token(%s%s view) {\n"
                         "  const LParserGreenNode *spine[4];\n"
                         "  const LParserGreenNode *token;\n"
                         "  uint32                  count;\n\n"
                         "  count = view.node == NULL ? 0 : %s_%s_spine(view, spine);\n\n"
                         "  if (count < 2) {\n"
                         "    return NULL;\n"
                         "  }\n\n"
                         "  token = count == 2 ? spine[0] : spine[1];\n\n",
                         bind->prefix, rule_lower, bind->camel, rule_camel, bind->prefix, rule_lower);
  /* An operator may be wrapped in a sequence with the global rules around
   * it. */
  logos_tool_text_format(out, "  while (token != NULL &&\n"
                              "         logos_language_tooling_parsing_syntax_green_node_kind_get("
                              "token) == LPGK_SEQUENCE) {\n"
                              "    const LParserGreenAlternative *inner;\n"
                              "    const LParserGreenNode        *next;\n"
                              "    uint32                         i;\n\n"
                              "    inner = "
                              "logos_language_tooling_parsing_syntax_green_node_alternative_first_get("
                              "view.tree, token);\n"
                              "    next  = NULL;\n\n");
  logos_tool_text_format(out,
                         "    for (i = 0; next == NULL && inner != NULL && i < "
                         "logos_language_tooling_parsing_syntax_alternative_child_count_get("
                         "inner); i++) {\n"
                         "      const LParserGreenNode *part;\n\n"
                         "      part = "
                         "logos_language_tooling_parsing_syntax_alternative_child_get(view.tree, "
                         "inner, i);\n\n"
                         "      if (!%s_node_is_global(view.tree, part)) {\n"
                         "        next = part;\n"
                         "      }\n"
                         "    }\n\n"
                         "    token = next;\n"
                         "  }\n\n"
                         "  return token;\n"
                         "}\n\n",
                         bind->prefix);
  logos_tool_text_format(out,
                         "/* The lexer token kind of the operator of an operation. */\n"
                         "static NEXUS_MAYBE_UNUSED LLexerTokenKind %s_%s_operator_kind(%s%s "
                         "view) {\n"
                         "  const LParserGreenNode *token;\n\n"
                         "  token = %s_%s_operator_token(view);\n\n"
                         "  return token == NULL ? (LLexerTokenKind)L_LEXER_TOKEN_KIND_ERROR\n"
                         "                       : "
                         "logos_language_tooling_parsing_syntax_green_node_token_kind_get(view."
                         "tree, token);\n"
                         "}\n\n",
                         bind->prefix, rule_lower, bind->camel, rule_camel, bind->prefix, rule_lower);
  logos_tool_text_format(out,
                         "/* A view of an operand of an operation: itself an operation or an "
                         "atom. */\n"
                         "static NEXUS_MAYBE_UNUSED %s%s %s_%s_operand(%s%s view, uint32 index) "
                         "{\n"
                         "  const LParserGreenNode *spine[4];\n"
                         "  uint32                  count;\n"
                         "  %s%s operand;\n\n",
                         bind->camel, rule_camel, bind->prefix, rule_lower, bind->camel, rule_camel, bind->camel, rule_camel);
  logos_tool_text_format(out,
                         "  count        = view.node == NULL ? 0 : %s_%s_spine(view, spine);\n"
                         "  operand.tree = view.tree;\n"
                         "  operand.node = NULL;\n\n"
                         "  if (count == 2 && index == 0) {\n"
                         "    operand.node = spine[1];\n"
                         "  } else if (count == 3 && index < 2) {\n"
                         "    operand.node = index == 0 ? spine[0] : spine[2];\n"
                         "  }\n\n"
                         "  return operand;\n"
                         "}\n\n",
                         bind->prefix, rule_lower);
  logos_tool_text_format(out,
                         "/* The atom of a node that is not an operation. */\n"
                         "static NEXUS_MAYBE_UNUSED %s%s %s_%s_atom(%s%s view) {\n"
                         "  const LParserGreenNode *spine[4];\n"
                         "  uint32                  count;\n"
                         "  %s%s atom;\n\n"
                         "  count     = view.node == NULL ? 0 : %s_%s_spine(view, spine);\n"
                         "  atom.tree = view.tree;\n"
                         "  atom.node = count == 1 && %s_node_is_rule(view.tree, spine[0], "
                         "\"%s\") ? spine[0] : NULL;\n\n"
                         "  return atom;\n"
                         "}\n\n",
                         bind->camel, atom_camel, bind->prefix, rule_lower, bind->camel, rule_camel, bind->camel, atom_camel, bind->prefix,
                         rule_lower, bind->prefix, atom_name);

  nexus_memory_deallocate(atom_name);
  nexus_memory_deallocate(atom_camel);
  nexus_memory_deallocate(rule_camel);
  nexus_memory_deallocate(rule_lower);
}
static void l_tool_bind_generate(LToolBind *bind, const LGenesisEngine *engine, const char *guard) {
  const LGenesisGrammar *grammar;
  LToolText             *out;
  char                  *exported;
  uint32                 high;
  uint32                 low;
  uint32                 i;
  boolean                first;

  grammar  = bind->grammar;
  out      = &bind->out;
  exported = logos_languages_genesis_engine_export_text(engine);
  l_tool_bind_fingerprint(exported, &high, &low);
  nexus_memory_deallocate(exported);

  logos_tool_text_format(out, "/* Generated by genesis bind from the language %s. Do not edit. */\n",
                         logos_languages_genesis_engine_name_get(engine));
  logos_tool_text_format(out, "#ifndef %s\n#define %s\n\n#include <nexus.h>\n#include <logos.h>\n\n", guard, guard);
  logos_tool_text_format(out, "#define %s_GRAMMAR_NAME \"%s\"\n", bind->upper, logos_languages_genesis_engine_name_get(engine));
  logos_tool_text_format(out, "#define %s_GRAMMAR_FINGERPRINT_HIGH 0x%08Xu\n#define %s_GRAMMAR_FINGERPRINT_LOW 0x%08Xu\n\n", bind->upper,
                         (unsigned)high, bind->upper, (unsigned)low);

  {
    static const char *const matches[] = {"/* Tells whether an engine was compiled from the grammar these bindings were generated from. */",
                                          "static NEXUS_MAYBE_UNUSED boolean @P@_grammar_matches(const LGenesisEngine *engine) {",
                                          "  char      *text;",
                                          "  uint32     high;",
                                          "  uint32     low;",
                                          "  uint_large i;",
                                          "  boolean    same;",
                                          "",
                                          "  text = logos_languages_genesis_engine_export_text(engine);",
                                          "  high = 0x811C9DC5u;",
                                          "  low  = 0x9E3779B9u;",
                                          "",
                                          "  for (i = 0; text[i] != '\\0'; i++) {",
                                          "    high = (high ^ (byte)text[i]) * 0x01000193u;",
                                          "    low  = (low ^ (byte)text[i]) * 0x01000193u;",
                                          "  }",
                                          "",
                                          "  nexus_memory_deallocate(text);",
                                          "  same = high == @U@_GRAMMAR_FINGERPRINT_HIGH && low == @U@_GRAMMAR_FINGERPRINT_LOW;",
                                          "",
                                          "  return same;",
                                          "}",
                                          "",
                                          NULL};

    l_tool_bind_emit(bind, matches);
  }

  logos_tool_text_format(out, "/* Token kinds of the lexer. */\nenum {\n");
  first = TRUE;

  for (i = 0; i < logos_languages_genesis_grammar_token_count_get(grammar); i++) {
    const LGenesisGrammarToken *token;
    char                       *upper;
    uint32                      j;
    boolean                     seen;

    token = logos_languages_genesis_grammar_token_at(grammar, i);

    if (token->fragment || l_tool_bind_private(token->name)) {
      continue;
    }

    /* A token declared in several contexts is one kind. */
    seen = FALSE;

    for (j = 0; j < i; j++) {
      const LGenesisGrammarToken *earlier;

      earlier = logos_languages_genesis_grammar_token_at(grammar, j);
      seen    = seen || (!earlier->fragment && earlier->kind == token->kind);
    }

    if (seen) {
      continue;
    }

    upper = logos_tool_identifier_upper(token->name);
    logos_tool_text_format(out, "%s  %s_TOKEN_%s = %u", first ? "" : ",\n", bind->upper, upper, (unsigned)token->kind);
    nexus_memory_deallocate(upper);
    first = FALSE;
  }

  logos_tool_text_format(out, "\n};\n\n");

  logos_tool_text_format(out, "/* Names of the rules. */\nenum {\n  %s_RULE_NONE = -1", bind->upper);

  for (i = 0; i < logos_languages_genesis_grammar_rule_count_get(grammar); i++) {
    const LGenesisGrammarRule *rule;
    char                      *upper;

    rule = logos_languages_genesis_grammar_rule_at(grammar, i);

    if (l_tool_bind_private(rule->name)) {
      continue;
    }

    upper = logos_tool_identifier_upper(rule->name);
    logos_tool_text_format(out, ",\n  %s_RULE_%s", bind->upper, upper);
    nexus_memory_deallocate(upper);
  }

  logos_tool_text_format(out, "\n};\n\n");

  logos_tool_text_format(
      out,
      "/* The name of a token kind, or NULL. */\nstatic NEXUS_MAYBE_UNUSED const char *%s_token_name(LLexerTokenKind kind) {\n  switch (kind) {\n",
      bind->prefix);

  for (i = 0; i < logos_languages_genesis_grammar_token_count_get(grammar); i++) {
    const LGenesisGrammarToken *token;
    char                       *upper;
    uint32                      j;
    boolean                     seen;

    token = logos_languages_genesis_grammar_token_at(grammar, i);

    if (token->fragment || l_tool_bind_private(token->name)) {
      continue;
    }

    seen = FALSE;

    for (j = 0; j < i; j++) {
      const LGenesisGrammarToken *earlier;

      earlier = logos_languages_genesis_grammar_token_at(grammar, j);
      seen    = seen || (!earlier->fragment && earlier->kind == token->kind);
    }

    if (seen) {
      continue;
    }

    upper = logos_tool_identifier_upper(token->name);
    logos_tool_text_format(out, "  case %s_TOKEN_%s:\n    return \"%s\";\n", bind->upper, upper, token->name);
    nexus_memory_deallocate(upper);
  }

  logos_tool_text_format(out, "  default:\n    return NULL;\n  }\n}\n\n");

  logos_tool_text_format(out,
                         "/* The rule a syntax node is, or %s_RULE_NONE. */\nstatic NEXUS_MAYBE_UNUSED int32 %s_rule_of(const LParserTree "
                         "*tree, const LParserGreenNode *node) {\n  const char *label;\n\n",
                         bind->upper, bind->prefix);
  logos_tool_text_format(
      out,
      "  if (node == NULL || logos_language_tooling_parsing_syntax_green_node_kind_get(node) != LPGK_RULE) {\n    return %s_RULE_NONE;\n  }\n\n  "
      "label = logos_language_tooling_parsing_syntax_green_node_label_get(tree, node);\n\n  if (label == NULL) {\n    return %s_RULE_NONE;\n  }\n\n",
      bind->upper, bind->upper);

  for (i = 0; i < logos_languages_genesis_grammar_rule_count_get(grammar); i++) {
    const LGenesisGrammarRule *rule;
    char                      *upper;

    rule = logos_languages_genesis_grammar_rule_at(grammar, i);

    if (l_tool_bind_private(rule->name)) {
      continue;
    }

    upper = logos_tool_identifier_upper(rule->name);
    logos_tool_text_format(out, "  if (nexus_strings_string_equals(label, \"%s\")) {\n    return %s_RULE_%s;\n  }\n\n", rule->name, bind->upper,
                           upper);
    nexus_memory_deallocate(upper);
  }

  logos_tool_text_format(out, "  return %s_RULE_NONE;\n}\n\n", bind->upper);

  l_tool_bind_helpers(bind);
  l_tool_bind_generic(bind);

  for (i = 0; i < logos_languages_genesis_grammar_rule_count_get(grammar); i++) {
    const LGenesisGrammarRule *rule;

    rule = logos_languages_genesis_grammar_rule_at(grammar, i);

    if (!l_tool_bind_private(rule->name)) {
      l_tool_bind_typedef(bind, rule);
    }
  }

  for (i = 0; i < logos_languages_genesis_grammar_rule_count_get(grammar); i++) {
    const LGenesisGrammarRule *rule;

    rule = logos_languages_genesis_grammar_rule_at(grammar, i);

    if (!l_tool_bind_private(rule->name)) {
      l_tool_bind_view(bind, rule);

      if (rule->kind == LGRK_PRATT) {
        l_tool_bind_pratt(bind, rule);
      }
    }
  }

  if (logos_languages_genesis_grammar_entry_get(grammar) != NULL) {
    char *entry_lower;
    char *entry_camel;

    entry_lower = logos_tool_identifier_lower(logos_languages_genesis_grammar_entry_get(grammar));
    entry_camel = logos_tool_identifier_camel(logos_languages_genesis_grammar_entry_get(grammar));
    logos_tool_text_format(out,
                           "/* The entry rule of a parsed document. */\n"
                           "static NEXUS_MAYBE_UNUSED %s%s %s_root(const LParserTree *tree) {\n"
                           "  %s%s view;\n\n"
                           "  view.tree = tree;\n"
                           "  view.node = %s_rule_at(tree, logos_language_tooling_parsing_syntax_tree_root_get(tree), \"%s\", 0);\n\n"
                           "  return view;\n"
                           "}\n\n",
                           bind->camel, entry_camel, bind->prefix, bind->camel, entry_camel, bind->prefix,
                           logos_languages_genesis_grammar_entry_get(grammar));
    nexus_memory_deallocate(entry_lower);
    nexus_memory_deallocate(entry_camel);
  }

  logos_tool_text_format(out, "#endif\n");
}

int logos_tool_bind_run(const LToolOptions *options) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LToolMapping      mapping;
  LToolBind         bind;
  const char       *setting;
  char             *directory;
  char             *file_name;
  char             *path;
  char             *guard;
  char             *stem;
  char             *mapping_directory;
  boolean           changed;
  int               result;

  engine = logos_tool_grammar_compile(options, &compiler);

  if (engine == NULL) {
    logos_languages_genesis_compiler_destroy(compiler);

    return 1;
  }

  if (!logos_tool_mapping_for(options, &mapping)) {
    logos_languages_genesis_engine_release(engine);
    logos_languages_genesis_compiler_destroy(compiler);

    return 1;
  }

  /* Configuration: the command line wins, then the mapping file, then a default derived from the language name. */
  setting = options->prefix != NULL ? options->prefix : logos_tool_mapping_setting_get(&mapping, "bind.prefix");
  stem    = logos_tool_identifier_lower(setting != NULL ? setting : logos_languages_genesis_engine_name_get(engine));

  bind.grammar = logos_languages_genesis_engine_grammar_get(engine);
  bind.prefix  = stem;
  bind.upper   = logos_tool_identifier_upper(stem);
  bind.camel   = logos_tool_identifier_camel(stem);
  logos_tool_text_initialize(&bind.out);

  setting = logos_tool_mapping_setting_get(&mapping, "bind.guard");

  if (setting != NULL && setting[0] != '\0') {
    guard = logos_tool_identifier_upper(setting);
  } else {
    guard = (char *)nexus_memory_allocate(nexus_strings_string_length_get(bind.upper) + 16);
    (void)stbsp_snprintf(guard, (int)nexus_strings_string_length_get(bind.upper) + 16, "%s_BINDINGS_H", bind.upper);
  }

  l_tool_bind_generate(&bind, engine, guard);

  mapping_directory = logos_tool_files_directory_of(mapping.path != NULL ? mapping.path : options->grammar);
  setting           = options->output != NULL ? options->output : logos_tool_mapping_setting_get(&mapping, "bind.directory");
  directory         = options->output != NULL ? logos_tool_files_join(NULL, options->output)
                                              : logos_tool_files_join(mapping_directory, setting != NULL ? setting : "generated");
  setting           = logos_tool_mapping_setting_get(&mapping, "bind.file");

  if (setting != NULL && setting[0] != '\0') {
    file_name = logos_tool_files_join(NULL, setting);
  } else {
    file_name = (char *)nexus_memory_allocate(nexus_strings_string_length_get(stem) + 12);
    (void)stbsp_snprintf(file_name, (int)nexus_strings_string_length_get(stem) + 12, "%s_bindings.h", stem);
  }

  path   = logos_tool_files_join(directory, file_name);
  result = 0;

  if (!logos_tool_files_write_if_changed(path, logos_tool_text_data_get(&bind.out), logos_tool_text_length_get(&bind.out), &changed)) {
    (void)nexus_stdio_stderr_write_formatted("genesis: cannot write %s\n", path);
    result = 1;
  } else if (!options->quiet) {
    (void)nexus_stdio_stderr_write_formatted("genesis: %s %s\n", changed ? "wrote" : "unchanged", path);
  }

  nexus_memory_deallocate(path);
  nexus_memory_deallocate(file_name);
  nexus_memory_deallocate(directory);
  nexus_memory_deallocate(mapping_directory);
  nexus_memory_deallocate(guard);
  nexus_memory_deallocate(stem);
  nexus_memory_deallocate(bind.upper);
  nexus_memory_deallocate(bind.camel);
  logos_tool_text_release(&bind.out);
  logos_tool_mapping_release(&mapping);
  logos_languages_genesis_engine_release(engine);
  logos_languages_genesis_compiler_destroy(compiler);

  return result;
}
