# Phase 1 — The Photo Play / I.G.O. dongle protection

Canonical deliverable. Everything below is derived from the 20 supplied disk images;
raw evidence lives under `analysis/p0/` and `analysis/p1/` and is grepped, not read.

## 1. System identity

| | |
|---|---|
| OS | **PTS-DOS 6.0** (`PTSBIO.SYS`/`PTSDOS.SYS`, OEM string `PTSDOS60`), FAT16, volume `PHOTOPLAY` |
| Disk | MBR, one type-0x06 partition at LBA 63, 3 096 513 sectors, 32 KB clusters |
| Boot chain | `CONFIG.PTS` → `\PTSDOS\COMMAND.COM /p \AUTOPTS.BAT` → `\UPDATE\CHECK.BAT`, `\TOUCH\TCH_INIT.BAT`, `\SOUND\SND_INIT.BAT` → `\MENU\MAIN.COM` → `MENU.EXE` → `\EXE\<game>.EXE` |
| Toolchain | Borland C++ 16-bit, large model (`push cs` + near `call` emulating far calls, `enter`/`leave`, `9A` far calls fixed up by the MZ relocation table) |
| Protection HW | **Aladdin HASP** parallel-port dongle. A Dallas **DS1982** iButton is a secondary, alternative token (`DS1982 FAILED` string). |
| Dongle identity | `\MENU\NSB.NR` holds the site serial (e.g. `ND003`), matching the image name |

## 2. What the dongle actually provides

`check_dongle()` reads a **62-byte block** out of the HASP into a fixed buffer in DGROUP.
Its layout, recovered from the KEYN TSR that emulates it:

```
+0x00  30 bytes  "Version 2006 (AT)" , NUL-padded      <- version/territory banner
+0x1E  32 bytes  eight little-endian dwords            <- opaque; identical in every
                 0000038B 000181CD 0001D760 00029B92      known TSR, i.e. not per-site
                 0001287E 0000089D 000273A6 FDDEBF3E
```

Two independent tests are then run on that block:

1. **Version test** — `strstr(buf, "Version 2006")`. Failure sets error bit `0x400`
   (`NDONGLE FAILED`).
2. **Crypto test** — the block is fed through four routines (init → key `0x00030268` →
   verify → extract) and the extracted plaintext is compared, byte by byte, against an
   obfuscated constant stored XOR-0x7C in the data segment. For IGO6 that constant
   decodes to `Photo Play 2000 Version 3`. A mismatch sets error bit `0x1`.
   The compare loop also does `xor byte [buf+i], 0xFF` on the fly — a deliberate
   anti-tamper booby trap that corrupts the buffer as it walks it.

The result is a bitmask, cached in two DGROUP bytes: `g_checked` (has the check run) and
`g_errcode` (the low byte of the mask). Bits seen in the wild:

| bit | message |
|---|---|
| 0x001 | `DS1982 FAILED` / generic |
| 0x010 | `HDONGLE FAILED` |
| 0x080 | `KDONGLE FAILED` |
| 0x100 | `MDONGLE FAILED` |
| 0x200 | `MBDONGLE FAILED` |
| 0x400 | `NDONGLE FAILED` |

## 3. The three code shapes

### 3.1 `check_dongle()` — present once in every protected game EXE

```asm
    enter  0x294,0                  ; ~660-byte frame
    push   si
    mov    word [bp-2], 0           ; result = 0            <-- key: zeroed FIRST
    cmp    byte [g_checked], 0
    jz     do_check                 ; <-- THE PATCH POINT
    jmp    epilogue                 ; already checked -> return 0
do_check:
    push   OFFSET dongle_buf
    push   cs
    call   read_hasp                ; <-- the KEYN patch point
    pop    cx
    ...  strstr / decrypt / compare, OR-ing error bits into [bp-2] ...
    mov    byte [g_checked], 1
    mov    al, [bp-2]
    mov    [g_errcode], al
epilogue:
    mov    ax, [bp-2]
    pop    si
    leave
    retf
```

Because `[bp-2]` is zeroed *before* the cached-flag test, **turning that `jz` into two
NOPs makes the function an unconditional `return 0`** and leaves `g_errcode` at its
initialised value of 0. Three encodings of the same idea exist:

