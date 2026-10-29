# Stock LK framebuffer analysis

This analysis closes the stock boot framebuffer layout for the Xiaomi 14T (`degas`, MT6897) without guessing values and without reading physical or MMIO addresses. It combines the retained live LK log with static disassembly of the exact stock `lk_b` image prefix.

## Proven contract

| Property | Proven value | Evidence |
|---|---:|---|
| Physical base | `0xfcc2f000` | Live LK allocation/init log; exact BL2_EXT active-layer address |
| Reservation size | `0x033d0000` (54,329,344 bytes) | Live LK allocation and retained mblock log |
| Reservation end, exclusive | `0xfffff000` | Base plus size |
| Visible geometry | 1220 x 2712 | Live DSI/panel log and full-frame logo-write coordinates |
| Logo-path rotation | 0 degrees | Exact main-LK screen structure passed to the writer |
| Aligned geometry | 1248 x 2720 | Exact BL2_EXT `(dimension + 31) & ~31` getters |
| Bits per pixel | 32 | Exact BL2_EXT getter and exact main-LK logo setup |
| Active pitch | 4,992 bytes | Exact active layer uses `align32(width) << 2` |
| Active offset | zero | Exact active layer uses the framebuffer base unchanged |
| Active layer | 0 | Exact zeroed `disp_input_config`, then `layer_en = 1` |
| MediaTek format | `DP_COLOR_BGRA8888` / `eBGRA8888`, value `0x01002002` | Exact immediate in both BL2_EXT layer configurations; MediaTek packed-enum definition |
| Pixel word | `0xAARRGGBB` on little-endian AArch64, bytes `BB GG RR AA` | Exact main-LK `32bit_argb8888` writer stores source word unchanged |
| UEFI format | `PixelBlueGreenRedReserved8BitPerColor` | Direct translation of the proved byte layout |
| UEFI masks | R `0x00ff0000`, G `0x0000ff00`, B `0x000000ff`, reserved `0xff000000` | Direct translation of the proved word layout |

The per-frame allocation is `1248 * 2720 * 4 = 13,578,240 = 0x00cf3000` bytes. The visible 2,712 rows occupy `4992 * 2712 = 0x00ce9400` bytes. The reservation is four aligned frames plus `0x4000` bytes:

```text
4 * 0x00cf3000 + 0x4000 = 0x033d0000
```

The active framebuffer is therefore the following exact range for one aligned frame:

```text
base                 0xfcc2f000
pitch                0x1380 (4992)
one-frame end         0xfd922000 (exclusive)
reservation end       0xfffff000 (exclusive)
```

## Why the offset is zero

The exact BL2_EXT display initializer creates two `disp_input_config` records. The first is layer 1, uses `base + 0x00cf3000`, and has a 4,880-byte `1220*4` pitch. This is a separate boot-menu overlay. The second record is the enabled layer 0 framebuffer: it loads the physical base directly, sets the source and destination to 1220 x 2712, and sets `src_pitch` to `align32(width)*4`, or 4,992 bytes.

The exact main LK independently confirms which surface receives the boot logo. Its framebuffer getter returns the stored base directly. The logo setup passes that pointer as its destination, enables aligned width, selects 32 bpp, and calls a writer that indexes each row using the aligned width. The retained log records writes at all four visible corners, through row 2711 and column 1219, followed by a framebuffer dump and `notify_boot_linux`.

The layer-1 address `0xfd922000` must not be used as the handoff base. It is the secondary boot-menu surface at one aligned-frame offset. The layer-0 address is exactly `0xfcc2f000`.

## Format proof and naming

BL2_EXT constructs the format value with two AArch64 immediates:

```text
mov  w21, #0x2002
movk w21, #0x100, lsl #16
result: 0x01002002
```

MediaTek's public LK color definition packs `DP_COLOR_BGRA8888` as one plane, 32 bits, RGB group, swap disabled, unique ID 2, which evaluates to `0x01002002`. `eBGRA8888` is an alias for that enum. The alternative `DP_COLOR_RGBA8888` sets the swap bit and evaluates to `0x01002022`, so the exact immediate distinguishes the two.

The main-LK draw path supplies another check that does not depend only on the enum name. It calls the logged `fill_rect_with_content_by_32bit_argb8888` path. The screen structure has `blue_offset = 0`; the exact binary therefore stores each `0xAARRGGBB` source word unchanged. A little-endian store lays that out as bytes blue, green, red, alpha/reserved. For UEFI GOP this is `PixelBlueGreenRedReserved8BitPerColor`.

