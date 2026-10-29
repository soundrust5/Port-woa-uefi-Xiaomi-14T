# Paused at user's request, 2026-10-28

No further flashing/reboots/experiments authorized until the user resumes.
Phone last verified in stock Android with ADB. Stock boot_b restored from
evidence/raw/2026-10-24-degas-return/boot_b.img. No NTFS/ESP mounts left by
the latest capture. Agents are interrupted. No active shell sessions pending.

Latest Windows test image:
artifacts/degas-windows-coherent-dualboot-v1/boot.img
SHA256 4b184a94d0633e01f0e6eebbfeccd0e8da273a6f7afb8b9a0cb3905d9af82aa0.
User saw HAL, one green / six white. 30-second EBS watchdog configured.

Fresh retained journal successfully decoded in:
evidence/raw/2026-10-28-degas-windows-coherent-result/decoded.json.
Attempt52a4d595-7377-4bc9-8000-000000000001, source hash verified.
AP1 coherent reader all four walks succeeded, status0:
TPIDR FFFFA000F0881000; LPB.PRCB FFFFA000F0881980 (TPIDR+980);
processor Number1; LocalSharedReadyQueue(+8300)=0;
SharedReadyQueue(+1908)=0; PC FFFFF80267091BA0;
LPB FFFFF8025C8595A0; TTBR1 004000004052A800.
AP2..7 uncommitted. This is pre-entry state, not yet proof of bugcheck cause.

Hardware qualification already passed private-MMU cache control A/A/B/B,
CPU1 OFF, status0, phase4/transition4. Original MMU-off physical reader was
proven blind to BSP dirty WB data. Do not reuse old BSP-looking PRCB readings
as evidence of actual incorrect Windows startup context.

Next priorities, with no speculative kernel NULL-pointer patch:
1. Explain missing queue allocation before AP1 release. Normal
   KeStartAllProcessors B41EB4 allocates +8300 before HalStartNextProcessor
   B41EF8, but a second caller exists in HalInitSystem A8C5A8, using a 6D0-byte
   state array. Determine whether this is an earlier legitimate startup phase.
2. Verify actual runtime kernel identity (PE/RSDS/LPB module list), not only
   installed-file SHA. Exact reported HAL tuple23/4/pointer/D still unmatched
   in audited kernel; check active ESP BCD before assuming loaded kernel.
3. Audit global physical startup stub publication/cache maintenance. Coherent
   LPB reads now differ from old physical reads; code/literals fetched with
   M/C/Ioff could deserve a similarly bounded check. No fix established.

BOOT ANDROID composite route worked after UEFI-only tests, but user reported
returning to UEFI after the Windows attempt. Latest captured console contains
one wrapper entry and Windows EBS tail, no Android-request marker or second
wrapper entry. Do not claim lost flag or Android kernel panic yet. Stock
Android boot works. Agent audit found no overlap of AP journal and flagF00.

Console wrapper now preserves valid ring on re-entry (ramoops_probe.c), source
and host tests verified. Latest wrapper build ID3c9a9a3bcfbd62d31ede799a94b4a6830d42571b301a0281581fa167fe6ebfa7.
AP journal is an independent Linux-compatible record at480AE000. Android flag
480AEF00 is outside published journal payload. Always use composite packaging
for a firmware exposing BOOT ANDROID; intermediate UEFI-only image is not final.

Windows observer: separate NVS NC executable page, immutable private tables
map exact whitelist WB/RO and mail/journal Device. Restores AP architectural
state before Windows. Targeted post-load CIVAC intentionally publishes read
lines to PoC and can affect behavior. Tests and linked-byte checks passed.
Normal Parking592bytes, observer1000bytes. Source identity manifest includes
new observer and mapper. Do not claim this is a production SMP fix.
