# 0002: Genesis reads every syntax; bindings are generated

**Status:** Proposed. **Tiers:** Lite.

In the context of reading `.rf`, JSON, CSV and TOML in C, facing the choice between hand-written parsers and the Logos Genesis
engine, we decided that every syntax is a Genesis grammar and every tree is read through the typed views `genesis bind`
generates during the build, to keep one parsing technology and make a grammar change a compile error instead of a silent
misreading, accepting that a bit of Pratt-expression structure (operators are not rules) is read from the green tree directly
and that the standard grammars (General, CSV, JSON, TOML) are the ones Logos ships, not copies.
