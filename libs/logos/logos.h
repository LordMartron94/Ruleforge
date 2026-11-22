/*
Summary: Logos regular expressions and context-free grammars with an Echo runtime.
Layer: language recognition above Nexus and Echo.
See: DESIGN.md
*/
#pragma once

#include <nexus.h>
#include <echo.h>
#include <pharos.h>

/* ---------------------------------------------------------------------------- */
/* RUNTIME                                                                      */
/* ---------------------------------------------------------------------------- */

#ifndef LOGOS_ECHO_LABEL
#  define LOGOS_ECHO_LABEL "Logos"
#endif

/*
Summary: owned Logos context borrowing its caller's Echo runtime.
Ownership: destroy before the borrowed Echo runtime.
Thread-safety: synchronize runtime lifecycle and Echo configuration.
*/
typedef struct LogosRuntime LogosRuntime;

/*
Summary: creates a Logos runtime with its Echo system and logging context.
Params:
  echo  borrowed; must outlive Logos; started by its caller when logging
  parent_system  optional borrowed registered parent in echo
Returns: the owned runtime; Echo lifecycle failures panic instead of returning.
Ownership: release with logos_runtime_destroy; Logos never destroys echo.
Errors: NULL echo asserts. Echo registration/context errors panic with their
  original diagnostics; these are configuration or lifecycle programming errors.
  Nexus allocation follows its abort-on-exhaustion contract.
Thread-safety: exclusive lifecycle access to the borrowed Echo configuration.
*/
extern LogosRuntime *logos_runtime_create(EchoRuntime *echo, EchoSystem *parent_system);

/*
Summary: releases Logos's Echo bindings and destroys its runtime.
Params: runtime  consumed; all borrowed context users/clones must be released first
Ownership: consumes runtime; its handle and borrowed Echo bindings become invalid.
  Echo remains caller-owned.
Errors: NULL runtime asserts. Echo unregistration errors panic with their
  original diagnostics; these are configuration or lifecycle programming errors.
Thread-safety: exclusive lifecycle access; no borrowed users may remain.
*/
extern void logos_runtime_destroy(LogosRuntime *runtime);
/*
Summary: retrieves the borrowed Logos Echo system.
Returns: runtime-owned system, valid until Logos runtime destruction begins.
Ownership: caller must not unregister it.
Errors: NULL runtime asserts.
Thread-safety: synchronize against runtime destruction.
*/
extern EchoSystem *logos_runtime_echo_system_get(LogosRuntime *runtime);
/*
Summary: retrieves the borrowed Logos Echo logging context.
Returns: runtime-owned context, valid until Logos runtime destruction begins.
Ownership: caller must not destroy it.
Errors: NULL runtime asserts.
Thread-safety: synchronize against runtime destruction.
*/
extern EchoContext *logos_runtime_echo_context_get(LogosRuntime *runtime);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* LANGUAGES                                                                    */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/* ---------------------------------------------------------------------------- */
/* REGULA                                                                       */
/* ---------------------------------------------------------------------------- */

/*
Summary: a 256-byte alphabet mask for Regula symbol operations.
Description: four words, each using Nexus's most-significant-bit indexing.
*/
typedef uint64 LRegulaObservationMask[4];

static NEXUS_MAYBE_UNUSED boolean logos_languages_regula_observation_mask_get(const LRegulaObservationMask mask, byte observation) {
  uint8 word_index = observation / 64;
  uint8 bit_index  = observation % 64;

  return nexus_bits_uint64_get_msb(mask[word_index], bit_index);
}

static NEXUS_MAYBE_UNUSED void logos_languages_regula_observation_mask_set(LRegulaObservationMask mask, byte observation, boolean value) {
  uint8 word_index = observation / 64;
  uint8 bit_index  = observation % 64;

  mask[word_index] = nexus_bits_uint64_set_msb(mask[word_index], bit_index, value);
}

static NEXUS_MAYBE_UNUSED void logos_languages_regula_observation_mask_set_range(LRegulaObservationMask mask, byte range_start, uint16 range_end,
                                                                                 boolean value) {
  uint16 i;

  NEXUS_PROGRAM_PRECONDITION(range_start < range_end, "range_start must be smaller than range end");
  NEXUS_PROGRAM_PRECONDITION(range_end <= 256, "range_end can be at most 256");

  for (i = range_start; i < range_end; i++) {
    logos_languages_regula_observation_mask_set(mask, i, value);
  }
}

static NEXUS_MAYBE_UNUSED void logos_languages_regula_observation_mask_clear(LRegulaObservationMask mask) {
  nexus_memory_bytes_clear(mask, NEXUS_SIZEOF(LRegulaObservationMask));
}

/*
Summary: inverts all 256 byte observations. Written by an LLM.
*/
static NEXUS_MAYBE_UNUSED void logos_languages_regula_observation_mask_invert(LRegulaObservationMask mask) {
  mask[0] = ~mask[0];
  mask[1] = ~mask[1];
  mask[2] = ~mask[2];
  mask[3] = ~mask[3];
}

typedef enum LRegulaOperationKind {
  /* CONSTRUCTOR */
  LROK_EMPTY_LANGUAGE,
  LROK_EMPTY_STRING,
  LROK_SYMBOL,

  /* OPERATION */
  LROK_UNIONIZE,
  LROK_CONCATENATE,
  LROK_STAR,

  LROK_COUNT
} LRegulaOperationKind;

typedef struct LRegula LRegula;

typedef struct LRegulaOperation {
  const char          *label; /* Optional for debugging. */
  LRegulaOperationKind kind;

  union {
    LRegulaObservationMask symbol_mask;

    struct {
      struct LRegulaOperation *operation_a;
      struct LRegulaOperation *operation_b;
    } unionize;

    struct {
      struct LRegulaOperation *operation_a;
      struct LRegulaOperation *operation_b;
    } concatenate;

    struct LRegulaOperation *operation_star;
  } operation_data;

  /* Internal assembly bookkeeping; clients must not modify these fields. */
  LRegula                 *owner;
  struct LRegulaOperation *assembly_parent;
  uint_large               storage_index;
  boolean                  assembly_active;
  boolean                  implicit_concatenation;
} LRegulaOperation;

struct LRegula {
  const char          *label;   /* Optional for debugging. */
  NexusSemanticVersion version; /* Optional for debugging. */

  NexusDataSlabbedList *operations;

  LRegulaOperation *root_rule; /* Kept up to date automatically. */

  boolean internal_constructing; /* Reserved for transactional regexp import. */
};

static NEXUS_MAYBE_UNUSED LRegula *logos_languages_regula_regula_create(const char *label, NexusSemanticVersion version) {
  LRegula *regula;

  regula = (LRegula *)nexus_memory_allocate(NEXUS_SIZEOF(*regula));

  regula->label                 = label;
  regula->version               = version;
  regula->root_rule             = NULL;
  regula->operations            = nexus_data_slabbed_list_create(NEXUS_SIZEOF(LRegulaOperation), 64, 64);
  regula->internal_constructing = FALSE;

  return regula;
}

static NEXUS_MAYBE_UNUSED void logos_languages_regula_regula_destroy(LRegula *regula) {
  nexus_data_slabbed_list_destroy(regula->operations);
  nexus_memory_deallocate(regula);
}

/*
Summary: appends a stored inactive operation to the regula's root.
Description: the first operation becomes the root; subsequent operations are
  implicitly concatenated with it. Normal callers use the typed add routines,
  which construct and attach in one call. All add routines return the new
  expression, not the implicit root. Unary operations replace their operand;
  binary operations consume adjacent active expressions while preserving
  surrounding expressions. Identical operands are allowed; other overlaps,
  intervening expressions, foreign or consumed operands assert. Explicit
  concatenation groups its operands into one active expression. Consumed
  handles and descendants inside compounds are no longer assembly targets.
  Assembly walks implicit parents in O(height) time.
Params: operation  an inactive node owned by regula; becomes an active expression
Returns: operation; regula->root_rule is updated internally.
Ownership: the returned pointer is borrowed from regula.
Errors: NULL regula or operation, foreign ownership, or an already active
  operation assert. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add(LRegula *regula, LRegulaOperation *operation);

/*
Summary: appends an empty-language operation to the root.
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula.
Errors: NULL regula asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_empty_language(const char *label, LRegula *regula);

/*
Summary: appends an empty-string operation to the root.
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula.
Errors: NULL regula asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_empty_string(const char *label, LRegula *regula);

/*
Summary: appends a single-symbol operation to the root.
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula.
Errors: NULL regula asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_symbol(const char *label, LRegula *regula, byte symbol);

/*
Summary: appends a symbol range with an exclusive upper bound.
Params: range_end  exclusive upper bound, up to 256; includes byte 255 when 256
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula.
Errors: NULL regula, range_start not below range_end, or range_end above 256
  asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_symbol_range(const char *label, LRegula *regula, byte range_start,
                                                                                  uint16 range_end);

/*
Summary: constructs a literal, then appends it to the root once.
Params: literal  borrowed for the call; NULL only for size zero; bytes are copied
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula.
Errors: NULL regula or a NULL nonempty literal asserts. Allocation follows
  Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_literal(const char *label, LRegula *regula, byte *literal, uint32 literal_size);

/*
Summary: combines adjacent active expressions into their union.
Params:
  operation_a  active expression; consumed by this combination
  operation_b  same as operation_a, or an adjacent nonoverlapping active expression
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula.
Errors: NULL, foreign, consumed, overlapping, or nonadjacent operands assert.
  Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_unionize(const char *label, LRegula *regula, LRegulaOperation *operation_a,
                                                                              LRegulaOperation *operation_b);

/*
Summary: combines adjacent active expressions into their concatenation.
Params:
  operation_a  active expression; consumed by this combination
  operation_b  same as operation_a, or an adjacent nonoverlapping active expression
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula.
Errors: NULL, foreign, consumed, overlapping, or nonadjacent operands assert.
  Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_concatenate(const char *label, LRegula *regula, LRegulaOperation *operation_a,
                                                                                 LRegulaOperation *operation_b);

/*
Summary: replaces an active expression with its Kleene star.
Params: operation  consumed as an active expression; its node remains regula-owned
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula.
Errors: NULL regula or operand, or an operand outside the current active
  expression, asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_star(const char *label, LRegula *regula, LRegulaOperation *operation);

/*
Summary: replaces an active expression with operation followed by its star.
Params: operation  consumed as an active expression; its node remains regula-owned
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula.
Errors: NULL regula or operand, or an operand outside the current active
  expression, asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_plus(const char *label, LRegula *regula, LRegulaOperation *operation);

/*
Summary: replaces an active expression with zero or one occurrence.
Description: unions the empty string with operation, allowing zero or one
  occurrence. The intermediate empty-string node does not update the root.
Params: operation  consumed as an active expression; its node remains regula-owned
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula. Consumed handles cannot be used for further assembly.
Errors: NULL regula or operand, or an operand outside the current active
  expression, asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_optional(const char *label, LRegula *regula, LRegulaOperation *operation);

/*
Summary: replaces an active expression with a bounded or unbounded repeat.
Description: concatenates the required occurrences with optional occurrences
  up to the maximum, or with a star when the maximum is absent. Repeating
  zero times produces the empty string. Intermediate nodes do not update
  the root, and repeated occurrences share the original operand.
Params:
  operation  active expression; consumed by this transformation
  min  inclusive minimum; ignored when has_min is false, which means zero
  max  inclusive maximum; ignored when has_max is false, which means unbounded
Returns: the newly constructed operation; regula->root_rule is updated internally.
Ownership: label is borrowed for the regula's lifetime; returned nodes are
  owned by regula. Consumed handles cannot be used for further assembly.
Errors: NULL regula or operation, operands outside the current active expression,
  or min greater than max when both bounds are present assert.
Thread-safety: exclusive access to regula.
Complexity: O(max) new nodes for bounded repeats, O(min) for unbounded repeats;
  the operand subtree is shared without copying its descendants.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_repeat(const char *label, LRegula *regula, LRegulaOperation *operation,
                                                                            uint16 min, uint16 max, boolean has_min, boolean has_max);

/*
Summary: appends one character-class operation. Written by an LLM.
Params:
  mask  borrowed for the call; copied into the node
  negated  complements the mask over all 256 bytes when true
Returns: the new operation, which is automatically appended to the root.
Ownership: label is borrowed; regula owns the result and the copied mask.
Errors: NULL regula or mask asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to regula.
*/
extern LRegulaOperation *logos_languages_regula_regula_operation_add_symbol_mask(const char *label, LRegula *regula,
                                                                                 const LRegulaObservationMask mask, boolean negated);

/*
Summary: identifies Logos's regexp errors. Written by an LLM.
*/
#define LOGOS_ERROR_LIBRARY_IDENTIFIER     NEXUS_PROGRAM_ERROR_LIBRARY_IDENTIFIER_CREATE('L', 'O', 'G', 'S')
#define LOGOS_REGULA_REGEXP_ERROR_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('R', 'G', 'E', 'X')
#define LOGOS_REGULA_REGEXP_ERROR_LIST(X, library, facility)                                                                                         \
  X(LOGOS_REGULA_REGEXP_INVALID, 0, 1, library, facility)                                                                                            \
  X(LOGOS_REGULA_REGEXP_UNSUPPORTED, 0, 2, library, facility)                                                                                        \
  X(LOGOS_REGULA_REGEXP_LIMIT, 0, 3, library, facility)                                                                                              \
  X(LOGOS_REGULA_REGEXP_INVALID_GRAPH, 0, 4, library, facility)
NEXUS_ERROR_FACILITY_DEFINE(LRegulaRegexpError, LOGOS_ERROR_LIBRARY_IDENTIFIER, LOGOS_REGULA_REGEXP_ERROR_FACILITY, LOGOS_REGULA_REGEXP_ERROR_LIST)

/*
Summary: bounds regexp text, group nesting, and expansion. Written by an LLM.
*/
#define LOGOS_REGULA_REGEXP_MAX_SIZE       ((uint_large)16u * 1024u * 1024u)
#define LOGOS_REGULA_REGEXP_MAX_DEPTH      256u
#define LOGOS_REGULA_REGEXP_MAX_OPERATIONS 1048576u

/*
Summary: parses and appends a regexp string atomically. Written by an LLM.
Description: accepts byte literals, grouping () and (?:), alternation, dot
  (all 256 bytes), classes and negated classes, *, +, ?, and {m}, {m,n},
  {m,}, {,n}. Counts are at most 65535. Empty expressions mean the empty
  string; [] means the empty language and [^] means any byte.
  Escapes include hex bytes \xHH, n, r, t, f, v, escaped punctuation, and
  ASCII d/D, s/S, w/W classes. Backspace \b is accepted only inside classes.
  Assertions, backreferences, lookaround, flags, Unicode properties, POSIX
  classes, and lazy/possessive modifiers return UNSUPPORTED. Malformed syntax
  returns INVALID. No part of the language changes on a returned error.
Params:
  regexp  borrowed NUL-terminated string; byte zero is written as \x00
  out_operation  out; NULL on failure, the new operation on success;
    must not alias the root or any language-owned state
Ownership: label is borrowed; regexp is not retained; regula owns the result.
  Nexus owns returned errors until its error pool is reset.
Errors: NULL regula, regexp, or out_operation, or out_operation aliasing the
  root, asserts. Returns the regexp
  INVALID, UNSUPPORTED, or LIMIT codes, with a byte offset in the message.
  Text over MAX_SIZE, group nesting over MAX_DEPTH, or expansion over
  MAX_OPERATIONS returns LIMIT.
Thread-safety: exclusive access to regula and the Nexus error pool.
*/
extern NEXUS_NODISCARD NexusError *logos_languages_regula_regula_operation_add_regexp(const char *label, LRegula *regula, const char *regexp,
                                                                                      LRegulaOperation **out_operation);

