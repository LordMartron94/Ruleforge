#include <nexus.h>
#include <logos.h>

#include "tool.h"

/*
The Sublime Text generator builds the syntax from the grammar, not only from the lexer. Delimited constructs of the
grammar (a rule or group that starts with one token and ends with another: an object between braces, a parenthesized
group) become contexts that the opening token pushes and the closing token pops, scoped with the rule's name; inside a
context only the tokens the grammar allows there are matched, found by following the rules referenced from it. The main
context holds what the entry rule can start with. Genesis lexical contexts (strings and the like) stay contexts, so
their push, pop and set actions carry over. Sublime takes the first pattern that matches where Genesis takes the
longest, so the patterns of a context are ordered to agree: higher priority first, then fixed texts longest first, then
the rest in definition order; a fixed word that a longer token could extend gets a lookahead guard.
*/

typedef struct LToolSublime {
  const LGenesisEngine  *engine;
  const LGenesisGrammar *grammar;
  const LToolMapping    *mapping;
  const char            *suffix;         /* The language part appended to every scope, e.g. "json". */
  NexusDataList         *global_entries; /* LToolEntry: what the global rules can match anywhere. */
  char                 **frame_names;    /* The context name of each frame of the grammar, or NULL when it is not usable. */
  NexusDataList         *variants;       /* LToolVariant: lexical contexts copied with a role scope. */
  NexusDataList         *owned;          /* Strings to free at the end. */
  LToolText              out;
  uint32                 problems;
} LToolSublime;

/* Summary: appends text as a single-quoted YAML scalar. */
static void l_tool_sublime_quote(LToolText *out, const char *text) {
  char one[2];

  one[1] = '\0';
  logos_tool_text_add(out, "'");

  for (; *text != '\0'; text++) {
    if (*text == '\'') {
      logos_tool_text_add(out, "''");
    } else {
      one[0] = *text;
      logos_tool_text_add(out, one);
    }
  }

  logos_tool_text_add(out, "'");
}

static boolean l_tool_sublime_word_character(char value) {
  return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') || value == '_';
}

/* Summary: whether the token is a fixed word that a longer token of the lexer's context would swallow, such as a keyword and an identifier. */
static boolean l_tool_sublime_needs_guard(const LToolSublime *sublime, const LGenesisGrammarToken *token, uint32 context_index) {
  LLexer                *lexer;
  char                  *probe;
  uint_large             length;
  LLexerToken            found;
  const LLexerTokenRule *rule;
  LLexerError            error;
  boolean                result;

  if (token->literal == NULL || token->literal[0] == '\0' ||
      !l_tool_sublime_word_character(token->literal[nexus_strings_string_length_get(token->literal) - 1])) {
    return FALSE;
  }

  lexer  = logos_languages_genesis_engine_lexer_get(sublime->engine);
  length = nexus_strings_string_length_get(token->literal);
  probe  = (char *)nexus_memory_allocate(length + 2);
  nexus_memory_bytes_copy(token->literal, probe, length);
  probe[length]     = 'a';
  probe[length + 1] = '\0';
  result =
      logos_language_tooling_lexing_lexer_match(lexer, NULL, context_index, (const byte *)probe, length + 1, 0, &found, &rule, &error) == LLS_TOKEN &&
      found.kind != token->kind && found.lexeme_width > length;
  nexus_memory_deallocate(probe);

  return result;
}

/* Summary: the index of a context by name, or -1. */
static int32 l_tool_sublime_context_index(const LGenesisGrammar *grammar, const char *name) {
  uint32 i;

  for (i = 0; i < logos_languages_genesis_grammar_context_count_get(grammar); i++) {
    if (nexus_strings_string_equals(logos_languages_genesis_grammar_context_name_at(grammar, i), name)) {
      return (int32)i;
    }
  }

  return -1;
}

static const char *l_tool_sublime_context_label(const char *name) {
  return nexus_strings_string_equals(name, "ROOT") ? "main" : name;
}

static void l_tool_sublime_scope(const LToolSublime *sublime, LToolText *out, const char *scope) {
  logos_tool_text_format(out, "%s.%s", scope, sublime->suffix);
}

/* ---------------------------------------------------------------------------- */
/* ROLES AND CLOSURES                                                            */
/* ---------------------------------------------------------------------------- */

/* A token that can appear in a context, with the scope of the syntactic role it has there (NULL when it has none). */
typedef struct LToolEntry {
  const LGenesisGrammarToken *token;
  const char                 *role;
} LToolEntry;

/* A lexical context copied so that everything inside carries a role scope. */
typedef struct LToolVariant {
  char       *name;
  const char *base;
  const char *role;
} LToolVariant;

static const LGenesisGrammarToken *l_tool_sublime_token_find(const LToolSublime *sublime, const char *name) {
  uint32 i;

  for (i = 0; i < logos_languages_genesis_grammar_token_count_get(sublime->grammar); i++) {
    const LGenesisGrammarToken *token;

    token = logos_languages_genesis_grammar_token_at(sublime->grammar, i);

    if (!token->fragment && nexus_strings_string_equals(token->name, name)) {
      return token;
    }
  }

  return NULL;
}

