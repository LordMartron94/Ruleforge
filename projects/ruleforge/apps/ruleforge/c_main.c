#include <nexus.h>
#include <echo.h>

#include "../../ruleforge.h"

/*
The ruleforge command line. Products go to standard output (JSON or filter text) or to the files named with -o/--model,
diagnostics to standard error (or, with --json, to standard output as JSON). The exit status is 0 on success, 1 when the sources
have errors and 2 for a usage problem.
*/

static void c_main_usage(void) {
  (void)nexus_stdio_stderr_write_cstring("usage: ruleforge <command> [options] <entry.rf>\n\ncommands:\n");
  (void)nexus_stdio_stderr_write_cstring("  ast        parse the file and its imports; write the syntax trees as JSON\n");
  (void)nexus_stdio_stderr_write_cstring("  check      parse the file and its imports; report diagnostics only\n");
  (void)nexus_stdio_stderr_write_cstring("  compile    run main and write the item filter (and with --model the structured model)\n");
  (void)nexus_stdio_stderr_write_cstring("  simulate   compile, then run the items of --items through the filter and explain each result\n");
  (void)nexus_stdio_stderr_write_cstring("  tokens     write the lexical tokens of the file as JSON (for editors)\n");
  (void)nexus_stdio_stderr_write_cstring("  manifest   write everything the engine can express as JSON (conditions, properties, standard library)\n");
  (void)nexus_stdio_stderr_write_cstring("  version    print the engine version\n\noptions:\n");
  (void)nexus_stdio_stderr_write_cstring("  -I DIR             add a folder searched for imports (repeatable)\n");
  (void)nexus_stdio_stderr_write_cstring("  --settings FILE    a settings file (TOML): [context], [datasets.\"source\"], [limits]\n");
  (void)nexus_stdio_stderr_write_cstring("  --set KEY=VALUE    a compile-time value readable as @KEY (repeatable)\n");
  (void)nexus_stdio_stderr_write_cstring("  --dataset S=PATH   the snapshot of the dataset source S; csv or json by extension (repeatable)\n");
  (void)nexus_stdio_stderr_write_cstring("  --name NAME        the filter's name, readable as @filter\n");
  (void)nexus_stdio_stderr_write_cstring("  -o FILE            write the filter there instead of standard output\n");
  (void)nexus_stdio_stderr_write_cstring("  --model FILE       also write the structured model (JSON) there\n");
  (void)nexus_stdio_stderr_write_cstring("  --items FILE       simulate: the items, a JSON array\n");
  (void)nexus_stdio_stderr_write_cstring("  --json             write diagnostics as JSON on standard output\n");
}

/* Summary: writes text to a file, creating its folder; FALSE when it cannot be written. */
static boolean c_main_file_write(const char *path, const char *text) {
  NexusPath        file_path;
  NexusFileHandle *file;
  uint_large       written;
  uint_large       size;

  file_path = nexus_filesystem_path_create(path);
  size      = nexus_strings_string_length_get(text);

  if (nexus_filesystem_file_ensure_parent_directory(&file_path) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return FALSE;
  }

  if (nexus_filesystem_file_open(&file_path, NFM_WRITE_BINARY, &file) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return FALSE;
  }

  if (size > 0 && (nexus_filesystem_file_write(file, (const byte *)text, size, &written) != NEXUS_PROGRAM_ERROR_SUCCESS() || written != size)) {
    (void)nexus_filesystem_file_close(file);

    return FALSE;
  }

  return nexus_filesystem_file_close(file) == NEXUS_PROGRAM_ERROR_SUCCESS();
}

/* Summary: reads a whole file into a NUL-terminated buffer the caller releases; NULL when it cannot be read. */
static char *c_main_file_read(const char *path) {
  NexusPath        file_path;
  NexusFileHandle *file;
  uint_large       size;
  uint_large       read_size;
  boolean          reached_eof;
  char            *text;

  file_path = nexus_filesystem_path_create(path);

  if (nexus_filesystem_file_open(&file_path, NFM_READ_BINARY, &file) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return NULL;
  }

  if (nexus_filesystem_file_size_get(file, &size) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)nexus_filesystem_file_close(file);

    return NULL;
  }

  text        = (char *)nexus_memory_allocate(size + 1);
  read_size   = 0;
  reached_eof = FALSE;

  if (size > 0 && nexus_filesystem_file_read(file, (byte *)text, size, &read_size, &reached_eof) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    read_size = size + 1;
  }

  text[size] = '\0';
  (void)nexus_filesystem_file_close(file);

  if (read_size != size) {
    nexus_memory_deallocate(text);

    return NULL;
  }

  return text;
}

