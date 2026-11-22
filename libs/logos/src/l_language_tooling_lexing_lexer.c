#include <nexus.h>
#include <echo.h>

#include "../logos.h"
#include "./l_internal.h"

/* Ranks and tags are 16 bits and L_ACCEPT_TAG_NONE is reserved. */
#define L_LEXER_RULES_PER_CONTEXT_MAX ((uint_large)UINT16_MAX_VAL - 1)

LLexer *logos_language_tooling_lexing_lexer_create(LogosRuntime *runtime) {
  LLexer *lexer;

  NEXUS_PROGRAM_PRECONDITION(runtime != NULL, "runtime must not be NULL");

  lexer                    = (LLexer *)nexus_memory_allocate(NEXUS_SIZEOF(*lexer));
  lexer->runtime           = runtime;
  lexer->contexts          = nexus_data_list_create(NEXUS_SIZEOF(LLexerContextData), 8);
  lexer->root_context      = 0;
  lexer->has_root_context  = FALSE;
  lexer->token_width_limit = LLEXER_TOKEN_WIDTH_LIMIT_DEFAULT;
  lexer->finalized         = FALSE;
  lexer->compiled          = NULL;
  lexer->compiled_count    = 0;

  return lexer;
}

void logos_language_tooling_lexing_lexer_destroy(LLexer *lexer) {
  uint_large i;
  uint_large j;

  NEXUS_PROGRAM_PRECONDITION(lexer != NULL, "lexer must not be NULL");

  for (i = 0; i < lexer->compiled_count; i++) {
    for (j = 0; j < lexer->compiled[i].rule_count; j++) {
      logos_automatons_nfa_destroy(lexer->compiled[i].nfas[j]);
    }

    nexus_memory_deallocate((void *)lexer->compiled[i].nfas);
    nexus_memory_deallocate(lexer->compiled[i].ranked_rules);
    nexus_memory_deallocate(lexer->compiled[i].rows);
    nexus_memory_deallocate(lexer->compiled[i].accept_tags);
    logos_automatons_dfa_destroy(lexer->compiled[i].dfa);
  }

  for (i = 0; i < nexus_data_list_length_get(lexer->contexts); i++) {
    nexus_data_list_destroy(((LLexerContextData *)nexus_data_list_at(lexer->contexts, i))->rules);
  }

  /* Only a finalized lexer has compiled contexts, and Nexus refuses to free NULL. */
  if (lexer->compiled != NULL) {
    nexus_memory_deallocate(lexer->compiled);
  }

  nexus_data_list_destroy(lexer->contexts);
  nexus_memory_deallocate(lexer);
}

