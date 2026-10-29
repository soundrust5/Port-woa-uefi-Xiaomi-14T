#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
src='diagnostic/degas-dvfs-windows'
out='artifacts/degas/dvfs-windows'
mkdir -p "$out"
clang -std=c11 -Wall -Wextra -Werror -I "$src/tests" \
  "$src/degas_backend.c" "$src/tests/test_backend_dynamic.c" -o "$out/test_backend_dynamic"
"$out/test_backend_dynamic"
"$out/test_backend_dynamic" no-set
"$out/test_backend_dynamic" mode-loss
"$out/test_backend_dynamic" ready-loss
