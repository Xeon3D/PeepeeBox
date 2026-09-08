# 5. 2001 to 2007, and I.G.O. Italy — HASP4 on the parallel port

One physical part, **three protocols on the same pair of wires**, all answering on
STATUS bit 5. That is why they were hard to tell apart, and separating them is the
single most useful thing to know before writing this device.

| layer | how it is framed | what it carries | state |
|---|---|---|---|
| **session / detection** | command bytes clocked on DATA **bit 0**, **bit 7 clear** | identity, the sweep, liveness — the gates | modelled; 12,856 of 12,856 reads in a real boot |
| **memory** | Microwire: CS = DATA bit 1, SK = bit 5, DI = bit 6, DO = STATUS bit 5 | the 112-byte record | exact |
| **transform** | payload clocked on DATA **bit 4**, **bit 7 set** | the picture cipher's keyed round | exact; 46,036 of 46,036 rounds |

Query payloads never move bit 0 and command bytes never move bit 4, so the transform
layer is never confused with the session layer. And the session state must be advanced
by the **STATUS reads**, not by the writes — Microwire traffic is bit-7-clear too, so a
tracker driven off DATA alone drifts through every record read.

It really is a HASP4: 112 bytes of memory (MemoHASP's size, and exactly the 56 words the
library reads), two 16-bit passwords, and the published nine-argument call
`hasp(service, seed, port, pass1, pass2, &p1..&p4)` with published service numbers —
`1` IsHasp, `5` HaspCode, `0x32` ReadBlock, `0x3C`/`0x3D` HaspEncodeData/HaspDecodeData.

## 5.1 The passwords

Measured twice over: lifted out of the binaries, then confirmed byte for byte by nine
h5dmp dumps of physical dongles (`PhotoPlay2000_h5dmp/`), which open with the two
passwords little-endian.

| generation | pass1 | pass2 |
|---|---|---|
| Photo Play 2001 | `7477` | `7D57` |
| I.G.O. 2 (2002) | `68BB` | `1329` |
| I.G.O. 3 (2003) | `6B91` | `24A3` |
| I.G.O. 5 (2005) | `6B91` | `24A3` |
| I.G.O. 6 (2006) | `68BB` | `1329` |
| I.G.O. 7 (2007) | `68BB` | `1329` |

Three pairs across six generations — consistent with a batch of dongles bought per
contract rather than per year. `pass1` is the fourth argument, `pass2` the fifth; a
`pushd 0x7D577477` puts pass1 at the lower address, and builds that *store* them
instead push them in reverse, so the two forms need opposite handling.

**`0000/0000` on an image means removed, not zero.** Every I.G.O. 6 image carries it and
the hardware says `68BB/1329`; I.G.O. Italy carries it too, in a build whose folder is
named `NSB NONE`.

### The consequence that actually matters

Two releases do not carry their passwords as literals at all. I.G.O. 6 (`MENU.EXE`
`0x3B2C8`) and I.G.O. Italy (`0x3C252`) **write them at runtime and probe**: try
`7477/7D57`, fall back to `68BB/1329`, and if service 5 answers neither, **set both
passwords to zero and carry on**. An emulated part that answers service 1 but not
service 5 always takes that last branch — so what those builds decode the record with is
`0x0000`, not the password their real dongle holds. Measured, not inferred: with `68BB`
as the key, I.G.O. 6 DE puts `>ÞÈ` on screen where `Vers` belongs, which is record
bytes 3..6 XORed with `0x68BB`.

## 5.2 The memory layer

Ordinary Microwire, with two geometry facts that are easy to get wrong and fatal when
wrong. Both are **measured** off a real `68BB/1329` part in the passthrough capture:

- **Six address bits, not eight.** A read is 25 clocks: start bit, two opcode bits,
  **six** address bits, sixteen data bits. Replaying the capture through an 8-bit
  decoder decodes **0** read instructions over all 12,856 status reads; through a 6-bit
  decoder it decodes **93**, at addresses 8, 9, 10, 11…
- **The part holds 64 words.** The library adds a start offset of **8** to the caller's
  word and asks for 56, and 8 + 56 is exactly 64: the record is the whole of the part
  above word 8, with nothing spare.

Bits go out MSB first, and the host clocks low-high-low and only then samples, so the
bit must be on the line from the rising edge on. Taking CS low abandons whatever was in
flight. Opcodes are the usual `2` READ, `1` WRITE, `3` ERASE, `0` EWEN/EWDS/ERAL/WRAL
distinguished by the top address bits.

