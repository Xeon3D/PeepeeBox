# Phase 0 — Bootstrap & plan

**System:** *Funny's Interactive Playworld* (German territory build), a funworld-adjacent
touchscreen arcade kiosk by **OrgaControl Systemhaus** for **Funny's Planet International
GmbH**, ~2001–2003. Not a Photo Play, but the same family: same `FN_*` network suite, same
`UPDATE\CHECK.BAT` that hunts for `pp2000.bat` on removable media, same cabinet class.

**Goal:** boot and run this disk image on PeepeeBox **with no bytes changed inside the
filesystem** — no patched executables, no software bypass. The same standard PeepeeBox holds
itself to for Photo Play.

Ground truth frozen at `docs/research/evidence/fi/hashes.txt`.
`Fixed.img` sha256 `776801cd0a257527b7130622b7b77714bd1f8f56feed389505b54691ba01ef06`,
2 038 063 104 bytes, mtime 2026-09-03 — i.e. **written today; provenance unconfirmed**
(see *What the user must supply*).

---

## 0.1 System identity

| | |
|---|---|
| Boot chain | PTS-DOS MBR (`(C) 1995-97 PhysTechSoft ltd`) → DR-DOS 7.03 VBR (OEM `DRDOS  7`) → `IBMBIO.COM` / `IBMDOS.COM` → `CONFIG.SYS` → `COMMAND.COM` → `AUTOEXEC.BAT` |
| `CONFIG.SYS` | `INSTALL=C:\TOOLS\SCREEN.EXE C:\TOOLS\BOOT.LGO` (splash), `HIMEM.SYS`, `DOS=HIGH,UMB`, `COUNTRY=49` |
| `AUTOEXEC.BAT` | `KEYB GR+` → `C:\TOUCH\ELO\ELODEV 2310,3,9600,3 -C7,4074,4062,199,1,255` → four sound-card init attempts (OPTi 930 / ESS 688 / ESS 1868 / C-Media PCI) → `CD C:\GAME` → `:RESTART` / `FSYSTEM.EXE C3 I3` / `GOTO RESTART` |
| Launcher | `GAME\FSYSTEM.EXE` — 60 KB Borland Pascal 7 real-mode program: touchscreen probe, coin-board probe, **dongle check**, installs an `iret` service handler, then runs `funny.dll` |
| The actual app | `GAME\FUNNY.DLL` — 280 KB **WDOSX 0.96** 32-bit protected-mode executable (Michael Tippach), compressed end to end (entropy 7.87 from byte 0). Despite the extension it is the game engine, and every game lives inside it |
| Content | `GAME\{BALLOON,DETECTIV,FUNBLOCK,FUNFIND,IMG,JIGSAW,MEMORY,MOSAIC,MUSIC,PAINTING,SCORE,SOUND,VIDEO,VOICE}` — 509 BMP, 106 AVI, 2 XM. `GAME\VIDEO` alone is 595 MB |
| Third-party (out of scope) | `DRDOS\` (Caldera DR-DOS 7.03), `TOUCH\ELO` + `TOUCH\_ELO` (Elo ELODEV 1.7a), `TOUCH\MT` (3M MicroTouch), `SOUND\*` (ESS / OPTi / C-Media DOS init tools), `FN_SYS\DFU\PPP*` (PPP stack), PKZIP |
| Storage / posture | FAT16, offline. `FN_MAIL` / `FN_MST` / `FN_NEWS` / `FN_PORT` / `FN_SYS` are the funworld online-portal suite (Oct 2000) — **not started by AUTOEXEC**, dormant |

Last time this software ran on real hardware: `SOUND\CHECKMB.BAT`, which `CHECKMB.EXE`
regenerates at every boot, is dated **2018-03-16 12:26:52** and contains `set MB=UNKN`.
`SOUND\MOBO.CSV` maps BIOS IDs to board names and includes `2A4IBL13 → TOM2` — a SiS 496
486 board, i.e. the Zida Tomato family PeepeeBox emulates.

## 0.2 Image structure and history

Partition table: one entry, type `06` (FAT16), boot flag `80`, start LBA 63,
**4 120 641 sectors**, end CHS cyl 1021 / head 63 / sector 63 → geometry 63 spt × 64 heads.
BPB: 512 B/sector, **32 KB clusters**, 2 FATs × 252 sectors, 512 root entries,
**63 spt / 64 heads**, 63 hidden, total 4 120 601 sectors, `NO LABEL`, OEM `DRDOS  7`.

* The filesystem is **clean**: 0 cross-linked clusters, 0 chain/size mismatches, both FAT
  copies byte-identical, no allocated cluster beyond end-of-file.
* 26 305 of 64 376 clusters in use; 831 MB of live data in 1 151 files / 236 directories.
* **The image file is 68.4 MB short of what its own filesystem declares.** It needs
  4 120 664 sectors (2 109 800 448 B); it has 3 980 592. Every missing sector is free
  space — nothing is lost — but it is a real inconsistency.
* `funny_interactive_de/` is a **faithful extraction**: 1 151 files, zero size mismatches,
  nothing missing in either direction. The only extras are two Windows artifacts
  (`RECYCLED\DESKTOP.INI`, `System Volume Information\IndexerVolumeGuid`) created by
  mounting the image read-write on Windows.

Read as raw directory entries, the root tells the disk's life story. 253 deleted entries
survive:

1. **2001-01-11 12:41–12:49** — a full funworld *quiz-cabinet* install is ghosted on:
   ~50 root-level game directories (`MENU`, `FUNQUIZ`, `IQ_MIX`, `IQ_SPOR`, `QP_MUS`,
   `QP_GEO`, `QP_BUL`, `QP_MSTR`, `NETGAME1..5`, `NETQUIZ1..5`, `HANGMAN`, `PYRAMID`,
   `SHANGHAI`, `SOLI`, `FREECELL`, `TOWERS`, `TRIVIA`, `GUINNESS`, `PTSDOS`, …) plus
   `FMEMO`, `FN_*`, `SOUND`, `UPDATE`.
2. **2001-01-27 17:22** — `GHOST.ERR`: a Ghost 204 clone of a 1.48 GB source onto a 528 MB
   target dies with `RawIO: Cylinder > 1023`. Historic; unrelated to the current contents.
3. **2001-10-01 → 2003-01-01** — DR-DOS 7.03 installed; `CONFIG.SYS`, `FSYSTEM.EXE`,
   `FUNNY.LGO`, `BOOT.LGO`, Elo drivers; `AUTOEXEC.BAT` last written 2003-01-01.
4. **2003-05-31** — `LASTGAME.LOG`: `Lastgame: QP_MUS (language: GER)` — the *old* game set
   was still the live one at that point.
5. **2013-04-01, 2018-03-16** — in-service writes (`FMEMO\STATS.DAT`, `SOUND\CHECKMB.BAT`).
6. **2024-02-20 11:21** — every directory under `GAME\`, plus `SOUND\PCIAUD`,
   `SOUND\SOUND16`, `TOUCH\_ELO`, `TOUCH\MT`, gets a fresh directory-entry timestamp while
   the files inside keep their 2001–2002 dates: a timestamp-preserving **bulk file copy**.
7. **2024-02-20 22:53** — Windows creates `System Volume Information` (now a deleted entry
   at cluster 40279).
8. **2024-02-20 22:57–23:08** — `TOUCH`, `DRDOS`, `GAME`, `TOOLS` re-stamped; DR-DOS setup
   files (`SETUP2.EXE`, `SETUP.INI`, `FORMAT.COM`, `LICENSE.TXT`, `TASKMGR.INI`,
   `DRDOS.INI`) copied to the root; `IBMBIO.COM` / `IBMDOS.COM` / `COMMAND.COM` re-SYSed
   into root slots 0–2.

**So this is not a pristine cabinet dump.** It is a 2001 quiz-cabinet disk that was gutted
and reloaded with the *Funny's Interactive Playworld* distribution in February 2024, under
Windows, file by file. Two consequences: any physical-layout-keyed protection would already
be dead (there is no evidence of one here), and the ~50 deleted 2001 game directories are
recoverable evidence rather than part of the deliverable.

## 0.3 Anti-analysis posture

* `FSYSTEM.EXE` — plain Borland Pascal, **fully static-analysable**; done, see 0.4.
* The **HASP client library inside it** — entry at `CS:0x1E7A`, roughly file `0x243A`–`0x28A9`
  with a `0x55` filler tail — is Aladdin's obfuscated DOS library: a `jmp` into a scrambled
  blob. Not worth static reversal; trace it on the wire instead.
* `FUNNY.DLL` — WDOSX-compressed end to end. **Static-only is not viable for the 32-bit
  half**; it has to be dumped from memory after the stub decompresses.
* Encrypted data at rest, all 7.91–7.96 bits/byte: `GAME\FUNNY.DAT` (162 KB),
  `GAME\FUNNY.AR` / `.DE` / `.EN` (24 960 B each), `GAME\FUNNY.MUL`, and all 29
  `FMEMO\RESOURCE\TEXT.*`. `FUNNY.DE` is `AB` followed by the first 31 bytes of `FUNNY.EN`
  and then diverges — a shared header, so these are structured/compressed rather than a
  naive repeating XOR. Graphics and video are **plain** BMP/AVI, unlike Photo Play 2001–2003.
* No packer, anti-debug, anti-VM or self-integrity markers in the real-mode binaries.

## 0.4 The protection, as recovered

`FSYSTEM.EXE` calls a genuine **Aladdin HASP** DOS library — the `HASPDOSDRV` device name is
in the binary, and the call sites use the classic
`hasp(service, seed, lptnum, pass1, pass2, &p1, &p2, &p3, &p4)` Pascal binding. Three
wrappers and one gate routine:

| Site (CS off) | Service | What it does |
|---|---|---|
| `0x02F3` | 1 | IsHasp. First out-param `== 0` → `COPYPROTECTION. NO DONGLE WAS FOUND.` |
| `0x03D8` | 6 | args 0, 0, LPT 0. Third out-param `!= 0` → `COPYPROTECTION. CAN'T GET DONGLE ID.` |
| `0x045C` | 3 | loop `i = 1..6`: read word `i-1`, compare. Mismatch → `COPYPROTECTION. INVALID SYSTEM CODE (i).` |
| `0x0188` | 3 | generic read-word wrapper — address in, value out, status returned |
| `0x01D1` | 4 | generic write-word wrapper |

