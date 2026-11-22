#include <nexus.h>

#include "../logos.h"
#include "./l_internal.h"

struct LLexerProcess {
  const LLexer *lexer;

  /* The bytes of the undecided token that earlier chunks held, which the process copied when it released them. carry_offset is where they start. */
  byte      *carry;
  uint_large carry_capacity;
  uint_large carry_size;
  uint_large carry_offset;

  /* The outstanding chunk, borrowed. It always starts where the carry ends: chunk_offset == carry_offset + carry_size. */
  const byte *chunk;
  uint_large  chunk_size;
  uint_large  chunk_offset;
  boolean     chunk_active;
  boolean     last_chunk_added;

  /* The token being decided: it starts at token_start, scan_end is the next byte to examine, and the DFA is at scan_row. */
  uint_large token_start;
  uint_large scan_end;
  uint32     scan_row;   /* The automaton's row offset (state * class_count) where the examined bytes left it. */
  boolean    has_accept; /* A longest match so far ends at accept_end with the rule ranked accept_tag. */
  uint_large accept_end;
  uint16     accept_tag;

  boolean     eof_emitted;
  LLexerError error;
  LLexerToken error_token;

  uint32        stack_depth; /* stack[0] is the root. */
  LLexerContext stack[LLEXER_CONTEXT_DEPTH_MAX];
};

/* ---------------------------------------------------------------------------- */
/* LIFECYCLE                                                                     */
/* ---------------------------------------------------------------------------- */

static void l_lexer_process_state_reset(LLexerProcess *process) {
  process->carry_size   = 0;
  process->carry_offset = 0;

  process->chunk            = NULL;
  process->chunk_size       = 0;
  process->chunk_offset     = 0;
  process->chunk_active     = FALSE;
  process->last_chunk_added = FALSE;

  process->token_start = 0;
  process->scan_end    = 0;
  process->scan_row    = 0;
  process->has_accept  = FALSE;
  process->accept_end  = 0;
  process->accept_tag  = 0;

  process->eof_emitted = FALSE;
  process->error       = LLE_NONE;

  process->error_token.kind          = L_LEXER_TOKEN_KIND_ERROR;
  process->error_token.lexeme_offset = 0;
  process->error_token.lexeme_width  = 0;

  process->stack_depth = 1;
  process->stack[0]    = process->lexer->root_context;
}

LLexerProcess *logos_language_tooling_lexing_lexer_process_create(LLexer *lexer) {
  LLexerProcess *process;

  NEXUS_PROGRAM_PRECONDITION(lexer != NULL, "lexer must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(lexer->finalized, "a process needs a finalized lexer");

  process                 = (LLexerProcess *)nexus_memory_allocate(NEXUS_SIZEOF(*process));
  process->lexer          = lexer;
  process->carry          = NULL;
  process->carry_capacity = 0;

  l_lexer_process_state_reset(process);

  return process;
}

void logos_language_tooling_lexing_lexer_process_destroy(LLexerProcess *process) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  /* Nexus refuses to free NULL, and the carry exists only once a token has straddled a chunk boundary. */
  if (process->carry != NULL) {
    nexus_memory_deallocate(process->carry);
  }

  nexus_memory_deallocate(process);
}

void logos_language_tooling_lexing_lexer_process_reset(LLexerProcess *process) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  l_lexer_process_state_reset(process);
}

/* ---------------------------------------------------------------------------- */
/* INPUT                                                                         */
/* ---------------------------------------------------------------------------- */

void logos_language_tooling_lexing_lexer_process_chunk_add(LLexerProcess *process, const byte *chunk, uint_large chunk_size, boolean is_last_chunk) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(chunk != NULL || chunk_size == 0, "a nonempty chunk must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(!process->chunk_active, "the previous chunk is still outstanding: lex until it returns LLS_NEED_INPUT");
  NEXUS_PROGRAM_PRECONDITION(!process->last_chunk_added, "the last chunk was already added");

  process->chunk            = chunk;
  process->chunk_size       = chunk_size;
  process->chunk_offset     = process->carry_offset + process->carry_size;
  process->chunk_active     = TRUE;
  process->last_chunk_added = is_last_chunk;
}

static void l_lexer_process_carry_reserve(LLexerProcess *process, uint_large required) {
  if (required > process->carry_capacity) {
    uint_large capacity;

    capacity = process->carry_capacity > 64 ? process->carry_capacity : 64;

    while (capacity < required) {
      capacity *= 2;
    }

    /* Nexus has no realloc of NULL either, so the first reservation allocates. */
    if (process->carry == NULL) {
      process->carry = (byte *)nexus_memory_allocate(capacity);
    } else {
      process->carry = (byte *)nexus_memory_reallocate(process->carry, capacity);
    }

    process->carry_capacity = capacity;
  }
}

