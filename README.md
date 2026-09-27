# Ruleforge

_Disclaimer: This is written by Gemini 2.5 Pro on 23 June 2025 with the entire codebase in mind (to save time). **However, I have fact-checked the explanation to ensure accuracy.**_

Ruleforge is a sophisticated, highly configurable compiler designed to generate item filters for the game Path of Exile.
It transforms a modular, easy-to-read syntax (.rf files) into a Path of Exile item filter.
By integrating directly with Path of Building data and real-time economic information from poe.ninja,
Ruleforge allows for the creation of powerful,
data-driven, and context-aware filters that go far beyond what is possible with simple text-based filter editors.

## Table of Contents
- [Table of Contents](#table-of-contents)
- [Core Concepts](#core-concepts)
- [Installation & Setup](#installation-and-setup)
  - [Configuration](#1-configuration-configjson)
  - [Styling](#2-styling-stylesjson)
  - [Running](#3-running-ruleforge)
- [Ruleforge Syntax](#ruleforge-syntax-rf-files)
  - [File Structure](#file-structure)
  - [Comments](#comments)
  - [Metadata Block](#metadata-block)
  - [Variables](#var-declarations)
  - [Style Overrides](#style-overrides)
  - [Section Block](#section-block)
  - [Rule Syntax](#rule-syntax)
  - [Macros](#macros)

## Core Concepts
Ruleforge is built around a few key ideas:

- **Modular Syntax**: Instead of a single, monolithic file, you define your filter logic in modular .rf script files using a clear and expressive syntax. 
- **Centralized Styling**: All visual aspects of the filter (colors, sizes, sounds, effects) are defined in a separate styles.json file. This separates logic from presentation, making styles reusable and easy to manage. 
- **Data-Driven Macros**: Ruleforge includes powerful macros that can automatically generate complex sets of rules based on game data. For example, it can create a full equipment progression for leveling, or automatically tier unique items and skill gems based on their market value. 
- **Economic Awareness**: By pulling data from poe.ninja, Ruleforge can make decisions based on an item's real economic value, ensuring your filter highlights what's truly valuable in the current league. 
- **Configuration over Hardcoding**: Key paths, league settings, and economic calculations are all controlled through a central config.json file, making the tool adaptable to your setup and preferences.

## Installation and Setup

### Quick start (Linux and Windows)

Requires Go 1.23 or newer, internet access for the first run (Go dependencies,
official Path of Building data, and poe.ninja economy data), and write access to
this project directory. No local Path of Building installation or Git executable
is required. From the extracted ZIP directory on Linux:

```sh
./run.sh
```

Or build and run directly from that directory on Linux or Windows:

```sh
go build -o ruleforge-app ./ruleforge/components/ruleforge/entry
./ruleforge-app -config config.json
```

On Windows, the built executable is `ruleforge-app.exe`. Launch it with
`ruleforge-app.exe -config config.json` (PowerShell: `./ruleforge-app.exe -config config.json`).
The included `examples/basic.rf` is compiled to `output/RuleforgeExample.filter`.
`run.sh` builds from the project root and forwards any command-line flags.

### 1. Configuration (config.json)

The included `config.json` uses relative paths and is ready to run from the
extracted ZIP directory. All configured filesystem paths (including each entry
in `FilterOutputDirs` and the optional `PathOfBuildingDataPath`) are resolved
**relative to the location of the config JSON file**, not the current working
directory. Absolute paths also work. Use your platform's path syntax.

- `FilterOutputDirs`: Where the compiled `.filter` files are written.
- `RuleforgeInputDir`: Directory containing `.rf` input scripts.
- `StyleJSONFile`, `StyleColorCSSFile`, `BaseTypeCSVFile`: Bundled style and base-type inputs.
- `PathOfBuildingDataPath`: Optional managed cache directory for the official
  Path of Building Lua sources. Defaults to `.ruleforge-cache/pob` beside the
  config file. **Do not point it at an existing PoB installation or personal
  data directory**: Ruleforge only manages a directory it created itself.
- `LeagueWeights`, `EconomyWeights`, `EconomyNormalizationStrategy`, and
  `ChaseVSGeneralPotentialFactor`: Settings for economy-based compilation.
  League names are resolved using poe.ninja's active PoE 1 economy leagues API;
  each configured league must be available there. If a league is unavailable,
  the error lists the currently advertised names and IDs. Do not change the
  weights to another league silently: update `LeagueWeights` deliberately.

Ruleforge uses poe.ninja's documented PoE 1 economy endpoints
(`/poe1/api/economy/leagues` and
`/poe1/api/economy/stash/current/item/overview`). Old economy caches created
with the retired `/api/data/itemoverview` endpoint are invalidated automatically.
The economy cache lasts 24 hours; source Lua item caches are independent.
An HTTP/API error aborts economy loading with the failed league, category, and
status instead of repeatedly treating every failed request as an empty dataset.

Item drop levels come from PoE Wiki in paced batches, with bounded retries for
throttling and temporary server failures. Failed requests abort item loading
instead of saving an incomplete cache; older caches affected by the previous
lookup failures are rebuilt automatically. PoB's required level is different
from drop level, so it is only a progression estimate for bases the wiki does
not list (level 1 when the requirement is omitted). Such bases are reported
together during enrichment, rather than repeatedly during sorting.

On the first run Ruleforge downloads the specific Lua files it uses from
`PathOfBuildingCommunity/PathOfBuilding` on GitHub, pinned to one commit.
After seven days it checks for an update, downloading a new full snapshot only
when upstream changed. Existing snapshots remain available offline if an
ordinary refresh check/download fails. `-refresh-pob` forces an update check
and reports an error if that check or a needed download fails; it does not
redownload an unchanged revision. `-update-cache-only` downloads/updates data
without compiling filters. To reset a managed snapshot, delete its cache folder
while Ruleforge is not running.

### 2. Styling (styles.json)
The styles.json file is a hierarchical JSON object where you define all visual styles.
Styles can be nested and can inherit from each other using a Combination property.
This allows you to create base styles and layer more specific properties on top.

**Example styles.json entry:**

```json
{
  "Tiers": {
    "Celestial": {
      "Comment": "The absolute best drops.",
      "TextColor": {"red": 255, "green": 215, "blue": 0, "alpha": 255},
      "BorderColor": {"red": 255, "green": 215, "blue": 0, "alpha": 255},
      "FontSize": 45,
      "Beam": { "Color": "Pink", "Temp": false }
    }
  },
  "Special": {
    "Currencies": {
      "Orbs": {
        "Main": {
          "BackgroundColor": { "red": 50, "green": 40, "blue": 70, "alpha": 255 }
        },
        "T1": {
          "Combination": [ "Special/Currencies/Orbs/Main", "Tiers/Celestial" ]
        }
      }
    }
  }
}
```

In this example,
the style `Special/Currencies/Orbs/T1` will inherit all properties from
`Special/Currencies/Orbs/Main` and `Tiers/Celestial`,
combining them into a single definitive style.

### 3. Running Ruleforge

See the Quick start above. Pass `-config /path/to/config.json` to load another
configuration from any working directory. Optional flags: `-verbose`,
`-refresh-pob`, `-update-cache-only`.

## Ruleforge Syntax (.rf files)

### File Structure
A .rf file is composed of three types of top-level blocks: METADATA, var, and SECTION.

### Comments
Single-line comments start with `!!`.
Block comments are enclosed in `!![` and `]!!`.

```rf
!! This is a single-line comment.

!![
  This is a
  multi-line block comment.
]!!
```

### `METADATA` Block
Every script must start with a `METADATA` block, which defines the filter's overall properties.
```rf
METADATA {
  NAME           => "MyAwesomeFilter"
  VERSION        => "1.0"
  STRICTNESS     => "STRICT"
  BUILD          => "SHADOW"
}
```
- `NAME`: The output filename of the filter (e.g., MyAwesomeFilter → which would compile into MyAwesomeFilter.filter). 
- `VERSION`: A version string for your reference. 
- `STRICTNESS`: A user-defined value. Can be `ALL`, `SOFT`, `SEMI-STRICT`, `STRICT`, `SUPER-STRICT`. 
  - Note: These are currently (23 June 2025) not used yet... These are on the to-do list to be implemented.
- `BUILD`: The character class archetype the filter is designed for. Macros use this to determine the appropriate gear. Valid options: `MARAUDER`, `RANGER`, `WITCH`, `TEMPLAR`, `DUELIST`, `SHADOW`.

### `var` Declarations
You can declare variables to store and combine styles.
This is extremely useful for creating reusable, complex styles.
Variables are prefixed with `$` when referenced (not when declared).

```rf
!! Var declaration
var rare_style = "ItemGroups/Equipment" + "Tiers/Primal"

!! If I were to reference this variable sometime later, I would say: $rare_style
```

### Style Overrides
When combining styles, you may have conflicts (e.g., both styles define a `FontSize`).
You can explicitly resolve these conflicts using an `!override` block.

```rf
var custom_weapon_style = "StyleA" + "StyleB" !override [
  "StyleB" => "FontSize"
]
```

This declaration combines `StyleA` and `StyleB`, but if both have a `FontSize`, the one from `StyleB` will be used.

**The default behavior is to use everything from `StyleA` if there is a conflict present,
UNLESS an `!override` block is defined.
In this case, it will override only the specific elements specified.
Obviously if there are no conflicts, it's a simple combination (this would be preferred).**

### `SECTION` Block
Sections are the core organizational unit of the filter.
They group a set of rules under a common theme and can apply conditions to all rules within them.

```rf
SECTION {
  METADATA {
    NAME        => "Flasks"
    DESCRIPTION => "Rules for all utility and life/mana flasks"
  }

  SECTION_CONDITIONS {
    WHERE @item_class == "Flask"
  }

  RULES {
    !! Rules go here...
  }
}
```

- `METADATA`: Contains the `NAME` and `DESCRIPTION` of the section, which are used to generate a Table of Contents in the final filter file. 
- `SECTION_CONDITIONS`: A block where you can define conditions that apply to every rule inside this section. This is great for reducing repetition. 
- `RULES`: The block that contains the actual filter rules and macros for this section.

### Rule Syntax
The standard rule format is a chain of a condition, a style, and an action.

`WHERE <condition> => <style> => <action>`
- `WHERE`: The start of every rule. 
- Condition: Defines what item property to check. 
  - Identifiers: `@area_level`, `@stack_size`, `@item_type`, `@item_class`, `@rarity`, `@sockets`, `@socket_group`, `@height`, `@width`. 
  - Operators: `==`, `!=`, `>=`, `<=`, `>`, `<`. 
  - Value: A quoted string `"value"`, a number, or a variable reference `$var`. 
- Style: A reference to a style defined in `styles.json`, either by its full path (`Essences/Tiers/High`) or via a variable (`$my_style`). 
- Action: What to do with the item. Can be `$Show` or `$Hide`.

**Example:**
```rf
RULES {
  !! Show rare helmets with an item level of 86 or higher using the celestial style
  WHERE @rarity == "Rare" -> @item_class == "Helmets" -> @item_level >= 86 => "Tiers/Celestial" => $Show
  
  !! Whitespace is optional. For brevity, the following is allowed too:
  WHERE @rarity=="Rare"->@item_class=="Helmets"->@item_level>=86=>"Tiers/Celestial"=>$Show
}
```

### Macros
Macros are powerful commands that generate large numbers of rules automatically.

`MACRO["<macro_name>"-><parameters>]`

`item_progression-equipment-leveling`, `item_progression-equipment-mapping` & `item_progression-flasks`
These macros generate a complete set of Show/Hide rules for equipment or flasks to guide the leveling process.
They automatically use drop-level data from Path of Building.

```rf
MACRO["item_progression-equipment-leveling"
  -> $show_normal => $leveling_style
  -> $show_magic  => $leveling_style
  -> $show_rare   => $leveling_style_rare
  -> $hidden_normal => $hidden_style
  -> $hidden_magic  => $hidden_style
  -> $hidden_rare   => $hidden_style
]
```

`unique_tiering` & `skill_gem_tiering`
These macros fetch economy data from poe.ninja
and use K-Means clustering to automatically group unique items or skill gems into tiers.
You provide a style for each tier.

```rf
!! The name of each parameter is irrelevant in this example (since it's not used by the macro)
MACRO["unique_tiering"
  -> $tier1 => "Special/Uniques_T1_Chase"
  -> $tier2 => "Special/Uniques_T2_Exceptional"
  -> $tier3 => "Special/Uniques_T3_Valuable"
  -> $tier4 => "Special/Uniques_T4_Useful"
  -> $tier5 => "Special/Uniques_T5_Common"
]
```

`handle_csv`
This macro generates rules based on a CSV file,
allowing for easy management of large, repetitive rule sets like currencies or essences.
It uses the `basetype_automation_config.csv` file by default.

```rf
MACRO["handle_csv"->$category=>"Orbs"]
```
This command would process all rows in the CSV file where the `Category` column is "Orbs,"
generating a rule for each one based on its configured `Basetype`, `MinStackSize`, `Style`, and `Tier`.

### Low-memory unique loading

This build deliberately **does not execute** PoB `Generated.lua` or download the
large modifier files needed by its generator. It still loads regular uniques
from `Uniques/*.lua` and any items in `Uniques/Special/New.lua`. Unique items
which exist *only* in PoB's generated dataset are not available for Ruleforge
rules or unique-specific tiering. No errors are suppressed: regular source-file
and parsing errors still fail normally.

The first run writes a new `data-cache/basetypes-standard-only.json` item cache.
The legacy `basetypes.json` file is intentionally not read or deleted, so a
large cache from earlier generated-unique builds cannot be reloaded. PoB's
existing `.ruleforge-cache/pob` source files can be reused unchanged; there is
no need to delete the PoB cache or force an upstream refresh.

The generator implementation remains in the source tree for future development,
but the application never invokes it in low-memory mode.
