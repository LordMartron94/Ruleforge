#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

static char *l_genesis_engine_copy(const char *text) {
  uint_large length;
  char      *copy;

  length = nexus_strings_string_length_get(text);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length + 1);

  return copy;
}

LGenesisEngine *logos_languages_genesis_engine_create(const char *name, const char *version, const char *source_name, LGenesisUnitKind kind) {
  LGenesisEngine *engine;

  engine                   = (LGenesisEngine *)nexus_memory_allocate(NEXUS_SIZEOF(*engine));
  engine->holds            = 1;
  engine->name             = l_genesis_engine_copy(name);
  engine->version          = version != NULL ? l_genesis_engine_copy(version) : NULL;
  engine->source_name      = l_genesis_engine_copy(source_name);
  engine->kind             = kind;
  engine->lexer            = NULL;
  engine->parser           = NULL;
  engine->contexta         = NULL;
  engine->global_rules     = NULL;
  engine->global_count     = 0;
  engine->choice           = LCCM_DEFAULT;
  engine->grammar_expected = FALSE;
  engine->imports          = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  engine->guests           = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  engine->guest_parsers    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  engine->labels           = NULL;
  engine->grammar          = NULL;

  return engine;
}

void logos_languages_genesis_engine_retain(LGenesisEngine *engine) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL, "engine must not be NULL");

  engine->holds++;
}

void logos_languages_genesis_engine_release(LGenesisEngine *engine) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(engine != NULL && engine->holds > 0, "engine must not be NULL and must hold at least once");

  engine->holds--;

  if (engine->holds > 0) {
    return;
  }

  /* The parser borrows the lexer, so it goes first; guests are borrowed by the parser, so they go last. */
  if (engine->parser != NULL) {
    logos_language_tooling_parsing_parser_destroy(engine->parser);
  }

  for (i = 0; i < nexus_data_list_length_get(engine->guest_parsers); i++) {
    logos_language_tooling_parsing_parser_destroy((LParser *)nexus_data_list_pointer_at(engine->guest_parsers, i));
  }

  if (engine->lexer != NULL) {
    logos_language_tooling_lexing_lexer_destroy(engine->lexer);
  }

  if (engine->contexta != NULL) {
    logos_languages_contexta_contexta_destroy(engine->contexta);
  }

  if (engine->labels != NULL) {
    nexus_data_arena_release(engine->labels);
    nexus_memory_deallocate(engine->labels);
  }

  if (engine->global_rules != NULL) {
    nexus_memory_deallocate((void *)engine->global_rules);
  }

  for (i = 0; i < nexus_data_list_length_get(engine->guests); i++) {
    logos_languages_genesis_engine_release((LGenesisEngine *)nexus_data_list_pointer_at(engine->guests, i));
  }

  for (i = 0; i < nexus_data_list_length_get(engine->imports); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(engine->imports, i));
  }

  nexus_data_list_destroy(engine->imports);
  nexus_data_list_destroy(engine->guests);
  nexus_data_list_destroy(engine->guest_parsers);
  nexus_memory_deallocate(engine->name);
  nexus_memory_deallocate(engine->source_name);

  if (engine->version != NULL) {
    nexus_memory_deallocate(engine->version);
  }

  nexus_memory_deallocate(engine);
}

LGenesisUnitKind logos_languages_genesis_engine_kind_get(const LGenesisEngine *engine) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL, "engine must not be NULL");

  return engine->kind;
}

const char *logos_languages_genesis_engine_name_get(const LGenesisEngine *engine) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL, "engine must not be NULL");

  return engine->name;
}

const char *logos_languages_genesis_engine_version_get(const LGenesisEngine *engine) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL, "engine must not be NULL");

  return engine->version;
}

const char *logos_languages_genesis_engine_source_name_get(const LGenesisEngine *engine) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL, "engine must not be NULL");

  return engine->source_name;
}

LLexer *logos_languages_genesis_engine_lexer_get(const LGenesisEngine *engine) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL, "engine must not be NULL");

  return engine->lexer;
}

LParser *logos_languages_genesis_engine_parser_get(const LGenesisEngine *engine) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL, "engine must not be NULL");

  return engine->parser;
}

uint_large logos_languages_genesis_engine_import_count_get(const LGenesisEngine *engine) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL, "engine must not be NULL");

  return nexus_data_list_length_get(engine->imports);
}

const char *logos_languages_genesis_engine_import_at(const LGenesisEngine *engine, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL && index < nexus_data_list_length_get(engine->imports), "import index must be in bounds");

  return (const char *)nexus_data_list_pointer_at(engine->imports, index);
}

const LGenesisGrammar *logos_languages_genesis_engine_grammar_get(const LGenesisEngine *engine) {
  NEXUS_PROGRAM_PRECONDITION(engine != NULL, "engine must not be NULL");

  return engine->grammar;
}
