#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

#define L_PRATT_NO_ENTRY ((uint32)0xFFFFFFFF)

/* What one token did to one run. */
#define L_PRATT_TAKEN      ((uint32)0) /* The run continues and consumed the token. */
#define L_PRATT_ENDED      ((uint32)1) /* The token cannot continue a complete expression: it was completed into the chart. */
#define L_PRATT_DIED       ((uint32)2) /* The token cannot continue an incomplete expression; the allowed terminals were collected. */
#define L_PRATT_DIED_QUIET ((uint32)3) /* The run ends without a diagnostic: leading trivia, or a resource limit. */

/* ---------------------------------------------------------------------------- */
/* COMPILATION                                                                   */
/* ---------------------------------------------------------------------------- */

void logos_language_tooling_parsing_pratt_compile(LParser *parser, const LContextaOperation *operation, LParsingPratt *out_pratt) {
  const LContextaPrattEntry *source;
  LParsingPrattEntry        *entry;
  LParsingPrattKey          *keys;
  uint32                    *key_count;
  LParsingPrattKey           key;
  const char                *label;
  uint32                     count;
  uint32                     i;
  uint32                     j;
  int32                      base;

  NEXUS_PROGRAM_PRECONDITION(parser != NULL && operation != NULL && out_pratt != NULL, "Pratt compilation arguments must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(operation->view.kind == LCOK_PRATT, "operation must be a Pratt operation");

  source = operation->view.operation_data.pratt.entries;
  count  = operation->view.operation_data.pratt.count;

  out_pratt->entries        = (LParsingPrattEntry *)nexus_memory_allocate((uint_large)count * NEXUS_SIZEOF(*out_pratt->entries));
  out_pratt->operands       = (LParsingPrattKey *)nexus_memory_allocate((uint_large)count * NEXUS_SIZEOF(*out_pratt->operands));
  out_pratt->operators      = (LParsingPrattKey *)nexus_memory_allocate((uint_large)count * NEXUS_SIZEOF(*out_pratt->operators));
  out_pratt->entry_count    = count;
  out_pratt->operand_count  = 0;
  out_pratt->operator_count = 0;

  for (i = 0; i < count; i++) {
    entry                           = &out_pratt->entries[i];
    entry->kind                     = source[i].kind;
    entry->label                    = logos_language_tooling_parsing_parser_label_copy(parser, source[i].label);
    entry->token_kind               = source[i].token_kind;
    entry->close_kind               = source[i].close_kind;
    entry->separator_kind           = source[i].separator_kind;
    entry->precedence               = source[i].precedence;
    entry->allow_empty              = source[i].allow_empty;
    entry->allow_trailing_separator = source[i].allow_trailing_separator;
    entry->left_bp                  = -1;
    entry->right_bp                 = -1;

    /*
    Each precedence owns two binding powers. A left-associative operator binds its right operand one step
    tighter than itself (the same operator then reduces); a right-associative one a step looser (it shifts).
    */
    base = (int32)(2 * entry->precedence);

    switch (entry->kind) {
    case LCPK_ATOM:
    case LCPK_ENCLOSED:
      break;
    case LCPK_PREFIX:
      entry->right_bp = base + 1;
      break;
    case LCPK_INFIX_LEFT:
    case LCPK_INFIX_NONE:
      entry->left_bp  = base;
      entry->right_bp = base + 1;
      break;
    case LCPK_INFIX_RIGHT:
    case LCPK_TERNARY:
      entry->left_bp  = base + 1;
      entry->right_bp = base;
      break;
    case LCPK_POSTFIX:
    case LCPK_APPLICATION:
      entry->left_bp = base;
      break;
    case LCPK_COUNT:
    default:
      NEXUS_PROGRAM_PANIC("invalid Pratt entry kind");
    }

    /* Diagnostics name a terminal by its entry label, else by the lexer rule that produces it. */
    label = logos_language_tooling_parsing_parser_token_label_get(parser, entry->token_kind);

    entry->terminal.token_kind = entry->token_kind;
    logos_languages_regula_observation_mask_clear(entry->terminal.symbol_mask);
    entry->terminal.label         = entry->label != NULL ? entry->label : logos_language_tooling_parsing_parser_label_copy(parser, label);
    entry->terminal.byte_terminal = FALSE;

    entry->close_terminal.token_kind = entry->close_kind;
    logos_languages_regula_observation_mask_clear(entry->close_terminal.symbol_mask);
    entry->close_terminal.label =
        logos_language_tooling_parsing_parser_label_copy(parser, logos_language_tooling_parsing_parser_token_label_get(parser, entry->close_kind));
    entry->close_terminal.byte_terminal = FALSE;

    entry->separator_terminal.token_kind = entry->separator_kind;
    logos_languages_regula_observation_mask_clear(entry->separator_terminal.symbol_mask);
    entry->separator_terminal.label = logos_language_tooling_parsing_parser_label_copy(
        parser, logos_language_tooling_parsing_parser_token_label_get(parser, entry->separator_kind));
    entry->separator_terminal.byte_terminal = FALSE;

    /* Sorted insertion: tables hold a handful of entries, and lookups binary-search them per token. */
    key.kind  = entry->token_kind;
    key.entry = i;

    if (entry->kind == LCPK_ATOM || entry->kind == LCPK_PREFIX || entry->kind == LCPK_ENCLOSED) {
      keys      = out_pratt->operands;
      key_count = &out_pratt->operand_count;
    } else {
      keys      = out_pratt->operators;
      key_count = &out_pratt->operator_count;
    }

    for (j = *key_count; j > 0 && keys[j - 1].kind > key.kind; j--) {
      keys[j] = keys[j - 1];
    }

    keys[j] = key;
    (*key_count)++;
  }
}

void logos_language_tooling_parsing_pratt_destroy(LParsingPratt *pratt) {
  NEXUS_PROGRAM_PRECONDITION(pratt != NULL, "table must not be NULL");

  nexus_memory_deallocate(pratt->entries);
  nexus_memory_deallocate(pratt->operands);
  nexus_memory_deallocate(pratt->operators);
}

/* ---------------------------------------------------------------------------- */
/* RUN STORAGE                                                                   */
/* ---------------------------------------------------------------------------- */

void logos_language_tooling_parsing_pratt_run_start(LParserProcess *process, uint_large symbol, uint_large origin) {
  const LParsingSymbol *grammar_symbol;
  LParsingPrattRun      run;

  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  grammar_symbol = (const LParsingSymbol *)nexus_data_list_at_const(process->parser->symbols, symbol);
  NEXUS_PROGRAM_INVARIANT(grammar_symbol->kind == LCOK_PRATT && grammar_symbol->pratt != LPARSER_INDEX_NONE, "a run needs a Pratt symbol");

  run.symbol            = symbol;
  run.origin            = origin;
  run.table             = (const LParsingPratt *)nexus_data_list_at_const(process->parser->pratts, grammar_symbol->pratt);
  run.top               = LPARSER_INDEX_NONE;
  run.value             = LPARSER_INDEX_NONE;
  run.pending           = LPARSER_INDEX_NONE;
  run.expecting_operand = TRUE;
  run.started           = FALSE;
  (void)nexus_data_list_append(process->pratt_runs, &run);
}

void logos_language_tooling_parsing_pratt_runs_reset(LParserProcess *process) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  nexus_data_list_clear(process->pratt_runs);
  nexus_data_chunked_list_clear(&process->pratt_frames);
  nexus_data_list_clear(process->pratt_expected);
  process->pratt_free = LPARSER_INDEX_NONE;
}

