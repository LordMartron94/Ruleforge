#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
Summary: interns a grammar/scope pair, appending new work in discovery order.
*/
static uint_large l_parsing_parser_symbol(LParser *parser, NexusDataHashMap *index, uint_large source, uint_large policy) {
  uint_large     key[2];
  uint_large     found;
  LParsingSymbol symbol;

  key[0] = source;
  key[1] = policy;

  if (nexus_data_hashmap_get_copy(index, key, &found)) {
    return found;
  }

  found = nexus_data_list_length_get(parser->symbols);

  if (found >= parser->limits.max_grammar_symbols) {
    return LPARSER_INDEX_NONE;
  }

  symbol.source              = source;
  symbol.policy              = policy;
  symbol.first_production    = LPARSER_INDEX_NONE;
  symbol.pratt               = LPARSER_INDEX_NONE;
  symbol.kind                = LCOK_COUNT;
  symbol.is_rule             = FALSE;
  symbol.marker              = 0;
  symbol.label               = NULL;
  symbol.terminal.token_kind = L_LEXER_TOKEN_KIND_ERROR;
  logos_languages_regula_observation_mask_clear(symbol.terminal.symbol_mask);
  symbol.terminal.label         = NULL;
  symbol.terminal.byte_terminal = FALSE;
  (void)nexus_data_list_append(parser->symbols, &symbol);
  nexus_data_hashmap_put(index, key, &found);

  return found;
}

const char *logos_language_tooling_parsing_parser_label_copy(LParser *parser, const char *label) {
  char      *copy;
  uint_large size;

  if (label == NULL) {
    return NULL;
  }

  size = nexus_strings_string_length_get(label) + 1;
  copy = (char *)nexus_memory_allocate(size);
  nexus_memory_bytes_copy(label, copy, size);
  (void)nexus_data_list_append_pointer(parser->labels, copy);

  return copy;
}

const char *logos_language_tooling_parsing_parser_token_label_get(const LParser *parser, LLexerTokenKind kind) {
  const LLexerContextData *lexer_context;
  const LLexerTokenRule   *token_rule;
  uint_large               i;
  uint_large               j;

  for (i = 0; i < nexus_data_list_length_get(parser->lexer->contexts); i++) {
    lexer_context = (const LLexerContextData *)nexus_data_list_at_const(parser->lexer->contexts, i);

    for (j = 0; j < nexus_data_list_length_get(lexer_context->rules); j++) {
      token_rule = (const LLexerTokenRule *)nexus_data_list_at_const(lexer_context->rules, j);

      if (token_rule->token_kind == kind && token_rule->label != NULL) {
        return token_rule->label;
      }
    }
  }

  return NULL;
}

/*
Summary: stores one binarized production and links it to its left-hand symbol.
*/
static void l_parsing_parser_production(LParser *parser, uint_large lhs, uint32 length, uint_large first, uint_large second) {
  LParsingProduction production;
  LParsingSymbol    *symbol;
  uint_large         index;

  symbol             = (LParsingSymbol *)nexus_data_list_at(parser->symbols, lhs);
  production.lhs     = lhs;
  production.length  = length;
  production.rhs[0]  = first;
  production.rhs[1]  = second;
  production.owner   = 0;
  production.role[0] = 0;
  production.role[1] = 0;
  production.next    = symbol->first_production;
  index              = nexus_data_list_length_get(parser->productions);
  (void)nexus_data_list_append(parser->productions, &production);
  symbol->first_production = index;
}

/*
Summary: how many elements the sequence at symbol has: the leaves of the concatenations it is made of.
Description: a precedence rule's body is one sequence, binarized into nested concatenations; an element is anything else.
*/
static uint32 l_parsing_parser_elements_count(const LParser *parser, uint_large symbol) {
  const LParsingSymbol     *entry;
  const LParsingProduction *production;

  entry = (const LParsingSymbol *)nexus_data_list_at_const(parser->symbols, symbol);

  if (entry->kind != LCOK_CONCATENATE || entry->first_production == LPARSER_INDEX_NONE) {
    return 1;
  }

  production = (const LParsingProduction *)nexus_data_list_at_const(parser->productions, entry->first_production);

  return l_parsing_parser_elements_count(parser, production->rhs[0]) + l_parsing_parser_elements_count(parser, production->rhs[1]);
}

