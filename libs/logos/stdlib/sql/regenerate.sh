#!/bin/sh
# Regenerates the SQL dialect grammars from the ANTLR grammars of the grammars-v4 project.
#   regenerate.sh <genesis> [work folder]
set -e
genesis=$1
work=${2:-grammars-v4}
here=$(cd "$(dirname "$0")" && pwd)

if [ ! -d "$work" ]; then
  git clone -q --depth 1 --filter=blob:none --sparse https://github.com/antlr/grammars-v4.git "$work"
  git -C "$work" sparse-checkout set sql/sqlite sql/tsql sql/mysql
fi

"$genesis" import antlr "$work/sql/sqlite/SQLiteLexer.g4" "$work/sql/sqlite/SQLiteParser.g4" --prefix SQLite -o "$here/SQLite.gen"
"$genesis" import antlr "$work/sql/tsql/TSqlLexer.g4" "$work/sql/tsql/TSqlParser.g4" --prefix TransactSQL -o "$here/TransactSQL.gen"
"$genesis" import antlr "$work/sql/mysql/Positive-Technologies/MySqlLexer.g4" "$work/sql/mysql/Positive-Technologies/MySqlParser.g4" --prefix MySQL -o "$here/MySQL.gen"
