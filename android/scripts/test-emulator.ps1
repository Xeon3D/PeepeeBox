<#
    PeepeeBox for Android - try the APK in the Windows Android emulator.

        pwsh android\scripts\test-emulator.ps1 [-Image <disk.img>[,<more.img>...]] [-Log]

    Boots the MegaPPBox_Android16 virtual device (Android 16, x86_64, in a window; it is
    MegaPPBox's, shared) unless it is already running, then does what deploy.ps1
    does: installs the APK, copies any -Image files into the app's folder, opens the
    Machine Manager.  Images already there stay (the virtual device keeps its storage).

    The APK is arm64: the x86_64 emulator runs it through Android's ARM translation.  Set up
    by Claude on 2026-09-30: SDK in %LOCALAPPDATA%\Android\Sdk, device in
    %USERPROFILE%\.android\avd.
#>
param([string[]]$Image, [switch]$Log)
$ErrorActionPreference = "Stop"
$emu = Join-Path $env:LOCALAPPDATA "Android\Sdk\emulator\emulator.exe"

if (-not (& adb devices | Select-String "^emulator-\d+\s+device")) {
    Write-Host "Starting the emulator..."
    Start-Process $emu -ArgumentList "-avd", "MegaPPBox_Android16", "-no-snapshot-save"
    & adb wait-for-device
    while ((& adb shell getprop sys.boot_completed 2>$null) -ne "1") { Start-Sleep -Seconds 2 }
    & adb shell settings put system screen_off_timeout 1800000
}

$args = @{}
if ($Image) { $args.Image = $Image }
if ($Log) { $args.Log = $true }
& (Join-Path $PSScriptRoot "deploy.ps1") @args
