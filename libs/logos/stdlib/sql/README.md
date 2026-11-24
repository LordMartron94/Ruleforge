# SQL dialects

One grammar per dialect, each converted by `genesis import antlr` from the ANTLR grammars of the
[grammars-v4](https://github.com/antlr/grammars-v4) project (the files are the output of `regenerate.sh`, kept in the repository so the
grammars work without network). They are as complete as the ANTLR grammars they come from, less what Genesis cannot carry over:

| Dialect | Source grammar | Parses the project's example scripts | Lost in the conversion |
| --- | --- | --- | --- |
| `SQLite.gen` | `sql/sqlite` (SQLiteLexer, SQLiteParser) | 17 of 17 | nothing but actions |
| `TransactSQL.gen` | `sql/tsql` (TSqlLexer, TSqlParser) | 110 of 137 | nested comments become a lexical mode; the rest stop at the Earley engine's item limit on very long scripts |
| `MySQL.gen` | `sql/mysql/Positive-Technologies` | 19 of 24 | actions; a few statements of the examples need constructs the converted grammar reads differently |

Not here yet: PostgreSQL (its ANTLR lexer relies on `more` and semantic predicates, which cannot be converted; the dialect will come from
`gram.y` once the bison importer exists), Oracle PL/SQL (its lexer has a non-greedy rule the importer cannot express), and the standard
ISO SQL. The sketch `../SQL.gen` stays for the embedding examples (Markdown with SQL fences); it is tolerant and not a dialect.

The ANTLR grammars are by their authors (Bart Kiers and others, Positive Technologies) under the licenses stated in their headers (MIT or
BSD); the converted files keep their rule names. Semantic predicates and actions of the originals are ignored, so these grammars accept
whatever the originals would have refused through a predicate (the importer says how many).
