# Phase 20 — the dumped dongles

Marcos supplied `PhotoPlay2000_h5dmp/`: h5dmp dumps of **nine** dongles — 2001 ES,
2001 PT, 2002 PT, 2003 ES, 2003 PT, 2005 ES, 2005 PT, 2006 PT and 2007 ES. Each is
a 719-byte `hasp.dmp`. They replace most of Phases 19 and 22's inference with
hardware, and they carry the one thing the emulator still needs.

## 1. Layout

| offset | bytes | what |
|---|---|---|
| `000` | 4 | the two passwords, little-endian |
| `004` | 5 | `01 01 01 00 00` |
| `009` | ~166 | **the crypto table** — see § 3 |
| `0AF` | 8 | per-unit; the only bytes that differ between two dongles of the same generation |
| `0C3` | 112 | the record, as the guest assembles it |
| `133` | — | `FF` padding |

## 2. What they confirm

**The passwords.** Byte for byte the values Phase 19 lifted out of the binaries,
including **2006's `68BB / 1329`**, which no image could give: all seven IGO 6
images carry `0000 / 0000`, exactly the neutered copies that phase suspected.

| generation | pass1 | pass2 |
|---|---|---|
| 2001 | `7477` | `7D57` |
| I.G.O. 2 (2002) | `68BB` | `1329` |
| I.G.O. 3 (2003) | `6B91` | `24A3` |
| I.G.O. 5 (2005) | `6B91` | `24A3` |
| I.G.O. 6 (2006) | `68BB` | `1329` |
| I.G.O. 7 (2007) | `68BB` | `1329` |

**The byte order**, independently. A 2001 dump reads as its record only after every
16-bit word is byte-swapped; an I.G.O. dump reads directly. That is the same
high-first / low-first split the two families' unpack loops use, arrived at here
from a garbled banner on screen.

**Three record shapes**, all now copied verbatim rather than guessed:

```
2001         13 spaces then "Version 2001 (ES)"      <- RIGHT-aligned in 30 columns
             then 000907 098765 120672 170898 075902 002205 160678 " -35733698"
2002, 2003   "PT" 00 "sion 2000 (SP)" 00             <- territory over a 2000 banner
2005..2007   "PT" '-' "Version" "2005B" 00 ')' 00
```

and for the I.G.O. shapes, eight little-endian dwords at byte 30. `v0`..`v5` are
identical on all nine dongles; `v6` is `160678` up to 2003 and **zero** from 2005
on; `v7` is per-unit and no game is known to read it.

Three things in this device were wrong before the dumps: 2001's banner was
left-aligned, its numbers space-padded, and its last field written unsigned
(`4259233598`, which a DOS `atol` overflows — the hardware says `" -35733698"`).
I.G.O. 6's banner is `Version 2006A`, matching 2005's `B` suffix.

## 3. The crypto table, and why it matters

Bytes `009`..`0AE` are **identical for every dongle that shares a password pair**:

| dongles | passwords | table begins |
|---|---|---|
| 2001 ES, 2001 PT | `7477 / 7D57` | `7B 6E AF E5 32 3B 5D 5D` |
| 2002 PT, 2006 PT, 2007 ES | `68BB / 1329` | `3B 7D B9 9E 22 71 E7 21` |
| 2003 ES/PT, 2005 ES/PT | `6B91 / 24A3` | `5E D7 BC 7D 32 5A D7 57` |

Three pairs, three tables, and dongles of *different generations* share a table
whenever they share the passwords. So it is the customer's key material, not a
per-unit or per-year value.

**This is what the outstanding work needs.** `notes/HANDOFF2001.md` § 20 established that
the 2001 photo cipher is not software: the library shifts a byte to the dongle and
reads one bit back (`0x2D651` → STATUS bit 5), forty times per eight bytes, and the
dongle computes. § 22 then found that **I.G.O. 3 cannot even boot without it** — it
calls service `0x3C`, HaspEncodeData, before anything else and reports
`dongle error` when it fails. So one mechanism blocks both.

The table is the dongle side of that function. Working out how it drives the
byte → bit oracle is the remaining problem, and there is an unusually good
validation path: the 2000 images hold the same 1397 FINDIT pictures with plaintext
bodies, so every 8-byte block of a 2001 archive is a known plaintext/ciphertext
pair under the `7477 / 7D57` table, testable offline with no hardware and no
booting. I.G.O. 3 gives a second, much smaller case — 20 bytes at `DS:0x50F6` —
under the `6B91 / 24A3` table.
