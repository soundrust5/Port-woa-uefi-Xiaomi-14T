#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
out=artifacts/degas/prcb-walk
mkdir -p "$out"
clang --target=aarch64-none-elf -c diagnostic/degas-prcb-walk/walk.S -o "$out/walk.o"
ld.lld --image-base=0 -Ttext=0x100000 --entry=DegasPrcbRead64 "$out/walk.o" -o "$out/walk.elf"
llvm-objcopy -O binary --only-section=.text "$out/walk.elf" "$out/walk.bin"
llvm-objdump -d "$out/walk.elf" > "$out/walk.disasm"