int main(int argument_count, char **arguments) {
  EchoRuntimeConfiguration configuration;
  EchoRuntime             *echo;
  EchoSystem              *system;
  NexusError              *error;
  RuleforgeRuntime        *runtime;
  RuleforgeCompilation    *compilation;
  RuleforgeSettings       *settings;
  const char              *command;
  const char              *file;
  const char              *output;
  const char              *model;
  const char              *items;
  boolean                  json_diagnostics;
  boolean                  ok;
  char                    *text;
  char                     message[512];
  int                      result;
  int                      i;

  if (argument_count >= 2 && nexus_strings_string_equals(arguments[1], "version")) {
    (void)nexus_stdio_stdout_write_formatted("ruleforge %s\n", ruleforge_version_get());

    return 0;
  }

  if (argument_count >= 2 && nexus_strings_string_equals(arguments[1], "manifest")) {
    char *manifest;

    manifest = ruleforge_manifest_json_write();
    (void)nexus_stdio_stdout_write_cstring(manifest);
    (void)nexus_stdio_stdout_write_cstring("\n");
    nexus_memory_deallocate(manifest);

    return 0;
  }

  if (argument_count < 3) {
    c_main_usage();

    return 2;
  }

  configuration = echo_runtime_configuration_default();
  system        = NULL;
  error         = echo_runtime_create(&configuration, &echo);

  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    error = echo_runtime_system_register(echo, "Ruleforge", NULL, &system);
  }

  if (error == NEXUS_PROGRAM_ERROR_SUCCESS()) {
    error = echo_runtime_start(echo);
  }

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)nexus_stdio_stderr_write_cstring("ruleforge: cannot start the logging runtime\n");

    return 1;
  }

  runtime          = ruleforge_runtime_create(echo, system);
  compilation      = ruleforge_compilation_create(runtime);
  settings         = ruleforge_settings_create();
  command          = arguments[1];
  file             = NULL;
  output           = NULL;
  model            = NULL;
  items            = NULL;
  json_diagnostics = FALSE;
  result           = 0;

  for (i = 2; i < argument_count && result == 0; i++) {
    if (nexus_strings_string_equals(arguments[i], "-I") && i + 1 < argument_count) {
      ruleforge_compilation_search_folder_add(compilation, arguments[++i]);
    } else if (nexus_strings_string_equals(arguments[i], "--json")) {
      json_diagnostics = TRUE;
    } else if (nexus_strings_string_equals(arguments[i], "--settings") && i + 1 < argument_count) {
      if (!ruleforge_settings_toml_load(runtime, settings, arguments[++i], message, NEXUS_SIZEOF(message))) {
        (void)nexus_stdio_stderr_write_formatted("ruleforge: %s\n", message);
        result = 2;
      }
    } else if (nexus_strings_string_equals(arguments[i], "--set") && i + 1 < argument_count) {
      const char *equals;
      char        key[256];

      i++;
      equals = nexus_strings_string_find(arguments[i], "=");

      if (equals == NULL || (uint_large)(equals - arguments[i]) >= NEXUS_SIZEOF(key)) {
        (void)nexus_stdio_stderr_write_formatted("ruleforge: --set needs KEY=VALUE, got %s\n", arguments[i]);
        result = 2;
      } else {
        nexus_memory_bytes_copy(arguments[i], key, (uint_large)(equals - arguments[i]));
        key[equals - arguments[i]] = '\0';
        ruleforge_settings_value_set(settings, key, equals + 1);
      }
    } else if (nexus_strings_string_equals(arguments[i], "--dataset") && i + 1 < argument_count) {
      const char *equals;
      char        source[256];

      i++;
      equals = nexus_strings_string_find(arguments[i], "=");

      if (equals == NULL || (uint_large)(equals - arguments[i]) >= NEXUS_SIZEOF(source)) {
        (void)nexus_stdio_stderr_write_formatted("ruleforge: --dataset needs SOURCE=PATH, got %s\n", arguments[i]);
        result = 2;
      } else {
        nexus_memory_bytes_copy(arguments[i], source, (uint_large)(equals - arguments[i]));
        source[equals - arguments[i]] = '\0';
        ruleforge_settings_dataset_set(settings, source, nexus_strings_string_ends_with(equals + 1, ".json") ? "json" : "csv", equals + 1, NULL);
      }
    } else if (nexus_strings_string_equals(arguments[i], "--name") && i + 1 < argument_count) {
      ruleforge_settings_filter_name_set(settings, arguments[++i]);
    } else if (nexus_strings_string_equals(arguments[i], "-o") && i + 1 < argument_count) {
      output = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "--model") && i + 1 < argument_count) {
      model = arguments[++i];
    } else if (nexus_strings_string_equals(arguments[i], "--items") && i + 1 < argument_count) {
      items = arguments[++i];
    } else if (arguments[i][0] == '-') {
      (void)nexus_stdio_stderr_write_formatted("ruleforge: unknown option %s\n", arguments[i]);
      result = 2;
    } else {
      file = arguments[i];
    }
  }

  if (result == 0 && file == NULL) {
    c_main_usage();
    result = 2;
  }

  if (result == 0 && !nexus_strings_string_equals(command, "ast") && !nexus_strings_string_equals(command, "check") &&
      !nexus_strings_string_equals(command, "compile") && !nexus_strings_string_equals(command, "simulate") &&
      !nexus_strings_string_equals(command, "tokens")) {
    (void)nexus_stdio_stderr_write_formatted("ruleforge: unknown command %s\n", command);
    result = 2;
  }

  if (result == 0 && nexus_strings_string_equals(command, "simulate") && items == NULL) {
    (void)nexus_stdio_stderr_write_cstring("ruleforge: simulate needs --items FILE\n");
    result = 2;
  }

  if (result == 0 && nexus_strings_string_equals(command, "tokens")) {
    char *source;

    source = c_main_file_read(file);

    if (source == NULL) {
      (void)nexus_stdio_stderr_write_formatted("ruleforge: cannot read %s\n", file);
      result = 1;
    } else {
      text = ruleforge_syntax_tokens_json_write(runtime, source, nexus_strings_string_length_get(source));
      (void)nexus_stdio_stdout_write_cstring(text);
      (void)nexus_stdio_stdout_write_cstring("\n");
      nexus_memory_deallocate(text);
      nexus_memory_deallocate(source);
    }
  } else if (result == 0) {
    ok = ruleforge_compilation_file_add(compilation, file);

    if (ok && (nexus_strings_string_equals(command, "compile") || nexus_strings_string_equals(command, "simulate"))) {
      ok = ruleforge_compilation_compile(compilation, settings);
    }

    if (ruleforge_compilation_diagnostic_count_get(compilation) > 0) {
      text = json_diagnostics ? ruleforge_compilation_diagnostics_json_write(compilation) : ruleforge_compilation_diagnostics_text_write(compilation);
      (void)(json_diagnostics ? nexus_stdio_stdout_write_cstring(text) : nexus_stdio_stderr_write_cstring(text));
      nexus_memory_deallocate(text);
    }

    if (ok && nexus_strings_string_equals(command, "ast")) {
      text = ruleforge_compilation_ast_json_write(compilation);
      (void)nexus_stdio_stdout_write_cstring(text);
      (void)nexus_stdio_stdout_write_cstring("\n");
      nexus_memory_deallocate(text);
    }

    if (ok && nexus_strings_string_equals(command, "compile")) {
      if (output != NULL) {
        if (!c_main_file_write(output, ruleforge_compilation_filter_get(compilation))) {
          (void)nexus_stdio_stderr_write_formatted("ruleforge: cannot write %s\n", output);
          ok = FALSE;
        }
      } else {
        (void)nexus_stdio_stdout_write_cstring(ruleforge_compilation_filter_get(compilation));
      }

      if (ok && model != NULL) {
        text = ruleforge_compilation_model_json_write(compilation);

        if (!c_main_file_write(model, text)) {
          (void)nexus_stdio_stderr_write_formatted("ruleforge: cannot write %s\n", model);
          ok = FALSE;
        }

        nexus_memory_deallocate(text);
      }
    }

    if (ok && nexus_strings_string_equals(command, "simulate")) {
      char *items_text;

      items_text = c_main_file_read(items);

      if (items_text == NULL) {
        (void)nexus_stdio_stderr_write_formatted("ruleforge: cannot read %s\n", items);
        ok = FALSE;
      } else {
        text = ruleforge_compilation_simulate_json_write(compilation, items_text);
        nexus_memory_deallocate(items_text);

        if (text == NULL) {
          (void)nexus_stdio_stderr_write_cstring("ruleforge: the items are not valid JSON\n");
          ok = FALSE;
        } else {
          (void)nexus_stdio_stdout_write_cstring(text);
          (void)nexus_stdio_stdout_write_cstring("\n");
          nexus_memory_deallocate(text);
        }
      }
    }

    result = ok ? 0 : 1;
  }

  ruleforge_settings_destroy(settings);
  ruleforge_compilation_destroy(compilation);
  ruleforge_runtime_destroy(runtime);
  (void)echo_runtime_close(echo);
  echo_runtime_destroy(echo);

  return result;
}
