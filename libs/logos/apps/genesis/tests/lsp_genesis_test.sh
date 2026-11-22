#!/bin/sh
# Drives `genesis lsp` over Genesis sources and a mapping file in a scratch folder and checks that names resolve in the
# file, across an IMPORT, and from the mapping to its grammar.
#   lsp_genesis_test.sh <genesis>
set -e
genesis=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat > "$work/Inner.gen" <<'GEN'
Language: Inner
Version: 1.0.0

PARSER {
  entry = #value
}

CONTEXT <ROOT> {
  $number = '0'..'9'+
}

#value = $number
GEN

cat > "$work/Outer.gen" <<'GEN'
Language: Outer
Version: 1.0.0

IMPORT Inner { #value }

PARSER {
  entry = #top
}

CONTEXT <ROOT> {
  $word = 'a'..'z'+
}

#top = #value $word
GEN

cat > "$work/Outer.mapping" <<'MAP'
extensions = out
$word = string.unquoted
rule #top = meta.top
$missing = invalid
MAP

cat > "$work/Lib.gen" <<'GEN'
Library: Lib
Version: 1.0.0

TEMPLATE <#twice>(
    $$item: Token
) -> GrammarExpression {
    #twice_body = $$item $$item
}
GEN

cat > "$work/Uses.gen" <<'GEN'
Language: Uses
Version: 1.0.0

IMPORT Lib { #twice }

PARSER {
  entry = #top
}

CONTEXT <ROOT> {
  $word = 'a'..'z'+
}

#top = #twice(item: $word)
GEN

printf '%s' 'Language: Broken
Version: 1.0.0

PARSER {
  entry = #top
}

CONTEXT <ROOT> {
  $word = '"'"'a'"'"'..'"'"'z'"'"'+
}

#top = $word (
' > "$work/Broken.gen"

message() {
  body=$1
  printf 'Content-Length: %s\r\n\r\n%s' "${#body}" "$body"
}

text() {
  # The text of a file as a JSON string.
  sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/\t/\\t/g' "$1" | awk 'BEGIN{ORS="\\n"} {print}'
}

open() {
  message "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/didOpen\",\"params\":{\"textDocument\":{\"uri\":\"file://$work/$1\",\"version\":1,\"text\":\"$(text "$work/$1")\"}}}"
}

request() {
  # request <id> <method> <file> <line> <character> [extra params]
  message "{\"jsonrpc\":\"2.0\",\"id\":$1,\"method\":\"$2\",\"params\":{\"textDocument\":{\"uri\":\"file://$work/$3\"},\"position\":{\"line\":$4,\"character\":$5}$6}}"
}

{
  message "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"rootUri\":\"file://$work\"}}"
  open Outer.mapping
  open Outer.gen
  open Broken.gen
  open Uses.gen
  request 2 textDocument/definition Outer.gen 6 12
  request 3 textDocument/definition Outer.gen 13 16
  request 4 textDocument/definition Outer.gen 13 9
  request 5 textDocument/definition Outer.mapping 1 2
  request 6 textDocument/rename Outer.mapping 1 2 ',"newName":"term"'
  request 7 textDocument/definition Outer.gen 3 9
  request 8 textDocument/references Outer.gen 13 2
  request 11 textDocument/definition Uses.gen 13 9
  request 12 textDocument/definition Uses.gen 13 15
  request 13 textDocument/references Lib.gen 3 12
  message "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"textDocument/documentSymbol\",\"params\":{\"textDocument\":{\"uri\":\"file://$work/Outer.gen\"}}}"
  message '{"jsonrpc":"2.0","id":10,"method":"shutdown","params":null}'
  message '{"jsonrpc":"2.0","method":"exit"}'
} | "$genesis" lsp -I "$work" > "$work/out" 2> "$work/err"

fail=0
check() {
  if ! grep -q -- "$2" "$work/out"; then
    echo "FAIL: $1 (expected $2)" >&2
    fail=1
  fi
}

where() {
  # where <file> <line> <character>: a location in that file at that start.
  printf '"uri":"file://%s/%s","range":{"start":{"line":%s,"character":%s}' "$work" "$1" "$2" "$3"
}

check "a rule goes to its declaration in the same file" "\"id\":2,\"result\":\[{$(where Outer.gen 13 1)"
check "a token goes to its declaration in the same file" "\"id\":3,\"result\":\[{$(where Outer.gen 10 3)"
check "an imported rule goes to its declaration in the other file" "\"id\":4,\"result\":\[{$(where Inner.gen 11 1)"
check "a token of a mapping goes to the token of its grammar" "\"id\":5,\"result\":\[{$(where Outer.gen 10 3)"
check "a rename of a token reaches the mapping" "\"changes\":{.*Outer.mapping\":\[{\"range\":{\"start\":{\"line\":1,\"character\":1}"
check "a rename of a token reaches the grammar" "\"changes\":{.*Outer.gen\":\[{\"range\":{\"start\":{\"line\":10,\"character\":3}"
check "the name of an import goes to the imported file" "\"id\":7,\"result\":\[{\"uri\":\"file://$work/Inner.gen\""
check "references of a rule include the mapping" "\"id\":8,\"result\":.*Outer.mapping"
check "a template call goes to the template of its library, whichever sigil it has" "\"id\":11,\"result\":\\[{$(where Lib.gen 3 11)"
check "an argument name goes to the parameter of the template" "\"id\":12,\"result\":\\[{$(where Lib.gen 4 6)"
check "references of a template reach the importing file" "\"id\":13,\"result\":.*Uses.gen"
check "the symbols list the declarations" '"name":"top"'
if grep -q "declares no token" "$work/out"; then echo "FAIL: names are not judged, only syntax" >&2; fail=1; fi
check "a syntax error is an error in the grammar" 'Broken.gen","diagnostics":\[{"range"'

if [ "$fail" -ne 0 ]; then
  cat "$work/out" >&2
  cat "$work/err" >&2
fi
exit "$fail"
