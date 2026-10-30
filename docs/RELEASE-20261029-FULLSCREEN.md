# Xiaomi 14T UEFI — fullscreen / native 8 CPU experimental build

Experimental prerelease for Xiaomi 14T **degas / MT6897 only**, dated 29 October 2026.

## Changes and source update

- Restores the full-screen Windows framebuffer viewport by default; diagnostic timer overlays remain off.
- Native eight-CPU PSCI/context bridge, CPU topology, UFS, display and touch integration are included.
- Publishes current UEFI platform sources, Windows UFS/multitouch driver sources, qualified AMU feedback probe sources, build/package scripts, host tests and research reports.
- The `CPU_DVFS_SNAPSHOT` menu route explicitly performs a manual experimental 2 GHz control test. Windows DVFS is **not working yet**; AMU feedback and performance-provider integration remain under investigation.

Earlier physical-device builds demonstrated the UEFI menu and Android route, full Windows boot from UFS with all eight CPUs, UFS read/write, multitouch and autonomous framebuffer refresh. This exact new fullscreen binary has passed build/link and packaging checks; it **has not yet been booted on the phone**. Build qualification does not prove a new hardware run.

## Contents and limitations

`boot.img` is the UEFI Android boot container, not a Windows installer. Windows requires a separate compatible installation and drivers; no Windows images, proprietary Windows drivers, NT kernels, private signing material or raw phone dumps are bundled.

Native eight-CPU support is gated to NT kernel SHA-256 `5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c`. Other Windows kernels are unsupported. GPU acceleration, Wi-Fi and Bluetooth are not implemented. Sleep/resume, hotplug and general device compatibility are unqualified. The Windows UFS driver disables the watchdog; automatic recovery after storage initialization is not guaranteed.

## Flash and restore

Use only on an unlocked Xiaomi 14T degas with a verified stock backup and working fastboot recovery. Check `SHA256SUMS` before use. The experimental project uses slot B; preserve your own original stock `boot_b.img` before replacing it. Do not use this package on Xiaomi 14T Pro or any other model.

```sh
# Install only after checking the device/model, current slot and your backup.
fastboot flash boot_b boot.img
fastboot set_active b
fastboot reboot

# Restore your own verified stock backup through fastboot if needed.
fastboot flash boot_b path/to/your-original-boot_b.img
fastboot set_active b
fastboot reboot
```

No repartitioning, Windows installation or bootloader unlocking is performed by these instructions. Existing Windows/ESP layout and compatible drivers are prerequisites for Windows boot; a general public Windows installation workflow is not available yet.

Full source reconstruction and toolchain requirements: [SOURCE.md](https://github.com/soundrust5/Port-woa-uefi-Xiaomi-14T/blob/main/SOURCE.md).
