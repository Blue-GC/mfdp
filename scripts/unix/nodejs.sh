#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
if [ ! -x build/mfdp ]; then
  bash scripts/unix/build.sh
fi
out="build/bindings/nodejs"
rm -rf "$out"
mkdir -p "$out"
cp -R bindings/node/. "$out/"
rm -rf "$out/node_modules"
if command -v npm >/dev/null 2>&1; then
  (cd "$out" && npm pack --silent --pack-destination . >/dev/null)
  echo "[mfdp] packed: $(ls "$out"/*.tgz)"
else
  echo "[mfdp] npm not found; copied the package without packing"
fi
echo "[mfdp] Node.js binding ready in $out"
echo "[mfdp] outside this repository set MFDP_ROOT (project root) and MFDP_BIN (path to build/mfdp)"
