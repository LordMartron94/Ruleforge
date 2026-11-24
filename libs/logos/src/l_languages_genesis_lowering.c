#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
Lowering turns an analyzed language into the objects of the library: the lexer from the contexts (every token and
fragment becomes a Regula over bytes, character ranges become UTF-8 automata), the Contexta grammar from the rules,
Pratt rules and embeds, and the backtracking parser that runs it. The engine keeps the lexer, the grammar (embedding
builds guest parsers from it) and the label text both borrow.
*/

typedef struct LGRuleHandle {
  const char    *name;
  LContextaRule *rule;
} LGRuleHandle;

typedef struct LGLower {
  LGenesisCompiler *compiler;
  LGModel          *model;
  LGenesisEngine   *engine;
  NexusDataArena   *labels;
  LContexta        *contexta;
  NexusDataList    *rules;    /* LGRuleHandle */
  NexusDataList    *regulas;  /* LRegula *, released after the lexer is finalized */
  NexusDataList    *contexts; /* LLexerContext, parallel to the model's contexts */
  boolean           earley;   /* The PARSER block asks for the Earley engine: plain unions and optionals, no choice modes. */
  boolean           ok;
} LGLower;

static const char *l_genesis_label(LGLower *lower, const char *text) {
  return nexus_data_arena_text_copy(lower->labels, text, nexus_strings_string_length_get(text));
}

static void l_genesis_lower_error(LGLower *lower, const char *code, PharosSpan span, const char *message) {
  PharosDiagnostic *diagnostic;

  diagnostic = pharos_diagnostics_report((lower->compiler)->reporting, PDS_ERROR, code, span, "%s", message);
  pharos_diagnostics_label_add(diagnostic, span, TRUE, NULL);
  lower->ok = FALSE;
}

/* ---------------------------------------------------------------------------- */
/* UTF-8 CHARACTER RANGES                                                        */
/* ---------------------------------------------------------------------------- */

typedef struct LGUtf8Sequence {
  byte   low[4];
  byte   high[4];
  uint32 length;
} LGUtf8Sequence;

/* Summary: splits a range of scalar values into sequences of byte ranges that match exactly its UTF-8 encodings. */
static void l_genesis_utf8_split(NexusDataList *out, uint32 low, uint32 high) {
  static const uint32 maxima[] = {0x7F, 0x7FF, 0xFFFF};
  LGUtf8Sequence      sequence;
  uint32              index;
  uint32              mask;
  uint32              length;
  byte                encoded_low[4];
  byte                encoded_high[4];

  if (low > high) {
    return;
  }

  if (low <= 0xDFFF && high >= 0xD800) {
    if (low < 0xD800) {
      l_genesis_utf8_split(out, low, 0xD7FF);
    }

    if (high > 0xDFFF) {
      l_genesis_utf8_split(out, 0xE000, high);
    }

    return;
  }

  for (index = 0; index < 3; index++) {
    if (low <= maxima[index] && maxima[index] < high) {
      l_genesis_utf8_split(out, low, maxima[index]);
      l_genesis_utf8_split(out, maxima[index] + 1, high);

      return;
    }
  }

  for (index = 1; index < 4; index++) {
    mask = (((uint32)1) << (6 * index)) - 1;

    if ((low & ~mask) != (high & ~mask)) {
      if ((low & mask) != 0) {
        l_genesis_utf8_split(out, low, low | mask);
        l_genesis_utf8_split(out, (low | mask) + 1, high);

        return;
      }

      if ((high & mask) != mask) {
        l_genesis_utf8_split(out, low, (high & ~mask) - 1);
        l_genesis_utf8_split(out, high & ~mask, high);

        return;
      }
    }
  }

  length = nexus_strings_utf8_encode(low, encoded_low);
  (void)nexus_strings_utf8_encode(high, encoded_high);
  nexus_memory_bytes_clear(&sequence, NEXUS_SIZEOF(sequence));
  sequence.length = length;

  for (index = 0; index < length; index++) {
    sequence.low[index]  = encoded_low[index];
    sequence.high[index] = encoded_high[index];
  }

  (void)nexus_data_list_append(out, &sequence);
}

/* ---------------------------------------------------------------------------- */
/* LEXICAL EXPRESSIONS                                                           */
/* ---------------------------------------------------------------------------- */

/*
A token's language is written as byte-regexp text and imported by Regula, whose importer builds the operations in
dependency order. (Appending operations one by one would make Regula re-assemble earlier operations around the new
ones.) Fragments are expanded textually; every compound is wrapped in a group so nothing depends on precedence.
*/

typedef struct LGRegexp {
  NexusDataList *text; /* char */
  boolean        ok;
} LGRegexp;

static void l_genesis_regexp_text(LGRegexp *out, const char *text) {
  uint_large i;
  uint_large length;

  length = nexus_strings_string_length_get(text);

  for (i = 0; i < length; i++) {
    (void)nexus_data_list_append(out->text, &text[i]);
  }
}

static void l_genesis_regexp_byte(LGRegexp *out, byte value) {
  static const char digits[] = "0123456789ABCDEF";
  char              escape[5];

  escape[0] = '\\';
  escape[1] = 'x';
  escape[2] = digits[value >> 4];
  escape[3] = digits[value & 0x0F];
  escape[4] = '\0';
  l_genesis_regexp_text(out, escape);
}

