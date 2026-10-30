# Xiaomi 14T — project status

[Home](../../README.md) | **English** | [Русский](../Russian/status.md)

Device: **Xiaomi 14T / degas / MediaTek MT6897**. Updated: **30 October 2026**.

**AI-assisted project:** the maintainer estimates that around **70% of development** was completed with AI assistance. This is an estimate; hardware functionality is verified on the physical device.

✅ Working · 🟡 Partial · 🚧 In development · ❌ Not implemented · ❔ Not verified

| Component | Status |
| --- | :---: |
| UEFI | ✅ |
| Windows boot from UFS (WinPE) | ✅ |
| Full Windows installation | ✅ |
| CPU | ✅ |
| All CPU cores / SMP | ✅ |
| Display | ✅ |
| GPU acceleration | ❌ |
| Touchscreen | ✅ |
| Internal storage — UFS | 🟡 (slow) |
| Volume buttons | ✅ UEFI and Windows |
| Power button | ✅ UEFI; 5-second hold opens shutdown confirmation in Windows |
| USB host | ❌ |
| USB device | ❌ |
| USB Power Delivery | ❔ |
| Charging control | ❔ |
| Battery reporting | ❔ |
| Wi-Fi | 🚧 |
| Bluetooth | 🚧 |
| NFC | ❌ |
| Mobile data / cellular modem | ❌ |
| Calls / SMS | ❌ |
| GNSS / GPS | ❌ |
| Speakers | ❌ |
| Microphones | ❌ |
| Bluetooth audio | 🚧 |
| Rear cameras | ❌ |
| Front camera | ❌ |
| Fingerprint reader | ❌ |
| Vibration motor | ❌ |
| Accelerometer | ❌ |
| Gyroscope | ❌ |
| Compass / magnetometer | ❌ |
| Ambient light sensor | ❌ |
| Proximity sensor | ❌ |
| Temperature sensors / software thermal throttling | ✅ |
| Automatic brightness | ❌ |
| Sleep / resume | ❌ Unavailable; not tested |
| TPM | ❌ |

All eight CPU cores, dynamic frequency scaling and boost have been tested in Windows. Touchscreen input, including multitouch and the screen edges, is reported working on the test device. Display output uses the framebuffer.

UFS reading and writing passed in Windows PE, including a stress test. Unverified entries do not imply working Windows support.

Temperature readings and software frequency limiting have been verified; thresholds remain conservative. Long-term thermal protection remains unverified.

Android now boots directly through its stock EFI stub, including after the qualified RF/CONN cleanup and reset. Native A-die initialization and an independent RF-SPI read both returned ID `66378A01`. Wi-Fi firmware startup, network association and a Windows Wi-Fi driver remain unfinished. The tested Windows configuration retains eight cores, DVFS, 10.4 GB RAM, UFS, touch and TE display refresh.

[Installation availability](installation.md)
