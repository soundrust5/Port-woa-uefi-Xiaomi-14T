# Degas MT6897 conninfra ATF contract — 2026-10-29

Update 2026-10-30: the live private trace in
`evidence/raw/2026-10-30-degas-native-conn-abi` resolves the formerly unknown
environment field +0xc to clock enum 1 (52 MHz co-clock), observes platform
config 0, and records successful cold SPI/A-die calls with `(current=0,next=1)`
returning A-die ID `0x66378a01`. See `DEGAS-WIFI-ADIE-ATF-20261030.md`.
The unresolved wording below is the original audit state, not the latest result.

The actual Android `conninfra.ko` establishes **SMC function ID `0xC2000534`**, operation numbers, argument widths, and the mixed ATF/Linux power dependencies. This is enough to specify a development interface; it is not yet a complete portable power-on implementation or evidence that any of these calls are safe from UEFI.

## Primary evidence and reproducibility

- Exact device module: `evidence/raw/2026-10-29-degas-wifi-contract-followup/conninfra.ko`, 1,483,528 bytes, SHA256 `9bf6b33149a961706a64188c0a50f8076638b797bcf64759ae227c92f8c0b324`.
- ELF is AArch64 relocatable with symbols and relocation records. Function extents were decoded with existing `tools/disassemble_elf_functions.py`, Capstone 5.0.9 and bundled pyelftools. Relocations identify call targets; unrelocated branch operands in ET_REL disassembly are not runtime target addresses.
- `.../atf-analysis/mt6897-disassembly.txt`: 171 exact function extents, annotated relocations and referenced diagnostic strings.
- `.../atf-analysis/mt6897-function-tables.json`: 11 platform/operation tables, byte contents and relocations.
- `.../atf-analysis/mt6897-smc-contract.json` and `.csv`: 33 SMC wrapper entries with decoded argument transfers. Each named wrapper was inspected in the disassembly; the table does not infer secure-firmware implementation.
- Earlier live FDT: `evidence/raw/2026-10-27-degas-windows-ufs-baseline/fdt.dtb`, SHA256 `29775b7b4361d040817d77bf130039966200055d0e2ae86eb02d63661bd61e68`; extracted connectivity nodes in `evidence/raw/2026-10-29-degas-wifi-android-audit/prior-live-fdt-wifi-resources.json`.

No phone command, MMIO read/write, SMC invocation, power toggle, or driver unload was performed for this analysis. No active UEFI or Windows driver sources were changed.

## Selected platform and function tables

Live DT compatible `mediatek,mt6897-consys-atf` selects the ATF platform according to the pinned reference match table. The binary contains a real nonzero `mt6897_plat_data_atf` object, `.rodata+0x3500`, size56. Its first words are `0x6897` and `0x02050400`; the second word's meaning is not established from the binary alone.

| Platform field offset | Exact binary target |
|---|---|
| +0x08 | g_consys_hw_ops_mt6897_atf |
| +0x10 | g_dev_consys_reg_ops_mt6897_atf |
| +0x18 | g_consys_platform_emi_ops_mt6897_atf |
| +0x20 | g_consys_platform_pmic_ops_mt6897 |
| +0x28 | g_consys_platform_coredump_ops_mt6897_atf |

The hardware table is `.data+0x13c8`, size376. Significant slots: +0x20 init ATF; +0x28 **non-ATF** `consys_conninfra_on_power_ctrl_mt6897`; +0x30 non-ATF pinmux; +0x38 ATF chip polling; +0x90/+0x98 ATF wake/sleep; +0xc0/+0xc8/+0xd0 ATF SPI read/write/update; +0xf0/+0xf8 ATF ADIE clock on/off; +0x108 ATF subsystem status. Full offsets and null slots are in JSON. Do not substitute table order or structures from a different SoC without checking offsets.

## Decoded SMC ABI

