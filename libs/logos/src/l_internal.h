#pragma once

#include <nexus.h>
#include <echo.h>

#include "../logos.h"

struct LogosRuntime {
  EchoRuntime *echo;
  EchoSystem  *log_system;
  EchoContext *log_context;
};

/* ---------------------------------------------------------------------------- */
/* BITSET                                                                       */
/* ---------------------------------------------------------------------------- */

/*
Summary: fixed-width set of small unsigned integers, one bit per member.
Description:
  Bit 0 is the most significant bit of words[0]. Every bitset combined
  with another must have the same word_count. A view (a bitset whose words
  point into a caller's arena, such as one row of a table) is made by
  setting words and word_count by hand; it owns nothing.
Ownership: logos_bitset_initialize allocates words; the owner releases them
  with nexus_memory_deallocate. A view's words are borrowed from the arena.
Fields:
  words       word_count 64-bit words of membership bits
  word_count  number of words; 16 bits cover every uint16 member
*/
typedef struct LBitset {
  uint64 *words;
  uint16  word_count;
} LBitset;

/* Summary: tells whether bit is a member of bitset. Asserts when bit lies beyond the bitset's words. */
static NEXUS_MAYBE_UNUSED boolean logos_bitset_get(const LBitset *bitset, uint16 bit) {
  uint16 word_index = bit / 64;
  uint8  bit_index  = bit % 64;

  NEXUS_PROGRAM_PRECONDITION(word_index < bitset->word_count, "bit must lie within the bitset");

  return nexus_bits_uint64_get_msb(bitset->words[word_index], bit_index);
}

/* Summary: adds bit to bitset when value is TRUE, removes it when FALSE. Asserts when bit lies beyond the bitset's words. */
static NEXUS_MAYBE_UNUSED void logos_bitset_set(LBitset *bitset, uint16 bit, boolean value) {
  uint16 word_index = bit / 64;
  uint8  bit_index  = bit % 64;

  NEXUS_PROGRAM_PRECONDITION(word_index < bitset->word_count, "bit must lie within the bitset");

  bitset->words[word_index] = nexus_bits_uint64_set_msb(bitset->words[word_index], bit_index, value);
}

/* Summary: removes every member of bitset. */
static NEXUS_MAYBE_UNUSED void logos_bitset_clear(LBitset *bitset) {
  nexus_memory_bytes_clear(bitset->words, NEXUS_SIZEOF(uint64) * bitset->word_count);
}

/* Summary: adds every member of source to destination. Asserts when the word counts differ. */
static NEXUS_MAYBE_UNUSED void logos_bitset_union(LBitset *destination, const LBitset *source) {
  uint16 i;

  NEXUS_PROGRAM_PRECONDITION(destination->word_count == source->word_count, "bitsets must have the same size");

  for (i = 0; i < destination->word_count; i++) {
    destination->words[i] |= source->words[i];
  }
}

/* Summary: tells whether bitset_a and bitset_b share a member, stopping at the first shared word. Asserts when the word counts differ. */
static NEXUS_MAYBE_UNUSED boolean logos_bitset_intersects(const LBitset *bitset_a, const LBitset *bitset_b) {
  uint16 i;

  NEXUS_PROGRAM_PRECONDITION(bitset_a->word_count == bitset_b->word_count, "bitsets must have the same size");

  for (i = 0; i < bitset_a->word_count; i++) {
    if ((bitset_a->words[i] & bitset_b->words[i]) != 0) {
      return TRUE;
    }
  }

  return FALSE;
}

/* Summary: the number of 64-bit words a bitset of bit_count bits occupies. */
static NEXUS_MAYBE_UNUSED uint16 logos_bitset_word_count_get(uint16 bit_count) {
  return (uint16)((bit_count + 63) / 64);
}

/*
Summary: allocates an empty bitset able to hold the bits 0 .. bit_count - 1.
Params: bitset  out; receives freshly allocated words and their count
Ownership: the caller owns bitset->words and releases them with nexus_memory_deallocate.
Errors: allocation follows Nexus policy.
*/
static NEXUS_MAYBE_UNUSED void logos_bitset_initialize(LBitset *bitset, uint16 bit_count) {
  uint16 required_words = logos_bitset_word_count_get(bit_count);

  bitset->words      = (uint64 *)nexus_memory_allocate(NEXUS_SIZEOF(uint64) * required_words);
  bitset->word_count = required_words;
  logos_bitset_clear(bitset);
}

/* Summary: sets every bit in [range_start, range_end) to value. Asserts when the range is empty or beyond the bitset. */
static NEXUS_MAYBE_UNUSED void logos_bitset_set_range(LBitset *bitset, uint16 range_start, uint16 range_end, boolean value) {
  uint16 i;

  NEXUS_PROGRAM_PRECONDITION(range_start < range_end, "range_start must be smaller than range end");

  for (i = range_start; i < range_end; i++) {
    logos_bitset_set(bitset, i, value);
  }
}

/* ---------------------------------------------------------------------------- */
/* LANGUAGES: REGULA                                                            */
/* ---------------------------------------------------------------------------- */

/*
Summary: deep-copies an operation tree into regula and attaches the copy.
Description:
  Duplicates operation and every operation reachable from it as a fresh,
  self-contained tree owned by regula, then attaches the copy's root with
  logos_languages_regula_regula_operation_add (so it follows that routine's
  root/concatenation rules, and is only stored, not attached, while
  regula->internal_constructing is set). The source may belong to regula or
  to any other regula and may be active or consumed; it is never modified.
  Operations shared inside the source (a subtree reachable along several
  paths) are copied once per path, so the copy is always a tree. Labels are
  copied as borrowed pointers. The walk is iterative, so source depth is
  bounded by memory, not by the call stack.
Params:
  regula     destination; receives every copied operation
  operation  source root; borrowed, never modified, may belong to any regula
Returns: the attached copy of operation (the new root of the duplicated tree).
Ownership: the returned pointer is borrowed from regula; a copied label stays
  borrowed from whoever owns the label text, so it must outlive regula.
Errors: NULL regula or operation, a missing operand in a compound operation,
  or an invalid operation kind assert. The source must be acyclic; a cycle
  never terminates and exhausts memory. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula; shared read access to the source.
Complexity: O(n) time and O(n) scratch space in the duplicated node count n,
  which can exceed the source's stored node count when subtrees are shared.
See: logos_languages_regula_regula_operation_add
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_duplicate(LRegula *regula, const LRegulaOperation *operation);

/*
Summary: copies a single operation, without its operands, into regula and attaches it.
Description:
  Stores a copy of operation's kind, label and symbol mask in regula and
  attaches it with logos_languages_regula_regula_operation_add (so it is only
  stored, not attached, while regula->internal_constructing is set). Operands
  are not followed: a unionize, concatenate or star copy has every operand
  pointer set to NULL, so it is an incomplete node until the caller assigns
  its operands. An unfilled compound copy must not reach the regexp writer or
  an automaton; build such copies with regula->internal_constructing set and
  attach the finished root yourself. Empty-language, empty-string and symbol
  copies are complete. The source is never modified and may belong to any
  regula.
Params:
  regula     destination; receives the copy
  operation  source operation; borrowed, never modified, may belong to any regula
Returns: the attached copy of operation.
Ownership: the returned pointer is borrowed from regula; the copied label
  stays borrowed from whoever owns the label text, so it must outlive regula.
Errors: NULL regula or operation, or an invalid operation kind assert.
  Allocation follows Nexus policy.
Thread-safety: exclusive access to regula; shared read access to the source.
Complexity: O(1).
See: logos_languages_regula_regula_operation_duplicate
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_duplicate_shallow(LRegula *regula, const LRegulaOperation *operation);

/* ---------------------------------------------------------------------------- */
/* AUTOMATONS: NFA                                                              */
/* ---------------------------------------------------------------------------- */

/*
Summary: the operations the DFA system needs from an NFA, so it never sees how the NFA stores its states.
Description:
  The NFA's states are numbered 0 .. state_count - 1 and a set of them is an
  LBitset of logos_bitset_word_count_get(state_count) words. The functions
  below are the whole contract between the two systems; everything else about
  the NFA's representation may change without them noticing.
*/

/* Summary: the number of states of nfa; every state set of nfa holds that many bits. */
extern uint16 logos_automatons_nfa_state_count_get(const LNFA *nfa);

