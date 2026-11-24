#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
Writes a compiled language back as Genesis source from its grammar description through the same writer the built-in
grammars use. Templates are already expanded, so the text is self-contained; compiling it yields the same lexer and grammar.
*/

char *logos_languages_genesis_engine_export_text(const LGenesisEngine *engine) {
  const LGenesisGrammar *grammar;
  LGenesisWriter        *writer;
  char                  *text;
  uint32                 i;

  NEXUS_PROGRAM_PRECONDITION(engine != NULL && engine->grammar != NULL, "only a compiled language can be exported");

  grammar = engine->grammar;
  writer  = logos_languages_genesis_writer_create(grammar->name, grammar->version);

  if (grammar->imports[0] != '\0') {
    uint_large start;
    uint_large end;

    /* One IMPORT per line. */
    start = 0;

    for (end = 0; grammar->imports[end] != '\0'; end++) {
      if (grammar->imports[end] == '\n') {
        char *line;

        line = (char *)nexus_memory_allocate(end - start + 1);
        nexus_memory_bytes_copy(grammar->imports + start, line, end - start);
        line[end - start] = '\0';
        logos_languages_genesis_writer_import_add(writer, line);
        nexus_memory_deallocate(line);
        start = end + 1;
      }
    }
  }

  if (grammar->entry != NULL) {
    logos_languages_genesis_writer_parser_entry_set(writer, grammar->entry);
  }

  for (i = 0; i < grammar->global_count; i++) {
    logos_languages_genesis_writer_parser_global_add(writer, grammar->globals[i]);
  }

  if (grammar->choice != NULL) {
    logos_languages_genesis_writer_parser_choice_set(writer, grammar->choice);
  }

  logos_languages_genesis_writer_parser_grammar_expected_set(writer, grammar->grammar_expected);

  for (i = 0; i < grammar->context_count; i++) {
    logos_languages_genesis_writer_context_add(writer, grammar->contexts[i]);
  }

  for (i = 0; i < grammar->token_count; i++) {
    const LGenesisGrammarToken *token;

    token = &grammar->tokens[i];
    logos_languages_genesis_writer_token_add(writer, token->context, token->name, token->definition, token->priority, token->action,
                                             token->action_target, token->action_amount);
  }

  for (i = 0; i < grammar->rule_count; i++) {
    const LGenesisGrammarRule *rule;

    rule = &grammar->rules[i];

    switch (rule->kind) {
    case LGRK_PRATT:
      logos_languages_genesis_writer_pratt_add(writer, rule->name, rule->definition);
      break;
    case LGRK_EMBED:
      logos_languages_genesis_writer_embed_add(writer, rule->name, rule->definition);
      break;
    case LGRK_RULE:
    default:
      logos_languages_genesis_writer_rule_add(writer, rule->name, rule->annotations, rule->definition);
      break;
    }
  }

  text = logos_languages_genesis_writer_text_get(writer);
  logos_languages_genesis_writer_destroy(writer);

  return text;
}
