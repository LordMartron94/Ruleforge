#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
A language defined as Logos objects, and its export to Genesis source. Tokens are Regula languages over bytes (bytes from
0x80 up stand for the non-ASCII characters), rules are Contexta operations; the export walks both and writes what the
Genesis compiler reads back: character classes as ranges, runs of characters as quoted strings, x x* as x+, a choice
between the empty string and x as x?.
*/

typedef struct LGDefinitionToken {
  char                 *name;
  char                 *context;
  LRegula              *language;
  int32                 priority;
  LGenesisContextAction action;
  char                 *target;
  int64                 amount;
  LLexerTokenKind       kind;
} LGDefinitionToken;

struct LGenesisDefinition {
  NexusDataList       *labels; /* char *: names the Contexta borrows. */
  char                *name;
  char                *version;
  NexusDataList       *tokens; /* LGDefinitionToken * */
  LContexta           *contexta;
  const LContextaRule *entry;
  NexusDataList       *globals; /* const LContextaRule * */
  boolean              grammar_expected;
};

static char *l_definition_copy(const char *text) {
  uint_large length;
  char      *copy;

  length = nexus_strings_string_length_get(text);
  copy   = (char *)nexus_memory_allocate(length + 1);
  nexus_memory_bytes_copy(text, copy, length + 1);

  return copy;
}

LGenesisDefinition *logos_languages_genesis_definition_create(const char *name, const char *version) {
  LGenesisDefinition *definition;

  NEXUS_PROGRAM_PRECONDITION(name != NULL, "name must not be NULL");

  definition                   = (LGenesisDefinition *)nexus_memory_allocate(NEXUS_SIZEOF(*definition));
  definition->name             = l_definition_copy(name);
  definition->version          = version != NULL ? l_definition_copy(version) : NULL;
  definition->labels           = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  definition->tokens           = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  definition->contexta         = logos_languages_contexta_contexta_create(definition->name, NEXUS_VERSION_PACK(0, 0, 1, 0));
  definition->entry            = NULL;
  definition->globals          = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 2);
  definition->grammar_expected = FALSE;

  return definition;
}

void logos_languages_genesis_definition_destroy(LGenesisDefinition *definition) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(definition != NULL, "definition must not be NULL");

  for (i = 0; i < nexus_data_list_length_get(definition->tokens); i++) {
    LGDefinitionToken *token;

    token = (LGDefinitionToken *)nexus_data_list_pointer_at(definition->tokens, i);
    logos_languages_regula_regula_destroy(token->language);
    nexus_memory_deallocate(token->name);
    nexus_memory_deallocate(token->context);

    if (token->target != NULL) {
      nexus_memory_deallocate(token->target);
    }

    nexus_memory_deallocate(token);
  }

  nexus_data_list_destroy(definition->tokens);
  nexus_data_list_destroy(definition->globals);
  logos_languages_contexta_contexta_destroy(definition->contexta);

  for (i = 0; i < nexus_data_list_length_get(definition->labels); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(definition->labels, i));
  }

  nexus_data_list_destroy(definition->labels);
  nexus_memory_deallocate(definition->name);

  if (definition->version != NULL) {
    nexus_memory_deallocate(definition->version);
  }

  nexus_memory_deallocate(definition);
}

LLexerTokenKind logos_languages_genesis_definition_token_add(LGenesisDefinition *definition, const char *name, const char *context,
                                                             const char *regexp, int32 priority, LGenesisContextAction action, const char *target,
                                                             int64 amount) {
  LGDefinitionToken *token;
  LRegulaOperation  *operation;
  NexusError        *error;
  uint_large         i;

  token           = (LGDefinitionToken *)nexus_memory_allocate(NEXUS_SIZEOF(*token));
  token->name     = l_definition_copy(name);
  token->context  = l_definition_copy(context);
  token->language = logos_languages_regula_regula_create(token->name, NEXUS_VERSION_PACK(0, 0, 1, 0));
  error           = logos_languages_regula_regula_operation_add_regexp(NULL, token->language, regexp, &operation);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    NEXUS_PROGRAM_PANIC_ERROR(error);
  }

  token->priority = priority;
  token->action   = action;
  token->target   = target != NULL ? l_definition_copy(target) : NULL;
  token->amount   = amount;
  token->kind     = (LLexerTokenKind)(L_LEXER_TOKEN_KIND_FIRST_USER + nexus_data_list_length_get(definition->tokens));

  /* A name declared in several contexts is one kind. */
  for (i = 0; i < nexus_data_list_length_get(definition->tokens); i++) {
    const LGDefinitionToken *earlier;

    earlier = (const LGDefinitionToken *)nexus_data_list_pointer_at(definition->tokens, i);

    if (nexus_strings_string_equals(earlier->name, name)) {
      token->kind = earlier->kind;
      break;
    }
  }

  (void)nexus_data_list_append_pointer(definition->tokens, token);

  return token->kind;
}

