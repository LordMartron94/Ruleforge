# Ruleforge — Language & Engine Design Document

**Language:** Ruleforge 1.0.0  
**Grammar engine:** Genesis 0.1.0  
**Target:** Path of Exile 1  
**Status:** Proposed design specification

---

## 1. Vision and Scope

Ruleforge is a statically typed, compile-time domain-specific language for generating Path of Exile item filters.

Its primary goal is to allow users to construct complex, maintainable loot filters from reusable modules rather than manually managing thousands of filter rules.

A single Ruleforge project can produce multiple filters based on strictness, visual themes, character progression, build preferences, league economy, and other configuration parameters.

### 1.1 Design principles

1. **Explicit over implicit.** Filter behavior and generated rule ordering must remain predictable and traceable.
2. **Composition over macros.** Complex functionality should be implemented through reusable functions, datasets, rulesets, and standard-library modules.
3. **Compile-time execution.** Ruleforge code executes during filter generation, not while playing.
4. **Strict typing, permissive data integration.** Required fields are validated; unexpected external fields are ignored.
5. **Domain-oriented expressiveness.** Provide enough computation for tiering and transformations without creating a general-purpose mathematical language.
6. **Determinism.** Identical inputs and data snapshots produce identical output.
7. **Separation of concerns.** Data acquisition, rule generation, styling, and filter emission remain independent.

### 1.2 Non-goals

Ruleforge will not execute arbitrary filesystem or network operations from scripts, interact with game memory, automate player actions, or simulate the full Path of Exile economy.

---

## 2. Language Fundamentals

### 2.1 Modules and visibility

Every Ruleforge file declares its language version and module identifier.

Definitions are private to their module unless explicitly exported. Modules can import other modules and reference their exported declarations.

**Example — module declarations**

```ruleforge
version 1.0.0;
module filters.currency;

import std.economy;
import filters.themes;

export func GenerateCurrencyRules() -> ruleset {
    // Rule-generation implementation.
}
```

The version is an unquoted semantic version.

Module imports provide visibility, not automatic rule execution. Rules must be included or emitted explicitly.

### 2.2 Types

The proposed type system contains:

| Type | Purpose |
|---|---|
| `int` | Signed integers |
| `float` | Finite floating-point numbers |
| `bool` | Boolean values |
| `string` | Text and identifiers |
| `color` | Visual color values |
| `style` | Resolved visual style |
| `rule` | Immutable generated rule |
| `ruleset` | Ordered collection of rules |
| `list<T>` | Homogeneous collection |
| `dataset<T>` | Read-only external records |
| `T?` | Optional value |
| `void` | No return value |
| Named schemas | User-defined record types |

Function parameters use `name type`, preserving the original Ruleforge convention.

**Example — types and variables**

```ruleforge
func IsValuable(price float, threshold float) -> bool {
    let result = $price >= $threshold;
    return result;
}

func CalculatePrice(value float, amount int) -> float {
    return $value * $amount;
}
```

`let` is immutable. `var` permits local compile-time reassignment.

Parameters are referenced using `$`, whereas ordinary local variables use unprefixed identifiers.

### 2.3 Expressions

Ruleforge supports:

- Arithmetic: `+`, `-`, `*`, `/`.
- Comparisons: `==`, `!=`, `<`, `<=`, `>`, `>=`.
- Boolean operators: `and`, `or`, `not`.
- Parentheses, function calls, and typed references.
- List literals and indexed access.

Operators use Genesis Pratt parsing for predictable precedence.

**Example — bounded scoring**

```ruleforge
func ScoreItem(
    price float,
    craftingValue float,
    craftingWeight float
) -> float {
    return $price * (1 - $craftingWeight)
         + $craftingValue * $craftingWeight;
}
```

All numerical computation executes during compilation. It does not add computational work to the resulting PoE filter.

The language intentionally excludes arbitrary user-defined operators, reflection, dynamic evaluation, and unrestricted numerical algorithms.

### 2.4 Control flow

Ruleforge provides `if`/`else`, finite `for` iteration, and `return`.

