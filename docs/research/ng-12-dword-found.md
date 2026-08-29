# Docs/12-dword-found.md — Phase 12: the DWORD, found and inverted

**Result: the cipher is fully broken. The key is recoverable from the shipped data alone,
with no dongle.**

---

## 1. The algorithm (complete)

`CONCENT.EXE` `0xD816` — `decrypt(far buf, len, dword code)`:

```
  mov  eax,[bp+0xc]        ; the code
  mov  [0x3576],eax        ; SEED the global PRNG with it
loop:
  push 0x100 ; call rand   ; rand(256)
  xor  [es:bx],al          ; XOR one byte
  inc  bx ; inc si ; cmp si,[bp+0xa] ; jc loop
  mov  [0x3576],<saved>    ; restore the previous seed
```

`rand(n)` at `0xD572` is the **Turbo Pascal LCG**:

```
  s = s * 0x08088405 + 1        ; [0x3576]
  return (s * n) >> 32          ; for n = 256 this is simply  s >> 24
```

**So: `keystream[i] = (s_i >> 24)`, `s_{i+1} = s_i * 0x08088405 + 1`, `s_0 = code`.**

The caller I mislabelled in P10 as `get_dongle_code` actually **fetches the code *and*
decrypts**: `push dword [bp-4] ; push 0x80 ; push ds ; push 0x4584 ; call decrypt` —
128 bytes of PCX header. A second call at `0x1803B` decrypts the **palette** (`count*3`
bytes at `0x4604 + idx*3`) with the same code.

## 2. Inverting it — no dongle needed

A PCX header has **54 known-zero filler bytes at [74..127]**, so the ciphertext there *is*
54 consecutive LCG top-bytes. Recovering the seed:

1. brute-force the low 24 bits of `s_75` (top 8 bits are known) — 2^24, vectorised;
2. filter on the next 5 outputs → ~3 candidates;
3. confirm against all 54 bytes;
4. step backwards 75 times with `Minv = M^-1 mod 2^32` to get `s_0`.

Runs in seconds per entry. **Verified:** `CONCENT/100X135/0` `IMG000.PCX` seed
`0xB785F81C` decrypts to `magic=0x0A ver=5 enc=1 bpp=8 100x135 planes=1 bpl=100` — and
`100x135` matches the directory name exactly.

## 3. The code is per-entry, derived from the filename

| WAD | entry | seed |
|---|---|---|
| CONCENT/…/0 | `IMG000.PCX` | `0xB785F81C` |
| CONCENT/…/0 | `IMG001.PCX` | `0xB78CF81C` (+0x70000) |
| CONCENT/…/3 | `IMG000.PCX` | `0xB785F81C` (**same as WAD 0**) |
| FMEMO | `10003.PCX` | `0x16215765` |
| FMEMO | `10004.PCX` | `0x16255765` (+0x40000) |
| FMEMO | `10090.PCX` | `0xB7156F65` (low word differs too) |
| **FINDIT** | `BACKGR/FALSCH/RICHTIG.PCX` | **`0x00012345`** |

Two things fall out:

- A `+1` in the last filename digit shifts the seed by a fixed amount that depends on
  character **position** (`+0x70000` for `IMG00x`, `+0x40000` for `1000x`) — a
  position-weighted hash of the name. Same-named entries in different WADs get the same
  seed, which is why `IMG039.PCX` had identical ciphertext across six archives.
- **`FINDIT`'s PCX entries are keyed with `0x00012345` — the plain vendor default.** They
  are not dongle-keyed at all, which means FINDIT's freeze is a *different* fault and P10
  already gives it the right key.

## 4. The repair, and why it needs no hardware

The patched game now always calls `decrypt` with `0x12345` (P10). So the data can be moved
to meet it:

> for each PCX entry: recover its true seed -> decrypt the 128-byte header (and the
> palette) -> **re-encrypt with seed `0x12345`** -> write back.

The unmodified in-game decrypt path then produces correct headers. No dongle, no code patch
beyond what already exists, and every step is verifiable (header must satisfy
`magic=0x0A, enc=1, bpp=8, planes=1` and dimensions must match the RLE decode).

Scope: `CONCENT` (6 WADs), `FMEMO` (472 entries), and any other dongle-keyed archive.
`FINDIT` needs nothing here.