LContextaRule *logos_languages_genesis_definition_rule_add(LGenesisDefinition *definition, const char *name) {
  char *label;

  NEXUS_PROGRAM_PRECONDITION(definition != NULL && name != NULL, "arguments must not be NULL");

  label = l_definition_copy(name);
  (void)nexus_data_list_append_pointer(definition->labels, label);

  return logos_languages_contexta_contexta_rule_add(label, definition->contexta);
}

LContexta *logos_languages_genesis_definition_contexta_get(LGenesisDefinition *definition) {
  NEXUS_PROGRAM_PRECONDITION(definition != NULL, "definition must not be NULL");

  return definition->contexta;
}

void logos_languages_genesis_definition_entry_set(LGenesisDefinition *definition, const LContextaRule *rule) {
  definition->entry = rule;
}

void logos_languages_genesis_definition_global_add(LGenesisDefinition *definition, const LContextaRule *rule) {
  (void)nexus_data_list_append_pointer(definition->globals, rule);
}

void logos_languages_genesis_definition_grammar_expected_set(LGenesisDefinition *definition, boolean grammar_expected) {
  definition->grammar_expected = grammar_expected;
}

/* ---------------------------------------------------------------------------- */
/* TEXT                                                                          */
/* ---------------------------------------------------------------------------- */

static void l_definition_add(NexusDataList *out, const char *text) {
  uint_large i;

  for (i = 0; text[i] != '\0'; i++) {
    (void)nexus_data_list_append(out, &text[i]);
  }
}

static void l_definition_format(NexusDataList *out, const char *format, ...) {
  char    buffer[128];
  va_list arguments;

  va_start(arguments, format);
  (void)stbsp_vsnprintf(buffer, (int)sizeof(buffer), format, arguments);
  va_end(arguments);
  l_definition_add(out, buffer);
}

/* Summary: a code point inside a quoted Genesis literal. */
static void l_definition_codepoint(NexusDataList *out, uint32 value) {
  switch (value) {
  case '\'':
    l_definition_add(out, "\\'");
    break;
  case '\\':
    l_definition_add(out, "\\\\");
    break;
  case '\n':
    l_definition_add(out, "\\n");
    break;
  case '\t':
    l_definition_add(out, "\\t");
    break;
  case '\r':
    l_definition_add(out, "\\r");
    break;
  default:
    if (value >= 0x20 && value < 0x7F) {
      char one;

      one = (char)value;
      (void)nexus_data_list_append(out, &one);
    } else {
      l_definition_format(out, "\\u{%X}", (unsigned)value);
    }
    break;
  }
}

/* ---------------------------------------------------------------------------- */
/* REGULA TO GENESIS                                                             */
/* ---------------------------------------------------------------------------- */

#define LG_PRECEDENCE_UNION  1
#define LG_PRECEDENCE_CONCAT 2
#define LG_PRECEDENCE_ATOM   3

typedef struct LGExport {
  NexusDataList *out;
  boolean        ok;
} LGExport;

static boolean l_definition_regula_equal(const LRegulaOperation *left, const LRegulaOperation *right) {
  uint32 i;

  if (left == right) {
    return TRUE;
  }

  if (left == NULL || right == NULL || left->kind != right->kind) {
    return FALSE;
  }

  switch (left->kind) {
  case LROK_SYMBOL:
    for (i = 0; i < 4; i++) {
      if (left->operation_data.symbol_mask[i] != right->operation_data.symbol_mask[i]) {
        return FALSE;
      }
    }

    return TRUE;
  case LROK_UNIONIZE:
    return l_definition_regula_equal(left->operation_data.unionize.operation_a, right->operation_data.unionize.operation_a) &&
           l_definition_regula_equal(left->operation_data.unionize.operation_b, right->operation_data.unionize.operation_b);
  case LROK_CONCATENATE:
    return l_definition_regula_equal(left->operation_data.concatenate.operation_a, right->operation_data.concatenate.operation_a) &&
           l_definition_regula_equal(left->operation_data.concatenate.operation_b, right->operation_data.concatenate.operation_b);
  case LROK_STAR:
    return l_definition_regula_equal(left->operation_data.operation_star, right->operation_data.operation_star);
  case LROK_EMPTY_STRING:
  case LROK_EMPTY_LANGUAGE:
    return TRUE;
  case LROK_COUNT:
  default:
    return FALSE;
  }
}