/* Summary: overwrites set with the NFA's initial state set. Asserts when set has the wrong width. */
extern void logos_automatons_nfa_initial_set_get(const LNFA *nfa, LBitset *set);

/*
Summary: overwrites destination with every state reachable from a state of source in one transition, whatever the byte.
Errors: a set of the wrong width, or source and destination being the same set, asserts.
Complexity: O(members * words) time.
*/
extern void logos_automatons_nfa_successors_get(const LNFA *nfa, const LBitset *source, LBitset *destination);

/*
Summary: removes from set every state that a transition on byte cannot enter, and tells whether any state remains.
Description: applied to the result of logos_automatons_nfa_successors_get this
  completes one step of the simulation.
Returns: TRUE if set is still nonempty.
Errors: a set of the wrong width asserts.
Complexity: O(words) time.
*/
extern boolean logos_automatons_nfa_byte_restrict(const LNFA *nfa, byte value, LBitset *set);

/*
Summary: tells whether set holds an accepting state. Asserts when set has the wrong width.
Complexity: O(words) time.
*/
extern boolean logos_automatons_nfa_set_accepts(const LNFA *nfa, const LBitset *set);

/*
Summary: partitions the 256 byte values into classes that the NFA cannot tell apart.
Description: two bytes share a class exactly when logos_automatons_nfa_byte_restrict
  treats them identically, so one representative per class stands for all of
  its bytes. Classes are numbered in order of their lowest byte.
Params:
  byte_class           out; the class of each of the 256 byte values
  class_representative out; the lowest byte of each class; the first class_count entries are set
Returns: the number of classes, 1 to 256.
Complexity: O(256 * classes * words) time.
*/
extern uint16 logos_automatons_nfa_byte_classes_get(const LNFA *nfa, byte *byte_class, byte *class_representative);

/* ---------------------------------------------------------------------------- */
/* AUTOMATONS: ACCEPT TAGS                                                       */
/* ---------------------------------------------------------------------------- */

/*
Summary: the accept tag of a state that does not accept.
Description:
  Every accepting NFA state carries a 16-bit accept tag and a DFA state takes
  the smallest tag among the accepting NFA states it stands for, so a lower
  tag wins. logos_automatons_nfa_from_regula tags every accepting state 0.
  The lexer tags each rule with its rank (priority, then registration order)
  and unites the rules of a context into one automaton, so the winning tag of
  an accepting DFA state names the rule that matched.
*/
#define L_ACCEPT_TAG_NONE ((uint16)UINT16_MAX_VAL)

/* Summary: read-only view of the tables of a DFA, for a matching loop that must not pay one call per byte. Borrowed from the DFA. */
typedef struct LDFAView {
  const uint16 *transitions; /* Row s, column c: the state entered from s on a byte of class c. */
  const byte   *byte_class;  /* 256 entries: the column of every byte. */
  const uint16 *accept_tags; /* Per state: the winning tag, or L_ACCEPT_TAG_NONE. */
  uint16        state_count;
  uint16        class_count;
  uint16        dead_state; /* The non-accepting state that only loops onto itself, or UINT16_MAX_VAL when there is none. */
} LDFAView;

/* Summary: fills view with the tables of dfa. The view is valid until the dfa is destroyed. */
extern void logos_automatons_dfa_view_get(const LDFA *dfa, LDFAView *view);

/*
Summary: the smallest accept tag among the accepting states of set, or L_ACCEPT_TAG_NONE when it holds none.
Errors: a set of the wrong width asserts.
Complexity: O(words + members) time.
*/
extern uint16 logos_automatons_nfa_set_accept_tag_get(const LNFA *nfa, const LBitset *set);

/*
Summary: builds one NFA that accepts the union of several, remembering which one accepted.
Description:
  The parts share the initial state of the result; every other state of a
  part follows, renumbered. An accepting state of part i takes tags[i], and
  the initial state accepts, with the smallest tag of any part, when a part
  accepts the empty string. The result does not refer to the parts.
Params:
  parts    count NFAs; borrowed, not modified
  tags     count tags, one per part; borrowed; none may be L_ACCEPT_TAG_NONE
  context  Echo context that receives the construction statistics; borrowed
Returns: the owned NFA, or NULL when the parts together need more than 65535
  states (an error is logged and nothing is allocated).
Ownership: release a returned NFA with logos_automatons_nfa_destroy.
Errors: NULL arguments, a zero count, or a tag equal to L_ACCEPT_TAG_NONE assert.
Complexity: O(total states * words) time.
*/
extern LNFA *logos_automatons_nfa_union(const LNFA *const *parts, const uint16 *tags, uint_large count, EchoContext *context);

/* ---------------------------------------------------------------------------- */
/* LANGUAGE TOOLING: LEXING                                                      */
/* ---------------------------------------------------------------------------- */

/* What a matching loop must do on entering a state of a compiled context, decided by which range of state numbers it falls in. */
#define L_LEXER_STATE_LIVE   ((byte)0) /* Not accepting, not dead: keep going. */
#define L_LEXER_STATE_ACCEPT ((byte)1) /* Accepting, and a longer match is still possible: remember it and keep going. */
#define L_LEXER_STATE_FINAL  ((byte)2) /* Accepting, and no byte can extend it: the token is decided, so stop without reading further. */
#define L_LEXER_STATE_DEAD   ((byte)3) /* No match is possible any more: stop. */

/*
One context, compiled: the minimal DFA of all its rules, whose accept tag is the rank of the winning rule.
The states are renumbered so that the kind of a state is a range test instead of a second dependent load per byte, and
the table stores each target as a row offset (state << class_shift, each row padded to a power of two), so one byte costs one
addition and one load. A "row" below is such an offset:
  [0, live_limit)             LIVE
  [live_limit, accept_limit)  ACCEPT
  [accept_limit, final_limit) FINAL
  [final_limit, ...)          DEAD
*/
typedef struct LLexerCompiledContext {
  LDFA            *dfa;         /* Owned; supplies the byte classes. */
  LDFAView         view;        /* Into dfa: counts and byte classes are valid; transitions and accept_tags are the DFA's own numbering. */
  uint32          *rows;        /* Owned. Entry (row + class): the row entered from that row on a byte of that class. */
  uint16          *accept_tags; /* Owned. Per renumbered state, so indexed by row / class_count: the winning rank, or L_ACCEPT_TAG_NONE. */
  uint32           class_shift; /* A row is padded to 2^class_shift entries, so row / stride is a shift. */
  uint32           initial_row;
  uint32           live_limit;
  uint32           accept_limit;
  uint32           final_limit;
  LNFA           **nfas;         /* Owned: one automaton per rule, in registration order, kept to unite subsets of the rules later. */
  LLexerTokenRule *ranked_rules; /* Indexed by accept tag: the rules ordered by priority, then registration. */
  uint_large       rule_count;
} LLexerCompiledContext;

typedef struct LLexerContextData {
  const char    *label;
  boolean        is_root;
  NexusDataList *rules; /* LLexerTokenRule, in registration order. */
} LLexerContextData;

struct LLexer {
  LogosRuntime *runtime;

  NexusDataList *contexts; /* LLexerContextData, indexed by LLexerContext. */
  LLexerContext  root_context;
  boolean        has_root_context;

  uint32  token_width_limit;
  boolean finalized;

  LLexerCompiledContext *compiled; /* Per context; NULL until finalized. */
  uint_large             compiled_count;
};

/* ---------------------------------------------------------------------------- */
/* CONTEXTA                                                                     */
/* ---------------------------------------------------------------------------- */

struct LContextaOperation {
  LContextaOperationView view;
  LContexta             *owner;
  uint_large             index;
};

struct LContextaRule {
  const char               *label;
  LContexta                *owner;
  const LContextaOperation *body;
  uint_large                index;
  LContextaAssociativity    associativity;
  int32                     level;
};

typedef struct LContextaTokenSet {
  LLexerTokenKind *kinds;
} LContextaTokenSet;

struct LContexta {
  const char           *label;
  NexusSemanticVersion  version;
  NexusDataSlabbedList *operations;
  NexusDataSlabbedList *rules;
  const LContextaRule  *start_rule;
  NexusDataList        *token_sets;
  NexusDataList        *pratt_tables; /* Owned LContextaPrattEntry arrays, freed with the grammar. */
};

