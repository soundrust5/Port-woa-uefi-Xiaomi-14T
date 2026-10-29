# Degas panel transfer and autonomous refresh audit — 2026-10-26

Offline hardware-path review. No phone, ADB, fastboot, flash, reboot, display
MMIO write, production edit, or additional agent. Read the display handoff,
framebuffer contract, exact LK disassembly, current diagnostic/GOP implementation,
installed Android modules and pinned MediaTek source. The other display audit
owns the GOP API/alpha/stride interface and its implementation changes.

## Main finding

The current hardware contract is a **single DSI command-mode output fed by two
processing halves**, with explicit frame triggers. Merely storing new pixels in
the advertised physical framebuffer has no proven path to a new panel transfer.
No autonomous hardware refresh program exists in the current Mu implementation.

There is a source-backed path to remove the CPU/Boot Services refresh dependency:
a GCE hardware command packet can wait for an appropriate hardware event, issue
the verified dual-processing-half/DSI transfer, and jump to its own beginning.
The installed driver demonstrably uses GCE loops. However its Android trigger
loop consumes a software STREAM_DIRTY token: transplanting that loop unchanged
will not notice arbitrary Windows framebuffer stores. A firmware bridge needs
its own repeating, appropriately paced packet and proper GCE/DMA ownership.
This has not been implemented or hardware validated; it is not a one-bit change.

Switching the host DSI mode bits to video mode has **no panel-supported basis**
in the existing evidence. An OS-owned display implementation remains the proper
long-term owner of frame submission, clocks/power, interrupts and mode changes.
That driver alone would not make earlier Windows Boot Manager graphics visible;
the firmware-to-driver interval still needs an autonomous bridge or other proven
display contract.

## What the working wrapper actually does

`diagnostic/framebuffer.c:86-143` only validates and writes pixels. The active
command-mode transfer is in `diagnostic/display_frame_v2.c:34-48` and the matching
`DegasGop.c:63-91`:

1. Require retained addresses, geometry, DSI command mode, and mutex module masks.
2. Require DSI not busy; clean framebuffer writes in the cached Mu path.
3. Perform the exact LK DSI start/queue preparation.
4. Enable mutex0 in **all four** processing blocks: secondary `0x14201000` and
   `0x14601000`, then primary `0x14001000` and `0x14401000`.
5. Require idle, submit `0x002c3909` to DSI0 command queue `0x1400dd00`, set
   queue control `0x8001` at `0x1400d060`, toggle DSI_START 0 then 1, and poll idle.

The two OVL views point at `0xfcc2f000` and `0xfcc2f95c` (599 pixels apart), each
621 pixels wide with 22-pixel overlap. This is not two DSI links. The captured
DSI1 words are zero. Exact LK `display-update-chain.asm:332-374` has a scenario18
helper that activates the extra processing pair; the current wrapper's use was
subsequently validated by a visible frame, not inferred solely from scenario ID.

Hardware contrast already exists: v1 without the second pair remained busy and
showed no marker; v2 with the second pair completed and displayed the marker.
`evidence/raw/2026-10-24-display-frame-v2-result/console-ramoops-0` SHA256 is
`0ae947dd228c2a70deb2123859b4741f7d908972f236ecf97e877610566e9750`.
It records idle-after=1 and status-after=0x10. The driver source identifies DSI
INTSTA bit4 as FRAME_DONE and bit31 as BUSY. This proves an explicit transfer,
not sustained scanout after direct RAM changes.

Current `OsHandoff.h` stops UFS and adjusts the watchdog; it neither starts a
display loop nor intentionally disables the retained display. Leaving retained
display registers untouched preserves the last panel image, but does not add a
new-frame submission mechanism.

## Exact installed binary checks

Independent `llvm-objcopy --dump-section .note.gnu.build-id=...` plus bytewise
`cmp` matched both installed modules to saved sysfs build-ID notes. These are
offline captures of the tested phone, stronger evidence than the donor source.

| Module | SHA256 | GNU build ID |
| --- | --- | --- |
| `evidence/extracted/2026-09-14/display-modules/mediatek-drm.ko` | `4ab3c4f70930259695c31bf41b31eb3f14cfaeeb1a4e42a8e8a95b67a63ff356` | `4d041509e87b332b1aa2f4104b9bb8be1f5df907` |
| `evidence/extracted/2026-09-14/display-modules-live-b/panel-n12a-36-02-0b-dsc-cmd.ko` | `f1146811e0827d24548b224ac0a03f1dcc5eee4e520ddd38f70c0ddb2f6adfa3` | `d952c6934ac3884cd3a7b217435e91c1e0416ffb` |

The notes compared against
`evidence/raw/2026-09-14-memory-display/mediatek-drm-build-id.bin` and
`panel-build-id.bin`. Disassembly used `llvm-objdump -dr --disassemble-symbols=...`.

- `mtk_crtc_start_trig_loop` starts at module text address `0x53e88`, size
  `0xda4`. It enables a display mutex at `0x54674`, invokes component TRIGGER
  at `0x54684`, waits for a command event at `0x54690`, and invokes component EOF
  at `0x546a0`. At `0x54968` it calls `cmdq_pkt_finalize_loop`; at `0x5497c` it
  calls `cmdq_pkt_flush_async`. Thus GCE iteration is present in the exact binary.
- `mtk_dsi_trigger` is at `0xc1384`; `mtk_dsi_config_trigger` at `0xd104c` polls
  idle, distinguishes PRE_TRIGGER/TRIGGER/EOF, and emits command-packet writes.
  A complete port must account for the selected SoC/panel branch rather than
  retaining only an arbitrary DSI_START toggle from generic source.