**Example — conditional compilation**

```ruleforge
if @strictness >= 3 {
    include currency.StrictCurrency;
} else {
    include currency.NormalCurrency;
}
```

**Example — iteration**

```ruleforge
for item in CurrencyItems where item.enabled {
    emit rule {
        scope {
            baseType = item.baseType;
        }

        action show;
        style DefaultTheme.godTier;
    }
}
```

Loops are bounded and operate on materialized collections. Unrestricted `while` loops are excluded from version 1.0.

---

## 3. Datasets and External Data

### 3.1 Dataset abstraction

Datasets are the primary mechanism for accessing external information in Ruleforge.

The language itself is independent of the storage format. CSV, JSON, SQLite, remote APIs, and internal game manifests are normalized into the same typed record interface.

Scripts reference datasets by logical name and declare the schema they expect.

### 3.2 Schema declarations

**Example — economy dataset**

```ruleforge
version 1.0.0;
module data.currency;

export schema CurrencyRecord {
    baseType string;
    chaosValue float?;
    listingCount int?;
    craftingValue float?;
}

export dataset Currencies: CurrencyRecord
    from "economy.currency";
```

The dataset registry resolves `"economy.currency"` to its configured source.

A record may contain additional fields not declared in `CurrencyRecord`. They are ignored.

Missing required fields produce errors. Missing optional fields resolve to `none`.

Incorrect types also produce errors rather than silently coercing data.

### 3.3 Supported data sources

| Source | Integration |
|---|---|
| CSV | Header-based field mapping |
| JSON | Object/array projection |
| SQLite | Read-only queries and snapshots |
| poe.ninja | Economy data through provider adapters |
| Path of Building | Item and progression-related game data |
| Game manifests | Native PoE item and condition information |
| Character snapshots | Authorized character information |
| Manual datasets | User-provided records |

Source configuration belongs to the engine, not the script.

**Example — external engine configuration (TOML, not Ruleforge)**

```toml
[datasets."economy.currency"]
provider = "poe-ninja"
league = "current"
refresh = "1h"

[datasets."build.preferences"]
provider = "csv"
path = "data/build-preferences.csv"

[datasets."game.baseTypes"]
provider = "manifest"

[datasets."custom.equipment"]
provider = "sqlite"
database = "data/equipment.db"
query = """
SELECT baseType, category, requiredLevel
FROM Equipment
ORDER BY baseType
"""
```

All source adapters produce immutable snapshots before script evaluation.

### 3.4 Dataset iteration and filtering

**Example — generating rules from CSV-backed data**

```ruleforge
schema EquipmentEntry {
    baseType string;
    category string;
    minimumLevel int;
}

dataset Equipment: EquipmentEntry
    from "custom.equipment";

export func GenerateEquipmentRules() -> ruleset {
    for item in Equipment where item.minimumLevel <= @character.level {
        emit rule {
            scope {
                baseType = item.baseType;
            }

            action show;
            style DefaultTheme.godTier;
        }
    }
}
```

This replaces the original `handle_csv` macro with ordinary data access and rule generation.

Dataset iteration order must be deterministic. Missing character-level data requires a deliberate fallback rather than an implicit numeric default.

---

## 4. Economy Integration and Tiering

### 4.1 Data acquisition

Economy providers retrieve market information before compilation. The initial target is poe.ninja's PoE 1 economy data.

Snapshots should preserve item identity, variants, market value, league, timestamps, listing counts or exchange volume, and provenance.

Unavailable prices are represented explicitly. An unpriced item must not automatically be interpreted as worthless.

### 4.2 Tiering strategies

The standard library should provide:

| Strategy | Behavior |
|---|---|
| Absolute thresholds | Fixed price boundaries |
| Quantiles | Distribution-based tier assignment |
| Clustering | Deterministic bounded clustering |
| Weighted scoring | Combines market, crafting, and build value |
| Persistent tiers | Reduces instability between updates |

The compiler need not hardcode any particular economic scoring strategy.

