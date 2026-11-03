#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
rm -rf build build-direct adapters/rust/target adapters/csharp/bin adapters/csharp/obj api/dist
find . -name "__pycache__" -type d -prune -exec rm -rf {} + 2>/dev/null || true
echo "[mfdp] cleaned"
