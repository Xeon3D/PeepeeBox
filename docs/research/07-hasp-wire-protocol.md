# Phase 7 — The HASP wire protocol, recovered

The 2001+ releases reach the dongle through Aladdin's linked-in library, which resisted
both static reading and the Unicorn sandbox (`Docs/04`). **The 1999 generation does not
use that library** — it bit-bangs the parallel port inline, in the game binary, in the
clear. That is the crack in the wall.

All offsets below are file offsets in `PP1999DE-NSB 81519 / EXE/FINDIT.EXE`
(`analysis/p5/findit99.bin`); the same code is in every 1999-generation game.

## Register use

| line | role |
|---|---|
| `DATA` (base+0) bits 0-3 | nibble sent to the dongle |
| `DATA` bit 4 | host handshake / acknowledge |
| `DATA` bits 5-7 | held high (`0xE0`) throughout |
| `CTRL` (base+2) bit 0 | STROBE — pulsed to latch a sent nibble |
| `CTRL` bit 2 | asserted then released during init |
| `CTRL` bit 3 | released then asserted during init |
| `STATUS` (base+1) bits 3-6 | nibble returned by the dongle |
| `STATUS` bit 7 (BUSY) | dongle handshake |

## Init — `0x1CF94`  `hasp_init(struct *s, port)`

```
s->port = port                 ; the struct's first word IS the port
DATA  <- 0xE0
CTRL  |= 0x01                  ; strobe high
CTRL  |= 0x04
CTRL  &= ~0x08
delay(10)
CTRL  &= ~0x04
CTRL  |= 0x08
delay(100)
```

## Send — `0x1D16A`  `send_byte(struct *s, byte b)`

Two nibbles, low first, each latched with a strobe pulse:

```
for nib in (b & 0x0F), (b >> 4):
    DATA <- (nib | 0xE0) & ~0x10
    CTRL &= ~0x01              ; strobe low
    CTRL |=  0x01              ; strobe high -> dongle latches
    delay(2)
```

## Receive — `0x1D044`  `read_byte(struct *s) -> int`

Returns `-100` (`0xFF9C`) on timeout. Every wait spins up to **0x186A0 = 100 000**
iterations.

```
wait until BUSY == 1
lo = (STATUS >> 3) & 0x0F
DATA bit4 <- 1 ; wait until BUSY == 0        (timeout -> -100)
DATA bit4 <- 0 ; wait until BUSY == 1        (timeout -> -100)
hi = (STATUS >> 3) & 0x0F
DATA bit4 <- 1 ; wait until BUSY == 0
DATA bit4 <- 0
return lo | (hi << 4)
```

Helpers: `0x1D01B` `busy()` = `(STATUS & 0x80) == 0x80`;
`0x1CFFC` `set_ack(s, flag)` = set/clear `DATA` bit 4 via read-modify-write.

## Why the 1999 images stall

`check_dongle` performs **two** accesses:

```asm
push 0x378 ; push <struct A> ; call hasp_init      ; the real hardware path
push <buffer B>              ; call read_hasp      ; the 62-byte block  <- our INT 2Bh patch
```

Only the second is redirected to `KEYN.COM`. The first drives a retry loop at `0x1D29C`:
an inner poll with a 10 000-iteration limit, retried up to 10 times, on top of the
100 000-iteration waits inside `read_byte`. With no dongle answering, that is the
observed hang while a photo game loads.

It cannot be fixed by pointing the first call at the TSR: **struct A's first word is the
port number**, so writing the 62-byte block over it would destroy the port and the
handshake state. The first path needs a device that actually answers, not canned data.

## What this unlocks

- The protocol above is enough to write an 86Box parallel-port device.
- The payload is already known: the 62-byte block recovered from `KEYN.COM`
  (banner + 8 dwords), and the banner itself is now readable per image from
  `\FOTO\SETTINGS\MAIN.SET` (`Docs/08`, `scripts/mainset.py`).
- **Hypothesis worth testing early:** the Aladdin library in the 2001+ releases very
  likely drives the same wire protocol — same dongle, same cable — just wrapped in the
  vendor envelope. If so, one device model satisfies every generation, and the library
  never has to be reversed at all. That would retire the hardest open problem in
  `Docs/04`.

