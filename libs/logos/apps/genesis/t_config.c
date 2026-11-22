#include <nexus.h>
#include <logos.h>

#include "tool.h"

/*
The configuration of the tool, a TOML file named genesis.toml in the root of a project (or the file given with --config). It is read with
the Genesis TOML grammar of the standard library, so the tool eats its own cooking. A key is addressed as "table.key" ("lsp.cache"); the
values stay as the text of the file and are read as a string, an integer, a boolean or a list of strings on request.
*/

#define LIC_KEY_MAX 160

static boolean l_config_is(const LParserTree *tree, const LParserGreenNode *node, const char *label) {
  const char *text;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) != LPGK_RULE) {
    return FALSE;
  }

  text = logos_language_tooling_parsing_syntax_green_node_label_get(tree, node);

  return text != NULL && nexus_strings_string_equals(text, label);
}

/* Summary: the first rule node labelled `label` at or below node, looking through the nodes that carry no label. */
static const LParserGreenNode *l_config_find(const LParserTree *tree, const LParserGreenNode *node, const char *label) {
  const LParserGreenAlternative *alternative;
  uint32                         i;

  if (l_config_is(tree, node, label)) {
    return node;
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative == NULL) {
    return NULL;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    const LParserGreenNode *found;

    found = l_config_find(tree, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), label);

    if (found != NULL) {
      return found;
    }
  }

  return NULL;
}

/* Summary: appends the text of every token below node, which is the text of the node, since the tree keeps every byte. */
static void l_config_text_append(const LParserTree *tree, NexusStringBuilder *out, const LParserGreenNode *node) {
  const LParserGreenAlternative *alternative;
  uint32                         i;

  if (logos_language_tooling_parsing_syntax_green_node_kind_get(node) == LPGK_TOKEN) {
    uint_large  size;
    const byte *text;

    text = logos_language_tooling_parsing_syntax_green_node_text_get(tree, node, &size);

    if (text != NULL && size > 0) {
      (void)nexus_strings_string_builder_append_formatted(out, "%.*s", (int)size, (const char *)text);
    }

    return;
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative == NULL) {
    return;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    l_config_text_append(tree, out, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i));
  }
}

/* Summary: a copy of the text of a node with white space at both ends removed. */
static char *l_config_text(const LParserTree *tree, const LParserGreenNode *node) {
  NexusStringBuilder builder;
  const char        *text;
  uint_large         size;
  uint_large         start;
  char              *copy;

  builder = nexus_strings_string_builder_create_growable(64);
  l_config_text_append(tree, &builder, node);
  text  = nexus_strings_string_builder_string_get(&builder);
  size  = nexus_strings_string_builder_length_get(&builder);
  start = 0;

  while (start < size && (text[start] == ' ' || text[start] == '\t' || text[start] == '\n' || text[start] == '\r')) {
    start++;
  }

  while (size > start && (text[size - 1] == ' ' || text[size - 1] == '\t' || text[size - 1] == '\n' || text[size - 1] == '\r')) {
    size--;
  }

  copy = (char *)nexus_memory_allocate(size - start + 1);

  if (size > start) {
    nexus_memory_bytes_copy(text + start, copy, size - start);
  }

  copy[size - start] = '\0';
  nexus_strings_string_builder_destroy(&builder);

  return copy;
}

/* Summary: a key without its quotes and the white space around its dots: `a . "b c"` is `a.b c`. */
static char *l_config_key_normalize(const char *key) {
  uint_large length;
  uint_large i;
  uint_large out;
  char      *result;
  char       quote;

  length = nexus_strings_string_length_get(key);
  result = (char *)nexus_memory_allocate(length + 1);
  out    = 0;
  quote  = '\0';

  for (i = 0; i < length; i++) {
    if (quote != '\0') {
      if (key[i] == quote) {
        quote = '\0';
      } else {
        result[out++] = key[i];
      }
    } else if (key[i] == '"' || key[i] == '\'') {
      quote = key[i];
    } else if (key[i] != ' ' && key[i] != '\t') {
      result[out++] = key[i];
    }
  }

  result[out] = '\0';

  return result;
}

