# 0007. nexus_hijack.h: opt-in malloc/calloc/realloc/free/memset/memcpy redirect, poisoning stays the default, plus nexus_memory_foreign_free

## Status
Proposed

## Context and Problem Statement
`nexus.h` poisons `malloc`/`calloc`/`realloc`/`free`/`memset`/`memcpy`/`mmap`/`munmap`/`mprotect`
by default (`C_POISON`, guarded by `NEXUS_INTERNAL_MEMORY_IMPLEMENTATION` for `n_memory.c`'s own
implementation) -- a translation unit that writes one of those raw calls fails to compile. That
catches an accidental non-Nexus allocation in code that should always go through
`nexus_memory_*`, which is exactly what it's for. It cannot, by construction, help a translation
unit that *has* to compile genuine third-party source using those calls internally (a vendored
`.c` file, e.g. something under `external/`, or a header-only library) -- poisoning simply
refuses to compile that source at all inside a TU that also wants Nexus's own tracked allocator.

Mr. Hoorn asked for a second, opt-in path modeled directly on Eskil Steenberg's Forge (`forge.h`'s
`FORGE_MEMORY_DEBUG` redirect of `malloc`/`realloc`/`free` to Forge's own tracked equivalents --
already a credited influence at `nexus.h`'s own top), with one explicit constraint: it must not
replace poisoning as the default, only add an alternative a translation unit opts into.

## Decision Drivers
- Dependability/Insulation: every allocation this process makes should be visible to Nexus's
  tracker when it's practical to arrange that, not just the allocations Nexus-native code itself
  makes.
- Poisoning's own purpose (catching an *accidental* raw call) must stay intact everywhere this
  new header isn't explicitly opted into -- the two mechanisms coexist, not compete.
- Explicit over implicit: a translation unit's choice to redirect rather than poison has to be a
  visible, deliberate `#include`, not an ambient global switch that silently changes behavior
  everywhere.
