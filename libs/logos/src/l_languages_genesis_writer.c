#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

struct LGenesisWriter {
  char          *name;
  char          *version;
  NexusDataList *comments; /* char * */
  NexusDataList *imports;  /* char * */
  char          *entry;
  NexusDataList *globals; /* char * */
  char          *choice;
  boolean        grammar_expected;
  NexusDataList *contexts; /* char * */
  NexusDataList *tokens;   /* LGWriterToken * */
  NexusDataList *rules;    /* LGWriterRule * */
};

typedef struct LGWriterToken {
  char                 *context;
  char                 *name;
  char                 *definition;
  int64                 priority;
  LGenesisContextAction action;
  char                 *target;
  int64                 amount;
} LGWriterToken;

typedef struct LGWriterRule {
  LGenesisRuleKind kind;
  char            *name;
  char            *annotations;
  char            *definition;
} LGWriterRule;

static char *l_writer_copy(const char *text) {
  uint_large length;
  char      *copy;

  length = nexus_strings_string_length_get(text);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length + 1);

  return copy;
}

LGenesisWriter *logos_languages_genesis_writer_create(const char *name, const char *version) {
  LGenesisWriter *writer;

  NEXUS_PROGRAM_PRECONDITION(name != NULL, "name must not be NULL");

  writer                   = (LGenesisWriter *)nexus_memory_allocate(NEXUS_SIZEOF(*writer));
  writer->name             = l_writer_copy(name);
  writer->version          = version != NULL ? l_writer_copy(version) : NULL;
  writer->comments         = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  writer->imports          = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  writer->entry            = NULL;
  writer->globals          = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  writer->choice           = NULL;
  writer->grammar_expected = FALSE;
  writer->contexts         = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);
  writer->tokens           = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  writer->rules            = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);

  return writer;
}

static void l_writer_strings_destroy(NexusDataList *list) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(list); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(list, i));
  }

  nexus_data_list_destroy(list);
}

void logos_languages_genesis_writer_destroy(LGenesisWriter *writer) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(writer != NULL, "writer must not be NULL");

  for (i = 0; i < nexus_data_list_length_get(writer->tokens); i++) {
    LGWriterToken *token;

    token = (LGWriterToken *)nexus_data_list_pointer_at(writer->tokens, i);
    nexus_memory_deallocate(token->context);
    nexus_memory_deallocate(token->name);
    nexus_memory_deallocate(token->definition);

    if (token->target != NULL) {
      nexus_memory_deallocate(token->target);
    }

    nexus_memory_deallocate(token);
  }

  for (i = 0; i < nexus_data_list_length_get(writer->rules); i++) {
    LGWriterRule *rule;

    rule = (LGWriterRule *)nexus_data_list_pointer_at(writer->rules, i);
    nexus_memory_deallocate(rule->name);
    nexus_memory_deallocate(rule->annotations);
    nexus_memory_deallocate(rule->definition);
    nexus_memory_deallocate(rule);
  }

  nexus_data_list_destroy(writer->tokens);
  nexus_data_list_destroy(writer->rules);
  l_writer_strings_destroy(writer->comments);
  l_writer_strings_destroy(writer->imports);
  l_writer_strings_destroy(writer->globals);
  l_writer_strings_destroy(writer->contexts);

  if (writer->entry != NULL) {
    nexus_memory_deallocate(writer->entry);
  }

  if (writer->choice != NULL) {
    nexus_memory_deallocate(writer->choice);
  }

  if (writer->version != NULL) {
    nexus_memory_deallocate(writer->version);
  }

  nexus_memory_deallocate(writer->name);
  nexus_memory_deallocate(writer);
}

void logos_languages_genesis_writer_comment_add(LGenesisWriter *writer, const char *text) {
  (void)nexus_data_list_append_pointer(writer->comments, l_writer_copy(text));
}

void logos_languages_genesis_writer_import_add(LGenesisWriter *writer, const char *declaration) {
  (void)nexus_data_list_append_pointer(writer->imports, l_writer_copy(declaration));
}

void logos_languages_genesis_writer_parser_entry_set(LGenesisWriter *writer, const char *rule) {
  if (writer->entry != NULL) {
    nexus_memory_deallocate(writer->entry);
  }

  writer->entry = l_writer_copy(rule);
}

