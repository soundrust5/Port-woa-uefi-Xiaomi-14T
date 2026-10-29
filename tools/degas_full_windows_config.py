"""Prepare Xiaomi offline installation configuration; never alter host trust/hives."""
from pathlib import Path
import ctypes as c
from ctypes import wintypes as w
import hashlib,json,subprocess
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/degas/full-windows'

def main():
 OUT.mkdir(parents=True,exist_ok=True)
 cert=(ROOT/'artifacts/degas/ufs-windows/test-package/development.cer').read_bytes()
 assert hashlib.sha256(cert).hexdigest()=='0515a9334e3dfc3c4a90a52dfc8a5fc6a0e995d199d77e92a1a90f3ef37920d6'
 crypt=c.WinDLL('crypt32',use_last_error=True)
 crypt.CertCreateCertificateContext.argtypes=[w.DWORD,c.c_void_p,w.DWORD]
 crypt.CertCreateCertificateContext.restype=c.c_void_p
 crypt.CertSerializeCertificateStoreElement.argtypes=[c.c_void_p,w.DWORD,c.c_void_p,c.POINTER(w.DWORD)]
 crypt.CertFreeCertificateContext.argtypes=[c.c_void_p]
 buf=c.create_string_buffer(cert);ctx=crypt.CertCreateCertificateContext(1,buf,len(cert));assert ctx
 try:
  size=w.DWORD();assert crypt.CertSerializeCertificateStoreElement(ctx,0,None,c.byref(size))
  blob=c.create_string_buffer(size.value);assert crypt.CertSerializeCertificateStoreElement(ctx,0,blob,c.byref(size))
  (OUT/'test-cert-store-blob.bin').write_bytes(blob.raw[:size.value])
 finally:crypt.CertFreeCertificateContext(ctx)
 (OUT/'test-cert.json').write_text(json.dumps({'sha1':hashlib.sha1(cert).hexdigest().upper(),'sha256':hashlib.sha256(cert).hexdigest(),'scope':'offline SOFTWARE only'},indent=2))
 (OUT/'unattend.xml').write_text('''<?xml version="1.0" encoding="utf-8"?>
<unattend xmlns="urn:schemas-microsoft-com:unattend">
 <settings pass="specialize">
  <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="arm64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
   <ComputerName>DEGAS-PC</ComputerName><TimeZone>Russian Standard Time</TimeZone>
  </component>
  <component name="Microsoft-Windows-Deployment" processorArchitecture="arm64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
   <RunSynchronous><RunSynchronousCommand wcm:action="add"><Order>1</Order><Description>Disable unsupported automatic sleep</Description><Path>cmd.exe /c %WINDIR%\\Setup\\Scripts\\DegasPower.cmd</Path></RunSynchronousCommand></RunSynchronous>
  </component>
 </settings>
 <settings pass="oobeSystem">
  <component name="Microsoft-Windows-International-Core" processorArchitecture="arm64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS">
   <InputLocale>0409:00000409;0419:00000419</InputLocale><SystemLocale>ru-RU</SystemLocale><UILanguage>ru-RU</UILanguage><UserLocale>ru-RU</UserLocale>
  </component>
  <component name="Microsoft-Windows-Shell-Setup" processorArchitecture="arm64" publicKeyToken="31bf3856ad364e35" language="neutral" versionScope="nonSxS" xmlns:wcm="http://schemas.microsoft.com/WMIConfig/2002/State">
   <OOBE><HideOnlineAccountScreens>true</HideOnlineAccountScreens><HideWirelessSetupInOOBE>true</HideWirelessSetupInOOBE><ProtectYourPC>3</ProtectYourPC></OOBE>
   <UserAccounts><LocalAccounts><LocalAccount wcm:action="add"><Name>Xiaomi</Name><DisplayName>Xiaomi</DisplayName><Group>Administrators</Group><Password><Value></Value><PlainText>true</PlainText></Password></LocalAccount></LocalAccounts></UserAccounts>
  </component>
 </settings>
</unattend>
''',encoding='utf-8')
 power=b'@echo off\r\npowercfg /hibernate off\r\npowercfg /change standby-timeout-ac 0\r\npowercfg /change standby-timeout-dc 0\r\npowercfg /change monitor-timeout-ac 0\r\npowercfg /change monitor-timeout-dc 0\r\nexit /b 0\r\n'
 (OUT/'DegasPower.cmd').write_bytes(power)
 (OUT/'SetupComplete.cmd').write_bytes(b'@echo off\r\ncall "%WINDIR%\\Setup\\Scripts\\DegasPower.cmd"\r\nexit /b 0\r\n')
 subprocess.run(['wsl.exe','-d','Ubuntu','--','python3','/mnt/d/Edk2-For-Xiaomi14T/tools/degas_full_windows_hives.py'],check=True)
 print('PASS: local Xiaomi/no password configuration and offline hive copies prepared')
if __name__=='__main__':main()
