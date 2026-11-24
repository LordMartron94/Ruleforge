#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

/*
The backtracking engine evaluates a compiled rule program over the retained source with PEG semantics. Evaluation
state is a byte position, a lexical context stack and the set of suppressed global rules. Alternatives are tried
transactionally: a checkpoint is the state plus the lengths of the syntax tree's node and alternative lists, so
undoing speculative work is a truncation. The lexical context stack is persistent (a linked list in an arena), so
a checkpoint of it is one index.
*/

#define LPEG_CACHE_SIZE         512
#define LPEG_FAR_KINDS_MAX      64
#define LPEG_ALTERNATIVES_STACK 16

typedef struct LPegStackFrame {
  uint32 context;
  uint32 parent;
  uint32 depth;
} LPegStackFrame;

typedef struct LPegLexEntry {
  uint_large             position;
  uint32                 stack;
  uint32                 set;
  boolean                valid;
  LLexerStatus           status;
  LLexerToken            token;
  const LLexerTokenRule *rule;
  LLexerError            error;
} LPegLexEntry;

typedef struct LPegState {
  uint_large pos;
  uint32     stack;
  uint64     excluded;
} LPegState;

typedef struct LPegCheckpoint {
  LPegState  state;
  uint_large nodes;
  uint_large alternatives;
  uint_large tokens; /* The length of the token table, which the parse fills as it goes. */
} LPegCheckpoint;

typedef struct LPegProcess {
  LParser     *parser; /* Must stay first: the shared process API reads it. */
  LPegProgram *program;
  LParserTree *tree;

  boolean       ready_for_chunk;
  boolean       last_chunk;
  boolean       done;
  LParserStatus status;

  NexusDataChunkedList stack_frames;
  LPegLexEntry        *cache;
  boolean              det;       /* The deterministic run: no backtracking, the first failure ends it (see l_peg_det_compute). */
  uint64              *det_union; /* The token kinds of the global rules that are not excluded, for det_union_mask. */
  uint64               det_union_mask;
  boolean              fast;     /* The optimizations that skip alternatives are on; off for the run that reports a failure. */
  const LPegLexEntry  *last_lex; /* The entry the latest l_peg_lex answered with, or NULL: checked before the hashed cache. */
  uint_large           context_count;
  LParserProcess     **guests;      /* One lazily created guest process per embed of the program. */
  uint_large           last_offset; /* The lexeme of the token the latest successful l_peg_token matched. */
  uint32               last_width;

  uint32  depth;
  boolean limited;

  boolean         far_valid;
  uint_large      far_position;
  uint32          far_count;
  LLexerTokenKind far_kinds[LPEG_FAR_KINDS_MAX];
  LLexerError     far_error;

  uint_large ambiguity[8];
  uint32     ambiguity_count;

  /* The kind of the next token after the boundary, for the position and state it was asked at (choices peek it once, not per alternative). */
  boolean         peek_valid;
  uint_large      peek_position;
  uint_large      peek_end; /* Where that token starts. */
  uint32          peek_stack;
  uint64          peek_excluded;
  uint32          peek_end_stack; /* The lexical stack at that boundary. */
  LLexerTokenKind peek_kind;      /* L_LEXER_TOKEN_KIND_ERROR when there is no token to go by. */
} LPegProcess;

static const LPegNode *l_peg_program_node(const LPegProgram *program, uint32 index) {
  return program->node_base + index;
}

static const uint32 *l_peg_program_list(const LPegProgram *program, uint32 index) {
  return program->list_base + index;
}

static const LPegRule *l_peg_program_rule(const LPegProgram *program, uint32 index) {
  return program->rule_base + index;
}

/* ---------------------------------------------------------------------------- */
/* TREE CONSTRUCTION                                                             */
/* ---------------------------------------------------------------------------- */

static uint32 l_peg_node_new(LPegProcess *process, LParserGreenKind kind, uint_large width, uint32 aux) {
  LParsingGreenNode *node;
  uint_large         index;

  index = nexus_data_chunked_list_length_get(&process->tree->nodes);

  if (index >= process->parser->limits.max_nodes) {
    process->limited = TRUE;

    return (uint32)LPARSER_INDEX_NONE;
  }

  node                    = (LParsingGreenNode *)nexus_data_chunked_list_append_uninitialized(&process->tree->nodes);
  node->kind              = (uint8)kind;
  node->lexical_error     = (uint8)LLE_NONE;
  node->reserved          = 0;
  node->width             = (uint32)width;
  node->alternative_count = 0;
  node->aux               = aux;
  node->first_alternative = (uint32)LPARSER_INDEX_NONE;
  node->last_alternative  = (uint32)LPARSER_INDEX_NONE;
  node->payload.label     = NULL;

  return (uint32)index;
}

static void l_peg_alternative_add(LPegProcess *process, uint32 parent, uint32 first, uint32 second) {
  LParsingGreenNode   *node;
  LParsingAlternative *alternative;
  LParsingAlternative *previous;
  uint_large           index;
  uint_large           width;

  index = nexus_data_chunked_list_length_get(&process->tree->alternatives);

  if (parent == LPARSER_INDEX_NONE || index >= process->parser->limits.max_alternatives) {
    process->limited = TRUE;

    return;
  }

  node  = (LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, parent);
  width = 0;

  if (first != LPARSER_INDEX_NONE) {
    width = ((const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, first))->width;
  }

  if (second != LPARSER_INDEX_NONE) {
    width += ((const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, second))->width;
  }

  NEXUS_PROGRAM_INVARIANT(width == node->width, "every packed family must cover exactly its parent's bytes");

  alternative              = (LParsingAlternative *)nexus_data_chunked_list_append_uninitialized(&process->tree->alternatives);
  alternative->parent      = parent;
  alternative->children[0] = first;
  alternative->children[1] = second;
  alternative->next        = (uint32)LPARSER_INDEX_NONE;

  if (node->last_alternative == LPARSER_INDEX_NONE) {
    node->first_alternative = (uint32)index;
  } else {
    previous       = (LParsingAlternative *)nexus_data_chunked_list_at(&process->tree->alternatives, node->last_alternative);
    previous->next = (uint32)index;
  }

  node->last_alternative = (uint32)index;
  node->alternative_count++;
}

/*
Summary: a node together with its first and only alternative, written in one go.
Description: most nodes (a sequence, a rule) have exactly one alternative when they are made, which saves what adding one to a node that may already
  have several costs (the lookup of the width of the children, the link from the previous alternative).
*/
static uint32 l_peg_node_packed(LPegProcess *process, LParserGreenKind kind, uint_large width, uint32 aux, uint32 first, uint32 second) {
  LParsingGreenNode   *node;
  LParsingAlternative *alternative;
  uint_large           index;
  uint_large           alternative_index;

  index             = nexus_data_chunked_list_length_get(&process->tree->nodes);
  alternative_index = nexus_data_chunked_list_length_get(&process->tree->alternatives);

  if (index >= process->parser->limits.max_nodes || alternative_index >= process->parser->limits.max_alternatives) {
    process->limited = TRUE;

    return (uint32)LPARSER_INDEX_NONE;
  }

  node                    = (LParsingGreenNode *)nexus_data_chunked_list_append_uninitialized(&process->tree->nodes);
  node->kind              = (uint8)kind;
  node->lexical_error     = (uint8)LLE_NONE;
  node->reserved          = 0;
  node->width             = (uint32)width;
  node->alternative_count = 1;
  node->aux               = aux;
  node->first_alternative = (uint32)alternative_index;
  node->last_alternative  = (uint32)alternative_index;
  node->payload.label     = NULL;

  alternative              = (LParsingAlternative *)nexus_data_chunked_list_append_uninitialized(&process->tree->alternatives);
  alternative->parent      = (uint32)index;
  alternative->children[0] = first;
  alternative->children[1] = second;
  alternative->next        = (uint32)LPARSER_INDEX_NONE;

  return (uint32)index;
}

static uint_large l_peg_width(const LPegProcess *process, uint32 node) {
  return node == LPARSER_INDEX_NONE ? 0 : ((const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, node))->width;
}

/* Summary: the node covering a then b; an absent side contributes nothing. */
static uint32 l_peg_sequence(LPegProcess *process, uint32 first, uint32 second) {
  uint32 node;

  if (first == LPARSER_INDEX_NONE) {
    return second;
  }

  if (second == LPARSER_INDEX_NONE) {
    return first;
  }

  node = l_peg_node_packed(process, LPGK_SEQUENCE, l_peg_width(process, first) + l_peg_width(process, second), (uint32)LPARSER_INDEX_NONE, first,
                           second);

  return node;
}

/* ---------------------------------------------------------------------------- */
/* STATE, CHECKPOINTS AND LEXING                                                 */
/* ---------------------------------------------------------------------------- */

static LPegCheckpoint l_peg_checkpoint(const LPegProcess *process, const LPegState *state) {
  LPegCheckpoint checkpoint;

  checkpoint.state        = *state;
  checkpoint.nodes        = nexus_data_chunked_list_length_get(&process->tree->nodes);
  checkpoint.alternatives = nexus_data_chunked_list_length_get(&process->tree->alternatives);
  checkpoint.tokens       = nexus_data_chunked_list_length_get(&process->tree->tokens);

  return checkpoint;
}

