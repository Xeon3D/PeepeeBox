# Phase 9 — The 2008 generation uses a THIRD token: "NG-DONGLE"

Opened while trying to boot a pristine `IGO8ES-VM007` with no patching at all, the way the
1999 generation now boots (`Docs/07`).

## The image really is untouched

`Menu Patched Versions/IGO8ES-VM007.img` is, despite the folder name, the **original**:
all 63 executables under `/MENU` and `/EXE` are byte-identical to the Phase 0 forensic
extraction, and there is no `KEYN.COM`. Working copy: `analysis/p5/igo8test86/`.

## It is not the HASP, and not the DS1982

The guest reports:

```
COPY PROTECTION
NG-DONGLE not found
```

`MENU.EXE` carries a **table of eleven dongle types**, each with its own message
(file offsets in `analysis/p0/ex/IGO8ES-VM007/MENU/MENU.EXE`):

```
0x3DA8B  'dongle not found'        0x3DB06  'KDONGLE not found'
0x3DA9C  'CDONGLE not found'       0x3DB18  'MDONGLE not found'
0x3DAD0  'HDONGLE not found'       0x3DB2A  'MBDONGLE not found'
0x3DAE2  'GDONGLE not found'       0x3DB3D  'NDONGLE not found'
0x3DAF4  'IDONGLE not found'       0x3DB4F  'ODONGLE not found'
                                   0x3DB61  'NG-DONGLE not found'
0x3DB75  'wrong dongle version'
```

So the front end supports a whole family of tokens and this build is configured for **NG**.
The protection landscape is wider than the two tokens of `Docs/03`.

## The wire probe, captured live

First traffic ever recorded from a post-1999 generation, via PeepeeBox's raw LPT trace:

```
write_data FF
write_ctrl 00
write_data 5A   write_data 00   write_data A5   write_data 00
write_data FF
write_ctrl 00
write_data 5A   write_data 00   write_data A5   write_data 00
write_ctrl 08
write_ctrl 0C
read_status            <- ONE status read, then the verdict
```

Nothing like the 1999 transport: no STROBE framing, no nibble handshake, no BUSY/ack. It
writes a `0x5A` / `0xA5` complement pair interleaved with `0x00` (data/clock, most likely),
raises CTRL bit 2, and reads STATUS **once**. That single byte decides the verdict.

`0xA5` never appears as a literal in `MENU.EXE` (`mov al,0A5h`: zero occurrences), and the
only genuine `mov al,5Ah` there is a colour argument — the value is computed as `~0x5A`.
The probe code was not located; it is not reachable through literal constants.

## What already works on IGO8

**The DS1982 iButton emulation serves this generation unchanged** — the same 0x268 UART
device answered READ ROM, SKIP ROM and two READ MEMORY sequences correctly during the IGO8
boot. `Docs/05` predicted this ("the port constant is identical in all 12 generations") and
it is now confirmed on a 2008 image. One of the three tokens is already solved everywhere.

## Measured: the LPT probe is the ENTIRE interaction

With a raw trace on every parallel-port access, an untouched IGO8 image was booted, the
`NG-DONGLE not found` screen dismissed with **ESC**, and a game launched. Result:

```
LPT   : 10 write_data, 4 write_ctrl, 4 read_data, 1 read_status   <- unchanged from boot
iButton: READ ROM / SKIP ROM / READ MEMORY x2   ... then AGAIN when the game started
```

**Not one further byte reached the parallel port.** The NG-DONGLE is probed exactly once,
by `MENU.EXE` at boot, and the games never speak to it again. So unlike the 1999 HASP —
whose real structure (type byte, nonce, 48-byte block, length tables) only appeared when a
game ran — there is no richer NG conversation to capture. The presence probe is all there
is, and black-box observation is exhausted.

The **iButton, by contrast, is checked again by each game**, and the emulation serves it
correctly on this 2008 image. That is the token that actually matters at runtime here.

## Black-box search: exhausted, both axes

| Axis | Coverage | Result |
|---|---|---|
| STATUS byte | all 27 distinct values (`lpt_read_status` masks `& 0xF8`, so 256 tries were really 32) | every one fails |
| DATA readback transform | 20 candidates: identity, complement, 00, FF, nibble swap, shifts, masks, XOR patterns | every one fails |

Presence is therefore **not** a stateless function of what we put on either port.

### Two harness false positives, and what caused them

