#!/usr/bin/env bash
# Build the app before placing help inside its output directory.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
bun run build:wasm
bun run typecheck
bun run build
(cd help && bun run build)

# Successful build exits alone do not prove the combined artifact survived.
for route in index.html help/index.html help/ar/index.html; do
  if [[ ! -f "dist/$route" || ! -s "dist/$route" ]]; then
    echo "Incomplete static site: missing or empty dist/$route" >&2
    exit 1
  fi
done
echo "Verified static site: /, /help/, /help/ar/ in web/dist"