LLexerContext logos_language_tooling_lexing_lexer_context_create(LLexer *lexer, const char *label, boolean is_root_context) {
  LLexerContextData context;
  LLexerContext     identifier;

  NEXUS_PROGRAM_PRECONDITION(lexer != NULL, "lexer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(!lexer->finalized, "a finalized lexer cannot gain contexts");
  NEXUS_PROGRAM_PRECONDITION(!is_root_context || !lexer->has_root_context, "a lexer has exactly one root context");

  identifier = (LLexerContext)nexus_data_list_length_get(lexer->contexts);

  context.label   = label;
  context.is_root = is_root_context;
  context.rules   = nexus_data_list_create(NEXUS_SIZEOF(LLexerTokenRule), 8);
  (void)nexus_data_list_append(lexer->contexts, &context);

  if (is_root_context) {
    lexer->root_context     = identifier;
    lexer->has_root_context = TRUE;
  }

  return identifier;
}

void logos_language_tooling_lexing_lexer_context_token_rule_add(LLexer *lexer, LLexerContext context, LLexerTokenRule rule) {
  LLexerContextData *data;

  NEXUS_PROGRAM_PRECONDITION(lexer != NULL, "lexer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(!lexer->finalized, "a finalized lexer cannot gain rules");
  NEXUS_PROGRAM_PRECONDITION(context < nexus_data_list_length_get(lexer->contexts), "the context must exist");
  NEXUS_PROGRAM_PRECONDITION(rule.token_language != NULL, "a rule needs a token language");
  NEXUS_PROGRAM_PRECONDITION(rule.token_kind >= L_LEXER_TOKEN_KIND_FIRST_USER, "a rule's token kind must not be reserved");
  NEXUS_PROGRAM_PRECONDITION((uint32)rule.action_kind < (uint32)LLTCAK_COUNT, "the context action must exist");

  switch (rule.action_kind) {
  case LLTCAK_NONE:
    break;
  case LLTCAK_PUSH:
    NEXUS_PROGRAM_PRECONDITION(rule.action_data.push_context < nexus_data_list_length_get(lexer->contexts), "the pushed context must exist");
    break;
  case LLTCAK_POP:
    NEXUS_PROGRAM_PRECONDITION(rule.action_data.pop_amount >= 1, "a pop must remove at least one context");
    break;
  case LLTCAK_SET:
    NEXUS_PROGRAM_PRECONDITION(rule.action_data.set_context < nexus_data_list_length_get(lexer->contexts), "the set context must exist");
    break;
  case LLTCAK_REPLACE:
    NEXUS_PROGRAM_PRECONDITION(rule.action_data.replace_context < nexus_data_list_length_get(lexer->contexts), "the replacing context must exist");
    break;
  case LLTCAK_COUNT:
  default:
    NEXUS_PROGRAM_PANIC("invalid context action kind");
  }

  data = (LLexerContextData *)nexus_data_list_at(lexer->contexts, context);

  NEXUS_PROGRAM_PRECONDITION(nexus_data_list_length_get(data->rules) < L_LEXER_RULES_PER_CONTEXT_MAX, "a context holds at most 65534 rules");

  (void)nexus_data_list_append(data->rules, &rule);
}

void logos_language_tooling_lexing_lexer_token_width_limit_set(LLexer *lexer, uint32 limit) {
  NEXUS_PROGRAM_PRECONDITION(lexer != NULL, "lexer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(!lexer->finalized, "a finalized lexer cannot change its limit");
  NEXUS_PROGRAM_PRECONDITION(limit >= 1, "the token width limit must be at least 1");

  lexer->token_width_limit = limit;
}

/* The rules' order of precedence: higher priority first, then earlier registration. A stable insertion sort of indices, since the rule count is
   small and the sort runs once per finalize. */
static void l_lexer_rank_order(const LLexerTokenRule *rules, uint_large rule_count, uint16 *order) {
  uint_large i;

  for (i = 0; i < rule_count; i++) {
    uint_large position;

    position = i;

    while (position > 0 && rules[order[position - 1]].match_priority < rules[i].match_priority) {
      order[position] = order[position - 1];
      position--;
    }

    order[position] = (uint16)i;
  }
}

/*
Summary: turns the merged NFA of a rule set into the renumbered DFA tables of a compiled context.
Description: consumes merged. Returns FALSE when the automaton needs more than 65535 states.
*/
static boolean l_lexer_tables_build(EchoContext *echo, LNFA *merged, LLexerCompiledContext *compiled) {
  LDFA      *determinized;
  uint_large state;
  uint_large column;
  byte      *state_kind;
  uint16    *old_to_new;
  uint_large next_state;
  byte       kind;

  determinized = merged != NULL ? logos_automatons_dfa_from_nfa(merged, echo) : NULL;

  if (merged != NULL) {
    logos_automatons_nfa_destroy(merged);
  }

  if (determinized == NULL) {
    return FALSE;
  }

  compiled->dfa = logos_automatons_dfa_minimize(determinized, echo);
  logos_automatons_dfa_destroy(determinized);
  logos_automatons_dfa_view_get(compiled->dfa, &compiled->view);

  /* Per state, what the matching loop must do on entering it. A state is FINAL when it accepts and every byte leads to the dead state. */
  state_kind = (byte *)nexus_memory_allocate(compiled->view.state_count);

  for (state = 0; state < compiled->view.state_count; state++) {
    if (compiled->view.accept_tags[state] == L_ACCEPT_TAG_NONE) {
      state_kind[state] = state == compiled->view.dead_state ? L_LEXER_STATE_DEAD : L_LEXER_STATE_LIVE;

      continue;
    }

    state_kind[state] = L_LEXER_STATE_FINAL;

    for (column = 0; column < compiled->view.class_count; column++) {
      if (compiled->view.transitions[(state * compiled->view.class_count) + column] != compiled->view.dead_state) {
        state_kind[state] = L_LEXER_STATE_ACCEPT;

        break;
      }
    }
  }

  compiled->class_shift = 0;

  while (((uint_large)1 << compiled->class_shift) < compiled->view.class_count) {
    compiled->class_shift++;
  }

  /* Renumber: live states first, then accepting, then final, then dead, so the matching loop tests a range instead of loading a kind per byte. */
  old_to_new = (uint16 *)nexus_memory_allocate(compiled->view.state_count * NEXUS_SIZEOF(uint16));
  next_state = 0;

  for (kind = L_LEXER_STATE_LIVE; kind <= L_LEXER_STATE_DEAD; kind++) {
    for (state = 0; state < compiled->view.state_count; state++) {
      if (state_kind[state] == kind) {
        old_to_new[state] = (uint16)next_state;
        next_state++;
      }
    }

    if (kind == L_LEXER_STATE_LIVE) {
      compiled->live_limit = (uint32)(next_state << compiled->class_shift);
    } else if (kind == L_LEXER_STATE_ACCEPT) {
      compiled->accept_limit = (uint32)(next_state << compiled->class_shift);
    } else if (kind == L_LEXER_STATE_FINAL) {
      compiled->final_limit = (uint32)(next_state << compiled->class_shift);
    }
  }

  compiled->rows = (uint32 *)nexus_memory_allocate(((uint_large)compiled->view.state_count << compiled->class_shift) * NEXUS_SIZEOF(uint32));
  nexus_memory_bytes_clear(compiled->rows, ((uint_large)compiled->view.state_count << compiled->class_shift) * NEXUS_SIZEOF(uint32));
  compiled->accept_tags = (uint16 *)nexus_memory_allocate(compiled->view.state_count * NEXUS_SIZEOF(uint16));

  for (state = 0; state < compiled->view.state_count; state++) {
    compiled->accept_tags[old_to_new[state]] = compiled->view.accept_tags[state];

    for (column = 0; column < compiled->view.class_count; column++) {
      compiled->rows[((uint_large)old_to_new[state] << compiled->class_shift) + column] =
          (uint32)((uint_large)old_to_new[compiled->view.transitions[(state * compiled->view.class_count) + column]] << compiled->class_shift);
    }
  }

  compiled->initial_row = (uint32)((uint_large)old_to_new[0] << compiled->class_shift);
  nexus_memory_deallocate(old_to_new);
  nexus_memory_deallocate(state_kind);

  return TRUE;
}

/* Compiles one context. Returns NULL and sets *out_error on failure; on success fills compiled, which then owns everything it points to. */
static boolean l_lexer_context_compile(const LLexer *lexer, LLexerContext context_id, const LLexerContextData *context,
                                       LLexerCompiledContext *compiled, NexusError **out_error) {
  EchoContext           *echo;
  const LLexerTokenRule *rules;
  uint_large             rule_count;
  uint16                *order;
  uint16                *tags;
  LNFA                 **parts;
  LNFA                  *merged;
  uint_large             i;
  uint_large             state;
  boolean               *tag_present;

  echo       = logos_runtime_echo_context_get(lexer->runtime);
  rule_count = nexus_data_list_length_get(context->rules);
  rules      = (const LLexerTokenRule *)nexus_data_list_data_get_const(context->rules);

  /* A rule that accepts the empty string could be "matched" without consuming a byte, so the lexer would never advance. */
  parts = (LNFA **)nexus_memory_allocate((rule_count > 0 ? rule_count : 1) * NEXUS_SIZEOF(LNFA *));

  for (i = 0; i < rule_count; i++) {
    parts[i] = logos_automatons_nfa_from_regula(rules[i].token_language, echo);

    if (logos_automatons_nfa_match(parts[i], NULL, 0)) {
      uint_large destroyed;

      *out_error = NEXUS_PROGRAM_ERROR_FAILURE(
          GLOBAL_LOGOS_LEXER_RULE_EMPTY_MATCH,
          "Lexer context \"%s\" (%u): rule %llu (\"%s\") accepts the empty string, so it could match without consuming input.",
          context->label != NULL ? context->label : "(unlabelled)", (unsigned int)context_id, (unsigned long long)i,
          rules[i].label != NULL ? rules[i].label : "(unlabelled)");

      for (destroyed = 0; destroyed <= i; destroyed++) {
        logos_automatons_nfa_destroy(parts[destroyed]);
      }

      nexus_memory_deallocate((void *)parts);

      return FALSE;
    }
  }

  /* The rank of a rule is its place in the order of precedence; the automaton keeps the smallest rank of the rules that accept at a state. */
  order = (uint16 *)nexus_memory_allocate((rule_count > 0 ? rule_count : 1) * NEXUS_SIZEOF(uint16));
  tags  = (uint16 *)nexus_memory_allocate((rule_count > 0 ? rule_count : 1) * NEXUS_SIZEOF(uint16));
  l_lexer_rank_order(rules, rule_count, order);

  compiled->ranked_rules = (LLexerTokenRule *)nexus_memory_allocate((rule_count > 0 ? rule_count : 1) * NEXUS_SIZEOF(LLexerTokenRule));
  compiled->rule_count   = rule_count;

  for (i = 0; i < rule_count; i++) {
    tags[order[i]]            = (uint16)i;
    compiled->ranked_rules[i] = rules[order[i]];
  }

  /* A context without rules has no parts to unite: its automaton is the initial state alone, which accepts nothing. */
  if (rule_count == 0) {
    LRegula *empty;
    LNFA    *empty_nfa;

    empty     = logos_languages_regula_regula_create("empty_context", NEXUS_VERSION_PACK(0, 0, 1, 0));
    empty_nfa = logos_automatons_nfa_from_regula(empty, echo);
    merged    = empty_nfa;
    logos_languages_regula_regula_destroy(empty);
  } else {
    merged = logos_automatons_nfa_union((const LNFA *const *)parts, tags, rule_count, echo);
  }

  nexus_memory_deallocate(tags);

  if (!l_lexer_tables_build(echo, merged, compiled)) {
    *out_error = NEXUS_PROGRAM_ERROR_FAILURE(
        GLOBAL_LOGOS_LEXER_STATE_LIMIT, "Lexer context \"%s\" (%u): its %llu rules need more than 65535 automaton states.",
        context->label != NULL ? context->label : "(unlabelled)", (unsigned int)context_id, (unsigned long long)rule_count);

    for (i = 0; i < rule_count; i++) {
      logos_automatons_nfa_destroy(parts[i]);
    }

    nexus_memory_deallocate((void *)parts);
    nexus_memory_deallocate(order);
    nexus_memory_deallocate(compiled->ranked_rules);

    return FALSE;
  }

  /* The rule automata stay: a later subset of this context's rules is united from them. */
  compiled->nfas = parts;

  /* A rule whose tag no state carries can never win: report it, because it is almost certainly a mistake. */
  tag_present = (boolean *)nexus_memory_allocate((rule_count > 0 ? rule_count : 1) * NEXUS_SIZEOF(boolean));
  nexus_memory_bytes_clear(tag_present, (rule_count > 0 ? rule_count : 1) * NEXUS_SIZEOF(boolean));

  for (state = 0; state < compiled->view.state_count; state++) {
    if (compiled->view.accept_tags[state] != L_ACCEPT_TAG_NONE) {
      tag_present[compiled->view.accept_tags[state]] = TRUE;
    }
  }

  for (i = 0; i < rule_count; i++) {
    if (!tag_present[i]) {
      (void)echo_warning(echo, "Lexer context \"%s\" (%u): rule \"%s\" can never win: every string it accepts is taken by a rule that outranks it.",
                         context->label != NULL ? context->label : "(unlabelled)", (unsigned int)context_id,
                         compiled->ranked_rules[i].label != NULL ? compiled->ranked_rules[i].label : "(unlabelled)");
    }
  }

  (void)echo_debug(echo, "Lexer context \"%s\" (%u): %llu rules compiled to a minimal DFA of %u states over %u byte classes.",
                   context->label != NULL ? context->label : "(unlabelled)", (unsigned int)context_id, (unsigned long long)rule_count,
                   (unsigned int)compiled->view.state_count, (unsigned int)compiled->view.class_count);

  nexus_memory_deallocate(tag_present);
  nexus_memory_deallocate(order);

  return TRUE;
}

NexusError *logos_language_tooling_lexing_lexer_finalize(LLexer *lexer) {
  LLexerCompiledContext *compiled;
  uint_large             context_count;
  uint_large             i;

  NEXUS_PROGRAM_PRECONDITION(lexer != NULL, "lexer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(!lexer->finalized, "the lexer is already finalized");
  NEXUS_PROGRAM_PRECONDITION(lexer->has_root_context, "a lexer needs a root context");

  context_count = nexus_data_list_length_get(lexer->contexts);
  compiled      = (LLexerCompiledContext *)nexus_memory_allocate(context_count * NEXUS_SIZEOF(*compiled));

  for (i = 0; i < context_count; i++) {
    NexusError *error;

    error = NULL;

    if (!l_lexer_context_compile(lexer, (LLexerContext)i, (const LLexerContextData *)nexus_data_list_at_const(lexer->contexts, i), &compiled[i],
                                 &error)) {
      uint_large done;
      uint_large j;

      /* Nothing is committed: the lexer stays editable, so the caller can repair the rule and finalize again. */
      for (done = 0; done < i; done++) {
        for (j = 0; j < compiled[done].rule_count; j++) {
          logos_automatons_nfa_destroy(compiled[done].nfas[j]);
        }

        nexus_memory_deallocate((void *)compiled[done].nfas);
        nexus_memory_deallocate(compiled[done].ranked_rules);
        nexus_memory_deallocate(compiled[done].rows);
        nexus_memory_deallocate(compiled[done].accept_tags);
        logos_automatons_dfa_destroy(compiled[done].dfa);
      }

      nexus_memory_deallocate(compiled);

      return error;
    }
  }

  lexer->compiled       = compiled;
  lexer->compiled_count = context_count;
  lexer->finalized      = TRUE;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

/* ---------------------------------------------------------------------------- */
/* STATELESS MATCHING                                                            */
/* ---------------------------------------------------------------------------- */

struct LLexerSubset {
  LLexerCompiledContext compiled;
  LLexerContext         context;
};

void logos_language_tooling_lexing_lexer_stack_initialize(const LLexer *lexer, LLexerStack *stack) {
  NEXUS_PROGRAM_PRECONDITION(lexer != NULL && stack != NULL, "lexer and stack must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(lexer->finalized, "a stack needs a finalized lexer");

  stack->depth       = 1;
  stack->contexts[0] = lexer->root_context;
}

LLexerError logos_language_tooling_lexing_lexer_stack_apply(const LLexer *lexer, LLexerStack *stack, const LLexerTokenRule *rule) {
  NEXUS_PROGRAM_PRECONDITION(lexer != NULL && stack != NULL && rule != NULL, "lexer, stack and rule must not be NULL");

  switch (rule->action_kind) {
  case LLTCAK_NONE:
    return LLE_NONE;
  case LLTCAK_PUSH:
    if (stack->depth >= LLEXER_CONTEXT_DEPTH_MAX) {
      return LLE_CONTEXT_OVERFLOW;
    }

    stack->contexts[stack->depth] = rule->action_data.push_context;
    stack->depth++;

    return LLE_NONE;
  case LLTCAK_POP:
    if (rule->action_data.pop_amount >= stack->depth) {
      return LLE_CONTEXT_UNDERFLOW;
    }

    stack->depth -= rule->action_data.pop_amount;

    return LLE_NONE;
  case LLTCAK_SET:
    stack->depth = 1;

    if (rule->action_data.set_context != lexer->root_context) {
      stack->contexts[1] = rule->action_data.set_context;
      stack->depth       = 2;
    }

    return LLE_NONE;
  case LLTCAK_REPLACE:
    if (stack->depth < 2) {
      return LLE_CONTEXT_UNDERFLOW;
    }

    stack->contexts[stack->depth - 1] = rule->action_data.replace_context;

    return LLE_NONE;
  case LLTCAK_COUNT:
  default:
    NEXUS_PROGRAM_PANIC("invalid context action kind");
  }

  return LLE_NONE;
}

LLexerSubset *logos_language_tooling_lexing_lexer_subset_create(const LLexer *lexer, LLexerContext context, const LLexerTokenKind *kinds,
                                                                uint32 count) {
  LLexerSubset            *subset;
  const LLexerContextData *data;
  const LLexerTokenRule   *rules;
  LLexerTokenRule         *selected;
  LNFA                   **parts;
  uint16                  *order;
  uint16                  *tags;
  LNFA                    *merged;
  EchoContext             *echo;
  uint_large               rule_count;
  uint_large               chosen;
  uint_large               i;
  uint32                   k;

  NEXUS_PROGRAM_PRECONDITION(lexer != NULL && lexer->finalized, "a subset needs a finalized lexer");
  NEXUS_PROGRAM_PRECONDITION(context < lexer->compiled_count, "the context must exist");
  NEXUS_PROGRAM_PRECONDITION(kinds != NULL || count == 0, "a nonempty kind set must not be NULL");

  echo       = logos_runtime_echo_context_get(lexer->runtime);
  data       = (const LLexerContextData *)nexus_data_list_at_const(lexer->contexts, context);
  rule_count = nexus_data_list_length_get(data->rules);
  rules      = (const LLexerTokenRule *)nexus_data_list_data_get_const(data->rules);
  selected   = (LLexerTokenRule *)nexus_memory_allocate((rule_count > 0 ? rule_count : 1) * NEXUS_SIZEOF(*selected));
  parts      = (LNFA **)nexus_memory_allocate((rule_count > 0 ? rule_count : 1) * NEXUS_SIZEOF(*parts));
  chosen     = 0;

  /* Registration order is kept among the chosen rules, so ties still go to the earlier one. */
  for (i = 0; i < rule_count; i++) {
    for (k = 0; k < count; k++) {
      if (kinds[k] == rules[i].token_kind) {
        selected[chosen] = rules[i];
        parts[chosen]    = lexer->compiled[context].nfas[i];
        chosen++;

        break;
      }
    }
  }

  subset                        = (LLexerSubset *)nexus_memory_allocate(NEXUS_SIZEOF(*subset));
  subset->context               = context;
  subset->compiled.rule_count   = chosen;
  subset->compiled.nfas         = NULL;
  subset->compiled.ranked_rules = (LLexerTokenRule *)nexus_memory_allocate((chosen > 0 ? chosen : 1) * NEXUS_SIZEOF(LLexerTokenRule));
  order                         = (uint16 *)nexus_memory_allocate((chosen > 0 ? chosen : 1) * NEXUS_SIZEOF(uint16));
  tags                          = (uint16 *)nexus_memory_allocate((chosen > 0 ? chosen : 1) * NEXUS_SIZEOF(uint16));
  l_lexer_rank_order(selected, chosen, order);

  for (i = 0; i < chosen; i++) {
    tags[order[i]]                   = (uint16)i;
    subset->compiled.ranked_rules[i] = selected[order[i]];
  }

  if (chosen == 0) {
    LRegula *empty;

    empty  = logos_languages_regula_regula_create("empty_subset", NEXUS_VERSION_PACK(0, 0, 1, 0));
    merged = logos_automatons_nfa_from_regula(empty, echo);
    logos_languages_regula_regula_destroy(empty);
  } else {
    /* The union copies the parts, which stay owned by the lexer. */
    merged = logos_automatons_nfa_union((const LNFA *const *)parts, tags, chosen, echo);
  }

  nexus_memory_deallocate(order);
  nexus_memory_deallocate(tags);
  nexus_memory_deallocate((void *)parts);
  nexus_memory_deallocate(selected);

  if (merged == NULL || !l_lexer_tables_build(echo, merged, &subset->compiled)) {
    nexus_memory_deallocate(subset->compiled.ranked_rules);
    nexus_memory_deallocate(subset);

    return NULL;
  }

  return subset;
}

void logos_language_tooling_lexing_lexer_subset_destroy(LLexerSubset *subset) {
  NEXUS_PROGRAM_PRECONDITION(subset != NULL, "subset must not be NULL");

  nexus_memory_deallocate(subset->compiled.ranked_rules);
  nexus_memory_deallocate(subset->compiled.rows);
  nexus_memory_deallocate(subset->compiled.accept_tags);
  logos_automatons_dfa_destroy(subset->compiled.dfa);
  nexus_memory_deallocate(subset);
}

LLexerStatus logos_language_tooling_lexing_lexer_match(const LLexer *lexer, const LLexerSubset *subset, LLexerContext context, const byte *source,
                                                       uint_large source_size, uint_large offset, LLexerToken *out_token,
                                                       const LLexerTokenRule **out_rule, LLexerError *out_error) {
  const LLexerCompiledContext *compiled;
  const uint32                *rows;
  const byte                  *byte_class;
  uint_large                   stop;
  uint_large                   i;
  uint_large                   accepted_end;
  uint_large                   width;
  uint32                       row;
  uint32                       accepted_row;
  boolean                      accepted;
  boolean                      dead;

  NEXUS_PROGRAM_PRECONDITION(lexer != NULL && lexer->finalized && out_token != NULL && out_rule != NULL && out_error != NULL,
                             "match arguments must be valid and the lexer finalized");
  NEXUS_PROGRAM_PRECONDITION(offset <= source_size && (source != NULL || source_size == 0), "the offset must lie within the source");

  *out_rule  = NULL;
  *out_error = LLE_NONE;

  if (offset == source_size) {
    out_token->kind          = L_LEXER_TOKEN_KIND_EOF;
    out_token->lexeme_offset = offset;
    out_token->lexeme_width  = 0;

    return LLS_TOKEN;
  }

  compiled     = subset != NULL ? &subset->compiled : &lexer->compiled[context];
  rows         = compiled->rows;
  byte_class   = compiled->view.byte_class;
  stop         = lexer->token_width_limit > source_size - offset ? source_size : offset + lexer->token_width_limit;
  row          = compiled->initial_row;
  accepted     = FALSE;
  dead         = FALSE;
  accepted_end = 0;
  accepted_row = 0;

  for (i = offset; i < stop; i++) {
    row = rows[row + byte_class[source[i]]];

    if (row < compiled->live_limit) {
      continue;
    }

    if (row >= compiled->final_limit) {
      dead = TRUE;
      break;
    }

    accepted     = TRUE;
    accepted_end = i + 1;
    accepted_row = row;

    if (row >= compiled->accept_limit) {
      break;
    }
  }

  if (!dead && i == stop && stop - offset >= lexer->token_width_limit) {
    /* The match was still alive at the width limit: an error whatever the source after it. */
    out_token->kind          = L_LEXER_TOKEN_KIND_ERROR;
    out_token->lexeme_offset = offset;
    out_token->lexeme_width  = lexer->token_width_limit;
    *out_error               = LLE_TOKEN_TOO_LONG;

    return LLS_ERROR;
  }

  if (accepted) {
    *out_rule                = &compiled->ranked_rules[compiled->accept_tags[accepted_row >> compiled->class_shift]];
    out_token->kind          = (*out_rule)->token_kind;
    out_token->lexeme_offset = offset;
    out_token->lexeme_width  = (uint32)(accepted_end - offset);

    return LLS_TOKEN;
  }

  width = i - offset;

  if (dead && width == 0) {
    width = 1;
  }

  out_token->kind          = L_LEXER_TOKEN_KIND_ERROR;
  out_token->lexeme_offset = offset;
  out_token->lexeme_width  = (uint32)(width > 0 ? width : 1);
  *out_error               = LLE_UNRECOGNIZED;

  return LLS_ERROR;
}