**The record is stored scrambled**, and the scramble key is the release's **first
password**:

```c
/* i is the word address, 0..63; the record occupies words 8..63.
   plain[] is the record as two-byte words (see the byte order below), and is
   taken as 0 outside it -- so words 0..7 decode to zero, which costs nothing
   and keeps the part looking uniform. */
word[i] = plain[i - 8] ^ (uint16_t)(i - 8) ^ pass1 ^ (i < 8 ? 0xFF00 : 0x0000);
```

**Byte order splits by family**, and the dumps confirm it independently — a 2001 dump
reads as its record only after every word is byte-swapped, an I.G.O. dump reads
directly:

- **2001** unpacks each word **high byte first** (`0x362DF`).
- **The I.G.O. builds** unpack **low byte first**. Serving them 2001's order puts the
  banner on screen with every byte pair swapped: `Version 2005B (AT)` comes out as
  `roi n02 50 BA (eV)`.

The record layouts themselves are in `07`.

## 5.3 The session layer — the gates

The library runs three distinct transactions here, and a device that answers all of them
with one rule fails. All figures below are from one full boot of I.G.O. 2 on a real
`68BB/1329` part.

**The identity ramp** — 96 occurrences. The library walks DATA `00, 02 … 7E`, one STATUS
read per step, and accumulates `acc = 0x7E XOR (XOR of addr<<1 wherever DO came back
set)`. The address is DATA bits 1..6, so mask to six bits: I.G.O. 5 drives the identical
sequence with bit 7 set (`80, 82 … FE`) and the guest accumulates the loop index either
way, so masking is all that is needed to serve both.

The measured answer, byte-identical on all 96 ramps:

```
HD_SIGNATURE = 0xCEFF0AFFCECE0A0A     /* one bit per address 0..63 */
37 of 64 addresses set  ->  acc = 0x18
```

`acc` is then looked up in a four-entry table (`0008, 000C, 0018, 001C`); a miss, or the
value `0x7E`, makes the library give up. **Do not synthesise `0x1C`.** It is reachable
and its handler sets the memory size unconditionally, which is why it was once chosen —
but it advertises 256 words, and picking it leaves the library in a state where it reads
the record happily and never runs a decode. Scored against the 96 real ramps: the
synthesised `0x1C` rule reproduces 2,880 of 6,144 reads (chance); the measured
signature reproduces **6,144 of 6,144**. The measured identity and the 64-word geometry
of `5.2` belong together — one without the other garbles the banner.

**The 64-step sweep** — 6 occurrences. The library writes a 64-byte sequence and reads one
bit back each time. The written bytes are identical on all six occurrences and so is the
reply:

```
reply bits, MSB first:  F5 7A 37 E7 8F 8F BD DA
```

**The sequence is not a table funworld chose — it is generated**, and knowing that is what
lets it be recognised in a generation whose capture we do not have. I.G.O. 3's `MENU.EXE`
computes it at `0x24FB`:

```c
x = 100;                                   /* the seed, a literal in the code */
for (i = 0; i < 64; i++) {
    x = (x * 0x1989 + 5) & 0xFFFF;         /* a 16-bit LCG */
    payload = (x >> 8) & 0x7E;             /* the high byte, masked to bits 1..6 */
}
```

Its output is byte-identical to `hs_sweep_w[]` in `src/device/dongle_photoplay.c`, which
was measured off a real I.G.O. 2 part — all 64 values. So **I.G.O. 2 and I.G.O. 3 ask the
same 64 questions**, and any generation's build can be checked for the same LCG rather
than needing its sweep measured from scratch.

The two generations differ only in how they put those payloads on the wire: I.G.O. 2 sends
them with bit 7 clear, I.G.O. 3 with bit 7 set (see `5.6`). A matcher that compares the
raw byte therefore fires on one and not the other.

Some tools call these bits "read and discarded"; that is true of the tool and evidently
not of the game.

**The liveness probe** — 59 occurrences in one boot. The library writes `1E`, then `1C`,
and the part answers **1** then **0**. Address 15 really is clear in the measured
signature, so a ramp rule answers `0` to both — which is precisely the stuck line that
gate exists to reject.

With those three modelled, and the real part's own record loaded so no content
difference can flatter the result, the whole capture reconciles:

| phase | status reads | agreeing |
|---|---|---|
| identity ramp | 6,144 | 6,144 |
| the 64-step sweep | 384 | 384 |
| liveness | 59 | 59 |
| record (Microwire) | 1,488 | 1,488 |
| the keyed round | 4,722 | 4,722 |
| everything else | 59 | 59 |
| **total** | **12,856** | **12,856** |

**What that does not prove.** It is one boot of one game on one `68BB/1329` part. The
sweep reply is measured, not derived; if those 64 bits are a function of the password
then other parts answer differently. The sweep's *written* bytes are the guest's, so if
they differ per release a matcher simply will not fire.

## 5.4 The transform layer — the picture cipher's keyed round

The photo cipher is not software. The library shifts a byte to the part and reads one
bit back, forty times per eight bytes, two eight-byte blocks per 4 KB buffer — and the
part computes. Everything around it is arithmetic the game does itself, so **this is the
whole of what the hardware ever contributed.**

The part is a small shift register:

```c
/* seeded once per round by the preamble */
i5  = ((val >> 1) & 0x07) | ((val >> 2) & 0x18);   /* five bits; 5..7 are dropped */
st  = (key >> i5) & 1;

b0  = i5 ^ ((st ^ 1) & (i5 >> 3)) ^ (i5 >> 4);
b0 ^= cur >> 10;
b0 ^= cur >> 7;
if (i5 & 2) b0 ^= cur >> 5;
if (i5 & 4) b0 ^= cur >> 8;

cur = ((cur ^ ((i5 & 1) << 2)) << 1) | (b0 & 1);
answer = ((cur >> 11) ^ st) & 1;
```

Two clock edges drive it, on different lines:

| edge | meaning |
|---|---|
| DATA bit 0 rising, bit 7 set | a command byte — the round **preamble**, so reset `cur` to the initial state |
| DATA bit 4 rising, bit 7 set | a **query** — answer it and latch the bit for the next STATUS read |

The answer is consumed by exactly one STATUS read and must take priority over the record
path: a query is three DATA writes and one read with nothing in between.

### The keys

| release | pass pair | key | initial state | how obtained |
|---|---|---|---|---|
| Photo Play 2001 | `7477/7D57` | `CF47CB42` | `0x7DF` | fitted from the 2000 generation's plaintext |
| I.G.O. 2 | `68BB/1329` | `3B227944` | `0x7DF` | fitted twice — from the capture, and from I.G.O. 4's plaintext |
| I.G.O. 3 | `6B91/24A3` | `AB32E970` | `0x5DF` | fitted from I.G.O. 4's plaintext; **no dongle for this pair exists here** |
| I.G.O. 5 | `6B91/24A3` | `AB32E970` | `0x5DF` | shares I.G.O. 3's pair — inferred from the pair, not measured (its FINDIT is plain, so nothing could check it) |
| I.G.O. 6, 7, Italy | — | none | — | their pictures ship as plain GIF; a key would be a guess at a part no game asks |

`0x7DF` for I.G.O. 2 is not a fitted constant: running the model's password schedule on
`0x132968BB` produces it, and of the four plausible password forms only that one does.
2001's `0x7DF` is **recorded, not derived** — neither order of `7477/7D57` produces it
under that schedule (they give `0x55F` and `0x5DF`), and 2001's library differs in word
order anyway, which is where to look.

### Why no hardware is needed to fit a key

The round is an LFSR with polynomial `0x80500062`. Over thirty-nine steps the system is
triangular, so `c_8..c_39` are **uniquely determined by the round's output** — only
`c_1..c_7` fall off unconstrained, giving 128 candidate paths per round rather than
2^39. And because the bit shifted in during a step lands at bit 0, bit 11 of the updated
register is known before the key bit is: **every observed answer names its key bit
outright**. One pass over any set of known (plaintext, ciphertext) blocks either yields a
consistent key or contradicts itself.

That is how I.G.O. 3's key was obtained with no dongle for its password pair: I.G.O. 4
ships the same FINDIT pictures in the clear that I.G.O. 2 and 3 encrypt. Nine
(plaintext, ciphertext) pairs settle a key. I.G.O. 2 was the control — fitted from
I.G.O. 4 alone, capture never opened, it landed on the key the real part produced.

### Verification

