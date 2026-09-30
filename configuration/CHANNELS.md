# Reading the filter

Every enabled theme uses the same channel rules. A tier means **pickup priority**:
market value where economy tiering exists, otherwise the configured usefulness
of the item during progression. It is not a claim that every item with that
tier has the same market price. Colours change with the palette; meanings do not.

| Channel | Meaning and precedence |
| --- | --- |
| Text colour and font size | Global priority tier, always. Neither rarity, subtype nor gem quality can take these channels. |
| Background | Purpose/category, including the forge field for crafting bases and semantic league fields. In Aryzel, T0/T1 radiance overrides the category field; the shape and item name still identify purpose. |
| Border | The most important property represented by the winning rule. Tier-specific radiance takes precedence; then hidden item state (quality, fractured, corruption implicits, veiled and max-roll), crafting eligibility, subtype/league, rare 5/6-link or 5/6-socket marks, and finally rarity. Ordinary links, sockets and RGB layouts are quiet reminders. |
| Beam | Priority rank for T0–T4. Links and sockets never add a beam; a temporary green beam is reserved for crafting bases. |
| Minimap colour and size | Global priority tier, including crafting. A crafting icon is a small cyan moon because crafting is T5; its green landing beam does not recolour the icon. |
| Minimap shape | Broad purpose, never rarity or price. Related consumable families intentionally share a shape. |
| Sound | T0–T2 priority alerts override category sounds. Below T2, a category sound wins when present; otherwise the tier supplies its alert or remains silent. Gem property annotations never add or replace sounds. |

The loud pink fallback is a diagnostic exception: it means “unclassified”, not
“valuable”. Rule order selects one complete style; the border hierarchy does not
combine every property from later matching rules. The Studio inspector shows
which rule won and which rules were shadowed.

## Priority ladder

| Global tier | Font size | Beam | Icon colour / size |
| --- | ---: | --- | --- |
| T0 | 45 | Permanent white | White / 0 |
| T1 | 45 | Permanent yellow | Yellow / 0 |
| T2 | 40 | Permanent orange | Orange / 1 |
| T3 | 38 | Temporary red | Red / 1 |
| T4 | 36 | Temporary purple | Purple / 2 |
| T5 | 33 | None; crafting gets the green cue | Cyan / 2 |
| T6 | 30 | None | Grey / 2 |
| T7 | 28 | None | Grey / 2 |
| T8 | 26 | None | Grey / 2 |
| T9 | 24 | None | Grey / 2 |
| T10 | 22 | None | Grey / 2 |

PoE's icon size 0 is largest. Style paths such as `Final/Orbs/T1` are local
category ranks and may map to a different **global** tier. Their template
combination is authoritative; e.g. `Final/Orbs/T1` maps to global T0.

## Shape vocabulary

| Shape | Purpose |
| --- | --- |
| Triangle | Equipment and jewellery |
| Moon | Selected crafting equipment |
| Raindrop | Flasks |
| Cross | Skill/support gems |
| Circle | Currency and crafting consumables, including essences/oils |
| Kite | Utility supplies |
| Pentagon | Divination cards |
| Diamond | Playable areas: maps, contracts, blueprints, logbooks, charts |
| Star | Area-enabling fragments, breachstones and scarabs |
| Square | Access/objective items: Labyrinth keys/trinkets and Stone of Passage |
| Hexagon | Gold |
| UpsideDownHouse | Unclassified fallback |

Specific league identity remains in the background/border, avoiding a separate
shape for every league when the game exposes only twelve shapes.

## Crafting and special items

Both normal and magic crafting bases use global T5 text and size. The forge
field, distinctive hot border and moon identify their purpose immediately and
win over the secondary link/socket reminder.
Rarity deliberately yields the border to crafting eligibility. This preserves
the visual signal without making text colour mean rarity for these items.

Essence/oil/fragment subtype colours live on borders. Their complete labels use
the common priority ladder. Black Oil has a visible charcoal border on the same
dark field as other oils; it no longer borrows the inverted top-tier treatment.
The configured global priorities are essence low/mid/high/special = T7/T5/T3/T2,
oil common/uncommon/rare/legendary = T7/T6/T4/T2, and fragment standard/small =
T3/T5. These are curated pickup ranks, not live price estimates.

Gem annotation order is **tier-specific border > Vaal > quality**. A top-tier
radiant gem retains its readable outline and alert; a lower-tier Vaal gem keeps
its Vaal border even if it also has quality. Missing tier beams/sounds remain
missing. Quality does not promote a cheap gem into another priority rank.

Links and sockets use the same restraint: 3/4-link and RGB marks keep a small
border, while 5/6-link and 5/6-socket marks retain a stronger border. Their
font, icon and alert stay below the hidden-state tiers. Max-roll never makes an
item visible; it only changes how a shown item looks. The script declares
`max_roll_precedence` before the crafting section, and from there every Show
rule that could match armour is preceded by a copy restricted to max-rolled
armour (`BaseDefencePercentile == 100` on body armours, boots, gloves, helmets
and shields). The copy keeps the rule's background and icon shape, takes the
max-roll border, and takes the higher of the two priority tiers: a max-rolled
3-link gets the full max-roll look, a max-rolled crafting base keeps its forge
field and green beam. Hide rules get no copy. Fractured and corruption-implicit
rules also win before the link/socket section.

Corruption alone never makes an item visible; it only takes away crafting,
recolouring and linking. The corrupted border marks a corruption implicit
(`CorruptedMods >= 1`) on rares the build can wear, rare rings and amulets, and
magic or rare jewels. Corrupted normal and magic gear is hidden right after the
socket layouts that keep their worth when corrupted (5/6-link, 6-socket, RGB)
and before the socket reminders and progression rules. Corrupted rares without
a corruption implicit are judged like any other rare.

## Campaign scope

The campaign's currency rules run first, then the supplemental mapping currency
categories, all below area level 68. Overlapping entries retain the campaign
priority. Maps have their own campaign-only rules before ordinary equipment
rules. Those rules are permissive about map tier so reward or manually dropped
maps also remain visible. They do not implement a filter for mapping areas.

Scouting Reports are legacy items, removed in 3.28. Orb of Unmaking has natural
drop level 70. Supplemental rules can still style an item put on the ground or
generated by the Studio without claiming it currently drops in that area.
See the [research note](../results/research/2026-09-28-0215-campaign-mapping-items.md).

The Studio's random generator is a synthetic preview. Its currency pool comes
from configuration rows, including legacy entries, and does not enforce their
actual drop restrictions. Appearance there is not evidence of campaign availability.
