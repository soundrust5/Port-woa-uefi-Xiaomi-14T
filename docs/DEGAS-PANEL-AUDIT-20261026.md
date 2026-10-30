# Panel refresh and GCE

The Xiaomi 14T panel uses DSI command mode. The display keeps its last frame
until another transfer is submitted. This caused the original frozen Windows
logo even though Windows was updating framebuffer memory.

## Transfer path

The two OVL views start at `0xfcc2f000` and `0xfcc2f95c`, 599 pixels apart.
Each view is 621 pixels wide with a 22-pixel overlap. They feed one DSI link;
the captured DSI1 registers were zero.

A transfer enables mutex0 in both processing pairs:

1. Secondary blocks `0x14201000` and `0x14601000`.
2. Primary blocks `0x14001000` and `0x14401000`.
3. DSI0 command `0x002c3909` at `0x1400dd00`, queue control `0x8001` at
   `0x1400d060`, then DSI_START 0 -> 1.

The first test omitted the second pair and left MI displayed. Enabling both
pairs produced the marker. DSI INTSTA bit4 is FRAME_DONE; bit31 is BUSY.

## Refresh after ExitBootServices

GOP BLT and firmware timers stop being usable after EBS. The replacement is
an immutable GCE packet that waits for frame timing, submits the transfer and
loops. It must not depend on Android's software STREAM_DIRTY token.

Hardware tests progressed from a command sentinel to a single frame, a fresh
FRAME_DONE handshake, an autonomous color sequence and the animated Windows
spinner. TE synchronization later removed brief animation artifacts.
See [bring-up results](DEGAS-DISPLAY-HANDOFF-20261024.md).

Opaque framebuffer output is required because Windows may leave the reserved
byte of RGBX pixels zero. The implementation configures the OVL alpha path;
it does not repaint every Windows pixel in software.

## GCE memory ownership

GCE command memory, page tables and translation mappings must remain allocated
after EBS. Buffers cannot be freed while command fetching may continue.
The owned mapping is checked before command submission and kept separate from
UFS DMA, the boot wrapper and the stock Android kernel image.

The GZ A6 mapping query requires initialized NS and protected hardware table
VA fields (+0xb8/+0xa8). Return -3 means the tables are unavailable. It must
not trigger attempts to fabricate or dereference them. Mapping qualification
compares all 256 pages in the selected 1 MiB granule before and after setup,
plus the reserved CMDQ/framebuffer markers outside it.

For the single-boot mapping experiment, the expected result was one NS small
page and all other pages invalid in that granule. A failed query, collision or
unexpected neighboring mapping aborts command submission. The software-root
association is global; switching clients or roots requires separate review.

## Sources

- `DegasGopDxe/DegasGop.c`
- `CheckpointDxe/GceWindows.h`, `GceFramePacket.h`, `GceArena.h`
- `diagnostic/display_frame_v2.c`
- [GCE fetch and translation audit](DEGAS-GCE-FETCH-CONTROL-20261026.md)
- Local LK/ATF/GZ disassembly and Android display captures.

There is no verified panel video-mode sequence. GPU acceleration, display
power management and resume remain separate work.
