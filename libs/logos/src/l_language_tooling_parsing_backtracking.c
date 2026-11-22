#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/* ---------------------------------------------------------------------------- */
/* PROGRAM CONSTRUCTION                                                          */
/* ---------------------------------------------------------------------------- */

typedef struct LPegBuilder {
  LParser                    *parser;
  LPegProgram                *program;
  const LContexta            *contexta;
  uint32                     *operation_node; /* Per Contexta operation: its node, or LPARSER_INDEX_NONE. */
  uint32                      default_mode;
  const LContextaRule *const *globals;
  uint32                      global_count;
} LPegBuilder;

static uint32 l_peg_symbol_add(LPegBuilder *builder, LContextaOperationKind kind, boolean is_rule, const char *label, LLexerTokenKind token_kind) {
  LParsingSymbol symbol;
  uint32         index;

  index = (uint32)nexus_data_list_length_get(builder->parser->symbols);

  symbol.source           = 0;
  symbol.policy           = 0;
  symbol.first_production = LPARSER_INDEX_NONE;
  symbol.pratt            = LPARSER_INDEX_NONE;
  symbol.kind             = kind;
  symbol.is_rule          = is_rule;
  symbol.label            = logos_language_tooling_parsing_parser_label_copy(builder->parser, label);

  if (kind == LCOK_TOKEN && symbol.label == NULL) {
    symbol.label = logos_language_tooling_parsing_parser_label_copy(
        builder->parser, logos_language_tooling_parsing_parser_token_label_get(builder->parser, token_kind));
  }

  symbol.terminal.token_kind = token_kind;
  logos_languages_regula_observation_mask_clear(symbol.terminal.symbol_mask);
  symbol.terminal.label         = symbol.label;
  symbol.terminal.byte_terminal = FALSE;
  (void)nexus_data_list_append(builder->parser->symbols, &symbol);

  return index;
}

static uint32 l_peg_node_add(LPegBuilder *builder, LPegNodeKind kind) {
  LPegNode node;
  uint32   index;

  index = (uint32)nexus_data_list_length_get(builder->program->nodes);

  nexus_memory_bytes_clear(&node, NEXUS_SIZEOF(node));
  node.kind   = (uint8)kind;
  node.a      = (uint32)LPARSER_INDEX_NONE;
  node.b      = (uint32)LPARSER_INDEX_NONE;
  node.symbol = (uint32)LPARSER_INDEX_NONE;
  (void)nexus_data_list_append(builder->program->nodes, &node);

  return index;
}

static LPegNode *l_peg_node_get(LPegBuilder *builder, uint32 index) {
  return (LPegNode *)nexus_data_list_at(builder->program->nodes, index);
}

/* Summary: gathers the alternatives of nested choices that share one mode, in source order. */
static void l_peg_alternatives_collect(const LContextaOperation *operation, LContextaChoiceMode mode, NexusDataList *out) {
  const LContextaOperation *left;
  const LContextaOperation *right;
  LContextaChoiceMode       left_mode;
  LContextaChoiceMode       right_mode;

  if (operation->view.kind == LCOK_UNIONIZE) {
    left  = operation->view.operation_data.binary.operation_a;
    right = operation->view.operation_data.binary.operation_b;
  } else {
    left  = operation->view.operation_data.choice.operation_a;
    right = operation->view.operation_data.choice.operation_b;
  }

  left_mode  = left->view.kind == LCOK_UNIONIZE ? LCCM_DEFAULT : left->view.operation_data.choice.mode;
  right_mode = right->view.kind == LCOK_UNIONIZE ? LCCM_DEFAULT : right->view.operation_data.choice.mode;

  if ((left->view.kind == LCOK_UNIONIZE || left->view.kind == LCOK_CHOICE) && left_mode == mode) {
    l_peg_alternatives_collect(left, mode, out);
  } else {
    (void)nexus_data_list_append_pointer(out, left);
  }

  if ((right->view.kind == LCOK_UNIONIZE || right->view.kind == LCOK_CHOICE) && right_mode == mode) {
    l_peg_alternatives_collect(right, mode, out);
  } else {
    (void)nexus_data_list_append_pointer(out, right);
  }
}

static uint32 l_peg_operation_compile(LPegBuilder *builder, const LContextaOperation *operation);