void logos_languages_genesis_writer_parser_global_add(LGenesisWriter *writer, const char *rule) {
  (void)nexus_data_list_append_pointer(writer->globals, l_writer_copy(rule));
}

void logos_languages_genesis_writer_parser_choice_set(LGenesisWriter *writer, const char *mode) {
  if (writer->choice != NULL) {
    nexus_memory_deallocate(writer->choice);
  }

  writer->choice = l_writer_copy(mode);
}

void logos_languages_genesis_writer_parser_grammar_expected_set(LGenesisWriter *writer, boolean grammar_expected) {
  writer->grammar_expected = grammar_expected;
}

void logos_languages_genesis_writer_context_add(LGenesisWriter *writer, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(writer->contexts); i++) {
    if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(writer->contexts, i), name)) {
      return;
    }
  }

  (void)nexus_data_list_append_pointer(writer->contexts, l_writer_copy(name));
}

void logos_languages_genesis_writer_token_add(LGenesisWriter *writer, const char *context, const char *name, const char *definition, int64 priority,
                                              LGenesisContextAction action, const char *target, int64 amount) {
  LGWriterToken *token;

  logos_languages_genesis_writer_context_add(writer, context);
  token             = (LGWriterToken *)nexus_memory_allocate(NEXUS_SIZEOF(*token));
  token->context    = l_writer_copy(context);
  token->name       = l_writer_copy(name);
  token->definition = l_writer_copy(definition);
  token->priority   = priority;
  token->action     = action;
  token->target     = target != NULL ? l_writer_copy(target) : NULL;
  token->amount     = amount;
  (void)nexus_data_list_append_pointer(writer->tokens, token);
}

static void l_writer_rule_add(LGenesisWriter *writer, LGenesisRuleKind kind, const char *name, const char *annotations, const char *definition) {
  LGWriterRule *rule;

  rule              = (LGWriterRule *)nexus_memory_allocate(NEXUS_SIZEOF(*rule));
  rule->kind        = kind;
  rule->name        = l_writer_copy(name);
  rule->annotations = l_writer_copy(annotations != NULL ? annotations : "");
  rule->definition  = l_writer_copy(definition);
  (void)nexus_data_list_append_pointer(writer->rules, rule);
}

void logos_languages_genesis_writer_rule_add(LGenesisWriter *writer, const char *name, const char *annotations, const char *definition) {
  l_writer_rule_add(writer, LGRK_RULE, name, annotations, definition);
}

void logos_languages_genesis_writer_pratt_add(LGenesisWriter *writer, const char *name, const char *body) {
  l_writer_rule_add(writer, LGRK_PRATT, name, "", body);
}

void logos_languages_genesis_writer_embed_add(LGenesisWriter *writer, const char *name, const char *body) {
  l_writer_rule_add(writer, LGRK_EMBED, name, "", body);
}

static void l_writer_add(NexusDataList *out, const char *text) {
  uint_large i;

  for (i = 0; text[i] != '\0'; i++) {
    (void)nexus_data_list_append(out, &text[i]);
  }
}

static void l_writer_format(NexusDataList *out, const char *format, ...) {
  char    buffer[512];
  va_list arguments;

  va_start(arguments, format);
  (void)stbsp_vsnprintf(buffer, (int)sizeof(buffer), format, arguments);
  va_end(arguments);
  l_writer_add(out, buffer);
}