## 5. Corrections carried

- P10 named `0x17CA4` "get_dongle_code". It is **fetch-code-and-decrypt-header**. The name
  hid the fact that the decrypt was right there in the function I had already patched.
- `Docs/09-cipher.md` said "no dongle input found". Right for the archive *directory*
  (hardcoded XOR `0x55`) and for `FINDIT`'s PCXs (`0x12345`), **wrong** for `CONCENT`/`FMEMO`
  PCX headers, which are keyed per filename with a non-default code.

---

# ADDENDUM — can static dongle bytes be patched in? (user question)

**Short answer: no — the dongle is a challenge-response keyed on the FILENAME.** A fixed
byte string cannot reproduce it. But a minimal-byte fix still exists (§ B).

## A. Why a static reply cannot work

`get_dongle_code` is `CONCENT.EXE` `0x89F4` (note: `0x4F9*16 + 0x3200 = 0x8190`, `+0x864`).
It does **not** just read the dongle — it builds a per-file challenge:

```
  call fnsplit(path,…)              ; 0x0:0x38F0  -> basename
  call strupr(name)                 ; 0x0:0x4ACD
  mov  byte [0x2D5C],1              ; opcode
  mov  byte [0x2D5D..0x2D64],0x20   ; 8 spaces
  call rand(256) ; mov [0x2D65],al  ; nonce byte
  <copy the uppercased filename over the 8 spaces>
  call <dongle txn 0x89EF> ; call <dongle txn 0x8911>
  <read 4 bytes back from [0x2D5C..0x2D5F]>
```

So the request is `{0x01, "NAME    ", nonce}` and the device returns a **4-byte value that
depends on the filename**. That is why every entry has a different seed and why identical
names share a seed across archives. There is no single 48-byte or 4-byte reply that
satisfies all entries — the hardware is doing a computation, not storing a constant.

## B. The minimal-byte route that *does* work

The game already owns everything except the transform. Since the seed is recoverable for
any entry from the shipped data (§ 2), the mapping

```
    seed = f(uppercased 8-char basename)
```

can be derived from recovered samples and then **implemented as a small stub in the dead
dongle-probe region that Phase 7 already overwrote** — same technique, same region, no new
space needed. The original `decrypt` then runs unmodified over **untouched WAD data**.

- data changed: **none**
- code changed: the existing P7/P10 patch sites plus roughly 30–50 bytes of stub
- verification: every decoded header must satisfy `magic=0x0A, enc=1, bpp=8, planes=1` and
  its dimensions must match the RLE decode — 543 self-checking test vectors.

Compare with the data-rewrite option: 0 extra code but rewrites ~140 MB of archives.
**The stub is the smaller change by far and is the recommended path.**

## C. What is still needed for B

`f` is not yet derived. Known samples:

| name (padded to 8) | seed |
|---|---|
| `IMG000  ` | `0xB785F81C` |
| `IMG001  ` | `0xB78CF81C` |
| `10003   ` | `0x16215765` |
| `10004   ` | `0x16255765` |
| `10090   ` | `0xB7156F65` |
| `BACKGR  ` / `FALSCH  ` / `RICHTIG ` | `0x00012345` (default — FINDIT packed with no dongle) |

Structure so far: a `+1` on the last character shifts the seed by a position-dependent
constant (`+0x70000` at index 5, `+0x40000` at index 4), and changes at earlier indices
alter the low word too — consistent with a multiply-accumulate hash rather than a plain
weighted sum. A linear-sum model was tested and **fails** (`9*W[3]` is not integral against
the observed delta), so it is not `sum(c[i]*W[i])`.

Next step: recover ~20 more seeds across varied names (seconds each) and solve for `f`.

---

# ADDENDUM 2 — constraints: no patched files AND must run on real hardware

**86Box device emulation is ruled out** (it would not run on a real machine), even though
86Box does have the framework for it (`lpt_device_*`, `lpt_attach`, and a worked example in
`dongle_savquest` / `hasp_init_savquest`). Recorded so it is not revisited.

## The unavoidable tension