/* Releases the outstanding chunk: copies the bytes from token_start on, which are the only ones a later decision can need, into the carry. */
static void l_lexer_process_chunk_release(LLexerProcess *process) {
  uint_large available_end;

  available_end = process->chunk_offset + process->chunk_size;

  if (process->token_start >= process->chunk_offset) {
    uint_large kept;

    kept = available_end - process->token_start;

    l_lexer_process_carry_reserve(process, kept);

    if (kept > 0) {
      nexus_memory_bytes_copy(process->chunk + (process->token_start - process->chunk_offset), process->carry, kept);
    }

    process->carry_size = kept;
  } else {
    uint_large dropped;

    /* The undecided token began in an earlier chunk, so it still sits in the carry: drop what is decided, then append the whole chunk. */
    dropped = process->token_start - process->carry_offset;

    if (dropped > 0) {
      nexus_memory_bytes_move(process->carry + dropped, process->carry, process->carry_size - dropped);
      process->carry_size -= dropped;
    }

    l_lexer_process_carry_reserve(process, process->carry_size + process->chunk_size);

    if (process->chunk_size > 0) {
      nexus_memory_bytes_copy(process->chunk, process->carry + process->carry_size, process->chunk_size);
    }

    process->carry_size += process->chunk_size;
  }

  process->carry_offset = process->token_start;
  process->chunk        = NULL;
  process->chunk_size   = 0;
  process->chunk_offset = process->carry_offset + process->carry_size;
  process->chunk_active = FALSE;
}

/* ---------------------------------------------------------------------------- */
/* MATCHING                                                                      */
/* ---------------------------------------------------------------------------- */

typedef enum LLexerScanOutcome {
  LLSO_EXHAUSTED, /* The bytes ran out with the automaton still alive. */
  LLSO_DEAD,      /* A byte killed the automaton. */
  LLSO_FINAL      /* The automaton reached an accepting state that nothing can extend. */
} LLexerScanOutcome;

/* Feeds bytes[0 .. count), which sit at absolute offset base, to the automaton: per byte one addition, one load and one range test, with the
   longest match so far kept in registers until the scan returns. On LLSO_DEAD *position is the offset of the killing byte. */
static LLexerScanOutcome l_lexer_process_scan(LLexerProcess *process, const LLexerCompiledContext *context, const byte *bytes, uint_large count,
                                              uint_large base, uint_large *position) {
  const uint32 *rows;
  const byte   *byte_class;
  const byte   *cursor;
  const byte   *stop;
  const byte   *accepted_at;
  uint32        live_limit;
  uint32        row;
  uint32        accepted_row;

  rows         = context->rows;
  byte_class   = context->view.byte_class;
  live_limit   = context->live_limit;
  cursor       = bytes;
  stop         = bytes + count;
  accepted_at  = NULL;
  accepted_row = 0;
  row          = process->scan_end == process->token_start ? context->initial_row : process->scan_row;

  while (cursor != stop) {
    row = rows[row + byte_class[*cursor]];
    cursor++;

    if (row < live_limit) {
      continue;
    }

    if (row >= context->final_limit) {
      process->scan_row = row;
      *position         = base + (uint_large)(cursor - bytes) - 1;

      if (accepted_at != NULL) {
        process->has_accept = TRUE;
        process->accept_end = base + (uint_large)(accepted_at - bytes);
        process->accept_tag = context->accept_tags[accepted_row >> context->class_shift];
      }

      return LLSO_DEAD;
    }

    accepted_at  = cursor;
    accepted_row = row;

    if (row >= context->accept_limit) {
      process->scan_row   = row;
      process->scan_end   = base + (uint_large)(cursor - bytes);
      process->has_accept = TRUE;
      process->accept_end = process->scan_end;
      process->accept_tag = context->accept_tags[row >> context->class_shift];

      return LLSO_FINAL;
    }
  }

  if (accepted_at != NULL) {
    process->has_accept = TRUE;
    process->accept_end = base + (uint_large)(accepted_at - bytes);
    process->accept_tag = context->accept_tags[accepted_row >> context->class_shift];
  }

  process->scan_row = row;
  process->scan_end = base + count;

  return LLSO_EXHAUSTED;
}

