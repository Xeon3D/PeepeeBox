# I.G.O. 2 (Version 2002) — everything found

Status: **boots and plays.** All menus, animations, buttons and instruction art are
correct. Two archives — and only two — come up blank, both of them the ones holding
photographs. That is the 2001-generation picture cipher, which is unsolved.

Images: eight untouched builds under `IGO2-6/` — BE ×2, DE, GR, IT ×2, NL ×2. Every
`MAIN.SET` reads `Version 2002 (<territory>)`, so the device's auto-ident resolves both
the release and the territory with nothing pinned by hand.

---

## 1. What the guest checks

`MENU.EXE` (DE build, 266,162 bytes). DGROUP sits at file `0x3BEA0`. That was not
guessed: the six `… not found` message strings are pushed as immediates inside one
400-byte dispatch block at `0xBB37`, and only one base makes all six land there.

The token list is shorter than the later generations' — `dongle`, `CDONGLE`, `DS1425`,
`DS1982`, **`HDONGLE`**, `GDONGLE` — and the dispatch is an index, `[bp-2] - 1`, not a
bitmask.

### 1.1 Passwords: literals, no probe

`0x68BB` / `0x1329`, written as immediates at five call sites, matching the 2002PT dump.
I.G.O. 2 does **not** run the runtime password probe that I.G.O. 6 and Italy use, so it
never falls through to a zero key. The HASP entry point is `lcall 0x35E9:0x0009`.

### 1.2 The check — `0x36D7C`

```
36D8C  call 0x36B42                                      ; fill the buffer at DS:0x8880
36D94  push "Version 2002" ; push buf ; lcall 0,0x42F9   ; strstr
36DA4  or ax, 0x20   if it missed
36DB9  … DS1982 iButton at port 0x30268; its page is compared against
       "Photo Play 2000 Version 3", stored XOR 0x7C at DG+0x4A55
```

The iButton string is what this device already serves, so that half passes untouched.

### 1.3 The filler — `0x36B42`

```
36B8E  service 1     bail if p1 == 0
36BC1  service 5     bail if p1 == 0        <- note: != 0, not == 1
36BF1  service 5     p2 -> the lptnum used below
36C30  service 0x32  start word 0, 15 words, into [bp-0x98]; bail if p3 != 0
36C43  strcpy(dest, "Version 2002 (")       <- DG+0x4A37, the guest's own literal
36C4F  strcat(dest, record)                 <- the record, as a C string
36C5D  strcat(dest, ")")
36C70  8x: read 2 words at (0x0F + 2n), assemble a little-endian dword,
       store at si+0x1E+4n
```

**The version text is the guest's own literal.** The record supplies only the territory:
two characters and a NUL. Once the block read returns `p3 == 0`, the strstr for
`"Version 2002"` cannot fail.

This is why the 2002 and 2003 dumps read `"PT" 00 "sion 2000 (SP)" 00` — everything past
the NUL is dead weight left over from a 2000-era banner, and nothing reads it. `HD_RSION`
already builds exactly this, so I.G.O. 2 needed no new device code. The byte order is
confirmed by the same routine: the block-read buffer is handed straight to `strcat`, so
the library's words land low byte first (`swap = 1`).

### 1.4 A second filler that nothing appears to reach

`MENU.EXE` also carries a **2001-shaped** filler at `0x3688E`: 56 words from word 0,
unpacked high byte first (`idiv 0x100`, high then low), a 30-column banner and
six-column decimal fields at 30, 36, 42 … No path traced from the copy-protection check
reaches it, but if some module does, it wants `HD_R2001` instead — and the two shapes
cannot both be served at once.

---

## 2. The record is correct, and the dump proves it

The 2002PT dongle dump's eight dwords read

```
907   98765   120672   170898   75902   2205   160678   -371202944
```

which is `hd_fields[]` followed by `hd_keys[]`'s `v6` and `v7` for "Version 2002" — the
`v7` included, which until this check had only ever been fitted. Nothing in the record
this device serves I.G.O. 2 is in doubt.

---

## 3. The pictures: two ciphers, one wall

funworld shipped two picture schemes side by side and each archive uses one or the other.

- **The 1999/2000 scheme** enciphers only the **first 128 bytes**, per picture, with the
  Turbo Pascal LCG. The key is recoverable from the shipped data alone, and this device
  already handles it.
