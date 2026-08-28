# Phase 6 — `--mode menu` is not sufficient. Corrected.

**This falsifies the central recommendation of `Docs/01` and `Docs/03`.** Reported from the
field by the user: on the ten `--mode menu` images, *"every game that uses photos (find it)
for example does not actually load the images and stalls"*, while *"the versions with the
keyn patch work fine"*.

## Why menu mode breaks photo/localised games

`--mode menu` makes `check_dongle()` return 0 immediately. That is correct as far as the
*error* logic goes — no error bits, no error screen. But it also means the HASP is never
read, so the **62-byte dongle buffer stays all zeros**.

That buffer is not only an error-check input. The localisation code reads the territory
straight out of it:

```asm
get_country(char *dest):
    [dest] = 0
    strstr(dongle_buf, "(")        ; find "(AT)" inside "Version 2006 (AT)"
    if (!found) goto done          ; <- with an empty buffer, always taken
    strncpy(dest, found+1, 2)      ; the two-letter territory
done:
    dest[2] = 0
```

With an empty buffer `dest` comes back as `""`. The strings in the same binaries name what
happens next: `fotoplay_getlanguage: failed reading language`,
`\foto\localize\language.csv`, `\foto\localize\lngname.csv`. Anything that resolves a
localised or photo asset path fails, and the game stalls.

KEYN builds work because the TSR **populates** that buffer with a real banner, so the
territory parse succeeds. Confirmed independently by the Phase 4 experiment: with
`KEYN.COM` supplying `Version 2001 (IT)`, the error screen reported `Language : ITA` and
`SYSTEM.INI` came out `ITA ITA,ENG,GER` — that is exactly this parse working.

## Why I did not catch it

The IGO8ES runtime test (`Docs/01` §7) only reached the **menu**. The menu recovers its
language from elsewhere, so it looked healthy — `SYSTEM.INI` even came out correctly as
`SPA ENG,FRE,SPA`. The dependency only bites once a game loads localised assets, which
that test never did. *A boot-to-menu test does not validate a dongle bypass.*

## Consequence

`--mode keyn` is now the **default and the recommendation**. `--mode menu` is retained
only for reproducing the historical menu-patched builds, and is documented as
photo-breaking.

**The user's own `Menu Patched Versions/` (IGO4-K, IGO8-K) are menu-patched and therefore
carry this same latent defect**, whether or not it has been noticed yet.

## What had to be generalised to make keyn work everywhere

The keyn path had only ever been exercised on variant-A executables (2001+). The older
families needed two fixes:

| | before | after |
|---|---|---|
| HASP call site | fixed pattern `74 03 E9 ?? ?? 68 ?? ?? 0E E8` — variant A only, one pushed argument | scan forward from the prologue for `push <buf>; push cs; call`, where `<buf>` must equal the buffer the "Wrong Version" report prints. Variants B/C push **two** arguments (`push <port>; push <buf>`), but the buffer is always the word pushed immediately before `push cs` — which is exactly the stack slot the KEYN TSR reads, so the TSR needs no change |
| iButton error bit | `80 7E F6 00 75 0B 8B 46 FE 0D 01 00 ...` | `80 7E ?? 00 75 ?? 8B 46 FE 0D 01 00 ...` — the local-variable offset and the `jnz` displacement vary by generation; `or ax,1` does not |

Both changes were regression-checked: `analysis/p1/sh/validate.py` still reproduces
**184 reference files byte-for-byte**.

## New tooling

- `--mode revert` — undoes menu-mode edits (`90 90` → `74 03`/`74 06`, `EB` → `74`),
  inferring the original displacement from the following instruction.
- **Auto-detected banner.** `--mode keyn` now derives `Version 20xx` from the executables
  themselves and appends a territory (`--territory`, else guessed from the image name).
  This matters: the banner must satisfy `strstr(buf,"Version 20xx")` *and* carry the
  right `(XX)` for localisation.
- Already-applied edits are recognised, so re-running is idempotent.

## Banner per release

| release | banner | release | banner |
|---|---|---|---|
| PP1998 / 1999 | `Version 99` | IGO4 | `Version 2004` |
| 2000 | `Version 2000` | IGO5 | `Version 2005B` |
| 2001 | `Version 2001` | IGO6 | `Version 2006` |
| IGO2 | `Version 2002` | IGO7 | `Version 2007` |
| IGO3 / PP1998SP | `Version 2003` | IGO8 | `Version 2008` |

## Result

All ten `Other Versions/` folders reconverted and audited (`analysis/p3/audit_keyn.py`):
279 protected games with the HASP read redirected **and** the iButton bit neutralised,
every `MENU.EXE` closed, every `KEYN.COM` well-formed with the correct banner, every
`AUTOPTS.BAT` calling it exactly once before `main.com`. **ALL PASS.**