### 4.3 Example — economy-aware currency rules

```ruleforge
version 1.0.0;
module filters.currency;

import data.currency;
import filters.themes;

export func GenerateCurrencyRules() -> ruleset {
    for item in currency.Currencies {
        if item.chaosValue == none {
            // Unpriced currency receives a visible fallback.
            emit rule {
                scope {
                    baseType = item.baseType;
                }

                action show;
                style DefaultTheme.godTier;
            }
        } else if item.chaosValue >= 50 {
            emit rule {
                scope {
                    baseType = item.baseType;
                }

                action show;
                style DefaultTheme.godTier;
            }
        } else {
            emit rule {
                scope {
                    baseType = item.baseType;
                }

                action show;
                style DefaultTheme.poopTier;
            }
        }
    }
}
```

This intentionally uses fixed thresholds to illustrate the language primitives.

More advanced scoring belongs in `std.economy`, so users should not need to write clustering or statistical calculations themselves.

### 4.4 Example — standard-library tiering

The following demonstrates the proposed API, whose precise function signatures still need to be finalized.

```ruleforge
import std.economy;

func GenerateUniqueRules() -> ruleset {
    let tiers = economy.AssignTiers(
        data: UniquePrices,
        field: "chaosValue",
        count: 5
    );

    for item in tiers {
        emit rule {
            scope {
                baseType = item.baseType;
            }

            action show;
            style DefaultTheme.godTier;
        }
    }
}
```

The standard library calculates tier assignments; the language supplies typed records, iteration, function calls, and rule emission.

The example uses a single style for brevity. Actual tiering functions must expose the assigned tier so callers can select distinct styles.

### 4.5 Currency-specific scoring

Currency scoring should support crafting usefulness, market price, trend, confidence, and stack size.

The standard library can implement weighted scoring using ordinary arithmetic.

**Example — value per stack**

```ruleforge
func StackValue(price float, quantity int) -> float {
    return $price * $quantity;
}
```

The library can use this result to generate native stack-size conditions at useful thresholds.

### 4.6 Market-data reliability

Economy snapshots should retain raw historical observations, provider identity, freshness, and confidence signals.

Tier persistence can require multiple new reliable snapshots before changing an established tier.

Recompiling the same snapshot must not count as new evidence.

---

## 5. Themes and Visual Styling

Themes are declared directly within Ruleforge modules, replacing the original engine's external theme and stylesheet files.

### 5.1 Theme contracts

A `themeSchema` defines the styles a theme must provide.

**Example — complete theme**

```ruleforge
version 1.0.0;
module filters.themes;

export themeSchema BaseTiering {
    godTier;
    poopTier;
}

colorSet Palette {
    GOLD = #FFD700;
    DARK = hsl(h220 s20 l15 a240);
    LOW = hsv(h15 s35 v45 a220);
}

export theme DefaultTheme implements BaseTiering {
    godTier {
        onShow {
            Background Palette.GOLD;
            Border r255 g240 b120;
        }

        onHide {
            Background Palette.DARK;
        }
    }

    poopTier {
        onShow {
            Background Palette.LOW;
        }

        onHide {
            Background Palette.DARK;
        }
    }
}

export theme AlternativeTheme extends DefaultTheme {
    override godTier {
        onShow {
            Background #402080;
            Border Palette.GOLD;
        }
    }
}
```

Theme inheritance is explicit. An inherited theme retains unspecified style definitions.

The compiler validates that all required schema keys are implemented.

### 5.2 Theme adapters

A theme implements one `themeSchema`, and a script's rules are written against one: `style godTier;` names a key of the schema the project
works with. A theme written for a different schema (a downloaded theme, an older project) therefore cannot be used as it is. An **adapter**
maps one schema onto the styles of a theme that implements another, so the theme can be used without editing it or the rules.