/*
Summary: writes the language root as a regexp string. Written by an LLM.
Description: writes the import dialect with precedence-preserving grouping,
  hex-escaped bytes and classes. An absent root is the empty language ([]). Labels are
  not serialized. Output may expand shared subexpressions and is limited to
  MAX_SIZE bytes and MAX_DEPTH nested groups; workspace is limited to
  MAX_OPERATIONS owned nodes. Iterative traversal detects cycles without recursion.
Params: out_regexp  out; owned NUL-terminated string on success, NULL on failure
Ownership: release the output with nexus_memory_deallocate. Regula is borrowed.
  Nexus owns returned errors until its error pool is reset.
Errors: NULL regula or out_regexp asserts. Returns INVALID_GRAPH for invalid
  node kinds, foreign nodes, or cycles; LIMIT for excessive output/workspace.
Thread-safety: synchronize against changes to regula and Nexus error-pool access.
*/
extern NEXUS_NODISCARD NexusError *logos_languages_regula_regula_regexp_write(const LRegula *regula, char **out_regexp);

/*
Summary: what a token is. Kinds 0 and 1 are reserved; every rule uses a kind of 2 or more.
Values:
  L_LEXER_TOKEN_KIND_EOF    the zero-width token produced once, after the last byte
  L_LEXER_TOKEN_KIND_ERROR  the kind of the span reported with LLS_ERROR for unmatched input
*/
typedef uint64 LLexerTokenKind;

#define L_LEXER_TOKEN_KIND_EOF        ((LLexerTokenKind)0)
#define L_LEXER_TOKEN_KIND_ERROR      ((LLexerTokenKind)1)
#define L_LEXER_TOKEN_KIND_FIRST_USER ((LLexerTokenKind)2)

/* ---------------------------------------------------------------------------- */
/* CONTEXTA                                                                     */
/* ---------------------------------------------------------------------------- */

/*
Summary: an owned context-free byte- or token-language grammar.
Description:
  Rules denote the least languages satisfying their productions. Union is
  unordered choice; concatenation and star have their usual language meanings.
  References support forward, mutual, left and right recursion. A cycle with
  no finite terminal derivation denotes the empty language.
  Expressions are immutable and explicitly combined; constructors do not
  concatenate automatically or select a start rule. Handles may be reused.
Ownership: owns all rules and operations; labels are borrowed until destruction.
Thread-safety: exclusive access for construction; concurrent readers require
  construction to have stopped and must finish before destruction.
*/
typedef struct LContexta          LContexta;
typedef struct LContextaRule      LContextaRule;
typedef struct LContextaOperation LContextaOperation;

/*
Summary: Contexta uses the same 256-byte terminal alphabet as Regula.
Description: use logos_languages_regula_observation_mask_* to prepare masks.
*/
typedef LRegulaObservationMask LContextaObservationMask;

/*
Summary: the primitive context-free expression constructors.
Description: plus, optional and repeat lower to these primitives.
*/
typedef enum LContextaOperationKind {
  LCOK_EMPTY_LANGUAGE,
  LCOK_EMPTY_STRING,
  LCOK_SYMBOL,
  LCOK_UNIONIZE,
  LCOK_CONCATENATE,
  LCOK_STAR,
  LCOK_REFERENCE,
  LCOK_TOKEN,
  LCOK_ALLOWED_TOKENS,
  LCOK_PRATT,
  LCOK_CHOICE,
  LCOK_EXCLUDE,
  LCOK_OPERATORS,
  LCOK_EMBED,
  LCOK_COUNT
} LContextaOperationKind;

typedef struct LParser LParser;

/*
Summary: how a choice between alternatives is resolved by the backtracking engine.
Values:
  LCCM_ORDERED  try alternatives in order and take the first that strictly succeeds, even on a prefix
  LCCM_LONGEST  evaluate every alternative and take the one consuming the most bytes; ties go to the earlier
  LCCM_UNAMBIGUOUS  require exactly one alternative to succeed; two or more are an ambiguity diagnostic
  LCCM_DEFAULT  use the parser's default mode
The Earley engine keeps every alternative and ignores the mode.
*/
typedef enum LContextaChoiceMode {
  LCCM_DEFAULT,
  LCCM_ORDERED,
  LCCM_LONGEST,
  LCCM_UNAMBIGUOUS,
  LCCM_COUNT
} LContextaChoiceMode;

/*
Summary: the roles of an operator entry of a binding-power expression.
Values:
  LCOP_PREFIX  token operand; the operand is parsed with right_bp as the minimum
  LCOP_INFIX  operand token operand; continues while left_bp is at least the minimum, the right operand uses right_bp
  LCOP_POSTFIX  operand token; continues while left_bp is at least the minimum
*/
typedef enum LContextaOperatorKind {
  LCOP_PREFIX,
  LCOP_INFIX,
  LCOP_POSTFIX,
  LCOP_COUNT
} LContextaOperatorKind;

/*
Summary: one operator of a binding-power expression.
Description: an infix entry with left_bp 30 and right_bp 31 groups left-associatively and one with 80 and 80
  groups right-associatively. Higher powers bind tighter. Powers are signed and unrelated to lexical priority.
Fields:
  label  nullable; names the syntax node the operator produces; borrowed for the grammar's lifetime
  token_kind  the operator token; at least FIRST_USER
  left_bp  infix and postfix; ignored for prefix
  right_bp  infix and prefix; ignored for postfix
*/
typedef struct LContextaOperatorEntry {
  LContextaOperatorKind kind;
  const char           *label;
  LLexerTokenKind       token_kind;
  int32                 left_bp;
  int32                 right_bp;
} LContextaOperatorEntry;

/*
Summary: how an embedded language reads its input.
Values:
  LCEM_REGION  the bytes between a host open token and a host close token
  LCEM_TOKEN  the interior of one host token: prefix_bytes skipped at its start and suffix_bytes at its end
*/
typedef enum LContextaEmbedMode {
  LCEM_REGION,
  LCEM_TOKEN,
  LCEM_COUNT
} LContextaEmbedMode;

typedef enum LContextaEmbedBoundary {
  LCEB_ANY,
  LCEB_LINE_START,
  LCEB_COUNT
} LContextaEmbedBoundary;

/*
Summary: the specification of an embedded-language operation.
Fields:
  guest  the compiled guest parser, whose entry is the embedded entry rule; borrowed and outliving the parser
  open_token, close_token  region mode: the host tokens that bound the guest source
  close_boundary  region mode: where the closing token may begin
  source_token, prefix_bytes, suffix_bytes  token mode: the host token and the interior span
*/
typedef struct LContextaEmbed {
  LContextaEmbedMode     mode;
  LParser               *guest;
  LLexerTokenKind        open_token;
  LLexerTokenKind        close_token;
  LContextaEmbedBoundary close_boundary;
  LLexerTokenKind        source_token;
  uint32                 prefix_bytes;
  uint32                 suffix_bytes;
} LContextaEmbed;

/*
Summary: the roles an operator-precedence (Pratt) table entry can play.
Description: a Pratt operation is a deterministic expression sub-language
  over lexer tokens, declared by a table of entries instead of productions.
  Higher precedence binds tighter. Entries sharing a precedence share one
  associativity. Roles:
Values:
  LCPK_ATOM  an operand of one token_kind; becomes a token leaf
  LCPK_PREFIX  token_kind operand, as in -x; precedence binds the operand
  LCPK_INFIX_LEFT  a token_kind b; a op b op c groups (a op b) op c
  LCPK_INFIX_RIGHT  a token_kind b; a op b op c groups a op (b op c)
  LCPK_INFIX_NONE  a token_kind b; chaining a op b op c ends the expression
    at the second operator, leaving it to the surrounding grammar
  LCPK_POSTFIX  a token_kind, as in x!
  LCPK_ENCLOSED  token_kind expression close_kind, an operand such as (x);
    with separator_kind it holds a separated list such as [x, y]
  LCPK_APPLICATION  a token_kind list close_kind, as call f(x, y) or index a[i]
  LCPK_TERNARY  a token_kind b close_kind c, right-associative, as in a ? b : c
*/
typedef enum LContextaPrattKind {
  LCPK_ATOM,
  LCPK_PREFIX,
  LCPK_INFIX_LEFT,
  LCPK_INFIX_RIGHT,
  LCPK_INFIX_NONE,
  LCPK_POSTFIX,
  LCPK_ENCLOSED,
  LCPK_APPLICATION,
  LCPK_TERNARY,
  LCPK_COUNT
} LContextaPrattKind;

#define LCONTEXTA_PRATT_TOKEN_NONE     ((LLexerTokenKind)L_LEXER_TOKEN_KIND_ERROR)
#define LCONTEXTA_PRATT_PRECEDENCE_MAX ((uint32)1000000)

/*
Summary: one entry of a Pratt operation's table.
Fields:
  kind  the role of this entry
  label  nullable; labels the syntax node an operator or enclosure produces
    and names the terminal in diagnostics; borrowed for the grammar's lifetime
  token_kind  the atom, operator, or opening token; at least FIRST_USER
  close_kind  ENCLOSED/APPLICATION closing token, or TERNARY second token;
    LCONTEXTA_PRATT_TOKEN_NONE for every other role
  separator_kind  ENCLOSED/APPLICATION element separator, or
    LCONTEXTA_PRATT_TOKEN_NONE for a single element
  precedence  at most LCONTEXTA_PRATT_PRECEDENCE_MAX; ignored by ATOM/ENCLOSED
  allow_empty  ENCLOSED/APPLICATION accept an immediately closed list: f()
  allow_trailing_separator  ENCLOSED/APPLICATION accept [x, y,]
*/
typedef struct LContextaPrattEntry {
  LContextaPrattKind kind;
  const char        *label;
  LLexerTokenKind    token_kind;
  LLexerTokenKind    close_kind;
  LLexerTokenKind    separator_kind;
  uint32             precedence;
  boolean            allow_empty;
  boolean            allow_trailing_separator;
} LContextaPrattEntry;

/*
Summary: a read-only view of one immutable Contexta operation.
Ownership: the grammar owns this view and all referenced handles;
  label text remains borrowed from the caller. Do not modify the view.
Thread-safety: shared read access after construction stops.
Fields:
  operation_data  only the member selected by kind is meaningful
*/
typedef struct LContextaOperationView {
  const char            *label;
  LContextaOperationKind kind;
  union {
    LContextaObservationMask symbol_mask;
    struct {
      const LContextaOperation *operation_a;
      const LContextaOperation *operation_b;
    } binary;
    const LContextaOperation *operation_star;
    const LContextaRule      *reference;
    LLexerTokenKind           token_kind;
    struct {
      const LContextaOperation *operation;
      const LLexerTokenKind    *kinds;
      uint32                    count;
    } allowed_tokens;
    struct {
      const LContextaPrattEntry *entries;
      uint32                     count;
    } pratt;
    struct {
      const LContextaOperation *operation_a;
      const LContextaOperation *operation_b;
      LContextaChoiceMode       mode;
    } choice;
    struct {
      const LContextaOperation   *operation;
      const LContextaRule *const *rules;
      uint32                      count;
    } exclude;
    struct {
      const LContextaOperation     *atom;
      const LContextaOperatorEntry *entries;
      uint32                        count;
    } operators;
    LContextaEmbed embed;
  } operation_data;
} LContextaOperationView;

#define LOGOS_CONTEXTA_ERROR_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('C', 'T', 'X', 'A')
#define LOGOS_CONTEXTA_ERROR_LIST(X, library, facility)                                                                                              \
  X(LOGOS_CONTEXTA_MISSING_START, 0, 1, library, facility)                                                                                           \
  X(LOGOS_CONTEXTA_UNDEFINED_RULE, 0, 2, library, facility)                                                                                          \
  X(LOGOS_CONTEXTA_RECURSIVE, 0, 3, library, facility)                                                                                               \
  X(LOGOS_CONTEXTA_NOT_BYTE_LANGUAGE, 0, 4, library, facility)
NEXUS_ERROR_FACILITY_DEFINE(LContextaError, LOGOS_ERROR_LIBRARY_IDENTIFIER, LOGOS_CONTEXTA_ERROR_FACILITY, LOGOS_CONTEXTA_ERROR_LIST)

/*
Summary: creates an empty Contexta without a start rule.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: the owned grammar.
Ownership: release with logos_languages_contexta_contexta_destroy.
Errors: allocation follows Nexus policy.
*/
extern LContexta *logos_languages_contexta_contexta_create(const char *label, NexusSemanticVersion version);

/*
Summary: releases a Contexta and every rule and operation it owns.
Ownership: consumes contexta; all its handles and views become invalid.
Errors: NULL contexta asserts.
Thread-safety: exclusive access; all readers must have stopped.
*/
extern void logos_languages_contexta_contexta_destroy(LContexta *contexta);

/*
Summary: declares a nonterminal before its production is defined.
Description: rule identity is its handle, not its optional debugging label.
  Duplicate labels are allowed. Declaration does not select the start rule.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: an undefined grammar-owned rule, stable until grammar destruction.
Ownership: contexta owns the returned rule.
Errors: NULL contexta asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaRule *logos_languages_contexta_contexta_rule_add(const char *label, LContexta *contexta);

/*
Summary: defines a declared nonterminal's complete production exactly once.
Description: use union to express alternative productions. The body may
  reference this rule or any other declared rule, including undefined rules.
Params:
  rule  borrowed, previously undefined rule belonging to contexta
  body  borrowed expression belonging to contexta; remains reusable
Ownership: all handles remain grammar-owned.
Errors: NULL arguments, foreign handles, or an already defined rule assert.
Thread-safety: exclusive access to contexta.
*/
extern void logos_languages_contexta_contexta_rule_define(LContexta *contexta, LContextaRule *rule, const LContextaOperation *body);

/*
Summary: how operators of one precedence level group, as the %left, %right and %nonassoc of yacc declare them.
Values:
  LCPA_NONE      no associativity; a rule without precedence
  LCPA_LEFT      a op b op c groups as (a op b) op c
  LCPA_RIGHT     a op b op c groups as a op (b op c)
  LCPA_NONASSOC  a op b op c is not a sentence
*/
typedef enum LContextaAssociativity {
  LCPA_NONE,
  LCPA_LEFT,
  LCPA_RIGHT,
  LCPA_NONASSOC
} LContextaAssociativity;

/*
Summary: gives a rule a precedence level and an associativity, which the Earley engine uses to drop the derivations a yacc grammar resolves.
Description: the rule's body must be one sequence. Where such a rule begins or ends with a nonterminal and a neighbouring rule of the same
  kind sits in that place, the engine keeps the grouping yacc would have chosen by comparing the levels: the higher level binds tighter, and
  equal levels group by associativity. Rules without precedence are never filtered. The backtracking engine ignores precedence.
Params:
  rule         borrowed rule belonging to a contexta
  associativity  LCPA_LEFT, LCPA_RIGHT or LCPA_NONASSOC
  level        compared between rules; larger binds tighter
Errors: NULL rule or LCPA_NONE asserts.
Thread-safety: exclusive access to the owning contexta.
*/
extern void logos_languages_contexta_contexta_rule_precedence_set(LContextaRule *rule, LContextaAssociativity associativity, int32 level);

/* Summary: the associativity of a rule (LCPA_NONE when it has no precedence) and, through out_level, its level. */
extern LContextaAssociativity logos_languages_contexta_contexta_rule_precedence_get(const LContextaRule *rule, int32 *out_level);

/*
Summary: selects or changes the grammar's start nonterminal.
Params: rule  borrowed rule belonging to contexta; may still be undefined
Ownership: retains the grammar-owned handle.
Errors: NULL arguments or a foreign rule assert.
Thread-safety: exclusive access to contexta.
*/
extern void logos_languages_contexta_contexta_start_rule_set(LContexta *contexta, const LContextaRule *rule);

/*
Summary: returns the selected start rule, or NULL before one is selected.
Ownership: borrowed from contexta until its destruction.
Errors: NULL contexta asserts.
Thread-safety: synchronize against construction and destruction.
*/
extern const LContextaRule *logos_languages_contexta_contexta_start_rule_get(const LContexta *contexta);

/*
Summary: returns the number of declared rules, including undefined rules.
Ownership: borrows contexta for the call.
Errors: NULL contexta asserts.
Thread-safety: synchronize against construction and destruction.
*/
extern uint_large logos_languages_contexta_contexta_rule_count_get(const LContexta *contexta);

/*
Summary: retrieves a declared rule in declaration order.
Ownership: returned rule is borrowed from contexta until its destruction.
Errors: NULL contexta or an index outside the rule count asserts.
Thread-safety: synchronize against construction and destruction.
*/
extern const LContextaRule *logos_languages_contexta_contexta_rule_at(const LContexta *contexta, uint_large index);

