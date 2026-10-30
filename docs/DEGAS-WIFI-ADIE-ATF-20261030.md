# Wi-Fi A-die identification in UEFI

The cold power/identification test passes. Native configuration and a separate
RF-SPI read both return `0x66378a01`. WLAN firmware and network access are not
running yet.

## Native Android reference

The installed `conninfra.ko` uses SMC FID `0xc2000534`. A temporary Android
trace recorded platform config0, clock enum1 (52 MHz co-clock), and cold SPI /
A-die setup with `(current=0,next=1)`. The first native subsystem in that trace
was BT; this is shared CONNINFRA initialization, not BT firmware startup.
The subsequent WLAN request used driver3 with current mask1 and next mask9.

The trace is in `evidence/raw/2026-10-30-degas-native-conn-abi`. Temporary
probes were removed and the original Wi-Fi/Bluetooth states restored.

## Test sequence

1. Require CONN off; reject an inherited on state before changing power.
2. Init OP0 `(config=0,clock=1)`, then RF and CONN power/pin setup.
3. Verify CONN IP `0x02050400` and readable host CSR.
4. OP10 poll, OP70 EMI remap, OP11 D-die, OP12 SPI and OP13 A-die setup.
   Ops12/13 use `(0,1)`; OP70 takes no address arguments.
5. Require the full returned ID `0x66378a01`.
6. OP90 clock on, OP20 `(subsystem=5,address=0x2c)`, matching ID, clock off.
7. Restore CONN protection/power, pinmux and RF fields, then reset.

OP13 performs native analog configuration and efuse reads. It is an active
initialization step. No firmware upload, WFSYS power or command DMA is included.

## Stock ATF return values

The current `tee_b` payload matches ATF SHA256
`ef838aa754b367a0cfef4d18272d812895bb28aa55525b6a672245f223ca4935`.
Its handler is `0x4880de68`, opcode table `0x48859752`.

- OP0 initializes tables/config, stores the clock enum and returns `w0=-1`.
- OP90 calls clock control and then unconditionally returns `w0=-1`.
- OP20 returns the real SPI status and read data in x1.

Only the two verified side-effect opcodes accept low32 `0xffffffff`.
Other errors abort. Clock-off completion is not a separate physical clock
measurement; parent-domain power-off/readback and reset remain required.

## Hardware results

| Version | Result |
| --- | --- |
| v1 | Stopped on OP0 return before RF power changes |
| v2 | OP13 returned `66378A01`; stopped on OP90 return before OP20 |
| v3 | OP13 and OP20 both returned status0 / ID `66378A01`; cleanup and Android boot passed |

Tested image SHA256:
`202b99cbb28993350c990f6ecce9ca0d4a29744f35584663803000416c752284`.
Capture: `evidence/raw/2026-10-30-degas-adie-atf-v3-result`.
Portable tests cover call order, arguments, mismatched IDs, failures and cleanup.

Sources: `diagnostic/degas/wifi/adie_atf_core.h`, `conn_power_core.h`,
`CheckpointDxe/WifiAdieAtf.h` and `WifiRfPowerProbe.h`.
