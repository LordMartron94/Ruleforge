# Logos

Logos is a shared C library for language tooling: defining grammars and
recognizing source through lexing and parsing. Its API currently provides the
Echo-backed Runtime, Regula regular byte-language expressions, and Contexta
context-free byte-language grammars.

## System Design

The library currently owns expression construction and regexp string I/O.
Language tooling provides a context-aware streaming lexer, compiled to one
minimized DFA per context, and a generalized streaming Contexta parser with
lossless packed green syntax and allocation-free red occurrences. Their
execution contracts are documented in `logos.h`.

### 1. System/Environment Distinction

Logos owns language recognition and its internal representations. Callers
provide source data, language specifications, and the application policy
for consuming results. Nexus supplies foundational facilities; Echo supplies
logging. Editors, source-file management, application semantics, and program
execution remain in the environment.

### 2. Operational Code (Binary Distinction)

Recognized/unrecognized under the supplied language specification. This
classifies source structure; recognizing syntax does not establish its
application meaning or correctness.

### 3. System Programs (Rules and Routines)

A Logos Runtime borrows its
caller's Echo runtime and owns its registered Echo system and context.
Construction returns the owned Runtime pointer and panics if its required
Echo setup fails, following the workspace Runtime-construction policy.
Destroy Logos before its borrowed Echo runtime is closed or destroyed, and
release all borrowed context users first. Regula owns slabbed expression
nodes, constructs compounds from the regular-language primitives, and imports
and exports byte-oriented regexp strings.

### 4. Internal Differentiation

The Runtime subsystem owns the logging attachment. Regula owns regular-language
expressions and their automatic assembly. Contexta owns immutable expression
graphs, declared nonterminals, recursive productions, and explicit start-rule
selection. Its rule equations denote their least languages; choice is unordered.
Language tooling owns lexing (the lexer, its processes and the source-location
index), parsing (compiled grammar, streaming chart processes, and the Pratt
engine that runs operator-precedence operations of Contexta beside the
chart), and syntax (owned source, immutable green forest, red occurrences and
diagnostics).
The parser borrows a finalized lexer and copies Contexta's reachable grammar,
labels and token scopes. Clients may release Contexta after compilation.

### 5. Autopoietic Execution (Internal Communication)

The opaque Runtime stores its borrowed Echo runtime and its owned Echo system
and context. Regula distinguishes an implicit concatenation assembly tree
from completed expression interiors. Unary transformations replace an active
assembly target; binary operations consume adjacent targets without losing
surrounding content. Parent links are maintained only for assembly edges.
Stable slab addresses permit shared immutable interiors. Import stages all
nodes before committing; export detects cycles with an iterative path stack.

### 6. Structural Coupling (API and Interfaces)

Consumers include `logos.h` and link `Logos::Logos` through CMake. Public
functions use `logos_`; source files use `l_`. Runtime getters return borrowed
Echo handles. Runtime lifecycle requires exclusive access, and destruction
must not race with users of those handles. Regula callers synchronize language
access, borrow labels, and receive language-owned expression handles. Typed
add calls return the newly created operation and assemble internally. Regexp
strings are borrowed on import and Nexus-allocated on export. See README.md
for the exact byte dialect and resource limits, and the proposed draft
[0001-regula-implicit-composition-and-regexp-strings](adr/0001-regula-implicit-composition-and-regexp-strings.md)
for the composition contract.