/* Summary: whether the first (or last) element of the sequence at symbol is a reference to a rule. */
static boolean l_parsing_parser_end_is_reference(const LParser *parser, uint_large symbol, boolean last) {
  const LParsingSymbol     *entry;
  const LParsingProduction *production;

  entry = (const LParsingSymbol *)nexus_data_list_at_const(parser->symbols, symbol);

  while (entry->kind == LCOK_CONCATENATE && entry->first_production != LPARSER_INDEX_NONE) {
    production = (const LParsingProduction *)nexus_data_list_at_const(parser->productions, entry->first_production);
    entry      = (const LParsingSymbol *)nexus_data_list_at_const(parser->symbols, production->rhs[last ? 1 : 0]);
  }

  return entry->kind == LCOK_REFERENCE;
}

/* Summary: marks the productions of the sequence at symbol with the rule that owns it and the place their nonterminal elements have in it. */
static void l_parsing_parser_roles_assign(LParser *parser, uint_large symbol, uint32 base, uint32 total, uint16 owner) {
  const LParsingSymbol *entry;
  LParsingProduction   *production;
  uint32                first_count;
  uint32                slot;

  entry = (const LParsingSymbol *)nexus_data_list_at_const(parser->symbols, symbol);

  if (entry->kind != LCOK_CONCATENATE || entry->first_production == LPARSER_INDEX_NONE) {
    return;
  }

  production        = (LParsingProduction *)nexus_data_list_at(parser->productions, entry->first_production);
  production->owner = owner;
  first_count       = l_parsing_parser_elements_count(parser, production->rhs[0]);

  for (slot = 0; slot < 2; slot++) {
    const LParsingSymbol *child;
    uint32                index;

    child = (const LParsingSymbol *)nexus_data_list_at_const(parser->symbols, production->rhs[slot]);
    index = slot == 0 ? base : base + first_count;

    if (child->kind == LCOK_CONCATENATE) {
      l_parsing_parser_roles_assign(parser, production->rhs[slot], index, total, owner);
    } else if (child->kind == LCOK_REFERENCE) {
      production->role[slot] = (uint8)((index == 0 ? L_PARSING_ROLE_FIRST : 0) | (index + 1 == total ? L_PARSING_ROLE_LAST : 0));
    }
  }
}

/* Summary: derives the shape of every rule with a precedence from its sequence, once the productions exist. */
static void l_parsing_parser_precedence_finish(LParser *parser) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(parser->symbols); i++) {
    const LParsingSymbol     *entry;
    const LParsingProduction *rule_production;
    LParsingPrecedence       *precedence;
    uint_large                body;
    uint32                    total;

    entry = (const LParsingSymbol *)nexus_data_list_at_const(parser->symbols, i);

    if (entry->marker == 0 || entry->first_production == LPARSER_INDEX_NONE) {
      continue;
    }

    rule_production = (const LParsingProduction *)nexus_data_list_at_const(parser->productions, entry->first_production);
    body            = rule_production->rhs[0];
    total           = l_parsing_parser_elements_count(parser, body);
    precedence      = (LParsingPrecedence *)nexus_data_list_at(parser->precedences, (uint_large)entry->marker - 1);

    if (total >= 2) {
      precedence->left_open  = l_parsing_parser_end_is_reference(parser, body, FALSE);
      precedence->right_open = l_parsing_parser_end_is_reference(parser, body, TRUE);
      l_parsing_parser_roles_assign(parser, body, 0, total, entry->marker);
    }
  }
}

static int32 l_parsing_parser_kind_compare(const void *left, const void *right, void *user_data) {
  LLexerTokenKind lhs;
  LLexerTokenKind rhs;

  (void)user_data;
  lhs = *(const LLexerTokenKind *)left;
  rhs = *(const LLexerTokenKind *)right;

  return (lhs > rhs) - (lhs < rhs);
}

void logos_language_tooling_parsing_parser_configuration_initialize(LParserConfiguration *configuration) {
  NEXUS_PROGRAM_PRECONDITION(configuration != NULL, "configuration must not be NULL");

  configuration->allowed_tokens      = NULL;
  configuration->allowed_token_count = 0;
  configuration->engine              = LPE_EARLEY;
  configuration->entry               = NULL;
  configuration->choice              = LCCM_DEFAULT;
  configuration->global_rules        = NULL;
  configuration->global_rule_count   = 0;
  configuration->grammar_expected    = FALSE;
  configuration->depth_limit         = 0;
}

