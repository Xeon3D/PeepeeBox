# 10. The dongles themselves — 2026-09-24/25

Everything in this file is **measured**: read off physical dongles on the parallel port of
the Atom dongle host (Debian 13, `/dev/port` at `0x378`), either by a tool put to the
part directly or by a real game driving the part through PeepeeBox's passthrough. The
traces, the query logs and the raw wire are in
`docs/research/evidence/hw-2026-09-24/`; the tools are in `tools/dongcap/` (`10.9`).

It corrects `09` in five places (`10.8`), and it replaces two recorded stand-ins in the
emulator with the parts' own answers.

## 10.1 What was on the bench

| dongle | what it turned out to be | result |
|---|---|---|
| 1999 PT, blue, larger | the funworld 8051 dongle (`02`) | boots Photo Play 99 PT, FMEMO shows pictures |
| 1999 PT, smaller | **a Photo Play 2000 PT CDONGLE** — mislabelled | boots Photo Play 2000 PT; FIND IT, AMORE, CONCENTRATION play |
| 2001 PT | HASP4, `7477/7D57` | session, record, keyed round measured |
| 2002 PT | HASP4, `68BB/1329` | the reference part of `05`; replays its 2026-09-06 boot 12,856/12,856 |
| 2003 PT, 2005 PT | HASP4, `6B91/24A3` | identical to each other in every answer |
| 2004 ES | **a CDONGLE** — the 2000 protocol | boots I.G.O. 4 ES |
| 2006 PT, 2007 ES | HASP4, `68BB/1329` | identical to the 2002 in every answer; 2007 ES boots I.G.O. 7 ES |
| IGO 8 Italy | HASP4, **`68BB/1329`** | record `IT-Version08IT` |
| I.G.O. 6 ES clone | SX28AC microcontroller + 93C46 EEPROM, externally powered | silent — probably killed by a 12 V supply |
| I.G.O. 7 ES clone | clone | silent to every probe and to I.G.O. 7 itself (`wrong dongle version`) |
| I.G.O. 8 ES clone | clone, 5 V from an AT plug | silent; the only I.G.O. 8 ES images are modified builds |

A HASP4 part is identified by **one** password pair and nothing else: the 2002, 2006,
2007 and Italy dongles give byte-identical answers to every question asked, and so do the
2003 and 2005. Nothing in the protocol varies per unit or per year — only the record's
debris bytes (`10.5`) do.

## 10.2 HASP4: the session layer is two tables behind a password

`05.3` modelled the session layer as three recorded exchanges. It is simpler than that,
and it is keyed.

**A command burst opens a mode.** Command bytes are clocked on DATA bit 0 as
`b&FE, b|1, b&FE`. After a burst the part answers every write with one bit on STATUS
bit 5: **the bit at the address the payload names** (bits 1..6) in a 64-bit table. Which
table depends on the burst:

| table (address 0 is the top bit) | `68BB/1329` | `7477/7D57` | `6B91/24A3` |
|---|---|---|---|
| identity | `50 50 73 73 FF 50 FF 73` | same | same |
| sweep | `22 5E 9E DE 44 5C DC DC` | `42 3E D3 BF E2 BE F3 BF` | `0E E6 97 F7 4C E4 D5 F5` |

The identity table is `HD_SIGNATURE` in the emulator's bit order, and it is the same on
all three pairs. The sweep table is the pair's own. The 64-step sweep of `05.3` is simply
64 lookups into it; the answers the games see are

```
68BB/1329   F5 7A 37 E7 8F 8F BD DA     (= HS_SWEEP_A, the 2026-09-06 capture)
7477/7D57   BF CF BF 9E B3 5F 7F 4D
6B91/24A3   53 FB 97 17 9A EE F5 5E
```

Every repeated payload in the sweep gets the same answer — 14 repeats on each pair, none
inconsistent — so the table is the whole of it. Bit 7 of the payload makes no difference
on any pair, which settles `05.6`'s open point: the part ignores it.