static uint32 l_peg_operation_compile(LPegBuilder *builder, const LContextaOperation *operation) {
  const LContextaOperatorEntry *entries;
  const LContextaOperation     *alternative;
  NexusDataList                *alternatives;
  LPegOperator                  entry;
  LPegEmbed                     embed;
  LPegTokenSymbol               token_symbol;
  LContextaChoiceMode           mode;
  uint32                        node;
  uint32                       *children;
  uint32                        child;
  uint32                        first;
  uint32                        symbol;
  uint32                        i;
  uint32                        j;
  uint_large                    k;

  if (builder->operation_node[operation->index] != (uint32)LPARSER_INDEX_NONE) {
    return builder->operation_node[operation->index];
  }

  switch (operation->view.kind) {
  case LCOK_EMPTY_LANGUAGE:
    node = l_peg_node_add(builder, LPN_FAIL);
    break;
  case LCOK_EMPTY_STRING:
    node = l_peg_node_add(builder, LPN_EMPTY);
    break;
  case LCOK_TOKEN:
    symbol = l_peg_symbol_add(builder, LCOK_TOKEN, FALSE, operation->view.label, operation->view.operation_data.token_kind);
    node   = l_peg_node_add(builder, LPN_TOKEN);
    l_peg_node_get(builder, node)->token_kind = operation->view.operation_data.token_kind;
    l_peg_node_get(builder, node)->symbol     = symbol;
    token_symbol.kind                         = operation->view.operation_data.token_kind;
    token_symbol.symbol                       = symbol;
    (void)nexus_data_list_append(builder->program->token_symbols, &token_symbol);
    break;
  case LCOK_SYMBOL:
    symbol                                = l_peg_symbol_add(builder, LCOK_SYMBOL, FALSE, operation->view.label, L_LEXER_TOKEN_KIND_ERROR);
    node                                  = l_peg_node_add(builder, LPN_SYMBOL);
    l_peg_node_get(builder, node)->b      = (uint32)nexus_data_list_length_get(builder->program->masks);
    l_peg_node_get(builder, node)->symbol = symbol;
    (void)nexus_data_list_append(builder->program->masks, operation->view.operation_data.symbol_mask);
    break;
  case LCOK_CONCATENATE:
    child                            = l_peg_operation_compile(builder, operation->view.operation_data.binary.operation_a);
    first                            = l_peg_operation_compile(builder, operation->view.operation_data.binary.operation_b);
    node                             = l_peg_node_add(builder, LPN_SEQ);
    l_peg_node_get(builder, node)->a = child;
    l_peg_node_get(builder, node)->b = first;
    break;
  case LCOK_STAR:
    child                            = l_peg_operation_compile(builder, operation->view.operation_data.operation_star);
    node                             = l_peg_node_add(builder, LPN_STAR);
    l_peg_node_get(builder, node)->a = child;
    break;
  case LCOK_REFERENCE:
    node                             = l_peg_node_add(builder, LPN_RULE);
    l_peg_node_get(builder, node)->a = (uint32)operation->view.operation_data.reference->index;
    break;
  case LCOK_UNIONIZE:
  case LCOK_CHOICE:
    mode         = operation->view.kind == LCOK_UNIONIZE ? LCCM_DEFAULT : operation->view.operation_data.choice.mode;
    alternatives = nexus_data_list_create(NEXUS_EFFECTIVE_POINTER_SIZE_BYTES, 8);
    l_peg_alternatives_collect(operation, mode, alternatives);
    /* Compile every alternative first: compiling one may append its own list, and this list must be contiguous. */
    children = (uint32 *)nexus_memory_allocate((nexus_data_list_length_get(alternatives) + 1) * NEXUS_SIZEOF(uint32));

    for (k = 0; k < nexus_data_list_length_get(alternatives); k++) {
      alternative = (const LContextaOperation *)nexus_data_list_pointer_at(alternatives, k);
      children[k] = l_peg_operation_compile(builder, alternative);
    }

    first = (uint32)nexus_data_list_length_get(builder->program->lists);

    for (k = 0; k < nexus_data_list_length_get(alternatives); k++) {
      (void)nexus_data_list_append(builder->program->lists, &children[k]);
    }

    nexus_memory_deallocate(children);
    node                                 = l_peg_node_add(builder, LPN_CHOICE);
    l_peg_node_get(builder, node)->a     = first;
    l_peg_node_get(builder, node)->count = (uint32)nexus_data_list_length_get(alternatives);
    l_peg_node_get(builder, node)->mode  = (uint8)(mode == LCCM_DEFAULT ? builder->default_mode : (uint32)mode);
    nexus_data_list_destroy(alternatives);
    break;
  case LCOK_ALLOWED_TOKENS:
    /* A default-allowed set is an Earley notion; the backtracking engine has global rules instead. */
    node = l_peg_operation_compile(builder, operation->view.operation_data.allowed_tokens.operation);
    break;
  case LCOK_EXCLUDE:
    child                            = l_peg_operation_compile(builder, operation->view.operation_data.exclude.operation);
    node                             = l_peg_node_add(builder, LPN_EXCLUDE);
    l_peg_node_get(builder, node)->a = child;

    for (i = 0; i < operation->view.operation_data.exclude.count; i++) {
      for (j = 0; j < builder->global_count; j++) {
        if (builder->globals[j] == operation->view.operation_data.exclude.rules[i]) {
          l_peg_node_get(builder, node)->mask |= (uint64)1 << j;
        }
      }
    }

    break;
  case LCOK_OPERATORS:
    child   = l_peg_operation_compile(builder, operation->view.operation_data.operators.atom);
    entries = operation->view.operation_data.operators.entries;
    first   = (uint32)nexus_data_list_length_get(builder->program->operators);

    for (i = 0; i < operation->view.operation_data.operators.count; i++) {
      entry.kind       = (uint8)entries[i].kind;
      entry.left_bp    = entries[i].left_bp;
      entry.right_bp   = entries[i].right_bp;
      entry.token_kind = entries[i].token_kind;
      entry.symbol     = l_peg_symbol_add(builder, LCOK_OPERATORS, FALSE, entries[i].label, entries[i].token_kind);
      (void)nexus_data_list_append(builder->program->operators, &entry);
    }

    node                                 = l_peg_node_add(builder, LPN_OPERATORS);
    l_peg_node_get(builder, node)->a     = child;
    l_peg_node_get(builder, node)->b     = first;
    l_peg_node_get(builder, node)->count = operation->view.operation_data.operators.count;
    break;
  case LCOK_EMBED:
    embed.spec                            = operation->view.operation_data.embed;
    embed.symbol                          = l_peg_symbol_add(builder, LCOK_EMBED, FALSE, operation->view.label, L_LEXER_TOKEN_KIND_ERROR);
    node                                  = l_peg_node_add(builder, LPN_EMBED);
    l_peg_node_get(builder, node)->a      = (uint32)nexus_data_list_length_get(builder->program->embeds);
    l_peg_node_get(builder, node)->symbol = embed.symbol;
    (void)nexus_data_list_append(builder->program->embeds, &embed);
    break;
  case LCOK_PRATT:
    /* The token-only table is the Earley engine's; binding-power expressions with rule atoms replace it here. */
    node = l_peg_node_add(builder, LPN_FAIL);
    break;
  case LCOK_COUNT:
  default:
    NEXUS_PROGRAM_PANIC("invalid Contexta operation kind");
  }

  builder->operation_node[operation->index] = node;

  return node;
}

/* ---------------------------------------------------------------------------- */
/* GRAMMAR-EXPECTED TOKEN SETS                                                   */
/* ---------------------------------------------------------------------------- */

/*
With grammar_expected the lexer is asked for the tokens the grammar can accept where a token is wanted, not for every
token of the context. For each node: first (the tokens it can begin with), nullable, follow (what can come after it,
joined over every use of a rule) and start = first plus, when nullable, follow. A token is lexed with the start set of
the outermost construct that begins at its position (the alternatives of a choice share the choice's set), plus the
first tokens of the global rules, which may appear at any boundary.
*/

typedef struct LPegSetAnalysis {
  LPegProgram *program;
  uint32       words;
  uint32       count;
  uint64      *first;
  uint64      *follow;
  uint64      *start;
  boolean     *nullable;
  uint64      *globals;
} LPegSetAnalysis;

