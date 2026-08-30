# Phase 17 — the IGO8 ES "keyless" image, diffed against the original

Two 1,655,635,968-byte images of the same machine:

| | |
|---|---|
| original | `Original/HardDisk.img` |
| keyless | `Keyless/IGO8ESK-VM007.img` |

Identical geometry: one partition, type `06`, LBA 63, 3,096,513 sectors, FAT16
under PTS-DOS 6.0, 32 KB clusters, 189-sector FATs. Mounted and compared file by
file rather than sector by sector, because the sector diff (1,024,923 sectors) is
dominated by allocation noise and says nothing.

## What is *not* there

The keyless image adds **no loader, no TSR, no driver, no replacement `.EXE`**.
Its five extra directory entries are all runtime state a booted machine writes by
itself:

```
/FOTO/HISCORE/AMORE2.HIS   /FQ2_FUN/DATA/SPA/1.BUF
/MENU/BONUS.INI            /MENU/STAT.DAT            /MENU/SYSTEM.INI
```

Nothing like `KEYN.COM` (`docs/research/10`). The protection is defeated entirely
by editing bytes inside files that already existed.

## What differs

223 files. They fall into three groups.

| group | count | what |
|---|---|---|
| executables | 60 | 59 of the 60 games in `\EXE`, plus `\MENU\MENU.EXE` |
| content databases | 160 | `.DAB` / `.DBF` under `FINDIT`, `FND_MORD`, `FQ2_*`, `TR_GE`, `TR_TE` |
| configuration / state | 3 | `FOTO\SETTINGS\GAMES.DBF`, `FN_SYS\DATABASE\USER\MONITOR.TAB`, `SOUND\CHECKMB.BAT` (+ a temp screen dump) |

Every executable keeps its exact original size. The largest is a three-byte edit.

---

## 1. The games: three bytes each

All 60 game executables contain the same protection routine, compiled from the
same object file, so the patch is the same everywhere and only the addresses
move. Taking `\EXE\SOLI.EXE` as the reference:

### Site B — the gate (2 bytes)

```
0x214C7   80 3E 26 26 00    cmp  byte [check_done], 0
0x214CC   74 03             jz   run_the_check          ->  90 90
0x214CE   E9 8C 01          jmp  routine_end
```

`check_done` is a one-byte "already ran" latch in DGROUP, zero at start-up. With
`74 03` turned into two NOPs the `jmp` is unconditional and **the entire body of
the protection routine — 396 bytes — never executes**. No dongle is opened, no
iButton is read, no banner is built, and the six per-title content keys are never
written.

### Site A — the complaint (2 bytes)

6,093 bytes earlier, at the end of the routine that reports failures:

```
0x1FCF5   9A BC 48 00 00    call far strcmp(banner, MAIN.SET["Version"])
0x1FCFD   0B C0             or   ax, ax
0x1FCFF   74 1E             jz   ok                     ->  EB 1E
0x1FD01   68 AC 78          push offset banner
0x1FD04   68 FA 21          push offset "Wrong Version: >%s<"
          ...               sprintf + error screen
```

Because site B left the banner buffer empty, this comparison now always fails.
`74 1E` → `EB 1E` makes the jump unconditional, so the `Wrong Version` error
screen is never reached.

The 6,093-byte distance between the two sites holds in 54 of the 59 patched
games — the same library object linked at the same relative offset. Five link it
elsewhere: `FRAGILE` (4,889), `TRIVIA` (5,851), `SUDOKU` (6,043), and
`MARBLES` (113,080) and `DIAMOND` (125,448), whose reporting code sits in an
earlier segment entirely. The two instructions patched are identical in all of
them, so search for the pattern rather than seeking to an offset.

### The two that are wrong

| file | what |
|---|---|
| `FINDFAST.EXE` | **not patched at all** — byte-identical in both images, yet listed in `MAIN.SET`'s `Games` *and* `Newgames`. Launching it from the menu of the keyless image reaches the untouched site A with an empty banner and reports `Wrong Version: ><`. |
| `FSCHEIN.EXE` | site A was written as `FB 1E`, not `EB 1E` — one bit wrong. `FB` is `STI`, so the error path is entered rather than skipped and the game shows the Photo Play error screen. Harmless in practice: `FSCHEIN` is not in the `Games` list and nothing launches it. |

`PYRAMID.EXE` carries a third edit, `74 09` → `EB 09` at `0x29F17`, suppressing
the `DS1982 FAILED` message. It is redundant — site B already guarantees the
failure bitmask stays zero — and no other game has it.

---

## 2. `MENU.EXE`: ten bytes, and a different strategy

The menu's **dongle gate is left untouched**. `MENU.EXE` still opens the reader,
still reads the iButton, and still fails at both. It gets away with it because
`MENU.EXE` is the one executable that carries none of the `… FAILED` strings, so
there is nothing to print; the check's only visible effect is that the banner
stays empty. The patches sit above it, in the licensing logic.

