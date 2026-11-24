# Move a filter project to the rewrite

The C engine and Go Studio replace the legacy Go engine. Treat existing filter projects as migration inputs, not as
source-compatible projects. Keep the original project and its last working `.filter` output until the replacement is checked.
The legacy source remains available in Git history at `d98030f1c13e1e60cf070464594a7eef7c9a975d`.

## Start on another machine

1. Download the Linux or Windows archive and `SHA256SUMS` from the GitHub release. Check the archive's SHA-256 against that file
   (`sha256sum` on Linux; `Get-FileHash -Algorithm SHA256` in PowerShell).
2. Extract the archive. Keep `ruleforge` and `ruleforge-studio` together (`.exe` on Windows). The archive also includes `examples/`
   and this `docs/` directory. Add the extracted directory to `PATH`, or invoke the programs by their full paths.
3. Copy `examples/currency` into a new project folder. From that folder, run:

   ```sh
   ruleforge compile entry.rf --settings settings.toml -o currency.filter --model model.json
   ruleforge simulate entry.rf --settings settings.toml --items items.json
   ruleforge-studio serve --project .
   ```

   Open `http://127.0.0.1:8750`. Studio finds the engine beside its executable; `--engine PATH` can select another copy.

The example uses a local CSV and does not need a PoB download or an economy service. Keep generated filters in the new project
until comparison is complete; then set each output's `path` in `studio.json` to the desired PoE filter location.

## Port the filter in a separate project

- Begin with the existing filter's intended order and behavior: valuable-item exceptions, leveling/build rules, general tiers,
  then fallback rules. The first matching generated block wins. Keep representative copied items and the old generated filter
  as comparison fixtures.
- Put the entry point in `entry.rf` with `version 1.0.0;`, a `module` declaration, imports, `configuration` and an exported
  `main` function. Its `include` statements choose and order rulesets. The example provides complete working syntax.
- Move colours into a `colorSet`, theme keys into a `themeSchema`, and show/hide properties into a `theme`. Refer to those keys
  from rules and templates. Select the default theme in `configuration`. See [language semantics](language.md).
- Import `std.conditions` for PoE conditions. Use the current manifest (`ruleforge manifest`) and language documentation when
  translating old condition names or operators. Preserve scope and first-match order explicitly.
- Declare project datasets in `settings.toml` for direct CLI compilation and in `studio.json` for Studio regeneration. Keep
  `entry`, context values, dataset names and outputs consistent between the two. Studio owns refreshes and output publication;
  the engine evaluates the resulting snapshots.
- Add PoB/catalogue data, progression policy and economy history after the local rules work. Their configuration, offline cache
  behavior and standard modules are described in [the Studio guide](studio.md). Preserve the existing collector database or
  volume; the collector retains protocol 1 and migrates its schema. Never copy access tokens into project files.
- Compile after each logical group of rules. Use Studio's Inspector and Simulator to compare winning blocks, hidden items and
  styles with the old output. Paste representative PoE items; use explicit JSON fields for conditions absent from copied text.
  Seeded loot scattering checks catalogue coverage, not real drop probabilities.

There is no automatic legacy-project converter. Script and dataset-mapping forms remain deferred; edit those `.rf` files and
configuration directly. Themes and palettes have source-preserving forms with conflict checks. Automatic regeneration should be
enabled only after outputs and providers are configured and verified; failed compiles retain the last valid published filter.

## Handoff to the next session

Give the next session the release tag, this guide, a copy of the legacy project, the last generated filter, and the intended
build/classes, league, strictness levels and output locations. Keep credentials in environment variables. The next session can
port and compare the filters against the installed release without rebuilding the engine or recovering this conversation.