| variant | bytes at the branch | fall-through |
|---|---|---|
| **A** | `74 03` / `E9 rel16` | `jmp` to `mov ax,[bp-2] … leave / retf` |
| **B** | `74 06` / `33 C0 5F 5E C9 CB` | inline `xor ax,ax; leave; retf` |
| **C** | `74 06` / `8B 46 FE E9 rel16` | `mov ax,[bp-2]` then `jmp` to the teardown |

All three were verified mechanically for every executable in every image: the jump target
is always a genuine return-0 epilogue (`analysis/p1/sh/` + `scripts/classify.py`).

### 3.2 The `Wrong Version` report — a second, independent consumer

Elsewhere in each game:

```asm
    push   OFFSET dongle_buf
    push   OFFSET "Version"
    push   OFFSET scratch
    call   far strtok
    add    sp,4
    push   ax
    call   far atoi
    add    sp,4
    or     ax,ax
    jz     skip                     ; <-- THE PATCH POINT (74 -> EB)
    push   OFFSET dongle_buf
    push   OFFSET "Wrong Version: >%s<"
    lea    ax,[bp-0x1D0]
    push   ax
    call   far sprintf
    add    sp,6
    lea    ax,[bp-0x1D0]
    push   ax
    call   far Error                ; aborts to the error screen
skip:
```

This block re-reads the buffer directly, so neutralising `check_dongle` alone is not
enough — with an empty buffer it fires. The `jz` displacement (0x1E, or 0x63 on the
1999 AT build) covers exactly the report, so `74 → EB` skips it cleanly.

### 3.3 `MENU.EXE` — the error screen

`MENU.EXE` maps the same error bitmask onto an error *number* 1..N via a chain of
`test`/`mov word [bp-2],k` pairs, then:

```asm
    mov    word [bp-2], N           ; N = number of dongle error codes (3..0x14)
    cmp    word [bp-2], 0
    jnz    show_error_screen        ; <-- THE PATCH POINT (75 03 -> 90 90)
    jmp    ok
```

NOPing the `jnz` always takes the `ok` jump. **N is the discriminator**: the identical
12-byte shape occurs three times in every `MENU.EXE`, but the two decoys always carry
`N == 1` and the dongle site never does. N grows with the release (1999→3 … IGO8→0x14),
which is why a fixed-byte signature only ever worked on one generation at a time.

## 4. `KEYN.COM` — the historical TSR emulator

114 bytes. Installs an `INT 2Bh` handler and terminates resident:

```asm
0100  jmp  0x15C
0102  <62-byte dongle image: banner + 8 dwords>
0140  push cx / push ds / push es
      push ds / pop es              ; ES = caller's DS
      push cs / pop ds              ; DS = TSR
      mov  di,sp / add di,0x0E
      mov  di,ss:[di]               ; DI = the buffer offset the caller pushed
      lea  si,[0x102]
      mov  cx,0x3E                  ; 62 bytes
      rep  movsb                    ; hand the block to the caller
      pop es / pop ds / pop cx / iret
015C  mov ah,25h / al,2Bh / dx,140h / int 21h    ; hook INT 2Bh
      lea dx,[015C] / shr dx,4 / inc dx
      mov ah,31h / int 21h                        ; TSR
```

The game-side patch replaces `call read_hasp` (`E8 rel16`) with `INT 2Bh` + `NOP`
(`CD 2B 90`) — same 3 bytes, and the pushed buffer offset is already on the stack where
the handler expects it. The three supplied TSRs differ **only** in the banner string;
`scripts/ppkeyless.py --mode keyn` rebuilds all three byte-for-byte.

Note what this does and does not achieve: the banner makes the *version* test pass, so
no `NDONGLE FAILED` and no `Wrong Version`. The crypto test still fails, which is why the
KEYN builds always carry a second patch — `or ax,1` → `or ax,0` on the pre-IGO4 layout,
or `Error()` → `retf` on IGO4+.

## 5. `Error()` → `retf` — what that patch really is

The IGO4+ KEYN builds stub the function at the `00 59 C9 CB | 55 8B EC 6A 00 6A`
signature. It is **not** a protection routine: it is the program-wide
`Error(char *msg)` reporter — ~150 call sites in a single game, writing
`\logging.out` and `\fn_sys\dfu\trans\ERROR.LOG` and painting the error screen.
Stubbing it silences *every* error in the program, dongle-related or not. It works, but
it is the bluntest of the available options and it will hide genuine faults.

