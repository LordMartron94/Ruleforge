# 0006. Anonymous memory-backed files and file-backed memory mapping: split across FILESYSTEM/MEMORY, opaque handle with an explicit descriptor accessor, enum-based sharing, Linux-only

## Status
Proposed

## Context and Problem Statement
Janus's Wayland backend needs to hand a Wayland compositor a shared-memory buffer: create an
anonymous, memory-backed file descriptor, map it into this process's address space with
`MAP_SHARED` (so writes are visible to whichever other process maps the same descriptor), and
give the raw descriptor to `wl_shm_create_pool`, which sends it to the compositor over the
Wayland socket. `nexus_memory_map` could not serve this at all -- it only ever creates a fresh,
anonymous, `MAP_PRIVATE` mapping (`libs/nexus/src/n_memory.c`, `mmap(NULL, size, protection,
MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)`), with no fd parameter and no sharing concept. Janus worked
around this with its own `_j_platform_libc_shm_buffer_create`/`_destroy` in
`libs/janus/src/j_platform_libc.c`, calling `memfd_create`/`ftruncate`/`mmap`/`munmap` directly.

That is exactly the situation Mr. Hoorn's own stated principle for this codebase rules out:
Nexus is the foundational library; other libraries should almost never have to import standard
libraries themselves. This record is that capability moving into Nexus, in the systems where it
belongs, so Janus (and any future consumer with the same need) delegates instead of reimplementing
it.

## Decision Drivers
- Dependency Insulation (`STYLE_GUIDE.md`): a consumer library reaching around Nexus to touch
  `mmap`/`memfd_create` directly is the exact coupling Nexus exists to absorb on every consumer's
  behalf.
- Black-Box Modules: `NexusFileHandle` is documented as opaque, cast only at the boundary in
  `n_filesystem.c` -- any crack in that has to be a deliberate, narrow, explicit one, not a
  general leak.