**The passwords are obfuscated as ×1024 longints and divided down at run time:**

```
CS:0x02F7  mov word [DS:0x204], 0xD400     ; longint 0x010ED400
CS:0x02FD  mov word [DS:0x206], 0x010E
CS:0x0303  mov word [DS:0x208], 0x2800     ; longint 0x01652800
CS:0x0309  mov word [DS:0x20A], 0x0165
           ... both divided by 1024 via Borland LongDiv at 0xD32:0x0D18
```

    pass1 = 0x010ED400 / 1024 = 0x43B5   (17333)
    pass2 = 0x01652800 / 1024 = 0x594A   (22858)

**The expected memory contents** are built on the stack at `CS:0x030F` as six words
`4F52 4741 434F 4E54 524F 4C20`, i.e. dongle words 0–5 must read as the ASCII
**`ORGACONTROL `** (byte-swapped per word). Any mismatch prints `INVALID SYSTEM CODE (n)`
with the 1-based index.

Every failure path ends in a `jmp` back onto a `Sound(800) / Delay(500) / NoSound /
Delay(300)` loop — **no dongle means an infinite beep, not a graceful exit.**

`FSYSTEM.EXE` then installs an `iret` handler (vector number held at `DS:0x24E`; the
installer at `CS:0xC805` writes `0x0250`, so **INT 50h** is the strong candidate) and
`FUNNY.DLL` reaches the dongle only through it. Handler frame: `AX` = function, `BX` =
argument/result.

