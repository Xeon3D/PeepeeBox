# PeepeeBox for Android

PeepeeBox, the funworld Photo Play / I.G.O. cabinet emulator (on 86Box), as a native
Android app. It runs untouched Photo Play disk images on an arm64 phone or tablet, both
protection tokens answered, using the emulator's arm64 dynamic recompiler. It is built from
this repository, with the same emulator as the Windows, Linux and macOS versions, and
released with them.

## Installing

Download `PeepeeBox-android-arm64.apk` from the
[Releases](https://github.com/Xeon3D/PeepeeBox/releases) and open it on the device (allow
installing from your browser or file manager when asked). It needs a 64-bit ARM device with
Android 9 or later. Samsung's **Auto Blocker** (Settings > Security and privacy) blocks app
installs from outside the stores: turn it off to install, and back on afterwards if you like.

The **Machine Manager** lists the images in the app's folder, identified from their contents
(release, territory, NSB); a tap runs one, with the dongle answering for what the image says
it is. **Import** copies an image in.

## The cabinet's controls

The cabinet shows the 4:3 picture with Credit, Setup, Manager, Fullscreen and More (Coins &
notes, Network, Modem, Pause, Reset, About, Exit).

- **Credit**: one 1.00 EUR coin. A long press (or More → Coins & notes) offers all ten money
  lines the cabinet takes: six coins and four notes, as the desktop toolbar has them.
- **Setup**: the operator button behind the door.
- **Network**: the RTL8139 an image with the Ethernet option reaches fun.net through, on the
  local switch or a remote one. Off by default: no cabinet had one.
- **Modem**: none, or one of the two parts the fun.net cabinets had on COM4 (ELSA MicroLink
  56k, Diamond SupraExpress 56e PRO). A dial connects to a TCP host, such as a
  [fun.net stand-in](https://github.com/Xeon3D/fun.net-server) (port 23).

There is no Calibrate: Photo Play cabinets have no such button. The desktop's other Tools
(the dongle's banner, touchscreen part, fun.link, the receipt printer's window, CD-ROM and
floppy drives) are not in the app; the dongle is set from the image, as the Machine
Manager does on the desktop, and the rest keep PeepeeBox's defaults.

## What is here

| Path | What |
|---|---|
| `native/` | the Android platform layer, with no Qt: platform functions, and the JNI bridge (emulation thread, frames into a `SurfaceView`, touch, the funworld I/O card's lines, image identification, network card and modem). The top CMake builds it in place of the Qt front end when the target is Android (`if(ANDROID)`), making the emulator a shared library, `libPeepeeBox.so`, without libslirp and with OpenAL Soft on OpenSL ES |
| `app/` | the app, in Kotlin with Android framework widgets: the Machine Manager and the cabinet |
| `scripts/` | toolchain, libraries, build (Linux or WSL), deploy and the Windows Android emulator (PowerShell and adb) |

## Building

On Linux or in WSL (Ubuntu), everything user-local under `~/mpb-android` (shared with
MegaPPBox, whose toolchain and libraries are the same; this app's own files there are named
`ppbox-*`):

```sh
git clone https://github.com/Xeon3D/PeepeeBox.git
cd PeepeeBox
bash android/scripts/setup-toolchain.sh jdk
bash android/scripts/setup-toolchain.sh sdk
bash android/scripts/setup-toolchain.sh pkgs   # accepts the Android SDK licences
bash android/scripts/build-deps.sh             # zstd, libpng, FreeType, libsndfile, OpenAL Soft
bash android/scripts/build.sh                  # -> android/out/PeepeeBox-debug.apk
bash android/scripts/build.sh release          # -> android/out/PeepeeBox-<version>-android.apk
```

The version is PeepeeBox's release number, from the last `v*` tag as for the desktop builds.
The release build is signed with a key kept outside the repository
(`~/mpb-android/ppbox-release.keystore` and `ppbox-release-signing.properties`; on GitHub,
the release workflow takes them from the repository's secrets). A debug build and a release
build have different signatures, so one cannot be installed over the other: uninstalling
first deletes the app's folder and the images in it.

Then, on Windows, with a phone connected (USB debugging on):

```powershell
pwsh android\scripts\deploy.ps1 -Image "D:\Photo Play\IGO6\HardDisk.img"
```

Images live in the app's folder, `Android/data/io.github.xeon3d.peepeebox/files`. Put them
there with `deploy.ps1 -Image` or the Machine Manager's Import; an image called
`HardDisk.img` arrives under a name of its own (its rig folder's with `deploy.ps1`, what it
identifies as with Import). The emulator opens images with plain file calls, which Android
only allows there. Images can also be tried in the Windows Android emulator with
`android\scripts\test-emulator.ps1`.

The app ships the repository's `roms/` and its seeded `nvr/` (the cabinet's settled CMOS: a
blank one stops the BIOS on a hard disk error). The nvram is copied only where missing, so
the cabinet's own is kept from then on.