static const LGenesisGrammarRule *l_tool_sublime_rule_find(const LToolSublime *sublime, const char *name) {
  uint32 i;

  for (i = 0; i < logos_languages_genesis_grammar_rule_count_get(sublime->grammar); i++) {
    const LGenesisGrammarRule *rule;

    rule = logos_languages_genesis_grammar_rule_at(sublime->grammar, i);

    if (nexus_strings_string_equals(rule->name, name)) {
      return rule;
    }
  }

  return NULL;
}

/* Summary: a scope with the language suffix, owned by the generator. */
static const char *l_tool_sublime_full_scope(LToolSublime *sublime, const char *scope) {
  LToolText text;
  char     *copy;

  logos_tool_text_initialize(&text);
  l_tool_sublime_scope(sublime, &text, scope);
  copy = (char *)nexus_memory_allocate(logos_tool_text_length_get(&text) + 1);
  nexus_memory_bytes_copy(logos_tool_text_data_get(&text), copy, logos_tool_text_length_get(&text) + 1);
  logos_tool_text_release(&text);
  (void)nexus_data_list_append_pointer(sublime->owned, copy);

  return copy;
}

/* Summary: a rule name without the hygiene suffix template expansion adds ("delimited__i12" becomes "delimited"); in out. */
static void l_tool_sublime_plain_name(const char *name, char *out, uint_large size) {
  uint_large i;

  for (i = 0; name[i] != '\0' && i + 1 < size; i++) {
    if (name[i] == '_' && name[i + 1] == '_' && name[i + 2] == 'i') {
      break;
    }

    out[i] = name[i];
  }

  out[i] = '\0';
}

/* Summary: whether a frame was found in the rule; its mapping then scopes the frame rather than the tokens it leads to. */
static boolean l_tool_sublime_owns_frame(const LToolSublime *sublime, const char *rule) {
  uint32 i;

  for (i = 0; i < logos_languages_genesis_grammar_frame_count_get(sublime->grammar); i++) {
    if (nexus_strings_string_equals(logos_languages_genesis_grammar_frame_at(sublime->grammar, i)->rule, rule)) {
      return TRUE;
    }
  }

  return FALSE;
}

/* Summary: the role scope the mapping gives a rule reached from a parent: the edge "#parent > #child" first, then the rule itself. */
static const char *l_tool_sublime_role(LToolSublime *sublime, const char *parent, const char *rule) {
  char        key[256];
  const char *scope;

  if (parent != NULL) {
    (void)stbsp_snprintf(key, (int)sizeof(key), "#%s>#%s", parent, rule);
    scope = logos_tool_mapping_context_scope_get(sublime->mapping, key);

    if (scope != NULL && scope[0] != '\0') {
      return l_tool_sublime_full_scope(sublime, scope);
    }
  }

  if (l_tool_sublime_owns_frame(sublime, rule)) {
    return NULL;
  }

  (void)stbsp_snprintf(key, (int)sizeof(key), "#%s", rule);
  scope = logos_tool_mapping_context_scope_get(sublime->mapping, key);

  return scope != NULL && scope[0] != '\0' ? l_tool_sublime_full_scope(sublime, scope) : NULL;
}

static boolean l_tool_sublime_visited(const NexusDataList *visited, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(visited); i++) {
    if (nexus_strings_string_equals((const char *)nexus_data_list_pointer_at(visited, i), name)) {
      return TRUE;
    }
  }

  return FALSE;
}

static void l_tool_sublime_entry_add(NexusDataList *out, const LGenesisGrammarToken *token, const char *role) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(out); i++) {
    if (((const LToolEntry *)nexus_data_list_at_const(out, i))->token == token) {
      return; /* The first role a token is reached with wins. */
    }
  }

  {
    LToolEntry entry;

    entry.token = token;
    entry.role  = role;
    (void)nexus_data_list_append(out, &entry);
  }
}

/* Summary: follows a rule and the rules below it, adding the tokens they can start a construct with. A frame's interior is not entered. */
static void l_tool_sublime_visit(LToolSublime *sublime, const char *name, const char *parent, const char *inherited, NexusDataList *visited,
                                 NexusDataList *out) {
  const LGenesisGrammarRule *rule;
  const char                *role;
  uint32                     i;

  if (l_tool_sublime_visited(visited, name)) {
    return;
  }

  (void)nexus_data_list_append_pointer(visited, name);
  rule = l_tool_sublime_rule_find(sublime, name);

  if (rule == NULL) {
    return;
  }

  role = l_tool_sublime_role(sublime, parent, name);
  role = role != NULL ? role : inherited;

  for (i = 0; i < rule->top_token_ref_count; i++) {
    const LGenesisGrammarToken *token;

    token = l_tool_sublime_token_find(sublime, rule->top_token_refs[i]);

    if (token != NULL) {
      l_tool_sublime_entry_add(out, token, role);
    }
  }

  for (i = 0; i < rule->top_rule_ref_count; i++) {
    l_tool_sublime_visit(sublime, rule->top_rule_refs[i], name, role, visited, out);
  }
}