static boolean l_peg_set_merge(uint64 *destination, const uint64 *source, uint32 words) {
  uint32  i;
  uint64  merged;
  boolean changed;

  changed = FALSE;

  for (i = 0; i < words; i++) {
    merged = destination[i] | source[i];

    if (merged != destination[i]) {
      destination[i] = merged;
      changed        = TRUE;
    }
  }

  return changed;
}

static void l_peg_set_bit(uint64 *set, LLexerTokenKind kind) {
  set[kind / 64] |= (uint64)1 << (kind % 64);
}

static uint64 *l_peg_set_at(const LPegSetAnalysis *analysis, uint64 *table, uint32 node) {
  return table + ((uint_large)node * analysis->words);
}

/* Summary: the children a node evaluates at its own start position, and the one evaluated after the first. */
static uint32 l_peg_node_body(const LPegSetAnalysis *analysis, const LPegNode *node) {
  return ((const LPegRule *)nexus_data_list_at_const(analysis->program->rules, node->a))->body;
}

static boolean l_peg_first_pass(LPegSetAnalysis *analysis) {
  const LPegNode *node;
  uint32          index;
  uint32          i;
  uint32          child;
  const uint32   *alternatives;
  boolean         changed;
  boolean         was;

  changed = FALSE;

  for (index = 0; index < analysis->count; index++) {
    node = (const LPegNode *)nexus_data_list_at_const(analysis->program->nodes, index);
    was  = analysis->nullable[index];

    switch (node->kind) {
    case LPN_EMPTY:
      analysis->nullable[index] = TRUE;
      break;
    case LPN_SYMBOL:
      for (i = 0; i < analysis->words; i++) {
        l_peg_set_at(analysis, analysis->first, index)[i] = ~(uint64)0;
      }
      break;
    case LPN_SEQ:
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->first, index), l_peg_set_at(analysis, analysis->first, node->a), analysis->words);

      if (analysis->nullable[node->a]) {
        changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->first, index), l_peg_set_at(analysis, analysis->first, node->b), analysis->words);
      }

      analysis->nullable[index] = analysis->nullable[node->a] && analysis->nullable[node->b];
      break;
    case LPN_CHOICE:
      alternatives = (const uint32 *)nexus_data_list_at_const(analysis->program->lists, node->a);

      for (i = 0; i < node->count; i++) {
        changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->first, index), l_peg_set_at(analysis, analysis->first, alternatives[i]),
                                   analysis->words);

        if (analysis->nullable[alternatives[i]]) {
          analysis->nullable[index] = TRUE;
        }
      }
      break;
    case LPN_STAR:
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->first, index), l_peg_set_at(analysis, analysis->first, node->a), analysis->words);
      analysis->nullable[index] = TRUE;
      break;
    case LPN_RULE:
      child = l_peg_node_body(analysis, node);
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->first, index), l_peg_set_at(analysis, analysis->first, child), analysis->words);
      analysis->nullable[index] = analysis->nullable[child];
      break;
    case LPN_EXCLUDE:
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->first, index), l_peg_set_at(analysis, analysis->first, node->a), analysis->words);
      analysis->nullable[index] = analysis->nullable[node->a];
      break;
    case LPN_OPERATORS: {
      const LPegOperator *operators;

      operators = (const LPegOperator *)nexus_data_list_at_const(analysis->program->operators, node->b);
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->first, index), l_peg_set_at(analysis, analysis->first, node->a), analysis->words);

      for (i = 0; i < node->count; i++) {
        if (operators[i].kind == LCOP_PREFIX) {
          l_peg_set_bit(l_peg_set_at(analysis, analysis->first, index), operators[i].token_kind);
        }
      }
    } break;
    case LPN_EMBED: {
      const LPegEmbed *embed;

      embed = (const LPegEmbed *)nexus_data_list_at_const(analysis->program->embeds, node->a);
      l_peg_set_bit(l_peg_set_at(analysis, analysis->first, index),
                    embed->spec.mode == LCEM_REGION ? embed->spec.open_token : embed->spec.source_token);
    } break;
    case LPN_TOKEN:
      l_peg_set_bit(l_peg_set_at(analysis, analysis->first, index), node->token_kind);
      break;
    case LPN_FAIL:
    default:
      break;
    }

    changed |= was != analysis->nullable[index];
  }

  return changed;
}

/* Summary: follow of a node's children from its own follow. */
static boolean l_peg_follow_pass(LPegSetAnalysis *analysis) {
  const LPegNode *node;
  uint32          index;
  uint32          i;
  uint32          child;
  const uint32   *alternatives;
  boolean         changed;
  uint64         *follow;
  uint64         *work;

  changed = FALSE;
  work    = (uint64 *)nexus_memory_allocate(analysis->words * NEXUS_SIZEOF(uint64));

  for (index = 0; index < analysis->count; index++) {
    node   = (const LPegNode *)nexus_data_list_at_const(analysis->program->nodes, index);
    follow = l_peg_set_at(analysis, analysis->follow, index);

    switch (node->kind) {
    case LPN_SEQ:
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->follow, node->b), follow, analysis->words);
      nexus_memory_bytes_copy(l_peg_set_at(analysis, analysis->first, node->b), work, analysis->words * NEXUS_SIZEOF(uint64));

      if (analysis->nullable[node->b]) {
        (void)l_peg_set_merge(work, follow, analysis->words);
      }

      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->follow, node->a), work, analysis->words);
      break;
    case LPN_CHOICE:
      alternatives = (const uint32 *)nexus_data_list_at_const(analysis->program->lists, node->a);

      for (i = 0; i < node->count; i++) {
        changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->follow, alternatives[i]), follow, analysis->words);
      }
      break;
    case LPN_STAR:
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->follow, node->a), follow, analysis->words);
      changed |=
          l_peg_set_merge(l_peg_set_at(analysis, analysis->follow, node->a), l_peg_set_at(analysis, analysis->first, node->a), analysis->words);
      break;
    case LPN_RULE:
      child = l_peg_node_body(analysis, node);
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->follow, child), follow, analysis->words);
      break;
    case LPN_EXCLUDE:
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->follow, node->a), follow, analysis->words);
      break;
    case LPN_OPERATORS: {
      const LPegOperator *operators;

      operators = (const LPegOperator *)nexus_data_list_at_const(analysis->program->operators, node->b);
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->follow, node->a), follow, analysis->words);

      for (i = 0; i < node->count; i++) {
        if (operators[i].kind != LCOP_PREFIX) {
          if (!(l_peg_set_at(analysis, analysis->follow, node->a)[operators[i].token_kind / 64] & ((uint64)1 << (operators[i].token_kind % 64)))) {
            l_peg_set_bit(l_peg_set_at(analysis, analysis->follow, node->a), operators[i].token_kind);
            changed = TRUE;
          }
        }
      }
    } break;
    default:
      break;
    }
  }

  nexus_memory_deallocate(work);

  return changed;
}

