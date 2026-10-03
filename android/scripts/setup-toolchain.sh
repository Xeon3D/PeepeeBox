#!/usr/bin/env bash
# PeepeeBox for Android - the build toolchain, user-local under ~/mpb-android (no root
# needed; shared with MegaPPBox's; CI uses the runner's SDK and JDK instead, see env.sh).
#
#   bash android/scripts/setup-toolchain.sh jdk|sdk|pkgs
#
#   jdk  Amazon Corretto 21 (the Gradle 9.3 wrapper does not run on Java 25)
#   sdk  Android command-line tools
#   pkgs accepts the Android SDK licences (the user agreed, 2026-09-30) and installs what
#        the app needs: platform android-36, build-tools 36.1.0, platform-tools,
#        NDK 27.2.12479018
set -euo pipefail
. "$(dirname "$0")/env.sh"
mkdir -p "$T"
case "${1:-}" in
jdk)
    curl -fsSL -o "$T/jdk21.tgz" https://corretto.aws/downloads/latest/amazon-corretto-21-x64-linux-jdk.tar.gz
    rm -rf "$T/jdk21" && mkdir "$T/jdk21" && tar -xzf "$T/jdk21.tgz" -C "$T/jdk21" --strip-components=1
    rm "$T/jdk21.tgz"
    "$T/jdk21/bin/java" -version
    ;;
sdk)
    # Newest command-line tools package, from Google's own repository index.
    zip=$(curl -fsSL https://dl.google.com/android/repository/repository2-3.xml |
          grep -o 'commandlinetools-linux-[0-9]*_latest.zip' | sort -V | tail -1)
    curl -fsSL -o "$T/clt.zip" "https://dl.google.com/android/repository/$zip"
    rm -rf "$T/sdk/cmdline-tools" && mkdir -p "$T/sdk/cmdline-tools"
    python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])" "$T/clt.zip" "$T/sdk/cmdline-tools"
    mv "$T/sdk/cmdline-tools/cmdline-tools" "$T/sdk/cmdline-tools/latest" && rm "$T/clt.zip"
    chmod +x "$T/sdk/cmdline-tools/latest/bin/"*
    echo "command-line tools: $zip"
    ;;
pkgs)
    sm() { "$SDK/cmdline-tools/latest/bin/sdkmanager" --sdk_root="$SDK" "$@"; }
    yes | sm --licenses > /dev/null || true   # yes(1) dies of SIGPIPE
    sm "platform-tools" "platforms/android-36" "build-tools/36.1.0" "ndk/27.2.12479018" | grep -v "^\[" | tail -3
    ls "$SDK" "$SDK/ndk"
    ;;
*) sed -n 2,15p "$0"; exit 1 ;;
esac