/* ---------------------------------------------------------------------------- */
/* LANGUAGE TOOLING: PARSING                                                     */
/* ---------------------------------------------------------------------------- */

/*
Indices of charts, items, nodes, alternatives and grammar symbols are bounded by the parser's limits, well below 2^32,
so storage keeps them in 32 bits and this sentinel is the all-ones 32-bit value: it survives storing into and
loading from a uint32 field unchanged, and compares equal as a uint_large.
*/
#define LPARSER_INDEX_NONE ((uint_large)0xFFFFFFFFU)

typedef uint_large LParsingNodeIndex;
typedef uint_large LParsingAlternativeIndex;

typedef struct LParsingLimits {
  uint32     recovery_insertions;
  uint_large initial_capacity;
  uint_large initial_source_bytes;
  uint_large max_grammar_symbols;
  uint_large max_source_bytes;
  uint_large max_tokens;
  uint_large max_items;
  uint_large max_nodes;
  uint_large max_alternatives;
  uint_large max_diagnostics;
} LParsingLimits;

/* An original lexer result as retained by the tree's token table, with its leaf node (LPARSER_INDEX_NONE once a limit stopped leaf creation). */
typedef struct LParserToken {
  uint_large offset;
  uint32     width;
  uint32     leaf;
  uint32     kind;  /* LLexerTokenKind: kinds are consecutive small numbers, far below 2^32. */
  uint8      error; /* LLexerError */
} LParserToken;

struct LParserExpectation {
  LLexerTokenKind          token_kind; /* ERROR when matching a one-byte class. */
  LContextaObservationMask symbol_mask;
  const char              *label;
  boolean                  byte_terminal; /* TRUE: symbol_mask describes a one-byte lexeme. */
};

struct LParserDiagnostic {
  LParserDiagnosticKind kind;
  uint_large            byte_offset;
  uint_large            byte_width;
  LLexerTokenKind       actual_kind;
  LLexerError           lexical_error;
  uint_large            expected_count;
  uint32                missing_token_count;
};

typedef struct LParsingPolicy {
  LLexerTokenKind *kinds;
  uint32           count;
} LParsingPolicy;

typedef struct LParsingSymbol {
  uint_large             source;
  uint_large             policy;
  uint_large             first_production;
  uint_large             pratt; /* Index into LParser.pratts for LCOK_PRATT, else LPARSER_INDEX_NONE. */
  LContextaOperationKind kind;
  boolean                is_rule;
  uint16                 marker; /* 1 + the index in LParser.precedences of a rule with a precedence, else 0. */
  const char            *label;
  LParserExpectation     terminal;
} LParsingSymbol;

/* ---------------------------------------------------------------------------- */
/* LANGUAGE TOOLING: PARSING, PRATT                                              */
/* ---------------------------------------------------------------------------- */

typedef struct LParsingPrattEntry {
  LContextaPrattKind kind;
  const char        *label; /* Parser-owned; nullable. */
  LLexerTokenKind    token_kind;
  LLexerTokenKind    close_kind;
  LLexerTokenKind    separator_kind;
  uint32             precedence;
  int32              left_bp;  /* How strongly the operator binds its left operand. */
  int32              right_bp; /* Least binding power its right operand may continue with; negative for a barrier. */
  boolean            allow_empty;
  boolean            allow_trailing_separator;

  LParserExpectation terminal; /* For diagnostics: the entry's own token. */
  LParserExpectation close_terminal;
  LParserExpectation separator_terminal;
} LParsingPrattEntry;

/* A token kind with the entry it selects; tables are sorted by kind for binary search. */
typedef struct LParsingPrattKey {
  LLexerTokenKind kind;
  uint32          entry;
} LParsingPrattKey;

typedef struct LParsingPratt {
  LParsingPrattEntry *entries;
  uint32              entry_count;
  LParsingPrattKey   *operands; /* ATOM, PREFIX, ENCLOSED. */
  uint32              operand_count;
  LParsingPrattKey   *operators; /* INFIX_*, POSTFIX, APPLICATION, TERNARY. */
  uint32              operator_count;
} LParsingPratt;

/* A pending operator or enclosure of one run; frames form a linked stack in a shared arena. */
typedef struct LParsingPrattFrame {
  uint_large        below;    /* Frame beneath, or LPARSER_INDEX_NONE; the free-list link while unused. */
  uint_large        barrier;  /* Nearest barrier frame at or beneath this one, or LPARSER_INDEX_NONE. */
  LParsingNodeIndex children; /* Left-nested green cover of the children collected so far. */
  uint32            children_count;
  uint32            entry;
  uint32            stage; /* Enclosures: 0 just opened, 1 after a separator; ternary: 0 awaiting its second token, 1 awaiting its last operand. */
  int32             right_bp;
} LParsingPrattFrame;

/* One deterministic Pratt expression in progress, started where a chart item waits for its symbol. */
typedef struct LParsingPrattRun {
  uint_large           symbol;
  uint_large           origin; /* Chart index the run started at. */
  const LParsingPratt *table;
  uint_large           top;     /* Top frame, or LPARSER_INDEX_NONE. */
  LParsingNodeIndex    value;   /* The finished operand while operator-expecting. */
  LParsingNodeIndex    pending; /* Left-nested trivia not yet attached to a significant token. */
  boolean              expecting_operand;
  boolean              started; /* A significant token has been consumed. */
} LParsingPrattRun;

/* Which place of a rule with a precedence a production's right-hand symbol is: its first element, its last, or both. */
#define L_PARSING_ROLE_FIRST 1
#define L_PARSING_ROLE_LAST  2

typedef struct LParsingProduction {
  uint_large lhs;
  uint_large rhs[2];
  uint_large next;
  uint32     length;
  uint16     owner;   /* The marker of the rule with a precedence whose sequence this production binarizes, else 0. */
  uint8      role[2]; /* L_PARSING_ROLE_* of rhs[i] when it is a nonterminal element of that sequence, else 0. */
} LParsingProduction;

/*
The precedence of a rule, as yacc declares it with %left, %right, %nonassoc and %prec. left_open and right_open tell whether the
rule's sequence begins or ends with a nonterminal: the places where a neighbouring rule's operator can bind to it instead.
*/
typedef struct LParsingPrecedence {
  int32                  level;
  LContextaAssociativity associativity;
  boolean                left_open;
  boolean                right_open;
} LParsingPrecedence;

struct LPegProgram;

struct LParser {
  LParserEngine       engine;
  struct LPegProgram *peg; /* The compiled rule program of a backtracking parser, else NULL. */
  LLexer             *lexer;
  LParsingLimits      limits;
  NexusDataList      *symbols;
  NexusDataList      *productions;
  NexusDataList      *labels;
  NexusDataList      *pratts;      /* LParsingPratt, indexed by LParsingSymbol.pratt; fixed once creation returns. */
  NexusDataList      *precedences; /* LParsingPrecedence, indexed by LParsingSymbol.marker - 1; fixed once creation returns. */
  LParsingPolicy     *policies;
  uint_large          policy_count;
  uint_large          process_count;
  uint_large          tree_count;
};

/*
A green node is 32 bytes, and the tree it belongs to is not part of it: the getters take the tree. Everything a getter can derive stays out of it: the label, operation kind and terminal
token kind of a grammar node come from its grammar symbol, and a leaf's text is its source offset into the tree.
  aux  token and unparsed leaves: the byte offset; every other node: the grammar symbol, or LPARSER_INDEX_NONE for
       the sequence, error and source nodes and for the operator nodes of a Pratt operation
  payload  token leaves: the token kind; Pratt operator nodes: the entry label
*/
struct LParserGreenNode {
  uint8  kind;          /* LParserGreenKind */
  uint8  lexical_error; /* LLexerError; leaves only */
  uint16 reserved;
  uint32 width;
  uint32 alternative_count;
  uint32 aux;
  uint32 first_alternative;
  uint32 last_alternative;
  union {
    const char     *label;
    LLexerTokenKind token_kind;
  } payload;
};
typedef struct LParserGreenNode LParsingGreenNode;

/* One ordered child sequence of a node: no child is LPARSER_INDEX_NONE for an empty family, only the first for one child. */
struct LParserGreenAlternative {
  uint32 parent;
  uint32 children[2];
  uint32 next;
};
typedef struct LParserGreenAlternative LParsingAlternative;

