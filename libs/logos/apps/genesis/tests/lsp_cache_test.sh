#!/bin/sh
# The language server keeps what it learned about the files of a workspace on disk (the cache directory of genesis.toml): a second
# session answers the same, a file that changed since is analysed again, and `cache = false` writes nothing.
#   lsp_cache_test.sh <genesis>
set -e
genesis=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/project"
cd "$work/project"

cat > Defs.gen <<'GEN'
Language: Defs
Version: 1.0.0

PARSER { entry = #thing }

CONTEXT <ROOT> {
  $x = 'x'
}

#thing = $x
GEN
for name in One Two Three; do
  cat > "$name.gen" <<GEN
Language: $name
Version: 1.0.0

IMPORT Defs { #thing }

PARSER { entry = #user_$name }

CONTEXT <ROOT> {
  $x = 'x'
}

#user_$name = #thing
GEN
done

message() {
  body=$1
  printf 'Content-Length: %s\r\n\r\n%s' "${#body}" "$body"
}

# The references of #thing from its declaration, with a workspace that is read from disk.
session() {
  {
    message "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"file://$work/project\"}}"
    message '{"jsonrpc":"2.0","method":"initialized","params":{}}'
    message "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file://$work/project/Defs.gen\",\"version\":1,\"text\":\"$(sed ':a;N;$!ba;s/\n/\\n/g' Defs.gen)\"}}}"
    message "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"textDocument/references\",\"params\":{\"textDocument\":{\"uri\":\"file://$work/project/Defs.gen\"},\"position\":{\"line\":9,\"character\":2},\"context\":{\"includeDeclaration\":true}}}"
    message '{"jsonrpc":"2.0","id":3,"method":"shutdown","params":null}'
    message '{"jsonrpc":"2.0","method":"exit"}'
  } | "$genesis" lsp -I "$work/project" 2> /dev/null | tr -d '\r' | grep -o '"uri":"file://[^"]*"' | sort | uniq -c
}

count() {
  grep -c "$1" "$2" || true
}

cat > genesis.toml <<TOML
[lsp]
cache = true
cache_directory = "$work/cache"
TOML

session > "$work/first"
[ "$(count One.gen "$work/first")" -ge 1 ] || { echo "FAIL: the first session did not find the use in One.gen" >&2; cat "$work/first" >&2; exit 1; }
ls "$work/cache"/*.pcache > /dev/null 2>&1 || { echo "FAIL: no cache file was written" >&2; exit 1; }

session > "$work/second"
cmp -s "$work/first" "$work/second" || { echo "FAIL: the second session answered differently" >&2; diff "$work/first" "$work/second" >&2; exit 1; }

# A file that changed is analysed again: Two.gen no longer mentions #thing.
sleep 1
sed -i '/#thing/d' Two.gen
session > "$work/third"
if grep -q Two.gen "$work/third"; then echo "FAIL: a changed file was taken from the cache" >&2; exit 1; fi
[ "$(count One.gen "$work/third")" -ge 1 ] || { echo "FAIL: an unchanged file lost its reference" >&2; exit 1; }

# cache = false writes nothing.
rm -rf "$work/cache"
cat > genesis.toml <<TOML
[lsp]
cache = false
cache_directory = "$work/cache"
TOML
session > "$work/fourth"
if ls "$work/cache"/*.pcache > /dev/null 2>&1; then echo "FAIL: a cache was written although cache = false" >&2; exit 1; fi
