# PeepeeBox for Android - where the toolchain is (sourced by the other scripts).
#
# Locally everything is user-local under ~/mpb-android (setup-toolchain.sh makes it).
# CI points these at the runner's own SDK and JDK instead.
T=${MPB_ANDROID:-$HOME/mpb-android}
SDK=${MPB_ANDROID_SDK:-$T/sdk}
NDK=$SDK/ndk/27.2.12479018
export JAVA_HOME=${MPB_JAVA_HOME:-$T/jdk21}
export ANDROID_HOME=$SDK ANDROID_SDK_ROOT=$SDK ANDROID_NDK_ROOT=$NDK