/* Summary: the set the node's first token is lexed with: its start set, joined with that of the constructs it begins. */
static boolean l_peg_start_pass(LPegSetAnalysis *analysis) {
  const LPegNode *node;
  uint32          index;
  uint32          i;
  const uint32   *alternatives;
  boolean         changed;
  uint64         *start;

  changed = FALSE;

  for (index = 0; index < analysis->count; index++) {
    node  = (const LPegNode *)nexus_data_list_at_const(analysis->program->nodes, index);
    start = l_peg_set_at(analysis, analysis->start, index);

    switch (node->kind) {
    case LPN_SEQ:
    case LPN_STAR:
    case LPN_EXCLUDE:
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->start, node->a), start, analysis->words);
      break;
    case LPN_CHOICE:
      alternatives = (const uint32 *)nexus_data_list_at_const(analysis->program->lists, node->a);

      for (i = 0; i < node->count; i++) {
        changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->start, alternatives[i]), start, analysis->words);
      }
      break;
    case LPN_RULE:
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->start, l_peg_node_body(analysis, node)), start, analysis->words);
      break;
    case LPN_OPERATORS:
      changed |= l_peg_set_merge(l_peg_set_at(analysis, analysis->start, node->a), start, analysis->words);
      break;
    default:
      break;
    }
  }

  return changed;
}

static uint32 l_peg_set_intern(LPegProgram *program, const uint64 *set) {
  uint_large i;

  for (i = 0; i < nexus_data_list_length_get(program->sets) / program->set_words; i++) {
    if (nexus_memory_bytes_equal((const byte *)nexus_data_list_at_const(program->sets, i * program->set_words), (const byte *)set,
                                 program->set_words * NEXUS_SIZEOF(uint64))) {
      return (uint32)i;
    }
  }

  for (i = 0; i < program->set_words; i++) {
    (void)nexus_data_list_append(program->sets, &set[i]);
  }

  return (uint32)((nexus_data_list_length_get(program->sets) / program->set_words) - 1);
}

/* Summary: the largest token kind the program names, which sizes its sets. */
static LLexerTokenKind l_peg_token_maximum(const LPegProgram *program) {
  LLexerTokenKind maximum;
  uint_large      i;

  maximum = L_LEXER_TOKEN_KIND_FIRST_USER;

  for (i = 0; i < nexus_data_list_length_get(program->nodes); i++) {
    const LPegNode *node;

    node = (const LPegNode *)nexus_data_list_at_const(program->nodes, i);

    if (node->kind == LPN_TOKEN && node->token_kind > maximum) {
      maximum = node->token_kind;
    }
  }

  for (i = 0; i < nexus_data_list_length_get(program->operators); i++) {
    if (((const LPegOperator *)nexus_data_list_at_const(program->operators, i))->token_kind > maximum) {
      maximum = ((const LPegOperator *)nexus_data_list_at_const(program->operators, i))->token_kind;
    }
  }

  for (i = 0; i < nexus_data_list_length_get(program->embeds); i++) {
    const LContextaEmbed *spec;

    spec = &((const LPegEmbed *)nexus_data_list_at_const(program->embeds, i))->spec;

    if (spec->open_token > maximum) {
      maximum = spec->open_token;
    }

    if (spec->close_token > maximum) {
      maximum = spec->close_token;
    }

    if (spec->source_token > maximum) {
      maximum = spec->source_token;
    }
  }

  return maximum;
}

/* Summary: collects the token kinds an expression consists of when it is only tokens, alternatives of tokens and rules of those; FALSE for anything else. */
static boolean l_peg_tokens_only(const LPegProgram *program, uint32 index, uint32 depth, uint64 *bits, uint32 words) {
  const LPegNode *node;
  const uint32   *alternatives;
  uint32          i;

  if (depth > 32) {
    return FALSE;
  }

  node = (const LPegNode *)nexus_data_list_at_const(program->nodes, index);

  switch (node->kind) {
  case LPN_TOKEN:
    if (node->token_kind / 64 >= words) {
      return FALSE;
    }

    bits[node->token_kind / 64] |= (uint64)1 << (node->token_kind % 64);

    return TRUE;
  case LPN_RULE:
    return l_peg_tokens_only(program, ((const LPegRule *)nexus_data_list_at_const(program->rules, node->a))->body, depth + 1, bits, words);
  case LPN_CHOICE:
    alternatives = (const uint32 *)nexus_data_list_at_const(program->lists, node->a);

    for (i = 0; i < node->count; i++) {
      if (!l_peg_tokens_only(program, alternatives[i], depth + 1, bits, words)) {
        return FALSE;
      }
    }

    return TRUE;
  default:
    return FALSE;
  }
}

/*
Summary: whether the deterministic run applies to the program: the optimistic parse that takes the first alternative the next token allows and gives up
  (for the full parser) at the first failure. It needs ordered choices, no operator expressions or embeds, and global rules that are single tokens, so
  that the trivia before a token can be skipped over by looking at tokens alone.
*/
static void l_peg_det_compute(LPegProgram *program) {
  uint_large i;
  uint32     j;

  program->det_ok = TRUE;

  for (i = 0; i < nexus_data_list_length_get(program->nodes) && program->det_ok; i++) {
    const LPegNode *node;

    node = (const LPegNode *)nexus_data_list_at_const(program->nodes, i);

    if (node->kind == LPN_SYMBOL || node->kind == LPN_OPERATORS || node->kind == LPN_EMBED ||
        (node->kind == LPN_CHOICE && node->mode != LCCM_ORDERED)) {
      program->det_ok = FALSE;
    }
  }

  if (program->det_ok && program->global_count > 0) {
    program->global_kinds = (uint64 *)nexus_memory_allocate((uint_large)program->global_count * program->first_words * NEXUS_SIZEOF(uint64));
    nexus_memory_bytes_clear(program->global_kinds, (uint_large)program->global_count * program->first_words * NEXUS_SIZEOF(uint64));

    for (j = 0; j < program->global_count && program->det_ok; j++) {
      program->det_ok = l_peg_tokens_only(program, ((const LPegRule *)nexus_data_list_at_const(program->rules, program->globals[j]))->body, 0,
                                          program->global_kinds + ((uint_large)j * program->first_words), program->first_words);
    }
  }
}

