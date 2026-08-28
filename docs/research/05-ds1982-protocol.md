# Phase 5 — The DS1982 iButton link, fully recovered

Complete, implementable spec for the second token. Recovered from IGO6 `11SPEED.EXE`;
the port constant (`0x0268`, MCR `3`) is identical in all 12 generations.

## Hardware

A **16550-class UART at I/O base 0x268** with a Dallas 1-Wire adapter on its TX/RX pair —
the classic DS9097 arrangement described in Maxim app note AN214. No interrupts are used;
everything is polled. Timeouts come from the BIOS tick counter at `0040:006C`
(15 ticks ≈ 0.8 s for reset, 30 ticks ≈ 1.65 s for a byte).

Object layout (266 bytes), for reference when reading the disassembly:

| offset | meaning |
|---|---|
| `+0x000` | 3 status bytes; `+3` is the timeout flag |
| `+0x004` | 256-byte CRC8 table, copied from `DS:0x2176` |
| `+0x104` | **UART base address** (`0x0268`) — set from `push dword 0x00030268` |
| `+0x106` | **MCR value** (`3` = DTR\|RTS) — *not* an IRQ |
| `+0x107` | `6` |
| `+0x109` | running CRC8 accumulator |

## Reset / presence  (`0x27723`)

```
LCR   (base+3) <- 0x83      ; DLAB, 8N1
DLL   (base+0) <- 0x01      ; divisor 1  -> 115200
DLM   (base+1) <- 0x00
LCR   (base+3) <- 0x03      ; DLAB off
IER   (base+1) <- 0x00      ; polled, no interrupts
MCR   (base+4) <- 3         ; DTR|RTS
   wait LSR & 0x60 == 0x60 (THRE|TEMT); drain RX while LSR & 1
LCR   (base+3) <- 0x83
DLM            <- 0x00
DLL            <- 0x0B      ; divisor 11 -> ~10473 baud   << the reset rate
LCR            <- 0x03
TX             <- 0xF0      ; << THE RESET PULSE
   wait LSR & 1, read RX
presence = (RX != 0xF0)     ; a slave holding the line low corrupts the echo
LCR <- 0x83 ; DLL <- 1 ; DLM <- 0 ; LCR <- 0x03    ; back to 115200
```

## Byte transfer  (`0x27853`)

One UART byte per 1-Wire **bit**, LSB first, 8 iterations:

```
for mask in 1,2,4,...,0x80:
    TX <- 0xFF if (value & mask) else 0x00     ; read-slot / write-1, or write-0
    wait LSR & 1
    r = RX
    result >>= 1 ; if (r & 1) result |= 0x80   ; bit0 of the echo is the wire level
return result
```

## CRC8

`crc = table[crc ^ byte]`, table lifted from `DS:0x2176`. **Verified byte-identical to the
standard Maxim/Dallas CRC8** (reflected polynomial 0x8C, init 0). No custom crypto.

## Command sequences the games issue

**Read ROM** (`0x2791B`) — sets error bit `0x001` on failure:

```
reset()                       ; must report presence
xfer(0x33)                    ; READ ROM
crc = 0
for i in 0..7: b = xfer(0xFF); crc = crc8(crc, b); dest[i] = b
ok = (crc == 0)               ; CRC8 over all 8 bytes must be zero
```

Family code for DS1982/DS2502 is `0x09`. The serial is **not** compared against anything —
only the CRC is checked — so any well-formed ROM id is accepted.

**Read Memory** (`0x27B70`):

```
memset(dest, 0, 0x80)
reset()                       ; must report presence
xfer(0xCC)                    ; SKIP ROM
xfer(0xF0)                    ; READ MEMORY
xfer(TA1); xfer(TA2)          ; address, 0x0000
crc = crc8(0xF0, TA1, TA2)
b = xfer(0xFF)                ; the device returns its CRC of command+address
if (b != crc) -> retry/fail
... then stream memory bytes from TA ...
```

The caller compares `dest[5:]` against the XOR-0x7C constant that decodes to
`Photo Play 2000 Version 3`. Bytes `dest[0..4]` are not examined.

