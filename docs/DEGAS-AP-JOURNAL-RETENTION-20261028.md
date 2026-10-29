# Retained AP journal: hardware qualification

The user observed `RAM JOURNAL RETAINED` after the explicitly selected RAM test
and its automatic watchdog reset. The firmware compared all 4096 bytes against
the original fixture. No Windows boot or AP startup was involved.

The user then selected END TEST, entered fastboot after the automatic reset,
and the host restored the verified stock boot_b image and started Android.
The captured `dmesg-ramoops-30` is exactly 2021 bytes and equals fixture bytes
27..2047. Header CRC, UUID and identity hash all validate; all seven AP slots
remain uncommitted, as expected for a transport-only test.

Evidence: `evidence/raw/2026-10-28-degas-aplog-retention-result/`.
Fixture: `artifacts/degas/aplog-retention-v1/zone.bin`.
Attempt: `42ea4bc0-5224-4953-b7dc-28303baffec8`.
Image SHA256: `37527e033b0fb512b73c1de47ce0617cb110d0ba357c80da60ac230564239c49`.

This proves this slot survives the tested watchdog/UEFI/fastboot/Android path.
It does not prove survival of power loss or long-key hard reset, nor successful
AP writes while Windows starts. It is a small pre-handoff journal, not a kernel
crash dump or a captured Windows bugcheck callback.

Next diagnostic integration preserves a separate immutable slot per AP before
branching to Windows. A diagnostic-only build option rearms the existing
watchdog for 30 seconds at ExitBootServices; normal builds retain watchdog-off
behavior. The next UEFI menu validates and copies any previous journal into the
independent BSP console without clearing it. A fresh source-manifest identity
and retained attempt sequence distinguish captures. These changes do not claim
to fix the underlying CPU initialization failure.
