# 0010. Low-level Internet networking

## Status

Proposed draft for Mr. Hoorn to finalize. Mr. Hoorn selected IPv4/IPv6,
TCP/UDP, address resolution, blocking/nonblocking I/O, readiness waits,
and socket options with a Linux backend, without an event loop or
higher-level protocols.

## Context and Problem Statement

Nexus has platform primitives for files, processes, and threads, but no
network boundary. Consumers would otherwise import native socket layouts,
constants, error conventions, and descriptor lifecycle rules themselves.
A thin foundation must leave connection policy, framing, retry budgets,
and scheduling with its caller while making native edge cases explicit.

## Decision Drivers

- One owned public interface, with native networking headers confined to
  `src/n_network.c`.
- Client-controlled blocking mode, transport, endpoint, socket options,
  backlog, shutdown direction, and readiness interests.
- No allocation or buffering in successful send, receive, or wait calls.
- Preserve short stream transfers, EOF, empty datagrams, truncation,
  pending connections, and would-block as distinct observable outcomes.
- Validate one real backend rather than claim untested portability.

## Considered Options

1. Expose native descriptors and socket structures directly.
2. Wrap Internet sockets and readiness behind Nexus-owned types and
   leave orchestration to clients (chosen).

## Decision Outcome

`NexusNetworkAddress` is a caller-owned value: a concrete family, 16 address
bytes in network order, and host-order port and IPv6 scope index. Parsing
and formatting concern numeric hosts only: no endpoint-string grammar,
service names, brackets, or interface-name lookup. OS hostname resolution
is explicit and synchronous. It writes into caller-provided storage only
when every result fits, otherwise reports the required count. libc may
allocate during resolution; no resolver work is hidden inside connect.

`NexusNetworkSocket` is opaque and uniquely owned. Creation and acceptance
use atomic close-on-exec flags; accepted blocking mode is explicit, rather
than inherited or guessed. IPv6-only is enabled at creation and can be
changed before binding through the option API. Close consumes and nulls
the handle on every return. Linux close is never retried after EINTR,
because the descriptor number can already have been reused.

A socket operation returns a Nexus error for an actual failure and a
separate `NexusNetworkOutcome` for ordinary progress. Would-block does not
allocate an error. TCP transfers expose their actual byte count; callers
own remaining-buffer handling. Receive requires positive capacity so a
zero-byte TCP result unambiguously means EOF. UDP receives use MSG_TRUNC:
size reports the full datagram length, while only min(size, capacity)
bytes were copied. A truncated datagram has been consumed. Empty UDP
messages remain successful messages, including through connected I/O.

Nonblocking connect exposes IN_PROGRESS. Its completion operation first
polls without blocking, then reads SO_ERROR once readiness is observed.
Reading SO_ERROR alone before readiness could mistake a still-pending
connection for a completed one. EINTR during connect is also pending;
blindly repeating connect would not be equivalent to retrying an
interrupted read. A failed TCP connection must be closed. Sends use
MSG_NOSIGNAL locally; Nexus never changes the process's SIGPIPE policy.

`NexusNetworkWaitSet` owns reusable pollfd storage, allocated at creation.
Each call supplies borrowed entries and a timeout in milliseconds. There
are no retained registrations or callbacks. Positive waits retain a
monotonic deadline across EINTR. Ready events include error, hangup, and
invalid-descriptor indications independently of requested interests.
Readiness does not reserve data: clients should perform nonblocking I/O.
This O(n) primitive is suitable for small sets; a high-scale reactor is a
separate layer and is not represented as a scheduler here.

Real syscall failures use the existing OS errno facility. Resolver failures
that are not errno distinguish temporary failure, missing names, and other
resolution failure. Numeric parse and caller-buffer failures have explicit
synthetic reasons. Networking retains Nexus's current shared diagnostics
and memory contracts: clients synchronize them; these wrappers do not
claim library-wide thread safety that the underlying error pool and debug
allocator do not provide.

Linux is implemented. Other platform branches remain linkable and call
NEXUS_PLATFORM_UNSUPPORTED_RAISE at use. No Windows networking backend,
Unix-domain sockets, TLS, HTTP, multicast management, hidden worker,
background resolver, connection racing, or event loop is introduced.

### Pros and Cons of the Options

Native exposure is immediately flexible and permits arbitrary OS-specific
operations. It would also spread the platform boundary into every client,
make ownership easy to bypass, and tie the public surface to native types.

The owned interface keeps native constants and resource invariants in one
translation unit and makes progress outcomes uniform. Its costs are one
allocation per socket, setup allocations for readiness storage, explicit
wrapping work for future platform capabilities, and an O(n) poll scan.
Those allocations occur at resource setup rather than in I/O loops.

## Consequences

Clients can build servers, synchronous tools, or nonblocking protocols
without importing networking headers. Connection policy, framing, all-data
loops, cancellation, and retry deadlines remain explicit client decisions.
The header describes failure outputs and ownership at every entry point.

Porting requires implementing and testing a real second backend. The first
backend does not provide a persistent epoll interest set; many thousands
of sockets will warrant a separate measured design. Resolver calls may
block for OS-configured DNS timeouts. Debug allocations and failures still
share Nexus's existing synchronization constraints.

Focused loopback tests cover IPv4 and IPv6 TCP/UDP, blocking and
nonblocking connection paths, pending/refused connections, partial reads,
backpressure, readiness, half-close, SIGPIPE suppression, empty/truncated
messages, option round trips, and interrupted timed waits. They use local
resolver data and numeric addresses; no public Internet service is needed.

## Sources

- [Linux connect(2)](https://man7.org/linux/man-pages/man2/connect.2.html):
  pending completion, SO_ERROR, interruption, and failed-socket lifetime.
- [Linux recv(2)](https://man7.org/linux/man-pages/man2/recvmsg.2.html):
  stream EOF, empty datagrams, short reads, and MSG_TRUNC.
- [Linux poll(2)](https://man7.org/linux/man-pages/man2/poll.2.html):
  readiness, unconditional error/hangup events, and interrupted waits.
- [Linux getaddrinfo(3)](https://man7.org/linux/man-pages/man3/getaddrinfo.3.html):
  synchronous resolution, passive addresses, and resolver error domain.

- [Linux accept(2)](https://man7.org/linux/man-pages/man2/accept.2.html):
  atomic close-on-exec and explicit accepted-socket blocking flags.
- [Linux close(2)](https://man7.org/linux/man-pages/man2/close.2.html):
  descriptor reuse and why Linux close must not be retried after an error.
- [Linux send(2)](https://man7.org/linux/man-pages/man2/send.2.html):
  partial stream transfers and per-call SIGPIPE suppression.

## Related Decisions

[0001-nexus-error-representation](./0001-nexus-error-representation.md)
provides the shared error pool and source attribution.
[0004-process-spawn-communicate](./0004-process-spawn-communicate.md)
establishes the Linux-specific primitive boundary and real-errno reporting.
