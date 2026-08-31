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
generation. Passwords and record layouts below come from h5dmp dumps of nine real
dongles (`docs/research/20`), not from inference.

| Release | Dongle | Pictures | Status |
|---|---|---|---|
| Photo Play 99 | funworld two-chip, parallel | encrypted, per-picture key | **runs** — games and photo games |
| Photo Play 2000 | CDONGLE, parallel | encrypted, per-picture key | **runs** — games and photo games |
| Photo Play 2001 / I.G.O. 1 | HASP4 `7477/7D57` | encrypted, dongle-computed | boots and plays; **pictures scramble** |
| I.G.O. 2 (2002) | HASP4 `68BB/1329` | encrypted, dongle-computed | untried; expect the 2001 wall |
| I.G.O. 3 (2003) | HASP4 `6B91/24A3` | encrypted, dongle-computed | **fails at boot** — needs the cipher to start |
| I.G.O. 4 (2004) | CDONGLE, parallel | plain GIF | **runs** — games and photo games |
| I.G.O. 5 (2005) | HASP4 `6B91/24A3` | plain GIF | menu and photo games run; **menu buttons garbled** |
| I.G.O. 6 (2006) | HASP4 `68BB/1329` | plain GIF | untried; record known, expect I.G.O. 5's result |
| I.G.O. 7 (2007) | HASP4 `68BB/1329` | plain GIF | **record accepted, games launch** — screen not yet checked |
| I.G.O. 8 (2008) | serial reader, COM2 | plain GIF | **runs** — games and photo games |
| I.G.O. Italy (2008-era) | HASP4, parallel | — | reads the record and rejects it; **passwords unknown** |

Two things gate the rest.

**The record**, which the device now serves for every HASP generation: 2001 uses a
30-column banner and decimal text fields, while I.G.O. 2 and 3 use one structured
form and 5, 6 and 7 another, each with eight binary content dwords. That work is
done, and it is what brought up I.G.O. 5.

I.G.O. Italy is blocked on evidence rather than analysis: its banner (`Version 08IT
(IT)`) matches no dongle we have, every Italy image carries `0000/0000` passwords --
the neutered pattern -- and there is no 2008 parallel dongle among the nine dumped.
It reads the record over the wire and rejects it, so only the key and layout are
missing.

**The picture cipher**, which is not.  2001 through I.G.O. 3 encrypt their photo
archives with a cipher the dongle itself computes -- the library shifts a byte out
and reads one bit back, forty times per eight bytes. funworld stopped encrypting
pictures from I.G.O. 4 on, which is why the later generations need only the record.
I.G.O. 3 is the worst case: it asks the dongle to encrypt 20 bytes before it will
boot at all.

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