static void l_genesis_regexp_range(LGRegexp *out, uint32 low, uint32 high) {
  NexusDataList *sequences;
  uint_large     i;

  sequences = nexus_data_list_create(NEXUS_SIZEOF(LGUtf8Sequence), 8);
  l_genesis_utf8_split(sequences, low, high);
  l_genesis_regexp_text(out, "(?:");

  for (i = 0; i < nexus_data_list_length_get(sequences); i++) {
    const LGUtf8Sequence *sequence;
    uint32                position;

    sequence = (const LGUtf8Sequence *)nexus_data_list_at_const(sequences, i);

    if (i > 0) {
      l_genesis_regexp_text(out, "|");
    }

    for (position = 0; position < sequence->length; position++) {
      l_genesis_regexp_text(out, "[");
      l_genesis_regexp_byte(out, sequence->low[position]);
      l_genesis_regexp_text(out, "-");
      l_genesis_regexp_byte(out, sequence->high[position]);
      l_genesis_regexp_text(out, "]");
    }
  }

  l_genesis_regexp_text(out, ")");
  nexus_data_list_destroy(sequences);
}

#define LG_FRAGMENT_DEPTH_MAX 64

static void l_genesis_lower_lexical(LGLower *lower, LGRegexp *out, const LGContextModel *context, const LGExpr *expression, uint32 depth) {
  const LGToken *fragment;
  byte           bytes[4];
  uint32         i;
  uint32         length;

  if (expression == NULL || depth > LG_FRAGMENT_DEPTH_MAX) {
    l_genesis_regexp_text(out, "(?:)");

    return;
  }

  switch (expression->kind) {
  case LGE_STRING:
    l_genesis_regexp_text(out, "(?:");

    for (i = 0; i < expression->codepoint_count; i++) {
      uint32 position;

      length = nexus_strings_utf8_encode(expression->codepoints[i], bytes);

      for (position = 0; position < length; position++) {
        l_genesis_regexp_byte(out, bytes[position]);
      }
    }

    l_genesis_regexp_text(out, ")");
    break;
  case LGE_TEXT:
    l_genesis_regexp_text(out, "(?:");

    for (i = 0; expression->name[i] != '\0'; i++) {
      l_genesis_regexp_byte(out, (byte)expression->name[i]);
    }

    l_genesis_regexp_text(out, ")");
    break;
  case LGE_RANGE:
    l_genesis_regexp_range(out, expression->lo, expression->hi);
    break;
  case LGE_ANY:
    l_genesis_regexp_range(out, 0, 0x10FFFF);
    break;
  case LGE_NOT: {
    NexusDataList *set;
    uint_large     k;

    /* The characters the operand leaves of the alphabet, as ranges of UTF-8 sequences. */
    set = nexus_data_list_create(NEXUS_SIZEOF(LGCodepointRange), 8);

    if (!logos_languages_genesis_analysis_charset_get(context, expression, set) || nexus_data_list_length_get(set) == 0) {
      l_genesis_regexp_text(out, "[]");
    } else {
      l_genesis_regexp_text(out, "(?:");

      for (k = 0; k < nexus_data_list_length_get(set); k++) {
        const LGCodepointRange *range;

        range = (const LGCodepointRange *)nexus_data_list_at_const(set, k);

        if (k > 0) {
          l_genesis_regexp_text(out, "|");
        }

        l_genesis_regexp_range(out, range->low, range->high);
      }

      l_genesis_regexp_text(out, ")");
    }

    nexus_data_list_destroy(set);
    break;
  }
  case LGE_FRAGMENT:
    fragment = logos_languages_genesis_analysis_fragment_find(context, expression->name);
    l_genesis_lower_lexical(lower, out, context, fragment->expression, depth + 1);
    break;
  case LGE_UNION:
    l_genesis_regexp_text(out, "(?:");

    for (i = 0; i < expression->item_count; i++) {
      if (i > 0) {
        l_genesis_regexp_text(out, "|");
      }

      l_genesis_lower_lexical(lower, out, context, expression->items[i], depth);
    }

    l_genesis_regexp_text(out, ")");
    break;
  case LGE_CONCAT:
    l_genesis_regexp_text(out, "(?:");

    for (i = 0; i < expression->item_count; i++) {
      l_genesis_lower_lexical(lower, out, context, expression->items[i], depth);
    }

    l_genesis_regexp_text(out, ")");
    break;
  case LGE_REPEAT:
    /* Always grouped: the child may itself end in a quantifier, and x+? would read as a lazy one. */
    l_genesis_regexp_text(out, "(?:");
    l_genesis_lower_lexical(lower, out, context, expression->child, depth);
    l_genesis_regexp_text(out, ")");

    if (expression->repeat_op == '{') {
      char counts[48];

      if (expression->hi == 0xFFFFFFFFU) {
        (void)stbsp_snprintf(counts, (int)sizeof(counts), "{%u,}", (unsigned)expression->lo);
      } else if (expression->hi == expression->lo) {
        (void)stbsp_snprintf(counts, (int)sizeof(counts), "{%u}", (unsigned)expression->lo);
      } else {
        (void)stbsp_snprintf(counts, (int)sizeof(counts), "{%u,%u}", (unsigned)expression->lo, (unsigned)expression->hi);
      }

      l_genesis_regexp_text(out, counts);
    } else {
      bytes[0] = (byte)expression->repeat_op;
      (void)nexus_data_list_append(out->text, bytes);
    }

    break;
  case LGE_GROUP:
    l_genesis_lower_lexical(lower, out, context, expression->child, depth);
    break;
  case LGE_EMPTY:
  default:
    l_genesis_regexp_text(out, "(?:)");
    break;
  }
}

