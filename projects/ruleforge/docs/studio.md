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

## Persistent logs and diagnostics

Studio starts as a live HTTP application and serves its editor, themes and copied-item parser without a previous compilation.
Compile and simulation requests run the current source through the engine. Script form editors remain deferred in `TODO.md`.

`studio.json` accepts:

```json
"logging": { "directory": ".ruleforge/logs", "keep": 10, "maxBytes": 4194304 }
```

The host writes `studio.log` and numbered rotations, with `keep` including the active file. Engine Echo logs use timestamped
`ruleforge-` files in the same directory. Engine retention counts files; host `maxBytes` bounds each host log. Direct engine runs use
`--log-dir DIR --log-keep N` (the defaults are `.ruleforge/logs`, 10). Logging failures are errors. Engine diagnostics are deduplicated
by severity, location, code, message and note. Regeneration aggregates identical diagnostics across all project outputs and logs each
once; the regeneration status and `.ruleforge/manifest.json` retain that shared diagnostic list.

## Themes and colour checks

Themes and colour sets have form controls for colour, opacity and property expressions. Palette references stay linked: changing a
linked colour edits its palette definition. Edits use engine-reported byte spans and a complete-file hash; stale forms refuse to
overwrite concurrent changes. Save open source buffers before using form controls.

When a theme state explicitly supplies both text and background colours, Studio shows WCAG ratio, signed APCA Lc and CIEDE2000.
Translucent colours are composited over a black scene backdrop; the backdrop assumption is shown beside the values. Missing or
inherited colours that are not explicit in that state are not assigned invented defaults. These numbers describe the chosen pair:
APCA is polarity-sensitive, and DeltaE measures colour separation rather than readability. They are not a font-size conformance verdict.

All computations belong to Blaze's generic `color` API. The engine's `std.color` module is a documented binding to it, so scripts,
Studio and other C consumers use the same implementation. Blaze tests the full 34-pair
[Sharma/Wu/Dalal CIEDE2000 dataset](https://hajim.rochester.edu/ece/sites/gsharma/ciede2000/dataNprograms/ciede2000testdata.txt).
Formula references are [WCAG 2.2](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html) and
[APCA 0.0.98G-4g](https://github.com/Myndex/apca-w3/blob/master/src/apca-w3.js).

## PoB data, progression and loot previews

Enable a managed PoB cache with `"gameData": {}` in `studio.json`, then run `ruleforge-studio build` or **Regenerate**. A complete
revision is downloaded into `.ruleforge/pob`; the cache remains usable offline. Set `gameData.commit` to a full 40-character SHA to
pin it. Alternatively set `gameData.pobPath` to your own PoB `src/Data` directory; it is read only and never updated by Studio.

```json
"gameData": {
  "pobPath": "/path/to/PathOfBuilding/src/Data",
  "dropLevelsPath": "data/drop-levels.json"
}
```

Drop levels can be a JSON object such as `{"Driftwood Wand": 3}`, or set `fetchDropLevels: true` to fetch paced, batched PoE wiki
queries. Unknown drop levels remain null. The default does not query the wiki. Complete normalized generations are published through
an atomic manifest under `.ruleforge/game`; failed refreshes retain the prior generation. Lua data runs without filesystem, process,
network, package or debug libraries, with a per-file deadline. Ordinary bases, gems and named standard uniques are supported;
procedurally generated uniques are excluded. The snapshots are `game.bases`, `game.gems` and `game.uniques`.

Import `std.items` to use its `Bases`, `Gems` and `Uniques` datasets. `std.build.ForClasses` selects the classes a build uses;
`std.progression.Available` selects an inclusive drop-level window; `Upgrades` keeps the highest available level per class. These
policies are ordinary `.rf` functions. The host validates exact BaseType conditions against the configured catalogue, warns once per
unknown name, and deduplicates those warnings across output filters. A warning may mean the catalogue is older than the game.

The Simulator accepts English text copied with Ctrl+C in PoE. `ruleforge item item.txt` exposes the same Genesis-based parser on the
command line. Names, class, rarity, item/gem level, quality, stack size, sockets and common status flags are interpreted. Unknown lines
are retained as `_unparsed`. Magic base types are resolved with the configured catalogue when possible; the inspector warns otherwise.
Copied text cannot reveal drop-only metadata or internal modifier identifiers; supply known condition values in the JSON editor.

**Scatter loot** makes repeatable uniform draws from the chosen PoB catalogue with a seed and area level, then runs the current
filter's simulator and shows winning/shadowed blocks and styled labels. It is a coverage preview, not a model of the game's drop
probabilities. Entries without drop levels remain eligible and their count is reported.

## Economy history

Configure the existing collector or a local database:

```json
"economyHistory": {
  "database": ".ruleforge/economy/history.db",
  "url": "http://127.0.0.1:8087",
  "tokenEnv": "RULEFORGE_HISTORY_TOKEN",
  "leagues": ["Standard"],
  "memory": {
    "gameVersion": "3.30", "since": "2026-11-13T19:00:00Z",
    "maxAgeHours": 72, "bucketHours": 4, "minimumListings": 5,
    "minimumTradedUnits": 20, "reliableBuckets": 3,
    "promotionBuckets": 2, "demotionBuckets": 3
  }
}
```

Choose your actual league, patch regime and start time. Omit `url` to read a local database without synchronization. Tokens are read
from the named environment variable and are never saved into `studio.json`. Refresh errors retain the last valid dataset and are
reported in regeneration status. SQLite retains raw observations. The normalized `economy.history` dataset contains patch-filtered
time buckets, variants, source, prices and per-league liquidity evidence. `std.economy.History`, `ReliableHistory` and `MemoryPrice`
consume it; `PersistTiers` remains the explicit policy function for caller-supplied previous tier state.

See [collector deployment](../deployment/economy/README.md) for the Docker image and migration of the existing remote container.
