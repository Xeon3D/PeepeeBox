# I.G.O. Italy (Version 08IT) — handoff

## Status: WORKING at HEAD. There is nothing to fix.

Marcos confirmed Italy works on the **Release 1.5** build, and
`git diff v1.5 HEAD -- src/device/dongle_photoplay.c` is **empty** — HEAD's dongle code *is*
v1.5's. The only changes between those two commits are CI workflow bumps and research notes.

**If Italy is failing for you, you have broken it. Rebuild from a clean tree before doing
anything else:**

```
git status --short          # src/device/dongle_photoplay.c must NOT be listed
touch src/device/dongle_photoplay.c && ninja -C build
```

(`ninja` will say "no work to do" after a `git checkout` if mtimes look unchanged, and you will
keep running a stale binary. Touch the file.)

---

## 1. Why Italy works — the `probe` column

Italy and I.G.O. 6 do not hardcode their passwords; `MENU.EXE 0x3C252` hunts for a pair at
runtime and keeps whichever one the part answers service 5 with a 1 to:

| order | pass1 | pass2 | decimal |
|---|---|---|---|
| first | `0x7477` | `0x7D57` | 29815 / 32087 |
| fallback | `0x68BB` | `0x1329` | 26811 / 4905 |

Our part answers service 1 with a 1 and service 5 with neither, so **the probe always falls
through to the last branch and zeroes both passwords.** `pass1` is the descramble key, so those
releases decode the record with **`0x0000`** — not with the password their real dongle holds.

That is measured, not inferred: I.G.O. 6 DE put `>ÞÈ` on screen where `Vers` belongs, which is
record bytes 3..6 XORed byte-wise with `0x68BB` — our scramble undone with zero.

The table encodes this with a `probe` flag that keeps the dumped password on record while serving
the key the guest will actually use (`dongle_photoplay.c:1481`):

```c
const uint16_t key = hd_keys[rel].probe ? 0x0000 : hd_keys[rel].pass1;
```

```c
{ "Version 2001",  0x7477, 0, 0, HD_R2001, 160678,  -35733698 },
{ "Version 2002",  0x68BB, 0, 1, HD_RSION, 160678, -371202944 }, /* I.G.O. 2 */
{ "Version 2003",  0x6B91, 0, 1, HD_RSION, 160678, -738037894 }, /* I.G.O. 3 */
{ "Version 2005",  0x6B91, 0, 1, HD_RVERS,      0,          0 }, /* I.G.O. 5 */
{ "Version 2006",  0x68BB, 1, 1, HD_RVERS,      0,          0 }, /* I.G.O. 6 */
{ "Version 2007",  0x68BB, 0, 1, HD_RVERS,      0,          0 }, /* I.G.O. 7 */
{ "Version 08",    0x68BB, 1, 1, HD_RVERS,      0,          0 }  /* I.G.O. Italy */
```

Columns are `banner, pass1, probe, swap, shape, v6, v7`. **Do not remove the `probe` column** and
do not "simplify" it into `pass1`. Make service 5 answer and this correctly flips back to `pass1`.

---

## 2. How this was broken (so it is not repeated)

A session edited `hd_keys[]` down to five columns, dropping `probe`, and set Italy to
`pass1 = 0x7477`, `HD_RVERS`. It also removed an `HD_RTEXT` shape and raised the read-log cap.
Italy then failed with `NDONGLE not found`, and a long investigation followed that treated the
self-inflicted failure as a property of the guest. Every "combination tried and failed" recorded
in the earlier version of this document — `0x7477`/swap0, `0x7477`/swap1, `0x68BB`/swap1,
`0x0000`/swap1 — was measured against a broken build and none of it means anything.

**Lesson: check `git status` and diff against the last known-good tag before investigating a
regression.** The whole detour would have been avoided by one `git diff v1.5 HEAD`.

---

## 3. Disassembly that IS accurate and worth keeping

This part was verified against the binary and against nine real dongle dumps, and stands
independently of the mess above.

`MENU.EXE` original is 301,592 bytes. HDR `0x6000`, DGROUP static seg `0x3E12`, DGROUP file offset
`0x44120`, **runtime load delta `0x23F0`**, so **DGROUP linear = `0x46510`**. Confirmed: the needle
string at DG+`0x4D63` was found at RAM `0x4B273`, and the filler's sprintf buffer `DS:0x844A` is at
RAM `0x4E95A`.

### The NDONGLE check — `0x3C8B3`

```
3C8BD  cmp byte [0x4d10], 0 ; je 0x3c8c7      ; else return the cached answer
3C8C7  push 0x844a ; push cs ; call 0x3c322   ; fill the buffer at DS:0x844A
3C8CF  push 0x4d63 ; push 0x844a ; lcall 0,0x4a95   ; strstr(buf, "Version 08IT")
3C8DD  or ax,ax ; jne 0x3c8ea
3C8E4  or ax, 0x400                           ; <-- NDONGLE
3C8EA..3C923  iButton block (port 0x30268)
3C97F  [0x4d10]=1 ; caches only the LOW byte, so the 0x400 bit is lost on later calls
```