The dongle is read by **direct port I/O** (`out dx,al` / `in al,dx` on `0x378`/`0x37A`).
In 16-bit real mode there is **no way to trap port I/O in software** — no V86 layer is in
use, and PTS-DOS loads `himem486.sys` only, not an I/O-trapping monitor. Therefore, on real
hardware, if the physical dongle is absent, **something must answer the port reads, or the
code that issues them must not run.** Those are the only two possibilities.

## Option 1 — replacement dongle (true zero software change)

A small MCU on the parallel port that speaks the original protocol. This is the only path
that leaves **every file byte-identical** and works on real hardware.

Needs:
1. the **wire protocol** — reverse `dongle_init` (`out 0x378,0xE0`; STROBE/INIT/SELECT
   toggling on `0x37A`) plus the two transaction routines `0x89EF` and `0x8911`;
2. **`f(basename) -> 4 bytes`** — derivable from shipped data (§ 2/§ C), no original dongle
   required;
3. the **48-byte reply** containing `"Version 99"` for the P1 launch check;
4. the **iButton**: P1 established it is a pure bool-gate (sets bits 0/1 of the mask), so a
   genuine blank **DS1982** in the IO-card socket should satisfy it — cheap and period-correct.

## Option 2 — boot-time TSR that patches in memory

A resident program hooking `INT 21h AH=4Bh` (EXEC) that applies the patches to each module
**in RAM as it loads**. All 25 EXEs and all ~140 MB of WADs stay byte-identical on disk.

Cost: one new file plus one line in `AUTOPTS.BAT` — so not literally zero file changes, but
**no original binary or data file is modified**. Works on real hardware and under emulation.
Still needs `f` (the stub must compute what the dongle would have returned).

## Recommendation

Both options need `f` and the wire protocol, so **that work is shared and should come
first** — it is the gate on either path. Option 2 is far cheaper to build and test and can
validate `f` end-to-end; Option 1 is the archival endpoint and can reuse the identical
`f` implementation.

---

# ADDENDUM 3 — user ground truth: dongles 1999-2008, all NL regions owned

**New facts from the user (2026-08-26):**
- updates + dongles exist for **every release 1999-2008**, same hardware;
- **every region has a different dongle with a region byte programmed in**;
- the user **owns all NL-region dongles** and a **Windows XP box with a parallel port**
  able to script and capture dongle traffic;
- 1999 alone is solvable with **no dongle** (already demonstrated).

## Strategic consequence: capture beats derive

Deriving `f` statistically works for 1999 but does **not** generalise — each year and region
has its own dongle, so a derived 1999 `f` is one point in a 10-year x N-region matrix.
Querying real dongles produces exact ground truth for every one of them.

## The key insight: the challenge space is FINITE and enumerable

`get_dongle_code` challenges the dongle with the **uppercased 8-char basename** of the file
being opened. Every such name is already known — they are the entry names in the `GWAD`
directories, which are readable with the hardcoded `0x55` and need no dongle:

- 1999 NL: **543** PCX entries across the three affected games (`CONCENT` 6 WADs,
  `FMEMO` 472, `FINDIT` 4).

So **a complete `name -> 4-byte` table is an exact solution** even without ever
understanding `f`. Enumerate names from the WADs, query the dongle once per name, store the
table. This is finite, verifiable, and generalises to every year/region the user owns.

## Order of work (my analysis unblocks their capture)

1. **Reverse the wire protocol** from `dongle_init` + `0x89EF`/`0x8911` — pure disassembly,
   no hardware. Produces a documented bit-bang spec.
2. **Ship a query utility** built from that spec so the user can drive any dongle directly
   and dump `name -> response` in bulk. Best as a small **DOS** program (FreeDOS USB on the
   XP box) — real-mode direct port I/O, no XP driver shim, identical to the original
   environment. `inpout32`-based scripting on XP is the fallback.
3. **Cross-validate against 1999**: the seeds recovered statistically (§ 2/§ C) must match
   what the 1999 NL dongle returns for the same names. That check validates both the
   protocol spec and the recovery method before any effort is spent on 2000-2008.
4. Then either a table-driven **replacement dongle** (archival endpoint, zero file changes)
   or, if the captures reveal `f` is simple, a computed implementation.

**Region byte:** the disc under analysis is NL and the user's dongles are NL, so they match.
Worth capturing the same name on two regional dongles later to locate the region byte in the
response - but not needed for the NL goal.
