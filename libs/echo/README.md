# Echo

Some of the implementation in this library is LLM-generated. Echo is reused
as a foundational logging core rather than rewritten from scratch.

Echo is the MergeSource asynchronous logging library. Producers copy a
bounded record into a Nexus channel; one worker performs filtering, sink
presentation, and terminal/file I/O. Runtime and sink registries grow
geometrically, so registering a sink does not depend on a compile-time sink
count.

The public API is in `echo.h`. Function-styled macros such as `echo_info` and
`echo_error` capture the call site and expand to the explicit provenance
function. Formatting happens on the producer thread; presentation and I/O
happen on Echo's worker thread.

The normal lifecycle is:

1. create a runtime with `echo_runtime_create`;
2. register systems, contexts, and sinks;
3. call `echo_runtime_start`;
4. log through `echo_trace`, `echo_debug`, `echo_info`, `echo_notice`,
   `echo_warning`, `echo_error`, or `echo_critical`;
5. flush or close, then destroy the runtime.

The default queue policy blocks when full. `EQSP_DROP_NEWEST` is available for
applications that explicitly accept lossy ordinary logging; error and critical
records remain blocking.

## CMake build

Restored from archived snapshot `29d1610f7960ec1995554988ffff61a59d203174`.
The active build uses CMake; `echo.helm` is retained as archived material.
Consumers link `Echo::Echo`, which propagates Nexus and the archived workspace's
64-character system-label capacity consistently to every ABI consumer.

From the MergeSource root:

```sh
make configure
make TARGET=echo_tests JOBS=4
ctest --test-dir build/Debug --output-on-failure -R '^echo_tests$'
```
