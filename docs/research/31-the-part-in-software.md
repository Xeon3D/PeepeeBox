# Phase 31 — the part in software

Marcos asked whether the dumped dongles could be emulated on the Linux box to get what
I.G.O. 1 and I.G.O. 3 need. The answer turned out to be better than emulation: I.G.O. 3
needs neither a dongle nor its dump. Its key was fitted from data already in the
collection, and a program with no hardware attached now answers every query the part
would have.

All three releases behind this cipher are solved, and two earlier conclusions are
withdrawn — one of them from the first version of this document.

| | password pair | key | initial state | how it was obtained |
|---|---|---|---|---|
| I.G.O. 2 | `68BB/1329` | `3B227944` | `0x7DF` | fitted twice — from the capture, and from I.G.O. 4's plaintext |
| I.G.O. 3 | `6B91/24A3` | `AB32E970` | `0x5DF` | fitted from I.G.O. 4's plaintext; **no dongle exists for this pair here** |
| I.G.O. 5 | `6B91/24A3` | same as I.G.O. 3 | | shares the pair |
| Photo Play 2001 | `7477/7D57` | `CF47CB42` | `0x7DF` | fitted from 2000's plaintext, once the stream origin was right — § 6 |

## 1. The round hands over its own answers

The keyed round is an LFSR, and the part enters it in exactly one place: the shift
decision.

    c_j = p_(j-1) XOR (v_(j-1) & 1)        v_j = (v_(j-1) >> 1) XOR (c_j ? POLY : 0)

Thirty-nine steps of that, with `v_0` fully shifted out by the end, leave

    v_39 = XOR over j of  c_j * (POLY >> (39-j))

`POLY` is `0x80500062`, whose top bit is set, so `POLY >> (39-j)` has its leading bit at
position `j-8`. The system is triangular: **`c_8 .. c_39` are uniquely determined by the
round's output**, by back-substitution from bit 31 downwards. Only `c_1 .. c_7` fall off
the end of the register unconstrained — 128 candidate paths per round, not 2^39.

That is the whole reason none of this needed more hardware time. Every `(input, output)`
pair already recorded is also a record of thirty-two of that round's answers.

## 2. What the wire says the part actually is

`docs/research/evidence/igo2-dongle-wire-2026-09-06.log.gz` holds 118 rounds the game
drove through the passthrough. Collapsing the transport's four-fold repeats and matching
`pay, pay|0x10, pay, read_status` gives 4,720 consultations, in 118 runs of exactly forty.

Three facts fall out with no model at all:

- **The part sees five bits, not eight.** The query framing is
  `((q<<1)&0x0E) | ((q<<2)&0x60) | 0x80`, which carries `q` bits 0–4 and drops 5–7.
  Thirty-two distinct queries appear in the trace and no more.
- **The answer is deterministic in the query history** — 0 clashes over every shared
  prefix — so the preamble really does reset it and nothing else varies.
- **It is not a lookup.** The answer is not a function of the query, nor of
  (position, query), nor of the previous query or answer, nor of any running sum, xor or
  parity. A search for a consistent 256-entry byte→bit table across all 46,036 captured
  rounds finds none either.

The model's candidate paths were checked against the wire before anything was built on
them: for both calibration rounds, one of the 128 candidates reproduces the real round's
forty queries *and* its thirty-nine answers exactly.

## 3. The state machine, and the password that confirms it

`io.hasp4` — `batteryshark/dongle-lab`, the public project Marcos found in
`docs/research/23`, not the clean-room HASP4 work — models the oracle as a shift register
`cur` seeded from the password, plus a 32-bit key indexed by the five-bit query. Its
answer is

    ((cur >> 11) ^ key_bit) & 1

and the bit shifted in during that step lands at bit 0, so bit 11 of the updated register
is known *before* the key bit is. **Every observed answer therefore names its key bit
outright.** No search: one pass over the trace either produces a consistent key or
contradicts itself.

The model allows only 32 initial states. Exactly one of them, `0x7DF`, is consistent with
all 4,720 observations, and it yields a complete key, `3B227944`.