static void l_tool_sublime_closure(LToolSublime *sublime, const char *const *tokens, uint32 token_count, const char *const *rules, uint32 rule_count,
                                   const char *parent, NexusDataList *out) {
  NexusDataList *visited;
  uint32         i;

  visited = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);

  for (i = 0; i < token_count; i++) {
    const LGenesisGrammarToken *token;

    token = l_tool_sublime_token_find(sublime, tokens[i]);

    if (token != NULL) {
      l_tool_sublime_entry_add(out, token, NULL);
    }
  }

  for (i = 0; i < rule_count; i++) {
    l_tool_sublime_visit(sublime, rules[i], parent, NULL, visited, out);
  }

  nexus_data_list_destroy(visited);
}

/* ---------------------------------------------------------------------------- */
/* CONTEXTS                                                                      */
/* ---------------------------------------------------------------------------- */

/* Summary: the frame that a token opens, or -1; unusable frames never match. */
static int32 l_tool_sublime_frame_opened_by(const LToolSublime *sublime, const char *token) {
  uint32 i;

  for (i = 0; i < logos_languages_genesis_grammar_frame_count_get(sublime->grammar); i++) {
    if (sublime->frame_names[i] != NULL && nexus_strings_string_equals(logos_languages_genesis_grammar_frame_at(sublime->grammar, i)->open, token)) {
      return (int32)i;
    }
  }

  return -1;
}

/* Summary: the context name of a lexical context copied for a role, created on first use. */
static const char *l_tool_sublime_variant(LToolSublime *sublime, const char *base, const char *role) {
  uint_large   i;
  LToolVariant variant;
  char         name[256];

  for (i = 0; i < nexus_data_list_length_get(sublime->variants); i++) {
    const LToolVariant *existing;

    existing = (const LToolVariant *)nexus_data_list_at_const(sublime->variants, i);

    if (nexus_strings_string_equals(existing->base, base) && nexus_strings_string_equals(existing->role, role)) {
      return existing->name;
    }
  }

  (void)stbsp_snprintf(name, (int)sizeof(name), "%s-role-%u", l_tool_sublime_context_label(base),
                       (unsigned)nexus_data_list_length_get(sublime->variants) + 1);
  variant.name = (char *)nexus_memory_allocate(nexus_strings_string_length_get(name) + 1);
  nexus_memory_bytes_copy(name, variant.name, nexus_strings_string_length_get(name) + 1);
  variant.base = base;
  variant.role = role;
  (void)nexus_data_list_append(sublime->variants, &variant);
  (void)nexus_data_list_append_pointer(sublime->owned, variant.name);

  return variant.name;
}

/* Summary: whether left comes before right in a context: priority, then fixed text before pattern, then longer fixed text. */
static boolean l_tool_sublime_before(const LGenesisGrammarToken *left, const LGenesisGrammarToken *right) {
  if (right->priority != left->priority) {
    return right->priority > left->priority;
  }

  if ((right->literal != NULL) != (left->literal != NULL)) {
    return right->literal != NULL;
  }

  if (right->literal != NULL && left->literal != NULL) {
    return nexus_strings_string_length_get(right->literal) > nexus_strings_string_length_get(left->literal);
  }

  return FALSE;
}