- `nexus.h`'s own explicit, already-documented decision to have no calloc-equivalent in Nexus's
  native API (directly above where the redirect's backing function is now declared) is a real
  constraint this decision has to respect, not quietly reverse.

## Considered Options
- **Where the redirect lives**: A -- fold the `#define`s into `nexus.h` itself, gated by a macro
  defined before including it. B -- a separate header (`nexus_hijack.h`), meant to be included
  *before* `nexus.h` (chosen).
- **calloc**: A -- leave `calloc` poisoned even under hijack (partial coverage). B -- back it
  with a new, clearly-labeled-internal function reusing `nexus_memory_allocate` +
  `nexus_memory_bytes_set`, not exposed as a general Nexus-native convenience (chosen).
- **mmap/munmap/mprotect**: A -- also redirect them, adapting their signature to
  `nexus_memory_map`/`unmap`/`protect`. B -- leave them poisoned unconditionally, hijack or not
  (chosen).

## Decision Outcome
**Separate header (B):** `nexus_hijack.h`, included before `nexus.h`, rather than a flag inside
`nexus.h` itself. `nexus.h`'s own poison block runs at a fixed point relative to its own
`#include <stdlib.h>`/other system headers; a caller-controlled `#ifdef` toggling that behavior
from *inside* `nexus.h` would make the same header behave differently depending on external state
set by something the reader has to go find, whereas "this file is included, or it isn't, before
`<nexus.h>`" is visible at the top of the translation unit it affects -- the same reasoning that
already put `NEXUS_INTERNAL_MEMORY_IMPLEMENTATION` in `n_memory.c` alone rather than as a runtime
check. `nexus.h`'s poison block does gain one new condition (`#ifndef NEXUS_MEMORY_HIJACK`), the
minimum needed to let this second header suppress it for six of the nine already-poisoned
identifiers.

**calloc (B):** `_nexus_memory_hijack_calloc(count, size, file, line)`, declared and implemented
in `nexus.h`/`n_memory.c` directly beside the existing "we have no calloc equivalent on purpose"
comment, with its own doc comment stating explicitly that this doesn't reverse that decision --
it exists only to give the redirect a faithful `calloc()` replacement for third-party source that
already calls it and expects real `calloc` semantics, not as a Nexus-native convenience. Nexus's
own code still writes `nexus_memory_allocate` + `nexus_memory_bytes_clear` explicitly, same as
before this record. Rejected leaving `calloc` poisoned under hijack (A): the stated goal was a
third party's *own* allocations getting tracked once its source is compiled under this header,
and a third party calling `calloc` internally (common) would silently keep failing to compile
under a hijacked TU otherwise -- a partial redirect that still breaks on contact with real
third-party source doesn't achieve what this was asked to do.

**mmap/munmap/mprotect (B):** left poisoned unconditionally. Unlike the allocation functions,
their signatures don't reorder-map onto `nexus_memory_map`/`unmap`/`protect`'s the way `memset`/
`memcpy`'s argument order does onto `nexus_memory_bytes_set`/`copy` -- `nexus_memory_map` takes a
`NexusFileHandle *`/`NexusMemorySharing` pair `mmap` has no equivalent parameters for, and returns
a `NexusError *` rather than `mmap`'s pointer-or-`MAP_FAILED` convention, so a macro-only redirect
would need real translation logic (flag/protection-bit mapping, an out-parameter reshuffle) rather
than a straight forward. A caller that needs a mapping calls `nexus_memory_map` directly.

**A load-bearing limitation, not a design choice -- verified, not assumed:** this mechanism only
reaches third-party code compiled as *source* in the same translation unit (a vendored `.c` under
`external/`, a header-only library). It cannot retroactively track allocations made by a
pre-built shared or static library already linked against (e.g. `libxcb`) -- that code's `malloc`
calls were already compiled to machine code with no visibility into this header's macros at all.
Concretely: this does not eliminate `libs/janus/src/j_platform_libc.c`'s `_j_platform_libc_free`,
which frees XCB reply structs XCB itself allocated via its own (pre-built, linked) `malloc` --
that boundary has no substitute for calling XCB's own matching `free` directly, isolated in its
own translation unit per Insulation, regardless of this record. Verified empirically: compiled
and ran a standalone program exercising every redirected call (`malloc`/`calloc`/`realloc`/`free`/
`memset`/`memcpy`) under `NEXUS_MEMORY_DEBUG_MODE=1` (guard bytes, checksums, the tracked
allocation list all active, not just the release passthrough) -- correct zero-fill, correct
copy/set semantics, no guard-byte corruption across independent allocations, clean
tracked-deallocate with no panic. Also verified the regression case: a translation unit including
only `<nexus.h>` (no hijack) still fails to compile on a raw `malloc()` call, unchanged.

### Pros and Cons of the Options
**Where the redirect lives A -- fold into nexus.h, flag-gated**
- Good: one header to look at instead of two.
- Bad: makes `nexus.h`'s own poison behavior conditional on external state the reader has to
  discover was set elsewhere, rather than a visible `#include` at the point it takes effect.

**calloc A -- leave poisoned under hijack**
- Good: no new function, `calloc`'s no-equivalent stance stays literally undisturbed.
- Bad: defeats the actual purpose for any real third-party source that uses `calloc` internally,
  which is common -- rejected as not actually solving the problem this was asked to solve.

**mmap/munmap/mprotect A -- redirect with signature translation**
- Good: full parity with the poison list; a hijacked TU has nothing left poisoned that isn't also
  redirectable.
- Bad: real adapter logic (flag/protection-bit translation, return-convention reshuffle) with a
  meaningfully higher chance of a subtle, hard-to-notice translation bug (wrong protection bits
  silently mapped) for a case with no concrete motivating consumer in this codebase right now --
  `nexus_memory_map` already exists and is what any Nexus-aware caller should reach for directly.

## Consequences
Easier: a future vendored source dependency (under `external/`, or a future header-only library)
that allocates internally can have those allocations tracked by Nexus's debugger by including
`nexus_hijack.h` first in whichever translation unit compiles it, without patching that
dependency's own source.

Harder / unchanged: this does not reduce Janus's `_j_platform_libc_free` boundary at all -- see
the load-bearing limitation above. No current translation unit in this codebase includes
`nexus_hijack.h` yet; this record establishes the mechanism for when one does.

## Framework Hook (optional)
Directly Dependability: this widens how much of a process's actual allocation activity Nexus's
tracker can see, for the specific case (vendored source) where that's achievable, while leaving
poisoning's own guarantee -- catching an accidental raw call in code that should never make one --
completely intact everywhere this header isn't deliberately opted into. The calloc resolution is
Black-Box Modules' Cheng/Ch'i split applied literally: the existing no-calloc-equivalent decision
governs Nexus's own face to Nexus-native callers unchanged; `_nexus_memory_hijack_calloc` is
interior machinery serving a different, narrower contract (faithfully proxying a libc call for
code that isn't Nexus-native and doesn't know Nexus exists) that never crosses back into the
face poisoning and Nexus's public API present to ordinary callers.

## Related Decisions (optional)
None superseded. Sits beside `NEXUS_INTERNAL_MEMORY_IMPLEMENTATION` (the pre-existing,
narrower escape hatch for `n_memory.c`'s own implementation) without changing it.

## Amendment: nexus_memory_foreign_free (added, same record)
This record's own "load-bearing limitation" left `libs/janus/src/j_platform_libc.c` in place --
a Janus-local translation unit that deliberately doesn't include `nexus.h` at all, isolating one
raw libc `free()` call for XCB reply structs XCB itself allocated. Correct, but the isolation
trick was Janus's own to invent and would have had to be reinvented by any future consumer with
the same need (freeing memory a pre-built, linked third-party library allocated and owns the
allocator contract for -- the case `nexus_hijack.h` explicitly cannot reach, per this record's
own limitation above).

Added `nexus_memory_foreign_free(void *ptr)` (`nexus.h`, implemented in `n_memory.c`, which
already has unpoisoned access to real libc `free` via `NEXUS_INTERNAL_MEMORY_IMPLEMENTATION`) as
the one sanctioned way to do this: a normal function, not the token `free`, so it is untouched by
poisoning in a default translation unit and untouched by `nexus_hijack.h`'s redirect in a
hijacked one -- verified by calling it correctly from both. `libs/janus/src/j_platform_libc.c` is
deleted; `j_platform_x11.c`'s two call sites now call `nexus_memory_foreign_free` directly like
any other Nexus function. NULL is a no-op, mirroring libc `free(NULL)` and
`nexus_filesystem_file_close`'s own contract.

This is judged a clean additive facet of the same "how does Nexus code coexist with memory a
foreign allocator owns" decision this record already covers -- Status stays Proposed rather than
a new record, since nothing above changes: `nexus_hijack.h`'s redirect, its scope, and its stated
limitation are all unchanged by this addition; this only gives that limitation's known workaround
(one library reaching for raw libc `free` in its own isolated translation unit) a single,
Nexus-owned home instead of requiring each consumer to invent it locally.
