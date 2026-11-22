# 0005: Game conditions are declared in the language

**Status:** Proposed. **Tiers:** Lite.

In the context of a filter language that tracks a game whose filter syntax gains conditions with each league, facing a built-in C table
of 46 conditions that only an engine release could extend, we decided conditions are declared with `condition name { keyword "..."; accepts
int|string|bool|enum {...}; ordered; order N; }`, that the Path of Exile set lives in `std.conditions` (a project imports it), and that
`order` places a condition inside an emitted block, to let a user add a new condition in their own module the day it appears, accepting
that a script must now import the module that declares the conditions it uses and that the simulator still special-cases the four
conditions the game counts as zero when absent (to be a declared property). Style properties stay built in; declaring them the same way is
the next step.
