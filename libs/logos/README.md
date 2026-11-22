# Logos

A fresh shared C library for language tooling: lexing, parsing, and grammar
recognition. See [DESIGN.md](DESIGN.md) for the ownership boundary.

Regula provides byte-language expressions, character classes, and regexp
string import/export alongside the standard Echo-backed Runtime.
Contexta adds context-free grammars with declared nonterminals and recursive
productions, over the same byte alphabet.

Consumers include `logos.h` and link `Logos::Logos`. Public functions use
`logos_`; implementation files use `l_`. Nexus and Echo are dependencies.

Create Logos with a caller-owned Echo runtime and an optional parent system.
Its constructor returns the owned `LogosRuntime *` directly; required Echo
setup failures panic under the workspace Runtime-construction policy.
Getters expose borrowed system/context handles. Release all borrowed users,
then destroy Logos before closing or destroying Echo. Logos never starts,
closes, or destroys the caller's Echo runtime.

CMake is the active workspace build. `logos.helm` records the standard shared
library adapter for reference.

From the MergeSource root:

```sh
make configure
cmake --build build/Debug --parallel 2 --target logos
python3 tools/scripts/check_c_changes.py libs/logos
```

## Regula composition

The typed `logos_languages_regula_regula_operation_add_*` calls construct and
attach in one step. Each returns the **new operation**, rather than the updated
implicit root. Read `regula->root_rule` when the whole expression is needed.
This changes the earlier return-value contract.

Symbols, ranges, masks, literals, and imported regexps append automatically.
Star, plus, optional, and repeat replace the supplied active expression in its
current position. Thus adding `a`, adding `b`, starring the returned `b`, and
adding `c` builds `ab*c`. Applying star immediately to the first `a` builds
`a*`.

Union and explicit concatenation consume two adjacent active expressions. They
preserve any prefix and suffix outside those operands, even when the operands
are not immediate siblings of the implicit concatenation tree. Operand order
determines the resulting expression's order. Identical operands are allowed;
other overlaps and combinations across intervening expressions assert.

To build `(ab|cd)*`, add `a` and `b`, explicitly concatenate their returned
handles into `ab`, add `c` and `d`, explicitly concatenate those into `cd`,
union `ab` with `cd`, then star the returned union. Every call attaches
internally; no separate client attachment step is needed.

Transformation and combination consume their operand handles for assembly.
Use the newly returned handle afterward. Descendants inside completed compounds
cannot be targeted independently. Their nodes remain owned by the language
and retain stable addresses until language destruction. Only implicit assembly
edges are rewritten; shared expression interiors are preserved.

An `LRegulaObservationMask` covers all 256 bytes. Clear it, set individual bytes
or half-open ranges with the mask helpers, then pass it to
`logos_languages_regula_regula_operation_add_symbol_mask`. Its `negated` flag
complements the mask over the byte alphabet. For example, combine ranges
`['a', 'z' + 1)`, `['A', 'Z' + 1)`, and `_` into one symbol node. An exclusive
range end of 256 includes byte 255; `[0, 256)` is a wildcard. Caller labels are
assigned to completed literal, optional, and repeat expressions, rather than
their generated intermediate nodes.

## Regexp strings

`logos_languages_regula_regula_operation_add_regexp(label, regula, regexp,
out_operation)` accepts a NUL-terminated string and returns a `NexusError *`.
Compare its error code with the Nexus success code. On success, the new
expression is appended automatically and returned through `out_operation`.
On a returned error, the existing language and its operation count are
unchanged, and the output handle is NULL. The output location must not alias
language-owned state.

Supported syntax includes byte literals; grouping `(...)` and `(?:...)`;
alternation `|`; concatenation; `*`, `+`, `?`; `{m}`, `{m,n}`, `{m,}`, and
`{,n}`; classes such as `[a-zA-Z_]`; negated classes; and dot, which accepts
all 256 bytes including newline. Repetition bounds are inclusive and at most
65535. Empty expressions denote the empty string. This byte dialect accepts
`[]` as the empty language and `[^]` as any byte. Inside a class, escape `]`
to include it. Whitespace is literal.

Escapes include `\xHH`, `\n`, `\r`, `\t`, `\f`, `\v`, escaped punctuation,
and ASCII `\d`/`\D`, `\w`/`\W`, `\s`/`\S`. `\w` includes ASCII letters,
digits, and underscore; `\s` includes space and bytes 9 through 13. `\b`
means backspace only inside a class. Byte zero is written as `\x00`.

