#!/bin/sh
# The configuration file is found in the project root and above, an explicit --config wins, and a file that is not TOML is refused.
#   config_test.sh <genesis>
set -e
genesis=$1
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/project/deep/er"
cat > "$work/project/genesis.toml" <<'TOML'
title = "x"
[lsp]
cache = true
workspace_max_files = 1_000
ignore = ["build", 'tmp']
TOML

out=$(cd "$work/project/deep/er" && PWD="$work/project/deep/er" "$genesis" config)
echo "$out" | grep -q "lsp.cache = true" || { echo "FAIL: the file above was not found" >&2; exit 1; }
echo "$out" | grep -q "lsp.ignore = \[\"build\", 'tmp'\]" || { echo "FAIL: arrays are kept as written" >&2; exit 1; }

printf '[other]\nkey = 1\n' > "$work/other.toml"
out=$(cd "$work/project" && PWD="$work/project" "$genesis" config --config "$work/other.toml")
echo "$out" | grep -q "other.key = 1" || { echo "FAIL: --config was not read" >&2; exit 1; }
if echo "$out" | grep -q "lsp.cache"; then echo "FAIL: --config must replace the found file" >&2; exit 1; fi

printf 'key = = 1\n' > "$work/bad.toml"
if "$genesis" config --config "$work/bad.toml" 2> /dev/null; then echo "FAIL: invalid TOML was accepted" >&2; exit 1; fi