## 6. Recommended patch set

`--mode menu` (the default) applies, per protected game EXE:

1. `check_dongle` cached-flag `jz` → `90 90` — provably `return 0`
2. `wrong_version` `jz` → `EB` — skips exactly the report block

and, once, to `MENU.EXE`:

3. dongle-error `jnz` → `90 90`

No TSR, no added files, no suppression of unrelated error reporting. `g_errcode` stays 0,
so none of the `?DONGLE FAILED` reporters fire either.

## 7. Verification

`analysis/p1/sh/validate.py` re-derives every patch from the *original* executables and
compares against the user's known-good keyless builds.

**184 files reproduced byte-for-byte**, including all 45 IGO6 and all 51 IGO7 files.
Every remaining difference is accounted for:

| case | finding |
|---|---|
| 5 × 2001IT (`AMORE`,`CONCENT`,`FINDIT`,`FMEMO`,`FUNQUIZ`) | reference build also zeroes a `mov byte [X],1`. Cross-referencing shows `X` is a scoped UI flag saved/restored around a text-drawing block, with **no reference to either dongle global**. Unrelated to the protection; not replicated. |
| `IGO8/MENU.EXE` (8 bytes) | reference build carries 5 extra edits that are hardware-wait / DOS-version patches, not dongle patches — see `Docs/02-86box-surface.md` |
| `IGO8/FINDFAST.EXE` | **the reference build left this game unpatched.** Ours patches it. |
| `IGO8/FSCHEIN.EXE` | reference build wrote `FB` (`sti`) where `EB` (`jmp`) was intended — a typo that leaves the error path live and pushes `ds` without popping it. Ours writes `EB`. |
| `IGO8/PYRAMID.EXE` | reference build makes one extra `74 → EB` on a `test g_errcode,1` reporter. Harmless but redundant once `check_dongle` returns 0. |
| 42 × IGO4 game EXEs | the `-K` image ships a *different, larger build* of every game; not byte-comparable. Only `MENU.EXE` is, and it matches exactly. |

Independent structural cross-check, all **422** protected executables in all 20 images:
the buffer offset printed by the `wrong_version` block is always the same offset
`check_dongle` hands to the HASP reader. This is now an assertion inside the patcher.

### Runtime confirmation (2026-08-27)

`Menu Patched Versions/IGO8ES-VM007.img` — the **original, dongle-locked** image — patched
with `--mode menu` and booted in `86boxv2` (86Box 5950, machine `4dps`, MicroTouch on COM3):

- 61/61 files patched, 60/60 games (the reference `-K` build patches 59).
- Boots, runs touchscreen calibration (`\MENU\TOUCH.INI` created), writes
  `\MENU\SYSTEM.INI`, and **reaches the Photo Play menu**.
- No `\logging.out`, no `\FN_SYS\DFU\TRANS\ERROR.LOG`, no error `.PCX` in the root.
- Achieved **without** `KEYN.COM`, without the `Error() → retf` stub, and **without the
  five extra `MENU.EXE` edits** the reference `-K` build carries — confirming those are
  not required for the dongle bypass (see `Docs/02`).

This is the same generation, same machine config, and same game build as the user's
known-good `-K` image, so it is a controlled head-to-head, not just "it booted".

## 8. Coverage over the supplied images

| family | protected EXEs | prologue variant |
|---|---|---|
| 1999 AT | 22 | B ×10, C ×12 |
| 1999 NL / PP1999 PT | 23 | A ×3, B ×9, C ×11 |
| 2000 NL | 29 | B ×15, C ×14 |
| 2001 IT | 36 | A |
| PP1998 SP | 34 (+2 unprotected) | A |
| IGO2 PT | 31 | A |
| IGO3 SE | 8 (+28 unprotected) | A |
| IGO4 AT | 42 | A |
| IGO5 IT / PT | 43 | A |
| IGO6 AT | 44 | A |
| IGO7 AT | 50 | A |
| IGO8 ES | 60 | A |

Every protected executable matches exactly one prologue, and every `MENU.EXE` matches
exactly one dongle site. There are no unhandled files.
