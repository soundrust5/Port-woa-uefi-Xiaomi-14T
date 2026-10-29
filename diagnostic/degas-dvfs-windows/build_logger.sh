#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
sdk='/mnt/c/Program Files (x86)/Windows Kits/10'
vc='/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/include'
out=artifacts/degas/dvfs-windows
mkdir -p "$out"
clang --target=aarch64-pc-windows-msvc -fms-extensions -fno-builtin -fno-stack-protector -O2 -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-ignored-attributes -isystem "$vc" -isystem "$sdk/Include/10.0.26100.0/ucrt" -isystem "$sdk/Include/10.0.26100.0/shared" -isystem "$sdk/Include/10.0.26100.0/um" -c diagnostic/degas-dvfs-windows/degas_logger.c -o "$out/logger.obj"
lld-link /entry:Entry /subsystem:console /nodefaultlib /dynamicbase /nxcompat /out:"$out/DegasDvfsLog.exe" "$out/logger.obj" /libpath:"$sdk/Lib/10.0.26100.0/um/arm64" kernel32.lib advapi32.lib user32.lib