**The burst is the password.** The burst before the identity ramp is the same for every
pair (`46 0A 78 5A 3A 14 48 28 00 12 50 30 0C 1E 5C 3C`), which is why a 2001 part
answered the identity ramp to I.G.O. 2's bytes. The burst before the sweep is the pair's:

```
68BB/1329   46 5A 58 1A 7A 54 08 68 40 32 40 20 2C 16 1C 34
7477/7D57   46 00 7C 4A 2A 10 68 08 40 1A 70 10 1C 0E 1C 2C
6B91/24A3   46 2E 38 1A 7A 54 40 08 40 02 70 70 4C 16 1C 1C
```

— produced by I.G.O. 3's own library under unicorn with each password set
(`hasplib.py`), and each accepted only by its own part: a part given another pair's burst
goes silent (every read `1`) until the next burst it recognises. Trying all 64 values of
the eleventh byte, the part answers two and ignores 62: one selects the sweep table, the
other the identity table. So a burst is a code the part checks as a whole, not a mode
field beside a password. The library's second sweep changes exactly that byte, which is
why a real part never answers two sweeps alike and passes the newer library's anti-replay
gate (`05.8` fault 4) — the emulator's XOR-by-sweep-number stands in for that.

The "liveness probe" of `05.3` (`1E` then `1C`) is not a third exchange; it is the tail of
a Microwire read.

**Consequence for I.G.O. 6 and Italy.** Their menus probe `7477/7D57` first and fall back
to `68BB/1329`. A real `68BB` part refuses the `7477` probe — measured on the Italy dongle
through the live library: status fails and the record reads as zeros with `7477`,
`6B91` or `0000`, and succeeds with `68BB`. So on real hardware those releases decode
their record with **`68BB`**, not `0000` (`05.1`) and not `7477` (what the emulator
serves, because its part accepts any burst). Making the emulator's part check bursts is
what would make it exact here; the burst generator is in the library and is next.

## 10.3 HASP4: the keyed round and EncodeData, on the parts

Run through the library itself against the part (`hasplive.py`), `HaspDecodeData`
mode 0 on two fixed blocks:

| pair | key | register | agrees with `softpart.py` |
|---|---|---|---|
| `7477/7D57` | `CF47CB42` | `0x55F` | both blocks |
| `68BB/1329` | `3B227944` | `0x7DF` | both blocks — FIND IT's first block decodes to `GIF87a` |
| `6B91/24A3` | `AB32E970` | `0x5DF` | both blocks, on the 2003 **and** the 2005 part |

All three keys were fitted without hardware; all three are now confirmed on the part.
I.G.O. 5's key is measured, not inferred (`09.5` closed).

The 2001 register is the one the password schedule gives for `7477/7D57` when I.G.O. 3's
library hands the part the password — `0x55F` — not the recorded `0x7DF`. The register is
therefore set by the part from what the library sends, and 2001's own library, which
orders the words differently, is what yields `0x7DF` (`09.4`, narrowed to the library).

**EncodeData modes 1..4** on the 2003 PT, through the library: every one of 404
`GAMESTAT.OLD` rows tried (101 rows × 4 modes) reproduces exactly, with no library error,
so that table is a genuine recording of a `6B91` part. 2,000 further random blocks, 500
per mode, were recorded with the full wire (all 40 answer bits of every round) for the
function behind them (`09.11`); the 2005 PT gives byte-identical answers to 200 of them,
and the 2006 and 2007 agree with each other on the same 200.

## 10.4 HASP4: the record, as the part returns it

Read with `ReadBlock(0, 0x38)` through the library:

```
2001 PT   "            Version 2001 (PT)" 000907 098765 120672 170898 075902 002205 160678 " -35733698"
          (every word high byte first), zeros to byte 109, FF FF
2003 PT   "PT" 00 "sion 2000 (SP)" 00 | 39 3F 12 3D 39 3F 14 3D 39 3F 16 3D | v0..v6, v7=D4026F7A | FF...
2005 PT   "PT-Version2005B" 00 ")" 00 | 62 40 DE 3F ... | v0..v5, v6=0, v7=0 | FF...
2006 PT   "PT-Version2006A" 00 ")" 00 | 35 40 4A 3E ... | v0..v5, 0, 0      | FF...
2007 ES   "ES-Version2007" 00 "P)" 00 | 6F 40 88 3E ... | v0..v5, 0, 0      | FF...
Italy     "IT-Version08IT" 00 "P)" 00 | 94 3C 0C 41 ... | v0..v5, 0, 0      | FF...
```

