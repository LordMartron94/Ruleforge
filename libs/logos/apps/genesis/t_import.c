#include <nexus.h>
#include <logos.h>

#include "tool.h"

/* Summary: the name of a language for a file: its stem, upper-cased at the first letter and at each word start. */
static char *l_import_name(const char *path) {
  char      *stem;
  char      *name;
  uint_large length;
  uint_large i;
  uint_large out;
  boolean    start;

  stem   = logos_tool_files_stem_of(path);
  length = nexus_strings_string_length_get(stem);
  name   = (char *)nexus_memory_allocate(length + 2);
  out    = 0;
  start  = TRUE;

  for (i = 0; i < length; i++) {
    char letter;

    letter = stem[i];

    if (!((letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') || (letter >= '0' && letter <= '9'))) {
      start = TRUE;
      continue;
    }

    if (start && letter >= 'a' && letter <= 'z') {
      letter = (char)(letter - 'a' + 'A');
    }

    name[out++] = letter;
    start       = FALSE;
  }

  if (out == 0 || (name[0] >= '0' && name[0] <= '9')) {
    nexus_memory_bytes_move(name + 1, name, out);
    name[0] = 'G';
    out++;
  }

  name[out] = '\0';
  nexus_memory_deallocate(stem);

  return name;
}

int logos_tool_import_run(const LToolOptions *options) {
  LGenesisCompiler  *compiler;
  LInterchangeInput *inputs;
  uint32             input_count;
  char              *genesis;
  char              *report;
  char              *name;
  uint_large         i;
  boolean            ok;
  boolean            changed;
  const char        *input;

  if (options->grammar == NULL || nexus_data_list_length_get(options->files) == 0) {
    uint32 index;

    (void)nexus_stdio_stderr_write_cstring(
        "usage: genesis import FORMAT grammar-file... [-o out.gen] [--name Language] [-I folder]...\n\nformats:\n");

    for (index = 0; index < logos_languages_interchange_format_count_get(); index++) {
      (void)nexus_stdio_stderr_write_formatted("  %-14s %s\n", logos_languages_interchange_format_name_at(index),
                                               logos_languages_interchange_format_description_at(index));
    }

    return 2;
  }

  input_count = (uint32)nexus_data_list_length_get(options->files);
  inputs      = (LInterchangeInput *)nexus_memory_allocate(input_count * NEXUS_SIZEOF(*inputs));
  input       = (const char *)nexus_data_list_pointer_at(options->files, 0);

  for (i = 0; i < input_count; i++) {
    byte      *bytes;
    uint_large size;

    inputs[i].name = (const char *)nexus_data_list_pointer_at(options->files, i);

    if (!logos_tool_files_read(inputs[i].name, &bytes, &size)) {
      (void)nexus_stdio_stderr_write_formatted("genesis: cannot read %s\n", inputs[i].name);

      return 1;
    }

    inputs[i].text = bytes;
    inputs[i].size = size;
  }

  compiler = logos_languages_genesis_compiler_create(options->runtime);

  for (i = 0; i < nexus_data_list_length_get(options->folders); i++) {
    logos_languages_genesis_compiler_search_folder_add(compiler, (const char *)nexus_data_list_pointer_at(options->folders, i));
  }

#ifdef LOGOS_TOOL_STDLIB_DIR
  /* The grammars of the formats live next to the standard grammars. */
  {
    char *formats;

    logos_languages_genesis_compiler_search_folder_add(compiler, LOGOS_TOOL_STDLIB_DIR);
    formats = logos_tool_files_join(LOGOS_TOOL_STDLIB_DIR, "formats");
    logos_languages_genesis_compiler_search_folder_add(compiler, formats);
    nexus_memory_deallocate(formats);
  }
#endif

  name = options->prefix != NULL ? NULL : l_import_name(input);
  ok   = logos_languages_interchange_convert(compiler, options->grammar, options->prefix != NULL ? options->prefix : name, inputs, input_count,
                                             &genesis, &report);

  if (report == NULL) {
    (void)nexus_stdio_stderr_write_formatted("genesis: unknown format %s\n", options->grammar);
  } else if (report[0] != '\0') {
    (void)nexus_stdio_stderr_write_cstring(report);
  }

  if (!ok) {
    (void)nexus_stdio_stderr_write_formatted("genesis: %s cannot be converted\n", input);
  } else if (options->output != NULL) {
    changed = FALSE;

    if (!logos_tool_files_write_if_changed(options->output, genesis, nexus_strings_string_length_get(genesis), &changed)) {
      (void)nexus_stdio_stderr_write_formatted("genesis: cannot write %s\n", options->output);
      ok = FALSE;
    }
  } else {
    (void)nexus_stdio_stdout_write_cstring(genesis);
  }

  if (genesis != NULL) {
    nexus_memory_deallocate(genesis);
  }

  if (report != NULL) {
    nexus_memory_deallocate(report);
  }

  if (name != NULL) {
    nexus_memory_deallocate(name);
  }

  for (i = 0; i < input_count; i++) {
    nexus_memory_deallocate((void *)inputs[i].text);
  }

  nexus_memory_deallocate(inputs);
  logos_languages_genesis_compiler_destroy(compiler);

  return ok ? 0 : 1;
}
