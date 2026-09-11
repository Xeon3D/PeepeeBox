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

Unpack the download, then drop your `HardDisk.img` into that folder — the one
holding `PeepeeBox.exe`, alongside the `roms` and `nvr` folders that came with
it. Do not move the executable out to sit next to an image somewhere else; it
needs the whole folder:

```
PeepeeBox.exe
roms\
nvr\
HardDisk.img      <- yours, dropped in here
```

Now run `PeepeeBox.exe`. There is no machine to pick and no disk to mount — it
loads the image sitting next to it and boots.

PeepeeBox is always portable, and keeps everything in one file. `86box.cfg` is
written into that same folder and holds both halves -- the machine under
`[General]`, `[Machine]` and the rest, and your settings under `[Emulator]`,
`[Input]` and `[Keybinds]`. Nothing is written anywhere else on the machine, so
two folders holding two releases keep two sets of settings and neither can
surprise the other. Copy the folder and everything goes with it; delete it and
nothing is left behind.

A folder from an older build that still has a separate `86box_global.cfg` has
its settings read across once, into `86box.cfg`. The old file is then ignored,
and can be deleted.

The Machine Manager
-------------------

The first start asks one question: whether to use the Machine Manager. It is off
by default, the question is put once, and the Machine Manager page in Preferences
changes the answer and sets the folder later.

Switched on, it adds a button at the left of the toolbar. **Scan for HDD images**
walks the folder you pointed it at and asks every `.img` it finds what it is —
release, territory and the raw `Version` line, read out of the image's own
`\FOTO\SETTINGS\MAIN.SET` rather than guessed from the folder name, which is
worth knowing because folder names in circulation are often wrong. The result is
saved, so the window opens on the list next time; the button then reads
**Update list** and scans again over the top.

**Double-click a release to run it.** That image goes into the drive and the
cabinet hard resets onto it. The pick lasts for the run — hard resets included —
and is not written anywhere, so the next launch starts on nothing again and a rig
folder carrying its own `HardDisk.img` goes on booting that one.

Which releases run
------------------

The cabinets changed their protection every year or two, so coverage is per
generation. Passwords and record layouts below come from h5dmp dumps of nine real
dongles (`docs/research/20`), not from inference.

| Release | Dongle | Pictures | Status |
|---|---|---|---|
| Photo Play 2.0 | Microcosm CopyControl (disk layout) | plain PCX | **runs** — games and photo games |
| Photo Play 99 | funworld two-chip, parallel | encrypted, per-picture key | **runs** — games and photo games |
| Photo Play 2000 | CDONGLE, parallel | encrypted, per-picture key | **runs** — games and photo games |
| Photo Play 2001 / I.G.O. 1 | HASP4 `7477/7D57` | encrypted, dongle-computed | boots and plays; key solved, **pictures not retested yet** |
| I.G.O. 2 (2002) | HASP4 `68BB/1329` | encrypted, dongle-computed | **runs** — games and photo games |
| I.G.O. 3 (2003) | HASP4 `6B91/24A3` | encrypted, dongle-computed | **fails at boot** — cipher solved, the check still refuses |
| I.G.O. 4 (2004) | CDONGLE, parallel | plain GIF | **runs** — games and photo games |
| I.G.O. 5 (2005) | HASP4 `6B91/24A3` | plain GIF | menu and photo games run; **menu buttons garbled** |
| I.G.O. 6 (2006) | HASP4, probed | plain GIF | **runs** — games and photo games |
| I.G.O. 7 (2007) | HASP4 `68BB/1329` | plain GIF | **runs** — games and photo games |
| I.G.O. 8 (2008) | serial reader, COM2 | plain GIF | **runs** — games and photo games |
| I.G.O. Italy (2008-era) | HASP4, probed | plain GIF | **runs** — games and photo games |
| Photo Play Junior 1.5 / Touchtoy | CDONGLE, parallel, with the DS1982 iButton | n/a — no photo game | **runs** — all six games |

Junior is the small cabinet rather than a generation of the big one: six games
(`ZEICHNEN, SIMON, PUZZLE, MEMORY, FINDIT, ANMALEN`), no photo game, and so
nothing that needs the picture cipher. It is a Photo Play through and through --
PTS-DOS, `\MENU\MENU.EXE`, `\MENU\NSB.NR`, its release named in
`\FOTO\SETTINGS\MAIN.SET`, and a MicroTouch on COM3 -- so the profile and the
token are the ordinary ones. Three images run: `Junior 1.5 (NL)` NSB 8A5B,
`Junior 1.5 (DE)` NSB E239, and `Junior 1.5 (IL)` NSB TT01, the last branded
Touchtoy. Some of them are filed under Funny's, who distributed them; they are
not the Funny Interactive cabinet, which this emulator does not run.