/*
Summary: the first token every node can begin with, for choices to skip the alternatives that cannot match the next token.
  Only without grammar_expected: there a token is lexed for the node that wants it, so the next token's kind is not one thing.
*/
static void l_peg_dispatch_compute(LPegProgram *program) {
  LPegSetAnalysis analysis;
  uint32          i;

  analysis.program  = program;
  analysis.count    = (uint32)nexus_data_list_length_get(program->nodes);
  analysis.words    = (uint32)((l_peg_token_maximum(program) / 64) + 1);
  analysis.first    = (uint64 *)nexus_memory_allocate((uint_large)analysis.count * analysis.words * NEXUS_SIZEOF(uint64));
  analysis.nullable = (boolean *)nexus_memory_allocate(analysis.count * NEXUS_SIZEOF(boolean));
  analysis.follow   = NULL;
  analysis.start    = NULL;
  analysis.globals  = NULL;
  nexus_memory_bytes_clear(analysis.first, (uint_large)analysis.count * analysis.words * NEXUS_SIZEOF(uint64));
  nexus_memory_bytes_clear(analysis.nullable, analysis.count * NEXUS_SIZEOF(boolean));

  while (l_peg_first_pass(&analysis)) {
  }

  program->first       = analysis.first;
  program->nullable    = analysis.nullable;
  program->first_words = analysis.words;

  /* Looking at the next token costs about one failed alternative; it pays from three alternatives that can fail. */
  for (i = 0; i < analysis.count; i++) {
    LPegNode     *node;
    const uint32 *alternatives;
    uint32        failing;
    uint32        j;

    node = (LPegNode *)nexus_data_list_at(program->nodes, i);

    /* A repetition tries its body until it fails: when the next token cannot begin the body, that failed try is not made. */
    if (node->kind == LPN_STAR) {
      node->dispatch = !analysis.nullable[node->a] ? 1 : 0;
      continue;
    }

    if (node->kind != LPN_CHOICE) {
      continue;
    }

    alternatives = (const uint32 *)nexus_data_list_at_const(program->lists, node->a);
    failing      = 0;

    for (j = 0; j < node->count; j++) {
      failing += analysis.nullable[alternatives[j]] ? 0 : 1;
    }

    /* An option is a choice of the item and nothing. */
    if (node->count == 2 && ((const LPegNode *)nexus_data_list_at_const(program->nodes, alternatives[1]))->kind == LPN_EMPTY) {
      node->dispatch = !analysis.nullable[alternatives[0]] ? 1 : 0;
      continue;
    }

    node->dispatch = failing >= 3 ? 1 : 0;
  }

  l_peg_det_compute(program);

  /* The tokens any global rule can begin with: a boundary where the next token is none of them cannot hold trivia. */
  if (program->global_count > 0) {
    program->global_first = (uint64 *)nexus_memory_allocate((uint_large)analysis.words * NEXUS_SIZEOF(uint64));
    nexus_memory_bytes_clear(program->global_first, (uint_large)analysis.words * NEXUS_SIZEOF(uint64));

    for (i = 0; i < program->global_count; i++) {
      const LPegRule *global;
      uint32          word;

      global = (const LPegRule *)nexus_data_list_at_const(program->rules, program->globals[i]);

      for (word = 0; word < analysis.words; word++) {
        program->global_first[word] |= analysis.first[((uint_large)global->body * analysis.words) + word];
      }
    }
  }
}

/*
Summary: collects the first tokens a node can begin with, as (eligible set, kind) pairs; FALSE when it cannot be told (a byte class, an operator
  expression, an embedded region).
*/
static boolean l_peg_probes_of(const LPegProgram *program, uint32 index, uint32 depth, const boolean *nullable, NexusDataList *out) {
  const LPegNode *node;
  const uint32   *alternatives;
  uint32          i;

  if (depth > 32) {
    return FALSE;
  }

  node = (const LPegNode *)nexus_data_list_at_const(program->nodes, index);

  switch (node->kind) {
  case LPN_EMPTY:
  case LPN_FAIL:
    return TRUE;
  case LPN_TOKEN: {
    LPegProbe probe;

    probe.set  = node->a;
    probe.kind = node->token_kind;
    (void)nexus_data_list_append(out, &probe);

    return TRUE;
  }
  case LPN_SEQ:
    return l_peg_probes_of(program, node->a, depth + 1, nullable, out) &&
           (!nullable[node->a] || l_peg_probes_of(program, node->b, depth + 1, nullable, out));
  case LPN_CHOICE:
    alternatives = (const uint32 *)nexus_data_list_at_const(program->lists, node->a);

    for (i = 0; i < node->count; i++) {
      if (!l_peg_probes_of(program, alternatives[i], depth + 1, nullable, out)) {
        return FALSE;
      }
    }

    return TRUE;
  case LPN_STAR:
  case LPN_EXCLUDE:
    return l_peg_probes_of(program, node->a, depth + 1, nullable, out);
  case LPN_RULE:
    return l_peg_probes_of(program, ((const LPegRule *)nexus_data_list_at_const(program->rules, node->a))->body, depth + 1, nullable, out);
  case LPN_SYMBOL:
  case LPN_OPERATORS:
  case LPN_EMBED:
  default:
    return FALSE;
  }
}

