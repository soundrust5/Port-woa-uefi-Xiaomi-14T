# Parked-CPU bugcheck reader, 2026-10-29

Purpose: obtain Windows's actual stop code and four parameters automatically.
This is diagnostic instrumentation, not a fix for the AP initialization fault.

The preceding two-CPU test verified the running NT build by entry instruction,
DOS/PE headers and full CodeView GUID/age, but still failed with user-reported
HAL5C. Its AP record survived even though the Android menu route failed. Stock
boot_b was restored, Android ADB recovered, and the fresh record was decoded.
No failure of the Android kernel or loss of the Android flag was established.

## Configuration and ownership

```
DEGAS_BOOT_CORE=0
DEGAS_CPU_TEST_LIMIT=3
DEGAS_CPU_PARKING_EXPERIMENT=1
DEGAS_WINDOWS_IDENTITY_OBSERVER=1
DEGAS_WINDOWS_BUGCHECK_MONITOR=1
DEGAS_DIAGNOSTIC_EBS_WATCHDOG=1
```

All three CPUs are advertised in MADT; CPU2 is not hidden from Windows.
Only while still parked, CPU2 runs a bounded observer. It aborts without a
monitor commit if Windows requests CPU2 release, restores its architectural
state, and resumes the ordinary Parking handshake. Other CPUs remain off.
The working single-CPU7 fullscreen image remains preserved separately.

AP1 captures the same runtime identity. Its fresh per-attempt retained commit
is the source-publication handshake. CPU2 validates all six identity qwords
in AP1's firmware-private memory, obtains Windows TTBR1/TCR from that AP's
OS-populated startup mailbox, and uses the existing private identity map.
Only verified whitelisted RAM is mapped WB RO/Inner Shareable; private code
is NC RX and private mailboxes/journal Device RW XN. This diagnostic compiles
the software walker WITHOUT CIVAC, including AP1's identity reads. The earlier
hardware coherency control observed new BSP data before the post-read CIVAC.
There are no Windows memory writes or live Windows cache-maintenance loops.

## Publication discipline

For the verified NT build, globals are:

- KiBugCheckData: RVA DBB9A0, five U64 values;
- KiBugCheckDataInitialized: C57000, U32;
- KiBugCheckActive: D9796C, U32 (translated via the aligned qword D97968).

Initialized=1 is written BEFORE the tuple. It is not a completion flag.
The monitor acquires flags on their coherent physical aliases, brackets
two complete tuple reads separated by at least 1ms with unchanged flags,
and requires the same tuple/flags across a 10ms settling interval. Zero
parameters are valid; any nonzero stop code is accepted. Concurrent bugchecks
can split the flag and tuple writers, so this is repeated stable observation,
not a formal atomic snapshot under arbitrary concurrent failure.

The monitor deadline is 60 seconds from its entry, measured with CNTVCT at
13MHz. The existing watchdog is armed for 30 seconds after EBS and may reset
first. Timeout/read failure is inconclusive. No private exception vectors
are installed; unexpected synchronous faults rely on that watchdog.

## Retained record

CPU2 owns journal slot2 only. The private context status high32 is tag BCG1
(42434731), explicitly decoded as bugcheck_monitor_v1, not a Windows AP
release. Context word0 is the validated NT base and word1 source LPB.

Snapshot E00 is status (0 stable, 1 timeout, 2 identity mismatch, 3 read failure
with failing-read stage in high32). E08 packs Active in high32 and Initialized
in low32. E10..E30 contain code and four parameters. E38 is CNTVCT at commit.
The later ordinary Parking writer skips an already committed slot, preserving
the monitor's immutable record while still allowing normal Windows release.

## Validation before hardware

Actual monitor/walker/journal instructions pass emulator scenarios for partial
publication, zero parameters, tuple changes/settling, timeout, identity/read
failures, OS-release abort and architectural guards. The tests check register
and system-register restoration, private-write boundaries, no CMO, exact CRC
and explicit decoder payload type. Existing observer and identity tests pass.
Actual PrepareCpuMadt passes host ASan/UBSan including limit3. Full EFI build
and exact linked-byte checks are required before packaging/flashing.

