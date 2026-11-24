# 0002. Memory debugger tracking: self-verifying inline headers over guard-bounded blocks

## Status
Accepted. Core implemented in `n_memory.c` as of 2026-09-18: inline
self-verifying headers, two-sided guards, the intrusive live-allocation list,
the checksummed corruption-recovery path, the quarantine ring with content
poisoning, `nexus_memory_mark_persistent`/`nexus_memory_comment_set`, and the
leak/summary reports (with per-call-site hotspot detection). Not yet done:
the memory-operation logging mentioned in `n_memory.c`'s own `TODO`s,
deferred to a separate change.

## Context and Problem Statement
`nexus_memory_allocate`/`reallocate`/`deallocate` (`nexus.h`, `MEMORY`
section) are the sole sanctioned entry points for heap memory in Nexus —
`malloc`/`calloc`/`realloc`/`free`/`memset`/`memcpy` are poisoned for every
translation unit except `n_memory.c` itself. Today they are thin,
unconditional pass-throughs to libc; the `file`/`line` parameters captured by
the `nexus_memory_*` macros are accepted but unused. A `TODO` in
`n_memory.c` names the gap: a real debug-mode implementation, gated by the
already-declared `NEXUS_MEMORY_DEBUG_MODE` macro, that turns this from a bare
wrapper into an actual observability tool.

STYLE_GUIDE.md's Quality Assurance section is explicit that this kind of
tooling is not optional polish: "we cannot simply observe the code as static
text... true systemic debuggability requires engineering the very lenses
through which the software's failures will be viewed." A memory debugger is
one of the named examples. It needs to, at minimum: detect buffer
overflow/underflow, detect leaks, detect double-free/use-after-free, and
expose live/peak/aggregate statistics for profiling — while adding as close
to zero cost as possible to the already-shipped release path, since
`NEXUS_MEMORY_DEBUG_MODE` gates *inclusion*, not *frequency* — allocation
happens constantly even in a debug build.

Because `nexus_memory_*` is the lowest layer in the library — every future
data structure, string, or I/O buffer in Nexus will allocate through it —
it cannot depend on any general-purpose container (a hash map, a dynamic
array module) for its own bookkeeping. Such a container would itself
allocate through `nexus_memory_*` once it exists, which is circular: the
allocator's internal registry cannot be built out of something the allocator
is supposed to be underneath.

Separately, WG14 N2464 (adopted into C23) makes `realloc(ptr, 0)`
undefined behavior for *any* `ptr`, including `NULL` — a change from prior
standards, where it was implementation-defined (typically: free the object,
return `NULL`). A debug build that still calls libc's `realloc` with a
caller-supplied size of zero is silently relying on UB.

## Decision Drivers
- **Performance/footprint discipline** (STYLE_GUIDE § MICRO Inner Resilience,
  Performance is first-class) — this is one of the hottest call paths in the
  entire library; the common case must stay O(1) with no hidden scans.
- **Dependable Dependencies / layering** (STYLE_GUIDE § Dependency
  Management) — the allocator is foundational and cannot depend on any
  module that will itself depend on it.
- **Explicit, loud failure over silent corruption** (STYLE_GUIDE § Error
  Handling) — a detected integrity violation must be reported accurately,
  never trusted-through.
- **Second-Order Observation** (STYLE_GUIDE § Quality Assurance) — the
  debugger's entire reason to exist is that first-order code reading cannot
  reveal actual runtime heap behavior.
- **C23 correctness** — `realloc(ptr, 0)` must never reach libc; it is real
  UB (WG14 N2464), not a matter of style.
- **"Not extremely complex machinery"** — an explicit constraint from Mr.
  Hoorn: favor the simplest design that still meets the above, over maximal
  detection power.

## Considered Options
1. **External per-callsite bucket table, linear-scan lookup** — Eskil
   Steenberg's Forge `f_mem_debug.c`, both the archived
   (`gmCAD/quelsolaar-archive/Forge/f_mem_debug.c`) and current
   (`quelsolaar/MergeSource/f_mem_debug.c`) variants: a fixed/growable array
   of per-`(file,line)` buckets, each holding a dynamic array of live
   `{pointer, size}` pairs, found by scanning every bucket and every entry in
   it.
