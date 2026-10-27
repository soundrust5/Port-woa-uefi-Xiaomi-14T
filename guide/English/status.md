# Xiaomi 14T — project status

[Home](../../README.md) | **English** | [Русский](../Russian/status.md)

Device: **Xiaomi 14T / degas / MediaTek MT6897**. Updated: **27 October 2026**.

**AI-assisted project:** the maintainer estimates that around **90% of development** was completed with AI assistance. This is an estimate; hardware functionality is verified on the physical device.

✅ Working · 🟡 Partial · 🚧 In development · ❌ Not implemented · ❔ Not verified

| Component | Status |
| --- | :---: |
| UEFI | ✅ |
| Windows PE boot | ✅ |
| Full Windows installation | 🚧 |
| CPU | ✅ |
| All CPU cores / SMP | ❔ |
| Display | ✅ |
| GPU acceleration | ❌ |
| Touchscreen | ✅ |
| Multitouch | 🚧 |
| Internal storage — UFS | ✅ |
| Booting full Windows from UFS | 🚧 |
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

CPU status confirms the boot CPU and ARM64 execution. Touchscreen status confirms single-finger Windows input, including the edges. Display output uses the framebuffer. Buttons currently work in UEFI only.

UFS reading and writing passed in Windows PE, including a 256 MiB stress test. Write qualification currently covers a dedicated test area; unrestricted writes and reliable full Windows boot are still being prepared. Unverified entries do not imply working Windows support.

[Installation availability](installation.md)
