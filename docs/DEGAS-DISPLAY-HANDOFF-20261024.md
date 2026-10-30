# Display and Windows boot bring-up

The first WinPE installer screen appeared on 27 October 2026 with V23.
The current display implementation also supports installed Windows; see
[component status](../guide/English/status.md).

## Resolved failures

| Stage | Failure | Correction |
| --- | --- | --- |
| Early framebuffer | MI remained on screen | Enable both display-processing pairs before the DSI transfer |
| First DXE | Boot stopped before GOP | Initialize PCD early |
| GOP BLT | Colored rectangle appeared black | Configure opaque framebuffer output |
| Mu boot manager | Pre-Windows assertion | Include SetupBrowserDxe |
| EFI application load | File read succeeded but application would not start | Signal EndOfDxe before loading |
| Large boot reads | Slow loading | HS UFS negotiation and faster buffer copying |
| Windows handoff | Pixels changed in RAM but panel stayed frozen | Autonomous GCE refresh after ExitBootServices |
| Animation | Brief horizontal artifacts | TE-synchronized frame submission |

The panel uses DSI command mode. A RAM write alone does not submit a frame.
GOP BLT submits frames during UEFI; Windows needs the persistent GCE loop after
Boot Services end. A Boot Services timer cannot provide post-EBS refresh.

## GCE test results

- V15: owned DMA page mapping verified.
- V16: command execution verified.
- V17: visible single frame; completion handling needed correction.
- V18: a green frame appeared, but the BUSY assertion handshake timed out.
- V19: the CPU saw a fresh completion flag while GCE POLL remained blocked.
- V20: address-GPR substitution produced a valid fresh-frame handshake.
- V21: autonomous cyan/red/green/blue sequence completed.
- V22: Windows spinner animated after EBS.
- V23: disabling the diagnostic post-EBS watchdog allowed WinPE setup to open.

The first WinPE baseline was
`artifacts/degas-windows-no-wdt-v23/boot.img`, SHA256
`5351e16bef481c830ea9382dadba40549a71ec4c86313511a322573b375cf657`.
It is historical, not the current download.

## Implementation

- `mu/Platforms/Xiaomi/degasPkg/Drivers/DegasGopDxe/`
- `CheckpointDxe/GceWindows.h` and `GceFramePacket.h`
- `diagnostic/display_frame_v2.c`

Detailed bring-up captures are under `evidence/raw/2026-10-24-*` and
`2026-10-27-degas-gce-*` in the local research archive. Earlier revisions of
this document retain the full test chronology.

## Related notes

- [Panel and GCE](DEGAS-PANEL-AUDIT-20261026.md)
- [GCE address translation](DEGAS-GCE-FETCH-CONTROL-20261026.md)
- [Display handoff audit](DEGAS-DISPLAY-WATCHDOG-AUDIT-20261026.md)
- [Android EFI boot](DEGAS-ANDROID-EFI-BOOT-20261030.md)
