@echo off
setlocal EnableExtensions DisableDelayedExpansion
title Xiaomi 14T - UFS 0.2 and setup recovery
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" goto fail
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1 || goto fail
if not exist W:\DegasInstall\maintenance.once goto fail
if not exist W:\DegasInstall\installed.ok goto fail
if not exist W:\DegasInstall\BCD.system goto fail
W:\DegasInstall\degas-maintenance-verify.exe targets || goto fail
if exist W:\DegasInstall\maintenance-complete.ok goto fail
if exist W:\DegasInstall\maintenance-backup goto fail
mkdir W:\DegasInstall\maintenance-backup || goto fail
copy /b W:\Windows\System32\config\SYSTEM W:\DegasInstall\maintenance-backup\SYSTEM >nul || goto fail
if exist W:\Windows\System32\config\SYSTEM.LOG1 copy /b W:\Windows\System32\config\SYSTEM.LOG1 W:\DegasInstall\maintenance-backup\SYSTEM.LOG1 >nul
if exist W:\Windows\System32\config\SYSTEM.LOG2 copy /b W:\Windows\System32\config\SYSTEM.LOG2 W:\DegasInstall\maintenance-backup\SYSTEM.LOG2 >nul
copy /b S:\EFI\Microsoft\Boot\BCD W:\DegasInstall\maintenance-backup\BCD >nul || goto fail
copy /b W:\Windows\System32\config\SOFTWARE W:\DegasInstall\maintenance-backup\SOFTWARE >nul || goto fail
copy /b W:\Windows\System32\config\DRIVERS W:\DegasInstall\maintenance-backup\DRIVERS >nul || goto fail
echo STEP 1: Installing UFS 0.2 into installed Windows...
dism /Image:W:\ /Add-Driver /Driver:W:\DegasInstall\Ufs02\degasufs.inf /ForceUnsigned /LogPath:W:\DegasInstall\ufs02-install.log
if errorlevel 1 goto fail
W:\DegasInstall\degas-maintenance-verify.exe boot || goto fail
echo STEP 2: Resetting two failed setup bookkeeping values...
W:\DegasInstall\degas-maintenance-verify.exe repairsetup || goto fail
copy /b W:\DegasInstall\unattend-retry.xml W:\Windows\Panther\unattend.xml >nul || goto fail
echo STEP 3: Publishing the verified system BCD...
copy /b W:\DegasInstall\BCD.system S:\EFI\Microsoft\Boot\BCD >nul || goto fail
echo UFS 0.2 installed; system BCD restored; setup retry ready.>W:\DegasInstall\maintenance-complete.ok
move W:\DegasInstall\maintenance.once W:\DegasInstall\maintenance.used >nul || goto fail
echo SUCCESS - UFS 0.2 installed and setup recovery prepared.
echo Return to fastboot and report SUCCESS. Do not run installation again.
exit /b 0
:fail
echo MAINTENANCE STOPPED. Preserve W:\DegasInstall logs.
exit /b 1
