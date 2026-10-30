# Android boot through UEFI

Direct Android EFI boot passed both ordinary startup and startup after a cold
RF/CONN test. Tested image SHA256:
`202b99cbb28993350c990f6ecce9ca0d4a29744f35584663803000416c752284`.

## Boot path

`AndroidEfiBoot.h` loads the stock ARM64 EFI-stub kernel with LoadImage/StartImage.
It passes the original LK DTB and initrd, preserves bootargs and appends
`acpi=off efi=noruntime`. The kernel CRC, DTB and initrd bounds are checked
before launch. Android does not use the Windows context patch or GCE loop.

The kernel source is protected at `0x42200000`, below GCE memory. The wrapper
remains `0x4480000` bytes. A failed pre-EBS launch restores the EFI table and
protocol state and returns an error to the menu.

## Framebuffer crash and fix

The first EFI attempt reached Android, then faulted in `clear_page` at about
22.36 seconds after the native display driver freed the LK framebuffer.

| Memory description | Start | Exclusive end |
| --- | --- | --- |
| DT framebuffer | `0xfcc2f000` | `0xfffff000` |
| Original EFI descriptor | `0xfcc2f000` | `0xfd919000` |
| Missing range | `0xfd919000` | `0xfffff000` |

The four fault addresses translated to physical `0xfda00000`, `0xfda1e000`,
`0xfda98000` and `0xfdbff000`, all in the missing range.

Linux rebuilds its RAM map from EFI. A reserved/nomap range cannot be returned
to the page allocator by the vendor DRM driver. `AndroidMemoryView.h` therefore
provides an Android-only map covering the full DT framebuffer as writeback
LoaderData; the DT reservation remains until DRM releases it. GOP instances
advertise BLT-only during this launch so the stub does not add a conflicting
raw-framebuffer nomap reservation. Windows and the firmware allocator retain
their original map. Pre-EBS failure restores the temporary changes.

## Test results

- v10b: paging fault; capture `2026-10-30-degas-android-efi-v10b-result`.
- v11b: Android boot completed, EFI present, native framebuffer release passed.
- Warm RF attempt: the cold-state guard refused an already-on CONN domain;
  Android still booted.
- Cold RF cycle: RF/CONN cleanup passed, then Android completed boot.
- A-die v3: independent RF-SPI ID passed, cleanup passed, Android completed boot.

Host tests cover descriptor stride, buffer-size retries, overlap rejection,
state restoration and coverage of the four original fault pages. The older
reset-marker route repeatedly returned to UEFI; its failing retention stage
was not established by the incomplete logs and is no longer used for Android.

## References

- [A-die test](DEGAS-WIFI-ADIE-ATF-20261030.md)
- [Original reset-route investigation](DEGAS-ANDROID-ROUTE-TRACE-20261030.md)
- [Linux EFI stub](https://docs.kernel.org/admin-guide/efi-stub.html)
- [Linux EFI memory handling](https://github.com/torvalds/linux/blob/v6.1/drivers/firmware/efi/efi-init.c)
