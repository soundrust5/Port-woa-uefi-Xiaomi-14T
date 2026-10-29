# OnePlus 12 companion Android build prerequisites

`tools/build_op12_companion.py` builds the plain-Java companion without Gradle.
It expects the official Android SDK Platform 35 and Android SDK Build-Tools
35.0.0 plus a complete JDK. It checks `--sdk`, `ANDROID_SDK_ROOT`,
`ANDROID_HOME`, and the standard per-user Windows SDK directory in that order.
It never modifies the global environment.

The application may use either of these layouts:

* `diagnostic/oneplus12/companion/android/app/src/main`
* `diagnostic/oneplus12/companion/android/src/main`

Run from anywhere with:

```powershell
python D:\Edk2-For-Xiaomi14T\tools\build_op12_companion.py
```

The signed development artifact is
`artifacts/oneplus12/companion/op12-companion.apk`. The reusable development
key is generated under `artifacts/local/op12-companion`; that entire local
state directory is excluded by its parent `.gitignore`. No install, ADB,
Fastboot, driver, reboot, or flashing command is performed.

The pure-Java journal and release-manifest checks can be compiled from the real
application sources and run on the host JDK with:

```powershell
python D:\Edk2-For-Xiaomi14T\tools\test_op12_companion_java.py
```

The host suite uses only test files below `tools/android-build/tests`. Its small
`org.json` implementation exists solely to execute `ManifestSpec` without an
Android runtime or an added third-party dependency.

`provenance.json` records the exact locally discovered SDK/JDK components used
during tool preparation. These packages were already present, so preparation
did not download or install anything.
