# Phase 18 — the 2008 dongle is a serial smart-card reader, and here is its protocol

Recovered from the **unpatched** IGO8 ES image by disassembling the protection
module every 2008 executable carries. In `\EXE\SOLI.EXE` it is code segment
`0x2A2F`, file offset `0x2E6F0`; all offsets below are offsets inside that
segment. The same module, byte for byte, is in all 61 executables.

A working model with a self-test is `igo8_dongle.py` (in the analysis working
directory). Running it plays the game's entire start-up handshake against the
emulated dongle and asserts every check the game makes.

## Summary of the correction

`docs/research/09` recorded that the 2008 generation "uses a third token, NG".
That is right that it is a third thing and wrong about what. It is **not** a
parallel-port device at all:

| generation | port | device |
|---|---|---|
| 1999 | LPT1 | funworld two-chip dongle, nibble-bang (`docs/research/12`) |
| 2000 | LPT1 | second funworld dongle, sharing the port (`docs/research/15`) |
| **2008** | **COM2, I/O 0x2F8** | **serial smart-card reader fronting an ISO 7816 card** |

The DS1982 iButton on `0x268` is unchanged and still mandatory.

---

## 1. Transport

Construction (`sub_04`) takes the port base; `0xFFFF` means "default `0x2F8`".
Every 2008 executable passes `0x2F8` explicitly.

Initialisation (`sub_63`) programs a plain 8250/16550:

```
IER  (base+1) = 0x00
LCR  (base+3) = 0x80          ; DLAB
DLL  (base+0) = 0x0C          ; divisor 12
DLM  (base+1) = 0x00          ; -> 9600 baud
LCR  (base+3) = 0x03          ; 8N1
THR  (base+0) = 0xF0          ; wake byte
MCR  (base+4) = 0x00
                              ; delay 50 ms
MCR  (base+4) = 0x03          ; DTR + RTS
                              ; drain RX, seed reply codec (0xAB, 0xD9)
```

Transmit polls `LSR` bit `0x20` (THRE); receive polls `LSR` bit `0x01` (DR) and
reads `RBR`.

**Timing matters in one place.** The receive helper (`sub_D3A`) reads the BIOS
tick counter at `0040:006C` and gives up after `timeout × 10` ticks (~55 ms
each), but returns the moment the requested byte count has arrived. Return the
exact expected count and the machine flies; return fewer and every transaction
sits out its full timeout — up to eleven seconds each.

---

## 2. The two keystreams

Every frame opens with four random bytes `r0 r1 r2 r3` drawn by the host. `r0`
and `r1` go out in the clear; everything after them is obfuscated. The nonce is
fresh per frame, so a recorded exchange cannot be replayed.

### Host → dongle: a nibble LCG (`sub_2DB` seed, `sub_3F0` step)

Four 4-bit registers, seeded from the two plaintext nonce bytes. Each round
emits **two** bytes.

```
seed(lo, hi):  n0 = lo & 15 ; n1 = lo >> 4 ; n2 = hi & 15 ; n3 = hi >> 4 ; half = 0

next():
    if half:  half = 0 ; return n0 | (n1 << 4)
    n0 = n0 + 1
    if n0 > 15: n0 &= 15 ; n1 = (n1 + 1) & 15
    n3 ^= n0 ; n0 ^= n1 ; n1 ^= n2 ; n2 ^= n3
    half = 1 ; return n2 | (n3 << 4)
```

The stream is seeded with `(r0, r1)` and its **first output is consumed by
`r2`**.

### Dongle → host: a byte-pair walk (`sub_453` seed, `sub_469` step)

Two byte registers. The data byte is XORed with one register before the walk and
one after; the walk itself never depends on the data, so the identical routine
encodes on the dongle and decodes on the host.

```
apply(x):
    x   ^= s72
    old  = s73
    s72  = (s73 + 0x25) & 0xFF
    if old < 0x1E:  s72 = 0xE9
    if s72 > 0xAE:  s73 = 0x17
    x   ^= s73
    s73  = (s72 + 0x75) & 0xFF
    if s72 < 0x38:  s73 = 0x39
    if s73 > 0x7B:  s72 = 0xD5
    return x
```

Seeded `(0xAB, 0xD9)` at initialisation, then re-seeded to `(r2, r3)` before
every reply — except for reader command `0x04`, which re-seeds to the constant
`(0xAB, 0xD9)` instead. `r2` and `r3` reach the dongle *encrypted*, so it must
decrypt them with the inbound stream before it can encrypt its answer: that
coupling is the whole point of the design.

