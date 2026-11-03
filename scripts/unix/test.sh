#!/usr/bin/env bash
set -uo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$root"
status=0
if [ ! -x build/mfdp_tests ]; then
  bash scripts/unix/build.sh || exit 1
fi
echo "[mfdp] C++ tests"
./build/mfdp_tests || status=1
if command -v node >/dev/null 2>&1; then
  echo "[mfdp] Node.js binding tests"
  MFDP_BIN="$root/build/mfdp" node --test bindings/node/test/*.test.js || status=1
  if [ -f api/dist/test/security.test.js ]; then
    echo "[mfdp] API security tests"
    node --test api/dist/test/*.test.js || status=1
  else
    echo "[mfdp] API tests skipped (run make setup, then: npm run build --workspace api)"
  fi
else
  echo "[mfdp] node not found; skipping Node.js tests"
fi
if command -v python3 >/dev/null 2>&1; then
  echo "[mfdp] Python binding tests"
  MFDP_BIN="$root/build/mfdp" python3 -m unittest discover -s bindings/python/tests -v || status=1
fi
exit $status