static void l_config_add(LToolConfig *config, const char *section, const char *key, const char *value) {
  char  full[LIC_KEY_MAX];
  char *normal;
  char *entry_key;
  char *entry_value;

  normal = l_config_key_normalize(key);

  if (section[0] != '\0') {
    (void)stbsp_snprintf(full, (int)sizeof(full), "%s.%s", section, normal);
  } else {
    (void)stbsp_snprintf(full, (int)sizeof(full), "%s", normal);
  }

  nexus_memory_deallocate(normal);
  entry_key   = (char *)nexus_memory_allocate(nexus_strings_string_length_get(full) + 1);
  entry_value = (char *)nexus_memory_allocate(nexus_strings_string_length_get(value) + 1);
  nexus_memory_bytes_copy(full, entry_key, nexus_strings_string_length_get(full) + 1);
  nexus_memory_bytes_copy(value, entry_value, nexus_strings_string_length_get(value) + 1);
  (void)nexus_data_list_append_pointer(config->keys, entry_key);
  (void)nexus_data_list_append_pointer(config->values, entry_value);
}

/* Summary: visits the lines of the document in order, keeping the table that is current. */
static void l_config_walk(const LParserTree *tree, LToolConfig *config, const LParserGreenNode *node, char *section, uint_large capacity) {
  const LParserGreenAlternative *alternative;
  uint32                         i;

  if (l_config_is(tree, node, "table")) {
    const LParserGreenNode *key;
    char                   *text;
    char                   *normal;

    key = l_config_find(tree, node, "key");

    if (key != NULL) {
      text   = l_config_text(tree, key);
      normal = l_config_key_normalize(text);
      (void)stbsp_snprintf(section, (int)capacity, "%s", normal);
      nexus_memory_deallocate(normal);
      nexus_memory_deallocate(text);
    }

    return;
  }

  if (l_config_is(tree, node, "array_of_tables")) {
    section[0] = '\0';

    return;
  }

  if (l_config_is(tree, node, "key_value")) {
    const LParserGreenNode *key;
    const LParserGreenNode *value;
    char                   *key_text;
    char                   *value_text;

    key   = l_config_find(tree, node, "key");
    value = l_config_find(tree, node, "value");

    if (key != NULL && value != NULL) {
      key_text   = l_config_text(tree, key);
      value_text = l_config_text(tree, value);
      l_config_add(config, section, key_text, value_text);
      nexus_memory_deallocate(key_text);
      nexus_memory_deallocate(value_text);
    }

    return;
  }

  alternative = logos_language_tooling_parsing_syntax_green_node_alternative_first_get(tree, node);

  if (alternative == NULL) {
    return;
  }

  for (i = 0; i < logos_language_tooling_parsing_syntax_alternative_child_count_get(alternative); i++) {
    l_config_walk(tree, config, logos_language_tooling_parsing_syntax_alternative_child_get(tree, alternative, i), section, capacity);
  }
}

void logos_tool_config_initialize(LToolConfig *config) {
  config->path   = NULL;
  config->keys   = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  config->values = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
}

void logos_tool_config_release(LToolConfig *config) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(config->keys); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(config->keys, i));
    nexus_memory_deallocate(nexus_data_list_pointer_at(config->values, i));
  }

  if (config->path != NULL) {
    nexus_memory_deallocate(config->path);
  }

  nexus_data_list_destroy(config->keys);
  nexus_data_list_destroy(config->values);
  config->path = NULL;
}

boolean logos_tool_config_parse(LToolConfig *config, LogosRuntime *runtime, const char *path, const byte *text, uint_large size, char *error,
                                uint_large error_size) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LParserProcess   *process;
  LParserTree      *tree;
  LParserStatus     status;
  char             *grammar;
  char              section[LIC_KEY_MAX];

  compiler = logos_languages_genesis_compiler_create(runtime);
  logos_languages_genesis_compiler_search_folder_add(compiler, LOGOS_TOOL_STDLIB_DIR);
  grammar = logos_tool_files_join(LOGOS_TOOL_STDLIB_DIR, "TOML.gen");
  engine  = logos_languages_genesis_compiler_compile_file(compiler, grammar);
  nexus_memory_deallocate(grammar);

  if (engine == NULL) {
    (void)stbsp_snprintf(error, (int)error_size, "the TOML grammar of the standard library does not compile");
    logos_languages_genesis_compiler_destroy(compiler);

    return FALSE;
  }

  process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));

  if (logos_language_tooling_parsing_parser_process_chunk_add(process, text, size, TRUE) != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    (void)stbsp_snprintf(error, (int)error_size, "%s: the file could not be read", path);
    logos_language_tooling_parsing_parser_process_destroy(process);
    logos_languages_genesis_engine_release(engine);
    logos_languages_genesis_compiler_destroy(compiler);

    return FALSE;
  }

  do {
    status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
  } while (status == LPS_PROGRESS);

  tree = logos_language_tooling_parsing_parser_process_tree_take(process);
  logos_language_tooling_parsing_parser_process_destroy(process);

  if (status != LPS_ACCEPTED) {
    char message[256];

    message[0] = '\0';

    if (logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(tree) > 0) {
      (void)logos_language_tooling_parsing_syntax_tree_diagnostic_message_write(tree, 0, message, sizeof(message));
    }

    (void)stbsp_snprintf(error, (int)error_size, "%s: not valid TOML: %s", path, message);
    logos_language_tooling_parsing_syntax_tree_destroy(tree);
    logos_languages_genesis_engine_release(engine);
    logos_languages_genesis_compiler_destroy(compiler);

    return FALSE;
  }

  section[0] = '\0';
  l_config_walk(tree, config, logos_language_tooling_parsing_syntax_tree_root_get(tree), section, sizeof(section));
  logos_language_tooling_parsing_syntax_tree_destroy(tree);
  logos_languages_genesis_engine_release(engine);
  logos_languages_genesis_compiler_destroy(compiler);

  return TRUE;
}