/* Summary: stores the distinct (set, kind) probes of scratch as the probes of a node. */
static void l_peg_probes_keep(LPegProgram *program, const NexusDataList *scratch, uint32 node) {
  uint_large k;
  uint_large kept;

  program->probe_start[node] = (uint32)nexus_data_list_length_get(program->probes);
  kept                       = 0;

  for (k = 0; k < nexus_data_list_length_get(scratch); k++) {
    const LPegProbe *probe;
    uint_large       earlier;
    boolean          duplicate;

    probe     = (const LPegProbe *)nexus_data_list_at_const(scratch, k);
    duplicate = FALSE;

    for (earlier = 0; earlier < k; earlier++) {
      const LPegProbe *other;

      other     = (const LPegProbe *)nexus_data_list_at_const(scratch, earlier);
      duplicate = duplicate || (other->set == probe->set && other->kind == probe->kind);
    }

    if (!duplicate) {
      (void)nexus_data_list_append(program->probes, probe);
      kept++;
    }
  }

  program->probe_count[node] = (uint16)kept;
}

/*
Summary: for every alternative of a choice, the first tokens it can begin with, so that a choice lexes them and tries only the alternatives whose
  token the lexer finds. A token is lexed with the eligible set of the node that wants it, so what an alternative would find is exactly what its
  first token node finds: skipping an alternative whose every first token fails changes nothing but the time.
*/
static void l_peg_probes_compute(LPegProgram *program, const boolean *nullable) {
  uint32         count;
  uint32         i;
  uint32         j;
  NexusDataList *scratch;

  count                = (uint32)nexus_data_list_length_get(program->nodes);
  program->probes      = nexus_data_list_create(NEXUS_SIZEOF(LPegProbe), 64);
  program->probe_start = (uint32 *)nexus_memory_allocate((uint_large)count * NEXUS_SIZEOF(uint32));
  program->probe_count = (uint16 *)nexus_memory_allocate((uint_large)count * NEXUS_SIZEOF(uint16));
  scratch              = nexus_data_list_create(NEXUS_SIZEOF(LPegProbe), 16);

  for (i = 0; i < count; i++) {
    program->probe_start[i] = 0;
    program->probe_count[i] = LPEG_PROBES_UNKNOWN;
  }

  for (i = 0; i < count; i++) {
    LPegNode     *node;
    const uint32 *alternatives;
    uint32        skippable;
    boolean       option;

    node = (LPegNode *)nexus_data_list_at(program->nodes, i);

    /* The body of a repetition: the same probes, kept for the body node, which the loop asks about before each try. */
    if (node->kind == LPN_STAR) {
      uint32 body;

      body           = node->a;
      node->dispatch = 0;
      nexus_data_list_clear(scratch);

      if (!nullable[body] && l_peg_probes_of(program, body, 0, nullable, scratch) && nexus_data_list_length_get(scratch) > 0 &&
          nexus_data_list_length_get(scratch) < LPEG_PROBES_UNKNOWN) {
        l_peg_probes_keep(program, scratch, body);
        node->dispatch = 2;
      }

      continue;
    }

    if (node->kind != LPN_CHOICE || node->count < 2) {
      continue;
    }

    alternatives = (const uint32 *)nexus_data_list_at_const(program->lists, node->a);
    skippable    = 0;

    /* An option is a choice of the item and nothing; a larger choice needs expected_dispatch. */
    option = node->count == 2 && ((const LPegNode *)nexus_data_list_at_const(program->nodes, alternatives[1]))->kind == LPN_EMPTY;
    if (!option && node->count < 3) {
      continue;
    }

    for (j = 0; j < node->count; j++) {
      if (program->probe_count[alternatives[j]] != LPEG_PROBES_UNKNOWN) {
        skippable++;
        continue;
      }

      nexus_data_list_clear(scratch);

      if (nullable[alternatives[j]] || !l_peg_probes_of(program, alternatives[j], 0, nullable, scratch) || nexus_data_list_length_get(scratch) == 0 ||
          nexus_data_list_length_get(scratch) >= LPEG_PROBES_UNKNOWN) {
        continue;
      }

      l_peg_probes_keep(program, scratch, alternatives[j]);
      skippable++;
    }

    node->dispatch = skippable >= (option ? 1U : 2U) ? 2 : 0;
  }

  nexus_data_list_destroy(scratch);
}

