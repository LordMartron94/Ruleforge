#include <nexus.h>
#include <logos.h>

#include "tool.h"

/* Summary: milliseconds between two readings of the monotonic clock. */
static double l_bench_milliseconds(NexusTime start, NexusTime end) {
  return (double)nexus_time_duration_between(start, end).nanoseconds / 1000000.0;
}

/* Summary: parses a file once; returns the status. The tree is destroyed. */
static LParserStatus l_bench_parse(LParser *parser, const byte *bytes, uint_large size, uint_large *out_tokens, uint_large *out_nodes,
                                   uint_large *out_alternatives) {
  LParserProcess *process;
  LParserStatus   status;
  LParserTree    *tree;

  process = logos_language_tooling_parsing_parser_process_create(parser);

  if (logos_language_tooling_parsing_parser_process_chunk_add(process, bytes, size, TRUE) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    logos_language_tooling_parsing_parser_process_destroy(process);

    return LPS_LIMIT;
  }

  do {
    status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
  } while (status == LPS_PROGRESS);

  tree              = logos_language_tooling_parsing_parser_process_tree_take(process);
  *out_tokens       = logos_language_tooling_parsing_syntax_tree_token_count_get(tree);
  *out_nodes        = logos_language_tooling_parsing_syntax_tree_node_count_get(tree);
  *out_alternatives = logos_language_tooling_parsing_syntax_tree_alternative_count_get(tree);
  logos_language_tooling_parsing_syntax_tree_destroy(tree);
  logos_language_tooling_parsing_parser_process_destroy(process);

  return status;
}

int logos_tool_bench_run(const LToolOptions *options) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  NexusTime         start;
  NexusTime         end;
  double            compile_milliseconds;
  double            best;
  double            total;
  uint32            run;
  uint32            files;
  uint_large        i;

  /* Compiling: a fresh compiler each time, since imports are cached in one. */
  total = 0.0;
  best  = 0.0;

  for (run = 0; run < options->iterations; run++) {
    LToolOptions copy;

    copy       = *options;
    copy.quiet = TRUE;
    start      = nexus_time_get_monotonic();
    engine     = logos_tool_grammar_compile(&copy, &compiler);
    end        = nexus_time_get_monotonic();

    if (engine == NULL) {
      logos_languages_genesis_compiler_destroy(compiler);

      return 1;
    }

    compile_milliseconds = l_bench_milliseconds(start, end);
    total += compile_milliseconds;
    best = run == 0 || compile_milliseconds < best ? compile_milliseconds : best;

    if (run + 1 < options->iterations) {
      logos_languages_genesis_engine_release(engine);
      logos_languages_genesis_compiler_destroy(compiler);
    }
  }

  (void)nexus_stdio_stdout_write_formatted("compile  best %8.3f ms  mean %8.3f ms  (%u runs)\n", best,
                                           options->iterations > 0 ? total / (double)options->iterations : 0.0, (unsigned)options->iterations);

  files = 0;

  for (i = 0; i < nexus_data_list_length_get(options->files); i++) {
    byte         *bytes;
    uint_large    size;
    uint_large    tokens;
    uint_large    nodes;
    uint_large    alternatives;
    LParserStatus status;

    if (!logos_tool_files_read((const char *)nexus_data_list_pointer_at(options->files, i), &bytes, &size)) {
      (void)nexus_stdio_stderr_write_formatted("genesis: cannot read %s\n", (const char *)nexus_data_list_pointer_at(options->files, i));
      continue;
    }

    tokens       = 0;
    nodes        = 0;
    alternatives = 0;
    status       = LPS_ACCEPTED;
    total        = 0.0;
    best         = 0.0;

    for (run = 0; run < options->iterations; run++) {
      double milliseconds;

      start  = nexus_time_get_monotonic();
      status = l_bench_parse(logos_languages_genesis_engine_parser_get(engine), bytes, size, &tokens, &nodes, &alternatives);
      end    = nexus_time_get_monotonic();

      milliseconds = l_bench_milliseconds(start, end);
      total += milliseconds;
      best = run == 0 || milliseconds < best ? milliseconds : best;
    }

    (void)nexus_stdio_stdout_write_formatted("parse    best %8.3f ms  mean %8.3f ms  %8.2f MB/s  %lu bytes  %lu tokens  %s  %s\n", best,
                                             options->iterations > 0 ? total / (double)options->iterations : 0.0,
                                             best > 0.0 ? ((double)size / 1048576.0) / (best / 1000.0) : 0.0, (unsigned long)size,
                                             (unsigned long)tokens, status == LPS_ACCEPTED ? "accepted" : "REJECTED",
                                             (const char *)nexus_data_list_pointer_at(options->files, i));
    (void)nexus_stdio_stdout_write_formatted("         %lu nodes (%.2f per token), %lu alternatives, about %.1f MB of tree\n", (unsigned long)nodes,
                                             tokens > 0 ? (double)nodes / (double)tokens : 0.0, (unsigned long)alternatives,
                                             ((double)nodes * 32.0 + (double)alternatives * 16.0 + (double)tokens * 24.0) / 1048576.0);
    nexus_memory_deallocate(bytes);
    files++;
  }

  logos_languages_genesis_engine_release(engine);
  logos_languages_genesis_compiler_destroy(compiler);

  return files == nexus_data_list_length_get(options->files) ? 0 : 1;
}