Photo Play 2.0 is the odd one out: no dongle at all.  Its games are wrapped in
Microcosm CopyControl, whose key is the **physical layout of the disk** -- where
two files sit, and a pattern hidden in the slack past the end of one of them.
Copying a cabinet's disk file-by-file destroys both, which is why the images that
survive refuse to start a game.  `ppfix.exe`, which ships beside the emulator,
puts the layout back on request without altering a single game file, and stamps
the image so PeepeeBox can tell a repaired one from a copy that will fail and say
so instead of letting the games mystify you (`docs/research/24`).

Two things gate the rest.

**The record**, which the device serves for every HASP generation. Three shapes:
2001 holds a 30-column banner and decimal text fields; I.G.O. 2 and 3 hold a
territory and little else, because those builds supply the version text from their
own literal and read only two characters and a NUL off the dongle; 5, 6, 7 and
Italy hold a territory, `"Version"` and a token, formatted with `"%s %s (%c%c)"`.
All three carry eight binary content dwords. That work is done, and it is what
brought up I.G.O. 2, 5, 6, 7 and Italy.

Two releases — I.G.O. 6 and Italy — do not carry their passwords as literals.
`MENU.EXE` tries `7477/7D57`, falls back to `68BB/1329`, and zeroes both if neither
answers service 5. A real HASP discriminates there; a synthesised part cannot, so
the probe always reaches that last branch and the descramble key is `0x0000`. The
device serves the key the guest will use, and keeps the dumped password on record
(`docs/research/21`).

**The picture cipher**, which used to be the wall and is now solved.  2001 through
I.G.O. 3 encrypt their photo archives with a cipher the dongle itself computes --
the library shifts a byte out and reads one bit back, forty times per eight bytes.
PeepeeBox computes that round itself.  The keyed step is a small shift register
whose only secret is 32 bits, and each generation's 32 bits were fitted from
archives whose plaintext ships in *another* release -- I.G.O. 4 carries in the
clear what I.G.O. 2 and 3 carry enciphered, and Photo Play 2000 does the same for
2001 -- so no dongle was needed to recover any of them (`docs/research/31`).

The check on that is I.G.O. 2's: its key was fitted from I.G.O. 4's plaintext
alone, then run against all **46,036 rounds a real dongle answered** over a
passed-through parallel port.  It agrees on every one.  I.G.O. 2 now plays FIND IT
with its photographs decrypting, which is what that table row means.

funworld stopped encrypting pictures from I.G.O. 4 on, which is why the later
generations need only the record.

I.G.O. 3 is still the worst case: it asks the dongle to encrypt 20 bytes before it
will boot at all.  It now *asks* -- where before it never got that far -- and is
refused.  The round itself is not the suspect, since that block comes out as
`c:/foto/` under the fitted key; what is unverified is the session exchange around
it, which has only ever been measured on a `68BB/1329` part while I.G.O. 3 is
`6B91/24A3` (`docs/research/32`).

What you *can* change
---------------------

Six things, all under the **Tools** menu:

| Item | What it does |
|---|---|
| **Dongle…** | The version banner and territory the dongle reports, and whether the iButton is present. The banner must match `MAIN.SET["Version"]` for the image you are running. |
| **Touchscreen…** | Which part is fitted — a 3M MicroTouch or an Elo SmartSet — and its port, IRQ and speed. The cabinets shipped a MicroTouch on COM3, IRQ 4, 9600 baud, and that is the default. Move it and touch stops working with nothing on screen saying so. |
| **Modem…** | Which modem is fitted to COM4, at 0x2E8 on IRQ 10 — an ELSA MicroLink 56k or a Diamond SupraExpress 56e PRO, the two parts the fun.net cabinets are found with — and what its telephone line is attached to. None by default; the line is dead unless you point it at a TCP host. |
| **Network…** | Network card selection, as upstream. The cabinets are offline, but adding a NIC is harmless. |
| **CD-ROM drive** | Attaches a generic 52× ATAPI CD-ROM as secondary master. Off by default. |
| **Floppy drive** | Attaches a 3.5" 1.44 MB drive as A:. Off by default. |

With a CD-ROM or floppy attached, the **Media** menu gains the usual
new / existing image / eject actions for it.

Everything else — machine, CPU, RAM, video card, sound card, hard disk — is
fixed by the Photo Play profile in
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
already in `roms/`, and `nvr/` holds a settled CMOS -- copy both in beside the
executable, or the 4DPS BIOS comes up with no drive configured and stops on a
hard disk error.

Relationship to upstream
------------------------

This is a hard fork, not a branch. The first commit in this repository is the
unmodified 86Box source at upstream commit
[`5fc4619`](https://github.com/86Box/86Box/commit/5fc461926455c5643d95a947b6779cc60ccd3269)
(v7.0); diffing against that commit shows the complete set of PeepeeBox changes.
Because the machine set has been reduced to one, upstream changes cannot be
merged back in automatically.