static LLexerContext l_genesis_context_id(const LGLower *lower, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(lower->model->contexts); i++) {
    if (nexus_strings_string_equals(((const LGContextModel *)nexus_data_list_pointer_at(lower->model->contexts, i))->name, name)) {
      return *(const LLexerContext *)nexus_data_list_at_const(lower->contexts, i);
    }
  }

  return 0;
}

static int32 l_genesis_priority_clamp(int64 value) {
  if (value > 2147483647) {
    return 2147483647;
  }

  if (value < -2147483647) {
    return -2147483647;
  }

  return (int32)value;
}

static boolean l_genesis_lower_lexer(LGLower *lower, LogosRuntime *runtime) {
  LGModel    *model;
  LLexer     *lexer;
  NexusError *error;
  uint_large  i;
  uint_large  j;
  uint32      next_kind;

  model     = lower->model;
  lexer     = logos_language_tooling_lexing_lexer_create(runtime);
  next_kind = L_LEXER_TOKEN_KIND_FIRST_USER;

  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    const LGContextModel *context;
    LLexerContext         id;

    context = (const LGContextModel *)nexus_data_list_pointer_at(model->contexts, i);
    id      = logos_language_tooling_lexing_lexer_context_create(lexer, l_genesis_label(lower, context->name),
                                                                 nexus_strings_string_equals(context->name, "ROOT"));
    (void)nexus_data_list_append(lower->contexts, &id);
  }

  /* One kind per token name; a name declared in several contexts is the same kind there. */
  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    const LGContextModel *context;

    context = (const LGContextModel *)nexus_data_list_pointer_at(model->contexts, i);

    for (j = 0; j < nexus_data_list_length_get(context->tokens); j++) {
      LGToken *token;
      LGToken *first;

      token = (LGToken *)nexus_data_list_pointer_at(context->tokens, j);

      if (token->fragment) {
        continue;
      }

      first = logos_languages_genesis_analysis_token_find(model, token->name, NULL);

      if (first->kind == 0) {
        first->kind = next_kind++;
      }

      token->kind = first->kind;
    }
  }

  for (i = 0; i < nexus_data_list_length_get(model->contexts); i++) {
    const LGContextModel *context;
    LLexerContext         id;

    context = (const LGContextModel *)nexus_data_list_pointer_at(model->contexts, i);
    id      = *(const LLexerContext *)nexus_data_list_at_const(lower->contexts, i);

    for (j = 0; j < nexus_data_list_length_get(context->tokens); j++) {
      const LGToken  *token;
      LRegula        *regula;
      LLexerTokenRule rule;

      token = (const LGToken *)nexus_data_list_pointer_at(context->tokens, j);

      if (token->fragment) {
        continue;
      }

      {
        LGRegexp          regexp;
        LRegulaOperation *operation;
        NexusError       *regexp_error;
        char              terminator;

        regexp.text = nexus_data_list_create(NEXUS_SIZEOF(char), 256);
        regexp.ok   = TRUE;
        l_genesis_lower_lexical(lower, &regexp, context, token->expression, 0);
        terminator = '\0';
        (void)nexus_data_list_append(regexp.text, &terminator);
        regula = logos_languages_regula_regula_create(l_genesis_label(lower, token->name), NEXUS_VERSION_PACK(0, 0, 1, 0));
        (void)nexus_data_list_append_pointer(lower->regulas, regula);
        regexp_error =
            logos_languages_regula_regula_operation_add_regexp(NULL, regula, (const char *)nexus_data_list_data_get_const(regexp.text), &operation);

        if (regexp_error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
          PharosDiagnostic *diagnostic;

          diagnostic = pharos_diagnostics_report((lower->compiler)->reporting, PDS_ERROR, "G0262", token->span,
                                                 "the token `$%s` is too large to compile", token->name);
          pharos_diagnostics_label_add(diagnostic, token->span, TRUE, "%s",
                                       regexp_error->message != NULL ? regexp_error->message : "regular expression import failed");
          lower->ok = FALSE;
        }

        nexus_data_list_destroy(regexp.text);
      }

      nexus_memory_bytes_clear(&rule, NEXUS_SIZEOF(rule));
      rule.label          = l_genesis_label(lower, token->name);
      rule.token_language = regula;
      rule.token_kind     = token->kind;
      rule.match_priority = l_genesis_priority_clamp(token->priority);

      switch (token->action) {
      case LGA_PUSH:
        rule.action_kind              = LLTCAK_PUSH;
        rule.action_data.push_context = l_genesis_context_id(lower, token->target);
        break;
      case LGA_POP:
        rule.action_kind            = LLTCAK_POP;
        rule.action_data.pop_amount = (uint8)token->amount;
        break;
      case LGA_SET:
        rule.action_kind             = LLTCAK_SET;
        rule.action_data.set_context = l_genesis_context_id(lower, token->target);
        break;
      case LGA_REPLACE:
        rule.action_kind                 = LLTCAK_REPLACE;
        rule.action_data.replace_context = l_genesis_context_id(lower, token->target);
        break;
      case LGA_NONE:
      default:
        rule.action_kind = LLTCAK_NONE;
        break;
      }

      logos_language_tooling_lexing_lexer_context_token_rule_add(lexer, id, rule);
    }
  }

  if (!lower->ok) {
    for (i = 0; i < nexus_data_list_length_get(lower->regulas); i++) {
      logos_languages_regula_regula_destroy((LRegula *)nexus_data_list_pointer_at(lower->regulas, i));
    }

    nexus_data_list_destroy(lower->regulas);
    lower->regulas = NULL;
    logos_language_tooling_lexing_lexer_destroy(lexer);

    return FALSE;
  }

  error = logos_language_tooling_lexing_lexer_finalize(lexer);

  for (i = 0; i < nexus_data_list_length_get(lower->regulas); i++) {
    logos_languages_regula_regula_destroy((LRegula *)nexus_data_list_pointer_at(lower->regulas, i));
  }

  nexus_data_list_destroy(lower->regulas);
  lower->regulas = NULL;

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    PharosDiagnostic *diagnostic;

    diagnostic = pharos_diagnostics_report((lower->compiler)->reporting, PDS_ERROR, "G0260", model->unit->name_span,
                                           "the lexer of `%s` could not be compiled", model->unit->name);
    pharos_diagnostics_label_add(diagnostic, model->unit->name_span, TRUE, "%s",
                                 error->message != NULL ? error->message : "lexer compilation failed");
    pharos_diagnostics_note_add(
        diagnostic, "a context whose tokens need more than 65535 automaton states, or a token that can match nothing, cannot be compiled");
    logos_language_tooling_lexing_lexer_destroy(lexer);
    lower->ok = FALSE;

    return FALSE;
  }

  lower->engine->lexer = lexer;

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* GRAMMAR                                                                       */
/* ---------------------------------------------------------------------------- */

