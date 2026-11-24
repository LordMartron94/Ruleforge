# 0003: A public mirror that vendors the libraries it builds on

**Status:** Proposed. **Tiers:** Lite.

In the context of publishing Ruleforge with releases and an updater while Nexus, Echo, Pharos and Logos live in a private
monorepo, we decided the public repository is generated (`tools/mirror/mirror.py`) as a snapshot of the monorepo at the same
relative paths with a root build file that links the libraries statically, to let anyone build it and CI build and release both
platforms, accepting that the mirror must not be edited by hand and that it publishes those libraries' source with the engine.