```ruleforge
themeSchema Legacy {
    huge;
    tiny;
}

theme Old implements Legacy {
    huge { onShow { Background #FF0000; } }
    tiny { onShow { Background #00FF00; } }
}

configuration {
    default-schema = themes.BaseTiering;        // every theme in use must implement this schema

    adapter OldToBase -> themes.BaseTiering {   // adapter Name -> the schema the rules are written against
        godTier  = huge;                        // key of BaseTiering = style of the adapted theme
        poopTier = tiny;                        // keys the adapter does not mention use the style of the same name
    }
}

export func main() {
    include currency.GenerateCurrencyRules() with { theme: Old, adapter: OldToBase };
}
```

- `default-schema` is the contract of the project. A theme that implements a different schema is refused wherever it would become the
  default theme (`default-theme`, or `with { theme: ... }`), with a note naming the way out. A theme that implements no schema has no
  contract to compare and is checked style by style when its styles are used.
- An `adapter` is declared in the `configuration` block of the module that uses it and is private to that module, like any declaration
  not marked `export`. Its target is a `themeSchema`; each mapping reads `schemaKey = themeStyle;`.
- `with { theme: T, adapter: A }` uses T through A: the included ruleset sees a theme with the keys of A's target schema, each served by
  the style A maps it to, and nothing else. `with { adapter: A }` alone adapts the current default theme. The adapted theme replaces the
  default theme for that `include` only, like `theme:` does.
- The compiler checks the whole mapping before it uses it: every left side is a key of the target schema and is mapped once, every right
  side is a style the theme has, and every key of the target schema ends up served. The model still names the theme's own styles, so the
  style of a block says `Old.huge`, the style that was actually used.
- Adapters rename styles; they do not compute. Merging or deriving a style (a darker `huge`) is an `extends` theme, not an adapter.

**Datasets.** The same need exists for data: a snapshot whose columns are called differently from the schema's fields. That is not
covered by this version. If it proves valuable it should be a mapping on the `dataset` declaration (`dataset D: Schema from "src" { field = column; }`)
rather than a second meaning for `adapter`, because a dataset is bound where it is declared and a theme where it is used.

### 5.3 Color syntax

Supported color representations include:

```ruleforge
Background r255 g128 b64 a240;
Background rgb(r255 g128 b64 a240);
Background hsl(h210 s75 l45 a240);
Background hsv(h210 s75 v80 a240);
Background #FF8040;
Background #FF8040F0;
```

HSL/HSV colors are converted to the game's native RGB/RGBA representation during compilation.

### 5.4 Style properties

The styling registry should cover the full supported PoE filter surface, including text and background colors, border colors, font sizes, minimap icons, beams, and sounds.

Property names and argument types are verified semantically against the target-game manifest.

---

## 6. Rulesets and Rule Generation

### 6.1 Ruleset ordering

Rules are emitted in lexical order.

Within a ruleset, earlier rules take precedence over later rules according to the game's evaluation semantics.

The entrypoint's `include` order determines global composition.

**Example — ruleset declaration**

```ruleforge
export ruleset Currency {
    description "Currency rules";

    coreConditions {}

    rule ExaltedOrb {
        scope {
            baseType = "Exalted Orb";
        }

        action show;
        style DefaultTheme.godTier;
    }

    rule OrdinaryCurrency {
        scope {
            baseType = "Scroll of Wisdom";
        }

        action hide;
        style DefaultTheme.poopTier;
    }
}
```

### 6.2 Templates and variants

Templates define reusable rule structures.

**Example — conditional variants**

```ruleforge
template BaseRule(
    itemName string,
    goodStyle style,
    badStyle style
) {
    scope {
        baseType = $itemName;
    }

    baseline {
        action show;
        style $goodStyle;
    }

    variants {
        when @strictness >= 3 {
            action hide;
            style $badStyle;
        }

        when areaLevel >= 90 {
            inherit action;
            style $goodStyle;
        }
    }
}
```

Compile-time conditions are simplified before emission. Remaining game-time conditions are lowered into the appropriate native filter constraints.

Variant ordering and baseline fallback must preserve first-match-wins semantics.

### 6.3 First-class rule values

`rule` and `ruleset` are immutable semantic values.

Functions may inspect existing rules and return transformed rule sequences.

