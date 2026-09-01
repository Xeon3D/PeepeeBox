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

## 6. Where that leaves it

The algorithm is not in doubt — four constants and both rotation schedules match the
disassembly. What is not yet pinned is the **state the cipher starts from**: block 0 fails
under every key tried with an assumed IV of zero, so either the initial value is not zero
or the key material is not in the dump in a form these readings reach.

Next, in order:

1. **Settle the chaining with a test that can fail.** Under CBC on a zero-plaintext run,
   `C_i = E(C_{i-1})`, so blocks 74–96 give 23 chained observations of `E` per entry, over
   1397 entries. Two entries whose runs ever collide would confirm it outright.
2. **Solve for the IV.** With the chaining known, block 0 and block 1 together constrain
   both key and IV, and the corpus supplies 1397 independent instances of block 0.
3. **Read `0x2E44D`'s marshalling** in I.G.O. 2's `FINDIT.EXE` rather than 2001's, now
   that its plaintext is known exactly and any candidate can be checked against 72 MB.

## Data

`docs/research/evidence/findit_block0.pkl` holds the first 64 bytes of the first 24
entries in all three archives — enough to reproduce every result above without the disk
images. The archives themselves come from `F:\HDDImages\IGO2|IGO3|IGO4\…\HardDisk.img`.