Bytes 18..29 are three far pointers left over from the programming PC, different on every
unit; the `P` at byte 15 of the 2007 and Italy dongles is debris too. Everything from
byte 62 on is `FF` on every I.G.O. dongle. `v7`, recorded in `07` as per-unit, is zero on
the 2005, 2006, 2007 and Italy units.

## 10.5 The 1999 dongle, live

Photo Play 99 PT booted on the blue dongle through the passthrough and FMEMO showed its
pictures. Decoded off the wire:

```
type 3, nonce CD and CA   "Version 99 (PT)" 00 | v0=0 | 38B 181CD 1D760 29B92 1287E 89D | v7=BAE8A135
type 1 "296053  "         84 81 FE B7     = the hash of 02.3
type 1 "2305    "         8D 61 D5 B7     = the hash of 02.3
```

Two nonces decrypt to the same record, so both keystreams are right; both picture keys are
the firmware's hash to the byte. This unit's `v0` is clean zeros, not host memory.

Getting there took two passthrough fixes (`10.9`): the 1999 guest keeps CTRL bit 5 set,
which turned the host port's data lines around and unpowered the dongle, and it sends its
nibbles into the port latch without the data-write callback ever firing.

## 10.6 The CDONGLE (2000 and 2004), live

The 2004 ES dongle speaks the 2000 protocol exactly (`04.1`), and I.G.O. 4 is a CDONGLE
release, not an unknown `KDONGLE`. The smaller "1999" dongle is a 2000 PT. Both were
driven by their games through the passthrough and then questioned with `cdong`.

**Transport details the docs lacked.** The part needs about 200 µs per write, CONTROL
writes included. The transaction opens with `55`, CTRL `04`, `BF 7F BF`. Every
transaction ends with the host sending the command byte again, unscrambled. A reply is
claimed and handshaken **per call's chunk**: the licence queries stream both bytes in one
claim; the autodetect, the record read and the picture replies claim each byte on its own.
After a byte's closing `BF`, ACK follows DATA bit 5, so a "ready" check passes before the
part has the next byte — the guest's slowness hid that.

**What the part answers, and what it refuses:**

| query | answer | notes |
|---|---|---|
| `AC CB` | `C5` | the autodetect; both parts |
| `A0 86 2E D0` | `93 46` | the licence; both parts |
| `A1 A6 4B D0` | `4E 05` | sent before every `AB` picture query — **new** |
| `A3 73 07 09` | `06 12` | case 2's constant, `0x1206` — **measured** |
| `A4 76 02 27` | `43 73` | case 3's constant, `0x7343` — **measured** |
| `AD A6 8C 0D 00 <len> …` | `len` bytes of the record | offset 0, length ≤ 48; anything else refused |
| any other challenge, `A2`, `A5`–`A8` | nothing | refused: ACK stays up after the claim |

A refusal is silence: the host's claim is never acknowledged, and the part recovers at
the next transaction. There is no challenge→response function to recover (`09.6`): the
part knows these queries and no others.

**The records:**

```
2000 PT   "Version 2000 (PT)" 00 "P" 00 | 38B 181CD 1D760 29B92 1287E 89D | v7=A5ED75F2
2004 ES   "Version 2004 (ES)" 00 "P" 00 | same six                       | v7=0A2E03A1
```

**The picture-key query**, as FIND IT, AMORE and CONCENTRATION send it:

```
licence  A0 86 2E D0 -> 93 46              (A1 A6 4B D0 -> 4E 05 before an AB)
nonce, AA|AB, 08 00, constant low byte first
then eight times: one name byte out, one reply byte in (claimed on its own)
one more reply byte, then the command byte unscrambled
```