/* Applies the context action of a rule to the stack, and tells whether it could. A failed action leaves the stack as it was. */
static LLexerError l_lexer_process_action_apply(LLexerProcess *process, const LLexerTokenRule *rule) {
  switch (rule->action_kind) {
  case LLTCAK_NONE:
    return LLE_NONE;
  case LLTCAK_PUSH:
    if (process->stack_depth >= LLEXER_CONTEXT_DEPTH_MAX) {
      return LLE_CONTEXT_OVERFLOW;
    }

    process->stack[process->stack_depth] = rule->action_data.push_context;
    process->stack_depth++;

    return LLE_NONE;
  case LLTCAK_POP:
    if (rule->action_data.pop_amount >= process->stack_depth) {
      return LLE_CONTEXT_UNDERFLOW;
    }

    process->stack_depth -= rule->action_data.pop_amount;

    return LLE_NONE;
  case LLTCAK_SET:
    process->stack_depth = 1;

    if (rule->action_data.set_context != process->lexer->root_context) {
      process->stack[1]    = rule->action_data.set_context;
      process->stack_depth = 2;
    }

    return LLE_NONE;
  case LLTCAK_REPLACE:
    if (process->stack_depth < 2) {
      return LLE_CONTEXT_UNDERFLOW;
    }

    process->stack[process->stack_depth - 1] = rule->action_data.replace_context;

    return LLE_NONE;
  case LLTCAK_COUNT:
  default:
    NEXUS_PROGRAM_PANIC("invalid context action kind");
  }

  return LLE_NONE;
}

/* Consumes [token_start, token_start + width) and prepares the scan of the next token, which starts right behind it. */
static void l_lexer_process_token_consume(LLexerProcess *process, uint_large width) {
  process->token_start = process->token_start + width;
  process->scan_end    = process->token_start;
  process->scan_row    = 0;
  process->has_accept  = FALSE;
}

static LLexerStatus l_lexer_process_error_report(LLexerProcess *process, LLexerError error, LLexerTokenKind kind, uint_large width,
                                                 LLexerToken *out_token) {
  process->error                     = error;
  process->error_token.kind          = kind;
  process->error_token.lexeme_offset = process->token_start;
  process->error_token.lexeme_width  = (uint32)width;

  l_lexer_process_token_consume(process, width);

  *out_token = process->error_token;

  return LLS_ERROR;
}

