# Phase 4 — "PeepeeBox": an 86Box that emulates both tokens

Feasibility assessment. **Verdict: possible, but it is a real project, and it is
both-or-nothing** (`Docs/03` measured that emulating one token alone changes nothing).

## What has to be built

Two device models inside 86Box.

### 1. DS1982 iButton on a UART at I/O 0x268 — *low risk*

| | |
|---|---|
| interface | 16550-style register file; the game polls LSR (`base+5`) for `THRE\|TEMT` (0x60) and moves bytes through `base+0`, with a ~1.65 s timeout taken from the BIOS tick counter at `0040:006C` |
| protocol | 1-Wire. Confirmed: command `0x33` = **Read ROM**, then 8 read slots, CRC8 accumulated, performed **twice** into a double buffer and compared. A second call then pulls a ~128-byte page out of the DS1982's 1 Kbit EPROM |
| payload | **already known** — 8-byte ROM ID with valid CRC8 (trivially generated) and an EPROM page whose byte 5 onward reads `Photo Play 2000 Version 3` |
| unknown | the exact byte framing (how many UART bytes per 1-Wire bit/byte, and whether the adapter is a dumb DS9097 or a smart DS2480B). Requires finishing the disassembly of the reader at `0x27853` in IGO6 `11SPEED.EXE` |

### 2. HASP on the parallel port — *medium/high risk*

| | |
|---|---|
| entry | the standard Aladdin envelope `hasp(service, lpt, pass1, pass2, &p1..&p4)`; in IGO6 `11SPEED.EXE` at `3693:000A` (file `0x3BD3A`), dispatching to the implementation at file `0x3BED4` |
| services used | only **1** (`IsHasp`), **5** (memory read) and **50**. `HaspCode` (service 2) is *never* called, so **no vendor secret is required** |
| payload | **already known** — the 62-byte block recovered from `KEYN.COM` |
| unknown | **the LPT wire protocol.** Must be reversed out of the vendor library |
| shortcut ruled out | the library has a vestigial `HASPDOSDRV` branch, but **no HASP driver is shipped on any of the 20 disks** and only IGO6's `MENU.EXE` even contains the string. The direct port-I/O path is the one in use, so "just supply a fake DOS driver" does not work |

## Build environment

Checked on this machine:

| | status |
|---|---|
| git, cmake, ninja, gcc/g++ (WSL) | present |
| network / GitHub reachable | yes |
| Qt | **missing** — `apt` installable for a Linux build |
| mingw-w64 + Qt-for-mingw | **missing**, and Qt for mingw is not packaged in Ubuntu |

So a **Linux PeepeeBox running under WSLg** is straightforward to reach. A native
**Windows `PeepeeBox.exe`** needs either MSYS2 installed on the host or a hand-built
mingw+Qt cross-toolchain — that is the expensive path, and it means installing a build
environment on the user's machine.

## Recommended sequencing — validate before building

There is a way to develop and prove the DS1982 emulation **without touching 86Box at all**.

The port number is not baked into the hardware access — it is an argument:

```asm
66 68 68 02 03 00     push dword 0x00030268     ; 0x0268 = port base, 3 = mode/IRQ
```

Changing `68 02` to `E8 02` rewrites the base to **0x02E8 = COM4**, a port 86Box already
provides *and already has enabled* in `86boxv2/86box.cfg` (`serial4_enabled = 1`). COM4,
not COM3 — COM3 is where the MicroTouch lives.

That gives a two-byte, per-EXE change that alters **a hardware address only, not program
logic**, and unlocks this loop:

1. redirect the iButton port to COM4 in one game EXE;
2. point 86Box's COM4 at a host pipe via its existing **Serial Passthrough**;
3. implement the DS1982 in a throwaway host-side Python program;
4. iterate until the game stops printing `DS1982 FAILED`.

When that passes, the protocol is *proven* and porting it into an 86Box C device is
mechanical. Only then is it worth attacking the HASP half, which is the part that can
actually sink the schedule.

**Caveat**: the reader polls LSR rather than using interrupts, so the `3` (IRQ/mode) is
probably irrelevant — but whether the code programs a baud divisor first is not yet
established. Finish the `0x27853` disassembly before relying on this.

## Bottom line

- Both devices are emulatable in principle; neither needs a cryptographic secret, and both
  payloads are already recovered.
- The single unrecovered piece is the **HASP LPT wire protocol**.
- Nothing here displaces `--mode menu`, which already works and is shipped.
