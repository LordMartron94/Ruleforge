# Ruleforge

A programmable loot-filter engine for Path of Exile 1. You write `.rf` scripts (modules, functions, templates, datasets, themes);
the engine runs them at compile time and emits item filters, plus a structured model that explains every generated block: which
declaration produced it, through which includes and calls, from which dataset row.

- **Engine** (`ruleforge`, C): parses, checks, evaluates, lowers and simulates. One authority on what a script means.
- **Studio** (`ruleforge-studio`, Go): a web editor and inspector, a scheduler that refreshes data and republishes filters,
  and the self-updater. It never interprets a script itself; it calls the engine.
- Both run on **Linux and Windows**, built from this repository by CI.

```
design:     DESIGN.md             language and architecture
semantics:  docs/language.md      how the open points of the design were decided
formats:    docs/ast.md           the syntax-tree JSON the parser emits
            docs/model.md         the compile model (blocks, conditions, styles, traces) and the simulation result
studio:     docs/studio.md        the web editor, inspector, scheduler, updates
```

## Try it

```sh
cd examples/currency
ruleforge compile entry.rf --settings settings.toml -o currency.filter --model model.json
ruleforge simulate entry.rf --settings settings.toml --items items.json
ruleforge-studio serve --project .          # open http://127.0.0.1:8750
```

`ruleforge ast entry.rf` writes the syntax trees as JSON: any tool in any language can read them without parsing `.rf` again.
`ruleforge tokens` and `ruleforge manifest` feed editors (highlighting; completion from what the engine really accepts).

## Build

Needs a C compiler (GCC or Clang; on Windows MinGW-w64 via MSYS2), CMake 3.20+, Ninja, and Go 1.22+ for the studio.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ruleforge_cli
ctest --test-dir build -R ruleforge
(cd projects/ruleforge/studio && go test ./... && go build ./cmd/ruleforge-studio)
```

The syntax bindings are not written by hand: `genesis bind` generates typed views of every parse tree from the grammars
(`spec/ruleforge.gen`, and the CSV, JSON and TOML grammars of the Logos library) while the build runs.
JSON, CSV, TOML and copied PoE item text are therefore read through Genesis grammars and generated bindings.

## Studio additions

Theme and palette forms edit current source with conflict checks. The Simulator parses English copied item text and generates
repeatable catalogue samples. Generic colour calculations live in Blaze and are exposed through `std.color`. Managed PoB data,
local path overrides, progression/class-use modules, persistent rotated logs and the compatible economy history service are described
in [the Studio guide](docs/studio.md). Script form editors remain a later TODO.

## Releases and updates

Pushing a tag `vX.Y.Z` to this repository builds and tests Linux and Windows, publishes `ruleforge-<version>-<os>-<arch>.zip` and
`SHA256SUMS` as a GitHub release. `ruleforge-studio` checks the latest release, and `ruleforge-studio update --apply` (or the
"Update" button) downloads the archive for its platform, verifies the checksum and replaces the programs. Stable releases also publish
the compatible `ruleforge-economy:latest` Docker image; [existing-container migration](deployment/economy/README.md) preserves the history volume.

## About this repository

This is the public mirror of the Ruleforge part of a private monorepo. It is regenerated from there (`UPSTREAM.md` names the
commits it was made from), so it carries the libraries it builds on (`libs/nexus`, `echo`, `pharos`, `logos`) as a snapshot.
Do not edit it by hand.