/* Summary: the file genesis.toml in a folder, or in the nearest folder above it; Nexus-allocated path or NULL. */
static char *l_config_discover(const char *folder) {
  char *current;

  current = (char *)nexus_memory_allocate(nexus_strings_string_length_get(folder) + 1);
  nexus_memory_bytes_copy(folder, current, nexus_strings_string_length_get(folder) + 1);

  for (;;) {
    char       *candidate;
    byte       *bytes;
    uint_large  size;
    const char *slash;

    candidate = logos_tool_files_join(current, "genesis.toml");

    if (logos_tool_files_read(candidate, &bytes, &size)) {
      nexus_memory_deallocate(bytes);
      nexus_memory_deallocate(current);

      return candidate;
    }

    nexus_memory_deallocate(candidate);
    slash = logos_tool_string_find_last_byte(current, '/');

    if (slash == NULL || slash == current) {
      nexus_memory_deallocate(current);

      return NULL;
    }

    current[slash - current] = '\0';
  }
}

boolean logos_tool_config_load(LToolConfig *config, const LToolOptions *options, const char *root, char *error, uint_large error_size) {
  char      *path;
  byte      *bytes;
  uint_large size;
  boolean    ok;

  error[0] = '\0';
  path     = NULL;

  if (options->config != NULL) {
    path = (char *)nexus_memory_allocate(nexus_strings_string_length_get(options->config) + 1);
    nexus_memory_bytes_copy(options->config, path, nexus_strings_string_length_get(options->config) + 1);
  } else if (root != NULL) {
    path = l_config_discover(root);
  }

  if (path == NULL) {
    return TRUE;
  }

  if (!logos_tool_files_read(path, &bytes, &size)) {
    (void)stbsp_snprintf(error, (int)error_size, "cannot read the configuration %s", path);
    nexus_memory_deallocate(path);

    return FALSE;
  }

  config->path = path;
  ok           = logos_tool_config_parse(config, options->runtime, path, bytes, size, error, error_size);
  nexus_memory_deallocate(bytes);

  return ok;
}

const char *logos_tool_config_raw_get(const LToolConfig *config, const char *key) {
  uint_large i;

  for (i = nexus_data_list_length_get(config->keys); i > 0; i--) {
    if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(config->keys, i - 1), key)) {
      return (const char *)nexus_data_list_pointer_at(config->values, i - 1);
    }
  }

  return NULL;
}

boolean logos_tool_config_boolean_get(const LToolConfig *config, const char *key, boolean fallback) {
  const char *raw;

  raw = logos_tool_config_raw_get(config, key);

  if (raw == NULL) {
    return fallback;
  }

  if (nexus_strings_string_equals(raw, "true")) {
    return TRUE;
  }

  if (nexus_strings_string_equals(raw, "false")) {
    return FALSE;
  }

  return fallback;
}

int64 logos_tool_config_integer_get(const LToolConfig *config, const char *key, int64 fallback) {
  const char *raw;
  int64       value;
  boolean     negative;
  uint_large  i;

  raw = logos_tool_config_raw_get(config, key);

  if (raw == NULL || raw[0] == '\0') {
    return fallback;
  }

  i        = 0;
  negative = FALSE;
  value    = 0;

  if (raw[0] == '-' || raw[0] == '+') {
    negative = raw[0] == '-';
    i++;
  }

  for (; raw[i] != '\0'; i++) {
    if (raw[i] == '_') {
      continue;
    }

    if (raw[i] < '0' || raw[i] > '9') {
      return fallback;
    }

    value = (value * 10) + (int64)(raw[i] - '0');
  }

  return negative ? -value : value;
}

