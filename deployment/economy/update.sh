#!/bin/sh
# Updates this service only. A failed pull leaves the running collector intact.
set -eu
cd "$(dirname "$0")"
if command -v flock >/dev/null 2>&1; then
  exec 9>.update.lock
  flock -n 9 || exit 0
fi
docker compose -f compose.yaml pull economy-history
docker compose -f compose.yaml up -d --no-deps economy-history