static void l_peg_rollback(LPegProcess *process, LPegState *state, const LPegCheckpoint *checkpoint) {
  *state = checkpoint->state;
  nexus_data_chunked_list_truncate(&process->tree->nodes, checkpoint->nodes);
  nexus_data_chunked_list_truncate(&process->tree->alternatives, checkpoint->alternatives);
  nexus_data_chunked_list_truncate(&process->tree->tokens, checkpoint->tokens);
}

/* Summary: adds the token leaf (its kind already set) to the token table; a rollback cuts the table back with the tree. */
static void l_peg_token_record(LPegProcess *process, uint32 leaf) {
  const LParsingGreenNode *node;
  LParserToken            *record;

  node           = (const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, leaf);
  record         = (LParserToken *)nexus_data_chunked_list_append_uninitialized(&process->tree->tokens);
  record->kind   = (uint32)node->payload.token_kind;
  record->offset = node->aux;
  record->width  = node->width;
  record->error  = node->lexical_error;
  record->leaf   = leaf;
}

static LLexerContext l_peg_context(const LPegProcess *process, uint32 stack) {
  return ((const LPegStackFrame *)nexus_data_chunked_list_at(&process->stack_frames, stack))->context;
}

/* Summary: applies a winning rule's context action to a persistent stack, returning the new stack or the failure cause. */
static LLexerError l_peg_stack_apply(LPegProcess *process, uint32 stack, const LLexerTokenRule *rule, uint32 *out_stack) {
  const LPegStackFrame *frame;
  LPegStackFrame       *created;
  uint32                index;
  uint32                popped;

  frame      = (const LPegStackFrame *)nexus_data_chunked_list_at(&process->stack_frames, stack);
  *out_stack = stack;

  switch (rule->action_kind) {
  case LLTCAK_NONE:
    return LLE_NONE;
  case LLTCAK_PUSH:
    if (frame->depth >= LLEXER_CONTEXT_DEPTH_MAX) {
      return LLE_CONTEXT_OVERFLOW;
    }

    index            = (uint32)nexus_data_chunked_list_length_get(&process->stack_frames);
    created          = (LPegStackFrame *)nexus_data_chunked_list_append_uninitialized(&process->stack_frames);
    created->context = rule->action_data.push_context;
    created->parent  = stack;
    created->depth   = frame->depth + 1;
    *out_stack       = index;

    return LLE_NONE;
  case LLTCAK_POP:
    if (rule->action_data.pop_amount >= frame->depth) {
      return LLE_CONTEXT_UNDERFLOW;
    }

    index = stack;

    for (popped = 0; popped < rule->action_data.pop_amount; popped++) {
      index = ((const LPegStackFrame *)nexus_data_chunked_list_at(&process->stack_frames, index))->parent;
    }

    *out_stack = index;

    return LLE_NONE;
  case LLTCAK_SET:
    *out_stack = 0;

    if (rule->action_data.set_context != l_peg_context(process, 0)) {
      index            = (uint32)nexus_data_chunked_list_length_get(&process->stack_frames);
      created          = (LPegStackFrame *)nexus_data_chunked_list_append_uninitialized(&process->stack_frames);
      created->context = rule->action_data.set_context;
      created->parent  = 0;
      created->depth   = 2;
      *out_stack       = index;
    }

    return LLE_NONE;
  case LLTCAK_REPLACE:
    if (frame->depth < 2) {
      return LLE_CONTEXT_UNDERFLOW;
    }

    index            = (uint32)nexus_data_chunked_list_length_get(&process->stack_frames);
    created          = (LPegStackFrame *)nexus_data_chunked_list_append_uninitialized(&process->stack_frames);
    created->context = rule->action_data.replace_context;
    created->parent  = frame->parent;
    created->depth   = frame->depth;
    *out_stack       = index;

    return LLE_NONE;
  case LLTCAK_COUNT:
  default:
    NEXUS_PROGRAM_PANIC("invalid context action kind");
  }

  return LLE_NONE;
}

/* Summary: the lexer subset of an eligible set in a context, compiled on first use; NULL lexes with every rule of the context. */
#if defined(__GNUC__) || defined(__clang__)
#  define L_PEG_COLD __attribute__((noinline))
#else
#  define L_PEG_COLD
#endif

/* Summary: builds the lexer subset of (eligible set, context) the first time it is needed; kept out of line, as its frame is large and it runs once per pair. */
L_PEG_COLD static const LLexerSubset *l_peg_subset_build(LPegProcess *process, uint32 set, LLexerContext context, uint_large slot) {
  LPegProgram     *program;
  LLexerTokenKind  kinds[256];
  LLexerTokenKind *list;
  const uint64    *bits;
  uint32           words;
  uint32           count;
  uint32           kind;

  program = process->program;
  words   = program->set_words;
  bits    = (const uint64 *)nexus_data_list_at_const(program->sets, (uint_large)set * words);
  list    = kinds;
  count   = 0;

  for (kind = L_LEXER_TOKEN_KIND_FIRST_USER; kind < words * 64 && count < NEXUS_ARRAY_SIZE_ELEMENTS(kinds); kind++) {
    if ((bits[kind / 64] & ((uint64)1 << (kind % 64))) != 0) {
      list[count++] = kind;
    }
  }

  program->subsets[slot]      = logos_language_tooling_lexing_lexer_subset_create(process->parser->lexer, context, list, count);
  program->subset_built[slot] = TRUE;

  return program->subsets[slot];
}

static const LLexerSubset *l_peg_subset(LPegProcess *process, uint32 set, LLexerContext context) {
  LPegProgram *program;
  uint_large   slot;

  if (set == (uint32)LPARSER_INDEX_NONE || !process->program->grammar_expected) {
    return NULL;
  }

  program = process->program;
  slot    = ((uint_large)set * process->context_count) + context;

  if (!program->subset_built[slot]) {
    return l_peg_subset_build(process, set, context, slot);
  }

  return program->subsets[slot];
}

/* Summary: the token at the state's position in its context, cached because backtracking lexes one position many times. */
static const LPegLexEntry *l_peg_lex_miss(LPegProcess *process, const LPegState *state, uint32 set) {
  LPegLexEntry *entry;
  uint_large    slot;

  slot  = (uint_large)((state->pos * 31U + (uint_large)state->stack * 131U + (uint_large)set * 17U) % LPEG_CACHE_SIZE);
  entry = &process->cache[slot];

  if (entry->valid && entry->position == state->pos && entry->stack == state->stack && entry->set == set) {
    process->last_lex = entry;

    return entry;
  }

  entry->position = state->pos;
  entry->stack    = state->stack;
  entry->set      = set;
  entry->valid    = TRUE;
  entry->status = logos_language_tooling_lexing_lexer_match(process->parser->lexer, l_peg_subset(process, set, l_peg_context(process, state->stack)),
                                                            l_peg_context(process, state->stack), process->tree->source, process->tree->source_size,
                                                            state->pos, &entry->token, &entry->rule, &entry->error);
  process->last_lex = entry;

  return entry;
}

/* Summary: the token at the state's position, with the eligible set a node asks for (the set only matters with grammar_expected). */
static const LPegLexEntry *l_peg_lex(LPegProcess *process, const LPegState *state, uint32 set) {
  const LPegLexEntry *last;

  if (!process->program->grammar_expected) {
    set = (uint32)LPARSER_INDEX_NONE;
  }

  /* A token is asked for several times in a row (the boundary, the peek of a choice, the match), so the latest answer is tried before the hash. */
  last = process->last_lex;

  if (last != NULL && last->position == state->pos && last->stack == state->stack && last->set == set) {
    return last;
  }

  return l_peg_lex_miss(process, state, set);
}

/* Summary: records that a token was expected at a position, keeping only the farthest failure. */
static void l_peg_expect(LPegProcess *process, uint_large position, LLexerTokenKind kind, LLexerError error) {
  uint32 i;

  if (!process->far_valid || position > process->far_position) {
    process->far_valid    = TRUE;
    process->far_position = position;
    process->far_count    = 0;
    process->far_error    = error;
  }

  if (position != process->far_position) {
    return;
  }

  for (i = 0; i < process->far_count; i++) {
    if (process->far_kinds[i] == kind) {
      return;
    }
  }

  if (process->far_count < LPEG_FAR_KINDS_MAX) {
    process->far_kinds[process->far_count] = kind;
    process->far_count++;
  }
}

/* ---------------------------------------------------------------------------- */
/* EVALUATION                                                                    */
/* ---------------------------------------------------------------------------- */

static boolean l_peg_eval(LPegProcess *process, LPegState *state, uint32 node, uint32 *out);

/* Summary: l_peg_eval, with the two commonest children (a token and a rule) matched here so that they cost no evaluator call. */
static boolean l_peg_eval_child(LPegProcess *process, LPegState *state, uint32 node_index, uint32 *out);

static uint64 l_peg_all_globals(const LPegProgram *program) {
  return program->global_count >= 64 ? ~(uint64)0 : (((uint64)1 << program->global_count) - 1);
}

