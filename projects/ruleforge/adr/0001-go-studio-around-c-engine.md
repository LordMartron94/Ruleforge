# 0001: A Go studio around the C engine

**Status:** Proposed. **Tiers:** Full.

**Context.** Ruleforge needs a web editor and inspector, data providers that reach the network, a scheduler and a self-updater, on
Linux and Windows, while the language engine stays in Dependable C on Nexus and Logos (which only has Linux backends today).

**Decision.** The engine (`ruleforge`) is a C program that does all parsing, checking, evaluation, lowering and simulation and
speaks JSON. The studio (`ruleforge-studio`) is a Go program that starts the engine, serves an embedded web app, runs providers
and the scheduler, and updates both programs. The studio never interprets a script.

**Considered.** (a) The engine compiled to WebAssembly in the browser: Nexus and Echo are not portable to it. (b) The engine
as a shared library loaded by the studio: needs an export surface and a data-ownership ABI on Windows. (c) Doing the network and
scheduling in C: needs TLS and an HTTP client Nexus does not have.

**Consequences.** One semantic model: the UI cannot disagree with the compiler. Every UI question costs a process start
(about 0.2 s), which the editor hides behind debounce. Data acquisition happens before evaluation, as the design requires.