---

## 3. Frames

### Reader-level command (`sub_317`)

Eight bytes. `E()` is the host→dongle stream seeded `(r0, r1)`.

```
r0  r1  E(r2)  E(r3)  E(0x00)  E(cmd)  E(arg)  E(chk)
chk = r0 ^ r1 ^ r2 ^ r3 ^ 0x00 ^ cmd ^ arg
```

### Card frame (`sub_4F6`)

```
r0  r1  E(r2)  E(r3)  E(L)  E(body[0..L-1])

body = addr, ctrl, len, payload[len], chk
chk  = XOR of addr, ctrl, len and the payload
L    = len + 4, the number of body bytes that follow
```

`addr` is always `0x00`. `ctrl` is `0x00` or `0x40` for an I-frame carrying an
APDU — bit 6 is a sequence toggle that alternates per frame — and `0xC1` for a
frame addressed to the reader itself. `chk` does **not** cover the nonce or `L`.

The fifth byte is what tells the two frame kinds apart: `0x00` for a reader
command (it is the `addr` field), `≥ 5` for a card frame (it is `L`).

### Reply frame (`sub_9C4`)

Every byte passes through the reply codec. After decoding:

```
addr  ctrl  len  payload[len]  chk
```

The host requires `addr == 0`, `ctrl` bit 7 clear, and the XOR of *all* decoded
bytes including `chk` to be zero. The last two payload bytes are taken as
`SW1 SW2`; the caller then requires `0x9000`.

---

## 4. The handshake, in order

| # | routine | what goes out | what must come back |
|---|---|---|---|
| 1 | `sub_69D` | after 400 ms, reader command `cmd=0x0A arg=0x00` | **exactly 9 bytes**; contents never inspected. 3 attempts, MCR toggled 0→3 between them |
| 2 | `sub_622` | link frame, `ctrl=0xC1`, payload `6E` | **exactly 5 bytes** whose decoded XOR is 0. 3 attempts |
| 3 | `sub_C45` | `00 20 00 04 08 01 02 03 04 05 06 07 08` — VERIFY, PIN `01..08` | **6 bytes**, SW `9000` |
| 4a | `sub_BA4` | `00 A4 08 00 02 7F AB 00` — SELECT path `7FAB` | **28 bytes**, SW `9000` |
| 4b | `sub_BA4` | `00 A4 08 00 04 7F AB 81 FB 00` — SELECT path `7FAB/81FB` | **28 bytes**, SW `9000` |
| 4c | `sub_7FB` | `00 B0 00 00 64` — READ BINARY, 100 bytes from offset 0 | **106 bytes**, SW `9000` |
| 4d | `sub_C0B` | `00 A4 08 00 02 3C D7 00` — SELECT path `3CD7` | **28 bytes**, SW `9000` |

The APDUs are not literals in the code; they are built from five word-arrays the
constructor writes into the reader object at `si+0x74`, `+0x84`, `+0x94` and
`+0xA8` (CLA, INS, P1, P2, Lc, Le, then data), which is why they are identical in
every executable.

Nothing in steps 1–4 sets a failure bit. **A dongle that does not answer produces
no error message at all** — it just leaves the banner empty, and the version
comparison downstream is what fails. That is why the keyless patch had to remove
the *version* complaint and not a *dongle* one (`docs/research/17`).

---

## 5. The record

The 100 bytes from READ BINARY are XORed with the Borland/Delphi LCG:

```
x = 0x01BB253A                       ; identical in all 61 executables
for i in 0..99:
    x = (x * 0x8088405 + 1) mod 2^32
    rec[i] ^= (x >> 24) & 0xFF
```

Only three fields are ever read:

| offset | length | use |
|---|---|---|
| `0x00` | 3 | family — `stricmp` against `"IGO"` |
| `0x03` | 2 | territory — substituted into `"Version 2008 (%s)"` |
| `0x13` | 6 | title — `stricmp` against `"IGO 08"` |

Everything else is ignored. The banner is `sprintf`'d into a 30-byte buffer in
DGROUP; the six per-title content keys are constants the routine then writes at
**banner + 30**, which is where `docs/research/16`'s fixed-offset rule comes
from. A `strstr` for `"Version 2008"` follows and sets bit `0x1000` of the
returned status when it fails, but that bit is dropped when the status is
latched into the flags byte, so it never becomes visible.