static boolean l_peg_rule(LPegProcess *process, LPegState *state, uint32 rule_index, uint32 *out) {
  const LPegRule *rule;
  uint_large      start;
  uint32          child;
  uint32          node;
  boolean         ok;

  if (process->depth >= process->program->depth_limit) {
    process->limited = TRUE;

    return FALSE;
  }

  rule  = l_peg_program_rule(process->program, rule_index);
  start = state->pos;
  child = (uint32)LPARSER_INDEX_NONE;

  process->depth++;
  ok = l_peg_eval_child(process, state, rule->body, &child);
  process->depth--;

  if (!ok) {
    return FALSE;
  }

  node = l_peg_node_packed(process, LPGK_RULE, state->pos - start, rule->symbol, child, (uint32)LPARSER_INDEX_NONE);

  if (node == LPARSER_INDEX_NONE) {
    return FALSE;
  }

  *out = node;

  return !process->limited;
}

/* Summary: consumes every global rule that matches at the state's position, repeatedly, as one trivia node. */
/*
Summary: whether no global rule can match at the state's boundary, so that there is nothing to try.
Description: the next token is lexed once (and cached for the token that is matched next); when it cannot begin any global rule, trying them would
  cost a checkpoint, a rule evaluation and a rollback at every token. A lexical error is left to the full path, which reports it.
*/
static boolean l_peg_globals_idle(LPegProcess *process, const LPegState *state) {
  const LPegProgram  *program;
  const LPegLexEntry *next;
  uint32              words;
  boolean             possible;
  uint64              all;

  program = process->program;
  all     = l_peg_all_globals(program);

  if (program->global_count == 0 || (state->excluded & all) == all) {
    return TRUE;
  }

  if (!process->fast || program->global_first == NULL || state->excluded != 0) {
    return FALSE;
  }

  /* With grammar_expected the boundary is lexed with just the tokens of the global rules: a token there means a global rule may match, none means none can. */
  words = program->grammar_expected ? program->set_words : program->first_words;
  next  = l_peg_lex(process, state, program->grammar_expected ? program->global_set : (uint32)LPARSER_INDEX_NONE);

  possible = next->status == LLS_TOKEN && next->token.kind >= L_LEXER_TOKEN_KIND_FIRST_USER && (uint_large)(next->token.kind / 64) < words &&
             (program->global_first[next->token.kind / 64] & ((uint64)1 << (next->token.kind % 64))) != 0;

  return !possible && (next->status == LLS_TOKEN || program->grammar_expected);
}

/* Summary: the global rules that match at the boundary, one after the other until none does, as one trivia node; the caller has checked that some may. */
static boolean l_det_globals_run(LPegProcess *process, LPegState *state, uint32 *out);

static boolean l_peg_globals_run(LPegProcess *process, LPegState *state, uint32 *out) {
  const LPegProgram *program;
  LPegCheckpoint     checkpoint;
  uint64             all;
  uint64             saved;
  uint32             result;
  uint32             global;
  boolean            progressed;

  if (process->det) {
    return l_det_globals_run(process, state, out);
  }

  program = process->program;
  *out    = (uint32)LPARSER_INDEX_NONE;
  all     = l_peg_all_globals(program);

  do {
    progressed = FALSE;

    for (global = 0; global < program->global_count; global++) {
      if ((state->excluded & ((uint64)1 << global)) != 0) {
        continue;
      }

      checkpoint      = l_peg_checkpoint(process, state);
      saved           = state->excluded;
      state->excluded = all; /* A global rule is not injected into itself or its siblings. */
      result          = (uint32)LPARSER_INDEX_NONE;

      if (l_peg_rule(process, state, program->globals[global], &result) && state->pos > checkpoint.state.pos) {
        state->excluded = saved;
        *out            = l_peg_sequence(process, *out, result);
        progressed      = TRUE;
      } else {
        l_peg_rollback(process, state, &checkpoint);
      }

      if (process->limited) {
        return FALSE;
      }
    }
  } while (progressed);

  return TRUE;
}

static boolean l_peg_globals(LPegProcess *process, LPegState *state, uint32 *out) {
  *out = (uint32)LPARSER_INDEX_NONE;

  if (l_peg_globals_idle(process, state)) {
    return TRUE;
  }

  return l_peg_globals_run(process, state, out);
}

/* Summary: matches one token. kind selects a token kind; mask, when not NULL, instead selects any one-byte token in the byte class. */
static boolean l_peg_token(LPegProcess *process, LPegState *state, LLexerTokenKind kind, const uint64 *mask, uint32 set, uint32 *out) {
  const LPegLexEntry *entry;
  LPegCheckpoint      checkpoint;
  LParsingGreenNode  *leaf_node;
  uint32              trivia;
  uint32              leaf;
  uint32              stack;
  LLexerError         action_error;
  boolean             matches;
  boolean             ran;

  /* A boundary with nothing to try consumes and builds nothing, so a failure needs no rollback: the checkpoint is taken only before the global rules run. */
  trivia = (uint32)LPARSER_INDEX_NONE;
  ran    = !l_peg_globals_idle(process, state);

  if (ran) {
    checkpoint = l_peg_checkpoint(process, state);

    if (!l_peg_globals_run(process, state, &trivia)) {
      l_peg_rollback(process, state, &checkpoint);

      return FALSE;
    }
  }

  entry   = l_peg_lex(process, state, set);
  matches = FALSE;

  if (entry->status == LLS_TOKEN) {
    if (mask != NULL) {
      matches = entry->token.kind >= L_LEXER_TOKEN_KIND_FIRST_USER && entry->token.lexeme_width == 1 &&
                logos_languages_regula_observation_mask_get(mask, process->tree->source[entry->token.lexeme_offset]);
    } else {
      matches = entry->token.kind == kind;
    }
  }

  if (!matches) {
    l_peg_expect(process, state->pos, kind, entry->status == LLS_ERROR ? entry->error : LLE_NONE);
    if (ran) {
      l_peg_rollback(process, state, &checkpoint);
    }

    return FALSE;
  }

  action_error = LLE_NONE;
  stack        = state->stack;

  if (entry->rule != NULL) {
    action_error = l_peg_stack_apply(process, state->stack, entry->rule, &stack);
  }

  if (action_error != LLE_NONE) {
    l_peg_expect(process, state->pos, kind, action_error);
    if (ran) {
      l_peg_rollback(process, state, &checkpoint);
    }

    return FALSE;
  }

  process->last_offset = entry->token.lexeme_offset;
  process->last_width  = entry->token.lexeme_width;
  leaf                 = l_peg_node_new(process, LPGK_TOKEN, entry->token.lexeme_width, (uint32)entry->token.lexeme_offset);

  if (leaf == LPARSER_INDEX_NONE) {
    if (ran) {
      l_peg_rollback(process, state, &checkpoint);
    }

    return FALSE;
  }

  leaf_node                     = (LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, leaf);
  leaf_node->payload.token_kind = entry->token.kind;
  l_peg_token_record(process, leaf);
  state->pos   = entry->token.lexeme_offset + entry->token.lexeme_width;
  state->stack = stack;
  *out         = l_peg_sequence(process, trivia, leaf);

  return !process->limited;
}

/* Summary: finds the operator of the expression whose token comes next, without consuming anything. */
static int32 l_peg_operator_peek(LPegProcess *process, LPegState *state, const LPegNode *node, boolean prefix) {
  const LPegOperator *operators;
  const LPegLexEntry *entry;
  LPegCheckpoint      checkpoint;
  uint32              trivia;
  uint32              i;
  int32               found;

  operators  = (const LPegOperator *)nexus_data_list_at_const(process->program->operators, node->b);
  checkpoint = l_peg_checkpoint(process, state);
  found      = -1;

  if (l_peg_globals(process, state, &trivia)) {
    entry = l_peg_lex(process, state, (uint32)node->mask);

    if (entry->status == LLS_TOKEN) {
      for (i = 0; i < node->count; i++) {
        if (operators[i].token_kind == entry->token.kind && (operators[i].kind == LCOP_PREFIX) == prefix) {
          found = (int32)i;
          break;
        }
      }
    }
  }

  l_peg_rollback(process, state, &checkpoint);

  return found;
}

