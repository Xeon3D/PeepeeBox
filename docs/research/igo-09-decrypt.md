# Phase 9 — Photo-pack offline decryption

Status: **SOLVED — model fully reverse-engineered and validated.**
Class: **breakable obfuscation, NOT a hardware crypto oracle.**
Outcome: **no dongle, no binary patch, no cipher uncertainty.** The
photo-game residual from Phase 8 is removed by a one-time, data-only,
size-preserving asset re-key performed entirely offline.

> ## ⚠ CORRECTION (2026-06-10) — RAWPLAIN files must NOT be re-keyed; cipher is length-bounded
>
> Two claims in §3/§4/§5 below are **FALSIFIED** and were the *Surface-G re-key bug*
> (cross-project: NG1·NG2·NG3·NG4). Canonical record / evidence / corrected tooling:
> **`../../../SurfaceG-Rekey-Fix/`** (`Docs/01-MODEL-AND-BUG.md`).
>
> 1. **The engine HAS a raw-plaintext load path (path B).** RAWPLAIN (plaintext-on-disk)
>    assets are loaded **raw, with no decrypt** — `fcn.1000e1c0` detects the plaintext magic
>    and skips the LCG. Re-keying them to `F` (as §3/§5 prescribed) means the engine then runs
>    `lcg(·,F)` on already-keyed bytes → garbage → **after overlay they render as a small red
>    square.** Correct action: **KEEP RAWPLAIN byte-identical to factory** (do NOT write them to
>    `rekeyed/`/the overlay). The 19 NG1 RAWPLAIN are now KEEP.
> 2. **The cipher is LENGTH-BOUNDED, not whole-file.** The engine deciphers only
>    `N = filesize & 0xFFFF` bytes (the count is read as a 16-bit WORD — note `count = arg & 0xffff`
>    in §1); files ≥ 64 KB are **partially** enciphered, the tail is plaintext on disk. All
>    decrypt/encrypt/re-key operate on `raw[:N]` only; PHOTO re-key = `lcg(lcg(raw[:N],seed),F)+raw[N:]`.
>    Consequence: the former lone **UNKNOWN `Soli/.../quitpic.bmp` is actually STD** (196 616 bytes →
>    N=8; whole-file decoding mis-binned it).
>
> The cipher, the formula seed `F`, the PHOTO brute-recover, and the STD analysis below are all
> **correct** — only the RAWPLAIN action and the cipher *length* were wrong. Corrected method +
> L1–L5 validation: `../../../SurfaceG-Rekey-Fix/Docs/02-METHOD-AND-VALIDATION.md`. Re-key is now
> **PHOTO-only** (class-based pass-through, full-PIL-decode oracle); `scripts/p9-rekey.py` /
> `scripts/p9-validate.py` carry the fix; outputs regenerated; `Overlay/PP_NG1_7712002-V5/E/Prog/Games` rebuilt
> PHOTO-only; **validate.py L1–L5 = PASS**. Corrected corpus: STD 4857 · PHOTO 23395 · RAWPLAIN 19
> (KEEP) · UNKNOWN 0. The legacy buggy script is kept as `scripts/p9-rekey.py.bug.bak`.

## 0. Headline — Phase 8 §8c/§8d/§8e are FALSIFIED

Phase 8 concluded the photo image packs were decrypted *live* by the
physical COM2 TDongle acting as a per-game crypto oracle
(`fcn.10018860` ISO-7816 APDU), and that Phase 9 would need the user's
physical dongle for a one-time pre-decryption.

**That is wrong.** There is no cryptographic secret in the dongle for
this. Every game image asset — board games *and* photo packs — is
obfuscated with the **same trivially reversible Borland/Delphi LCG
keystream XOR**. The only difference for the photo packs is the *seed*:
standard assets use a seed the engine computes from the filename and
filesize; photo packs use a different seed the engine cannot compute
(historically supplied by the dongle). That seed is **brute-forced
offline in milliseconds** because the decrypted header is a
near-zero-false-positive oracle. The dongle stored a number; it did not
perform unrecoverable cryptography.

