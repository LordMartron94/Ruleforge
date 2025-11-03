#!/usr/bin/env bash
set -euo pipefail

# Always run from the script's directory (assumed to be repo root)
cd "$(dirname "$0")"

echo "=== Git Submodule Adder ==="

# Ask for details
read -rp "Enter submodule name (e.g. vision): " NAME
read -rp "Enter repository URL (e.g. ssh://git@localhost:22222/home/git/git-repos/vision.git): " URL
read -rp "Enter parent directory where submodule should be placed (default: libs): " PARENT_DIR

# Default to "libs" if empty
if [ -z "$PARENT_DIR" ]; then
  PARENT_DIR="libs"
fi

# Construct the final location
LOCATION="$PARENT_DIR/$NAME"

# Ensure parent directory exists
mkdir -p "$PARENT_DIR"

# Ensure .gitmodules exists
if [ ! -f .gitmodules ]; then
  echo "No .gitmodules file found, creating one..."
  touch .gitmodules
fi

echo
echo "Adding submodule:"
echo "  Name:     $NAME"
echo "  URL:      $URL"
echo "  Location: $LOCATION"
echo

# Add the submodule
git submodule add --name "$NAME" "$URL" "$LOCATION"

# Initialize and update
git submodule update --init --recursive "$LOCATION"

echo
echo "Done!"
echo "Submodule '$NAME' added at '$LOCATION' with URL '$URL'."