- Panel `lcm_probe` at `0x288` retains its mipi_dsi_device in x19. At `0x330` it
  sets lanes=4, and at `0x348` loads mode flags `0xe00`, stored at device+`0x3b8`
  by `0x364`. That value lacks MIPI_DSI_MODE_VIDEO bit0. The panel's `mode_switch`
  at `0x172c` selects tables for 60/90/120/144 Hz; it does not establish a switch
  between command and video transfer protocols.

## GCE source: viable mechanism and missing platform work

Pinned reference: MiCode/MTK_kernel_device_modules commit
`3c4ccd07d5624879555ec710c11bb8818e5cb5c6`, available through
`git -C evidence/sources/mt6897-device-modules show HASH:PATH`.
It is a same-MT6897 duchamp reference, not an exact degas board-source claim.

- [mtk_drm_crtc.c](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_crtc.c#L8712):
  `mtk_crtc_start_trig_loop` at line8712, wait STREAM_BLOCK at8789 and consuming
  wait STREAM_DIRTY at8790; TE synchronization follows. Lines8929-8943 perform
  component pre-trigger, mutex enable, trigger, command EOF wait, then EOF.
  Lines9071-9072 finalize and submit the loop. `mtk_crtc_comp_trigger:7983-8001`
  visits both current and dual-pipe component lists and the DSC component.
- [mtk-cmdq-helper-ext.c](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/drivers/misc/mediatek/cmdq/mailbox/mtk-cmdq-helper-ext.c#L2568):
  `cmdq_pkt_finalize_loop:2568-2593` emits EOC and an absolute jump back to the
  start. Its EOC implementation selects `CMDQ_EOC_IRQ_DIS` when `pkt->no_irq`
  is true. A persistent bridge must encode no per-frame IRQ or have a real OS
  interrupt owner; firmware CPU callbacks cannot acknowledge frames after EBS.
- [mt6897-gce.h](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/include/dt-bindings/gce/mt6897-gce.h#L418)
  names DSI0 FRAME_DONE event451 and DSI0 TE event898 (line593). These are source
  event IDs, not authorization to program an uninspected GCE engine.
- `cmdq-platform-mt6897.c:10` identifies GCE-D physical base `0x1e980000`.
  The actual saved degas FDT corroborates `/soc/gce@1e980000`, size`0x4000`,
  mediatek,mt6897-gce, `gce` and `gce-timer` clocks, power domain, IOMMU and DMA
  properties (`live-fdt.json:33446-33527`).

Current Mu maps display MMIO `[0x14000000,0x14800000)` but not GCE-D at
`0x1e980000` (`MemoryMapLib.c:4-18`). There is no GCE thread initialization,
command DMA mapping, reserved instruction buffer or display-loop stop/ownership
contract. It would be incorrect to assume UFS's identity DMA behavior proves
GCE command-fetch addressing through the separate GCE IOMMU path.

Concrete development sequence, with no speculative register writes in this audit:

1. Inventory exact installed GCE driver, live-FDT mailbox/thread assignment,
   event routing, clock/power ownership, DMA addressing and inherited state.
   Use retained source/binary evidence before selecting registers or a thread.
2. Build an encoder and host decoder tests for the exact GCE instruction format,
   subsystem addressing, event waits, masked writes, no-IRQ EOC and loop jump.
   Keep all writes inside the already proven display transaction allowlist.
3. Allocate and explicitly reserve a controller-addressable instruction buffer
   for the entire autonomous lifetime. Prove cache visibility and IOMMU mapping;
   never leave command storage in reclaimable EfiBootServicesData at EBS.
4. First validate one GCE-submitted version of the already proven transfer with
   bounded completion, then a limited repeated/TE-paced test. Confirm that TE
   and frame-done GCE events actually route with this retained DSI configuration.
   A source event number is not proof that a wait will complete on hardware.
5. Only then test a no-CPU-callback loop which repeats without consuming an
   OS-unwritten STREAM_DIRTY token. Require changing pixels to become visible
   after successful EBS and firmware timer shutdown, and a bounded recovery path.
6. Provide explicit OS takeover: stop/retire the bridge before resetting display
   clocks or programming new buffers/modes. A hidden loop touching an OS-owned
   display indefinitely is not a complete handoff design.

This path can in principle maintain the command-mode panel through EBS without
a UEFI timer, because the controller fetches a reserved packet and loops in
hardware. It still requires proof of all six items before calling the advertised
framebuffer autonomously visible. Do not substitute an unpaced jump loop, which
could race busy hardware, or start a generic Android loop that stalls awaiting a
software token Windows never writes.

## Proper opaque framebuffer candidate

There is now a more concrete alpha fix candidate than rewriting every pixel's
reserved byte. The same-SoC OVL source and exact installed binary agree:

- [mtk_disp_ovl.c:147](https://github.com/MiCode/MTK_kernel_device_modules/blob/3c4ccd07d5624879555ec710c11bb8818e5cb5c6/drivers/gpu/drm/mediatek/mediatek_v2/mtk_disp_ovl.c#L147)
  defines `DISP_OVL_LAYER_CONST_BLD=BIT(28)` in the layer PITCH register. Lines
  2400-2415 distinguish real per-pixel alpha and set constant alpha in CON;
  lines2529-2539 set PITCH bit28 for DRM_MODE_BLEND_PIXEL_NONE, otherwise clear it.
- Exact `mtk_ovl_layer_config` starts at `0x39820`. Normal layer pitch offset is
  built as layer*`0x20`+`0x44` at `0x3ab7c`. At `0x3adc8`, the binary compares
  pixel-blend mode to2; `0x3adcc` conditionally selects one, `0x3add4` shifts it
  by28, and `0x3ade0-0x3ade4` writes with mask`0x10000000` through cmdq_pkt_write.

Thus the implementation candidate is hardware constant blending with opaque
constant alpha, applied coherently to both retained OVL views. It is **not** a
blind clear of CON.AEN bit8: the driver itself sets AEN and separately uses the
PITCH constant-blend selector. The current pitch captures/guards equal4992,
so bit28 is currently clear. The saved read-only surveys do not include layer
CON at `0x14402030` and `0x14602030`; capture/verify its color format and constant
alpha before a guarded diagnostic change. Any later pitch guard must compare
the stride bits separately from the intended blend bit.

This could remove the need to mutate Windows's reserved pixel byte, including
after EBS. It does not solve the independent command-mode frame-trigger need.
No OVL register change is made or claimed hardware-proven here.

## Why video mode is not the next justified change

Reference `mtk_dsi_set_mode:1549-1564` selects video only from panel mode flags;
video initialization also calculates/programs video porches and packet timing.
The exact installed panel is command-only in its probe configuration, and its
available refresh-rate mode tables are not video-mode support evidence. DSI host
capability to emit video is insufficient: the panel protocol, DSC stream,
timings, dual-processing split and clocks must all agree. No exact panel video
sequence or video timing contract was found. Keep this route unproven unless
that evidence is obtained.

## Confidence and recommendation

High confidence: explicit dual-processing-half trigger is necessary for the
currently proven path; current code lacks autonomous refresh; installed Android
code uses GCE loop execution; an unmodified Android dirty-token loop does not
solve direct framebuffer stores; source/binary identify a proper opaque blend
candidate. Moderate confidence: a carefully ported GCE loop can provide the
needed early-OS bridge on retained hardware. It is a practical engineering
direction with substantial missing platform validation, not an existing fix.

The immediate discrimination remains a controlled direct-write/no-refresh,
refresh-only, and alpha experiment with fresh persistent logging. It establishes
which issue hides graphics without conflating that with whether Windows reaches
EBS. A pre-EBS timer heartbeat is useful only as a diagnostic; it cannot prove a
working post-EBS display contract.

## V11 follow-up: concrete offline GCE one-frame prototype

The coordinator reports fresh V11 evidence of framebuffer RGB changes, reserved
byte zero in 1518/1536 samples, and the final EBS-ready marker. That supersedes
the earlier absence of captured EBS for V11. This follow-up only derives GCE
startup/encoding; it does not integrate with or claim a hardware result for V12.

New, **unlinked** artifacts:

- `diagnostic/include/degas_gce_one_frame.h`: pure instruction builder and an
  injected-MMIO one-frame runner. There is no actual MMIO backend, firmware INF,
  DSC/FDF inclusion, clock enable, IOMMU change, global reset or persistent loop.
- `diagnostic/tests/test_degas_gce_one_frame.c`: independent bitfield decoder and
  controller model. Golden raw words, all 18 display targets/order, RMW bit
  preservation, PC aperture/shift, threads0/6, explicit rejection of7..31
  including secure8/9, malformed-packet rejection,
  readiness/no-write guards, publish/reset/timeout/error/stop faults pass.

Exact test command (workspace root, WSL):

```sh
gcc -std=c11 -O2 -Wall -Wextra -Werror -fsanitize=undefined \
  diagnostic/tests/test_degas_gce_one_frame.c -o /tmp/degas-gce-one-frame-test
/tmp/degas-gce-one-frame-test
```

Exit0, exact output:
`PASS: GCE exact encoder golden words, independent decoder/18 allowed display writes, no IRQ/loop, aperture+shift, thread0/6, reject7..31 including secure8/9, guards and bounded faults; NO HARDWARE`

The packet is **53 instructions / 424 bytes**: the proven full dual-processing
half transfer, three DSI-idle polls, no-IRQ EOC, pass-forward jump. It performs
one transfer only; the last jump is +8, not a back-edge. The CPU runner polls for
at most200ms, then suspends/resets/disables its selected thread with bounded
cleanup. If `quiesced` is false, its command storage must remain quarantined
until reset. `completed` means the controller reached the expected end of the
command sequence; it is not independent proof that a panel displayed new pixels.

The runner requires external evidence for five conditions: readable/mapped MMIO,
power/clocks, owned controller-visible command allocation/mapping, selected
nonsecure thread permission, and the retained display contract. All five proof
bits are caller assertions, not runtime discoveries. It additionally checks
GCE-D/SMI/26M clock status, retained GCTL=0x20002, all32 threads disabled, DSI
command mode/idle and clean selected IRQ state. Mask/poll instructions use global
GPR0/GPR1, so all32-disabled is a deliberate restriction. Candidate threads are
restricted to0..6: actual normal-display mailbox assignments include these,
and pinned `cmdq_thread_ddr_module` says they do not require the extra per-thread
DDR-request management omitted here. `cmdq-sec-mailbox.h:13-14` reserves8..12 for
secure execution;8/9 are explicitly rejected even though they share the no-extra-
DDR classification. The earlier prototype allowed8/9 before that security
constraint was found; it was never linked or hardware-executed. No
thread is yet established as available to Mu. The runner writes global SLOT
cycles0x3200 plus its selected thread CSRs; it is not a transparent resident
driver or a complete cold-start implementation.

### New exact source/binary facts

The installed `mtk-cmdq-drv-ext.ko` was extracted **offline** into the Windows
temporary directory using the already captured legacy-LZ4 vendor ramdisk and
inventory offsets. Its SHA256
`ff73e817d3c2fc2fdc11b3bddb3bbb6b8bf6a962cb4f55f94349608d6065a3ba`
matches the inventory. The platform module SHA256 is
`f30d7e3028923fdecb8e92734831900b73ccd454288a847963f9280e331b5ea6`.
No new device read occurred. Relevant text addresses:

- `cmdq_pkt_append_command:0xd668`, especially0xd6b4..0xd6f8, independently
  confirms field placement: opcode bits63:56, operand types55:53, sub-op52:48,
  argA47:32, argB31:16, argC15:0.
- `cmdq_pkt_write_value_addr:0xe020` and `cmdq_pkt_store_value:0xe104` confirm
  thread SPR0 address-high and indirect WRITE_S address-low bit1. Masked stores
  first emit inverse-mask opcode0x02, then opcode0x91. No subsystem-ID guess is
  required for these direct-address instructions.
- `cmdq_pkt_poll_addr:0xf9d4` supplies the legacy-address poll encoding used by
  the prototype; `cmdq_thread_reset:0xf08` confirms selected-thread reset/poll.

Pinned mailbox source `gce_plat_v5:2945-2946` and matching MT6897 compatible entry
specify32 threads, shift3, **MMINFRA aperture0x40000000**. Source
`CMDQ_REG_SHIFT_ADDR` is `(dma_address + 0x40000000) >> 3`, not PA>>3. Saved
actual degas Android dmesg independently logs those three values at
`evidence/raw/2026-10-24-degas-aligned-result/dmesg.txt:1691`.

Command addresses are DMA/IOVA addresses, not necessarily CPU physical addresses.
The FDT GCE-D IOMMU specifier0x561 is **fake larb43, port1 (GCE_DM)**, as named in
`mt6897-larb-port.h`; it is not an ordinary SMI larb whose NONSEC_CON can be
guessed. Its IOMMU is `0x1e810000`. The MT6897 MDP platform source uses IAS34,
35-bit physical output, extended ARM_V7S page tables and SKIP_CFG_PORT. Therefore
generic ARM64 page-table walking, generic SMI port toggles, a simple PA identity
assumption, or copying UFS's DMA behavior are not justified.

The exact stock BL2_EXT also provides inherited-state evidence:

- At linked VA `0xffff000078038a04..0xffff000078038a50` it constructs GCED base
  `0x1e980048`, sets bit17 at GCED+0x54 and GCEM+0x54, then writes **0x20002** to
  GCED+0x48. The saved LK log explicitly prints `mminfra_init GCED_GCTL_VALUE=0x20002`
  at `evidence/raw/2026-09-14-memory-display/pl_lk.txt:3728`. The meaning of
  GCED+0x54 bit17 is not established here; do not silently replace it.
- BL2_EXT `0xffff000078055c20` allocates a1MiB `me_cmdq_reserved` range and
  `0xffff000078055ce0` passes its interval to a protection routine. The FDT records
  `[0x33fe00000,0x33ff00000)` for it. This is a protected stock reservation, **not
  free Mu command storage**. Do not borrow it or change its protection.
- The proven LK display transaction still uses direct CPU writes. These findings
  establish stock GCE platform initialization/reservation, not that an autonomous
  display thread remains executing at Mu entry.

### Smallest staged read-only snapshot for the next authorized test

Map only the required device pages/ranges and precede every new range with a
persistent breadcrumb. **These are source-backed candidates, not a guarantee
that a gated hardware read cannot fault/hang.** Current display operation and
platform power evidence must precede new-domain access; retain watchdog recovery.

0. Before mapping/reading the new MMINFRA domain, read the already accessible
   SPM word `0x1c001eac` and require `(value & 0xc0000000) == 0xc0000000`.
   This is the MT6897 MM_INFRA power-ack guard established below. On failure,
   skip every MMINFRA/GCE/IOMMU access; do not try to power the domain on.
1. MMINFRA clock status: `0x1e800100`, `0x1e800110`. GCE-D bit0 and SMI bit2 of
   CON0, and26M bit17 of CON1, must be clear for the inherited-ready prototype.
   Do not write SET/CLR/voter registers to make this pass.
2. If that domain is accessible, GCE-D globals:
   `0x1e980010` IRQ summary, `0x1e980018` loaded-thread state,
   `0x1e980030` slot cycles, `0x1e980040` bus control,
   `0x1e980048` GCTL, **`0x1e980054` exact-LK-set word**, and
   `0x1e9800c0` prefetch configuration. This is7 words.
3. Read only ENABLE for all32 threads: `0x1e980104 + 0x80*n`, n0..31. This makes
   the first-stage inventory41 words total including the2 clock words. Any
   enabled thread rejects the current prototype; do not reset it to free it.
4. Only if **all32 ENABLE words are inactive**, read thread0 details at base
   `0x1e980100`, offsets
   `0x00,0x08,0x0c,0x10,0x14,0x20,0x24,0x28,0x30,0x40,0x44,0x50`.
   These expose reset/suspend/status/IRQ/PC/end/count/wait/config/prefetch/cycles.
   No token-selector write, token sweep, core reset, debug mux or command FIFO.
5. A separate mapping investigation may read IOMMU `0x1e810000`, `+0x008`,
   `+0x048`, `+0x110`, under its verified clock/power conditions. A TTBR value
   alone does not prove a valid mapping or bypass for GCE_DM. Do not dereference
   its inferred table address without a bounded mapping/ownership check.

Unresolved facts that the snapshot can narrow: GCE clocks retained, global GCTL/
aperture-related state retained, any live stock/secure threads, candidate CSR
accessibility, whether an existing translation root is present. Facts it cannot
establish alone: permission to own/reset that thread, exact command-buffer DMA
translation, whether TE898/frame451 route correctly, or persistent ownership
through OS takeover. Strict read-only observation cannot select/query arbitrary
GCE tokens because the selector itself is a write. Those need source-backed
permission/mapping work and eventually a bounded one-frame execution result.

Next engineering step is to resolve those facts, then run one frame and compare
actual visible output/controller state. Only after that should the packet gain
validated event/timer pacing and a back-edge in reserved nonreclaimable storage.
The current artifacts intentionally contain no autonomous loop or production
enablement.

### MT6897 power, secure-thread and translation follow-up

All source line numbers below refer to MiCode device-modules commit
`3c4ccd07d5624879555ec710c11bb8818e5cb5c6`, a same-SoC reference; exact degas
binary/FDT evidence remains separately identified above.

The source-backed pre-access power guard is SPM **MM_INFRA_PWR_CON at +0xeac**,
not +0xea8. `drivers/clk/mediatek/mtk-pd-chk-mt6897.c:922` associates
MT6897_CHK_PD_MM_INFRA with SPM offset0xeac and GENMASK(31,30); its check at
lines928–943, particularly939, reports on only if both bits are set.
`drivers/clk/mediatek/clkchk-mt6897.c:1329` supplies SPM base0x1c001000 and
line1624 names MM_INFRA_PWR_CON at0xeac. The SoC-specific
`drivers/misc/mediatek/lpm/modules/include/mt6897/spm_reg.h:469` names the same
register; lines2889–2890 define its two power-ack bits30/31. Offset0xea8 belongs
to OVL1 on this SoC. A generic debug-driver guard using +0xea8 must not be copied.
Both power acks are a necessary gate; they do not prove individual clock or
access permission. Preserve the per-address RAM breadcrumb and watchdog.

Thread candidates are restricted to **0..6**, never8/9. Linux ordinary display
mailbox assignments support those candidates, but do not grant Mu ownership.
`drivers/misc/mediatek/cmdq/mailbox/cmdq-sec-mailbox.h:13–14` specifies five
secure threads starting at8; `cmdq-sec-mailbox.c:900–904` enforces that range.
`cmdq-util.c:494–502` independently classifies8..12 as secure. The normal
mailbox xlate function's channel-bounds check (`mtk-cmdq-mailbox-ext.c:2536–2551`)
is not a permission check. No established source-backed per-thread permission
CSR has been found; observing inactive ENABLE/IRQ state is insufficient to
authorize resetting a secure or unowned thread. The offline test explicitly
rejects all7..31, including8/9, before any backend access.

No command-fetch translation-enable bit or LK no-IOMMU default has been proven.
The relevant GCE_DM stream is fake larb43 port1, mapped by FDT to the MDP IOMMU
at0x1e810000. `drivers/iommu/mtk_iommu.c:3528–3543` sets MT6897 MDP
SKIP_CFG_PORT, IOVA_34_EN, PGTABLE_PA_35_EN and IOMMU_SEC_EN; lines1468–1470
therefore skip ordinary larb port configuration. The four proposed IOMMU CSRs
show root/status/control context, but no identified field in them establishes
per-stream bypass for GCE_DM. A zero root does not prove bypass; a nonzero root
does not prove that the proposed packet DMA address maps to owned physical RAM.
The stock GCE+0x54 bit17 write is also not an identified MMU-enable field.

There is an ATF-controlled display-virtual-address path, but it must not be
confused with GCE command-fetch translation. `cmdq-util.h:16–28` defines
ENABLE_DISP_VA(1) and MMUEN_SET_DEVAPC_DISABLE(7); `cmdq-util.c:513–531`
invokes those through SMC. The display runtime chain is explicit:
`drivers/gpu/drm/mediatek/mediatek_v2/mtk_drm_crtc.c:7391–7450` requests ATF's
display-VA prebuilt packet, then enables display components' IOMMU ports.
`mtk_drm_ddp_comp.c:1426–1477` writes ordinary component larb
SMI_LARB_NON_SEC_CON bit0. That supplies no address or toggle for fake larb43.
Neither SMC is part of the read-only snapshot or prototype.

The pinned Linux SMC ID also differs from the exact LK call: Linux
`include/linux/soc/mediatek/mtk_sip_svc.h:99–100` defines function0x518, yielding
0xc2000518 for ARM64. Exact LK's cmdq_smc at0xffff00005071b1f4 invokes
0xc2000418 with other arguments zero. The Linux request enum cannot safely be
assigned to that older/different firmware entry point.

Consequently the next read-only snapshot can decide whether inherited power,
clocks, GCTL and all-thread inactivity meet the prototype's prerequisites. It
cannot close command-fetch DMA mapping/bypass or thread ownership. Keep those
external proof bits unset until firmware-specific evidence or a separately
authorized bounded experiment establishes them. The prototype remains unlinked.

### V13 capture: decode of the inherited IOMMU root

Evidence is the completed, read-only
`evidence/raw/2026-10-26-degas-gce-v13-result/console-ramoops-0`: thread0 PC/end
at lines209/211, IOMMU words at223–229, and completion at230. Power/clocks and
GCTL passed, and all32 thread ENABLE values are zero. These resolve the snapshot
accessibility and inactivity prerequisites, not DMA mapping or thread ownership.

The MTK TTBR extension has a concrete decode. Xiaomi kernel source commit
`11f638af362bd6600a47fcb8e4739607fb118d36`,
`drivers/iommu/io-pgtable-arm-v7s.c:885–888`, emits
`ttbr = (u32)(paddr | upper_32_bits(paddr))` when MTK extensions are enabled.
IAS34 makes the level1 table16384 four-byte entries, or64KiB
(lines47–61). For an aligned35-bit table PA the inverse is therefore
`(raw & 0xffff0000) | ((uint64_t)(raw & 7) << 32)`.
**Raw0xc0000007 decodes to PA0x7c0000000**, not0xc0000000 with normal ARM TTBR
cache/shareability flags. The arithmetic is canonical under that source format;
it does not assert that firmware is exposing a currently active root in that
format. The decoded address is aligned and within the35-bit encoding, but
outside this device's recorded DRAM `[0x40000000,0x340000000)`.
Saved LK records two0x180000000-byte ranks at
`evidence/raw/2026-09-14-memory-display/pl_lk.txt:3088–3089`, with the last rank
ending0x340000000 at3116. Do not dereference that root or treat it as owned RAM.
No explicit source definition was found naming0xc0000007 as a reset/sentinel.

The control values strengthen the conclusion that this is not the normal
initialized Android IOMMU context. V13 MISC_CTRL0x000a000a has IN_ORDER_WR bits1/17
and STANDARD_AXI bits3/19 set. The MT6897 flags require both pairs cleared by
`mtk_iommu.c:2069–2075`; definitions are at67–69. V13 CTRL0x1000 lacks MON_EN
bit1 and SYNC_INVLDT bit3, which the same driver sets at2039–2050.
Actual saved Android dmesg independently logs MISC_CTRL0 and CTRL0x100a at
`evidence/raw/2026-10-24-degas-aligned-result/dmesg.txt:1638`. These observations
are consistent with an uninitialized/inactive context, but do not prove reset
defaults or that GCE_DM is bypassing translation.

There is also direct evidence that firmware mediates Android's tables.
The saved dmesg at1625–1626 reports hyp-mmu type2 enabled; at1639 the GCE device
attaches to1e810000.iommu with software TTBR0x53c10000 but hardware readback
0x7bf00000. The pinned device-module driver explicitly distinguishes software
`fake_pa` from `hw_pa` (`mtk_iommu.c:1123–1129`) and obtains actual mappings
through HYP_PMM_IOVA_TO_PHYS (`0xbb00ffa6`, lines242–248 and624–654).
This explains why a readback cannot simply be assumed to be the software-owned
root; it does not establish that this hypervisor interface is available or safe
from Mu. No SMC was invoked or proposed for V13.

Likewise inactive thread0's captured PC0xe7b25221 and END0xe5c2355d are not a
valid retained normal-format command allocation. Inverting the GCE aperture/shift
gives0x6fd929108 and0x6ee11aae8 respectively, both outside IAS34. Treat these
disabled-thread detail words as stale/uninitialized or otherwise uninterpretable,
not a packet location to inspect or reuse. No cause for those values is proven.

Execution blocker remains exact GCE_DM fetch routing: the capture contains no
identified per-stream translation-enable/bypass field, usable owned mapping,
or verified physical-fetch default. Source-backed Linux IOVA behavior and these
inherited values do not justify submitting a CPU physical packet address.

### Preserved exact binaries and normal Linux mapping flow

The three exact modules previously in temporary storage are now preserved in
`evidence/extracted/2026-10-26-gce/`, with SHA256, build IDs, source-ramdisk SHA,
inventory entry offsets and raw GNU notes in `metadata.json` and companion files.
All three size/SHA pairs match the saved live-b vendor-ramdisk inventory:

|Module|SHA256|GNU build ID|
|---|---|---|
|mtk-cmdq-drv-ext.ko|ff73e817d3c2fc2fdc11b3bddb3bbb6b8bf6a962cb4f55f94349608d6065a3ba|88bc6cef637716eea9ca8b64b78724c4099c1830|
|cmdq-platform-mt6897.ko|f30d7e3028923fdecb8e92734831900b73ccd454288a847963f9280e331b5ea6|efd63d507c69f22748c36b5338107c54dfb8b8c1|
|mtk-mminfra-debug.ko|3699241e9c738c57edd674de8c11e56d0c5d4b31cdefc135dd7e5c595dfa52ff|921b07ff17565b9e61216e022640e429ee4c77db|

The source-backed Android mapping order is concrete, and type2 is queried before
the IOMMU is initialized, not enabled later during command submission:

1. GCE's DT IOMMU specifier causes normal IOMMU DMA attach to1e810000. In
   `mtk_iommu.c:1631–1659`, attach finalizes the shared MM table. Finalization
   sets IAS34/OAS35/MTK extensions, calls HYP_PMM_GET_HYPMMU_TYPE2_EN
   (`0xbb00ffa4`, x1..x7 zero), and if enabled selects coherent page-table
   handling and registers the platform's PROTECTED IOVA regions
   (lines1492–1508,1531–1557). This is a query of existing firmware state.
2. Attach resumes the domain, initializes IOMMU controls/interrupts, then writes
   the software ARMv7s TTBR (lines1660–1687). The table may be shared by multiple
   MM IOMMU devices; it is not a GCE-only root. Ordinary per-port configuration
   remains skipped for this MT6897 fake-port case.
3. GCE command allocation obtains a DMA/IOVA address through its device/pool;
   `mtk-cmdq-helper-ext.c:884–943` records this separately from the physical
   address, and commands/PC use the DMA/IOVA. `mtk_iommu_map:1719–1763` writes
   software ARMv7s PTEs. `mtk_iommu_sync_map:1823–1851` calls
   HYP_PMM_HYPMMU_TYPE2_INV (`0xbb00ffa7`, encoded range-start/end, table ID)
   before hardware TLB synchronization when type2 is enabled. HYP_SHARE_IOVA
   is not the normal per-command-buffer API in this branch: the alternate
   non-type2 path only shares designated PROTECTED regions (lines726–773).

This is evidence for the Linux translated route, not a recommendation to replace
the inherited shared IOMMU context before the GCE physical/VA mode is resolved.

Exact saved GZ firmware independently confirms the root mediation. Source image
`evidence/raw/2026-10-24-degas-return/gz_b.img` SHA256
`011c07ed744d69a7b2760a3fb2bdd727327bec08bc844dd4177086e4ce04ee0e` is durable.
Focused disassembly is preserved as
`evidence/extracted/2026-10-26-gce/gz-iommu-root-flow.asm`. The references below
are **raw partition file offsets**; the file also gives image-relative addresses
(file offset minus0x200) so branch targets can be followed without assuming a
runtime base.

- Root-write handler0xdcbc..0xdf34 references exact embedded function name
  `write_mmu_pt_base_addr`. It decodes the incoming software table address,
  compares the previously registered address, and reports
  `ERR: pgd not matched` / `pgd can't be changed!!!` for a different nonzero
  registered root. At0xdee8..0xdefc the first root is recorded and a64KiB table
  association is created. At0xde04..0xde18 it encodes and writes a distinct
  hypervisor-owned NS hardware root; the log explicitly names `(type2)
  hw_pgd_pa`. This directly supports the software/hardware root distinction.
  It also means blindly trying a second guest root may be rejected by firmware.
- `enable_iommu_device`0xf768..0xf994 installs register virtualization through
  the callback table (raw0x83770 includes root-handler pointer0x3800dabc);
  its failure diagnostic names `register_vreg`. `mtk_iommu_post_init` loops
  through the configured devices. This establishes a firmware facility, not
  proof that Mu currently executes inside the VM/context to which it applies.
- Dispatcher0x58cd8..0x58ce4 explicitly matches0xbb00ffa4 and reaches0x58e88,
  which calls the query at0xae38..0xae7c. That function only loads the firmware
  enable word, converts it to boolean, logs and returns. It performs no root,
  mapping or device initialization. The exact binary therefore agrees with
  the Linux query semantics.

The smallest next discriminating experiment, if separately authorized for a new
image, is this **query only**, with CurrentEL captured and RAM breadcrumbs before
and after the SMC under the existing watchdog. It can establish whether Mu's
firmware route exposes type2 and returns enabled, disabled or unsupported. It
does not prove a particular command IOVA mapping or stage2 MMIO trapping; EL1
alone cannot distinguish native execution from a GZ guest. It needs no GCE
thread enable, root write, clock write, address-table dereference or framebuffer
write. No such call has been made by this audit.

In parallel the exact ATF/LK investigation must establish what global GCE+0x54
bit17 (`set_va_mode` in ATF) means for **instruction fetch**, and which mode
survives into Mu. V13 reads this word as zero, but the function name alone does
not prove physical bypass or select an address encoding. A proven physical
fetch route could avoid the translated Linux initialization entirely; until
that distinction is established neither route should be guessed.

### V14: bounded owned mapping design, without command execution

V14 establishes CurrentEL=4 (EL1) and type2 query=1. Its MDP root/control words
and all32 inactive GCE threads remain as in V13. This establishes the query path
in Mu; it does not establish ownership of the shared software root.

The proposed low-heap layout is sufficient for an offline table image:
root64KiB at0x44440000, L2 page at0x44450000 (only first1024 bytes used), and
packet page at0x44451000, all inside owned/quarantined
`[0x44440000,0x44470000)`. No allocation may overlap these ranges. Keep all of
it reserved through the watchdog reset; a failed mapping test cannot free it.
No instruction packet or GCE thread is executed in this mapping-only stage.

For IOVA0x10000000 the Linux MTK ARMv7s format yields exactly:

```c
/* Offline construction only; all three allocations are owned below4GiB. */
ZeroMem (Root, 0x10000); /* 16384 UINT32 entries */
ZeroMem (L2,   0x1000); /* 256 entries used; owned page remains zero */
ZeroMem (PacketPage, 0x1000);
Root[0x10000000 >> 20] = 0x44450000 | 0x9; /* TABLE | NS */
L2[(0x10000000 >> 12) & 0xff] = 0x44451000 | 0xc42;
/* c42: small-page2, NG, shareable, TEX1; NO_PERMS, normal NC. */
/* Clean owned table/packet ranges to PoC if CPU mappings are cacheable;
   DSB SY before a firmware/root publication. No root write in this snippet. */
```

The parent builder must verify the addresses, alignments, full range ownership,
and zero entries before considering a root write. The descriptor derivation is
`io-pgtable-arm-v7s.c:185–209,338–365,473–482` in the pinned Xiaomi kernel.
CPU memory barriers/cache maintenance remain required even though no DMA runs.

The calling ABI and units are source-backed:

```c
/* TYPE2_INV: values are encoded byte addresses, not page numbers/counts. */
Smc (0xbb00ffa7, 0x10000000, 0x10000000, 0 /* MM_TABLE */, 0,0,0,0);
/* IOVA_TO_PHYS: low/high halves of the byte IOVA; table ID, not IOMMU ID. */
Raw = Smc (0xbb00ffa6, 0x10000000, 0, 0 /* MM_TABLE */, 0,0,0,0);
UINT32 Ns = (UINT32)Raw, Prot = (UINT32)(Raw >> 32);
UINT64 Pa = ((UINT64)(Ns & 7) << 32) | (Ns & 0xfffff000U);
UINT32 Type = (Ns >> 8) & 3, Level = (Ns >> 10) & 3;
/* Required success: Type==2, Level==2, Pa==0x44451000.
   Log both NS and PROTECTED halves and reject exact firmware error returns.
   Do not classify Raw merely by its sign: a valid protected half may set bit63.
   An invalid PTE can return descriptor0; a missing table can return -3. */
```

`MTK_IOMMU_TLB_ADDR` at `mtk_iommu.c:272–276` combines address bits31:12 with
upper address bits in the low word; for this low IOVA its output is unchanged.
`mtee_hypmmu_type2_inv:697–710` supplies inclusive start/end (size-1);
`mtee_iova_to_phys:624–654` supplies and decodes the query ABI. The source casts
must be widened before the PA-high shift in any standalone implementation.

There are three concrete limitations to account for before a hardware mapping
test; these are effects of the exact GZ code, not hypothetical generic IOMMU
risks:

1. **A first root registers software memory while retaining firmware tables.**
   GZ association creation at raw0xb650..0xb708 allocates a24-byte association,
   records the software root PA/64KiB length, and maps that memory. It does not
   zero/recreate existing NS hardware or protected page tables. Root handling
   at0xde04..0xdeac rebinds the affected IOMMU's NS/protected bank registers to
   those existing firmware tables. Existing LK reserved slots outside the
   synchronized IOVA range are therefore not cleared by association creation.
   The known CMDQ reserved IOVA0x107400000 is outside the proposed range.
2. **The collision guard is per-device; the software table is shared.**
   At0xdd9c..0xddc0, GZ compares against the selected IOMMU device's registered
   guest root (device+0x88). `enable_iommu_device:0xf818..0xf81c` installs that
   device pointer in the register descriptor. Association creation then indexes
   the global table record by device table-ID at+0xc4 and writes its software
   PA/size/VA fields at+0x80/+0x88/+0x90. Thus the first MDP registration can
   replace a global MM_TABLE0 software association already established through
   DISP, even if MDP's local guard is zero. Linux avoids this by sharing the same
   root across MM devices. Both MT6897 DISP/MDP platform records use MM_TABLE0
   (`mtk_iommu.c:3511–3543`). An MDP-only snapshot cannot prove global ownership.
   No established public query returns the registered guest root. The root-read
   callback at0xd024..0xd094 returns actual hardware root, not that guest PA.
   A same-device conflicting root branches to the fatal helper at0x5e6e0..0x5e78c
   before MMU/table writes. Expect panic/watchdog reset, not a recoverable error;
   it does not protect a different device's first registration.
3. **One-page INV processes a complete1MiB L2 block.** GZ's INV handler
   at0xe5b4..0xeb68 selects L1 entries from the supplied range. For each selected
   L1 table descriptor, it scans all256 L2 entries (0xe93c,0xe9a0..0xe9a8,
   0xeb10..0xeb1c), synchronizing the whole1MiB block into the hardware NS table.
   It can also clear/update the corresponding protected leaves. Claim the whole
   `[0x10000000,0x10100000)` IOVA granule, not just one page. Previously existing
   normal/protected mappings in that granule would be affected. A query returning
   no mapping does not alone establish root ownership. Missing-table INV logs
   and returns0 (0xeea0..0xeec8), so its return0 is insufficient: the positive
   query and adjacent unmapped queries are essential.

The mapping-only experiment can therefore be isolated from GCE execution, but
its root publication is a shared-MM-table operation. Before that publication,
account for DISP as well as MDP and the known protected slots; table0 may have
users even when all GCE threads are inactive. Merely being separate physical
IOMMU blocks does not make their GZ software tables independent. Preserve the
working display configuration and all firmware-owned protected reservations.
The exact ATF protected mapping helper, independently decoded by the companion
agent, allocates fixed protected/secure slots; it is not a generic normal NS
buffer allocator and does not remove these ownership requirements.

No root restoration to a different guest PA should be attempted. A clean mapping
test result still does not establish DMA fetch, VA-mode permissions, TLB setup,
or autonomous frame completion; those remain separate later stages. This audit
performed no hardware call, root mutation, command submission or production edit.

#### Can the mapping-query preflight run before the first guest root?

Yes, the exact firmware design permits it. A6 is not inherently a missing-table
error before normal guest registration. Its guards at raw0xda64..0xdaec require
only the hardware NS table VA (+0xb8) and protected table VA (+0xa8). It does not
require the fake/software guest table VA (+0x90).

Those hardware tables have an independent firmware initialization path:
boot-argument parsing at raw0x3d24..0x3d2c passes the reserved base/size to
`hyp_pmm_assign_pgd_tables` at0xab3c, which assigns protected PA (+0x98) and NS
hardware PA (+0xb0). The init-array entry at raw0x82af0 points to
`hyp_mmu_init` (raw0xb5b4). That routine calls0xb458, whose loop maps the64KiB
protected and hardware NS tables and stores VA fields +0xa8/+0xb8. The code
does this independently of the later normal guest-root association. These
excerpts are now preserved in the same GZ disassembly evidence. Actual readiness
still comes from A6's runtime result: exact error return -3 must abort before
any root write; it is not permission to initialize or dereference a missing table.

The parent's revised single-boot mapping test accounts for the observed shared
state effects: query all256 pages in the chosen1MiB granule first; require both
NS and protected TYPE0 with no firmware error; establish owned/quarantined RAM;
register the owned software root; synchronize only that granule; then query all
256 pages again, requiring exactly one expected NS small page and the others
invalid. Compare complete pre/post query results at the known reserved CMDQ
markers0x107400000/0x1074ff000 and framebuffer marker0xfcc2f000. On failure or
completion, end in watchdog reset: no GCE execution, frees, different-root
restoration, Windows or Android chainload. Under that scope the global software
association is a temporary sole-client association until reset; existing
firmware PTEs outside the1MiB sync range are retained by the decoded code.

This review supports that **mapping-only** experiment with those preconditions.
It does not turn a failed preflight into authorization for initialization, prove
the GCE VA-mode routing, or verify DMA transport. The fatal same-device collision
path and the different-device/global-association distinction remain as described
above; the former may end in watchdog recovery before the root write completes.
