# Language decisions

DESIGN.md lists contracts that had to be decided before 1.0 (section 16). This is what the engine does. It is the specification
the tests hold the implementation to.

## Evaluation

- **Compile-time execution.** Everything outside a game condition runs while compiling; nothing is evaluated by the game except
  the conditions the engine lowers into the filter.
- **Budgets.** 50 000 000 evaluation steps, 100 000 generated rules and a call depth of 64 by default (settings `[limits]`).
  Exhausting one is an error naming the limit. Every loop iteration costs at least one step, so no loop is free.
- **Purity.** Functions have no side effects outside the rules they emit. There is no I/O, clock, randomness or reflection.
  Recursion is allowed within the depth limit.
- **Numbers.** `int` is a signed 64-bit integer, `float` a 64-bit IEEE double. `/` always gives a float and division by zero is an
  error. `int` widens to `float` where a float is expected; nothing else is coerced. Library functions that use floating point
  (`math.Sqrt`, the quantile interpolation) use fixed iteration counts so results are identical on every platform.
- **Optionals.** `none` can be compared with `==`/`!=`; any other use of it is an error with a note to check `!= none` first.
  `and`/`or` short-circuit, so `@character.level != none and @character.level >= 80` is safe.
- **Determinism.** The same sources, datasets and settings give the same filter byte for byte. The filter header carries a source
  hash (sources in module order, dataset snapshot hashes, settings) and no timestamp.

## Names and modules

- A module is one `.rf` file. `import a.b.c;` looks for `a/b/c.rf` next to the importing entry file, then in every `-I` folder.
  `std.*` imports are the native standard library.
- An imported module's exports are reachable as `c.Name` (its last segment) or the full dotted name, and unqualified when no other
  import exports the same name. Only `export`ed declarations are visible outside their module.
- `$name` is a parameter, `@name` a compile-time setting, anything else a local, a declaration, a field or a style key. A local
  shadows a game condition name (a warning-worthy habit).
- Named arguments are allowed (`f(a: 1)`), positional ones first. Parameter and return types are checked when the value arrives
  or is returned, with the error at the call.

## Game-time conditions

`when`, `coreConditions` and `scope` mix compile-time values and game conditions (the manifest: areaLevel, itemLevel, rarity,
class, baseType, ...). The engine folds every compile-time part and lowers the rest:

- a block is a conjunction, so `or` becomes several blocks (at most 256 alternatives per condition);
- `not` is pushed into the comparison (`!(areaLevel < 5)` is `areaLevel >= 5`);
- a variant whose compile-time condition is true and leaves nothing for the game is *terminal*: nothing after it, including the
  baseline, can be reached, and the rule records how many variants it shadowed;
- `scope { key = value; }` is `key == value` (a list value matches any of its entries); ranges use `when` or `coreConditions`.

## Rules and rulesets

- `rule`, `emit rule` and a template use all append a rule to the ruleset being built. Order of emission is the filter order and
  first match wins, as in the game.
- A rule has an optional `scope`, a `baseline` (action, style) and `variants`. Bare `action`/`style` are the baseline. A variant
  must set `action` or `inherit action`; a style it omits is the baseline's.
- `coreConditions` of a ruleset apply to every rule created while it runs, including through its includes and calls, and must be
  a conjunction.
- `include X;` appends the rules of a ruleset declaration, a function returning `ruleset`, or a ruleset value. `with { theme: T }`
  changes the default theme while it runs.
- `style key;` (a bare name) looks the key up in the configuration's `default-theme`.
- `native "..."` emits a block of raw filter text; it is traced but cannot be simulated.

## Rule inspection and transformation (`std.rules`)

Rules are immutable. The 1.0 intrinsic set is `Concat`, `Count`, `Reverse`, `WithAction`, `WithStyle` and `WithScope`; each
returns new rules, keeps order, and appends a `transform` entry to the rule's trace. They are enough to write max-roll or fated
precedence as ordinary rulesets.

## Datasets

- A dataset is read once, before evaluation, into typed records; the logical source (`from "economy.currency"`) is mapped to a
  snapshot file by the settings file or `--dataset`. CSV and JSON are read by Genesis grammars.
- Missing required fields and wrong types are errors at the `dataset` declaration with the row number; extra fields are ignored.
  CSV cells convert to the declared type (int, float, `true`/`false`, `#hex` colors); an empty cell is a missing value. JSON is
  strict except that a float field accepts any number.
- Iteration is in file order. The hosts that write snapshots (the studio's providers) sort them, so the same market gives the
  same bytes.
- An unpriced item has `none` for its price and stays `none` through `economy.AssignTiers`: it is never assumed worthless.

## Settings and character data

- `@key` reads a setting (`--set key=value`, `[context]` in the settings file). `@character.*` and `@atlas.*` that are not set are
  `none`; any other unset setting is an error listing the known ones.
- **Precedence** of character values: explicit `--set` over the settings file over the studio's project file. The studio
  records where a value came from in its manifest.
- Atlas completion data has no built-in provider; supply `atlas.highestCompletedTier` as a setting.

## Standard library (1.0)

`std.math`, `std.collections`, `std.stats`, `std.economy` (`AssignTiers` with the quantile, threshold and cluster strategies,
`Score`, `StackValue`, `PersistTiers`) and `std.rules` are native, pure and bounded. `ruleforge manifest` lists them with their
parameters. Game item classification data (`std.items`, `std.progression`) comes from datasets, not from code.

## Not in this version

`with { adapter: ... }` is parsed and reported as having no effect; the filter's multiline native syntax is a single string;
`std.items` and `std.progression` have no bundled data; SQLite and the official GGG character API are provider work for the studio.