/*
Summary: returns a rule's debugging label, possibly NULL.
Ownership: borrows rule; returned text remains caller-owned.
Errors: NULL rule asserts.
Thread-safety: synchronize against grammar destruction.
*/
extern const char *logos_languages_contexta_contexta_rule_label_get(const LContextaRule *rule);

/*
Summary: returns a rule's immutable body, or NULL before definition.
Ownership: borrows rule; returned body is grammar-owned.
Errors: NULL rule asserts.
Thread-safety: synchronize against construction and destruction.
*/
extern const LContextaOperation *logos_languages_contexta_contexta_rule_body_get(const LContextaRule *rule);

/*
Summary: retrieves a read-only operation view for compilers and inspection.
Ownership: borrows operation; returned view remains valid until grammar destruction.
Errors: NULL operation asserts.
Thread-safety: synchronize against grammar destruction.
*/
extern const LContextaOperationView *logos_languages_contexta_contexta_operation_view_get(const LContextaOperation *operation);

/*
Summary: checks that a start rule is selected and every declaration is defined.
Description: recursion, ambiguity, unreachable rules and unproductive rules
  are valid. This does not decide regularity, ambiguity or parser suitability.
Ownership: borrows contexta; no state is changed.
Errors: NULL contexta asserts. Returns MISSING_START or UNDEFINED_RULE.
Thread-safety: shared read access; synchronize Nexus error-pool access.
Complexity: O(rules) time, O(1) scratch space.
*/
extern NEXUS_NODISCARD NexusError *logos_languages_contexta_contexta_validate(const LContexta *contexta);

/*
Summary: constructs the empty language.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_empty_language(const char *label, LContexta *contexta);

/*
Summary: constructs the language containing only the empty string.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_empty_string(const char *label, LContexta *contexta);

/*
Summary: constructs one byte terminal.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_symbol(const char *label, LContexta *contexta, byte symbol);

/*
Summary: constructs a byte range with an exclusive upper bound.
Description: range_end may be 256 to include byte 255.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta, range_start >= range_end, or range_end > 256 asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_symbol_range(const char *label, LContexta *contexta, byte range_start,
                                                                                        uint16 range_end);

/*
Summary: constructs a copied byte class, complemented when negated.
Params: label  nullable; borrowed for the grammar's lifetime
  mask  borrowed for the call; copied into the expression
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta or mask asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_symbol_mask(const char *label, LContexta *contexta,
                                                                                       const LContextaObservationMask mask, boolean negated);

/*
Summary: constructs a copied byte literal; size zero denotes the empty string.
Params: label  nullable; borrowed for the grammar's lifetime
  literal  borrowed for the call; bytes are copied; NULL only for size zero
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta or a NULL literal with nonzero size asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
Complexity: O(literal_size) time and new nodes.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_literal(const char *label, LContexta *contexta, const byte *literal,
                                                                                   uint32 literal_size);

/*
Summary: constructs the unordered union of two expressions.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta or operands, or foreign operands, assert. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_unionize(const char *label, LContexta *contexta,
                                                                                    const LContextaOperation *operation_a,
                                                                                    const LContextaOperation *operation_b);

/*
Summary: constructs the ordered concatenation of two expressions.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta or operands, or foreign operands, assert. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_concatenate(const char *label, LContexta *contexta,
                                                                                       const LContextaOperation *operation_a,
                                                                                       const LContextaOperation *operation_b);

/*
Summary: constructs zero or more repetitions of an expression.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta or operation, or a foreign operation, asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_star(const char *label, LContexta *contexta,
                                                                                const LContextaOperation *operation);

/*
Summary: constructs one or more repetitions of an expression.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta or operation, or a foreign operation, asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_plus(const char *label, LContexta *contexta,
                                                                                const LContextaOperation *operation);

/*
Summary: constructs zero or one occurrence of an expression.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta or operation, or a foreign operation, asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_optional(const char *label, LContexta *contexta,
                                                                                    const LContextaOperation *operation);

/*
Summary: constructs a bounded or unbounded repetition.
Description: bounds are inclusive. An absent minimum is zero; an absent
  maximum is unbounded. Zero occurrences denote the empty string.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta or operation, a foreign operation, or min > max
  with both bounds present asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
Complexity: O(max) nodes with a maximum, O(min) otherwise; subtrees are shared.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_repeat(const char *label, LContexta *contexta,
                                                                                  const LContextaOperation *operation, uint16 min, uint16 max,
                                                                                  boolean has_min, boolean has_max);

/*
Summary: constructs a reference to a declared nonterminal.
Description: the rule may be undefined; it must be defined before validation.
Params: label  nullable; borrowed for the grammar's lifetime
Returns: a new grammar-owned immutable expression; no root is updated.
Ownership: operands remain grammar-owned and reusable; labels remain borrowed.
Errors: NULL contexta or rule, or a foreign rule, asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_reference(const char *label, LContexta *contexta,
                                                                                     const LContextaRule *rule);

/*
Summary: constructs a terminal matching one lexer token kind.
Params: label  nullable borrowed debugging label; must outlive contexta
Returns: a grammar-owned immutable expression; operands remain reusable.
Ownership: contexta owns the returned handle.
Errors: NULL contexta or a reserved token kind (below FIRST_USER) asserts.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_token(const char *label, LContexta *contexta, LLexerTokenKind kind);

/*
Summary: replaces the inherited default-allowed token set within an expression.
Description: defaults are ordinary token leaves, never discarded. The override
  applies inside operation; the enclosing expression still controls its outside
  boundaries. An empty set requires explicit matching inside the expression.
  References inherit this scope, including through recursion; nested wrappers
  replace it again. Explicit matches and default admission are both retained
  when both derive the input; no semantic preference is imposed.
Params:
  label  nullable borrowed debugging label; must outlive contexta
  operation  borrowed expression owned by contexta; remains reusable
  kinds  count token kinds copied for the grammar; NULL only for count zero
Returns: a new grammar-owned wrapper expression.
Ownership: contexta owns the wrapper and copied set.
Errors: NULL contexta or operation, a foreign operation, NULL nonempty kinds,
  or any reserved kind below FIRST_USER asserts. Allocation follows Nexus policy.
Thread-safety: exclusive access to contexta.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_allowed_tokens(const char *label, LContexta *contexta,
                                                                                          const LContextaOperation *operation,
                                                                                          const LLexerTokenKind *kinds, uint32 count);

/*
Summary: adds an operator-precedence (Pratt) expression defined by a table.
Description: the operation derives one expression over lexer tokens, parsed
  deterministically and iteratively: no recursion, O(1) per token. It may be
  used anywhere an operation can, so one grammar mixes Pratt expressions and
  ordinary rules; ordinary rules reach it by reference or concatenation.
  Contract of a parsed Pratt expression:
  - it takes the longest expression the table allows; a token that cannot
    continue it ends it, and the surrounding grammar must accept that token
  - it never matches empty input, and offers no alternative shorter match
  - default-allowed tokens in force around the operation are absorbed as
    trivia between its tokens and after its last token, never before its first
    token; a token that is also an entry of the current role is that entry
  - it creates one syntax node per operator or enclosure, labelled by the
    entry, with operation kind LCOK_PRATT and exactly one alternative; atoms
    stay token leaves
  - missing-token repair does not apply inside it; an unexpected token ends
    the expression and is diagnosed with the tokens the table allowed there
  Operands are tokens or nested Pratt syntax only; use an ordinary rule
  around the operation for richer constructs.
Params:
  entries  copied; labels remain borrowed
  entry_count  at least one, with at least one ATOM
Returns: the new operation, owned by contexta.
Ownership: the grammar owns the copied table.
Errors: NULL arguments, an empty table, a reserved or missing token kind,
  a precedence above LCONTEXTA_PRATT_PRECEDENCE_MAX, a separator on a role
  without lists, two operand-role entries sharing a token, two operator-role
  entries sharing a token, an operator token reused as a closing, separating
  or second-ternary token, mixed associativity at one precedence, or no ATOM
  asserts.
Thread-safety: exclusive access, as all construction.
See: DESIGN.md § 4. Internal Differentiation
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_pratt(const char *label, LContexta *contexta,
                                                                                 const LContextaPrattEntry *entries, uint32 entry_count);

/*
Summary: adds a choice between two alternatives with an explicit resolution mode.
Description: the Earley engine treats it as a union. The backtracking engine applies the mode; a left or right
  operand that is itself a choice of the same mode is part of the same alternative group.
Errors: NULL arguments, foreign operands or an invalid mode asserts.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_choice(const char *label, LContexta *contexta,
                                                                                  const LContextaOperation *operation_a,
                                                                                  const LContextaOperation *operation_b, LContextaChoiceMode mode);

/*
Summary: evaluates operation with the global rules listed (rules) suppressed between its tokens.
Description: the leading boundary before the first token belongs to the caller and is not suppressed. The Earley
  engine ignores it.
Params: rules  copied; each a rule of contexta
Errors: NULL arguments or foreign handles asserts.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_exclude(const char *label, LContexta *contexta,
                                                                                   const LContextaOperation   *operation,
                                                                                   const LContextaRule *const *rules, uint32 rule_count);

/*
Summary: adds a binding-power (Pratt) expression whose operand is any operation.
Description: the atom is evaluated for operands; prefix, infix and postfix entries combine them by binding power.
  Supported by the backtracking engine only.
Params: entries  copied; at least one; no two of one role share a token
Errors: NULL arguments, a reserved token kind, an empty or duplicated table asserts.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_operators(const char *label, LContexta *contexta,
                                                                                     const LContextaOperation     *atom,
                                                                                     const LContextaOperatorEntry *entries, uint32 entry_count);

/*
Summary: adds an embedded-language operation. Supported by the backtracking engine only.
Params: embed  copied; its guest parser is borrowed
Errors: NULL arguments, a reserved token kind or a NULL guest asserts.
*/
extern LContextaOperation *logos_languages_contexta_contexta_operation_add_embed(const char *label, LContexta *contexta, const LContextaEmbed *embed);

/*
Summary: imports Regula's byte-regexp dialect as a Contexta expression.
Params:
  label  nullable; borrowed for the grammar's lifetime
  regexp  borrowed NUL-terminated text; not retained
  out_operation  out; receives a grammar-owned expression or NULL on failure
Ownership: contexta owns all imported nodes; no start rule or body is changed.
Errors: NULL arguments other than label assert. Returns Regula regexp errors
  with identical limits. Returned failures leave the grammar unchanged.
Thread-safety: exclusive access to contexta and the Nexus error pool.
Complexity: O(imported nodes) time and scratch space, plus regexp parsing.
*/
extern NEXUS_NODISCARD NexusError *logos_languages_contexta_contexta_operation_add_regexp(const char *label, LContexta *contexta, const char *regexp,
                                                                                          LContextaOperation **out_operation);

/*
Summary: converts a grammar with acyclic reachable references to Regula.
Description: validates all declarations, then substitutes reachable rule
  references while preserving shared expression interiors. Recursive rules
  unreachable from the start are ignored by conversion. No exact regularity
  decision is attempted: recursive grammars may still denote regular languages.
  The resulting Regula can be passed to the existing lexer and NFA compiler.
Params: out_regula  out; receives an owned Regula, or NULL on failure
Ownership: input is borrowed and unchanged; destroy output with Regula's
  destroy routine. Labels remain borrowed from the input's label owners;
  rule-reference labels are omitted during substitution.
Errors: NULL contexta or out_regula asserts. Returns MISSING_START,
  UNDEFINED_RULE, RECURSIVE, or NOT_BYTE_LANGUAGE for token terminals and
  allowed-token scopes. No partial output escapes on failure.
Thread-safety: shared read access; synchronize Nexus error-pool access.
Complexity: O(stored operations + rules) time and scratch space; iterative walk.
*/
extern NEXUS_NODISCARD NexusError *logos_languages_contexta_contexta_to_regula(const LContexta *contexta, LRegula **out_regula);

/*
Summary: exports an acyclic Contexta's start language in Regula regexp syntax.
Params: out_regexp  out; receives an owned NUL-terminated string, or NULL on failure
Ownership: input is borrowed; release output with nexus_memory_deallocate.
Errors: NULL contexta or out_regexp asserts. Returns Contexta conversion or
  Regula writer errors, including output and workspace limits.
Thread-safety: shared read access; synchronize Nexus error-pool access.
See: logos_languages_contexta_contexta_to_regula
*/
extern NEXUS_NODISCARD NexusError *logos_languages_contexta_contexta_regexp_write(const LContexta *contexta, char **out_regexp);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* AUTOMATONS                                                                   */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/* ---------------------------------------------------------------------------- */
/* NFA                                                                          */
/* ---------------------------------------------------------------------------- */

typedef struct LNFA LNFA;

/*
Summary: compiles regula into an epsilon-free NFA by Glushkov's construction and logs its statistics.
Description: the NFA has one initial state plus one state per symbol occurrence
  in regula's expression. It does not refer to regula afterwards. An empty
  regula (no operations) yields the initial state alone, which accepts nothing.
Params:
  regula   the language to compile; borrowed, not retained, not modified
  context  Echo context that receives the construction statistics; borrowed
Returns: the owned NFA.
Ownership: release with logos_automatons_nfa_destroy.
Errors: NULL regula or context asserts. A regula expanding to more than 65534
  symbol occurrences, or an unreasonable number of linearized nodes, asserts.
  Allocation follows Nexus policy.
Thread-safety: shared read access to regula; context follows Echo's rules.
Complexity: O(n * s / 64) time in n linearized nodes and s symbol occurrences.
See: DESIGN.md
*/
extern LNFA *logos_automatons_nfa_from_regula(LRegula *regula, EchoContext *context);

/*
Summary: releases an NFA.
Params: nfa  consumed
Errors: NULL nfa asserts.
*/
extern void logos_automatons_nfa_destroy(LNFA *nfa);

/*
Summary: tells whether the NFA accepts the whole of source.
Description: simulates the NFA over source one byte at a time with sets of
  states, so it never backtracks. Stops early once no state is alive.
Params: source  the bytes to match; borrowed; may be NULL only when source_size is 0
Returns: TRUE if source is in the NFA's language.
Errors: NULL nfa, or NULL source with a nonzero size, asserts.
Thread-safety: shared read access to nfa; the simulation's working sets are
  local to the call, so concurrent matches on one nfa are safe.
Complexity: O(source_size * a * w) for a active states and w words per stateset;
  one allocation of 2 * w words per call, released before it returns.
*/
extern boolean logos_automatons_nfa_match(const LNFA *nfa, const byte *source, uint_large source_size);

/* ---------------------------------------------------------------------------- */
/* DFA                                                                          */
/* ---------------------------------------------------------------------------- */

/*
Summary: a complete deterministic automaton over bytes, matched with one table lookup per byte.
Description: state 0 is the initial state and every state is reachable from
  it. The table has one column per byte class (the bytes that no state can
  tell apart), not 256 columns, and state ids are 16 bits, so a DFA holds at
  most 65535 states.
Ownership: release with logos_automatons_dfa_destroy.
*/
typedef struct LDFA LDFA;

/*
Summary: builds the DFA of nfa by subset construction and logs its statistics.
Description: every DFA state stands for a set of NFA states, found by
  breadth-first discovery from the NFA's initial state, so the DFA is
  complete and has no unreachable states. It does not refer to nfa afterwards.
  It is not minimal; see logos_automatons_dfa_minimize.
Params:
  nfa      the automaton to determinize; borrowed, not retained, not modified
  context  Echo context that receives the construction statistics and errors; borrowed
Returns: the owned DFA, or NULL when the subset construction needs more than
  65535 states (an error is logged and nothing is allocated).
Ownership: release a returned DFA with logos_automatons_dfa_destroy.
Errors: NULL nfa or context asserts. Too many states is a returned NULL, since
  the expression, not the caller, causes it. Allocation follows Nexus policy.
Thread-safety: shared read access to nfa; context follows Echo's rules.
Complexity: O(d * (m * w + c * w)) time in d DFA states, m members per state,
  w words per set and c byte classes; O(d * (c + w)) space.
See: logos_automatons_dfa_minimize
*/
extern LDFA *logos_automatons_dfa_from_nfa(const LNFA *nfa, EchoContext *context);