typedef struct LParsingDiagnostic {
  LParserDiagnostic view;
  uint_large        expected_begin;
} LParsingDiagnostic;

struct LParserTree {
  LParser              *parser;
  byte                 *source;
  uint_large            source_size;
  uint_large            source_capacity;
  NexusDataChunkedList  nodes;
  NexusDataChunkedList  alternatives;
  NexusDataChunkedList  tokens;
  NexusDataSlabbedList *diagnostics;
  NexusDataSlabbedList *expectations;
  LParsingNodeIndex     root;
  LParserStatus         status;
  boolean               taken;
};

typedef struct LParsingItem {
  uint32 production;
  uint16 dot;    /* 0, 1 or 2: productions have at most two symbols. */
  uint16 marker; /* The precedence marker of the rule a unit-production item completes through, else 0; part of the item's identity. */
  uint32 origin;
  uint32 prefix; /* The node covering the symbols before the dot. */
  uint32 next_chart;
  uint32 next_waiter;
} LParsingItem;

typedef struct LParsingChart {
  uint32  byte_offset;
  uint32  first;
  uint32  last;
  uint32  root;
  uint32  waiter_first; /* First of this chart's waiter buckets, which are consecutive in the waiter list. */
  uint32  waiter_count;
  boolean closed;
} LParsingChart;

/* The chain of items of one chart waiting for one symbol; chains are linked through LParsingItem.next_waiter. */
typedef struct LParsingWaiter {
  uint32 symbol;
  uint32 first;
  uint32 last;
} LParsingWaiter;

struct LParserProcess {
  LParser             *parser;
  LLexerProcess       *lexer;
  LParserTree         *tree;
  NexusDataChunkedList charts;
  NexusDataChunkedList items;
  /*
  Memos of facts about the one chart under construction (items, interned nodes, alternatives), keyed by four words
  with one word of value. Every lookup targets the chart being built, so each stays small and cache-resident, and
  moving to the next chart empties it in O(1). The *_chart fields tell which chart the live entries belong to.
  */
  NexusDataWordMap    *item_memo;
  NexusDataWordMap    *node_memo;
  NexusDataWordMap    *alternative_memo;
  uint_large           item_memo_chart;
  uint_large           node_memo_chart;
  uint_large           alternative_memo_chart;
  NexusDataChunkedList waiter_list; /* LParsingWaiter: each chart's buckets are consecutive, found by a short scan. */
  NexusDataWordMap    *waiters;     /* (chart, symbol) -> bucket index, only for charts with more buckets than a scan should cover. */
  uint_large          *expectation_marks;
  NexusDataList       *pratt_runs;     /* LParsingPrattRun, active. */
  NexusDataChunkedList pratt_frames;   /* LParsingPrattFrame arena. */
  uint_large           pratt_free;     /* Head of the free frame list, or LPARSER_INDEX_NONE. */
  NexusDataList       *pratt_expected; /* const LParserExpectation *, collected from runs that ended on the current token. */
  boolean              pratt_consumed; /* A run took the current token. */
  uint_large           current_chart;
  uint_large           decided_end;
  uint_large           last_unexpected;
  boolean              ready_for_chunk;
  boolean              last_chunk;
  boolean              limited;
  boolean              finishing;
  boolean              done;
  LParserStatus        status;
};

/* ---------------------------------------------------------------------------- */
/* PARSING: SHARED BY THE PARSER, ITS PROCESSES AND THE PRATT ENGINE              */
/* ---------------------------------------------------------------------------- */

/* Summary: copies label into parser-owned storage released with the parser; NULL stays NULL. */
extern const char *logos_language_tooling_parsing_parser_label_copy(LParser *parser, const char *label);

/* Summary: returns the label of the first lexer rule producing kind, or NULL. Borrowed from the lexer. */
extern const char *logos_language_tooling_parsing_parser_token_label_get(const LParser *parser, LLexerTokenKind kind);

/*
Summary: compiles one validated Pratt operation into an immutable table.
Description: copies entries and labels into parser-owned storage, derives binding
  powers, and sorts the operand and operator roles by token kind.
Params: out_pratt  written in place; release with logos_language_tooling_parsing_pratt_destroy
*/
extern void logos_language_tooling_parsing_pratt_compile(LParser *parser, const LContextaOperation *operation, LParsingPratt *out_pratt);

/* Summary: releases the arrays of a compiled table; labels belong to the parser. */
extern void logos_language_tooling_parsing_pratt_destroy(LParsingPratt *pratt);

/*
Summary: starts a deterministic run for a chart item waiting on a Pratt symbol.
Description: the run receives the next token fed to the process. Call once per
  (origin, symbol); the chart's waiter list already holds the waiting items.
*/
extern void logos_language_tooling_parsing_pratt_run_start(LParserProcess *process, uint_large symbol, uint_large origin);

/*
Summary: gives one token to every active run from first_run onward.
Description: a run that cannot continue on the token either finishes, posting its
  completed node into chart (which must still be open), or dies, leaving the
  terminals it allowed in process->pratt_expected. Sets process->pratt_consumed
  when a run took the token. Runs that ended are removed.
*/
extern void logos_language_tooling_parsing_pratt_runs_feed(LParserProcess *process, uint_large first_run, const LLexerToken *token,
                                                           LParsingNodeIndex leaf, uint_large chart);

/* Summary: drops every run and frame, keeping capacity. */
extern void logos_language_tooling_parsing_pratt_runs_reset(LParserProcess *process);

/*
Summary: appends the terminals collected from ended runs to the tree's expectations.
Params: expected_begin  first expectation of the diagnostic being built; duplicates by kind are skipped
Returns: the number of expectations appended.
*/
extern uint_large logos_language_tooling_parsing_pratt_expectations_flush(LParserProcess *process, uint_large expected_begin);

/*
Summary: creates a fresh green node, or LPARSER_INDEX_NONE once a node limit is reached.
Description: symbol, when not LPARSER_INDEX_NONE, supplies the label, operation kind and token kind.
*/
extern LParsingNodeIndex logos_language_tooling_parsing_parser_process_node_create(LParserProcess *process, LParserGreenKind kind, uint_large width,
                                                                                   uint_large symbol);

/* Summary: returns the interned node for (category, identity, origin, end), creating it on first use. */
extern LParsingNodeIndex logos_language_tooling_parsing_parser_process_node_get(LParserProcess *process, uint_large category, uint_large identity,
                                                                                uint_large origin, uint_large end, LParserGreenKind kind,
                                                                                uint_large symbol);

/*
Summary: like node_get for a node that a rule with a precedence took part in completing; nodes of different markers stay apart, so that
  the precedence filter can tell which derivations a parent may use.
*/
extern LParsingNodeIndex logos_language_tooling_parsing_parser_process_node_marked_get(LParserProcess *process, uint_large category,
                                                                                       uint_large identity, uint_large origin, uint_large end,
                                                                                       LParserGreenKind kind, uint_large symbol, uint_large marker);

/* Summary: appends an alternative of one or two children without the duplicate lookup; the caller guarantees it is new. */
extern void logos_language_tooling_parsing_parser_process_alternative_append(LParserProcess *process, LParsingNodeIndex parent,
                                                                             LParsingNodeIndex first, LParsingNodeIndex second);

/* Summary: advances every item of the origin chart waiting on symbol over completed, into chart. */
extern void logos_language_tooling_parsing_parser_process_waiters_advance(LParserProcess *process, uint_large chart, uint_large origin,
                                                                          uint_large symbol, LParsingNodeIndex completed);

/* Summary: like waiters_advance for a node completed through the rule with that precedence marker; waiters that may not take it are skipped. */
extern void logos_language_tooling_parsing_parser_process_waiters_marked_advance(LParserProcess *process, uint_large chart, uint_large origin,
                                                                                 uint_large symbol, LParsingNodeIndex completed, uint_large marker);

/* Summary: reserves room for count more nodes and alternatives; sets the process limited and returns FALSE when it cannot. */
extern boolean logos_language_tooling_parsing_parser_process_headroom_reserve(LParserProcess *process, uint_large count);

/* ---------------------------------------------------------------------------- */
/* PARSING: BACKTRACKING ENGINE                                                  */
/* ---------------------------------------------------------------------------- */

