# Current source snapshot — 29 October 2026

The repository now includes the complete current Xiaomi 14T (`degas`, MT6897) platform sources, shared Mu firmware changes, Windows UFS/touch driver sources, experimental CPU/DVFS research drivers, build and packaging scripts, host tests and technical reports.

- Firmware: `mu/Platforms/Xiaomi/degasPkg/`.
- Windows storage/touch: `diagnostic/degas/ufs/windows/` and `diagnostic/degas/touch/windows/`.
- CPU and performance research: `diagnostic/degas-dvfs-windows/`, `diagnostic/degas-dvfs-audit/` and `docs/DEGAS-*.md`.
- Build/package tools: `tools/build_degas_*.sh` and `tools/package_degas_*.py`.
- Exact upstream revision and pinned dependency commits: `source-snapshot.json`.

## Reconstruct dependencies

```sh
git clone --recurse-submodules https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T.git
cd Port-woa-uefi-Xiaomi-14T
git -C mu/Mu_Basecore apply ../../source-patches/Mu_Basecore/current-working-tree.patch
cp -a source-patches/Mu_Basecore/new-files/. mu/Mu_Basecore/
```

Apply only `current-working-tree.patch`. The older `local-changes.patch` is retained for history and must not be applied in addition. The current patch includes all local Basecore changes against the pinned upstream commit; the files in `new-files/` complete it.

## Build requirements and limits

The firmware build uses Linux/WSL, Clang/LLVM, AArch64 binutils and the Python Mu environment. `tools/build_degas_first_dxe.sh` records the current DEBUG firmware command and feature switches; adjust its local virtual-environment path before use. Windows driver scripts also require a separately installed Windows SDK/WDK and MSVC headers, with paths adjusted for your machine. Start with `tools/build_degas_ufs.sh`, `tools/build_degas_touch.sh`, and `diagnostic/degas-dvfs-windows/build_driver.sh` for their corresponding components.

Packaging scripts require your own matching stock Android boot image and local layout/source inputs. This checkout intentionally contains no phone partition dumps, personal registry hives, signing private keys/certificates, Windows installation images, Microsoft driver binaries or NT kernel fixtures. Some reverse-engineering tests require a matching legally obtained NT kernel/PDB fixture at their documented local paths; they cannot run from this source-only checkout alone. Research reports and synthetic host test results are not proof of a new hardware run.

## Current state

Prior physical-device testing established full Windows boot from UFS, all eight CPU cores, UFS read/write, multitouch input and autonomous framebuffer display refresh. CPU DVFS remains unfinished; AMU feedback and PEP/provider integration are research work. GPU acceleration, Wi-Fi and Bluetooth are not implemented. The fullscreen-default experimental release is build/package-qualified; its exact new binary has not yet been booted on the phone.

Original copyright notices and individual dependency licenses remain in force, including [Mu-Silicium's license](mu/LICENSE). This project does not claim authorship of upstream Mu/EDK2 or other third-party components.

The eight-core native context bridge is gated to the exact matching Windows NT kernel SHA-256 `5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`; other kernels are unsupported. The qualified AMU probe sources are restored from the archived `amu-probe-v1/qualified-sources.zip`, independently of ongoing local development. The menu CPU_DVFS_SNAPSHOT route performs an explicit experimental 2 GHz control test; it does not establish working Windows frequency scaling.