/* Summary: takes a frame from the free list or the arena and links it above the run's top. The caller reserved room. */
static LParsingPrattFrame *l_pratt_frame_push(LParserProcess *process, LParsingPrattRun *run, uint32 entry, int32 right_bp,
                                              LParsingNodeIndex children, uint32 children_count) {
  LParsingPrattFrame *frame;
  uint_large          index;

  if (process->pratt_free != LPARSER_INDEX_NONE) {
    index               = process->pratt_free;
    frame               = (LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, index);
    process->pratt_free = frame->below;
  } else {
    index = nexus_data_chunked_list_length_get(&process->pratt_frames);
    frame = (LParsingPrattFrame *)nexus_data_chunked_list_append_uninitialized(&process->pratt_frames);
  }

  frame->below          = run->top;
  frame->children       = children;
  frame->children_count = children_count;
  frame->entry          = entry;
  frame->stage          = 0;
  frame->right_bp       = right_bp;

  if (right_bp < 0) {
    frame->barrier = index;
  } else if (run->top == LPARSER_INDEX_NONE) {
    frame->barrier = LPARSER_INDEX_NONE;
  } else {
    frame->barrier = ((const LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, run->top))->barrier;
  }

  run->top = index;

  return frame;
}

/* Summary: returns the frame at index to the free list; the caller has already unlinked it. */
static void l_pratt_frame_release(LParserProcess *process, uint_large index) {
  LParsingPrattFrame *frame;

  frame               = (LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, index);
  frame->below        = process->pratt_free;
  process->pratt_free = index;
}

