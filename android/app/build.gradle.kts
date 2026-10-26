plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "com.bbshd.middleman"
    compileSdk = 34

    defaultConfig {
        applicationId = "com.bbshd.middleman"
        minSdk = 26
        targetSdk = 34
        versionCode = 14
        versionName = "0.9.2"
    }

    buildTypes {
        debug {
            // Sideloaded builds are never published, so the debug key is fine and
            // keeps `assembleDebug` self-contained (no keystore to manage).
            applicationIdSuffix = ""
            isMinifyEnabled = false
        }
        release {
            // Deliberately signed with the default debug key so `assembleRelease`
            // also produces something installable without setting up a keystore.
            // Anyone distributing this beyond a personal device should make a
            // real keystore first.
            signingConfig = signingConfigs.getByName("debug")
            isMinifyEnabled = false
        }
    }

    // A debug keystore normally lives in ~/.android, which does not exist and is
    // not writable in the throwaway-toolchain setup. tools/build.sh therefore
    // generates one inside .toolchain and points this property at it. Anyone
    // building in Android Studio passes nothing and gets the normal behaviour.
    val localDebugKeystore = providers.gradleProperty("bbshdDebugKeystore").orNull
    if (localDebugKeystore != null) {
        signingConfigs.getByName("debug").apply {
            storeFile = file(localDebugKeystore)
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }
}

// No third-party dependencies at all: framework `org.json` for parsing and plain
// framework views for the UI. That keeps the APK under 100 KB, the build fast,
// and the app free of a CVE surface it does not need.
dependencies {
}
