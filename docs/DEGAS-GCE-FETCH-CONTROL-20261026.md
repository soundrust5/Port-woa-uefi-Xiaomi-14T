# GCE command-fetch control: exact ATF/LK follow-up, 2026-10-26

Offline only. No production edits, device access, SMC invocation, GCE submission, or IOMMU/security changes were performed. Focused disassembly and metadata are preserved under `evidence/extracted/2026-10-26-gce/`.

## Concrete result

The PC encoding is established: **`(controller DMA address + 0x40000000) >> 3`**, not bare `PA >> 3`. The unresolved part is whether the controller DMA address for a new Mu buffer may equal its CPU physical address in the inherited V13 state. A nonsecure read of zero at global GCE `+0x54` does not by itself close that gap.

There is now exact-firmware evidence identifying the `+0x54` bit17 setter: the ATF routine with error string `cmdq_set_va_mode` sets it. However, the exact binary also sets it in the operation named `CMDQ_MMUEN_SET_DEVAPC_DISABLE`. That operation changes access permissions as well. A generic assumption that bit17 is specifically the GCE_DM **instruction-fetch** translation enable remains an inference, not a decoded hardware field definition.

## Exact images and preserved artifacts

- ATF: `evidence/extracted/2026-09-14/atf-analysis/atf.bin`, SHA256 `ef838aa754b367a0cfef4d18272d812895bb28aa55525b6a672245f223ca4935`. Its existing manifest matches the captured stock tee partition and gives linked physical base `0x48800000`.
- Installed CMDQ module: `evidence/extracted/2026-10-26-gce/mtk-cmdq-drv-ext.ko`, SHA256 `ff73e817d3c2fc2fdc11b3bddb3bbb6b8bf6a962cb4f55f94349608d6065a3ba`.
- `atf-cmdq-handlers.asm`: ATF `0x4880d870..0x4880de68`, including both handlers, the request jump table's targets, reserved-buffer setup, the VA-mode setter and the request7 writes.
- `atf-cmdq-control-metadata.json`: image hash, descriptor offsets, handler addresses and decoded request jump-table targets.
- `cmdq-fetch-control.asm`: exact installed module's SMC helpers, normal channel selection/startup, PC decoding and submission.
- `lk-cmdq-smc.asm`: exact LK's bootloader-specific SMC wrapper.

## The two SMC entry points are demonstrably different

ATF's static descriptor table contains both IDs, so the difference is not merely a mismatching header:

| Descriptor file offset | AArch64 SMC ID | Handler PA | Name string |
| --- | --- | --- | --- |
| `0x680a0` | `0xc2000518` | `0x4880d948` | `MTK_SIP_CMDQ_CONTROL` at `0x48859447` |
| `0x680c0` | `0xc2000418` | `0x4880d870` | `MTK_SIP_BL_CMDQ_CONTROL` at `0x4885945c` |

The common dispatch passes the request as handler x0; therefore the Linux wrapper's SMC x1 corresponds to x0 in the handler below.

Exact LK `0xffff00005071b1f4..0xffff00005071b250` calls `0xc2000418` with x1..x7 zero. The ATF BL handler accepts request0 and calls `0x4880dc50`, identified by its strings as `cmdq_reserved_init`. Other requests log an unknown-request message and return zero. This is **not** the Linux request7 operation and is not an ordinary caller-supplied command-buffer submission interface.

`cmdq_reserved_init` looks up `me_cmdq_reserved`, maps its CPU view and maps it through a second routine whose failure message is `iommu map failed`. It stores the returned controller address at ATF global `0x48903fa8`; the CPU view is stored separately at `0x48903fa0`. The existing protected stock reservation is not available as Mu-owned command storage.

## What global +0x54 bit17 actually has evidence for

The request jump table is at ATF file offset `0x5942c`, with targets relative to `0x4880da20`:

- Request3 (`PREBUILT_ENABLE` in pinned Linux enum) goes to `0x4880daec`, calls `0x4880dd20` (a no-op in this binary), then `0x4880de1c`.
- `0x4880de1c` validates the passed base is GCE-D `0x1e980000` or GCE-M `0x1e990000`; its error path names `cmdq_set_va_mode`. It RMW-sets `0x20000` at base`+0x54`, then installs callback `0x4880dd24`.
- Request7 goes to `0x4880db4c`, which calls `0x4880ddb4`. That routine RMW-sets bit17 at **both** GCE-D and GCE-M `+0x54`, installs the same callback, then clears bits `[1:0]` and `[17:16]` at `0x1e820000`.
- The saved actual FDT classifies `0x1e820000` as a DEVAPC region (`/soc/devapc@10207000`, reg_index12), not an IOMMU or SMI port register. Pinned `devapc-mt6897.h` names permission entries0 and8 as GCE_M_APB_S and GCE_D_APB_S. Interpreting the two cleared two-bit fields as their permission fields is consistent with those entries, but the register-level mapping is an inference.
- The installed module's `cmdq_util_mmuen_devapc_disable` at text`0x16258` calls exactly `0xc2000518`, x1=7, x2..x7=0. This verifies which ATF operation the source name denotes. It does not authorize invoking it in Mu.

The installed/pinned normal mailbox driver does not name a global `+0x54` field. Its similarly numbered `CMDQ_THR_INST_THRESX=0x54` is **relative to a thread base** and must not be substituted for the global register.

V13's global readback zero is therefore useful state evidence, but cannot alone distinguish physical command fetch, a write-mask/access-control interpretation, or which transaction class the VA-mode setting covers. No secure call or global54 write is proposed from the name alone.

## Address aperture and call ordering

The exact installed module's `cmdq_thread_get_pc` at `0xd60` reads PC, shifts left by `gce_shift_bit`, and subtracts `gce_mminfra` (`0xda0..0xdac`). Submission performs the inverse using the task's DMA address. The pinned MT6897 platform values are shift3 and aperture`0x40000000`; the saved Android boot log independently records those values.

The ATF request1 (`ENABLE_DISP_VA`) supplies a second exact implementation. At `0x4880da64` it obtains the separately stored mapped controller address. It adds `0x4000`, shifts by3 at `0x4880da90`, adds `0x08000000` at `0x4880dab0`, and programs GCE-D thread29's PC/end at `0x4880dac8..0x4880dacc`. Algebraically the PC is `(mapped_controller_address + 0x4000 + 0x40000000) >> 3`. This is a protected prebuilt display-VA packet, not Mu's buffer and not thread0.

Linux's MMINFRA power-on callback calls request7 before ordinary runtime usage. BL2_EXT also sets bit17 while initializing MMINFRA. Thus a later translated Android execution is not evidence that the V13 zero-read state uses the same route, nor that its DMA address is a CPU PA.

No ordinary LK GCE packet allocation/PC programming path was established. The main LK binary's only CMDQ-specific function string is `cmdq_smc`; the known display transfer uses CPU writes. Searches for direct GCE-D/GCE-M base materialization and relevant literal words did not identify a normal GCE submission. This is a negative search result, not a proof that no indirect path exists. It cannot supply a missing PA-identity contract.

## Thread0 and the smallest useful fetch experiment

Exact installed `cmdq_xlate` at `0x84c0` checks index against channel count and populates software timeout/priority; `cmdq_mbox_startup` at `0x9e68` sets a software occupied byte. Neither configures or proves hardware nonsecure ownership. Normal display FDT assignments and the separate secure8..12 range make thread0 the narrowest source-backed **candidate**, subject to a fresh all-disabled/clean-state check and a bounded nonsecure CSR reset/readback test. Inactivity alone is not ownership evidence.

The next hardware **execution** experiment should be only an internal SPR0 magic assignment, EOC without IRQ, and terminal/pass instruction. It should have no packet memory writes, peripheral writes, waits, loops, display commands, SMCs, global54 writes, IOMMU-root replacement or protected-reservation reuse. Require SPR0 magic, execution-count change and bounded PC/end status; on timeout/error record status before selected-thread cleanup and retain the packet allocation if quiescence is not proved.

Use the established PC encoder on a **controller DMA address**, with its CPU backing address separately recorded. A compile-time/runtime `dma_address = cpu_pa` assumption is not yet justified by this inspection. Keep the address-route proof separate from the already established encoder and do not relabel a guessed PA as a mapped DMA address. If the parent requires a source-proven route before integration, this experiment remains unlinked until that one unresolved mapping/bypass condition is closed. Another broad read-only snapshot of the same globals would not close it.

## V14 follow-up: the ATF map is a fixed secure-module mapping