/*
Summary: builds the minimal DFA equivalent to dfa by Hopcroft's algorithm and logs its statistics.
Description: merges states that no input can tell apart. The result accepts
  the same language, has the fewest states any complete DFA for it can have,
  and keeps dfa's byte classes. State 0 of the result is the initial state.
  dfa is not modified; a DFA that is already minimal yields an identical copy.
Params:
  dfa      the automaton to minimize; borrowed, not retained, not modified
  context  Echo context that receives the statistics; borrowed
Returns: the owned minimal DFA.
Ownership: release with logos_automatons_dfa_destroy.
Errors: NULL dfa or context asserts. Allocation follows Nexus policy.
Thread-safety: shared read access to dfa; context follows Echo's rules.
Complexity: O(c * n * log n) time in n states and c byte classes; scratch
  space is about five times the size of dfa's table, released before returning.
See: logos_automatons_dfa_from_nfa
*/
extern LDFA *logos_automatons_dfa_minimize(const LDFA *dfa, EchoContext *context);

/*
Summary: releases a DFA.
Params: dfa  consumed
Errors: NULL dfa asserts.
*/
extern void logos_automatons_dfa_destroy(LDFA *dfa);

/*
Summary: tells whether the DFA accepts the whole of source.
Description: follows one transition per byte and stops early on the dead
  state, so it never backtracks and never allocates.
Params: source  the bytes to match; borrowed; may be NULL only when source_size is 0
Returns: TRUE if source is in the DFA's language.
Errors: NULL dfa, or NULL source with a nonzero size, asserts.
Thread-safety: shared read access to dfa; concurrent matches are safe.
Complexity: O(source_size) time, O(1) space.
*/
extern boolean logos_automatons_dfa_match(const LDFA *dfa, const byte *source, uint_large source_size);

/*
Summary: the number of states of the DFA, the dead state included.
Params: dfa  borrowed
Returns: 1 to 65535.
Errors: NULL dfa asserts.
*/
extern uint16 logos_automatons_dfa_state_count_get(const LDFA *dfa);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* LANGUAGE TOOLING                                                             */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/* ---------------------------------------------------------------------------- */
/* LEXING                                                                       */
/* ---------------------------------------------------------------------------- */

/*
Summary: a context-aware, streaming, lossless lexer over bytes.
Description:
  A lexer is configuration: contexts, each holding token rules whose
  languages are Regula expressions. Finalizing compiles every context into
  one minimized DFA, after which the lexer is immutable and any number of
  processes may run over it. A process is the execution state: a stack of
  contexts, the undecided input and the position. A token is the output.

  Execution contract, in the order it is applied to every token:
  1. Rules are evaluated only in the context on top of the process's stack;
     parent contexts contribute nothing.
  2. Longest match wins: the token is the longest prefix of the remaining
     input that any rule of the context accepts.
  3. Equal-length matches go to the higher match_priority, and equal
     priorities to the rule registered first in that context.
  4. A rule must not accept the empty string, so every token consumes at
     least one byte and the lexer always makes progress. Finalizing rejects
     such a rule.
  5. The winning rule's context action is applied after the token is
     produced, so it governs the NEXT token.

  The lexer is lossless. Nothing is skipped: whitespace and comments are
  tokens like any other, produced by rules the caller registers. Every byte
  of the source belongs to exactly one produced token or one error span, and
  the spans, in order, tile the source with no gap and no overlap, so
  concatenating their bytes reproduces the source.

  The source is a logical byte sequence supplied as chunks. Offsets are
  absolute positions in that sequence, so they do not depend on how it was
  cut into chunks. Chunks are borrowed (see logos_..._process_chunk_add);
  the lexer retains only the bytes of a token it has not yet decided, at
  most the token width limit. Extracting lexemes after a chunk is released
  is the client's concern: keep the source, or copy lexemes while the
  chunk is still valid.
Ownership: see the individual functions.
Thread-safety: a finalized LLexer is immutable and may be shared; every
  LLexerProcess needs exclusive access.
See: DESIGN.md
*/
typedef struct LLexer LLexer;

/*
Summary: identifies a context within one lexer; the value is an index, valid for that
  lexer only.
*/
typedef uint32 LLexerContext;

/*
Summary: how many contexts a process's stack can hold; the root counts as one.
Description: a push beyond it is a lexical error (LLE_CONTEXT_OVERFLOW), not a crash.
*/
#define LLEXER_CONTEXT_DEPTH_MAX 256u

/*
Summary: the default of the token width limit. See
  logos_language_tooling_lexing_lexer_token_width_limit_set.
*/
#define LLEXER_TOKEN_WIDTH_LIMIT_DEFAULT ((uint32)16u * 1024u * 1024u)

/* Summary: the token_limit of lex_many that asks for no limit. */
#define LLEXER_TOKEN_LIMIT_UNBOUNDED ((uint_large) ~(uint_large)0)

/*
Summary: what a rule does to the context stack once its token has been produced.
Description:
  The root context is the bottom of every stack and can never be removed.
Values:
  LLTCAK_NONE     leave the stack alone
  LLTCAK_PUSH     push push_context; fails when the stack already holds
    LLEXER_CONTEXT_DEPTH_MAX contexts
  LLTCAK_POP      remove pop_amount (at least 1) contexts from the top; fails unless more
    than pop_amount contexts are stacked
  LLTCAK_SET      replace everything above the root with set_context (just the root, when
    set_context is the root)
  LLTCAK_REPLACE  replace the top context with replace_context; fails when the top is the root
  LLTCAK_COUNT    the number of actions; not an action
*/
typedef enum LLexerTokenContextActionKind {
  LLTCAK_NONE,
  LLTCAK_PUSH,
  LLTCAK_POP,
  LLTCAK_SET,
  LLTCAK_REPLACE,

  LLTCAK_COUNT
} LLexerTokenContextActionKind;

/*
Summary: one token rule of a context.
Fields:
  label           optional name for diagnostics; borrowed, must outlive the lexer
  token_language  the language of the lexemes; borrowed until finalize returns, then
    never referenced again; must not accept the empty string
  token_kind      what the token is called; must not be reserved (at least
    L_LEXER_TOKEN_KIND_FIRST_USER)
  action_kind     the context action applied after the token
  match_priority  breaks ties between rules that accept the same longest match: higher
    wins, then earlier registration
  action_data     the operand of action_kind, per its member; unused for LLTCAK_NONE
*/
typedef struct LLexerTokenRule {
  const char *label;

  LRegula *token_language;

  LLexerTokenKind              token_kind;
  LLexerTokenContextActionKind action_kind;

  int32 match_priority;

  union {
    LLexerContext push_context;
    uint8         pop_amount;
    LLexerContext set_context;
    LLexerContext replace_context;
  } action_data;
} LLexerTokenRule;

/*
Summary: identifies the lexer's compile failures. Written by an LLM.
Description: RULE_EMPTY_MATCH is a rule whose language accepts the empty
  string; STATE_LIMIT is a context whose automaton needs more than 65535 states.
*/
#define LOGOS_LEXER_ERROR_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('L', 'E', 'X', 'R')
#define LOGOS_LEXER_ERROR_LIST(X, library, facility)                                                                                                 \
  X(LOGOS_LEXER_RULE_EMPTY_MATCH, 0, 1, library, facility)                                                                                           \
  X(LOGOS_LEXER_STATE_LIMIT, 0, 2, library, facility)
NEXUS_ERROR_FACILITY_DEFINE(LLexerCompileError, LOGOS_ERROR_LIBRARY_IDENTIFIER, LOGOS_LEXER_ERROR_FACILITY, LOGOS_LEXER_ERROR_LIST)

/*
Summary: creates an empty, unfinalized lexer.
Params: runtime  borrowed; must outlive the lexer; its Echo context receives compile statistics
Returns: the owned lexer.
Ownership: release with logos_language_tooling_lexing_lexer_destroy, after every process
  made from it.
Errors: NULL runtime asserts. Allocation follows Nexus policy.
*/
extern LLexer *logos_language_tooling_lexing_lexer_create(LogosRuntime *runtime);

/*
Summary: releases a lexer and its compiled automata.
Params: lexer  consumed; no process made from it may remain
Errors: NULL lexer asserts.
*/
extern void logos_language_tooling_lexing_lexer_destroy(LLexer *lexer);

/*
Summary: adds a context to an unfinalized lexer.
Description: a lexer has exactly one root context. It is where every process
  starts and the bottom of its stack; finalizing requires that it exists.
Params:
  label            optional name for diagnostics; borrowed, must outlive the lexer
  is_root_context  TRUE for the one root context
Returns: the new context's identifier.
Errors: NULL lexer, a lexer already finalized, or a second root context asserts.
*/
extern LLexerContext logos_language_tooling_lexing_lexer_context_create(LLexer *lexer, const char *label, boolean is_root_context);

/*
Summary: registers a token rule in a context, after the rules already registered there.
Description: the registration order is the last tie-breaker of the
  matching contract. Rules are only recorded here; whether their languages
  compile is reported by logos_language_tooling_lexing_lexer_finalize.
Params: rule  copied; its token_language is borrowed until finalize returns
Errors: NULL lexer or token_language, a lexer already finalized, an unknown
  context, a reserved token_kind, a context action whose context is unknown,
  an action_kind outside the enumeration, or a pop_amount of 0 asserts.
*/
extern void logos_language_tooling_lexing_lexer_context_token_rule_add(LLexer *lexer, LLexerContext context, LLexerTokenRule rule);

/*
Summary: bounds how many bytes the lexer examines for one token, and so how much input a
  process retains.
Description:
  A process retains the bytes of the token it has not decided. A pattern
  like a string literal with no closing quote can keep it undecided to the
  end of the source, so the retention needs a bound. When the automaton is
  still alive after limit bytes, the token is not produced: the first limit
  bytes become an error span (LLE_TOKEN_TOO_LONG) and lexing resumes after
  them. The limit also bounds the lookahead past a token when the longest
  match has to be abandoned for a shorter one. Defaults to
  LLEXER_TOKEN_WIDTH_LIMIT_DEFAULT.
Params: limit  at least 1
Errors: NULL lexer, a lexer already finalized, or a limit of 0 asserts.
*/
extern void logos_language_tooling_lexing_lexer_token_width_limit_set(LLexer *lexer, uint32 limit);

/*
Summary: compiles every context into a minimized DFA and freezes the lexer.
Description:
  After a successful finalize the lexer is immutable: adding a context or a
  rule, or setting the limit, asserts. The token languages are no longer
  referenced and may be destroyed. A failed finalize changes nothing; the
  caller may repair the rules and finalize again.
  Rules that can never win (every string they accept is accepted by a rule
  that outranks them) are logged as warnings, not rejected.
Errors: NULL lexer, a lexer already finalized, or no root context asserts.
  Returns LOGOS_LEXER_RULE_EMPTY_MATCH naming the first rule whose language
  accepts the empty string, and LOGOS_LEXER_STATE_LIMIT naming a context
  that needs more than 65535 automaton states.
Complexity: subset construction is exponential in the worst case; it is bounded by the
  state limit.
*/
extern NEXUS_NODISCARD NexusError *logos_language_tooling_lexing_lexer_finalize(LLexer *lexer);

/*
Summary: the execution state of one lexing run over a finalized lexer.
Ownership: release with logos_language_tooling_lexing_lexer_process_destroy, before the lexer.
Thread-safety: exclusive access; distinct processes over one lexer may run concurrently.
*/
typedef struct LLexerProcess LLexerProcess;

/*
Summary: a token, or an error span: a kind and a half-open byte range of the logical source.
Description: covers [lexeme_offset, lexeme_offset + lexeme_width). The width
  never exceeds the token width limit; only L_LEXER_TOKEN_KIND_EOF is empty.
Fields:
  kind           what the token is
  lexeme_offset  absolute offset of its first byte
  lexeme_width   its length in bytes
*/
typedef struct LLexerToken {
  LLexerTokenKind kind;

  uint_large lexeme_offset;
  uint32     lexeme_width;
} LLexerToken;

/*
Summary: the outcome of asking a process for a token.
Values:
  LLS_TOKEN       *out_token holds the next token
  LLS_NEED_INPUT  the next token is undecided until more input arrives: add a chunk. The
    previous chunk has been released.
  LLS_DONE        the end-of-input token was already produced; nothing is left
  LLS_ERROR       the input at hand is not a token; see logos_..._process_error_get. The
    span is consumed, so lexing may go on
*/
typedef enum LLexerStatus {
  LLS_TOKEN,
  LLS_NEED_INPUT,
  LLS_DONE,
  LLS_ERROR,

  LLS_COUNT
} LLexerStatus;

/*
Summary: why a process reported LLS_ERROR.
Values:
  LLE_NONE               no error yet
  LLE_UNRECOGNIZED       no rule of the context matches. The span is the longest prefix
    still viable for some rule, or one byte when the very first byte kills every rule;
    its kind is L_LEXER_TOKEN_KIND_ERROR
  LLE_TOKEN_TOO_LONG     the automaton was still alive after the token width limit; the
    span is the first limit bytes, kind L_LEXER_TOKEN_KIND_ERROR
  LLE_CONTEXT_OVERFLOW   the rule's push (or set or replace) would exceed
    LLEXER_CONTEXT_DEPTH_MAX contexts. The token WAS matched and consumed; it is the
    reported token, with its own kind, and the stack is unchanged
  LLE_CONTEXT_UNDERFLOW  the rule's pop or replace would remove the root. Reported like
    LLE_CONTEXT_OVERFLOW
*/
typedef enum LLexerError {
  LLE_NONE,
  LLE_UNRECOGNIZED,
  LLE_TOKEN_TOO_LONG,
  LLE_CONTEXT_OVERFLOW,
  LLE_CONTEXT_UNDERFLOW,

  LLE_COUNT
} LLexerError;

/*
Summary: a snapshot-able lexical context stack for stateless matching.
Description: copying the struct checkpoints the stack; assigning it back rolls it back. Only the first depth
  entries are meaningful, so a checkpoint may copy depth * sizeof(LLexerContext) bytes plus the depth.
*/
typedef struct LLexerStack {
  uint32        depth;
  LLexerContext contexts[LLEXER_CONTEXT_DEPTH_MAX];
} LLexerStack;

/*
Summary: a compiled subset of one context's rules, for matching only the tokens a caller allows.
Description: longest match and priority are decided over the chosen rules alone, so an excluded rule can neither
  win nor lengthen a match. Built from the finalized lexer; the lexer must outlive it.
*/
typedef struct LLexerSubset LLexerSubset;

/* Summary: starts a stack at the root context. */
extern void logos_language_tooling_lexing_lexer_stack_initialize(const LLexer *lexer, LLexerStack *stack);

/*
Summary: applies the context action of a winning rule to a stack.
Returns: LLE_NONE, or the overflow/underflow cause, in which case the stack is unchanged.
*/
extern LLexerError logos_language_tooling_lexing_lexer_stack_apply(const LLexer *lexer, LLexerStack *stack, const LLexerTokenRule *rule);

/*
Summary: compiles the rules of one context whose token kind is in kinds.
Returns: the owned subset, or NULL when its automaton needs more than 65535 states.
Ownership: release with logos_language_tooling_lexing_lexer_subset_destroy.
Errors: NULL lexer, an unfinalized lexer, or a missing context asserts.
Complexity: unites the retained rule automata, then determinizes and minimizes.
*/
extern LLexerSubset *logos_language_tooling_lexing_lexer_subset_create(const LLexer *lexer, LLexerContext context, const LLexerTokenKind *kinds,
                                                                       uint32 count);

/* Summary: releases a subset. */
extern void logos_language_tooling_lexing_lexer_subset_destroy(LLexerSubset *subset);

/*
Summary: decides the token that starts at offset of a complete source, with no process state.
Description: the same rules as a process (longest match, priority, registration order, width limit), over the whole
  source at once. The context action is NOT applied: apply *out_rule to a stack with stack_apply when the token commits.
Params:
  subset  nullable; restricts the token set, otherwise every rule of context competes
  context  the lexical context to match in
  out_rule  the winning rule for LLS_TOKEN, NULL for the end token
  out_error  the cause for LLS_ERROR
Returns: LLS_TOKEN (the end token at the end of the source) or LLS_ERROR with the error span in out_token.
Errors: NULL arguments, an unfinalized lexer, or an offset beyond the source asserts.
Complexity: O(bytes examined).
*/
extern LLexerStatus logos_language_tooling_lexing_lexer_match(const LLexer *lexer, const LLexerSubset *subset, LLexerContext context,
                                                              const byte *source, uint_large source_size, uint_large offset, LLexerToken *out_token,
                                                              const LLexerTokenRule **out_rule, LLexerError *out_error);

