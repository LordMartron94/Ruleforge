# 0004: The standard library is Ruleforge, with a few documented intrinsics

**Status:** Proposed. **Tiers:** Lite.

In the context of a standard library (math, collections, statistics, economy tiering, rule transformation) that was native C code,
facing the goal that the library should show how functions are written in Ruleforge and should not be able to differ from what a user
can write, we decided that `std.*` are `.rf` modules embedded in the engine, with only what the language cannot express declared as
`export intrinsic func Name(...) -> T;` (no body, bound by module and name to C, accepted in embedded modules only), to let every
library function be read, documented and tested as ordinary Ruleforge, accepting that the evaluator interprets what used to be C
(bounded by the step budget; list appends were made amortized so loops stay linear) and that `number`, `record` and `any` joined the
types and `std.` became a reserved module prefix.

Alternatives considered: one hidden `std.sys` module of natives (rejected: the native surface is invisible in each module and
importable by users).