/* Summary: appends the items of a nested concatenation (or union) in order. */
static void l_definition_flatten(const LRegulaOperation *operation, LRegulaOperationKind kind, NexusDataList *items) {
  const LRegulaOperation *first;
  const LRegulaOperation *second;

  if (operation->kind != kind) {
    (void)nexus_data_list_append_pointer(items, operation);

    return;
  }

  first  = kind == LROK_UNIONIZE ? operation->operation_data.unionize.operation_a : operation->operation_data.concatenate.operation_a;
  second = kind == LROK_UNIONIZE ? operation->operation_data.unionize.operation_b : operation->operation_data.concatenate.operation_b;
  l_definition_flatten(first, kind, items);
  l_definition_flatten(second, kind, items);
}

/* Summary: if the symbol is exactly one ASCII byte, returns TRUE with the byte. */
static boolean l_definition_single_byte(const LRegulaOperation *operation, byte *out) {
  uint32 count;
  uint32 i;

  if (operation->kind != LROK_SYMBOL) {
    return FALSE;
  }

  count = 0;

  for (i = 0; i < 128; i++) {
    if (logos_languages_regula_observation_mask_get(operation->operation_data.symbol_mask, (byte)i)) {
      *out = (byte)i;
      count++;
    }
  }

  for (i = 128; i < 256; i++) {
    if (logos_languages_regula_observation_mask_get(operation->operation_data.symbol_mask, (byte)i)) {
      return FALSE;
    }
  }

  return count == 1;
}

/* Summary: appends the code point ranges of a mask as "'a'..'z' || 'x'". A mask with some but not all of the upper half fails the export. */
static void l_definition_class(LGExport *state, const uint64 *mask) {
  uint32  starts[130];
  uint32  ends[130];
  uint32  count;
  uint32  i;
  boolean upper_all;
  boolean upper_any;

  upper_all = TRUE;
  upper_any = FALSE;

  for (i = 128; i < 256; i++) {
    if (logos_languages_regula_observation_mask_get(mask, (byte)i)) {
      upper_any = TRUE;
    } else {
      upper_all = FALSE;
    }
  }

  if (upper_any && !upper_all) {
    state->ok = FALSE;
  }

  count = 0;
  i     = 0;

  while (i < 128) {
    if (!logos_languages_regula_observation_mask_get(mask, (byte)i)) {
      i++;
      continue;
    }

    starts[count] = i;

    while (i + 1 < 128 && logos_languages_regula_observation_mask_get(mask, (byte)(i + 1))) {
      i++;
    }

    ends[count] = i;
    count++;
    i++;
  }

  if (upper_all) {
    if (count > 0 && ends[count - 1] == 127) {
      ends[count - 1] = 0x10FFFF;
    } else {
      starts[count] = 0x80;
      ends[count]   = 0x10FFFF;
      count++;
    }
  }

  for (i = 0; i < count; i++) {
    if (i > 0) {
      l_definition_add(state->out, " || ");
    }

    l_definition_add(state->out, "'");
    l_definition_codepoint(state->out, starts[i]);
    l_definition_add(state->out, "'");

    if (ends[i] != starts[i]) {
      l_definition_add(state->out, "..'");
      l_definition_codepoint(state->out, ends[i]);
      l_definition_add(state->out, "'");
    }
  }
}

static void l_definition_regula(LGExport *state, const LRegulaOperation *operation, uint32 precedence);

static void l_definition_wrap_open(LGExport *state, boolean wrap) {
  if (wrap) {
    l_definition_add(state->out, "(");
  }
}

static void l_definition_wrap_close(LGExport *state, boolean wrap) {
  if (wrap) {
    l_definition_add(state->out, ")");
  }
}