```
software part vs. the real one:  46,036 of 46,036 rounds agree, 0 wrong
device edge detection vs. wire:   4,720 of 4,720 answers agree, 0 wrong
                                  (4,720 is the query count — it neither invents nor misses one)

I.G.O. 2 and I.G.O. 3, offline, no hardware:
  /FINDIT/PICS/FOTOPLAY.WAD   1397 of 1397 entries decrypt to a picture header
  /AMORE/COMIX/FOTOPLAY.WAD    332 of  332 entries decrypt to a picture header

I.G.O. 3's boot-check block at DS:0x50F6 transforms to  "c:/foto/"
  — a wrong key gives eight random bytes
```

`tools/dongcap/softpart.py verify` runs the whole check;
`tools/dongcap/capture-igo2/DONGCAP.BIN` is a regression test, not a dependency.

### One layout note for 2001

2001's photo archives are `[128-byte PCX header under a separate XOR keystream][body
under the picture cipher]`. The picture cipher begins **at byte 128**, not at byte 0.
Comparing byte 0 against a plaintext twin's byte 0 compares two different layers, and
that is what once made 2001 look like a different cipher. With the origin right it fits
like the others, and FINDIT and AMORE give the same key independently.

## 5.5 On screen

**I.G.O. 2 (BE)** boots, and FIND IT plays with the photographs decrypting — the whole
chain end to end, from a key fitted with no dongle in the room.

**I.G.O. 3 (DE)** now *asks* the round with its own key and still stops at
`error number 228.250.107, in module MENU, dongle error` -- see `5.7` for what
raises that, and `09` for what is still unknown about it.

## 5.6 I.G.O. 3 puts the same three layers on different lines

Everything above is I.G.O. 2's arrangement. I.G.O. 3 runs the same three protocols with
two of them moved, which is why a device built for I.G.O. 2 hears almost nothing from it.
All of this is read out of I.G.O. 3's own `MENU.EXE`, not inferred from traffic:

| | I.G.O. 2 | I.G.O. 3 |
|---|---|---|
| oracle query clock | DATA bit 4 | **DATA bit 0** (`0x19FC` masks the payload with `0x7E` and writes `V, V\|1, V`) |
| oracle round preamble | any bit-0 rise, bit 7 set | **payload `0x46`** (wire `C6`), clocked alone by `0x1A8F` before each service call |
| session layer | bit 7 **clear** | bit 7 **set** |
| Microwire | bit 7 clear | bit 7 clear, unchanged |

Three consequences for anything emulating this part, each of which cost a build to find:

1. **The sweep matcher must ignore bit 7** on I.G.O. 3, or it never matches.
2. **Bit-7-set writes must not reach the Microwire decoder** on I.G.O. 3. Its session
   payloads move bit 1 and bit 5, which that decoder reads as CS and SK, so it parses the
   sweep as an instruction and then owns the STATUS reads the sweep was meant to answer.
3. **A query answer must be held for repeated writes of the same DATA value.** I.G.O. 3's
   transport repeats every write four times, so one query is followed by several reads;
   consuming the answer on the first drops the rest through to the session matcher, where
   query payload `F8` aliases onto `hs_sweep_w[0]` (`0x78`). Hold it until the DATA value
   changes, which is what the part does anyway — DO stays driven until the next clock.

## 5.7 What raises `dongle error` on I.G.O. 3

Not any of the session gates. The string is at `DS:0x5129` and has three identical call
sites, all of this shape:

```
push &p1..&p4
pushd 0x24A36B91        ; pass2:pass1 = 24A3:6B91
push [0x7E01]           ; port
push [bp-0xa]           ; seed
push 0x3C               ; service 3C, HaspEncodeData
lcall 0x3AE3:000B       ; the HASP library, inside MENU.EXE
cmp  word [bp-6],0      ; p3 -- the library's status word
je   ok
push 0x5129             ; "dongle error"
```

So the condition is **`p3 != 0` after HaspEncodeData**, which is what `docs/research/20`
§ 3 said all along. `p3` is set by the library, and the library ships in the same binary at
segment `0x3AE3`, writing status codes such as `0xFC19` and `0xFFF4` — so what makes it
non-zero is readable code rather than a property of the part.

A gate at `0x20BB` — 64 folded bits compared against `DS:0x4C86` — was investigated at
length and is **not** this failure. It is a real check, it passes and fails on its own
terms, and forcing it to pass changes nothing on screen. Recorded here so nobody follows
that trail twice.