This enables the original max-roll, fated-item, and other precedence features to be implemented as standard-library transformations.

The engine must expose a minimal typed API for inspecting and reconstructing rules; the exact intrinsic interface remains to be specified.

### 6.4 Rule transformation principles

A transformation must:

- Preserve rule ordering unless explicitly instructed otherwise.
- Preserve visibility semantics unless the transformation intentionally changes them.
- Retain a source trace to the original rule.
- Never silently affect rules outside its input sequence.
- Produce deterministic output.

This replaces the original engine's implicit global macros with explicit composition.

---

## 7. Character Progression

### 7.1 Character level as a compilation input

Ruleforge supports using the selected character's level to generate more appropriate filters.

This is particularly important when a high-level character visits an earlier area.

A filter based only on `areaLevel` cannot know that the player already has much better equipment.

The proposed compilation context includes:

| Value | Purpose |
|---|---|
| `@character.level` | Selected character level |
| `@character.name` | Selected character identity |
| `@character.class` | Character class |
| `@character.league` | Character league |
| `@character.highestEnteredMapTier` | Highest observed map tier entered |
| `@atlas.highestCompletedTier` | Highest verified completed map tier |

Every field that may be unavailable is optional.

### 7.2 Example — level-aware filtering

```ruleforge
export ruleset CampaignEquipment {
    rule OldEquipment {
        scope {
            itemLevel = 1;
        }

        variants {
            when @character.level != none
                 and @character.level >= 80
                 and areaLevel < 60 {
                action hide;
                style DefaultTheme.poopTier;
            }
        }
    }
}
```

This is an illustrative conditional-generation pattern. A complete progression implementation would use equipment datasets and class/build requirements rather than a single item-level match.

The compiler evaluates `@character.level` using its captured snapshot. It emits the remaining game-valid conditions.

### 7.3 Example — progressively stricter filter selection

```ruleforge
export func GenerateProgressionRules() -> ruleset {
    if @character.level == none {
        include progression.DefaultEquipment;
    } else if @character.level < 40 {
        include progression.EarlyCampaign;
    } else if @character.level < 68 {
        include progression.LateCampaign;
    } else {
        include progression.EndgameEquipment;
    }
}
```

This allows filter behavior to advance with the character without requiring the player to manually switch between many predefined files.

The standard library should also support smooth progression using actual equipment requirements rather than only broad level bands.

### 7.4 Character synchronization

Character level should be retrievable through the official GGG character API when authorized OAuth access is available.

Ruleforge should additionally support manual selection and manual level values.

Because new OAuth application registrations are currently restricted, official synchronization must remain optional.

Snapshot provenance must identify whether a value came from the API, manual configuration, or another supported provider.

### 7.5 Atlas progression

Map progression may provide additional strictness information.

It must distinguish a map tier merely entered from a map tier actually completed.

**Example — map-progress-aware rules**

```ruleforge
export func GenerateMapRules() -> ruleset {
    if @atlas.highestCompletedTier != none
       and @atlas.highestCompletedTier >= 12 {
        include maps.StrictLowTierMaps;
    } else {
        include maps.NormalMapProgression;
    }
}
```

This is valid as a language-level compilation policy even when the selected map-completion provider is unavailable.

Atlas passive information alone is insufficient to assert a specific highest completed map tier.

Manual or future verified data providers can supply the completion snapshot.

---

## 8. Automatic Data Refresh and Regeneration

### 8.1 Architecture

Regeneration is controlled by the host engine, not by Ruleforge scripts.

The scheduler periodically refreshes data sources and regenerates filters when relevant input snapshots change.

The last valid filter remains available if a refresh or compilation fails.

### 8.2 Example — engine configuration

The following is external TOML configuration, not Ruleforge syntax.

```toml
[character]
source = "official-api"
name = "MyCharacter"
fallback_level = 80

[refresh.character]
interval = "5m"

[refresh.economy]
interval = "1h"

[regeneration]
enabled = true
on_snapshot_change = true
minimum_interval = "5m"
atomic_publish = true
preserve_last_valid = true
```

