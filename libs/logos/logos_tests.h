/*
Summary: Logos's own tests, kept out of logos.h so the public API stays free of them.
Layer: above logos.h; for the testbed and other callers that exercise Logos.
See: logos.h
*/
#pragma once

#include "logos.h"

/*
Summary: compiles regular expressions into NFAs and logs, per input, whether the NFA's verdict is the expected one.
Description: covers the construction (Glushkov) and the simulation of
  logos_automatons_nfa_from_regula and logos_automatons_nfa_match: literals,
  repetition, classes, ambiguous splits, nested stars, extreme bytes, statesets
  wider than one word, and the empty language. Each input logs PASS at info or
  FAIL at error, followed by a summary line.
Params: runtime  the Logos runtime whose Echo context receives the log; borrowed
Errors: NULL runtime asserts. A mismatch is logged, not returned or asserted.
Thread-safety: synchronize against runtime destruction.
*/
extern void logos_tests_automatons_nfa_run(LogosRuntime *runtime);

/*
Summary: builds the DFA of every test regular expression, minimizes it, and logs whether both accept exactly what the NFA tests expect.
Description: reuses the inputs of logos_tests_automatons_nfa_run, and also
  checks that each minimal DFA has the expected number of states, is no larger
  than the DFA it came from, and is unchanged by minimizing it again. Each
  check logs PASS at info or FAIL at error, followed by a summary line.
Params: runtime  the Logos runtime whose Echo context receives the log; borrowed
Errors: NULL runtime asserts. A mismatch is logged, not returned or asserted.
Thread-safety: synchronize against runtime destruction.
*/
extern void logos_tests_automatons_dfa_run(LogosRuntime *runtime);

/*
Summary: runs the lexer's contract tests and logs, per check, PASS at info or FAIL at error, followed by a summary line.
Description: covers longest match, priority and registration-order ties, the
  three outcomes of lexing (token, need input, error), the end token, every
  context action and its failure, error-span extents, the token width limit,
  rejection of an empty-matching rule, lossless tiling of the source, and the
  source index. Each case is also lexed under 180 random chunkings and must give
  identical tokens, and random sources are lexed both whole and cut, with and
  without a width limit, so chunk boundaries never change the result.
Params: runtime  the Logos runtime whose Echo context receives the log; borrowed
Errors: NULL runtime asserts. A mismatch is logged, not returned or asserted.
Thread-safety: synchronize against runtime destruction.
*/
extern void logos_tests_lexing_run(LogosRuntime *runtime);

/*
Summary: runs the parser's contract tests and logs, per check, PASS at info or FAIL at error, followed by a summary line.
Description: covers Pratt operations inside Contexta grammars: precedence and
  associativity, prefix, postfix, enclosures, calls, indexing, the ternary, a
  non-associative operator, errors with the terminals the table allowed, a
  Pratt expression inside ordinary Earley rules with default-allowed
  whitespace, and lossless trees. Every case is parsed whole and in several
  chunkings, which must agree exactly; 50000 nested groups and 50000-term
  chains must parse without recursion.
Params: runtime  the Logos runtime whose Echo context receives the log; borrowed
Errors: NULL runtime asserts. A mismatch is logged, not returned or asserted.
Thread-safety: synchronize against runtime destruction.
*/
extern void logos_tests_parsing_run(LogosRuntime *runtime);

/*
Summary: runs the backtracking engine's contract tests and logs, per check, PASS at info or FAIL at error, followed by a summary line.
Description: covers ordered, longest and unambiguous choice on the same bytes, the parser default mode,
  global trivia rules with scoped exclusion, a pushed lexical context, lossless trees, farthest-failure
  expectations, binding-power expressions over rule atoms (precedence, associativity, prefix, postfix,
  parentheses through the atom), the nesting depth limit, and agreement between whole and chunked input.
Params: runtime  the Logos runtime whose Echo context receives the log; borrowed
Errors: NULL runtime asserts. A mismatch is logged, not returned or asserted.
Thread-safety: synchronize against runtime destruction.
*/
extern void logos_tests_backtracking_run(LogosRuntime *runtime);

/* Summary: runs the Genesis compiler tests: the spec's languages, the compiler API and its diagnostics. */
extern void logos_tests_genesis_run(LogosRuntime *runtime);