| AX | Behaviour |
|---|---|
| `0x0011` | read dongle word `BX+13`, increment, write it back — **persistent counters** |
| `0x0020` | print a string |
| `0x1234` | return the six system-code words in `AX BX CX DX SI DI` |
| `0x1235` | return **dongle word 9** in `BX` — an unknown value, and the prime suspect for the content key of the encrypted `FUNNY.*` files |
| `0x00FF` | return 1 if a DS flag byte is set |

`GAME\FSYSTEM.EXE` is the **only** file on the disk that mentions a dongle at all; every
other reference is sealed inside the compressed `FUNNY.DLL`.

## 0.5 Protection / lock surfaces, and what the cheap scan cannot see

| # | Surface | Where | Type | Cadence | Status |
|---|---|---|---|---|---|
| A | Aladdin HASP `IsHasp` | `FSYSTEM.EXE` `CS:0x02F3`, svc 1 | bool gate | every boot | **open** — PeepeeBox has no HASP4 |
| B | HASP service 6 status | `CS:0x03D8` | bool gate | every boot | **open** |
| C | HASP words 0–5 = `ORGACONTROL ` | `CS:0x045C`, svc 3 | value-bearing, **known** | every boot | **specified** |
| D | HASP word 9 (INT 50h `AX=1235`) | `CS:0x0CBA` | value-bearing, **unknown** | on demand | **open** |
| E | HASP words 13+ counters (`AX=0011`) | `CS:0x0BE2` | writable state | runtime | **open** — needs write plus read-back |
| F | `FUNNY.*` / `TEXT.*` file cipher | data at rest | value-bearing | runtime | **open** — key source unknown, D suspected |
| G | Whatever `FUNNY.DLL` checks on its own | compressed 32-bit half | unknown | unknown | **not yet visible** |
| H | Touchscreen probe (Elo, then MicroTouch) | `FSYSTEM.EXE` `CS:0x11BB` | availability gate | every boot | **open** |
| I | `CoinControl (6chanel)` I/O board probe | `FSYSTEM.EXE` `CS:0x15E7` | availability gate | every boot | **open** |
| J | Disk geometry: BPB 63/64 vs PeepeeBox's fixed 63/16 | VBR + `photoplay.c` | correctness | every boot | **needs proof** |
| K | Image truncated 68.4 MB below its own BPB | partition + BPB | correctness | on write | **open** |

