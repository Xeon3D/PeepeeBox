# Phase 23 — the picture cipher's keyed round, in software

Marcos found `batteryshark/dongle-lab`, `projects/io.hasp4`. Its
`src/hasp4_core.c` contains **the function this project has been missing**.

## 1. It is provably the same algorithm

`HaspDecodeBlock` is, line for line, what `notes/HANDOFF2001.md` § 20 reconstructed out
of `FINDIT.EXE`:

| | reverse-engineered here | `hasp4_core.c` |
|---|---|---|
| Feistel A constant | `0x5B2C004A` | `0x5B2C004Au` |
| Feistel A rotations | 25, 20, 15, 10, 5, 0 | `for (s = 25; s >= 0; s -= 5)` |
| Feistel B constant | `0x803425C3` | `0x803425C3u` |
| Feistel B rotations | 10, 8, 6, 4, 2, 0 | `for (s = 10; s >= 0; s -= 2)` |
| LFSR polynomial | `0x80500062` | `0x80500062u` |
| keyed round | twice, between the stages | `HaspTransformWord`, twice |

Four independently derived constants and both rotation schedules agree. There is
no doubt this is the cipher.

**And it gives us the keyed round in software.** `HaspTransformWord` is the
byte-to-bit oracle we could only ever watch on the wire: 39 LFSR steps, each
consulting one byte of the running word and folding one bit back. It needs no
hardware. That is the whole reason 2001, I.G.O. 2 and I.G.O. 3 have been stuck.

## 2. What it says about the key material

Two paths, chosen by whether a password was supplied:

* **password non-zero** — the LFSR is seeded from `password ^ 0x01081989` and the
  only other input is an **8-byte security table**. The project's `PROFILE_FORMAT.md`
  says `SecTable` is "exactly 8 bytes; derived from `PW` when omitted", which
  matches this corpus exactly: the blob at `0x007` in the h5dmp dumps is a pure
  function of the password pair (`docs/research/20` § 3), and its first eight bytes
  are the table. `HASP4 Emulator/Docs/00` parked that blob as "provably unused";
  it is the security table, and it is the dongle side of the cipher.
* **password zero** — seeded instead from `column_mask` and `crypt_init_vect`, one
  byte each, through `HaspPrepareTransformKey`.

## 3. Where it stands

The transform is ported to `scratchpad/hasp4.py` and the port is **faithful**:
`encode_block(decode_block(x)) == x` on 20/20 random blocks.

There is also a clean oracle to test it against. Every 2001 FINDIT picture shares
the first ciphertext block `d2 46 3a 28 cf f9 62 d6`, and the matching plaintext is
known from the 2000 image plus its LCG header key: `0a 05 01 08 00 00 00 00` — a
plain PCX header. Eight known bytes, one known block.

**It does not decrypt yet.** A sweep of the obvious parameter space — every 8-byte
window of the 2001 dump as the security table (187 of them), five password
encodings, both directions, and both readings of the table's bit index — is 3740
combinations and produces no hit; the best is 2 bytes of 8, which is chance.

## 4. Both key paths are now ruled out, for this framing

The password-zero path was brute-forced: **every 8-byte window of the 719-byte dump as
the security table (711 of them), both readings of the table's bit index, and all 256 x
256 column_mask / crypt_init_vect pairs** -- about 93 million keys, in C. **No hit.** The
best partial is 4 bytes of 8, and at 93 million trials chance alone predicts about one
such near-miss, so there is no signal in it.

Together with the earlier 3740-combination password sweep, that closes both paths against
the pair we have.

The transliteration itself is not in doubt. `HaspDeriveUniversalSecTable` reproduces the
project's own test vector exactly -- password `0x1234ABCD` gives
`C7 05 C3 01 F6 34 F2 30` -- which also confirms the 32-bit password is
`(pass1 << 16) | pass2`. (That universal table is a fallback for dongles with no dumped
table; it does not match ours, as expected, and using it does not decrypt either.)

## 5. So the algorithm is right and the framing is wrong

What the sweep actually rules out is narrow: that **the file's first eight bytes, fed to
this transform, yield the PCX header**. One of those two ends must be wrong, and the
evidence says it is the input.

The 2001 and 2000 archives hold the same 1397 pictures at **identical sizes**, so every
block is a known plaintext/ciphertext pair once the 2000 LCG header layer is undone. Over
one picture's 8637 blocks, one plaintext block appears with **two different ciphertexts**.
The cipher is therefore position-dependent, not ECB -- which is exactly `notes/HANDOFF2001.md`
section 24's model: 4 KB buffers, only the first block of each taking the dongle path,
the rest going through the software round at `0x2E682`.

So the block that reaches `0x2D04C` is not simply the file's first eight bytes. Section
24.1 already found `0x2E44D` marshalling **two** buffers and setting `state[+0x16] = 2`
before dispatching, and that marshalling is now the thing to read -- with the advantage,
new today, that its output can be checked against a working software model instead of
guessed at.

## 6. What this is worth anyway

Even unfinished, the find removes the reason the cipher was called unsolvable. It was
recorded as something only the dongle could compute; it is not. It is 39 LFSR steps over
an 8-byte table, and it runs anywhere. What remains is bookkeeping about which bytes go
in -- a tractable problem, and one this project has the known-plaintext corpus to settle.