/*
Summary: writes the match patterns of one context. entries are what the grammar allows there (only tokens of the lexical
  context lexical_name are used); close names the token that pops a frame; when entries is NULL every token of the
  lexical context is used, in definition order.
*/
static void l_tool_sublime_patterns(LToolSublime *sublime, const NexusDataList *entries, const char *lexical_name, const char *close_name) {
  const LGenesisGrammar *grammar;
  LToolText             *out;
  LToolEntry            *sorted;
  uint_large             count;
  uint_large             i;
  uint_large             j;
  int32                  lexical_index;

  grammar       = sublime->grammar;
  out           = &sublime->out;
  lexical_index = l_tool_sublime_context_index(grammar, lexical_name);
  count         = 0;
  sorted        = (LToolEntry *)nexus_memory_allocate(
      ((entries != NULL ? nexus_data_list_length_get(entries) : logos_languages_genesis_grammar_token_count_get(grammar)) + 1) *
      NEXUS_SIZEOF(LToolEntry));

  if (entries != NULL) {
    for (i = 0; i < nexus_data_list_length_get(entries); i++) {
      const LToolEntry *entry;

      entry = (const LToolEntry *)nexus_data_list_at_const(entries, i);

      if (nexus_strings_string_equals(entry->token->context, lexical_name)) {
        sorted[count++] = *entry;
      }
    }
  } else {
    for (i = 0; i < logos_languages_genesis_grammar_token_count_get(grammar); i++) {
      const LGenesisGrammarToken *token;

      token = logos_languages_genesis_grammar_token_at(grammar, (uint32)i);

      if (!token->fragment && nexus_strings_string_equals(token->context, lexical_name)) {
        sorted[count].token = token;
        sorted[count].role  = NULL;
        count++;
      }
    }
  }

  /* The closing token of a frame is always there. */
  if (close_name != NULL) {
    const LGenesisGrammarToken *close;
    boolean                     present;

    close   = l_tool_sublime_token_find(sublime, close_name);
    present = FALSE;

    for (i = 0; i < count; i++) {
      present = present || sorted[i].token == close;
    }

    if (close != NULL && !present) {
      sorted[count].token = close;
      sorted[count].role  = NULL;
      count++;
    }
  }

  /* A stable insertion sort: the entry order is the tie-breaker. */
  for (i = 1; i < count; i++) {
    LToolEntry current;

    current = sorted[i];

    for (j = i; j > 0 && l_tool_sublime_before(sorted[j - 1].token, current.token); j--) {
      sorted[j] = sorted[j - 1];
    }

    sorted[j] = current;
  }

  for (i = 0; i < count; i++) {
    const LGenesisGrammarToken *token;
    const char                 *scope;
    const char                 *full;
    LToolText                   pattern;
    boolean                     is_close;
    int32                       frame;

    token    = sorted[i].token;
    is_close = close_name != NULL && nexus_strings_string_equals(token->name, close_name);
    frame    = token->action == LGCA_NONE ? l_tool_sublime_frame_opened_by(sublime, token->name) : -1;

    logos_tool_text_initialize(&pattern);
    logos_tool_text_add(&pattern, token->regexp);

    if (l_tool_sublime_needs_guard(sublime, token, (uint32)lexical_index)) {
      logos_tool_text_add(&pattern, "(?![A-Za-z0-9_])");
    }

    logos_tool_text_add(out, "    - match: ");
    l_tool_sublime_quote(out, logos_tool_text_data_get(&pattern));
    logos_tool_text_add(out, "\n");
    logos_tool_text_release(&pattern);

    scope = logos_tool_mapping_scope_get(sublime->mapping, token->name);
    full  = scope != NULL && scope[0] != '\0' ? l_tool_sublime_full_scope(sublime, scope) : NULL;

    if (full != NULL || sorted[i].role != NULL) {
      logos_tool_text_add(out, "      scope: ");

      if (full != NULL) {
        logos_tool_text_add(out, full);
      }

      if (full != NULL && sorted[i].role != NULL) {
        logos_tool_text_add(out, " ");
      }

      if (sorted[i].role != NULL) {
        logos_tool_text_add(out, sorted[i].role);
      }

      logos_tool_text_add(out, "\n");
    }

    switch (token->action) {
    case LGCA_PUSH:
    case LGCA_SET:
    case LGCA_REPLACE:
      if (l_tool_sublime_context_index(grammar, token->action_target) < 0) {
        (void)nexus_stdio_stderr_write_formatted("genesis: the token $%s refers to the unknown context %s\n", token->name, token->action_target);
        sublime->problems++;
      } else {
        const char *target;

        target = sorted[i].role != NULL ? l_tool_sublime_variant(sublime, token->action_target, sorted[i].role)
                                        : l_tool_sublime_context_label(token->action_target);
        logos_tool_text_format(out, "      %s: %s\n", token->action == LGCA_PUSH ? "push" : "set", target);
      }
      break;
    case LGCA_POP:
      if (token->action_amount > 1) {
        logos_tool_text_format(out, "      pop: %lld\n", (long long)token->action_amount);
      } else {
        logos_tool_text_add(out, "      pop: true\n");
      }
      break;
    case LGCA_NONE:
    default:
      if (is_close) {
        logos_tool_text_add(out, "      pop: true\n");
      } else if (frame >= 0) {
        logos_tool_text_format(out, "      push: %s\n", sublime->frame_names[frame]);
      }
      break;
    }
  }

  nexus_memory_deallocate(sorted);
}

static void l_tool_sublime_meta(LToolSublime *sublime, const char *key, const char *value) {
  logos_tool_text_format(&sublime->out, "    - %s: %s\n", key, value);
}

/* Summary: a lexical context of the grammar: its tokens as they are, with the scopes the mapping gives the context. */
static void l_tool_sublime_lexical_context(LToolSublime *sublime, uint32 context_index) {
  const char *name;
  const char *scope;
  char        key[160];

  name = logos_languages_genesis_grammar_context_name_at(sublime->grammar, context_index);

  if (nexus_strings_string_equals(name, "ROOT")) {
    return;
  }

  logos_tool_text_format(&sublime->out, "  %s:\n", l_tool_sublime_context_label(name));
  scope = logos_tool_mapping_context_scope_get(sublime->mapping, name);

  if (scope != NULL && scope[0] != '\0') {
    l_tool_sublime_meta(sublime, "meta_scope", l_tool_sublime_full_scope(sublime, scope));
  }

  (void)stbsp_snprintf(key, (int)sizeof(key), "%s.content", name);
  scope = logos_tool_mapping_context_scope_get(sublime->mapping, key);

  if (scope != NULL && scope[0] != '\0') {
    l_tool_sublime_meta(sublime, "meta_content_scope", l_tool_sublime_full_scope(sublime, scope));
  }

  l_tool_sublime_patterns(sublime, NULL, name, NULL);
}