- **The 2001 scheme** enciphers the **whole file** in 8-byte CBC blocks with a cipher the
  dongle computes — the library shifts a byte out and reads one bit back, forty times per
  eight bytes. Unsolved.

They separate without decrypting anything: the header-only scheme leaves the body
plaintext, so body entropy collapses; and it keys per picture, so no two entries share a
first block.

| archive | entries | distinct first blocks | body entropy | |
|---|---|---|---|---|
| `FINDIT/PICS` | 1397 | **1** of 40 | **7.90** | whole-file — blank |
| `FINDIT/PICS/PART0` | 1394 | 40 of 40 | 5.28 | header-only — renders |
| `FINDIT/PICS/PART1` | 1394 | 40 of 40 | 5.25 | header-only — renders |
| `FINDIT/` | 4 | 1 of 4 | 4.54 | header-only — renders |
| `FINDIT/ANIM` | 19 | 19 of 19 | 4.14 | header-only — renders |
| `FINDIT/RES` | 6 | 2 of 6 | 4.82 | header-only — renders |
| `FINDIT/INS` | 3 | 3 of 3 | 4.50 | header-only — renders |
| `AMORE/COMIX` | 332 | **1** of 40 | **7.90** | whole-file — blank |
| `AMORE/ANIM` | 4 | 4 of 4 | 4.29 | header-only — renders |
| `AMORE/GRAFIX` | 27 | 1 of 27 | 3.42 | header-only — renders |
| `AMORE/RES` | 6 | 3 of 6 | 5.01 | header-only — renders |
| `AMORE/INS` | 3 | 3 of 3 | 4.80 | header-only — renders |

**Exactly two archives in the release are behind the dongle cipher, and they are the two
that hold photographs.**

### 3.1 Which is exactly the reported symptom

FIND IT lays a difference overlay from `PART0..PART4` over a base photo from
`FINDIT/PICS`. The base is blank and the overlay draws, so what reaches the screen is the
differences and nothing else. AMORE's photo comics live in `AMORE/COMIX` alone, so they
come up black while the rest of that game is intact.

### 3.2 Same cipher as 2001

All 1397 `FINDIT/PICS` entries share the first ciphertext block
`97 36 21 d0 1d 6a 2d c7` and diverge from byte 8; all 332 `AMORE/COMIX` entries share
`32 e8 22 09 e0 da 19 68`. A constant first block with immediate divergence is CBC under
a zero IV — the 2001 model exactly. No entry ends in a GIF trailer (`0x3B`), so the
bodies are genuinely enciphered rather than merely compressed.

Two cautions against over-reading the shared constants.
`AMORE/GRAFIX/FOTOPLAY.WAD` and `FINDIT/FOTOPLAY.WAD` are **byte-identical** across the
2000, 2001 and I.G.O. 2 images — the same shipped files, so their matching first blocks
say nothing about keys. Where the content genuinely differs, so does the constant:
`AMORE/COMIX` is `32 e8 …` here against 2001's `d2 46 …`, over 332 entries with no shared
names.

### 3.3 What I.G.O. 2 adds to attacking it

The container changed. 2001's photo archives are PCX and its known plaintext had to be
reconstructed from the 2000 images; I.G.O. 2's `FINDIT/PICS` holds **GIF**, whose opening
bytes are fixed by the format. It is a second corpus under a second password pair
(`68BB/1329` against 2001's `7477/7D57`), which constrains the algorithm from a direction
the 2001 data alone cannot.

---

## 4. Two leads closed

**The reference HASP4 emulator does not have the function.** `haspnt64`'s DOS core
implements `HASP_INSTALLED`, `HASP_CODE`, `READ_MEMO`, `WRITE_MEMO`, `GET_HASP_STATUS`,
`GET_ID_NUM`, the two undocumented calls and the block reads — but **not** EncodeData or
DecodeData. The picture function is not in it.

**Its `HaspCode` transliteration does not explain the probe either.** At the seed the
guest uses (zero), it returns `0030 2247 856A AD59` for `7477/7D57`, `8A6F FB84 3B91
76BC` for `68BB/1329` and `AEE5 CC79 ABB0 992C` for `6B91/24A3` — no `p1 == 1` anywhere,
so it cannot be what I.G.O. 6's probe is testing. It remains unvalidated.

---

## 5. Status

Nothing further is needed from the record or the transport. What is left is the picture
cipher, the same wall 2001 and I.G.O. 3 stand behind.