static boolean l_peg_pratt(LPegProcess *process, LPegState *state, const LPegNode *node, int32 min_bp, uint32 *out) {
  const LPegOperator *operators;
  const LPegOperator *entry;
  uint32              left;
  uint32              operator_group;
  uint32              right;
  uint32              result;
  uint32              cover;
  int32               index;
  LPegCheckpoint      checkpoint;

  if (process->depth >= process->program->depth_limit) {
    process->limited = TRUE;

    return FALSE;
  }

  operators = (const LPegOperator *)nexus_data_list_at_const(process->program->operators, node->b);
  left      = (uint32)LPARSER_INDEX_NONE;
  index     = l_peg_operator_peek(process, state, node, TRUE);
  process->depth++;

  if (index >= 0) {
    entry = &operators[index];

    if (!l_peg_token(process, state, entry->token_kind, NULL, (uint32)node->mask, &operator_group) ||
        !l_peg_pratt(process, state, node, entry->right_bp, &right)) {
      process->depth--;

      return FALSE;
    }

    result = l_peg_node_new(process, LPGK_EXPRESSION, l_peg_width(process, operator_group) + l_peg_width(process, right), entry->symbol);

    if (result == LPARSER_INDEX_NONE) {
      process->depth--;

      return FALSE;
    }

    l_peg_alternative_add(process, result, operator_group, right);
    left = result;
  } else if (!l_peg_eval(process, state, node->a, &left)) {
    process->depth--;

    return FALSE;
  }

  for (;;) {
    index = l_peg_operator_peek(process, state, node, FALSE);

    if (index < 0) {
      break;
    }

    entry = &operators[index];

    if (entry->left_bp < min_bp) {
      break;
    }

    checkpoint = l_peg_checkpoint(process, state);

    if (!l_peg_token(process, state, entry->token_kind, NULL, (uint32)node->mask, &operator_group)) {
      l_peg_rollback(process, state, &checkpoint);
      break;
    }

    if (entry->kind == LCOP_INFIX) {
      if (!l_peg_pratt(process, state, node, entry->right_bp, &right)) {
        process->depth--;

        return FALSE;
      }

      cover  = l_peg_sequence(process, left, operator_group);
      result = l_peg_node_new(process, LPGK_EXPRESSION, l_peg_width(process, cover) + l_peg_width(process, right), entry->symbol);

      if (result == LPARSER_INDEX_NONE) {
        process->depth--;

        return FALSE;
      }

      l_peg_alternative_add(process, result, cover, right);
    } else {
      result = l_peg_node_new(process, LPGK_EXPRESSION, l_peg_width(process, left) + l_peg_width(process, operator_group), entry->symbol);

      if (result == LPARSER_INDEX_NONE) {
        process->depth--;

        return FALSE;
      }

      l_peg_alternative_add(process, result, left, operator_group);
    }

    left = result;
  }

  process->depth--;
  *out = left;

  return TRUE;
}

/* Summary: parses bytes [start, end) of the source with the guest parser of an embed. Guest problems never fail the host. */
static void l_peg_guest_parse(LPegProcess *process, uint32 embed_index, uint_large start, uint_large end) {
  const LPegEmbed *embed;
  LParserProcess  *guest;
  LParserStatus    status;
  LParserTree     *tree;
  NexusError      *error;

  embed = (const LPegEmbed *)nexus_data_list_at_const(process->program->embeds, embed_index);

  if (process->guests[embed_index] == NULL) {
    process->guests[embed_index] = logos_language_tooling_parsing_parser_process_create(embed->spec.guest);
  }

  guest = process->guests[embed_index];
  logos_language_tooling_parsing_parser_process_reset(guest);
  error = logos_language_tooling_parsing_parser_process_chunk_add(guest, process->tree->source + start, end - start, TRUE);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return;
  }

  do {
    status = logos_language_tooling_parsing_parser_process_parse(guest, LPARSER_TOKEN_BUDGET_UNBOUNDED);
  } while (status == LPS_PROGRESS);

  tree = logos_language_tooling_parsing_parser_process_tree_take(guest);
  logos_language_tooling_parsing_syntax_tree_destroy(tree);
}

/* Summary: the byte where the host closer begins at or after start, honouring the boundary constraint; (uint_large)-1 when there is none. */
static uint_large l_peg_region_end(LPegProcess *process, const LPegState *state, const LContextaEmbed *spec, uint_large start) {
  LLexerToken            token;
  const LLexerTokenRule *rule;
  LLexerError            error;
  uint_large             position;
  LLexerContext          context;

  context = l_peg_context(process, state->stack);

  for (position = start; position <= process->tree->source_size; position++) {
    if (spec->close_boundary == LCEB_LINE_START && position > 0 && process->tree->source[position - 1] != '\n') {
      continue;
    }

    if (logos_language_tooling_lexing_lexer_match(process->parser->lexer, NULL, context, process->tree->source, process->tree->source_size, position,
                                                  &token, &rule, &error) == LLS_TOKEN &&
        token.kind == spec->close_token) {
      return position;
    }
  }

  return (uint_large)-1;
}

static boolean l_peg_embed(LPegProcess *process, LPegState *state, const LPegNode *node, uint32 *out) {
  const LPegEmbed *embed;
  LPegCheckpoint   checkpoint;
  uint32           opened;
  uint32           closed;
  uint32           region;
  uint32           cover;
  uint32           result;
  uint_large       start;
  uint_large       end;

  embed      = (const LPegEmbed *)nexus_data_list_at_const(process->program->embeds, node->a);
  checkpoint = l_peg_checkpoint(process, state);
  opened     = (uint32)LPARSER_INDEX_NONE;
  closed     = (uint32)LPARSER_INDEX_NONE;
  region     = (uint32)LPARSER_INDEX_NONE;

  if (embed->spec.mode == LCEM_TOKEN) {
    if (!l_peg_token(process, state, embed->spec.source_token, NULL, (uint32)node->mask, &opened)) {
      l_peg_rollback(process, state, &checkpoint);

      return FALSE;
    }

    start = process->last_offset + embed->spec.prefix_bytes;
    end   = process->last_offset + process->last_width;
    end   = end >= embed->spec.suffix_bytes ? end - embed->spec.suffix_bytes : end;

    if (start <= end) {
      l_peg_guest_parse(process, node->a, start, end);
    }

    cover = opened;
  } else {
    if (!l_peg_token(process, state, embed->spec.open_token, NULL, (uint32)node->mask, &opened)) {
      l_peg_rollback(process, state, &checkpoint);

      return FALSE;
    }

    start = state->pos;
    end   = l_peg_region_end(process, state, &embed->spec, start);

    if (end == (uint_large)-1) {
      l_peg_expect(process, start, embed->spec.close_token, LLE_NONE);
      l_peg_rollback(process, state, &checkpoint);

      return FALSE;
    }

    if (end > start) {
      region = l_peg_node_new(process, LPGK_TOKEN, end - start, (uint32)start);

      if (region == LPARSER_INDEX_NONE) {
        l_peg_rollback(process, state, &checkpoint);

        return FALSE;
      }

      ((LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, region))->payload.token_kind = L_LEXER_TOKEN_KIND_ERROR;
      l_peg_token_record(process, region);
      l_peg_guest_parse(process, node->a, start, end);
    }

    state->pos = end;

    if (!l_peg_token(process, state, embed->spec.close_token, NULL, (uint32)LPARSER_INDEX_NONE, &closed)) {
      l_peg_rollback(process, state, &checkpoint);

      return FALSE;
    }

    cover = l_peg_sequence(process, l_peg_sequence(process, opened, region), closed);
  }

  result = l_peg_node_new(process, LPGK_EXPRESSION, l_peg_width(process, cover), node->symbol);

  if (result == LPARSER_INDEX_NONE) {
    l_peg_rollback(process, state, &checkpoint);

    return FALSE;
  }

  l_peg_alternative_add(process, result, cover, (uint32)LPARSER_INDEX_NONE);
  *out = result;

  return !process->limited;
}

/*
Summary: the kind of the next token, past the global rules, without consuming anything; cached for the state it is asked in.
Returns: the kind, or L_LEXER_TOKEN_KIND_ERROR when the lexer finds no token there (the alternatives then say why).
*/
static LLexerTokenKind l_peg_peek(LPegProcess *process, LPegState *state, uint_large *out_end) {
  LPegCheckpoint      checkpoint;
  const LPegLexEntry *entry;
  uint32              trivia;

  if (process->peek_valid && process->peek_position == state->pos && process->peek_stack == state->stack &&
      process->peek_excluded == state->excluded) {
    *out_end = process->peek_end;

    return process->peek_kind;
  }

  checkpoint = l_peg_checkpoint(process, state);

  process->peek_kind = L_LEXER_TOKEN_KIND_ERROR;
  process->peek_end  = state->pos;

  if (l_peg_globals(process, state, &trivia)) {
    entry              = l_peg_lex(process, state, (uint32)LPARSER_INDEX_NONE);
    process->peek_end  = state->pos;
    process->peek_kind = entry->status == LLS_TOKEN ? entry->token.kind : L_LEXER_TOKEN_KIND_ERROR;
  }

  process->peek_valid    = TRUE;
  process->peek_position = checkpoint.state.pos;
  process->peek_stack    = checkpoint.state.stack;
  process->peek_excluded = checkpoint.state.excluded;
  l_peg_rollback(process, state, &checkpoint);
  *out_end = process->peek_end;

  return process->peek_kind;
}

/*
Summary: the state at the next token's start, past the global rules (trivia), without consuming anything; cached for the state it is asked in.
Returns: FALSE when the boundary cannot be told (the work limit was reached).
*/
static boolean l_peg_boundary(LPegProcess *process, LPegState *state, LPegState *out) {
  LPegCheckpoint checkpoint;
  uint32         trivia;
  boolean        ok;

  if (process->peek_valid && process->peek_position == state->pos && process->peek_stack == state->stack &&
      process->peek_excluded == state->excluded) {
    out->pos      = process->peek_end;
    out->stack    = process->peek_end_stack;
    out->excluded = state->excluded;

    return TRUE;
  }

  checkpoint = l_peg_checkpoint(process, state);
  ok         = l_peg_globals(process, state, &trivia);

  if (ok) {
    process->peek_valid     = TRUE;
    process->peek_position  = checkpoint.state.pos;
    process->peek_stack     = checkpoint.state.stack;
    process->peek_excluded  = checkpoint.state.excluded;
    process->peek_end       = state->pos;
    process->peek_end_stack = state->stack;
    process->peek_kind      = L_LEXER_TOKEN_KIND_ERROR;
    out->pos                = state->pos;
    out->stack              = state->stack;
    out->excluded           = state->excluded;
  }

  l_peg_rollback(process, state, &checkpoint);

  return ok;
}