| offset | original | patched | effect |
|---|---|---|---|
| `0x5C0A` | `CD 21` | `CC 90` | `mov ah,30h / int 21h` in Borland's `_astart` becomes `int3; nop`. `_version` keeps whatever `AX` held at process entry. Not protection — see below. |
| `0xC0D8` | `74 B9` | `90 90` | `les bx,[5A74]` then `cmp [es:bx],1 / +2,2 / +4,3 / +6,4`; on a full match it jumped to `0xC093`, which prints a message and loops forever. NOPed, so the hang is unreachable. |
| `0xC0F9` | `75 04` | `EB 04` | never `call sub_B6DF`, the `INT 60h`-driven service/setup entry |
| `0xC30D` | `75 18` | `EB 18` | always skip `licence_lookup(banner)`; the licence bitmask at `[5AA2]` stays 0 |
| `0xC32D` | `74 05` | `90 90` | `test word [5AA2],4 / jz` → unconditional `mov word [bp-2],1`: **force licence class 1** |
| `0xC3FD` | `75 03` | `90 90` | `cmp word [bp-2],0 / jnz / jmp 0xC598` → always take the `jmp`, skipping the licence splash screen and its wait-for-touch loop |

So the menu is told it holds licence class 1, is stopped from ever computing the
real one from the banner, and is stopped from showing the screen that would
announce it.

`0x5C0A` is the odd one out. It is in the C run-time entry stub, nowhere near the
protection, and replacing a DOS call with a breakpoint buys nothing. The most
economical reading is a debugger breakpoint that was written into the file and
never taken back out — the `21` → `90` alongside it is consistent with someone
patching over the two-byte instruction rather than restoring it.

---

## 3. The content databases: ciphertext replaced by plaintext

160 `.DAB`/`.DBF` files were rewritten. They are dBase III files whose **header is
plain and whose records are encrypted**, and in the keyless image the records are
plain too.

The cipher, recovered by XORing the two versions:

```
seed = per-title key                  # 32-bit
for each record independently:        # keystream restarts every record
    x = seed
    for i in 0 .. record_length-1:
        x = (x * 0x8088405 + 1) mod 2^32
        record[i] ^= (x >> 24) & 0xFF
```

`0x8088405` is the Borland/Delphi LCG, and the game reaches it through
`random(0x100)` with `RandSeed` saved, set to the key, and restored — the same
shape as the `MAIN.SET` cipher in `docs/research/08`, differing only in that the
keystream restarts per *record* rather than per *field*.

The keys are the six dwords the protection routine writes at `banner + 30`:

| dword | value | used by |
|---|---|---|
| 0 | `0x0000038B` | `FQ2_FUN`, `FQ2_ESM2`, `FQ2_ESST` question sets |
| 1 | `0x000181CD` | `TR_GE`, `TR_TE` |
| 2 | `0x0001D760` | `FINDIT`, `FND_MORD` |
| 3 | `0x00029B92` | — (no file in this image uses it) |
| 4 | `0x0001287E` | — |
| 5 | `0x0000089D` | — |

**Why plaintext files work in the patched build.** The DBF layer branches on the
key before reading:

```
cmp dword [dbf_obj+0x10], 0
jnz  read_encrypted          ; fread + LCG
     read_plain              ; bare fread
```

Site B stops the key table from ever being filled, so every key reads back as
zero and the reader takes the plain path. That is the whole reason the data files
had to be rewritten: not because the patch broke the cipher, but because it
switched the cipher off.

**Correction to `docs/research/14`.** In this generation the six dwords are
*compile-time constants inside each executable*, byte-identical across all 61 of
them — not values carried in the dongle record. What the 2008 dongle supplies is
the banner and nothing else. The 1999/2000 finding stands for those generations;
it does not carry forward to 2008.

---

## 4. The three configuration files

| file | change |
|---|---|
| `FOTO\SETTINGS\GAMES.DBF` | 81 → 82 records (one game added), a per-game flag at record offset 34 flipped `'0'` → `'1'` in the first seven records, trailing flags `FFTTT` → `FFFFF` |
| `FN_SYS\DATABASE\USER\MONITOR.TAB` | 17 bytes: a per-entry flag `1` → `0` at a 26-byte stride — funworld's network monitoring table, unrelated to the dongle |
| `SOUND\CHECKMB.BAT` | `set MB=6WEV` → `set MB=` — the motherboard ID a hardware probe had written |

`MENU\TMP\BACK_STP.SAV` also differs; it is a saved screen buffer, i.e. runtime
debris from whichever image was booted last.

## 5. What this tells us about the original

The patch is a clean negative image of the protection. Everything it removes is
something an emulated dongle has to supply:

1. a card reader on COM2 that answers the five-step handshake,
2. a licence record whose banner equals `MAIN.SET["Version"]` — for this image,
   `Version 2008 (ES)`,
3. a DS1982 iButton on `0x268` holding `Photo Play 2000 Version 3`.

Supply those three and the original image needs no patching at all, and — unlike
the keyless build — keeps its encrypted content databases, `FINDFAST` and
`FSCHEIN` included.

`docs/research/18` is that protocol, and `igo8_dongle.py` is a working model of
it.