2. **General-purpose hash map for the live-allocation registry.**
3. **Inline per-block header, trusted as-is** — metadata (size, file, line,
   intrusive list links) stored directly before the user's data, in the same
   allocation, with no self-verification.
4. **Inline per-block header, self-verifying** — as (3), plus a checksum
   over the header's own fields, verified before any field is trusted; a
   full O(n) list walk (matching by raw pointer identity) as the recovery
   path used only when guard or checksum verification fails.
5. **Hand-rolled fixed-capacity pointer-keyed open-addressing table** —
   metadata kept fully external to any client block (average O(1) lookup via
   hashing the pointer value), built by hand rather than via a general
   container, to stay layering-compliant.

## Decision Outcome
**Option 4** — inline self-verifying header, threaded into a global
intrusive doubly-linked list, with two-sided guard bytes and a header
checksum — combined with three settled facets of the same design:

- **Guard placement**: a fixed-width leading guard sits between the header
  and the user's data (closer to the pointer than the header is), and a
  trailing guard sits after it — both filled with a recognizable byte
  pattern at allocation time. Putting the guard on the side facing the
  caller, rather than the header, means guard width does double duty: it
  bounds ordinary overflow/underflow *and* raises the depth an underflow
  needs to reach before it can corrupt the header itself.
- **Header self-verification**: a checksum over the header's other fields
  (size, file, line, intrusive links), recomputed and compared before any of
  those fields are trusted. A single magic constant was considered and
  rejected — it only catches corruption that happens to land on its own
  bytes; a stray write elsewhere in the header (e.g. directly on `size` or
  the list pointers) would pass a magic check while still being corrupted.
- **Corruption recovery path**: when a guard or the header checksum fails,
  the block's own `prev`/`next` are never trusted for unlinking. Instead, a
  full O(n) walk of the global list from its head, matching by raw address,
  safely locates and removes the node. This is only ever paid at the moment
  corruption is already detected — a panic-shaped event, where STYLE_GUIDE's
  own cost model says cost is "close to irrelevant" because the alternative
  (trusting corrupted metadata) is worse than being slow.
