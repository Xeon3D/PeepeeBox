# KEYN.COM, in full

`\MENU\KEYN.COM` is the 114-byte TSR at the heart of every `-K` ("keyless") build in this
collection, and of `ppkeyless.py --mode keyn`. This document explains all 114 bytes, every
edit that goes with it, and what it does and does not achieve.

Everything below was read off the shipped artefacts:
`analysis/p0/ex/{2001IT-A3735-K,IGO6AT-ND003-K,IGO7AT-MK002-K}/MENU/KEYN.COM`.

---

## 1. The idea in one paragraph

Each protected executable calls a routine that reads a **62-byte block** out of the HASP
dongle into a buffer, then checks that block for a version banner. KEYN replaces that
single call with `INT 2Bh` and installs a resident handler that copies the same 62 bytes
out of itself. The program's own check then passes on data that never touched hardware.
No protection logic is removed — the check still runs, and still succeeds or fails on the
contents of the buffer.

## 2. Where it sits in the boot chain

`ppkeyless.py` adds one line to `AUTOPTS.BAT`:

```
cd\menu
keyn.com          <- added: installs the TSR
main.com          <- the front end, which chains MENU.EXE and the games
```

It must run **before** anything that checks the dongle, and it stays resident for the
lifetime of the machine, so every game inherits the hooked vector.

## 3. File layout — 114 bytes (0x72)

| Range | Size | Contents |
|---|---|---|
| `0x00` | 2 | `EB 5A` — `jmp short 0x5C`, over the data, to the installer |
| `0x02`–`0x1F` | 30 | Version banner, NUL-padded |
| `0x20`–`0x3F` | 32 | Eight little-endian dwords |
| `0x40`–`0x5B` | 28 | The `INT 2Bh` handler (resident) |
| `0x5C`–`0x71` | 22 | The installer (runs once, then discarded) |

The banner and the dwords together are `0x02`–`0x3F` = **62 bytes**, which is exactly what
the handler copies.

### What varies between builds

Comparing the three shipped TSRs:

```
banner  0x02..0x1F   differs      'Version 2001 (IT)' / 'Version 2006 (AT)' / 'Version 2007 (AT)'
dwords  0x20..0x3F   IDENTICAL
handler 0x40..0x5B   IDENTICAL
install 0x5C..0x71   IDENTICAL
```

**Only the banner changes.** The code and the eight dwords are byte-identical across
generations, which is why `build_keyn(version_string)` can reproduce any of them exactly.

### The eight dwords

```
0000038B  000181CD  0001D760  00029B92  0001287E  0000089D  000273A6  FDDEBF3E
```

Copied verbatim from the original TSRs. Their meaning is **not known**. They are almost
certainly per-site counters or licence values from a real dongle, captured once and reused
everywhere. No game observed so far rejects them, so whatever they mean, these values are
acceptable to every title in the collection.

---

## 4. The resident handler, annotated

Loaded at `0x100` like any `.COM`, so file offset `0x40` is runtime address `0x140`.

```asm
0140  51            push cx
0141  1E            push ds
0142  06            push es
0143  1E            push ds
0144  07            pop  es          ; ES = the CALLER's DS (still unchanged at this point)
0145  0E            push cs
0146  1F            pop  ds          ; DS = KEYN's own segment
0147  8B FC         mov  di,sp
0149  83 C7 0E      add  di,0x0E     ; -> the caller's pushed buffer pointer
014C  36 8B 3D      mov  di,[ss:di]  ; DI = buffer offset
014F  8D 36 02 01   lea  si,[0x102]  ; SI = the 62-byte block (0x100 + 2)
0153  B9 3E 00      mov  cx,0x3E     ; 62 bytes
0156  F3 A4         rep  movsb       ; DS:SI -> ES:DI
0158  07            pop  es
0159  1F            pop  ds
015A  59            pop  cx
015B  CF            iret
```

Two details are worth dwelling on.

**The `push ds / pop es` ordering.** `ES` is loaded from `DS` *before* `push cs / pop ds`
changes it, so `ES` ends up as the caller's data segment while `DS` becomes KEYN's. That
makes `rep movsb` copy from KEYN's block into the caller's buffer in one instruction. It
relies on the buffer being a DGROUP global in the caller's data segment — which it is, in
every Borland large-model build here.

**Why `+0x0E`.** The patched call site is `push <buf>; push cs; int 2Bh`, so at handler
entry the stack reads:

```
SP+0  IP        pushed by INT
SP+2  CS        pushed by INT
SP+4  FLAGS     pushed by INT
SP+6  <cs>      from the caller's 'push cs'
SP+8  <buf>     the buffer pointer we want
```

The handler then pushes `cx`, `ds`, `es` (net three words after the `push ds`/`pop es`
pair), moving everything down by 6:

```
SP+0 es   SP+2 ds   SP+4 cx   SP+6 IP   SP+8 CS   SP+10 FLAGS   SP+12 <cs>   SP+14 <buf>
```

`0x0E` is 14. The constant is exact, and it is why the `push cs` in the original call site
**must be left in place** by the patch.

## 5. The installer

```asm
015C  B4 25         mov ah,0x25      ; DOS Set Interrupt Vector
015E  B0 2B         mov al,0x2B      ; vector 2Bh
0160  BA 40 01      mov dx,0x140     ; DS:DX = handler (DS = CS for a .COM)
0163  CD 21         int 21h
0165  8D 16 5C 01   lea dx,[0x15C]   ; end of the resident part
0169  B1 04         mov cl,4
016B  D3 EA         shr dx,cl        ; -> paragraphs
016D  42            inc dx           ; round up
016E  B4 31         mov ah,0x31      ; Terminate and Stay Resident
0170  CD 21         int 21h
```

