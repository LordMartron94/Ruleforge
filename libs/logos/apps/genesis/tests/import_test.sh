#!/bin/sh
# Converts a grammar of another format to Genesis, then checks that the result parses what the original grammar accepts and
# rejects what it does not.
#   import_test.sh <genesis> <format> <grammar file>[ <more files>] <ok folder> <bad folder>
# The grammar files are given as one argument, separated by blanks (a yacc grammar and its flex scanner).
set -e
genesis=$1
format=$2
grammar=$3
ok=$4
bad=$5
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$genesis" import "$format" $grammar -o "$work/Converted.gen" 2> "$work/report"

for file in "$ok"/*; do
  if ! "$genesis" check "$work/Converted.gen" "$file" -q 2> "$work/error"; then
    echo "FAIL: $file should be accepted" >&2
    cat "$work/error" >&2
    exit 1
  fi
done

for file in "$bad"/*; do
  if "$genesis" check "$work/Converted.gen" "$file" -q 2> /dev/null; then
    echo "FAIL: $file should be rejected" >&2
    exit 1
  fi
done