- **Use-after-free / double-free**: a bounded quarantine ring (own
  fixed-capacity, `malloc`-backed storage, layering-compliant the same way
  Steenberg's own bookkeeping arrays are) holding the last *N* freed blocks.
  `free()` is deferred for a quarantined block — the memory is poisoned and
  held, not returned to libc, until its ring slot is recycled — so a
  use-after-free shortly after freeing lands on recognizable poison instead
  of reused memory. The ring also carries alloc-site and free-site
  file/line, giving a double-free a full "already freed at X, originally
  allocated at Y" diagnostic for free.
- **Check cadence**: guard and checksum verification is mandatory at every
  free/realloc of a given block (cheap — it touches only that block). A
  separate, explicitly-invoked full sweep additionally walks every *live*
  block's guards and every *quarantined* block's entire body against its
  poison pattern — two different checks answering two different questions
  (boundary violation vs. a write that landed within bounds via a dangling
  pointer) — paid only when the caller opts in.
- **Leak reporting**: `nexus_memory_mark_persistent(ptr)` flags a header so
  the leak sweep skips reporting it, without unlinking it from the live
  list — it still participates in guard/checksum verification. Resolves the
  known trap from the pre-reset implementation (the error-pool and
  `program_context` globals must never be reported as leaks) with an
  explicit, self-documenting call at the allocation site rather than an
  implicit exception baked into the sweep itself.

Decisive reason: this is the only option that keeps the common-path cost at
O(1) (satisfying the performance driver) without depending on any container
module (satisfying the layering driver), while still closing the
metadata-corruption gap surfaced during design (satisfying the loud-failure
and observability drivers). The external-table alternatives either
reintroduce an O(n) scan on every free/realloc, or (option 5) require a
hand-rolled hash table with deletion — real, fiddly complexity of its own —
while not actually eliminating the corruption risk, only reducing its
likelihood: in a single flat address space, a sufficiently wild or large
underrun can still reach memory that lives elsewhere in the heap regardless
of how far away it's placed.

### Pros and Cons of the Options

**1. External per-callsite bucket table (Steenberg/Forge)**
- Good: real, battle-tested prior art; metadata physically separate from
  client blocks, so an ordinary overflow/underflow can never corrupt it;
  simple to reason about.
- Bad: free/realloc scan every bucket and every live entry in it to find a
  pointer — a hidden O(n) cost per call that grows with live allocation
  count, exactly the class of hazard flagged in this project's own
  performance rules.

**2. General-purpose hash map**
- Good: would give O(1) average lookup with a data structure Nexus will
  eventually have anyway.
- Bad: doesn't exist yet, and even once it does, using it here inverts the
  dependency the allocator sits at the bottom of — the hash map's own
  buckets would allocate through `nexus_memory_*`, which is the module being
  implemented. Rejected on layering grounds alone, independent of whether
  the hash map existed.

**3. Inline header, trusted as-is**
- Good: O(1) everywhere, zero extra bookkeeping allocation — the header
  rides along with the block the caller already asked for.
- Bad: a client underflow deep enough to skip the guard lands directly on
  the header's own `size`/`prev`/`next`. Trusting those blindly during
  unlink can turn a benign-looking bug into a wild pointer write elsewhere
  in the heap — worse than the corruption the tool exists to catch. This is
  what option 4 exists to fix.

**4. Inline header, self-verifying (chosen)**
- Good: keeps every strength of (3) — O(1), no extra allocation, no
  container dependency — while never trusting header contents without
  verifying them first; the expensive fallback only triggers once
  corruption is already known to have happened.
- Bad: a sufficiently adversarial corruption pattern that defeats *both* the
  guard and the checksum in the same event is not theoretically ruled out —
  accepted because the actual threat model is accidental bugs in Mr.
  Hoorn's own code, not adversarial corruption. Real additional
  implementation surface versus a naive header (checksum computation,
  fallback walk, two-tier sweep) that will need its own dedicated test
  coverage.

**5. Hand-rolled external pointer-keyed table**
- Good: O(1) average lookup without an inline header at all, so no metadata
  ever sits inside a client block's blast radius.
- Bad: hand-rolled deletion in an open-addressing scheme (tombstones or
  backward-shift) is real, fiddly complexity — the thing Mr. Hoorn explicitly
  asked to avoid — for a guarantee it doesn't fully deliver anyway, since a
  large enough corruption can still reach any address in the same heap.

## Consequences
- Alloc/free/realloc bookkeeping stays O(1) on the common path; leak
  reporting and live/peak/total statistics fall out of the same list
  walk/counters already touched on every operation, at no extra cost.
- Guard width becomes a decision with two purposes (overflow/underflow
  detection *and* header blast-radius protection), not a single free
  tuning knob.
- Double-free and use-after-free get a full diagnostic (original allocation
  site *and* prior free site) essentially for free, as a side effect of the
  quarantine ring already existing.
- **Negative, accepted**: the quarantine ring keeps up to *N* freed blocks'
  worth of memory fully resident at all times while debug mode is active —
  a deliberate, real footprint cost, not a bug. *N* has no single correct
  default and will need tuning against actual workloads once benchmarked.
- **Negative, accepted**: peak-memory statistics must count quarantined
  blocks as still resident (they are), or the reported peak will
  under-represent the program's actual worst-case footprint.
- **Negative, accepted**: this is now one of the largest pieces of
  hand-written logic behind a single `#if`, and the module every other
  Nexus system will sit on top of — per STYLE_GUIDE's Dependable
  Dependencies Principle, its DDP rank demands the heaviest test investment
  in the library, not the lightest.

## Framework Hook
Second-Order Observation (STYLE_GUIDE § Quality Assurance) is the direct
justification for building this at all — the tool exists because reading
`n_memory.c` as static text cannot reveal what the heap actually does at
runtime. The layering constraint that ruled out options 2 and 5 is the
Dependable Dependencies Principle in its most literal form: the module nearly
everything else depends on must not depend upward on any of them, even
transitively through its own bookkeeping.

## Related Decisions
Echoes a driver from [0001-nexus-error-representation](./0001-nexus-error-representation.md):
that record required `NexusError` not depend on machinery whose own failure
would need reporting through the same error system. The same shape of
constraint applies here — the memory debugger's own bookkeeping must not
depend on machinery (a general container) that would itself route failures
back through the allocator being instrumented.
