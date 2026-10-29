# LK handoff evidence

This note records only what the retained LK log and the verified, extracted slot-B boot images establish about the Android kernel handoff. It does not approve a custom payload layout or a phone operation.

## Result

| Item | Exact value | Evidence class | Limit |
|---|---:|---|---|
| LK kernel reservation | `[0x40000000, 0x47c80000)`, size `0x7c80000` | Log fact: `mb_kernel`, `map:0` | A reservation is not proof of the bytes actually loaded, the decompression destination, or the branch target. |
| Stock kernel container | legacy LZ4, `0xfc718a` bytes | Verified extracted-image fact | SHA-256 `618d772325d2b369a163c1dae6b1f517e3d709bc0ce6c61de85ba429d506f191`. |
| Decompressed stock kernel | `0x21caa00` bytes | Reproducible offline derivation | Five legacy-LZ4 blocks; SHA-256 `dbb1b31dfc4cc1beee2bd4600591cf5420d39969b5ab142b9eeb3ef2e442668a`. This size is the decoded file length, not the ARM64 memory footprint. |
| ARM64 Image header | `text_offset=0`, `image_size=0x2270000`, flags `0xa`, magic `ARMd` | Decompressed-image fact | Fields are at decoded offsets `0x8`, `0x10`, `0x18`, and `0x38`. |
| Stock intended load / pre-secure kernel target | `0x40000000` | Combined inference | The slot-B `vendor_boot` header says kernel load address `0x40000000`; the decoded Image has `text_offset=0` and begins with its entry instruction. The retained log does not print the actual decompression destination or final branch register. The later secure-firmware entry remains unknown. |
| LK DTB reservation | `[0x47c80000, 0x48080000)`, size `0x400000` | Log fact: `mb_dtb`, `map:0` | The reservation is larger than every observed DTB. |
| Vendor DTB header | address `0x47c80000`, size `400206` (`0x61b4e`) | Verified extracted-header fact | Extracted DTB SHA-256 `ce9fa587134c770e4d3bbe576fe29a5ec89c795789bf8830c0179525625d79cf`. |
| LK DT overlay result | `488607` bytes (`0x7749f`) after applying DTBO entry 0, whose payload is `88465` bytes | Log fact | The logged `lk_main_dtb` and `lk_dtbo` load addresses are LK scratch virtual addresses, not the kernel handoff DTB address. |
| Later live DTB | address `0x47c80000`, total size `0x781a2` | Reconciled live-runtime fact | It is 3331 bytes larger than the logged post-overlay value, so the log value must not be substituted for the final live DTB length. |
| LK initrd reservation | `[0x66f00000, 0x6af00000)`, size `0x4000000` | Log fact: `mb_ramdisk`, `map:0` | This is capacity, not the exact populated length. |
| Slot-B ramdisk components | vendor ramdisks `61718424` bytes total; `init_boot` ramdisk `3620170` bytes | Verified extracted-header facts | Component sum is `65338594` (`0x3e4fce2`) bytes. |
| Later live initrd | `[0x66f00000, 0x6ad5012e)`, size `0x3e5012e` | Reconciled live `/chosen` fact | This is 1100 bytes larger than the extracted component sum. The retained evidence does not name those extra bytes, so no bootconfig-padding explanation is asserted. |
| Boot mode | numeric `0`; normal boot | Log fact | `BOOT_MODE: 0` and an explicit `normal-boot mode` message agree. Bootconfig independently records `androidboot.force_normal_boot="1"`. |
| Active slot | `_b` | Live bootconfig fact | The log also names `init_boot_b`, `vendor_boot_b`, and `lk_b`. |
| Entry exception level | unknown | Not captured | No retained line records `CurrentEL` at the kernel or a custom-payload entry. |
| Entry MMU/cache state | unknown | Not captured | No retained line records `SCTLR_ELx`, `TCR_ELx`, `TTBRx_ELx`, or cache state at entry. |

The stock address `0x40000000` is therefore a tightly supported stock-path target, but it is not an entry-state contract for a custom payload. This log alone does not show format dispatch or the secure-firmware result. The separate exact-image static audit in `docs/LK-HANDOFF-STATIC.md` closes the first point: the analyzed path accepts gzip or legacy LZ4 and rejects a raw, uncompressed Image; it then passes the kernel target and DTB target through SMC `0xc2000115`. The secure handler's output EL, register convention, MMU/cache state, and ultimate branch address remain unknown.

## Raw LK citations

Line numbers count LF-delimited records in `evidence/raw/2026-09-14-memory-display/pl_lk.txt`. Byte ranges are zero-based and end-exclusive in that exact 262244-byte file.

