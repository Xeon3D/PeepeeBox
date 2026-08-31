# Phase 22 — I.G.O. 2's pictures: which archives fail, and why

I.G.O. 2 boots and plays. Marcos reports AMORE showing black pictures and FIND IT
"only showing the differences between images". Both are the same fault, and the split
is exact.

## 1. The record is not the problem

The 2002PT dump settles it. Its eight dwords read

```
907  98765  120672  170898  75902  2205  160678  -371202944
```

which is `hd_fields[]` plus `hd_keys[]`'s `v6` and `v7` for "Version 2002", including
the `v7` that until now had only been fitted. The record this device serves I.G.O. 2 is
correct in every field.

## 2. Two ciphers, and only one of them is a wall

funworld shipped two picture schemes side by side, and an archive uses one or the other.
The 1999/2000 scheme enciphers **only the first 128 bytes**, per picture, with the Turbo
Pascal LCG — the key is recoverable from the shipped data alone (`ng-12`), and this
device already handles it. The 2001 scheme enciphers the **whole file** in 8-byte CBC
blocks with the cipher the dongle computes, which is unsolved.

They are told apart without decrypting anything: the header-only scheme leaves the body
plaintext, so the body's entropy collapses; and it keys per picture, so no two entries
share a first block.

| archive | entries | distinct first blocks | body entropy | |
|---|---|---|---|---|
| `FINDIT/PICS` | 1397 | **1** of 40 | **7.90** | whole-file — blank |
| `FINDIT/PICS/PART0` | 1394 | 40 of 40 | 5.28 | header-only — renders |
| `FINDIT/PICS/PART1` | 1394 | 40 of 40 | 5.25 | header-only — renders |
| `FINDIT/`, `FINDIT/ANIM`, `FINDIT/RES`, `FINDIT/INS` | 4, 19, 6, 3 | — | 4.1–4.8 | header-only — renders |
| `AMORE/COMIX` | 332 | **1** of 40 | **7.90** | whole-file — blank |
| `AMORE/ANIM`, `AMORE/GRAFIX`, `AMORE/RES`, `AMORE/INS` | 4, 27, 6, 3 | — | 3.4–5.0 | header-only — renders |

**Exactly two archives in the whole release are behind the dongle cipher**, and they are
the two that hold photographs. Everything else — every animation, every button, every
instruction screen — is on the solved scheme.

That is the reported symptom, precisely. FIND IT lays a difference overlay from
`PART0..PART4` over a base photo from `FINDIT/PICS`: the base is blank and the overlay
draws, so what reaches the screen is the differences and nothing else. AMORE's photo
comics live in `AMORE/COMIX` alone, so they come up black while the rest of the game is
intact.

## 3. Same cipher as 2001, and what I.G.O. 2 adds to attacking it

All 1397 `FINDIT/PICS` entries share the first ciphertext block
`97 36 21 d0 1d 6a 2d c7` and diverge from byte 8 on; all 332 `AMORE/COMIX` entries share
`32 e8 22 09 e0 da 19 68`. A constant first block with immediate divergence is CBC under
a zero IV, which is the 2001 model exactly.

Two cautions against reading more into the shared constants than is there.
`AMORE/GRAFIX/FOTOPLAY.WAD` and `FINDIT/FOTOPLAY.WAD` are **byte-identical** across the
2000, 2001 and I.G.O. 2 images — they are the same shipped files, so their matching first
blocks say nothing about keys. Where the content genuinely differs, so does the constant:
`AMORE/COMIX` is `32 e8…` here against 2001's `d2 46…`, on 332 entries with no shared
names.

What is new is the container. 2001's photo archives are PCX and its known plaintext had
to be reconstructed from the 2000 images; I.G.O. 2's `FINDIT/PICS` holds **GIF**, whose
first bytes are fixed by the format. It is a second corpus under a second password pair
(`68BB/1329` against 2001's `7477/7D57`), which constrains the algorithm from a direction
the 2001 data alone cannot.

## 4. Status

I.G.O. 2 needs nothing further from the record or the transport. What it needs is the
picture cipher, which is the same wall 2001 and I.G.O. 3 stand behind.
