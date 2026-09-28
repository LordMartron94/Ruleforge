# Campaign mapping items — research note

## Scope

Which mapping-related items need campaign filter coverage, and what does the
current game actually allow? This is a focused implementation decision for the
existing campaign filter, checked on 28 September 2026. Building an endgame
filter and auditing every legacy item in the repository are outside scope.

## Findings

**Scouting Reports are obsolete.** GGG's 3.28 patch notes state that they are no
longer obtainable and existing reports no longer work. The same update removed
Kirac missions. Adding filter rules for legacy reports is therefore compatibility
coverage, not support for a current campaign drop. [1]

**Orb of Unmaking has natural drop level 70.** PoEDB's extracted item data
records that level. It is not evidence of a natural drop below campaign area
level 68; neither is seeing one in the Studio. Special rewards and manually
dropping an owned item are different questions from natural acquisition. [2]

**Maps themselves need campaign coverage.** GGG introduced Tier 1 map drops at
level 58 in 2.4. The exact threshold is historical evidence rather than a new
3.29 verification; the implementation consequently does not enforce a minimum
drop level. Its campaign-only `Maps` rules cover any map actually encountered,
including unusual rewards. [3]

**Available visual channels are finite.** The official filter reference offers
twelve minimap shapes, eleven named icon/beam colours, three icon sizes and
font sizes 1–45. This supports a small vocabulary of purposes rather than a
unique shape for each league. The choice of vocabulary is a project design
decision, not a GGG recommendation. [4]

## Repository audit and decisions

The mapping CSV categories add ten distinct names beyond the campaign currency
categories: Orb of Unmaking and nine Scouting Reports. Existing campaign
sections already cover configured scarabs, fragments and league supplies.
The script did not have an ordinary-map rule. These findings come from the
local configuration, not a claim about every current game item.

The implementation adds supplemental mapping categories after the campaign
currency categories, preserving first-match priority, and adds campaign-only
map rules. Legacy rows remain styleable. No endgame-area rules are added.
Channel ownership and exceptions are recorded in `configuration/CHANNELS.md`.

The Studio samples configured currency rows without acquisition restrictions.
Its random output must therefore not be used to establish where an item drops.

## Source evaluation

| Source | Type | Grade | Impact | Reason |
| --- | --- | --- | --- | --- |
| [1] GGG 3.28 patch notes | Primary | A2 | Critical | Authoritative removal announcement; no independent current reintroduction found. |
| [2] PoEDB item metadata | Primary data extraction | B2 | Critical | Direct DropLevel field; current extraction, but extraction methodology is external. |
| [3] GGG 2.4 patch notes | Primary | A2 | Supporting | Authoritative historical campaign-map threshold; not proof of an unchanged current threshold. |
| [4] GGG item-filter reference | Primary | A2 | Critical | Authoritative syntax and available display channels. |

Grades separate source reliability (A/B) from corroboration (2: consistent but
not independently verified here). The preferred-author list contains no direct
authority on PoE item acquisition or loot-filter visual design; no software
engineering author's unrelated views were substituted for game documentation.

## Gaps

PoE Wiki search results corroborated the report removal and level-70 metadata,
but full-page reads failed, so they are not cited as independently verified
sources. Special reward eligibility below level 68 was not established. The
permissive filter rules do not need to assume such eligibility. Legacy
currencies beyond this request remain in configuration and in synthetic previews.

## Sources

[1] **Content Update 3.28.0 — Path of Exile: Mirage**, Stacey_GGG / Grinding Gear
Games, official patch notes, published 26 February 2026, accessed 28 September
2026. [Read](https://www.pathofexile.com/forum/view-thread/3913392/filter-account-type/staff).
Documents the retirement of Kirac missions and Scouting Reports.

[2] **Orb of Unmaking**, PoEDB, extracted item metadata, publication date
unspecified, accessed 28 September 2026.
[Read](https://poedb.tw/us/Orb_of_Unmaking). Supplies the DropLevel 70 field.

[3] **Path of Exile 2.4.0: Atlas of Worlds Patch Notes**, Grinding Gear Games,
official patch notes, published August 2016, accessed 28 September 2026.
[Read](https://www.pathofexile.com/forum/view-thread/1716228). Records the
historical level-58 start for Tier 1 map drops.

[4] **About Item Filters**, Grinding Gear Games, official reference, publication
date unspecified, accessed 28 September 2026.
[Read](https://www.pathofexile.com/item-filter/about). Defines the display
channels and their supported values.
