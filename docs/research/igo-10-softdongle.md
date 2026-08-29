# Phase 10 — Config-driven soft-dongle (TDongle accessor emulation)

Status: **VALIDATED on VM (2026-05-16).** With `CountryID=NL` the
profile resolves to Netherlands and the original software regenerates
`progs.ini` / rebuilds the menu for that region — the menu now lists
the NL game set (more games than the frozen-UK Phase 7 build). The
soft-dongle is the current dongle bypass; **Phase 7's dongle patches
(main.exe Patches 1–7) are superseded.** Phase 7's BIOSVCHK / DALLAS /
TECHDATAS stubs and the Phase 9 game assets are unaffected and still
apply.
Class: **faithful NG-dongle simulation, not a pipeline bypass.** One
binary (`main.exe`), accessor-level only; every downstream original
mechanism is left intact and every patched path is traced to its
consumer (no guessed open ends).

## 0. Model

A genuine NG serial dongle returns three values the boot path uses:
`ProdID`, `VersionID`, `CountryID` (plus `IndexID`, `ProfileSub`, two
strings). **`ProdID` and `VersionID` are product/build constants —
every NG dongle returns the same ones** — so they are *hardcoded* into
the accessor stubs (this is faithful simulation, not a guessed fake
region). **Only the region differs between dongles**, so `CountryID`,
and only `CountryID`, is read from a plain-text virtual SIM,
`\Prog\vdongle.ini`. Switching region = editing that one line, exactly
like swapping the physical SIM; the original software then does
profile selection, `setup.dll` regenerating `progs.ini`, and the menu
DB filter natively.

`CONFIG.INI` is **software output** — the app *writes*
`[Profile]Profile=/Version=/ProductID=` *from* the dongle. It is never
an input to the emulation.

This supersedes the Phase 7 dongle surface (which froze one fake region
in `.data` and bypassed `Get_ProfileAvail`). P7 and P10 are mutually
exclusive on `main.exe`; deploy one.

## 1. Hardcoded constants — evidence (no guess)

