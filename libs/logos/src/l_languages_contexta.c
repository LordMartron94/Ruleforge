#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
Summary: stores one fully initialized immutable node at a stable slab address.
*/
static LContextaOperation *l_languages_contexta_operation_store(LContexta *contexta, const char *label, LContextaOperationKind kind) {
  LContextaOperation operation;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL, "contexta must not be NULL");

  operation.owner      = contexta;
  operation.index      = nexus_data_slabbed_list_length_get(contexta->operations);
  operation.view.label = label;
  operation.view.kind  = kind;
  nexus_memory_bytes_clear(&operation.view.operation_data, NEXUS_SIZEOF(operation.view.operation_data));

  return (LContextaOperation *)nexus_data_slabbed_list_append(contexta->operations, &operation);
}

LContexta *logos_languages_contexta_contexta_create(const char *label, NexusSemanticVersion version) {
  LContexta *contexta;

  contexta               = (LContexta *)nexus_memory_allocate(NEXUS_SIZEOF(*contexta));
  contexta->label        = label;
  contexta->version      = version;
  contexta->operations   = nexus_data_slabbed_list_create(NEXUS_SIZEOF(LContextaOperation), 64, 64);
  contexta->rules        = nexus_data_slabbed_list_create(NEXUS_SIZEOF(LContextaRule), 16, 16);
  contexta->start_rule   = NULL;
  contexta->token_sets   = nexus_data_list_create(NEXUS_SIZEOF(LContextaTokenSet), 8);
  contexta->pratt_tables = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 4);

  return contexta;
}

void logos_languages_contexta_contexta_destroy(LContexta *contexta) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL, "contexta must not be NULL");

  for (i = 0; i < nexus_data_list_length_get(contexta->token_sets); i++) {
    nexus_memory_deallocate(((LContextaTokenSet *)nexus_data_list_at(contexta->token_sets, i))->kinds);
  }

  for (i = 0; i < nexus_data_list_length_get(contexta->pratt_tables); i++) {
    nexus_memory_deallocate(nexus_data_list_pointer_at(contexta->pratt_tables, i));
  }

  nexus_data_list_destroy(contexta->pratt_tables);
  nexus_data_list_destroy(contexta->token_sets);
  nexus_data_slabbed_list_destroy(contexta->rules);
  nexus_data_slabbed_list_destroy(contexta->operations);
  nexus_memory_deallocate(contexta);
}

LContextaRule *logos_languages_contexta_contexta_rule_add(const char *label, LContexta *contexta) {
  LContextaRule rule;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL, "contexta must not be NULL");

  rule.label         = label;
  rule.owner         = contexta;
  rule.body          = NULL;
  rule.index         = nexus_data_slabbed_list_length_get(contexta->rules);
  rule.associativity = LCPA_NONE;
  rule.level         = 0;

  return (LContextaRule *)nexus_data_slabbed_list_append(contexta->rules, &rule);
}

void logos_languages_contexta_contexta_rule_define(LContexta *contexta, LContextaRule *rule, const LContextaOperation *body) {
  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && rule != NULL && body != NULL, "definition arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(rule->owner == contexta && body->owner == contexta, "rule and body must belong to contexta");
  NEXUS_PROGRAM_PRECONDITION(rule->body == NULL, "a rule may be defined only once");

  rule->body = body;
}