/* Summary: the text of a string value (basic or literal), unquoted, with the common escapes decoded; Nexus-allocated. */
static char *l_config_unquote(const char *text, uint_large length) {
  char      *result;
  uint_large i;
  uint_large out;
  char       quote;

  result = (char *)nexus_memory_allocate(length + 1);
  out    = 0;

  if (length >= 2 && (text[0] == '"' || text[0] == '\'') && text[length - 1] == text[0]) {
    quote = text[0];
    i     = 1;
    length--;
  } else {
    quote = '\0';
    i     = 0;
  }

  for (; i < length; i++) {
    if (quote == '"' && text[i] == '\\' && i + 1 < length) {
      i++;

      switch (text[i]) {
      case 'n':
        result[out++] = '\n';
        break;
      case 't':
        result[out++] = '\t';
        break;
      default:
        result[out++] = text[i];
        break;
      }
    } else {
      result[out++] = text[i];
    }
  }

  result[out] = '\0';

  return result;
}

char *logos_tool_config_string_get(const LToolConfig *config, const char *key) {
  const char *raw;

  raw = logos_tool_config_raw_get(config, key);

  return raw == NULL ? NULL : l_config_unquote(raw, nexus_strings_string_length_get(raw));
}

uint32 logos_tool_config_list_get(const LToolConfig *config, const char *key, char ***out_items) {
  const char *raw;
  uint_large  length;
  uint_large  i;
  uint32      count;
  char      **items;

  *out_items = NULL;
  raw        = logos_tool_config_raw_get(config, key);

  if (raw == NULL || raw[0] != '[') {
    return 0;
  }

  length = nexus_strings_string_length_get(raw);
  items  = (char **)nexus_memory_allocate(((uint_large)length + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  count  = 0;
  i      = 1;

  while (i < length && raw[i] != ']') {
    uint_large start;
    uint_large end;

    while (i < length && (raw[i] == ' ' || raw[i] == '\t' || raw[i] == '\n' || raw[i] == '\r' || raw[i] == ',')) {
      i++;
    }

    if (i >= length || raw[i] == ']') {
      break;
    }

    start = i;

    if (raw[i] == '"' || raw[i] == '\'') {
      char quote;

      quote = raw[i];
      i++;

      while (i < length && raw[i] != quote) {
        if (quote == '"' && raw[i] == '\\') {
          i++;
        }

        i++;
      }

      i++;
    } else {
      while (i < length && raw[i] != ',' && raw[i] != ']' && raw[i] != ' ' && raw[i] != '\n') {
        i++;
      }
    }

    end            = i > length ? length : i;
    items[count++] = l_config_unquote(raw + start, end - start);
  }

  *out_items = items;

  return count;
}

void logos_tool_config_list_release(char **items, uint32 count) {
  uint32 i;

  if (items == NULL) {
    return;
  }

  for (i = 0; i < count; i++) {
    nexus_memory_deallocate(items[i]);
  }

  nexus_memory_deallocate((void *)items);
}

/* Summary: the folder the tool runs in, from the environment (the shell keeps PWD current); "." when it is unknown. */
static const char *l_config_here(char *buffer, uint_large capacity) {
  boolean found;

  if (nexus_environment_variable_get("PWD", buffer, capacity, &found) != NEXUS_PROGRAM_ERROR_SUCCESS() || !found || buffer[0] != '/') {
    (void)stbsp_snprintf(buffer, (int)capacity, ".");
  }

  return buffer;
}

int logos_tool_config_run(const LToolOptions *options) {
  LToolConfig config;
  char        error[512];
  char        here[1024];
  uint_large  i;

  logos_tool_config_initialize(&config);

  if (!logos_tool_config_load(&config, options, l_config_here(here, sizeof(here)), error, sizeof(error))) {
    (void)nexus_stdio_stderr_write_formatted("genesis: %s\n", error);
    logos_tool_config_release(&config);

    return 1;
  }

  if (config.path == NULL) {
    (void)nexus_stdio_stdout_write_cstring("# no genesis.toml found\n");
  } else {
    (void)nexus_stdio_stdout_write_formatted("# %s\n", config.path);
  }

  for (i = 0; i < nexus_data_list_length_get(config.keys); i++) {
    (void)nexus_stdio_stdout_write_formatted("%s = %s\n", (const char *)nexus_data_list_pointer_at(config.keys, i),
                                             (const char *)nexus_data_list_pointer_at(config.values, i));
  }

  logos_tool_config_release(&config);

  return 0;
}