## The command grammar

Recovered by executing the real code in `scripts/hasp99sim.py`, then re-reading the loop
tail at `0x1D302`.

The dongle "struct" is **not** a stack object — it is the DS global at `0x5192`:
port at `+0`, transaction buffer from `+2` (`DS:0x5194`) onward.

Two 4-entry tables live back to back at `DS:0x2283`, both indexed by the **transaction
type** held in `bufA[2]`:

```
DS:0x2283  table_send = 00 0A 32 02      bytes the host sends
DS:0x2287  table_recv = 00 04 00 30      bytes the host expects back
DS:0x228B  g_checked                     (the cached-result flag)
DS:0x228D  "Version 99"                  (the banner substring)
```

| type | send | receive |
|---|---|---|
| 0 | 0 | 0 |
| 1 | 10 | 4 |
| 2 | 50 | 0 |
| 3 | 2 | 48 |

The transaction is therefore:

```
send   table_send[type] bytes starting at bufA[2]   (the type byte itself goes first)
read   table_recv[type] bytes back into bufA[2...]
        read_byte() == -100  -> retry: re-init and start over, up to 10 times
        each retry re-runs hasp_init(0x378, bufA)
return 1 on success, 0 after 10 failed attempts
```

### Type 3 — the challenge/response  (`read_hasp_block`, `0x1D452`)

This is the one the boot path uses. **It is also the call KEYN replaces**: the `int 2Bh`
at `0x1D654` in a patched binary sits where `call 0x1D452` stands in an original.

```c
bufA[2] = 3;  bufA[3] = rand() & 0xFF;   /* the nonce */
poll();                                  /* send {03,nonce}, read 48 back */
decrypt(0, nonce);                       /* 0x1D206 */
memcpy(dest, &bufA[2], 48);
```

`0x1D206` is not a validator — it is a plain XOR against a keystream seeded by the nonce:

```c
for (i = 0; i < 48; i++) { buf[i] ^= k;
                           k += 0x75;
                           if (k < 0x28) k = 0xCB;
                           if (k > 0xC8) k = 0x13; }
```

So the dongle must answer `{03,nonce}` with the 48-byte block XORed under that keystream.
It collapses to a `0x13`/`0x88` two-cycle after the first step — the nonce really only
masks byte 0 and picks the phase. Weak, but that is what the code does.

### Validated live, 2026-08-28

`analysis/p5/hasptest86/` — PeepeeBox with `lpt1_device = dongle_photoplay`, running a
**pristine** 1999AT image (`analysis/p2/1999AT-hasptest.img`: the F: copy with all 23
patched executables restored byte-for-byte from the Phase 0 extraction, so the original
`call read_hasp_block` is back and no `int 2Bh` exists anywhere).

```
PP: Photo Play dongle attached, banner "Version 99 (AT)"
PP: host->dongle 03 (byte 1 of type 3)
PP: host->dongle 89 (byte 2 of type 3)
PP: type 3, nonce 89 -> 48 encrypted bytes
PP: *** host drained all 96 nibbles (48 bytes) ***
```

One transaction, no retries, and the guest then displayed **`COPY PROTECTION / DS1982 not
found`** — not a version or dongle error. The HASP half is satisfied: the block was read,
`strstr(buf, "Version 99")` matched, and execution moved on to the iButton check, which
fails only because no DS1982 is attached in that test folder. The type-3 grammar is
therefore confirmed against real 1999 code.

#### One transport correction found by that test

The first run showed the host sending `30 90 38 90 38 ...` forever. Realigning by one
nibble gives `3,0 -> 03` and `9,8 -> 89`: the command was correct, but the framing was a
nibble out of phase. Cause: `hasp_init` raises STROBE (`CTRL |= 0x01`) as part of its line
toggling, and that rising edge latched a bogus leading nibble. The device now treats any
movement on CTRL bits 2/3 — which only `hasp_init` performs — as a link reset and drops
all framing and queued output. **Any reimplementation of this protocol must do the same.**

### Type 1 — an 8-byte name in, a dword out  (`type1_query`, `0x1D34F`)

