# Direct stock Android EFI boot — offline candidate

Status: v10b passes offline checks but **failed the first hardware boot**.
Android began starting, then rebooted before completion. Stock `boot_b` was
restored afterward and `sys.boot_completed=1` verified. Do not publish v10b as
a working Android route.

## First hardware result

Evidence: `evidence/raw/2026-10-30-degas-android-efi-v10b-result`.
The retained console contains Linux paging faults at about 22.36 seconds:
`ESR=0x96000046`, level-2 translation fault on a write. The first recorded
fault is in `clear_page`, reached through `post_alloc_hook`, page allocation
and a user-page fault. Several CPUs fault on nearby linear-map addresses
`ffffff80bda00000..ffffff80bdbff000`.

Immediately before this, the display driver reports
`mtk_crtc_frame_buffer_release, free frame buffer`. This is a lead to audit
against the EFI memory map and DT reservations, not proof of causality.
The retained console lacks the early EFI memory-map lines. Next investigation
must compare the stock LK memory contract with EFI-discovered RAM and the
display framebuffer-release path before another image is flashed.

## Reason for changing the route

The reboot-based route repeatedly returned to UEFI after RF tests. The live V9
footer showed `A E 6 M 9 S 1 Q 0` with an increasing tick. Retained console
generations were incomplete, so neither flag loss nor a particular failing
handoff stage is established. Software reset and a console-token backup did
not resolve the observed behavior.

The new opt-in `DEGAS_ANDROID_EFI_BOOT=1` route avoids reboot flags. The original
uncompressed Android Image has an ARM64 PE/COFF EFI entry. It is loaded with
UEFI LoadImage/StartImage, using the original LK device tree and initrd. This
follows the Linux EFI-stub interface documented at
https://docs.kernel.org/admin-guide/efi-stub.html . Header validity and a
successful UEFI build do not establish hardware compatibility.

## Implementation and boundaries

- Protect the original kernel at `0x42200000` as BootServicesData instead of
  allowing DXE allocations over the old `0x40400000` source. The wrapper remains
  `0x4480000` bytes; GCE and UFS DMA arenas remain separate.
- Verify the kernel CRC before loading it. Validate the original DTB and initrd
  bounds before use; copy the DTB into allocated memory.
- Supply the initrd through Linux LoadFile2 and the DTB through the FDT
  configuration table. Preserve bootargs and append `acpi=off efi=noruntime`.
- Connect the standard UEFI console before invoking the stub. The Android EBS
  callback disables the watchdog without applying Windows context patches or
  starting the autonomous Windows display loop.
- Report pre-EBS failures by stage/status and return to the menu. Hardware
  behavior after EBS remains unqualified, including display, GZ and reset paths.
- Do not modify Android partitions, GZ, ATF, LK, preloader or Windows files.

The preserved LK initrd location is based on captured data and checked at
runtime. A different LK layout must fail validation, not be silently assumed.

## Reproducible candidate

Directory: `artifacts/degas-android-efi-v10b`.

- Build ID: `3429e167-b9e5-4914-9fea-764880e5ae76`
- boot.img SHA256: `9875a74cc70fac2dc71ca195b4b3db12ce2d633091c87dfedee7867fdda75514`
- Stock raw Image SHA256: `dbb1b31dfc4cc1beee2bd4600591cf5420d39969b5ab142b9eeb3ef2e442668a`
- `android-efi-qualification.json`: offline pass; all 49 memory descriptors
  disjoint, protected kernel bytes exact, GCE/UFS arenas initially zero.
- Ten linked ARM64 route regression tests passed for each of the legacy and
  protected layouts. They test the early route, not the Linux EFI stub.

The earlier v10 candidate is superseded by v10b's console and cleanup fixes.
Neither candidate is a public release or a proven Android boot fix.

## Next hardware test, after the user returns

First verify stock Android and preserve the existing evidence. Capture a fresh
preflight and use the qualified boot-only flashing workflow. Test ANDROID alone
before introducing an RF test. Record any `ANDROID EFI S ... E ...` failure or
visible kernel output. A successful plain Android boot must precede testing
the RF-test-to-Android path. Keep stock boot recovery available.

## Framebuffer contract audit after the failed boot

The saved audit `framebuffer-memory-audit.json` compares the captured LK DTB
with the actual v10b descriptor and decodes all four fault addresses using
the 39-bit linear-map base and logged `PHYS_OFFSET=0x40000000`.

| Contract | Start | Exclusive end | Bytes |
| --- | --- | --- | --- |
| DT framebuffer / chosen videolfb | `0xfcc2f000` | `0xfffff000` | 54329344 |
| EFI Display Reserved | `0xfcc2f000` | `0xfd919000` | 13541376 |
| Missing from EFI RAM descriptors | `0xfd919000` | `0xfffff000` | 40787968 |

Fault VAs translate to physical `0xfda00000`, `0xfda1e000`, `0xfda98000`,
`0xfdbff000`: all four fall inside the DT framebuffer and outside the EFI
descriptor. The display reference's `free_fb_buf()` uses chosen videolfb
base/size and passes every page to `free_reserved_page()`. The trace shows
later allocation/clearing of these pages faults because their linear mapping
is absent. This is strong, address-specific evidence for a memory-contract
failure in the new EFI route. It does not establish the older reboot-route
failure's cause.

