// The app: framework UI only (no AndroidX), Kotlin, arm64.  The emulator is the
// prebuilt src/main/jniLibs/arm64-v8a/libPeepeeBox.so that scripts/build.sh makes
// first; PeepeeBox's ROM set and seeded nvram go in as assets/roms and assets/nvr
// (unpacked on first run: Assets.kt).
import java.util.Properties

plugins {
    id("com.android.application")
}

// The toolchain folder (scripts/env.sh): ~/mpb-android, or $MPB_ANDROID on CI.  It is
// shared with MegaPPBox, so this app's own files in it are named ppbox-*.
val mpbAndroid = System.getenv("MPB_ANDROID") ?: (System.getProperty("user.home") + "/mpb-android")

// The release key: ppbox-release.keystore there, its passwords in
// ppbox-release-signing.properties (made once, never in the repository; on CI from the
// repository's secrets).  Every update of a release install must be signed with it:
// keep a backup.
val releaseSigning = File("$mpbAndroid/ppbox-release-signing.properties")
    .takeIf { it.isFile }
    ?.let { f -> Properties().apply { f.inputStream().use { load(it) } } }

// The version is PeepeeBox's release number, which scripts/build.sh passes in
// (-PpeepeeboxRelease): "1.15" -> versionName 1.15, versionCode 115; "1.15+" (past the
// tag) keeps 115.  Without one (a Gradle run of its own) it is "dev".
val ppbRelease = (findProperty("peepeeboxRelease") as String?)?.takeIf { it.isNotBlank() } ?: "dev"
val ppbNumber = Regex("^([0-9]+)\\.([0-9]+)").find(ppbRelease)?.groupValues

android {
    namespace = "io.github.xeon3d.peepeebox"
    compileSdk = 36
    ndkVersion = "27.2.12479018" // strips the native library

    // The local debug key (debug.keystore in the toolchain folder, shared with
    // MegaPPBox): an update must be signed as the installed app is.  Without it,
    // Android's default debug key.
    signingConfigs {
        val debugKey = File("$mpbAndroid/debug.keystore")
        if (debugKey.isFile) getByName("debug") {
            storeFile = debugKey
            storePassword = "android"
            keyAlias = "androiddebugkey"
            keyPassword = "android"
        }
        if (releaseSigning != null) {
            create("release") {
                storeFile = file(releaseSigning.getProperty("storeFile"))
                storePassword = releaseSigning.getProperty("storePassword")
                keyAlias = releaseSigning.getProperty("keyAlias")
                keyPassword = releaseSigning.getProperty("keyPassword")
            }
        }
    }

    buildTypes {
        // Not debuggable.  No R8: the JNI entry points are found by class and method
        // name, and the app is small anyway.
        getByName("release") {
            isMinifyEnabled = false
            signingConfig = signingConfigs.findByName("release")
        }
    }

    defaultConfig {
        applicationId = "io.github.xeon3d.peepeebox"
        minSdk = 28
        targetSdk = 36
        versionCode = ppbNumber?.let { it[1].toInt() * 100 + it[2].toInt() } ?: 1
        versionName = ppbRelease
        ndk { abiFilters += "arm64-v8a" }
    }

    sourceSets {
        getByName("main") {
            assets.directories.add(layout.buildDirectory.dir("generated/ppb-assets").get().asFile.path)
        }
    }
}

// The repository's roms/ and nvr/ as assets/roms and assets/nvr.  The nvram is the
// cabinet's settled CMOS and flash: without it the 4DPS BIOS finds no drive and stops
// on a hard disk error (nvr/README.md).
val copyAssets by tasks.registering(Copy::class) {
    from(rootProject.file("../roms")) {
        into("roms")
        exclude("README.md", "LICENSE")
    }
    from(rootProject.file("../nvr")) {
        into("nvr")
        exclude("README.md")
    }
    into(layout.buildDirectory.dir("generated/ppb-assets"))
}
tasks.named("preBuild") { dependsOn(copyAssets) }
