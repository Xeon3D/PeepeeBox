# 6. The 2008 token — a serial smart-card reader

The IGO 8 images do not use the parallel port for protection at all. **Zero of their 61
executables link the parallel library**; 15 name `NGDONGLE` beside the serial base
`0x2F8`. The games open a reader there and push ISO 7816-4 APDUs at a card behind it,
under two layers of obfuscation keyed by a fresh four-byte nonce per frame.

(The I.G.O. Italy build of the same year is the other flavour — entirely parallel. See
`01`. And `HDONGLE FAILED` in an IGO 8 game's strings is a shared message table, not a
code path.)

*State:* **complete.** An untouched IGO 8 image boots and keeps its encrypted content
databases.

## 6.1 Transport

A plain 8250/16550 at **`0x2F8` (COM2), 9600 8N1** — every 2008 executable passes the
base explicitly. Its init sequence:

```
IER = 0x00 ; LCR = 0x80 ; DLL = 0x0C ; DLM = 0x00 ; LCR = 0x03
THR = 0xF0            ; a wake byte, not part of any frame
MCR = 0x00 ; delay 50 ms ; MCR = 0x03
                      ; drain RX, seed the reply codec (0xAB, 0xD9)
```

Transmit polls `LSR` bit `0x20` (THRE); receive polls bit `0x01` (DR).

**Byte counts are not cosmetic.** The receive helper returns the moment the requested
count has arrived, and otherwise sits out a BIOS-tick timeout of up to eleven seconds
per transaction. Serving a short frame is not a protocol error — it just makes the
machine crawl. Every count below is the one the guest asks for.

A lull on the line is the right resynchronisation signal: about five byte times of
silence means whatever is held is not the start of a frame (the wake byte, an abandoned
retry).

## 6.2 The two keystreams

Every frame opens with four random bytes `r0 r1 r2 r3`. `r0` and `r1` go out **in the
clear**; everything after them is obfuscated. The nonce is fresh per frame, so a
recorded exchange cannot be replayed — and the device must decrypt `r2 r3` with the
inbound stream before it can encrypt its answer. That coupling is the whole point of the
design.

**Host → reader** — four 4-bit registers seeded from the two plaintext nonce bytes, two
bytes emitted per round. The stream's **first output is consumed by `r2`**.

```c
seed(lo, hi): n0 = lo & 15; n1 = lo >> 4; n2 = hi & 15; n3 = hi >> 4; half = 0;

next():
    if (half) { half = 0; return n0 | (n1 << 4); }
    if (++n0 > 15) { n0 &= 15; n1 = (n1 + 1) & 15; }
    n3 ^= n0; n0 ^= n1; n1 ^= n2; n2 ^= n3;
    half = 1; return n2 | (n3 << 4);
```

**Reader → host** — two byte registers whose walk never depends on the data, so the same
routine encodes here and decodes in the guest.

```c
apply(x):
    x  ^= a;
    old = b;
    a   = (b + 0x25) & 0xFF;
    if (old < 0x1E) a = 0xE9;
    if (a > 0xAE)   b = 0x17;
    x  ^= b;
    b   = (a + 0x75) & 0xFF;
    if (a < 0x38)   b = 0x39;
    if (b > 0x7B)   a = 0xD5;
    return x;
```

Seeded `(0xAB, 0xD9)` at init, then re-seeded to `(r2, r3)` before every reply — except
reader command `0x04`, which re-seeds to the constant `(0xAB, 0xD9)`.

## 6.3 Frames

**Reader command** — eight bytes. `E()` is the host→reader stream:

```
r0  r1  E(r2)  E(r3)  E(0x00)  E(cmd)  E(arg)  E(chk)
chk = r0 ^ r1 ^ r2 ^ r3 ^ 0x00 ^ cmd ^ arg
```

**Card frame:**

```
r0  r1  E(r2)  E(r3)  E(L)  E(body[0..L-1])
body = addr, ctrl, len, payload[len], chk
chk  = XOR of addr, ctrl, len and the payload      (it does not cover the nonce or L)
L    = len + 4
```

`addr` is always `0x00`. `ctrl` is `0x00`/`0x40` for an I-frame carrying an APDU (bit 6
is a sequence toggle) and `0xC1` for a frame addressed to the reader itself.

**Byte 4 tells the two kinds apart:** `0x00` means a reader command (it is the `addr`
field), `≥ 5` means a card frame (it is `L`).

