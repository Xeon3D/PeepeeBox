# Phase 26 — the picture cipher, broken

Branch: `picturedecryptingtest`.

## 1. Result

The two dwords the dongle contributes to a 4 KB buffer are **recovered by search**, and
with them the buffer decrypts with no hardware at all.

| release | passwords | D | acc |
|---|---|---|---|
| I.G.O. 2 | `68BB/1329` | `DF57708B` | `32FC6611` |
| I.G.O. 3 | `6B91/24A3` | `2295D802` | `8E90F818` |

Verified against I.G.O. 4's plaintext over the **first 300 entries of both archives:
300/300 decrypt their entire first 4 KB buffer exactly**, byte for byte, allowing for the
last two blocks the walker deliberately leaves alone.

## 2. How

Block 0 is the only block the dongle touches. Writing its decode out, with `f1` and `f2`
the two keyed-round outputs:

```
(L1,R1) = A_rounds(C_0)            keyless -- computed once, outside the loop
(L2,R2) = (f1 ^ R1, L1)
(L3,R3) = B_rounds(L2,R2)          keyless
P_0     = (f2 ^ R3, L3)
```

`P_0` is known exactly from I.G.O. 4, and **its second dword is `L3`, which does not
involve `f2`**. So guessing `f1` is a 2^32 search with an immediate 32-bit filter; `f2`
then falls out as `P_0.lo ^ R3`. Survivors are checked against blocks 1 and 2 through the
software round at `0x3483D`, another 128 bits.

Exactly **one** candidate passed the filter for each release, and it verified. The whole
search takes 21 seconds (`scratchpad/solvekey.c`).

The walker assigns `D` from the transform's output slot `+0` and `acc` from `+4`; the
verification says **`D` is the second keyed output and `acc` the first**.

## 3. A bug worth recording

The first run returned **zero** candidates past the 32-bit filter. That is not a result —
with 2^32 guesses against a 32-bit test, roughly one survivor is expected even from a
wrong model, so zero means the code is broken rather than the theory. It was: after the
B rounds `b0` is `L3` and `b1` is `R3`, and the filter had them the other way round. One
line, and the search went from nothing to a clean unique hit.

## 4. What it does not do

Each 4 KB buffer gets **its own** pair from the dongle, keyed on that buffer's first
block, and every entry's later buffers differ. So this breaks the **first 4096 bytes of
every picture in the archive** from one solve — because all 1397 entries share block 0 —
but decrypting a whole 42 KB picture needs a solve per buffer, about eleven of them, and
the whole archive would need roughly fifteen thousand. At 21 seconds each that is days,
not minutes. It is a complete break of the *scheme*, not yet a practical bulk decryptor.

## 5. What it opens

Far more valuable than the plaintext: each solve yields **exact input/output pairs of the
dongle's keyed round** — the function this project has never been able to compute.

```
I.G.O. 2   f(504EF2AE) = 32FC6611      f(012C6137) = DF57708B
I.G.O. 3   f(41E6AE33) = 8E90F818      f(012C6137) = 2295D802
```

Note the second input is the same for both — it is the plaintext's second dword, so every
entry and every release shares it — with different outputs under different password pairs.
That is precisely the shape needed to attack the security table: identical input, two keys,
two known answers, and a generator for as many more as wanted at 21 seconds each.

The emulator's remaining job is unchanged in kind but now measurable: make the device's
byte-to-bit oracle reproduce `f`. Until now there was no way to know what `f` should
return for any input. Now there is.
