# FilterBlade 3.30 and Ruleforge — Research Report

## Scope

**Question:** Which changes in NeverSink's current PoE 1 filter should inform Ruleforge's MartronAradaCampaign and ThunderGod filters, particularly unidentified item tiers, Viscera, and economic memory?

Reviewed on 19 November 2026: upstream release information, generated filters and public FilterBlade configuration, GGG's specifications, both local scripts, the shared active CSV, economy implementation, Studio, and the existing SQLite history. This is a comparison and proposed work order, not an implementation. Existing working-tree changes were preserved. No Ruleforge build, filter regeneration, Go tests, or in-game validation ran.

Upstream reference: `NeverSinkDev/NeverSink-Filter` commit `7c8f374b802564aa265a00061a0cd1d009afc0fb`, whose generated filters identify themselves as **8.21.0e**. GitHub's latest tagged release remains **8.21.0a**, published November 13. The branch changelog includes subsequent fixes. Public-assets reference: `03a3feadb1d9feba4d4b1c4893f99cc58834203b`. Local HEAD: `736fb5c379dd61fce7166ab9799471cb37388fff`, plus the existing substantial uncommitted work. [1–3, L]

## Executive summary

The most valuable improvement is completing the integration of item tiers into **rule priority**, rather than adding the condition again. Both scripts already recognize `UnidentifiedItemTier`, but campaign cutoffs and mapping jewellery rules can hide useful tiered drops before those exceptions apply. The shared Viscera configuration already matches all 164 names in NeverSink's three populated groups exactly. Several other new 3.30 items have no explicit local classification. Studio cannot currently evaluate item tiers and assigns incorrect classes to simulated Threads and Viscera. Ruleforge already implements historical economy smoothing, but it lacks explicit observation confidence, stale-item treatment, and tier hysteresis. NeverSink publicly describes the purpose of economic memory; the exact algorithm was not available in the sources examined. [1–8, L]

## 1. What to take from the current update

The changelog's relevant direction is a matrix of unidentified tier and base desirability, consolidated exotic bases, economy-aware map consumables, and more selective endgame visibility. Its follow-up fixes matter: low unidentified-tier items became stricter, and Fated gear lost its extra strictness exemption. Preserve the structural lessons while choosing thresholds for these build-oriented campaign/early-map filters. Copying trade-farming strictness wholesale would conflict with their existing purpose. [1, L]

### Item tiers: correct the interaction with existing hides first

GGG introduced tiers from Act 6 for magic and rare drops; higher tiers improve identification outcomes and influence bases, quality and sockets. Tier 5 can add an item level or produce fractured magic gear. These are probabilities, not guarantees of useful affixes. Some special bases are tier-gated in the core pool; GGG specifically gives Stygian Vise at tier 5 and Steel Ring at tier 3 or higher. [6]

**Local findings, present in both scripts:** [L]

| Finding | Concrete consequence | Recommended change |
|---|---|---|
| Campaign `Magic Gear Cutoff` hides remaining weapons/armour above area 23 without checking item tier. | An otherwise unmatched, build-usable tier-4 or tier-5 magic drop in Act 6+ can be hidden. Earlier socket/property rules save some drops, not every tiered drop. | Put campaign tier decisions ahead of generic cutoffs; also review the earlier off-class and corrupted-item cutoffs. |
| Mapping jewellery hides magic rings and amulets before `Mapping Gear`. | A magic tier-5 Helical Ring below item level 84, or Simplex Amulet below 85, misses the high-item-level crafting rules and then hides. | Protect selected exotic jewellery independently of `@class_use`, before jewellery hides. |
| The map tier-5 magic exception uses `@class_use`. | That macro expands to weapon and armour selections; it includes belts as armour but does not cover rings or amulets. | Define explicit jewellery and special-class policy. Keep build affinity as a separate input. |
| Tier-4/5 rares receive the generic equipment style, and earlier rare/jewellery rules can win first. | Visibility may be correct while the new tier remains visually indistinguishable. | Decide pickup priority from tier plus base desirability, then compose any property marker. |

