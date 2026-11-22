# 0001. Systems are identified by their handle, not their label

## Status
Accepted

## Context and Problem Statement
Echo required the fully-joined label of every registered system to be unique, and refused a duplicate with `ECHO_ERROR_SYSTEM_DUPLICATE`. Every library carries the same Runtime boilerplate, which registers a system under its own fixed label (`"Ordo"`, `"Splash"`, ...) and discarded the result of that call. A program that creates two runtimes of one library under the same parent (two Ordo instances, two renderers) therefore got a NULL system and a crash in the next call, with no message pointing at the cause. Should the second registration be refused, renamed, or allowed?

## Decision Drivers
- **Libraries are instantiable.** Nothing in a library's Runtime says there is only one of it; a program may need several, and shared libraries such as Ordo exist to be used more than once.
- **One failure mode, not two.** The failure should not depend on the caller remembering to check a result every library's boilerplate currently ignores.
- **Level control stays predictable.** Changing the level of a system must reach what is below it and nothing else.
- **No breaking change to every Runtime.** Changing every library's `*_runtime_create` signature is a large, repetitive edit.

## Considered Options
1. Keep refusing duplicates, and make every library's Runtime report the failure (a returned `NexusError`, callers choose a unique parent).
2. Keep unique labels, but let Echo disambiguate on registration (`Ordo`, `Ordo#2`, ...).
3. Allow duplicate sibling labels; a system's identity is its handle.

## Decision Outcome
Chosen: option 3. `echo_runtime_system_register` no longer fails on a duplicate label and `ECHO_ERROR_SYSTEM_DUPLICATE` is removed. The tree's structure is carried by a parent pointer in each system, so level changes (`echo_system_level_set`) walk the parent links and reach exactly the systems registered below the one named, never a same-named sibling. Unregistering a system re-parents whatever is still registered below it to its parent. Records carry the joined label as before, so two same-named siblings show up under the same name.

### Pros and Cons of the Options
- **Option 1.** Pro: Echo's identity rules do not change and the failure is explicit. Con: a breaking signature change in every library, and a program that legitimately wants two instances must invent unique parents or labels for them.
- **Option 2.** Pro: labels stay unique and no signature changes. Con: the label a library asked for is not the one that appears, which surprises anyone filtering by it, and a suffix scheme is one more rule to remember.
- **Option 3.** Pro: it matches what the callers mean, no signature changes, no renaming. Con: two same-named siblings are indistinguishable in a log line, and a filter on a label matches every sibling carrying it.

### Consequences
- A sink or filter that selects by system label now sees every same-named sibling. That is the intended reading of "the Ordo logs", but anyone who needs one instance's output apart must give it a different label or parent.
- Level changes were previously decided by label prefix and are now decided by parent links, which is the same for every tree that had unique labels and correct for one that does not.
- A system whose parent is unregistered first keeps its place under the grandparent, where it formerly kept it through its label.
