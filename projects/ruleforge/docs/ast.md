# The syntax-tree JSON

`ruleforge ast entry.rf` writes the parsed modules (the entry and everything it imports) as one JSON document. It is the
interchange form of the parser: read it from any language and nothing needs to parse `.rf` again.

```json
{"format":"ruleforge-syntax","version":1,"files":[{"name":"entry.rf","module":"entry","embedded":false,"tree":{...}}]}
```

The tree is written by the bindings that `genesis bind` generates from `spec/ruleforge.gen`, so it has no hand-made node kinds:

- a rule node is `{"rule":"<rule name>","start":N,"end":N,"children":[...]}`;
- a token is `{"token":"<token name>","start":N,"end":N,"text":"..."}`.

Rule and token names are the grammar's own (`theme_stmt`, `kw_theme`, `identifier`, ...), `start`/`end` are byte offsets into the
file, and comments and whitespace are not part of the tree. Because the grammar is the only place a name is written, the tree and the
engine cannot drift apart.

## Interpretations

What the syntax *means* is the engine's job, not the reader's. A consumer that needs an interpretation asks the engine for it
instead of walking the tree by hand:

| command | document |
|---|---|
| `ruleforge themes entry.rf --json` | `{"format":"ruleforge-themes",...}`: every theme with its styles, states, properties and arguments; every color set |
| `ruleforge compile ... --model out.json` | the structured filter model with provenance and traces |
| `ruleforge manifest` | everything the engine can express |

### Themes

```json
{"format":"ruleforge-themes","version":1,
 "themes":[{"name":"DefaultTheme","module":"filters.themes","file":"./filters/themes.rf","extends":null,
   "styles":[{"override":false,"key":"godTier","states":[{"state":"show","properties":[
     {"name":"Background","arguments":[{"kind":"reference","start":332,"end":344,"text":"Palette.GOLD"}]},
     {"name":"Border","arguments":[{"kind":"color","value":4293949695,"alpha":false,"start":365,"end":379,"text":"r255 g240 b120"}]}]}]}]}],
 "colorSets":[{"name":"Palette","file":"./filters/themes.rf","colors":[{"name":"GOLD","value":{"kind":"color",...}}]}]}
```

An argument's `kind` is `color` (a color literal; `value` is `0xRRGGBBAA` and `alpha` says whether the source wrote an alpha),
`reference` (a name such as `Palette.GOLD`) or `expression` (anything else). `start`/`end` are byte offsets in `file`, so an editor can
rewrite a color in place without parsing anything.