static void l_peg_sets_compute(LPegProgram *program) {
  LPegSetAnalysis analysis;
  LLexerTokenKind maximum;
  uint_large      i;
  uint32          j;
  uint64         *work;
  LPegNode       *node;

  analysis.program = program;
  analysis.count   = (uint32)nexus_data_list_length_get(program->nodes);
  maximum          = l_peg_token_maximum(program);

  analysis.words     = (uint32)((maximum / 64) + 1);
  program->set_words = analysis.words;
  analysis.first     = (uint64 *)nexus_memory_allocate((uint_large)analysis.count * analysis.words * NEXUS_SIZEOF(uint64));
  analysis.follow    = (uint64 *)nexus_memory_allocate((uint_large)analysis.count * analysis.words * NEXUS_SIZEOF(uint64));
  analysis.start     = (uint64 *)nexus_memory_allocate((uint_large)analysis.count * analysis.words * NEXUS_SIZEOF(uint64));
  analysis.nullable  = (boolean *)nexus_memory_allocate(analysis.count * NEXUS_SIZEOF(boolean));
  analysis.globals   = (uint64 *)nexus_memory_allocate(analysis.words * NEXUS_SIZEOF(uint64));
  nexus_memory_bytes_clear(analysis.first, (uint_large)analysis.count * analysis.words * NEXUS_SIZEOF(uint64));
  nexus_memory_bytes_clear(analysis.follow, (uint_large)analysis.count * analysis.words * NEXUS_SIZEOF(uint64));
  nexus_memory_bytes_clear(analysis.start, (uint_large)analysis.count * analysis.words * NEXUS_SIZEOF(uint64));
  nexus_memory_bytes_clear(analysis.nullable, analysis.count * NEXUS_SIZEOF(boolean));
  nexus_memory_bytes_clear(analysis.globals, analysis.words * NEXUS_SIZEOF(uint64));

  while (l_peg_first_pass(&analysis)) {
  }

  l_peg_set_bit(l_peg_set_at(&analysis, analysis.follow, ((const LPegRule *)nexus_data_list_at_const(program->rules, program->entry_rule))->body),
                L_LEXER_TOKEN_KIND_EOF);

  while (l_peg_follow_pass(&analysis)) {
  }

  for (j = 0; j < program->global_count; j++) {
    (void)l_peg_set_merge(
        analysis.globals,
        l_peg_set_at(&analysis, analysis.first, ((const LPegRule *)nexus_data_list_at_const(program->rules, program->globals[j]))->body),
        analysis.words);
  }

  for (i = 0; i < analysis.count; i++) {
    (void)l_peg_set_merge(l_peg_set_at(&analysis, analysis.start, (uint32)i), l_peg_set_at(&analysis, analysis.first, (uint32)i), analysis.words);

    if (analysis.nullable[i]) {
      (void)l_peg_set_merge(l_peg_set_at(&analysis, analysis.start, (uint32)i), l_peg_set_at(&analysis, analysis.follow, (uint32)i), analysis.words);
    }
  }

  /* The operand after a prefix operator starts with what an operand starts with, plus the operators themselves. */
  for (i = 0; i < analysis.count; i++) {
    node = (LPegNode *)nexus_data_list_at(program->nodes, i);

    if (node->kind == LPN_OPERATORS) {
      (void)l_peg_set_merge(l_peg_set_at(&analysis, analysis.start, node->a), l_peg_set_at(&analysis, analysis.first, (uint32)i), analysis.words);
    }
  }

  while (l_peg_start_pass(&analysis)) {
  }

  work = (uint64 *)nexus_memory_allocate(analysis.words * NEXUS_SIZEOF(uint64));

  /* The tokens a boundary can hold as trivia: a boundary where the lexer finds none of them has nothing to try. */
  if (program->global_count > 0) {
    program->global_first = (uint64 *)nexus_memory_allocate(analysis.words * NEXUS_SIZEOF(uint64));
    nexus_memory_bytes_copy(analysis.globals, program->global_first, analysis.words * NEXUS_SIZEOF(uint64));
    program->global_set = l_peg_set_intern(program, analysis.globals);
  }

  for (i = 0; i < analysis.count; i++) {
    node = (LPegNode *)nexus_data_list_at(program->nodes, i);

    if (node->kind == LPN_EMBED) {
      nexus_memory_bytes_copy(l_peg_set_at(&analysis, analysis.start, (uint32)i), work, analysis.words * NEXUS_SIZEOF(uint64));
      (void)l_peg_set_merge(work, analysis.globals, analysis.words);
      node->mask = l_peg_set_intern(program, work);
    } else if (node->kind == LPN_TOKEN || node->kind == LPN_SYMBOL) {
      nexus_memory_bytes_copy(l_peg_set_at(&analysis, analysis.start, (uint32)i), work, analysis.words * NEXUS_SIZEOF(uint64));
      (void)l_peg_set_merge(work, analysis.globals, analysis.words);
      node->a = l_peg_set_intern(program, work);
    } else if (node->kind == LPN_OPERATORS) {
      const LPegOperator *operators;

      operators = (const LPegOperator *)nexus_data_list_at_const(program->operators, node->b);
      nexus_memory_bytes_copy(l_peg_set_at(&analysis, analysis.first, (uint32)i), work, analysis.words * NEXUS_SIZEOF(uint64));
      (void)l_peg_set_merge(work, l_peg_set_at(&analysis, analysis.follow, (uint32)i), analysis.words);
      (void)l_peg_set_merge(work, l_peg_set_at(&analysis, analysis.first, node->a), analysis.words);
      (void)l_peg_set_merge(work, analysis.globals, analysis.words);

      for (j = 0; j < node->count; j++) {
        l_peg_set_bit(work, operators[j].token_kind);
      }

      node->mask = l_peg_set_intern(program, work);
    }
  }

  l_peg_probes_compute(program, analysis.nullable);

  nexus_memory_deallocate(work);
  nexus_memory_deallocate(analysis.first);
  nexus_memory_deallocate(analysis.follow);
  nexus_memory_deallocate(analysis.start);
  nexus_memory_deallocate(analysis.nullable);
  nexus_memory_deallocate(analysis.globals);
}

