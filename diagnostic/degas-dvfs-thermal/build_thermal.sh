#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
sdk='/mnt/c/Program Files (x86)/Windows Kits/10'
wdk='artifacts/toolchains/wdk-arm64-10.0.26100.6584/c'
vc='/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/include'
out='artifacts/degas/dvfs-windows'
mkdir -p "$out"
clang --target=aarch64-pc-windows-msvc -std=c11 -fms-extensions -fms-compatibility \
  -D_ARM64_ -D_KERNEL_MODE -DNTDDI_VERSION=0x0A00000C -D_WIN32_WINNT=0x0A00 \
  -fno-builtin -fstack-protector-strong -O2 -Wall -Wextra -Werror \
  -Wno-unknown-pragmas -Wno-ignored-attributes \
  -isystem "$vc" -isystem "$sdk/Include/10.0.26100.0/ucrt" \
  -isystem "$sdk/Include/10.0.26100.0/shared" \
  -isystem "$wdk/Include/10.0.26100.0/shared" \
  -isystem "$wdk/Include/10.0.26100.0/km" \
  -c diagnostic/degas-dvfs-windows/degas_thermal.c -o "$out/degas_thermal.obj"
llvm-objdump -d "$out/degas_thermal.obj" > diagnostic/degas-dvfs-thermal/windows-thermal.disasm.txt
clang -std=c11 -Wall -Wextra -Werror diagnostic/degas-dvfs-windows/test_thermal_policy.c \
  -o "$out/test_thermal_policy"
"$out/test_thermal_policy"
echo 'PASS: ARM64 thermal observer compiles; runtime guard tests pass'
