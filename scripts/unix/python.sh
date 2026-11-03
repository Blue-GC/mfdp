#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
if [ ! -x build/mfdp ]; then
  bash scripts/unix/build.sh
fi
out="build/bindings/python"
rm -rf "$out"
mkdir -p "$out"
cp -R bindings/python/. "$out/"
find "$out" -name "__pycache__" -type d -prune -exec rm -rf {} +
if python3 -m pip --version >/dev/null 2>&1 && python3 -c "import setuptools, wheel" >/dev/null 2>&1; then
  python3 -m pip wheel --no-deps --no-build-isolation -q -w "$out/dist" "$out" || echo "[mfdp] wheel build failed; the package directory is still usable"
else
  echo "[mfdp] pip, setuptools or wheel not available; skipping the wheel (the package directory is still usable)"
fi
echo "[mfdp] Python binding ready in $out"
echo "[mfdp] outside this repository set MFDP_ROOT (project root) and MFDP_BIN (path to build/mfdp)"
