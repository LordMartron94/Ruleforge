# Pharos

Pharos keeps an editor's picture of a body of text in sync with the files on disk and answers structural questions about it:
a language server framework and the rendering of the problems found.

## System Design
In here I will explain the blueprint of the system using Luhmann's functionally differentiated systems theory.
Note that this section is not meant to be eternalized in stone, but can be iteratively improved during development.

### 1. System/Environment Distinction
Internal: JSON-RPC framing over a byte stream, the document store (open buffers and files found on disk, change detection),
position mapping between byte offsets and UTF-16 columns, the occurrence index (names with kind, owner and scope, declaration
or use) and the queries built on it (definition, references, highlights, rename, symbols), and the diagnostic model with its
rendering.

Environment: what a name means (a provider analyses one document and reports its names and problems), the language, the
editor, the peer of the transport. Pharos does not know grammars or semantics and draws no UI.

### 2. Operational Code (Binary Distinction)
anchored / unanchored. A name occurrence is anchored when it resolves to a declaration (owner, kind, scope, name); everything
Pharos answers is the set of occurrences anchored to the same declaration. An unanchored occurrence is kept but answers nothing.

### 3. System Programs (Rules and Routines)
A document is analysed again when its text changes; a file found on disk is analysed when first needed and again when its
stamp changes. Positions are exact (UTF-16 columns over UTF-8 text). Diagnostics are syntax only; semantics belong to a later
analyzer. A request never waits for analysis it does not need.

### 4. Internal Differentiation
Transport (framing, dispatch), Documents (store, workspace, positions), Names (occurrences, queries), Diagnostics (model,
rendering).

### 5. Autopoietic Execution (Internal Communication)
Documents, names and problems per document; the occurrence table of a document is rebuilt whenever it is analysed.

### 6. Structural Coupling (API and Interfaces)
A provider interface of callbacks (the kind of a file by its path, the analysis of a document into names and problems, the text
of a hover) and the Echo runtime for logging. Logos implements the providers for Genesis, mapping files and any grammar.