Blind-spot checklist, with owning phases:

| Blind spot | Owner |
|---|---|
| linked-but-dead vs live code | P5 — `FUNNY.DLL` after the memory dump |
| runs-once vs every-boot vs runtime-revalidated | P5 — INT 50h call-site cadence inside `FUNNY.DLL` |
| conditionally-dispatched paths (coin board, portal apps, `UPDATE\CHECK.BAT` scanning A:/D:/E:/R:) | P4 |
| code/data from network or removable media | P4 — `FN_SYS\DFU` PPP stack, `CHECKUPD.EXE` |
| packed / encrypted regions | P5 (WDOSX), P5 (`FUNNY.*` cipher) |
| anti-debug / anti-VM / self-check | P5 — nothing in real mode, unverified in the 32-bit half |
| time / counter / hardware-state bombs | P3 — surface E writes counters back to the dongle |

## 0.6 Plan

Easiest → hardest. **New surfaces found during execution are expected and get appended
here rather than treated as a process violation.**

| Phase | The one question | Deliverable |
|---|---|---|
| **1** | Does it even reach the dongle? Fix the *container*, not the contents — file name, geometry, truncation — then run it and read the log and the screen. | `docs/research/fi-01-bring-up.md` + `docs/research/evidence/fi-stage-image.py` (append-only, SHA256 in/out) |
| **2** | What does the real HASP exchange look like on the wire? Instrument LPT in a local PeepeeBox build and capture services 1, 3, 4, 6 in full. | `docs/research/fi-02-hasp-wire.md` + capture logs |
| **3** | Implement HASP (4 or 3, whichever the wire says) as a new release row in `dongle_photoplay.c`: passwords `43B5`/`594A`, words 0–5 = `ORGACONTROL `, writable words 6+; and teach `photoplay_ident.c` to recognise this image instead of failing over to "no token". | `docs/research/fi-03-hasp-device.md` + patch |
| **4** | Bring up the rest of the cabinet: MicroTouch-on-COM3 versus the Elo the image expects, the `ELODEV` failure path, COM3's IRQ, the `CoinControl` probe, sound. | `docs/research/fi-04-cabinet-io.md` |
| **5** | Dump `FUNNY.DLL` from memory once WDOSX has unpacked it; enumerate every INT 50h call site; determine what word 9 is for and how `FUNNY.DAT/.AR/.DE/.EN/.MUL` and `TEXT.*` are keyed. | `docs/research/fi-05-funny-dll.md` |
| **6** | Carve the ~50 deleted 2001 game directories out of the FAT — evidence, not deliverable, and only if wanted. | `docs/research/fi-06-carve.md` |

## What the user must supply

1. **Provenance of `Fixed.img`.** It was written today. Is there an untouched original, and
   was the 68.4 MB truncation deliberate or an artifact of whatever produced it? Working
   from an already-modified image while claiming "no patch" is not defensible.
2. **Any hardware dump of this cabinet's HASP** (h5dmp or equivalent). Surfaces D and E —
   word 9 and the counters — are values that cannot be derived from the disk alone; without
   a dump they have to be guessed or brute-forced against the encrypted files.
3. Confirmation that launching PeepeeBox from this session is wanted. Phase 1 needs it.

## Falsification log

* *"This is a Photo Play image."* — never assumed, and confirmed **false**. It is a
  different funworld-family product: `FSYSTEM.EXE` + `FUNNY.DLL`, with no `MENU\`, no
  `FOTO\SETTINGS\MAIN.SET` and no `NSB.NR`. PeepeeBox's `photoplay_identify()` therefore
  returns 0. That is *harmless* to disk attachment — `pp_apply_disk()` mounts the image
  regardless and only leaves the window title blank — but fatal to the dongle, which picks
  which token to present from the identified banner and, with no match, presents **none**.
* *"PeepeeBox's dongle is called a HASP, so it might just answer."* — **false**.
  `dongle_photoplay.c` opens with "NOT a HASP"; its parallel part is a Microwire
  93C46-shaped EEPROM plus funworld's own two-chip and CDONGLE protocols. There is no
  Aladdin HASP responder anywhere in the build that the profile attaches — `hasp.c` is
  upstream 86Box's Savage Quest stub and is never wired up.
* *"PeepeeBox forces 63/16 while the BPB says 63/64, so it cannot boot."* — **probably
  false, and cheap to rule out.** The Award BIOS on the 4DPS applies LBA-assist
  translation, which for a ~2 GB disk halves cylinders and doubles heads until cyl ≤ 1024 —
  landing on exactly 63 spt / 64 heads, which is what the BPB wants. And everything the VBR
  itself touches lives in cylinder 0, where the two interpretations coincide regardless.
  **Phase 1 must confirm this by observation, not by this paragraph.**
