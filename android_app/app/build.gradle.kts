plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.kotlin.compose)
    alias(libs.plugins.kotlin.serialization)
}

import java.util.Properties

// Release signing: a key you create once with keytool (see keystore.example.properties), described in keystore.properties
// next to settings.gradle.kts (git-ignored; keystore.example.properties shows the format). Without that
// file, assembleRelease still builds, but the APK is unsigned and can't be installed.
val keystoreFile = rootDir.resolve("keystore.properties")
val keystore = Properties().apply { if (keystoreFile.exists()) keystoreFile.inputStream().use { load(it) } }

android {
    namespace = "io.github.yahyatamur.blinds"
    compileSdk = 36

    defaultConfig {
        applicationId = "io.github.yahyatamur.blinds"
        minSdk = 26          // java.time, adaptive icons, notification channels
        targetSdk = 36
        versionCode = 11
        versionName = "0.3.2"
    }

    signingConfigs {
        if (keystoreFile.exists()) create("release") {
            storeFile = file(keystore.getProperty("storeFile"))
            // storePassword/keyPassword, or one "password" for both (the format Tauri projects use)
            val password = keystore.getProperty("password")
            storePassword = keystore.getProperty("storePassword") ?: password
            keyAlias = keystore.getProperty("keyAlias")
            keyPassword = keystore.getProperty("keyPassword") ?: storePassword
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
            if (keystoreFile.exists()) signingConfig = signingConfigs.getByName("release")
            resValue("string", "app_name", "Blinds")
        }
        debug {
            // A separate app next to the release one ("Blinds debug"), so trying a debug build never
            // forces uninstalling the real app (and losing its settings and saved Wi-Fi password)
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
            resValue("string", "app_name", "Blinds debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    buildFeatures {
        compose = true
        buildConfig = true   // VERSION_NAME for the settings screen
        resValues = true     // app_name per build type
    }
}

dependencies {
    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.activity.compose)
    implementation(libs.androidx.lifecycle.runtime.compose)
    implementation(libs.androidx.lifecycle.viewmodel.compose)
    implementation(platform(libs.androidx.compose.bom))
    implementation(libs.androidx.compose.ui)
    implementation(libs.androidx.compose.ui.tooling.preview)
    implementation(libs.androidx.compose.material3)
    implementation(libs.androidx.compose.material.icons.core)
    implementation(libs.kotlinx.serialization.json)
    implementation(libs.kotlinx.coroutines.android)
    debugImplementation(libs.androidx.compose.ui.tooling)

    testImplementation(libs.junit)
    testImplementation(libs.kotlinx.coroutines.test)
}