**The banner must equal `MAIN.SET["Version"]`**, compared with `strcmp` — case
sensitive, unlike the two record fields. For this image, decrypting
`\FOTO\SETTINGS\Main.set` with key `0x00016295` (the same key as the 1999 build,
per `docs/research/08`) gives:

```
Profil   'ES_E_EU'      Country  'ES'      Version  'Version 2008 (ES)'
```

so the territory field must be `ES`.

---

## 6. The other token: DS1982

Same UART trick as ever, at I/O `0x268`, mode `3` (`sub_1253` in the game's own
segment, not the reader module). The driver is a full AN214 implementation with
the standard Maxim CRC8 table copied into the object at construction.

* `sub_126B` — reset, `33` READ ROM, 8 bytes, CRC must land on zero. The whole
  read is performed twice and the two copies must match; up to 6 attempts.
* `sub_135F` — reset, `CC` SKIP ROM, `F0` READ MEMORY, address `00 00`; the CRC
  over `F0 00 00` is verified against the byte the part returns, then 128 bytes
  are read and their CRC verified. Also done twice and compared, up to 6
  attempts.

The protection routine then requires:

1. **ROM byte 0 non-zero** — the buffer is zeroed before the call, so this is
   simply "the read succeeded". A real DS1982 answers family code `0x09`.
2. **memory[5 .. 29] == `"Photo Play 2000 Version 3"`**. The expected string
   lives in the data segment XOR `0x7C`.

Failure sets bit `0x01`, which is what prints `DS1982 FAILED`.

There is an anti-patch trick worth knowing about. Each byte is compared, and on a
mismatch the routine does `banner[i] ^= 0xFF` and then compares *again*:

```
mov  dl,[expected+si] ; xor dl,0x7C
cmp  [read+si],dl
jz   +5
xor  byte [banner+si],0xFF     ; sabotage first
cmp  [read+si],dl
jz   +9
or   word [flags],1            ; only then set the failure bit
```

Neutralising the second comparison suppresses the message but leaves the banner
corrupted, so the version check fails instead. An emulator that serves the right
string never trips it.

---

## 7. What an emulator has to do

1. Present a 16550 at `0x2F8` that survives the init sequence in §1.
2. Take `r0 r1` in the clear, seed the inbound stream, decrypt `r2 r3`, seed the
   reply codec with them (or with `0xAB, 0xD9` for reader command `0x04`).
3. Answer reader command `0x0A` with **exactly 9 bytes** of anything.
4. Answer the `6E` link frame with **exactly 5 bytes** that XOR to zero.
5. Answer VERIFY with `9000` in a 6-byte frame; answer each of the three SELECTs
   with a 22-byte body plus `9000` in a 28-byte frame.
6. Answer READ BINARY with the 100-byte record, LCG-encrypted under
   `0x01BB253A`, plus `9000` — 106 bytes.
7. Build the record with family `IGO`, title `IGO 08` and the territory that
   makes the banner match this image's `MAIN.SET["Version"]`. Read it from the
   settings file; never infer it from the image's filename
   (`docs/research/08`).
8. Serve a DS1982 on `0x268` whose ROM starts `09` and whose memory holds
   `Photo Play 2000 Version 3` at offset 5.

Byte counts are not cosmetic: getting them right is the difference between an
instant boot and a minute of timeouts.

Do all eight and the original image needs no patching — and keeps its encrypted
content databases, which the keyless build had to replace wholesale.

## 8. Where this lives in PeepeeBox

`src/device/dongle_igo8.c`. It attaches to COM2 through the normal
`serial_attach()` path, so 86Box's own 16550 handles the divisor, the line
control and the status bits, and this file only sees bytes. `dongle_photoplay.c`
brings it up next to the iButton, because all three tokens belong to the same
piece of hardware from the guest's point of view.

Two details worth keeping if this is ever rewritten:

* **The record is built from `photoplay_image_ident()`**, i.e. from the image's
  own `MAIN.SET`, for the reason `docs/research/08` gives: a cabinet's dongle
  always matched the disk it shipped with, and image *filenames* lie.
* **A stray byte has to be tolerated.** Initialisation writes `0xF0` to the
  transmit register before any frame, so a reader that starts assembling at the
  first byte it sees is permanently one byte out of step. A character-gap timer
  resyncs on any lull, which also covers the MCR toggle between retries.

The two keystreams and the record cipher were checked against
`igo8_dongle.py` — the same seeds produce byte-identical output from both.