/*
Summary: creates a process positioned at offset 0 of an empty logical source, with the
  root context as its whole stack.
Params: lexer  borrowed; must be finalized and must outlive the process
Returns: the owned process.
Ownership: release with logos_language_tooling_lexing_lexer_process_destroy.
Errors: NULL or unfinalized lexer asserts. Allocation follows Nexus policy.
*/
extern LLexerProcess *logos_language_tooling_lexing_lexer_process_create(LLexer *lexer);

/*
Summary: releases a process, and with it the retained bytes.
Params: process  consumed
Errors: NULL process asserts.
*/
extern void logos_language_tooling_lexing_lexer_process_destroy(LLexerProcess *process);

/*
Summary: returns a process to a new, empty logical source: offset 0, only the root
  context stacked, nothing retained, no error.
Description: keeps the allocated retention buffer, so reusing a process does not allocate again.
Params: process  any state; a chunk still borrowed is released
Errors: NULL process asserts.
*/
extern void logos_language_tooling_lexing_lexer_process_reset(LLexerProcess *process);

/*
Summary: supplies the next bytes of the logical source.
Description:
  The chunk continues the source where the previous one ended. It is
  BORROWED, not copied: keep it valid and unchanged until a call to lex or
  lex_many returns LLS_NEED_INPUT (the process has by then copied the few
  bytes it still needs), LLS_DONE or LLS_ERROR with nothing more to read,
  or until the process is reset or destroyed. Tokens produced from it carry
  offsets, so the client can locate a lexeme in the chunk as
  chunk[lexeme_offset - chunk_offset], where chunk_offset is the source
  offset of the chunk's first byte.
  A chunk may be empty, and cut the source anywhere, including inside a
  token; the tokens do not depend on where the cuts are.
  Exactly one chunk is outstanding at a time, so a chunk is added only
  after the previous one yielded LLS_NEED_INPUT.
  The last chunk marks the end of the source: once it is consumed the
  process produces one L_LEXER_TOKEN_KIND_EOF token at the source's length
  and then LLS_DONE.
Params:
  chunk          borrowed; may be NULL only when chunk_size is 0
  is_last_chunk  TRUE when no byte will follow
Errors: NULL process, NULL chunk with a nonzero size, a chunk added while
  the previous one is still outstanding, or a chunk added after the last asserts.
Complexity: O(1); no bytes are copied until the process has to retain them.
*/
extern void logos_language_tooling_lexing_lexer_process_chunk_add(LLexerProcess *process, const byte *chunk, uint_large chunk_size,
                                                                  boolean is_last_chunk);

/*
Summary: produces the next token, or says why it cannot.
Description:
  Three outcomes are distinguished. LLS_TOKEN: a token was decided (a
  token can be decided before the chunk ends when no rule could match
  anything longer). LLS_NEED_INPUT: the decision depends on bytes not yet
  supplied, for instance after "=" when "==" is also a token; nothing was
  consumed and the process asks for the next chunk. LLS_ERROR: a lexical
  error, with the cause in logos_language_tooling_lexing_lexer_process_error_get.
  *out_token is written for LLS_TOKEN and LLS_ERROR only. After LLS_ERROR the
  offending span is consumed and the next call continues behind it.
Params: out_token  out; see Description
Errors: NULL process or out_token asserts. A call before any chunk was
  added, or after LLS_NEED_INPUT and before the next chunk, asserts.
Complexity: O(bytes examined). A rule set whose longest match must be
  abandoned for a shorter one re-examines those bytes for the next token, so
  the worst case is O(width limit) per token; typical sets examine each byte once.
*/
extern LLexerStatus logos_language_tooling_lexing_lexer_process_lex(LLexerProcess *process, LLexerToken *out_token);

/*
Summary: produces tokens until the chunk is used up, the end, an error, or token_limit tokens.
Description: appends to out_tokens after clearing it, so on return its
  length is the number of tokens produced by this call. Returns the status
  that stopped the batch: LLS_NEED_INPUT, LLS_DONE, LLS_ERROR (the error
  span is not appended; read it with ..._process_error_get), or LLS_TOKEN
  when token_limit tokens were produced and more may follow.
Params:
  out_tokens   list of LLexerToken; cleared first; borrowed
  token_limit  the most tokens to produce; LLEXER_TOKEN_LIMIT_UNBOUNDED for no limit; at least 1
Errors: as logos_language_tooling_lexing_lexer_process_lex; a NULL out_tokens, an element
  size other than LLexerToken's, or a limit of 0 asserts.
*/
extern LLexerStatus logos_language_tooling_lexing_lexer_process_lex_many(LLexerProcess *process, NexusDataList *out_tokens, uint_large token_limit);

/*
Summary: tells why the most recent LLS_ERROR happened and which bytes it concerns.
Params: out_token  optional out; the span as lex reported it; NULL to skip
Returns: the cause; LLE_NONE when the process has not reported an error since its
  creation or reset.
Errors: NULL process asserts.
*/
extern LLexerError logos_language_tooling_lexing_lexer_process_error_get(const LLexerProcess *process, LLexerToken *out_token);

/*
Summary: the absolute offset of the first byte that no produced token or error span has
  covered yet.
Errors: NULL process asserts.
*/
extern uint_large logos_language_tooling_lexing_lexer_process_offset_get(const LLexerProcess *process);

/*
Summary: the context on top of the process's stack, the one that lexes the next token.
Errors: NULL process asserts.
*/
extern LLexerContext logos_language_tooling_lexing_lexer_process_context_get(const LLexerProcess *process);

/* ---------------------------------------------------------------------------- */
/* LEXING: SOURCE LOCATIONS                                                     */
/* ---------------------------------------------------------------------------- */

/*
Summary: where a byte offset lies, measured the way a text editor (Sublime Text) measures it.
Description:
  Lines and columns start at 0; add 1 to show them as an editor's status bar
  does. The text is read as UTF-8, and one position carries all three
  column measures, so the caller picks the one it needs:
  column_byte       bytes from the start of the line
  column_character  Unicode code points from the start of the line, what
                    Sublime's view.rowcol reports. A combining mark or a
                    wide character is one code point, one column
  column_visual     the column with tabs expanded to the next multiple of the
                    index's tab size, what Sublime's status bar shows
  Line breaks are LF, CRLF (one break) and a lone CR, as Sublime reads a file;
  other Unicode separators are ordinary characters. The LF of a CRLF lies on the
  line it ends, after the CR.
  A leading UTF-8 byte order mark (EF BB BF) is not a column: bytes after it
  count from 0 characters, like an editor that hides it. Bytes that are not
  valid UTF-8 (a stray continuation byte, an overlong or surrogate encoding,
  or a sequence cut off by the next byte or the end) count one column each, as
  in a single-byte encoding. An offset inside a multi-byte character is
  reported at the start of that character.
Fields:
  line              0-based
  column_byte       0-based
  column_character  0-based
  column_visual     0-based
*/
typedef struct LLexerSourcePosition {
  uint_large line;
  uint_large column_byte;
  uint_large column_character;
  uint_large column_visual;
} LLexerSourcePosition;

/*
Summary: the positions of the two ends of a token. Half-open: end is the position just after the last byte, so an empty span has start equal to end.
*/
typedef struct LLexerSourceSpan {
  LLexerSourcePosition start;
  LLexerSourcePosition end;
} LLexerSourceSpan;

/*
Summary: an index of a logical source's lines and of the few bytes in them that make columns differ from byte counts.
Description:
  Lexing does not need it; a client that wants line and column adds each
  chunk here too, in order, cut anywhere (even inside a UTF-8 sequence or a
  CRLF). The index keeps no text. It stores one small record per line and one
  per tab, multi-byte character or byte order mark, so a source of ASCII text
  costs only its lines, and a position costs a binary search over the lines
  and another over the irregular bytes of one line, however long the source.
  While the index ends inside a UTF-8 sequence, or on a CR, positions at the
  very end may change when the next chunk arrives: the open sequence is read
  as invalid bytes until it completes, and a CR's break grows to CRLF.
Ownership: release with logos_language_tooling_lexing_source_index_destroy.
Thread-safety: exclusive access for adding chunks; concurrent reads of an index nobody is adding to.
*/
typedef struct LLexerSourceIndex LLexerSourceIndex;

/*
Summary: creates an empty index of an empty source.
Params: tab_size  the width of a tab stop for column_visual, 1 to 255; Sublime's default is 4
Errors: a tab_size of 0 asserts. Allocation follows Nexus policy.
*/
extern LLexerSourceIndex *logos_language_tooling_lexing_source_index_create(uint8 tab_size);

/*
Summary: releases an index.
Errors: NULL index asserts.
*/
extern void logos_language_tooling_lexing_source_index_destroy(LLexerSourceIndex *index);

/*
Summary: indexes the next bytes of the source.
Params: chunk  borrowed, not retained; may be NULL only when chunk_size is 0
Errors: NULL index, or NULL chunk with a nonzero size, asserts. A line wider
  than 4 Gi columns before a tab or multi-byte character asserts.
Complexity: O(chunk_size); allocates only to grow its tables.
*/
extern void logos_language_tooling_lexing_source_index_chunk_add(LLexerSourceIndex *index, const byte *chunk, uint_large chunk_size);

/*
Summary: how many bytes the index has seen.
Errors: NULL index asserts.
*/
extern uint_large logos_language_tooling_lexing_source_index_length_get(const LLexerSourceIndex *index);

/*
Summary: how many lines the source has so far: one more than its line breaks.
Errors: NULL index asserts.
*/
extern uint_large logos_language_tooling_lexing_source_index_line_count_get(const LLexerSourceIndex *index);

/*
Summary: resolves a byte offset to a line and its three columns.
Params: offset  at most the index's length; the length itself is the position just past the last byte
Errors: NULL arguments, or an offset beyond the indexed length, asserts.
Complexity: O(log lines + log irregular bytes of the line).
*/
extern void logos_language_tooling_lexing_source_index_position_get(const LLexerSourceIndex *index, uint_large offset,
                                                                    LLexerSourcePosition *out_position);

/*
Summary: resolves a token's half-open byte range to a span.
Params: token  any token or error span the lexer produced over the chunks this index has seen
Errors: NULL arguments, or a token reaching past the indexed length, asserts.
Complexity: as two position_get calls.
*/
extern void logos_language_tooling_lexing_source_index_span_get(const LLexerSourceIndex *index, LLexerToken token, LLexerSourceSpan *out_span);

/*
Summary: the byte range of a line's text, without its line break.
Params:
  out_start  out; offset of the line's first byte
  out_end    out; offset just past its last byte before the break
Errors: NULL arguments, or a line at or beyond the line count, asserts.
*/
extern void logos_language_tooling_lexing_source_index_line_range_get(const LLexerSourceIndex *index, uint_large line, uint_large *out_start,
                                                                      uint_large *out_end);

/*
Summary: the byte offset of a character column of a line, the inverse of position_get's column_character (Sublime's text_point).
Description: a column past the end of the line's text gives the end of the text, so the result never lies in the line break.
Params:
  line              at most the line count minus 1
  column_character  0-based, in code points
Returns: the byte offset.
Errors: NULL index, or a line at or beyond the line count, asserts.
Complexity: O(log irregular bytes of the line).
*/
extern uint_large logos_language_tooling_lexing_source_index_offset_get(const LLexerSourceIndex *index, uint_large line, uint_large column_character);

/* ---------------------------------------------------------------------------- */
/* PARSING                                                                      */
/* ---------------------------------------------------------------------------- */

/*
Summary: immutable compiled Contexta grammar borrowing a finalized lexer.
Description: preserves the derivations of a Contexta grammar over lexer tokens.
  Left/right/mutual recursion, nullable productions and ambiguity are supported.
  Contexta TOKEN matches one lexer kind; SYMBOL matches a single
  one-byte lexeme in its byte mask. The lexer alone decides token boundaries.
  No semantic actions or discarded tokens exist, and precedence arises only
  from explicit Pratt operations, which run deterministically inside the chart.
Ownership: destroys its copied grammar/labels; never destroys the lexer.
  Must outlive its processes and returned trees. Contexta may be destroyed
  after parser creation returns.
Thread-safety: compiled configuration is immutable; separate processes may run
  concurrently. Synchronize lifecycle operations, including process/tree release.
*/
typedef struct LParserProcess LParserProcess;
typedef struct LParserTree    LParserTree;

/*
Summary: the execution engine behind a parser; the parser and process API is the same for both.
Values:
  LPE_EARLEY  a generalized chart parser over Contexta: every derivation is kept, ambiguity is packed
  LPE_BACKTRACKING  a parser-driven engine with PEG semantics: ordered, longest and unambiguous choice,
    global rules between tokens, binding-power expressions over rule operands, grammar-expected lexing and
    embedded languages. Supports the whole of Contexta, including the operations the Earley engine rejects
*/
typedef enum LParserEngine {
  LPE_EARLEY,
  LPE_BACKTRACKING,
  LPE_COUNT
} LParserEngine;

/*
Summary: grammar-level behavior shared by a parser's processes.
Description: initialize with parser_configuration_initialize, then set the
  default-allowed token set if needed. Storage sizing, work bounds and repair
  strategy are managed internally.
Fields:
  allowed_tokens  copied global default set; Contexta scopes replace it locally
  allowed_token_count  number of kinds, all at least FIRST_USER
  engine  which engine parses; the default is the Earley engine
  entry  nullable; the rule parsing starts at, instead of the Contexta start rule
  choice  backtracking: the mode of choices left at LCCM_DEFAULT; LCCM_DEFAULT here means LCCM_ORDERED
  global_rules  backtracking: rules applied at every token boundary and at the document boundaries, outside
    expansions that exclude them; each must not match the empty input; copied
  grammar_expected  backtracking: lex each token with only the tokens the grammar can accept there
  depth_limit  backtracking: the most nested rule invocations; 0 selects a default
Ownership: configuration, allowed_tokens and global_rules are borrowed only during creation.
*/
typedef struct LParserConfiguration {
  const LLexerTokenKind *allowed_tokens;
  uint32                 allowed_token_count;

  LParserEngine               engine;
  const LContextaRule        *entry;
  LContextaChoiceMode         choice;
  const LContextaRule *const *global_rules;
  uint32                      global_rule_count;
  boolean                     grammar_expected;
  uint32                      depth_limit;
} LParserConfiguration;

/*
Summary: describes the latest streaming parsing outcome.
Values:
  LPS_PROGRESS  token budget used; call parse again on the same chunk
  LPS_NEED_INPUT  chunk drained; add another chunk, even if a prefix accepts
  LPS_ACCEPTED  EOF reached with a complete derivation and no diagnostics
  LPS_RECOVERED  EOF reached with a complete repaired derivation
  LPS_REJECTED  EOF reached without a complete derivation within recovery bounds
  LPS_LIMIT  an internal work/storage limit stopped parsing; retained input
    and all decided tokens remain available in a partial result
*/
typedef enum LParserStatus {
  LPS_PROGRESS,
  LPS_NEED_INPUT,
  LPS_ACCEPTED,
  LPS_RECOVERED,
  LPS_REJECTED,
  LPS_LIMIT
} LParserStatus;

#define LPARSER_TOKEN_BUDGET_UNBOUNDED ((uint_large) ~(uint_large)0)

/*
Summary: immutable syntax node owned by a completed result.
Ownership: borrowed from its tree; never allocate, modify or free separately.
Thread-safety: shared read access until tree destruction.
*/
typedef struct LParserGreenNode LParserGreenNode;

/*
Summary: one possible ordered child sequence of a syntax node.
Ownership: borrowed from its tree; never allocate, modify or free separately.
Thread-safety: shared read access until tree destruction.
*/
typedef struct LParserGreenAlternative LParserGreenAlternative;