NexusError *logos_language_tooling_parsing_backtracking_compile(LLexer *lexer, const LContexta *contexta, const LParserConfiguration *configuration,
                                                                LParser **out_parser) {
  LParser             *parser;
  LPegProgram         *program;
  LPegBuilder          builder;
  LPegRule             rule_entry;
  const LContextaRule *rule;
  const LContextaRule *entry;
  uint_large           operation_count;
  uint_large           rule_count;
  uint_large           i;
  uint32               j;
  uint32               k;

  NEXUS_PROGRAM_PRECONDITION(lexer != NULL && contexta != NULL && configuration != NULL && out_parser != NULL, "arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(configuration->global_rule_count <= LPEG_GLOBALS_MAX, "at most 64 global rules are supported");
  NEXUS_PROGRAM_PRECONDITION(configuration->global_rules != NULL || configuration->global_rule_count == 0, "nonempty global rules must not be NULL");

  *out_parser = NULL;
  entry       = configuration->entry != NULL ? configuration->entry : contexta->start_rule;

  if (entry == NULL) {
    return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_CONTEXTA_MISSING_START, "Contexta has no start rule and the configuration names no entry rule");
  }

  rule_count = nexus_data_slabbed_list_length_get(contexta->rules);

  for (i = 0; i < rule_count; i++) {
    rule = (const LContextaRule *)nexus_data_slabbed_list_at(contexta->rules, i);

    if (rule->body == NULL) {
      return NEXUS_PROGRAM_ERROR_FAILURE(GLOBAL_LOGOS_CONTEXTA_UNDEFINED_RULE, "Contexta rule %llu (%s) is undefined", (unsigned long long)i,
                                         rule->label != NULL ? rule->label : "unlabeled");
    }
  }

  operation_count = nexus_data_slabbed_list_length_get(contexta->operations);

  parser         = (LParser *)nexus_memory_allocate(NEXUS_SIZEOF(*parser));
  parser->engine = LPE_BACKTRACKING;
  parser->lexer  = lexer;
  nexus_memory_bytes_clear(&parser->limits, NEXUS_SIZEOF(parser->limits));
  parser->limits.initial_capacity     = 1024;
  parser->limits.initial_source_bytes = 4096;
  parser->limits.max_grammar_symbols  = 262144;
  parser->limits.max_source_bytes     = (uint_large)256 * 1024 * 1024;
  parser->limits.max_tokens           = 4000000;
  parser->limits.max_items            = 4000000;
  parser->limits.max_nodes            = 8000000;
  parser->limits.max_alternatives     = 16000000;
  parser->limits.max_diagnostics      = 4096;
  parser->symbols                     = nexus_data_list_create(NEXUS_SIZEOF(LParsingSymbol), 64);
  parser->productions                 = nexus_data_list_create(NEXUS_SIZEOF(LParsingProduction), 4);
  parser->labels                      = nexus_data_list_create(NEXUS_SIZEOF(char *), 64);
  parser->pratts                      = nexus_data_list_create(NEXUS_SIZEOF(LParsingPratt), 4);
  parser->precedences                 = nexus_data_list_create(NEXUS_SIZEOF(LParsingPrecedence), 4);
  parser->policies                    = NULL;
  parser->policy_count                = 0;
  parser->process_count               = 0;
  parser->tree_count                  = 0;

  program                   = (LPegProgram *)nexus_memory_allocate(NEXUS_SIZEOF(*program));
  program->nodes            = nexus_data_list_create(NEXUS_SIZEOF(LPegNode), 64);
  program->lists            = nexus_data_list_create(NEXUS_SIZEOF(uint32), 64);
  program->rules            = nexus_data_list_create(NEXUS_SIZEOF(LPegRule), 16);
  program->operators        = nexus_data_list_create(NEXUS_SIZEOF(LPegOperator), 16);
  program->masks            = nexus_data_list_create(NEXUS_SIZEOF(LContextaObservationMask), 4);
  program->embeds           = nexus_data_list_create(NEXUS_SIZEOF(LPegEmbed), 4);
  program->token_symbols    = nexus_data_list_create(NEXUS_SIZEOF(LPegTokenSymbol), 16);
  program->entry_rule       = (uint32)entry->index;
  program->global_count     = configuration->global_rule_count;
  program->grammar_expected = configuration->grammar_expected;
  program->global_first     = NULL;
  program->det_ok           = FALSE;
  program->det_disabled     = FALSE;
  program->global_kinds     = NULL;
  program->probes           = NULL;
  program->probe_start      = NULL;
  program->probe_count      = NULL;
  program->global_set       = (uint32)LPARSER_INDEX_NONE;
  program->depth_limit      = configuration->depth_limit != 0 ? configuration->depth_limit : 2000;
  program->sets             = nexus_data_list_create(NEXUS_SIZEOF(uint64), 64);
  program->set_words        = 1;
  program->first            = NULL;
  program->nullable         = NULL;
  program->first_words      = 0;
  program->subsets          = NULL;
  program->subset_built     = NULL;
  program->subset_slots     = 0;
  program->subset_contexts  = 0;
  parser->peg               = program;

  builder.parser         = parser;
  builder.program        = program;
  builder.contexta       = contexta;
  builder.default_mode   = configuration->choice == LCCM_DEFAULT ? (uint32)LCCM_ORDERED : (uint32)configuration->choice;
  builder.globals        = configuration->global_rules;
  builder.global_count   = configuration->global_rule_count;
  builder.operation_node = (uint32 *)nexus_memory_allocate((operation_count > 0 ? operation_count : 1) * NEXUS_SIZEOF(uint32));

  for (i = 0; i < operation_count; i++) {
    builder.operation_node[i] = (uint32)LPARSER_INDEX_NONE;
  }

  for (j = 0; j < configuration->global_rule_count; j++) {
    program->globals[j] = (uint32)configuration->global_rules[j]->index;
  }

  /* Every rule gets its symbol first, so a rule's syntax nodes carry its label. */
  for (i = 0; i < rule_count; i++) {
    rule              = (const LContextaRule *)nexus_data_slabbed_list_at(contexta->rules, i);
    rule_entry.body   = (uint32)LPARSER_INDEX_NONE;
    rule_entry.symbol = l_peg_symbol_add(&builder, LCOK_COUNT, TRUE, rule->label, L_LEXER_TOKEN_KIND_ERROR);
    (void)nexus_data_list_append(program->rules, &rule_entry);
  }

  for (i = 0; i < rule_count; i++) {
    rule                                                      = (const LContextaRule *)nexus_data_slabbed_list_at(contexta->rules, i);
    k                                                         = l_peg_operation_compile(&builder, rule->body);
    ((LPegRule *)nexus_data_list_at(program->rules, i))->body = k;
  }

  if (program->grammar_expected) {
    l_peg_sets_compute(program);
  } else {
    l_peg_dispatch_compute(program);
  }

  program->node_base = (const LPegNode *)nexus_data_list_at_const(program->nodes, 0);
  program->list_base = nexus_data_list_length_get(program->lists) > 0 ? (const uint32 *)nexus_data_list_at_const(program->lists, 0) : NULL;
  program->rule_base = (const LPegRule *)nexus_data_list_at_const(program->rules, 0);

  nexus_memory_deallocate(builder.operation_node);
  *out_parser = parser;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

void logos_language_tooling_parsing_backtracking_destroy(LPegProgram *program) {
  NEXUS_PROGRAM_PRECONDITION(program != NULL, "program must not be NULL");

  nexus_data_list_destroy(program->nodes);
  nexus_data_list_destroy(program->lists);
  nexus_data_list_destroy(program->rules);
  nexus_data_list_destroy(program->operators);
  nexus_data_list_destroy(program->masks);
  nexus_data_list_destroy(program->embeds);
  nexus_data_list_destroy(program->token_symbols);
  if (program->probes != NULL) {
    nexus_data_list_destroy(program->probes);
    nexus_memory_deallocate(program->probe_start);
    nexus_memory_deallocate(program->probe_count);
  }

  nexus_data_list_destroy(program->sets);

  if (program->subsets != NULL) {
    uint_large slot;

    for (slot = 0; slot < program->subset_slots; slot++) {
      if (program->subset_built[slot] && program->subsets[slot] != NULL) {
        logos_language_tooling_lexing_lexer_subset_destroy(program->subsets[slot]);
      }
    }

    nexus_memory_deallocate((void *)program->subsets);
    nexus_memory_deallocate(program->subset_built);
  }

  if (program->first != NULL) {
    nexus_memory_deallocate(program->first);
    nexus_memory_deallocate(program->nullable);
  }

  if (program->global_kinds != NULL) {
    nexus_memory_deallocate(program->global_kinds);
  }

  if (program->global_first != NULL) {
    nexus_memory_deallocate(program->global_first);
  }

  nexus_memory_deallocate(program);
}