static LContextaRule *l_genesis_rule_handle(const LGLower *lower, const char *name) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(lower->rules); i++) {
    const LGRuleHandle *handle;

    handle = (const LGRuleHandle *)nexus_data_list_at_const(lower->rules, i);

    if (nexus_strings_string_equals(handle->name, name)) {
      return handle->rule;
    }
  }

  return NULL;
}

static LContextaChoiceMode l_genesis_choice_of(const LGAnnotation *annotations, uint32 count) {
  uint32 i;

  for (i = 0; i < count; i++) {
    if (nexus_strings_string_equals(annotations[i].name, (logos_languages_genesis_spec_word_text_get(LGSW_CHOICE) + 1)) &&
        annotations[i].argument_count == 1 && annotations[i].arguments[0]->kind == LGE_ENUM) {
      if (nexus_strings_string_equals(annotations[i].arguments[0]->name, "LONGEST")) {
        return LCCM_LONGEST;
      }

      if (nexus_strings_string_equals(annotations[i].arguments[0]->name, "UNAMBIGUOUS")) {
        return LCCM_UNAMBIGUOUS;
      }

      return LCCM_ORDERED;
    }
  }

  return LCCM_DEFAULT;
}

/* Summary: gives the Contexta rule the precedence of an `@prec(left, 30)` annotation; the analysis has checked its shape. */
static void l_genesis_precedence_apply(LGLower *lower, const LGRuleModel *rule) {
  uint32                 i;
  LContextaAssociativity associativity;

  for (i = 0; i < rule->annotation_count; i++) {
    const LGAnnotation *annotation;

    annotation = &rule->annotations[i];

    if (!nexus_strings_string_equals(annotation->name, (logos_languages_genesis_spec_word_text_get(LGSW_PREC) + 1)) ||
        annotation->argument_count != 2 || annotation->arguments[0]->kind != LGE_IDENT || annotation->arguments[1]->kind != LGE_INT) {
      continue;
    }

    associativity = LCPA_NONASSOC;

    if (nexus_strings_string_equals(annotation->arguments[0]->name, "left")) {
      associativity = LCPA_LEFT;
    } else if (nexus_strings_string_equals(annotation->arguments[0]->name, "right")) {
      associativity = LCPA_RIGHT;
    }
    logos_languages_contexta_contexta_rule_precedence_set(l_genesis_rule_handle(lower, rule->name), associativity,
                                                          (int32)annotation->arguments[1]->integer);
  }
}

static const LContextaOperation *l_genesis_exclude_wrap(LGLower *lower, const LContextaOperation *body, const LGAnnotation *annotations,
                                                        uint32 count) {
  uint32               i;
  uint32               j;
  uint32               total;
  const LContextaRule *rules[64];

  total = 0;

  for (i = 0; i < count; i++) {
    if (!nexus_strings_string_equals(annotations[i].name, (logos_languages_genesis_spec_word_text_get(LGSW_EXCLUDE) + 1))) {
      continue;
    }

    for (j = 0; j < annotations[i].argument_count && total < NEXUS_ARRAY_SIZE_ELEMENTS(rules); j++) {
      const LContextaRule *rule;

      rule = l_genesis_rule_handle(lower, annotations[i].arguments[j]->name);

      if (rule != NULL) {
        rules[total++] = rule;
      }
    }
  }

  if (total == 0) {
    return body;
  }

  return logos_languages_contexta_contexta_operation_add_exclude(NULL, lower->contexta, body, rules, total);
}

static const LContextaOperation *l_genesis_lower_grammar(LGLower *lower, const LGExpr *expression, LContextaChoiceMode mode);

/* Summary: item or nothing, greedy for the backtracking engine (a PEG takes the item when it matches), a plain optional for the Earley engine. */
static const LContextaOperation *l_genesis_lower_optional(LGLower *lower, const LContextaOperation *item) {
  if (lower->earley) {
    return logos_languages_contexta_contexta_operation_add_optional(NULL, lower->contexta, item);
  }

  return logos_languages_contexta_contexta_operation_add_choice(
      NULL, lower->contexta, item, logos_languages_contexta_contexta_operation_add_empty_string(NULL, lower->contexta), LCCM_ORDERED);
}

