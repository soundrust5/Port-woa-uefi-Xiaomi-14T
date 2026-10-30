# Xiaomi 14T — project status

[Home](../../README.md) | **English** | [Русский](../Russian/status.md)

Device: **Xiaomi 14T / degas / MediaTek MT6897**. Updated: **30 October 2026**.

The maintainer estimates that AI assisted around 70% of development.

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

Windows has eight working cores, frequency scaling and boost, 10.4 GB RAM, multitouch and framebuffer display output. UFS read/write works but remains slow.

Android boots directly from UEFI. Radio initialization tests pass; Wi-Fi firmware startup, network connection and a Windows Wi-Fi driver are still unfinished.

Temperature readings and software throttling work. Long-term thermal protection remains unverified; sleep/resume is unavailable.

[Install UEFI](installation.md)