void logos_languages_contexta_contexta_rule_precedence_set(LContextaRule *rule, LContextaAssociativity associativity, int32 level) {
  NEXUS_PROGRAM_PRECONDITION(rule != NULL, "rule must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(associativity != LCPA_NONE && (uint32)associativity <= (uint32)LCPA_NONASSOC, "a precedence needs an associativity");

  rule->associativity = associativity;
  rule->level         = level;
}

LContextaAssociativity logos_languages_contexta_contexta_rule_precedence_get(const LContextaRule *rule, int32 *out_level) {
  NEXUS_PROGRAM_PRECONDITION(rule != NULL, "rule must not be NULL");

  if (out_level != NULL) {
    *out_level = rule->level;
  }

  return rule->associativity;
}

void logos_languages_contexta_contexta_start_rule_set(LContexta *contexta, const LContextaRule *rule) {
  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && rule != NULL, "contexta and rule must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(rule->owner == contexta, "rule must belong to contexta");

  contexta->start_rule = rule;
}

const LContextaRule *logos_languages_contexta_contexta_start_rule_get(const LContexta *contexta) {
  NEXUS_PROGRAM_PRECONDITION(contexta != NULL, "contexta must not be NULL");

  return contexta->start_rule;
}

uint_large logos_languages_contexta_contexta_rule_count_get(const LContexta *contexta) {
  NEXUS_PROGRAM_PRECONDITION(contexta != NULL, "contexta must not be NULL");

  return nexus_data_slabbed_list_length_get(contexta->rules);
}

const LContextaRule *logos_languages_contexta_contexta_rule_at(const LContexta *contexta, uint_large index) {
  NEXUS_PROGRAM_PRECONDITION(contexta != NULL, "contexta must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(index < nexus_data_slabbed_list_length_get(contexta->rules), "rule index must be in bounds");

  return (const LContextaRule *)nexus_data_slabbed_list_at(contexta->rules, index);
}

const char *logos_languages_contexta_contexta_rule_label_get(const LContextaRule *rule) {
  NEXUS_PROGRAM_PRECONDITION(rule != NULL, "rule must not be NULL");

  return rule->label;
}

const LContextaOperation *logos_languages_contexta_contexta_rule_body_get(const LContextaRule *rule) {
  NEXUS_PROGRAM_PRECONDITION(rule != NULL, "rule must not be NULL");

  return rule->body;
}

const LContextaOperationView *logos_languages_contexta_contexta_operation_view_get(const LContextaOperation *operation) {
  NEXUS_PROGRAM_PRECONDITION(operation != NULL, "operation must not be NULL");

  return &operation->view;
}

NexusError *logos_languages_contexta_contexta_validate(const LContexta *contexta) {
  const LContextaRule *rule;
  uint_large           count;
  uint_large           i;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL, "contexta must not be NULL");

  if (contexta->start_rule == NULL) {
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_CONTEXTA_MISSING_START, "Contexta has no start rule");
  }

  count = nexus_data_slabbed_list_length_get(contexta->rules);

  for (i = 0; i < count; i++) {
    rule = (const LContextaRule *)nexus_data_slabbed_list_at(contexta->rules, i);

    if (rule->body == NULL) {
      return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_CONTEXTA_UNDEFINED_RULE, "Contexta rule %llu (%s) is undefined", (unsigned long long)i,
                                         rule->label != NULL ? rule->label : "unlabeled");
    }
  }

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

/* ---------------------------------------------------------------------------- */
/* EXPRESSIONS                                                                  */
/* ---------------------------------------------------------------------------- */