typedef enum LPegNodeKind {
  LPN_EMPTY,     /* Matches nothing, always succeeds. */
  LPN_FAIL,      /* Always fails. */
  LPN_TOKEN,     /* token_kind; symbol is its token symbol; a is its eligible set (grammar_expected). */
  LPN_SYMBOL,    /* A one-byte token in the mask b of the program's mask list. */
  LPN_SEQ,       /* a then b. */
  LPN_CHOICE,    /* count alternatives starting at a in the program's list of node indices, resolved by mode. */
  LPN_STAR,      /* a, repeated while it succeeds with progress. */
  LPN_RULE,      /* The rule a; symbol is its rule symbol. */
  LPN_EXCLUDE,   /* a, with the global rules whose bits are set in mask suppressed between its tokens. */
  LPN_OPERATORS, /* atom a, count operators starting at b in the program's operator list. */
  LPN_EMBED      /* The embed a of the program's embed list. */
} LPegNodeKind;

typedef struct LPegNode {
  uint8           kind;
  uint8           mode;     /* LContextaChoiceMode, never LCCM_DEFAULT once compiled. */
  uint16          dispatch; /* A choice with enough alternatives that looking at the next token first pays. */
  uint32          a;
  uint32          b;
  uint32          symbol;
  uint32          count;
  LLexerTokenKind token_kind;
  uint64          mask;
} LPegNode;

/* A first token an alternative can begin with: the eligible set it is lexed with and its kind. */
typedef struct LPegProbe {
  uint32          set;
  LLexerTokenKind kind;
} LPegProbe;

#define LPEG_PROBES_UNKNOWN ((uint16)0xFFFF)

typedef struct LPegRule {
  uint32 body; /* A node index. */
  uint32 symbol;
} LPegRule;

typedef struct LPegOperator {
  uint8           kind; /* LContextaOperatorKind */
  int32           left_bp;
  int32           right_bp;
  uint32          symbol;
  LLexerTokenKind token_kind;
} LPegOperator;

typedef struct LPegEmbed {
  LContextaEmbed spec;
  uint32         symbol;
} LPegEmbed;

typedef struct LPegTokenSymbol {
  LLexerTokenKind kind;
  uint32          symbol;
} LPegTokenSymbol;

#define LPEG_GLOBALS_MAX 64

struct LPegProgram {
  NexusDataList *nodes;         /* LPegNode */
  NexusDataList *lists;         /* uint32 */
  NexusDataList *rules;         /* LPegRule */
  NexusDataList *operators;     /* LPegOperator */
  NexusDataList *masks;         /* LContextaObservationMask */
  NexusDataList *embeds;        /* LPegEmbed */
  NexusDataList *token_symbols; /* LPegTokenSymbol */
  uint32         entry_rule;
  uint32         globals[LPEG_GLOBALS_MAX]; /* Rule indices. */
  uint32         global_count;
  boolean        grammar_expected;
  uint32         depth_limit;
  uint64 *global_first;  /* The tokens a global rule can begin with: first_words uint64 (set_words with grammar_expected); NULL without globals. */
  uint32  global_set;    /* With grammar_expected: the eligible set of exactly those tokens, to lex a boundary with. */
  boolean det_ok;        /* The deterministic run applies: ordered choices, no operators or embeds, and trivia that is a single token. */
  boolean det_disabled;  /* The deterministic run failed on a document that is valid, so it is not tried again for this grammar. */
  uint64 *global_kinds;  /* Per global rule: the token kinds it consists of, first_words uint64 each; set when det_ok. */
  NexusDataList *probes; /* LPegProbe: the first tokens of alternatives, with grammar_expected. */
  uint32        *probe_start; /* Per node: where its probes begin; valid when probe_count is not LPEG_PROBES_UNKNOWN. */
  uint16        *probe_count; /* Per node: how many; LPEG_PROBES_UNKNOWN when the node may match without one of them, or has no known first tokens. */
  const struct LPegNode *node_base; /* The tables as plain arrays, valid once compiled: read by the evaluator. */
  const uint32          *list_base;
  const struct LPegRule *rule_base;
  NexusDataList         *sets;      /* Eligible token sets: set_words uint64 each. Empty unless grammar_expected. */
  uint32                 set_words; /* Words per set. */
  uint64 *
      first; /* Per node: the tokens it can begin with, first_words uint64 each. NULL with grammar_expected, where the token kind depends on the node. */
  boolean *nullable;    /* Per node: whether it can match nothing. */
  uint32   first_words; /* Words per first set. */

  /*
  With grammar_expected, the lexer tables of each (eligible set, context), built when first needed and kept for every
  process of the parser (building one costs far more than parsing a short input). Processes of one parser are driven
  from one thread at a time.
  */
  LLexerSubset **subsets;
  boolean       *subset_built;
  uint_large     subset_slots;
  uint_large     subset_contexts;
};

typedef struct LPegProgram LPegProgram;

/*
Summary: compiles a Contexta grammar into a backtracking parser over a finalized lexer.
Description: copies everything it needs, so the Contexta may be destroyed afterwards. Embedded guest parsers are borrowed.
Returns: success, or a Contexta validation error, or a configuration error such as an unknown entry rule.
*/
extern NexusError *logos_language_tooling_parsing_backtracking_compile(LLexer *lexer, const LContexta *contexta,
                                                                       const LParserConfiguration *configuration, LParser **out_parser);

/* Summary: releases a compiled program. */
extern void logos_language_tooling_parsing_backtracking_destroy(LPegProgram *program);

/* Summary: allocates an empty syntax result sized by the parser's limits. */
extern LParserTree *logos_language_tooling_parsing_parser_process_tree_create(LParser *parser);

/* Summary: empties a syntax result for reuse, keeping its capacity. */
extern void logos_language_tooling_parsing_parser_process_tree_clear(LParserTree *tree);

/* Summary: appends source bytes to a tree, growing its buffer, or reports the source limit. */
extern NexusError *logos_language_tooling_parsing_parser_process_source_append(LParserTree *tree, const LParser *parser, const byte *chunk,
                                                                               uint_large chunk_size);

/* Summary: the process entry points of the backtracking engine; the shared parser API dispatches to them by the parser's engine. */
extern LParserProcess *logos_language_tooling_parsing_backtracking_process_create(LParser *parser);
extern void            logos_language_tooling_parsing_backtracking_process_destroy(LParserProcess *handle);
extern void            logos_language_tooling_parsing_backtracking_process_reset(LParserProcess *handle);
extern NexusError     *logos_language_tooling_parsing_backtracking_process_chunk_add(LParserProcess *handle, const byte *chunk, uint_large chunk_size,
                                                                                     boolean is_last_chunk);
extern LParserStatus   logos_language_tooling_parsing_backtracking_process_parse(LParserProcess *handle, uint_large token_budget);
extern LParserTree    *logos_language_tooling_parsing_backtracking_process_tree_take(LParserProcess *handle);

/* The first member of every process, so the shared API can read the engine of a process of either kind. */
typedef struct LParserProcessHeader {
  LParser *parser;
} LParserProcessHeader;

/* ---------------------------------------------------------------------------- */
/* GENESIS: SOURCES, DIAGNOSTICS, SYNTAX                                         */
/* ---------------------------------------------------------------------------- */

/* ---- syntax tree ---- */

typedef enum LGExprKind {
  LGE_EMPTY,    /* Matches nothing; also the value of an omitted optional. */
  LGE_STRING,   /* A single-quoted lexical literal: codepoints. */
  LGE_RANGE,    /* 'a'..'z': codepoints lo and hi. */
  LGE_TOKEN,    /* $name: a token reference. */
  LGE_FRAGMENT, /* $_name: a fragment reference. */
  LGE_RULE,     /* #name, optionally qualified by an import alias. */
  LGE_PARAM,    /* $$name: a template parameter. */
  LGE_CALL,     /* A template invocation with named arguments. */
  LGE_UNION,    /* items, alternatives of || */
  LGE_CONCAT,   /* items, in sequence */
  LGE_REPEAT,   /* child, with repeat_op one of ? * + or { for a count (lo, hi) */
  LGE_GROUP,    /* child, with annotations: a parenthesized group */
  LGE_IF,       /* @if (condition) child @else else_branch */
  LGE_COMPARE,  /* children[0] == or != children[1]; compare_op is '=' or '!' */
  LGE_NONE,     /* the value none */
  LGE_ENUM,     /* Enum.MEMBER: name is the member, qualifier the enum */
  LGE_INT,
  LGE_TEXT,  /* a double-quoted string value */
  LGE_IDENT, /* a bare identifier value */
  LGE_ANY,   /* `.`: any one character (lexical) */
  LGE_NOT    /* `!x`: any one character that x does not match; child is a character or a set of characters (lexical) */
} LGExprKind;