Resident size is `(0x15C >> 4) + 1` = **0x16 paragraphs = 352 bytes**. The installer itself
sits above that mark and is released.

`INT 2Bh` is a DOS-reserved-but-unused vector, which is why it is safe to take.

---

## 6. The patch on the caller's side

`hasp_call_site()` locates the near call inside `check_dongle`:

```
        push <buf>      68 xx xx
        push cs         0E
        call read_hasp  E8 rel16          <- 3 bytes
```

and overwrites **only the `E8 rel16`**:

```
        push <buf>      68 xx xx
        push cs         0E               <- preserved, the handler depends on it
        int  2Bh        CD 2B
        nop             90
```

Three bytes replaced by three bytes: `E8 xx xx` → `CD 2B 90`. Nothing moves, no offsets
shift, and the file length is unchanged.

### The site is verified, not guessed

Two independent checks guard against patching a lookalike call:

1. The `check_dongle` prologue signature (`PRO`) must match **exactly once** in the file.
2. The word pushed immediately before `push cs` must equal the buffer that the
   `Wrong Version: >%s<` report prints — recovered separately from the `WRONGVER`
   signature. A call that pushes some other pointer is rejected.

### A deliberate 2-byte stack imbalance

The original `call`/`retf` pair popped both the return address **and** the `push cs`.
`INT 2Bh`/`IRET` pops only what `INT` pushed, so the `push cs` word is left behind and the
caller's single `pop cx` no longer balances the frame.

This is harmless, and not by accident: `check_dongle` is a Borland `enter`/`leave` frame
(`C8 96 02 00 … C9 CB`), and `leave` restores `SP` from `BP` on exit, discarding the stray
word. The check also runs once per process — its result is cached in `g_checked` — so the
imbalance cannot accumulate.

---

## 7. What else `--mode keyn` changes

The TSR alone is not sufficient. Per executable:

| Edit | Purpose |
|---|---|
| `hasp_call` → `INT 2Bh` | the redirect described above |
| **errbit1**: `0D 01 00` → `0D 00 00` (`or ax,1` → `or ax,0`) | clears the **DS1982 iButton** failure bit — a *second* token, unrelated to the HASP (`Docs/03`). Without this every game aborts with `DS1982 FAILED` |
| *fallback only*: `Error()` thunk `55` → `CB` | an early `retf` that silences the program-wide error reporter |

And in `MENU.EXE`:

| Edit | Purpose |
|---|---|
| `menu_dongle_err`: `75 03` → `90 90` | neutralises the error-screen branch |
| `hasp_call` → `INT 2Bh` | MENU has its **own** `check_dongle` and its own 62-byte buffer |
| errbit1 cleared | as above |

MENU's buffer matters more than it looks: the front end picks the **UI language** out of
that buffer, and the button artwork lives in per-language WADs
(`\MENU\BUTTONS\<LANG>\FOTOPLAY.WAD`). Leave it empty and the language comes out blank —
garbled buttons on IGO5, an outright dongle error on the IGO3 and PP1998SP builds.

### The `Error()` stub is the fallback, not the default

Stubbing `Error()` silences **every** error report in the program — including
`Error opening PCX-File` and `Out of Pictures in DCX-File` — which hides real faults. The
reference `-K` builds do this. `ppkeyless` prefers the surgical `errbit1` edit and only
falls back to the stub when the `or ax,1` site is absent. It will also *un-stub* a
previously stubbed `Error()` when it can make the surgical edit instead.

---

## 8. Why KEYN and not `--mode menu`

`--mode menu` NOPs the branch in `check_dongle` so the function returns success without
reading anything. It is a smaller patch and it needs no TSR — but it leaves **the 62-byte
buffer empty**. The localisation code parses the territory out of that buffer, so photo and
localised games stall. This was falsified in the field, not in theory; see `Docs/06`.

KEYN fills the buffer, which is why it is the default.

---

## 9. Honest limitations

- **It is not hardware emulation.** The dongle is not present; the software is edited to
  ask a TSR instead. `Docs/07` covers actual emulation of the 1999 wire protocol, where an
  unpatched image boots with no TSR at all.
- **The dwords are unexplained.** They work, but nobody here knows what they mean.
- **62 bytes is more than the hardware returns.** The real 1999 type-3 transaction returns
  **48** bytes (`Docs/07`). KEYN serves 62. The extra 14 bytes land in a region the games
  use as scratch — `FMEMO.EXE` saves four colour globals there — so the surplus is
  harmless, but the TSR is not a faithful model of the device.
- **It needs the boot chain edited**, so an image patched this way is no longer original.

## 10. Reproducing it

`scripts/ppkeyless.py`:

```python
build_keyn("Version 2006 (AT)")   # -> 114 bytes, byte-identical to the shipped TSR
```

The banner must equal `MAIN.SET["Version"]` for that image — read it with
`scripts/mainset.py`, never from a filename (`Docs/08`; four images ship a foreign profile).
Maximum banner length is 29 characters plus the terminator.

`analysis/p1/sh/validate.py` regenerates the reference builds and diffs them: **181 files
reproduced byte-for-byte**, which is what establishes that this description is complete.
