# Xiaomi 14T touchscreen

Touch works in UEFI and Windows; two simultaneous contacts were tested.
Sources: `diagnostic/degas/touch/` and its `windows/` subdirectory.

## Hardware

The active Android driver is FocalTech `focaltech_touch_rothko.ko`, not the
Goodix alternative found in DT. Controller ID is `0x3683`, CID type `0x92`;
the commercial part name is unconfirmed.

| Resource | Value |
| --- | --- |
| Host | SPI2 CS0, compatible `mediatek,mt6985-spi` |
| MMIO | `0x11012000/0x100` |
| SPI settings | Mode 0, maximum 15 MHz, tickdly 3, pad-select 0 |
| IRQ | GPIO9; host GIC INTID 276 |
| Reset / AVDD enable | GPIO60 / GPIO73 |
| IOVDD | MT6368 VTP, 1.8 V |
| Android coordinate range | X 0..121999, Y 0..271199 |
| Windows device / service | `DGS0002` / `DegasTouch` |

Linux global GPIO numbers and Android virtual IRQ56 are not SoC-local GPIO
indices or ACPI interrupt numbers.

## Protocol

`fts_protocol.h` implements the installed driver's framing:

- Request: address, `A0`, big-endian payload length and three dummy bytes.
- Payload begins at offset 7; total transaction length is payload + 9.
- Response byte3 must have mask `A0` clear.
- CRC16: reflected polynomial `0x8408`, seed `FFFF`, little-endian result,
  no final XOR.

Report type 2 uses the high nibble of payload[1], a contact count in its low
nibble, and records starting at offset4 with stride8. Duplicate IDs, unknown
packet types, truncated packets and CRC errors are rejected.

The FIFO engine preserves chip select across each transaction. GPIO CS control
was required to get a valid chip ID; a working SPI host alone was insufficient.
Power setup also requires the VTP rail, AVDD and reset sequence.

## Windows input

The KMDF driver runs bounded FIFO transactions at PASSIVE_LEVEL and submits
contacts through VHF. It validates the ACPI resources, chip ID and report mode.
Tests cover contact release/replacement, stale input, malformed packets and
failed HID submission. The WinPE canvas showed real Windows pointer input;
the two-finger test confirmed independent contacts and return to zero fingers.

The package uses a development test signature. Resume and interrupt-driven
input remain unqualified; the initial implementation uses polling and no DMA.

## Captures

- Android binding and gestures: `evidence/raw/2026-10-27-degas-touch/`.
- UEFI pin, power, ID and draw tests: the `2026-10-27-degas-touch-*` captures.
- Windows tests and signed packages: `artifacts/degas/touch-windows/`.

The active Android module SHA256 is
`2dd865855c329eaee6c246b701cc6004dee0d692bc232aebdfa9f312d1d53b00`.
