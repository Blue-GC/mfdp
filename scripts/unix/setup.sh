#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
node scripts/lib/setup-env.js
mkdir -p artifacts
if command -v npm >/dev/null 2>&1; then
  npm install
else
  echo "[mfdp] npm not found; install Node.js 18+ and run: npm install" >&2
  exit 1
fi
echo "[mfdp] setup complete. Next: make build"
