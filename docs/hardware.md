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
| COM4 | An external modem, IRQ 10 | Optional — only the cabinets on fun.net had one. See below. |
| COM1 | The **fun.link** adapter, IRQ 4 | Optional — the bus that joins cabinets together. See below. |

Four things are optional. All are off by default, all are switched on from the
**Tools** menu, and all restart the machine when changed, because each changes
what hardware is present. Their settings live in a `[Photo Play]` section of
`86box.cfg`.

- A generic **52× ATAPI CD-ROM** as secondary master. No cabinet shipped with a
  drive, but service and installation media exist.
- A **3.5" 1.44 MB floppy** as drive A:, for the same reason.
- The **modem on COM4** — an ELSA MicroLink 56k or a Diamond SupraExpress 56e
  PRO, the two parts the cabinets on fun.net are found with.
- The **fun.link adapter on COM1**, which joins this cabinet to others.

None of them is configurable beyond existing, except the modem and fun.link:
the modem's telephone line is a choice because there is no longer a telephone
network to attach it to, and fun.link's is a choice because the cabinet at the
other end is now another copy of PeepeeBox somewhere.

## The modem

Cabinets on **fun.net** — funworld's dial-up network for statistics, updates and
the online games — had an external modem on COM4. Which one is not a guess: the
disk carries funworld's own modem database in `\FN_SYS\DATABASE\NETWORK\`, and
the operator shell identifies the fitted part by sending the `ATI` command that
table names and looking for the substring beside it. Two of its rows are the
parts these machines turn up with, and PeepeeBox emulates both:

| | recognised by | firmware from |
|---|---|---|
| **ELSA MicroLink 56k** (row 2) | `MicroLink` and `56`, both in `ATI6` | `ATI3` |
| **Diamond SupraExpress 56e PRO** (row 3) | `SupraExpress` in `ATI3` | `ATI7` |

| | |
|---|---|
| Port | **COM4 at 0x02E8, IRQ 10** — not the PC-standard 3 |
| Line | 57600 8N1, RTS/CTS |
| Dial | `ATDT`, then Novell ODI PPP and WATTCP on top |

The port and interrupt come from real dial sessions: two images — a 2001 NL
machine and a 2006 DE one — still carry the `NET.CFG` their last connection
wrote, and both say `PORT 02E8` / `INT 10` / `BAUD 57,600`. All three are
literals in `FN_SYS.EXE`, so they are the same on every cabinet. The IRQ matters for the same reason COM3's does — the PPP
driver installs an ISR on the interrupt that file names and then stops polling,
so a modem on IRQ 3 would identify perfectly and then die the moment the link
came up.

**There is nothing to dial.** fun.net is gone, so the emulated line is dead by
default: the cabinet finds the modem, reports what it is, accepts every init
string in its tables, dials, and gets the failure a real modem would give into a
dead socket. **Tools → Modem…** can point it at a TCP host instead, in which case
dialling anything connects there and the modem becomes a transparent pipe —
which is what would be needed to stand a fun.net replacement up.

Full detail, including where every `ATI` answer comes from and why it cannot be
mistaken for one of the other twenty-nine modems in the table, is in
[`research/33-modem.md`](research/33-modem.md).

## fun.link

**fun.link** is funworld's cabinet-to-cabinet link: an adapter with a 25-pin
D-sub to the machine's I/O connector and a DIN onward to the next cabinet, and
funworld's own advert for it draws four machines joined in a ring. The 25-pin
plug makes the parallel port the obvious guess. It is the wrong one — the games
say so themselves:

```
LINK ERROR: No serial-port found !!!!. Please check mainboard COM-settings
LINK ERROR: Unable to send on BUS. Please check LINK-adaptor and LINK-cable
```

It is a multi-drop serial bus on **COM A**, and funworld's service manual lists
the cabinet's four serial ports:

| port | address | IRQ | connector | what is on it |
|---|---|---|---|---|
| COM A | `3F8` | 4 | **25-pin** | **fun.link** |
| COM B | `2F8` | 3 | 9-pin | Data Print |
| COM C | `3E8` | 3 | 9-pin | SMT3, the serial touchscreen controller |
| COM D | `2E8` | 10 | 9-pin | modem (Photo Play MASTERS) |

So the 25-pin D-sub on the adapter is COM A's, not the I/O card's. Note COM C:
a cabinet with fun.link fitted has its touchscreen on **IRQ 3**, because the link
driver takes IRQ 4 for itself. PeepeeBox follows that automatically.

Every release that carries the driver opens the port the same way.

| | |
|---|---|
| Port | **COM1 at 0x03F8, IRQ 4** — the one port nothing else in the cabinet uses |
| Line | 115200 8N1 |
| Transmit enable | MCR bit 0 (DTR), raised around each byte |
| Arbitration | CSMA in software: back off `rand()%90+10` ticks, fifty tries |
| Frame | 16 × `0x55` preamble, `PHDR` header, payload, `PHND` trailer |

None of that has to be understood to emulate it, and that is the point: the
cabinets talk to each other, not to the emulator. Arbitration, framing and the
invitation handshake all happen inside the guests. What PeepeeBox provides is
the wire — bytes from each cabinet reaching all the others.

The adapter is not just a wire. Before the menu will open the bus it looks for a **DS1982 on that same COM1** and checks what it says -- a fixed record, identical in all three releases, whose ROM also carries this cabinet's station number. That is what the box is for, and PeepeeBox answers it as well as carrying the traffic. Each cabinet needs its own number; left automatic, the one hosting the bus takes 1 and a joiner takes 2, which is right for a pair.

The wire itself is a TCP connection. Every cabinet is its own PeepeeBox: a second copy
beside the first, or one on another PC. **Tools → fun.link…** fits the adapter,
and its Options decide where the bus is. On the default — *joins if the bus
exists, else hosts it*, at `127.0.0.1` port 7662 — two copies on one machine
find each other whichever starts first. To link across a network, set one to
host and point the others at its address. Up to four cabinets share one bus,
which is what the advert draws.

**Most disk images cannot use it.** Only **1998/99, 2000 and 2001** can start a
linked game. I.G.O. 1 and 2 still carry the serial driver — their binaries
still hold its error messages — but their menu launches every game with
`/IPX=0`, and a game started that way never opens COM1; the invitation session is
missing from the binary entirely. From I.G.O. 3 the driver is gone as well,
leaving the artwork and the "Fun Link" menu entry with nothing behind them. The
per-generation table, the disassembly it comes from, and the one thing about the
real adapter that is still a guess — whether a cabinet hears its own
transmissions — are in [`research/34-funlink.md`](research/34-funlink.md).

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
