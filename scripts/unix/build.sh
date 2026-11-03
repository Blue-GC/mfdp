#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
jobs="$( (nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2) | head -n 1)"
if command -v cmake >/dev/null 2>&1; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j "$jobs"
else
  echo "[mfdp] cmake not found; using the direct compiler build"
  bash scripts/unix/build-direct.sh "$root/build"
fi
if [ ! -x build/mfdp ]; then
  echo "[mfdp] build finished but build/mfdp was not produced" >&2
  exit 1
fi
echo "[mfdp] built $root/build/mfdp"
