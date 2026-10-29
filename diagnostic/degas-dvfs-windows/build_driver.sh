#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
sdk='/mnt/c/Program Files (x86)/Windows Kits/10'
wdk='artifacts/toolchains/wdk-arm64-10.0.26100.6584/c'
vc='/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/include'
src='diagnostic/degas-dvfs-windows'
out='artifacts/degas/dvfs-windows'
mkdir -p "$out"
clang -std=c11 -Wall -Wextra -Werror "$src/degas_protocol.c" "$src/test_protocol.c" -o "$out/test_protocol"
"$out/test_protocol"
clang -std=c11 -Wall -Wextra -Werror -I "$src/tests" "$src/test_amu.c" -o "$out/test_amu"
"$out/test_amu"
flags=(--target=aarch64-pc-windows-msvc -fms-extensions -fms-compatibility
  -D_ARM64_ -D_KERNEL_MODE -DNTDDI_VERSION=0x0A00000C -D_WIN32_WINNT=0x0A00
  -fno-builtin -fstack-protector-strong -O2 -Wall -Wextra -Werror
  -Wno-unknown-pragmas -Wno-ignored-attributes
  -isystem "$vc" -isystem "$sdk/Include/10.0.26100.0/ucrt"
  -isystem "$sdk/Include/10.0.26100.0/shared"
  -isystem "$wdk/Include/10.0.26100.0/shared"
  -isystem "$wdk/Include/10.0.26100.0/km")
for unit in degas_protocol degas_pep; do
  clang "${flags[@]}" -c "$src/$unit.c" -o "$out/$unit.obj"
done
# Root supplies the qualified hardware backend separately.
test -f "$src/degas_backend.c"
clang "${flags[@]}" -c "$src/degas_backend.c" -o "$out/degas_backend.obj"
clang "${flags[@]}" -c "$src/degas_thermal.c" -o "$out/degas_thermal.obj"
clang "${flags[@]}" -c "$src/degas_ramlog.c" -o "$out/degas_ramlog.obj"
clang "${flags[@]}" -c "$src/degas_amu.c" -o "$out/degas_amu.obj"
lld-link /driver /subsystem:native /entry:DriverEntry /nodefaultlib /machine:arm64 \
  /dynamicbase /nxcompat /integritycheck /tsaware:no /timestamp:0 \
  /out:"$out/degasdvfs.sys" "$out/degas_pep.obj" "$out/degas_protocol.obj" "$out/degas_backend.obj" "$out/degas_thermal.obj" "$out/degas_ramlog.obj" "$out/degas_amu.obj" \
  /libpath:"$wdk/Lib/10.0.26100.0/km/ARM64" \
  ntoskrnl.lib hal.lib libcntpr.lib bufferoverflowfastfailk.lib wdmsec.lib
llvm-objdump -p "$out/degasdvfs.sys" > "$out/imports.txt"
echo 'PASS: ARM64 performance PEP linked; no phone installation'
