# Phase 3 — There are **two** tokens, and what that means for emulation

This falsifies the framing carried by every prior note in the workspace (mine included):
the machines are not protected by "a dongle". They are protected by **two physically
separate tokens**, checked independently inside the same function.

## The second token

`check_dongle()` calls four routines on a 266-byte object. They looked like a decoder;
they are not. `init()` copies a 256-byte table from the data segment into the object and
`setparams()` stores two values from the constant `push dword 0x00030268`:

```asm
setparams:  mov ax,[bp+8] ; mov [si+0x104],ax     ; 0x0268
            mov al,[bp+0xA]; mov [si+0x106],al    ; 3
```

`[si+0x104]` is not a key. It is an **I/O port base**:

```asm
            mov  dword [bp-6], 0x0040006C     ; far ptr to the BIOS tick counter
            les  bx,[bp-6] / mov eax,[es:bx]  ; timeout base
wait:       ...re-read ticks; if delta > 0x1E (~1.65 s) -> flag timeout, return 0xFF
            mov  dx,[si+0x104]                ; 0x0268
            add  dx,5
            in   al,dx                        ; +5 = 8250/16550 Line Status Register
            and  ax,0x60 / cmp ax,0x60        ; THRE|TEMT — transmitter ready
            jnz  wait
            mov  dx,[si+0x104] / in al,dx     ; +0 = data register
```

That is a **16550 UART at I/O 0x268** driven byte-at-a-time. What it sends settles it:

```asm
            push 0x33  ; ---> 1-Wire READ ROM
            ...
            push 0xFF  ; ---> 1-Wire read slot, x8
            ... store 8 bytes, accumulate a running byte at [si+0x109]  ; CRC8
            ... twice, into a 2 x 8 double buffer, and compare            ; verify
```

`0x33` is the Dallas **1-Wire Read ROM** command; eight bytes with a trailing CRC8 is the
1-Wire 64-bit ROM ID. The strings in the binaries name the parts outright:
`DS1982 FAILED`, `DS1982 not found`, `DS1425 not found`.

So the second token is a **Dallas DS1982 iButton on a 1-Wire bus, bridged through a
serial port at I/O 0x268** (a DS9097-class adapter). After the ROM read, a further call
pulls a ~128-byte record out of the iButton's 1 Kbit EPROM; the text at offset +5 of that
record is what gets compared against the XOR-0x7C constant that decodes to
`Photo Play 2000 Version 3`.

**Port 0x268 / mode 3 is universal** — identical in all 12 product generations from
PP1998 to IGO8 (`analysis/p2/ibutton.py`).

## Corrected error-bit map

| bit | message | token |
|---|---|---|
| **0x001** | `DS1982 FAILED` | **DS1982 iButton**, 1-Wire over UART @ 0x268 |
| 0x010 | `HDONGLE FAILED` | HASP |
| 0x080 | `KDONGLE FAILED` | HASP |
| 0x100 | `MDONGLE FAILED` | HASP |
| 0x200 | `MBDONGLE FAILED` | HASP |
| **0x400** | `NDONGLE FAILED` | **HASP**, version-banner test |

## This retro-explains every historical patch

`KEYN.COM` serves the HASP block over `INT 2Bh`, so bit 0x400 passes. It says nothing
about the iButton, so **bit 0x001 always fails under KEYN** — which is exactly why:

- the **pre-IGO4 KEYN builds** additionally patch `or ax,1` → `or ax,0`: that is the
  iButton failure bit, and nothing else;
- the **IGO4+ KEYN builds** instead stub `Error()` → `retf`, which hides the resulting
  `DS1982 FAILED` report (along with every other error in the program).

Neither is a mystery any more, and neither is needed by `--mode menu`, which neutralises
the whole function before either token is touched.

## What the HASP is actually used for

Across all call sites in a game binary, only three services are ever requested:

| service | count | meaning |
|---|---|---|
| 1 | 7 | `IsHasp` — presence |
| 5 | 13 | memory read |
| 50 | 11 | HASP4 extended read |

**Service 2 (`HaspCode`) is never called.** The cryptographic challenge/response engine —
the part of HASP that carries a per-customer secret and cannot be emulated without it — is
not used at all. The dongle is an identity-plus-62-bytes-of-memory device.

## So: can a real emulator be built?

Yes in principle, and the payoff is real — **zero modification to any game file**. The
scope is now bounded, because we know what both devices have to say:

| | HASP (LPT) | DS1982 (1-Wire @ 0x268) |
|---|---|---|
| payload needed | the 62-byte block — **already recovered** from `KEYN.COM` (banner + 8 dwords, identical across every known TSR) | 8-byte ROM ID with a valid CRC8 (trivially generated) + an EPROM page whose byte 5 onward reads `Photo Play 2000 Version 3` — **string already recovered** |
| secret needed | **none** — no `HaspCode` | none |
| wire protocol | **unknown** — must be recovered from the linked-in HASP interface routine (entry located, e.g. `3693:000A` in IGO6 `11SPEED.EXE`) | well documented — 1-Wire over a UART is a standard, small state machine |

### Assessment

- The **iButton half is the tractable one**: a documented protocol, a known payload, and a
  plain 16550 at a fixed address. If 86Box can be given a serial port at base 0x268, this
  could even be served from the host through serial passthrough with no emulator changes
  at all. *Unverified*: whether 86Box supports a non-standard serial base — that is the
  first thing to check before committing to this path.
- The **HASP half is the risk**: it needs the LPT wire protocol reverse-engineered out of
  the HASP interface routine and then implemented as an 86Box LPT device. Bounded, but it
  is the part that can eat the schedule.
- **An emulator that does only one of the two is worthless** — it lands you exactly where
  `KEYN.COM` landed, needing a patch for the other half anyway.

### Recommendation

Keep `--mode menu` as the shipping answer: two bytes and one byte per game, structurally
proven, and now confirmed booting to the menu on an original image. Treat the emulator as
a separate preservation-grade project, and sequence it **iButton first** — it is the
cheaper half, it is the half `KEYN` never solved, and finishing it would tell us whether
the HASP half is worth attempting.

## MEASURED: an HASP-only emulator is not enough

Built with `ppkeyless.py --mode keyn --hasp-only -V 'Version 2001 (IT)'` on a pristine
`2001IT-A3735.img`: all 36 games' HASP reads redirected to `INT 2Bh` and served by
`KEYN.COM`, with the iButton failure bit and `Error()` **left fully live**
(audit: `analysis/p2/audit_hasponly.py`). Booted in `86boxv2`, menu reached, game launched:

```
A critical error occured !!!

Module      : C:\EXE\TOWERS.EXE
Date/Time   : 27.8.2026   21:32
Language    : ITA

Error Description
DS1982 FAILED
```

`\FN_SYS\DFU\TRANS\ERROR.LOG`:

```
27.8.2026 21:32 in C:\EXE\TOWERS.EXE --> DS1982 FAILED
```

Three things are now measured rather than inferred:

1. **The HASP half works.** No `NDONGLE FAILED`, no `Wrong Version` — `KEYN.COM`'s 62-byte
   block satisfies every HASP-derived test. A HASP emulator would be a solved problem.
2. **The iButton half is independently enforced and fatal.** The game aborts to a hard
   error screen naming the part. This is not a warning that can be ignored.
3. **An emulator covering only the HASP lands exactly here** — an error screen on every
   game launch. Emulating one token buys nothing on its own.

Bonus confirmation: `Language : ITA`, and `\MENU\SYSTEM.INI` came out `ITA ITA,ENG,GER`.
That is the `strstr(dongle_buf, "(")` territory parse reading `(IT)` out of KEYN's banner —
the dongle banner does drive language selection. Reassuringly, `--mode menu` does **not**
lose this: the IGO6 (AT) menu-mode boot produced `GER ENG,FRE,GER` and the IGO8 (ES) boot
produced `SPA ENG,FRE,SPA`, so the correct territory is recovered from elsewhere when the
dongle buffer is empty.

## Revised recommendation

An emulator is a **both-or-nothing** project, and 86Box gives no free ride on either half:
it exposes exactly four serial ports at fixed COM bases (no configurable base address) and
has no 1-Wire/iButton device, so even the "easy" half needs C in the emulator. Realistic
scope:

| | work | risk |
|---|---|---|
| DS1982 device at I/O 0x268 | a small ISA device model answering the byte-oriented 1-Wire protocol with a canned ROM ID + EPROM page — payload already known | low; protocol documented, payload recovered |
| HASP device on LPT | wire protocol must first be reversed out of the linked-in HASP interface routine, then modelled | moderate; no crypto secret needed (no `HaspCode`), but the protocol is unrecovered |

`--mode menu` remains the shipping answer: three bytes per game, structurally proven, and
confirmed booting to the menu on an unmodified original. The emulator is worth doing only
as a separate preservation-grade effort, and only if **both** devices are in scope.
