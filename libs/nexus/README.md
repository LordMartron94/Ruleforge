# Nexus

Some of the implementation in this library is LLM-generated. Nexus is reused
as an extensive foundational core rather than a domain project or mastery goal.

Restored from the archived Nexus snapshot
`705f971421e9c2d31996f4446cd533ec73874a7a`, including the original source,
tests, ADRs, allocation-hijack header, and reference Helm configuration.
The active build uses CMake; `nexus.helm` is retained as archived material.

The public interface is `nexus.h`. The implementation covers platform and
primitive types, errors and assertions, memory, containers, strings, files,
processes, signals, threads, time, entropy, random generation, hashing, identity,
and Internet networking.

Entropy acquisition lives in `nexus_entropy_system_fill`. General random
generation lives in `nexus_random_*`: the system source supplies secure OS
bytes, while caller-owned generators use explicit seeds and never acquire
entropy automatically. There is no implicit global PRNG state. Statistical
distributions, vectorized generation, and parallel stream scheduling belong
in Blaze.

Use `nexus_random_system_source_get` for secure random sampling, or initialize
a `NexusRandomGenerator` for reproducible output. Source and generator APIs
provide byte fills, uint32/uint64 draws, unbiased bounded integers in
`[0, upper_bound)`, and real32/real64 values on uniform 24/53-bit grids in
`[0, 1)`. Bounds must be positive. Sampling inherits the source's quality;
tapes and counters are diagnostic fixtures. Scalar outputs change only on
success; byte buffers must be discarded after a source failure. Rejection
sampling may consume rejected draws before a finite source exhausts.

For explicit lower and upper bounds, use
`nexus_random_source_uint32_range_get` / `uint64_range_get`, or their
`nexus_random_generator_*` equivalents. Signed `int32_range_get` and
`int64_range_get` variants support negative bounds and intervals crossing
zero, including spans wider than the signed type's positive range, without
signed overflow or out-of-range signed casts. These draw unbiased integers in
`[minimum, maximum)`: minimum is included, maximum is excluded, and minimum
must be less than maximum. They retain the max-only APIs' rejection and
failure behavior, including leaving output unchanged on failure.

`NEXUS_RANDOM_SEQUENCE_VERSION` pins the byte-stream and scalar conventions
across library releases. Named algorithms retain their specified sequences;
generator object memory is not a serialization format. ChaCha20 follows
RFC 8439 and reports exhaustion before its block counter could repeat. A
secret, unpredictable key and unique stream positions remain the caller's
responsibility; use the system source for ordinary secure randomness.

To draw a fresh seed without constructing a source, use
`nexus_random_seed_uint32_get`, `nexus_random_seed_uint64_get`, or
`nexus_random_seed_uint128_get`. Use `nexus_random_seed_fill` for arbitrary
byte counts, such as a 32-byte ChaCha20 key. These helpers always use the
secure OS source, accept every bit pattern including zero, and leave
generator initialization explicit. Record the resulting seed for replay;
typed outputs change only on success. Discard a byte seed buffer on error.

The entropy/random split removes the old entropy generator/source API:
`NexusEntropyGenerator`, `NexusEntropySource`, `NexusEntropyAlgorithm`,
`NEA_*`, and `nexus_entropy_generator_*` / `nexus_entropy_source_*` become
their `Random`, `NRA_*`, and `nexus_random_*` equivalents. The system-source
getter moves to random; the entropy fill drops its dummy state argument.
Generator errors use `NEXUS_RANDOM_ERROR_*`. UUID injection uses
`nexus_identity_uuid_v4_generate_with_random` and
`nexus_identity_uuid_v7_generate_with_random`. Compatibility aliases are
deliberately absent. See the draft
[ADR 0009](adr/0009-separate-entropy-and-random-generation.md) for the
responsibility boundary and exact sequence contract.

Networking lives in `nexus_network_*`: numeric IPv4/IPv6 endpoints, explicit
OS hostname resolution, TCP/UDP socket ownership, bind/listen/accept/connect,
partial I/O, half-close, socket options, and reusable readiness waits. The
backend currently supports Linux; other platforms raise on use. It provides
no event loop, TLS, HTTP, or protocol framing. Socket handles and wait sets
must be released explicitly. See the draft
[ADR 0010](adr/0010-low-level-internet-networking.md).

For a TCP server, parse a local address, create a socket, set options before
binding, bind, and listen. Query the local address after binding port zero
to discover the assigned port. Accept returns a separately owned socket.
A client resolves or parses its destination, creates a matching socket,
and connects. When connect returns `NNO_IN_PROGRESS`, wait for readiness
and call `nexus_network_socket_connect_finish` to inspect the final result.
Close a failed TCP connection rather than reusing it.

`NNO_WOULD_BLOCK` and pending connections are ordinary outcomes, separate
from `NexusError`. Keep the untransferred suffix after a short TCP send.
TCP receive distinguishes `NNO_END_OF_STREAM`; an empty UDP datagram is
`NNO_COMPLETE`. UDP truncation is `NNO_TRUNCATED`, and the reported size is
the full message length, potentially larger than the buffer. Only the
buffer's capacity was written, and the discarded suffix cannot be read
later. `nexus_network_wait` reuses a fixed-capacity wait set, reports
readiness/error/hangup flags, and preserves its timeout across signals.
Nonblocking I/O remains necessary after readiness because readiness is a
hint. Successful I/O and waits allocate no Nexus memory. Resource setup,
resolution, and diagnostics retain Nexus's documented allocation and
synchronization contracts.

From the MergeSource root:

```sh
make configure
cmake --build build/Debug --parallel 4 --target nexus_tests nexus_network_tests
ctest --test-dir build/Debug --output-on-failure -R '^nexus(_network)?_tests$'
```

Consumers link the CMake target `Nexus::Nexus`. Debug builds enable developer
assertions, memory guards, and allocation tracing consistently in the library
and its consumers; other configurations disable those development facilities.