The confirmation is that `0x7DF` is not a fitted constant. Running the model's password
schedule on `0x132968BB` — the literal `push 0x132968BB ; pass2:pass1 = 1329:68BB` in
FINDIT.EXE, quoted in `scratchpad/decodedata.py` — gives `0x7DF`. Of the four plausible
password forms only that one does.

## 4. Checked against the real part, every round

`tools/dongcap/softpart.py verify` runs the software part over the whole capture:

    I.G.O. 2 software part against the real one: 46036 of 46036 rounds agree, 0 wrong

`capture-igo2/DONGCAP.BIN` is now a regression test rather than a dependency.

## 5. Fitting a generation that has no dongle here

Because § 1 forces thirty-two answers per pair and § 3 turns an answer into a key bit, a
key can be fitted from any block whose plaintext is known — and `mklist.solve_from_plain`
already turns a (plaintext, ciphertext) block into the two round pairs. So a release whose
photographs ship in the clear in *another* release needs no hardware:

| enciphered | plaintext twin | result |
|---|---|---|
| I.G.O. 2 FINDIT | I.G.O. 4 FINDIT | `3B227944` — the same key the hardware gave |
| I.G.O. 3 FINDIT | I.G.O. 4 FINDIT | `AB32E970` |

Nine pairs settle it in both cases. I.G.O. 2 is the control: fitted from I.G.O. 4 alone,
with the capture never opened, it lands on the key the part itself produced.

Then, with no dongle in the room:

    /FINDIT/PICS/FOTOPLAY.WAD: 1397 of 1397 entries decrypt to a picture header
    /AMORE/COMIX/FOTOPLAY.WAD: 332 of 332 entries decrypt to a picture header

for **both** I.G.O. 2 and I.G.O. 3 — AMORE included, which has no plaintext twin
anywhere and was the one archive the capture was genuinely needed for.

And the thing that stops I.G.O. 3 booting. `docs/research/20` § 3: it calls EncodeData over
twenty bytes at `DS:0x50F6` before anything else and reports `dongle error` when that
fails. The software part transforms the first block to

    63 3a 2f 66 6f 74 6f 2f   =   c:/foto/

A wrong key gives eight random bytes. This one gives a path.

## 6. Photo Play 2001, and the origin that hid it

**This section first said 2001 was a different cipher. It is not, and the correction is
the useful part.**