/*
Summary: a counted repetition of grammar, `x{n}`, `x{n,}` or `x{n,m}`, written out: n copies of x, then star(x) when the count is open, or the
  remaining m - n copies as nested options, x{2,4} being x x (x (x)?)?.
*/
static const LContextaOperation *l_genesis_lower_counted(LGLower *lower, const LGExpr *expression) {
  const LContextaOperation *result;
  const LContextaOperation *tail;
  uint32                    i;
  uint32                    optional;

  result = NULL;
  tail   = NULL;

  for (i = 0; i < expression->lo; i++) {
    const LContextaOperation *copy;

    copy   = l_genesis_lower_grammar(lower, expression->child, LCCM_DEFAULT);
    result = result == NULL ? copy : logos_languages_contexta_contexta_operation_add_concatenate(NULL, lower->contexta, result, copy);
  }

  if (expression->hi == 0xFFFFFFFFU) {
    tail =
        logos_languages_contexta_contexta_operation_add_star(NULL, lower->contexta, l_genesis_lower_grammar(lower, expression->child, LCCM_DEFAULT));
  } else {
    for (optional = expression->hi - expression->lo; optional > 0; optional--) {
      const LContextaOperation *copy;

      copy = l_genesis_lower_grammar(lower, expression->child, LCCM_DEFAULT);
      tail = l_genesis_lower_optional(
          lower, tail == NULL ? copy : logos_languages_contexta_contexta_operation_add_concatenate(NULL, lower->contexta, copy, tail));
    }
  }

  if (result == NULL) {
    return tail != NULL ? tail : logos_languages_contexta_contexta_operation_add_empty_string(NULL, lower->contexta);
  }

  return tail != NULL ? logos_languages_contexta_contexta_operation_add_concatenate(NULL, lower->contexta, result, tail) : result;
}

static const LContextaOperation *l_genesis_lower_grammar(LGLower *lower, const LGExpr *expression, LContextaChoiceMode mode) {
  const LContextaOperation *result;
  const LContextaOperation *item;
  const LGToken            *token;
  uint32                    i;

  if (expression == NULL) {
    return logos_languages_contexta_contexta_operation_add_empty_string(NULL, lower->contexta);
  }

  switch (expression->kind) {
  case LGE_TOKEN:
    token = logos_languages_genesis_analysis_token_find(lower->model, expression->name, NULL);

    return logos_languages_contexta_contexta_operation_add_token(NULL, lower->contexta, token->kind);
  case LGE_RULE:
    return logos_languages_contexta_contexta_operation_add_reference(NULL, lower->contexta, l_genesis_rule_handle(lower, expression->name));
  case LGE_CONCAT:
    result = l_genesis_lower_grammar(lower, expression->items[0], LCCM_DEFAULT);

    for (i = 1; i < expression->item_count; i++) {
      item   = l_genesis_lower_grammar(lower, expression->items[i], LCCM_DEFAULT);
      result = logos_languages_contexta_contexta_operation_add_concatenate(NULL, lower->contexta, result, item);
    }

    return result;
  case LGE_UNION:
    /* Right-nested choices of one mode are one alternative group to the engine. */
    result = l_genesis_lower_grammar(lower, expression->items[expression->item_count - 1], LCCM_DEFAULT);

    for (i = expression->item_count - 1; i > 0; i--) {
      item   = l_genesis_lower_grammar(lower, expression->items[i - 1], LCCM_DEFAULT);
      result = lower->earley ? logos_languages_contexta_contexta_operation_add_unionize(NULL, lower->contexta, item, result)
                             : logos_languages_contexta_contexta_operation_add_choice(NULL, lower->contexta, item, result, mode);
    }

    return result;
  case LGE_REPEAT:
    if (expression->repeat_op == '{') {
      return l_genesis_lower_counted(lower, expression);
    }

    item = l_genesis_lower_grammar(lower, expression->child, LCCM_DEFAULT);

    if (expression->repeat_op == '?' && lower->earley) {
      return logos_languages_contexta_contexta_operation_add_optional(NULL, lower->contexta, item);
    }

    if (expression->repeat_op == '?') {
      /* Greedy, as in a PEG: take the item when it matches, else nothing. add_optional puts nothing first, which an ordered choice would commit to. */
      return logos_languages_contexta_contexta_operation_add_choice(
          NULL, lower->contexta, item, logos_languages_contexta_contexta_operation_add_empty_string(NULL, lower->contexta), LCCM_ORDERED);
    }

    if (expression->repeat_op == '*') {
      return logos_languages_contexta_contexta_operation_add_star(NULL, lower->contexta, item);
    }

    return logos_languages_contexta_contexta_operation_add_plus(NULL, lower->contexta, item);
  case LGE_GROUP:
    result = l_genesis_lower_grammar(lower, expression->child, l_genesis_choice_of(expression->annotations, expression->annotation_count));

    return l_genesis_exclude_wrap(lower, result, expression->annotations, expression->annotation_count);
  case LGE_EMPTY:
  case LGE_NONE:
  default:
    return logos_languages_contexta_contexta_operation_add_empty_string(NULL, lower->contexta);
  }
}

static LLexerTokenKind l_genesis_token_kind(const LGLower *lower, const LGExpr *token) {
  return logos_languages_genesis_analysis_token_find(lower->model, token->name, NULL)->kind;
}

