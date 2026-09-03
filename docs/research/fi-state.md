# Funny's Interactive Playworld — project state

> **This was the working `CLAUDE.md` of a separate investigation workspace, folded into
> this repository so there is one Photo Play project.** It is kept because the surface
> table, the phase ledger and the abandoned-branches index are the fastest way back into
> the work; the `fi-NN-*.md` files are the canonical detail. It was written against a
> standalone workspace on `F:\`, so some paths below describe where things *were*: the
> evidence it cites now lives in `docs/research/evidence/fi/` and the scripts in
> `docs/research/evidence/fi-*.py`.

> **STATUS: Phase 5 complete — THE IMAGE RUNS UNMODIFIED, FROM A RIGGED FOLDER.**
> `docs/research/fi-00-plan.md` (bootstrap + surfaces), `docs/research/fi-01-bring-up.md` (it boots, and stops on
> the dongle), `docs/research/fi-02-hasp-wire.md` (the protection, solved), `docs/research/fi-03-dongle-device.md`
> (the device, shipped and passing), `docs/research/fi-04-cabinet-io.md` (the real hardware, and why
> touch is dead) and `docs/research/fi-05-ppngelo-rig.md` (merged onto the Elo build, rigged in
> `ppngelo\`) are the canonical deliverables — read them before anything else. `METHOD.md` has already been read; it rarely needs
> re-reading. This file is the per-session orientation doc and is kept lean on purpose.

## What this workspace is

This is **not a software project with a build pipeline.** There is no compile/lint/test
workflow; never look for one. It is a preservation and reverse-engineering workspace for
**Funny's Interactive Playworld, German build, OrgaControl Systemhaus / Funny's Planet
International GmbH, ~2001–2003** — a funworld-family touchscreen arcade kiosk running
DR-DOS 7.03. "Work" means inspecting binaries, the extracted image, configs, and producing
emulator-side changes so the image runs *unmodified*.

There is one exception to "no build": **PeepeeBox itself is built from source here**
(MSYS2 mingw64, Qt 5, `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON
-DUSE_QT6=OFF && ninja -C build`) from Phase 2 onward. That is emulator work, not image work.

Authorized preservation / interoperability context only.

## How to read this file vs. the others

- **`METHOD.md`** — immutable cross-project constitution. Already read at Phase 0.
- **`CLAUDE.md`** (this file) — per-session orientation and current project state.
  Update it at the end of every phase that moves a slot (hard rule, `METHOD.md § Maintenance`).
- **`Docs/NN-*.md`** — the canonical per-phase deliverable. **The only files read end-to-end.**
  Everything under `docs/research/evidence/fi/` is on-disk evidence to grep into, never read whole
  (`raw/peepeebox-symbols.txt` is 19 MB; `raw/fsystem-seg0.asm` is 1.1 MB).

## Hard guardrails (cue cards — rationale in `METHOD.md § guardrails`)

1. Originals immutable; work on copies under `analysis/`.
2. Shell first, Claude last; never read a multi-MB dump into context.
3. One canonical `Docs/NN-*.md` per phase; raw/JSON stays on disk.
4. Every mutation is a reproducible script with input + output SHA256.
5. Falsify your own framing; correct in place, never silently overwrite.
6. Parsimony before elaboration — including for tooling and methodology.
7. Adversarial-miss audit is an EARLY gate.
8. Plan for mid-execution discovery; surfaces appear out of phase order.

**Project-specific rule:** the deliverable is "runs on PeepeeBox with zero bytes changed
inside the filesystem". Patching `FSYSTEM.EXE` or any game file is a failed phase, not a
solution. Changes go in the emulator.

---

# PROJECT STATE

## System identity

- **OS / arch / era:** DR-DOS 7.03, x86 16-bit real mode + a 32-bit protected-mode half, little-endian, 2001–2003.
- **Boot chain:** PTS-DOS MBR → DR-DOS VBR → `IBMBIO.COM`/`IBMDOS.COM` → `CONFIG.SYS` (`TOOLS\SCREEN.EXE` splash + `HIMEM.SYS`) → `COMMAND.COM` → `AUTOEXEC.BAT` (`ELODEV` on COM3, four sound-init attempts) → `C:\GAME` → `FSYSTEM.EXE C3 I3` in a `:RESTART` loop.
- **App entry points:** `GAME\FSYSTEM.EXE` (Borland Pascal 7 real-mode launcher; probes touch + coin board, runs the dongle gate, installs an `iret` service handler — INT 50h suspected — then execs) → `GAME\FUNNY.DLL` (WDOSX 0.96 32-bit, compressed; the actual engine, all games inside it).
- **Toolchain:** Borland Pascal 7 (`Portions Copyright (c) 1983,92 Borland`); WDOSX 0.96 (Michael Tippach) for the 32-bit half; Aladdin HASP DOS library linked into `FSYSTEM.EXE`.
- **Backend/storage:** FAT16, 32 KB clusters, offline. The `FN_MAIL`/`FN_MST`/`FN_NEWS`/`FN_PORT`/`FN_SYS` portal suite and its `DFU` PPP stack are present but **dormant** — nothing starts them.

## Artifact buckets

| Bucket | Path | What it is | Read-only? |
|---|---|---|---|
| Original media | `F:\funny_interactive_de.img\Fixed.img` | 2 038 063 104 B FAT16 image, sha256 `776801cd…1ef06`. **The user ruled on 2026-09-03 that this IS the original, unpatched image.** Its 68.4 MB shortfall against its own BPB is therefore a property of the artefact, not damage introduced here. Never modify it. | yes |
| Under test | `F:\funny_interactive_de.img\HardDisk.img` | Byte-identical copy of the above under the only name PeepeeBox opens. Recreate with `docs/research/evidence/fi-stage-image.py` | yes |
| Extracted snapshot | `F:\funny_interactive_de.img\funny_interactive_de\` | Verified faithful extraction: 1 151 files, zero size mismatches, nothing missing either way; two Windows mount artifacts extra | yes (forensic) |
| Emulator under test | `F:\funny_interactive_de.img\PeepeeBox.exe` (+ `roms\`, `nvr\`, `86box.cfg`) | Shipped build, sha256 `5036dddf…5e3c`. Carries a full COFF symbol table | careful |
| **The rig** | `F:\funny_interactive_de.img\ppngelo\` | **The deliverable.** PeepeeBox built from the Phase 5 merge, + `ppfix.exe`, `roms\`, a **seeded** `nvr\4dps.{bin,nvr}`, `run.cmd`, and `HardDisk.img` byte-identical to `Fixed.img`. Boots the image unmodified | the deliverable |
| Emulator source (merged) | `%TEMP%\claude\…\scratchpad\ppb-latest\` (github.com/**Xeon3D**/PeepeeBox) | On upstream `697b52d`, the Elo commit. **Uncommitted.** Build: MSYS2 mingw64, `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON -DUSE_QT6=OFF && ninja -C build`. Diff + device in `docs/research/evidence/fi/` | the deliverable |
| Older builds | `PeepeeBox-p3.exe`, `PeepeeBox-p4.exe` in the workspace root | Phase 3 and 4, kept for comparison. `-p4` carries the **abandoned** global `COM3_IRQ = 3` and the MicroTouch wire trace | — |
| Evidence | `docs/research/evidence/fi/` | Image maps, the `FSYSTEM.EXE` disassembly and strings, the LPT cycle, the verifier's output | yes |

## Protection / lock surface table

| # | Surface | Where | Type | Cadence | Status |
|---|---|---|---|---|---|
| A | `IsHasp` (svc 1) | `FSYSTEM.EXE` `CS:0x02F3` | bool gate | every boot | **closed (P3)** — passing in the emulator |
| B | svc 6 status must be 0 | `CS:0x03D8` | bool gate | every boot | **closed (P3)** — passing in the emulator |
| C | words 0–5 = `ORGACONTROL `, passwords **`0x43B5` / `0x594A`** | `CS:0x045C` + `CS:0x02F7` | value-bearing | every boot | **closed (P3)** — guest reads the record; menu text legible, which is what a wrong key would garble |
| D | word 9, served to the app via INT 50h `AX=1235` | `CS:0x0CBA` | value-bearing | on demand | **exposed as the `word9` device option (P3)**; default `0000` accepted through boot, correct value still unknown |
| E | words 13+ read-increment-write counters (svc 4, `AX=0011`) | `CS:0x0BE2` | writable state | runtime | **implemented but unexercised (P3)** — no service-4 write has ever been observed, in the emulator or offline |
| F | `FUNNY.DAT/.AR/.DE/.EN/.MUL`, `FMEMO\RESOURCE\TEXT.*` cipher | data at rest | value-bearing | runtime | open — key source unknown, D suspected |
| G | Anything `FUNNY.DLL` checks on its own | compressed 32-bit half | unknown | unknown | not yet visible |
| M | The HASP-4 detection layer and the Microwire part are modelled as two objects sharing one port | `scripts/p2-*.py`, `dongle_funny.c` | modelling risk | — | open — held for a whole boot, still not what the silicon does |
| H | Touchscreen: Elo probed first (`CS:0x16FD`), MicroTouch second | `CS:0x11BB` | availability gate | every boot | **transport proven good (P4)** — MicroTouch handshake completes, packets delivered and read, app still ignores them. Upstream now emulates Elo; the FI image selects it by default (P5). Whether the app then responds is the open question |
| I | `CoinControl (6chanel)` I/O board probe | `CS:0x15E7` | availability gate | every boot | **closed (P4)** — `CS:0x1BFF`/`CS:0x1C59` install handlers and print "found" unconditionally; there is no probe and nothing is gated |
| N | The original cabinet was a Pentium II/III-class PCI board (`6WEV`), not the 486 the profile pins | `SOUND\MOBO.CSV`, `AUTOEXEC.BAT`, Cinepak 640×480@30 | performance / fidelity | always | open — the ~7 min load is this |
| O | A fresh `nvr\` halts POST at `CMOS checksum error … Press F1` | any new rig folder | rig correctness | first boot | **closed (P5)** — seed `nvr/4dps.{bin,nvr}` from a machine that has booted once |
| J | BPB geometry 63/64 vs PeepeeBox's fixed 63/16 | VBR + `src/photoplay.c` | correctness | every boot | **closed (P1)** — BIOS LBA-assist gives 987/64/63; boots clean, no disk errors |
| K | Image truncated 68.4 MB below its own BPB | partition + BPB | correctness | on write | **closed as a boot blocker (P1)**; still the right fix before sustained guest writes — `--pad` in `docs/research/evidence/fi-stage-image.py` |
| L | COM2 occupied by the I.G.O. 8 card reader, I/O 268h by the DS1982 iButton — devices this image knows nothing about | `photoplay.c` profile | interference | every boot | open (P4) — harmless so far |

> No surface has been reclassified yet. When one is, keep the wrong guess visible with a
> strikethrough and a "reclassified Phase N because …" note.

## Phase ledger

| Phase | Deliverable | Outcome |
|---|---|---|
| 0 — bootstrap & plan | `docs/research/fi-00-plan.md` | Done. Image fully mapped; boot chain traced; the dongle gate in `FSYSTEM.EXE` reversed to passwords + expected contents; PeepeeBox confirmed to have **no** Aladdin-HASP responder, so stock PeepeeBox cannot run this image. |
| 1 — bring-up | `docs/research/fi-01-bring-up.md` | Done. Renamed to `HardDisk.img` (byte-identical, no padding needed) → **boots clean to `FSYSTEM.EXE`** and stops on `COPYPROTECTION. NO DONGLE WAS FOUND.` Geometry and truncation both ruled out as blockers. Full HASP-4 transaction captured via `PEEPEEBOX_LPT_TRACE=1`: wake `C6 C7 C6 80`, 15 clocked password bytes, 64-step even read ramp. |
| 5 — merge + rig | `docs/research/fi-05-ppngelo-rig.md` | Done. Fast-forwarded onto `697b52d` (the Elo commit); the FI image now selects the **Elo** touchscreen by default, because its own AUTOEXEC loads `ELODEV 2310,3,9600,3`. Global `COM3_IRQ` hack dropped. `ppngelo\` boots a pristine image end to end. |
| 4 — cabinet I/O | `docs/research/fi-04-cabinet-io.md` | Done. Original hardware inferred as a Pentium II/III-class PCI board. **IRQ-3 theory falsified**; MicroTouch transport instrumented and proven correct (packets delivered, coordinates right, guest reads them) — the app ignores them, so the problem is above the driver. Reset-race and coin-board hypotheses both killed. |
| 3 — the device | `docs/research/fi-03-dongle-device.md` | Done. `src/device/dongle_funny.c` (new) + 50 lines across six files. The image identifies itself from `\GAME\{FSYSTEM.EXE,FUNNY.DLL}`, `pp_apply_ports()` swaps the token, and **the cabinet boots to its German main menu with zero bytes changed inside the image.** |
| 2 — protection wire | `docs/research/fi-02-hasp-wire.md` | Done. **Not a HASP**: Aladdin API and `C6 C7 C6 80` wake in front of plain **Microwire** on PeepeeBox's existing HDONGLE pin map (CS=D1, SK=D5, DI=D6, DO=S5), 9 address bits, `returned(n) = raw[n+8] ^ pass1 ^ n`. All boot gates verified passing offline. |

## Abandoned branches index

| Branch / phase | Why abandoned | Anything still live from it? |
|---|---|---|
| Static reversal of the HASP client blob (`CS:0x1E7A`, file `0x243A`–`0x28A9`) | Aladdin's obfuscated DOS library; scrambled, not worth it | Yes — trace it on the wire in Phase 2 instead |
| Cloning `github.com/PeepeeBox/PeepeeBox` | Repo does not exist | Yes — the real repo is `github.com/Xeon3D/PeepeeBox` |
| Rebuilding PeepeeBox just to get an LPT trace | Not needed — the shipped binary honours `PEEPEEBOX_LPT_TRACE=1` | Yes — that env var is the tracing tool for every later phase |

## Tooling baseline

- **Static:** WSL Ubuntu — `radare2`/`rabin2`, `objdump`, `ndisasm`, `strings`, `file`, `mtools`, `upx`, Ghidra 11.3.2 at `~/ghidra`. No `binwalk`, no `ent`, no `7z`. Windows-side Git Bash has **no** binutils — always use WSL for that.
- **Custom:** `docs/research/evidence/fi-p0-fatwalk.py` — FAT12/16 walker emitting JSON lines. `docs/research/evidence/fi-p2-haspsim.py` — runs `FSYSTEM.EXE`'s protection routine offline under Unicorn (WSL `python3`, unicorn 2.1.4), with `p2-hasp4.py` + `p2-microwire.py` as the candidate dongle and `p2-verify.py` as the one-command check. Seconds per experiment instead of a 50 s emulator boot.
- **Emulation target:** PeepeeBox pins Zida Tomato 4DPS (SiS 496) / iDX4 100 MHz / 16 MB / CL-GD5480 / ESS ES1688 / MicroTouch TouchPen on **COM3** / dongle on LPT1 / `HardDisk.img` as primary IDE master at **63 spt × 16 heads**. All of that is stamped after config parse in `photoplay_apply_profile()`, so `86box.cfg` cannot change it.
- **Build:** verified present — MSYS2 at `C:\msys64` with mingw64 `gcc`, `cmake`, `ninja` and `mingw64/qt5-static`.
- **Tracing:** `PEEPEEBOX_LPT_TRACE=1` on the shipped `PeepeeBox.exe` logs every LPT access with the guest's caller chain. No rebuild required.
- **Screen capture:** `scratchpad/grab.ps1`, and it must run under **Windows PowerShell 5.1** (`powershell.exe`) — PowerShell 7 cannot load `System.Drawing.Common`.
- **From the user on request:** provenance of `Fixed.img` and any untouched original; any h5dmp/hardware dump of this cabinet's HASP (the only route to surfaces D and E); permission to launch the emulator from a session.

---

## What you cannot / must not do here

- No `make`/`npm`/`pytest` on the *image*; the only build is PeepeeBox itself.
- Do not start/stop the live instance from this session unless the user says so.
- Do not modify anything inside the filesystem of the image — that is the whole point.
  Container-level changes (file name, zero-padding the tail back to the declared size) are
  negotiable and must go through a reproducible script with SHA256 in and out.
- Do not copy provenance/README/checksum sidecars into a deploy tree.
- Do not treat "missing tool" as corruption until an intentional lockdown/strip step has
  been ruled out.
