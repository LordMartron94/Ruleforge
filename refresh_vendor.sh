#!/usr/bin/env bash
# ======================================================================
# go-vendor-refresh.sh
# ----------------------------------------------------------------------
# Tidy all Go modules in the workspace, sync the workspace definition,
# and regenerate the unified vendor directory (for offline builds).
# ======================================================================

set -euo pipefail

echo "🧹 Tidy all Go modules..."
while IFS= read -r modfile; do
    moddir=$(dirname "$modfile")
    echo "   - Tidying $moddir"
    (cd "$moddir" && go mod tidy)
done < <(find . -type f -name "go.mod")

echo "🔄 Syncing go.work with modules..."
go work sync

echo "📦 Rebuilding vendor directory..."
rm -rf vendor
go work vendor

echo "✅ Vendor refresh complete."
echo "   Check 'vendor/' for external dependencies."
