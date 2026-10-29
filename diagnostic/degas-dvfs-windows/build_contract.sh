#!/usr/bin/env bash
# Host-only ABI compilation; produces an ARM64 object, no loadable driver.
set -euo pipefail
cd "$(dirname "$0")/../.."
sdk='/mnt/c/Program Files (x86)/Windows Kits/10'
wdk='artifacts/toolchains/wdk-arm64-10.0.26100.6584/c'
vc='/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/include'
out='diagnostic/degas-dvfs-windows'
clang --target=aarch64-pc-windows-msvc -fms-extensions -fms-compatibility \
  -D_ARM64_ -D_KERNEL_MODE -DNTDDI_VERSION=0x0A00000C -D_WIN32_WINNT=0x0A00 \
  -fno-builtin -O2 -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-ignored-attributes \
  -isystem "$vc" -isystem "$sdk/Include/10.0.26100.0/ucrt" \
  -isystem "$sdk/Include/10.0.26100.0/shared" \
  -isystem "$wdk/Include/10.0.26100.0/shared" \
  -isystem "$wdk/Include/10.0.26100.0/km" \
  -c "$out/pep_contract.c" -o "$out/pep_contract.obj"
llvm-objdump -t "$out/pep_contract.obj" > "$out/pep_contract.symbols.txt"
echo 'PASS: WDK ARM64 PEP callback ABI compiled; no registration, driver, or MMIO'
