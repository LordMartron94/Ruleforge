
#include <nexus.h>
#include <echo.h>
#include <logos.h>

#include "json_bindings.h"

static boolean l_filter(const EchoRecord *record, void *user_data) {
  (void)record;
  (void)user_data;

  return FALSE;
}

static int l_failures;

static void l_check(boolean ok, const char *name) {
  if (!ok) {
    (void)nexus_stdio_stderr_write_formatted("FAIL: %s\n", name);
    l_failures++;
  }
}

int main(void) {
  EchoRuntimeConfiguration configuration;
  EchoRuntime             *echo;
  EchoSystem              *system;
  EchoSink                *sink;
  LogosRuntime            *logos;
  LGenesisCompiler        *compiler;
  LGenesisEngine          *engine;
  LParserProcess          *process;
  LParserTree             *tree;
  JsonDocument             document;
  JsonElement              element;
  JsonObject               object;
  JsonKeyValuePair         pair;
  JsonArray                array;
  LParserStatus            status;
  const char              *source;
  const LParserGreenNode  *token;
  uint_large               size;

  configuration = echo_runtime_configuration_default();
  (void)echo_runtime_create(&configuration, &echo);
  (void)echo_runtime_system_register(echo, "Test", NULL, &system);
  (void)echo_sink_rich_terminal_create(l_filter, NULL, echo_rich_terminal_configuration_ansi_create(NTP_NANO_SECOND), &sink);
  (void)echo_runtime_sink_register(echo, sink);
  (void)echo_runtime_start(echo);
  logos = logos_runtime_create(echo, system);

  compiler = logos_languages_genesis_compiler_create(logos);
  engine   = logos_languages_genesis_compiler_compile_file(compiler, JSON_GRAMMAR_FILE);
  l_check(engine != NULL, "the grammar compiles");

  if (engine != NULL) {
    l_check(json_grammar_matches(engine), "the bindings match the compiled grammar");

    source  = "{\"a\": [1, 2], \"b\": true}";
    process = logos_language_tooling_parsing_parser_process_create(logos_languages_genesis_engine_parser_get(engine));
    (void)logos_language_tooling_parsing_parser_process_chunk_add(process, (const byte *)source, nexus_strings_string_length_get(source), TRUE);

    do {
      status = logos_language_tooling_parsing_parser_process_parse(process, LPARSER_TOKEN_BUDGET_UNBOUNDED);
    } while (status == LPS_PROGRESS);

    l_check(status == LPS_ACCEPTED, "the document parses");
    tree     = logos_language_tooling_parsing_parser_process_tree_take(process);
    document = json_root(tree);
    l_check(document.node != NULL && json_document_count_rule_element(document) == 1, "the root is a document with one element");

    element = json_document_rule_element(document, 0);
    l_check(json_element_count_rule_object(element) == 1 && json_element_count_rule_array(element) == 0, "the element is an object");

    object = json_element_rule_object(element, 0);
    l_check(json_object_count_rule_key_value_pair(object) == 2 && json_object_count_token_comma(object) == 1,
            "the object has two pairs and one comma");

    pair = json_object_rule_key_value_pair(object, 0);
    l_check(json_key_value_pair_count_token_colon(pair) == 1, "a pair has a colon");

    element = json_key_value_pair_rule_element(pair, 0);
    array   = json_element_rule_array(element, 0);
    l_check(array.node != NULL && json_array_count_rule_element(array) == 2, "the first value is an array of two elements");

    pair    = json_object_rule_key_value_pair(object, 1);
    element = json_key_value_pair_rule_element(pair, 0);
    token   = json_element_token_true(element, 0);
    l_check(token != NULL && json_element_count_token_true(element) == 1, "the second value is the token true");

    if (token != NULL) {
      const byte *text;

      text = logos_language_tooling_parsing_syntax_green_node_text_get(tree, token, &size);
      l_check(size == 4 && nexus_memory_bytes_equal(text, (const byte *)"true", 4), "the token text is available");
    }

    l_check(json_rule_of(tree, logos_language_tooling_parsing_syntax_tree_root_get(tree)) == JSON_RULE_NONE, "the source root is no rule");
    l_check(json_token_name(JSON_TOKEN_NUMBER) != NULL && nexus_strings_string_equals(json_token_name(JSON_TOKEN_NUMBER), "number"),
            "token kinds have names");

    logos_language_tooling_parsing_syntax_tree_destroy(tree);
    logos_language_tooling_parsing_parser_process_destroy(process);
    logos_languages_genesis_engine_release(engine);
  }

  logos_languages_genesis_compiler_destroy(compiler);
  logos_runtime_destroy(logos);
  (void)echo_runtime_close(echo);
  echo_runtime_destroy(echo);

  if (l_failures == 0) {
    (void)nexus_stdio_stdout_write_cstring("genesis bindings: all checks passed\n");
  }

  return l_failures == 0 ? 0 : 1;
}
