#!/usr/bin/env bash
set -euo pipefail

# 1. Resolve repo root (Keep this - it's your anchor)
REPO_ROOT="$(cd "$(dirname "$0")" && pwd)"
while [[ ! -f "$REPO_ROOT/go.work" && "$REPO_ROOT" != "/" ]]; do
    REPO_ROOT="$(dirname "$REPO_ROOT")"
done

if [[ ! -f "$REPO_ROOT/go.work" ]]; then
    echo "❌ go.work not found"
    exit 1
fi
cd "$REPO_ROOT"

# 2. Environment Setup
export PATH="$HOME/.local/bin:$PATH"
export PATH="$HOME/go/bin:$PATH"
export WEBKIT_DISABLE_COMPOSITING_MODE=1
export PYTHONDONTWRITEBYTECODE=1

# 3. Pre-run analysis (Optional, but keep if you use the files)
tree -L 8 -I vendor > tree.txt
NO_COLOR=1 ./run_analyzer.sh > analysis.txt

# 4. Argument Parsing
MODE="default"
APP_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --testing) MODE="testing"; shift ;;
         --clean)   MODE="clean"; shift ;;
        *)         APP_ARGS+=("$1"); shift ;;
    esac
done

# 5. Dispatch to Makefile
# We pass APP_ARGS into the 'ARGS' variable inside the Makefile
export ARGS="${APP_ARGS[*]}"

case "$MODE" in
    "testing")
        make test
        ;;
    "clean")
        make clean
        ;;
    "default")
        # 'make dev' handles the Air check and fallback
        make build
        ;;
esac