/* Summary: the scope a frame gets: the mapping of its rule, else meta.<rule> with hyphens. */
static const char *l_tool_sublime_frame_scope(LToolSublime *sublime, const LGenesisGrammarFrame *frame) {
  char        key[256];
  const char *scope;
  LToolText   text;
  char       *copy;
  uint_large  i;
  char        one[2];

  (void)stbsp_snprintf(key, (int)sizeof(key), "#%s", frame->rule);
  scope = logos_tool_mapping_context_scope_get(sublime->mapping, key);

  if (scope == NULL) {
    char plain[160];

    l_tool_sublime_plain_name(frame->rule, plain, sizeof(plain));
    (void)stbsp_snprintf(key, (int)sizeof(key), "#%s", plain);
    scope = logos_tool_mapping_context_scope_get(sublime->mapping, key);
  }

  if (scope != NULL && scope[0] != '\0') {
    return l_tool_sublime_full_scope(sublime, scope);
  }

  {
    char plain[160];

    l_tool_sublime_plain_name(frame->rule, plain, sizeof(plain));
    logos_tool_text_initialize(&text);
    logos_tool_text_add(&text, "meta.");
    one[1] = '\0';

    for (i = 0; plain[i] != '\0'; i++) {
      one[0] = plain[i];

      if (plain[i] == '_') {
        one[0] = '-';
      }

      logos_tool_text_add(&text, one);
    }
  }

  logos_tool_text_add(&text, ".");
  logos_tool_text_add(&text, sublime->suffix);
  copy = (char *)nexus_memory_allocate(logos_tool_text_length_get(&text) + 1);
  nexus_memory_bytes_copy(logos_tool_text_data_get(&text), copy, logos_tool_text_length_get(&text) + 1);
  logos_tool_text_release(&text);
  (void)nexus_data_list_append_pointer(sublime->owned, copy);

  return copy;
}

/* Summary: a frame's context: pushed by its opening token, popped by its closing token, holding what the grammar allows inside. */
static void l_tool_sublime_frame_context(LToolSublime *sublime, uint32 frame_index) {
  const LGenesisGrammarFrame *frame;
  NexusDataList              *entries;

  frame   = logos_languages_genesis_grammar_frame_at(sublime->grammar, frame_index);
  entries = nexus_data_list_create(NEXUS_SIZEOF(LToolEntry), 16);
  l_tool_sublime_closure(sublime, frame->token_refs, frame->token_ref_count, frame->rule_refs, frame->rule_ref_count, frame->rule, entries);

  {
    uint_large i;

    for (i = 0; i < nexus_data_list_length_get(sublime->global_entries); i++) {
      const LToolEntry *entry;

      entry = (const LToolEntry *)nexus_data_list_at_const(sublime->global_entries, i);
      l_tool_sublime_entry_add(entries, entry->token, entry->role);
    }
  }

  logos_tool_text_format(&sublime->out, "  %s:\n", sublime->frame_names[frame_index]);
  l_tool_sublime_meta(sublime, "meta_scope", l_tool_sublime_frame_scope(sublime, frame));
  l_tool_sublime_patterns(sublime, entries, "ROOT", frame->close);
  nexus_data_list_destroy(entries);
}

/* Summary: the main context: what the entry rule can start with plus the global rules; every root token when the grammar names no entry. */
static void l_tool_sublime_main_context(LToolSublime *sublime, NexusDataList *entries) {
  const char *entry_name;
  uint_large  i;

  entry_name = logos_languages_genesis_grammar_entry_get(sublime->grammar);

  if (entry_name != NULL) {
    l_tool_sublime_closure(sublime, NULL, 0, &entry_name, 1, NULL, entries);
  }

  for (i = 0; i < nexus_data_list_length_get(sublime->global_entries); i++) {
    const LToolEntry *entry;

    entry = (const LToolEntry *)nexus_data_list_at_const(sublime->global_entries, i);
    l_tool_sublime_entry_add(entries, entry->token, entry->role);
  }

  logos_tool_text_add(&sublime->out, "  main:\n");

  if (nexus_data_list_length_get(entries) == 0) {
    l_tool_sublime_patterns(sublime, NULL, "ROOT", NULL);
  } else {
    l_tool_sublime_patterns(sublime, entries, "ROOT", NULL);
  }
}

/* Summary: the "extensions" setting as a YAML list. */
static void l_tool_sublime_extensions(LToolSublime *sublime) {
  const char *setting;
  const char *cursor;
  const char *end;

  setting = logos_tool_mapping_setting_get(sublime->mapping, "extensions");

  if (setting == NULL || setting[0] == '\0') {
    return;
  }

  logos_tool_text_add(&sublime->out, "file_extensions:\n");
  cursor = setting;

  while (*cursor != '\0') {
    char       piece[64];
    uint_large length;

    while (*cursor == ' ' || *cursor == ',') {
      cursor++;
    }

    end = cursor;

    while (*end != '\0' && *end != ',' && *end != ' ') {
      end++;
    }

    length = (uint_large)(end - cursor);

    if (length > 0 && length < sizeof(piece)) {
      nexus_memory_bytes_copy(cursor, piece, length);
      piece[length] = '\0';
      logos_tool_text_format(&sublime->out, "  - %s\n", piece);
    }

    cursor = end;
  }
}

