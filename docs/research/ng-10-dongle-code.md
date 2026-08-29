# Docs/10-dongle-code.md — Phase 10: the SECOND dongle surface

**Trigger:** the user's per-title results. This is the finding that broke the case open.

| title | runs | result |
|---|---|---|
| AMORE | `AMORE.EXE` | **works** |
| HEXAGON / SHANGHAI / ELEVENS | own EXEs | **works** |
| CONCENT2 | **`CONCENT.EXE`** | **`DONGLE CODE ERROR`** |
| FMEMO | `FMEMO.EXE` | `Database … may be corrupted!` |
| FINDIT, FND_MORD | **both `FINDIT.EXE`** | freeze |

(The `EXE` mapping comes from `FOTO/SETTINGS/GAMES.DBF`: `CONCENT2`→`CONCENT`,
`FND_MORD`→`FINDIT`. So there are **three** failing binaries, not five.)

## 1. "Photo games fail" was the wrong frame

**AMORE works** — and it is one of the heaviest photo titles in the set (99 `.DAB` files,
72 MB). Phases 8–9 spent effort on a photo/general split that never existed. The real
split is which binaries reach a *second* dongle function.

## 2. The second surface — value-bearing, and P1/P7 never touched it

`CONCENT.EXE` `0x17CA4` (the same shape in all 25 modules):

```
  enter 4,0
  cmp   dword [0x4905],0
  jz    ret
  mov   dword [bp-4],0x12345    ; <- the vendor's OWN DEFAULT code
  cmp   byte [0xAA],0
  jz    skip_read               ; flag clear -> keep the default
  push  <buf>
  call  get_dongle_code         ; -> DX:AX ; returns -1 with no hardware
  mov   [bp-4],eax
skip_read:
  cmp   dword [bp-4],-1
  jnz   ok
  push  "DONGLE CODE ERROR" ; call fatal_error   <- what the user saw
ok:
  mov   eax,[bp-4] ; shld edx,eax,0x10 ; leave ; retf   ; returns the DWORD
```

This is **not** the P1/P7 protection check. That one returns a pass/fail bitmask; **this one
returns a 32-bit value the caller consumes.** It is the *payload* surface, and I had only
ever patched the *verdict* surface.

Present exactly once in **all 25 modules** (working ones too) — shared library code. What
differs is whether a given title sets the `[0xAA]` flag, i.e. actually asks for a code.

## 3. This revises the Phase 9 answer

`Docs/09-cipher.md` concluded "no dongle input found" for the cipher. That stands **for the
photo archives** — the `GWAD` files really are XOR-`0x55` with a hardcoded key, proven by
decoding 472/472 entries offline. But it was **too broad a claim**: there *is* a
dongle-derived DWORD in this release, and its default is `0x12345` — **the same constant
seeded into the PRNG globals** (`mov dword [0x33C5],0x12345`, `mov dword [0x38A],0x12345`).

That is suggestive, not proven: I have **not** traced `get_dongle_code`'s return value into
the `.DAB` keystream generator. If it does feed it, the user's instinct — later releases
cipher photos through the dongle — has an ancestor here, in the question-database cipher
rather than the photo archive.

**Corrected position:** the 1999 *photo archive* cipher is dongle-free (hardcoded `0x55`).
A dongle-derived DWORD exists and may seed the *database* cipher. Two payloads, two
mechanisms — the same distinction I got wrong in `09 § 4`.

## 4. The patch

Force the vendor's own fallback: make the `jz` unconditional so the read is always skipped
and the function returns `0x12345` instead of `-1`.

```
  cmp byte [flag],0
  74 xx   jz skip_read   ──►   EB xx   jmp skip_read
```

2 bytes, 25 modules, size-preserving. `analysis/p10/sh/apply-p10-donglecode.py`, located by
the `mov dword [bp-4],0x12345` + `cmp byte [imm16],0` + `jz` signature (exactly one site per
module). Applied on top of the P7 tree → `analysis/p10/final/`.

**New image:** `86Box/1999NL-NODRM.img`
sha256 `6f6ea3bdbc23f7461f453f4dbd82ec64a2acefa1a0ab7ccf5cf7ed7081065780`.
Read-back 25/25 byte-identical; originals re-verified (`1999NL.img` `47da8f8e…16e8`, all
3404 extracted files unchanged).

## 5. Expected outcome, honestly stated

- **CONCENT2** should now clear `DONGLE CODE ERROR` — high confidence, the error is raised
  precisely on the `-1` this patch prevents.
- **FMEMO / FINDIT** — *uncertain*. If they fail because the code is `-1`, fixed. If they
  need the code's **actual value** (because it seeds the database cipher) and the real
  dongle returns something other than `0x12345`, they will still fail — but the *symptom
  should change*, which is itself diagnostic.

If FMEMO still reports `Database … may be corrupted!`, that is strong evidence the real
dongle code ≠ `0x12345` and the database cipher is dongle-seeded — i.e. exactly the scheme
the user described in later releases. The recovery route then is not the dongle: the `.DAB`
keystream is a fixed per-file constant, already shown recoverable statistically
(`Docs/09-cipher.md` § 1 decrypts real Dutch/German text with no hardware), so the data can
be pre-decrypted offline and the cipher disabled.

## 6. Method note

Fourth instance this project of verifying one object and concluding about another
(mask→buffer, check→caller, verdict→payload, archive-cipher→database-cipher). The
generalisation was avoidable each time. **A protection system with both a verdict and a
payload has two surfaces; finding one says nothing about the other.**
