#!/usr/bin/env bash
# Compile shared Mu modules with the existing stone dependency graph only.
# Optional control-fd mode builds the donor RELEASE FD for host inspection.
# No Android packaging or phone commands are performed in either mode.
set -eo pipefail
mode="control-fd"
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
common_args=(-p Platforms/Xiaomi/degasPkg/degas.dsc -a AARCH64 -t CLANGPDB -n 8
    -D DEGAS_CPU_PARKING_EXPERIMENT="${DEGAS_CPU_PARKING_EXPERIMENT:-0}"
    -D DEGAS_DIAGNOSTIC_EBS_WATCHDOG="${DEGAS_DIAGNOSTIC_EBS_WATCHDOG:-0}"
    -D DEGAS_BOOT_CORE="${DEGAS_BOOT_CORE:-0}"
    -D DEGAS_CPU_TEST_LIMIT="${DEGAS_CPU_TEST_LIMIT:-8}"
    -D DEGAS_WINDOWS_IDENTITY_OBSERVER="${DEGAS_WINDOWS_IDENTITY_OBSERVER:-0}"
    -D DEGAS_WINDOWS_BUGCHECK_MONITOR="${DEGAS_WINDOWS_BUGCHECK_MONITOR:-0}"
    -D DEGAS_NATIVE_CONTEXT_BRIDGE="${DEGAS_NATIVE_CONTEXT_BRIDGE:-0}"
    -D DEGAS_CPU_TOPOLOGY="${DEGAS_CPU_TOPOLOGY:-0}"
    -D FD_BASE=0x40200000 -D FD_SIZE=0x200000 -D FD_BLOCKS=0x200
    -D ENABLE_SECUREBOOT=0 -D DEVICE_MODEL=0 -D MEMORY_PROTECTION=TRUE -D SHIP_MODE=FALSE)
if [ "$mode" = control-fd ]; then
    log="$task_root/evidence/build/degas-first-dxe.log"
    echo "Building degas DEBUG minimal SEC/DXE checkpoint firmware"
    if build "${common_args[@]}" -b DEBUG > "$log" 2>&1; then
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