typedef struct LGExpr LGExpr;

typedef struct LGAnnotation {
  const char *name; /* priority, exclude, choice */
  LGExpr    **arguments;
  uint32      argument_count;
  PharosSpan  span;
} LGAnnotation;

struct LGExpr {
  LGExprKind    kind;
  PharosSpan    span;
  const char   *name;      /* references, calls, enum members, identifiers, text */
  const char   *qualifier; /* import alias or enum name */
  char          sigil;     /* calls: '$' or '#' */
  uint32        lo;
  uint32        hi;
  uint32       *codepoints; /* LGE_STRING */
  uint32        codepoint_count;
  int64         integer;
  LGExpr      **items;     /* union and concat items; the argument values of a call */
  const char  **arg_names; /* calls: parallel to items */
  PharosSpan   *arg_spans; /* calls: the span of each argument name */
  uint32        item_count;
  LGExpr       *child;
  LGExpr       *else_branch;
  char          repeat_op;
  char          compare_op;
  LGAnnotation *annotations;
  uint32        annotation_count;
};

typedef enum LGDeclKind {
  LGD_IMPORT,
  LGD_ENUM,
  LGD_PARSER,
  LGD_CONTEXT,
  LGD_TOKEN,    /* $name = expression [@priority] [-> action] */
  LGD_FRAGMENT, /* $_name = expression */
  LGD_RULE,     /* #name [annotations] = expression */
  LGD_PRATT,    /* PRATT <#name> { atom = ..., entries } */
  LGD_EMBED,    /* EMBED <#name> { properties } */
  LGD_TEMPLATE,
  LGD_CALL,  /* a template invocation at declaration level */
  LGD_ENTRY, /* a Pratt entry: prefix, infix or postfix */
  LGD_IF     /* @if (condition) { declarations } @else { declarations } */
} LGDeclKind;

typedef enum LGActionKind {
  LGA_NONE,
  LGA_PUSH,
  LGA_POP,
  LGA_SET,
  LGA_REPLACE
} LGActionKind;

typedef struct LGParam {
  const char  *name; /* without $$ */
  PharosSpan   span;
  const char **types; /* type names: Token, Expression, string, an enum name, ... */
  uint32       type_count;
  boolean      optional;
  LGExpr      *default_value;
} LGParam;

typedef struct LGDecl LGDecl;

struct LGDecl {
  LGDeclKind    kind;
  PharosSpan    span;
  const char   *name;       /* without sigil */
  char          sigil;      /* '$' or '#' for tokens, fragments, rules, templates */
  LGExpr       *name_param; /* when the declared name is a $$parameter of a template */
  const char   *alias;      /* import alias */
  LGExpr       *expression;
  LGAnnotation *annotations;
  uint32        annotation_count;
  LGActionKind  action;
  const char   *action_target; /* the context named by push, set or replace */
  int64         action_amount; /* pop */
  PharosSpan    action_span;
  LGExpr       *priority; /* an @priority argument */
  LGDecl      **children; /* context members, template body, pratt entries, if branches */
  uint32        child_count;
  LGDecl      **else_children; /* declaration-level @else */
  uint32        else_count;
  const char  **names; /* import items; parser and embed property names; enum members */
  PharosSpan   *name_spans;
  LGExpr      **values; /* parser and embed property values */
  uint32        name_count;
  LGParam      *params;
  uint32        param_count;
  const char   *result_type;
  const char   *entry_kind; /* prefix, infix, postfix for LGD_ENTRY */
  LGExpr       *entry_token;
  LGExpr       *left_bp;
  LGExpr       *right_bp;
  LGExpr       *condition; /* LGD_IF */
  LGExpr       *call;      /* LGD_CALL and template-generated entries */
};

typedef struct LGUnit {
  LGenesisUnitKind kind;
  const char      *name;
  const char      *version;
  PharosSpan       name_span;
  LGDecl         **decls;
  uint32           decl_count;
  uint32           source;
} LGUnit;

/*
Summary: parses a source into a syntax tree, reporting problems as diagnostics.
Params: out_unit  the tree, always set (possibly partial) when the header was recognized, else NULL
Returns: TRUE when no error diagnostic was reported.
*/
extern boolean logos_languages_genesis_syntax_parse(LGenesisCompiler *compiler, NexusDataArena *arena, uint32 source, LGUnit **out_unit);

/* ---- compiler and engine ---- */

typedef struct LGRegistryEntry {
  char            *name; /* The name declared in the header. */
  char            *stem; /* The file name without its extension, when the source came from a file. */
  char            *path;
  LGenesisUnitKind kind;
  LGenesisEngine  *engine;  /* A language's engine, once compiled. */
  LGUnit          *unit;    /* A library's syntax tree, kept for template expansion. */
  NexusDataArena  *arena;   /* Owns the unit; a language's arena is released after compilation. */
  NexusDataList   *imports; /* LGImport *, kept for a library so its templates resolve names in its own scope. */
  uint32           source;
  boolean          in_progress;
} LGRegistryEntry;

struct LGenesisCompiler {
  LogosRuntime      *runtime;
  NexusDataList     *folders;   /* char *, as pointers */
  PharosDiagnostics *reporting; /* The sources and the diagnostics reported against them. */
  NexusDataList     *registry;  /* LGRegistryEntry * */
  boolean            chunk_open;
  char              *chunk_name;
  NexusDataList     *chunk_bytes; /* byte */
  uint32             import_depth;
  boolean            check_only; /* The source being compiled (not its imports) is analysed but not lowered. */
};

struct LGenesisEngine {
  uint32                holds;
  char                 *name;
  char                 *version;
  char                 *source_name;
  LGenesisUnitKind      kind;
  LLexer               *lexer;
  LParser              *parser;
  LContexta            *contexta;     /* Kept so guest parsers with other entry rules can be built for embedding. */
  const LContextaRule **global_rules; /* Into contexta. */
  uint32                global_count;
  LContextaChoiceMode   choice;
  boolean               grammar_expected;
  NexusDataList        *imports;       /* char *, as pointers */
  NexusDataList        *guests;        /* LGenesisEngine *, retained */
  NexusDataList        *guest_parsers; /* LParser *, owned: the guest parsers of this language's embeds */
  LGenesisGrammar      *grammar;       /* In the labels arena. */
  NexusDataArena       *labels;        /* Owns the label text the contexta and lexer borrow. */
};

#define LG_IMPORT_DEPTH_MAX 32

extern uint32 logos_languages_genesis_compiler_source_add(LGenesisCompiler *compiler, const char *name, const byte *text, uint_large size);
extern const PharosSource *logos_languages_genesis_compiler_source_get(const LGenesisCompiler *compiler, uint32 source);

/*
Summary: runs the whole pipeline on a source already added to the compiler: parse, imports, expansion, analysis and lowering.
Returns: the engine with one hold for the caller, or NULL when errors were reported.
*/
extern LGenesisEngine *logos_languages_genesis_compiler_unit_compile(LGenesisCompiler *compiler, uint32 source, const char *path);

/*
Summary: finds a compiled or parseable unit by file name or declared name, searching the folders when it is not registered.
Returns: the registry entry, compiled when it is a language, or NULL after reporting a diagnostic at span.
*/
extern LGRegistryEntry *logos_languages_genesis_compiler_import_resolve(LGenesisCompiler *compiler, const char *name, PharosSpan span);

/* Summary: the path of the source an IMPORT of that name finds in the search folders, or NULL; Nexus-allocated. Reads and compiles nothing. */
extern char *logos_languages_genesis_compiler_import_locate(const LGenesisCompiler *compiler, const char *name);

/* Summary: reads and parses (only) a file into a syntax tree in the arena; diagnostics of its syntax go to the compiler. FALSE when it cannot be read. */
extern boolean logos_languages_genesis_compiler_file_parse(LGenesisCompiler *compiler, NexusDataArena *arena, const char *path, LGUnit **out_unit);

extern LGenesisEngine *logos_languages_genesis_engine_create(const char *name, const char *version, const char *source_name, LGenesisUnitKind kind);

