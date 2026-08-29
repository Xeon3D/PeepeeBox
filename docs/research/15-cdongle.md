# Phase 15 — The 2000 generation's second dongle, on the wire

Opened while trying to boot four untouched Photo Play 2000 images (DE, IT ×2, NL). The
menu stops at `CDONGLE not found`; the games stop at `PDONGLE FAILED`.

## Those two messages are the same check

They are not two tokens. `MENU.EXE` and every game EXE call the same library routine,
read the same 48 bytes, run the same test on the result and set the same status bit.
Only the wording of the message differs:

| | `MENU.EXE` | game EXEs (`FMEMO`, `AMORE`, …) |
|---|---|---|
| read | 48 bytes, one routine | same routine |
| test | `strstr(record, "Version 2000")` | same |
| bit set on failure | `0x08` | `0x08` |
| message for `0x08` | `CDONGLE not found` | `PDONGLE FAILED` |

So the 2000 generation has **one** unsolved token, not two. Chasing "PDONGLE" as if it
were the 1999 dongle under a new name is a dead end — see below.

## This build tests exactly two tokens

The status word is assembled in one function (`MENU.EXE` at `2A10:0850`, and the same
code inside each game). Walking it end to end, only two bits are ever set:

| bit | meaning | message | state |
|---|---|---|---|
| `0x01` | DS1982 iButton | `DS1982 not found` | **already emulated, answers correctly** |
| `0x02` | DS1425 | `DS1425 not found` | never set — dead branch |
| `0x04` | the 1999 dongle | `dongle not found` | never set — dead branch |
| `0x08` | this dongle | `CDONGLE not found` / `PDONGLE FAILED` | **the blocker** |

The `0x02` and `0x04` branches are inherited from the shared codebase and unreachable in
this generation. A live trace confirms the iButton half: `READ ROM`, `SKIP ROM`,
`READ MEMORY` ×2, all answered.

## It is not the 1999 dongle relabelled

Checked directly against a 1999 image's `MENU.EXE`:

| | 1999 | 2000 |
|---|---|---|
| `CDONGLE not found` string | absent | present |
| the bit-bang library below | absent | present |
| banner | `Version 99` | `Version 2000` |

The 1999 build has neither the string nor a byte of the code. This is a second,
different device with its own transport, added for the 2000 generation. The 1999
transport (STROBE framing, BUSY/nibble handshake, `Docs/07`) is not involved.

The `5A`/`A5` presence probe that `Docs/09` recorded on IGO8 and attributed to the
NG-DONGLE is **this library's** probe, byte for byte. The 2008 generation is running the
same SDK family, so what is written here should carry to it.

## What the record has to contain

The 48 bytes are checked twice, and one record satisfies both:

1. `strstr(record, "Version 2000")` must be non-NULL — the plain release, no territory.
2. `strcmp(record, MAIN.SET["Version"])` must be 0 — the full string *with* territory,
   e.g. `Version 2000 (DE)`.

So the record starts with the NUL-terminated `MAIN.SET["Version"]` string. Test 1 is
implied by test 2. Nothing else in the 48 bytes is examined by these two checks.

## The transport

Everything happens on the parallel port. The library selects it by a small port number
(`0x0B` in these builds) rather than an address; `1..3` and `0x0B..0x0D` both map to
LPT1..LPT3. Data goes out on the data port, and **the only line coming back is STATUS
bit 6 (`0x40`, /ACK)**.

### Sending a byte

`0x05F4` writes a byte as five data-port writes, low nibble first, with the top bits
acting as clock and framing:

```
    out  0xF0 | (b & 0x0F)        ; low nibble presented
    out  0xC0 | (b & 0x0F)        ; clock
    out  0xF0 | (b & 0x0F)
    out  0x90 | (b >> 4)          ; high nibble presented
    out  0x80 | (b >> 4)          ; clock
```