/* Summary: the body of lex, shared with lex_many so a batch does not pay a second call and copy per token. */
static LLexerStatus l_lexer_process_next(LLexerProcess *process, LLexerToken *out_token) {
  const LLexerCompiledContext *context;
  uint_large                   available_end;
  uint_large                   scan_stop;
  uint_large                   scan_limit_end;
  uint_large                   width_limit;
  uint_large                   position;
  LLexerScanOutcome            outcome;

  NEXUS_PROGRAM_PRECONDITION(process != NULL && out_token != NULL, "process and out_token must not be NULL");

  if (process->eof_emitted) {
    return LLS_DONE;
  }

  NEXUS_PROGRAM_PRECONDITION(process->chunk_active, "no chunk is outstanding: add one after LLS_NEED_INPUT");

  available_end = process->chunk_offset + process->chunk_size;

  /* Nothing is left to decide: either the source is over or the next chunk has to say. */
  if (process->token_start == available_end) {
    if (process->last_chunk_added) {
      process->eof_emitted = TRUE;

      out_token->kind          = L_LEXER_TOKEN_KIND_EOF;
      out_token->lexeme_offset = process->token_start;
      out_token->lexeme_width  = 0;

      return LLS_TOKEN;
    }

    l_lexer_process_chunk_release(process);

    return LLS_NEED_INPUT;
  }

  context     = &process->lexer->compiled[process->stack[process->stack_depth - 1]];
  width_limit = process->lexer->token_width_limit;

  /* The bytes of the token are examined in at most two stretches, the carry and then the chunk, and never more than width_limit of them. */
  scan_limit_end = (width_limit > UINT_LARGE_MAX_VAL - process->token_start) ? UINT_LARGE_MAX_VAL : process->token_start + width_limit;
  scan_stop      = available_end < scan_limit_end ? available_end : scan_limit_end;
  outcome        = LLSO_EXHAUSTED;

  if (process->scan_end < process->chunk_offset && process->scan_end < scan_stop) {
    uint_large carry_stop;

    carry_stop = scan_stop < process->chunk_offset ? scan_stop : process->chunk_offset;

    outcome = l_lexer_process_scan(process, context, process->carry + (process->scan_end - process->carry_offset), carry_stop - process->scan_end,
                                   process->scan_end, &position);
  }

  if (outcome == LLSO_EXHAUSTED && process->scan_end >= process->chunk_offset && process->scan_end < scan_stop) {
    outcome = l_lexer_process_scan(process, context, process->chunk + (process->scan_end - process->chunk_offset), scan_stop - process->scan_end,
                                   process->scan_end, &position);
  }

  if (outcome == LLSO_EXHAUSTED) {
    /* The automaton is alive and the examined bytes ran out. Past the limit, that is an error whatever the chunking, so the verdict never depends
       on where the source was cut. */
    if (process->scan_end - process->token_start >= width_limit) {
      return l_lexer_process_error_report(process, LLE_TOKEN_TOO_LONG, L_LEXER_TOKEN_KIND_ERROR, width_limit, out_token);
    }

    if (!process->last_chunk_added) {
      l_lexer_process_chunk_release(process);

      return LLS_NEED_INPUT;
    }
  }

  /* Decided: the automaton died, nothing can extend the match, or the source ended. The longest accepting prefix, if there was one, is the token. */
  if (process->has_accept) {
    const LLexerTokenRule *rule;
    LLexerError            action_error;

    rule = &context->ranked_rules[process->accept_tag];

    out_token->kind          = rule->token_kind;
    out_token->lexeme_offset = process->token_start;
    out_token->lexeme_width  = (uint32)(process->accept_end - process->token_start);

    l_lexer_process_token_consume(process, process->accept_end - process->token_start);

    action_error = l_lexer_process_action_apply(process, rule);

    if (action_error != LLE_NONE) {
      process->error       = action_error;
      process->error_token = *out_token;

      return LLS_ERROR;
    }

    return LLS_TOKEN;
  }

  /* Nothing matched. The error span is the longest prefix that was still viable, or the one byte that no rule can start with, so it always
     consumes input and each byte is consumed once. */
  {
    uint_large width;

    if (outcome == LLSO_DEAD) {
      width = position > process->token_start ? position - process->token_start : 1;
    } else {
      width = process->scan_end - process->token_start;
    }

    return l_lexer_process_error_report(process, LLE_UNRECOGNIZED, L_LEXER_TOKEN_KIND_ERROR, width, out_token);
  }
}

LLexerStatus logos_language_tooling_lexing_lexer_process_lex(LLexerProcess *process, LLexerToken *out_token) {
  return l_lexer_process_next(process, out_token);
}

/* ---------------------------------------------------------------------------- */
/* BATCHES AND INSPECTION                                                        */
/* ---------------------------------------------------------------------------- */

LLexerStatus logos_language_tooling_lexing_lexer_process_lex_many(LLexerProcess *process, NexusDataList *out_tokens, uint_large token_limit) {
  uint_large   produced;
  LLexerToken *slot;
  LLexerStatus status;

  NEXUS_PROGRAM_PRECONDITION(process != NULL && out_tokens != NULL, "process and out_tokens must not be NULL");
  NEXUS_PROGRAM_PRECONDITION(token_limit >= 1, "token_limit must be at least 1");
  NEXUS_PROGRAM_PRECONDITION(nexus_data_list_element_size_get(out_tokens) == NEXUS_SIZEOF(LLexerToken), "out_tokens must hold LLexerToken elements");

  nexus_data_list_clear(out_tokens);

  for (produced = 0; produced < token_limit; produced++) {
    /* The token is decided straight into its slot; a status other than a token gives the slot back. */
    slot   = (LLexerToken *)nexus_data_list_append_uninitialized(out_tokens);
    status = l_lexer_process_next(process, slot);

    if (status != LLS_TOKEN) {
      (void)nexus_data_list_pop(out_tokens, NULL);

      return status;
    }
  }

  return LLS_TOKEN;
}

LLexerError logos_language_tooling_lexing_lexer_process_error_get(const LLexerProcess *process, LLexerToken *out_token) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  if (out_token != NULL) {
    *out_token = process->error_token;
  }

  return process->error;
}

uint_large logos_language_tooling_lexing_lexer_process_offset_get(const LLexerProcess *process) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  return process->token_start;
}

LLexerContext logos_language_tooling_lexing_lexer_process_context_get(const LLexerProcess *process) {
  NEXUS_PROGRAM_PRECONDITION(process != NULL, "process must not be NULL");

  return process->stack[process->stack_depth - 1];
}