The parent reports V14's query returned hyp-IOMMU type2 enabled while executing at EL1, with the sampled registers unchanged and all channels disabled. This does not identify normal guest-root ownership or make a CPU PA a controller DMA address.

The exact routine at `0x4882b880` is now traced through selection and update:

1. `cmdq_reserved_init` passes `(PA, size, mem_type=1, mod_id=2)` at `0x4880dc80..0x4880dc90`.
2. `0x4882b880` calls selector `0x4882b35c`, passing a pointer for the selected entry index. The selector requires nonzero, 1MiB-aligned PA and size, `mem_type<=1`, `mod_id<10`, and a matching static pair. It does not accept an arbitrary IOVA.
3. The static table starts at `0x488694f0`, with ten 40-byte records. Record2 at `0x48869540` contains `mod_id=2`, `mem_type=1`, MM table ID0, maximum size `0x100000`, and fixed IOVA `0x107400000`. It stores the supplied physical base in the record. Record3 similarly fixes IOVA `0x107500000` for a different module.
4. The wrapper calls `0x4882be10` (`mtk_iommu_update_pgtable`, identified by its error string). It obtains a table with `get_pgtable(mem_type=1, id=0)`, writes the selected fixed IOVA's 1MiB section descriptors and cleans their cache range. PAs above 4GiB are encoded into extended ARM-v7s descriptor bits.
5. `get_pgtable` at `0x4882b298` sends mem_type1 through `0x4882b794`, whose error path names `mtk_iommu_init_secure_pt_mem`. Mem_type0 instead uses `0x4882b634`, named `mtk_iommu_init_protect_pt_mem`. The pinned Linux `mtk_iommu_sec.c` enum independently defines PROTECT_TYPE=0 and SECURE_TYPE=1. These are ATF-managed tables, not the normal Linux/Mu guest table backing bank0.

Consequently this CMDQ path cannot provide a generic mapping for an owned nonsecure Mu packet. Reusing record2 would repurpose the protected stock CMDQ reservation and its fixed IOVA, rather than allocate a clean command buffer. Neither request3 nor request7 takes a buffer address, allocates IOVA space, or updates the normal guest mapping. No such reuse or call is proposed.

Preserved additional evidence: `atf-cmdq-map.asm`, `atf-cmdq-map-inner.asm`, and `atf-cmdq-update-pgtable.asm` under the same evidence directory. The section-update routine contains a short branch through ATF's existing patch trampoline at `0x48800158`; this computes the static-record pointer then returns to `0x4882be70`. It does not turn the fixed table selector into an arbitrary allocation API.

### Request3 versus request7, and readback limits

For an eventual justified VA-mode setup, request3 with hardware ID0 is the narrower exact operation: the handler selects GCE-D, calls `0x4880dd20` (a no-op in this image), sets GCE-D `+0x54` bit17 and installs an ATF callback that can touch OVL controls during the separate display-VA flow. Request7 sets both GCE-D and GCE-M and relaxes DEVAPC permission fields. The latter is broader than a selected-thread fetch test and should not be used to discover a mapping by trial.

Both paths reach the common zero return even when a helper logs an invalid/unhandled condition. A successful SMC return would not establish a correct mode, permission change or mapped buffer. The supplied binary has no exposed clear/restore operation for bit17 in these routines, so reversible setup must not be assumed.

No identified GCE+0x54 read-as-zero handler was found in the inspected GZ root-registration path. The known GZ MMIO handlers cover IOMMU registers; absence of a GCE handler in that inspection is not proof of hardware passthrough. Conversely, there is no evidence that V14's zero is a masked secure value. Preserve both possibilities; neither is established. The VA-mode setter's name still does not define whether the bit specifically governs instruction fetch, packet-generated transactions, or both.

The safest concrete normal-bank design is therefore an owned, aligned guest table and packet buffer, registered through the already decoded GZ-mediated normal-root contract, retaining any required protected mappings. It requires proof that table ID0 is available for first registration or knowledge of its existing registered guest root. A negative address query conflating unready root with unmapped PTE does not supply that proof. Do not overwrite the inherited shared hardware root or repurpose the secure CMDQ table to avoid this ownership condition. Once an actual normal DMA mapping exists, the internal-SPR sentinel can use its known IOVA with the established encoder; no request3/7 is justified merely to guess PA bypass.
