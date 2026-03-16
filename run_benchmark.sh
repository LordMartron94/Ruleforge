#!/usr/bin/env bash
set -euo pipefail

# Always run from repo root
cd "$(dirname "$0")"

# Ensure build directory exists
mkdir -p build

export PATH="$HOME/.local/bin:$PATH"
export PATH="$HOME/go/bin:$PATH"

# Default entrypoint is orchestrator main.go at root
ENTRYPOINT="./tools/anvil"
BINARY="./build/anvil"
CONFIG_PATH="./benchmark_config.toml"

echo "🔹 Building Anvil project..."
echo "   > go build -o $BINARY $ENTRYPOINT"
go build -o "$BINARY" "$ENTRYPOINT"

echo
echo "🔹 Running Anvil project..."

sass assets/styles/main.scss assets/styles/dist/dark.css

# Build command with base flags
CMD_ARGS=("--benchconfig=$CONFIG_PATH")

# Pass through all remaining arguments (including --web if provided)
# Example usage:
#   ./run_benchmark.sh interpret results/bench-*.txt --web
#   ./run_benchmark.sh benchmark --web
if [ $# -gt 0 ]; then
	CMD_ARGS+=("$@")
fi

echo "   > $BINARY ${CMD_ARGS[*]}"
echo

"$BINARY" "${CMD_ARGS[@]}"
