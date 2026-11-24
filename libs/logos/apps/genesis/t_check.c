#include <nexus.h>
#include <logos.h>

#include "tool.h"

/* Summary: parses one file with a parser; TRUE when the whole file is accepted. */
static boolean l_tool_check_file(LParser *parser, const char *path) {
  LParserProcess *process;
  LParserTree    *tree;
  LParserStatus   status;
  byte           *bytes;
  uint_large      size;
  NexusError     *error;

  if (!logos_tool_files_read(path, &bytes, &size)) {
    (void)nexus_stdio_stderr_write_formatted("genesis: cannot read %s\n", path);

    return FALSE;
  }

  process = logos_language_tooling_parsing_parser_process_create(parser);
  error   = logos_language_tooling_parsing_parser_process_chunk_add(process, bytes, size, TRUE);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }

  do {
    status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
  } while (status == LPS_PROGRESS);

  tree = logos_language_tooling_parsing_parser_process_tree_take(process);

  if (status != LPS_ACCEPTED && logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(tree) > 0) {
    char message[512];

    (void)logos_language_tooling_parsing_syntax_tree_diagnostic_message_write(tree, 0, message, sizeof(message));
    (void)nexus_stdio_stderr_write_formatted("%s: %s\n", path, message);
  }

  logos_language_tooling_parsing_syntax_tree_destroy(tree);
  logos_language_tooling_parsing_parser_process_destroy(process);
  nexus_memory_deallocate(bytes);

  return status == LPS_ACCEPTED;
}

int logos_tool_check_run(const LToolOptions *options) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  uint_large        i;
  uint32            rejected;

  engine = logos_tool_grammar_compile(options, &compiler);

  if (engine == NULL) {
    logos_languages_genesis_compiler_destroy(compiler);

    return 1;
  }

  rejected = 0;

  for (i = 0; i < nexus_data_list_length_get(options->files); i++) {
    if (!l_tool_check_file(logos_languages_genesis_engine_parser_get(engine), (const char *)nexus_data_list_pointer_at(options->files, i))) {
      rejected++;
    }
  }

  logos_languages_genesis_engine_release(engine);
  logos_languages_genesis_compiler_destroy(compiler);

  return rejected == 0 ? 0 : 1;
}
