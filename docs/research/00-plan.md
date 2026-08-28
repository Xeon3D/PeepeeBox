# Phase 0 — Bootstrap and plan

## Goal

Run every supplied Photo Play / I.G.O. DOS installation without its HASP dongle, on 86Box.

## Ground truth

20 raw hard-disk images, 1.55–4.0 GB each, never modified. All work happens on copies or
through a read-only reader. Per-image file manifests: `analysis/p0/raw/tree-*.txt`.
Extracted `MENU/` + `EXE/` binaries: `analysis/p0/ex/` (194 MB, 422 protected executables).

No image was mounted and no root was used: `scripts/fatread.py` is a self-contained
MBR + FAT12/16/32 reader/writer that maps a file's cluster chain to image byte offsets.
All executable patches are same-size in-place edits written straight through that mapping.

## Decision: emulate the dongle, or patch the executables?

**Patch.** Ruled out dongle emulation early (guardrail 6 — the boring explanation first):

- A HASP *hardware* emulator would need a parallel-port device model inside 86Box; a
  *software* emulator would need to hook the HASP API entry point, which is exactly what
  `KEYN.COM` already does — and `KEYN.COM` is demonstrably **not sufficient on its own**.
  Every historical KEYN build also carries a second patch, because the TSR reproduces the
  dongle's version banner but not the crypto payload.
- Patching removes the dependency entirely, needs nothing resident, and — as it turns out —
  is provably a no-op-and-return-0 on a single branch instruction.

`KEYN` mode is still implemented, for reproducing and verifying the historical builds.

## Surface table

| # | Surface | Where | Type | Cadence | Status |
|---|---|---|---|---|---|
| A | HASP dongle read + version/crypto validation | `check_dongle()`, once per game EXE | value-bearing → bool gate | once per process, cached | **bypassed** (`Docs/01`) |
| B | `Wrong Version` report | second, independent reader of the same buffer | bool gate | once per process | **bypassed** (`Docs/01`) |
| C | Dongle error screen | `MENU.EXE` | bool gate | every menu entry | **bypassed** (`Docs/01`) |
| D | `?DONGLE FAILED` reporters | `Error()` call sites keyed on `g_errcode` | reporting | on failure | inert once A returns 0 |
| E | Hardware wait loops / watchdog reboot | `MENU.EXE` | blocking wait | every boot | **open, not dongle** (`Docs/02`) |
| F | Touchscreen: ELO vs MicroTouch | `\TOUCH\TCH_INIT.BAT` vs `86box.cfg` | config mismatch | every boot | **open** (`Docs/02`) |
| G | MicroCosm CopyControl (Photo Play 2.0) | n/a in these images | disk binding | — | deferred by the user |

## Blind-spot audit (run at bootstrap, per guardrail 7)

| blind spot | owner | finding |
|---|---|---|
| linked-but-dead vs live code | Phase 1 | far-call xrefs rebuilt from the MZ relocation table (`scripts/xref.py`). This is what showed the `55 → CB` patch target is `Error()` with ~150 call sites, not a protection routine. |
| runs-once vs every-boot | Phase 1 | `check_dongle` self-caches in `g_checked`; the MENU check runs on every menu entry. |
| conditionally-dispatched paths | Phase 1 | the `Wrong Version` reader (surface B) is exactly this — a second consumer only reachable after the first returned. Found by cross-checking buffer offsets, not by reading code. |
| packing / self-modifying code | Phase 0 | none. Plain Borland C++ MZ images, full relocation tables, readable strings throughout. |
| anti-debug / integrity self-check | Phase 1 | one anti-tamper booby trap: the compare loop XORs the dongle buffer with 0xFF as it walks it. Irrelevant once the function returns early. |
| time/counter bombs | Phase 2 | one found — the `MENU.EXE` hardware-wait timeout that cold-reboots the machine (`Docs/02`). Not dongle-related. |
| code loaded at runtime | Phase 2 | `MENU.EXE` error text is indexed into a *loaded* resource, not the EXE image — which is why offsets like `0x34B` do not resolve against the executable's own strings. Noted so a future session does not re-walk it. |

## Phase ledger

| Phase | Deliverable | Outcome |
|---|---|---|
| 0 — bootstrap & plan | this file | 20 images mapped, PTS-DOS/FAT16 identified, read-only FAT reader built |
| 1 — dongle surface | `Docs/01-hasp-protection.md` | protection fully characterised; `scripts/ppkeyless.py`; 184 files reproduced byte-for-byte against the reference builds; 2 bugs found in those builds |
| 2 — 86box surface | `Docs/02-86box-surface.md` | hardware waits + watchdog reboot identified as a separate, non-dongle surface; ELO/MicroTouch config mismatch found |

## Abandoned branches

| branch | why abandoned | anything still live? |
|---|---|---|
| HASP hardware emulation in 86Box | unnecessary once `check_dongle` proved to be a single-branch bypass | no |
| Deriving `MENU.EXE`'s DGROUP base from its embedded strings | the error text is indexed into a runtime-loaded resource with the same offsets, so the embedded copy gives a false base | the string *table* is still a valid map of the 11 dongle messages |

## What the user supplies

- Confirmation that 86Box itself drives a known-good image (in progress).
- Which images to write out as keyless deliverables.
