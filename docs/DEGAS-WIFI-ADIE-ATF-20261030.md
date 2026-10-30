# MT6897 A-die identification test

Hardware status: v3 passed on the phone; see the result below. This advances the
previous cold RF/CONN power test to native secure A-die configuration and an
independent RF SPI ID read. No firmware upload, WFSYS power, AFE calibration
operations 14/15/16, DMA rings or network association are included.

## Evidence from this phone

`evidence/raw/2026-10-30-degas-native-conn-abi` contains one reversible Android
radio cycle captured with temporary module kprobes in a private trace instance.
Wi-Fi/Bluetooth were initially enabled, temporarily disabled, and restored.
Both settings were read back as 1 afterward; our probes/instance were removed.
The original trace instances were untouched. No partitions were changed.

The trace captures:

- Cold native `consys_hw_pwr_on(current=0,driver=0,cfg=0)`.
- Co-clock getter returns 1, and init-ATF wrapper observes config 0.
- SPI-master setup `(current=0,next=1)` returns 0.
- A-die setup with the same arguments returns 0 and stores `0x66378a01` in
  `conn_hw_env`.
- Later WLAN power requests driver 3 with current mask 1, giving next mask 9.

This cold-first shared initialization is not WLAN firmware startup. In this
capture the first subsystem was BT. The exact module's direct implementation
branches on current status zero before A-die initialization; the next mask is
not a substitute for a standalone subsystem start request.

`tools/trace_degas_conn_abi.py` reproduces the trace. Kprobe syntax reference:
https://docs.kernel.org/trace/kprobetrace.html . Its recording instructions
come from the installed module symbols, not a guessed syscall ABI.

Exact binary disassembly:
`evidence/raw/2026-10-30-degas-wifi-power/secure-init-wrappers.txt` and
`adie-next-sequence.txt`. These establish FID `0xc2000534`, low32 integer
status in x0 and ID/read data in x1. Module SHA256 remains
`9bf6b33149a961706a64188c0a50f8076638b797bcf64759ae227c92f8c0b324`.

## New isolated UEFI sequence

1. Keep the cold/off CONN guard; never force an inherited on domain off.
2. Op 0 with config 0 and clock enum 1, then existing RF prepare and CONN
   power/pin sequence. All SMC x4..x7 are zero.
3. Only after the hardware-read CONN ID matches `0x02050400` and host CSR is
   readable: op 10 poll, op 70 EMI remap (zero address arguments), op 11 D-die
   setup, op 12 SPI setup `(0,1)`, op 13 A-die setup `(0,1)`.
4. Require returned full ID `0x66378a01`. Op 13 is active native analog
   configuration and includes efuse **reads** in the reference path; it must
   not be described as a passive ID-only observation.
5. Enable the A-die top clock with op 90, read op 20 `(subsystem=5,address=0x2c)`,
   require matching ID, then attempt clock-off even if clock-on/read failed.
6. Pass every callback return through existing CONN off/protection/pin cleanup
   and RF field restoration. Reset after any SMC attempt, including init-only
   failure; do not continue into Windows in the same UEFI session.

The secure firmware remains the installed stock implementation. No GZ/ATF/LK
or preloader partition is replaced. This is nevertheless a new runtime secure
call path requiring a hardware result; native Android success alone does not
prove UEFI success.

## Verification

ASan/UBSan host tests pass for actual `adie_atf_core.h`: exact operation/argument
order, configuration ID and independent SPI ID, wrong-ID rejection, failure at
every operation, transport failure and required clock-off attempts. Extended
CONN tests verify callback failure still restores power and pins.

Expected screen: `ADIE RF SPI ID PASS`, with both IDs `66378A01`.
Any failure logs the SMC operation and raw x0..x3 before the existing reset.
After the reset, Android must boot through the qualified EFI memory view.

## v1 hardware result and precise OP 0 correction

`evidence/raw/2026-10-30-degas-adie-atf-v1-result` records init OP 0 returning
`x0=0x00000000ffffffff`. The test stopped before any RF/CONN writes, restored
flags remained true, and Android subsequently completed boot.

The stock ATF extracted earlier was compared byte-for-byte by hash with the
current `tee_b` ATF payload: both SHA256
`ef838aa754b367a0cfef4d18272d812895bb28aa55525b6a672245f223ca4935`.
Evidence is under `evidence/extracted/2026-10-30-conn-smc`.
The SIP descriptor at physical-link address `0x48868108` contains IDs
`0x82000534/0xc2000534`, with handler `0x4880de68`.
Its opcode jump table at `0x48859752` selects `0x4880deec` for OP 0.
That path initializes register tables, applies config when in range, then
checks clock enum <=3. For the valid enum it executes `mov w0,#-1` at
`0x4880df3c`, stores the clock at `0x4880df40`, and branches to the return
epilogue. This explains the native Linux caller ignoring this return.

v2 accepts low32 `0xffffffff` specifically from OP 0, with the fixed captured
arguments config 0 / clock 1. Other return values and all other operations
retain strict error checking. Host tests explicitly verify OP 0's -1 path
can progress and -1 from later operations still aborts. No ATF code is patched.

## v2 hardware result: A-die ID reached, OP 90 return mismatch

`evidence/raw/2026-10-30-degas-adie-atf-v2-result` records successful ops
10/70/11/12/13, with OP13 returning `x0=0,x1=0x66378a01`.
Clock on/off OP90 both returned low32 `0xffffffff`, so v2 did not issue OP20.
CONN, RF and pin cleanup completed (`restored=1`), and Android completed boot.

The exact stock handler's jump table selects `0x4880e1c0` for OP90. Valid
boolean x2 branches to `0x4880e324`, normalizes the boolean, calls
`0x4880f050` (which branches to the actual clock control at `0x4880f058`),
then unconditionally branches to `mov w0,#-1` at `0x4880e2f0`.
The clock function writes the control register and polls the corresponding
status, but the dispatcher does not forward its result. Thus this raw -1
alone cannot diagnose clock failure.

v3 treats only OP0 and OP90 as the verified side-effect-then-minus-one cases.
OP20 retains its real returned SPI status and must produce the matching ID.
Clock-off SMC completion is not claimed as an independent physical clock-off
measurement; existing power-off/readback cleanup plus reset remain required.
Host regression tests accept known -1 on these two opcodes and reject -1 on
the remaining status-returning calls, with cleanup attempts on read failure.

## v3 hardware result: PASS

Image SHA256 `202b99cbb28993350c990f6ecce9ca0d4a29744f35584663803000416c752284`.
Evidence: `evidence/raw/2026-10-30-degas-adie-atf-v3-result`.
OP13 returns `x0=0,x1=0x66378a01`; independent OP20 returns exactly the same
status and ID. OP90 on/off has the binary-qualified -1 behavior.
The final result is `ADIE result=0 ... id=66378a01 spi_id=66378a01 clock_off_failed=0`.
RF/CONN cleanup reports `restored=1`, `pins_restored=1`, CONN code 0.
The retained console includes subsequent Android EFI EBS, and the later
`runtime-confirmation.txt` verifies `sys.boot_completed=1` and EFI present.
The first capture happened before boot_completed was set; do not interpret
its blank initial property as a failed boot. The current captured dmesg has
no earlier kernel paging/Oops/panic signature.

This proves native A-die initialization and a new RF-SPI read in UEFI. It does
not prove WLAN MCU startup, firmware execution, association or a Windows WLAN
driver. v3 is the local recovery/reference point for the next stage.
