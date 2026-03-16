#!/usr/bin/env bash
set -euo pipefail

# Always run from repo root
cd "$(dirname "$0")"

# 🧠 Ensure PATH includes local CLI tools
export PATH="$HOME/.local/bin:$PATH"
export PATH="$HOME/go/bin:$PATH"

BINARY="id_gen"

go build -o ./build/$BINARY ./libs/essence/cli
./build/$BINARY
exit 0