static void l_definition_regula(LGExport *state, const LRegulaOperation *operation, uint32 precedence) {
  NexusDataList *items;
  uint_large     i;
  byte           single;
  boolean        wrap;

  switch (operation->kind) {
  case LROK_EMPTY_STRING:
    l_definition_add(state->out, "()");
    break;
  case LROK_EMPTY_LANGUAGE:
    state->ok = FALSE;
    break;
  case LROK_SYMBOL:
    if (l_definition_single_byte(operation, &single)) {
      l_definition_add(state->out, "'");
      l_definition_codepoint(state->out, single);
      l_definition_add(state->out, "'");
    } else {
      /* A class is a union of ranges. */
      wrap = precedence > LG_PRECEDENCE_UNION;
      l_definition_wrap_open(state, wrap);
      l_definition_class(state, operation->operation_data.symbol_mask);
      l_definition_wrap_close(state, wrap);
    }
    break;
  case LROK_UNIONIZE:
    items = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    l_definition_flatten(operation, LROK_UNIONIZE, items);

    /* A choice between the empty string and one thing is that thing, optional. */
    if (nexus_data_list_length_get(items) == 2 && ((const LRegulaOperation *)nexus_data_list_pointer_at(items, 0))->kind == LROK_EMPTY_STRING) {
      l_definition_regula(state, (const LRegulaOperation *)nexus_data_list_pointer_at(items, 1), LG_PRECEDENCE_ATOM);
      l_definition_add(state->out, "?");
    } else if (nexus_data_list_length_get(items) == 2 &&
               ((const LRegulaOperation *)nexus_data_list_pointer_at(items, 1))->kind == LROK_EMPTY_STRING) {
      l_definition_regula(state, (const LRegulaOperation *)nexus_data_list_pointer_at(items, 0), LG_PRECEDENCE_ATOM);
      l_definition_add(state->out, "?");
    } else {
      /* Single-character items merge into one class. */
      uint64         merged[4];
      NexusDataList *others;
      boolean        have_class;

      merged[0] = merged[1] = merged[2] = merged[3] = 0;
      have_class                                    = FALSE;
      others                                        = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);

      for (i = 0; i < nexus_data_list_length_get(items); i++) {
        const LRegulaOperation *item;

        item = (const LRegulaOperation *)nexus_data_list_pointer_at(items, i);

        if (item->kind == LROK_SYMBOL) {
          merged[0] |= item->operation_data.symbol_mask[0];
          merged[1] |= item->operation_data.symbol_mask[1];
          merged[2] |= item->operation_data.symbol_mask[2];
          merged[3] |= item->operation_data.symbol_mask[3];
          have_class = TRUE;
        } else {
          (void)nexus_data_list_append_pointer(others, item);
        }
      }

      wrap = precedence > LG_PRECEDENCE_UNION && (nexus_data_list_length_get(others) + (have_class ? 1U : 0U)) > 1;
      l_definition_wrap_open(state, wrap);

      if (have_class) {
        l_definition_class(state, merged);
      }

      for (i = 0; i < nexus_data_list_length_get(others); i++) {
        if (have_class || i > 0) {
          l_definition_add(state->out, " || ");
        }

        l_definition_regula(state, (const LRegulaOperation *)nexus_data_list_pointer_at(others, i), LG_PRECEDENCE_CONCAT);
      }

      l_definition_wrap_close(state, wrap);
      nexus_data_list_destroy(others);
    }

    nexus_data_list_destroy(items);
    break;
  case LROK_CONCATENATE:
    items = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    l_definition_flatten(operation, LROK_CONCATENATE, items);
    wrap = precedence > LG_PRECEDENCE_CONCAT && nexus_data_list_length_get(items) > 1;
    l_definition_wrap_open(state, wrap);
    i = 0;

    while (i < nexus_data_list_length_get(items)) {
      const LRegulaOperation *item;
      const LRegulaOperation *next;

      item = (const LRegulaOperation *)nexus_data_list_pointer_at(items, i);

      if (i > 0) {
        l_definition_add(state->out, " ");
      }

      /* A run of single characters is one quoted string. */
      if (l_definition_single_byte(item, &single)) {
        l_definition_add(state->out, "'");
        l_definition_codepoint(state->out, single);
        i++;

        while (i < nexus_data_list_length_get(items) &&
               l_definition_single_byte((const LRegulaOperation *)nexus_data_list_pointer_at(items, i), &single)) {
          l_definition_codepoint(state->out, single);
          i++;
        }

        l_definition_add(state->out, "'");
        continue;
      }

      /* x followed by x* is x+. */
      next = i + 1 < nexus_data_list_length_get(items) ? (const LRegulaOperation *)nexus_data_list_pointer_at(items, i + 1) : NULL;

      if (next != NULL && next->kind == LROK_STAR && l_definition_regula_equal(item, next->operation_data.operation_star)) {
        l_definition_regula(state, item, LG_PRECEDENCE_ATOM);
        l_definition_add(state->out, "+");
        i += 2;
        continue;
      }

      l_definition_regula(state, item, LG_PRECEDENCE_CONCAT + 1);
      i++;
    }

    l_definition_wrap_close(state, wrap);
    nexus_data_list_destroy(items);
    break;
  case LROK_STAR:
    l_definition_regula(state, operation->operation_data.operation_star, LG_PRECEDENCE_ATOM);
    l_definition_add(state->out, "*");
    break;
  case LROK_COUNT:
  default:
    state->ok = FALSE;
    break;
  }
}