/* ---- expansion, analysis and lowering ---- */

typedef struct LGToken {
  char        *name; /* A token without its $, a fragment without its $_. */
  boolean      fragment;
  LGExpr      *expression;
  int64        priority;
  LGActionKind action;
  const char  *target;
  int64        amount;
  PharosSpan   span;
  PharosSpan   action_span;
  uint32       kind; /* Lowering: the lexer token kind of an emitting token. */
  int32        mark; /* Analysis: depth-first state. */
  boolean      used; /* Analysis: referenced by some rule or fragment. */
} LGToken;

typedef struct LGContextModel {
  char          *name;
  PharosSpan     span;
  NexusDataList *tokens; /* LGToken * */
} LGContextModel;

typedef struct LGRuleModel {
  boolean       used; /* Analysis: referenced from the entry, a global rule or another used rule. */
  boolean       nullable;
  char         *name;
  LGExpr       *expression;
  LGAnnotation *annotations;
  uint32        annotation_count;
  PharosSpan    span;
} LGRuleModel;

typedef struct LGEntryModel {
  const char *kind; /* prefix, infix, postfix */
  LGExpr     *token;
  int64       left_bp;
  int64       right_bp;
  PharosSpan  span;
} LGEntryModel;

typedef struct LGPrattModel {
  boolean        used;
  char          *name;
  LGExpr        *atom;
  NexusDataList *entries; /* LGEntryModel */
  PharosSpan     span;
} LGPrattModel;

typedef struct LGEmbedModel {
  boolean      used;
  char        *name;
  const char **keys;
  LGExpr     **values;
  uint32       count;
  PharosSpan   span;
} LGEmbedModel;

typedef struct LGEnumModel {
  char        *name;
  const char **members;
  uint32       count;
} LGEnumModel;

/* The imports of a unit as seen by expansion and analysis. */
typedef struct LGImport {
  char            *name;  /* As written in IMPORT. */
  char            *alias; /* AS alias, or NULL. */
  LGRegistryEntry *entry;
  const char     **items; /* As written, with sigils. */
  uint32           item_count;
  PharosSpan       span;
} LGImport;

typedef struct LGModel {
  LGUnit         *unit;
  NexusDataArena *arena;
  NexusDataList  *contexts; /* LGContextModel * */
  NexusDataList  *rules;    /* LGRuleModel * */
  NexusDataList  *pratts;   /* LGPrattModel * */
  NexusDataList  *embeds;   /* LGEmbedModel * */
  NexusDataList  *enums;    /* LGEnumModel * */
  NexusDataList  *imports;  /* LGImport * */
  const char    **parser_keys;
  LGExpr        **parser_values;
  uint32          parser_count;
  PharosSpan      parser_span;
  boolean         has_parser;
  uint32          next_instance; /* Hygiene counter. */
} LGModel;

/* Summary: collects the imports of a unit and compiles or parses each; returns FALSE after reporting a failure. */
extern boolean logos_languages_genesis_resolver_imports_resolve(LGenesisCompiler *compiler, LGUnit *unit, NexusDataList *out_imports);

/* Summary: expands templates and flattens a language into its contexts, rules, Pratt rules and embeds. NULL after reporting errors. */
extern LGModel *logos_languages_genesis_template_expand(LGenesisCompiler *compiler, NexusDataArena *arena, LGUnit *unit, NexusDataList *imports);

/* Summary: validates a library: its imports resolve and it declares only enums, templates and imports. */
extern boolean logos_languages_genesis_analysis_library_check(LGenesisCompiler *compiler, LGUnit *unit, NexusDataList *imports);

/* Summary: validates an expanded language. Returns FALSE after reporting errors; warnings do not fail it. */
extern boolean logos_languages_genesis_analysis_language_check(LGenesisCompiler *compiler, LGModel *model);

/* Summary: lowers an analyzed language into the engine's lexer and parser. Returns FALSE after reporting errors. */
extern boolean logos_languages_genesis_lowering_language_compile(LGenesisCompiler *compiler, LGModel *model, LGenesisEngine *engine);

extern void logos_languages_genesis_model_destroy(LGModel *model);

/* Summary: lookups over an expanded language, shared by analysis and lowering; each returns NULL when nothing matches. */
extern LGToken *logos_languages_genesis_analysis_token_find(const LGModel *model, const char *name, const LGContextModel **out_context);
extern LGToken *logos_languages_genesis_analysis_fragment_find(const LGContextModel *context, const char *name);

/* A range of code points, both ends included. */
typedef struct LGCodepointRange {
  uint32 low;
  uint32 high;
} LGCodepointRange;

/*
Summary: the set of characters a lexical expression matches, as sorted, merged ranges appended to out (LGCodepointRange), when the expression matches exactly
  one character: a character, a range, the wildcard, alternatives of those, a group, a fragment of that kind, or a negation of one.
Returns: FALSE when it does not (a sequence, a repetition, a string of several characters, a token).
*/
extern boolean         logos_languages_genesis_analysis_charset_get(const LGContextModel *context, const LGExpr *expression, NexusDataList *out);
extern LGRuleModel    *logos_languages_genesis_analysis_rule_find(const LGModel *model, const char *name);
extern LGPrattModel   *logos_languages_genesis_analysis_pratt_find(const LGModel *model, const char *name);
extern LGEmbedModel   *logos_languages_genesis_analysis_embed_find(const LGModel *model, const char *name);
extern const LGImport *logos_languages_genesis_analysis_import_find(const LGModel *model, const char *language);

/* Summary: tells whether a template of the given sigil and name is declared by unit or offered by one of its imports. */
extern boolean logos_languages_genesis_template_visible(const LGUnit *unit, const NexusDataList *imports, char sigil, const char *name);

/* Summary: tells whether a name is an enumeration visible from unit: the built-in ChoiceMode, one declared in unit or an imported one. */
extern boolean logos_languages_genesis_enum_visible(const LGUnit *unit, const NexusDataList *imports, const char *name,
                                                    const char *const **out_members, uint32 *out_count);

/* Summary: renders the expanded model into the engine's grammar description, allocated in the engine's label arena. */
extern void logos_languages_genesis_grammar_build(LGenesisEngine *engine, const LGModel *model);

struct LGenesisGrammar {
  const char           *name;
  const char           *version;
  const char *const    *contexts;
  uint32                context_count;
  LGenesisGrammarToken *tokens;
  uint32                token_count;
  LGenesisGrammarRule  *rules;
  uint32                rule_count;
  const char           *entry;
  const char *const    *globals;
  uint32                global_count;
  const char           *choice; /* Genesis text, or NULL when unset. */
  boolean               grammar_expected;
  const char           *imports; /* The IMPORT declarations of embedded languages, in Genesis syntax. */
  LGenesisGrammarFrame *frames;
  uint32                frame_count;
};

/* ---- the Genesis spec: words and punctuation the lexer and parser use ---- */

typedef enum LGSpecWordId {
  LGSW_LANGUAGE,
  LGSW_LIBRARY,
  LGSW_VERSION,
  LGSW_IMPORT,
  LGSW_AS,
  LGSW_ENUM,
  LGSW_PARSER,
  LGSW_CONTEXT,
  LGSW_PRATT,
  LGSW_EMBED,
  LGSW_TEMPLATE,
  LGSW_ATOM,
  LGSW_PREFIX,
  LGSW_INFIX,
  LGSW_POSTFIX,
  LGSW_PUSH,
  LGSW_POP,
  LGSW_SET,
  LGSW_REPLACE,
  LGSW_NONE,
  LGSW_IF,
  LGSW_ELSE,
  LGSW_PRIORITY,
  LGSW_CHOICE,
  LGSW_EXCLUDE,
  LGSW_PREC,
  LGSW_COUNT
} LGSpecWordId;

typedef enum LGSpecPunctuationId {
  LGSP_LBRACE,
  LGSP_RBRACE,
  LGSP_LPAREN,
  LGSP_RPAREN,
  LGSP_LANGLE,
  LGSP_RANGLE,
  LGSP_LBRACKET,
  LGSP_RBRACKET,
  LGSP_COLON,
  LGSP_COMMA,
  LGSP_ARROW,
  LGSP_EQUALS_EQUALS,
  LGSP_NOT_EQUALS,
  LGSP_EQUALS,
  LGSP_QUESTION,
  LGSP_STAR,
  LGSP_PLUS,
  LGSP_MINUS,
  LGSP_DOT_DOT,
  LGSP_DOT,
  LGSP_UNION,
  LGSP_BAR,
  LGSP_BANG,
  LGSP_COUNT
} LGSpecPunctuationId;

