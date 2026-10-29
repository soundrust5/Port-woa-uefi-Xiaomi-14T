#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p out
clang --target=aarch64-none-elf -c bridge.S -o out/bridge.o
ld.lld --image-base=0 -Ttext=0x100000 --entry=DegasContextBridge out/bridge.o -o out/bridge.elf
llvm-objcopy -O binary --only-section=.text out/bridge.elf out/bridge.bin
llvm-objdump -d out/bridge.elf > out/bridge.disasm