- Explicit over implicit (`STYLE_GUIDE.md`'s core mandate): a mapping's sharing semantics
  (`MAP_PRIVATE` vs `MAP_SHARED`) must be a visible, typed choice at the call site, never an
  implicit consequence of which overload or code path a caller happens to take.
- Reusability at the right rank: an anonymous memory-backed file is useful on its own (built up
  via ordinary buffered writes, never mapped at all), not just as raw material for a mapping --
  narrowing it to only the mapping use case would under-serve the Dependable Dependencies
  Principle rank a foundational library is held to.
- Existing precedent for shipping Linux-only foundational capability rather than blocking on
  portability: `adr/0004-process-spawn-communicate` (`pidfd_open`/`pidfd_send_signal`, no
  fallback attempted for a first version).

## Considered Options
- **Module boundary**: A -- everything in `nexus_memory`, which internally creates and owns its
  own fd, never exposing it. B -- split: `nexus_filesystem` owns anonymous-file creation and
  lifecycle (a `NexusFileHandle`, like any other file), `nexus_memory` owns mapping any
  `NexusFileHandle` into the address space (chosen).
- **Descriptor exposure**: A -- `nexus_memory_map` takes a raw platform fd directly, bypassing
  `NexusFileHandle` for this case. B -- takes an opaque `NexusFileHandle *`; a new, explicit
  `nexus_filesystem_file_descriptor_get` is the one deliberate crack in its opacity, for a caller
  that must hand the raw descriptor to something outside Nexus entirely (chosen).
- **Sharing mode representation**: A -- a boolean parameter. B -- folded into
  `NexusMemoryProtection`'s existing bitflags. C -- a new, dedicated `NexusMemorySharing` enum
  (chosen).
- **Scope**: A -- narrow: just enough to back a mapping (e.g. an fd the caller can never read/
  write through Nexus's own API). B -- general-purpose: the same `NexusFileHandle` and the same
  `nexus_filesystem_file_read`/`write`/`flush`/`close` surface as a path-backed file, just without
  a `NexusPath` (chosen).
- **Portability**: A -- design the macOS/BSD (`shm_open`) and Windows (`CreateFileMapping`) shapes
  into the API now, even with only Linux implemented. B -- Linux-only for now, same
  `NEXUS_PLATFORM_UNSUPPORTED_RAISE` posture as the rest of this file outside
  `NEXUS_PLATFORM_POSIX` and as `adr/0004` already established for a comparable case (chosen).
- **`nexus_memory_map`'s existing signature**: A -- leave it untouched, add a distinctly-named
  sibling function for the file-backed case. B -- widen it directly (`file_handle` +
  `sharing` parameters), updating the one existing call site
  (`libs/nexus/src/n_stack.c:nexus_stack_create`) to pass `NULL`/`NMS_PRIVATE` (chosen).

## Decision Outcome
**Module boundary (B):** `nexus_filesystem_file_anonymous_create(initial_size, out_file_handle)`
creates the file (Linux: `memfd_create` + `ftruncate` + `fdopen`, wrapped as the same
`NexusFileHandle` -- a `FILE*` underneath -- every other filesystem function already uses).
`nexus_memory_map` maps any `NexusFileHandle`, path-backed or anonymous. Filesystem owns
descriptor/handle lifecycle; memory owns address-space mapping -- the same separation of concerns
`nexus_filesystem`/`nexus_memory` already have for every other resource each one manages.

**Descriptor exposure (B):** `nexus_filesystem_file_descriptor_get(file_handle)` (`fileno`
underneath) is now declared directly beside `NexusFileHandle`'s own doc comment, which no longer
claims the handle is "never exposed" -- it says exactly what the one exception is and why.
`nexus_memory_map` calls this same accessor internally to get the descriptor `mmap` needs; a
consumer like Janus calls it directly only when it has to hand the raw descriptor to something
outside Nexus (Wayland's `wl_shm_create_pool`, which sends it to the compositor over the socket).

**Sharing mode (C):** `NexusMemorySharing { NMS_PRIVATE, NMS_SHARED }`, a required parameter on
the now-five-argument `nexus_memory_map(file_handle, size, protection, sharing, out_pointer)`.
Every existing call site had to state its choice explicitly rather than inherit a silent default.

**Scope (B):** an anonymous file behaves exactly like a path-backed one everywhere except that it
has no `NexusPath` and nothing to unlink -- `nexus_filesystem_file_read`/`write`/`flush` all work
on it unchanged. A caller that never intends to map it at all (building a buffer purely through
buffered writes) gets a real, general capability, not a narrow mmap-only helper wearing a
filesystem-shaped API.

**Portability (B):** Linux-only. `memfd_create` has no equivalent on macOS/BSD (`shm_open`,
POSIX but named, needs an explicit `shm_unlink`) or Windows (`CreateFileMapping`/`MapViewOfFile`,
a different API shape entirely); designing all three into the signature now, with two of them
permanently `NEXUS_PLATFORM_UNSUPPORTED_RAISE`, would be speculative surface with no second
platform to validate the abstraction against -- the same reasoning `adr/0004` already applied to
`pidfd`.

**Existing signature (B):** `nexus_memory_map` was widened in place. Nexus is the only caller of
its own function (`n_stack.c`), so there was no external compatibility cost to preserve, and one
call site was cheap to update -- a distinctly-named sibling would have meant two ways to reach the
same `mmap` for no consumer-facing benefit.

### Pros and Cons of the Options
**Module boundary A -- everything in `nexus_memory`, fd fully internal**
- Good: one function, one file, no cross-module call.
- Bad: an anonymous memory-backed file is a real filesystem-shaped resource (readable, writable,
  resizable) with a use independent of ever being mapped -- confining it to `nexus_memory` forces
  every future non-mapping consumer of the same idea to either duplicate it or reach into memory's
  internals for something conceptually filesystem-shaped.

**Descriptor exposure A -- raw fd parameter on `nexus_memory_map`**
- Good: no new accessor, no crack in `NexusFileHandle`'s opacity to justify.
- Bad: silently reintroduces exactly the "hand out a raw platform descriptor" pattern Black-Box
  Modules exists to prevent, and does nothing for a caller (Janus) that separately needs the same
  raw descriptor for a reason that has nothing to do with mapping (handing it to Wayland).

**Sharing mode A -- boolean**
- Good: fewer characters at the call site.
- Bad: `true`/`false` at a call site says nothing without reading the parameter name; rejected on
  the same explicit-over-implicit grounds `NexusMemoryProtection` and `NexusFileMode` already
  settled as enums, not flags of undocumented booleans.

**Sharing mode B -- fold into `NexusMemoryProtection`**
- Good: no new type.
- Bad: protection (read/write/exec) and sharing (private/shared) are orthogonal axes that combine
  freely -- cramming a second axis into one bitflag enum obscures that they're independent
  questions, and `NexusMemoryProtection`'s existing combinations (e.g. `NMP_READ_WRITE`) would
  become ambiguous to extend further.

**Scope A -- narrow, mapping-only**
- Good: smaller surface, nothing to explain beyond "backs a mapping."
- Bad: throws away reuse of the entire existing `nexus_filesystem_file_*` surface for free, and
  under-serves what a foundational, DDP-high-rank library owes its consumers -- rejected once the
  general-purpose version turned out to cost nothing extra (the same `FILE*` representation
  already used everywhere else in this module).

**Portability A -- design all three platforms in now**
- Good: no signature change needed if/when macOS/BSD/Windows support lands.
- Bad: two of the three shapes would be pure speculation with no implementation and no consumer to
  validate them against, and Nexus itself already treats `NEXUS_PLATFORM_UNSUPPORTED_RAISE` as an
  accepted, temporary posture (every function in this file has one) -- there is no reason this one
  needs to preempt that pattern.

**Existing signature A -- new sibling function, `nexus_memory_map` untouched**
- Good: zero risk to the one existing call site.
- Bad: two entry points that both end in the same `mmap` call, differing only in whether
  `file_handle` happens to be NULL, is a distinction without a difference for the API's own
  contract -- and Nexus already has zero external callers to actually protect from a widened
  signature.

## Consequences
Easier: `libs/janus/src/j_platform_libc.c` no longer imports `<sys/mman.h>`/`<unistd.h>` or
defines `_DEFAULT_SOURCE`/`_GNU_SOURCE` itself at all -- it shrank back to exactly one function
(`_j_platform_libc_free`, for XCB's libc-allocated replies, a genuinely different kind of boundary
that Nexus's tracked `nexus_memory_deallocate` cannot safely absorb, since it validates every
pointer against its own tracked-allocation registry and would panic on a foreign one). Any future
consumer that needs a shared, anonymous, cross-process memory segment -- not just Janus, not just
Wayland -- gets it from Nexus directly.

Harder: this capability is Linux-only; a consumer needing it on macOS/BSD/Windows gets
`NEXUS_PLATFORM_UNSUPPORTED_RAISE` until a second implementation is written, same disclosed cost
`adr/0004` already accepted for `pidfd`. `nexus_memory_map`'s page-rounding behavior (documented
already, unchanged by this record) now has one more caller-visible interaction to get right: the
size passed to `nexus_memory_map` and later to `nexus_memory_unmap` must agree, which for a
file-backed mapping also means the backing file must be at least that (page-rounded) size, or the
mapping's tail pages fault -- `nexus_filesystem_file_anonymous_create`'s caller is responsible for
sizing the file accordingly, the same way `nexus_stack_create` already page-aligns before calling
`nexus_memory_map` today.

## Framework Hook (optional)
Directly Dependency Insulation, restated in Mr. Hoorn's own words as the driver for this record:
"other libraries should almost never have to import standard libraries." Black-Box Modules governs
the shape of the fix, not just its existence -- opacity stays the default, and the one exception
(`nexus_filesystem_file_descriptor_get`) is a named, documented, singular escape hatch rather than
a general widening of what `NexusFileHandle` reveals. Choosing to widen `nexus_memory_map` in
place rather than freeze its current shape is also an application of Rewriting vs. Refactoring's
point that an API designed before its second real use case is often still finding its actual
shape -- this was that second use case.

## Related Decisions (optional)
Follows the Linux-only-for-now precedent set by
[0004-process-spawn-communicate](./0004-process-spawn-communicate.md) (`pidfd`, no portable
fallback attempted for a first version) for the same reason: a real second-platform
implementation to validate an abstraction against does not exist yet, and would be speculative
otherwise.