`0x05E1` calls that and adds a trailing `0xDF` (`or 0x90`, `or 0x4F`) as the strobe. On
the wire a byte is therefore the very recognisable `F? C? F? 9? 8? DF`.

### Receiving a byte

`0x114C`, eight times round, **MSB first**:

```
    out  0xCF                     ; clock low  -> dongle presents the next bit
    in   STATUS                   ; bit 6 is the data bit
    shl  al,2 / rcl ah,1          ; bit 6 -> carry -> into the byte, MSB first
    out  0xFF                     ; clock high -> advance
```

### The scramble

Both directions are XORed with a running key (`0x0788`: `xor al,[cs:0x1AD]`).

```
    key  = 0
    send(nonce)                   ; nonce = the byte at 0000:0440
    key  = nonce ^ 0xD3
```

The nonce is whatever happens to be in low memory, so it is effectively random per run —
but it is **transmitted to the dongle in clear** (key is still 0 at that point). An
emulated dongle therefore does not have to guess it: capture the first byte of the
transaction and use `nonce ^ 0xD3` for everything afterwards.

### The attention handshake

Before streaming bytes, `0x1067` writes four pairs and demands a specific ACK level after
each. Failure gives error `0x17`:

| write, then write | required STATUS bit 6 |
|---|---|
| `0xDF`, `0xEF` | set |
| `0xBF`, `0xCF` | clear |
| `0x9F`, `0xEF` | set |
| `0xBF`, `0x8F` | clear |

Reading the pairs as (previous, current), the required ACK equals **bit 5 of the byte
just written** — `0xEF` and `0xFF` have it set, `0xCF`, `0x8F` and `0x9F` do not. That is
consistent with ACK being driven from bit 5 while the dongle is idle, and carrying data
bits once streaming starts.

### Ready, and the timeout that is being hit

`0x0F6F` waits for ACK to go **set**, `mov cx,0x64` — a hundred polls — then writes
`0xCF` and waits for it to go **clear**, then runs the handshake and reads `bx` bytes.
Error codes seen in this path: `0x05` timeout, `0x11`, `0x17` handshake failure.

## The captured trace

PeepeeBox with `PEEPEEBOX_LPT_TRACE=1`, booting an untouched 2000 DE image. `W` = data
write, `C` = control write, `r` = data read, `s` = status read, with the value:

```
W55 r55                          bus test: write 0x55, read it back
WFC WCC WFC  W9A W8A  WDF        byte 0xAC + strobe
s00                              one status read
W1C  C04  WBF W7F WBF            control, then a reset pulse train
WF0 WC0 WF0  W90 W80  WDF        byte 0x00
WFF WCF WFF  W97 W87  WD0        byte 0x7F
WF8 WC8 WF8  W91 W81  WDF        byte 0x18
s00 x ~100                       the cx=0x64 ready poll, timing out
```

…and the whole thing three times, which is the retry count above the read.

The hundred `s00` are the smoking gun: PeepeeBox returns `0x00` from STATUS, so ACK never
rises, so the library gives up and reports the dongle missing. Nothing is wrong with the
guest — it is waiting for a line nothing drives.

## What this leaves

Enough to implement: the framing both ways, the ACK semantics, the handshake pattern, the
key derivation, and the exact content the record must carry. Not yet established:

- what the command bytes mean (`0xAC`, and `0x7F`/`0x18` after descrambling) — an
  emulated dongle may not need to care, since it controls the reply;
- whether anything beyond the banner in the 48 bytes is used later, the way the 1999
  dwords turned out to be picture keys (`Docs/14`). Nothing in the boot path reads them.

## Method note

The two `MENU.EXE` patches used to get past the check and reach the games — neutering the
`or ax,0x08` at `2A10:0885` and the version compare's `jz` at file `0x10937` — were made
on a scratch copy only, to find out what the games check. They are not needed by, and
have nothing to do with, the emulation described here.