The eight values the game folds are the wire bytes with its merge undone
(`r[i] = wire[i] & 0F | wire[i+1] & F0`); the two nibbles the merge discards vary from
query to query and mean nothing. Byte *j* depends on name byte *j* only, so one query
with a character in all eight positions reads that character's column: **256 queries per
case read the part's whole table**, 1,024 in all, none refused.

Against the table the emulator shipped (fitted from 3,765 cracked picture keys): every
position differs from the part by one constant, and those constants cancel in the fold —
XOR for case 0, ADD for cases 2 and 3, and for case 1 the pairs are **2/5, 3/6 and 4/7**,
which is why it never reads the first two. So the fitted table was right in everything the
game computes, and the part now supplies the bytes themselves, including the 58 case-3
entries and every character outside `0x20..0x5F` that the archives never showed.

**The constant is an input, not a tag.** The part answers any constant, differently for
each, and differently again under the other command (`038C`, `0002`, `1207`, `7344`,
`AA` with `0A8E`, `AB` with `038B` were tried). `7344`'s answer looks like `7343`'s moved
one position, which is a lead on the real function. The emulator serves the four pairs the
games use and refuses the rest.

## 10.7 On screen

| release | image | dongle | screen |
|---|---|---|---|
| Photo Play 99 PT | C5196 | blue 1999 | menu; FMEMO pictures show |
| Photo Play 2000 PT | A2132 | 2000 PT | FIND IT, AMORE, CONCENTRATION play |
| I.G.O. 4 ES | A0009 | 2004 ES | menu |
| I.G.O. 7 ES | MK002 | 2007 ES | a game starts |
| I.G.O. 7 ES | MK002 | I.G.O. 7 clone | `wrong dongle version`; 129 status reads, all `78` |

## 10.8 What this changes in 09

- **09.2 closed, the other way.** The session layer *is* keyed by the password: the sweep
  table is per pair. I.G.O. 3 and 5 were being served `68BB`'s sweep and booted anyway.
- **09.4 narrowed.** The register comes from what the library hands the part; `0x55F` is
  measured for I.G.O. 3's library, `0x7DF` stays recorded for 2001's own.
- **09.5 closed.** I.G.O. 5's key `AB32E970` is measured on its part.
- **09.6 mostly closed.** There is no challenge function (the part refuses every other
  challenge); `A3`/`A4` are `1206`/`7343`; the picture table is read off the part. Still
  open: the closed form behind the table and how the constant enters it.
- **09.8** — I.G.O. 4's dongle is a CDONGLE. Its `KDONGLE` letter is the menu's name for
  it.
- **05.1's "decode with `0x0000`" is wrong for real hardware**: a real part makes I.G.O. 6
  and Italy settle on `68BB`.

## 10.9 Tools and fixes

| | |
|---|---|
| `tools/dongcap/dongsession.c` | the session questions, with a ramp burst and a sweep burst (`-p`, `-s`) |
| `tools/dongcap/dongwire.c` | runs a script of `W`/`C`/`S`/`D`/`M` lines against the part and scores every STATUS read (`-m` selects the bit: `40` for CDONGLE) |
| `tools/dongcap/hasplive.py` | a release's own HASP library under unicorn with the **real** port — any service, any password, the game's own wire |
| `tools/dongcap/cdong.c` | the CDONGLE transport: any command (`CMD:ARGS:LEN/CHUNK`), continuations (`+`), picture queries (`P<AA|AB>:<const>:<name hex>`) |

`SESSION.COM` never worked: it sends no burst, so a part answers nothing. `dongsession`
is what it was meant to be.

Two passthrough fixes in `src/device/dongle_photoplay.c`, both needed for any 1999 dongle:
CONTROL bit 5 is never passed to the host port (a cabinet port is plain SPP and the bit
means nothing there), and the port's latched DATA is pushed to the host port before every
CONTROL write and STATUS read, because 86Box does not call `write_data` while the guest
holds bit 5.

The Atom host builds nothing now: the Linux Qt binary is built on the workstation in a
`debian:trixie` container (matching the host's glibc and Qt 6.8) and copied over.