Both were timing, not protocol, and both are worth remembering:

1. An all-black capture of a half-initialised window was read as "not the red screen".
2. The funworld boot logo (`install = \foto\logo.com`, which runs *long* before any dongle
   check) was captured and read as success — twice.

The fix was to stop judging at a fixed moment and instead **wait up to 100s for the red
screen**, treating everything else as undecided; and to validate the classifier against
known-fail cases before trusting any sweep. Anything that never reaches red is now flagged
"needs review" rather than declared a pass.

## Practical status: the menu runs, the GAMES DO NOT

ESC dismisses the boot warning and `MENU.EXE` comes up correctly (right territory, right
language buttons). **But launching a game fails:**

```
error number: 228.353.33
in module   : TOWERS
language    : SPA
Wrong Version: ><
```

`Wrong Version: >%s<` **with an empty buffer** — the identical signature to the 1999
generation, and the identical failure mode `--mode menu` produced in `Docs/06`: the dongle
banner buffer is never filled, so the version check has nothing to match.

`TOWERS.EXE` carries the plain string `Version 2008` at file offset `0x2FA37`, which is the
substring it wants to find in that buffer — exactly as 1999 games look for `Version 99`.

### Why the games emit no LPT traffic

Because they never try. The boot probe fails, `MENU.EXE` records "no dongle", and the games
skip the read entirely and run the version check against an empty buffer. That is why ESC
gets to the menu but not into a game.

So the boot presence probe is not a cosmetic warning after all — it is **the** gate.
Satisfying it should also unlock the banner read, and that read is where the NG protocol
would finally become observable.

### The iButton side is confirmed correct

`TOWERS.EXE` holds the XOR-0x7C constant at `0x2FA50` decoding to
`Photo Play 2000 Version 3` — byte-for-byte what `Docs/05` recovered from IGO6 and exactly
what the emulated page serves. No DS1982 error appears anywhere in the 2008 run, and each
game re-reads the iButton successfully. That token is solved across generations.

## Checked: the NG token IS on LPT1 (hypothesis falsified)

Before more reverse engineering, the load-bearing assumption was tested directly. PeepeeBox
gained a deduplicated log of every I/O port **nothing** answers (`io.c`, enabled by the
device's *"Log unclaimed I/O ports"* option). If the guest were probing hardware we never
attached, every value fed to our device would have been answering the wrong thing.

It is not. The result:

- `0378` / `037A` do appear as unclaimed — but at **log lines 72-74, before** the device's
  first traffic at line 80. That is the BIOS POST parallel-port scan, before the LPT
  handler goes live. Every byte of the actual probe reaches our device.
- After the probe fails, the guest scans `0x203, 0x207, 0x20B ... 0x2FF` — one read every
  4 ports, i.e. `base+3` (LCR) for each candidate UART base. That is the **1-Wire adapter
  search**, and it visibly skips `0x268-0x26F` because our iButton claims that range.
- Nothing else in the scan looks like a protection token: `0279`/`0A79` are ISA PnP,
  `0300/0301` and `0210-0213` are the usual sound/game-port probes.

So the premise holds: the NG dongle is on the parallel port, our device receives its probe,
and the uniform failure across every STATUS value and every readback transform is a real
property of the check -- not an artefact of answering the wrong hardware.

That leaves **static reverse engineering of the probe code** as the only remaining route.
`5A`/`A5` are not literals in `MENU.EXE` (`0xA5` appears zero times; the one real
`mov al,5Ah` is a colour argument), so the pattern is built arithmetically or lives in an
overlay. That is a proper RE job, comparable in size to the 1999 work in `Docs/07`.

## Status

**IGO8 cannot boot unpatched today.** `--mode keyn` remains the answer for this generation.

The encouraging part is how small the remaining gate is: detection is a single STATUS read,
so if the expected value were known, the device change would be a few lines. Two ways to
get it:

1. **Find the probe code.** It is not in `MENU.EXE`'s literals; look for overlays, or a
   library routine building the pattern arithmetically.
2. **Brute-force it live.** `AUTOPTS.BAT` re-runs the menu in a `:restart` loop, so each
   failure produces another probe. Returning a different STATUS byte per probe and watching
   for the guest to proceed would sweep all 256 values without user involvement — the
   touchscreen calibration is already saved in the working image, so reboots go straight to
   the check.

Option 2 is cheap and needs no further reverse engineering; it is the obvious next move.