Every captured MT6897 ATF wrapper constructs `w0=0xC2000534` and calls relocated `__arm_smccc_smc`. `x1` is operation ID; arguments use x2..x7. Unused arguments are zero in these wrappers. The result structure belongs to the Linux SMCCC helper and is passed on the C stack: it is **not an x8 secure-world buffer argument**. A UEFI SMCCC bridge must collect returned x0..x3 directly, preserving the calling ABI. Most integer wrappers return low32 of `res.a0`; do not discard the raw result in a diagnostic implementation.

`u32(argN)` means zero-extended incoming wN. `sxtw` means sign-extended low32. Semantic names below follow the symbol/reference enum; incoming argument numbers are the binary-proven part.

| x1 op | Wrapper role | x2 onward / result detail |
|---:|---|---|
| 0 | init ATF data | x2=sxtw(platform config getter); x3=u32(conn_hw_env+0xc); rest0. Local once flag set after call even if return is failure. Meaning/current value of environment +0xc remains unresolved. |
| 10 | poll chip ID | all args0; integer result. Not a generic arbitrary register read. |
| 11 | D-die config | all0 |
| 12 | SPI master config | x2=u32(arg0), x3=u32(arg1) |
| 13 | A-die config | same two args; res.a1 low32 saved to conn_hw_env+0 |
| 14,15,16 | AFE patch, AFE calibration, subsystem PLL init | all0; patch wrapper ignores result |
| 17 | low-power setting | x2=u32(arg0), x3=u32(arg1) |
| 18,19 | conninfra wake, sleep | all0; **state-changing ownership operations** |
| 20 | SPI read | x2=u32(arg0), x3=u32(arg1); res.a1 low32 written to caller's output pointer; res.a0 low32 returned |
| 21 | SPI write | x2=u32(arg0), x3=u32(arg1), x4=u32(arg2) |
| 22 | SPI update bits | x2..x5=u32(arg0..arg3); exact mask/data semantic order still needs caller/source confirmation |
| 23 | SPI clock switch | x2=u32(arg0) |
| 24 | subsystem status update | x2=arg0 &1; x3=sxtw(arg1); exact subsystem enum needs confirmation |
| 25,26 | thermal query, reset power-state counters | all0; do not treat reset as a read |
| 27..34,35 | power-state dump batch then end | all0; batch loop takes res.a1..a3, aborts on nonzero res.a0; 35 terminates sequence. Snapshot side effects unverified. |
| 36 | MCU control | all0 in this exact wrapper; do not invent forwarded arguments from other platforms |
| 37 | pre-cal backup | x2=u32(arg0), x3=u32(arg1) |
| 38 | pre-cal clean | all0 |
| 50,51 | debug init/deinit | all0; return ignored |
| 52,53 | off-domain status, IRQ status | all0; returns low32 res.a0 |
| 54 | print debug | x2=u32(arg0) |
| 55 | bus-clock status | all0; wrapper requires raw res.a0==6, returns0 on match. Four attempts with delays; after failure returns8. **Raw0 is not success here.** |
| 70 | EMI remap | all0 in captured wrapper; do not forward DT addresses as invented arguments |
| 80 | coredump dump-region setup | x2=sxtw(arg0) |
| 81 | coredump dynamic remap | x2=sxtw(arg0), x3=u32(arg1); other arguments0 in captured wrapper |
| 90 | ADIE top clock enable | x2=1 on /0 off; all others0 |

Calls named read/status cannot be certified side-effect-free from their Linux-side wrappers. Secure firmware may access protected domains, alter latch/counter state, or assume initialization. None is presently an approved automatic boot probe.

## Power dependency: ATF is only part of the path

`consys_conninfra_on_power_ctrl_mt6897` at `.text+0x1e940` calls `consys_platform_spm_conn_ctrl_mt6897` at `.text+0x1e7a4`, size236. The latter uses **Linux runtime PM**, not the ATF SMC above: enable invokes `__pm_runtime_resume(device,4)`, then device-wakeup setup; disable clears wake capability and invokes `__pm_runtime_idle(device,4)`. Its provider implements the real CONN power-domain transition. Prior FDT has `power-domains=<0x41 1>`; resolve phandle/provider against matching MT6897 SCPSYS source before porting.

