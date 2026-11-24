# 0005. Recoverable assertion/panic mode: thread-local arm point, caller-owned setjmp, pool-reused error

## Status
Proposed

## Context and Problem Statement
A consumer (SHIELD, a test-framework tool built on top of `nexus_process` per `adr/0004`) needs to
run untrusted-ish test code that may call into Nexus's own `NEXUS_PROGRAM_PRECONDITION`/
`POSTCONDITION`/`INVARIANT`/`ASSERT`/`PANIC` machinery internally, several call frames deep inside
whatever Nexus function the test happened to call -- and report that failure as ordinary data (a
message, a location) rather than as a process death, without touching the hundreds of existing
call sites that already use these macros throughout the codebase. Every one of those macros
currently does exactly one thing on failure: log to stderr, then `abort()`.

This record covers `nexus_program_recovery_arm`/`_disarm`/`_last_error_get`, added to
`libs/nexus/src/n_program.c`'s existing FAILURE PATH SYSTEM and declared in `nexus.h`.

## Decision Drivers
- No call-site changes. `NEXUS_PROGRAM_PRECONDITION(condition, message, ...)` and its seven
  siblings are used unmodified, unmoved, everywhere they already are -- only their own macro
  expansions may change internally.
- Must not weaken production behavior. Outside an explicitly armed thread, every one of these
  macros must abort exactly as before -- this feature must be additive-only, never a way for a
  real invariant violation to silently not-crash.
- Thread-local, not global. Nexus has its own threading module (`n_threads.c`); a background
  thread a test spawns that hits a precondition failure must still abort the normal way, and a
  `longjmp` can only ever unwind within the thread that called the matching `setjmp` regardless,
  so a shared/global arm point could not be made correct even with a lock around it.
- Reuse the existing error-pool/message machinery (`_nexus_program_errors_failure_create` and
  friends, `adr/0001-nexus-error-representation`) for the recovered failure's detail -- not a
  second, parallel channel for carrying a message out of a failure.
- Explicit scope boundary: this can only ever intercept Nexus's own explicit, code-level failure
  checks. A real SIGSEGV/hardware trap from a wild pointer write is a signal delivered by the OS,
  bypassing this (and all of ordinary C control flow) entirely -- `nexus_process`'s
  `NEXUS_PROCESS_EXIT_SIGNALED` reporting (`adr/0004`) remains the only mechanism that catches
  that, unchanged by this record.

## Considered Options
- **Scope of what gets armed**: A -- a global flag/jmp_buf. B -- thread-local (chosen).
- **Who calls `setjmp`**: A -- a Nexus-provided function does the `setjmp` on the caller's behalf.
  B -- the caller calls `setjmp` itself, at its own call site, with Nexus only holding a pointer to
  where to jump (chosen).
- **Where the failure detail comes from**: A -- a new, purpose-built struct just for a recovered
  failure's message/location. B -- construct (or, for `_nexus_program_panic_error`, reuse) a real
  pool-resident `NexusError`, read back via `nexus_program_recovery_last_error_get` (chosen).
- **Extending `_OR_ERROR`'s existing "don't abort" path**: `NEXUS_PROGRAM_*_OR_ERROR` already
  returns a `NexusError` instead of aborting -- but only outside `NEXUS_DEVELOPER_MODE`; inside it
  (the mode a test binary actually runs under, since assertions must be live to test anything) it
  aborts exactly like the plain macros. Extending `_OR_ERROR` itself was rejected: it would only
  cover the `_OR_ERROR` call sites specifically (a small minority), still abort in dev mode, and
  still require every risky call to be manually routed through the `_OR_ERROR` variant rather than
  whatever assertion macro the callee already happens to use internally.
- **Violation-code plumbing**: `_nexus_program_assert`/`_nexus_program_assert_v` gained a
  `NexusErrorCode code` parameter (every one of the 8 call sites inside the macro expansions now
  passes its matching `GLOBAL_NEXUS_*_VIOLATION`) so a recovered assertion failure carries the same
  specific code the `_OR_ERROR` family already does, rather than one generic catch-all code.