/*
Summary: structural categories in the immutable green forest.
Values:
  LPGK_SOURCE  entire retained source, including the zero-width EOF token
  LPGK_RULE  declared Contexta nonterminal
  LPGK_EXPRESSION  one Contexta expression, or one operator node of a Pratt operation
  LPGK_SEQUENCE  ordered sequence of syntax children
  LPGK_TOKEN  original lexer token, including default-allowed tokens and EOF
  LPGK_MISSING_TOKEN  inserted zero-width expected terminal; source unchanged
  LPGK_ERROR  unexpected token wrapper or an unrecovered source root
  LPGK_UNPARSED  retained bytes not incorporated before a work/token limit
*/
typedef enum LParserGreenKind {
  LPGK_SOURCE,
  LPGK_RULE,
  LPGK_EXPRESSION,
  LPGK_SEQUENCE,
  LPGK_TOKEN,
  LPGK_MISSING_TOKEN,
  LPGK_ERROR,
  LPGK_UNPARSED
} LParserGreenKind;

/*
Summary: categories of structural and lexical diagnostics.
Values:
  LPDK_UNEXPECTED_TOKEN  a token that continues no derivation, retained in an error node
  LPDK_MISSING_TOKEN  an expected terminal was inserted with zero width
  LPDK_LEXICAL  the lexer reported a failure; see the diagnostic's lexical_error
  LPDK_UNEXPECTED_EOF  the source ended before a derivation completed
  LPDK_LIMIT  an internal work/storage limit stopped parsing at this offset
  LPDK_AMBIGUOUS  more than one alternative of an unambiguous choice succeeded
*/
typedef enum LParserDiagnosticKind {
  LPDK_UNEXPECTED_TOKEN,
  LPDK_MISSING_TOKEN,
  LPDK_LEXICAL,
  LPDK_UNEXPECTED_EOF,
  LPDK_LIMIT,
  LPDK_AMBIGUOUS
} LParserDiagnosticKind;

/*
Summary: one structural or lexical diagnostic with its source extent and repair.
Description: consecutive discarded unexpected tokens are grouped until a real
  grammar/default transition succeeds. Missing tokens have zero source width.
Ownership: borrowed from its tree; never allocate, modify or free separately.
Thread-safety: shared read access until tree destruction.
*/
typedef struct LParserDiagnostic LParserDiagnostic;

/*
Summary: one grammar-derived terminal expectation.
Description: either a lexer token kind or a one-byte symbol class, with an
  optional readable label. No message setup is required to use it.
Ownership: borrowed from its tree or parser; the parser outlives every tree.
Thread-safety: shared read access until tree destruction.
*/
typedef struct LParserExpectation LParserExpectation;

/*
Summary: a caller-owned red occurrence of a green node, with parent and position.
Description: alternative selection is explicit at each descent, preserving
  ambiguity. No wrapper allocation is needed. Cyclic forest paths remain
  visible; callers control traversal depth.
Ownership: borrows tree and parent; keep both alive and unmoved while using this
  occurrence or any child referring to it. Do not modify the fields.
*/
typedef struct LParserRedNode {
  const LParserTree             *tree;
  const struct LParserRedNode   *parent;
  const LParserGreenNode        *green;
  uint_large                     byte_offset;
  const LParserGreenAlternative *parent_alternative;
  uint32                         index_in_parent;
} LParserRedNode;

#define LOGOS_PARSER_ERROR_FACILITY NEXUS_PROGRAM_ERROR_FACILITY_IDENTIFIER_CREATE('P', 'A', 'R', 'S')
#define LOGOS_PARSER_ERROR_LIST(X, library, facility)                                                                                                \
  X(LOGOS_PARSER_GRAMMAR_LIMIT, 0, 1, library, facility)                                                                                             \
  X(LOGOS_PARSER_SOURCE_LIMIT, 0, 2, library, facility)                                                                                              \
  X(LOGOS_PARSER_UNSUPPORTED, 0, 3, library, facility)
NEXUS_ERROR_FACILITY_DEFINE(LParserError, LOGOS_ERROR_LIBRARY_IDENTIFIER, LOGOS_PARSER_ERROR_FACILITY, LOGOS_PARSER_ERROR_LIST)

/*
Summary: initializes parser behavior with an empty default-allowed token set.
Ownership: configuration is borrowed and written in place.
Errors: NULL configuration asserts.
*/
extern void logos_language_tooling_parsing_parser_configuration_initialize(LParserConfiguration *configuration);

/*
Summary: compiles Contexta into a reusable token parser over a finalized lexer.
Params:
  configuration  initialized grammar behavior; copied during this call
  out_parser  out; owns the compiled parser, or NULL on returned failure
Ownership: copies grammar, token sets and labels; borrows lexer until destroy.
Errors: NULL arguments, an unfinalized lexer, invalid allowed-token input or reserved
  allowed kinds assert. Returns Contexta validation errors or GRAMMAR_LIMIT.
Thread-safety: shared read access to grammar and lexer; synchronize Nexus pool.
Complexity: proportional to reachable operation/rule and scope combinations.
*/
extern NEXUS_NODISCARD NexusError *logos_language_tooling_parsing_parser_create(LLexer *lexer, const LContexta *contexta,
                                                                                const LParserConfiguration *configuration, LParser **out_parser);

/*
Summary: destroys a compiled parser and its owned grammar storage.
Ownership: consumes parser; never destroys its borrowed lexer.
Errors: NULL parser or outstanding processes/trees asserts.
Thread-safety: exclusive lifecycle access.
*/
extern void logos_language_tooling_parsing_parser_destroy(LParser *parser);

/*
Summary: creates a reusable parsing workspace and its owned lexer process.
Ownership: returns an owned process; destroy it before parser.
Errors: NULL parser asserts. Allocation follows Nexus policy.
Thread-safety: synchronize parser lifecycle counters.
*/
extern LParserProcess *logos_language_tooling_parsing_parser_process_create(LParser *parser);

/*
Summary: releases a process, its workspace and any result not taken by the client.
Ownership: consumes process; previously taken trees remain caller-owned.
Errors: NULL process asserts.
Thread-safety: exclusive access; synchronize parser lifecycle counters.
*/
extern void logos_language_tooling_parsing_parser_process_destroy(LParserProcess *process);

/*
Summary: restarts parsing while retaining allocated workspace capacity.
Description: invalidates an untaken result. If the previous result was taken,
  allocates new output storage; otherwise reuses it. Releases outstanding input.
Ownership: borrows process; taken trees are unchanged.
Errors: NULL process asserts.
Thread-safety: exclusive access.
*/
extern void logos_language_tooling_parsing_parser_process_reset(LParserProcess *process);

/*
Summary: copies the next source chunk and supplies it to the owned lexer process.
Description: chunk boundaries may split tokens. Input is copied once so every
  byte survives caller buffer reuse. Add only after NEED_INPUT (or initially).
  On SOURCE_LIMIT no bytes are accepted and the process remains unchanged.
Ownership: chunk is borrowed only for this call; source copy is process-owned.
Errors: NULL process, NULL nonempty chunk, an outstanding chunk, input after
  the last chunk, or input before reset after taking a result asserts.
  Returns SOURCE_LIMIT if the internal source bound would be exceeded.
Thread-safety: exclusive access.
Complexity: O(chunk_size), plus geometric buffer growth when capacity is exceeded.
*/
extern NEXUS_NODISCARD NexusError *logos_language_tooling_parsing_parser_process_chunk_add(LParserProcess *process, const byte *chunk,
                                                                                           uint_large chunk_size, boolean is_last_chunk);

/*
Summary: continues generalized parsing across input chunks.
Description: consumes up to token_budget lexer results, including EOF.
  One token's generalized work is indivisible; internal work bounds stop
  pathological ambiguity. Default-allowed tokens are ordinary leaves at
  boundaries in the current inherited scope, including completion.
  On structural failure, tries bounded missing-token insertions that permit
  the current token, then deletes an unexpected token; at EOF inserts up to
  the same internal repair bound. No bytes are synthesized or lost.
  Lexical failures always diagnose.
  Recovery is bounded structural repair, not a minimum-edit or semantic oracle.
  Token budget zero is invalid. Terminal statuses are stable until reset/take.
Ownership: borrows process; a completed immutable tree can be taken separately.
Errors: NULL process, zero budget, or parsing without an outstanding chunk
  asserts. Syntax/lexical issues are result diagnostics, never panics.
  LPS_LIMIT reports internal storage/work exhaustion, never silent truncation.
Thread-safety: exclusive access; no logging in recognition loops.
Complexity: worst-case cubic time/quadratic recognition space in token count
  for a fixed grammar; alternatives may occupy cubic space. Recovery and
  default-admission ambiguity consume the same internal work bounds.
*/
extern LParserStatus logos_language_tooling_parsing_parser_process_parse(LParserProcess *process, uint_large token_budget);

/*
Summary: transfers the terminated immutable result, including rejected/limited input.
Ownership: caller owns the result and destroys it before parser; process must
  be reset before more input. Source bytes, EOF, errors and every forest family
  remain inspectable. A limited result wraps undecided retained bytes explicitly.
Errors: NULL process, no result, or a nonterminal parsing status asserts.
Thread-safety: exclusive access; synchronize parser lifecycle counters.
*/
extern LParserTree *logos_language_tooling_parsing_parser_process_tree_take(LParserProcess *process);

/*
Summary: destroys one taken syntax result and its owned source and forest.
Ownership: consumes tree; every borrowed red/green/diagnostic view expires.
Errors: NULL tree asserts.
Thread-safety: readers must finish; synchronize parser lifecycle counters.
*/
extern void logos_language_tooling_parsing_syntax_tree_destroy(LParserTree *tree);

/*
Summary: retrieves all original source bytes, including malformed/undecided input.
Params: out_size  required out; receives byte count, with no implied terminator
Returns: tree-owned immutable bytes; empty source is valid.
Ownership: borrows tree; returned bytes expire when it is destroyed.
Errors: NULL tree or out_size asserts.
Thread-safety: shared read access.
*/
extern const byte *logos_language_tooling_parsing_syntax_tree_source_get(const LParserTree *tree, uint_large *out_size);

/*
Summary: retrieves the immutable result's terminal parsing status.
Ownership: borrows tree.
Errors: NULL tree asserts.
Thread-safety: shared read access.
*/
extern LParserStatus logos_language_tooling_parsing_syntax_tree_status_get(const LParserTree *tree);

/*
Summary: retrieves the root covering the entire retained source.
Returns: the immutable root syntax node.
Ownership: borrowed from tree until its destruction.
Errors: NULL tree asserts.
Thread-safety: shared read access.
*/
extern const LParserGreenNode *logos_language_tooling_parsing_syntax_tree_root_get(const LParserTree *tree);

/*
Summary: returns the number of original lexer results, including EOF and lexical errors.
Ownership: borrows tree.
Errors: NULL tree asserts.
Thread-safety: shared read access.
*/
extern uint_large logos_language_tooling_parsing_syntax_tree_token_count_get(const LParserTree *tree);

/* Summary: how many green nodes and packed alternatives the tree holds (the memory a parse costs is about 40 and 16 bytes of these). */
extern uint_large logos_language_tooling_parsing_syntax_tree_node_count_get(const LParserTree *tree);
extern uint_large logos_language_tooling_parsing_syntax_tree_alternative_count_get(const LParserTree *tree);

/*
Summary: copies one original lexer result by its tree-local index.
Description: results are preserved without filtering or reclassification;
  the lexeme bytes are addressed by offset/width into syntax_tree_source_get.
Params: out_token  required out; receives the token value
Returns: the lexer failure recorded for this token, or LLE_NONE.
Ownership: out_token is caller-owned and written in place.
Errors: NULL tree or out_token, or an index outside the token count, asserts.
Thread-safety: shared read access.
*/
extern LLexerError logos_language_tooling_parsing_syntax_tree_token_at(const LParserTree *tree, uint_large index, LLexerToken *out_token);

/*
Summary: returns the number of structural and lexical diagnostics.
Ownership: borrows tree.
Errors: NULL tree asserts.
Thread-safety: shared read access.
*/
extern uint_large logos_language_tooling_parsing_syntax_tree_diagnostic_count_get(const LParserTree *tree);

/*
Summary: retrieves one immutable diagnostic by its tree-local index.
Returns: the diagnostic handle; inspect it with the syntax_diagnostic getters.
Ownership: the returned handle is borrowed from tree until its destruction.
Errors: NULL tree or an index outside the corresponding count asserts.
Thread-safety: shared read access.
*/
extern const LParserDiagnostic *logos_language_tooling_parsing_syntax_tree_diagnostic_at(const LParserTree *tree, uint_large index);

/*
Summary: retrieves a terminal expectation for one diagnostic.
Params:
  diagnostic_index  selects a diagnostic in tree
  expectation_index  less than that diagnostic's expected count
Returns: the selected terminal expectation.
Ownership: returned immutable handle is borrowed from tree.
Errors: NULL tree or either index outside its respective range asserts.
Thread-safety: shared read access.
*/
extern const LParserExpectation *logos_language_tooling_parsing_syntax_tree_diagnostic_expectation_at(const LParserTree *tree,
                                                                                                      uint_large         diagnostic_index,
                                                                                                      uint_large         expectation_index);

/*
Summary: returns a syntax node's structural category.
Ownership: borrows node.
Errors: NULL node asserts.
Thread-safety: shared read access.
*/
extern LParserGreenKind logos_language_tooling_parsing_syntax_green_node_kind_get(const LParserGreenNode *node);

/*
Summary: returns the number of source bytes a syntax node covers.
Description: each of the node's alternatives has children covering exactly
  this width. Nodes have no parents or absolute positions; see red occurrences.
Ownership: borrows node.
Errors: NULL node asserts.
Thread-safety: shared read access.
*/
extern uint_large logos_language_tooling_parsing_syntax_green_node_width_get(const LParserGreenNode *node);

/*
Summary: returns a syntax node's copied grammar or lexer label.
Params: tree  the tree the node belongs to; nodes do not point back to it
Returns: the label, or NULL when the node has none.
Ownership: borrowed from the tree; valid until its destruction.
Errors: NULL node asserts.
Thread-safety: shared read access.
*/
extern const char *logos_language_tooling_parsing_syntax_green_node_label_get(const LParserTree *tree, const LParserGreenNode *node);

/*
Summary: returns the Contexta constructor a syntax node was derived from.
Params: tree  the tree the node belongs to; nodes do not point back to it
Returns: the constructor kind, or LCOK_COUNT for non-expression nodes.
Ownership: borrows node.
Errors: NULL node asserts.
Thread-safety: shared read access.
*/
extern LContextaOperationKind logos_language_tooling_parsing_syntax_green_node_operation_kind_get(const LParserTree      *tree,
                                                                                                  const LParserGreenNode *node);

/*
Summary: returns the lexer token kind of a token or missing-token leaf.
Params: tree  the tree the node belongs to; nodes do not point back to it
Returns: the kind; meaningless for other node categories.
Ownership: borrows node.
Errors: NULL node asserts.
Thread-safety: shared read access.
*/
extern LLexerTokenKind logos_language_tooling_parsing_syntax_green_node_token_kind_get(const LParserTree *tree, const LParserGreenNode *node);

/*
Summary: returns the original lexer failure preserved on a leaf.
Returns: the failure reason, or LLE_NONE.
Ownership: borrows node.
Errors: NULL node asserts.
Thread-safety: shared read access.
*/
extern LLexerError logos_language_tooling_parsing_syntax_green_node_lexical_error_get(const LParserGreenNode *node);

/*
Summary: returns the exact source bytes of a token or unparsed leaf.
Params:
  tree  the tree the node belongs to; nodes do not point back to it
  out_size  required out; receives the node's byte width
Returns: tree-owned bytes, or NULL for other node categories.
Ownership: borrowed from the tree; valid until its destruction.
Errors: NULL node or out_size asserts.
Thread-safety: shared read access.
*/
extern const byte *logos_language_tooling_parsing_syntax_green_node_text_get(const LParserTree *tree, const LParserGreenNode *node,
                                                                             uint_large *out_size);

/*
Summary: returns the number of equally retained structural alternatives.
Description: cycles from nullable recursion stay visible; clients bound traversal.
Ownership: borrows node.
Errors: NULL node asserts.
Thread-safety: shared read access.
*/
extern uint_large logos_language_tooling_parsing_syntax_green_node_alternative_count_get(const LParserGreenNode *node);

