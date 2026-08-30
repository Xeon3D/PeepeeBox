# Phase 16 — Why a photo game shows no pictures, and how to fix it in one pass

Written after the 2000 generation's FINDIT was made to work. It is the third time
this same class of bug has been diagnosed from scratch — once in 1999 (`Docs/14`),
once in the sibling NG investigation (`ng-12`), and once here — so this document
is the checklist rather than another narrative.

**Read this before touching a photo game that will not display pictures.** Nothing
in it needs re-deriving.

## The two independent mechanisms

A photo game needs **two** different things from the dongle, and they fail in
completely different ways. Almost all the time lost on this has come from
diagnosing one while the other was broken.

| | what it is | what the dongle supplies | how it fails |
|---|---|---|---|
| **A. Level database** | a dBASE III `.DAB` listing which pictures to show | **one dword read from the licence record at a fixed offset** | game runs, UI draws, **picture panels are black**; no picture-key query is ever sent |
| **B. Per-picture key** | the LCG seed that decrypts each PCX header | a **per-name query** answered by the dongle | game reports `not a PCX-File`, or stalls with an unknown-character log line |

The decisive tell, and it costs nothing to check: **does the log show a
picture-key query at all?**

- **No query** → mechanism A. The game never got a valid filename to ask about,
  because its level database decrypted to noise. Do not touch the key tables.
- **Queries, but wrong answers** → mechanism B.

Both were wrong here at the same time, which is why fixing B alone changed
nothing on screen.

## A. The record layout — the fixed-offset rule

**The games do not walk a struct.** Each one reads a single absolute offset into
the 48-byte record it was handed, hardcoded at compile time:

| game | offset | dword | value |
|---|---|---|---|
| `FINDIT` | `+0x1C` | `v[3]` | `0001D760` |
| `MOSAIC` | `+0x20` | `v[4]` | `00029B92` |
| `FMEMO` | `+0x24` | `v[5]` | `0001287E` |

Those offsets are `v[3]`, `v[4]` and `v[5]` of the record the **hardware** serves:

```c
char     banner[16];    /* NUL-terminated */
uint32_t v[8];          /* little-endian, from offset 16 */
```

So **the dwords always start at offset 16, whatever the banner is.** That is the
whole rule, and it holds across generations because the offsets are baked into
each executable.

### The trap, which has now caught this project twice

`KEYN.COM` serves the same fields with a **30-byte** banner. Copy that and every
dword lands 14 bytes late. `Docs/14` found and fixed exactly this for 1999.

The fix made there was conditional — a short banner got the 16-byte field, a long
one kept KEYN's 30 — and that condition is what broke the 2000 generation. A 1999
banner (`Version 99 (AT)`, 15 characters) fits sixteen bytes and worked. A 2000
banner (`Version 2000 (DE)`, **17** characters) does not, took the KEYN branch,
and put the dwords at `+0x1E`. FINDIT then read `+0x1C` across the boundary, got a
wrong-but-non-zero key, took the decrypting path, and produced noise.

**A banner that does not fit does not move the dwords.** Write the dwords at
offset 16 and lay the banner over the start of the block. A 17-character banner
clips the first two bytes of `v[0]`, which is the per-unit word — uninitialised
host memory on a real dongle — and no game reads it. That collision is free; the
alternative costs FINDIT its level database.

Zero matters too: **key 0 means "read plaintext"**, so a game whose database is
not encrypted still works with no dongle value at all. A *wrong non-zero* key is
worse than none, because it takes the decrypting path.

### Confirming it in one line

The device logs the record it built at attach time:

```
banner "Version 2000 (DE)" (17 chars), dwords at +10; FINDIT reads +1C = 0001D760
```

If that last value is not the game's documented dword, stop — nothing downstream
will work.

## B. The per-picture key

Recovered in full for the 2000 generation; see `Docs/15` for the transport and the
sections below for the function.

The guest sends the uppercased 8-character basename and the dongle answers
**eight bytes, one per name position**. The guest folds them itself: pairs `(i,
i+4)`, XOR for cases 0 and 1, ADD for 2 and 3, with case 1 aliasing. The case is
`(name[0]^name[1]^name[2]^name[3] ^ '.' ^ 'C') & 3` — `fnsplit` keeps the dot, so
it is not `'P' ^ 'X'`.

Answering `S_j(name[j])` and letting the guest fold is not merely tidier than
computing the key and inverting the fold — it is the only shape that works.
`0x081D` interleaves the exchange, sending `name[j]` and reading reply byte `j`
before it sends `name[j+1]`, and the key depends on `name[j]` *and* `name[j+4]`.
Any implementation that needs the whole name before answering will pass on
AMORE's three-character names and fail on everything longer.

### The structure of `S_j`

1. **Separable in the nibbles**: `S(c) = a[hi] + b[lo]` mod 256, exactly, in every
   position of every case — no contradiction over 3096 cracked keys.
2. **`b` is linear in the bits of the low nibble**, so four weights give all
   sixteen entries. This is why an affine model in the name *bytes* was
   rank-deficient and died (`Docs/09`): the function is affine in its **bits**.
3. Verified 667 / 810 / 911 / 708 over the four cases — **3096/3096**.

The closed form behind `a[]` is still unknown, as is the constant-to-transform
rule that would give the real `A3` and `A4`. The tables in
`src/device/dongle_photoplay.c` are measured where the archives show a character
and filled from the nibble law where they do not; anyone recovering the real
function must reproduce them.

## Recovering the ground truth for a new generation

Both mechanisms are checkable offline, without the emulator and without guessing,
because both plaintexts are known:

- **PCX**: a header begins `0A 05 01 08 00 00 00 00`. Eight known bytes pin the
  Borland LCG seed (`s = s*0x08088405 + 1`, keystream `s >> 24`, first 128 bytes
  only). `evidence/amore-pcx/crack.c` does the whole archive in about 90 ms.
- **dBASE III**: a `.DAB` begins `03` followed by a date and a record count.

Sanity check the method against an archive packed **without** a dongle: it must
recover the vendor default `0x00012345` exactly. `AMORE/GRAFIX/FOTOPLAY.WAD` and
`/FINDIT/FOTOPLAY.WAD` are both like that — so not every archive in a photo game
is dongle-keyed, and finding one that is not proves nothing about the others.

## Checklist

1. Does the game show a picture-key query in the log?
   - **No** → mechanism A. Check the attach line's `+1C` value against the table
     above. Fix the record layout, not the key tables.
   - **Yes** → mechanism B. Compare a served key against a cracked one.
2. Is the game's archive dongle-keyed at all? Crack it; if it comes out
   `0x00012345` it never needed the dongle.
3. Is the banner longer than sixteen characters? If so, confirm the dwords did
   **not** move.
4. Only then look at the per-name function.

## What this corrected in the code

`PP_BANNER_KEYN` and the conditional that chose between the two layouts are gone.
The record is now always `banner over dwords-at-16`, and the attach line prints
what FINDIT will read so the failure is visible before a game is ever launched.
