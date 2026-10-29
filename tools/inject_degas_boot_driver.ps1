param([Parameter(Mandatory=$true)][string]$Wim)
$ErrorActionPreference='Stop'
$root='D:\Edk2-For-Xiaomi14T'
$out=Join-Path $root 'artifacts\degas\ufs-windows\boot-injection'
New-Item -ItemType Directory -Path $out -Force | Out-Null
$mount=Join-Path $out 'mount'
New-Item -ItemType Directory -Path $mount -Force | Out-Null
$mounted=$false
$result=@{success=$false;hostDriverInstallation=$false;wim=$Wim}
function RunDism([string[]]$Arguments,[string]$Log) {
 & "$env:SystemRoot\System32\dism.exe" @Arguments "/LogPath:$out\dism.log" /English *> (Join-Path $out $Log)
 if($LASTEXITCODE -ne 0){throw "DISM failed ${LASTEXITCODE}: $Log"}
}
try {
 if(-not [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){throw 'Administrative servicing token required'}
 $resolved=(Resolve-Path -LiteralPath $Wim).Path
 if(-not $resolved.StartsWith("$root\artifacts\degas\winpe-log\",[StringComparison]::OrdinalIgnoreCase)){throw 'WIM outside candidate directory'}
 if(@(Get-ChildItem -LiteralPath $mount -Force).Count){throw 'Mount not empty'}
 RunDism @('/Mount-Image',"/ImageFile:$resolved",'/Index:2',"/MountDir:$mount") 'mount.txt'
 $mounted=$true
 RunDism @("/Image:$mount",'/Add-Driver',"/Driver:$root\artifacts\degas\ufs-windows\test-package\degasufs.inf",'/ForceUnsigned') 'add.txt'
 RunDism @("/Image:$mount",'/Get-Drivers','/All','/Format:List') 'drivers.txt'
 $result.success=$true
} catch {$result.error=$_.Exception.Message}
finally {
 if($mounted){try {
  $action=if($result.success){'/Commit'}else{'/Discard'}
  RunDism @('/Unmount-Image',"/MountDir:$mount",$action) 'unmount.txt'
 }catch {$result.success=$false;$result.unmountError=$_.Exception.Message}}
 $result | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'result.json')
}
