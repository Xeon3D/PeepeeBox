#!/usr/bin/env bash
# PeepeeBox for Android - the C libraries PeepeeBox links, built static for arm64-v8a
# with the NDK's CMake toolchain, into $T/deps (the prefix build.sh points at; the same
# libraries as MegaPPBox's, and shared with it locally).
#
#   bash android/scripts/build-deps.sh [name ...]     (default: all, in order)
#
# zlib comes with the NDK.  Left out on purpose: libslirp (needs glib; fun.net uses the
# switch, not slirp), SDL (Windows-only here), X11/Wayland/evdev (not on Android).
set -euo pipefail
. "$(dirname "$0")/env.sh"
API=28                                  # the app's minimum
P=$T/deps
SRC=$T/deps-src
mkdir -p "$P" "$SRC"

# name  url  extra-cmake-args
deps=(
  "zstd|https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz|build/cmake|-DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_SHARED=OFF -DZSTD_BUILD_TESTS=OFF"
  "libpng|https://download.sourceforge.net/libpng/libpng-1.6.50.tar.gz|.|-DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF"
  "freetype|https://download.savannah.gnu.org/releases/freetype/freetype-2.14.1.tar.gz|.|-DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_BROTLI=ON -DFT_DISABLE_PNG=ON -DFT_REQUIRE_ZLIB=ON"
  "libsndfile|https://github.com/libsndfile/libsndfile/releases/download/1.2.2/libsndfile-1.2.2.tar.xz|.|-DBUILD_PROGRAMS=OFF -DBUILD_EXAMPLES=OFF -DBUILD_TESTING=OFF -DENABLE_EXTERNAL_LIBS=OFF -DENABLE_MPEG=OFF -DENABLE_CPACK=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5"
  "openal-soft|https://github.com/kcat/openal-soft/archive/refs/tags/1.24.3.tar.gz|.|-DLIBTYPE=STATIC -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF -DALSOFT_BACKEND_OPENSL=ON -DALSOFT_REQUIRE_OPENSL=ON"
)

want=("$@")
for d in "${deps[@]}"; do
    IFS='|' read -r name url sub args <<< "$d"
    if [ ${#want[@]} -gt 0 ] && [[ ! " ${want[*]} " =~ " $name " ]]; then continue; fi
    echo "=== $name"
    tarball="$SRC/$(basename "$url")"
    [ -f "$tarball" ] || curl -fsSL -o "$tarball" "$url"
    sha256sum "$tarball"
    rm -rf "$SRC/$name" && mkdir "$SRC/$name"
    tar -xf "$tarball" -C "$SRC/$name" --strip-components=1
    # shellcheck disable=SC2086
    cmake -S "$SRC/$name/$sub" -B "$SRC/$name/build" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$P" \
        -DCMAKE_FIND_ROOT_PATH="$P" -DCMAKE_PREFIX_PATH="$P" \
        -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        $args > "$SRC/$name.configure.log" 2>&1 || { tail -30 "$SRC/$name.configure.log"; exit 1; }
    cmake --build "$SRC/$name/build" > "$SRC/$name.build.log" 2>&1 || { grep -m10 -i error "$SRC/$name.build.log"; exit 1; }
    cmake --install "$SRC/$name/build" > /dev/null
done
ls "$P/lib"