/* ---------------------------------------------------------------------------- */
/* CONTEXTA TO GENESIS                                                           */
/* ---------------------------------------------------------------------------- */

typedef struct LGContextaExport {
  const LGenesisDefinition *definition;
  NexusDataList            *out;
  boolean                   ok;
} LGContextaExport;

static const char *l_definition_token_name(const LGenesisDefinition *definition, LLexerTokenKind kind) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(definition->tokens); i++) {
    const LGDefinitionToken *token;

    token = (const LGDefinitionToken *)nexus_data_list_pointer_at(definition->tokens, i);

    if (token->kind == kind) {
      return token->name;
    }
  }

  return NULL;
}

static boolean l_definition_contexta_equal(const LContextaOperation *left, const LContextaOperation *right) {
  const LContextaOperationView *left_view;
  const LContextaOperationView *right_view;

  if (left == right) {
    return TRUE;
  }

  left_view  = logos_languages_contexta_contexta_operation_view_get(left);
  right_view = logos_languages_contexta_contexta_operation_view_get(right);

  if (left_view->kind != right_view->kind) {
    return FALSE;
  }

  switch (left_view->kind) {
  case LCOK_TOKEN:
    return left_view->operation_data.token_kind == right_view->operation_data.token_kind;
  case LCOK_REFERENCE:
    return left_view->operation_data.reference == right_view->operation_data.reference;
  case LCOK_EMPTY_STRING:
    return TRUE;
  case LCOK_CONCATENATE:
  case LCOK_UNIONIZE:
    return l_definition_contexta_equal(left_view->operation_data.binary.operation_a, right_view->operation_data.binary.operation_a) &&
           l_definition_contexta_equal(left_view->operation_data.binary.operation_b, right_view->operation_data.binary.operation_b);
  case LCOK_STAR:
    return l_definition_contexta_equal(left_view->operation_data.operation_star, right_view->operation_data.operation_star);
  default:
    return FALSE;
  }
}

static void l_definition_contexta_flatten(const LContextaOperation *operation, LContextaOperationKind kind, NexusDataList *items) {
  const LContextaOperationView *view;

  view = logos_languages_contexta_contexta_operation_view_get(operation);

  if (view->kind != kind) {
    (void)nexus_data_list_append_pointer(items, operation);

    return;
  }

  l_definition_contexta_flatten(view->operation_data.binary.operation_a, kind, items);
  l_definition_contexta_flatten(view->operation_data.binary.operation_b, kind, items);
}

static void l_definition_contexta(LGContextaExport *state, const LContextaOperation *operation, uint32 precedence);

