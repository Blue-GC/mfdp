#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
if [ ! -f .env ]; then
  echo "[mfdp] .env is missing; run: make setup" >&2
  exit 1
fi
if [ ! -x build/mfdp ]; then
  bash scripts/unix/build.sh
fi
if [ ! -d node_modules ]; then
  npm install
fi
npm run build --workspace api
exec node api/dist/src/server.js
