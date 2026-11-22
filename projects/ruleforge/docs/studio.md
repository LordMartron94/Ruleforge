# Ruleforge Studio

`ruleforge-studio serve --project DIR` serves the editor at `http://127.0.0.1:8750` (loopback only; requests with another Host or
Origin are refused). It needs the `ruleforge` engine next to it, on `PATH`, or via `--engine`.

- **Editor**: highlighting from the engine's lexer (`ruleforge tokens`), diagnostics underlined where the engine reports them.
- **Filter**: the generated filter; click a line to inspect its block.
- **Inspector**: every block, searchable, with a preview of the item label drawn from its style, its conditions, the declaration
  it came from, the include/call/template/dataset-row trace (each entry opens the source), and what it shadows.
- **Simulator**: paste an item (or an array), see which block wins, why every earlier block did not match and which later
  blocks it shadows. Items can be saved in the browser.
- **Themes**: every theme and its colors; picking a color rewrites that exact source span (and a palette color when the theme
  names one), then recompiles.
- **Changes**: blocks added, removed or restyled by the last edit.

`studio.json` in the project folder describes entry, outputs (several filters from one project, each with its own settings such
as `strictness`), datasets and their providers (`csv`, `json`, `poe-ninja-stash`, `poe-ninja-exchange`), the manual character
level and the scheduler. `ruleforge-studio build` refreshes the datasets that are due, compiles every output and publishes each
atomically; with `"regeneration": {"enabled": true}` `serve` does it on a timer and skips the compile when no input changed. A
failed refresh keeps the previous snapshot, a failed compile keeps the previously published filter, and
`.ruleforge/manifest.json` records what happened.