NexusError *logos_language_tooling_parsing_parser_create(LLexer *lexer, const LContexta *contexta, const LParserConfiguration *configuration,
                                                         LParser **out_parser) {
  LParser                  *parser;
  NexusDataHashMap         *index;
  NexusDataHashMapConfig    map_configuration;
  NexusError               *error;
  const LContextaOperation *operation;
  const LContextaRule      *rule;
  LParsingPratt             pratt;
  const char               *token_label;
  LParsingSymbol            symbol;
  LParsingSymbol           *stored;
  uint_large                operation_count;
  uint_large                i;
  uint_large                first;
  uint_large                second;
  uint_large                policy;

  NEXUS_PROGRAM_PRECONDITION(lexer != NULL && contexta != NULL && configuration != NULL && out_parser != NULL,
                             "parser creation arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(lexer->finalized, "parser needs a finalized lexer");
  NEXUS_PROGRAM_PRECONDITION(configuration->allowed_tokens != NULL || configuration->allowed_token_count == 0,
                             "nonempty allowed-token set must not be NULL");

  for (i = 0; i < configuration->allowed_token_count; i++) {
    NEXUS_PROGRAM_PRECONDITION(configuration->allowed_tokens[i] >= L_LEXER_TOKEN_KIND_FIRST_USER, "allowed kinds must not be reserved");
  }

  *out_parser = NULL;

  if (configuration->engine == LPE_BACKTRACKING) {
    return logos_language_tooling_parsing_backtracking_compile(lexer, contexta, configuration, out_parser);
  }

  error = logos_languages_contexta_contexta_validate(contexta);

  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    return error;
  }

  NEXUS_PROGRAM_INVARIANT(contexta->start_rule != NULL, "validated grammar must have a start");

  operation_count                     = nexus_data_slabbed_list_length_get(contexta->operations);
  parser                              = (LParser *)nexus_memory_allocate(NEXUS_SIZEOF(*parser));
  parser->lexer                       = lexer;
  parser->engine                      = LPE_EARLEY;
  parser->peg                         = NULL;
  parser->limits.recovery_insertions  = 8;
  parser->limits.initial_capacity     = 1024;
  parser->limits.initial_source_bytes = 4096;
  parser->limits.max_grammar_symbols  = 262144;
  parser->limits.max_source_bytes     = (uint_large)256 * 1024 * 1024;
  parser->limits.max_tokens           = 4000000;
  parser->limits.max_items            = 4000000;
  parser->limits.max_nodes            = 8000000;
  parser->limits.max_alternatives     = 16000000;
  parser->limits.max_diagnostics      = 4096;
  NEXUS_PROGRAM_INVARIANT(parser->limits.max_items < LPARSER_INDEX_NONE && parser->limits.max_nodes < LPARSER_INDEX_NONE &&
                              parser->limits.max_alternatives < LPARSER_INDEX_NONE && parser->limits.max_grammar_symbols < LPARSER_INDEX_NONE &&
                              parser->limits.max_tokens < LPARSER_INDEX_NONE && parser->limits.max_source_bytes < LPARSER_INDEX_NONE,
                          "parser storage indexes charts, items, nodes, alternatives, symbols and source bytes with 32 bits");
  parser->symbols       = nexus_data_list_create(NEXUS_SIZEOF(LParsingSymbol), 64);
  parser->productions   = nexus_data_list_create(NEXUS_SIZEOF(LParsingProduction), 128);
  parser->labels        = nexus_data_list_create(NEXUS_SIZEOF(char *), 64);
  parser->pratts        = nexus_data_list_create(NEXUS_SIZEOF(LParsingPratt), 4);
  parser->precedences   = nexus_data_list_create(NEXUS_SIZEOF(LParsingPrecedence), 4);
  parser->policy_count  = operation_count + 1;
  parser->policies      = (LParsingPolicy *)nexus_memory_allocate(parser->policy_count * NEXUS_SIZEOF(*parser->policies));
  parser->process_count = 0;
  parser->tree_count    = 0;

  /* Each scope has a canonical policy identity, independent of its caller. */
  for (i = 0; i < parser->policy_count; i++) {
    parser->policies[i].kinds = NULL;
    parser->policies[i].count = 0;
    operation                 = i > 0 ? (const LContextaOperation *)nexus_data_slabbed_list_at(contexta->operations, i - 1) : NULL;

    if (i == 0) {
      parser->policies[i].count = configuration->allowed_token_count;
    } else if (operation->view.kind == LCOK_ALLOWED_TOKENS) {
      parser->policies[i].count = operation->view.operation_data.allowed_tokens.count;
    }

    if (parser->policies[i].count > 0) {
      parser->policies[i].kinds = (LLexerTokenKind *)nexus_memory_allocate((uint_large)parser->policies[i].count * NEXUS_SIZEOF(LLexerTokenKind));
      nexus_memory_bytes_copy(i == 0 ? configuration->allowed_tokens : operation->view.operation_data.allowed_tokens.kinds, parser->policies[i].kinds,
                              (uint_large)parser->policies[i].count * NEXUS_SIZEOF(LLexerTokenKind));

      {
        NexusDataArray view;

        view = nexus_data_array_view_create(parser->policies[i].kinds, NEXUS_SIZEOF(LLexerTokenKind), parser->policies[i].count);
        nexus_data_array_sort(&view, l_parsing_parser_kind_compare, NULL);
      }
    }
  }

  nexus_data_hashmap_config_initialize(&map_configuration, NEXUS_SIZEOF(uint_large) * 2, NEXUS_SIZEOF(uint_large));
  map_configuration.initial_capacity = 128;
  map_configuration.seed_is_set      = TRUE;
  map_configuration.seed             = 0;
  error                              = nexus_data_hashmap_create(&map_configuration, &index);

  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    logos_language_tooling_parsing_parser_destroy(parser);

    return error;
  }

  (void)l_parsing_parser_symbol(parser, index, operation_count + contexta->start_rule->index, 0);

  for (i = 0; i < nexus_data_list_length_get(parser->symbols); i++) {
    symbol = *(const LParsingSymbol *)nexus_data_list_at(parser->symbols, i);
    first  = LPARSER_INDEX_NONE;
    second = LPARSER_INDEX_NONE;
    policy = symbol.policy;

    if (symbol.source >= operation_count) {
      rule = (const LContextaRule *)nexus_data_slabbed_list_at(contexta->rules, symbol.source - operation_count);
      NEXUS_PROGRAM_INVARIANT(rule->body != NULL, "validated rule must have a body");
      symbol.is_rule = TRUE;
      symbol.source -= operation_count;
      symbol.label = logos_language_tooling_parsing_parser_label_copy(parser, rule->label);
      first        = l_parsing_parser_symbol(parser, index, rule->body->index, policy);

      if (rule->associativity != LCPA_NONE) {
        LParsingPrecedence precedence;

        if (nexus_data_list_length_get(parser->precedences) >= 0xFFFFU - 1) {
          error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_PARSER_GRAMMAR_LIMIT, "too many rules with a precedence");
          break;
        }

        precedence.level         = rule->level;
        precedence.associativity = rule->associativity;
        precedence.left_open     = FALSE;
        precedence.right_open    = FALSE;
        (void)nexus_data_list_append(parser->precedences, &precedence);
        symbol.marker = (uint16)nexus_data_list_length_get(parser->precedences);
      }
    } else {
      operation    = (const LContextaOperation *)nexus_data_slabbed_list_at(contexta->operations, symbol.source);
      symbol.kind  = operation->view.kind;
      symbol.label = logos_language_tooling_parsing_parser_label_copy(parser, operation->view.label);

      switch (symbol.kind) {
      case LCOK_EMPTY_LANGUAGE:
      case LCOK_EMPTY_STRING:
        break;
      case LCOK_SYMBOL:
        symbol.terminal.byte_terminal = TRUE;
        nexus_memory_bytes_copy(operation->view.operation_data.symbol_mask, symbol.terminal.symbol_mask, NEXUS_SIZEOF(LContextaObservationMask));
        break;
      case LCOK_TOKEN:
        symbol.terminal.token_kind = operation->view.operation_data.token_kind;
        break;
      case LCOK_UNIONIZE:
      case LCOK_CONCATENATE:
        first  = l_parsing_parser_symbol(parser, index, operation->view.operation_data.binary.operation_a->index, policy);
        second = l_parsing_parser_symbol(parser, index, operation->view.operation_data.binary.operation_b->index, policy);
        break;
      case LCOK_STAR:
        first  = l_parsing_parser_symbol(parser, index, operation->view.operation_data.operation_star->index, policy);
        second = i;
        break;
      case LCOK_REFERENCE:
        first = l_parsing_parser_symbol(parser, index, operation_count + operation->view.operation_data.reference->index, policy);
        break;
      case LCOK_ALLOWED_TOKENS:
        first = l_parsing_parser_symbol(parser, index, operation->view.operation_data.allowed_tokens.operation->index, operation->index + 1);
        break;
      case LCOK_CHOICE:
      case LCOK_EXCLUDE:
      case LCOK_OPERATORS:
      case LCOK_EMBED:
        break;
      case LCOK_PRATT:
        /* Deterministic: no productions; a process starts a run where an item waits for this symbol. */
        logos_language_tooling_parsing_pratt_compile(parser, operation, &pratt);
        symbol.pratt = nexus_data_list_length_get(parser->pratts);
        (void)nexus_data_list_append(parser->pratts, &pratt);
        break;
      case LCOK_COUNT:
      default:
        NEXUS_PROGRAM_PANIC("invalid Contexta constructor");
      }
    }

    if (symbol.kind == LCOK_CHOICE || symbol.kind == LCOK_EXCLUDE || symbol.kind == LCOK_OPERATORS || symbol.kind == LCOK_EMBED) {
      error =
          NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_PARSER_UNSUPPORTED,
                                      "the Earley engine does not support choice modes, exclusions, binding-power expressions or embedded languages; "
                                      "use the backtracking engine");
      break;
    }

    if (nexus_data_list_length_get(parser->symbols) >= parser->limits.max_grammar_symbols &&
        (first == LPARSER_INDEX_NONE && (symbol.is_rule || symbol.kind == LCOK_REFERENCE || symbol.kind == LCOK_ALLOWED_TOKENS ||
                                         symbol.kind == LCOK_STAR || symbol.kind == LCOK_UNIONIZE || symbol.kind == LCOK_CONCATENATE))) {
      error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_PARSER_GRAMMAR_LIMIT, "Contexta grammar/scope expansion limit exceeded");
      break;
    }

    if ((symbol.kind == LCOK_UNIONIZE || symbol.kind == LCOK_CONCATENATE) && second == LPARSER_INDEX_NONE) {
      error = NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_PARSER_GRAMMAR_LIMIT, "Contexta grammar/scope expansion limit exceeded");
      break;
    }

    if (symbol.kind == LCOK_TOKEN && symbol.label == NULL) {
      token_label  = logos_language_tooling_parsing_parser_token_label_get(parser, symbol.terminal.token_kind);
      symbol.label = logos_language_tooling_parsing_parser_label_copy(parser, token_label);
    }

    symbol.terminal.label = symbol.label;
    stored                = (LParsingSymbol *)nexus_data_list_at(parser->symbols, i);
    *stored               = symbol;

    if (symbol.is_rule || symbol.kind == LCOK_REFERENCE || symbol.kind == LCOK_ALLOWED_TOKENS) {
      l_parsing_parser_production(parser, i, 1, first, LPARSER_INDEX_NONE);
    } else if (symbol.kind == LCOK_CONCATENATE) {
      l_parsing_parser_production(parser, i, 2, first, second);
    } else if (symbol.kind == LCOK_UNIONIZE) {
      l_parsing_parser_production(parser, i, 1, second, LPARSER_INDEX_NONE);
      l_parsing_parser_production(parser, i, 1, first, LPARSER_INDEX_NONE);
    } else if (symbol.kind == LCOK_STAR) {
      /* Left recursion: Earley completes a left-recursive rule in O(1) per element, but a right-recursive one holds O(n) pending items. */
      l_parsing_parser_production(parser, i, 2, i, first);
      l_parsing_parser_production(parser, i, 0, LPARSER_INDEX_NONE, LPARSER_INDEX_NONE);
    } else if (symbol.kind == LCOK_EMPTY_STRING) {
      l_parsing_parser_production(parser, i, 0, LPARSER_INDEX_NONE, LPARSER_INDEX_NONE);
    }
  }

  nexus_data_hashmap_destroy(index);

  if (error->code != NEXUS_PROGRAM_ERROR_SUCCESS()->code) {
    logos_language_tooling_parsing_parser_destroy(parser);

    return error;
  }

  l_parsing_parser_precedence_finish(parser);
  *out_parser = parser;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void logos_language_tooling_parsing_parser_destroy(LParser *parser) {
  uint_large i;

  NEXUS_PROGRAM_PRECONDITION(parser != NULL, "parser must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(parser->process_count == 0 && parser->tree_count == 0, "processes and trees must be destroyed before their parser");

  if (parser->peg != NULL) {
    logos_language_tooling_parsing_backtracking_destroy(parser->peg);
  }

  for (i = 0; i < nexus_data_list_length_get(parser->labels); i++) {
    nexus_memory_deallocate(*(void **)nexus_data_list_at(parser->labels, i));
  }

  for (i = 0; i < parser->policy_count; i++) {
    if (parser->policies[i].kinds != NULL) {
      nexus_memory_deallocate(parser->policies[i].kinds);
    }
  }

  for (i = 0; i < nexus_data_list_length_get(parser->pratts); i++) {
    logos_language_tooling_parsing_pratt_destroy((LParsingPratt *)nexus_data_list_at(parser->pratts, i));
  }

  if (parser->policies != NULL) {
    nexus_memory_deallocate(parser->policies);
  }

  nexus_data_list_destroy(parser->precedences);
  nexus_data_list_destroy(parser->pratts);
  nexus_data_list_destroy(parser->labels);
  nexus_data_list_destroy(parser->productions);
  nexus_data_list_destroy(parser->symbols);
  nexus_memory_deallocate(parser);
}
