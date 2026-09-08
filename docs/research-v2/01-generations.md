# 1. One table for the whole family

**There is no Aladdin HASP in the 1999 or 2000 generations, and Aladdin's library is
not involved in reaching any token.** The 1999 device is funworld's own silicon. From
2001 the parallel token *is* a HASP4 — identified as such, corroborated by its 112-byte
memory, its two 16-bit passwords and the published service numbers it is called with —
but it is still driven by funworld's own code, and `HASPDOSDRV` / `No HASP!` strings in
IGO 5/6/7 belong to one probe branch out of ten, not to the path in use.

`MENU.EXE` carries a per-type "not found" message for every dongle design the company
ever shipped, and the list grows by about one letter a year and never loses one
(1999: none; 2000: C; 2001: C,H; 2002: +G; 2003: +I; 2004: +K; 2005: +M,MB;
2006: +N; 2007: +O; 2008: +NG). A letter being present says the *software* can probe
for that type, not that the cabinet has one.

## What each release actually has

| release | banner | parallel token | second token | state |
|---|---|---|---|---|
| Photo Play 99 | `Version 99` | funworld 8051 + 24Cxx | DS1982 | **complete** — boots, plays, pictures decrypt |
| Photo Play 2000 | `Version 2000` | CDONGLE / PDONGLE | DS1982 | **complete** — all four images boot, play, pictures decrypt |
| Photo Play 2001 / I.G.O. 1 | `Version 2001` | HASP4, pass `7477/7D57` | — | transport complete; picture key `CF47CB42` fitted and verified offline |
| I.G.O. 2 | `Version 2002` | HASP4, pass `68BB/1329` | — | **complete** — boots, FIND IT plays, photographs decrypt on screen |
| I.G.O. 3 | `Version 2003` | HASP4, pass `6B91/24A3` | — | **does not boot.** Transport complete and the session layer served as a real part answers it; stops at `dongle error` inside the service exchange, before the keyed round is ever entered |
| I.G.O. 4 | `Version 2004` | a parallel dongle, **not** HASP — no HASP library in any of its 43 executables | — | its pictures are plain GIF87a, so nothing on the image needs a content key |
| I.G.O. 5 | `Version 2005B` | HASP4, pass `6B91/24A3` | — | transport complete; pictures are plain |
| I.G.O. 6 | `Version 2006A` | HASP4, pass `68BB/1329` (from the dumps) | — | transport complete; pictures are plain |
| I.G.O. 7 | `Version 2007` | HASP4, pass `68BB/1329` | — | transport complete; pictures are plain |
| I.G.O. 8 | `Version 2008` | **none** — serial card reader on COM2 | — | **complete** — untouched images boot |
| I.G.O. Italy | `Version 08IT` | HASP4 (all 54 executables link it) | — | transport complete |

Two cautions that cost time when they were missed:

- **The 2008 generation comes in both flavours.** The IGO 8 images are serial-only —
  zero of 61 executables link the parallel library, and 15 name `NGDONGLE` beside the
  serial base `0x2F8`. The I.G.O. Italy build of the same year is entirely parallel —
  54 of 54 link it, none mention NG. Which one a cabinet has is a property of the
  build, not the year. `HDONGLE FAILED` appearing in an IGO 8 game's strings means
  nothing: the message table is shared and carries every type the family ever used.
- **I.G.O. 6 and I.G.O. Italy store `0000 / 0000` as their passwords in every
  executable.** For I.G.O. 6 the dumped hardware says the real pair is `68BB / 1329`,
  so every I.G.O. 6 image in circulation has been neutered the same way. Treat a zero
  pair on an image as "removed", never as a value — but note the practical consequence
  in `05`: what those builds actually *decode the record with* is `0x0000`.

## The banner is the whole of what the check compares

Every generation ends up doing the same thing: assemble a version string out of the
dongle's record and compare it against `\FOTO\SETTINGS\MAIN.SET["Version"]`. Get it
right and the machine boots; get it wrong and you get `Wrong Version`,
`wrong dongle version`, or `<X>DONGLE FAILED`.

`MAIN.SET` is the authority, not the image's filename — one image in circulation is
filed as a 1998 Spanish build and its `MAIN.SET` says `Version 2003 (ES)`. It is
encrypted with a *fixed* key, so it can be read straight off the image with no dongle:
three 16-bit words (count, key-block size, pool size), then the key block, then the
string pool, every read restarting the same Borland LCG keystream
(`s = s*0x08088405 + 1`, keystream byte `s >> 24`) from seed `0x00016295`.
`src/photoplay_ident.c` does exactly this.
