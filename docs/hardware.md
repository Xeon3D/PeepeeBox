# The hardware PeepeeBox emulates

PeepeeBox emulates one machine: a funworld **Photo Play / I.G.O.** arcade kiosk.
Nothing about it is configurable, because nothing about it was ever a choice —
these are the parts the cabinets shipped with, and any deviation is a bug rather
than a preference.

| Part | What it is | Why it is that |
|---|---|---|
| Motherboard | Zida Tomato 4DPS (SiS 496/497, PCI) | The board in the cabinets. BIOS `4DPS172G.BIN`. |
| CPU | Intel iDX4 at 100 MHz (3 × 33 MHz) | Socket 3, 5 V. |
| RAM | 16 MB | |
| Video | Cirrus Logic CL-GD5480 (PCI) | The games' VESA mode selection assumes it. |
| Sound | ESS ES1688 AudioDrive | What the games' drivers talk to. |
| Input | 3M MicroTouch TouchPen on **COM3** | Move it and touch input dies silently. |
| Disk | `HardDisk.img` beside the executable, IDE primary master | Geometry derived from file size. |
| LPT1 | Photo Play protection dongle | See below. |
| I/O card | funworld ISA card: NEC D71055C (8255) at **0x210** | The coin acceptor and the two buttons behind the door. See [`research/27-io-card.md`](research/27-io-card.md). |

Two things are optional, because service and installation media exist even though
no cabinet shipped with a drive. Both are off by default and both are toggled
from the **Tools** menu:

- A generic **52× ATAPI CD-ROM** as secondary master.
- A **3.5" 1.44 MB floppy** as drive A:.

Neither is configurable beyond existing. Toggling either restarts the machine,
because it changes what hardware is present. The setting lives in a
`[Photo Play]` section of `86box.cfg`.

## The disk

PeepeeBox always loads `HardDisk.img` from its own directory. There is no file
picker and no path setting.

The geometry is **not** hardcoded. Images in circulation are 1.6 GB, 3.0 GB and
4.3 GB, so a fixed cylinder count would break most of them. Instead the file size
is read at load time and cylinders derived from it. Sectors-per-track and heads
are fixed at 63/16 because that is the geometry the images' MBRs and FAT16 boot
records were written under, and PTS-DOS still addresses by CHS.

If `HardDisk.img` is missing, no disk is attached and the log says so. PeepeeBox
deliberately does not fall through to 86Box's create-on-open path, which would
silently produce a blank multi-gigabyte image and boot to a dead machine — a
missing disk should not look like a corrupt one.

## The protection

This is the reason PeepeeBox exists. The kiosks are gated by **two independent
hardware tokens**, and both must answer before the menu or any game will run.
Emulating only one changes nothing: with the iButton absent, every game aborts
with `DS1982 FAILED` regardless of what the other one says.

### 1. The funworld dongle, on the parallel port

**Not a HASP.** Five of these were dumped and their firmware disassembled and
executed (`docs/research/12`): it is funworld's own two-chip design — an
AT89C2051-class 8051 plus a 24Cxx I²C EEPROM holding the licence record. Aladdin
is not involved in any generation (`docs/research/13`); "H" is one of ten dongle
types the front end learned to probe for. The 1999 games bit-bang LPT inline.

- **Host → dongle:** two nibbles per byte on DATA 0–3, each latched on a STROBE
  rising edge.
- **Dongle → host:** two nibbles per byte on STATUS 3–6, with STATUS 7 (BUSY) as
  the ready flag and DATA bit 4 as the host acknowledgement.

Every transaction opens with a type byte; two four-entry tables in the game's
data segment give the send and receive lengths. The boot path uses **type 3**:
the host sends `{03, nonce}` and the dongle must return a 48-byte block XORed
under a keystream seeded with that nonce. The nonce is drawn at random per
transaction, so this is a genuine challenge/response — a recorded exchange cannot
be replayed.

The 48-byte block is `char banner[16]; uint32 v[8]` — the exact record the real
EEPROMs hold, so PeepeeBox serves a block byte-identical to a dumped dongle. Six
of the dwords are funworld's fixed per-title keys: each photo game reads one and
uses it as the LCG seed that decrypts its picture database
(`docs/research/14`). The banner is string-matched by the guest.
It must equal `MAIN.SET["Version"]` for the image being run, which differs per
image and per territory, so banner and territory stay selectable — that is what
the **Tools → Dongle** dialog is for. Get it wrong and the game reports
`Wrong Version`.

### 2. The Dallas DS1982 iButton, on a UART at I/O 0x268

1-Wire over a UART, per Dallas application note AN214, with the standard Maxim
CRC8. PeepeeBox implements READ ROM, SKIP ROM and READ MEMORY.

### Verifying it works

`86box.log` records the whole conversation:

```
PP: Photo Play dongle attached, banner "Version 99 (AT)"
IB: DS1982 iButton at I/O 268, ROM 09 50 50 42 4F 58 00 FF
PP: host->dongle 03 / 89
PP: type 3, nonce 89 -> 48 encrypted bytes
PP: *** host drained all 96 nibbles (48 bytes) ***
IB: READ ROM / SKIP ROM / READ MEMORY ...
```

A **different nonce each boot is expected and is the point** — it is what makes
the exchange a real challenge/response rather than a replayed recording.

## Where this came from

`docs/research/` holds the reverse-engineering notes the emulation is built on,
in the order they were written. They contradict each other in places, on purpose:
each one records what was believed at the time and later ones say plainly where
that turned out to be wrong. The two that matter most for the code here are
`07-hasp-wire-protocol.md` and `05-ds1982-protocol.md`.