This format statement describes the LK framebuffer memory. A later Android DRM plane may use a different buffer, IOVA, or fourcc after Linux takes over; those values do not redefine the retained LK allocation.

## Evidence identity and extraction

The stock firmware file is 3,682,512 bytes with SHA-256 `92e0432da31ab7c2b549bbca2ac0ca63ccc4e3edb4804b42c99fa11642cb339a`. Its complete byte range is identical to bytes `[0, 3682512)` of the 16 MiB captured `lk_b.img`. This proves that the captured partition begins with the analyzed stock image. It does not claim the shorter stock file equals the entire partition capture.

The MediaTek container uses 512-byte headers with little-endian magic `0x58881688`. The relevant executables are raw, uncompressed AArch64 payloads:

| Payload | File offset | Size | SHA-256 |
|---|---:|---:|---|
| `lk` | `0x200` | `0x1070c0` | `6afd84aefda089681f0ec4a224c6710228f525ec312bfa2532977be9e4496580` |
| `bl2_ext` | `0x108370` | `0x1035f0` | `74f9bf4faafc694193dda1dc1f9bef40338cd389278781292d9f8044d4d3a35a` |

The retained log reports BL2_EXT loaded at physical address `0x78000000` and the main LK payload loaded at `0xffff000050700000`. Absolute AArch64 references in BL2_EXT establish its `0xffff000078000000` virtual/link base. Minimal ELF wrappers assign those virtual addresses to the raw bytes for `llvm-objdump`; they add no symbols and perform no transformation of the payload.

Reviewable artifacts are in [`evidence/extracted/2026-09-14/lk-analysis/`](../evidence/extracted/2026-09-14/lk-analysis/):

- [`framebuffer-contract.json`](../evidence/extracted/2026-09-14/lk-analysis/framebuffer-contract.json) is the machine-readable result.
- [`disassembly-evidence.md`](../evidence/extracted/2026-09-14/lk-analysis/disassembly-evidence.md) records the decisive instructions and addresses.
- [`live-log-excerpts.txt`](../evidence/extracted/2026-09-14/lk-analysis/live-log-excerpts.txt) records the relevant retained-log lines.
- [`container-manifest.json`](../evidence/extracted/2026-09-14/lk-analysis/container-manifest.json) records payload offsets and hashes.

## Source cross-checks

No public source for this exact MT6897 Xiaomi LK build was found, so source code is used only to name structures and constants already proved by the exact binary. The older public MediaTek LK lineage at commit [`37ec8bcbc4361c0e267617f517d154656804c17a`](https://github.com/svoboda18/lk/tree/37ec8bcbc4361c0e267617f517d154656804c17a) provides:

- the [`disp_input_config` field order](https://github.com/svoboda18/lk/blob/37ec8bcbc4361c0e267617f517d154656804c17a/platform/mt6785/include/platform/primary_display.h),
- the [`DP_COLORFMT_PACK` and `DP_COLOR_BGRA8888` definitions](https://github.com/svoboda18/lk/blob/37ec8bcbc4361c0e267617f517d154656804c17a/platform/mt6785/include/platform/ddp_data_type.h),
- the matching [framebuffer and boot-menu layer construction](https://github.com/svoboda18/lk/blob/37ec8bcbc4361c0e267617f517d154656804c17a/platform/mt6785/mt_disp_drv.c), and
- the matching [32-bit logo writer behavior](https://github.com/svoboda18/lk/blob/37ec8bcbc4361c0e267617f517d154656804c17a/lib/libshowlogo/show_logo_common.c).

The exact runtime values come from the retained device log and the matching stock image, not from this older source tree.

## Limits

The evidence proves the LK allocation, the active layer configuration, and the memory layout at boot. It does not prove that display hardware is still scanning the layer at every instruction after `notify_boot_linux`, nor does it prove how firmware cache attributes or coherency behave when UEFI begins executing. Static code also does not establish a count of independently managed display buffers: the reservation is arithmetically four aligned frames plus `0x4000`, while the exact initializer explicitly configures only the base framebuffer and one secondary boot-menu surface. Those are runtime handoff concerns and must be handled by the platform's display ownership and cache policy. They do not make the base, pitch, pixel layout, rotation, or zero offset unknown.
