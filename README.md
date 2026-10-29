<img align="right" src="assets/xiaomi-14t.png" width="300" alt="Xiaomi 14T">

# Windows 11 ARM64 & UEFI on Xiaomi 14T

**English** | [Русский](README.ru-RU.md)

Experimental Windows on ARM (WoA) port for **Xiaomi 14T (`degas`) — MediaTek Dimensity 8300-Ultra (MT6897)**.

Windows 11 PRO boots from UFS. CPU, display, touchscreen with multitouch, and UFS read/write work. Hardware buttons work in UEFI only.

> [!IMPORTANT]
> Public installation is not available yet.

- [Project status](guide/English/status.md)
- [Installation](guide/English/installation.md)
- [Mu-Silicium source](mu/) · [Source details & licenses](SOURCE.md)

Maintainer: [soundrust5](https://github.com/soundrust5).

<br clear="both">


## Current sources and experimental release — 29 October 2026

Current UEFI, Windows UFS/touch driver and CPU/DVFS research sources, build scripts, tests and reports are published. [Source contents and dependency reconstruction](SOURCE.md) · [UEFI releases](../../releases).

Earlier device testing confirmed Windows with all eight CPU cores, UFS read/write, multitouch and autonomous framebuffer refresh. DVFS is unfinished; GPU acceleration, Wi-Fi and Bluetooth are not implemented. The new fullscreen-default image passed build and packaging checks; this exact binary has not yet been booted on the phone. Windows images and proprietary driver binaries are not included.
