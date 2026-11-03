#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${1:-$root/build}"
mkdir -p "$out"
cxx="${CXX:-g++}"
flags="-std=c++20 -O2 -Wall -Wextra -I$root/include -I$root/src -I$root/src/core -I$root/src/runtime -I$root/src/pipeline -DMFDP_DEFAULT_ROOT=\"$root\""
lib_srcs=(
  core/errors core/types core/languages core/tokens core/sha256 core/json
  runtime/config runtime/adapter runtime/rpc
  pipeline/parse pipeline/resolve pipeline/builder pipeline/format pipeline/context
  api
)
lib_objs=()
for f in "${lib_srcs[@]}"; do
  obj="$out/$(echo "$f" | tr '/' '_').o"
  $cxx $flags -c "$root/src/$f.cpp" -o "$obj"
  lib_objs+=("$obj")
done
$cxx $flags "$root/apps/cli/main.cpp" "${lib_objs[@]}" -lz -o "$out/mfdp"
$cxx $flags "$root/apps/bench/main.cpp" "${lib_objs[@]}" -lz -o "$out/mfdp-bench"
$cxx $flags -DMFDP_TEST_DIR="\"$root/tests\"" -DMFDP_CLI_PATH="\"$out/mfdp\"" "$root"/tests/*.cpp "${lib_objs[@]}" -lz -o "$out/mfdp_tests"
$cxx $flags "$root/examples/api_usage.cpp" "${lib_objs[@]}" -lz -o "$out/mfdp_example"
echo "built into $out"