/*
Summary: starts iteration over a node's equally retained alternatives.
Params: tree  the tree the node belongs to; nodes do not point back to it
Returns: the first alternative, or NULL when the node has none; order has
  no semantic preference.
Ownership: borrows node; returned handle expires with its tree.
Errors: NULL node asserts.
Thread-safety: shared read access.
*/
extern const LParserGreenAlternative *logos_language_tooling_parsing_syntax_green_node_alternative_first_get(const LParserTree      *tree,
                                                                                                             const LParserGreenNode *node);

/*
Summary: advances to the next alternative of the same node.
Params: tree  the tree the alternative belongs to
Returns: the next alternative, or NULL at the end; order has no preference.
Ownership: borrows alternative; returned handle expires with its tree.
Errors: NULL alternative asserts.
Thread-safety: shared read access.
*/
extern const LParserGreenAlternative *logos_language_tooling_parsing_syntax_alternative_next_get(const LParserTree             *tree,
                                                                                                 const LParserGreenAlternative *alternative);

/*
Summary: returns the number of ordered children in an alternative.
Returns: the child count, including zero for an empty alternative.
Ownership: borrows alternative for the call.
Errors: NULL alternative asserts.
Thread-safety: shared read access.
*/
extern uint32 logos_language_tooling_parsing_syntax_alternative_child_count_get(const LParserGreenAlternative *alternative);

/*
Summary: retrieves an alternative's child in source order.
Params: tree  the tree the alternative belongs to
Returns: the selected immutable syntax node.
Ownership: borrows alternative; returned immutable node expires with its tree.
Errors: NULL alternative or an out-of-range child_index asserts.
Thread-safety: shared read access.
*/
extern const LParserGreenNode *
logos_language_tooling_parsing_syntax_alternative_child_get(const LParserTree *tree, const LParserGreenAlternative *alternative, uint32 child_index);

/*
Summary: writes an automatic diagnostic message and reports any truncation.
Description: uses structural expectations and copied grammar/lexer labels.
  Full expectation sets remain available separately even when text is shortened.
Ownership: tree is borrowed; buffer is caller-owned and written in place.
Errors: NULL tree/buffer, zero capacity, or an invalid diagnostic index asserts.
Thread-safety: shared read access; synchronize the output buffer.
*/
extern NexusStringFormatResult logos_language_tooling_parsing_syntax_tree_diagnostic_message_write(const LParserTree *tree, uint_large index,
                                                                                                   char *buffer, uint_large capacity);

/*
Summary: initializes an allocation-free red occurrence of the source root.
Ownership: out_node borrows tree; keep it alive and unmoved for its children.
Errors: NULL tree or out_node asserts.
Thread-safety: shared tree access; exclusive access to out_node.
*/
extern void logos_language_tooling_parsing_syntax_red_root_initialize(const LParserTree *tree, LParserRedNode *out_node);

/*
Summary: descends through an explicitly selected packed family and ordered child.
Description: computes absolute position from parent and preceding child widths.
  No family is chosen implicitly; grammar ambiguity and error nodes remain visible.
Ownership: out_node borrows parent's tree and parent occurrence; parent must
  outlive the child and remain unmoved. out_node must not alias parent.
Errors: NULL arguments, a foreign family, or an invalid child index asserts.
Thread-safety: shared tree/parent access; exclusive access to out_node.
*/
extern void logos_language_tooling_parsing_syntax_red_child_get(const LParserRedNode *parent, const LParserGreenAlternative *alternative,
                                                                uint32 child_index, LParserRedNode *out_node);

/*
Summary: retrieves the exact source slice covered by a red occurrence.
Params: out_size  required out; receives the green node's byte width
Returns: borrowed tree source; a missing token's slice is empty.
Ownership: the tree owns the returned bytes.
Errors: NULL node/out_size or an occurrence outside its tree's source asserts.
Thread-safety: shared read access.
*/
extern const byte *logos_language_tooling_parsing_syntax_red_text_get(const LParserRedNode *node, uint_large *out_size);

/*
Summary: retrieves the terminal descriptor of a terminal/missing green node.
Params: tree  the tree the node belongs to; nodes do not point back to it
Returns: the expectation for terminal nodes, or NULL for other nodes.
Ownership: node is borrowed; the returned handle is parser-owned.
Errors: NULL node asserts.
Thread-safety: shared read access.
*/
extern const LParserExpectation *logos_language_tooling_parsing_syntax_green_node_expectation_get(const LParserTree      *tree,
                                                                                                  const LParserGreenNode *node);

/*
Summary: returns a diagnostic's category.
Ownership: borrows diagnostic.
Errors: NULL diagnostic asserts.
Thread-safety: shared read access.
*/
extern LParserDiagnosticKind logos_language_tooling_parsing_syntax_diagnostic_kind_get(const LParserDiagnostic *diagnostic);

/*
Summary: returns the source byte offset where a diagnostic begins.
Ownership: borrows diagnostic.
Errors: NULL diagnostic asserts.
Thread-safety: shared read access.
*/
extern uint_large logos_language_tooling_parsing_syntax_diagnostic_byte_offset_get(const LParserDiagnostic *diagnostic);

/*
Summary: returns the source bytes a diagnostic covers; zero for missing tokens.
Ownership: borrows diagnostic.
Errors: NULL diagnostic asserts.
Thread-safety: shared read access.
*/
extern uint_large logos_language_tooling_parsing_syntax_diagnostic_byte_width_get(const LParserDiagnostic *diagnostic);

/*
Summary: returns the unexpected token kind, or EOF for an incomplete source.
Ownership: borrows diagnostic.
Errors: NULL diagnostic asserts.
Thread-safety: shared read access.
*/
extern LLexerTokenKind logos_language_tooling_parsing_syntax_diagnostic_actual_kind_get(const LParserDiagnostic *diagnostic);

/*
Summary: returns the preserved lexer failure reason, or LLE_NONE.
Ownership: borrows diagnostic.
Errors: NULL diagnostic asserts.
Thread-safety: shared read access.
*/
extern LLexerError logos_language_tooling_parsing_syntax_diagnostic_lexical_error_get(const LParserDiagnostic *diagnostic);

/*
Summary: returns how many terminal expectations the diagnostic carries.
Description: retrieve them with syntax_tree_diagnostic_expectation_at.
Ownership: borrows diagnostic.
Errors: NULL diagnostic asserts.
Thread-safety: shared read access.
*/
extern uint_large logos_language_tooling_parsing_syntax_diagnostic_expectation_count_get(const LParserDiagnostic *diagnostic);

/*
Summary: returns the number of inserted zero-width tokens the diagnostic represents.
Ownership: borrows diagnostic.
Errors: NULL diagnostic asserts.
Thread-safety: shared read access.
*/
extern uint32 logos_language_tooling_parsing_syntax_diagnostic_missing_token_count_get(const LParserDiagnostic *diagnostic);

/*
Summary: returns the lexer token kind an expectation names.
Returns: the kind, or ERROR when the expectation is a one-byte symbol class.
Ownership: borrows expectation.
Errors: NULL expectation asserts.
Thread-safety: shared read access.
*/
extern LLexerTokenKind logos_language_tooling_parsing_syntax_expectation_token_kind_get(const LParserExpectation *expectation);

/*
Summary: reports whether an expectation is a one-byte symbol class.
Description: when TRUE, expectation_symbol_mask_get describes the class.
Ownership: borrows expectation.
Errors: NULL expectation asserts.
Thread-safety: shared read access.
*/
extern boolean logos_language_tooling_parsing_syntax_expectation_byte_terminal_is(const LParserExpectation *expectation);

/*
Summary: copies the one-byte symbol class of a byte-terminal expectation.
Params: out_mask  required out; receives the observation mask
Ownership: out_mask is caller-owned and written in place.
Errors: NULL expectation or out_mask asserts.
Thread-safety: shared read access.
*/
extern void logos_language_tooling_parsing_syntax_expectation_symbol_mask_get(const LParserExpectation *expectation,
                                                                              LContextaObservationMask  out_mask);

/*
Summary: returns an expectation's copied structural or token label.
Returns: the label, or NULL when none was declared.
Ownership: borrowed from the parser, which outlives every tree.
Errors: NULL expectation asserts.
Thread-safety: shared read access.
*/
extern const char *logos_language_tooling_parsing_syntax_expectation_label_get(const LParserExpectation *expectation);

/* ============================================================================ */
/* ---------------------------------------------------------------------------- */
/* GENESIS                                                                      */
/* ---------------------------------------------------------------------------- */
/* ============================================================================ */

/*
Summary: compiles Genesis language and library sources into engines that parse the described languages.
Description:
  A Genesis source declares a language (lexical contexts, grammar rules, Pratt rules, embedded languages) or a
  library (imports, enumerations, templates). The compiler reads sources from strings, chunks or files, resolves
  IMPORTs through an ordered list of search folders and the sources it has already compiled, expands templates,
  validates the grammar with diagnostics in the style of rustc (codes, labelled spans, notes, fix hints), and
  produces an engine per language. The engine owns the compiled lexer and parser; a library compiles to an
  engine without either.

  An import names a source by its file name (JSON resolves to JSON.gen) or by the name it declares in its header,
  and a source compiled earlier by this compiler is found by either name without a search.
Ownership: the compiler owns its sources, diagnostics and registry; every engine it returned stays valid until
  the engine is released and the compiler destroyed, whichever is later.
Thread-safety: exclusive access per compiler; engines are shared read-only once compiled, and parse processes
  are independent.
*/
typedef struct LGenesisCompiler LGenesisCompiler;
typedef struct LGenesisEngine   LGenesisEngine;

/* The problems the compiler reports are Pharos diagnostics: see pharos.h for their accessors. */

typedef enum LGenesisUnitKind {
  LGUK_LANGUAGE,
  LGUK_LIBRARY
} LGenesisUnitKind;

/*
Summary: creates a compiler with no search folders.
Ownership: borrows runtime, which must outlive the compiler; release with logos_languages_genesis_compiler_destroy.
*/
extern LGenesisCompiler *logos_languages_genesis_compiler_create(LogosRuntime *runtime);

/* Summary: destroys the compiler, its sources and diagnostics, and releases its hold on every engine it compiled. */
extern void logos_languages_genesis_compiler_destroy(LGenesisCompiler *compiler);

/*
Summary: appends a folder to the ordered list searched when an IMPORT names a source.
Params: folder  copied; a relative path is relative to the working directory
Errors: NULL arguments assert.
*/
extern void logos_languages_genesis_compiler_search_folder_add(LGenesisCompiler *compiler, const char *folder);

/*
Summary: makes the compiler analyse the source it is asked to compile without building its lexer and parser, which is what
  an editor needs for diagnostics at a fraction of the cost. The engine returned for such a source has no parser and must
  only be released. Imported sources are compiled in full.
*/
extern void logos_languages_genesis_compiler_check_only_set(LGenesisCompiler *compiler, boolean check_only);

/* Summary: returns the number of search folders. */
extern uint_large logos_languages_genesis_compiler_search_folder_count_get(const LGenesisCompiler *compiler);

/* Summary: returns the search folder at index; borrowed from the compiler. */
extern const char *logos_languages_genesis_compiler_search_folder_at(const LGenesisCompiler *compiler, uint_large index);

/*
Summary: compiles one source given as a NUL-terminated text.
Params:
  source_name  names the source in diagnostics, typically a file name; copied
  text  the source; copied
Returns: the engine, or NULL when the source has errors; read them with the diagnostic functions.
Ownership: the caller owns one hold on the engine and releases it with logos_languages_genesis_engine_release.
Errors: NULL arguments assert. Problems in the source are diagnostics, never assertions.
*/
extern LGenesisEngine *logos_languages_genesis_compiler_compile_text(LGenesisCompiler *compiler, const char *source_name, const char *text);

/* Summary: compiles one source given as size bytes; the bytes need no terminator. See compile_text. */
extern LGenesisEngine *logos_languages_genesis_compiler_compile_string(LGenesisCompiler *compiler, const char *source_name, const byte *text,
                                                                       uint_large size);

/*
Summary: starts a chunked compilation; add the source with chunk_add and compile it with chunk_finish.
Errors: starting while another chunked compilation is open asserts.
*/
extern void logos_languages_genesis_compiler_chunk_begin(LGenesisCompiler *compiler, const char *source_name);

/* Summary: appends bytes of the source being assembled; chunk boundaries have no meaning. */
extern void logos_languages_genesis_compiler_chunk_add(LGenesisCompiler *compiler, const byte *chunk, uint_large chunk_size);

/* Summary: compiles the assembled source. See compile_text for the result. */
extern LGenesisEngine *logos_languages_genesis_compiler_chunk_finish(LGenesisCompiler *compiler);

/*
Summary: reads and compiles a file.
Returns: the engine, or NULL with a diagnostic when the file cannot be read or has errors.
*/
extern LGenesisEngine *logos_languages_genesis_compiler_compile_file(LGenesisCompiler *compiler, const char *path);

/* Summary: the number of diagnostics recorded since the last clear, warnings included. */
extern uint_large logos_languages_genesis_compiler_diagnostic_count_get(const LGenesisCompiler *compiler);

/* Summary: returns the diagnostic at index; borrowed until the next clear or destroy. */
extern const PharosDiagnostic *logos_languages_genesis_compiler_diagnostic_at(const LGenesisCompiler *compiler, uint_large index);

/* Summary: tells whether any recorded diagnostic is an error. */
extern boolean logos_languages_genesis_compiler_has_errors(const LGenesisCompiler *compiler);

/* Summary: forgets every recorded diagnostic. */
extern void logos_languages_genesis_compiler_diagnostics_clear(LGenesisCompiler *compiler);

/*
Summary: renders every diagnostic as text in the style of rustc.
Description: each diagnostic shows its severity, code and message, the source location, the source lines with
  carets under the labelled spans, and notes and help lines; a summary line closes the text.
Params: color  TRUE wraps severities and carets in ANSI colors
Returns: Nexus-allocated NUL-terminated text.
Ownership: release with nexus_memory_deallocate.
*/
extern char *logos_languages_genesis_compiler_diagnostics_render(const LGenesisCompiler *compiler, boolean color);

/* ---------------------------------------------------------------------------- */
/* GENESIS: SYMBOL INDEX                                                         */
/* ---------------------------------------------------------------------------- */

/* What a name in a Genesis source stands for. */
typedef enum LGenesisSymbolKind {
  LGSK_UNIT,       /* a language or library: its declared name, or the name an IMPORT gives it */
  LGSK_ALIAS,      /* the name after AS in an IMPORT, and where it qualifies a rule or an enumeration */
  LGSK_RULE,       /* #name: a rule, a Pratt rule or an embedding */
  LGSK_TOKEN,      /* $name: a token, declared in any number of contexts */
  LGSK_FRAGMENT,   /* $_name: a fragment, scoped to its context */
  LGSK_CONTEXT,    /* CONTEXT <NAME> and the contexts that push, set and replace name */
  LGSK_TEMPLATE,   /* a template, by its sigil and name */
  LGSK_PARAMETER,  /* $$name in a template, and the argument names of a call, scoped to the template */
  LGSK_ENUM,       /* an enumeration */
  LGSK_ENUM_MEMBER /* a member of an enumeration, scoped to it */
} LGenesisSymbolKind;

/*
One occurrence of a name: where it stands, and which declaration it means. Two occurrences mean the same thing when
owner, kind, name and scope are equal; the owner is the source that declares the name, so a rule imported from another
language and used here has that language's file as its owner.
Fields:
  name         the name without sigil
  offset,width the name in the indexed text, sigil excluded (so a rename replaces exactly this range)
  declaration  TRUE when this occurrence declares the name
  owner        the path of the source declaring the name (the indexed source's own name when it declares it itself); NULL when unresolved
  scope        the template of a parameter, the context of a fragment, the enumeration of a member; NULL otherwise
*/
typedef struct LGenesisSymbol {
  LGenesisSymbolKind kind;
  const char        *name;
  uint32             offset;
  uint32             width;
  boolean            declaration;
  const char        *owner;
  const char        *scope;
} LGenesisSymbol;

typedef struct LGenesisIndex LGenesisIndex;

