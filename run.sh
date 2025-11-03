#!/usr/bin/env bash
set -euo pipefail

# Always run from repo root
cd "$(dirname "$0")"

# Ensure build directory exists
mkdir -p build

ENTRYPOINT="./ruleforge/components/ruleforge/entry"
BINARY="./build/ruleforge"

echo "🔹 Building Ruleforge project..."
echo "   > go build -o $BINARY $ENTRYPOINT"
go build -o "$BINARY" "$ENTRYPOINT"

echo
echo "🔹 Running Ruleforge project..."
echo "   > $BINARY $@"
echo

"$BINARY" "$@"
