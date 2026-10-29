#!/usr/bin/env bash
set -euo pipefail
cd /mnt/d/Edk2-For-Xiaomi14T
sdk='/mnt/c/Program Files (x86)/Windows Kits/10'
vc='/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/include'
out=artifacts/degas/install-pe
extra=()
case "${1:-}" in
 "") ;;
 --maintenance) out=artifacts/degas/setup-maintenance; extra=(-DDEGAS_MAINTENANCE=1) ;;
 --runtime-diag) out=artifacts/degas/runtime-maintenance; extra=(-DDEGAS_RUNTIME_DIAG=1) ;;
 *) exit 2 ;;
esac
mkdir -p "$out"
clang --target=aarch64-pc-windows-msvc -fms-extensions -fno-builtin -fno-stack-protector -Os -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-ignored-attributes \
 "${extra[@]}" \
 -isystem "$vc" -isystem "$sdk/Include/10.0.26100.0/ucrt" -isystem "$sdk/Include/10.0.26100.0/shared" -isystem "$sdk/Include/10.0.26100.0/um" \
 -c tools/degas_install_bootstrap.c -o "$out/bootstrap.obj"
lld-link /entry:Entry /subsystem:console /nodefaultlib /dynamicbase /nxcompat \
 /out:"$out/degas-install-bootstrap.exe" "$out/bootstrap.obj" \
 /libpath:"$sdk/Lib/10.0.26100.0/um/arm64" kernel32.lib user32.lib setupapi.lib advapi32.lib crypt32.lib
llvm-objdump -p "$out/degas-install-bootstrap.exe" > "$out/imports.txt"
