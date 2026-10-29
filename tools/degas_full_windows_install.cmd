@echo off
setlocal EnableExtensions DisableDelayedExpansion
title Xiaomi 14T - Windows 11 Pro deployment
echo Installing only into the previously verified Windows partition.
echo Keep external power connected. Do not reset during image application.
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" goto fail
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1 || goto fail
if not exist X:\Windows\System32\degas-install-authorized.txt goto fail
if not exist W:\DegasInstall\launch.once goto fail
if not exist W:\DegasInstall\install-pro.wim goto fail
if not exist W:\DegasInstall\degas-install-verify.exe goto fail
if not exist W:\DegasInstall\SYSTEM-DEGAS goto fail
if not exist W:\DegasInstall\SOFTWARE-DEGAS goto fail
if not exist W:\DegasInstall\drivers\ufs\degasufs.inf goto fail
if not exist W:\DegasInstall\drivers\touch\degastouch.inf goto fail
if not exist S:\EFI\Microsoft\Boot\BCD goto fail
if exist W:\Windows\System32\config\SYSTEM goto fail
if exist W:\DegasInstall\installed.ok goto fail
if exist W:\DegasInstall\BCD.before goto fail
W:\DegasInstall\degas-install-verify.exe targets || goto fail
copy /b S:\EFI\Microsoft\Boot\BCD W:\DegasInstall\BCD.before >nul || goto fail
if not exist W:\DegasInstall\scratch mkdir W:\DegasInstall\scratch
echo STEP 1: Applying clean Windows 11 Pro ARM64...
dism /Apply-Image /ImageFile:W:\DegasInstall\install-pro.wim /Index:1 /ApplyDir:W:\ /CheckIntegrity /Verify /ScratchDir:W:\DegasInstall\scratch /LogPath:W:\DegasInstall\apply.log
if errorlevel 1 goto fail
echo STEP 2: Applying checked offline account and power configuration...
copy /b W:\DegasInstall\SYSTEM-DEGAS W:\Windows\System32\config\SYSTEM >nul || goto fail
copy /b W:\DegasInstall\SOFTWARE-DEGAS W:\Windows\System32\config\SOFTWARE >nul || goto fail
if not exist W:\Windows\Panther mkdir W:\Windows\Panther
if not exist W:\Windows\Setup\Scripts mkdir W:\Windows\Setup\Scripts
copy /b W:\DegasInstall\unattend.xml W:\Windows\Panther\unattend.xml >nul || goto fail
copy /b W:\DegasInstall\DegasPower.cmd W:\Windows\Setup\Scripts\DegasPower.cmd >nul || goto fail
copy /b W:\DegasInstall\SetupComplete.cmd W:\Windows\Setup\Scripts\SetupComplete.cmd >nul || goto fail
echo STEP 3: Installing boot UFS and touch source driver...
dism /Image:W:\ /Add-Driver /Driver:W:\DegasInstall\drivers\ufs\degasufs.inf /ForceUnsigned /LogPath:W:\DegasInstall\ufs-install.log
if errorlevel 1 goto fail
dism /Image:W:\ /Add-Driver /Driver:W:\DegasInstall\drivers\touch\degastouch.inf /ForceUnsigned /LogPath:W:\DegasInstall\touch-install.log
if errorlevel 1 goto fail
rem Full Windows uses its inbox HID child driver. Do not install the PE-wide fallback.
W:\DegasInstall\degas-install-verify.exe boot || goto fail
if not exist W:\Windows\System32\winload.efi goto fail
echo STEP 4: Preparing separate BCD candidate, preserving WinPE...
copy /b W:\DegasInstall\BCD.before W:\DegasInstall\BCD.full >nul || goto fail
set "STORE=W:\DegasInstall\BCD.full"
set "OSID={d7343a14-1027-4c14-9e5a-6897c0de0001}"
bcdedit /store %STORE% /create %OSID% /d "Windows 11 Pro - Xiaomi 14T" /application osloader || goto fail
bcdedit /store %STORE% /set %OSID% device partition=W: || goto fail
bcdedit /store %STORE% /set %OSID% osdevice partition=W: || goto fail
bcdedit /store %STORE% /set %OSID% path \Windows\System32\winload.efi || goto fail
bcdedit /store %STORE% /set %OSID% systemroot \Windows || goto fail
bcdedit /store %STORE% /set %OSID% inherit {bootloadersettings} || goto fail
bcdedit /store %STORE% /set %OSID% locale ru-RU || goto fail
bcdedit /store %STORE% /set %OSID% testsigning on || goto fail
bcdedit /store %STORE% /set %OSID% hypervisorlaunchtype off || goto fail
bcdedit /store %STORE% /set %OSID% recoveryenabled no || goto fail
bcdedit /store %STORE% /set %OSID% bootlog yes || goto fail
bcdedit /store %STORE% /displayorder %OSID% /addfirst || goto fail
bcdedit /store %STORE% /default %OSID% || goto fail
bcdedit /store %STORE% /timeout 15 || goto fail
bcdedit /store %STORE% /enum all > W:\DegasInstall\BCD.full.txt || goto fail
echo Image and drivers installed. BCD candidate awaits verified publication.>W:\DegasInstall\installed.ok
echo SUCCESS - image installed; boot selection is still WinPE.
echo Report SUCCESS before restarting. Logs: W:\DegasInstall.
exit /b 0
:fail
echo INSTALL STOPPED. Do not retry or format. Preserve logs in W:\DegasInstall.
exit /b 1