## Decision Outcome
**Thread-local (B):** mirrors `n_program.c`'s existing `_n_program_context_get` pattern exactly --
a `pthread_key_t`-backed per-thread state under `NEXUS_THREAD_SAFE`, a plain `static` struct
otherwise. No new synchronization primitive introduced; the same reasoning that made
`program_context` thread-local applies unchanged (a shared instance would let one thread's arm
state be silently overwritten or misattributed by another's).

**Caller-owned `setjmp` (B):** `nexus_program_recovery_arm(jmp_buf *recovery_point)` only stores
the pointer; the caller calls `setjmp(*recovery_point)` itself, immediately, in its own stack
frame:
```c
jmp_buf recovery_point;
if (setjmp(recovery_point) == 0) {
  nexus_program_recovery_arm(&recovery_point);
  risky_call();
  nexus_program_recovery_disarm();
} else {
  nexus_program_recovery_disarm();
  NexusError *error = nexus_program_recovery_last_error_get();
}
```
This is not a stylistic choice -- `setjmp`'s saved state is only valid for the exact stack frame
that calls it directly (C99 7.13.2.1), so a Nexus-provided "arm and setjmp for you" function is not
just awkward but incorrect: the saved frame would be Nexus's own, already gone by the time a
caller tried to use it. `nexus_program_recovery_disarm` is idempotent and unconditional-safe to
call after either branch, or after a `longjmp` landed there from deep inside Nexus's own code.
Arming happens only in `setjmp`'s direct-return branch, after its saved environment is valid.
The recovery path also disarms itself immediately before `longjmp`, so failure-reporting code in
the handler cannot recursively jump back into the same handler if it triggers another Nexus
assertion; the explicit disarm in the handler is therefore idempotent defense in depth.

**Failure detail via the existing pool (B):** when armed, `_nexus_program_assert_v` constructs a
`NexusError` via the same `_nexus_program_errors_failure_create` the whole codebase already uses,
passing the *already-rendered* message string through `"%s"` rather than re-consuming the original
`va_list` a second time (a `va_list` may not be walked twice without an intervening `va_copy`, and
this project's `-std=c89 -pedantic` build doesn't declare `va_copy` cleanly -- see
`NEXUS_PROGRAM_ERROR_FAILURE`'s own doc comment for the same constraint elsewhere in this file).
`_nexus_program_panic_error` reuses the `NexusError *error` it was already given directly, rather
than constructing a second one that would just restate it. Either way, `nexus_program_recovery_
last_error_get()` hands back a real pool pointer with the same ownership contract as any other
`NexusError` this codebase produces -- no new struct, no new ownership rule to learn.

**Insertion point:** the recovery check lives in exactly two places -- the bottom of
`_nexus_program_assert_v` (which every one of `NEXUS_PROGRAM_ASSERT`/`PRECONDITION`/
`POSTCONDITION`/`INVARIANT`, plain and `_ALWAYS`, and `_nexus_program_assert_or_error`'s
`NEXUS_DEVELOPER_MODE` branch, all funnel through) and the bottom of `_nexus_program_panic`/
`_nexus_program_panic_error`. When armed, these call `longjmp` and never return; the macros' own
subsequent `abort()` calls become unreachable in that case, not dead code removed -- when *not*
armed, execution falls through to that `abort()` exactly as before. This is the one point where
"no call-site changes" required a small, disclosed exception: the 8 macro expansions (not their
call sites) now pass one more argument, `NEXUS_PROGRAM_PRECONDITION(condition, message, ...)`
itself is byte-for-byte unchanged everywhere it's written.

**Scope boundary (unchanged, restated):** a genuine SIGSEGV never reaches any of this -- it is a
signal, not a function call. SHIELD's crash-isolated worker model (`adr/0001` in this repo's
`tools/shield`) is what actually survives that case. A worker may serve consecutive passing tests,
but a controlled failure or real crash both discard it before another test runs.

### Pros and Cons of the Options
**Scope A -- global arm state**
- Good: one less per-thread lookup on the hot assertion-check path.
- Bad: a background thread's own precondition failure would either be silently misattributed to
  whichever thread happens to be armed, or -- since `longjmp` cannot cross threads regardless --
  would corrupt the process in a different, worse way than just aborting. Rejected outright.

**Who calls setjmp A -- Nexus does it for the caller**
- Bad only: not merely inconvenient but incorrect per the C standard's `setjmp` scope rule --
  rejected, not a tradeoff.

**Failure detail A -- a new purpose-built struct**
- Good: could be a smaller, cheaper structure than a full pool-resident `NexusError`.
- Bad: a second representation of "what failed" alongside the one every other Nexus failure path
  already uses, with its own ownership rules to document and remember -- rejected as needless
  duplication for a cost (one pool slot) already paid by every other failure in this codebase.

## Consequences
Easier: a SHIELD test that calls a real Nexus function which happens to trip one of that
function's own preconditions is now reported with the same fidelity as a `SHIELD_ASSERT*` the test
author wrote directly -- SHIELD's `sh_run.c` arms recovery using its own existing `ShieldContext.
abort_point` as the target, so a failure from either source lands at the identical `setjmp` point
and is indistinguishable to the reporter. Nothing about existing, non-SHIELD Nexus call sites
changes in behavior, cost, or source.

Harder: a recovered failure still leaves any in-flight Nexus-internal state (a lock held, a
partially-mutated structure, memory allocated but not yet freed, entries already pushed onto the
per-thread diagnostic context stack via `NEXUS_PROGRAM_CONTEXT_PUSH` and not yet popped) exactly as
it was at the instant of `longjmp` -- `longjmp` runs no cleanup, by definition. This is only safe
because of a policy enforced one layer up, in `tools/shield`: a worker process is discarded and a
fresh one spawned after *any* failure, never reused -- this record does not attempt to make
in-process recovery safe to continue from on its own, and would not be, used that way.

## Framework Hook (optional)
Directly extends the "assertion behavior switches by mode without touching call sites" precedent
`ASSERT_CRITICAL` already established (`STYLE_GUIDE.md` § Error Handling) -- this is a third mode
in that same family (development-abort / release-vanish-or-become-error / this-thread-currently-
armed-so-recover), selected at the failure site by thread-local state instead of a compile-time
flag. The insulation between "a test's own bug" and "the tool running the test" is Dependable
Dependencies Principle applied at the process layer (`adr/0004`) and now, additionally, within a
single process's own control flow for the narrower case this record covers.

## Related Decisions
Builds directly on `adr/0004-process-spawn-communicate` (the crash-isolation backstop this record
does not and cannot replace). Consumed by `tools/shield` `adr/0001-shield-provider-repl-protocol`
(cross-repo reference per the numbering convention -- no Markdown link: this file's relative path
to that repo would break if either is browsed standalone), which arms this mechanism using its own
`ShieldContext.abort_point` as the recovery target.