Syntax conventions follow the relevant parts of the
[PCRE2 pattern specification](https://www.pcre.org/current/doc/html/pcre2pattern.html),
but this is a separate, explicitly bounded byte-language dialect. Grouping
does not record captures. Unsupported escapes, anchors, lookaround, flags,
backreferences, Unicode properties, POSIX classes, control verbs, and
lazy/possessive quantifiers return `GLOBAL_LOGOS_REGULA_REGEXP_UNSUPPORTED`.
Malformed syntax returns `GLOBAL_LOGOS_REGULA_REGEXP_INVALID`. Messages include
the input byte offset.

`logos_languages_regula_regula_regexp_write(regula, out_regexp)` writes the
whole root to an owned NUL-terminated string. Release it with
`nexus_memory_deallocate`. It preserves language meaning and precedence,
uses hex-escaped bytes and classes, and omits labels. The absent root writes
`[]`. Foreign nodes, missing operands, invalid kinds, and cycles return
`GLOBAL_LOGOS_REGULA_REGEXP_INVALID_GRAPH`.

Input/output text is bounded to 16 MiB, nested grouping to 256, and regexp
expansion/writer workspace to 1,048,576 operations. Exceeding a bound returns
`GLOBAL_LOGOS_REGULA_REGEXP_LIMIT`. Shared graphs can expand substantially when
written as strings. Export traverses iteratively, and allocates its stack and
output buffers geometrically. Import parses in temporary owned storage before
copying nodes and committing the completed expression once.

New methods carry `Written by an LLM` comments. The composition decision is
recorded in the proposed draft
[0001-regula-implicit-composition-and-regexp-strings](adr/0001-regula-implicit-composition-and-regexp-strings.md).

## Contexta grammars

`LContexta` owns opaque rule and expression handles. It supports empty language,
empty string, byte symbols, ranges, masks, literals, union, concatenation, star,
plus, optional, bounded/unbounded repetition, and references to nonterminals.
Masks use `LContextaObservationMask`, compatible with Regula's mask helpers.
Rules may reference themselves or other rules declared before their bodies are
defined. Left recursion, mutual recursion and ambiguity are representable;
productions denote their least languages. An unproductive cycle denotes the
empty language. The generalized parser below consumes Contexta over lexer tokens.

Construct expressions independently and combine their returned handles
explicitly. Constructors do not append to an implicit root. Expressions remain
immutable and reusable, including across rule bodies. Define each declared rule
once, and select the start rule explicitly. Labels are optional debugging text,
borrowed for the grammar's lifetime; rule identity is its handle.

For example, `S = epsilon | a S b` represents equally many `a` and `b` bytes:

```c
LContexta *grammar;
LContextaRule *start;
LContextaOperation *self;
LContextaOperation *left;
LContextaOperation *right;
LContextaOperation *body;
LContextaOperation *empty;

grammar = logos_languages_contexta_contexta_create("a^n b^n", 0);
start = logos_languages_contexta_contexta_rule_add("S", grammar);
self = logos_languages_contexta_contexta_operation_add_reference(NULL, grammar, start);
left = logos_languages_contexta_contexta_operation_add_symbol(NULL, grammar, 'a');
right = logos_languages_contexta_contexta_operation_add_symbol(NULL, grammar, 'b');
body = logos_languages_contexta_contexta_operation_add_concatenate(NULL, grammar, left, self);
body = logos_languages_contexta_contexta_operation_add_concatenate(NULL, grammar, body, right);
empty = logos_languages_contexta_contexta_operation_add_empty_string(NULL, grammar);
body = logos_languages_contexta_contexta_operation_add_unionize(NULL, grammar, empty, body);
logos_languages_contexta_contexta_rule_define(grammar, start, body);
logos_languages_contexta_contexta_start_rule_set(grammar, start);
/* Check logos_languages_contexta_contexta_validate(grammar) before use. */
/* Destroy after all consumers finish. */
logos_languages_contexta_contexta_destroy(grammar);
```

`contexta_validate` checks the selected start and every declared rule body,
including unreachable declarations. Inspection APIs enumerate rules and expose
read-only primitive operation views for grammar consumers. All handles remain
stable until grammar destruction. Construction and destruction require exclusive
access; concurrent inspection is allowed once construction stops.

`contexta_operation_add_regexp` imports the same dialect and limits as Regula.
On syntax failure it returns the Regula error, clears the output, and leaves the
grammar unchanged. Successful import creates an independent expression and
does not define a rule or change the start rule.

`contexta_to_regula` validates the grammar and substitutes acyclic references
reachable from the start, preserving shared interiors. Its owned output works
with existing lexer and automaton APIs. Reachable recursion returns
`GLOBAL_LOGOS_CONTEXTA_RECURSIVE` with a NULL output; this is a conversion
limitation, not a claim that the language is non-regular. Unreachable recursive
rules do not block conversion. Token terminals and allowed-token scopes return
`GLOBAL_LOGOS_CONTEXTA_NOT_BYTE_LANGUAGE`. `contexta_regexp_write` applies that conversion
then uses Regula's writer, returning owned text released with
`nexus_memory_deallocate`. Both conversions leave Contexta unchanged.

## Generalized streaming parsing

The parser consumes the existing lossless lexer's output. Define Contexta token
terminals with `logos_languages_contexta_contexta_operation_add_token`, using
the same `LLexerTokenKind` values registered in the lexer. Existing Contexta
byte-class terminals match one lexer token only when its lexeme is exactly one
byte in the class. A Contexta byte literal therefore expects separate one-byte
tokens; use a token terminal for a multi-byte keyword, identifier or string.
The lexer alone decides maximal munch, lexical contexts and token boundaries.

Create a `LParserConfiguration` with
`logos_language_tooling_parsing_parser_configuration_initialize`, then call
`logos_language_tooling_parsing_parser_create(lexer, contexta, &configuration,
&parser)`. The lexer must already be finalized. Compilation copies grammar
structure, labels and allowed-token sets; Contexta can then be destroyed. The
lexer must outlive the parser, and the parser must outlive its processes and
result trees. Lifecycle counters require synchronization when callers create or
release processes or trees concurrently.

A parser process owns a lexer process and an incremental Earley chart. Supply
chunks with `..._process_chunk_add`, then call `..._process_parse` with a token
budget until it reports `LPS_NEED_INPUT` or a terminal result. `LPS_PROGRESS`
means the budget was used: call parse again on the same chunk. Input is copied
once into owned source storage, so callers can immediately reuse their chunk
buffers. Tokens split across chunks are handled by the existing lexer. A prefix
that currently accepts still returns NEED_INPUT until the final chunk and EOF.
Earlier charts are retained and are not reparsed when another chunk arrives.

`LPS_ACCEPTED` means a complete derivation without diagnostics. `LPS_RECOVERED`
means a complete derivation after structural or lexical issues. `LPS_REJECTED`
means bounded recovery could not complete the source. `LPS_LIMIT` means an
internal work/storage bound was reached. All four allow
`..._process_tree_take`; the process must be reset before reuse. Reset retains
workspace capacity, and also retains output capacity if its last result was
not taken. Taken results remain independent and immutable.

### Pratt operations: precedence inside Contexta

Expression grammars with operator precedence do not need a cascade of
productions. `contexta_operation_add_pratt` adds an operation defined by a
table of `LContextaPrattEntry` values: atoms, prefix and postfix operators,
left-, right- and non-associative infix operators, enclosures (`( x )`,
`[x, y]`), applications (call `f(x, y)`, index `a[i]`) and the ternary
`a ? b : c`, each with a precedence (higher binds tighter). The operation is
an ordinary Contexta operation: put it in a rule, reference the rule, and
combine it with unions, concatenations, stars and allowed-token scopes, so one
grammar mixes Pratt and Earley rules.

```c
{LCPK_ATOM,        "number", T_NUM,   LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 0, FALSE, FALSE},
{LCPK_INFIX_LEFT,  "add",    T_PLUS,  LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 1, FALSE, FALSE},
{LCPK_INFIX_LEFT,  "mul",    T_STAR,  LCONTEXTA_PRATT_TOKEN_NONE, LCONTEXTA_PRATT_TOKEN_NONE, 2, FALSE, FALSE},
{LCPK_APPLICATION, "call",   T_LPAREN, T_RPAREN,                  T_COMMA,                    3, TRUE,  FALSE},
```

Execution. Where an Earley item waits for a Pratt symbol the parser starts one
deterministic run, which receives the same lexer tokens as the chart and keeps
an explicit stack of pending operators in a shared arena: no recursion, so a
hostile `((((...` cannot overflow the call stack, and constant work per token
instead of a chart item per precedence level. When a token cannot continue a
complete expression the run completes into the chart, advancing the items that
waited for it. Chunk boundaries, token budgets and `LPS_NEED_INPUT` behave as
for any other rule, because a run only ever needs the current token.

What a run guarantees, and what it gives up in exchange for speed:

- **Longest match, one derivation.** The expression takes every token the table
  allows. A shorter prefix is never offered to the surrounding grammar, so a
  grammar that needs an expression to stop earlier must use a token that
  cannot continue it.
- **Syntax shape.** Each operator, enclosure, application or ternary is one
  `LPGK_EXPRESSION` node labelled by its entry, with operation kind
  `LCOK_PRATT` and exactly one alternative; the children are the operand
  nodes, the operator tokens, and (for lists) separators and closers, in source
  order. Atoms stay token leaves. The operation itself contributes one node,
  with the operation's own label, above the expression.
- **Trivia.** Default-allowed tokens in force around the operation are
  admitted between its tokens and after its last token, attached as sequence
  nodes in front of the token they precede, and trailing trivia becomes part of
  the operation's node. They are never taken before its first token: the
  surrounding grammar handles those exactly as before. A token that is also
  an entry for the current role is that entry.
- **Errors.** Missing-token repair does not apply inside a Pratt expression:
  a token that cannot continue an incomplete expression ends it, the
  surrounding parse reports it, and the diagnostic lists the terminals the
  table allowed there (including closers and separators of the open
  enclosure). Non-associative operators that chain end the expression at the
  second operator.
- **Operands.** Atoms are single tokens and enclosures contain the same
  expression. For richer primaries, wrap the operation in an ordinary rule.

Table validation is a programmer-error assertion at construction: reserved or
missing token kinds, duplicate tokens in one role, an operator token reused as
a closer or separator, mixed associativity at one precedence, or no atom.
Binding powers are derived from precedence (two per level), so equal
precedence needs no manual numbering.

### Default-allowed tokens and local overrides

Set `configuration.allowed_tokens` and `allowed_token_count` to admit tokens
such as whitespace and comments at every grammar boundary by default. These
tokens are ordinary token leaves in the syntax structure; they are never
filtered, attached through a separate trivia interface or removed from source.

Wrap an expression with
`logos_languages_contexta_contexta_operation_add_allowed_tokens(label, contexta,
body, kinds, count)` to **replace** the inherited set inside that expression.
An empty set makes every token inside it explicit. The surrounding grammar
still controls the boundaries outside the wrapped expression. References and
recursive calls inherit the active set; nested wrappers replace it again.
Scopes are compiled into grammar/scope variants so one shared rule can be used
under different local policies without mutating that rule.

For example, a global whitespace token may be admitted around a compound name,
while an empty local set around `Identifier Dot Identifier` prohibits whitespace
between its components. A local set containing comments but not whitespace
permits only comments at those internal boundaries. Whitespace may also be an
explicit grammar terminal where its presence is structurally required.

Admission and explicit matching both remain possible when both derive the same
input. Alternative structural attachments of default tokens are also retained.
The parser does not silently prefer a placement or interpret a token's meaning.

### Lossless green syntax and red occurrences

The result owns every source byte and every original lexer result, including
EOF, unmatched spans and context-action failures. Green leaves retain exact
bytes, token kinds and lexical errors. Missing tokens have zero width and
never synthesize bytes. A limited result also exposes an explicit UNPARSED
leaf for retained source not incorporated before the limit; the raw token
table still preserves any tokens already decided there.

Green nodes expose widths, labels, constructor kinds and alternative counts;
they have no parents or absolute source positions, and they do not point back to
their tree (a node is 32 bytes, an alternative 16): the getters that read the tree's
source, labels or alternatives (`syntax_green_node_label_get`, `_operation_kind_get`,
`_token_kind_get`, `_text_get`, `_expectation_get`, `_alternative_first_get`,
`syntax_alternative_next_get`, `syntax_alternative_child_get`) take the tree as their
first argument. Nodes, alternatives,
diagnostics and expectations are opaque handles; only `LParserConfiguration`
(caller-filled input) and `LParserRedNode` (caller-allocated, so navigation
needs no heap) are transparent. Obtain the root with `syntax_tree_root_get`,
inspect a node with the `syntax_green_node_*_get` getters, and iterate its
alternatives with
`syntax_green_node_alternative_first_get` and `syntax_alternative_next_get`.
Each alternative exposes its ordered children through
`syntax_alternative_child_count_get` and `syntax_alternative_child_get`.
Every alternative's child widths sum to its parent's width. Nullable recursion can create
cycles representing infinitely many derivations; they remain visible and
clients control traversal depth and derivation selection.

`syntax_red_root_initialize` and `syntax_red_child_get` fill caller-owned
`LParserRedNode` occurrences. Select an opaque alternative explicitly at each
descent. Each occurrence supplies its parent and absolute byte offset, with no
heap allocation. Keep parent occurrences alive and unmoved while children
reference them. `syntax_red_text_get` returns that occurrence's exact source
slice. No nodes or tokens are flattened or hidden by navigation.

### Structural errors and recovery

The chart supplies expected terminals, using Contexta labels or the lexer's
token-rule labels for readable descriptions. Clients do not need to write
ordinary syntax-error messages. Diagnostics retain source extents, original
lexer failure reasons and the full expected-terminal set. Retrieve expectations
for a specific diagnostic with `syntax_tree_diagnostic_expectation_at`, using
`syntax_diagnostic_expectation_count_get`; expectation table storage stays
internal. Read diagnostics and expectations through the
`syntax_diagnostic_*_get` and `syntax_expectation_*_get` accessors, and the
raw token table through `syntax_tree_token_at`, which copies an `LLexerToken`.
`syntax_tree_diagnostic_message_write` formats an automatic message into a
caller-provided buffer and reports truncation explicitly.

When a source token cannot continue any derivation, recovery tries bounded
missing-terminal sequences that let that token continue, preferring the first
successful insertion depth. Otherwise it consumes the unexpected
token into an ERROR wrapper while retaining the viable chart structure. At
EOF it may insert a bounded number of missing terminals. Consecutive
unexpected tokens are grouped until a genuine grammar/default transition
succeeds, reducing cascaded diagnostics. This is bounded structural repair;
it does not promise globally minimal edits or infer intended semantics.
Repaired alternatives remain packed. If no complete derivation can be formed,
an error root still covers the entire original source and raw token stream;
speculative recognition states remain internal.

### Performance and limits

Prediction/completion uses indexed items and symbol-specific waiter lists.
Forest nodes and alternatives are interned; shared ambiguity is not expanded
into separate trees. Recognition allocates in slabs or geometric capacity
growth, never one allocation per item, tree node or red occurrence. Storage
sizing, grammar expansion bounds, recovery depth, and recognition limits are
managed internally. `LParserConfiguration` exposes only the default-allowed
token set. Compiler identities, arena indices, child-array layouts, linked-list
metadata, and speculative nodes are not part of the client API. No logging
occurs in recognition.

Generalized Earley parsing has cubic worst-case time and quadratic chart
space for a fixed grammar; packed families can occupy cubic space. Defaults,
nullable cycles and highly ambiguous grammars can consume these bounds. A
token budget bounds lexer results per call, while nullable closure and one
token's chart work remain indivisible and are bounded by item/forest limits.
Repetition (`star`, and `plus`, `optional` and `repeat` which lower to it) derives left-nested
lists, `X* = X* X | empty`: Earley completes a left-recursive rule in constant time per element, while a
right-recursive one keeps a pending item per element and is quadratic over a long file.

Hot-path layout. Charts, items, syntax nodes, alternatives and tokens live in Nexus chunked lists
(address-stable, indexed inline), with 32-bit indices bounded by the parser limits: a green node is 40
bytes, an alternative 24, an item 24. A node derives its label, operation kind and terminal token kind
from its grammar symbol, and a leaf's text from its source offset, instead of storing them. Items,
interned nodes and alternatives are memoized in small per-chart Nexus word maps that reset in O(1) when
the next chart starts; waiter chains are found by a short scan of the chart's own consecutive buckets.
The raw token cover that failed parses need is built only when a parse fails.

The lexer renumbers each context's DFA states (live, accepting, final, dead) and pads rows to a power
of two, so one byte costs one addition, one load and one range test, and a token's rule is a shift away.

`logos_benchmarks_run` measures the lexer, both parser workloads and lexer compilation (best of five;
build optimized). On one machine, against the first parser commit, over generated statements:
Pratt-heavy parse 1 MiB in 0.30 s (about 450 ns per token) and a repetition-heavy Earley-only parse in
0.71 s (about 1.4 us per token), where the earlier parser could not finish either within its item limit;
lexing about 190 MiB/s on 6-24 byte tokens (was 127) and 62 MiB/s on 1.5 byte tokens (unchanged).
Generalized Earley parsing remains cubic in the worst case, and packed ambiguity multiplies nodes.

The algorithm and syntax representation follow the mechanics described by
[Scott, SPPF-Style Parsing From Earley Recognisers](https://pure.royalholloway.ac.uk/en/publications/sppf-style-parsing-from-earley-recognisers/)
and [Roslyn's red/green design](https://github.com/dotnet/roslyn/blob/main/docs/compilers/Design/Red-Green%20Trees.md).
Logos deliberately retains allowed tokens as ordinary leaves and exposes
packed alternatives directly.

## Genesis: grammar files compiled into parsers

A Genesis source (`Language: Name` or `Library: Name`) describes lexical contexts, grammar rules, Pratt rules and
embedded languages; the compiler turns it into an engine that owns a compiled lexer and parser. The spec and
worked examples are in `notes/genesis_grammar.md`; compiled examples live in `genesis/`.

```c
LGenesisCompiler *compiler = logos_languages_genesis_compiler_create(runtime);
logos_languages_genesis_compiler_search_folder_add(compiler, "grammars");   /* ordered import folders */
LGenesisEngine *engine = logos_languages_genesis_compiler_compile_file(compiler, "grammars/TOML.gen");

if (engine == NULL) {
  char *text = logos_languages_genesis_compiler_diagnostics_render(compiler, TRUE);  /* rustc-style */
  /* print text, then nexus_memory_deallocate(text) */
}

LParser *parser = logos_languages_genesis_engine_parser_get(engine);   /* the raw parser */
LLexer  *lexer  = logos_languages_genesis_engine_lexer_get(engine);    /* the raw lexer  */
```

- Entry points: `compile_text` (NUL-terminated), `compile_string` (bytes), `chunk_begin/add/finish`, `compile_file`.
- `IMPORT Name { ... }` resolves by the file stem or the declared name, first among sources the compiler already
  compiled, then through the search folders in order.
- Parsing uses the backtracking engine (`LPE_BACKTRACKING`): PEG-style ordered choice, `LONGEST` and `UNAMBIGUOUS`
  modes, global rules between tokens, binding-power (Pratt) expressions over any rule, `lexical_selection =
  grammar_expected` (only the tokens the grammar accepts at a position compete), and `EMBED` regions and token
  interiors parsed by a guest language.
- In tokens, `.` is the wildcard (any one character) and `!x` is any one character that `x` does not match: `$line = '//' (!'\n')*`,
  `!('"' || '\\')`, `!$_digit`. `x` must be a character, a range, a fragment of that kind or alternatives and groups of them (error `G0264`
  otherwise); a repetition after it repeats the negation, as in `!'"'+`. The wildcard and the negation cover every scalar value but the surrogates.
- Lexical and grammatical expressions can repeat a bounded number of times: `x{3}`, `x{2,}` and `x{2,5}` (the brace touches the operand).
  In a token the count goes to the regular expression of the lexer; in a rule it is written out (copies of `x`, then `x*` or nested
  options). A minimum above the maximum or a count above 65535 is the error `G0263`.
- Limitation: the guest parse of an embed is run and checked but its tree is not yet grafted into the host tree.
  The host tree keeps the region as one token-like leaf, so reconstruction stays byte-exact.
- `PARSER { engine = earley }` selects the Earley engine instead: any context-free grammar, left recursion included, and every
  derivation is kept. It has no choice modes; ambiguity is settled by precedence. A rule written
  `#add @prec(left, 10) = #expr $plus #expr` has a level (larger binds tighter) and an associativity (`left`, `right`, `nonassoc`).
  Where such rules meet (the last element of one is a rule that begins with an expression, or the first element is a rule that ends in
  one), the engine keeps the grouping that yacc would choose by comparing the levels, and equal levels group by associativity; two
  `nonassoc` rules of one level do not nest at all. A rule with `@prec` should be one sequence. The backtracking engine ignores the
  annotation (and warns). The nodes of a rule with a precedence stay apart by the rule they were completed through; for a grammar
  that uses no `@prec` the only cost is a marker test per completion (about 2% of the instructions of the Earley-only benchmark).

## The genesis tool

`genesis` (built from `apps/genesis`) turns a grammar into other artifacts. Every artifact is a pure function of the
grammar, its mapping file and the tool, and a file is only written when its text changed.

```
genesis export  grammar.gen [-o out.gen]        # the grammar as self-contained Genesis source
genesis export  --builtin genesis|mapping -o X.gen [-M X.mapping]   # the languages the tool knows by heart
genesis check   grammar.gen file...             # parse files with a grammar; non-zero when one is rejected
genesis bind    grammar.gen [-o dir] [--prefix json]   # typed C bindings (one header)
genesis sublime grammar.gen [-o dir] [-t tests]        # a Sublime Text syntax and syntax tests
genesis lsp     grammar.gen...                          # a language server over stdio
```

```
genesis import FORMAT file... [-o out.gen] [--name Language]   # a grammar of another format, as Genesis (see below)
```

`-I dir` adds an import folder; imports also resolve next to the grammar. Settings that a grammar cannot carry live in
a mapping file, `<grammar>.mapping` next to it (or `-m file`). Lines are `key = value`, `$token = scope.name`,
`context NAME = scope.name`, `symbol #rule name=$token kind=function` and `reference $token`; a line starting with `//` is a comment.
Settings: `bind.directory`, `bind.prefix`, `bind.file`, `bind.guard`, `extensions`, `scope`, `test.comment`,
`sublime.directory`, `sublime.package`.

**Bindings.** The header defines enumerations of the token kinds and rules, a view type per rule
(`JsonObject`) and accessors for what the rule refers to: `json_object_count_token_comma(view)`,
`json_object_rule_key_value_pair(view, index)`, `json_root(tree)`. A view holds its tree and its node;
`json_object_cast(tree, node)` makes a view of a node the tree contains, and `json_rule_of(tree, node)` names its rule. The prefix is the namespace; the output
directory, file name, prefix and include guard are configurable. `json_grammar_matches(engine)` checks that an engine
was compiled from the grammar the header was generated from.

**Configuration.** `genesis.toml` in the root of a project (the nearest one at or above the current folder, or the root of the
editor's workspace for `lsp`; `--config FILE` names another) holds the settings of the tool, read with the Genesis TOML grammar.
`genesis config` shows what applies. For the language server (`[lsp]`):

| Key | Default | Meaning |
| --- | --- | --- |
| `walk` | `true` | look for the files of the workspace on a thread after `initialize` |
| `workspace_max_files`, `workspace_max_depth` | 20000, 24 | limits of that walk |
| `ignore` | `build .git external node_modules results .cache .hg .svn target vendor legacy` | folder names the walk does not enter |
| `text_prefilter` | `true` | analyse a file for a name only when its text contains the name |
| `cache` | `true` | keep the names and problems found in each file on disk between sessions |
| `cache_directory` | `$XDG_CACHE_HOME/pharos` or `~/.cache/pharos` | where; a relative path is taken from the project root |
| `cache_max_megabytes` | `128` | the cache is not written when it would be larger |

The backtracking engine always runs with its optimizations, which change how fast it parses and never what it accepts or the tree it builds:
a choice, repetition or option looks at the next token (with `grammar_expected`: lexes the first tokens of its alternatives, each with the eligible
set its token node uses) and does not try what cannot match; white space and comments are tried at a boundary only when the next token can begin
one; the token table is filled while parsing and cut back by a rollback. A document that is rejected is parsed once more without the skipping, so
that its diagnostic is always the plain parser's. A developer build checks that the token table equals a walk of the finished tree.

The cache holds one file per workspace and language set. Each record is tied to the size and modification time of the file it
was made from, and the whole cache to the version of the analysis and to the text of the grammars served, so a changed file or grammar is
analysed again and nothing stale is shown. A later session answers a workspace-wide search (find references, rename) from the cache
without reading the files that did not change; the text of a file is read only when a position in it is needed. The documents the editor has open
are always analysed from their text.

**Importing grammars.** `genesis import FORMAT files...` reads a grammar written for another tool and writes it as Genesis
for the Earley engine, with a report of what could not be carried over. Each format is itself a Genesis grammar in
`stdlib/formats/`, so the reader of a format is checked, extended and edited like any other language. `genesis import` without
arguments lists the formats.

| Format | Files | Carried over | Said in the report |
| --- | --- | --- | --- |
| `abnf` | RFC 5234 / 7405 | rules, repetitions, case-sensitive strings, numeric values | prose values |
| `antlr` | `.g4` lexer, parser or combined | rules, fragments, modes, skip and channels, `.*?` before literals | actions, labels, predicates dropped; `more`, `import` rejected |
| `ebnf` | ISO/IEC 14977, or the W3C notation of the XML specification (told apart by `::=`) | rules, options, repetitions, groups, counts, strings, code points, classes, the exception `x - y` between sets of characters | special sequences `? ... ?` rejected; an exception on anything but characters rejected; constraint notes `[ vc: ... ]` are comments |
| `bison` | `.y` and the flex `.l` that returns its tokens | rules, tokens from the scanner's `return`s, start conditions as modes, `%left %right %nonassoc %precedence %prec` as `@prec` | actions dropped; predicates, `error` alternatives, `^ $ r/s`, `yymore` and similar approximated; computed token returns left out |

A yacc grammar needs its scanner: the tokens are what flex returns (`return NUMBER;`), the characters that a rule writes as `'+'` are
tokens of their own, and a scanner pattern whose action returns nothing is skipped (white space, comments, the pieces of a string that
a start condition collects). Where a flex action switches the start condition with `BEGIN`, the lexer pushes or pops its context stack;
a switch that depends on a counter (nested HTML in DOT, for example) always happens. A rule of the grammar that carries a precedence and
begins or ends with a nonterminal becomes a rule of its own named `<rule>_alt<n>` in the result, so that the tree has a node for the
operator.

**Keeping generated files current.** `cmake/LogosGenesis.cmake` provides `logos_genesis_bindings(target GRAMMAR ...)`
and `logos_genesis_sublime(...)`. They rerun when the grammar, its mapping file, any grammar next to it or in an
include folder, or the tool (and so the engine) changes; consumers depend on the target, so an edit that leaves the
output unchanged rebuilds nothing.

**Sublime syntaxes.** `genesis sublime` builds the syntax from the grammar, not only from the lexer:
- *Frames.* A delimited construct (a rule or inline group that starts with one token and ends with another: an
  object between braces, a parenthesized group) becomes a context pushed by the opening token and popped by the closing
  token, scoped `meta.<rule>.<language>`; inside it only the tokens the grammar allows there are matched, found by
  following the rules referenced from it. The `main` context holds what the entry rule can start with (plus the global
  rules), so a `}` outside an object is not tokenized.
- *Lexical contexts.* Genesis `CONTEXT`s (strings and the like) stay contexts, with their push, pop and set actions.
- *Mapping syntactic nodes.* `$token = scope` scopes a token (the suffix of `scope` is appended: `constant.numeric`
  becomes `constant.numeric.json`). `rule #name = scope` scopes a delimited rule's frame, or, for a rule without a
  frame, the tokens that only it leads to; `rule #parent > #child = scope` scopes what #parent reaches through #child.
  When one token is reachable with different roles in the same context, the first role wins (Sublime has no lookahead
  state for positions inside a sequence, so key and value of a pair cannot be told apart). `context NAME = scope` adds a
  `meta_scope` and `context NAME.content = scope` a `meta_content_scope` to a lexical context.
- *Ordering.* Sublime takes the first pattern that matches where Genesis takes the longest, so patterns are ordered to
  agree: higher priority first, fixed texts longest first, then the rest; a fixed word that a longer token could extend
  (a keyword and an identifier) gets a `(?![A-Za-z0-9_])` guard, found by probing the compiled lexer.
- *Tests.* With `test.comment` set, a `syntax_test_*` file is written for Sublime's runner: one assertion per fixed-text
  token of the main context, and for each frame its opening and closing text with the scopes of both and of the frame.
  `apps/genesis/sublime/run_syntax_tests.sh` runs it with the headless runner. CTest compares the output for JSON and
  Calculator with the expected files in `apps/genesis/tests/golden`.

Not generated yet: `embed:` for EMBED regions, and positions inside a rule's sequence (the part before and after a `:`).

**The languages of Genesis and of mapping files.** They are not written by hand. Each is defined once as a Logos
language (`logos_languages_genesis_definition_*`: tokens as Regula regexps in lexical contexts, rules as Contexta
operations), and the exporter turns that definition into Genesis source, the same way it exports a compiled grammar.
Genesis is defined from the tables the compiler's lexer and parser use for their words and punctuation
(`src/l_languages_genesis_spec.c`); the language of mapping files from the form table of the mapping reader
(`apps/genesis/t_spec.c`). A keyword or statement form that exists in the code therefore exists in its grammar, and the
editor mappings that style them are written from the same tables. `genesis export --builtin genesis -o Genesis.gen
-M Genesis.mapping` writes them; the build does it (`logos_genesis_builtin`), generates their Sublime syntaxes, and
CTest checks that the Genesis language parses every grammar in the repository and the mapping language every mapping
file. Set `LOGOS_SUBLIME_PACKAGE_DIRECTORY` (for example `~/.config/sublime-text/Packages/User`) to keep both syntaxes
installed in Sublime Text; they are rewritten only when their text changes.

**Benchmark.** `genesis bench grammar.gen file... [-n runs]` times compiling the grammar and parsing each file (best and mean of n runs, MB/s, tokens), the numbers to profile and optimize against.

**Language server.** `genesis lsp [-I dir]... [grammar.gen[:mapping]...]` serves over stdio.

- *Genesis sources (`.gen`) and mapping files (`.mapping`) are always served*, across files. Every name is an occurrence of a
  declaration: rules, tokens (in any number of contexts), fragments, contexts, templates, template parameters and call
  arguments, enumerations and their members, import names and aliases. Go to definition, references, document highlights,
  hover, document symbols, prepare-rename and rename all work on that: from a use to its declaration in the same file, into
  the file an `IMPORT` brings it from (the imported file is found through the `-I` folders and the importing file's folder),
  from a mapping's `$token`, `#rule` and `context NAME` to the grammar it maps (the `.gen` of the same name beside it or in
  a `-I` folder), and back (references and renames of a token reach the mapping files of the workspace). An argument that
  names a token or rule a template generates (`name: $let`) declares it at the call. Diagnostics are the compiler's own,
  with their hints; a mapping that names something the grammar lacks is a warning.
- *Any other language* named as `grammar.gen[:mapping]` is parsed with that grammar; what its parser cannot accept is a
  diagnostic, hover shows the rule chain, and the mapping's `symbol #rule name=$token kind=function` and `reference $token`
  lines give it the same definition, references and rename.

The workspace is the client's folders and the `-I` folders (`.gen` and `.mapping` files, skipping `build`, `external`,
`results` and `.git`, at most 600). Positions are exact (UTF-16 columns over UTF-8 text). The editor sees what is on disk
for files that are not open, and imports are read from disk, so an unsaved change to an imported file shows after saving.
Not done: completion, incremental sync, semantic analysis beyond names. The Sublime Text client is a `clients` entry in
`LSP.sublime-settings`:

```
"genesis": { "command": ["…/bin/genesis", "lsp", "-I", "…/libs/logos/stdlib", "-I", "…/libs/logos/genesis"],
             "selector": "source.genesis | source.genesis-mapping" }
```