/* ---------------------------------------------------------------------------- */
/* GREEN CONSTRUCTION                                                            */
/* ---------------------------------------------------------------------------- */

/* Summary: a sequence node over two adjacent nodes. The caller reserved room for it. */
static LParsingNodeIndex l_pratt_sequence(LParserProcess *process, LParsingNodeIndex first, LParsingNodeIndex second) {
  LParsingNodeIndex sequence;
  uint_large        width;

  width = ((const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, first))->width +
          ((const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, second))->width;

  sequence = logos_language_tooling_parsing_parser_process_node_create(process, LPGK_SEQUENCE, width, LPARSER_INDEX_NONE);
  NEXUS_PROGRAM_INVARIANT(sequence != LPARSER_INDEX_NONE, "reserved room must suffice");
  logos_language_tooling_parsing_parser_process_alternative_append(process, sequence, first, second);

  return sequence;
}

/* Summary: the significant token's leaf, preceded by any pending trivia as one sequence. */
static LParsingNodeIndex l_pratt_group(LParserProcess *process, LParsingPrattRun *run, LParsingNodeIndex leaf) {
  LParsingNodeIndex group;

  if (run->pending == LPARSER_INDEX_NONE) {
    return leaf;
  }

  group        = l_pratt_sequence(process, run->pending, leaf);
  run->pending = LPARSER_INDEX_NONE;

  return group;
}

/* Summary: appends one child to a left-nested cover of count children; the first child is its own cover. */
static LParsingNodeIndex l_pratt_children_push(LParserProcess *process, LParsingNodeIndex cover, uint32 count, LParsingNodeIndex child) {
  if (count == 0) {
    return child;
  }

  return l_pratt_sequence(process, cover, child);
}

/* Summary: creates an operator node whose one alternative is cover (of count children) followed by last. */
static LParsingNodeIndex l_pratt_node_close(LParserProcess *process, LParsingNodeIndex cover, uint32 count, LParsingNodeIndex last,
                                            const LParsingPrattEntry *entry) {
  LParsingNodeIndex  node;
  LParsingGreenNode *green;
  uint_large         width;

  width = ((const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, last))->width;

  if (count > 0) {
    width += ((const LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, cover))->width;
  }

  node = logos_language_tooling_parsing_parser_process_node_create(process, LPGK_EXPRESSION, width, LPARSER_INDEX_NONE);
  NEXUS_PROGRAM_INVARIANT(node != LPARSER_INDEX_NONE, "reserved room must suffice");
  green                = (LParsingGreenNode *)nexus_data_chunked_list_at(&process->tree->nodes, node);
  green->payload.label = entry->label; /* aux stays LPARSER_INDEX_NONE: no grammar symbol marks an operator node. */

  if (count > 0) {
    logos_language_tooling_parsing_parser_process_alternative_append(process, node, cover, last);
  } else {
    logos_language_tooling_parsing_parser_process_alternative_append(process, node, last, LPARSER_INDEX_NONE);
  }

  return node;
}