| Lines | Byte range | Observation |
|---:|---:|---|
| 1 | `[0,99)` | Ring header: buffer `0x40000`, PL size `0x1d375`, LK size `0x2288b`. |
| 3353 | `[114832,114845)` | `BOOT_MODE: 0`. |
| 3401–3404 | `[116081,116192)` | `mb_kernel`: start `0x40000000`, size `0x7c80000`, `map:0`. |
| 3405–3408 | `[116193,116301)` | `mb_dtb`: start `0x47c80000`, size `0x400000`, `map:0`. |
| 3445–3448 | `[117325,117437)` | `mb_ramdisk`: start `0x66f00000`, size `0x4000000`, `map:0`. |
| 3576–3577, 3587 | `[121720,121903)`, `[122757,122852)` | The three reservations are repeated by `mblock_show_internal`. |
| 7271–7303 | `[240407,241670)` | `lk_main_dtb` size `400142`, DTBO image size `88529`, selected DTBO payload `88465`, overlay result `488607`. Scratch loads are at `0xffff000050a90010` and `0xffff000050b18790`. |
| 7312 | `[242067,242118)` | Explicit `normal-boot mode` message. |
| 7492 | `[253095,253165)` | `boot_mode = 0, boot_reason = 4`. |
| 7532, 7538 | `[255928,255970)`, `[256171,256219)` | Linux boot notification and readiness event. These do not print the handoff registers. |
| 7568–7608 | `[258045,260456)` | Slot-B `vendor_boot` and `init_boot` exist; AVB reports descriptor digest mismatches, then the boot/init_boot/vendor_boot headers are read. |
| 7651 | `[262231,262244)` | Capture ends mid-line (`get_suffix:21`), proving that the final load/decompress/jump messages, if emitted later, are outside this retained snapshot. |

The log's `lk_main_dtb` scratch loads are deliberately excluded from the handoff address table. Their `0xffff0000...` values are temporary LK virtual addresses used before the overlay. The handoff reservation and the vendor header both place the kernel-facing DTB at physical `0x47c80000`.

## Image and hash provenance

The raw log was captured by `head -c 4194304 /proc/pl_lk` at `2026-09-14T18:10:24.596130+00:00`; the capture manifest records 262244 bytes and SHA-256 `e49d8c091224f73ac077886cfd46bab46dfe7f5adf3e0d78838861c919744948`. The capture manifest itself hashes to `ccc5df49ea1fb891e61fa34dc9b22e5a7ae4810220a7c8e86c0d5350cd783a35`.

`evidence/extracted/2026-09-14/boot-images-complete/manifest.json` hashes to `2cb3b8cdc955daf730d6af5ebe9035561ea5f88c802d7345d70b4de9ab9560f5`. It identifies the live slot-B sources as follows:

| Source image | SHA-256 | Relevant verified output |
|---|---|---|
| `boot_b.img` | `fa01819342dd24164dea568f88f9715bc3132f738a68c49453907982946c1113` | kernel: 16544138 bytes, SHA-256 `618d772325d2b369a163c1dae6b1f517e3d709bc0ce6c61de85ba429d506f191` |
| `init_boot_b.img` | `f56e8385d9fccb159b85f136e39f25ead61ccba6d75ea39b35925867748c20b9` | ramdisk: 3620170 bytes, SHA-256 `2d8117b3ef62c5eb050b7d6191ba4f0cffcc6d0b8f40fdbcd237e337fb5504bd` |
| `vendor_boot_b.img` | `eb5a09e9ee7a50039492f0d28fdc2eb07baadbb7102a5ac79c293b9c47edaf2e` | DTB: 400206 bytes, SHA-256 `ce9fa587134c770e4d3bbe576fe29a5ec89c795789bf8830c0179525625d79cf` |
| `dtbo_b.img` | `7c7c8262e482188d074ab626f6c7eac1ba6e37adbf6aa2869368d2d8f66c5896` | entry 0: 88465 bytes, SHA-256 `1a367545a29e4e7d5dc5e342a118a243d2d7d818a4f81055914d36b0438f9267` |

The extracted header references are `live-b-boot/header.txt` lines 2–6, `live-b-init_boot/header.txt` lines 2–6, and `live-b-vendor_boot/header.txt` lines 2–12. The manifest records the AOSP unpacker at commit `d2bb0af5ba6d3198a3e99529c97eda1be0b5a093`, script SHA-256 `a9d260978a63bd06a24b6347e7dee8a28ff96639793caea15dff6aa491316308`.

The LZ4 result was reproduced offline from the verified kernel bytes with Python `lz4` 4.4.5. The five compressed block records are `(header offset, compressed bytes, decoded bytes)`: `(0x4,4581800,8388608)`, `(0x45e9b0,4641035,8388608)`, `(0x8cbabf,3931406,8388608)`, `(0xc8b7d1,3039711,8388608)`, and `(0xf719b4,350162,1878528)`. No decompressed file was added; the JSON evidence contains the exact algorithm inputs, offsets, sizes, and resulting hash.

## Relationship to memory reconciliation

`evidence/extracted/2026-09-14/memory-reconciliation.json` supplies the later live-DTB and initrd boundaries and already warns that LK mblock ranges describe stage-specific lifetime. Its kernel, DTB, and initrd reservations agree exactly with the lines above. The post-boot Linux map cannot reconstruct the missing entry EL, MMU/cache state, actual LK branch target, or the custom-payload loader behavior.
