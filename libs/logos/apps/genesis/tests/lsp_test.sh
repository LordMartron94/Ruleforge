#!/bin/sh
# Drives `genesis lsp` over stdio with a scripted client and checks its answers.
#   lsp_test.sh <genesis> <grammar.gen> <mapping> <import folder>
set -e
genesis=$1
grammar=$2
mapping=$3
imports=$4
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

message() {
  body=$1
  printf 'Content-Length: %s\r\n\r\n%s' "${#body}" "$body"
}

{
  message '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
  message '{"jsonrpc":"2.0","method":"initialized","params":{}}'
  message '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///a.calc","version":1,"text":"let a = 1"}}}'
  message '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///b.calc","version":1,"text":"a + b"}}}'
  message '{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///c.calc","version":1,"text":"let = 1"}}}'
  message '{"jsonrpc":"2.0","id":2,"method":"textDocument/definition","params":{"textDocument":{"uri":"file:///b.calc"},"position":{"line":0,"character":0}}}'
  message '{"jsonrpc":"2.0","id":3,"method":"textDocument/references","params":{"textDocument":{"uri":"file:///a.calc"},"position":{"line":0,"character":4}}}'
  message '{"jsonrpc":"2.0","id":4,"method":"textDocument/rename","params":{"textDocument":{"uri":"file:///a.calc"},"position":{"line":0,"character":4},"newName":"total"}}'
  message '{"jsonrpc":"2.0","id":5,"method":"textDocument/documentSymbol","params":{"textDocument":{"uri":"file:///a.calc"}}}'
  message '{"jsonrpc":"2.0","id":6,"method":"textDocument/hover","params":{"textDocument":{"uri":"file:///b.calc"},"position":{"line":0,"character":2}}}'
  message '{"jsonrpc":"2.0","id":7,"method":"textDocument/prepareRename","params":{"textDocument":{"uri":"file:///b.calc"},"position":{"line":0,"character":0}}}'
  message '{"jsonrpc":"2.0","id":8,"method":"shutdown","params":null}'
  message '{"jsonrpc":"2.0","method":"exit"}'
} | "$genesis" lsp -I "$imports" "$grammar:$mapping" > "$work/out" 2> "$work/err"

fail=0
check() {
  if ! grep -q -- "$2" "$work/out"; then
    echo "FAIL: $1 (expected $2)" >&2
    fail=1
  fi
}

check "initialize answers with capabilities" '"renameProvider"'
check "a syntax error is a diagnostic" '"uri":"file:///c.calc","diagnostics":\[{"range"'
if grep -q "is not declared" "$work/out"; then echo "FAIL: names are not judged, only syntax" >&2; fail=1; fi
check "definition finds the declaration" '"id":2,"result":\[{"uri":"file:///a.calc","range":{"start":{"line":0,"character":4},"end":{"line":0,"character":5}}}\]'
check "references span documents" '"uri":"file:///b.calc"'
check "rename edits every occurrence" '"newText":"total"'
check "the declaration is a document symbol" '"name":"a","kind":13'
check "hover shows the rule chain" '#document'
check "prepareRename gives the token range" '"id":7,"result":{"start"'
check "shutdown is answered" '"id":8,"result":null'

if [ "$fail" -ne 0 ]; then
  cat "$work/out" >&2
  cat "$work/err" >&2
fi
exit "$fail"
