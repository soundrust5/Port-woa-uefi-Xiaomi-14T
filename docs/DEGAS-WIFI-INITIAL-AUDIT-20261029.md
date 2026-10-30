# Degas / MT6897 Wi-Fi initial implementation audit — 2026-10-29

## Result

Degas uses MediaTek **MT6637 A-die / SOC7_0 Wi-Fi over an on-chip AXI/platform interface**. It is not the OnePlus 12 Qualcomm PCIe architecture. The viable route is a native ARM64 Windows hardware driver with a SOC7_0 hardware/firmware backend and a Windows Wi-Fi frontend. An INF edit, Android `.ko` conversion, DLL shim, or assumed PCIe emulation is not a demonstrated route.

This audit performed offline source analysis and primary-source web research only. No phone action, firmware load, MMIO access, test build, or flash was performed by this audit. The Android observations below were supplied by the separate capture task. Hardware identity from properties is distinguished from a silicon register read, which has **not** been made.

## Actual-device evidence

Evidence directory: `evidence/raw/2026-10-29-degas-wifi-android-audit/`.

| Evidence | Finding |
|---|---|
| `firmware.txt:126,149,152` | `ro.hardware.wlan.chip=mt6637`, `vendor.connsys.wifi.adie.chipid=0x6637`, running firmware `t-neptune-sp-soc7_0-2410-tc8sp-PONSOT_SOC7_0_E1_ASIC-20240618160842` |
| `identity.txt:76-81` | `wlan_drv_gen4m_6897`, `wmt_chrdev_wifi_connac2`, `conninfra`, `connfem`, `connadp`; conninfra and connfem are used by both WLAN and `bt_drv_6897` |
| `platform.txt:13-20`; `network.txt:118` | `18000000.wifi`, platform driver `wlan`, compatible `mediatek,wifi`; wlan0 resolves to that platform device |
| `firmware.txt:18-20` | BT filenames `soc7_0_ram_bt_1_1_hdr.bin`, `soc7_0_ram_bt_1a_1_hdr.bin`, `soc7_0_ram_bt_1b_1_hdr.bin` |

The presence of generic loaded `btsdio` or `hci_uart` modules does not identify this WLAN transport. The actual WLAN parent and matching vendor build configuration do.

Root's later read-only filename follow-up, `firmware-paths-followup.txt`, additionally identifies `/vendor/firmware/soc7_0_ram_mcu_{1,1a,1b}_1_hdr.bin` alongside the BT images and `wifi.cfg`. These names supplement the source templates below; the complete Wi-Fi image selection, hashes and firmware-to-ECO match remain to be established.

Resource extraction is `prior-live-fdt-wifi-resources.json`, derived offline from the previously captured live FDT `evidence/raw/2026-10-27-degas-windows-ufs-baseline/fdt.dtb`, SHA256 `29775b7b4361d040817d77bf130039966200055d0e2ae86eb02d63661bd61e68`:

- Wi-Fi MMIO: `0x18000000 + 0x700000`.
- DT interrupts: SPI `0x379`, SPI `0x265`, level high. Their GIC INTIDs are 921 and 645 respectively (SPI number + 32); verify ACPI GSIV treatment against this port's GIC implementation before publishing `_CRS`.
- Wi-Fi reserved DMA pool: `0x88b00000 + 0x1400000`, `no-map` shared DMA pool. Preserve this reservation; it is not ordinary free UEFI/Windows RAM.
- `consys@18000000`: `mediatek,mt6897-consys-atf`; connectivity EMI base `0x87e00000`. Consys node advertises `0xc00000`, reserved-memory node reserves `0xd00000`. Resolve the distinction, do not reduce the reservation.
- Shared EMI partitions include BT `0x2e0000`, Wi-Fi `0x300000`, GPS `0xa0000`, RO total `0x680000`, RW `0x480000`. These are shared connectivity resources, not a private Wi-Fi PCI BAR.

## Source identities and useful implementation evidence

Primary repositories inspected:

1. [MiCode/MTK_kernel_modules, bsp-degas-u-oss](https://github.com/MiCode/MTK_kernel_modules/tree/deb8efbbd16f46f16f741b34090326fb8ac75662), local `evidence/sources/degas-modules`, HEAD `deb8efbbd16f46f16f741b34090326fb8ac75662`.
2. [MiCode/MTK_kernel_device_modules, bsp-duchamp-u-oss](https://github.com/MiCode/MTK_kernel_device_modules/tree/3c4ccd07d5624879555ec710c11bb8818e5cb5c6), local `evidence/sources/mt6897-device-modules`, HEAD `3c4ccd07d5624879555ec710c11bb8818e5cb5c6`.
3. [MiCode/MTK_kernel_modules, bsp-rodin-v-oss](https://github.com/MiCode/MTK_kernel_modules/tree/26e64cd4c795b2ff2c07d530b956beabfdada2f1), exact comparison commit `26e64cd4c795b2ff2c07d530b956beabfdada2f1`, fetched into `degas-modules` object store without changing its checkout. **Cross-device, later BSP reference; not proof of stock binary equivalence.**

The degas module tree does contain `gen4-mt79xx` and AXI glue, but lacks `chips/soc7_0` and conninfra. The device-modules tree also lacks the connectivity implementation. The rodin comparison tree supplies SOC7_0 and `Kbuild.6897`, closing part of that source gap. It does not close the full matching MT6897 conninfra platform gap: its platform table has weak empty `mt6897_plat_data[_atf]` declarations and matching DT entries, while `platform/mt6897` implementation is absent from the inspected tree.

Exact reference files (all links pinned to the comparison commit):

- [Kbuild.6897 lines 5–12](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/Kbuild.6897#L5-L12): `CONNAC2X2_SOC7_0`, HIF `axi`, module `wlan_drv_gen4m_6897`.
- [soc7_0.h lines 25–34](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/include/chips/soc7_0.h#L25-L34): backend chip ID `0x7961`, RX descriptor length 24, TX append length 32. **This internal chip ID does not make the phone a PCI VEN_14C3 DEV_7961 adapter.**
- [soc7_0.c lines 223–292](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/chips/soc7_0/soc7_0.c#L223-L292): AXI chip-to-host mapping, including chip `0x830c0000` to AP `0x18400000`, CONN_INFRA and FW download remap apertures. Struct names and comments containing `PCIE` are shared vendor abstractions, not evidence of a PCIe attachment.
- [axi.c lines 194–296](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/os/linux/hif/axi/axi.c#L194-L296): reserved-memory DMA setup and resource mapping; [lines 667–725](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/os/linux/hif/axi/axi.c#L667-L725): DT IRQ mapping and ISR registration.
- [soc7_0.c lines 470–492](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/chips/soc7_0/soc7_0.c#L470-L492): Connac-format download, EMI download and MCU init callbacks. Generic patch callback is NULL; do not transplant an unrelated MT7921 patch procedure.
- [soc7_0.c lines 849–910](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/chips/soc7_0/soc7_0.c#L849-L910): `WIFI_RAM_CODE_soc7_0_...` and `soc7_0_ram_wmmcu_..._hdr.bin` naming by IP set/flavor/ECO. These are source naming templates, not yet verified filenames from the phone's complete firmware inventory.
- [soc7_0.c lines 1320–1375](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/chips/soc7_0/soc7_0.c#L1320-L1375): wake operation is a **write** to `0x180601a4`, followed by 200 us delay, CONNSYS version polling at `0x18011000`, then restore status polling. This is not an unconditional safe read-only identification routine.
- [soc7_0.c lines 1398–1590](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/chips/soc7_0/soc7_0.c#L1398-L1590): power/reset/sleep protection and WFSYS IP version check `0x184b0010`. [Lines 2104–2159](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/chips/soc7_0/soc7_0.c#L2104-L2159): MCU initialization, host/firmware ownership synchronization and PCCIF operations.
- [gl_kal.c lines 15576–15602](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/wlan/core/gen4m/os/linux/gl_kal.c#L15576-L15602): `MTK_SIP_KERNEL_WLAN_CONTROL` SMC wrapper, including unsupported-operation/register failure handling. [conninfra platform table lines 25–26,73–82](https://github.com/MiCode/MTK_kernel_modules/blob/26e64cd4c795b2ff2c07d530b956beabfdada2f1/connectivity/conninfra/conn_drv/connv2/platform/consys_hw_plat_data.c#L25-L82): MT6897 weak data and ATF-compatible selection.

Selected reference files are saved under `evidence/sources/wifi-audit-20261029/` with path separators replaced by `__`; `source-manifest.json` records paths, commits and SHA256 of these local UTF-8 extracts. Original authoritative bytes remain available via `git show` of the pinned commit.

## Existing Windows driver assessment

MediaTek ARM64 Wi-Fi drivers **do exist**. The [official Microsoft Catalog package 6.4.0.3279, update e5f218d5-5944-4969-a21d-a30d1f12d3e8](https://www.catalog.update.microsoft.com/ScopedViewInline.aspx?updateid=e5f218d5-5944-4969-a21d-a30d1f12d3e8) explicitly declares ARM64, dated July 1, 2026. Its supported IDs are PCI devices (DEV_6639, DEV_7925, DEV_7927 and rebrand IDs 0717/0738); no MT6637 platform ID is listed. Saved official HTML: `catalog-mediatek-6.4.0.3279.html`, architecture at lines 329–331 and IDs starting line 491. This package is an investigation reference, **not a compatible degas driver**; its binaries/INF were not downloaded or tested.

Binding a driver requires matching architecture, bus interface, register/DMA layouts, interrupts, firmware protocol, power/security interface and board calibration. Sharing the MediaTek brand, internal `0x7961`, or ARM64 CPU alone proves none of those contracts. Adding an ACPI ID or fake PCI ID cannot supply an absent hardware interface. No existing Windows ARM64 driver matching this phone was verified in this audit; this is a bounded search result, not proof none exists privately.

Microsoft states that kernel drivers must be [native ARM64; application emulation does not emulate drivers](https://learn.microsoft.com/en-us/windows/arm/apps-on-arm-x86-emulation). Build a standard ARM64 WDK target; ARM64EC and x64 `.sys` files are not a driver compatibility solution. WDK [supports ARM64 driver builds and target debugging](https://learn.microsoft.com/en-us/windows-hardware/drivers/develop/building-arm64-drivers).

For Windows 11, [WiFiCx](https://learn.microsoft.com/en-us/windows-hardware/drivers/netcx/wifi-wdf-class-extension-wificx) supplies KMDF/PnP/power, NetAdapterCx data path and Wi-Fi control interfaces. Microsoft [recommends WiFiCx; WDI is in maintenance](https://learn.microsoft.com/en-us/windows-hardware/drivers/network/wdi-miniport-driver-design-guide). A new backend must still implement the radio/firmware operations behind scan, connect, keys, capabilities, receive/transmit, reset and cancellation. Follow the [WiFiCx client guide](https://learn.microsoft.com/en-us/windows-hardware/drivers/netcx/writing-a-wificx-client-driver) for device/adapter lifecycle and asynchronous WDI command/indication handling. Framework availability does not implement a MediaTek firmware protocol.

| Linux vendor component | Reuse potential / Windows work |
|---|---|
| SOC7_0 register tables, descriptors, firmware section formats, command/event definitions | Best factual backend reference; retain exact chip/ECO checks and review each register side effect. WLAN files inspected carry BSD-2-Clause headers; review license per file before reuse. |
| MCU bootstrap, ownership, WFDMA ring and DMASHDL logic | Port algorithm carefully only after matching stock configuration and secure-world behavior. Replace DMA allocation/mapping, barriers, locking, ISR/DPC and teardown with Windows contracts. |
| conninfra/connfem/board calibration | Required platform service layer: rails, clocks, RF SPI, shared EMI, semaphores, resets, calibration, ATF calls and coexistence. Missing matching MT6897 implementation is a concrete blocker. |
| `cfg80211`, netdev/sk_buff, NAPI, Linux threads/workqueues, procfs, Android HAL | Cannot be linked into Windows. Replace with WDF/NetAdapterCx/WiFiCx objects, buffers, queues and command handling. No reason to port the Android userspace HAL for initial station support. |
| Existing Android firmware | Candidate device-specific payload and protocol evidence; hash and identify actual images/board calibration first. Android AArch64 `.ko` code itself is not a Windows driver. |

## First realistic UEFI test and bring-up gates

The first test should be **resource/identity qualification**, with no firmware bootstrap or radio enable. Implement it only after source review; this report does not add executable code.

1. Offline: confirm all live-FDT resources, source hashes, firmware images, ECO/flavor, calibration ownership, reservations and MT6897 ATF ABI. Verify exact expected CONNSYS/WFSYS project versions instead of guessing from the SoC marketing name. Identify which status/version registers remain readable with domains off and whether reads require a host wake/clock request. Review clear-on-read behavior.
2. UEFI qualification: print metadata and memory reservations first. Verify existing platform power/clock/protection state using only registers already established accessible by this port. If that cannot establish access, print `SKIP: domain accessibility unproven`; **do not touch the entire 7 MiB block to discover it**. A bounded software poll cannot recover a synchronous bus transaction that never returns.
3. Conditional read-only identity: only with accessibility proven and no enabling writes, take a small whitelist of 32-bit reads (candidate CONNSYS version `0x18011000`, candidate WFSYS version `0x184b0010` only if WFSYS separately proven on). Record address/value/domain prerequisites and skip reasons to the existing persistent journal. No remap writes, clock requests, FW-own clear, W1C interrupt acknowledgments, DMA/ring enable, RF SPI or secure power SMC. These values qualify IP versions; the A-die `0x6637` identity likely needs the validated conninfra RF/ATF path and is not yet a proven read-only MMIO test.
4. Separate later active stage: reconstruct matching MT6897 conninfra/ATF power-on, rails/clocks/semaphores and memory protection; establish an explicit bounded failure/cleanup path and shared-radio reference counts. Then WFSYS reset/sleep protection, version checks and ownership synchronization. The reference has MT6897-specific semaphore behavior, so copying a neighboring SoC is insufficient.
5. Only after stage 4: validate firmware image integrity/flavor/ECO and calibration; set up reserved EMI/remaps; load the matching ROM/MCU/Wi-Fi sections and wait for a documented ready indication. Set up DMA allocations with proven device address width/coherency/IOMMU handling, then masked interrupts/rings; prove command/event exchange before enabling packet queues.
6. Windows path: describe the platform device and real resources in ACPI; begin with an ARM64 KMDF diagnostic backend, then firmware query/command tests, then WiFiCx station-only scan → association/key installation → packet exchange → disconnect/reset. Power transitions and failed-init rollback are required before broad feature work. Bluetooth comes afterward, but its shared conninfra/connfem resources must be preserved from stage 4 onward.

These gates allow progress without repeating weeks of unrelated Qualcomm PCIe bring-up. The useful next deliverable is the **matching MT6897 conninfra/ATF contract and complete hashed firmware inventory**, followed by a reviewed diagnostic whitelist. A full Windows WLAN driver is substantial engineering beyond proving UEFI register access.