**Reply frame**, every byte through the reply codec:

```
addr  ctrl  len  payload[len]  chk
```

The host requires `addr == 0`, `ctrl` bit 7 clear, and the XOR of *all* decoded bytes
including `chk` to be zero. The last two payload bytes are `SW1 SW2` and the caller
requires `0x9000`.

## 6.4 The handshake, in order

| # | what goes out | what must come back |
|---|---|---|
| 1 | after 400 ms, reader command `cmd=0x0A arg=0x00` | **exactly 9 bytes**; contents never inspected. 3 attempts, MCR toggled 0→3 between them |
| 2 | link frame, `ctrl=0xC1`, payload `6E` | **exactly 5 bytes** whose decoded XOR is 0. 3 attempts |
| 3 | `00 20 00 04 08 01 02 03 04 05 06 07 08` — VERIFY, PIN `01..08` | **6 bytes**, SW `9000` |
| 4a | `00 A4 08 00 02 7F AB 00` — SELECT `7FAB` | **28 bytes**, SW `9000` |
| 4b | `00 A4 08 00 04 7F AB 81 FB 00` — SELECT `7FAB/81FB` | **28 bytes**, SW `9000` |
| 4c | `00 B0 00 00 64` — READ BINARY, 100 bytes from offset 0 | **106 bytes**, SW `9000` |
| 4d | `00 A4 08 00 02 3C D7 00` — SELECT `3CD7` | **28 bytes**, SW `9000` |

The APDUs are built from word-arrays the constructor writes into the reader object, which
is why they are identical in every executable.

**Nothing in this sequence sets a failure bit.** A reader that never answers produces no
message at all — it leaves the banner empty and the *version* comparison downstream is
what complains. That is why the keyless patch in circulation removes the version
complaint and not a dongle one, and it is why an emulated reader should log what it
serves.

## 6.5 The record

The 100 bytes from READ BINARY are XORed with the Borland/Delphi LCG:

```c
x = 0x01BB253A;                      /* identical in all 61 executables */
for (i = 0; i < 100; i++) { x = x * 0x08088405 + 1; rec[i] ^= x >> 24; }
```

Only three fields are ever read:

| offset | length | use |
|---|---|---|
| `0x00` | 3 | family — `stricmp` against `"IGO"` |
| `0x03` | 2 | territory — substituted into `"Version 2008 (%s)"` |
| `0x13` | 6 | title — `stricmp` against `"IGO 08"` |

Everything else is ignored. The composed banner must equal `MAIN.SET["Version"]`,
compared with `strcmp` — **case sensitive**, unlike the two record fields.

Deriving the title from the banner: take the version **token** (the text between
`Version ` and the territory) and drop a leading `20`. `2008` → `08` gives `IGO 08`.
A digit-run rule is not enough — I.G.O. Italy's banner is `Version 08IT (IT)`, its token
is `08IT`, and it needs the title `IGO 08IT`.

**In 2008 the dongle supplies the banner and nothing else.** The six per-title content
keys are *compile-time constants inside each executable*, byte-identical across all 61,
which the game itself writes at banner + 30. The 1999/2000 rule that the dwords come
from the dongle holds for those generations and does not carry forward.

## 6.6 An emulator's checklist

1. A 16550 at `0x2F8` that survives the init in `6.1`.
2. Take `r0 r1` in the clear, seed the inbound stream, decrypt `r2 r3`, seed the reply
   codec with them (or with `0xAB, 0xD9` for reader command `0x04`).
3. Answer reader command `0x0A` with **exactly 9 bytes** of anything.
4. Answer the `6E` link frame with **exactly 5 bytes** that XOR to zero — echoing the
   payload works.
5. VERIFY → `9000` in a 6-byte frame. Each SELECT → a 22-byte body plus `9000`, 28 bytes
   on the wire.
6. READ BINARY → the 100-byte record LCG-encrypted, plus `9000`: 106 bytes.
7. Build the record with family `IGO`, the title derived as above, and the territory that
   makes the banner match this image's `MAIN.SET["Version"]`. **Read it from the settings
   file; never infer it from the image's filename.**
8. Serve a DS1982 on `0x268` as in `03`.

One integration note: the reader claims COM2 whether or not anyone talks to it, and COM2
is also where these cabinets put the Dataprint printer. Attach it only for 2008 images
(and for an image that cannot be identified at all, where losing the dongle is the worse
failure).