There is exactly one condition for NDONGLE: the strstr must hit. Bit 10 = `0x400`, confirmed
against the message table at file `0x4444D7`: `dongle`(0), `CDONGLE`(1), `DS1425`(2), `DS1982`(3),
`HDONGLE`(4), `GDONGLE`(5), `IDONGLE`(6), `KDONGLE`(7), `MDONGLE`(8), `MBDONGLE`(9),
**`NDONGLE`(10)**, `ODONGLE`(11), `NG-DONGLE`(12).

### The filler — `0x3C322`, the same one every I.G.O. build uses

`hasp(service, seedcode, lptnum, pass1, pass2, &p1, &p2, &p3, &p4)`, far call `lcall 0x3de2,0x000d`.

```
3C32B  if [0x4d0c]==0 -> call 0x3c252         ; the password probe
3C380  service 1   -> bail (al=0) if p1 == 0
3C3B5  service 5   -> bail if p1 == 0
3C3E7  service 5   -> p3 becomes the lptnum used for the block read
3C3F7  p1=0 (start word), p2=0x0F (15 words), p3=ss, p4=offset of [bp-0x9c]
3C42B  service 0x32 (block read)  -> bail if p3 != 0
3C43E  strlen(rec); if <= 2 -> sprintf "Wrong HASP!" / "No HASP!" instead
3C462  dest[3..9]   = rec[3..9]        -> strA
3C47F  dest[0xA..D] = rec[0xA..0xD]    -> strB
3C4B1  sprintf(di, "%s %s (%c%c)", strA, strB, rec[0], rec[1])
3C4E7  8x: read 2 words at caller word (n*2+0x0F), store dword at di+0x1E+4n
```

**The record is fields, not text.** The space in `Version 08IT` comes from the format string,
never from the dongle:

| bytes | content |
|---|---|
| 0–1 | `IT` (territory — this is the `%c%c`) |
| 2 | `-` (skipped) |
| 3–9 | `Version` |
| 10–13 | `08IT` |
| 30+ | 8 little-endian dwords |

Nine real `hasp.dmp` dumps (`PhotoPlay2000_h5dmp/`, record at offset `0xC3`) confirm this layout
byte-for-byte for 2005/2006/2007 and confirm the table's dwords — 2003PT gives
`160678, -738037894`, exactly the values in `hd_keys[]`.

`HD_START = 8`: the library adds 8, so caller word 0 is EEPROM word 8. On the wire the guest reads
`0x08..0x16` (the 15-word block) then `0x17..0x26` (the 8 dwords).

**Do not add an `HD_RTEXT`-style shape that copies the banner in as text.** It puts `'V','e'` where
the territory belongs and can never match.

---

## 4. I.G.O. 2 and I.G.O. 6

**They already run** — commit `e09a788 "I.G.O. 2 and I.G.O. 6 run"`, written up in Release 1.4.
(An earlier version of this document claimed no images existed. That was wrong.)

Images are in `IGO2-6/` — fifteen of them:
`IGO 2` in BE ×2, DE, GR, IT ×2, NL ×2; `IGO 6` in AT, DE, ES, GR, IT, PT ×2.
`PPIGO6DE/` is a ready-made I.G.O. 6 DE rig.

Known open issue, from `446ea63`: in I.G.O. 2, two archives are blank — and they are the two
containing photographs. That is the picture-cipher problem, not a dongle problem.

---

## 5. Odds and ends

- **`MAIN.SET` files are encrypted.** Identifying a rig by extracting `FOTO\SETTINGS\MAIN.SET` and
  grepping for a version string does not work — it is ciphertext, and an empty grep result is not
  an extraction failure. Use image file dates instead.
- **Recon rig identities** (by `FN_SYS\DFU\PPPMENU.EXE` timestamp):
  `15-igo3` 2003-07 · `13-igo4` 2003-12 · `14-igo5` 2005-02 · `16-igo7` 2006-12 ·
  `17-igo8` 2008-02 · `18-igoitaly` 2009-05.
- **`PPIGO8IT/`** is a cracked rig with stock `86Box.exe`. Its `MENU\MENU.EXE` is 267,616 bytes vs
  the original 301,592 — a different build, so a byte-diff against the original will not isolate
  the crack. Its black FIND IT images and all-yellow price button follow from the mismatched menu
  binary, not from anything dongle-related.
- Never point a rig at `PP2001/` or `PP2000/`; always run from a copy.
- Run rigs as `./PeepeeBox.exe -L 86box.log` — without `-L` no log is written at all.
- Tooling: no `ndisasm` / `objdump`; use python `capstone` with `CS_MODE_16` over raw file offsets.
  7-Zip is at `/c/Program Files/7-Zip/7z.exe` (not on PATH) and opens `HardDisk.img` directly.
  Build needs `export PATH="/c/msys64/mingw64/bin:$PATH"` or `cc.exe` fails silently.
