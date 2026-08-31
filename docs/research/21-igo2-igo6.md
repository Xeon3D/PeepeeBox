# Phase 21 — I.G.O. 2 and I.G.O. 6, and a regression that faked a wall

Marcos supplied fifteen untouched images under `IGO2-6/`: eight I.G.O. 2 builds
(BE ×2, DE, GR, IT ×2, NL ×2) and seven I.G.O. 6 builds (AT, DE, ES, GR, IT,
PT ×2). Each folder is already a rig — `HardDisk.img`, `nvr/`, `roms/`,
`run.cmd`. Every `MAIN.SET` reads cleanly:

| folder | MAIN.SET `Version` |
|---|---|
| `IGO 2 <terr> …` | `Version 2002 (<terr>)` |
| `IGO 6 <terr> …` | `Version 2006 (<terr>)` |

so the device's auto-ident picks both fields up without anything being pinned.

## 1. The regression

`hd_write_data()`'s Microwire `case 2 /* READ */` had lost `dev->hd_ph =
HD_READ;`. Without it the state machine stays in `HD_ADDR` after the address,
so every following clock shifts another address bit in and **no data is ever
shifted out**. The device answered the framing and served nothing.

That is what `handoffigoitaly.md` § 4 measured and read as "the library performs
the wire reads and then discards the result, substituting an index-derived
pattern". There is no substitution: the guest was descrambling a read that never
happened. Everything § 4 concludes from it — including "the probe can never
select `0x68BB`" and the Italy row's move from `68BB` to `7477` — rests on that
build and has to be re-measured. The line is restored.

## 2. I.G.O. 2 — `MENU.EXE` 0x36B42, and the record hardly matters

DGROUP is at file `0x3BEA0` (found by voting: the six `not found` message
offsets are pushed as immediates in one 400-byte dispatch block at `0xBB37`).
The token list is shorter than the later ones — `dongle`, `CDONGLE`, `DS1425`,
`DS1982`, **`HDONGLE`**, `GDONGLE` — and the dispatch is an index, `[bp-2]-1`,
not a bitmask.

Passwords are **literals**: `0x68BB` / `0x1329`, five call sites, matching the
2002 dump. No runtime probe. The HASP entry is `lcall 0x35E9:0x0009`.

The check is `0x36D7C`:

```
36D8C  call 0x36B42                 ; fill the buffer at DS:0x8880
36D94  push "Version 2002" ; push buf ; lcall 0,0x42F9   ; strstr
36DA4  or ax, 0x20   if it missed
36DB9  … DS1982 iButton at port 0x30268, page compared against
       "Photo Play 2000 Version 3" (stored XOR 0x7C at DG+0x4A55)
```

and the filler `0x36B42` is the ordinary I.G.O. one:

```
36B8E  service 1     bail if p1 == 0
36BC1  service 5     bail if p1 == 0
36BF1  service 5     p2 -> the lptnum used below
36C30  service 0x32  start word 0, 15 words, into [bp-0x98]; bail if p3 != 0
36C43  strcpy(dest, "Version 2002 (")      <- DG+0x4A37
36C4F  strcat(dest, record)                <- the record, as a C string
36C5D  strcat(dest, ")")
36C70  8x: read 2 words at (0x0F + 2n), assemble a little-endian dword,
       store at si+0x1E+4n
```

**The version text is the guest's own literal.** The record supplies only the
territory: two characters and a NUL. That is exactly why the 2002 and 2003 dumps
read `"PT" 00 "sion 2000 (SP)" 00` — everything past the NUL is dead weight left
over from a 2000-era banner, and the strstr for `"Version 2002"` cannot fail once
the block read succeeds. `HD_RSION` already builds this, so I.G.O. 2 needs
nothing new; the byte order is confirmed too, since the buffer is handed straight
to `strcat` (low byte first, `swap = 1`).

One caveat: `MENU.EXE` also carries a **second, 2001-shaped filler** at
`0x3688E` — 56 words from word 0, unpacked high byte first (`idiv 0x100`, high
then low), a 30-column banner and six-column decimal fields at 30, 36, 42 … If
some module reaches that one it wants the `HD_R2001` record instead, and the two
shapes cannot both be served at once.

## 3. I.G.O. 6 — the runtime probe, same as Italy and I.G.O. 7

No password literals in the pushes; `0x3B2C8` writes them at runtime, and the
sequence is Italy's exactly:

```
3B2CA  [0x4C98]=0x7477, [0x4C9A]=0x7D57
3B30C  service 1  -> if p1 != 1, END      (keeps pair A)
3B33A  service 5  -> if p1 == 1, END      (keeps pair A)
3B34A  [0x4C98]=0x68BB, [0x4C9A]=0x1329
3B374  service 5  -> if p1 == 1, END      (keeps pair B)
3B384  else both = 0
```

`pass1` is the scramble key, so which pair the probe settles on decides whether
the record decodes. **I.G.O. 7 probes the same way and runs**, which is the
evidence that this path is already satisfied — contrary to the Italy note. Its
token list runs to `NDONGLE`, and its needles are `"Version 2002 ("`,
`"Version 2003 ("`, `"Version 2005 ("` and `"Version 2006"`, the last with no
parenthesis, so the `2006` / `2006A` difference between `MAIN.SET` and the dumped
dongle costs nothing.

## 4. Rigs

All fifteen folders now carry the current build and `hd2001 = 1`. Nothing else
was changed in them.
