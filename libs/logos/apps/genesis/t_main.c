
#include <nexus.h>
#include <echo.h>
#include <logos.h>
#include <pharos.h>

#include "tool.h"

static boolean l_tool_sink_filter(const EchoRecord *record, void *user_data) {
  (void)record;
  (void)user_data;

  return FALSE;
}

LGenesisEngine *logos_tool_grammar_compile(const LToolOptions *options, LGenesisCompiler **out_compiler) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  uint_large        i;
  char             *text;

  compiler = logos_languages_genesis_compiler_create(options->runtime);

  for (i = 0; i < nexus_data_list_length_get(options->folders); i++) {
    logos_languages_genesis_compiler_search_folder_add(compiler, (const char *)nexus_data_list_pointer_at(options->folders, i));
  }

  /* Imports also resolve next to the grammar itself. */
  {
    char *directory;

    directory = logos_tool_files_directory_of(options->grammar);
    logos_languages_genesis_compiler_search_folder_add(compiler, directory);
    nexus_memory_deallocate(directory);
  }

  engine = logos_languages_genesis_compiler_compile_file(compiler, options->grammar);

  if (logos_languages_genesis_compiler_diagnostic_count_get(compiler) > 0 && (engine == NULL || !options->quiet)) {
    text = logos_languages_genesis_compiler_diagnostics_render(compiler, FALSE);
    (void)nexus_stdio_stderr_write_cstring(text);
    nexus_memory_deallocate(text);
  }

  if (engine != NULL && logos_languages_genesis_engine_kind_get(engine) != LGUK_LANGUAGE) {
    (void)nexus_stdio_stderr_write_formatted("genesis: %s is a library; tools need a language\n", options->grammar);
    logos_languages_genesis_engine_release(engine);
    engine = NULL;
  }

  *out_compiler = compiler;

  return engine;
}

boolean logos_tool_mapping_for(const LToolOptions *options, LToolMapping *mapping) {
  char   *path;
  char   *directory;
  char   *stem;
  char   *name;
  char    error[512];
  boolean found;
  boolean ok;

  if (options->mapping != NULL) {
    path = (char *)nexus_memory_allocate(nexus_strings_string_length_get(options->mapping) + 1);
    nexus_memory_bytes_copy(options->mapping, path, nexus_strings_string_length_get(options->mapping) + 1);
  } else {
    directory = logos_tool_files_directory_of(options->grammar);
    stem      = logos_tool_files_stem_of(options->grammar);
    name      = (char *)nexus_memory_allocate(nexus_strings_string_length_get(stem) + 9);
    (void)stbsp_snprintf(name, (int)(nexus_strings_string_length_get(stem) + 9), "%s.mapping", stem);
    path = logos_tool_files_join(directory, name);
    nexus_memory_deallocate(directory);
    nexus_memory_deallocate(stem);
    nexus_memory_deallocate(name);
  }

  ok = logos_tool_mapping_load(mapping, path, &found, error, sizeof(error));

  if (!ok) {
    (void)nexus_stdio_stderr_write_formatted("genesis: %s\n", error);
  } else if (options->mapping != NULL && !found) {
    (void)nexus_stdio_stderr_write_formatted("genesis: cannot read the mapping file %s\n", path);
    ok = FALSE;
  }

  nexus_memory_deallocate(path);

  return ok;
}

static void l_tool_usage(void) {
  (void)nexus_stdio_stderr_write_cstring("usage: genesis <command> [options] <grammar.gen>\n");
  (void)nexus_stdio_stderr_write_cstring("\n");
  (void)nexus_stdio_stderr_write_cstring("commands:\n");
  (void)nexus_stdio_stderr_write_cstring("  export    write the grammar as self-contained Genesis source\n");
  (void)nexus_stdio_stderr_write_cstring("  check     parse files with a grammar: genesis check grammar.gen file...\n");
  (void)nexus_stdio_stderr_write_cstring("  config    show the settings that apply (genesis.toml of the project, or --config file)\n");
  (void)nexus_stdio_stderr_write_cstring(
      "  bench     time compiling a grammar and parsing a file with it: genesis bench grammar.gen file [-n runs]\n");
  (void)nexus_stdio_stderr_write_cstring("  bind      generate typed C bindings\n");
  (void)nexus_stdio_stderr_write_cstring("  sublime   generate a Sublime Text syntax (and syntax tests)\n");
  (void)nexus_stdio_stderr_write_cstring("  lsp       run a language server over stdio: genesis lsp [-I dir] grammar.gen...\n");
  (void)nexus_stdio_stderr_write_cstring("\n");
  (void)nexus_stdio_stderr_write_cstring("options:\n");
  (void)nexus_stdio_stderr_write_cstring("  -I DIR          add an import search folder (repeatable)\n");
  (void)nexus_stdio_stderr_write_cstring("  --config FILE   the TOML settings file (default: genesis.toml in the project root)\n");
  (void)nexus_stdio_stderr_write_cstring(
      "  --disable NAME  switch a parser optimization off (repeatable; also `[parser] disable`), to compare or to revert it\n");
  (void)nexus_stdio_stderr_write_cstring("  -m FILE         the mapping file (default: <grammar>.mapping next to the grammar)\n");
  (void)nexus_stdio_stderr_write_cstring("  -o PATH         the output file or directory\n");
  (void)nexus_stdio_stderr_write_cstring("  -t DIR          the directory for generated tests\n");
  (void)nexus_stdio_stderr_write_cstring("  --prefix NAME   override the identifier prefix\n");
  (void)nexus_stdio_stderr_write_cstring("  --install DIR   also write the Sublime syntax to DIR (for example Packages/User)\n");
  (void)nexus_stdio_stderr_write_cstring("  --builtin NAME  export a grammar the tool knows: genesis or mapping (with -M for its mapping file)\n");
  (void)nexus_stdio_stderr_write_cstring("  -q              do not print warnings\n");
}

