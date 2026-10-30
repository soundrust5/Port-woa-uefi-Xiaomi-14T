plugins {
    alias(libs.plugins.agp.app)
}

val locales = listOf(
    "ar", "az", "be", "cs", "de", "en", "es", "fa", "fr", "ind", "ja", "ka", "ko", "ms", "nl", "pl", "pt", "ru", "ro", "ro-rMD", "th", "tr", "uk", "vi", "zh", "zh-rCN", "zh-rHK", "zh-rMO", "zh-rSG", "zh-rTW"
)

android {
    namespace = "com.woa.helper"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.woa.helper.degas"
        minSdk = 25
        targetSdk = 36
        versionCode = 1
        versionName = "1.0-degas"

        val localesArray = locales.joinToString(", ") { tag ->
            val bcpTag = tag.replace("-r", "-").let { cleanTag ->
                when (cleanTag) {
                    "zh" -> "zh-Hans"
                    "zh-CN", "zh-SG" -> "zh-Hans-${cleanTag.substringAfter("-")}"
                    "zh-TW", "zh-HK", "zh-MO" -> "zh-Hant-${cleanTag.substringAfter("-")}"
                    else -> cleanTag
                }
            }
            "\"$bcpTag\""
        }
        buildConfigField("String[]", "LOCALES", "{$localesArray}")

        androidResources.localeFilters += locales
    }

    packaging { jniLibs.useLegacyPackaging = true }

    buildTypes {
        release {
            isShrinkResources = true
            isMinifyEnabled = true
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"))
        }

    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_21
        targetCompatibility = JavaVersion.VERSION_21
    }

    kotlin {
        jvmToolchain(21)
    }

    buildFeatures {
        viewBinding = true
        buildConfig = true
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    implementation(libs.com.github.topjohnwu.libsu.core)
    implementation(libs.com.github.topjohnwu.libsu.service)
    implementation(libs.com.github.topjohnwu.libsu.nio)
    implementation(libs.realtimeblurview)
}
