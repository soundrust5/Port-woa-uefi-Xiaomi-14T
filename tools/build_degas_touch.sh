#!/usr/bin/env bash
# Compile/link experimental KMDF client; no installation or phone access.
set -euo pipefail
cd "$(dirname "$0")/.."
sdk='/mnt/c/Program Files (x86)/Windows Kits/10'
wdk='artifacts/toolchains/wdk-arm64-10.0.26100.6584/c'
vc='/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/include'
out='artifacts/degas/touch-windows'
mkdir -p "$out"
test -f "$wdk/Include/10.0.26100.0/km/ntddk.h"
flags=(--target=aarch64-pc-windows-msvc -fms-extensions -fms-compatibility
  -D_ARM64_ -D_KERNEL_MODE -DNTDDI_VERSION=0x0A00000C -D_WIN32_WINNT=0x0A00
  -DKMDF_VERSION_MAJOR=1 -DKMDF_VERSION_MINOR=33 -DNTSTRSAFE_LIB -fno-builtin -fstack-protector-strong -O2
  -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-ignored-attributes
  -isystem "$vc" -isystem "$sdk/Include/10.0.26100.0/ucrt"
  -isystem "$sdk/Include/10.0.26100.0/shared"
  -isystem "$wdk/Include/10.0.26100.0/shared"
  -isystem "$wdk/Include/10.0.26100.0/km"
  -isystem "$wdk/Include/wdf/kmdf/1.33")
for unit in degastouch degas_vhf; do
  clang "${flags[@]}" -c "diagnostic/degas/touch/windows/$unit.c" -o "$out/$unit.obj"
done
lld-link /driver /subsystem:native /entry:FxDriverEntry /nodefaultlib /machine:arm64 \
  /dynamicbase /nxcompat /integritycheck /tsaware:no /timestamp:0 \
  /out:"$out/degastouch.sys" "$out/degastouch.obj" "$out/degas_vhf.obj" \
  /libpath:"$wdk/Lib/10.0.26100.0/km/ARM64" \
  /libpath:"$wdk/Lib/wdf/kmdf/ARM64/1.33" \
  ntoskrnl.lib hal.lib ntstrsafe.lib libcntpr.lib bufferoverflowfastfailk.lib WdfDriverEntry.lib WdfLdr.lib vhfkm.lib
cp diagnostic/degas/touch/windows/degastouch.inf "$out/degastouch.inf"
cp diagnostic/degas/touch/windows/degasvhf.inf "$out/degasvhf.inf"
llvm-objdump -p "$out/degastouch.sys" > "$out/imports.txt"
echo 'PASS: ARM64 FT3683 KMDF/VHF client linked; no installation'
