#!/usr/bin/env bash
# Compile shared Mu modules with the existing stone dependency graph only.
# Optional control-fd mode builds the donor RELEASE FD for host inspection.
# No Android packaging or phone commands are performed in either mode.
set -eo pipefail
mode="${1:-modules}"
case "$mode" in modules|control-fd) ;; *) echo "Usage: $0 [modules|control-fd]"; exit 2 ;; esac
task_root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$task_root/mu"
source /home/gavso/.venvs/degas-mu/bin/activate
export WORKSPACE="$PWD"
export EDK_TOOLS_PATH="$PWD/Mu_Basecore/BaseTools"
export PACKAGES_PATH="$PWD/Platforms/Xiaomi:$PWD/Common/Mu:$PWD/Common/Mu_OEM_Sample:$PWD/Mu_Basecore:$PWD/Silicon/MediaTek:$PWD/Silicon/Silicium:$PWD/Silicium-ACPI:$PWD/Silicium-ACPI/Platforms/Xiaomi:$PWD/Silicium-ACPI/Silicon/MediaTek"
export PYTHON_COMMAND=python3
export CLANG_BIN=/usr/bin/
export CLANG_HOST_BIN=/usr/bin/
export CLANGPDB_AARCH64_PREFIX=aarch64-linux-gnu-
export RECONFIG=FALSE
mkdir -p "$WORKSPACE/Conf"
source "$EDK_TOOLS_PATH/BuildEnv"
mkdir -p "$task_root/evidence/build"
common_args=(-p Platforms/Xiaomi/stonePkg/stone.dsc -a AARCH64 -t CLANGPDB -n 8
    -D FD_BASE=0x40000000 -D FD_SIZE=0x200000 -D FD_BLOCKS=0x200
    -D ENABLE_SECUREBOOT=0 -D DEVICE_MODEL=0 -D MEMORY_PROTECTION=TRUE -D SHIP_MODE=FALSE)
if [ "$mode" = control-fd ]; then
    log="$task_root/evidence/build/mu-control-fd.log"
    echo "Building stone RELEASE control FD with donor PCDs; not a degas image"
    if build "${common_args[@]}" -b RELEASE > "$log" 2>&1; then
        tail -n 12 "$log"
    else
        tail -n 65 "$log"
        exit 1
    fi
    exit 0
fi
common_args+=(-b DEBUG
    --pcd gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth=1220
    --pcd gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight=2712
    --pcd gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth=32
    --pcd gSiliciumPkgTokenSpaceGuid.PcdFrameBufferPixelsPerScanLine=1248)
for module in Silicon/Silicium/SiliciumPkg/Sec/Sec.inf Silicon/Silicium/SiliciumPkg/Drivers/SimpleFbDxe/SimpleFbDxe.inf; do
    name="$(basename -- "$module" .inf)"
    log="$task_root/evidence/build/mu-$name.log"
    echo "Compiling $name (donor graph, degas display PCDs, module-only)"
    if build "${common_args[@]}" -m "$module" > "$log" 2>&1; then
        tail -n 8 "$log"
    else
        tail -n 65 "$log"
        exit 1
    fi
done
