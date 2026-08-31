# Phase 23 — the picture cipher's keyed round, in software

Marcos found `batteryshark/dongle-lab`, `projects/io.hasp4`. Its
`src/hasp4_core.c` contains **the function this project has been missing**.

## 1. It is provably the same algorithm

`HaspDecodeBlock` is, line for line, what `HANDOFF2001.md` § 20 reconstructed out
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

## 4. The next experiment, precisely

The **password-zero path was never tested**, because `column_mask` and
`crypt_init_vect` are not known and are not obviously in the dump. They are one
byte each and only six bits of the second are used, so the whole space is at most
16384 keys against a fixed security table — a few minutes of brute force against
eight known plaintext bytes, and decisive either way.

After that, in order: how the caller derives the 32-bit password from the pair;
whether `use_chained_blocks` is on, which changes the framing from ECB; and
whether the 4 KB buffer split in `0x2EB26` means only the first block of each
buffer takes this path, as `HANDOFF2001.md` § 24 found.