A large generated direct-MMIO SPM sequence also exists in the binary, but its presence does not prove that this selected callback invokes it. An earlier quick interpretation confused that sequence with the selected power callback; the symbol-accurate finding above supersedes it.

The same ATF platform also binds Linux PMIC/regulator operations and non-ATF pinmux. `consys_plt_pmic_common_power_ctrl_mt6897` calls regulator voltage/mode/enable/disable and PMIC regmap updates, with branch checks for ADIE0x6637 and sleep/RC mode. UEFI needs matching regulator/provider behavior or a fully established preserved-state handoff. ACPI resource enumeration cannot supply those operations by itself. Android's booted connectivity state cannot be assumed after a reset into UEFI.

## Accessibility gate and bounded candidate reads

The selected register table `.data+0x1540` binds:

1. +0x60 `consys_check_conninfra_on_domain_status_mt6897` (non-ATF).
2. +0x68 ATF clock-status op55.
3. +0x70 ATF off-domain-status op52.
4. +0x78 ATF IRQ-status op53 (separate diagnostic operation).

The generic `consys_reg_mng_reg_readable` dispatch first performs the on-domain check, then clock-status and off-domain checks before reporting readable. Error branches may call debug/PMIC diagnostics, so copying the entire diagnostic function would not be a minimal read-only probe.

The on-domain check itself has only two 32-bit MMIO loads and no MMIO stores: `reg_window0+0x1cc` bit0, then `reg_window0+0x0c` bit25. Both must be clear for return0; bit0 set yields2, bit25 set yields4 (takes precedence). `consys_reg_init_mt6897` proves each window entry is24 bytes: virtual pointer+0, physical base+8, size+16, filled in DT reg order. With the prior FDT first window0x10001000, candidate addresses are therefore **0x100011cc** and **0x1000100c**. The embedded printk mentions different addresses/bits and is stale; trust machine operands plus binding, not its text.

These are bounded candidates, not a runtime safety proof: reconfirm current DT first window, provider always-on accessibility, allowed exception handling and memory attributes. Do not read 0x180xxxxx merely because the region is mapped. Do not call op55/52 or wake18 automatically until secure-side prerequisites and state changes are understood. The binary does not supply an unconditional safe-read contract for an unpowered connectivity block.

## Comparison to pinned public reference

Reference only: MiCode `MTK_kernel_modules`, commit `26e64cd4c795b2ff2c07d530b956beabfdada2f1` (`bsp-rodin-v-oss`), extracted in `evidence/sources/wifi-audit-20261029`; source-manifest records paths and hashes. `connsys_smc.h` enum agrees with all decoded op IDs, including55 and90, and its macro agrees with x1 opcode/x2..x7 arguments. It does not establish the target function ID without `plat_def.h`; **0xC2000534 comes from the actual module**. The match table maps MT6897 consys-atf to `mt6897_plat_data_atf`, but merely has weak platform declarations and is not a replacement for the matching implementation. Public reference is corroboration; device binary controls this report.

## Actionable implementation boundary

The next implementation can define a disabled-by-default diagnostic contract with raw SMC/result logging, exact per-op argument records, bounded always-on status-read candidates and separate runtime-PM/provider requirements. Keep power-changing execution behind an explicit development option until the CONN genpd/SCPSYS path, PMIC/regulator parameters, conn_hw_env+0xc, platform config, init ordering, calibration and shared BT/GPS ownership are resolved.

First hardware milestone remains a private ACPI development device with correct reservations and a probe that survives boot and reports documented accessibility state. Preserve WLAN DMA `0x88b00000+0x1400000` and full conninfra reserved `0x87e00000+0xd00000`; no firmware DMA or speculative secure calls yet. Subsequent controlled power-on must establish domain/PMIC readiness before chip polling, firmware download, IRQ/DMA and command-event tests. UEFI power-on alone would not implement the Windows WLAN host driver.