/*
Summary: indexes the names of one Genesis source: declarations, references, and for each reference the source that declares it.
  Imports are resolved through the compiler's search folders (and what it compiled before), so a name that comes from another
  file resolves to that file. A source with syntax errors is indexed as far as it parses.
Params:
  source_name  the path the source is known by; becomes the owner of what it declares
  text,size    the source
Returns: the index; never NULL. Release with logos_languages_genesis_index_destroy.
*/
extern LGenesisIndex *logos_languages_genesis_index_create(LGenesisCompiler *compiler, const char *source_name, const byte *text, uint_large size);

extern void logos_languages_genesis_index_destroy(LGenesisIndex *index);

extern uint_large            logos_languages_genesis_index_symbol_count_get(const LGenesisIndex *index);
extern const LGenesisSymbol *logos_languages_genesis_index_symbol_at(const LGenesisIndex *index, uint_large position);

/* Summary: the declared name of the indexed source; NULL when its header did not parse. */
extern const char *logos_languages_genesis_index_unit_name_get(const LGenesisIndex *index);

/* Summary: adds a hold on an engine. */
extern void logos_languages_genesis_engine_retain(LGenesisEngine *engine);

/* Summary: releases one hold; the engine is destroyed when the last hold goes. Parse processes and trees must be destroyed first. */
extern void logos_languages_genesis_engine_release(LGenesisEngine *engine);

extern LGenesisUnitKind logos_languages_genesis_engine_kind_get(const LGenesisEngine *engine);

/* Summary: the name declared in the source's header; borrowed from the engine. */
extern const char *logos_languages_genesis_engine_name_get(const LGenesisEngine *engine);

/* Summary: the version text declared in the header, or NULL; for debugging and client compatibility, not interpreted. */
extern const char *logos_languages_genesis_engine_version_get(const LGenesisEngine *engine);

/* Summary: the name the source was compiled under; borrowed. */
extern const char *logos_languages_genesis_engine_source_name_get(const LGenesisEngine *engine);

/*
Summary: the raw compiled lexer of a language, or NULL for a library.
Ownership: borrowed from the engine; valid until the engine is destroyed.
*/
extern LLexer *logos_languages_genesis_engine_lexer_get(const LGenesisEngine *engine);

/*
Summary: the raw compiled parser of a language, or NULL for a library.
Description: the parser uses the backtracking engine; create processes with the parser API.
Ownership: borrowed from the engine; valid until the engine is destroyed.
*/
extern LParser *logos_languages_genesis_engine_parser_get(const LGenesisEngine *engine);

/* Summary: the number of sources this engine imported while compiling, and their names; borrowed. */
extern uint_large  logos_languages_genesis_engine_import_count_get(const LGenesisEngine *engine);
extern const char *logos_languages_genesis_engine_import_at(const LGenesisEngine *engine, uint_large index);

/* ---- grammar description ---- */

/*
Summary: a read-only description of a compiled language, for tools: bindings, editor syntaxes, language servers, exporters.
Description: it holds the flat grammar after template expansion. Tokens and fragments come in definition order, with their
  definitions in Genesis syntax and as a regular expression in the Oniguruma dialect that editors use. Names created by
  hygienic template expansion contain "__i" and the instance number.
Ownership: owned by the engine; valid until the engine is destroyed.
*/
typedef struct LGenesisGrammar LGenesisGrammar;

typedef enum LGenesisContextAction {
  LGCA_NONE,
  LGCA_PUSH,
  LGCA_POP,
  LGCA_SET,
  LGCA_REPLACE
} LGenesisContextAction;

typedef enum LGenesisRuleKind {
  LGRK_RULE,
  LGRK_PRATT,
  LGRK_EMBED
} LGenesisRuleKind;

typedef struct LGenesisGrammarToken {
  const char           *name;    /* Without the $; a fragment's name starts with an underscore. */
  const char           *context; /* The lexical context it belongs to. */
  boolean               fragment;
  LLexerTokenKind       kind; /* The lexer kind of an emitting token; 0 for a fragment. */
  int64                 priority;
  LGenesisContextAction action;
  const char           *action_target; /* The context a push, set or replace names; NULL otherwise. */
  int64                 action_amount; /* The count of a pop. */
  const char           *definition;    /* The right-hand side in Genesis syntax, without priority and action. */
  const char           *regexp;        /* The same language as an Oniguruma regular expression over characters. */
  const char           *literal;       /* The fixed text when the token matches exactly one string, else NULL. */
} LGenesisGrammarToken;

typedef struct LGenesisGrammarRule {
  const char        *name; /* Without the #. */
  LGenesisRuleKind   kind;
  const char        *annotations; /* Such as "@choice(ChoiceMode.LONGEST)"; may be empty. */
  const char        *definition;  /* The right-hand side, or the body of a PRATT or EMBED block, in Genesis syntax. */
  const char *const *token_refs;  /* The distinct tokens the rule references, without the $. */
  uint32             token_ref_count;
  const char *const *rule_refs; /* The distinct rules it references, without the #; qualified ones as Alias.name. */
  uint32             rule_ref_count;
  const char *const *top_token_refs; /* The tokens it references outside the interior of any frame, plus the opening token of each frame. */
  uint32             top_token_ref_count;
  const char *const *top_rule_refs; /* The rules it references outside the interior of any frame. */
  uint32             top_rule_ref_count;
} LGenesisGrammarRule;

/*
Summary: a delimited construct of the grammar: a sequence that starts with one token and ends with another, such as an
  object between braces or a parenthesized group. Frames with the same opening and closing tokens are merged.
Description: editors model a frame as a context pushed by the opening token and popped by the closing token. Inside it
  only the interior's tokens and rules (and the rules they lead to) can appear.
*/
typedef struct LGenesisGrammarFrame {
  const char        *rule;       /* The first rule that contains the frame. */
  const char        *open;       /* Opening token, without the $. */
  const char        *close;      /* Closing token. */
  const char *const *token_refs; /* Tokens referenced inside the frame, without its own closing token. */
  uint32             token_ref_count;
  const char *const *rule_refs; /* Rules referenced inside it. */
  uint32             rule_ref_count;
} LGenesisGrammarFrame;

extern uint32                      logos_languages_genesis_grammar_frame_count_get(const LGenesisGrammar *grammar);
extern const LGenesisGrammarFrame *logos_languages_genesis_grammar_frame_at(const LGenesisGrammar *grammar, uint32 index);

extern const LGenesisGrammar *logos_languages_genesis_engine_grammar_get(const LGenesisEngine *engine);

/* Summary: the number of lexical contexts, and the name of one; ROOT is always among them. */
extern uint32      logos_languages_genesis_grammar_context_count_get(const LGenesisGrammar *grammar);
extern const char *logos_languages_genesis_grammar_context_name_at(const LGenesisGrammar *grammar, uint32 index);

extern uint32                      logos_languages_genesis_grammar_token_count_get(const LGenesisGrammar *grammar);
extern const LGenesisGrammarToken *logos_languages_genesis_grammar_token_at(const LGenesisGrammar *grammar, uint32 index);
extern uint32                      logos_languages_genesis_grammar_rule_count_get(const LGenesisGrammar *grammar);
extern const LGenesisGrammarRule  *logos_languages_genesis_grammar_rule_at(const LGenesisGrammar *grammar, uint32 index);

/* Summary: the PARSER block settings. entry is a rule name; globals are rule names; choice and selection are Genesis text. */
extern const char *logos_languages_genesis_grammar_entry_get(const LGenesisGrammar *grammar);
extern uint32      logos_languages_genesis_grammar_global_count_get(const LGenesisGrammar *grammar);
extern const char *logos_languages_genesis_grammar_global_at(const LGenesisGrammar *grammar, uint32 index);
extern boolean     logos_languages_genesis_grammar_grammar_expected_get(const LGenesisGrammar *grammar);

/*
Summary: writes the language as Genesis source text: contexts, tokens, fragments, rules, Pratt rules, embeds and the PARSER block.
Description: templates are already expanded, so the text is self-contained apart from the IMPORTs of embedded languages.
  Compiling the result gives a language with the same lexer and grammar.
Returns: Nexus-allocated NUL-terminated text.
Ownership: release with nexus_memory_deallocate.
*/
extern char *logos_languages_genesis_engine_export_text(const LGenesisEngine *engine);

/* ---- the Genesis language itself ---- */

/*
Summary: a word with a fixed meaning in Genesis (a declaration keyword, an action, a pratt word, or an annotation or
  conditional such as "@if"), and the role it plays.
Description: the roles are module, namespace, enum, struct, function, pratt, action, constant, condition and annotation.
  The compiler's lexer and parser read this table, so tools generated from it cannot differ from the compiler.
*/
typedef struct LGenesisSpecWord {
  const char *text;
  const char *role;
} LGenesisSpecWord;

/* Summary: a punctuation token of Genesis. A group is the name shared by an "open" and its "close" token. */
typedef struct LGenesisSpecPunctuation {
  const char *name; /* The token name without $: "lbrace". */
  const char *text;
  const char *role;  /* open, close, separator or operator. */
  const char *group; /* braces, parens, angles or brackets for open and close; empty otherwise. */
} LGenesisSpecPunctuation;

extern uint32                         logos_languages_genesis_spec_word_count_get(void);
extern const LGenesisSpecWord        *logos_languages_genesis_spec_word_at(uint32 index);
extern uint32                         logos_languages_genesis_spec_punctuation_count_get(void);
extern const LGenesisSpecPunctuation *logos_languages_genesis_spec_punctuation_at(uint32 index);

/* ---- writing Genesis source ---- */

/*
Summary: assembles a language as Genesis source text from its parts.
Description: this is how a grammar that exists as data or code rather than as a .gen file gets one: the engine's own
  export uses it, and so do the grammars of Genesis and of the genesis tool's mapping files. Definitions are Genesis
  expression text. A token whose name starts with an underscore is a fragment.
Ownership: copies everything it is given; release with logos_languages_genesis_writer_destroy.
*/
typedef struct LGenesisWriter LGenesisWriter;

extern LGenesisWriter *logos_languages_genesis_writer_create(const char *name, const char *version);
extern void            logos_languages_genesis_writer_destroy(LGenesisWriter *writer);

/* Summary: lines of comment (without the //) written under the header, for example "Generated by ...". */
extern void logos_languages_genesis_writer_comment_add(LGenesisWriter *writer, const char *text);

/* Summary: an IMPORT declaration, such as "IMPORT JSON AS Json { #document }". */
extern void logos_languages_genesis_writer_import_add(LGenesisWriter *writer, const char *declaration);

/* Summary: the PARSER block: entry rule, global rules, the default choice mode (Genesis text) and the lexical selection. */
extern void logos_languages_genesis_writer_parser_entry_set(LGenesisWriter *writer, const char *rule);
extern void logos_languages_genesis_writer_parser_global_add(LGenesisWriter *writer, const char *rule);
extern void logos_languages_genesis_writer_parser_choice_set(LGenesisWriter *writer, const char *mode);
extern void logos_languages_genesis_writer_parser_grammar_expected_set(LGenesisWriter *writer, boolean grammar_expected);

extern void logos_languages_genesis_writer_context_add(LGenesisWriter *writer, const char *name);

/*
Summary: adds a token or fragment to a context (added first, if need be).
Params: action  push, pop, set or replace; target the context of a push, set or replace; amount the count of a pop
*/
extern void logos_languages_genesis_writer_token_add(LGenesisWriter *writer, const char *context, const char *name, const char *definition,
                                                     int64 priority, LGenesisContextAction action, const char *target, int64 amount);

/* Summary: adds a grammar rule; annotations may be empty. A PRATT or EMBED block takes the text of its body. */
extern void logos_languages_genesis_writer_rule_add(LGenesisWriter *writer, const char *name, const char *annotations, const char *definition);
extern void logos_languages_genesis_writer_pratt_add(LGenesisWriter *writer, const char *name, const char *body);
extern void logos_languages_genesis_writer_embed_add(LGenesisWriter *writer, const char *name, const char *body);

/* Summary: the Genesis source; Nexus-allocated, release with nexus_memory_deallocate. */
extern char *logos_languages_genesis_writer_text_get(const LGenesisWriter *writer);

/* ---- language definitions ---- */

/*
Summary: a language as Logos objects: tokens as Regula languages in lexical contexts, rules as a Contexta grammar.
Description: this is the form in which a language that has no .gen file exists, such as Genesis itself. The exporter
  below turns it into Genesis source, so such a language needs to be defined only once.
Ownership: owns the Regula languages and the Contexta; release with logos_languages_genesis_definition_destroy.
*/
typedef struct LGenesisDefinition LGenesisDefinition;

extern LGenesisDefinition *logos_languages_genesis_definition_create(const char *name, const char *version);
extern void                logos_languages_genesis_definition_destroy(LGenesisDefinition *definition);

/*
Summary: adds a token to a lexical context (created on first use; the first context added is the root, to be named ROOT).
Params:
  regexp  the token's language in Regula's regexp dialect, over bytes; bytes from 0x80 up stand for non-ASCII characters
  action  a context action; target names the context of a push, set or replace, amount counts a pop
Returns: the token kind to use with Contexta's token operation.
Errors: a regexp that does not parse asserts.
*/
extern LLexerTokenKind logos_languages_genesis_definition_token_add(LGenesisDefinition *definition, const char *name, const char *context,
                                                                    const char *regexp, int32 priority, LGenesisContextAction action,
                                                                    const char *target, int64 amount);

/* Summary: the Contexta the rules of the language are added to; it is owned by the definition. */
extern LContexta *logos_languages_genesis_definition_contexta_get(LGenesisDefinition *definition);

/* Summary: declares a rule of the grammar; the name is copied. Define its body with Contexta's rule_define on the definition's Contexta. */
extern LContextaRule *logos_languages_genesis_definition_rule_add(LGenesisDefinition *definition, const char *name);

/* Summary: the entry rule and the global rules of the PARSER block, by Contexta rule. */
extern void logos_languages_genesis_definition_entry_set(LGenesisDefinition *definition, const LContextaRule *rule);
extern void logos_languages_genesis_definition_global_add(LGenesisDefinition *definition, const LContextaRule *rule);
extern void logos_languages_genesis_definition_grammar_expected_set(LGenesisDefinition *definition, boolean grammar_expected);

/*
Summary: writes the definition as Genesis source.
Description: Regula languages become lexical expressions (character classes as ranges, runs of characters as quoted
  strings, x x* as x+), Contexta operations become rule expressions. Returns NULL when an operation has no Genesis form.
Ownership: release with nexus_memory_deallocate.
*/
extern char *logos_languages_genesis_definition_export_text(const LGenesisDefinition *definition);

/* Summary: the definition of the Genesis language itself, built from the tables of the compiler's lexer and parser. */
extern LGenesisDefinition *logos_languages_genesis_spec_definition_create(void);

/* ---------------------------------------------------------------------------- */
/* GRAMMAR INTERCHANGE                                                           */
/* ---------------------------------------------------------------------------- */

/*
Grammars written for other tools, read and written as Genesis. Each format has a Genesis grammar of its own (in the standard
grammars, folder formats) that parses the source, and a converter that turns the syntax tree into Genesis. What a format has
and Genesis does not (semantic actions, predicates, embedded code) is rejected with a clear message when it matters and
dropped with a note when it does not; what Genesis does beyond the format is listed in the report too.
*/
/* One source file of a grammar; some formats split a grammar over several (an ANTLR lexer grammar and its parser grammar). */
typedef struct LInterchangeInput {
  const char *name;
  const byte *text;
  uint_large  size;
} LInterchangeInput;

extern uint32      logos_languages_interchange_format_count_get(void);
extern const char *logos_languages_interchange_format_name_at(uint32 index);
extern const char *logos_languages_interchange_format_description_at(uint32 index);

/*
Summary: converts a grammar of a format, in one or several files, to Genesis source.
Params:
  compiler  whose search folders hold the grammars of the formats
  format    a name of logos_languages_interchange_format_name_at
  name      the name of the resulting language
Returns: TRUE with the Genesis text; FALSE when the format is unknown or the source cannot be converted (see the report).
  out_report always receives the notes: what is rejected, approximated, or beyond the format. Both outputs are Nexus-allocated;
  out_genesis is NULL on failure.
*/
extern boolean logos_languages_interchange_convert(LGenesisCompiler *compiler, const char *format, const char *name, const LInterchangeInput *inputs,
                                                   uint32 input_count, char **out_genesis, char **out_report);