These settings illustrate the intended configuration contract; their precise names are not yet frozen.

Refreshing character level every few minutes does not mean the generated filter updates its contents every few minutes. The engine can skip regeneration when the normalized snapshot remains unchanged.

### 8.3 Regeneration behavior

A successful cycle:

1. Acquire authorized data snapshots.
2. Validate their schemas.
3. Compare their relevant values with the previous compilation.
4. Compile the affected filter variants.
5. Validate generated PoE filter syntax.
6. Publish results atomically.
7. Record the compilation manifest.

The scheduler respects provider rate limits, retries failures conservatively, and never overwrites a working filter with invalid output.

---

## 9. Entry Point and Complete Composition Example

The main function composes rulesets in their intended evaluation order.

```ruleforge
version 1.0.0;
module entry;

import filters.currency;
import filters.themes;
import filters.progression;
import filters.maps;

configuration {
    default-schema = themes.BaseTiering;
    default-theme = themes.DefaultTheme;
}

export func main() {
    // Valuable and exceptional drops first.
    include currency.ValuableDrops;

    // Economy-aware generated rules.
    include currency.GenerateCurrencyRules();

    // Character-dependent equipment filtering.
    include progression.GenerateProgressionRules();

    // Atlas progression may alter map visibility.
    include maps.GenerateMapRules();

    // General fallback last.
    include currency.GeneralFallback;
}
```

The compiler flattens every included ruleset and generated rule sequence into a single ordered filter.

The output order is explicit and can be inspected through source maps.

---

## 10. Standard Library

Specialized functionality should be implemented primarily in Ruleforge modules.

| Module | Responsibility |
|---|---|
| `std.collections` | Collection transformations and ordering |
| `std.math` | Basic bounded numerical helpers |
| `std.stats` | Quantiles, smoothing, clustering |
| `std.economy` | Prices, confidence, scoring, tiering |
| `std.progression` | Character-aware equipment progression |
| `std.items` | Game item classification and lookup |
| `std.rules` | Rule transformations and precedence |
| `std.themes` | Style composition helpers |

### 10.1 Original macro migration

| Original feature | Replacement |
|---|---|
| `handle_csv` | Dataset iteration and rule emission |
| `economy_csv` | Dataset-backed economy library |
| `unique_tiering` | Generic economy scoring and tier assignment |
| `currency_tiering` | Economy scoring with crafting and stack policies |
| `skill_gem_tiering` | Dataset-backed gem tiering |
| `divination_card_tiering` | Generic economy tiering |
| `tattoo_tiering` | Generic economy tiering |
| `item_progression-equipment-leveling` | Character/build progression library |
| `item_progression-flasks` | Flask progression library |
| `item_tier_matrix` | Dataset classification and scoring |
| `max_roll_precedence` | Explicit ruleset transformation |
| `fated_precedence` | Explicit ruleset transformation |

These implementations should remain inspectable and traceable like ordinary user-authored code.

---

## 11. Compiler Architecture

The engine uses a multi-stage compilation pipeline.

### Phase 1 — Frontend

Genesis-generated lexer and parser produce a lossless syntax tree containing the source program's structure, comments, and locations.

### Phase 2 — Semantic analysis

The compiler resolves modules, exports, declarations, function calls, types, and theme contracts.

It distinguishes compile-time expressions from conditions that must be emitted for game-time evaluation.

### Phase 3 — Snapshot preparation

The engine loads, normalizes, and validates all required datasets.

No network or filesystem operations are permitted from Ruleforge source code itself.

### Phase 4 — Compile-time execution

Functions, loops, template expansions, dataset processing, and ruleset transformations execute under explicit resource budgets.

### Phase 5 — Rule lowering

The compiler produces native PoE filter conditions and actions, preserving lexical ordering and first-match semantics.

### Phase 6 — Emission

The engine writes validated `.filter` files, source maps, diagnostics, and reproducibility manifests.

### Phase 7 — Optional scheduling

