<#
    PeepeeBox - package a runnable build folder.

    Every time something changes, this drops a self-contained folder next to the
    others so it can be run without rebuilding anything:

        PeepeeBox-builds\
            HardDisk.img                  <- the master disk image, one copy
            NN-short-description\
                PeepeeBox.exe
                roms\
                HardDisk.img              <- hard link to the master
                run.cmd
                BUILD.txt

    The disk image is *hard linked*, not copied.  A copy would be 1.6 GB per
    build and there is not room for many of those; the link costs nothing.  The
    consequence is that every build folder shares one disk, so whatever the guest
    writes carries across versions -- which is usually what you want when
    comparing builds, but means they are not isolated.  For an independent disk,
    replace the link in a folder with a real copy:

        del  NN-whatever\HardDisk.img
        copy HardDisk.img  NN-whatever\HardDisk.img

    Usage:
        pwsh scripts\make-build.ps1 "menus-trimmed"
#>
param(
    [Parameter(Mandatory = $true)][string] $Name,
    [string] $Root = "C:\Users\xeon4\Documents\Claude\PeepeeBox-builds",
    [string] $Repo = "C:\Users\xeon4\Documents\Claude\PeepeeBox"
)

$ErrorActionPreference = "Stop"

$exe = Join-Path $Repo "build\src\PeepeeBox.exe"
if (-not (Test-Path $exe)) { throw "No build at $exe - build first." }

$master = Join-Path $Root "HardDisk.img"
if (-not (Test-Path $master)) { throw "No master disk image at $master." }

# Number the folder so they sort in the order they were made.
$next = 1
if (Test-Path $Root) {
    $existing = Get-ChildItem $Root -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^(\d+)-' } |
                ForEach-Object { [int]($_.Name -split '-')[0] }
    if ($existing) { $next = ($existing | Measure-Object -Maximum).Maximum + 1 }
}
$dir = Join-Path $Root ("{0:d2}-{1}" -f $next, $Name)
New-Item -ItemType Directory -Force -Path $dir | Out-Null

Copy-Item $exe $dir -Force
Copy-Item (Join-Path $Repo "roms") $dir -Recurse -Force
New-Item -ItemType HardLink -Path (Join-Path $dir "HardDisk.img") -Target $master | Out-Null

@"
@echo off
rem Run this build.  The log records the protection exchange; check it if a
rem game complains about the dongle.
cd /d "%~dp0"
start "" "%~dp0PeepeeBox.exe" -P . -L 86box.log
"@ | Set-Content (Join-Path $dir "run.cmd") -Encoding ASCII

$commit  = (& git -C $Repo rev-parse --short HEAD).Trim()
$subject = (& git -C $Repo log -1 --pretty=%s).Trim()
@"
PeepeeBox build: $Name

Built    : $(Get-Date -Format 'yyyy-MM-dd HH:mm')
Commit   : $commit
           $subject

Run it   : double-click run.cmd, or PeepeeBox.exe directly.

HardDisk.img is a hard link to the shared master one level up, so the guest's
disk state is shared with every other build folder.  Replace it with a real copy
if you want this build to have its own disk.
"@ | Set-Content (Join-Path $dir "BUILD.txt") -Encoding UTF8

Write-Host "packaged -> $dir"