static const LContextaOperation *l_genesis_lower_pratt(LGLower *lower, const LGPrattModel *pratt) {
  const LContextaOperation *atom;
  LContextaOperatorEntry   *entries;
  uint_large                count;
  uint_large                i;
  const LContextaOperation *result;

  count   = nexus_data_list_length_get(pratt->entries);
  entries = (LContextaOperatorEntry *)nexus_memory_allocate((count + 1) * NEXUS_SIZEOF(LContextaOperatorEntry));
  atom    = l_genesis_lower_grammar(lower, pratt->atom, LCCM_DEFAULT);

  for (i = 0; i < count; i++) {
    const LGEntryModel *entry;

    entry = (const LGEntryModel *)nexus_data_list_pointer_at(pratt->entries, i);

    if (nexus_strings_string_equals(entry->kind, "prefix")) {
      entries[i].kind = LCOP_PREFIX;
    } else if (nexus_strings_string_equals(entry->kind, "postfix")) {
      entries[i].kind = LCOP_POSTFIX;
    } else {
      entries[i].kind = LCOP_INFIX;
    }

    entries[i].label      = l_genesis_label(lower, entry->token->name);
    entries[i].token_kind = l_genesis_token_kind(lower, entry->token);
    entries[i].left_bp    = l_genesis_priority_clamp(entry->left_bp);
    entries[i].right_bp   = l_genesis_priority_clamp(entry->right_bp);
  }

  result = logos_languages_contexta_contexta_operation_add_operators(NULL, lower->contexta, atom, entries, (uint32)count);
  nexus_memory_deallocate(entries);

  return result;
}

static const LGExpr *l_genesis_embed_value(const LGEmbedModel *embed, const char *key) {
  uint32 i;

  for (i = 0; i < embed->count; i++) {
    if (nexus_strings_string_equals(embed->keys[i], key)) {
      return embed->values[i];
    }
  }

  return NULL;
}

static const LContextaRule *l_genesis_guest_rule(const LGenesisEngine *guest, const char *name) {
  uint_large i;

  for (i = 0; i < logos_languages_contexta_contexta_rule_count_get(guest->contexta); i++) {
    const LContextaRule *rule;
    const char          *label;

    rule  = logos_languages_contexta_contexta_rule_at(guest->contexta, i);
    label = logos_languages_contexta_contexta_rule_label_get(rule);

    if (label != NULL && nexus_strings_string_equals(label, name)) {
      return rule;
    }
  }

  return NULL;
}

/* Summary: the token kinds of a global rule that is an alternative of tokens (the only kind the Earley engine can skip between tokens). */
static boolean l_genesis_trivia_kinds(LGLower *lower, const LGExpr *expression, LLexerTokenKind *kinds, uint32 *count, uint32 capacity) {
  uint32 i;

  if (expression == NULL) {
    return FALSE;
  }

  if (expression->kind == LGE_TOKEN) {
    const LGToken *token;

    token = logos_languages_genesis_analysis_token_find(lower->model, expression->name, NULL);

    if (token == NULL || *count >= capacity) {
      return FALSE;
    }

    kinds[(*count)++] = token->kind;

    return TRUE;
  }

  if (expression->kind == LGE_UNION || expression->kind == LGE_GROUP) {
    if (expression->kind == LGE_GROUP) {
      return l_genesis_trivia_kinds(lower, expression->child, kinds, count, capacity);
    }

    for (i = 0; i < expression->item_count; i++) {
      if (!l_genesis_trivia_kinds(lower, expression->items[i], kinds, count, capacity)) {
        return FALSE;
      }
    }

    return TRUE;
  }

  if (expression->kind == LGE_RULE) {
    const LGRuleModel *rule;

    rule = logos_languages_genesis_analysis_rule_find(lower->model, expression->name);

    return rule != NULL && l_genesis_trivia_kinds(lower, rule->expression, kinds, count, capacity);
  }

  return FALSE;
}

static const LContextaOperation *l_genesis_lower_embed(LGLower *lower, const LGEmbedModel *embed) {
  const LGExpr        *language;
  const LGExpr        *entry;
  const LGExpr        *mode;
  const LGExpr        *boundary;
  const LGExpr        *content;
  const LGImport      *import;
  LGenesisEngine      *guest;
  const LContextaRule *guest_entry;
  LParserConfiguration configuration;
  LParser             *parser;
  NexusError          *error;
  LContextaEmbed       spec;

  language = l_genesis_embed_value(embed, "language");
  entry    = l_genesis_embed_value(embed, "entry");
  mode     = l_genesis_embed_value(embed, "mode");
  import   = logos_languages_genesis_analysis_import_find(lower->model, language->name);
  guest    = import->entry->engine;

  guest_entry = l_genesis_guest_rule(guest, entry->name);

  if (guest_entry == NULL) {
    l_genesis_lower_error(lower, "G0258", embed->span, "the guest language does not define the entry rule");

    return logos_languages_contexta_contexta_operation_add_empty_string(NULL, lower->contexta);
  }

  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.engine            = LPE_BACKTRACKING;
  configuration.entry             = guest_entry;
  configuration.choice            = guest->choice;
  configuration.global_rules      = guest->global_rules;
  configuration.global_rule_count = guest->global_count;
  configuration.grammar_expected  = guest->grammar_expected;
  error                           = logos_language_tooling_parsing_parser_create(guest->lexer, guest->contexta, &configuration, &parser);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    PharosDiagnostic *diagnostic;

    diagnostic = pharos_diagnostics_report((lower->compiler)->reporting, PDS_ERROR, "G0259", embed->span,
                                           "cannot create a parser for the guest language `%s`", guest->name);
    pharos_diagnostics_label_add(diagnostic, embed->span, TRUE, "%s", error->message != NULL ? error->message : "parser creation failed");
    lower->ok = FALSE;

    return logos_languages_contexta_contexta_operation_add_empty_string(NULL, lower->contexta);
  }

  (void)nexus_data_list_append_pointer(lower->engine->guest_parsers, parser);
  logos_languages_genesis_engine_retain(guest);
  (void)nexus_data_list_append_pointer(lower->engine->guests, guest);

  nexus_memory_bytes_clear(&spec, NEXUS_SIZEOF(spec));
  spec.guest = parser;

  if (nexus_strings_string_equals(mode->name, "region")) {
    boundary = l_genesis_embed_value(embed, "close_boundary");

    spec.mode           = LCEM_REGION;
    spec.open_token     = l_genesis_token_kind(lower, l_genesis_embed_value(embed, "open"));
    spec.close_token    = l_genesis_token_kind(lower, l_genesis_embed_value(embed, "close"));
    spec.close_boundary = LCEB_ANY;

    if (boundary != NULL && boundary->kind == LGE_ENUM && nexus_strings_string_equals(boundary->name, "LINE_START")) {
      spec.close_boundary = LCEB_LINE_START;
    }
  } else {
    content = l_genesis_embed_value(embed, "content");

    spec.mode         = LCEM_TOKEN;
    spec.source_token = l_genesis_token_kind(lower, l_genesis_embed_value(embed, "source"));
    spec.prefix_bytes = (uint32)content->items[0]->integer;
    spec.suffix_bytes = (uint32)content->items[1]->integer;
  }

  return logos_languages_contexta_contexta_operation_add_embed(l_genesis_label(lower, embed->name), lower->contexta, &spec);
}

