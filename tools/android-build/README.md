# Build WoA Helper

Requires JDK 21 and Android SDK 36. Set `ANDROID_HOME` or put the SDK path in
`apps/woa-helper-degas/local.properties` as `sdk.dir=...`.

From the repository root:

```powershell
cd apps/woa-helper-degas
.\gradlew.bat testDebugUnitTest assembleDebug
```

APK: `app/build/outputs/apk/debug/app-debug.apk`.

The Gradle wrapper downloads dependencies on the first build. Local SDK paths,
build directories and signing keys must not be committed.
