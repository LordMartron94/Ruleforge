# 0001. NexusError representation: a stable-pointer handle over an arena-backed causal chain

## Status
Accepted

## Context and Problem Statement
`NexusError` is the return-value type of the whole Nexus failure-path system
(assertions, preconditions, `_OR_ERROR` variants, and any future facility
that raises errors through `NEXUS_PROGRAM_ERROR_CODE_MAKE`). It needs to do
more than carry a code:

- It must be able to represent that one error occurred *because of* another
  (wrapping/forwarding), so that when the same error code can be produced by
  several different call paths, the actual path taken is recoverable without
  a debugger.
- It must carry a notion of "where this came from" that stays correct no
  matter how long the error lives before something finally inspects it. The
  existing `program_context` push/pop stack (`n_program.c`) does not satisfy
  this: it is one mutable global that later, unrelated pushes and pops
  overwrite, so a frame index captured at error-creation time is not
  trustworthy by the time the error is actually reported.
- It must remain cheap to copy and return regardless of how deep its causal
  chain is — because it is the return type of the failure path itself, not
  an occasional diagnostic add-on.
- It must not depend on machinery complicated enough that the machinery's own
  failure would need to be reported *through the same error system* — that
  is a direct recursion (the error system reporting a failure of the thing
  it is built on).

**Revision (2026-09-14):** originally implemented as a value-type struct
carrying its own `address` field (a self-pointer back to its canonical
pool slot), specifically so mutating operations (`wrap`, `forward`) could
reach the canonical copy despite the caller only holding a value copy.
Superseded by using `NexusError *` (a stable pointer into slab storage)
everywhere instead, once implementation exposed two problems with the
self-pointer approach: (1) it requires every mutating function to remember
to go through `.address` rather than the local copy — `wrap()` did this
correctly, `forward()` did not, and shipped with a real bug where its
linkage silently never reached the canonical copy; (2) the "safe frozen
snapshot" a value copy seems to promise is only partial anyway — `message`
and `contained_error` are themselves pointers into the same reusable slab
storage, so copying the outer struct never protected them from pool-reset
staleness, only the inline scalar fields. The original worry behind wanting
value semantics — avoiding the ownership/lifetime ambiguity of a typical
pointer return type — doesn't actually apply to a pointer into
never-individually-freed arena storage, which is closer to a pointer into
a `static const` array than to a `malloc`'d pointer with unclear ownership.

## Decision Drivers
- Construction (originating or forwarding an error) must be O(1) and free of
  string work — it sits on every failure path, not just a diagnostic one.
- Walking/printing a chain is rare (only when an error is actually reported)
  and may be comparatively expensive — pointer-chasing and formatting are
  acceptable there.
- The causal chain must support arbitrary depth (Go `Unwrap()`/Rust
  `source()`-style), not a fixed cap, and must be programmatically walkable,
  not just visible in a printed log.
- Whether an intermediate function appears in the chain is caller policy: a
  bare `return error;` must be entirely transparent (zero cost, no frame
  added); a function must opt in explicitly to appear in the trace.
- The client must never be responsible for freeing an individual error.
- The system must never simply fail to represent an error because of
  resource exhaustion — but the one path that *can* still fail (growing the
  backing storage) must not create a reporting-recursion.

## Considered Options
- **A — No chain in the value (errno/kernel style)**: `NexusError` stays
  code+message only; a wrapped error is logged or pushed onto the context
  stack at the moment of wrapping, then discarded.
- **B — Single-level wrap**: add `NexusErrorCode wrapped_code` plus a
  non-owning `wrapped_message`, mirroring Go's `Unwrap()`.
- **C — Small fixed-bound chain**: a short inline array (2-4 entries) of
  wrapped codes/messages instead of one.
- **D — Arbitrary-depth chain, nodes owned by the contributing function's own
  stack frame**: each function that opts in declares a local frame node and
  links it into the chain.
