# Echo Design

## System Design

In here I will explain the blueprint of the system using Luhmann's
functionally differentiated systems theory. Note that this section is not
meant to be eternalized in stone, but can be iteratively improved during
development.

### 1. System/Environment Distinction

Echo owns the transformation of logging attempts into bounded, independently
owned records and their delivery to enabled sinks. Its internal operations are
hierarchical system policy, contexts, spans, provenance, structured fields,
runtime and sink filtering, thread-safe capture, bounded asynchronous
transport, sink dispatch, presentation, terminal and file output, rotation,
flush and shutdown, and Nexus diagnostic integration.

The environment owns the semantic meaning of messages, systems, spans, and
fields. Callers own application state outside values Echo explicitly copies or
retains, and custom sink state according to the ownership policy selected at
registration.

Echo does not own general task scheduling, metrics, distributed tracing,
remote log storage, crash recovery, or arbitrary asynchronous I/O. Multiplex
is not a core transport dependency: its coroutines share an OS thread and
cannot move presentation or I/O work off that thread. A future adapter may let
Multiplex coroutines submit records without changing Echo's public model.

### 2. Operational Code (Binary Distinction)

Echo observes logging attempts through the binary distinction
`echoed/silenced`.

An attempt is echoed when it passes producer-side policy and is admitted into
the transport. It is silenced when it is filtered, suppressed by recursion
protection, rejected during shutdown, or dropped by the configured saturation
policy. Sink-specific filtering occurs after admission, so an echoed record
may still be ignored by every enabled sink. Echo counts every silencing reason
so lossy operation remains observable.

### 3. System Programs (Rules and Routines)

A producer resolves the effective system level, rejects disabled attempts,
captures real and monotonic timestamps plus provenance and context, formats
the payload into bounded record storage, copies spans and structured fields,
and submits the completed record to the transport.

The worker receives a record, drains an available batch, runs runtime and sink
filters, performs date/time conversion and sink-specific presentation, writes
to terminal, file, or custom sinks, and processes flush and shutdown barriers
in queue order.

The following invariants guide those operations:

- Ordinary logging attempts perform no heap allocation.
- Records never borrow transient producer memory.
- Successfully admitted records preserve producer order within one runtime.
- Presentation and sink I/O never hold registry or configuration locks.
- Configuration is immutable after initialization except for controls
  explicitly designed for concurrent mutation.
- Sink and system registries grow geometrically through Nexus allocation; there
  is no compile-time maximum number of registered sinks or systems.
- Destruction stops admission, drains accepted records, flushes sinks, joins
  the worker, and only then releases storage.
- Flush completes sink processing for every record admitted before its
  barrier. It does not promise durable storage unless a sink explicitly and
  successfully performs `fsync`.
- Public operations are thread-safe. Context mutation is serialized, although
  copying a context per logical task is the preferred model.

The bounded queue blocks producers by default. An optional drop-newest policy
may silence ordinary records at capacity, while error and critical records
still use blocking admission. Oversized messages and field values are
deterministically truncated and counted.

### 4. Internal Differentiation

Echo differentiates capture and record construction, hierarchical system
policy, context/span/field management, bounded transport, lifecycle and
synchronization, filtering and dispatch, terminal presentation, file
presentation and rotation, Nexus diagnostic adapters, and statistics and
emergency diagnostics.

These subsystems remain cohesive through the bounded record contract and the
runtime lifecycle. Terminal and file concerns never enter producer-side
capture, and sink implementations never control transport ownership.

### 5. Autopoietic Execution (Internal Communication)

Echo communicates internally through fixed-capacity records copied into a
bounded Nexus channel. Records are either log records or control records such
as flush and shutdown barriers. The channel establishes processing order, and
control records carry acknowledgements so callers can wait for a precise point
in that order.

The worker consumes records in batches to amortize wakeups and synchronization.
Sink failures use a non-recursive emergency diagnostic path and are never
logged back through the same runtime.

Nexus channel copies are themselves memory-trace events: the channel's own
internal copy of the record being sent can fire the installed Nexus
memory-tracer callback, which logs through Echo again. That second log call
must not submit a second record while the first channel send still owns the
channel mutex, or the sending thread deadlocks on itself. Echo enforces this
with a thread-local "currently sending on this runtime" flag, checked at the
top of every log call alongside the worker-recursion check below, and set only
for the duration of the channel send. The memory tracer's own thread-local
recursion flag is a separate, narrower guard on top of that: it stops the
trace callback from re-entering itself once already active, so tracing a
channel copy never regresses into tracing its own trace.

Logging recursively from a filter or sink into its own runtime is invalid: a
blocking queue could deadlock its only consumer. Echo detects this with
thread-local worker state, silences the recursive submission, increments a
counter, and sends error or critical diagnostics directly to the emergency
destination.

### 6. Structural Coupling (API and Interfaces)

Echo depends on Nexus for threads, synchronization, atomics, bounded channels,
time, filesystem facilities, error representation, allocation, and assertion
and memory-diagnostic integration.

The public surface exposes opaque Echo handles rather than Nexus
synchronization objects or transport records. Public functions and
function-styled logging macros use the `echo_*` prefix; internal symbols use
the `e_*` prefix. Uppercase names are reserved for constants and compile-time
configuration. The library is shared and its public repository is mirrored as
Echo-C.

Variadic logging macros inject source provenance directly into an internal
variadic function under the accepted C89 exception in the workspace's ADR
0003; they do not use the old thread-local metadata handoff.

Custom sinks run on Echo's worker thread. Their state ownership and destruction
callback are declared at registration. They must not re-enter the same runtime
or assume they execute on the submitting thread.
