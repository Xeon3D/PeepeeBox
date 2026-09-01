# Phase 24 — a complete known-plaintext corpus, and what it rules out

Branch: `picturedecryptingtest`.

## 1. The find: I.G.O. 4 ships the same pictures in the clear

`FINDIT/PICS/FOTOPLAY.WAD` is **72,373,757 bytes in I.G.O. 2, I.G.O. 3 and I.G.O. 4
alike**, and all three hold the same 1397 entries. Their directories are identical —
**1397 of 1397 entries agree on name, offset and size** — so the three archives are
byte-aligned.

I.G.O. 4 has no HASP (it is a CDONGLE release) and its entries are **plaintext**:

```
048005B.GIF   IGO4  47 49 46 38 37 61 2c 01  "GIF87a" + 0x012C
              IGO2  97 36 21 d0 1d 6a 2d c7
              IGO3  ec 1a d6 2a e7 9e bc 92
```

So there are **72 MB of exact known plaintext against two ciphertexts under two
different keys** — I.G.O. 2's `68BB/1329` and I.G.O. 3's `6B91/24A3`. No LCG layer to
undo and no reconstruction: 99.6% of every entry's bytes differ from the plaintext, first
byte to last, so the whole entry is enciphered and the alignment is exact.

This is a far better corpus than the 2000/2001 PCX route, which needed the per-picture
LCG keys undone first and gave one key only.

## 2. The cipher is not ECB — proven

At **block 97 and block 98 every entry has identical plaintext and completely different
ciphertext**:

```
block 97   pt 00000000 00002C00   ct 4491484A 5D9D4A7A   (048005B)
                                  ct 58FB8678 437A3C08   (062002B)
                                  ct 6F9908C3 71F1AE7A   (067055B)   … 8 files, 8 ciphertexts
```

and blocks 74–96 are a run of all-zero plaintext whose ciphertext never repeats. The
cipher therefore chains, or is otherwise position-dependent.

**Block 0 is the exception and is clean**: all 1397 entries share both the plaintext and
the ciphertext of their first block, so the chain starts from a fixed state and block 0 is
a pure known pair for the raw block function.

## 3. The keyed round can be isolated without the key

Both Feistel stages are keyless and, being rotations and XORs, affine over GF(2). Writing
`HaspDecodeBlock` out:

```
(A0,A1) = A_rounds(ciphertext)
(B0,B1) = (f(A0) ^ A1, A0)
(C0,C1) = B_rounds(B0,B1)
plaintext = (f(C0) ^ C1, C0)
```

`C0` is the plaintext's second dword, so it is known; `B1` must equal `A0`, which is 32
affine equations in `C1`. **That system has full rank 32** (checked at three values of
`C0`), so `C1` is unique — the extraction is well defined, not a free choice. Both keyed
rounds then fall out: `f(C0) = P0 ^ C1` and `f(A0) = B0 ^ A1`.

Run over 8 entries this solves **377,907 blocks** and yields ~740,000 input/output pairs
for `f`.

## 4. What the extraction says, and what it cannot say

The consistency of `f` is the test: the same input must give the same output.

| chaining assumed | distinct f-inputs | inconsistent | verdict |
|---|---|---|---|
| ECB | 47,546 | **6** | **refuted** |
| CBC | 47,755 | 0 | **no power — see below** |
| PCBC | 47,755 | 0 | **no power — see below** |

The ECB row has power because 209 of its inputs genuinely repeat, and 6 of the 7
repeating plaintext dwords give conflicting `f`. CBC and PCBC score zero inconsistencies
only because their effective plaintext `P_i ^ C_{i-1}` is essentially random and **no
input repeats at all** — 47,755 distinct from 47,760 samples. A test that cannot fail
proves nothing, and this one is recorded as untested rather than passed.

Note also that "every block solved, none unsolvable" is **not** evidence: for fixed `C0`
the map `C1 -> B1` is a bijection, so a solution always exists. Same trap.

## 5. Key search: still nothing

Against the block-0 pair, with the certain plaintext:

* the password path — every 8-byte window of the matching dump, five password encodings,
  both directions, and **four** readings of the security-table bit index — no hit;
* the `column_mask` / `crypt_init_vect` path — 711 windows x 2 indexings x 256 x 256,
  about **93 million keys** — no hit, best partial 4 bytes of 8 where chance predicts one
  such near-miss.