The existing generated Aryzel and General filters also contain the late tier exceptions and earlier jewellery/campaign rules. That corroborates source order without regenerating outputs; it does not prove the game accepts or executes the filters correctly. [L]

NeverSink's Soft filter uses multiple base ranks crossed with item tiers, plus special treatment for fractured/veiled items and certain classes. This is more informative than one blanket threshold. Its rule labels are not sufficient evidence by themselves: a generated rule labelled `any4` actually tests tier 5 in the inspected output. Read predicates, not labels. [4]

**Proposed Ruleforge policy, not a claim about NeverSink's exact choices:**

- During the campaign, retain useful build bases at modest tiers and prioritize tier-4/5 upgrades before generic hides. Preserve deliberately chosen compact vendor rares.
- In maps, cross item tier with the existing base rankings and build affinity. Use a small protected list for expensive/special bases regardless of build, including magic jewellery.
- Consider tiered jewels, utility flasks and Heist equipment explicitly. Their current broad show rules may already preserve them, but do not communicate their tier.
- Keep game item tier, the CSV's category rank, and Ruleforge's global pickup priority distinct. `configuration/CHANNELS.md` already defines global priority as a visual vocabulary. A game tier of 5 should not automatically become global T5. [L]

### Viscera: the published base grouping is already present

Static set comparison of the active `basetype_automation_config_new_theme.csv` with NeverSink's Regular filter produced **zero missing names, zero extra names, and zero cross-group differences**: [3, L]

| Local Wickerman category rank | NeverSink populated rank | Names |
|---|---|---:|
| T1 | T2 | 17 |
| T2 | T3 | 14 |
| T3 | T4 | 133 |
| Total | | 164 |

NeverSink's outer T1/T5 slots are unpopulated/commented in this inspected filter. The difference in numbering does not indicate that Ruleforge lacks a populated tier. Both local scripts also show the entire `Viscera` class as a fallback. [3, L]

Consequently, **do not retier all Viscera merely to match the update**. Useful follow-ups are a separate build-preference list, an identifiable unclassified-Viscera fallback, and eventually reliable economic/variant data. Current local rows only express base name, stack threshold, rarity, style and visibility; they cannot distinguish item-level or modifier variants. The inspected NeverSink Viscera rules also classify by base name, so they supply no verified variant-sensitive algorithm to copy. Do not invent `Greater ...` base names without checking actual copied items and filter-visible properties. [3, L]

### Missing named items and static consumable tiers

The active CSV already contains all ten mechanic Threads, Frayed Thread, Royal Visage and Cryptic Astrolabe. The following GGG-listed additions are absent from both that CSV and the currency profiles: **Essence of Trickery; Allflame Embers of the Cloister, Multitude and Divine; Soul Core of Instability; Immortal, Feral and Royal Astrolabes; Afarud Enshrouding Crystal; Cursed Poppet.** These should receive explicit categories/styles. The final show fallback prevents an automatic conclusion that they are hidden; the problem is unclassified presentation. [5, L]

The local economy fetch list covers uniques, gems, cards, tattoos, Currency, DeliriumOrb and Omen. It has no dedicated Threads, Allflame, Viscera, fossil, oil or essence feed. Those CSV categories remain static even as the market changes. Adding another history store would not solve this coverage gap. Verify current provider categories and response shapes before wiring new feeds; do not assume a category name or endpoint. [L]

Divine Vessel and Allflame Embers became map-enchantment currency. The active CSV still places Divine Vessel under its organizational `Map Fragments` category. That label does not break compilation because `handle_csv` emits base-name predicates, not a game-class predicate. It does mislead category-based simulation and makes a dedicated map-enchantment policy useful. Royal Visage is already listed and should join that family. [6, L]

For Threads, adopt deliberate stack promotion before singleton rules; the existing rows assign one fixed rank to ten mechanic Threads and another to Frayed Thread. Their prices/usefulness need not stay equal. Preserve the fragment purpose icon for mechanic Threads, while Frayed Thread is currency. [5, L]

### Fated gear and map properties

`Fated` is an official boolean condition, but is absent from Ruleforge's condition catalog, lexer and compiler mapping. Treat it as a property marker while retaining the equipment pickup decision. For these filters, use channel composition consistent with existing max-roll and corruption precedence; copying an unconditional early Show would bypass later visibility decisions. The current changelog explicitly withdrew the launch-era strictness bonus. [1, 5, L]

