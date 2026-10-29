@echo off
setlocal EnableExtensions DisableDelayedExpansion
if /i not "%PROCESSOR_ARCHITECTURE%"=="ARM64" exit /b 10
reg query HKLM\SYSTEM\CurrentControlSet\Control\MiniNT >nul 2>&1 || exit /b 11
if not exist W:\DegasInstall\installed.ok exit /b 12
if not exist W:\DegasInstall\runtime-backup-pe-v2\SYSTEM exit /b 13
if not exist X:\Windows\System32\DegasRuntime.exe exit /b 14
if not exist X:\Windows\System32\BCD.runtime-original exit /b 15
reg load HKLM\DEGAS_RUNTIME W:\Windows\System32\config\SYSTEM || exit /b 20
set "LOADED=1"
for /f "tokens=3" %%A in ('reg query HKLM\DEGAS_RUNTIME\Select /v Current') do set "CONTROLSET=%%A"
if /i not "%CONTROLSET%"=="0x1" goto fail
reg query HKLM\DEGAS_RUNTIME\ControlSet001\Services\DegasRuntime >nul 2>&1
if not errorlevel 1 goto fail
if exist W:\Windows\System32\DegasRuntime.exe (
 copy /b W:\Windows\System32\DegasRuntime.exe W:\DegasInstall\runtime-backup-pe-v2\DegasRuntime.exe >nul
 if errorlevel 1 goto fail
)
copy /b X:\Windows\System32\DegasRuntime.exe W:\Windows\System32\DegasRuntime.exe >nul || goto fail
set "SERVICE=HKLM\DEGAS_RUNTIME\ControlSet001\Services\DegasRuntime"
reg add "%SERVICE%" /v Type /t REG_DWORD /d 16 /f || goto fail
reg add "%SERVICE%" /v Start /t REG_DWORD /d 2 /f || goto fail
reg add "%SERVICE%" /v ErrorControl /t REG_DWORD /d 0 /f || goto fail
reg add "%SERVICE%" /v ImagePath /t REG_EXPAND_SZ /d "\SystemRoot\System32\DegasRuntime.exe" /f || goto fail
reg add "%SERVICE%" /v ObjectName /t REG_SZ /d LocalSystem /f || goto fail
reg add "%SERVICE%" /v DisplayName /t REG_SZ /d "Degas runtime CPU diagnostics" /f || goto fail
reg add HKLM\DEGAS_RUNTIME\ControlSet001\Control\CrashControl /v DisplayParameters /t REG_DWORD /d 1 /f || goto fail
reg add HKLM\DEGAS_RUNTIME\ControlSet001\Control\CrashControl /v AutoReboot /t REG_DWORD /d 0 /f || goto fail
reg unload HKLM\DEGAS_RUNTIME || goto fail
set "LOADED=0"
copy /b S:\EFI\Microsoft\Boot\BCD W:\DegasInstall\runtime-backup-pe-v2\BCD >nul || exit /b 21
copy /b X:\Windows\System32\BCD.runtime-original S:\EFI\Microsoft\Boot\BCD >nul || exit /b 22
echo CPU logger and BSOD display installed. No setup or CPU power policy edits.>W:\DegasInstall\runtime-diagnostics-installed.ok
exit /b 0
:fail
if "%LOADED%"=="1" reg unload HKLM\DEGAS_RUNTIME
exit /b 30
