plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

import java.io.ByteArrayOutputStream
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

val gitCommit = try {
    val stdout = ByteArrayOutputStream()
    project.exec {
        commandLine = listOf("git", "rev-parse", "--short", "HEAD")
        standardOutput = stdout
    }
    val hash = stdout.toString().trim()
    val dirty = ByteArrayOutputStream()
    project.exec {
        commandLine = listOf("git", "status", "--porcelain", "--untracked-files=normal")
        standardOutput = dirty
    }
    hash + if (dirty.size() > 0) "-dirty" else ""
} catch (e: Exception) {
    "dev"
}
val buildTime = SimpleDateFormat("yyyyMMdd-HHmm", Locale.US).format(Date())

android {
    namespace = "com.scanner.app"
    compileSdk = 35
    buildToolsVersion = "36.0.0"
    ndkVersion = "30.0.16248370"

    defaultConfig {
        applicationId = "moe.hachimi.cam"
        minSdk = 29
        targetSdk = 35
        versionCode = 41
        versionName = "0.1.2-rc17"

        buildConfigField("String", "GIT_HASH", "\"$gitCommit\"")
        buildConfigField("String", "BUILD_TIME", "\"$buildTime\"")
        
        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64")
        }
        
        externalNativeBuild {
            cmake {
                cppFlags += "-std=c++17"
                arguments += "-DANDROID_STL=c++_shared"
            }
        }
    }

    flavorDimensions += "edition"
    productFlavors {
        create("standard") {
            dimension = "edition"
            applicationId = "com.open.scan"
        }
        create("hachimi") {
            dimension = "edition"
            applicationId = "moe.hachimi.cam"
        }
    }

    splits {
        abi {
            isEnable = true
            reset()
            include("arm64-v8a", "armeabi-v7a", "x86_64")
            isUniversalApk = true
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = true
        }
    }

    signingConfigs {
        create("release") {
            val keyFile = file("hachimi-release.jks")
            if (keyFile.exists()) {
                storeFile = keyFile
                storePassword = "hachimi123"
                keyAlias = "hachimi"
                keyPassword = "hachimi123"
                enableV1Signing = true
                enableV2Signing = true
                enableV3Signing = true
            }
        }
    }

    buildTypes {
        debug {
            // A test build must be installable even without the private release key.
            signingConfig = signingConfigs.getByName("debug")
            versionNameSuffix = "-test"
            applicationIdSuffix = ".test"
            resValue("string", "build_app_name", "HachiCamTest")
            buildConfigField("boolean", "SENSITIVE_DIAGNOSTICS", "true")
            externalNativeBuild {
                cmake { arguments += "-DHACHICAM_GPU_EXPERIMENTAL=ON" }
            }
        }
        release {
            resValue("string", "build_app_name", "HachiCam")
            buildConfigField("boolean", "SENSITIVE_DIAGNOSTICS", "false")
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    
    kotlinOptions {
        jvmTarget = "17"
    }

    buildFeatures {
        compose = true
        prefab = true
        buildConfig = true
    }
    
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = providers.gradleProperty("scanner.cmakeVersion").getOrElse("3.22.1")
        }
    }
}

dependencies {
    testImplementation("junit:junit:4.13.2")
    val composeBom = platform("androidx.compose:compose-bom:2024.09.00")
    implementation(composeBom)
    androidTestImplementation(composeBom)
    
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    
    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
    
    implementation("androidx.core:core-ktx:1.13.1")
    implementation("androidx.appcompat:appcompat:1.7.0")
    implementation("androidx.activity:activity-compose:1.9.2")
    
    implementation("androidx.navigation:navigation-compose:2.8.0")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.6")
    
    val cameraxVersion = "1.4.1"
    implementation("androidx.camera:camera-camera2:$cameraxVersion")
    implementation("androidx.camera:camera-lifecycle:$cameraxVersion")
    implementation("androidx.camera:camera-view:$cameraxVersion")
    implementation("androidx.exifinterface:exifinterface:1.3.2")
    
    implementation("org.opencv:opencv:4.10.0")
}