char *logos_languages_genesis_writer_text_get(const LGenesisWriter *writer) {
  NexusDataList *out;
  char          *result;
  char           terminator;
  uint_large     i;
  uint_large     j;

  NEXUS_PROGRAM_PRECONDITION(writer != NULL, "writer must not be NULL");

  out = nexus_data_list_create(NEXUS_SIZEOF(char), 1024);
  l_writer_format(out, "Language: %s\n", writer->name);

  if (writer->version != NULL) {
    l_writer_format(out, "Version: %s\n", writer->version);
  }

  l_writer_add(out, "\n");

  if (nexus_data_list_length_get(writer->comments) > 0) {
    for (i = 0; i < nexus_data_list_length_get(writer->comments); i++) {
      l_writer_add(out, "//");

      if (((const char *)nexus_data_list_pointer_at(writer->comments, i))[0] != '\0') {
        l_writer_add(out, " ");
        l_writer_add(out, (const char *)nexus_data_list_pointer_at(writer->comments, i));
      }

      l_writer_add(out, "\n");
    }

    l_writer_add(out, "\n");
  }

  if (nexus_data_list_length_get(writer->imports) > 0) {
    for (i = 0; i < nexus_data_list_length_get(writer->imports); i++) {
      l_writer_add(out, (const char *)nexus_data_list_pointer_at(writer->imports, i));
      l_writer_add(out, "\n");
    }

    l_writer_add(out, "\n");
  }

  l_writer_add(out, "PARSER {\n");

  if (writer->entry != NULL) {
    l_writer_format(out, "\tentry = #%s\n", writer->entry);
  }

  if (nexus_data_list_length_get(writer->globals) == 1) {
    l_writer_format(out, "\tglobal = #%s\n", (const char *)nexus_data_list_pointer_at(writer->globals, 0));
  } else if (nexus_data_list_length_get(writer->globals) > 1) {
    l_writer_add(out, "\tglobal = [");

    for (i = 0; i < nexus_data_list_length_get(writer->globals); i++) {
      l_writer_format(out, "%s#%s", i > 0 ? ", " : "", (const char *)nexus_data_list_pointer_at(writer->globals, i));
    }

    l_writer_add(out, "]\n");
  }

  if (writer->choice != NULL) {
    l_writer_format(out, "\tchoice = %s\n", writer->choice);
  }

  if (writer->grammar_expected) {
    l_writer_add(out, "\tlexical_selection = grammar_expected\n");
  }

  l_writer_add(out, "}\n");

  for (i = 0; i < nexus_data_list_length_get(writer->contexts); i++) {
    const char *context;

    context = (const char *)nexus_data_list_pointer_at(writer->contexts, i);
    l_writer_format(out, "\nCONTEXT <%s> {\n", context);

    for (j = 0; j < nexus_data_list_length_get(writer->tokens); j++) {
      const LGWriterToken *token;

      token = (const LGWriterToken *)nexus_data_list_pointer_at(writer->tokens, j);

      if (!nexus_strings_string_equals(token->context, context)) {
        continue;
      }

      l_writer_format(out, "\t$%s = ", token->name);
      l_writer_add(out, token->definition);

      if (token->priority != 0) {
        l_writer_format(out, " @priority(%lld)", (long long)token->priority);
      }

      switch (token->action) {
      case LGCA_PUSH:
        l_writer_format(out, " -> push(%s)", token->target);
        break;
      case LGCA_POP:
        l_writer_format(out, " -> pop(%lld)", (long long)token->amount);
        break;
      case LGCA_SET:
        l_writer_format(out, " -> set(%s)", token->target);
        break;
      case LGCA_REPLACE:
        l_writer_format(out, " -> replace(%s)", token->target);
        break;
      case LGCA_NONE:
      default:
        break;
      }

      l_writer_add(out, "\n");
    }

    l_writer_add(out, "}\n");
  }

  l_writer_add(out, "\n");

  for (i = 0; i < nexus_data_list_length_get(writer->rules); i++) {
    const LGWriterRule *rule;

    rule = (const LGWriterRule *)nexus_data_list_pointer_at(writer->rules, i);

    switch (rule->kind) {
    case LGRK_PRATT:
      l_writer_format(out, "PRATT <#%s> {\n\t", rule->name);
      l_writer_add(out, rule->definition);
      l_writer_add(out, "\n}\n\n");
      break;
    case LGRK_EMBED:
      l_writer_format(out, "EMBED <#%s> {\n\t", rule->name);
      l_writer_add(out, rule->definition);
      l_writer_add(out, "\n}\n\n");
      break;
    case LGRK_RULE:
    default:
      l_writer_format(out, "#%s", rule->name);

      if (rule->annotations[0] != '\0') {
        l_writer_add(out, " ");
        l_writer_add(out, rule->annotations);
      }

      l_writer_add(out, " = ");
      l_writer_add(out, rule->definition);
      l_writer_add(out, "\n");
      break;
    }
  }

  terminator = '\0';
  (void)nexus_data_list_append(out, &terminator);
  result = (char *)nexus_memory_allocate(nexus_data_list_length_get(out));
  nexus_memory_bytes_copy(nexus_data_list_data_get_const(out), result, nexus_data_list_length_get(out));
  nexus_data_list_destroy(out);

  return result;
}
