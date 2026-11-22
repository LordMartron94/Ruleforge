#include <nexus.h>
#include <logos.h>

#include "tool.h"

/* Summary: the definition of a built-in language, and its editor mapping text, or NULL for an unknown name. */
static LGenesisDefinition *l_tool_builtin(const char *name, LToolText *mapping) {
  if (nexus_strings_string_equals(name, "genesis")) {
    logos_tool_spec_genesis_mapping_write(mapping);

    return logos_languages_genesis_spec_definition_create();
  }

  if (nexus_strings_string_equals(name, "mapping")) {
    logos_tool_spec_mapping_mapping_write(mapping);

    return logos_tool_spec_mapping_definition_create();
  }

  return NULL;
}

static int l_tool_output_write(const char *path, const char *text, const LToolOptions *options) {
  boolean changed;

  if (path == NULL) {
    (void)nexus_stdio_stdout_write_cstring(text);

    return 0;
  }

  if (!logos_tool_files_write_if_changed(path, text, nexus_strings_string_length_get(text), &changed)) {
    (void)nexus_stdio_stderr_write_formatted("genesis: cannot write %s\n", path);

    return 1;
  }

  if (!options->quiet) {
    (void)nexus_stdio_stderr_write_formatted("genesis: %s %s\n", changed ? "wrote" : "unchanged", path);
  }

  return 0;
}

int logos_tool_export_run(const LToolOptions *options) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  char             *text;
  int               result;

  if (options->builtin != NULL) {
    LToolText           mapping;
    LGenesisDefinition *definition;

    logos_tool_text_initialize(&mapping);
    definition = l_tool_builtin(options->builtin, &mapping);

    if (definition == NULL) {
      (void)nexus_stdio_stderr_write_formatted("genesis: no built-in grammar named %s (genesis, mapping)\n", options->builtin);
      logos_tool_text_release(&mapping);

      return 2;
    }

    text = logos_languages_genesis_definition_export_text(definition);

    if (text == NULL) {
      (void)nexus_stdio_stderr_write_cstring("genesis: the built-in language has an operation without a Genesis form\n");
      logos_languages_genesis_definition_destroy(definition);
      logos_tool_text_release(&mapping);

      return 1;
    }

    result = l_tool_output_write(options->output, text, options);

    if (result == 0 && options->mapping_output != NULL) {
      result = l_tool_output_write(options->mapping_output, logos_tool_text_data_get(&mapping), options);
    }

    nexus_memory_deallocate(text);
    logos_languages_genesis_definition_destroy(definition);
    logos_tool_text_release(&mapping);

    return result;
  }

  engine = logos_tool_grammar_compile(options, &compiler);

  if (engine == NULL) {
    logos_languages_genesis_compiler_destroy(compiler);

    return 1;
  }

  text   = logos_languages_genesis_engine_export_text(engine);
  result = l_tool_output_write(options->output, text, options);
  nexus_memory_deallocate(text);
  logos_languages_genesis_engine_release(engine);
  logos_languages_genesis_compiler_destroy(compiler);

  return result;
}