Maps currently receive tier/rarity styles without special-mod exceptions. NeverSink's Strict filter has earlier identified-map rules for special encounter modifiers and corrupted maps with seven or more explicit modifiers. Add selective map-property rules ahead of the generic map rules if those drops deserve extra attention here. Every local map already shows, so this is a prioritization improvement. Mod-count support must be checked separately from simple mod-name matching. [7, L]

Update the Sacred Orb crafting profile: it currently describes armour-defence rerolling only, although GGG added a Sacred Orb cost to weapon prefix/suffix locking. Remove the obsolete Orb of Binding profile from the current-league pool, or isolate legacy data explicitly. [5–6, L]

## 2. What economic memory actually means in the public evidence

**Confirmed:** NeverSink says the new system improves treatment of low-volume trades, alongside cleaned-up item metadata and revised early-league tiers. **Not confirmed:** its retention period, decay function, confidence thresholds, treatment of missing observations, promotion/demotion asymmetry, or whether it stores prices, tiers, or both. [1–2]

The generated filters contain final decisions, not their economic estimator. The inspected filter repository tree has no generator source candidates, public-assets searches found no economic-memory implementation, and FilterBlade's own FAQ says its website is closed source. This supports reporting an implementation gap, not guessing an algorithm from the feature name. “It uses a 14-day EWMA” or “it remembers the maximum price” would be unsupported claims. [3, 8]

**Inference:** it is intended to let previous economic evidence temper sparse current observations. The public announcement is compatible with several implementations; it does not identify which one was used. An independent source for its internal mechanics was not found. [1–2]

## 3. Ruleforge already has memory; improve what the memory means

### Existing implementation

`economy_history.go` stores timestamped per-league snapshots in SQLite. `EconomyHistory` loads the complete retained history. `item_tiering.go` combines recency-weighted price percentiles/medians and average listing counts, blends a base's general value with its highest-value named item, normalizes values and rarity, and assigns deterministic one-dimensional k-means tiers. It also has an optional Theil–Sen/Kendall trend signal. [L]

Current configuration: **14-day value half-life**, value/rarity/trend weights **0.65/0.35/0**, chase/general blend **0.85/0.15**; league weights are Hardcore Covens 0.50, Covens 0.30, Hardcore 0.15, Standard 0.05. Currency additionally uses 40% crafting utility. Trend computation exists, but its configured contribution is zero. [L]

A read-only SQLite query found: [L]

| League | Snapshots | First recorded UTC | Latest recorded UTC |
|---|---:|---|---|
| Covens | 5 | Nov 14, 04:54 | Nov 18, 01:18 |
| Hardcore Covens | 5 | Nov 14, 04:54 | Nov 18, 01:18 |
| Standard | 34 | Sep 26, 15:14 | Nov 18, 01:18 |
| Hardcore | 34 | Sep 26, 15:14 | Nov 18, 01:18 |

Allflame histories also remain stored, but are not in the current configured league mix. A refresh interval of one hour is an eligibility check when Ruleforge runs; it is not a background sampler. The current new-league history therefore spans four days with five recorded snapshots, not hourly continuous observation. [L]

### Weaknesses exposed by the low-volume question

1. **Trade volume and listing supply are conflated.** Exchange parsing divides `volumePrimaryValue` by `primaryValue`, truncates the estimated units to an integer, and writes that into both `Count` and `ListingCount`. Normalization rewards lower listing counts as greater rarity. Low exchange activity can consequently increase priority. The code itself describes the conversion as an approximation; it is not an observed stash-listing count. Preserve the provider's volume measure and source kind separately. [L]

2. **The “minimum listings” gate counts stored price observations.** `calculateRobustValue` checks `len(values) >= 20`, not each observation's `ListingCount`. With enough repeated snapshots or variants, a thin market reaches the 95th-percentile branch. Repeated quotes are not proof of independent trades. Add actual evidence-quality gating and use elapsed-time buckets so frequent local runs do not masquerade as stronger market evidence. [L]