LContextaOperation *logos_languages_contexta_contexta_operation_add_empty_language(const char *label, LContexta *contexta) {
  return l_languages_contexta_operation_store(contexta, label, LCOK_EMPTY_LANGUAGE);
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_empty_string(const char *label, LContexta *contexta) {
  return l_languages_contexta_operation_store(contexta, label, LCOK_EMPTY_STRING);
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_symbol(const char *label, LContexta *contexta, byte symbol) {
  LContextaOperation *operation;

  operation = l_languages_contexta_operation_store(contexta, label, LCOK_SYMBOL);
  logos_languages_regula_observation_mask_set(operation->view.operation_data.symbol_mask, symbol, TRUE);

  return operation;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_symbol_range(const char *label, LContexta *contexta, byte range_start,
                                                                                 uint16 range_end) {
  LContextaOperation *operation;

  NEXUS_PROGRAM_PRECONDITION(range_start < range_end && range_end <= 256, "symbol range must be nonempty and within the byte alphabet");

  operation = l_languages_contexta_operation_store(contexta, label, LCOK_SYMBOL);
  logos_languages_regula_observation_mask_set_range(operation->view.operation_data.symbol_mask, range_start, range_end, TRUE);

  return operation;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_symbol_mask(const char *label, LContexta *contexta,
                                                                                const LContextaObservationMask mask, boolean negated) {
  LContextaOperation *operation;

  NEXUS_PROGRAM_PRECONDITION(mask != NULL, "mask must not be NULL");

  operation = l_languages_contexta_operation_store(contexta, label, LCOK_SYMBOL);
  nexus_memory_bytes_copy(mask, operation->view.operation_data.symbol_mask, NEXUS_SIZEOF(LContextaObservationMask));

  if (negated) {
    logos_languages_regula_observation_mask_invert(operation->view.operation_data.symbol_mask);
  }

  return operation;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_literal(const char *label, LContexta *contexta, const byte *literal,
                                                                            uint32 literal_size) {
  LContextaOperation *operation;
  LContextaOperation *symbol;
  uint32              i;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL, "contexta must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(literal != NULL || literal_size == 0, "nonempty literal must not be NULL");

  if (literal_size == 0) {
    return logos_languages_contexta_contexta_operation_add_empty_string(label, contexta);
  }

  operation = logos_languages_contexta_contexta_operation_add_symbol(literal_size == 1 ? label : NULL, contexta, literal[0]);

  for (i = 1; i < literal_size; i++) {
    symbol    = logos_languages_contexta_contexta_operation_add_symbol(NULL, contexta, literal[i]);
    operation = logos_languages_contexta_contexta_operation_add_concatenate(i + 1 == literal_size ? label : NULL, contexta, operation, symbol);
  }

  return operation;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_unionize(const char *label, LContexta *contexta,
                                                                             const LContextaOperation *operation_a,
                                                                             const LContextaOperation *operation_b) {
  LContextaOperation *operation;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && operation_a != NULL && operation_b != NULL, "union arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation_a->owner == contexta && operation_b->owner == contexta, "operands must belong to contexta");

  operation                                         = l_languages_contexta_operation_store(contexta, label, LCOK_UNIONIZE);
  operation->view.operation_data.binary.operation_a = operation_a;
  operation->view.operation_data.binary.operation_b = operation_b;

  return operation;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_concatenate(const char *label, LContexta *contexta,
                                                                                const LContextaOperation *operation_a,
                                                                                const LContextaOperation *operation_b) {
  LContextaOperation *operation;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && operation_a != NULL && operation_b != NULL, "concatenation arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation_a->owner == contexta && operation_b->owner == contexta, "operands must belong to contexta");

  operation                                         = l_languages_contexta_operation_store(contexta, label, LCOK_CONCATENATE);
  operation->view.operation_data.binary.operation_a = operation_a;
  operation->view.operation_data.binary.operation_b = operation_b;

  return operation;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_star(const char *label, LContexta *contexta,
                                                                         const LContextaOperation *operation) {
  LContextaOperation *result;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && operation != NULL, "star arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation->owner == contexta, "operand must belong to contexta");

  result                                     = l_languages_contexta_operation_store(contexta, label, LCOK_STAR);
  result->view.operation_data.operation_star = operation;

  return result;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_plus(const char *label, LContexta *contexta,
                                                                         const LContextaOperation *operation) {
  LContextaOperation *star;

  star = logos_languages_contexta_contexta_operation_add_star(NULL, contexta, operation);

  return logos_languages_contexta_contexta_operation_add_concatenate(label, contexta, operation, star);
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_optional(const char *label, LContexta *contexta,
                                                                             const LContextaOperation *operation) {
  LContextaOperation *empty;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && operation != NULL, "optional arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation->owner == contexta, "operand must belong to contexta");

  empty = logos_languages_contexta_contexta_operation_add_empty_string(NULL, contexta);

  return logos_languages_contexta_contexta_operation_add_unionize(label, contexta, empty, operation);
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_repeat(const char *label, LContexta *contexta,
                                                                           const LContextaOperation *operation, uint16 min, uint16 max,
                                                                           boolean has_min, boolean has_max) {
  LContextaOperation *result;
  LContextaOperation *suffix;
  LContextaOperation *copy;
  uint32              minimum;
  uint32              i;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && operation != NULL, "repeat arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation->owner == contexta, "operand must belong to contexta");
  NEXUS_PROGRAM_PRECONDITION(!has_min || !has_max || min <= max, "repeat minimum must not exceed its maximum");

  minimum = has_min ? min : 0;
  result  = NULL;

  for (i = 0; i < minimum; i++) {
    if (result == NULL) {
      /* A fresh node preserves the caller's label without mutating its operand. */
      copy             = l_languages_contexta_operation_store(contexta, label, operation->view.kind);
      copy->view       = operation->view;
      copy->view.label = label;
      result           = copy;
    } else {
      result = logos_languages_contexta_contexta_operation_add_concatenate(label, contexta, result, operation);
    }
  }

  if (!has_max || max > minimum) {
    suffix = has_max ? logos_languages_contexta_contexta_operation_add_optional(NULL, contexta, operation)
                     : logos_languages_contexta_contexta_operation_add_star(NULL, contexta, operation);

    for (i = minimum; i < (has_max ? (uint32)max : minimum + 1); i++) {
      if (result == NULL) {
        copy             = l_languages_contexta_operation_store(contexta, label, suffix->view.kind);
        copy->view       = suffix->view;
        copy->view.label = label;
        result           = copy;
      } else {
        result = logos_languages_contexta_contexta_operation_add_concatenate(label, contexta, result, suffix);
      }
    }
  }

  if (result == NULL) {
    result = logos_languages_contexta_contexta_operation_add_empty_string(label, contexta);
  }

  return result;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_reference(const char *label, LContexta *contexta, const LContextaRule *rule) {
  LContextaOperation *operation;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && rule != NULL, "reference arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(rule->owner == contexta, "rule must belong to contexta");

  operation                                = l_languages_contexta_operation_store(contexta, label, LCOK_REFERENCE);
  operation->view.operation_data.reference = rule;

  return operation;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_token(const char *label, LContexta *contexta, LLexerTokenKind kind) {
  LContextaOperation *operation;

  NEXUS_PROGRAM_PRECONDITION(kind >= L_LEXER_TOKEN_KIND_FIRST_USER, "token kind must not be reserved");

  operation                                 = l_languages_contexta_operation_store(contexta, label, LCOK_TOKEN);
  operation->view.operation_data.token_kind = kind;

  return operation;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_allowed_tokens(const char *label, LContexta *contexta,
                                                                                   const LContextaOperation *operation, const LLexerTokenKind *kinds,
                                                                                   uint32 count) {
  LContextaOperation *result;
  LLexerTokenKind    *copied;
  LContextaTokenSet   owned_set;
  uint32              i;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && operation != NULL, "scope arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation->owner == contexta, "operand must belong to contexta");
  NEXUS_PROGRAM_PRECONDITION(kinds != NULL || count == 0, "nonempty token set must not be NULL");

  for (i = 0; i < count; i++) {
    NEXUS_PROGRAM_PRECONDITION(kinds[i] >= L_LEXER_TOKEN_KIND_FIRST_USER, "default-allowed token kinds must not be reserved");
  }

  copied = NULL;

  if (count > 0) {
    copied = (LLexerTokenKind *)nexus_memory_allocate((uint_large)count * NEXUS_SIZEOF(*copied));
    nexus_memory_bytes_copy(kinds, copied, (uint_large)count * NEXUS_SIZEOF(*copied));
    owned_set.kinds = copied;
    (void)nexus_data_list_append(contexta->token_sets, &owned_set);
  }

  result                                               = l_languages_contexta_operation_store(contexta, label, LCOK_ALLOWED_TOKENS);
  result->view.operation_data.allowed_tokens.operation = operation;
  result->view.operation_data.allowed_tokens.kinds     = copied;
  result->view.operation_data.allowed_tokens.count     = count;

  return result;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_pratt(const char *label, LContexta *contexta, const LContextaPrattEntry *entries,
                                                                          uint32 entry_count) {
  LContextaOperation        *result;
  LContextaPrattEntry       *copied;
  const LContextaPrattEntry *entry;
  const LContextaPrattEntry *other;
  uint32                     i;
  uint32                     j;
  uint32                     entry_class;
  uint32                     other_class;
  boolean                    has_atom;
  boolean                    entry_operand;
  boolean                    other_operand;
  boolean                    is_list;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && entries != NULL && entry_count > 0,
                             "Pratt arguments must not be NULL and the table must be nonempty");

  has_atom = FALSE;

  for (i = 0; i < entry_count; i++) {
    entry         = &entries[i];
    is_list       = entry->kind == LCPK_ENCLOSED || entry->kind == LCPK_APPLICATION;
    entry_operand = entry->kind == LCPK_ATOM || entry->kind == LCPK_PREFIX || entry->kind == LCPK_ENCLOSED;
    entry_class   = 0;

    NEXUS_PROGRAM_PRECONDITION(entry->kind < LCPK_COUNT, "Pratt entry kind must be valid");
    NEXUS_PROGRAM_PRECONDITION(entry->token_kind >= L_LEXER_TOKEN_KIND_FIRST_USER, "Pratt token kinds must not be reserved");
    NEXUS_PROGRAM_PRECONDITION(is_list || entry->kind == LCPK_TERNARY ? entry->close_kind >= L_LEXER_TOKEN_KIND_FIRST_USER
                                                                      : entry->close_kind == LCONTEXTA_PRATT_TOKEN_NONE,
                               "only enclosures, applications and ternaries take a closing token, and they require one");
    NEXUS_PROGRAM_PRECONDITION(is_list ? entry->separator_kind == LCONTEXTA_PRATT_TOKEN_NONE || entry->separator_kind >= L_LEXER_TOKEN_KIND_FIRST_USER
                                       : entry->separator_kind == LCONTEXTA_PRATT_TOKEN_NONE,
                               "only enclosures and applications take a separator");
    NEXUS_PROGRAM_PRECONDITION(is_list || (!entry->allow_empty && !entry->allow_trailing_separator), "only lists may be empty or end in a separator");
    NEXUS_PROGRAM_PRECONDITION(entry->precedence <= LCONTEXTA_PRATT_PRECEDENCE_MAX, "Pratt precedence must not exceed the maximum");
    NEXUS_PROGRAM_PRECONDITION(!entry->allow_trailing_separator || entry->separator_kind != LCONTEXTA_PRATT_TOKEN_NONE,
                               "a trailing separator needs a separator");

    if (entry->kind == LCPK_INFIX_LEFT) {
      entry_class = 1;
    } else if (entry->kind == LCPK_INFIX_NONE) {
      entry_class = 2;
    } else if (entry->kind == LCPK_INFIX_RIGHT || entry->kind == LCPK_TERNARY) {
      entry_class = 3;
    }

    has_atom = has_atom || entry->kind == LCPK_ATOM;

    for (j = 0; j < i; j++) {
      other         = &entries[j];
      other_operand = other->kind == LCPK_ATOM || other->kind == LCPK_PREFIX || other->kind == LCPK_ENCLOSED;
      other_class   = 0;

      if (other->kind == LCPK_INFIX_LEFT) {
        other_class = 1;
      } else if (other->kind == LCPK_INFIX_NONE) {
        other_class = 2;
      } else if (other->kind == LCPK_INFIX_RIGHT || other->kind == LCPK_TERNARY) {
        other_class = 3;
      }

      NEXUS_PROGRAM_PRECONDITION(entry_operand != other_operand || entry->token_kind != other->token_kind,
                                 "two Pratt entries of one role must not share a token kind");
      NEXUS_PROGRAM_PRECONDITION(entry_class == 0 || other_class == 0 || entry->precedence != other->precedence || entry_class == other_class,
                                 "Pratt operators sharing a precedence must share one associativity");
    }
  }

  NEXUS_PROGRAM_PRECONDITION(has_atom, "a Pratt table needs at least one atom");

  /* A closing, separating or second-ternary token must stay distinguishable from every operator in operator position. */
  for (i = 0; i < entry_count; i++) {
    entry = &entries[i];

    for (j = 0; j < entry_count; j++) {
      other = &entries[j];

      if (other->kind == LCPK_ATOM || other->kind == LCPK_PREFIX || other->kind == LCPK_ENCLOSED) {
        continue;
      }

      NEXUS_PROGRAM_PRECONDITION(entry->close_kind == LCONTEXTA_PRATT_TOKEN_NONE || entry->close_kind != other->token_kind,
                                 "a closing or second token must not also be an operator");
      NEXUS_PROGRAM_PRECONDITION(entry->separator_kind == LCONTEXTA_PRATT_TOKEN_NONE || entry->separator_kind != other->token_kind,
                                 "a separator must not also be an operator");
    }
  }

  copied = (LContextaPrattEntry *)nexus_memory_allocate((uint_large)entry_count * NEXUS_SIZEOF(*copied));
  nexus_memory_bytes_copy(entries, copied, (uint_large)entry_count * NEXUS_SIZEOF(*copied));
  (void)nexus_data_list_append_pointer(contexta->pratt_tables, copied);

  result                                    = l_languages_contexta_operation_store(contexta, label, LCOK_PRATT);
  result->view.operation_data.pratt.entries = copied;
  result->view.operation_data.pratt.count   = entry_count;

  return result;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_choice(const char *label, LContexta *contexta,
                                                                           const LContextaOperation *operation_a,
                                                                           const LContextaOperation *operation_b, LContextaChoiceMode mode) {
  LContextaOperation *operation;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && operation_a != NULL && operation_b != NULL, "choice arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation_a->owner == contexta && operation_b->owner == contexta, "operands must belong to contexta");
  NEXUS_PROGRAM_PRECONDITION((uint32)mode < (uint32)LCCM_COUNT, "the choice mode must exist");

  operation                                         = l_languages_contexta_operation_store(contexta, label, LCOK_CHOICE);
  operation->view.operation_data.choice.operation_a = operation_a;
  operation->view.operation_data.choice.operation_b = operation_b;
  operation->view.operation_data.choice.mode        = mode;

  return operation;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_exclude(const char *label, LContexta *contexta,
                                                                            const LContextaOperation *operation, const LContextaRule *const *rules,
                                                                            uint32 rule_count) {
  LContextaOperation   *result;
  const LContextaRule **copied;
  uint32                i;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && operation != NULL && (rules != NULL || rule_count == 0), "exclude arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation->owner == contexta, "operand must belong to contexta");

  for (i = 0; i < rule_count; i++) {
    NEXUS_PROGRAM_PRECONDITION(rules[i] != NULL && rules[i]->owner == contexta, "excluded rules must belong to contexta");
  }

  copied = NULL;

  if (rule_count > 0) {
    copied = (const LContextaRule **)nexus_memory_allocate((uint_large)rule_count * NEXUS_SIZEOF(*copied));
    nexus_memory_bytes_copy((const void *)rules, (void *)copied, (uint_large)rule_count * NEXUS_SIZEOF(*copied));
    (void)nexus_data_list_append_pointer(contexta->pratt_tables, (void *)copied);
  }

  result                                        = l_languages_contexta_operation_store(contexta, label, LCOK_EXCLUDE);
  result->view.operation_data.exclude.operation = operation;
  result->view.operation_data.exclude.rules     = copied;
  result->view.operation_data.exclude.count     = rule_count;

  return result;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_operators(const char *label, LContexta *contexta, const LContextaOperation *atom,
                                                                              const LContextaOperatorEntry *entries, uint32 entry_count) {
  LContextaOperation     *result;
  LContextaOperatorEntry *copied;
  uint32                  i;
  uint32                  j;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && atom != NULL && entries != NULL && entry_count > 0,
                             "operator arguments must not be NULL and the table nonempty");
  NEXUS_PROGRAM_PRECONDITION(atom->owner == contexta, "the atom must belong to contexta");

  for (i = 0; i < entry_count; i++) {
    NEXUS_PROGRAM_PRECONDITION((uint32)entries[i].kind < (uint32)LCOP_COUNT, "operator kind must exist");
    NEXUS_PROGRAM_PRECONDITION(entries[i].token_kind >= L_LEXER_TOKEN_KIND_FIRST_USER, "operator token kinds must not be reserved");

    for (j = 0; j < i; j++) {
      NEXUS_PROGRAM_PRECONDITION(entries[j].token_kind != entries[i].token_kind ||
                                     (entries[j].kind == LCOP_PREFIX) != (entries[i].kind == LCOP_PREFIX),
                                 "two operators of one position must not share a token");
    }
  }

  copied = (LContextaOperatorEntry *)nexus_memory_allocate((uint_large)entry_count * NEXUS_SIZEOF(*copied));
  nexus_memory_bytes_copy(entries, copied, (uint_large)entry_count * NEXUS_SIZEOF(*copied));
  (void)nexus_data_list_append_pointer(contexta->pratt_tables, copied);

  result                                        = l_languages_contexta_operation_store(contexta, label, LCOK_OPERATORS);
  result->view.operation_data.operators.atom    = atom;
  result->view.operation_data.operators.entries = copied;
  result->view.operation_data.operators.count   = entry_count;

  return result;
}

LContextaOperation *logos_languages_contexta_contexta_operation_add_embed(const char *label, LContexta *contexta, const LContextaEmbed *embed) {
  LContextaOperation *result;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && embed != NULL && embed->guest != NULL, "embed arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION((uint32)embed->mode < (uint32)LCEM_COUNT && (uint32)embed->close_boundary < (uint32)LCEB_COUNT,
                             "embed enumerations must be valid");
  NEXUS_PROGRAM_PRECONDITION(embed->mode == LCEM_REGION
                                 ? embed->open_token >= L_LEXER_TOKEN_KIND_FIRST_USER && embed->close_token >= L_LEXER_TOKEN_KIND_FIRST_USER
                                 : embed->source_token >= L_LEXER_TOKEN_KIND_FIRST_USER,
                             "embed tokens must not be reserved");

  result                            = l_languages_contexta_operation_store(contexta, label, LCOK_EMBED);
  result->view.operation_data.embed = *embed;

  return result;
}

/* ---------------------------------------------------------------------------- */
/* REGULA INTERCHANGE                                                           */
/* ---------------------------------------------------------------------------- */

NexusError *logos_languages_contexta_contexta_operation_add_regexp(const char *label, LContexta *contexta, const char *regexp,
                                                                   LContextaOperation **out_operation) {
  LRegula                *staging;
  LRegulaOperation       *root;
  const LRegulaOperation *source;
  LContextaOperation    **copies;
  LContextaOperation     *operation;
  NexusError             *error;
  uint_large              count;
  uint_large              i;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && regexp != NULL && out_operation != NULL, "regexp arguments must not be NULL");

  *out_operation                 = NULL;
  staging                        = logos_languages_regula_regula_create(NULL, 0);
  staging->internal_constructing = TRUE;
  error                          = logos_languages_regula_regula_operation_add_regexp(NULL, staging, regexp, &root);

  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    logos_languages_regula_regula_destroy(staging);

    return error;
  }

  count  = nexus_data_slabbed_list_length_get(staging->operations);
  copies = (LContextaOperation **)nexus_memory_allocate(count * NEXUS_SIZEOF(*copies));

  /* Regula's transactional importer emits children before parents. */
  for (i = 0; i < count; i++) {
    source    = (const LRegulaOperation *)nexus_data_slabbed_list_at(staging->operations, i);
    operation = NULL;

    switch (source->kind) {
    case LROK_EMPTY_LANGUAGE:
      operation = logos_languages_contexta_contexta_operation_add_empty_language(NULL, contexta);
      break;
    case LROK_EMPTY_STRING:
      operation = logos_languages_contexta_contexta_operation_add_empty_string(NULL, contexta);
      break;
    case LROK_SYMBOL:
      operation = logos_languages_contexta_contexta_operation_add_symbol_mask(NULL, contexta, source->operation_data.symbol_mask, FALSE);
      break;
    case LROK_UNIONIZE:
      operation =
          logos_languages_contexta_contexta_operation_add_unionize(NULL, contexta, copies[source->operation_data.unionize.operation_a->storage_index],
                                                                   copies[source->operation_data.unionize.operation_b->storage_index]);
      break;
    case LROK_CONCATENATE:
      operation = logos_languages_contexta_contexta_operation_add_concatenate(NULL, contexta,
                                                                              copies[source->operation_data.concatenate.operation_a->storage_index],
                                                                              copies[source->operation_data.concatenate.operation_b->storage_index]);
      break;
    case LROK_STAR:
      operation = logos_languages_contexta_contexta_operation_add_star(NULL, contexta, copies[source->operation_data.operation_star->storage_index]);
      break;
    case LROK_COUNT:
    default:
      NEXUS_PROGRAM_PANIC("invalid staged Regula operation");
    }

    copies[i] = operation;
  }

  operation             = copies[root->storage_index];
  operation->view.label = label;
  *out_operation        = operation;
  nexus_memory_deallocate((void *)copies);
  logos_languages_regula_regula_destroy(staging);

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

typedef struct LContextaConvertFrame {
  const LContextaOperation *operation;
  uint8                     phase;
} LContextaConvertFrame;

NexusError *logos_languages_contexta_contexta_to_regula(const LContexta *contexta, LRegula **out_regula) {
  LContextaConvertFrame    *frames;
  LContextaConvertFrame    *frame;
  const LContextaOperation *operation;
  const LContextaOperation *child;
  LRegulaOperation        **copies;
  LRegula                  *regula;
  byte                     *states;
  uint_large                count;
  uint_large                depth;
  NexusError               *error;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && out_regula != NULL, "conversion arguments must not be NULL");

  *out_regula = NULL;
  error       = logos_languages_contexta_contexta_validate(contexta);

  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return error;
  }

  NEXUS_PROGRAM_INVARIANT(contexta->start_rule != NULL && contexta->start_rule->body != NULL, "validated grammar must have a defined start");

  count  = nexus_data_slabbed_list_length_get(contexta->operations);
  frames = (LContextaConvertFrame *)nexus_memory_allocate(count * NEXUS_SIZEOF(*frames));
  copies = (LRegulaOperation **)nexus_memory_allocate(count * NEXUS_SIZEOF(*copies));
  states = (byte *)nexus_memory_allocate(count);
  nexus_memory_bytes_clear(states, count);
  regula                        = logos_languages_regula_regula_create(contexta->label, contexta->version);
  regula->internal_constructing = TRUE;
  depth                         = 1;
  frames[0].operation           = contexta->start_rule->body;
  frames[0].phase               = 0;

  /* Three-color DFS substitutes references once and rejects only reachable cycles. */
  while (depth > 0) {
    frame     = &frames[depth - 1];
    operation = frame->operation;
    child     = NULL;

    if (states[operation->index] == 2) {
      depth--;
      continue;
    }

    states[operation->index] = 1;

    switch (operation->view.kind) {
    case LCOK_UNIONIZE:
    case LCOK_CONCATENATE:
      NEXUS_PROGRAM_INVARIANT(operation->view.operation_data.binary.operation_a != NULL && operation->view.operation_data.binary.operation_b != NULL,
                              "binary operands must exist");

      if (frame->phase == 0) {
        child = operation->view.operation_data.binary.operation_a;
      } else if (frame->phase == 1) {
        child = operation->view.operation_data.binary.operation_b;
      }
      break;
    case LCOK_STAR:
      NEXUS_PROGRAM_INVARIANT(operation->view.operation_data.operation_star != NULL, "star operand must exist");

      if (frame->phase == 0) {
        child = operation->view.operation_data.operation_star;
      }
      break;
    case LCOK_REFERENCE:
      NEXUS_PROGRAM_INVARIANT(operation->view.operation_data.reference != NULL && operation->view.operation_data.reference->body != NULL,
                              "validated reference must have a defined rule");

      if (frame->phase == 0) {
        child = operation->view.operation_data.reference->body;
      }
      break;
    case LCOK_EMPTY_LANGUAGE:
    case LCOK_EMPTY_STRING:
    case LCOK_SYMBOL:
      break;
    case LCOK_TOKEN:
    case LCOK_ALLOWED_TOKENS:
    case LCOK_PRATT:
    case LCOK_CHOICE:
    case LCOK_EXCLUDE:
    case LCOK_OPERATORS:
    case LCOK_EMBED:
      error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_CONTEXTA_NOT_BYTE_LANGUAGE, "token operations cannot be converted to a byte Regula");
      break;
    case LCOK_COUNT:
    default:
      NEXUS_PROGRAM_PANIC("invalid Contexta operation kind");
    }

    if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
      break;
    }

    if (child != NULL) {
      frame->phase++;

      if (states[child->index] == 1) {
        error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_CONTEXTA_RECURSIVE, "reachable recursion cannot be substituted into Regula");
        break;
      }

      frames[depth].operation = child;
      frames[depth].phase     = 0;
      depth++;
      continue;
    }

    switch (operation->view.kind) {
    case LCOK_EMPTY_LANGUAGE:
      copies[operation->index] = logos_languages_regula_regula_operation_add_empty_language(operation->view.label, regula);
      break;
    case LCOK_EMPTY_STRING:
      copies[operation->index] = logos_languages_regula_regula_operation_add_empty_string(operation->view.label, regula);
      break;
    case LCOK_SYMBOL:
      copies[operation->index] =
          logos_languages_regula_regula_operation_add_symbol_mask(operation->view.label, regula, operation->view.operation_data.symbol_mask, FALSE);
      break;
    case LCOK_UNIONIZE:
      copies[operation->index] = logos_languages_regula_regula_operation_add_unionize(
          operation->view.label, regula, copies[operation->view.operation_data.binary.operation_a->index],
          copies[operation->view.operation_data.binary.operation_b->index]);
      break;
    case LCOK_CONCATENATE:
      copies[operation->index] = logos_languages_regula_regula_operation_add_concatenate(
          operation->view.label, regula, copies[operation->view.operation_data.binary.operation_a->index],
          copies[operation->view.operation_data.binary.operation_b->index]);
      break;
    case LCOK_STAR:
      copies[operation->index] = logos_languages_regula_regula_operation_add_star(operation->view.label, regula,
                                                                                  copies[operation->view.operation_data.operation_star->index]);
      break;
    case LCOK_REFERENCE:
      copies[operation->index] = copies[operation->view.operation_data.reference->body->index];
      break;
    case LCOK_TOKEN:
    case LCOK_ALLOWED_TOKENS:
    case LCOK_PRATT:
    case LCOK_CHOICE:
    case LCOK_EXCLUDE:
    case LCOK_OPERATORS:
    case LCOK_EMBED:
    case LCOK_COUNT:
    default:
      NEXUS_PROGRAM_PANIC("invalid Contexta operation kind");
    }

    states[operation->index] = 2;
    depth--;
  }

  if (error->code == NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    regula->root_rule                  = copies[contexta->start_rule->body->index];
    regula->root_rule->assembly_active = TRUE;
    regula->internal_constructing      = FALSE;
    *out_regula                        = regula;
  } else {
    logos_languages_regula_regula_destroy(regula);
  }

  nexus_memory_deallocate(states);
  nexus_memory_deallocate((void *)copies);
  nexus_memory_deallocate(frames);

  return error;
}

NexusError *logos_languages_contexta_contexta_regexp_write(const LContexta *contexta, char **out_regexp) {
  LRegula    *regula;
  NexusError *error;

  NEXUS_PROGRAM_PRECONDITION(contexta != NULL && out_regexp != NULL, "regexp write arguments must not be NULL");

  *out_regexp = NULL;
  error       = logos_languages_contexta_contexta_to_regula(contexta, &regula);

  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return error;
  }

  NEXUS_PROGRAM_INVARIANT(regula != NULL, "successful conversion must produce a Regula");

  error = logos_languages_regula_regula_regexp_write(regula, out_regexp);
  logos_languages_regula_regula_destroy(regula);

  return error;
}
