# 8. Four things that look like dongle problems and are not

## 8.1 Photo Play 2.0 — Microcosm CopyControl

Photo Play 2.0 has **no dongle at all**. It is protected by Microcosm CopyControl
v1.66 build 94, and its key is the **physical layout of the disk** — where two files sit,
plus a pattern hidden in FAT cluster slack. Imaging a cabinet's disk file-by-file
destroys both, which is why every 2.0 image in circulation refuses to start a game.

Exactly 21 files in `\EXE\` carry a ~5.3 KB stub which also **encrypts the first ~0x978
bytes of the real program**, so there is no "serve the right answer" equivalent to the
dongle work — the original cannot run unless the check genuinely passes. `MAIN\MAIN.COM`
is unprotected, so the failure shows when a game is launched, not at boot.

Two traps that cost time: patching the stub's strings does nothing (the messages come
from the engine's own copy in `EXE\PP2000.081\PP2000.CCC`), and the licence region is
encrypted on disk and only decrypts in RAM.

All three checks pass on unmodified originals, and `tools/ppfix` repairs an affected
image. Nothing here belongs in a dongle device.

A practical consequence for the emulator's UI: a 2.0 image has no `MAIN.SET` to be
identified from and no dongle either, so a device that reports a fallback banner for it
is inventing a machine that is not there.

## 8.2 `src/device/hasp.c` is not this dongle

That file is stock 86Box: RichardG and Peter Ferrie's HASP emulation for *Savage Quest*,
based on the MAME driver. It has nothing to do with funworld's tokens, shares no
constants with them, and is not on any Photo Play code path. Do not extend it or read it
for clues.

## 8.3 `KEYN.COM` and `NSB.COM` are cracks, not documentation

`KEYN.COM` is a TSR that fakes the dongle in software by patching `int 2Bh` and replacing
the whole read-and-parse routine. What it serves is therefore the **parsed struct** the
routine would have produced — a 30-byte banner followed by eight dwords, 62 bytes — not
what the hardware holds. Reading its output as a record layout is what put every content
key 14 bytes late, twice. See `07.2`.

An image carrying `KEYN.COM`, `NSB.COM`, or an `AUTOPTS.BAT` that is not the stock 1417
bytes has been converted. Its passwords may also have been zeroed (`05.1`).

## 8.4 I.G.O. 4 is the odd generation

I.G.O. 4 links **no HASP library at all** — the marshalling signature is absent from all
43 of its executables, while every I.G.O. 2, 3, 5, 6 and 7 executable has it. The images
are not patched. All three I.G.O. 4 images carry the identical 267,616-byte `MENU.EXE`.

What it does have: eight dongle message names dispatched through a jump table on a
message code (the 2008 style, not the 2001 bitmask), the string
`PCXHeader_decode: DONGLE CODE ERROR`, and all three parallel bases as immediates
(`0x378`, `0x278`, `0x3BC`) so it probes the port itself.

But **its pictures are plain `GIF87a`**, so nothing on the image is encrypted with a
dongle code. Its own type is presumably `KDONGLE`, the newest letter in its list. Which
check it actually requires has not been established, and the cheapest way to find out is
to boot an image and read the screen.

For emulation purposes: I.G.O. 4 wants neither the 1999 device, nor CDONGLE, nor the
parallel HASP part. Leave the parallel HASP part **off** for it — driving STATUS bit 5
for a generation that does not use it disturbs the lines other things read.
