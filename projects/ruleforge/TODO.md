# Ruleforge — roadmap and TODO

Decided 2026-11-22 with Mr. Hoorn. Order is dependency order, not priority: later items read what earlier ones provide.

## Decisions

- **Standard library = Ruleforge modules.** `std.*` are `.rf` files embedded in the engine and documented in place. What the language
  cannot express (floor, sqrt, trigonometry, list structure, sorting, colour channels, rule transformation) is an
  `export intrinsic func Name(...) -> T;` with no body, bound by name to C. `intrinsic` is accepted in embedded `std` modules only.
- **Conditions are declared in the language.** `export condition areaLevel { keyword "AreaLevel"; accepts int; ordered; order 10; }`.
  The built-in PoE set lives in `std.conditions` and a project imports it; a project can declare its own (new filter syntax needs no
  engine update). `order` is the position of the condition inside an emitted block. Style properties stay built in for now.
- **Colour computations belong to Blaze.** `std.color` is a documented binding to the generic C colour API; Studio uses the same implementation.
- **Class use and progression** are standard-library modules (`std.build`, `std.progression`) over datasets, not syntax.
- **PoB data and drop levels**: a managed download cache (pinned PoB commit, PoE wiki drop levels) with a path override in the project
  configuration; the host turns them into datasets.
- **Economy history**: a Docker collector image published from every release as `ghcr.io/lordmartron94/ruleforge-economy:latest` and
  `:X.Y.Z` (same SQLite file, same sync protocol, same flags and environment as the container already running). The existing
  container moves over by changing the image tag in its `.env`; nothing is pushed to the old branch tag.
- **Studio** is a live web application: nothing in it is generated from a previous compile.
- **Diagnostics are reported once per project run**, not once per output filter.
- **Logs** from Echo and the host persist to disk in rotated files; the project chooses how many are kept.

## Stages

1. Language core: `intrinsic func`, `number` type, embedded `std.*` modules in Ruleforge, declared conditions (`std.conditions`). **Done 2026-11-22**
   (ADRs 0004 and 0005); `ruleforge eval` and `ruleforge manifest` read the library and the conditions from the modules.
2. Diagnostics reported once; Echo file logging with rotation (engine flags and the studio project file). **Done 2026-11-24.**
3. Generic Blaze colour API (APCA, CIEDE2000, WCAG contrast), `std.color` bindings and Studio checks. **Done 2026-11-24** (draft ADR 0006).
4. Data: PoB provider and drop levels, `std.items`, `std.build`, `std.progression`, base-type validation (deduplicated). **Done 2026-11-24.**
5. PoE item-text grammar (Genesis), `ruleforge item`, pasted-item inspector, seeded catalogue loot preview. **Done 2026-11-24.** Copied text preserves unknown lines; preview sampling is explicitly synthetic.
6. Theme and colour-set form editors, source-position conflict checks, create/rename/delete and property controls. **Done 2026-11-24.**
7. Economy history: compatible collector/sync, `std.economy` history, Docker image and release automation, compose and migration documentation. **Done 2026-11-24** (draft ADR 0007). Image built and tested locally against a legacy SQLite database; version/latest publication happens on release.

## Verification 2026-11-24

- Focused CMake builds succeeded. All 83 standard-library cases passed, the compile/simulation golden and diagnostic cases passed,
  and the copied-item test passed after correcting the generated root accessor (LF, CRLF and rejection cases).
- Blaze passed all 34 published CIEDE2000 vectors, symmetry, WCAG, APCA, D65 and alpha checks. Echo retention regression passed.
- The calculator LSP regression now passes in under two seconds: name searches prefilter files from unrelated languages.
- Go tests and vet passed; browser checks exercised theme property editing, copied items and seeded loot in dark mode.
- Docker image built and passed a non-root/read-only smoke test, authenticated protocol v1, health checks and legacy-database migration.
- Exact database compiler commands and full clang-tidy diagnostics were run. New findings were corrected and focused checks pass;
  pre-existing include-cleaner diagnostics remain in the broader owning-library checks (including Nexus-owned NULL). No
  blanket warning-clean claim is made. Windows/arm64 execution and actual release publication were not performed locally.
  The host has no Docker Compose CLI plugin; deployment YAML was parsed, and the image was exercised directly with Docker.

## TODO for later

- **Form editors for scripts in the studio.** Themes and colour sets get forms (stage 6); scripts do not, because the language
  (modules, functions, templates, loops, variants) is now too rich for a form per construct. Revisit when the language settles; an
  outline view (declarations, jump to definition, rename) is probably a better first step than forms.
- Dataset column mapping (`dataset D: Schema from "src" { field = column; }`), proposed in DESIGN.md § 5.2.
- Style properties declared in the language like conditions (`property Beam { ... }`).
- The simulator's four "zero when absent" conditions (quality, sockets, linkedSockets, corruptedMods) are named in C; declare that as a
  property of the condition (`absent 0;`).
- Language server: names are shared by all files by text, so a parameter reference `$value` (one token with its `$`) does not match
  the declaration `value`; split the `$` from the name in the grammar to make rename follow parameters.
