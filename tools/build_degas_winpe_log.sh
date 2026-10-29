#!/bin/bash
set -euo pipefail
cd /mnt/d/Edk2-For-Xiaomi14T
sdk='/mnt/c/Program Files (x86)/Windows Kits/10'
ver=10.0.26100.0
vc='/mnt/c/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/include'
out=artifacts/degas/winpe-log
mkdir -p "$out"
for arch in arm64 x64; do
  target=aarch64-pc-windows-msvc
  if [[ $arch == x64 ]]; then target=x86_64-pc-windows-msvc; fi
  clang --target="$target" -fms-extensions -fno-builtin -fno-stack-protector -Os -Wall -Wextra -Werror -Wno-unknown-pragmas -Wno-ignored-attributes \
    -isystem "$vc" -isystem "$sdk/Include/$ver/ucrt" -isystem "$sdk/Include/$ver/shared" -isystem "$sdk/Include/$ver/um" \
    -c diagnostic/degas/winpe-log/degaslog.c -o "$out/degaslog-$arch.obj"
  lld-link /entry:Entry /subsystem:windows /nodefaultlib /dynamicbase /nxcompat \
    /out:"$out/degaslog-$arch.exe" "$out/degaslog-$arch.obj" \
    /libpath:"$sdk/Lib/$ver/um/$arch" kernel32.lib user32.lib gdi32.lib setupapi.lib cfgmgr32.lib advapi32.lib crypt32.lib
  llvm-objdump -p "$out/degaslog-$arch.exe" > "$out/imports-$arch.txt"
done
