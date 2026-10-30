# WoA Helper вЂ” Xiaomi 14T (degas)

Android companion adapted from the existing OnePlus 12 fork of n00b69/woa-helper. Package: `com.woa.helper.degas`.

## Р СѓСЃСЃРєРёР№

РќСѓР¶РµРЅ root (KernelSU/Magisk). РџСЂРёР»РѕР¶РµРЅРёРµ РїРѕРґРґРµСЂР¶РёРІР°РµС‚ С‚РѕР»СЊРєРѕ `degas`.

- РџРѕРґРєР»СЋС‡РµРЅРёРµ СЂР°Р·РґРµР»Р° Windows C: РІ `/storage/emulated/0/Windows` РёР»Рё `/mnt/Windows`.
- РћС‚РґРµР»СЊРЅС‹Р№ РІС‹Р±РѕСЂ В«С‚РѕР»СЊРєРѕ С‡С‚РµРЅРёРµВ» Рё В«С‡С‚РµРЅРёРµ/Р·Р°РїРёСЃСЊВ». РџР»РёС‚РєР°, РІРёРґР¶РµС‚ Рё Р°РІС‚РѕРјРѕРЅС‚РёСЂРѕРІР°РЅРёРµ РёСЃРїРѕР»СЊР·СѓСЋС‚ С‚РѕР»СЊРєРѕ С‡С‚РµРЅРёРµ.
- Р”Р»СЏ Р·Р°РїРёСЃРё РїРѕР»РЅРѕСЃС‚СЊСЋ Р·Р°РІРµСЂС€РёС‚Рµ СЂР°Р±РѕС‚Сѓ Windows. Р“СЂСЏР·РЅС‹Р№/РіРёР±РµСЂРЅРёСЂРѕРІР°РЅРЅС‹Р№ NTFS РЅРµ РІРѕСЃСЃС‚Р°РЅР°РІР»РёРІР°РµС‚СЃСЏ РїСЂРёРЅСѓРґРёС‚РµР»СЊРЅРѕ.
- РџРµСЂРµР·Р°РіСЂСѓР·РєР° РІ СѓСЃС‚Р°РЅРѕРІР»РµРЅРЅРѕРµ РјРµРЅСЋ UEFI, СЂРµР·РµСЂРІРЅР°СЏ РєРѕРїРёСЏ boot Р°РєС‚РёРІРЅРѕРіРѕ СЃР»РѕС‚Р°.
- Р СѓСЃСЃРєРёР№ Рё Р°РЅРіР»РёР№СЃРєРёР№ РёРЅС‚РµСЂС„РµР№СЃ. РџСЂРѕС€РёРІРєР° СЂР°Р·РґРµР»РѕРІ, РјРѕРґРµРјР° Рё devcfg РѕС‚РєР»СЋС‡РµРЅР°.

Р Р°Р·РґРµР» РЅР°С…РѕРґРёС‚СЃСЏ РїРѕ `/dev/block/by-name/Windows`, Р±РµР· С„РёРєСЃРёСЂРѕРІР°РЅРЅРѕРіРѕ РЅРѕРјРµСЂР° UFS РёР»Рё СЃР»РѕС‚Р°. SELinux РЅРµ РѕС‚РєР»СЋС‡Р°РµС‚СЃСЏ. Р’ РѕР±С‹С‡РЅРѕРј С„Р°Р№Р»РѕРІРѕРј РјРµРЅРµРґР¶РµСЂРµ РґРѕСЃС‚СѓРї Р·Р°РІРёСЃРёС‚ РѕС‚ СЂР°Р·СЂРµС€РµРЅРёР№ Android Рё РІРёРґРёРјРѕСЃС‚Рё mount namespace; РґР»СЏ РѕРєРѕРЅС‡Р°С‚РµР»СЊРЅРѕР№ РїСЂРѕРІРµСЂРєРё РЅСѓР¶РµРЅ С‚РµСЃС‚ UI РЅР° РєРѕРЅРєСЂРµС‚РЅРѕР№ РїСЂРѕС€РёРІРєРµ.

## English

Root (KernelSU/Magisk) is required. Only `degas` is supported. Mount the named Windows partition read-only or read/write; browse it at the paths above. Fully shut down Windows before writable access. Dirty/hibernated NTFS is never forced writable. Widgets/tiles/automount are read-only. Boot backup resolves the active slot dynamically; reboot enters the installed UEFI menu. Partition flashing, modem/devcfg and legacy boot patching remain disabled. SELinux is unchanged.

## Build / validation

JDK 21, Android SDK 36, bundled Gradle wrapper:

```powershell
.\gradlew.bat testDebugUnitTest assembleDebug
```

Debug APK is signed with the local Android debug certificate. Unit tests and APK build passed. Native RO mount and RW temporary-file readback/removal/unmount passed on the phone in the global root mount namespace, using the same NTFS binary. The user confirmed reading and writing through the installed app. The quickboot action now explicitly unmounts a verified Windows volume before reboot and cancels reboot on unmount failure; that reboot behavior has build validation, not a new device reboot test.

The native executable is `app/src/main/jniLibs/arm64-v8a/libntfs3g_exec.so`, SHA256 `77f47b1b5655a385c136d1b6bc0b0e1cd15f43446bb89886aa7b6c7a34b29a15`. It is the already-tested static ARM64 NTFS-3G build. Matching source archive and COPYING are in `third_party/`; root repository script `tools/degas_full_windows_build_ntfs.sh` records its build recipe. Preserved upstream README and OnePlus adaptation notes describe ancestry, not the current supported-device list. No new license is asserted over upstream code.