/* Summary: with expected-dispatch, whether an alternative can be skipped because none of its first tokens is found at the boundary by the lexer. */
static boolean l_peg_alternative_probe_skips(LPegProcess *process, uint32 alternative, LPegState *boundary) {
  const LPegProgram *program;
  const LPegProbe   *probes;
  uint32             count;
  uint32             i;

  program = process->program;
  count   = program->probe_count[alternative];

  if (count == LPEG_PROBES_UNKNOWN) {
    return FALSE;
  }

  probes = (const LPegProbe *)nexus_data_list_at_const(program->probes, program->probe_start[alternative]);

  for (i = 0; i < count; i++) {
    const LPegLexEntry *entry;

    entry = l_peg_lex(process, boundary, probes[i].set);

    if (entry->status == LLS_TOKEN && entry->token.kind == probes[i].kind) {
      return FALSE;
    }
  }

  return TRUE;
}

static boolean l_peg_alternative_skips(const LPegProgram *program, uint32 alternative, LLexerTokenKind kind);

/* Summary: whether a repetition ends here without trying its body, because the body cannot begin with the next token. */
static boolean l_peg_loop_ends(LPegProcess *process, LPegState *state, const LPegNode *node) {
  LPegState boundary;

  if (!process->fast) {
    return FALSE;
  }

  if (node->dispatch == 2) {
    return process->program->probes != NULL && l_peg_boundary(process, state, &boundary) &&
           l_peg_alternative_probe_skips(process, node->a, &boundary);
  }

  if (process->program->first != NULL) {
    uint_large      end;
    LLexerTokenKind kind;

    kind = l_peg_peek(process, state, &end);

    return l_peg_alternative_skips(process->program, node->a, kind);
  }

  return FALSE;
}

/* Summary: after every alternative failed, records the first tokens the skipped alternatives would have wanted at the boundary, for the diagnostic. */
static void l_peg_probe_skipped_expect(LPegProcess *process, const uint32 *alternatives, uint32 count, LPegState *boundary) {
  const LPegProgram *program;
  uint32             i;
  uint32             k;

  program = process->program;

  if (process->far_valid && boundary->pos < process->far_position) {
    return;
  }

  for (i = 0; i < count; i++) {
    const LPegProbe *probes;

    if (program->probe_count[alternatives[i]] == LPEG_PROBES_UNKNOWN || !l_peg_alternative_probe_skips(process, alternatives[i], boundary)) {
      continue;
    }

    probes = (const LPegProbe *)nexus_data_list_at_const(program->probes, program->probe_start[alternatives[i]]);

    for (k = 0; k < program->probe_count[alternatives[i]]; k++) {
      l_peg_expect(process, boundary->pos, probes[k].kind, LLE_NONE);
    }
  }
}

/* Summary: whether an alternative can be skipped because it cannot begin with the next token. */
static boolean l_peg_alternative_skips(const LPegProgram *program, uint32 alternative, LLexerTokenKind kind) {
  const uint64 *first;

  if (program->first == NULL || kind == L_LEXER_TOKEN_KIND_ERROR || program->nullable[alternative] || kind / 64 >= program->first_words) {
    return FALSE;
  }

  first = program->first + ((uint_large)alternative * program->first_words);

  return (first[kind / 64] & ((uint64)1 << (kind % 64))) == 0;
}

/* Summary: after every alternative failed, records the tokens the skipped ones would have wanted, for the diagnostic. */
static void l_peg_skipped_expect(LPegProcess *process, const uint32 *alternatives, uint32 count, LLexerTokenKind kind, uint_large position) {
  const LPegProgram *program;
  uint32             i;
  uint32             word;
  uint32             bit;

  program = process->program;

  if (process->far_valid && position < process->far_position) {
    return;
  }

  for (i = 0; i < count; i++) {
    if (!l_peg_alternative_skips(program, alternatives[i], kind)) {
      continue;
    }

    for (word = 0; word < program->first_words; word++) {
      uint64 bits;

      bits = program->first[((uint_large)alternatives[i] * program->first_words) + word];

      for (bit = 0; bits != 0; bit++, bits >>= 1) {
        if ((bits & 1U) != 0 && (word * 64) + bit >= L_LEXER_TOKEN_KIND_FIRST_USER) {
          l_peg_expect(process, position, ((LLexerTokenKind)word * 64U) + bit, LLE_NONE);
        }
      }
    }
  }
}

static boolean l_peg_choice(LPegProcess *process, LPegState *state, const LPegNode *node, uint32 *out) {
  const uint32   *alternatives;
  LPegCheckpoint  checkpoint;
  uint32          result;
  uint32          best;
  uint32          successes;
  uint_large      best_end;
  uint32          i;
  LLexerTokenKind kind;
  uint_large      position;
  boolean         skipped;
  boolean         probing;
  LPegState       boundary;

  alternatives = l_peg_program_list(process->program, node->a);
  checkpoint   = l_peg_checkpoint(process, state);
  kind = process->program->first != NULL && node->dispatch == 1 && process->fast ? l_peg_peek(process, state, &position) : L_LEXER_TOKEN_KIND_ERROR;
  position = state->pos;
  skipped  = FALSE;
  probing  = node->dispatch == 2 && process->fast && process->program->probes != NULL && l_peg_boundary(process, state, &boundary);

  if (node->mode == LCCM_ORDERED) {
    for (i = 0; i < node->count; i++) {
      result = (uint32)LPARSER_INDEX_NONE;

      if (probing ? l_peg_alternative_probe_skips(process, alternatives[i], &boundary)
                  : l_peg_alternative_skips(process->program, alternatives[i], kind)) {
        skipped = TRUE;
        continue;
      }

      if (l_peg_eval_child(process, state, alternatives[i], &result)) {
        *out = result;

        return TRUE;
      }

      l_peg_rollback(process, state, &checkpoint);

      if (process->limited) {
        return FALSE;
      }
    }

    if (skipped) {
      if (probing) {
        l_peg_probe_skipped_expect(process, alternatives, node->count, &boundary);
      } else {
        l_peg_skipped_expect(process, alternatives, node->count, kind, position);
      }
    }

    return FALSE;
  }

  /* Longest and unambiguous choice evaluate every alternative from the same checkpoint, then redo the winner. */
  best      = (uint32)LPARSER_INDEX_NONE;
  best_end  = 0;
  successes = 0;

  for (i = 0; i < node->count; i++) {
    result = (uint32)LPARSER_INDEX_NONE;

    if (probing ? l_peg_alternative_probe_skips(process, alternatives[i], &boundary)
                : l_peg_alternative_skips(process->program, alternatives[i], kind)) {
      skipped = TRUE;
      continue;
    }

    if (l_peg_eval_child(process, state, alternatives[i], &result)) {
      successes++;

      if (best == LPARSER_INDEX_NONE || (node->mode == LCCM_LONGEST && state->pos > best_end)) {
        best     = i;
        best_end = state->pos;
      }
    }

    l_peg_rollback(process, state, &checkpoint);

    if (process->limited) {
      return FALSE;
    }
  }

  if (best == LPARSER_INDEX_NONE) {
    if (skipped) {
      if (probing) {
        l_peg_probe_skipped_expect(process, alternatives, node->count, &boundary);
      } else {
        l_peg_skipped_expect(process, alternatives, node->count, kind, position);
      }
    }

    return FALSE;
  }

  if (node->mode == LCCM_UNAMBIGUOUS && successes > 1) {
    if (process->ambiguity_count < 8) {
      process->ambiguity[process->ambiguity_count] = checkpoint.state.pos;
      process->ambiguity_count++;
    }

    return FALSE;
  }

  result = (uint32)LPARSER_INDEX_NONE;

  if (!l_peg_eval(process, state, alternatives[best], &result)) {
    return FALSE;
  }

  *out = result;

  return TRUE;
}

/* ---------------------------------------------------------------------------- */
/* THE DETERMINISTIC RUN                                                         */
/* ---------------------------------------------------------------------------- */

/*
The first parse of a document is optimistic: a choice takes the first alternative the next token allows, a repetition goes on while the next token
begins its body, and the first failure ends the run, which the backtracking engine then repeats in full. What the run builds is what the backtracking
engine builds when it succeeds, since an alternative that the next token does not allow fails at its first token there as well. It keeps no
checkpoints and no record of failures, and looks at the token after the trivia by looking at tokens alone (the global rules are single tokens).
*/