Phase 2E's operational verdict ("no per-game dongle dependency for
offline play") is therefore **restored** — the photo packs carry a real
extra obfuscation layer (a non-derivable seed), but it needs no dongle
to defeat offline.

## 1. The cipher (game-DLL `fcn.10013750`)

A classic Borland C++ / Delphi `Random` LCG used as an XOR keystream,
applied in place over a buffer. Disassembly (findit.dll, identical in
every game DLL — it is statically linked engine code):

```
fcn.10013750(buf, count, seed):           ; count = arg & 0xffff
    x = seed
    repeat (count) times:
        x  = (x * 0x8088405 + 1) & 0xffffffff
        ks = ((((x>>16)<<8) + ((x&0xffff)>>8)) >> 16) & 0xff
        *buf ^= ks ; buf++
```

`0x8088405` = 134775813, the well-known Borland LCG multiplier. The
cipher is an involution under a fixed seed (XOR keystream), so
**encrypt == decrypt**. Whole files are processed; `count` is taken mod
2^16 per call, so the engine loops the call in ≤64 KiB chunks for large
files (it re-derives the seed per file, not per chunk — confirmed by
full-file decryption reproducing complete, valid BMP/JPEG bytes).

Evidence: `analysis/p8/raw/fcn_serial2.txt`-adjacent dump and the live
r2 disassembly recorded in `analysis/p9/` notes; the keystream formula
is validated below by exact `bfSize == filesize` matches.

## 2. The seed (game-DLL `fcn.1000e1c0`, "format detector")

`fcn.1000e1c0(buf, idx, filename)` derives the seed, LCG-decrypts the
first bytes, and checks for an image magic (`"BM"`, JFIF
`FF D8 FF E0`, `"GIF"`). The derivation:

```
key  = reverse(basename(filename))   with A–Z lowercased (+0x20),
                                      digits / '.' / lowercase kept
s    = "_" + key + str(idx)          ; sprintf("_%s%d", key, idx)
seed = idx + Σ signed_byte(c) for c in s
```

The caller `fcn.1000e6f0` (`TPicture::EncodeBuffer`/`Load`) passes
`idx = filesize`. So the **engine "formula" seed** is:

```
F = filesize + Σ signed_byte( "_" + reverse(lower(basename)) + str(filesize) )
```

If `LCG_decrypt(header, F)` shows an image magic, the engine accepts
the file and decrypts the whole buffer with `F` — **no dongle**. If
not, it ASCII-encodes the offset, issues the `fcn.10018860` CLA-`0x81`
APDU to the COM2 dongle, and on failure logs the Phase-8 line
`TPicture::EncodeBuffer … Encrypt(<file>)  NOT SUPPORTED`.

## 3. Asset classes (the real Phase 8 split)

| Class | On-disk bytes | Engine offline? | Action |
|---|---|---|---|
| **STD** | `lcg(image[:N], F)` | **Yes** — decodes itself | none (untouched) |
| **PHOTO** | `lcg(image[:N], seed≠F)` | No → dongle → fail | re-key first N to `F` |
| **RAWPLAIN** | plaintext image | **Yes — engine loads it RAW** | ~~re-key to `F`~~ **KEEP (see banner)** |
| **UNKNOWN** | neither | No | left untouched (never write a bad guess) |

> **⚠ 2026-06-10:** the RAWPLAIN row above is CORRECTED. The paragraph that follows it is
> **FALSIFIED** — the engine *does* take a raw-plaintext path (path B); RAWPLAIN must be **KEPT**,
> not re-keyed. See the top-of-file correction banner + `../../../SurfaceG-Rekey-Fix/`. Retained
> below as the superseded reasoning trail.

