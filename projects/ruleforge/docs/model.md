# The compile model and the simulation result

`ruleforge compile ... --model model.json` writes the filter as data, from the same blocks the filter text is written from.

```json
{"format":"ruleforge-model","version":1,"compiler":"ruleforge 0.1.0","language":"1.0.0","hash":"…",
 "modules":[{"name","file"}], "datasets":[{"name","source","rows","hash"}],
 "blocks":[{"id","action":"show|hide|native","role":"baseline|variant N|native","lines":[first,last],
            "rule":{"name","file","module","line","column","endLine","endColumn","shadowed"},
            "conditions":[{"name","keyword","op","values":[…]}],
            "style":{"theme","key","properties":[{"name","keyword","value"}]} | null,
            "trace":[{"kind","label","file","module","line","column","row"?,"record"?}…]}]}
```

- `lines` are the block's lines in the emitted `.filter` (1-based, the `# rf:N` comment excluded).
- `trace` runs outermost first: `call` (main, functions), `include`, `template`, `row` (the dataset record the block was generated
  from, with its fields, including `tier` and `score` when `economy.AssignTiers` ran), `transform` (a `std.rules` function) and
  finally `declaration`, the rule itself.

`ruleforge simulate entry.rf --items items.json` prints one result per item:

```json
{"index":0,"matched":3,"action":"show","default":false,"style":{…},
 "considered":[{"id":1,"failed":{"condition":{…},"actual":"Chaos Orb"}}],
 "shadowed":[7,12]}
```

`matched` is the first block whose conditions all hold (`null` when none does and the game would show the item with its
defaults); `considered` lists every earlier block with the first condition that failed and the item's actual value; `shadowed`
lists later blocks that would also have matched. Items use the manifest's condition names as keys: `baseType`, `class`,
`rarity`, `itemLevel`, `areaLevel`, `quality`, `stackSize`, `sockets`, `identified`, `corrupted`, ... String conditions match
case-insensitively against a string or an array of strings (mods, influences). Absent counts (`quality`, `sockets`) are 0.