3. **Recency weighting does not provide a stale-item state.** An item absent from recent snapshots still has its old observations in the aggregation. If only one old quote remains, multiplying its weight by a smaller number does not change the resulting weighted median. Keep `lastSeen` and `lastReliableSeen`; report stale historical value separately from current evidence. Freezing the estimator's reference time to the newest snapshot helps reproducibility but does not certify freshness. [L]

4. **Tier stability is not persisted.** The schema stores observations, not previous decisions. Each run reclusters the score population; deterministic clustering prevents randomness but not boundary movement. Add separate promotion/demotion criteria, or another explicitly chosen stability policy, if stable sounds/visibility are the objective. [L]

5. **Observation identity is too coarse for some future feeds.** The history schema lacks source kind, API item ID, item level, links, corruption, quality and variant flags. Prices are eventually pooled by base name and named item. Before introducing Viscera variants or refining uniques/gems, retain enough identity to keep non-comparable observations apart. A useful unidentified-base policy may intentionally combine possibilities later; it should not lose the distinctions during ingestion. [L]

6. **Permanent-league history crosses the 3.30 rules change.** Standard and Hardcore history begins in September, and those leagues have a combined 20% configured influence. Fourteen-day decay is helpful but does not distinguish a renamed/reworked item or a changed generation regime. Preserve raw history; choose explicitly which patch regimes can influence current estimates. Revalidate the chase-heavy 0.85 blend and 0.35 rarity weight against the intended hardcore/self-found priorities. [L]

### Recommended design direction

Retain SQLite and raw snapshots. Extend observation identity and source semantics before changing the estimator. Then compute a current estimate, a historical fallback, and an explicit confidence/freshness state. Promote on credible repeated evidence; retain a conservative prior decision during short data gaps; let sustained reliable cheap observations demote an item. Unknown new items should receive an explicit fallback rather than an invented price of zero. These are recommendations for Ruleforge, **not a reconstruction of NeverSink's private system**. [L]

Keep temporal evidence separate from cross-league preferences: mixing Standard and Hardcore with Covens is a user weighting decision, not evidence that a low-volume Covens price has been corroborated by more Covens trades. Expose the reason for a tier in Studio, including leagues used, observation age, measured supply/volume and any fallback. [L]

## 4. Studio must be able to validate the new rules

Static inspection found three directly relevant discrepancies: [L]

- `UnidentifiedItemTier` is absent from Studio's numeric evaluator. It returns unknown for this condition and excludes those blocks from known matches. Generated test drops also have no item-tier field.
- The pasted-item parser recognizes exactly `Unidentified`; `Unidentified (Tier 5)` is not recognized by that predicate, so such text would leave `identified` true. Add explicit parsing and inspector display for the tier, plus Fated state when supported.
- `STACKABLE_CLASS` defaults unknown CSV categories to `Stackable Currency`. `Wickerman` therefore simulates as currency instead of Viscera, and `Threads` becomes currency instead of Map Fragments. Frayed Thread really is currency, so mapping the entire Threads category to fragments would also be wrong. Use per-item class metadata or an explicit exception.

These findings concern the simulator, not proof of an in-game filter failure. Fix them before treating Studio screenshots as evidence that tier or Viscera rules work.

## 5. Proposed work order and acceptance cases

| Priority | Work | Acceptance evidence to obtain during implementation |
|---|---|---|
| 1 | Protect valuable tiered gear before generic hides in both scripts. | Campaign tier-4/5 magic build item survives; map tier-5 magic Helical/Simplex below the old crafting threshold survives; selected off-build exotics remain intentional. |
| 1 | Add missing named 3.30 items and correct simulator metadata. | Each listed new consumable receives a deliberate style; a new unknown Viscera reaches its class fallback; ten Threads and Frayed Thread have correct classes. |
| 2 | Add an explicit tier/base matrix and Fated property treatment. | Tier-2 ordinary gear does not flood maps; high-tier good bases are distinguishable; Fated composition preserves chosen visibility and channel priority. |
| 2 | Make Studio understand tiered pasted and generated items. | `Unidentified (Tier 5)` remains unidentified; tier predicates evaluate correctly; unsupported properties remain visibly unknown. |
| 3 | Extend economy category coverage and evidence semantics. | Real provider responses establish the new endpoints; exchange turnover cannot masquerade as listing supply; thin, stale and missing observations follow explicit policies. |
| 3 | Stabilize economy decisions and expose explanations. | Repeated sparse quotes cannot establish false confidence; missing data does not become free/zero value; sustained trustworthy changes can still move tiers. |
| 4 | Add map-property highlights and tune later-map strictness. | Special maps win before generic map styling; ordinary campaign supplies remain useful. |

