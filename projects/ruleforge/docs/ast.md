# The syntax-tree JSON

`ruleforge ast entry.rf` writes the parsed modules (the entry and everything it imports) as one JSON document. It is the
interchange form of the parser: read it from any language and nothing needs to parse `.rf` again.

```json
{"format":"ruleforge-ast","version":1,"files":[{"name":"entry.rf","module":"entry","embedded":false,"ast":{...}}]}
```

Every node: `kind`, `start`/`end` (byte offsets), `line`/`column`/`endLine`/`endColumn` (1-based), and where they apply `text`,
`text2`, `value`, `ref` (`parameter`, `compiler` or `plain`), the flags `export`, `override`, `optional`, `mutable`, `alpha`, and
`children`. The kinds and their children:

| kind | text | children |
|---|---|---|
| Module | module name | Version, Import*, declarations |
| Schema | name | Field(text, Type)* |
| Dataset | name | Type (schema), String (source) |
| ThemeSchema | name | Key* |
| ColorSet | name | ColorDef(text, expression)* |
| Theme | name | [Extends], [Implements], ThemeStyle* |
| ThemeStyle | key | ThemeState("show"/"hide")(StyleProperty(text, expressions, [AlphaOverride])*)* |
| Template | name | Params, rule-body items |
| Function | name | Params, [Type], Block |
| Ruleset | name | Description, CoreConditions, statements |
| Configuration | | ConfigAssign(text, expression)*, Adapter(text, Type, AdapterMap*)* |
| Let / Assign / If / For / Return / Include / Emit / Rule / Native | | statements |
| Scope(ScopeAssign*), Baseline, Variants(When*) | | rule-body items |
| Int / Float / String / Bool / None / Color / List / Call / Arg / Ref / Index / Unary / Binary | | expressions |

A Color node's `value` is `0xRRGGBBAA` as an integer and `text` its source form, so an editor can rewrite it by span.