/* Summary: the token kinds of the global rules that are not excluded. */
static const uint64 *l_det_trivia_kinds(LPegProcess *process, uint64 excluded) {
  const LPegProgram *program;
  uint32             global;
  uint32             word;

  program = process->program;

  if (excluded != process->det_union_mask) {
    nexus_memory_bytes_clear(process->det_union, (uint_large)program->first_words * NEXUS_SIZEOF(uint64));

    for (global = 0; global < program->global_count; global++) {
      if ((excluded & ((uint64)1 << global)) == 0) {
        for (word = 0; word < program->first_words; word++) {
          process->det_union[word] |= program->global_kinds[((uint_large)global * program->first_words) + word];
        }
      }
    }

    process->det_union_mask = excluded;
  }

  return process->det_union;
}

static boolean l_det_is_trivia(const uint64 *kinds, uint32 words, LLexerTokenKind kind) {
  return kind >= L_LEXER_TOKEN_KIND_FIRST_USER && kind / 64 < words && (kinds[kind / 64] & ((uint64)1 << (kind % 64))) != 0;
}

/*
Summary: the kind of the next token after the trivia, without consuming anything: the trivia tokens are lexed over.
Returns: the kind, or L_LEXER_TOKEN_KIND_ERROR when the lexer finds no token there.
*/
static LLexerTokenKind l_det_next(LPegProcess *process, const LPegState *state) {
  const LPegProgram  *program;
  const LPegLexEntry *entry;
  const uint64       *kinds;
  LPegState           probe;

  program = process->program;
  probe   = *state;
  kinds   = program->global_count > 0 ? l_det_trivia_kinds(process, state->excluded) : NULL;

  for (;;) {
    entry = l_peg_lex(process, &probe, (uint32)LPARSER_INDEX_NONE);

    if (entry->status != LLS_TOKEN) {
      return L_LEXER_TOKEN_KIND_ERROR;
    }

    if (kinds == NULL || !l_det_is_trivia(kinds, program->first_words, entry->token.kind)) {
      return entry->token.kind;
    }

    probe.pos += entry->token.lexeme_width;
  }
}

/* Summary: the global rules that match at the boundary, as the backtracking engine builds them, but ending the run when one does not. */
static boolean l_det_globals_run(LPegProcess *process, LPegState *state, uint32 *out) {
  const LPegProgram *program;
  uint64             all;
  uint64             saved;
  uint32             result;
  uint32             global;
  uint_large         start;
  boolean            progressed;

  program = process->program;
  *out    = (uint32)LPARSER_INDEX_NONE;
  all     = l_peg_all_globals(program);

  do {
    progressed = FALSE;

    for (global = 0; global < program->global_count; global++) {
      const LPegLexEntry *entry;
      const uint64       *kinds;

      if ((state->excluded & ((uint64)1 << global)) != 0) {
        continue;
      }

      entry = l_peg_lex(process, state, (uint32)LPARSER_INDEX_NONE);
      kinds = program->global_kinds + ((uint_large)global * program->first_words);

      if (entry->status != LLS_TOKEN || !l_det_is_trivia(kinds, program->first_words, entry->token.kind)) {
        continue;
      }

      start           = state->pos;
      saved           = state->excluded;
      state->excluded = all;
      result          = (uint32)LPARSER_INDEX_NONE;

      if (!l_peg_rule(process, state, program->globals[global], &result) || state->pos == start) {
        return FALSE;
      }

      state->excluded = saved;
      *out            = l_peg_sequence(process, *out, result);
      progressed      = TRUE;
    }
  } while (progressed);

  return TRUE;
}

static boolean l_det_choice(LPegProcess *process, LPegState *state, const LPegNode *node, uint32 *out) {
  const uint32   *alternatives;
  LLexerTokenKind kind;
  uint32          i;

  alternatives = l_peg_program_list(process->program, node->a);
  kind         = l_det_next(process, state);

  for (i = 0; i < node->count; i++) {
    uint32 result;

    if (l_peg_alternative_skips(process->program, alternatives[i], kind)) {
      continue;
    }

    result = (uint32)LPARSER_INDEX_NONE;

    if (!l_peg_eval_child(process, state, alternatives[i], &result)) {
      return FALSE;
    }

    *out = result;

    return TRUE;
  }

  return FALSE;
}

static boolean l_det_star(LPegProcess *process, LPegState *state, const LPegNode *node, uint32 *out) {
  uint32 accumulated;

  accumulated = (uint32)LPARSER_INDEX_NONE;

  for (;;) {
    uint32     iteration;
    uint_large start;

    if (l_peg_alternative_skips(process->program, node->a, l_det_next(process, state))) {
      break;
    }

    start     = state->pos;
    iteration = (uint32)LPARSER_INDEX_NONE;

    if (!l_peg_eval_child(process, state, node->a, &iteration) || state->pos == start) {
      return FALSE;
    }

    accumulated = l_peg_sequence(process, accumulated, iteration);

    if (process->limited) {
      return FALSE;
    }
  }

  *out = accumulated;

  return !process->limited;
}

static boolean l_peg_eval_child(LPegProcess *process, LPegState *state, uint32 node_index, uint32 *out) {
  const LPegNode *node;

  node = l_peg_program_node(process->program, node_index);

  if (node->kind == LPN_TOKEN) {
    *out = (uint32)LPARSER_INDEX_NONE;

    return l_peg_token(process, state, node->token_kind, NULL, node->a, out);
  }

  if (node->kind == LPN_RULE) {
    *out = (uint32)LPARSER_INDEX_NONE;

    return l_peg_rule(process, state, node->a, out);
  }

  return l_peg_eval(process, state, node_index, out);
}

/* Summary: a repetition: its body until it fails or makes no progress, left-nested as one sequence. Out of line, so that the common cases of the evaluator keep a small frame. */
L_PEG_COLD static boolean l_peg_star(LPegProcess *process, LPegState *state, const LPegNode *node, uint32 *out) {
  LPegCheckpoint checkpoint;
  uint32         accumulated;
  uint32         iteration;

  accumulated = (uint32)LPARSER_INDEX_NONE;

  for (;;) {
    if (node->dispatch != 0 && l_peg_loop_ends(process, state, node)) {
      break;
    }

    checkpoint = l_peg_checkpoint(process, state);
    iteration  = (uint32)LPARSER_INDEX_NONE;

    if (!l_peg_eval_child(process, state, node->a, &iteration) || state->pos == checkpoint.state.pos) {
      l_peg_rollback(process, state, &checkpoint);

      break;
    }

    accumulated = l_peg_sequence(process, accumulated, iteration);

    if (process->limited) {
      return FALSE;
    }
  }

  *out = accumulated;

  return !process->limited;
}

/* Summary: an expression in which the global rules do not apply at the boundaries inside. */
L_PEG_COLD static boolean l_peg_exclude(LPegProcess *process, LPegState *state, const LPegNode *node, uint32 *out) {
  uint64  saved;
  uint32  first;
  uint32  second;
  boolean ok;

  /* The boundary before the first token belongs to the caller; only the boundaries inside are suppressed. */
  if (!l_peg_globals(process, state, &first)) {
    return FALSE;
  }

  saved           = state->excluded;
  state->excluded = saved | node->mask;
  second          = (uint32)LPARSER_INDEX_NONE;
  ok              = l_peg_eval(process, state, node->a, &second);
  state->excluded = saved;

  if (!ok) {
    return FALSE;
  }

  *out = l_peg_sequence(process, first, second);

  return !process->limited;
}

static boolean l_peg_eval(LPegProcess *process, LPegState *state, uint32 node_index, uint32 *out) {
  const LPegNode *node;

  node = l_peg_program_node(process->program, node_index);
  *out = (uint32)LPARSER_INDEX_NONE;

  switch (node->kind) {
  case LPN_EMPTY:
    return TRUE;
  case LPN_FAIL:
    return FALSE;
  case LPN_TOKEN:
    return l_peg_token(process, state, node->token_kind, NULL, node->a, out);
  case LPN_SYMBOL:
    return l_peg_token(process, state, L_LEXER_TOKEN_KIND_ERROR, (const uint64 *)nexus_data_list_at_const(process->program->masks, node->b), node->a,
                       out);
  case LPN_SEQ: {
    uint32 first;
    uint32 second;

    first  = (uint32)LPARSER_INDEX_NONE;
    second = (uint32)LPARSER_INDEX_NONE;

    if (!l_peg_eval_child(process, state, node->a, &first) || !l_peg_eval_child(process, state, node->b, &second)) {
      return FALSE;
    }

    *out = l_peg_sequence(process, first, second);

    return !process->limited;
  }
  case LPN_CHOICE:
    return process->det ? l_det_choice(process, state, node, out) : l_peg_choice(process, state, node, out);
  case LPN_STAR:
    return process->det ? l_det_star(process, state, node, out) : l_peg_star(process, state, node, out);
  case LPN_RULE:
    return l_peg_rule(process, state, node->a, out);
  case LPN_EXCLUDE:
    return l_peg_exclude(process, state, node, out);
  case LPN_OPERATORS:
    return l_peg_pratt(process, state, node, (int32)(-2147483647 - 1), out);
  case LPN_EMBED:
    return l_peg_embed(process, state, node, out);
  default:
    NEXUS_PROGRAM_PANIC("invalid backtracking node kind");
  }

  return FALSE;
}

/* ---------------------------------------------------------------------------- */
/* DOCUMENT AND DIAGNOSTICS                                                      */
/* ---------------------------------------------------------------------------- */

