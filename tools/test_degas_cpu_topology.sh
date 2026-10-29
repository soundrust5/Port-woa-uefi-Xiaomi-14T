#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p artifacts/degas-smp-v1/tests
for task_cc in gcc clang; do
 "$task_cc" -std=c11 -Wall -Wextra -Werror -Wno-misleading-indentation \
  -fsanitize=address,undefined -fno-sanitize-recover=all \
  diagnostic/tests/test_degas_cpu_topology.c -o "artifacts/degas-smp-v1/tests/$task_cc"
 "artifacts/degas-smp-v1/tests/$task_cc"
done
