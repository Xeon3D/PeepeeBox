PeepeeBox
=========

**PeepeeBox is a fork of [86Box](https://github.com/86Box/86Box) that emulates
one machine: a funworld Photo Play / I.G.O. arcade kiosk, including both of its
hardware protection tokens.**

The cabinets are gated by a funworld dongle on the parallel port *and* a Dallas
DS1982 iButton on a 1-Wire link, and they need both. PeepeeBox answers
both, so a completely unmodified disk image boots and runs with zero bytes
changed on disk — no patched executables, no software bypass.

Everything else about the machine is fixed, because on a real cabinet none of it
was ever a choice. See [`docs/hardware.md`](docs/hardware.md) for the full
picture and for how the protection works.

Credit
------

**All credit for the emulator itself belongs to the 86Box project and its
authors.** PeepeeBox is a small amount of arcade-specific hardware emulation and
a large amount of deletion on top of many years of someone else's work. See
[AUTHORS](AUTHORS).

The Photo Play protection research and this fork are by the **HUEG PP team**.

Released under the GNU General Public License version 2 or later, the same as
86Box. See [COPYING](COPYING).

Running it
----------

```
PeepeeBox.exe
```

Put `PeepeeBox.exe` in a folder with your `HardDisk.img` and run it. There is no
machine to pick, no disk to mount and no first-run wizard — it loads the image
sitting next to it and boots.

To keep a log of the protection exchange, which is the quickest way to see
whether both tokens answered:

```
PeepeeBox.exe -P . -L 86box.log
```

Which releases run
------------------

The cabinets changed their protection every year or two, so coverage is per
generation rather than all-or-nothing.

| Release | Dongle | Status |
|---|---|---|
| Photo Play 99 | funworld two-chip, parallel | runs, games and photo games |
| Photo Play 2000 | second funworld dongle (CDONGLE), parallel | runs, games and photo games |
| Photo Play 2001 / I.G.O. 1 | HASP4, parallel | boots and plays; **photo pictures still scramble** |
| I.G.O. 4 (2004) | CDONGLE, parallel | runs, games and photo games |
| I.G.O. 8 (2008) | serial smart-card reader on COM2 | runs, games and photo games |
| I.G.O. 2, 3, 5, 6, 7 | HASP4, parallel | untried |

I.G.O. 4 needs nothing of its own: it drives the same CDONGLE transport the 2000
generation uses, its pictures are stored unencrypted, and so it runs on the device
as it already stands.  Verified on `IGO 4 AT A0006`; the IT and PT images carry an
identical `MENU.EXE` and should behave the same.

2001's record is served correctly and its games run, but that generation encrypts
its photo archives with a cipher the dongle itself computes, which is not yet
emulated.

What you *can* change
---------------------

Four things, all under the **Tools** menu:

| Item | What it does |
|---|---|
| **Dongle…** | The version banner and territory the dongle reports, and whether the iButton is present. The banner must match `MAIN.SET["Version"]` for the image you are running. |
| **Network…** | Network card selection, as upstream. The cabinets are offline, but adding a NIC is harmless. |
| **CD-ROM drive** | Attaches a generic 52× ATAPI CD-ROM as secondary master. Off by default. |
| **Floppy drive** | Attaches a 3.5" 1.44 MB drive as A:. Off by default. |

With a CD-ROM or floppy attached, the **Media** menu gains the usual
new / existing image / eject actions for it.

Everything else — machine, CPU, RAM, video card, sound card, touchscreen, hard
disk — is fixed by the Photo Play profile in
[`src/photoplay.c`](src/photoplay.c), which is applied *after* the config file is
parsed. A stale or hand-edited `86box.cfg` cannot produce a machine that is not a
Photo Play cabinet.

What was removed
----------------

86Box is a general-purpose PC emulator covering four decades of hardware.
PeepeeBox needs one machine, so most of that is gone:

| | 86Box | PeepeeBox |
|---|---|---|
| Machines | ~900 | 1 (Zida Tomato 4DPS) |
| Chipsets | 80 | 1 (SiS 496) |
| Super I/O chips | 30 | 1 (Winbond W83787IF) |
| Video cards | 105 | 1 (Cirrus Logic CL-GD5480) |
| Sound cards | 111 | 1 (ESS ES1688) |
| Storage controllers | 40 | the SiS 496's own IDE |
| Settings dialog | 11 pages | replaced by the dongle dialog |

Also gone: ACPI, ZIP and magneto-optical drives, tape, cassette, cartridges, MIDI
(RtMidi, FluidSynth, MUNT, Sound Canvas), the Voodoo, and the built-in VM
manager. The executable is about 15% smaller and the source tree is roughly a
third of its former size.

Where a subsystem could not be cleanly separated, it stayed and is labelled as
such in the source and in the commit that kept it — the 8514/A and XGA hooks
inside the SVGA core, the Sound Blaster and Aztech mixers inside `snd_sb.c`, and
the floppy controller the Super I/O drives on-chip. Each of those is unreachable
at runtime; none of them is pretending to be needed.

Building
--------

Same toolchain as upstream 86Box. On Windows with MSYS2 (mingw64), Qt 5:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON -DUSE_QT6=OFF
```

```bash
ninja -C build
```

The build produces `build/src/PeepeeBox.exe`. The two ROM images it needs are
already in `roms/`.

Relationship to upstream
------------------------

This is a hard fork, not a branch. The first commit in this repository is the
unmodified 86Box source at upstream commit
[`5fc4619`](https://github.com/86Box/86Box/commit/5fc461926455c5643d95a947b6779cc60ccd3269)
(v7.0); diffing against that commit shows the complete set of PeepeeBox changes.
Because the machine set has been reduced to one, upstream changes cannot be
merged back in automatically.