- **`ProdID = "NG"`** — `profile.MYD`: 51 profile rows, prodid `NG`
  for every one; zero `HG`/`PP`/`IGO`. The orchestrator additionally
  cross-checks the dongle `ProdID` (`0x437150`) against
  `CONFIG.INI [Profile]ProductID` via the Config object `[0x437648]`
  at `0x00407947`; returning `"NG"` satisfies that check by
  construction (the install's own `ProductID=NG`).
- **`VersionID = "WIN 02"`** — present in `profiledata` (`datakind
  'Version'`); Phase 7 dynamic test booted to the menu with exactly
  this value (`07-patches.md`, round 2). `profiledata` also holds
  `WIN2002`/`WIN2002B` (progressive build stamps); `WIN 02` is the one
  this build (`7712002-V5`) pairs with — corroborated by the
  software-written `CONFIG.INI [Profile]Version=WIN 02`. Whether a
  given region's profile carries a `WIN 02` row is a DB-content fact
  the *original* `Get_ProfileAvail` decides (returns rows or not — the
  faithful outcome); it is not an open end in the patch.

## 2. RE — accessor contract (traced, `Extracted/PP_NG1_7712002-V5/E/Prog/main.exe`)

`TDongle` is statically linked, live only in `main.exe`. Orchestrator
`fcn.004069c0` calls `Init` then the accessors via
`mov ecx,[0x437e54]` (global `TDongle*`). Each accessor is
**`__thiscall TDongle::GetX(char* outbuf)`** — `outbuf` at `[esp+4]`,
returns `AL` (1 = ok), `ret 4`; on `AL==0` the orchestrator throws
`MAIN: [ERROR] - Cannot read from dongle!`. Bodies are uniform
(`55 8b ec 6a ff …`) and **exactly 0x70 bytes** (next entry at
`+0x70`), so the whole body is free to rewrite. `ecx`(this) is unused
by the stubs; stack stays balanced (`ret 4`; `GetPrivateProfileStringA`
is `__stdcall` and cleans its own 6 args).

| Method | VA | dest (orch.) | Emulation | Why proven safe |
|---|---|---|---|---|
| `Init` | `0x411800` | — | `mov al,1; ret` | orch only tests `AL` after `call`; no COM2 needed |
| `GetProdID` | `0x411b80` | `→0x437150` | const `"NG"` | §1; matches the `0x407947` cross-check |
| `GetCountryID` | `0x411bf0` | `→0x437148` | `[dongle]CountryID` from `\Prog\vdongle.ini` | feeds `Profile::SetCountry` + `Get_ProfileAvail` — original validation kept |
| `GetIndexID` | `0x411c60` | `[ebp-0x20]` | `*outbuf=0; AL=1` | buffer has **one** ref (the `lea`) in the whole `0x4076bc–0x407f7c` licence span; slot later reused as an FP temp (`fstp [ebp-0x20]`) ⇒ value not live ⇒ content irrelevant, only `AL` gated |
| `GetProfileSub` | `0x411cd0` | `[ebp-0x44]` | `*outbuf=0; AL=1` | same proof; `[ebp-0x44]` has one ref in the span |
| `GetVersionID` | `0x411d40` | `→0x437244` | const `"WIN 02"` | §1 |
| `GetString1` | `0x411db0` | IGO1 payload | `*outbuf=0; AL=1` | consumed only inside the IGO1 block, which is unreachable (next row) |
| `GetString2` | `0x411e20` | `&[ebp-0x11]` | `*outbuf=0; AL=1` | `0x4076fa` presets `[ebp-0x11]=1`; we write 0; `0x40774b je 0x4078c3` is then **taken**, making the entire `LoadLibrary("rundll32.dll")`→`RunDLL` IGO1 block unreachable. Proven from disasm, not assumed — no IGO1-skip byte patch needed. |

Worker `fcn.00411980`, `Get_ProfileAvail` (`0x407ebd`), the
country/`CONFIG.INI`-Profile compare (`0x4079f9`/`0x407ac3`), profile
selection (`0x418130`), `setup.dll` progs.ini regeneration, and the
menu DB filter are **untouched** — the accessor stubs never reach the
worker, so it stays original.

## 3. The patch (`analysis/p10/sh/apply-softdongle.py`)

In → `Extracted/PP_NG1_7712002-V5/E/Prog/main.exe`; out →
`analysis/p10/work/E/Prog/main.exe` (size preserved 299008 B; sha256
`765e8d3a098f8a3c91a3977ba952e8e2226148c17191f8fd77410c8b09db7ec7`).

Stubs: `Init` 3 B; `GetProdID`/`GetVersionID` immediate-store
constants (`mov dword[eax],…; mov dword[eax+4],…; mov al,1; ret 4`);
`GetCountryID` 38 B `GetPrivateProfileStringA("dongle","CountryID","",
outbuf,64,"\Prog\vdongle.ini")`; `GetIndexID/ProfileSub/String1/
String2` 12 B `mov byte[eax],0; mov al,1; ret 4`. The four CountryID
strings live in `GetString2`'s freed body at `0x411e2c`
(`"dongle"`,`""`,`"\Prog\vdongle.ini"`,`"CountryID"`). Verifies
originals before writing; aborts on mismatch; idempotent. The patcher
is the byte source-of-truth.

## 4. The virtual SIM — `\Prog\vdongle.ini`

```
[dongle]
CountryID=NL
```
One line. `CountryID` = a `country.code` (`country.MYD`: `NL`, `UK`,
`AT`, `DE`, `BE`, `CH`, …). Default ships `NL` (this disc was dumped
by a Dutch collector; `NL_EURO` "Netherlands (Euro)" is a valid
profile and the NL content — `FQ2_NLMX` etc. — is on the disc).
Drive-relative path (current drive = E: on the kiosk, same convention
as the engine's own `\foto32\config\config.ini`). If the file is
missing, `GetPrivateProfileStringA` returns `""` → `Get_ProfileAvail`
finds nothing → the original "no profile" path (a correct fail-safe,
not a crash).

## 5. Deploy / test (boot is the ground truth)

1. `python3 analysis/p10/sh/apply-softdongle.py`
2. Overlay onto the kiosk: `analysis/p10/work/E/Prog/main.exe` →
   `E:\Prog\main.exe`; `…/vdongle.ini` → `E:\Prog\vdongle.ini`. Keep
   the P7 BIOSVCHK/DALLAS/TECHDATAS stubs + P9 game assets; **replace**
   the P7 `main.exe`.
3. Boot with `CountryID=NL`. Expect: profile selector offers
   **Netherlands (Euro)**; `setup.dll` (re)generates `progs.ini` for
   that profile; menu lists NL's provisioned games with content — no
   broken `FQ2_MX`/`FMEM2`. Flip `CountryID` (e.g. `AT`) + reboot →
   menu/`progs.ini` track the new region with no rebuild on our side.
4. If `progs.ini` is stale and `setup.dll` does not run on profile
   select, delete `E:\Foto32\Config\progs.ini` so it is regenerated
   for the active SIM.

## 6. Result & residual (DB-content only — architecture is closed)

- **Confirmed:** `CountryID=NL` → Netherlands profile selected, menu
  populated with the NL game set. The model holds — only the region
  varies; the original pipeline does the rest.
- Per-region residual is data, not code: a region only works if its
  profile carries a `WIN 02` `profiledata` row. The untouched original
  `Get_ProfileAvail` decides this; an empty profile selector for some
  other `CountryID` would mean that region's DB lacks the `WIN 02`
  row — a data observation, not a patch defect.
- `CLAUDE.md` and `07-patches.md` updated to mark the Phase-7
  dongle-patch surface superseded by this soft-dongle.