`RAWPLAIN` are the ~19 files shipped already-plaintext (e.g. Concent4
`pics/8/*.jpg`, `Pandora/9999.bmp`, `Findfast/.../cam/level1/my.bmp`).
~~The engine has **no raw-plaintext path for named files** —
`fcn.1000e1c0`'s no-decrypt fallback is gated on a *NULL* filename
pointer, never taken for real asset loads — so a plaintext file is
LCG-mangled by `F` and fails offline just like PHOTO. Re-keying them to
`F` (image = the raw bytes) normalises every asset to the single
invariant **on-disk = `lcg(true_image, F)`**, which the unmodified
engine decodes uniformly.~~ *(FALSIFIED — `fcn.1000e1c0` detects the
plaintext magic and loads RAW; re-keying RAWPLAIN to `F` makes the
engine `lcg(·,F)` real image bytes → red square. KEEP factory.)*

Validated end-to-end (`scripts/p9-lcg.py`, `p9-bruteseed.py`):

```
STD   Marbles ButtonN.bmp  size 57656   F=59163   -> BM bfSize=57656  160x120x24
STD   Towers  Exit.bmp     size 25796   F=26967   -> BM bfSize=25796  155x55x24
STD   Spaceace Background   1440056      F=1441928 -> BM bfSize=1440056 800x600x24
PHOTO Findit  PIC0591.jpg  size 28309   F=29556   seed 28552 -> valid JPEG (SOI/EOI)
PHOTO Concent4 1/45.jpg     size 19488   F=20325   seed 19644 -> valid JPEG
PHOTO Findfast tech.bmp     size 74392   F=75537   seed 74748 -> BM bfSize=74392 134x184x24
PHOTO Fq2     COS001.jpg    size 39101   F=40287   seed 39329 -> valid JPEG
```

For every PHOTO sample the recovered seed sits a few hundred bytes
above the filesize (Δ = +156…+356); for STD the formula matches exactly.
The PHOTO seed is some other deterministic function (not yet derived —
see Open items); brute-force makes deriving it unnecessary.

## 4. Why brute force is exact, not a guess

The acceptance oracle is structural, not heuristic. **`bfSize == filesize`
is NOT used** — several assets (e.g. Trivia `Tr_Fe/grafix/*.bmp`) use
the header-only convention `bfSize = 54`, which the real engine accepts
and an over-strict tool wrongly rejected (an early Phase-9 pass mislabelled
those 11 files UNKNOWN; they are in fact STD — the engine's BMP
acceptance is exactly this loose, which is *why* they ran in Phase 8).
The validated oracle:

- **BMP**: `biSize ∈ {12,40,108,124}` **and** `0 ≤ bfOffBits ≤ 4096`
  **and** `0 < width ≤ 8192` **and** `0 < |height| ≤ 8192` **and**
  `bpp ∈ {1,4,8,16,24,32}`. Combined false-positive ≈ 2⁻⁶⁰.
- **JPEG**: SOI+APPn `FF D8 FF` **and**, in the multi-seed brute loop,
  the fully-decrypted file ends with EOI `FF D9` (single-trial formula
  / raw checks skip the EOI confirm — one trial at 2⁻²⁴ is decisive and
  it avoids a full-file decrypt per file).

The PHOTO seed always lies a few hundred–few thousand bytes above the
filesize, so a tight window `[filesize-2048, filesize+12000]` finds it
immediately; a full `[0, filesize+300000]` sweep is the fallback.

## 5. The fix — re-key, not decrypt-to-plaintext

