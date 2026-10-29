# Xiaomi 14T — project status

[Home](../../README.md) | **English** | [Русский](../Russian/status.md)

Device: **Xiaomi 14T / degas / MediaTek MT6897**. Updated: **29 October 2026**.

**AI-assisted project:** the maintainer estimates that around **90% of development** was completed with AI assistance. This is an estimate; hardware functionality is verified on the physical device.

✅ Working · 🟡 Partial · 🚧 In development · ❌ Not implemented · ❔ Not verified

| Component | Status |
| --- | :---: |
| UEFI | ✅ |
| Windows boot from UFS (WinPE) | ✅ |
| Full Windows installation | ✅ |
| CPU | 🟡 |
| All CPU cores / SMP | ✅ 8 cores; DVFS unfinished |
| Display | ✅ |
| GPU acceleration | ❌ |
| Touchscreen | ✅ |
| Internal storage — UFS | ✅ |
| Volume buttons | 🟡 UEFI only |
| Power button | 🟡 UEFI only |
| USB host | ❌ |
| USB device | ❌ |
| USB Power Delivery | ❔ |
| Charging control | ❔ |
| Battery reporting | ❔ |
| Wi-Fi | ❌ |
| Bluetooth | ❌ |
| NFC | ❌ |
| Mobile data / cellular modem | ❌ |
| Calls / SMS | ❌ |
| GNSS / GPS | ❌ |
| Speakers | ❌ |
| Microphones | ❌ |
| Bluetooth audio | ❌ |
| Rear cameras | ❌ |
| Front camera | ❌ |
| Fingerprint reader | ❌ |
| Vibration motor | ❌ |
| Accelerometer | ❌ |
| Gyroscope | ❌ |
| Compass / magnetometer | ❌ |
| Ambient light sensor | ❌ |
| Proximity sensor | ❌ |
| Temperature / thermal management | ❔ |
| Automatic brightness | ❌ |
| Sleep / resume | ❔ |
| TPM | ❌ |

CPU status confirms the boot CPU and ARM64 execution. Touchscreen input, including multitouch and the screen edges, is reported working on the test device. Display output uses the framebuffer. Buttons currently work in UEFI only.

UFS reading and writing passed in Windows PE, including a stress test. Unverified entries do not imply working Windows support.

[Installation availability](installation.md)


2026-10-29 source refresh: all eight CPU cores were observed working in Windows. Clock-frequency scaling/DVFS is still under investigation. See [current source notes](../../SOURCE.md) for the build-qualified fullscreen prerelease and remaining limitations.