/* Summary: applies the top pending operator to the held operand. The caller reserved room and the top is not a barrier. */
static void l_pratt_reduce(LParserProcess *process, LParsingPrattRun *run) {
  const LParsingPrattFrame *frame;
  uint_large                top;

  top   = run->top;
  frame = (const LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, top);
  NEXUS_PROGRAM_INVARIANT(frame->right_bp >= 0, "only a non-barrier frame reduces");

  run->value = l_pratt_node_close(process, frame->children, frame->children_count, run->value, &run->table->entries[frame->entry]);
  run->top   = frame->below;
  l_pratt_frame_release(process, top);
}

/* ---------------------------------------------------------------------------- */
/* DIAGNOSTIC TERMINALS                                                          */
/* ---------------------------------------------------------------------------- */

/* Summary: records the terminals the table allowed where the run stopped. */
static void l_pratt_expect(LParserProcess *process, const LParsingPrattRun *run) {
  const LParsingPratt      *table;
  const LParsingPrattFrame *frame;
  const LParsingPrattEntry *entry;
  uint_large                barrier;
  uint32                    i;

  table = run->table;

  if (run->expecting_operand) {
    for (i = 0; i < table->operand_count; i++) {
      (void)nexus_data_list_append_pointer(process->pratt_expected, &table->entries[table->operands[i].entry].terminal);
    }

    if (run->top != LPARSER_INDEX_NONE) {
      frame = (const LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, run->top);
      entry = &table->entries[frame->entry];

      if (frame->right_bp < 0 && frame->stage <= 1 && (entry->kind == LCPK_ENCLOSED || entry->kind == LCPK_APPLICATION) &&
          (frame->stage == 0 ? entry->allow_empty : entry->allow_trailing_separator)) {
        (void)nexus_data_list_append_pointer(process->pratt_expected, &entry->close_terminal);
      }
    }

    return;
  }

  for (i = 0; i < table->operator_count; i++) {
    (void)nexus_data_list_append_pointer(process->pratt_expected, &table->entries[table->operators[i].entry].terminal);
  }

  barrier = run->top == LPARSER_INDEX_NONE ? LPARSER_INDEX_NONE
                                           : ((const LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, run->top))->barrier;

  if (barrier != LPARSER_INDEX_NONE) {
    frame = (const LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, barrier);
    entry = &table->entries[frame->entry];
    (void)nexus_data_list_append_pointer(process->pratt_expected, &entry->close_terminal);

    if (entry->separator_kind != LCONTEXTA_PRATT_TOKEN_NONE) {
      (void)nexus_data_list_append_pointer(process->pratt_expected, &entry->separator_terminal);
    }
  }
}

uint_large logos_language_tooling_parsing_pratt_expectations_flush(LParserProcess *process, uint_large expected_begin) {
  const LParserExpectation *candidate;
  const LParserExpectation *existing;
  uint_large                appended;
  uint_large                i;
  uint_large                j;
  boolean                   duplicate;

  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  appended = 0;

  for (i = 0; i < nexus_data_list_length_get(process->pratt_expected); i++) {
    candidate = (const LParserExpectation *)nexus_data_list_pointer_at(process->pratt_expected, i);
    duplicate = FALSE;

    for (j = expected_begin; j < nexus_data_slabbed_list_length_get(process->tree->expectations) && !duplicate; j++) {
      existing  = (const LParserExpectation *)nexus_data_slabbed_list_at(process->tree->expectations, j);
      duplicate = !existing->byte_terminal && existing->token_kind == candidate->token_kind;
    }

    if (!duplicate) {
      (void)nexus_data_slabbed_list_append(process->tree->expectations, candidate);
      appended++;
    }
  }

  return appended;
}

/* ---------------------------------------------------------------------------- */
/* DETERMINISTIC STEP                                                            */
/* ---------------------------------------------------------------------------- */

/* Summary: finds the entry selected by a token kind in a sorted role table, or L_PRATT_NO_ENTRY. */
static uint32 l_pratt_lookup(const LParsingPrattKey *keys, uint32 count, LLexerTokenKind kind) {
  uint32 low;
  uint32 high;
  uint32 middle;

  low  = 0;
  high = count;

  while (low < high) {
    middle = low + ((high - low) / 2);

    if (keys[middle].kind < kind) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }

  return low < count && keys[low].kind == kind ? keys[low].entry : L_PRATT_NO_ENTRY;
}