Dropping plaintext on disk in place of a **PHOTO** file would not work:
because the on-disk magic is enciphered, the engine LCG-decrypts that
slot with `F` first, mangling raw bytes. *(⚠ 2026-06-10: this is true
**only for files the engine takes down the decrypt path**. RAWPLAIN
files whose on-disk magic is already plaintext take the **raw** path
(path B) and must be left as-is — the blanket "engine always LCG-decrypts
with F first" is FALSE. See banner.)* Instead, for each PHOTO file
(operating on the first `N = filesize & 0xFFFF` bytes only):

```
plain = LCG_decrypt(disk_bytes, recovered_seed)   # true image
new   = LCG_encrypt(plain, F)                      # == plain XOR ks(F)
```

`new` has the same length, so `F` (a function of filesize+name) is
unchanged, and the unmodified engine's own `LCG_decrypt(new, F)`
yields the true image. **No binary patch to any of the 38 game DLLs,
no dongle, transparent at runtime.** STD files already satisfy this and
are left byte-for-byte untouched.

Verified: a re-keyed `Amore/common/pics/1.jpg`, decrypted with the
engine formula seed `F` (exactly what the game DLL does at runtime),
produces `FF D8 FF E0…` — a valid JPEG.

## 6. Tooling & artifacts

| Script | Role |
|---|---|
| `scripts/p9-classify.py` | first-pass offset-0 magic scan — **superseded** (offset-0 is not the engine's discriminator; kept for history of the false 1.3 GB "all encrypted" reading) |
| `scripts/p9-lcg.py` | LCG model + ground-truth probe (validated keystream) |
| `scripts/p9-bruteseed.py` | seed brute-force + oracle validation |
| `scripts/p9-rekey.py` | **production** (corrected 2026-06-10, Surface-G fix): classify (STD/PHOTO/RAWPLAIN/UNKNOWN) → recover seed → re-key **PHOTO only**, first `N` bytes → staging tree + manifest/report. Full-PIL-decode oracle; KEEPs STD/RAWPLAIN/UNKNOWN byte-identical to factory. Legacy buggy version kept as `p9-rekey.py.bug.bak`. Validator: `scripts/p9-validate.py` (L1–L5). |

(A `p9-offset.py` scratch encoding the falsified "the value is a file
offset" hypothesis was discarded — the value is the LCG *seed*, not a
seek position.)

`p9-rekey.py` outputs (under `analysis/p9/`, originals never mutated —
`Extracted/` is a read-only forensic snapshot):

- `manifest.csv` — per file: class, filesize, formula_seed,
  recovered_seed, seed−size, kind, roundtrip_ok
- `report.txt` — per-game STD/PHOTO/RAWPLAIN/UNKNOWN tallies + totals
- `rekeyed/<relpath>` — re-keyed **PHOTO only** (STD/RAWPLAIN/UNKNOWN
  are not written; STD already decodes, RAWPLAIN is loaded raw and must
  stay factory bytes, UNKNOWN is never overwritten with a guess)

**Final full-corpus result** — ⚠ **superseded by the 2026-06-10 corrected run** (see below);
the original (buggy) tally is struck through:

| Class | Count | ~~old Action~~ | roundtrip_ok |
|---|---|---|---|
| STD | ~~4 856~~ | untouched (engine decodes as-is) | 1 |
| PHOTO | 23 395 | re-keyed first N to `F` | 1 |
| RAWPLAIN | 19 | ~~re-keyed to `F`~~ | 1 |
| UNKNOWN | ~~1~~ | left untouched | 0 |

~~**28 270 / 28 271 (99.996 %) recovered, dongle-free.** The only
`UNKNOWN` is `Soli/common/Grafix/800x600/quitpic.bmp` (§8 — partial
header encryption, non-critical exit splash). `rekeyed/` holds the
23 414 PHOTO+RAWPLAIN files (673 MB); STD and the 1 UNKNOWN are not
written (left as the original media has them).~~

**Corrected full-corpus result (2026-06-10, `analysis/p9/report.txt`, 28 271 files; Surface-G fix):**

| Class | Count | Action | render_ok |
|---|---|---|---|
| STD | 4 857 | untouched (engine F-decrypts first N) | 1 |
| PHOTO | 23 395 | re-key first N to `F` (`lcg(lcg(raw[:N],seed),F)+raw[N:]`) | 1 |
| RAWPLAIN | 19 | **KEEP factory** (engine loads RAW — re-key = red square) | 1 |
| UNKNOWN | 0 | — | — |

**28 271 / 28 271 (100 %) dongle-free.** `rekeyed/` (and the sparse `Overlay/PP_NG1_7712002-V5/E/Prog/Games`)
hold **23 395 PHOTO files only**; STD + the 19 RAWPLAIN are left byte-identical to factory.
The former lone UNKNOWN `Soli/.../quitpic.bmp` is **STD** under the length-bounded cipher
(N = 196 616 & 0xFFFF = 8). `validate.py` L1–L5 = PASS (anchors `Concent4/.../1/45`=19644,
`Findit/.../PIC0591`=28552, `Fq2/.../COS001`=39329, `Findfast/.../tech.bmp`=74748). Canonical:
`../../../SurfaceG-Rekey-Fix/`.

## 7. Deployment (user-side, one-time, offline)

1. Run `python3 scripts/p9-rekey.py` (full corpus, one-time, offline).
2. Confirm `report.txt`: **UNKNOWN = 1** (only `quitpic.bmp`, §8) and
   every other file STD/PHOTO/RAWPLAIN with `roundtrip_ok=1`.
3. Overlay `analysis/p9/rekeyed/*` onto the game tree on the E:
   partition (path-for-path into `E:\Prog\Games\`), via re-master into
   `PROG.pqi` or a direct copy into the VM/86Box guest. STD and the one
   UNKNOWN file require no action (left as the original media has them).
4. Boot with the existing **P7** patch set. Photo games now load with
   no dongle and no extra binary patch. The P7 surface (main.exe, the
   three DOS stubs) is **orthogonal and unchanged** — it gates *boot*;
   Phase 9 is purely *asset data* inside already-running game DLLs.

## 8. Open items / optional refinements

- **Derive the exact PHOTO seed formula.** Δ(seed−filesize) is small
  and deterministic per file; recovering the closed form removes the
  brute step entirely (pure formula transform). Not required —
  windowed brute is fast and certain — but tidy.
- **Scope beyond `Prog/Games`.** `Foto32/` menu/StdGfx assets decode
  fine today (Phase 8) ⇒ they are STD; `Advert/` not yet scanned. If a
  full media re-master is wanted, run the same tool over those trees.
- **Performance.** The LCG is sequential; the pure-Python keystream is
  the bottleneck on the ~1.3 GB corpus (one-time, background, no Claude
  cost). A closed-form/numpy keystream is a possible speedup if the
  pass is re-run often.
- **The one `UNKNOWN`: `Soli/common/Grafix/800x600/quitpic.bmp`.**
  This file is **partially encrypted**: only a short header prefix is
  LCG-enciphered, while the rest is already plaintext (decrypted bytes
  8+ read `00 00 36 00 00 00 28 00` = `bfReserved=0,
  bfOffBits=54, biSize=40` in the clear, with only the first ~8 bytes
  ciphertext). No single whole-file seed reconstructs it, so the tool
  correctly refuses to overwrite it (a bad re-key is worse than the
  original). It is the Solitaire **quit splash** — shown only on exit,
  not gameplay-critical — so leaving it as the original media has it is
  acceptable. A targeted prefix-only re-key is a possible follow-up if
  exact fidelity is wanted. 1 / 28 271 files (0.0035 %).
- **Lesson banked:** an early Phase-9 oracle required
  `bfSize == filesize`; that wrongly flagged 11 Trivia `Tr_Fe` BMPs
  (header-only `bfSize=54`) and the 19 already-plaintext files as
  UNKNOWN. Corrected to a structural oracle + a `RAWPLAIN` class; the
  Trivia files are in fact plain **STD**.

## 9. CLAUDE.md / Phase 8 / memory sync

- `Docs/protection/08-graphics.md` §8c/§8d/§8e and §9: the
  "live per-game dongle decryption oracle" / "Phase 2E falsified"
  framing is **superseded by this phase**. The dongle path
  (`fcn.10018860`) exists but is *not a needed crypto oracle*; it held
  a recoverable seed.
- `CLAUDE.md` `E:\Prog\Games\` bullet + `Hardware bindings` notes:
  update to state photo-pack protection is a breakable LCG (Phase 9),
  no dongle required offline.
- Memory `project_game_dll_protection`: update to the Phase 9 finding.
