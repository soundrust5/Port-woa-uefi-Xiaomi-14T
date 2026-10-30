# Scoped RF power-control test — 30 October 2026

Exact hardware RF snapshot v5: all12 SID4 reads succeeded, Android menu boot
succeeded. Cold VCN13 selector1b3f=04 and trim1f88=05 match Android;
VRFIO18 trim1f2b=02 matches, selector1f2c=0c differs from Android0b.
Both enable/mode words1bcd/1d07=00, OP_CFG words1bd4/1d16=80.
HW_LP/op-enable words are zero in the cold baseline and configured later by
Android; they are observed but are not guessed or replayed in this test.

V6 private scope: reject a non-OFF CONN domain; snapshot5 writable RF words;
require both rails OFF/NORMAL and the exact device-qualified selector/trim
baseline. Set only VRFIO18 selector low nibble0b (nominal1.7V); retain both
factory calibration fields and VCN13 selector. Android requests1.35V for
VCN13; this probe preserves its observed selector/trim rather than inventing
a voltage encoding or claiming a measured physical voltage.
Then match common_power_ctrl: VRFIO18 OP_CFG bit7, enable bit0, wait180us
from live DT, NORMAL(bit1=0); VCN13 OP_CFG bit7, enable bit0, wait180us,
NORMAL. Validate owned readback fields. All other fields remain unchanged.

Only SID4 five-address PMIC write allowlist:1f2c low nibble;1bd4/1d16 bit7;
1bcd/1d07 bits0..1. No calibration registers, buck/global PMIC unlock,
secure calls, RF SPI, firmware, DMA, GPIO output or persistent configuration.
Primitive uses the already qualified EXT_WRITE_LONG PMIF pattern with SID4,
idle ownership checks, readback and finite timeouts. It does not acknowledge
or overwrite somebody else's busy host transaction.

After RF control setup run the hardware-qualified pinmux+SCPSYS CONN IP
and ownership probe. Its cleanup must finish OFF AND PROTECTED with pinmux
restored. Independently reread CTL and require its OFF state before restoring
RF fields. Disable VCN13 and restore OP_CFG, disable VRFIO18 and restore its
OP_CFG, then return the disabled VRFIO18 selector to its original value.
Each owned field is read back. Any failure prevents direct OS boot, and the
mandatory10s watchdog reset takes over. Every owned test resets, including
success. GZ/ATF/LK and installed Windows are untouched.

Pass label RF POWER CTRL AND CONN PASS establishes enable/mode/selector
readback plus digital CONN IP/host CSR access. It does NOT establish physical
rail voltage, MT6637 A-die communication, running WLAN firmware or networking.
GCC/Clang ASan/UBSan tests passed: scoped command encoding, read/write FSM,
no writes to calibration or keys, ordering, both180us ramps, unknown baseline
and already-enabled rejection, each failed write, restoration failure, PMIF
read errors, full original-register comparison and unrelated-bit preservation.

Android route observer remains enabled. V5 was a successful boot, so the
intermittent failure is not declared fixed. If a failed attempt returns to
UEFI, keep that menu open for the next recovery/log collection instruction.

Firmware prerequisite finding: exact installed soc7_0_McuInit has no calls
to kalFirmwareImageMapping/wlanImageSectionDownloadStage used by reference
soc7_0_SetupRomEmi; the source guards that call with CFG_WLAN_LK_FWDL_SUPPORT.
This supports LK ROM/EMI preloading. It does not prove a particular firmware
payload remains present or authenticated in UEFI. Check reserved EMI content
and remapping before copying payloads or releasing the WLAN MCU.


V6 hardware PASS: RF0/CONN0/IP02050400, both rail enable/NORMAL controls
read back01, VRFIO18 selector0b. RF restore returned1d07=00,1bcd=00,
OP_CFG80/80 and selector0c; CONN/pinmux cleanup also passed. Full result
retained under evidence/raw/2026-10-30-degas-rf-power-v6-result.
The user selected Android after the automatic reset, returned to UEFI, then
intentionally reran RF to preserve that result. Therefore this console is
from the SECOND RF test, not an uninterrupted trace of the failed Android
attempt; absence of its request line must not be interpreted as user error.