- **E — Arbitrary-depth chain, nodes allocated from a dedicated growable pool
  (arena), separate from `program_context`** — chosen.

For node storage specifically, a sub-option of extending/reusing the
existing `program_context` stack itself (rather than a separate pool) was
also considered and rejected.

For the one residual failure path (growing the pool's backing storage), two
outcomes were considered: treat it as fatal, or degrade gracefully by
returning the error anyway minus its newest frame.

For keeping existing references stable across a grow, two strategies were
considered: a single contiguous array grown via `realloc`, referenced
everywhere (the handle and every internal next-node link) by index rather
than raw pointer; and a chunked/slab list of fixed-size blocks, individually
allocated and never moved or resized, where growth means appending a new
block. The array+index approach works, but requires every internal link to
be converted to an index, or the dangling-pointer problem silently
reappears in whichever link was missed.

For the pool's lifecycle, a mandatory `nexus_program_initialize` /
`nexus_program_deinitialize` pair — with every function needing to verify
the program had been initialized first — was considered and rejected in
favor of lazy, implicit initialization with no mandatory teardown.

For how a message with interpolated values gets its final text, three
options were considered. Storing the `va_list` itself at construction and
rendering later was the original idea, but is not actually achievable in
portable C: a `va_list` (via `va_copy` or otherwise) is a descriptor into
the *originating call's own stack frame* on essentially every real ABI, not
a self-contained copy of the argument values — it is exactly as invalid the
instant that frame is gone as a raw pointer into the same frame would be,
so "storing" one for later never actually solves the lifetime problem.
Separately, and independently of lifetime: `va_arg` requires the type of
each argument to be known at the point it is pulled, and nothing in C
allows extracting arguments generically without that type information —
which must come from either the format string or explicit per-argument
tagging at the call site. Deferred, explicit typed-argument capture (values
tagged and copied into a small fixed-capacity array at construction,
`stbsp_sprintf`-rendered only at report time) was measured against
formatting immediately: a representative format string (2 ints + a short
string) costs roughly 200-400 ns to fully render vs. 30-70 ns for a
scan/capture pass with no text conversion — a consistent 4-8x difference
(wider, 6-11x, once a `%f` is involved). Eager formatting at construction
was chosen anyway, prioritizing minimizing implementation bug surface for
the initial version over a difference that, while real and consistently
measured, remains sub-microsecond in absolute terms on a path that is rare
by nature.

## Decision Outcome
`NexusError` is a small value-type handle: `NexusErrorCode code`, a
non-owning `const char *message`, and an optional pointer to the head of an
immutable, singly-linked trace-node chain. Nodes live in a dedicated pool
that is *not* `program_context` — separate storage with a different
lifetime model, since context frames pop when a scope ends while trace
nodes must outlive the scope that created them. The pool is a chunked/slab
list of fixed-size blocks: each block is individually allocated and is
never moved or resized once it exists, so every reference into it — the
top-level handle and every internal next-node link — can stay an ordinary
raw pointer, valid for as long as the pool itself exists, with no
index-based indirection layer required. Growth means allocating and
appending one more block; it never touches, moves, or invalidates any block
already in use. The initial block size is a compile-time
`#ifndef`-overridable constant (mirroring
`NEXUS_PROGRAM_CONTEXT_MAX_STACK_SIZE`'s existing convention), plus a
runtime API to query current capacity and to grow or shrink it explicitly.
The pool requires no explicit initialization: like `program_context`, it is
plain static storage, zero-initialized by the C runtime before `main` runs,
and the operation that allocates a node treats "no blocks yet" as simply
the first case of ordinary growth — there is no separate "has this been
initialized" state to check anywhere. Nodes are never freed individually,
and the pool is not required to be explicitly destroyed either: reclamation
is bulk and entirely voluntary, via the client-controlled shrink/reset
call, at whatever point the client independently knows nothing still
references the pool's contents — never a mandatory step. If growing the
pool's backing storage itself fails, that is treated as fatal — the same
panic/abort response the existing `program_context` stack already gives a
fixed-capacity overflow — rather than attempted to be represented as a
`NexusError`, which is exactly how the reporting-recursion is structurally
avoided: the failure mode that could recurse is never routed through the
system it would be recursing into.

A message with interpolated values is rendered to its final text
immediately, at construction, via the existing `stbsp_sprintf` family
already used elsewhere in the failure path (`n_program.c`) — the printf-style
call site is kept exactly as-is; nothing about how a message is written
changes. This is explicitly provisional: if profiling a real workload later
shows this cost matters, the deferred-capture alternative below is the
documented fallback, not something to re-derive from scratch. One detail
this doesn't yet answer, left open for whoever implements it: eager
rendering means the resulting text needs somewhere to live that isn't a
string literal, unlike today's plain `message` — where that buffer comes
from (the same slab pool as trace nodes, its own fixed-size inline buffer,
or something else) is still to be decided.

### Pros and Cons of the Options
**A — No chain in the value**
- Good: zero shape change, zero new machinery, cannot regress performance.
- Bad: defeats the actual motivating problem — nothing is programmatically
  inspectable after the fact, only what was printed to a log at the time.

**B — Single-level wrap**
- Good: small, fixed, cheap; matches a well-established prior-art shape
  (Go's `Unwrap()`).
- Bad: still can't answer "what was the *original* cause" past one hop
  without relying on a log trail for anything deeper — doesn't meet the
  arbitrary-depth requirement.

**C — Small fixed-bound chain**
- Good: bounded size, still no allocation.
- Bad: an arbitrary cap is still a cap; the motivating scenario (several
  possible originating paths for the same code) doesn't come with a known
  upper bound on forwarding depth.

**D — Local-stack-owned nodes**
- Good: no allocator at all, maximally simple in isolation.
- Bad: fatally broken as soon as the contributing function returns — the
  node is a pointer into a now-invalid stack frame, and by construction the
  whole point of forwarding is that intermediate functions *have* returned
  by the time the error is finally handled. Rejected outright, not a
  tradeoff.

**E — Dedicated growable pool (chosen)**
- Good: satisfies every driver above — O(1) construction, arbitrary depth,
  no per-error free burden, never silently fails to represent an error, and
  the one real failure mode (grow failure) is deliberately routed around the
  error system rather than through it.
- Bad: it is a second piece of foundational, allocation-adjacent machinery
  beyond `program_context`, widening the trusted base the whole failure path
  sits on; growth is a genuine (if rare and client-controlled) allocation
  point in a system that otherwise avoids one entirely.

**E1 — Single array grown via `realloc`, referenced by index (considered, rejected)**
- Good: familiar, one contiguous allocation.
- Bad: every reference into it — not just the top-level handle, every
  internal next-node link too — must be an index rather than a raw pointer,
  since `realloc` can move the block; missing even one link silently
  reintroduces the dangling-pointer bug this whole sub-decision exists to
  avoid.

**E2 — Chunked/slab block list (chosen)**
- Good: nothing already in use ever moves, so ordinary raw pointers stay
  valid for the pool's whole lifetime with no indirection layer to get
  wrong. Its one real cost — worse locality once a chain spans several
  blocks — lands entirely on the print/walk path, which was already
  accepted as allowed to be slower.
- Bad: more bookkeeping to track a growing list of blocks than one array
  would need.

**Mandatory init/deinit lifecycle (considered, rejected)**
- Good: an explicit `nexus_program_initialize`/`deinitialize` pair would
  make the pool's lifetime maximally explicit.
- Bad: requires every function touching the pool to verify the program had
  been initialized first, and requires knowing when it is safe to call
  deinitialize (i.e. that nothing still references the pool) — both real,
  ongoing burdens for no benefit `program_context`'s existing
  zero-initialized-static approach doesn't already get for free.

**F — Eager format at construction (chosen)**
- Good: printf-style call site preserved exactly; construction never needs
  to know or care what the format string contains; no new storage shape for
  captured arguments; minimal new implementation surface, and therefore
  minimal new surface for bugs.
- Bad: pays the full `stbsp_sprintf` cost (measured ~4-8x a capture-only
  pass, more with floating-point conversions) on every error, even ones
  never actually reported; the rendered text needs a home that isn't a
  string literal.

**F1 — Deferred, explicit typed-argument capture (considered, not chosen for now)**
- Good: measured 4-11x cheaper at construction than eager formatting;
  construction never inspects the format string either, same as F.
- Bad: a different, less familiar call site than plain printf-style `...`;
  more new implementation surface (the tagged-value array and its capture
  macros) to get right the first time.

**F2 — Store the `va_list` itself for later rendering (considered, ruled out)**
- Bad only: not achievable in portable C regardless of implementation
  effort. A `va_list`/`va_copy` is a descriptor into the originating call's
  stack frame on essentially every real ABI, not a value copy — it is
  invalid the instant that frame is gone, the same way a raw pointer into
  it would be. Rejected outright, not a tradeoff.

## Consequences
Easier: the motivating problem — the same error code produced by several
different call paths — becomes straightforward to diagnose, since the exact
path is reconstructible from the chain rather than requiring a debugger
session. Hot-path error construction and transparent forwarding stay cheap
regardless of how rich the eventual trace is. Clients never manage
individual error lifetimes.

Harder: the pool is a new foundational primitive that has to be gotten
right once and trusted thereafter, the same way `program_context` already
is. Growth-triggered reallocation is a real allocation point that a
fully allocation-free system wouldn't have — accepted deliberately, with
sizing and grow/shrink timing left to the client so the performance
consequence is theirs to choose, not hidden. An out-of-memory condition
while growing is unconditionally fatal; a long-running process that would
rather degrade than terminate does not have that option under this design —
accepted knowingly in exchange for never silently losing part of an error's
identity.

One more consequence worth flagging for any tooling built on top of this:
the pool's blocks are meant to live for the process's whole lifetime unless
the client explicitly shrinks them. A leak-detecting allocator — including
Nexus's own memory debugger — should treat blocks still reachable from the
pool at process exit as intentional long-lived state, the same way it
should already treat `program_context`'s static storage, not report them as
genuine leaks.

A message that would need truncating to fit `NEXUS_PROGRAM_ERROR_MESSAGE_MAX_SIZE`
is treated as a programmer error, not a runtime condition to degrade
gracefully — `stbsp_vsnprintf`'s return value (how long the text would have
been unbounded) is checked against the max size via the existing
`NEXUS_PROGRAM_PRECONDITION` mechanism, which aborts in `NEXUS_DEVELOPER_MODE`
and compiles away entirely otherwise, consistent with every other
precondition in this file. `format` itself is optional — `NULL` means "no
message" and is handled explicitly rather than reaching `stbsp_vsnprintf`
at all, since a NULL format string is not a violation, it's a legitimate
"nothing to say" case (assertions in particular routinely have none).

## Framework Hook (optional)
Directly an OODA "Decide" in STYLE_GUIDE.md's sense — this record exists
because the moment of deciding was expensive enough not to re-derive later.
It also mirrors § MICRO Inner Resilience's construction-vs-diagnostics
split: the code path that runs on every failure is kept minimal and
predictable, while the path that only runs when a human is actually looking
(printing/walking the chain) is allowed to be heavier. The pool itself is a
small Black-Box Module in the STYLE_GUIDE.md sense — its interior (bump
allocation today, potentially something smarter later) is deliberately
hidden behind query/grow/shrink so it can be re-optimized without touching
anything built on top of it. The eager-vs-deferred formatting call (F above)
is a direct application of "real optimization decisions are driven by the
profiler, not guesswork" (CLAUDE.local.md § Performance): the decision was
made *with* measured numbers in hand rather than either guessing formatting
is fine or preemptively building the more complex deferred path, and stays
provisional pending real profiling data rather than being treated as final.