static void l_definition_contexta(LGContextaExport *state, const LContextaOperation *operation, uint32 precedence) {
  const LContextaOperationView *view;
  NexusDataList                *items;
  uint_large                    i;
  boolean                       wrap;

  view = logos_languages_contexta_contexta_operation_view_get(operation);

  switch (view->kind) {
  case LCOK_EMPTY_STRING:
    l_definition_add(state->out, "()");
    break;
  case LCOK_TOKEN: {
    const char *name;

    name = l_definition_token_name(state->definition, view->operation_data.token_kind);

    if (name == NULL) {
      state->ok = FALSE;
    } else {
      l_definition_format(state->out, "$%s", name);
    }
  } break;
  case LCOK_REFERENCE:
    l_definition_format(state->out, "#%s", logos_languages_contexta_contexta_rule_label_get(view->operation_data.reference));
    break;
  case LCOK_STAR:
    l_definition_contexta(state, view->operation_data.operation_star, LG_PRECEDENCE_ATOM);
    l_definition_add(state->out, "*");
    break;
  case LCOK_CONCATENATE:
    items = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    l_definition_contexta_flatten(operation, LCOK_CONCATENATE, items);
    wrap = precedence > LG_PRECEDENCE_CONCAT && nexus_data_list_length_get(items) > 1;
    if (wrap) {
      l_definition_add(state->out, "(");
    }

    i = 0;

    while (i < nexus_data_list_length_get(items)) {
      const LContextaOperation     *item;
      const LContextaOperation     *next;
      const LContextaOperationView *next_view;

      item = (const LContextaOperation *)nexus_data_list_pointer_at(items, i);

      if (i > 0) {
        l_definition_add(state->out, " ");
      }

      next      = i + 1 < nexus_data_list_length_get(items) ? (const LContextaOperation *)nexus_data_list_pointer_at(items, i + 1) : NULL;
      next_view = next != NULL ? logos_languages_contexta_contexta_operation_view_get(next) : NULL;

      if (next_view != NULL && next_view->kind == LCOK_STAR && l_definition_contexta_equal(item, next_view->operation_data.operation_star)) {
        l_definition_contexta(state, item, LG_PRECEDENCE_ATOM);
        l_definition_add(state->out, "+");
        i += 2;
        continue;
      }

      l_definition_contexta(state, item, LG_PRECEDENCE_CONCAT + 1);
      i++;
    }

    if (wrap) {
      l_definition_add(state->out, ")");
    }

    nexus_data_list_destroy(items);
    break;
  case LCOK_UNIONIZE:
    items = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    l_definition_contexta_flatten(operation, LCOK_UNIONIZE, items);

    if (nexus_data_list_length_get(items) == 2 &&
        logos_languages_contexta_contexta_operation_view_get((const LContextaOperation *)nexus_data_list_pointer_at(items, 0))->kind ==
            LCOK_EMPTY_STRING) {
      l_definition_contexta(state, (const LContextaOperation *)nexus_data_list_pointer_at(items, 1), LG_PRECEDENCE_ATOM);
      l_definition_add(state->out, "?");
    } else {
      wrap = precedence > LG_PRECEDENCE_UNION;

      if (wrap) {
        l_definition_add(state->out, "(");
      }

      for (i = 0; i < nexus_data_list_length_get(items); i++) {
        if (i > 0) {
          l_definition_add(state->out, " || ");
        }

        l_definition_contexta(state, (const LContextaOperation *)nexus_data_list_pointer_at(items, i), LG_PRECEDENCE_CONCAT);
      }

      if (wrap) {
        l_definition_add(state->out, ")");
      }
    }

    nexus_data_list_destroy(items);
    break;
  case LCOK_CHOICE: {
    const char *mode;

    mode = "ORDERED";

    if (view->operation_data.choice.mode == LCCM_LONGEST) {
      mode = "LONGEST";
    } else if (view->operation_data.choice.mode == LCCM_UNAMBIGUOUS) {
      mode = "UNAMBIGUOUS";
    }

    l_definition_add(state->out, "(");
    l_definition_contexta(state, view->operation_data.choice.operation_a, LG_PRECEDENCE_CONCAT);
    l_definition_add(state->out, " || ");
    l_definition_contexta(state, view->operation_data.choice.operation_b, LG_PRECEDENCE_CONCAT);
    l_definition_format(state->out, ") @choice(ChoiceMode.%s)", mode);
  } break;
  case LCOK_EXCLUDE:
    l_definition_add(state->out, "(");
    l_definition_contexta(state, view->operation_data.exclude.operation, LG_PRECEDENCE_UNION);
    l_definition_add(state->out, ") @exclude(");

    for (i = 0; i < view->operation_data.exclude.count; i++) {
      l_definition_format(state->out, "%s#%s", i > 0 ? ", " : "",
                          logos_languages_contexta_contexta_rule_label_get(view->operation_data.exclude.rules[i]));
    }

    l_definition_add(state->out, ")");
    break;
  default:
    state->ok = FALSE;
    break;
  }
}

