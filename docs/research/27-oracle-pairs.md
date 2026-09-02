# Phase 27 — the search was unnecessary, and the oracle is the wire

Branch: `picturedecryptingtest`.

## 1. Solve, don't search

Phase 26 recovered the dongle's two dwords with a 2^32 search. The search was never
needed. The B rounds are rotations and XORs, so the quantity the filter tests is **affine
in the guessed dword**:

```
(L1,R1) = A_rounds(C_0)                    keyless
(L3,R3) = B_rounds(f1 ^ R1, L1)            keyless, and affine in f1
P_0     = (f2 ^ R3, L3)
```

`L3` is the plaintext's second dword, so `Lin(f1) = L3 ^ const` is 32 linear equations
over GF(2) with a unique solution, and `f2 = P_0.lo ^ R3` follows. Twenty-one seconds
becomes microseconds.

## 2. It verifies everywhere

`scratchpad/harvest.py` solves every 4 KB buffer of every entry and then **decrypts that
buffer and compares it against I.G.O. 4** before recording anything:

```
IGO2: 507 buffers, 507 solved and verified, 0 failed
      936 distinct f-inputs, 0 contradictions
IGO3: 507 buffers, 507 solved and verified, 0 failed
      936 distinct f-inputs, 0 contradictions
```

Every buffer decrypts exactly — not just the first one, as in Phase 26. That part stands.

> **Correction (Phase 28).** This section originally read the "936 distinct inputs, 0
> contradictions" as proof that the keyed round is a function of its input. **It is not.**
> All 78 duplicate observations come from exactly **two** inputs, `504EF2AE` and
> `012C6137`, each seen 40 times and every time at **buffer index 0** — the shared first
> buffer that all entries have in common. The test only showed that the same input at the
> same position gives the same answer, which it must. It never compared different
> positions, so it could not fail. Another vacuous validator, and this file walked into it.
> Phase 28 measures the thing properly and finds the opposite.

## 3. What the pairs are

Each buffer yields two exact input/output pairs of the dongle's keyed round, the function
this project has never been able to compute:

```
IGO2   f(000A610D) = FD29CDA4    f(00D04C01) = FE7B0FBB    f(01107102) = 2C16F05D
IGO3   f(000A610D) = C7E4A1CC    f(00D04C01) = F0619A94    f(01107102) = 51A98A36
```

The inputs coincide across releases because one of the two is always the plaintext's
second dword, and the plaintext is shared. So the corpus gives **the same input under two
different password pairs, with both answers known** — and the full archive would yield
some 35,000 pairs per release on the same terms.

## 4. The oracle, read out — it is the wire

`0x32F59`, the byte-to-bit call inside the keyed round, is not a table lookup:

```
032F5F  al = (byte << 1) & 0x0E          input bits 0,1,2 -> DATA bits 1,2,3
032F66  dl = (byte << 2) & 0x60          input bits 3,4   -> DATA bits 5,6
032F70  al |= dl
032F75  and [bp+6], 0xEF                 clear bit 4
032F84  call 0x32DB2                     write the port
032F8D  or dl, 0x10 ; call 0x32DB2       write again with bit 4 set
032FA9  call 0x32DB2                     and again with it clear   -- a clock pulse
032FB6  call 0x32D17                     read the port; the answer is one bit
```

so five bits of the byte go out on DATA, bit `0x10` is pulsed as a clock, and one status
bit comes back. That is `in_5_bit = byte & 0x1F` exactly as io.hasp4 models it, and it
confirms the oracle is silicon: **five bits in, one bit out, with state that `0x32FC2`
initialises once per keyed round.**

## 5. Key search against a correct target, at last

Every previous sweep aimed at a target derived from a model Phase 24 later refuted. These
aim at verified pairs.

* **Password path** — every 8-byte window of the matching dump, five password encodings,
  four readings of the security-table bit index: 14,220 candidates per release, **no hit**.
* **`column_mask` / `crypt_init_vect` path** — running; 711 windows x 4 indexings x 256 x
  256 per release.

## 6. Where this leaves the emulator

The device needs one thing: a byte-to-bit oracle whose 40 answers reproduce `f`. Until
Phase 26 there was no way to know what `f` should return for any input; now there are 936
verified answers per release, a generator for tens of thousands more, and the same input
under two keys. If io.hasp4's model of the silicon is right, a key exists that fits them —
and if the sweep finds none, that model is wrong and the pairs are the evidence to replace
it with.