```
sends   { 01, name[0..7], nonce }        10 bytes; nonce also kept at bufA[11]
reads   4 bytes -> decrypted -> returned as a 32-bit value
        returns 0xFFFFFFFF if poll() failed
```

Its decryptor at `0x1D258` uses a *different* keystream over 4 bytes:

```c
k += 0x25;  if (k < 0x1E) k = 0x7B;  if (k > 0xAE) k = 0x17;
```

The eight dwords in the 62-byte block are the obvious candidates for what these queries
return, but that is a guess — the mapping from name to dword is not established.

Type 2 (send 50, receive 0) is reached from a third routine ending just before `0x1D452`
that encrypts locally and then sends; it looks like a write. Not investigated.

### Corrected: `0x1D29C` is not itself the stall

Earlier this document called that loop the hang. It is not. On a cold boot `bufA` is a
zeroed BSS global, so `type = 0`, and **both** tables read 0 — nothing sent, nothing
expected, immediate success. Confirmed three ways in the sandbox (no data ready / banner
ready / full block ready): all returned `AX=0001` with zero port traffic. The loop only
does work once a caller sets a type, which `read_hasp_block` and `type1_query` do.

### Corrected: the two accesses in `check_dongle`

This document previously said the *first* access (`hasp_init`) drives the retry loop and
the second is the one KEYN redirects. That is backwards. `hasp_init` only toggles lines;
it never polls. The retry loop lives inside `read_hasp_block` — which is exactly the call
KEYN replaces with `int 2Bh`. On a KEYN-patched 1999 binary the hardware path is therefore
gone entirely.

## Scope limit — this is the 1999 generation only

Everything above was recovered from `PP1999AT-81519 / EXE/FINDIT.EXE`. In that binary
`type1_query` has **no call sites at all** (checked both near and far), so it is unused
there. The photo-game stalls reported in the field were on 2002–2008 images, which reach
the dongle through Aladdin's linked-in library (`Docs/04`) — a different code path that
this work does not cover. Do not assume the findings here explain that stall until the
same routines are located in one of those binaries.

## The 1999 photo-game stall — PARKED 2026-08-28, and it is not the dongle

Find It and Foto Memory freeze while loading pictures (FMEMO reports `Database %s may be
corrupted!` with a garbage `%s` pointing into the Borland copyright string). This is
**pre-existing and unrelated to the protection work.** Ruled out, each by measurement:

| Hypothesis | Verdict |
|---|---|
| The dongle emulation is incomplete | **No.** Zero idle polls on both tokens, every transaction fully drained, and the games never issue a type-1 or type-2 request |
| The 14 bytes KEYN serves beyond our 48 | **No.** `0x202EE`/`0x2033B` in FMEMO use `buf[48..51]` as scratch to save/restore four colour globals — incidental reuse, so the 48-byte reply is complete |
| Emulation vs the KEYN patch | **No.** The KEYN-patched original fails identically |
| An 86Box v7 regression | **No.** 86Box v4.2 fails too, and earlier |
| Missing or corrupt assets | **No.** All picture archives read back at full size, dense at head, middle and tail (`/FINDIT/PICS/FOTOPLAY.WAD` is 94 MB) |
| Wrong machine config | **No.** Byte-identical to the known-good `86boxv2` config |
| DOS file locking (`SHARE.EXE`) | **No.** `/PTSDOS/SHARE.EXE` exists and is loaded by neither `CONFIG.PTS` nor `AUTOPTS.BAT`; adding it to the boot chain changed nothing |
| A missing CD-ROM (`atapi.sys` + `ptscdex` are loaded) | **Unlikely.** The games reference no drive letter but `C:` |

Untried when this was parked: `C:\ERROR.LOG` — all three games write diagnostics there
(the literal is in every binary) and it had not been created yet at the time of checking.
Reading it after a failure is the cheapest next step. Also untried: whether any *other*
1999 image behaves the same, which would separate a bad dump from a systemic problem.

## Still unknown

- What the 4-byte type-1 replies should contain, and how a name maps to one.
- Type 2's payload.
- Whether the 2001+ Aladdin library speaks this same wire protocol. If it does, one device
  model serves every generation; that remains the single highest-value thing to test.