/* Summary: tells whether the run's scope admits the kind as default-allowed trivia. */
static boolean l_pratt_trivia_allows(const LParserProcess *process, const LParsingPrattRun *run, LLexerTokenKind kind) {
  const LParsingSymbol *symbol;
  const LParsingPolicy *policy;
  uint32                low;
  uint32                high;
  uint32                middle;

  symbol = (const LParsingSymbol *)nexus_data_list_at_const(process->parser->symbols, run->symbol);
  policy = &process->parser->policies[symbol->policy];
  low    = 0;
  high   = policy->count;

  while (low < high) {
    middle = low + ((high - low) / 2);

    if (policy->kinds[middle] < kind) {
      low = middle + 1;
    } else {
      high = middle;
    }
  }

  return low < policy->count && policy->kinds[low] == kind;
}

/*
Summary: feeds one token to one run and reports what happened.
Description: operand position takes atoms, prefix operators and enclosure openers; operator
  position takes infix, postfix, application and ternary tokens, reducing pending operators
  whose right binding power exceeds the new operator's left one, then closers and separators of
  the nearest barrier. The run ends on any token it cannot use once no barrier is open.
*/
static uint32 l_pratt_step(LParserProcess *process, LParsingPrattRun *run, const LLexerToken *token, LParsingNodeIndex leaf, uint_large chart) {
  const LParsingPratt      *table;
  const LParsingPrattEntry *entry;
  LParsingPrattFrame       *frame;
  LParsingNodeIndex         group;
  LParsingNodeIndex         cover;
  LParsingNodeIndex         node;
  LParsingNodeIndex         child;
  LParsingNodeIndex         completed;
  uint_large                barrier;
  uint_large                top;
  uint32                    found;
  uint32                    count;
  boolean                   is_close;
  boolean                   is_separator;
  boolean                   conflict;
  LLexerTokenKind           kind;

  table = run->table;
  kind  = token->kind;

  /* Every step creates at most four nodes and one frame outside its reduction loops, which reserve their own. */
  if (!logos_language_tooling_parsing_parser_process_headroom_reserve(process, 4)) {
    return L_PRATT_DIED_QUIET;
  }

  if (process->pratt_free == LPARSER_INDEX_NONE && nexus_data_chunked_list_length_get(&process->pratt_frames) >= process->parser->limits.max_items) {
    process->limited = TRUE;

    return L_PRATT_DIED_QUIET;
  }

  /* ---- Operand position ---------------------------------------------------- */

  if (run->expecting_operand) {
    found = kind >= L_LEXER_TOKEN_KIND_FIRST_USER ? l_pratt_lookup(table->operands, table->operand_count, kind) : L_PRATT_NO_ENTRY;

    if (found != L_PRATT_NO_ENTRY) {
      entry        = &table->entries[found];
      group        = l_pratt_group(process, run, leaf);
      run->started = TRUE;

      if (entry->kind == LCPK_ATOM) {
        run->value             = group;
        run->expecting_operand = FALSE;
      } else if (entry->kind == LCPK_PREFIX) {
        (void)l_pratt_frame_push(process, run, found, entry->right_bp, group, 1);
      } else {
        NEXUS_PROGRAM_INVARIANT(entry->kind == LCPK_ENCLOSED, "operand entries are atoms, prefixes and enclosures");
        (void)l_pratt_frame_push(process, run, found, -1, group, 1);
      }

      return L_PRATT_TAKEN;
    }

    /* An enclosure may close straight after its opener or a separator when its table says so. */
    if (run->top != LPARSER_INDEX_NONE) {
      frame = (LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, run->top);
      entry = &table->entries[frame->entry];

      if (frame->right_bp < 0 && frame->stage <= 1 && (entry->kind == LCPK_ENCLOSED || entry->kind == LCPK_APPLICATION) &&
          kind == entry->close_kind && (frame->stage == 0 ? entry->allow_empty : entry->allow_trailing_separator)) {
        group      = l_pratt_group(process, run, leaf);
        run->value = l_pratt_node_close(process, frame->children, frame->children_count, group, entry);
        top        = run->top;
        run->top   = frame->below;
        l_pratt_frame_release(process, top);
        run->expecting_operand = FALSE;

        return L_PRATT_TAKEN;
      }
    }

    if (kind >= L_LEXER_TOKEN_KIND_FIRST_USER && l_pratt_trivia_allows(process, run, kind)) {
      /* Trivia before the first significant token belongs to the grammar around the run, which handles it itself. */
      if (!run->started) {
        return L_PRATT_DIED_QUIET;
      }

      run->pending = run->pending == LPARSER_INDEX_NONE ? leaf : l_pratt_sequence(process, run->pending, leaf);

      return L_PRATT_TAKEN;
    }

    l_pratt_expect(process, run);

    return L_PRATT_DIED;
  }

  /* ---- Operator position --------------------------------------------------- */

  conflict = FALSE;
  found    = kind >= L_LEXER_TOKEN_KIND_FIRST_USER ? l_pratt_lookup(table->operators, table->operator_count, kind) : L_PRATT_NO_ENTRY;

  if (found != L_PRATT_NO_ENTRY) {
    entry = &table->entries[found];

    while (run->top != LPARSER_INDEX_NONE) {
      frame = (LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, run->top);

      if (frame->right_bp < 0 || entry->left_bp >= frame->right_bp) {
        break;
      }

      /* A non-associative operator may not chain with itself; the expression ends before the second one. */
      if (table->entries[frame->entry].kind == LCPK_INFIX_NONE && entry->kind == LCPK_INFIX_NONE &&
          entry->precedence == table->entries[frame->entry].precedence) {
        conflict = TRUE;
        break;
      }

      if (!logos_language_tooling_parsing_parser_process_headroom_reserve(process, 2)) {
        return L_PRATT_DIED_QUIET;
      }

      l_pratt_reduce(process, run);
    }

    if (!conflict) {
      group = l_pratt_group(process, run, leaf);

      if (entry->kind == LCPK_POSTFIX) {
        run->value = l_pratt_node_close(process, run->value, 1, group, entry);

        return L_PRATT_TAKEN;
      }

      cover = l_pratt_sequence(process, run->value, group);

      if (entry->kind == LCPK_INFIX_LEFT || entry->kind == LCPK_INFIX_RIGHT || entry->kind == LCPK_INFIX_NONE) {
        (void)l_pratt_frame_push(process, run, found, entry->right_bp, cover, 2);
      } else {
        NEXUS_PROGRAM_INVARIANT(entry->kind == LCPK_APPLICATION || entry->kind == LCPK_TERNARY,
                                "operator entries are infix, postfix, application and ternary");
        (void)l_pratt_frame_push(process, run, found, -1, cover, 2);
      }

      run->value             = LPARSER_INDEX_NONE;
      run->expecting_operand = TRUE;

      return L_PRATT_TAKEN;
    }
  }

  /* The nearest open enclosure or ternary decides which closing or separating token continues the expression. */
  barrier = run->top == LPARSER_INDEX_NONE ? LPARSER_INDEX_NONE
                                           : ((const LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, run->top))->barrier;

  if (barrier != LPARSER_INDEX_NONE) {
    frame        = (LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, barrier);
    entry        = &table->entries[frame->entry];
    is_close     = kind == entry->close_kind;
    is_separator = entry->separator_kind != LCONTEXTA_PRATT_TOKEN_NONE && kind == entry->separator_kind;

    if (is_close || is_separator) {
      while (run->top != barrier) {
        if (!logos_language_tooling_parsing_parser_process_headroom_reserve(process, 2)) {
          return L_PRATT_DIED_QUIET;
        }

        l_pratt_reduce(process, run);
      }

      group = l_pratt_group(process, run, leaf);
      count = frame->children_count;
      cover = l_pratt_children_push(process, frame->children, count, run->value);
      count++;

      if (is_close && entry->kind != LCPK_TERNARY) {
        run->value = l_pratt_node_close(process, cover, count, group, entry);
        run->top   = frame->below;
        l_pratt_frame_release(process, barrier);

        return L_PRATT_TAKEN;
      }

      /* A separator, or the ternary's second token, hands the frame back to the operand position. */
      cover = l_pratt_children_push(process, cover, count, group);
      count++;

      frame->children       = cover;
      frame->children_count = count;
      frame->stage          = 1;

      if (entry->kind == LCPK_TERNARY) {
        /* The ternary now waits for its last operand like a right-associative infix operator, so it stops being a barrier. */
        frame->right_bp = entry->right_bp;
        frame->barrier  = frame->below == LPARSER_INDEX_NONE
                              ? LPARSER_INDEX_NONE
                              : ((const LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, frame->below))->barrier;
      }

      run->value             = LPARSER_INDEX_NONE;
      run->expecting_operand = TRUE;

      return L_PRATT_TAKEN;
    }
  }

  if (kind >= L_LEXER_TOKEN_KIND_FIRST_USER && l_pratt_trivia_allows(process, run, kind)) {
    run->pending = run->pending == LPARSER_INDEX_NONE ? leaf : l_pratt_sequence(process, run->pending, leaf);

    return L_PRATT_TAKEN;
  }

  if (barrier != LPARSER_INDEX_NONE) {
    l_pratt_expect(process, run);

    return L_PRATT_DIED;
  }

  /* No barrier is open: the token ends the expression. Apply what is pending, then complete it into the chart. */
  while (run->top != LPARSER_INDEX_NONE) {
    if (!logos_language_tooling_parsing_parser_process_headroom_reserve(process, 2)) {
      return L_PRATT_DIED_QUIET;
    }

    l_pratt_reduce(process, run);
  }

  if (!logos_language_tooling_parsing_parser_process_headroom_reserve(process, 3)) {
    return L_PRATT_DIED_QUIET;
  }

  /* Trailing trivia is absorbed: the node ends where the chart does. */
  child = run->value;

  if (run->pending != LPARSER_INDEX_NONE) {
    child        = l_pratt_sequence(process, child, run->pending);
    run->pending = LPARSER_INDEX_NONE;
  }

  completed = logos_language_tooling_parsing_parser_process_node_get(process, 0, run->symbol, run->origin, chart, LPGK_EXPRESSION, run->symbol);

  if (completed == LPARSER_INDEX_NONE) {
    return L_PRATT_DIED_QUIET;
  }

  node = completed;
  logos_language_tooling_parsing_parser_process_alternative_append(process, node, child, LPARSER_INDEX_NONE);
  logos_language_tooling_parsing_parser_process_waiters_advance(process, chart, run->origin, run->symbol, completed);

  return L_PRATT_ENDED;
}

