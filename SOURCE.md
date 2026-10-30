# Source and build

Firmware is in `mu/Platforms/Xiaomi/degasPkg/`, Windows drivers in `diagnostic/degas/`, the Android app in `apps/woa-helper-degas/`, and build scripts in `tools/`. Dependency revisions are recorded in [source-snapshot.json](source-snapshot.json).

## Dependencies

```sh
git clone --recurse-submodules https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T.git
cd Port-woa-uefi-Xiaomi-14T
git -C mu/Mu_Basecore apply ../../source-patches/Mu_Basecore/current-working-tree.patch
cp -a source-patches/Mu_Basecore/new-files/. mu/Mu_Basecore/
```

Apply only `current-working-tree.patch`; do not also apply the older `local-changes.patch`.

## Build

UEFI needs Linux/WSL, Clang/LLVM, AArch64 binutils and the Mu Python environment. Start with `tools/build_degas_first_dxe.sh` and adjust its local paths. Windows driver builds also need the Windows SDK/WDK and MSVC headers. Packaging requires your own matching stock Android boot image.

The eight-core bridge supports only the Windows kernel identified in `mu/Platforms/Xiaomi/degasPkg/Include/NativeContextFixture.h`. Some research tests also need local kernel/PDB files, which are not included.

The [UEFI release](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/releases/latest) includes the exact build configuration, source hashes and `qualified-sources.zip`. Use the ZIP to verify original file hashes without Git line-ending conversion. See [project status](guide/English/status.md) for current support.

The Helper retains upstream toolbox assets. Its `third_party/` directory contains matching NTFS-3G source and COPYING.

Original copyright notices and dependency licenses remain in force, including [Mu-Silicium's license](mu/LICENSE). This project does not claim authorship of upstream Mu/EDK2 or other third-party code.
