#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/.."
sdk='/mnt/c/Program Files (x86)/Windows Kits/10'
wdk='artifacts/toolchains/wdk-arm64-10.0.26100.6584/c'
vc='/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/include'
out=artifacts/degas/ufs-windows
extra=()
case "${1:-}" in
 "") ;;
 --install) out="$out/install-candidate"; extra=(-DDEGAS_UFS_INSTALL_MODE=1) ;;
 --performance) out="$out/performance-candidate"; extra=(-DDEGAS_UFS_INSTALL_MODE=1 -DDEGAS_UFS_PERFORMANCE=1) ;;
 *) echo 'Usage: build_degas_ufs.sh [--install|--performance]'; exit 2 ;;
esac
mkdir -p "$out"
clang --target=aarch64-pc-windows-msvc -fms-extensions -fms-compatibility \
 "${extra[@]}" \
 -D_ARM64_ -D_KERNEL_MODE -DNTDDI_VERSION=0x0A00000C -D_WIN32_WINNT=0x0A00 \
 -fno-builtin -fstack-protector-strong -O2 -Wall -Wextra -Werror \
 -Wno-unknown-pragmas -Wno-ignored-attributes \
 -isystem "$vc" -isystem "$sdk/Include/10.0.26100.0/ucrt" \
 -isystem "$sdk/Include/10.0.26100.0/shared" -isystem "$wdk/Include/10.0.26100.0/shared" \
 -isystem "$wdk/Include/10.0.26100.0/km" \
 -c diagnostic/degas/ufs/windows/degasufs.c -o "$out/degasufs.obj"
lld-link /driver /subsystem:native /entry:GsDriverEntry /nodefaultlib /machine:arm64 \
 /dynamicbase /nxcompat /integritycheck /tsaware:no /timestamp:0 \
 /out:"$out/degasufs.sys" "$out/degasufs.obj" \
 /libpath:"$wdk/Lib/10.0.26100.0/km/ARM64" \
 storport.lib ntoskrnl.lib hal.lib libcntpr.lib bufferoverflowfastfailk.lib
llvm-objdump -p "$out/degasufs.sys" > "$out/imports.txt"
