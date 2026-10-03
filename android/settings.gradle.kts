// PeepeeBox for Android: the native app around libPeepeeBox.so (scripts/build.sh).
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositories {
        google()
        mavenCentral()
    }
}
rootProject.name = "PeepeeBox"
include(":app")