These are proposed checks, not tests executed during this research.

## Key findings

1. Fix rule priority around item tiers before expanding strictness.
2. Viscera's 164-name grouping already matches the inspected upstream filter exactly.
3. Missing 3.30 consumables need explicit classification; they currently rely on fallback visibility.
4. Ruleforge already has temporal economy memory, with important confidence and freshness gaps.
5. NeverSink's exact economic-memory algorithm remains unverified from public evidence.
6. Studio currently cannot validate several of the new conditions and item classes faithfully.

## Open questions and gaps

- NeverSink's memory formula and parameters were not published in the examined release notes, generated filters, configuration or FAQ. No independent confirmation of those internal mechanics was found.
- Live poe.ninja API requests could not be retrieved through the web tool. New category availability, exact field semantics and current prices are not claimed as verified. No current-price tier table was fabricated.
- Viscera item-level/modifier distinctions need real item text and verified filter-visible predicates before variant-sensitive rules can be specified.
- This comparison is about the active shared CSV. The disabled original theme's separate legacy CSV was not treated as an active filter target.
- The preferred-authors catalog has no loot-filter/economy specialist. Targeted searches for adjacent listed practitioners Eskil Steenberg and Casey Muratori found no relevant public position; no authority was inferred from unrelated programming commentary. The list was not changed.
- Validation is limited to source inspection, static CSV/name comparisons, existing generated-output inspection and read-only SQLite queries. No implementation, build, project test, regeneration, profiler or game execution occurred.

## Source Evaluation

Grades distinguish authority from independent corroboration. `A2` indicates primary evidence that is plausible/directly inspectable but whose full claim lacks independent confirmation; artifacts from the same upstream author are not independent sources.

| # | Source | Type | Grade | Impact | Why |
|---|---|---|---|---|---|
| 1 | NeverSink branch changelog | Primary | A2 | Critical | Current fixes and stated memory purpose; no estimator details. |
| 2 | GitHub latest release metadata | Primary | A2 | Supporting | Separates tagged release from newer branch output. |
| 3 | NeverSink Regular generated filter | Primary | A2 | Critical | Actual Viscera grouping, Fated decorator and version header. |
| 4 | NeverSink Soft generated filter | Primary | A2 | Critical | Actual tier/base matrix and exceptions. |
| 5 | GGG 3.30 filter announcement | Primary | A1 | Critical | Official keywords, names and classes; supported by current generated filters. |
| 6 | GGG 3.30 patch notes | Primary | A1 | Critical | Tier mechanics and currency changes corroborated by filter announcement/output. |
| 7 | NeverSink Strict generated filter | Primary | A2 | Supporting | Concrete special-map predicates. |
| 8 | FilterBlade public FAQ | Primary | A2 | Critical | Own statement of closed-source status; no memory algorithm. |
| L | Current Ruleforge sources, outputs and DB | Primary | A2 | Critical | Direct local evidence; source-derived behavior is not runtime proof. |

## Sources

