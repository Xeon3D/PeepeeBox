#!/usr/bin/env bash
# PeepeeBox for Android - build the native library and the app (Linux or WSL).
#
#   bash android/scripts/build.sh [configure] [release]
#                       (toolchain: setup-toolchain.sh; libraries: build-deps.sh)
#
# 1. libPeepeeBox.so: this repository's emulator (CMake, with if(ANDROID) in place of
#    the Qt front end) + android/native (the platform layer and the JNI bridge), with
#    the NDK's CMake toolchain, in $T/ppbox-build-native; copied to app/src/main/jniLibs.
# 2. The app (android/app, Kotlin, Gradle): android/out/PeepeeBox-debug.apk, or with
#    "release" android/out/PeepeeBox-<version>-android.apk - not debuggable, signed
#    with the release key ($T/ppbox-release-signing.properties; see app/build.gradle.kts).
#
# The version is PeepeeBox's own release number, worked out as the top CMakeLists.txt
# does (the last v* tag, "+" when past it), or PEEPEEBOX_RELEASE when set (CI).  It goes
# to the library (log, About) and the app (versionName, versionCode) alike.
set -euo pipefail
CONFIGURE=0 RELEASE=0
for a in "$@"; do
    case "$a" in
        configure) CONFIGURE=1 ;;
        release) RELEASE=1 ;;
        *) echo "usage: build.sh [configure] [release]" >&2; exit 2 ;;
    esac
done
HERE=$(cd "$(dirname "$0")/.." && pwd)   # android/
ROOT=$(cd "$HERE/.." && pwd)             # the repository
. "$HERE/scripts/env.sh"
B=$T/ppbox-build-native
# The NDK has zlib but no zlib.pc, which freetype2.pc requires (PeepeeBox's printer
# finds FreeType through pkg-config): a stand-in for the sysroot's libz.
PC=$T/ppbox-pkgconfig
mkdir -p "$PC"
printf 'Name: zlib\nDescription: zlib (NDK sysroot)\nVersion: 1.3\nLibs: -lz\nCflags:\n' > "$PC/zlib.pc"
export PKG_CONFIG_LIBDIR=$T/deps/lib/pkgconfig:$PC PKG_CONFIG_PATH=
# The build-deps.sh headers, for sources that include them without linking the
# library's target (the desktop builds find them in /usr/include).
DEPS_INC="-isystem $T/deps/include -isystem $T/deps/include/freetype2"

rel=${PEEPEEBOX_RELEASE:-}
if [ -z "$rel" ]; then
    rel=$(git -C "$ROOT" describe --tags --abbrev=0 --match 'v[0-9]*' 2>/dev/null | sed 's/^v//' || true)
    if [ -n "$rel" ]; then
        git -C "$ROOT" describe --tags --exact-match --match 'v[0-9]*' > /dev/null 2>&1 || rel="$rel+"
    else
        rel=dev
    fi
fi
echo "PeepeeBox $rel"

# 1. The library.  A build directory configured for another source tree starts over;
#    a release number other than the one configured reconfigures.
if [ -f "$B/CMakeCache.txt" ] && ! grep -q "CMAKE_HOME_DIRECTORY:INTERNAL=$ROOT\$" "$B/CMakeCache.txt"; then
    rm -rf "$B"
fi
if [ -f "$B/CMakeCache.txt" ] && ! grep -qx "PEEPEEBOX_RELEASE:[A-Z]*=$rel" "$B/CMakeCache.txt"; then
    CONFIGURE=1
fi
if [ $CONFIGURE = 1 ] || [ ! -f "$B/build.ninja" ]; then
    cmake -S "$ROOT" -B "$B" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_FIND_ROOT_PATH="$T/deps" -DCMAKE_PREFIX_PATH="$T/deps" \
        -DCMAKE_C_FLAGS="$DEPS_INC" -DCMAKE_CXX_FLAGS="$DEPS_INC" \
        -DQT=OFF -DSTATIC_BUILD=OFF -DDYNAREC=ON -DOPENAL=ON \
        -DPEEPEEBOX_RELEASE="$rel"
fi
cmake --build "$B" --target 86Box
mkdir -p "$HERE/app/src/main/jniLibs/arm64-v8a"
cp "$B/src/libPeepeeBox.so" "$HERE/app/src/main/jniLibs/arm64-v8a/"

# 2. The app.
mkdir -p "$HERE/out"
if [ $RELEASE = 1 ]; then
    [ -f "$T/ppbox-release-signing.properties" ] || { echo "no release key ($T/ppbox-release-signing.properties)" >&2; exit 1; }
    (cd "$HERE" && ./gradlew --no-daemon -q -PpeepeeboxRelease="$rel" assembleRelease)
    cp "$HERE/app/build/outputs/apk/release/app-release.apk" "$HERE/out/PeepeeBox-$rel-android.apk"
else
    (cd "$HERE" && ./gradlew --no-daemon -q -PpeepeeboxRelease="$rel" assembleDebug)
    cp "$HERE/app/build/outputs/apk/debug/app-debug.apk" "$HERE/out/PeepeeBox-debug.apk"
fi
ls -la "$HERE/out/"