/* Summary: decides which frames are usable and names them, and finds what the global rules can match anywhere. */
static void l_tool_sublime_prepare(LToolSublime *sublime) {
  uint32 i;
  uint32 j;

  sublime->frame_names =
      (char **)nexus_memory_allocate((logos_languages_genesis_grammar_frame_count_get(sublime->grammar) + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  for (i = 0; i < logos_languages_genesis_grammar_frame_count_get(sublime->grammar); i++) {
    const LGenesisGrammarFrame *frame;
    const LGenesisGrammarToken *open;
    const LGenesisGrammarToken *close;
    char                        name[256];
    uint32                      same;

    frame                   = logos_languages_genesis_grammar_frame_at(sublime->grammar, i);
    open                    = l_tool_sublime_token_find(sublime, frame->open);
    close                   = l_tool_sublime_token_find(sublime, frame->close);
    sublime->frame_names[i] = NULL;

    /* A frame needs both tokens in the root context and no lexical actions: the lexer's own contexts already model those. */
    if (open == NULL || close == NULL || !nexus_strings_string_equals(open->context, "ROOT") ||
        !nexus_strings_string_equals(close->context, "ROOT") || open->action != LGCA_NONE || close->action != LGCA_NONE) {
      continue;
    }

    same = 0;

    for (j = 0; j < i; j++) {
      if (sublime->frame_names[j] != NULL &&
          nexus_strings_string_equals(logos_languages_genesis_grammar_frame_at(sublime->grammar, j)->rule, frame->rule)) {
        same++;
      }
    }

    if (same == 0) {
      (void)stbsp_snprintf(name, (int)sizeof(name), "frame-%s", frame->rule);
    } else {
      (void)stbsp_snprintf(name, (int)sizeof(name), "frame-%s-%u", frame->rule, (unsigned)(same + 1));
    }

    sublime->frame_names[i] = (char *)nexus_memory_allocate(nexus_strings_string_length_get(name) + 1);
    nexus_memory_bytes_copy(name, sublime->frame_names[i], nexus_strings_string_length_get(name) + 1);
  }

  sublime->global_entries = nexus_data_list_create(NEXUS_SIZEOF(LToolEntry), 4);

  {
    uint32       count;
    const char **globals;

    count   = logos_languages_genesis_grammar_global_count_get(sublime->grammar);
    globals = (const char **)nexus_memory_allocate((count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

    for (i = 0; i < count; i++) {
      globals[i] = logos_languages_genesis_grammar_global_at(sublime->grammar, i);
    }

    l_tool_sublime_closure(sublime, NULL, 0, globals, count, NULL, sublime->global_entries);
    nexus_memory_deallocate((void *)globals);
  }
}

/* ---------------------------------------------------------------------------- */
/* TESTS                                                                         */
/* ---------------------------------------------------------------------------- */

static void l_tool_sublime_assert(LToolText *out, const char *comment, uint_large column, uint_large width, const char *scope) {
  uint_large k;

  logos_tool_text_add(out, comment);

  for (k = nexus_strings_string_length_get(comment); k < column; k++) {
    logos_tool_text_add(out, " ");
  }

  for (k = 0; k < width; k++) {
    logos_tool_text_add(out, "^");
  }

  logos_tool_text_format(out, " %s\n", scope);
}

/*
A syntax test file for Sublime's own runner. Each fixed-text token that the main context matches and that has a scope gets a
line and an assertion; each usable frame whose tokens are fixed texts gets its opening and closing text on one line, asserting
the scopes of both and the scope of the frame.
*/
static void l_tool_sublime_tests(LToolSublime *sublime, const NexusDataList *main_entries, const char *comment, const char *package,
                                 const char *syntax_file, LToolText *out) {
  uint32     i;
  uint_large pad;
  uint_large k;

  pad = nexus_strings_string_length_get(comment) + 1;
  logos_tool_text_format(out, "%s SYNTAX TEST \"Packages/%s/%s\"\n", comment, package, syntax_file);

  for (i = 0; i < nexus_data_list_length_get(main_entries); i++) {
    const LGenesisGrammarToken *token;
    const char                 *scope;

    token = ((const LToolEntry *)nexus_data_list_at_const(main_entries, i))->token;
    scope = logos_tool_mapping_scope_get(sublime->mapping, token->name);

    if (token->literal == NULL || token->literal[0] == '\0' || token->action != LGCA_NONE || scope == NULL || scope[0] == '\0' ||
        logos_tool_string_find_byte(token->literal, '\n') != NULL || logos_tool_string_find_byte(token->literal, '\t') != NULL ||
        l_tool_sublime_frame_opened_by(sublime, token->name) >= 0) {
      continue;
    }

    for (k = 0; k < pad; k++) {
      logos_tool_text_add(out, " ");
    }

    logos_tool_text_format(out, "%s\n", token->literal);
    l_tool_sublime_assert(out, comment, pad, nexus_strings_string_length_get(token->literal), l_tool_sublime_full_scope(sublime, scope));
  }

  for (i = 0; i < logos_languages_genesis_grammar_frame_count_get(sublime->grammar); i++) {
    const LGenesisGrammarFrame *frame;
    const LGenesisGrammarToken *open;
    const LGenesisGrammarToken *close;
    const char                 *open_scope;
    const char                 *close_scope;

    if (sublime->frame_names[i] == NULL) {
      continue;
    }

    frame       = logos_languages_genesis_grammar_frame_at(sublime->grammar, i);
    open        = l_tool_sublime_token_find(sublime, frame->open);
    close       = l_tool_sublime_token_find(sublime, frame->close);
    open_scope  = logos_tool_mapping_scope_get(sublime->mapping, frame->open);
    close_scope = logos_tool_mapping_scope_get(sublime->mapping, frame->close);

    if (open->literal == NULL || close->literal == NULL || open_scope == NULL || close_scope == NULL || open_scope[0] == '\0' ||
        close_scope[0] == '\0' || logos_tool_string_find_byte(open->literal, '\n') != NULL ||
        logos_tool_string_find_byte(close->literal, '\n') != NULL) {
      continue;
    }

    for (k = 0; k < pad; k++) {
      logos_tool_text_add(out, " ");
    }

    logos_tool_text_format(out, "%s%s\n", open->literal, close->literal);
    l_tool_sublime_assert(out, comment, pad, nexus_strings_string_length_get(open->literal), l_tool_sublime_full_scope(sublime, open_scope));
    l_tool_sublime_assert(out, comment, pad + nexus_strings_string_length_get(open->literal), nexus_strings_string_length_get(close->literal),
                          l_tool_sublime_full_scope(sublime, close_scope));
    l_tool_sublime_assert(out, comment, pad, nexus_strings_string_length_get(open->literal) + nexus_strings_string_length_get(close->literal),
                          l_tool_sublime_frame_scope(sublime, frame));
  }
}

int logos_tool_sublime_run(const LToolOptions *options) {
  LGenesisCompiler *compiler;
  LGenesisEngine   *engine;
  LToolMapping      mapping;
  LToolSublime      sublime;
  const char       *setting;
  char             *mapping_directory;
  char             *directory;
  char             *syntax_name;
  char             *path;
  char             *suffix;
  char             *scope_default;
  boolean           changed;
  int               result;
  uint32            i;
  NexusDataList    *main_entries;

  engine = logos_tool_grammar_compile(options, &compiler);

  if (engine == NULL) {
    logos_languages_genesis_compiler_destroy(compiler);

    return 1;
  }

  if (!logos_tool_mapping_for(options, &mapping)) {
    logos_languages_genesis_engine_release(engine);
    logos_languages_genesis_compiler_destroy(compiler);

    return 1;
  }

  scope_default = NULL;
  setting       = logos_tool_mapping_setting_get(&mapping, "scope");

  if (setting == NULL || setting[0] == '\0') {
    char *lower;

    lower         = logos_tool_identifier_lower(logos_languages_genesis_engine_name_get(engine));
    scope_default = (char *)nexus_memory_allocate(nexus_strings_string_length_get(lower) + 8);
    (void)stbsp_snprintf(scope_default, (int)(nexus_strings_string_length_get(lower) + 8), "source.%s", lower);
    nexus_memory_deallocate(lower);
    setting = scope_default;
  }

  /* The language suffix of every scope is the last segment of the root scope. */
  suffix = (char *)nexus_memory_allocate(nexus_strings_string_length_get(setting) + 1);
  nexus_memory_bytes_copy(logos_tool_string_find_last_byte(setting, '.') != NULL ? logos_tool_string_find_last_byte(setting, '.') + 1 : setting,
                          suffix,
                          nexus_strings_string_length_get(
                              logos_tool_string_find_last_byte(setting, '.') != NULL ? logos_tool_string_find_last_byte(setting, '.') + 1 : setting) +
                              1);

  sublime.engine   = engine;
  sublime.grammar  = logos_languages_genesis_engine_grammar_get(engine);
  sublime.mapping  = &mapping;
  sublime.suffix   = suffix;
  sublime.problems = 0;
  sublime.variants = nexus_data_list_create(NEXUS_SIZEOF(LToolVariant), 4);
  sublime.owned    = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 16);
  logos_tool_text_initialize(&sublime.out);
  l_tool_sublime_prepare(&sublime);

  logos_tool_text_format(&sublime.out, "%%YAML 1.2\n---\n# Generated by genesis sublime from the language %s. Do not edit.\n",
                         logos_languages_genesis_engine_name_get(engine));
  logos_tool_text_format(&sublime.out, "name: %s\n", logos_languages_genesis_engine_name_get(engine));
  logos_tool_text_format(&sublime.out, "scope: %s\n", setting);
  l_tool_sublime_extensions(&sublime);
  logos_tool_text_add(&sublime.out, "contexts:\n");

  /* Main first, then the frames, the lexical contexts and the role variants. */
  main_entries = nexus_data_list_create(NEXUS_SIZEOF(LToolEntry), 32);
  l_tool_sublime_main_context(&sublime, main_entries);

  for (i = 0; i < logos_languages_genesis_grammar_frame_count_get(sublime.grammar); i++) {
    if (sublime.frame_names[i] != NULL) {
      l_tool_sublime_frame_context(&sublime, i);
    }
  }

  for (i = 0; i < logos_languages_genesis_grammar_context_count_get(sublime.grammar); i++) {
    l_tool_sublime_lexical_context(&sublime, i);
  }

  for (i = 0; i < nexus_data_list_length_get(sublime.variants); i++) {
    const LToolVariant *variant;

    variant = (const LToolVariant *)nexus_data_list_at_const(sublime.variants, i);
    logos_tool_text_format(&sublime.out, "  %s:\n", variant->name);
    l_tool_sublime_meta(&sublime, "meta_scope", variant->role);
    logos_tool_text_format(&sublime.out, "    - include: %s\n", l_tool_sublime_context_label(variant->base));
  }

  result = sublime.problems > 0 ? 1 : 0;

  mapping_directory = logos_tool_files_directory_of(mapping.path != NULL ? mapping.path : options->grammar);
  syntax_name       = (char *)nexus_memory_allocate(nexus_strings_string_length_get(logos_languages_genesis_engine_name_get(engine)) + 20);
  (void)stbsp_snprintf(syntax_name, (int)(nexus_strings_string_length_get(logos_languages_genesis_engine_name_get(engine)) + 20), "%s.sublime-syntax",
                       logos_languages_genesis_engine_name_get(engine));
  setting   = options->output != NULL ? options->output : logos_tool_mapping_setting_get(&mapping, "sublime.directory");
  directory = options->output != NULL ? logos_tool_files_join(NULL, options->output)
                                      : logos_tool_files_join(mapping_directory, setting != NULL ? setting : "generated");
  path      = logos_tool_files_join(directory, syntax_name);

  if (result == 0) {
    if (!logos_tool_files_write_if_changed(path, logos_tool_text_data_get(&sublime.out), logos_tool_text_length_get(&sublime.out), &changed)) {
      (void)nexus_stdio_stderr_write_formatted("genesis: cannot write %s\n", path);
      result = 1;
    } else if (!options->quiet) {
      (void)nexus_stdio_stderr_write_formatted("genesis: %s %s\n", changed ? "wrote" : "unchanged", path);
    }
  }

  if (result == 0 && options->install != NULL) {
    char *installed;

    installed = logos_tool_files_join(options->install, syntax_name);

    if (!logos_tool_files_write_if_changed(installed, logos_tool_text_data_get(&sublime.out), logos_tool_text_length_get(&sublime.out), &changed)) {
      (void)nexus_stdio_stderr_write_formatted("genesis: cannot write %s\n", installed);
      result = 1;
    } else if (!options->quiet) {
      (void)nexus_stdio_stderr_write_formatted("genesis: %s %s\n", changed ? "wrote" : "unchanged", installed);
    }

    nexus_memory_deallocate(installed);
  }

  /* Syntax tests, when a comment prefix is configured. */
  setting = logos_tool_mapping_setting_get(&mapping, "test.comment");

  if (result == 0 && setting != NULL && setting[0] != '\0') {
    LToolText   tests;
    const char *package;
    const char *extension;
    char       *tests_directory;
    char       *test_name;
    char       *test_path;
    char        first_extension[32];
    uint_large  length;

    package   = logos_tool_mapping_setting_get(&mapping, "sublime.package");
    extension = logos_tool_mapping_setting_get(&mapping, "extensions");
    length    = 0;

    if (extension != NULL) {
      while (extension[length] != '\0' && extension[length] != ',' && extension[length] != ' ' && length + 1 < sizeof(first_extension)) {
        first_extension[length] = extension[length];
        length++;
      }
    }

    first_extension[length] = '\0';
    logos_tool_text_initialize(&tests);
    l_tool_sublime_tests(&sublime, main_entries, setting, package != NULL ? package : "User", syntax_name, &tests);
    tests_directory = options->tests != NULL ? logos_tool_files_join(NULL, options->tests) : logos_tool_files_join(directory, "tests");
    test_name       = (char *)nexus_memory_allocate(nexus_strings_string_length_get(sublime.suffix) + length + 20);
    (void)stbsp_snprintf(test_name, (int)(nexus_strings_string_length_get(sublime.suffix) + length + 20), "syntax_test_%s%s%s", sublime.suffix,
                         length > 0 ? "." : "", first_extension);
    test_path = logos_tool_files_join(tests_directory, test_name);

    if (!logos_tool_files_write_if_changed(test_path, logos_tool_text_data_get(&tests), logos_tool_text_length_get(&tests), &changed)) {
      (void)nexus_stdio_stderr_write_formatted("genesis: cannot write %s\n", test_path);
      result = 1;
    } else if (!options->quiet) {
      (void)nexus_stdio_stderr_write_formatted("genesis: %s %s\n", changed ? "wrote" : "unchanged", test_path);
    }

    nexus_memory_deallocate(test_path);
    nexus_memory_deallocate(test_name);
    nexus_memory_deallocate(tests_directory);
    logos_tool_text_release(&tests);
  }

  nexus_memory_deallocate(path);
  nexus_memory_deallocate(directory);
  nexus_memory_deallocate(syntax_name);
  nexus_memory_deallocate(mapping_directory);
  nexus_memory_deallocate(suffix);

  if (scope_default != NULL) {
    nexus_memory_deallocate(scope_default);
  }

  {
    uint_large owned_index;

    for (owned_index = 0; owned_index < nexus_data_list_length_get(sublime.owned); owned_index++) {
      nexus_memory_deallocate(nexus_data_list_pointer_at(sublime.owned, owned_index));
    }
  }

  nexus_data_list_destroy(sublime.owned);
  nexus_data_list_destroy(sublime.variants);
  nexus_data_list_destroy(sublime.global_entries);
  nexus_data_list_destroy(main_entries);

  for (i = 0; i < logos_languages_genesis_grammar_frame_count_get(sublime.grammar); i++) {
    if (sublime.frame_names[i] != NULL) {
      nexus_memory_deallocate(sublime.frame_names[i]);
    }
  }

  nexus_memory_deallocate((void *)sublime.frame_names);
  logos_tool_text_release(&sublime.out);
  logos_tool_mapping_release(&mapping);
  logos_languages_genesis_engine_release(engine);
  logos_languages_genesis_compiler_destroy(compiler);

  return result;
}