static void l_peg_diagnostic_add(LPegProcess *process, LParserDiagnosticKind kind, uint_large offset, uint_large width, LLexerTokenKind actual,
                                 LLexerError lexical_error, const LLexerTokenKind *expected, uint32 expected_count) {
  LParsingDiagnostic     diagnostic;
  const LParsingSymbol  *symbol;
  const LPegTokenSymbol *token_symbol;
  uint32                 i;
  uint_large             j;

  if (nexus_data_slabbed_list_length_get(process->tree->diagnostics) >= process->parser->limits.max_diagnostics) {
    return;
  }

  diagnostic.view.kind                = kind;
  diagnostic.view.byte_offset         = offset;
  diagnostic.view.byte_width          = width;
  diagnostic.view.actual_kind         = actual;
  diagnostic.view.lexical_error       = lexical_error;
  diagnostic.view.expected_count      = 0;
  diagnostic.view.missing_token_count = 0;
  diagnostic.expected_begin           = nexus_data_slabbed_list_length_get(process->tree->expectations);

  for (i = 0; i < expected_count; i++) {
    for (j = 0; j < nexus_data_list_length_get(process->program->token_symbols); j++) {
      token_symbol = (const LPegTokenSymbol *)nexus_data_list_at_const(process->program->token_symbols, j);

      if (token_symbol->kind == expected[i]) {
        symbol = (const LParsingSymbol *)nexus_data_list_at_const(process->parser->symbols, token_symbol->symbol);
        (void)nexus_data_slabbed_list_append(process->tree->expectations, &symbol->terminal);
        diagnostic.view.expected_count++;

        break;
      }
    }
  }

  (void)nexus_data_slabbed_list_append(process->tree->diagnostics, &diagnostic);
}

/* Summary: fills the tree's token table with the leaves of the finished tree, in source order. */
static void l_peg_tokens_collect(LPegProcess *process, NexusDataChunkedList *table) {
  NexusDataList             *pending;
  const LParsingGreenNode   *node;
  const LParsingAlternative *alternative;
  LParserToken               record;
  uint32                     index;

  pending = nexus_data_list_create(NEXUS_SIZEOF(uint32), 64);
  index   = (uint32)process->tree->root;
  (void)nexus_data_list_append(pending, &index);

  while (nexus_data_list_pop(pending, &index)) {
    node = (const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, index);

    if (node->kind == LPGK_TOKEN) {
      record.kind   = (uint32)node->payload.token_kind;
      record.offset = node->aux;
      record.width  = node->width;
      record.error  = node->lexical_error;
      record.leaf   = index;
      (void)nexus_data_chunked_list_append(table, &record);

      continue;
    }

    if (node->first_alternative == LPARSER_INDEX_NONE) {
      continue;
    }

    alternative = (const LParsingAlternative *)nexus_data_chunked_list_at(&process->tree->alternatives, node->first_alternative);

    /* Children are pushed right to left so the left one is visited first. */
    if (alternative->children[1] != LPARSER_INDEX_NONE) {
      index = alternative->children[1];
      (void)nexus_data_list_append(pending, &index);
    }

    if (alternative->children[0] != LPARSER_INDEX_NONE) {
      index = alternative->children[0];
      (void)nexus_data_list_append(pending, &index);
    }
  }

  nexus_data_list_destroy(pending);
}

/* Summary: in a developer build, checks that the token table made while parsing is the one a walk of the finished tree gives. */
static void l_peg_tokens_verify(LPegProcess *process) {
#if NEXUS_DEVELOPER_MODE
  NexusDataChunkedList walked;
  uint_large           i;

  nexus_data_chunked_list_initialize(&walked, NEXUS_SIZEOF(LParserToken), 12);
  l_peg_tokens_collect(process, &walked);
  NEXUS_PROGRAM_INVARIANT(nexus_data_chunked_list_length_get(&walked) == nexus_data_chunked_list_length_get(&process->tree->tokens),
                          "the recorded tokens must be those of the tree");

  for (i = 0; i < nexus_data_chunked_list_length_get(&walked); i++) {
    const LParserToken *left;
    const LParserToken *right;

    left  = (const LParserToken *)nexus_data_chunked_list_at(&walked, i);
    right = (const LParserToken *)nexus_data_chunked_list_at(&process->tree->tokens, i);
    NEXUS_PROGRAM_INVARIANT(left->leaf == right->leaf && left->kind == right->kind && left->offset == right->offset && left->width == right->width &&
                                left->error == right->error,
                            "a recorded token must equal the one in the tree");
  }

  nexus_data_chunked_list_release(&walked);
#else
  (void)process;
#endif
}

/*
Summary: parses the document once. With may_retry, a failure builds nothing and returns FALSE, so that the caller can run again without the optimizations,
  which skip alternatives and so leave out some of the tokens a failure would report as expected: the failure of a document is always reported by the plain run.
*/
static boolean l_peg_parse_once(LPegProcess *process, boolean may_retry) {
  LPegState           state;
  const LPegLexEntry *entry;
  uint32              content;
  uint32              trailing;
  uint32              eof;
  uint32              cover;
  uint32              root;
  uint32              unparsed;
  uint32              i;
  boolean             ok;

  process->tree->root = LPARSER_INDEX_NONE;
  state.pos           = 0;
  state.stack         = 0;
  state.excluded      = 0;
  content             = (uint32)LPARSER_INDEX_NONE;
  trailing            = (uint32)LPARSER_INDEX_NONE;
  eof                 = (uint32)LPARSER_INDEX_NONE;
  ok                  = l_peg_rule(process, &state, process->program->entry_rule, &content);

  if (ok) {
    ok = l_peg_globals(process, &state, &trailing);
  }

  if (ok) {
    entry = l_peg_lex(process, &state, (uint32)LPARSER_INDEX_NONE);

    if (entry->status == LLS_TOKEN && entry->token.kind == L_LEXER_TOKEN_KIND_EOF) {
      eof = l_peg_node_new(process, LPGK_TOKEN, 0, (uint32)state.pos);

      if (eof != LPARSER_INDEX_NONE) {
        ((LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, eof))->payload.token_kind = L_LEXER_TOKEN_KIND_EOF;
        l_peg_token_record(process, eof);
      }
    } else {
      l_peg_expect(process, state.pos, L_LEXER_TOKEN_KIND_EOF, entry->status == LLS_ERROR ? entry->error : LLE_NONE);
      ok = FALSE;
    }
  }

  if (ok && !process->limited) {
    cover = l_peg_sequence(process, content, trailing);
    root  = l_peg_node_new(process, LPGK_SOURCE, process->tree->source_size, (uint32)LPARSER_INDEX_NONE);

    if (cover == LPARSER_INDEX_NONE) {
      l_peg_alternative_add(process, root, eof, (uint32)LPARSER_INDEX_NONE);
    } else {
      l_peg_alternative_add(process, root, cover, eof);
    }

    process->tree->root = root;

    l_peg_tokens_verify(process);

    process->status = LPS_ACCEPTED;

    return TRUE;
  }

  if (may_retry && !process->limited) {
    return FALSE;
  }

  /* Failure: the result keeps every source byte as one unparsed region under an error root, and says where the farthest strict failure was. */
  nexus_data_chunked_list_clear(&process->tree->nodes);
  nexus_data_chunked_list_clear(&process->tree->alternatives);
  nexus_data_slabbed_list_clear(process->tree->diagnostics);
  nexus_data_slabbed_list_clear(process->tree->expectations);

  for (i = 0; i < process->ambiguity_count; i++) {
    l_peg_diagnostic_add(process, LPDK_AMBIGUOUS, process->ambiguity[i], 0, L_LEXER_TOKEN_KIND_ERROR, LLE_NONE, NULL, 0);
  }

  if (process->limited) {
    l_peg_diagnostic_add(process, LPDK_LIMIT, process->far_valid ? process->far_position : 0, 0, L_LEXER_TOKEN_KIND_EOF, LLE_NONE, NULL, 0);
  } else if (process->far_valid) {
    state.pos      = process->far_position;
    state.stack    = 0;
    state.excluded = 0;
    entry          = l_peg_lex(process, &state, (uint32)LPARSER_INDEX_NONE);

    if (process->far_error != LLE_NONE) {
      l_peg_diagnostic_add(process, LPDK_LEXICAL, process->far_position, entry->token.lexeme_width, L_LEXER_TOKEN_KIND_ERROR, process->far_error,
                           process->far_kinds, process->far_count);
    } else if (entry->status == LLS_TOKEN && entry->token.kind == L_LEXER_TOKEN_KIND_EOF) {
      l_peg_diagnostic_add(process, LPDK_UNEXPECTED_EOF, process->far_position, 0, L_LEXER_TOKEN_KIND_EOF, LLE_NONE, process->far_kinds,
                           process->far_count);
    } else {
      l_peg_diagnostic_add(process, LPDK_UNEXPECTED_TOKEN, process->far_position, entry->token.lexeme_width, entry->token.kind, LLE_NONE,
                           process->far_kinds, process->far_count);
    }
  }

  root = l_peg_node_new(process, LPGK_ERROR, process->tree->source_size, (uint32)LPARSER_INDEX_NONE);

  if (process->tree->source_size > 0) {
    unparsed = l_peg_node_new(process, LPGK_UNPARSED, process->tree->source_size, 0);
    l_peg_alternative_add(process, root, unparsed, (uint32)LPARSER_INDEX_NONE);
  } else {
    l_peg_alternative_add(process, root, (uint32)LPARSER_INDEX_NONE, (uint32)LPARSER_INDEX_NONE);
  }

  process->tree->root = root;
  process->status     = process->limited ? LPS_LIMIT : LPS_REJECTED;

  return TRUE;
}