The bit index is worth naming, because io.hasp4's is not standard. It reads
`sec_table[(i >> 2) & 0x0e]`, i.e. bytes **0, 2, 4, 6**, where ordinary MSB-first bit
indexing of the same table reads 0, 1, 2, 3. Both were swept, along with LSB-first and a
plain `(i >> 2) & 7`.

## 6. The chaining test, run properly

### 6.1 The first run was measuring duplicates

Merging the two f-streams gave ECB 88.2% agreement and CBC 99.5%, which looked like an
answer. It was not: **6,925 of the observations were duplicates** — the same
`(c0, c1, d0, d1)` seen again — and each contributed two guaranteed self-agreements,
2 x 6,925 = 13,850, which is almost exactly the agreement count every mode reported.
After deduplicating, **every mode scores zero**.

### 6.2 The machinery is sound — checked against a known key

Before trusting a negative that strong, the extractor was run on a synthetic corpus built
with `hasp4.py` under a known key, with deliberate plaintext repeats:

```
extracted f values that do NOT equal transform_word:   0 of 800
input collisions 272, of which agree                 272
```

So the extractor recovers `f` exactly when the model is right. The negatives below are
about the data, not the tool.

### 6.3 Result: the assumed block function is refuted

Deduplicated, over 100 entries, for every chaining mode and every way of mapping the
eight bytes onto the two dwords:

| block reading | cbc | pcbc |
|---|---|---|
| LE, `b0` = bytes 0–3 | 182 collisions, **0** agree | 203, **0** |
| LE, dwords swapped | 260, **0** | 274, **0** |
| BE | 200, **0** | 192, **0** |
| BE, dwords swapped | 276, **0** | 270, **0** |

ECB was refuted separately and more strongly (1,771 collisions, 0 agree). Every cell has
enough collisions to be decisive — a correct model agrees on all of them, as the synthetic
run shows.

**`HaspDecodeBlock`'s two-keyed-round Feistel is not I.G.O. 2's picture cipher.**

### 6.4 What the corpus does establish about the mode

Two measurements, both independent of any assumption about the block function:

* **Block size is 8.** Take pairs of entries and compare how far their plaintexts agree
  against how far their ciphertexts agree. In every pair tried the plaintext prefix is 13
  bytes and the ciphertext prefix is exactly **8** — `8 * floor(13 / 8)`. A byte-wise
  stream would have tracked 13.
* **It chains, and starts from a fixed state.** All 1397 entries share ciphertext block 0,
  so block 0 is a pure function of plaintext block 0 and the chain begins the same way
  every time. By blocks 97 and 98 the same plaintext gives eight different ciphertexts
  across eight files, so by then the state carries history.

That is CBC-shaped: `C_0 = E(P_0 ^ IV)` with a fixed IV, and later blocks depending on
what came before. What is wrong is `E`, not the mode.

## 7. Where that leaves it

The two Feistel stages are still the ones the 2001 disassembly describes — four constants
and both rotation schedules match, and that is not in question. What section 6.3 refutes
is that **I.G.O. 2's archives are enciphered by that arrangement**. Either its `FINDIT.EXE`
does something different from 2001's, or the block reaching the block function is not the
file's bytes as read here. Note the container changed from PCX to GIF between the two
releases, so the asset pipeline demonstrably changed with it, and the assumption that
I.G.O. 2 inherited 2001's cipher was never tested — it is now refuted.

The earlier key searches are superseded rather than merely unsuccessful: they were
searching for key material inside a block function that turns out to be the wrong one.

Next, in order:

1. **Read the block function out of I.G.O. 2's `FINDIT.EXE`.** Guessing is now exhausted:
   the mode is known, the block size is known, and the one thing left is `E`, which the
   binary contains. 2001's cipher was read at `0x2E44D` / `0x2D04C`; the equivalent in
   I.G.O. 2 has never been looked at, and the container changed from PCX to GIF between
   the two releases, so the asset pipeline plainly changed with it.
2. **Check any candidate against the corpus immediately.** 72 MB of exact plaintext under
   two keys, and the extractor is validated, so a proposed `E` is confirmed or killed in
   one run.
3. Only if that fails: revisit whether the 2001 archives use the same `E` as I.G.O. 2's,
   which was assumed here and never tested.

## Data

`docs/research/evidence/findit_block0.pkl` holds the first 64 bytes of the first 24
entries in all three archives — enough to reproduce every result above without the disk
images. The archives themselves come from `F:\HDDImages\IGO2|IGO3|IGO4\…\HardDisk.img`.