/* ---------------------------------------------------------------------------- */
/* EXPORT                                                                        */
/* ---------------------------------------------------------------------------- */

static char *l_definition_text_close(NexusDataList *out) {
  char  terminator;
  char *copy;

  terminator = '\0';
  (void)nexus_data_list_append(out, &terminator);
  copy = l_definition_copy((const char *)nexus_data_list_data_get_const(out));
  nexus_data_list_destroy(out);

  return copy;
}

/* Summary: the word `@prec` writes for an associativity. */
static const char *l_definition_associativity_name(LContextaAssociativity associativity) {
  switch (associativity) {
  case LCPA_LEFT:
    return "left";
  case LCPA_RIGHT:
    return "right";
  case LCPA_NONE:
  case LCPA_NONASSOC:
  default:
    return "nonassoc";
  }
}

char *logos_languages_genesis_definition_export_text(const LGenesisDefinition *definition) {
  LGenesisWriter *writer;
  uint_large      i;
  char           *text;
  boolean         ok;

  NEXUS_PROGRAM_PRECONDITION(definition != NULL, "definition must not be NULL");

  ok     = TRUE;
  writer = logos_languages_genesis_writer_create(definition->name, definition->version);

  logos_languages_genesis_writer_comment_add(writer, "Generated by the genesis tool from the language definition of the same name. Do not edit.");

  if (definition->entry != NULL) {
    logos_languages_genesis_writer_parser_entry_set(writer, logos_languages_contexta_contexta_rule_label_get(definition->entry));
  }

  for (i = 0; i < nexus_data_list_length_get(definition->globals); i++) {
    logos_languages_genesis_writer_parser_global_add(
        writer, logos_languages_contexta_contexta_rule_label_get((const LContextaRule *)nexus_data_list_pointer_at(definition->globals, i)));
  }

  logos_languages_genesis_writer_parser_grammar_expected_set(writer, definition->grammar_expected);

  for (i = 0; i < nexus_data_list_length_get(definition->tokens); i++) {
    const LGDefinitionToken *token;
    LGExport                 state;
    char                    *expression;

    token     = (const LGDefinitionToken *)nexus_data_list_pointer_at(definition->tokens, i);
    state.out = nexus_data_list_create(NEXUS_SIZEOF(char), 128);
    state.ok  = token->language->root_rule != NULL;

    if (state.ok) {
      l_definition_regula(&state, token->language->root_rule, LG_PRECEDENCE_UNION);
    }

    ok         = ok && state.ok;
    expression = l_definition_text_close(state.out);
    logos_languages_genesis_writer_token_add(writer, token->context, token->name, expression, token->priority, token->action, token->target,
                                             token->amount);
    nexus_memory_deallocate(expression);
  }

  for (i = 0; i < logos_languages_contexta_contexta_rule_count_get(definition->contexta); i++) {
    const LContextaRule *rule;
    LGContextaExport     state;
    char                *expression;

    rule             = logos_languages_contexta_contexta_rule_at(definition->contexta, i);
    state.definition = definition;
    state.out        = nexus_data_list_create(NEXUS_SIZEOF(char), 128);
    state.ok         = TRUE;
    l_definition_contexta(&state, logos_languages_contexta_contexta_rule_body_get(rule), LG_PRECEDENCE_UNION);
    ok         = ok && state.ok;
    expression = l_definition_text_close(state.out);
    {
      char                   annotation[64];
      int32                  level;
      LContextaAssociativity associativity;

      annotation[0] = '\0';
      associativity = logos_languages_contexta_contexta_rule_precedence_get(rule, &level);

      if (associativity != LCPA_NONE) {
        (void)stbsp_snprintf(annotation, (int)sizeof(annotation), "@prec(%s, %d)", l_definition_associativity_name(associativity), (int)level);
      }

      logos_languages_genesis_writer_rule_add(writer, logos_languages_contexta_contexta_rule_label_get(rule), annotation, expression);
    }

    nexus_memory_deallocate(expression);
  }

  text = ok ? logos_languages_genesis_writer_text_get(writer) : NULL;
  logos_languages_genesis_writer_destroy(writer);

  return text;
}
