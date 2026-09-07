# Phase 31 — the part in software

Marcos asked whether the dumped dongles could be emulated on the Linux box to get what
I.G.O. 1 and I.G.O. 3 need. The answer turned out to be better than emulation: I.G.O. 3
needs neither a dongle nor its dump. Its key was fitted from data already in the
collection, and a program with no hardware attached now answers every query the part
would have.

Two releases are solved, one is not, and one earlier conclusion is withdrawn.

| | password pair | key | initial state | how it was obtained |
|---|---|---|---|---|
| I.G.O. 2 | `68BB/1329` | `3B227944` | `0x7DF` | fitted twice — from the capture, and from I.G.O. 4's plaintext |
| I.G.O. 3 | `6B91/24A3` | `AB32E970` | `0x5DF` | fitted from I.G.O. 4's plaintext; **no dongle exists for this pair here** |
| I.G.O. 5 | `6B91/24A3` | same as I.G.O. 3 | | shares the pair |
| Photo Play 2001 | `7477/7D57` | — | — | **not this cipher** — § 6 |

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

## 6. Photo Play 2001 is a different cipher

2001 does not fit, and the failure is not a near miss.

A block that really went through the keyed round admits at least one key; a keyless block
essentially never does. Scanned over every 8-byte block of the first entries, I.G.O. 2 and
I.G.O. 3 light up at exactly `0, 4096, 8192, 12288, 16384` — the 4 KB stride, and nothing
else. 2001 lights up nowhere, against either 2000 or 1999 as the plaintext twin, in FINDIT
or AMORE.

That is not a password guess: sweeping all 32 initial states the model allows, I.G.O. 2
takes `0x7DF` on 48 of 48 pairs and I.G.O. 3 takes `0x5DF` on 48 of 48, while 2001's best
state matches 1 or 2 of 48 — the noise floor. The six word-order variants that Phase 20's
high-first/low-first split suggests change nothing.

The twin is sound: 1999's and 2000's FINDIT and AMORE end in a valid PCX palette marker on
1397 of 1397 and 332 of 332 entries, so their bodies really are plaintext. So 2001's
`7477/7D57` archives are on some other transform, and that is the open question — not the
dongle, which this phase has removed from the problem.

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
