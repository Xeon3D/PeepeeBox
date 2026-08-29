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

## What implementing it changed

Written up before the device existed, so read the above as the design and this as
what the wire actually did once something answered. The menu now boots on an
untouched 2000 DE image and `CDONGLE not found` is gone.

**The key is fixed, not running.** `0x0788` is `xor al,[cs:0x1AD]`, one byte of
the code segment, written once per transaction by `0x076B` (`key = 0`; send the
nonce; `key = nonce ^ 0xD3`). "Running key" above is loose wording — there is one
key and it applies to every byte in both directions.

**Between the nibble writes the host reads the CONTROL port, not STATUS.** The
trace transcribed above condensed both as status reads, which made the ready poll
look like the only place ACK is sampled by accident rather than by design. It
really is: STATUS is read twice before the poll loop and nowhere else during a
send. That is what lets an emulated dongle go ready the moment a command byte
lands, without knowing how long the command is.

**Every write is answered before the next is issued, including the handshake's
last.** Moving to the streaming state on the `8F` write puts a data bit under the
read that `8F` is owed — which must be clear — and the library gives up with
error `0x17`. The device has to hold until the host clocks the first bit out with
`CF`. This was the only bug between a decoded protocol and a booting menu.

**The reads are short, and each starts at the beginning of the record.** The
menu's two transactions ask for one byte and then two, not 48: `V`, then `Ve`.
Both are satisfied by streaming from offset 0, so whatever the command bytes
select, it is not an offset into the record — at least not for these. The command
is `{AC, CB}` for the one-byte read and `{A0, 86, 2E, D0}` for the two-byte one,
descrambled.

**A game runs the same two transactions, byte for byte — and still fails.**
Launching `SOLI.EXE` produces a third `{AC, CB}` then `{A0, 86, 2E, D0}` pair,
identical to the menu's two, and then reports `PDONGLE FAILED`.

This was briefly written up here as the game passing, on the strength of the log
showing both transactions completing. That was wrong, and the mistake is worth
naming: **completing the handshake is not passing the check.** The transport
succeeds and the guest then rejects what it was told. Only the screen settles it.

So the menu is satisfied by these answers and the games are not, from identical
wire traffic. The difference is in what each program does with the two bytes it
gets back, not in the exchange that delivers them.

**The nonce is not random, and on a cabinet it is always zero.** `0x075E` is:

```
    push es / mov ax,0 / mov es,ax / mov ah,[es:0x0440] / pop es / ret
```

Linear `0x440` is `0040:0040` — the BIOS data area's **floppy motor turn-off
counter**. It is non-zero only for the couple of seconds after a floppy access,
and a Photo Play cabinet never touches a floppy, so in practice the nonce is `00`
and the key is always `D3`. Every transaction observed, menu and game alike, uses
`00`.

That matters because it retires a success criterion carried over from the 1999
dongle, whose nonce comes from `rand()`: **for the 2000 generation a constant
nonce is correct, not a sign that the challenge/response is faked.** Do not go
looking for a bug when it does not vary.

The cost is that the key derivation has only ever been exercised at `nonce = 0`.
`nonce ^ 0xD3` is read straight off `0x077D` and both ends derive it from the
same transmitted byte, so it is hard to see how it could be wrong — but it is
untested for any other value. To force one, get the guest to touch a floppy
immediately before a check, or seed `0x440` non-zero from the emulator.

**The `D0` trailer is explained: it marks the command byte.** It does not come
from `0x1187` at all. `0x11FA`, which opens every transaction, is:

```
    call 0x76b        ; send the nonce, set the key
    call 0x11a0       ; send a byte as the five nibble writes
    or al,0xd0 / and al,0xf0 / out dx,al      ; -> exactly D0
```

So `D0` trails the byte `0x11FA` sends — the command — and `DF` trails every
ordinary byte after it. It is a frame type, not an anomaly, and it makes the
command byte identifiable on the wire without descrambling anything.

## The record model in this document is wrong

The transport above holds; it is confirmed twice over and the guest talks to it
happily. What does not hold is the premise that the guest reads a 48-byte record
and string-compares it. **There is no 48-byte read anywhere in this library.**
`0x0F6F`'s `bx` is a *pair* — `bh` bytes to send, `bl` bytes to read — and every
call site uses small counts:

| | |
|---|---|
| `mov bx,0x302` | send 3, read 2 — the workhorse, and transaction 2 |
| `mov bx,0x203` | send 2, read 3 |
| `mov bx,0x1` / `0x2` / `0x4` | short reads |

`strstr(record, "Version 2000")` and the 48-byte block are the **1999** grammar
(type 3, receive 48), carried across to this generation by assumption when this
document was written. The 2000 device answers small queries; it does not hand
over a record. Serving it record bytes from offset 0 satisfies the menu by luck
and tells the games nothing they will accept.

## What the two transactions actually are

**Transaction 1 is the parallel-port autodetect**, at `0x0BAD`: reset pulse
train, `0x11FA`, `mov al,0xCB`, send, read **1** byte. What follows it is a loop
over `0x278` / `0x378` / … deciding which port the dongle is on. It is not a
protection check.

**Transaction 2 is a generic API call**, at `0x0D10`: reset, `0x11FA`, send `bh`
bytes from `DS:SI`, read `bl`. The dispatcher at `0x06D6` builds the command byte
from the API function number in `ah`:

```
    cmd = nibbleswap(ah + 0x9F) ^ 0xA3
```

which for the observed `0xA0` gives `ah = 0x91`. `al` is a caller parameter,
preserved across that transform and sent as part of the payload — so `{86, 2E,
D0}` carries an argument this end currently ignores.

## What is still needed

The transport is done and proven. What is not known is **what the two bytes
returned by command `0xA0` are supposed to be.** They are not record bytes. The
next step is on the guest side rather than in this library: find what `MENU.EXE`
and a game EXE each do with that answer, and why one accepts `Ve` and the other
does not.

## Method note

The two `MENU.EXE` patches used to get past the check and reach the games — neutering the
`or ax,0x08` at `2A10:0885` and the version compare's `jz` at file `0x10937` — were made
on a scratch copy only, to find out what the games check. They are not needed by, and
have nothing to do with, the emulation described here.