void logos_language_tooling_parsing_pratt_runs_feed(LParserProcess *process, uint_large first_run, const LLexerToken *token, LParsingNodeIndex leaf,
                                                    uint_large chart) {
  LParsingPrattRun *run;
  LParsingPrattRun *kept;
  uint_large        count;
  uint_large        read;
  uint_large        write;
  uint_large        index;
  uint_large        next;
  uint32            outcome;

  NEXUS_PROGRAM_PRECONDITION(process != NULL && token != NULL, "process and token must not be NULL");

  count = nexus_data_list_length_get(process->pratt_runs);
  write = first_run;

  for (read = first_run; read < count; read++) {
    run     = (LParsingPrattRun *)nexus_data_list_at(process->pratt_runs, read);
    outcome = l_pratt_step(process, run, token, leaf, chart);

    if (outcome == L_PRATT_TAKEN) {
      process->pratt_consumed = TRUE;

      if (write != read) {
        kept  = (LParsingPrattRun *)nexus_data_list_at(process->pratt_runs, write);
        *kept = *run;
      }

      write++;
      continue;
    }

    /* A run that died leaves frames behind; recycle them. A run that ended has none. */
    for (index = run->top; index != LPARSER_INDEX_NONE; index = next) {
      next = ((const LParsingPrattFrame *)nexus_data_chunked_list_at(&process->pratt_frames, index))->below;
      l_pratt_frame_release(process, index);
    }
  }

  while (nexus_data_list_length_get(process->pratt_runs) > write) {
    (void)nexus_data_list_pop(process->pratt_runs, NULL);
  }
}