[1] [NeverSink filter changelog](https://github.com/NeverSinkDev/NeverSink-Filter/blob/7c8f374b802564aa265a00061a0cd1d009afc0fb/CHANGELOG.md) — NeverSink — GitHub — release notes — 8.21.0 through 8.21.0e; accessed 2026-11-19. Documents the advertised changes and subsequent corrections.

[2] [Latest tagged release](https://github.com/NeverSinkDev/NeverSink-Filter/releases/tag/8.21.0a) and [GitHub release metadata](https://api.github.com/repos/NeverSinkDev/NeverSink-Filter/releases/latest) — NeverSink/GitHub — release record — published 2026-11-13; accessed 2026-11-19. Establishes the tagged release and memory announcement.

[3] [Regular filter at inspected commit](https://github.com/NeverSinkDev/NeverSink-Filter/blob/7c8f374b802564aa265a00061a0cd1d009afc0fb/NeverSink%27s%20filter%20-%201-REGULAR.filter) — NeverSink — GitHub — generated filter source — version 8.21.0e, commit dated 2026-11-15; accessed 2026-11-19. Viscera section around lines 6916–6977, Fated decorator around 3551–3558.

[4] [Soft filter at inspected commit](https://github.com/NeverSinkDev/NeverSink-Filter/blob/7c8f374b802564aa265a00061a0cd1d009afc0fb/NeverSink%27s%20filter%20-%200-SOFT.filter) — NeverSink — GitHub — generated filter source — version 8.21.0e; accessed 2026-11-19. Supplies the item-tier matrix predicates.

[5] [Covens of Umbra Item Filter Information](https://www.pathofexile.com/forum/view-thread/4013923) — Natalia / Grinding Gear Games — official announcement — published 2026-11-11; accessed 2026-11-19. Defines keywords and item-name/class changes.

[6] [Content Update 3.30.0](https://www.pathofexile.com/forum/view-thread/4013680) — Grinding Gear Games — official patch notes — November 2026; accessed 2026-11-19. Item Generation Changes and Endgame Changes establish mechanics used in this comparison.

[7] [Strict filter at inspected commit](https://github.com/NeverSinkDev/NeverSink-Filter/blob/7c8f374b802564aa265a00061a0cd1d009afc0fb/NeverSink%27s%20filter%20-%203-STRICT.filter) — NeverSink — GitHub — generated filter source — version 8.21.0e; accessed 2026-11-19. Supplies special-map predicates.

[8] [FilterBlade FAQ source](https://github.com/NeverSinkDev/FilterBlade-Public-Assets/blob/03a3feadb1d9feba4d4b1c4893f99cc58834203b/FbSharedConfigs/FAQ_Content.json) — FilterBlade team — GitHub — official site content — revision dated 2026-11-16; accessed 2026-11-19. The open-source question states website implementation availability. Public configuration and repository-tree searches did not expose the estimator.

[L] Ruleforge local evidence — inspected 2026-11-19:

- `__filters__/martron_arada_campaign.rf`: league mechanics 120–201, maps 307–370, crafting 390 onward, jewellery 638–660, campaign cutoffs 726 onward, mapping gear 806 onward. `__filters__/thunder_god.rf` has the corresponding rules, offset by four lines in most later sections.
- `configuration/basetypes/basetype_automation_config_new_theme.csv`: Threads 308–318, Allflames 414–424, Astrolabes 434–441, Wickerman 473–636, crafting lists thereafter. `configuration/currency/currency_profiles.csv`, `configuration/CHANNELS.md`, `config.toml`, and style files identify active configuration.
- `ruleforge/components/ruleforge/data_generation/economy_history.go`: schema 20–40 and recording/loading methods. Existing `.ruleforge-cache/economy_history.db` opened with SQLite `mode=ro` for aggregate counts/timestamps only.
- `ruleforge/components/ruleforge/data_generation/item_tiering.go`: scoring 310–360, clustering 362–442, trends 510 onward, aggregation 655–733, decay 747 onward, robust-value gate 819–827.
- `ruleforge/components/ruleforge/data_generation/poe_client.go`: response identity fields 17–51, exchange transformation 181–209. `path_of_building_exporter.go`: economy categories 246 onward, refresh/loading methods thereafter.
- `ruleforge/components/ruleforge/compilation/rule_generator.go`: economy parameters 924–945, CSV generation 1140 onward, `@class_use` expansion 1350–1450. Condition catalog, lexer and compiler mapping checked for item tier/Fated support.
- `ruleforge/components/ruleforge/entry/studio/studio.html`: class mapping 540, condition evaluation 681–744, paste parser 748–827. Existing `__output__/MartronAradaCampaign (Aryzel I - The Stranded Prince).filter` and `__output__/ThunderGod (General).filter` inspected for rule ordering.
