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