A separate scheduler refreshes snapshots and triggers new compilations when required.

---

## 12. Performance and Safety

The compiler must provide predictable resource usage.

Recommended restrictions include finite iteration, bounded recursion, maximum emitted-rule counts, bounded temporary allocations, and deterministic evaluation.

Numerical operations should use a small, well-defined type system.

Expensive statistical procedures, such as clustering, may be implemented as native pure standard-library primitives where the performance benefit justifies it.

**The goal is not to eliminate mathematics but to prevent unbounded and unpredictable compilation work.**

Scripts cannot access arbitrary files, sockets, processes, credentials, or SQLite connections. These capabilities remain restricted to explicitly configured adapters.

---

## 13. Validation and Diagnostics

Validation is divided into:

1. Syntax validation.
2. Static type checking and semantic resolution.
3. External dataset schema validation.
4. Target-game condition and styling validation.
5. Generated filter verification.

Errors should identify the responsible module, source location, dataset record, or standard-library expansion.

Unrecognized game items can produce warnings when reasonable, but type errors and unrepresentable game conditions must produce errors.

### 13.1 Example — schema diagnostics

Given:

```ruleforge
schema MarketEntry {
    baseType string;
    chaosValue float;
}
```

A JSON record containing `baseType`, `chaosValue`, and several unexpected fields is valid.

A record missing `chaosValue` is invalid.

A record whose `chaosValue` is `"unknown"` is invalid unless the source adapter has an explicit, documented normalization rule.

This behavior provides permissive integration without silently accepting incorrect data.

---

## 14. Versioning and Reproducibility

Ruleforge scripts explicitly declare the target language version using semantic versioning.

The compiler records its version, the source-module graph, the standard-library version, normalized dataset hashes, compilation settings, and selected character snapshot.

Given these same inputs, compilation must reproduce the same filter.

Economy snapshots and character-progress updates intentionally change the generated output only when relevant values change.

Migration tools should assist users when later language versions introduce breaking changes.

---

## 15. Implementation Roadmap

**Phase 1 — Language foundation:** Finalize Genesis grammar, expression precedence, type checking, compile-time execution, and function semantics.

**Phase 2 — Dataset foundation:** Implement schemas, dataset binding, CSV/JSON/SQLite adapters, validation, and deterministic snapshots.

**Phase 3 — Rule-generation primitives:** Implement immutable rule values, rulesets, emission, and composable rule transformations.

**Phase 4 — Standard library:** Reimplement the original macro functionality using language primitives.

**Phase 5 — Economy and progress integration:** Add poe.ninja snapshots, tier history, character-level synchronization, and build-progression policies.

**Phase 6 — Automation and developer tooling:** Add scheduled regeneration, source mapping, diagnostics, filter comparison, and item simulation.

---

## 16. Outstanding Design Decisions

The following contracts must be finalized before Ruleforge 1.0.0 is frozen:

- Exact `rule`/`ruleset` inspection and transformation APIs.
- Function purity, recursion, and evaluation-budget behavior.
- Dataset identity, ordering, and field-name mapping conventions.
- Floating-point precision and reproducibility guarantees.
- Standard-library tiering API and tier-stability policies.
- Precedence of manual versus synchronized character values.
- Supported Atlas progression data providers.
- Filter publication and reload behavior.
- Multiline native PoE syntax.
- Complete semantic rules for combining compile-time and game-time conditions.

The code examples are **proposed language usage**, not evidence that the current Genesis grammar or compiler already implements each construct. They form acceptance targets for the grammar, semantic analyzer, standard library, and integration tests.

---

## 17. References

- Existing Ruleforge design document.
- Original Ruleforge README and ThunderGod filter, including macro-based economy tiering, progression, and styling.
- Genesis 0.1.0 language definition and `General` library.
- [Grinding Gear Games Developer Documentation](https://www.pathofexile.com/developer/docs)
- [GGG API Reference](https://www.pathofexile.com/developer/docs/reference)
- [poe.ninja Economy API](https://poe.ninja/docs/api)
