# Local OnePlus 12 adaptation

Based on https://github.com/n00b69/woa-helper (upstream notices and README retained).
Application ID: com.woa.helper.op12; version 1.8.7.2-op12.1. Personal local fork; no upstream update channel.

Root shell and Windows partition operations require the verified CPH2573 / OP595DL1 identity. Windows resolves only /dev/block/by-name/windows at runtime. No raw partition numbers are embedded. The distinct ESP and modem partitions are not used by enabled features.

Mount uses the ROM's /system_ext/bin/ntfs-3g in libsu's mount-master root shell. Read-only is default; the main mount dialog separately offers read/write. Both use norecover and no force/remove_hiberfile. Actual source, target and ro/rw mode are checked in /proc/mounts. Wrong access mode requires unmount first; a failed or read-only fallback writable request is not reported as writable. KernelSU namespace visibility in file managers remains untested until an authorized device install. Generic Windows tools and backup-to-Windows require the user to explicitly mount read/write first.

Reboot uses normal svc power reboot to the already-installed Mu menu; Windows selection is manual. No boot flash is involved. Current boot backup copies Mu, not Android init_boot, and is labelled accordingly. Android-storage backup remains available; this is not a comprehensive disaster-recovery backup.

Unsupported and visibly disabled: UEFI flash, StA/automatic boot restore, modem provisioning, DBKP, devcfg flashing, legacy USB host scripts. Quickboot QS tile is unavailable. Mount QS tile, widget and automount remain read-only. Legacy boot write entry points are additionally rejected by the shared shell guard. SELinux is not changed. In-app upstream updating is disabled.

Upstream has no tracked LICENSE/COPYING file at the inspected revision. Existing credits and notices are retained. This local adaptation does not establish redistribution permission or change upstream licensing. No Microsoft signature is supplied or claimed. Debug APK uses the local Android debug certificate.

Build: JDK 21, Gradle wrapper 9.4.1, AGP 9.2.1, SDK/build-tools 36; `gradlew.bat assembleDebug`. No phone installation, mount, reboot, flash, format or partition write was performed during creation.

Validation: `gradlew.bat testDebugUnitTest assembleDebug` passed, including four MountTable unit tests for exact source/target, read-only fallback, and occupied targets. APK construction is verified; device UI, NTFS operations and app/file-manager namespace visibility are not yet tested. No device installation was performed.
