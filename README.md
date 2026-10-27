# UEFI & Windows on Xiaomi 14T

**English** | [Русский](README.ru-RU.md)

Experimental UEFI and Windows on ARM port for **Xiaomi 14T (`degas`)**, powered by **MediaTek Dimensity 8300-Ultra (MT6897)**.

> [!IMPORTANT]
> **Installation is not available yet.** The port is under development. There is no public installation procedure or ready-to-install release at this stage.

## Current progress

**Windows boots.** Windows PE has reached the graphical Windows installer on the test device. CPU execution, display output, touchscreen input and UFS access have been demonstrated.

| Component | Status | Notes |
| --- | --- | --- |
| Windows boot | ✅ Working | Windows PE boots and displays the graphical installer. Full Windows installation is being prepared. |
| CPU | ✅ Working | ARM64 execution and the boot CPU are working. This does not yet confirm all-core or power-management support. |
| Display | ✅ Working | UEFI and Windows framebuffer output, including animated updates. |
| Touchscreen | ✅ Working | Touch input and correct coordinates, including the edges, verified in Windows. Multitouch is being developed. |
| UFS storage | ✅ Working | Reading and writing verified in Windows PE, including a 256 MiB stress test and readback. Full Windows boot support is being prepared. |
| Hardware buttons | 🟡 Partial | Volume and Power buttons work in UEFI. Windows button support is not implemented yet. |

The status describes development-device results, not a finished daily-use port. See the [full status page](guide/English/status.md) for the remaining components and limitations.

## Get started

- [📊 Project status](guide/English/status.md)
- [📖 Installation availability](guide/English/installation.md)
- [🇷🇺 Русская версия](README.ru-RU.md)

## Project

Maintained by [soundrust5](https://github.com/soundrust5).

This repository currently presents the project's progress and availability. Firmware and driver release packages will be documented when they are ready for public use.

This is an independent community project and is not affiliated with Xiaomi, MediaTek or Microsoft.

## Acknowledgements

- [Project Mu](https://github.com/microsoft/mu) and [Mu-Silicium](https://github.com/Project-Silicium/Mu-Silicium) for the UEFI foundation.
- [MiCode](https://github.com/MiCode) for published platform sources used as reference material.
- The [Xiaomi Pad 5 Windows project](https://github.com/erdilS/Port-Windows-11-Xiaomi-Pad-5) for inspiration for this documentation's language selection and page structure. Its installation instructions do not apply to Xiaomi 14T.

Last updated: **27 October 2026**.