For collection after Windows: allow the automatic reset, choose END TEST,
then hold Volume Down at its reset to enter fastboot. Restore stock boot_b
and capture Android pstore. Avoid relying on BOOT ANDROID for this test because
its post-Windows route failure has not been explained.

## Built and flashed

`artifacts/degas-bugcheck-monitor-v1/boot.img`, SHA256
`f473bc9770bcc7f7e05b804f843d2d3bedbd93220f0379558bffaede06de25f7`.
Copied observer2440 bytes and Parking640 bytes exactly match the emulator
fixtures inside the linked EFI. Source manifest
`cf5be4caf31f26ecb44e4bc867f9eddc75d5a949ae63e348cf819bf5d51d4123`.
Build successful; stock AVB tail preserved, degas/product and active slot b
checked. Flash boot_b and reboot succeeded. Awaiting hardware Windows action.

## First hardware monitor result

Hardware monitoring SUCCEEDED. Fresh evidence:
`evidence/raw/2026-10-29-degas-bugcheck-monitor-v1-result/decoded.json`.
AP1 runtime identity and AP2 monitor CRC/metadata/source-manifest validated.
Attempt `11947a23-0c1b-401d-8000-000000000001`.
Runtime NT base `FFFFF8006B000000`.

Stable tuple: **5C / 203 / 4 / FFFFF79C40001860 / D**.
Initialized=1, Active=3, consistent with logical CPU0 as bugcheck owner.
The first parameter is 203, not the earlier manually transcribed 23.
No queue field was resampled in this attempt. Stock boot_b restored and
Android ADB recovered; no Windows/GPT changes.

203 has multiple matching sites. A8D7AC (HalpInterruptInitSystem phase15)
reports deferred HalpInterruptLastProblem; D there is processor-start failure.
Other interrupt paths report unrecognized vector D with the same tuple.
The tuple does not establish a unique callsite or a specific GIC register bug.

## v2 detail extension

After the stable tuple, five bounded aligned reads obtain:
E051E8 (LastProblem upper32), E051F8 (Status low32), E05218 (Controller),
E05228 (File pointer), E05230 (Line upper32). Private scratch E40..60
holds these values; the second temporary tuple moved to EB0..ED0 to avoid
overlap. Stages13..17 identify detail-read failures.

BCG2 tag42434732 context is NTbase, LastProblemController, LastProblemFile,
LastProblemLine, tagged status. Telemetry F38 packs Problem upper32 and
Status low32. Primary tuple/flags/timestamp remain unchanged. The offline
decoder validates AP1's same-attempt build/base before resolving an ASCII
source-file pointer against the saved NT image. Failed detail reads do not
justify a source/cause interpretation.

Extended actual-instruction emulator tests pass for Start.c/negative status,
entry.c/unexpected interrupt and each detail-read failure, retaining previous
publication/ownership/state checks. Awaiting the v2 hardware attempt.

v2 built, linked observer2824bytes/Parking640bytes exactly matched tested
fixtures; 12 monitor tests passed. Image
`artifacts/degas-bugcheck-monitor-v2/boot.img`, SHA256
`c967c9459b5e16c5487447cd17bd620f6f55ff8be0d4be6ea64f24c87b38a3da`.
Source identity
`50100dbdc411d9c46ed7e94687adc90631a8566504db6ee943fc3fdf6abbdec3`,
seed `26df86da-b228-4a71-b0ad-63e5bc0f1388`. Stock AVB tail preserved;
product degas/slot b checked; flash boot_b and reboot succeeded.

v2 hardware SUCCEEDED: validated BCG2 record captured problemD/C0000001,
controller matching stopP3, source start.c1386. This disambiguates processor
start failure from the alternative unexpected SGI13 path. Stock Android
restored and source report saved under
`evidence/raw/2026-10-29-degas-bugcheck-monitor-v2-result/bugcheck-source.json`.
See DEGAS-HAL-ROOT-CAUSE-20261029.md for the confirmed FADT/fallback chain and
the separate GZ context-loss finding. No eight-core fix has been established.
