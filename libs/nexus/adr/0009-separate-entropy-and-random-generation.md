# 0009. Separate entropy and random generation

## Status

Proposed draft for Mr. Hoorn to finalize. Mr. Hoorn requested the separation
and selected migration without compatibility aliases.

## Context and Problem Statement

The entropy subsystem combines OS acquisition, deterministic generators, and
injected byte sources. Deterministic output does not acquire entropy. Nexus
needs general random primitives without depending on Blaze's numerical layer.
Persistent replay also requires a specified sequence rather than an algorithm
that may silently change behind an unchanged name.

## Decision Drivers

- Assign responsibilities independently of computational performance.
- Keep deterministic state and seeding explicit, with no hidden global PRNG.
- Preserve allocation-free generation and existing deterministic byte streams.
- Support secure OS randomness and injectable deterministic sources distinctly.
- Supply unbiased bounded integers and uniform values in [0, 1).

## Considered Options

1. Retain generators in entropy and add convenience sampling there.
2. Separate OS entropy acquisition from random sources and generators in Nexus.

## Decision Outcome

Choose the second option. `n_entropy.c` owns `nexus_entropy_system_fill`.
`n_random.c` owns `NexusRandomSource`, `NexusRandomGenerator`, secure system
random bytes, deterministic algorithms, and integer/uniform-real sampling.
Identity injection uses random-source vocabulary, including deterministic
test sources. HashMap seeds use secure random bytes. Remove old generator
and source names, migrate callers, and use a separate random error facility.

The secure system source delegates to the OS facility through entropy;
it never substitutes a deterministic fallback. Explicitly seeded ChaCha20
remains a deterministic stream operation: its security requires a secret
unpredictable key and no reuse of key/nonce/counter positions. It does not
promise automatic reseeding or forward secrecy. Counter exhaustion fails
before writing any bytes or advancing state for that fill request.

Explicit OS-backed seed helpers draw uint32, uint64, uint128, or an arbitrary
number of seed bytes. They never initialize a generator and never filter
zero or other bit patterns. Typed outputs change only on success; arbitrary
byte buffers follow the system source's discard-on-error contract. Callers
record the returned values when future replay is required.

`NEXUS_RANDOM_SEQUENCE_VERSION` identifies version 1 of the sequence
contract. Named algorithms retain their specified byte streams across library
versions and architectures. Integer draws decode successive little-endian
bytes; mixed draw sizes and split fills share that same stream. State object
memory is private working storage, not a serialization format. Future sequence
changes require a new algorithm identifier or an explicitly versioned API.

LCG64 uses multiplier 6364136223846793005 and increment
1442695040888963407, emitting the updated state. SplitMix64 uses the reference
64-bit Weyl increment and mixing constants. xoroshiro128+ retains the original
(55, 14, 36) transition; the all-zero seed maps to the two constants documented
in the implementation. PCG32 uses XSH-RR, reference two-step initialization,
and the low 63 bits of the sequence selector. Successive PCG outputs are
packed low word first into the byte stream. ChaCha20 follows RFC 8439 with
a 96-bit nonce and 32-bit block counter; all words serialize little-endian.
Tape replays once; counter emits the current value and adds step modulo 2^64.

Bounded draws reject values below 2^width modulo bound, then reduce modulo
the positive exclusive bound. Uniform real32/real64 draws use the upper
24/53 bits of a 32/64-bit draw, scaled by 2^-24/2^-53. These are uniform
fixed grids, not uniform over all representable floating-point numbers.
Sampling preserves a source's quality; it cannot repair a weak generator.
Rejection can consume multiple draws, and a finite source may exhaust after
rejected draws. Scalars are assigned only on success. Arbitrary failing
sources may have partially filled byte buffers, which callers must discard.

Statistical distributions, SIMD generation, and parallel stream scheduling
belong in Blaze. Nexus retains PCG's explicit stream selector as part of its
ordinary deterministic seed contract; it supplies no parallel execution API.

### Pros and Cons of the Options

Keeping everything in entropy minimizes migration, but mislabels reproducible
generation and leaves secure acquisition mixed with synthetic test output.
Separating the systems makes that distinction visible and gives Blaze a small
foundation. It breaks source compatibility and requires replay maintenance.

## Consequences

Existing entropy-generator/source types, function names, error constants, and
identity `with_entropy` entry points migrate to random vocabulary. The entropy
fill function no longer accepts a dummy callback-state argument. There are no
deprecated aliases. Existing little-endian deterministic streams are retained;
ChaCha's previous host-endian output is corrected on big-endian systems and
its previously repeating exhausted stream now returns an error.

Source capabilities do not imply security. Only the system source guarantees
OS-backed secure generation. Clients synchronize shared mutable generators;
distinct generator instances can be used independently. Golden vectors,
chunking invariance, rejection boundaries, float endpoints, exhaustion, and
identity injection are covered by deterministic tests. OS tests establish
successful acquisition only, not statistical or cryptographic proof.

## References

- [PCG minimal C API and bounded sampling](https://www.pcg-random.org/using-pcg-c-basic.html)
- [RFC 8439, ChaCha20 definition and vectors](https://www.rfc-editor.org/rfc/rfc8439.html)
- [Blackman and Vigna's generator references](https://prng.di.unimi.it/)