int main(int argument_count, char **arguments) {
  EchoRuntimeConfiguration configuration;
  EchoRuntime             *echo;
  EchoSystem              *system = NULL;
  EchoSink                *sink   = NULL;
  NexusError              *error;
  LogosRuntime            *logos;
  PharosRuntime           *pharos;
  LToolOptions             options;
  const char              *command;
  int                      result;
  int                      i;
  int                      j;

  if (argument_count < 2) {
    l_tool_usage();

    return 2;
  }

  command = arguments[1];

  configuration = echo_runtime_configuration_default();
  error         = echo_runtime_create(&configuration, &echo);

  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    error = echo_runtime_system_register(echo, "Genesis", NULL, &system);
  }

  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    error = echo_sink_rich_terminal_create(l_tool_sink_filter, NULL, echo_rich_terminal_configuration_ansi_create(NTP_NANO_SECOND), &sink);
  }

  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    error = echo_runtime_sink_register(echo, sink);
  }

  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    error = echo_runtime_start(echo);
  }

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)nexus_stdio_stderr_write_cstring("genesis: cannot start the logging runtime\n");

    return 1;
  }

  logos  = logos_runtime_create(echo, system);
  pharos = pharos_runtime_create(echo, system);

  options.runtime        = logos;
  options.pharos         = pharos;
  options.folders        = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  options.grammar        = NULL;
  options.mapping        = NULL;
  options.output         = NULL;
  options.tests          = NULL;
  options.prefix         = NULL;
  options.builtin        = NULL;
  options.iterations     = 20;
  options.install        = NULL;
  options.files          = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  options.mapping_output = NULL;
  options.config         = NULL;
  options.quiet          = FALSE;
  result                 = 0;

  for (i = 2; i < argument_count; i++) {
    if (nexus_strings_string_equals(arguments[i], "-I") && i + 1 < argument_count) {
      (void)nexus_data_list_append_pointer(options.folders, arguments[++i]);
    } else if (nexus_strings_string_equals(arguments[i], "-m") && i + 1 < argument_count) {
      options.mapping = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "-o") && i + 1 < argument_count) {
      options.output = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "-t") && i + 1 < argument_count) {
      options.tests = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "--prefix") && i + 1 < argument_count) {
      options.prefix = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "-n") && i + 1 < argument_count) {
      options.iterations = 0;
      i++;

      for (j = 0; arguments[i][j] >= '0' && arguments[i][j] <= '9'; j++) {
        options.iterations = (options.iterations * 10) + (uint32)(arguments[i][j] - '0');
      }
    } else if (nexus_strings_string_equals(arguments[i], "--install") && i + 1 < argument_count) {
      options.install = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "--builtin") && i + 1 < argument_count) {
      options.builtin = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "-M") && i + 1 < argument_count) {
      options.mapping_output = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "--config") && i + 1 < argument_count) {
      options.config = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "-q")) {
      options.quiet = TRUE;
    } else if (arguments[i][0] == '-' && !nexus_strings_string_equals(command, "lsp")) {
      (void)nexus_stdio_stderr_write_formatted("genesis: unknown option %s\n", arguments[i]);
      result = 2;
    } else if (options.grammar == NULL) {
      options.grammar = arguments[i];
    } else {
      (void)nexus_data_list_append_pointer(options.files, arguments[i]);
    }
  }

  if (result == 0 && nexus_strings_string_equals(command, "lsp")) {
    result = logos_tool_lsp_run(argument_count - 2, arguments + 2, &options);
  } else if (result == 0 && (options.grammar != NULL || options.builtin != NULL) && nexus_strings_string_equals(command, "export")) {
    result = logos_tool_export_run(&options);
  } else if (result == 0 && options.grammar != NULL && nexus_strings_string_equals(command, "bench")) {
    result = logos_tool_bench_run(&options);
  } else if (result == 0 && options.grammar != NULL && nexus_strings_string_equals(command, "check")) {
    result = logos_tool_check_run(&options);
  } else if (result == 0 && nexus_strings_string_equals(command, "config")) {
    result = logos_tool_config_run(&options);
  } else if (result == 0 && nexus_strings_string_equals(command, "import")) {
    result = logos_tool_import_run(&options);
  } else if (result == 0 && options.grammar != NULL && nexus_strings_string_equals(command, "bind")) {
    result = logos_tool_bind_run(&options);
  } else if (result == 0 && options.grammar != NULL && nexus_strings_string_equals(command, "sublime")) {
    result = logos_tool_sublime_run(&options);
  } else {
    l_tool_usage();
    result = 2;
  }

  nexus_data_list_destroy(options.files);
  nexus_data_list_destroy(options.folders);
  pharos_runtime_destroy(pharos);
  logos_runtime_destroy(logos);
  (void)echo_runtime_close(echo);
  echo_runtime_destroy(echo);

  return result;
}