## Minimum viable device

```
ROM  : 09 <6 bytes of anything> <crc8>       ; family 0x09, CRC8 over all 8 == 0
PAGE : 128 bytes, with "Photo Play 2000 Version 3" at offset 5
```

Implemented and self-tested in `scripts/ds1982sim.py`.

## Testing it without modifying 86Box

The UART base is an argument, not a constant, so it can be retargeted to a port 86Box
already provides — `scripts/ibutton_retarget.py <image> 0x2E8` rewrites the low word of
`push dword 0x00030268` to **COM4** in every game executable. This is a change to a
hardware address only; no program logic is touched.

Then in `86box.cfg`:

```ini
[Ports (COM & LPT)]
serial4_enabled = 1
serial4_passthrough_enabled = 1

[Serial port passthrough 4]
mode = 0                              ; 0 = Named Pipe (Server)
named_pipe = \\.\pipe\86Box\test      ; 86Box's own default
```

**Gotcha:** the pipe key is `named_pipe`, *not* `host_serial_path` (that one is for the
"Host Serial Device" mode). If `named_pipe` is absent, 86Box silently uses its default
`\\.\pipe\86Box\test`. 86Box is the pipe **server**; the simulator connects as a client.

Baud changes are invisible across a byte pipe, and they do not need to be visible: the only
literal values the host ever puts on the wire are `0xF0` (reset), `0xFF` and `0x00` (bit
slots), so the three cases are unambiguous by value alone.

Run `scripts/ds1982sim.py` with a **native Windows** Python — the MSYS2 one at
`C:\msys64\mingw64\bin\python3.exe` works; WSL Python cannot reach Windows named pipes.

## CONFIRMED WORKING (2026-08-27)

`2001IT-A3735`, HASP served by `KEYN.COM`, iButton port retargeted to COM4, simulator on
the passthrough pipe. The guest ran the complete sequence, unprompted and correctly:

```
connected.
  RESET -> presence
  READ ROM -> 09 50 50 42 4f 58 00 ff
  RESET -> presence
  READ ROM -> 09 50 50 42 4f 58 00 ff      <- the double-read verification
  RESET -> presence
  SKIP ROM
  READ MEMORY
  addr 0x0000, crc 0x8D, streaming 128 bytes
  RESET -> presence
  SKIP ROM
  READ MEMORY
  addr 0x0000, crc 0x8D, streaming 128 bytes
```

`\FN_SYS\DFU\TRANS\ERROR.LOG` gained **no new entry** — its last two lines are the
21:32 and 22:00 failures from before the simulator was attached:

```
27.8.2026 21:32 in C:\EXE\TOWERS.EXE  --> DS1982 FAILED
27.8.2026 22:00 in C:\EXE\TAKETWO.EXE --> DS1982 FAILED
```

So the emulated DS1982 satisfied **both** gates: the ReadROM CRC8 check *and* the
memory-page comparison against `Photo Play 2000 Version 3`. The double read/compare passed,
which means the byte framing is exactly right — an off-by-one in the bit order would have
produced two differing reads.

**The DS1982 half of PeepeeBox is validated against the real software.** Porting this state
machine into an 86Box C device at I/O 0x268 is now mechanical, and needs no further
reverse engineering.

## Ported natively into PeepeeBox (2026-08-28)

Done. `peepeebox/master/src/device/dongle_photoplay.c` now emulates a minimal 16550 at
I/O 0x268 with this exact state machine behind it, so **no retargeting, no named pipe and
no host-side Python are needed any more** — `scripts/ibutton_retarget.py` is obsolete for
normal use and is kept only for testing against a stock 86Box.

The UART model is deliberately tiny: LSR always reports `THRE|TEMT` (bytes are consumed
instantly) plus `DR` when a reply is waiting, and a write to THR is turned straight into a
1-Wire event whose answer lands in RBR. The games poll those bits and use no interrupts.

It is brought up by the LPT dongle device (config option *"Emulate the DS1982 iButton at
I/O 268h"*, on by default), because both tokens are mandatory and wanting one without the
other is never useful.
