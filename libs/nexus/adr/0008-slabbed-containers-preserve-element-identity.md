# 0008. Slabbed containers preserve element identity

## Status

Proposed draft for Mr. Hoorn to finalize. The surviving-element pointer
contract was explicitly selected for this implementation.

## Context and Problem Statement

Nexus's growable list, stack, FIFO queue, priority queue, and flat HashMap
store payloads inline. Growth relocates storage. List insertion/removal and
sorting, queue repacking, and heap sifting can also move a surviving element
even without an allocation. Merely splitting these byte arrays into slabs
would preserve addresses during growth but would not preserve which element
a pointer denotes during those other operations.

Mr. Hoorn requested slabbed variants wherever valuable and explicitly chose
to preserve element identity through all other mutations. Removing the
pointed-to element, clearing, or destroying the container ends its lifetime.

## Decision Drivers

- Preserve the address of each surviving payload, including HashMap keys.
- Retain indexed access and the established container semantics.
- Allocate in batches, reuse dead slots, and let clients reserve capacity.
- Keep storage opaque and continue using the existing Nexus allocator.
- Keep the ordinary contiguous variants available for compact bulk storage.

## Considered Options

1. Slab the existing inline payload arrays, guaranteeing stability on growth.
2. Keep payloads in slabs and move references for ordering and indexing.

## Decision Outcome

Use separate `NexusDataSlabbedList`, `NexusDataSlabbedStack`,
`NexusDataSlabbedQueue`, `NexusDataSlabbedPriorityQueue`, and
`NexusDataSlabbedHashMap` types with `nexus_data_slabbed_*` functions. The
second option satisfies the stronger contract selected by Mr. Hoorn.

Each create accepts an initial capacity and a slab capacity in slots. For
HashMap, initial capacity stays in `NexusDataHashMapConfig`; slab capacity is
an additional create argument. Zero slab capacity selects 64 slots. This is
a growth granularity, never a total limit. Whole slabs remain allocated until
destruction; reserve and clear never compact or relocate live storage.

Slots are rounded to ordinary maximum C alignment and at least pointer width.
The free list occupies dead payload bytes, avoiding a live per-slot node
header. Taking/releasing a slot is constant-time between slab allocations.
Lists, stacks, and heaps use a contiguous reference directory. FIFO queues
use a circular reference directory and grow that directory geometrically.
Sorting and heap sifting operate on references, passing actual payloads to
comparison callbacks. Slabbed HashMaps reuse the existing Swiss-table probe,
tombstone, and rehash mechanics, but table slots hold one reference to a
slab-owned key/value record. Rehash copies references, never payloads.

A pointer may survive a change in index, insertion/removal of other elements,
sorting, reserve, heap adjustment, or rehash. It becomes invalid when its own
element is removed, even if the address is reused for a new element. Clear
invalidates every element pointer. HashMap value replacement writes in place;
keys must remain immutable. Enumeration cursors still expire on structural
mutation, independently of already-returned key/value pointer lifetimes.
Clients synchronize access and must not modify live heap ordering fields.

Fixed array/bitset views and the fixed-capacity ring buffer do not relocate
on growth, and the intrusive linked list already uses caller-owned nodes.
They receive no redundant slabbed variant. Existing array sorting and ring
linearization retain their existing invalidation contracts. Slabbed variants
provide no contiguous payload or linearization operation.

### Pros and Cons of the Options

**Slab inline arrays:** fewer references and efficient scans within a slab;
constant-time index arithmetic is possible. However, list shifts/sorting and
heap swaps still move payload identity. Queue growth also needs careful
segmentation to avoid moving live records. This does not satisfy the selected
contract without a second ordering layer.

**Slab payloads with reference ordering:** surviving elements never move,
large payloads need no copying during reorder/rehash, and removed slots can
be reused without allocating. Costs include an extra indirection, reference
metadata, slot alignment padding, retained peak storage, and weaker scan
locality than the contiguous variants. Even a LIFO stack carries a reference
directory to reuse the same tested lifetime/storage mechanics.

## Consequences

- Clients can safely keep pointers into growable containers for each
  element's live lifetime. Inputs may refer to existing payloads in the same
  container; copy-out buffers must not overlap owned payloads.
- Initial capacity/reserve pays for payload slots and ordering metadata.
  There are no allocator operations while filling within reserved capacity,
  sorting, popping, or clearing the sequence containers. HashMap tombstone
  cleanup may still allocate table metadata after erase/insert churn.
- Neither capacity nor address stability means thread safety or immortal
  handles. Old pointers cannot identify a new element placed in a reused slot.
- The shared Swiss-table implementation adds a private pool discriminator and
  conditional entry access for flat maps; throughput effects are unmeasured.
  Flat and slabbed growth/collision behavior are both covered by focused tests.
- Allocation failure retains Nexus's existing hard allocator failure policy;
  HashMap creation can return an entropy error when OS seeding fails. Size,
  slab-rounding, directory, and slot-count overflow are checked explicitly.
- `NEXUS_ALIGNOF` now derives alignment from the padded structure size minus
  the element size using C89 `sizeof`, without a null member address or the
  C23-only anonymous type definition inside `offsetof`.
  Sanitizer validation exposed that dependency while checking slab alignment.

## Validation

The focused Nexus suite retains payload pointers across mutations, checks
key/value identity through automatic growth and tombstone cleanup, covers
empty/pop/clear/reuse and odd-width alignment, rejects stale enumeration
cursors in development mode, and checks reservation overflow. Memory tracing
checks that filling, sorting, draining, clearing, and refilling all five
reserved variants issues no allocate/reallocate/deallocate operations.

The final focused Helm development run passed. AddressSanitizer passed with
Nexus's guarded debug allocator. Combined AddressSanitizer/UndefinedBehaviorSanitizer
runs passed with the debug allocator disabled and stb's supported
`STB_SPRINTF_NOUNALIGNED` option, in both development and release assertion
modes. LeakSanitizer is unavailable under this execution environment's ptrace.
The normal guarded allocator UBSan run is blocked by a pre-existing unsigned
negative-index expression at `src/n_memory.c:296`; this task does not change it.

Direct `clang -fsyntax-only` checks of `n_data.c`, `nexus_tests.c`, and the
public header pass with the exact compilation-database flags. Development
and release configuration defines are also checked. The unfiltered project
clang-tidy pass has no remaining findings in the changed logic. It still
reports pre-existing include-cleaner noise for `NULL` (provided by Nexus)
and `timespec` (the header already includes `time.h`), plus three findings in
vendored stb. No warning suppression was added.

## Prior Art

[Boost.Container stable_vector documentation](https://www.boost.org/doc/libs/1_92_0/doc/html/doxygen/boost_container_header_reference/classboost_1_1container_1_1stable__vector.html)
provides reference stability with indexed access at the cost of contiguous
payload storage. Nexus uses that separation of payload lifetime and logical
order, but allocates its payloads in reusable batches rather than individually.
No Boost dependency or source code is introduced.