Two additional constraints must be handled in a correction; merely extending
the old reserved descriptor is insufficient:

1. Linux v6.1 EFI `reserve_regions()` discards DT-discovered RAM and rebuilds
   it from EFI. `EfiReservedMemoryType` is not usable mapped RAM; the current
   `WRITE_THROUGH` descriptor does not meet the writeback usable-RAM test.
   Android's DT reservation must remain reserved before display takeover,
   while the underlying pages must retain a valid linear RAM mapping so the
   vendor release path can return them safely.
2. Linux `init_screen_info()` marks an EFI GOP framebuffer nomap. The native
   Android DRM driver expects ownership through its DT contract, so the EFI
   generic framebuffer reservation must not conflict with that contract.

Primary reference sources, downloaded under
`evidence/extracted/2026-10-30-android-efi-memory`:

- https://github.com/torvalds/linux/blob/v6.1/drivers/firmware/efi/efi-init.c
- https://github.com/torvalds/linux/blob/v6.1/arch/arm64/include/asm/memory.h
- https://github.com/torvalds/linux/blob/v6.1/drivers/firmware/efi/libstub/efi-stub.c

These are upstream v6.1 references, not a claim that every vendor function
matches byte-for-byte. The display reference is
`evidence/extracted/2026-10-29-display-sync/mtk_drm_crtc.c:5482` onward.
## Scoped correction

`AndroidMemoryView.h` now supplies an Android-only GetMemoryMap view during
StartImage. It checks the existing framebuffer descriptor and rejects any
unexpected overlap, preserves the real firmware allocator and map key, then
reports the complete DT display range as writeback LoaderData. Native DT
reservation still keeps those pages reserved until the vendor DRM driver
releases them. No global platform descriptor or Windows memory view is changed.
Buffer-too-small retries account for the extra descriptor without mutating
the incomplete buffer; extended descriptor stride is retained.

All GOP mode-info instances, including a possible console splitter copy,
temporarily advertise BLT-only output to the Android stub. Existing EFI BLT
console rendering remains available, while Linux's generic raw GOP framebuffer
does not install a conflicting nomap reservation. The mode-info fields and
Boot Services function/CRC are restored if the kernel returns before EBS.
The original FDT chosen framebuffer properties must match the captured range
or the launch is rejected before starting Linux.

The actual transformation code passes a host test with ASan/UBSan, covering
the four recorded crash pages, capacity retries, extended descriptor stride,
unrelated descriptor preservation and rejection of conflicting/runtime ranges.
### v11b first hardware boot

Image SHA256: `ecf58eba16152a89be5cf994a481cf22078ba6fc019fa258a4241c85150fcb67`.
The user reports Android stays operational. Evidence is saved under
`evidence/raw/2026-10-30-degas-android-efi-v11b-result`:

- `sys.boot_completed=1`, `/sys/firmware/efi` present.
- Retained firmware console records the new memory view, stock kernel/initrd
  loading and Android ExitBootServices.
- Current dmesg records framebuffer release at 14.155611 seconds, without the
  previous kernel paging faults/Oops in the captured log.
- `/proc/iomem` includes `fcc2f000-12fdcffff : System RAM`, with the native
  `fcc2f000-ffffefff` reservation. The four previous fault pages are now in RAM.

This confirms one ordinary Android launch beyond the prior failure point,
not long-term stability or the RF-test-to-Android path. The latter is next.

### Warm RF attempt

Evidence: `evidence/raw/2026-10-30-degas-android-efi-v11b-rf-result`.
After a warm restart from Android, the RF test read CONN_CTL `0xc200000d`
(already on), instead of its required off baseline. It returned RF code 2,
`owned=0 ready=0 restored=1`: no RF power changes or active CONN test occurred.
Keep this guard; do not force a shared/already-powered domain through the cold
sequence. The subsequent direct Android boot completed (`sys.boot_completed=1`,
uptime over two minutes), with native framebuffer release and no prior paging
fault signature in the captured current dmesg. This does not qualify return
after a successful active RF power cycle. That requires a cold initial state.

### Cold RF cycle followed by Android: pass

Evidence: `evidence/raw/2026-10-30-degas-android-efi-v11b-cold-rf-result`.
The retained console includes both the active test and subsequent direct boot:

`RF result=0 owned=1 ready=1 restored=1 conn_code=0 conn_owned=1 conn_restored=1 id=02050400 ownership=00000004 pins_restored=1`.

It then records the intentional software reset, the v11b Android memory view,
kernel/initrd loading and Android EBS. ADB confirms `sys.boot_completed=1`.
Current dmesg records native framebuffer release at 14.623024 seconds and has
no occurrence of the previous paging-fault/Oops/panic signatures. The user
reports Android operational. Thus one cold RF control/CONN-ID/cleanup cycle
followed by a complete Android EFI boot is hardware-confirmed. Long-term
stability, warm-state active power transitions, A-die communication and WLAN
firmware execution are separate unqualified work.

Wi-Fi firmware start is still pending. RF control and CONN digital ID success
do not prove A-die communication or WLAN firmware execution.