static void l_genesis_rule_declare(LGLower *lower, const char *name) {
  LGRuleHandle handle;

  handle.name = l_genesis_label(lower, name);
  handle.rule = logos_languages_contexta_contexta_rule_add(handle.name, lower->contexta);
  (void)nexus_data_list_append(lower->rules, &handle);
}

static boolean l_genesis_lower_grammar_block(LGLower *lower) {
  LGModel              *model;
  uint_large            i;
  uint32                j;
  LContextaRule        *entry_rule;
  const LContextaRule **globals;
  LLexerTokenKind       trivia[256];
  uint32                trivia_count;
  uint32                global_count;
  LParserConfiguration  configuration;
  LParser              *parser;
  NexusError           *error;

  model           = lower->model;
  lower->contexta = logos_languages_contexta_contexta_create(l_genesis_label(lower, model->unit->name), NEXUS_VERSION_PACK(0, 0, 1, 0));

  for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
    l_genesis_rule_declare(lower, ((const LGRuleModel *)nexus_data_list_pointer_at(model->rules, i))->name);
  }

  for (i = 0; i < nexus_data_list_length_get(model->pratts); i++) {
    l_genesis_rule_declare(lower, ((const LGPrattModel *)nexus_data_list_pointer_at(model->pratts, i))->name);
  }

  for (i = 0; i < nexus_data_list_length_get(model->embeds); i++) {
    l_genesis_rule_declare(lower, ((const LGEmbedModel *)nexus_data_list_pointer_at(model->embeds, i))->name);
  }

  for (i = 0; i < nexus_data_list_length_get(model->rules); i++) {
    const LGRuleModel        *rule;
    const LContextaOperation *body;

    rule = (const LGRuleModel *)nexus_data_list_pointer_at(model->rules, i);
    body = l_genesis_lower_grammar(lower, rule->expression, l_genesis_choice_of(rule->annotations, rule->annotation_count));
    body = l_genesis_exclude_wrap(lower, body, rule->annotations, rule->annotation_count);
    logos_languages_contexta_contexta_rule_define(lower->contexta, l_genesis_rule_handle(lower, rule->name), body);
    l_genesis_precedence_apply(lower, rule);
  }

  for (i = 0; i < nexus_data_list_length_get(model->pratts); i++) {
    const LGPrattModel *pratt;

    pratt = (const LGPrattModel *)nexus_data_list_pointer_at(model->pratts, i);
    logos_languages_contexta_contexta_rule_define(lower->contexta, l_genesis_rule_handle(lower, pratt->name), l_genesis_lower_pratt(lower, pratt));
  }

  for (i = 0; i < nexus_data_list_length_get(model->embeds); i++) {
    const LGEmbedModel *embed;

    embed = (const LGEmbedModel *)nexus_data_list_pointer_at(model->embeds, i);
    logos_languages_contexta_contexta_rule_define(lower->contexta, l_genesis_rule_handle(lower, embed->name), l_genesis_lower_embed(lower, embed));
  }

  if (!lower->ok) {
    return FALSE;
  }

  entry_rule   = NULL;
  globals      = NULL;
  global_count = 0;

  logos_language_tooling_parsing_parser_configuration_initialize(&configuration);
  configuration.engine = LPE_BACKTRACKING;
  trivia_count         = 0;

  for (j = 0; j < model->parser_count; j++) {
    const LGExpr *value;

    value = model->parser_values[j];

    if (nexus_strings_string_equals(model->parser_keys[j], "entry")) {
      entry_rule = l_genesis_rule_handle(lower, value->name);
    } else if (nexus_strings_string_equals(model->parser_keys[j], "global")) {
      uint32 count;
      uint32 k;

      count   = value->kind == LGE_CONCAT ? value->item_count : 1;
      globals = (const LContextaRule **)nexus_memory_allocate((count + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

      for (k = 0; k < count; k++) {
        const LGExpr *item;

        item = value->kind == LGE_CONCAT ? value->items[k] : value;

        globals[k] = l_genesis_rule_handle(lower, item->name);
      }

      global_count = count;
    } else if (nexus_strings_string_equals(model->parser_keys[j], "choice")) {
      if (nexus_strings_string_equals(value->name, "LONGEST")) {
        configuration.choice = LCCM_LONGEST;
      } else if (nexus_strings_string_equals(value->name, "UNAMBIGUOUS")) {
        configuration.choice = LCCM_UNAMBIGUOUS;
      } else {
        configuration.choice = LCCM_ORDERED;
      }
    } else if (nexus_strings_string_equals(model->parser_keys[j], "lexical_selection")) {
      configuration.grammar_expected = nexus_strings_string_equals(value->name, "grammar_expected");
    } else if (nexus_strings_string_equals(model->parser_keys[j], "engine")) {
      configuration.engine = nexus_strings_string_equals(value->name, "earley") ? LPE_EARLEY : LPE_BACKTRACKING;
    }
  }

  logos_languages_contexta_contexta_start_rule_set(lower->contexta, entry_rule);

  /* The Earley engine has no global rules: what they match is skipped as tokens allowed anywhere. */
  if (configuration.engine == LPE_EARLEY && global_count > 0) {
    uint32 g;

    for (g = 0; g < global_count; g++) {
      const LGRuleModel *rule;

      rule = logos_languages_genesis_analysis_rule_find(lower->model, logos_languages_contexta_contexta_rule_label_get(globals[g]));

      if (rule == NULL || !l_genesis_trivia_kinds(lower, rule->expression, trivia, &trivia_count, 256)) {
        l_genesis_lower_error(lower, "G0262", lower->model->parser_span,
                              "with the Earley engine a global rule must be an alternative of tokens, which are skipped between tokens");

        return FALSE;
      }
    }

    configuration.allowed_tokens      = trivia;
    configuration.allowed_token_count = trivia_count;
    global_count                      = 0;
    nexus_memory_deallocate((void *)globals);
    globals = NULL;
  }

  configuration.entry             = entry_rule;
  configuration.global_rules      = globals;
  configuration.global_rule_count = global_count;
  error                           = logos_language_tooling_parsing_parser_create(lower->engine->lexer, lower->contexta, &configuration, &parser);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    PharosDiagnostic *diagnostic;

    diagnostic = pharos_diagnostics_report((lower->compiler)->reporting, PDS_ERROR, "G0261", model->unit->name_span,
                                           "the grammar of `%s` could not be compiled", model->unit->name);
    pharos_diagnostics_label_add(diagnostic, model->unit->name_span, TRUE, "%s", error->message != NULL ? error->message : "parser creation failed");
    lower->ok = FALSE;

    if (globals != NULL) {
      nexus_memory_deallocate((void *)globals);
    }

    return FALSE;
  }

  lower->engine->parser           = parser;
  lower->engine->global_rules     = globals;
  lower->engine->global_count     = global_count;
  lower->engine->choice           = configuration.choice;
  lower->engine->grammar_expected = configuration.grammar_expected;

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* ENTRY                                                                         */
/* ---------------------------------------------------------------------------- */

boolean logos_languages_genesis_lowering_language_compile(LGenesisCompiler *compiler, LGModel *model, LGenesisEngine *engine) {
  LGLower    lower;
  uint_large i;
  boolean    ok;

  NEXUS_PROGRAM_PRECONDITION(compiler != NULL && model != NULL && engine != NULL, "arguments must not be NULL");

  lower.compiler = compiler;
  lower.model    = model;
  lower.engine   = engine;
  lower.labels   = (NexusDataArena *)nexus_memory_allocate(NEXUS_SIZEOF(NexusDataArena));
  nexus_data_arena_initialize(lower.labels);
  engine->labels = lower.labels;
  lower.contexta = NULL;
  lower.rules    = nexus_data_list_create(NEXUS_SIZEOF(LGRuleHandle), 32);
  lower.regulas  = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 32);
  lower.contexts = nexus_data_list_create(NEXUS_SIZEOF(LLexerContext), 4);
  lower.ok       = TRUE;
  lower.earley   = FALSE;

  {
    uint32 setting;

    for (setting = 0; setting < model->parser_count; setting++) {
      if (nexus_strings_string_equals(model->parser_keys[setting], "engine") && model->parser_values[setting]->kind == LGE_IDENT &&
          nexus_strings_string_equals(model->parser_values[setting]->name, "earley")) {
        lower.earley = TRUE;
      }
    }
  }

  for (i = 0; i < nexus_data_list_length_get(model->imports); i++) {
    const LGImport *import;
    uint_large      length;
    char           *copy;

    import = (const LGImport *)nexus_data_list_pointer_at(model->imports, i);
    length = nexus_strings_string_length_get(import->name);
    copy   = (char *)nexus_memory_allocate(length + 1);
    nexus_memory_bytes_copy(import->name, copy, length + 1);
    (void)nexus_data_list_append_pointer(engine->imports, copy);
  }

  ok = l_genesis_lower_lexer(&lower, compiler->runtime);

  if (lower.regulas != NULL) {
    for (i = 0; i < nexus_data_list_length_get(lower.regulas); i++) {
      logos_languages_regula_regula_destroy((LRegula *)nexus_data_list_pointer_at(lower.regulas, i));
    }

    nexus_data_list_destroy(lower.regulas);
  }

  if (ok) {
    ok = l_genesis_lower_grammar_block(&lower);
  }

  if (ok && lower.ok) {
    logos_languages_genesis_grammar_build(engine, model);
  }

  /* The grammar is kept even when the parser failed, so teardown is uniform: the engine destroys what exists. */
  engine->contexta = lower.contexta;

  nexus_data_list_destroy(lower.rules);
  nexus_data_list_destroy(lower.contexts);

  return ok && lower.ok;
}
