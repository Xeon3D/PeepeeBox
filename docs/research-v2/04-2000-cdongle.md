# 4. The 2000 generation — CDONGLE

A **second, later parallel dongle**, sharing the port with the 1999 design and nothing
else: the 1999 build contains none of this code, and the 1999 guest drives only `E0..FF`
on these lines, so the two can coexist on one emulated port without ambiguity. The menu
calls it `CDONGLE` and the games call it `PDONGLE`; it is one device.

The DS1982 of `03` is still required alongside it.

*State:* **complete.** All four Photo Play 2000 images (DE, IT ×2, NL) boot, run their
ordinary games and display pictures in every photo game — played manually, on screen.

## 4.1 Transport

Everything is on the parallel port. Data goes out on the DATA port; **the only line
coming back is STATUS bit 6 (`0x40`, /ACK)**. The library selects the port by a small
number (`0x0B` in these builds; `1..3` and `0x0B..0x0D` both map to LPT1..LPT3).

**Transaction start.** The library opens every transaction with a `BF 7F BF` pulse
train. It is the one unambiguous frame marker on this wire — use it to drop half-decoded
state. It is *not* reliable as the only marker, though: the record read follows the
previous transaction with no pulse train at all.

**Sending a byte** — five DATA writes, low nibble first, top bits acting as clock and
framing, then a trailer:

```
out 0xF0 | (b & 0x0F)      ; low nibble presented
out 0xC0 | (b & 0x0F)      ; clock
out 0xF0 | (b & 0x0F)
out 0x90 | (b >> 4)        ; high nibble presented
out 0x80 | (b >> 4)        ; clock
out 0xD?                   ; trailer
```

So a byte is the recognisable `F? C? F? 9? 8? D?`. **The trailer says what the byte
was**, and that is what a parser must key on, not the pulse train:

| trailer | meaning |
|---|---|
| `0xD0` | this was the **command** byte |
| `0xDF` | an ordinary byte — a nonce (before the command) or an argument (after it) |

**Receiving a byte** — eight bits, **MSB first**, sampled off ACK:

```
out 0xCF   ; present the next bit on ACK
in  STATUS ; bit 6 is the data bit
out 0xFF   ; advance
```

**The scramble.** Both directions are XORed with one running key. The key starts at
zero, the host sends a nonce byte (whatever happens to be at `0000:0440`, so effectively
random per run) **while the key is still zero**, and only then:

```
key = nonce ^ 0xD3
```

An emulated dongle therefore never has to guess it — capture the plaintext byte before
the command and derive the same key.

**The attention handshake.** Before streaming a reply, the library writes four pairs and
demands a specific ACK level after each; failure gives its error `0x17`:

```
DF EF -> ACK set     BF CF -> ACK clear
9F EF -> ACK set     BF 8F -> ACK clear
```

The required ACK is simply **bit 5 of the byte just written**. That is consistent with
ACK following bit 5 while the device is idle and carrying data once streaming starts —
which is exactly how to implement it.

**Claiming a reply.** The host claims a waiting reply and then waits for ACK to fall.
There are two read routines and they use different bytes (`0x0F6F` writes `CF`,
`0x0FCA` writes `8F`), so keying on one value strands the other in its retry loop
forever. Keying on a bit does not work either — a frame's own `C?` write is
indistinguishable from `CF`. **What identifies a claim is its position:** immediately
after a byte's `D?` trailer, with no frame (`F?`) started since.

**States a device needs.** Idle (listening; ACK follows bit 5 of the last write) →
Ready (a reply is queued; ACK held high so the poll loop exits) → Armed (host claimed
it; ACK back low) → Handshake (the four pairs) → Stream (`CF` presents the next bit,
`FF` advances). At the end of a reply the device must go idle and let ACK follow bit 5
again, or the host is stranded in its wind-down loop.

## 4.2 The API, and the length that matters

The public entry is `dongle(func, port, in, out)`. Functions `1..8` all take the same
path — **send 3 bytes, read 2** — and the wire command byte is `func + 0x9F`
(so function 1 is `0xA0`). The picture-key calls are library functions `0x11`/`0x12`,
wire `0xAA`/`0xAB`.