Reading `FINDIT.EXE` settles the framing: 2001's walker at `0x2EB26` is the same design as
I.G.O. 2's -- 8-byte blocks, the 26-entry schedule `k[0]=D; k[j]=k[j-1]+acc;
k[1]^=ROR32(D, k[j-1]&31)`, CBC under a zero IV, and the loop stopping at `nblocks-2`. Its
decode at `0x2D04C` is the same two stages: six rounds of `ROL32(L ^ 0x5B2C004A, 25..0)`
then the keyed round, then six of `ROL32(L ^ 0x803425C3, 10..0)` and another. The LFSR at
`0x2CD0C` builds its polynomial as `[bp-2]=0x8050, [bp-4]=0x0062` -- a `long` whose low
word is `0x0062`, so **`0x80500062`**, the same one. `notes/HANDOFF2001.md` § 20.1 records
it as `0x00628050`, which is those two words written in the wrong order.

So the cipher is identical, and what was wrong was where its stream starts. § 8 found
2001's first 128 bytes are a PCX header under a separate XOR keystream; the picture cipher
begins **after** it, at byte 128. Every earlier test compared 2001's byte 0 against the
twin's byte 0 and was comparing two different layers.

The test that showed this uses no oracle at all. Block 0's two answers come out of
`solve_from_plain` whatever the key is; those build the schedule; block 1 is then pure
software. Landing on the twin's plaintext is a 64-bit coincidence otherwise:

| stream at | plaintext at | FINDIT | AMORE |
|---|---|---|---|
| 0 | 0 | 0/25 | 0/25 |
| 0 | 128 | 0/25 | 0/25 |
| 128 | 0 | 0/25 | 0/25 |
| **128** | **128** | **25/25** | **25/25** |

With the origin right, 2001 fits like the others: **`CF47CB42`**, register `0x7DF`, settled
after nine pairs, and **FINDIT and AMORE give the same key independently**. Decrypting
whole buffers against the 2000 plaintext -- block 0 through the software part, the rest
through the soft round and the CBC XOR -- gives 82,094 blocks right and 16 wrong in FINDIT,
156,509 right and 61 wrong in AMORE, the residue being each entry's final short chunk and
the two entries whose sizes differ between the releases.

One thing is recorded rather than derived. `initial_state()` turns the password into the
register correctly for the I.G.O. family, but 2001's fitted `0x7DF` is what neither order
of `7477/7D57` produces (they give `0x55F` and `0x5DF`). The 2001 library is a different
build and Phase 20 already found it differs in word order, so that is where to look; until
then `softpart.py` carries 2001's register as measured.

What made the earlier claim look strong was a control that was itself misaligned: I.G.O. 2
and 3 lit up at `0, 4096, 8192, ...` and 2001 nowhere, which reads as decisive until you
notice 2001 was never scanned at the offsets where its blocks actually are.

## 7. What the dumps contributed, and what they did not

The nine `hasp.dmp` files gave the password pairs and the byte order (Phase 20), and they
are why the `6B91/24A3` pair was known well enough to seed the search at all.

They did **not** give the key. The fitted `3B227944` does not appear anywhere in the
2002PT dump under any of `io.hasp4`'s four table-indexing modes, at any of the 703
offsets. Whatever the 166-byte block at `0x009` is, it is not those 32 bits laid out
plainly, and the working route turned out not to need it.

## 8. Correction: QUIZPRO2 and 2001's headers

`docs/research/30` § 11.5 said QUIZPRO2 shares a first block with 2001's FINDIT because
both are PCX headers under an unrelated per-archive LCG key. That was wrong, and so is
`docs/research/23` § 3's premise that 2001's block 0 is the PCX header seen through the
dongle transform.

Measured: bytes 74..127 — the 54 zero bytes that pad every PCX header, so the ciphertext
there *is* the keystream — are **identical**, 54 of 54, across I.G.O. 2's QUIZPRO2,
I.G.O. 3's QUIZPRO2, 2001's FINDIT and 2001's AMORE. Bytes 8..15 differ, which is exactly
where PCX headers carry their dimensions, and the difference does not propagate: byte 16
onwards agrees again. That is an XOR keystream, not a block cipher, and it is the same
keystream under the same key in all four.

So 2001's photo archives are `[128-byte header under the keystream][body under the
picture cipher]`, and QUIZPRO2 is the first half alone with a plaintext body — 80 of 80
entries end in a valid PCX palette marker. § 11.5's conclusion that QUIZPRO2 needs no
dongle stands. Its reasoning does not, and 2001's header layer is not the dongle's work.

## 9. In the device

`src/device/dongle_photoplay.c` now answers the keyed round itself, so no capture and no
part is needed at run time. `hd_keys[]` carries each release's key and register beside
the password it already held, and `t_data()` picks the queries out of the raw DATA stream.

Two clock edges do it, and they are on different lines, which is what keeps this clear of
the Microwire decoder that shares the same DO:

| edge | meaning |
|---|---|
| DATA bit 0 rising, bit 7 set | a command byte — the round preamble, so reset the register |
| DATA bit 4 rising, bit 7 set | a query — answer it and hold the bit for the next STATUS read |

Query payloads never move bit 0 and command bytes never move bit 4, so neither is taken
for the other. The answer is latched and consumed by one STATUS read, ahead of the record
path, because a query is three DATA writes and one read with nothing in between.

Checked by replaying the recorded wire into exactly that logic: **4,720 answers given,
4,720 agreeing with the real part, none wrong** — and 4,720 is the number of queries in
the trace, so the edge detection neither invents a query nor misses one.

I.G.O. 6, 7 and Italy carry no key. Their pictures are plain GIF, so there is nothing to
decrypt and a key there would only be a guess at a part no game asks.