extern const char *logos_languages_genesis_spec_word_text_get(LGSpecWordId word);

/* Summary: the longest punctuation token at the start of text; returns its length, or 0 when there is none. */
extern uint32 logos_languages_genesis_spec_punctuation_match(const byte *text, uint_large size, LGSpecPunctuationId *out_id);

/* ---------------------------------------------------------------------------- */
/* INTERCHANGE: GRAMMARS OF OTHER FORMATS                                        */
/* ---------------------------------------------------------------------------- */

/*
A grammar read from another format (ABNF, EBNF, ANTLR, tree-sitter, bison, flex) is first made into this neutral description,
then written as Genesis. The converters say what they could not carry over (notes of severity error make the whole conversion
fail; warnings are approximations; notes mention what Genesis does beyond the source format).
*/
typedef enum LIKind {
  LIK_EMPTY,
  LIK_LITERAL, /* text, length; insensitive for a literal that ignores case */
  LIK_CHARSET, /* ranges (pairs of code points), negated */
  LIK_REF,     /* text: the name of a definition */
  LIK_SEQ,
  LIK_ALT,
  LIK_STAR,
  LIK_PLUS,
  LIK_OPT
} LIKind;

typedef struct LINode LINode;

struct LINode {
  LIKind      kind;
  const char *text;
  uint_large  length;
  boolean     insensitive;
  boolean     negated;
  uint32     *ranges;
  uint32      range_count;
  LINode    **items;
  uint32      count;
  LINode     *child;
};

typedef enum LIClass {
  LIC_UNKNOWN, /* the format does not tell lexical from syntactic definitions; the emitter decides */
  LIC_TOKEN,
  LIC_FRAGMENT,
  LIC_RULE
} LIClass;

typedef enum LIAction {
  LIA_NONE,
  LIA_PUSH,
  LIA_POP,
  LIA_REPLACE
} LIAction;

typedef struct LIDefinition {
  const char            *name;
  LIClass                kind;
  LINode                *expression;
  const char            *mode; /* the lexical mode a token belongs to; NULL for the default one */
  LIAction               action;
  boolean                skip; /* the token is trivia: matched and left out of what the grammar sees */
  const char            *action_target;
  int64                  priority;
  LContextaAssociativity associativity; /* a rule written with a precedence (LCPA_NONE: none) */
  int32                  level;
} LIDefinition;

typedef enum LINoteSeverity {
  LIN_ERROR,   /* the conversion cannot be done */
  LIN_WARNING, /* done approximately, or something dropped */
  LIN_NOTE     /* informational */
} LINoteSeverity;

typedef struct LIGrammar {
  NexusDataArena arena;
  const char    *name;
  const char    *entry;
  NexusDataList *definitions; /* LIDefinition * */
  NexusDataList *modes;       /* const char * */
  NexusDataList *notes;       /* LINoteSeverity + text, as LINote */
  boolean        earley;      /* the grammar needs the Earley engine (left recursion, ambiguity) */
} LIGrammar;

typedef struct LINote {
  LINoteSeverity severity;
  const char    *text;
} LINote;

extern LIGrammar *logos_languages_interchange_grammar_create(const char *name);
extern void       logos_languages_interchange_grammar_destroy(LIGrammar *grammar);

extern LINode *logos_languages_interchange_node_new(LIGrammar *grammar, LIKind kind);
extern LINode *logos_languages_interchange_node_literal(LIGrammar *grammar, const char *text, uint_large length, boolean insensitive);
extern LINode *logos_languages_interchange_node_reference(LIGrammar *grammar, const char *name);
extern LINode *logos_languages_interchange_node_charset(LIGrammar *grammar, const uint32 *ranges, uint32 range_count, boolean negated);
extern LINode *logos_languages_interchange_node_group(LIGrammar *grammar, LIKind kind, LINode *const *items, uint32 count);
extern LINode *logos_languages_interchange_node_wrap(LIGrammar *grammar, LIKind kind, LINode *child);

/* Summary: adds a definition (name copied, expression adopted); the first one added is the entry unless one is set. */
extern LIDefinition *logos_languages_interchange_grammar_define(LIGrammar *grammar, const char *name, LIClass kind, LINode *expression);
extern void          logos_languages_interchange_grammar_note(LIGrammar *grammar, LINoteSeverity severity, const char *format, ...);
extern const char   *logos_languages_interchange_grammar_mode_add(LIGrammar *grammar, const char *name);

/* Summary: the Genesis source of the grammar (Nexus-allocated), or NULL after an error note; the notes become a report text. */
extern char *logos_languages_interchange_grammar_emit(LIGrammar *grammar);
extern char *logos_languages_interchange_grammar_report(const LIGrammar *grammar);

/* Summary: parses text with the Genesis grammar of a format (found in the compiler's search folders) and hands the syntax tree to a converter. */
typedef struct LIParsed {
  LGenesisEngine *engine;
  LParserTree    *tree;
  const byte     *source;
  uint_large      source_size;
} LIParsed;

extern boolean logos_languages_interchange_parse(LGenesisCompiler *compiler, LIGrammar *grammar, const char *format_grammar, const byte *text,
                                                 uint_large size, LIParsed *out);
extern void    logos_languages_interchange_parsed_release(LIParsed *parsed);

/*
Summary: compiles the grammar of a format once (NULL after an error note), so that many texts can be read with it; release it with
  logos_languages_genesis_engine_release once the parsed results of its texts are released too.
*/
extern LGenesisEngine *logos_languages_interchange_reader_open(LGenesisCompiler *compiler, LIGrammar *grammar, const char *format_grammar);

/* Summary: reads one text with an engine from reader_open; the result holds the engine. */
extern boolean logos_languages_interchange_reader_parse(LGenesisEngine *engine, LIGrammar *grammar, const byte *text, uint_large size, LIParsed *out);

/* Summary: the significant children of a syntax node: the rules and tokens below it, looking through sequences and expressions. */
extern uint32 logos_languages_interchange_children(const LIParsed *parsed, const LParserGreenNode *node, const LParserGreenNode **out,
                                                   uint32 capacity);
extern uint32 logos_languages_interchange_children_count(const LIParsed *parsed, const LParserGreenNode *node);

/* Summary: the significant children of a node in a Nexus-allocated array (release with nexus_memory_deallocate), and their number. */
extern const LParserGreenNode **logos_languages_interchange_children_get(const LIParsed *parsed, const LParserGreenNode *node, uint32 *out_count);

/* Summary: the name of a rule node ("" for another kind), a token node's token name, and its text (Nexus-allocated copy). */
extern const char *logos_languages_interchange_rule_name(const LIParsed *parsed, const LParserGreenNode *node);
extern const char *logos_languages_interchange_token_name(const LIParsed *parsed, const LParserGreenNode *node);
extern char       *logos_languages_interchange_token_text(const LIParsed *parsed, const LParserGreenNode *node);

typedef boolean LInterchangeConverter(LGenesisCompiler *compiler, LIGrammar *grammar, const LInterchangeInput *inputs, uint32 input_count);

extern LInterchangeConverter logos_languages_interchange_abnf_convert;
extern LInterchangeConverter logos_languages_interchange_antlr_convert;
extern LInterchangeConverter logos_languages_interchange_bison_convert;
extern LInterchangeConverter logos_languages_interchange_ebnf_convert;

/*
Summary: reads the scanner of a flex file (.l) into the neutral description: its definitions as fragments, its start conditions as modes, and
  its rules as tokens named by the `return` of their actions.
Description: patterns whose action returns nothing become skipped tokens; `BEGIN` and `yy_push_state` become token actions. The converter of a
  parser grammar (bison) calls it for each scanner file it is given, before it reads the rules, so that the tokens it names exist.
Params: named_tokens  receives the names of the tokens that were defined (const char *, owned by the grammar's arena)
Returns: FALSE after an error note.
*/
extern boolean logos_languages_interchange_flex_read(LGenesisCompiler *compiler, LIGrammar *grammar, const LInterchangeInput *input,
                                                     NexusDataList *named_tokens);