Serving the wrong *length* is a real failure mode: queue a 48-byte record for a call
that reads two bytes and the device is left parked mid-record, and the next transaction
is served the leftovers. Answer two bytes for `A0`–`A7` even when the value is unknown.

The parallel-port autodetect (`0x0BAD`) reads **one** byte and looks only at whether the
transport worked — it never examines the value.

## 4.3 The licence check — one constant, everywhere

The only query this generation's protection actually turns on:

```
command 0xA0, arguments 86 2E D0   ->   reply 93 46      (the long 0x00004693)
```

and the caller does, verbatim, `cmp DWORD PTR [bp-6], 0x4693 / jne fail`.

That pair is **identical in all 29 game executables, in `MENU.EXE`, and across all four
2000 images** — one fixed pair, not per-title or per-territory.

This is **recorded, not derived**. The function inside the real device that maps a
challenge to a response is not known, and nothing can compute it for a challenge that
has not been seen. If another challenge ever turns up it belongs beside this one in a
table, and the fallback should make its absence loud rather than silent.

## 4.4 The per-picture key

Each PCX header is decrypted with a Borland LCG seeded by a per-picture key, and the
game asks the dongle for it. The request is:

```
08 00 | <const lo> <const hi> | then the name, one byte per read
count    the 16-bit constant
```

**The name is not a payload.** `0x081D` sends four header bytes and then alternates:
push one name byte, read one reply byte. So reply byte *j* has to be on the wire when
only `name[0..j]` has been seen.

That forces the whole design, and it is the only shape that works:

- **The dongle answers eight bytes, `S_j(name[j])` — byte *j* depends on `name[j]`
  alone.** The *guest* folds them into the four-byte seed, pairing `j` with `j+4`:
  **XOR** for cases 0 and 1, **ADD** for cases 2 and 3, with case 1 aliasing (it never
  reads the first two).
- The case is `(name[0]^name[1]^name[2]^name[3] ^ '.' ^ 'C') & 3` — `fnsplit` keeps the
  dot, so it is not `'P' ^ 'X'`.
- Any implementation that needs the whole name before answering passes on AMORE's
  three-character names and fails on everything longer.

**The constant names the case.** Cases 0 and 1 use `0x038B` and `0x0A8E`, hardcoded in
the game. Cases 2 and 3 take theirs from the dongle, via the `A3` and `A4` queries, and
those real values are unknown — but they need not be known: whatever the device answers
is what the game hands back in the request, so the constant is only a tag. Pick two
values that cannot collide with the two hardcoded ones.

**One inversion the wire demands.** `0x081D` does not hand the caller what it received:
it ends with a backwards nibble merge (`0x08A9`),

```
seen[i] = (recv[i+1] & 0xF0) | (recv[i] & 0x0F)
```

with `recv[8]` an extra byte fetched afterwards by `0x0FCA`. Sending the wanted values
directly therefore delivers a nibble-shifted mess. Invert it — each wire byte carries
the low nibble of its own target and the high nibble of the one before:

```c
wire[0] = r[0] & 0x0F;
for (i = 1; i < 8; i++) wire[i] = (r[i-1] & 0xF0) | (r[i] & 0x0F);
wire[8] = r[7] & 0xF0;
```

### The shape of `S_j`

The closed form is **not** known. What is known, and what the tables in
`src/device/dongle_photoplay.c` are built from:

1. `S(c) = a[hi] + b[lo]` mod 256, exactly — **separable in the nibbles**, in every
   position of every case, with no contradiction anywhere in the corpus.
2. `b` is **linear in the bits** of the low nibble, and so is `a`. Four weights give all
   sixteen low nibbles; any three of `a[2..5]` give the fourth. This is why an affine
   model in the name *bytes* was rank-deficient: the function is affine in its **bits**.

The shipped table is measured where the archives show a character and filled from that
law where they do not; entries the law cannot reach are marked unknown and **logged
rather than guessed**, so the gap stays loud.

*Verified:* **3765 of 3765** — every picture in every dongle-keyed archive of all four
2000 images predicted exactly, nothing uncovered.

Anyone who recovers the real function must reproduce this table.