/* Summary: empties what a parse built, so that the document can be parsed again. */
static void l_peg_parse_reset(LPegProcess *process) {
  nexus_data_chunked_list_clear(&process->tree->nodes);
  nexus_data_chunked_list_clear(&process->tree->alternatives);
  nexus_data_chunked_list_clear(&process->tree->tokens);
  nexus_data_slabbed_list_clear(process->tree->diagnostics);
  nexus_data_slabbed_list_clear(process->tree->expectations);
  process->far_valid       = FALSE;
  process->far_count       = 0;
  process->ambiguity_count = 0;
  process->peek_valid      = FALSE;
  process->last_lex        = NULL;
  process->depth           = 0;
}

/*
Summary: parses the document in up to three runs: the deterministic one when the grammar allows it (the whole of the work for a document that fits it),
  the backtracking engine with its skipping, and, for a document that is rejected, the plain backtracking engine, whose diagnostic is the one reported.
*/
static void l_peg_parse_document(LPegProcess *process) {
  boolean deterministic;

  process->fast = TRUE;
  deterministic = process->program->det_ok && !process->program->det_disabled;

  if (deterministic) {
    process->det = TRUE;

    if (l_peg_parse_once(process, TRUE)) {
      process->det = FALSE;

      return;
    }

    process->det = FALSE;
    l_peg_parse_reset(process);
  }

  if (!l_peg_parse_once(process, TRUE)) {
    l_peg_parse_reset(process);
    process->fast = FALSE;
    (void)l_peg_parse_once(process, FALSE);
  } else if (deterministic) {
    /* The document is valid and the deterministic run gave up on it: the grammar needs the backtracking, so it is not tried again. */
    process->program->det_disabled = TRUE;
  }
}

/* ---------------------------------------------------------------------------- */
/* PROCESS LIFECYCLE                                                             */
/* ---------------------------------------------------------------------------- */

LParserProcess *logos_language_tooling_parsing_backtracking_process_create(LParser *parser) {
  LPegProcess *process;

  NEXUS_PROGRAM_PRECONDITION(parser != NULL && parser->engine == LPE_BACKTRACKING, "a backtracking process needs a backtracking parser");

  process          = (LPegProcess *)nexus_memory_allocate(NEXUS_SIZEOF(*process));
  process->parser  = parser;
  process->program = parser->peg;
  process->fast    = TRUE;
  process->det     = FALSE;
  process->det_union =
      (uint64 *)nexus_memory_allocate(((uint_large)(parser->peg->first_words > 0 ? parser->peg->first_words : 1)) * NEXUS_SIZEOF(uint64));
  process->det_union_mask = ~(uint64)0;
  process->tree           = NULL;
  process->peek_valid     = FALSE;
  process->last_lex       = NULL;
  process->cache          = (LPegLexEntry *)nexus_memory_allocate(LPEG_CACHE_SIZE * NEXUS_SIZEOF(LPegLexEntry));
  process->context_count  = nexus_data_list_length_get(parser->lexer->contexts);
  process->guests =
      (LParserProcess **)nexus_memory_allocate((nexus_data_list_length_get(parser->peg->embeds) + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
  nexus_memory_bytes_clear((void *)process->guests, (nexus_data_list_length_get(parser->peg->embeds) + 1) * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);

  if (parser->peg->grammar_expected && parser->peg->subsets == NULL) {
    LPegProgram *program;

    program                  = parser->peg;
    program->subset_contexts = process->context_count;
    program->subset_slots    = (nexus_data_list_length_get(program->sets) / program->set_words + 1) * process->context_count;
    program->subsets         = (LLexerSubset **)nexus_memory_allocate(program->subset_slots * NEXUS_EFFECTIVE_POINTER_SIZE_BYTES);
    program->subset_built    = (boolean *)nexus_memory_allocate(program->subset_slots * NEXUS_SIZEOF(boolean));
    nexus_memory_bytes_clear(program->subset_built, program->subset_slots * NEXUS_SIZEOF(boolean));
  }

  nexus_data_chunked_list_initialize(&process->stack_frames, NEXUS_SIZEOF(LPegStackFrame), 8);
  parser->process_count++;
  logos_language_tooling_parsing_backtracking_process_reset((LParserProcess *)process);

  return (LParserProcess *)process;
}

void logos_language_tooling_parsing_backtracking_process_destroy(LParserProcess *handle) {
  LPegProcess *process;

  process = (LPegProcess *)handle;

  if (process->tree != NULL) {
    logos_language_tooling_parsing_syntax_tree_destroy(process->tree);
  }

  {
    uint_large guest_index;

    for (guest_index = 0; guest_index < nexus_data_list_length_get(process->program->embeds); guest_index++) {
      if (process->guests[guest_index] != NULL) {
        logos_language_tooling_parsing_parser_process_destroy(process->guests[guest_index]);
      }
    }

    nexus_memory_deallocate((void *)process->guests);
  }

  nexus_data_chunked_list_release(&process->stack_frames);
  nexus_memory_deallocate(process->det_union);
  nexus_memory_deallocate(process->cache);
  process->parser->process_count--;
  nexus_memory_deallocate(process);
}

void logos_language_tooling_parsing_backtracking_process_reset(LParserProcess *handle) {
  LPegProcess    *process;
  LPegStackFrame *root;
  uint_large      i;

  process = (LPegProcess *)handle;

  if (process->tree == NULL) {
    process->tree = logos_language_tooling_parsing_parser_process_tree_create(process->parser);
  } else {
    logos_language_tooling_parsing_parser_process_tree_clear(process->tree);
  }

  for (i = 0; i < LPEG_CACHE_SIZE; i++) {
    process->cache[i].valid = FALSE;
  }

  nexus_data_chunked_list_clear(&process->stack_frames);
  root          = (LPegStackFrame *)nexus_data_chunked_list_append_uninitialized(&process->stack_frames);
  root->context = process->parser->lexer->root_context;
  root->parent  = (uint32)LPARSER_INDEX_NONE;
  root->depth   = 1;

  process->ready_for_chunk = TRUE;
  process->last_chunk      = FALSE;
  process->done            = FALSE;
  process->status          = LPS_NEED_INPUT;
  process->depth           = 0;
  process->limited         = FALSE;
  process->far_valid       = FALSE;
  process->peek_valid      = FALSE;
  process->last_lex        = NULL;
  process->fast            = TRUE;
  process->det             = FALSE;
  process->det_union_mask  = ~(uint64)0;
  process->far_position    = 0;
  process->far_count       = 0;
  process->far_error       = LLE_NONE;
  process->ambiguity_count = 0;
}

NexusError *logos_language_tooling_parsing_backtracking_process_chunk_add(LParserProcess *handle, const byte *chunk, uint_large chunk_size,
                                                                          boolean is_last_chunk) {
  LPegProcess *process;
  NexusError  *error;

  process = (LPegProcess *)handle;
  NEXUS_PROGRAM_PRECONDITION(process->tree != NULL && process->ready_for_chunk && !process->last_chunk && !process->done,
                             "input needs a reset process with no final chunk");

  error = logos_language_tooling_parsing_parser_process_source_append(process->tree, process->parser, chunk, chunk_size);

  if (error != NEXUS_PROGRAM_ERROR_SUCCESS()) {
    return error;
  }

  process->ready_for_chunk = FALSE;
  process->last_chunk      = is_last_chunk;

  return NEXUS_PROGRAM_ERROR_SUCCESS();
}

LParserStatus logos_language_tooling_parsing_backtracking_process_parse(LParserProcess *handle, uint_large token_budget) {
  LPegProcess *process;

  NEXUS_PROGRAM_PRECONDITION(token_budget > 0, "token budget must be positive");

  process = (LPegProcess *)handle;

  if (process->done) {
    return process->status;
  }

  NEXUS_PROGRAM_PRECONDITION(process->tree != NULL && !process->ready_for_chunk, "add input before parsing");

  /* Backtracking can need any amount of lookahead, so the source is parsed once its end is known; a partial source is never treated as complete. */
  if (!process->last_chunk) {
    process->ready_for_chunk = TRUE;
    process->status          = LPS_NEED_INPUT;

    return process->status;
  }

  l_peg_parse_document(process);
  process->tree->status = process->status;
  process->done         = TRUE;

  return process->status;
}

LParserTree *logos_language_tooling_parsing_backtracking_process_tree_take(LParserProcess *handle) {
  LPegProcess *process;
  LParserTree *tree;

  process = (LPegProcess *)handle;
  NEXUS_PROGRAM_PRECONDITION(process->done && process->tree != NULL, "taking a tree requires a terminated untaken result");

  tree          = process->tree;
  tree->taken   = TRUE;
  process->tree = NULL;
  process->parser->tree_count++;

  return tree;
}
