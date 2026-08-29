# Phase 13 — The dongles are funworld's own. "HASP" is a red herring

**Correction of a framing error that runs through `Docs/01`, `04`, `07`, `09`, `10` and
`11`.** Those documents call the parallel-port token a *HASP* and, for the 2001+ builds,
assert that it is reached "through Aladdin's linked-in library". That was an assumption,
never evidence, and it is **wrong**.

`Docs/12` already established the 1999 device from its own silicon: a custom funworld
two-chip dongle (AT89C2051-class 8051 + 24Cxx I²C EEPROM), nothing to do with Aladdin.
This document extends the correction to **every** generation and shows where the
misleading evidence comes from.

---

## 1. The front end supports a whole family of dongle types

`MENU.EXE` carries a table of per-type "not found" messages. Counting the distinct types
present in each generation's `MENU.EXE`:

| Image | Dongle types in the table | `HASPDOSDRV` present |
|---|---|---|
| `1999*` (all three) | **none** | no |
| `2000NL-C6532` | C | no |
| `2001IT-A3735` | C, **H** | no |
| `PPIGO2PT` (2002) | C, G, H | no |
| `IGO3SE`, `PP1998SP` | C, G, H, I | no |
| `IGO4AT` (2004) | C, G, H, I, K | no |
| `IGO5*` (2005) | C, G, H, I, K, M, MB | **yes** |
| `IGO6AT` (2006) | C, G, H, I, K, M, MB, N | **yes** |
| `IGO7AT` (2007) | C, G, H, I, K, M, MB, N, O | **yes** |
| `IGO8ES` (2008) | C, G, H, I, K, M, MB, N, **NG**, O | no |

The table **grows by roughly one entry per year and never loses one**. This is a vendor
accumulating support for successive dongle designs while keeping the old code paths for
machines already in the field.

Two things follow immediately:

- **`H` is one supported type out of ten.** Its presence says the *software* can talk to a
  HASP, not that any given cabinet has one.
- **The 1999 builds have no table at all** — consistent with `Docs/12`, where the 1999
  device is a single hardcoded custom design.

## 2. Where the "HASP" impression came from

Three artefacts, all genuine, all misread:

| Artefact | What it actually is |
|---|---|
| `HASPDOSDRV` in IGO5/6/7 binaries | the device name Aladdin's DOS driver installs. Code that opens it is *probing for* an H-type dongle — the `H` row of the table above |
| `No HASP!`, `Wrong HASP!` | the H-type path's own error strings |
| `@CheckHasp$qv` | a Borland-mangled `CheckHasp()` — funworld's own function name for that path |

Note the distribution: these strings appear in **IGO5, IGO6, IGO7 and not in 2001 or
IGO8**. If the 2001+ generations really went through an Aladdin library, the vendor's
strings would be present in 2001 — they are not — and would still be present in 2008 —
they are not. The strings track the *H code path*, which is only one branch.

## 3. What each generation actually uses

Established by observation, not inference:

| Generation | Token | Evidence |
|---|---|---|
| 1999 | **custom funworld 8051 + 24Cxx** | firmware disassembled and executed, `Docs/12` |
| 2008 | **NG** — `NG-DONGLE not found` on screen | live boot, `Docs/09` |

For 2000–2007 the type in use has **not** been confirmed, and this document does not
claim it. The pattern — each generation adding a type and shipping the newest — makes
`N` for IGO6, `O` for IGO7 and `M`/`MB` for IGO5 the obvious guesses, but they are
guesses. Booting each image without a dongle names its type on screen, exactly as IGO8
announced `NG`; that is the cheap way to settle it and it has not been done yet.

**No generation has been shown to use an Aladdin HASP.**

## 4. What this changes, and what it does not

### Does not change

The recovered 1999 wire protocol in `Docs/07` is unaffected — it was read out of the game
binaries' own inline bit-banging and validated live, and `Docs/12` independently confirms
it from the dongle's firmware. Only the *name* in that document is wrong. Likewise
`KEYN.COM` (`Docs/10`) and the DS1982 emulation (`Docs/11`) work exactly as described.

### Does change

- **`Docs/04`'s central premise.** It frames the 2001+ problem as "reverse the Aladdin
  library". There is no reason to believe an Aladdin library is involved, so the hard
  problem it describes is the wrong problem.
- **`Docs/09`'s framing.** It says the 2008 generation "reaches the dongle through
  Aladdin's linked-in library". It does not; NG is a funworld design like the rest.
- **The naming throughout.** `check_dongle`, not `check_hasp`; the *dongle* block, not the
  *HASP* block. `Docs/01` and `Docs/07` carry `hasp` in their filenames for historical
  continuity; the filenames are not evidence.

### Why it matters practically

The 1999 device was cracked by treating it as **funworld's own silicon** and reading its
firmware — not by attacking a vendor cryptosystem. Nothing about NG suggests a different
approach is needed. Framing the remaining work as "reverse Aladdin's library" points at a
problem that probably does not exist and makes the real one look harder than it is.

## 5. Next step this suggests for NG

`Docs/09` exhausted black-box search: every distinct STATUS value and every data-readback
transform fails, and no further traffic exists to observe. The 1999 precedent says the
productive move is to work from the **device**, not the host.

If a physical NG dongle can be dumped the way the 1999 units in `Dongle (1999)/` were, the
same toolchain applies directly — `analysis/p12/dis51.py` and `sim51.py` already
disassemble and execute MCS-51 firmware against a simulated I²C EEPROM and LPT host. That
is a far shorter path than decoding the probe from `MENU.EXE`'s overlays.
