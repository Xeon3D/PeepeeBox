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
- **The session layer** is modelled as the part follows it: bursts select a table or silence it (`10.12`).
- **09.11 closed.** Modes 1..4 are mode 0's register with one feedback term more (`10.10`).
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

## 10.10 EncodeData modes 1..4, solved

`09.11` said modes 1..4 are not the shift register under any key or starting register,
tested against the (challenge, answer) pairs in `GAMESTAT.OLD`. With the wire of 2,000 live
encodes on the 2003 PT part, the oracle's own answers could be examined instead: the 40
answers of every round, with the byte offered for each.

Three observations settle it:

1. **The first answer of a round is the same function of the query in every mode**, and it
   is mode 0's: the complement of key `AB32E970` at the queried index, which is what
   register `0x5DF` gives on the first step. Same key, same starting state.
2. **Rounds depart from mode 0 at step 11 at the earliest** — every round in mode 1, and
   roughly half as often at each later step in modes 2..4. The answer at step *s* reads the
   bit fed back at step *s* − 11, so this is exactly what a change in **the feedback bit
   alone** looks like.
3. **So the feedback bits can be read off the answers**:
   `b0[t] = ans[t+11] ^ key[i5[t+11]] ^ (i5[t+3] & 1)`, the register rebuilt exactly from
   them, and the difference from mode 0's feedback fitted. It is linear, and exact:

| mode | added to the feedback bit |
|---|---|
| 1 | register bit 3 |
| 2 | `1 ^ parity(i5)` |
| 3 | register bit 0 ^ register bit 3 |
| 4 | `1 ^ parity(i5)` ^ register bit 6 |

*Verified:* every complete round fits — 1,166, 1,170, 1,155 and 1,157 rounds in modes 1..4,
46,000-odd answers each, without an exception (the 160 rounds that fail are ones the parser
read as 39 queries, dropping the first). The method was checked first on mode 0: on the
2026-09-06 capture it reproduces 4,440 of 4,440 answers and recovers `3B227944` with no
contradiction. And end to end, I.G.O. 3's own library under unicorn, against
`hasplib.py`'s part with these terms and no table, encodes **every `GAMESTAT.OLD` row in
every mode to its recorded answer: 1,912 of 1,912 × 4**, no status errors.

The emulator now computes the modes (`t_mode_term()` in `dongle_photoplay.c`) and no longer
reads `GAMESTAT.OLD` at all. The terms were measured on a `6B91/24A3` part and the 2005 part
agrees; they are **the design's, not the pair's**: the same four terms with `68BB`'s key and
register reproduce the 2006 PT part's 200 recorded mode 1..4 answers and the 2007 ES part's,
200 of 200 each (I.G.O. 3's library under unicorn, the part given I.G.O. 3's bit-7 session).

## 10.11 The password burst, and I.G.O. 6 and Italy

The burst that opens a sweep (`10.2`) is the password spelled out. After the leading `46`,
each of the fifteen command bytes is a fixed table lookup on **one nibble** of the password
word `pass2 << 16 | pass1`:

```
byte   1..8    nibbles 0..7
byte   9..13   nibbles 7, 6, 5, 4, 3
byte  14       constant 1C
byte  15       nibble 1
```

The tables (`hs_burst_tab` in `dongle_photoplay.c`) were read out of I.G.O. 3's library
under unicorn — first by changing one nibble at a time from three base passwords to find
which byte follows which nibble, then filled from random passwords — and reproduce **170 of
170** bursts. The zero password is the library's special case: it sends the ramp burst
`0A 78 5A 3A 14 48 28 00 12 50 30 0C 1E 5C 3C` instead, which is why every part answers the
identity ramp. The identity variant is the same burst with byte 10 set to `50`.

I.G.O. 6's and Italy's menus carry the same library (the entry is byte-identical; found at
image offsets `0x3697B` and `0x3DE2D`, DGROUP from the startup code's `mov dx`) and send
exactly these bursts for `7477` and for `68BB`.

**The rule a part follows, as modelled:** its own password's burst (sweep or identity) or
the ramp burst — answer; a burst that decodes through the tables as some *other*
password's sweep burst — say nothing (DO held high) until it next sees one of its own;
anything else — a keyed-round preamble, a service request — no change. Over the whole real
I.G.O. 2 boot of 2026-09-06, judged as a `68BB` part, that rule never goes silent (96
identity, 6 sweep, 447 other bursts), and none of 100,000 random bursts decode as a sweep
burst.

*Verified:* I.G.O. 6's and Italy's own libraries, against a part with that rule, fail the
status probe with `7477/7D57` and pass it with `68BB/1329`, then read the record — what the
Italy dongle did on the bench.

The emulator now applies the rule for the two releases that probe, and scrambles their
record with `68BB`, the pair their dongles hold. It is not applied to the others: they only
ever send their own pair, and 2001's library hands the part its password in another form
(the `0x7DF` register, `10.3`) that this check has not been measured against.

## 10.12 The session layer is the bursts — and I.G.O. 5's buttons

I.G.O. 5 PT (MB001, original) was booted on the Atom through the passthrough with the real
2005 PT dongle: its button faces display correctly and a game starts. Under the emulator
they did not, and the emulator's I.G.O. 5 answered the whole session layer with a
synthesised rule instead of the measured identity (`synth_ident`).

Replaying that real boot into the model part and scoring every STATUS read showed why:

- the newer libraries' **second sweep is opened by a burst the part does not accept** —
  I.G.O. 5's changes the ninth byte of its own burst (`02` → `1A`; mapped on the part: of all
  64 values in that position only `02` is accepted), and the part answers the whole sweep
  with nothing, every bit 1. I.G.O. 3's library sets the mode byte to `50` instead, and the
  part answers from its identity table. Either way the second sweep differs from the first,
  which is what the anti-replay gate (`05.8` fault 4) checks — a replaying clone would fail
  it. The emulator's XOR-by-sweep-number imitated the outcome and got one read in eight of
  every I.G.O. 5 sweep wrong.
- a part answers the sweep from the table its last accepted burst selected, and a sweep
  opened by a burst it does not accept gets nothing, every step 1. Only the sweep: the rest
  of the session layer and a Microwire record read carry on as before. Silencing more than
  the sweep put `dongle error` on I.G.O. 3, whose library sends the same refused burst each
  time it logs in again, every eighth call; with the sweep alone, 30 encodes through its
  library pass and the real boots below fit better still.

With that rule, the measured identity, each pair's own sweep table and no XOR, the session
layer of three real boots agrees on all but two reads:

| boot | session reads wrong | before (pattern matcher, XOR) |
|---|---|---|
| I.G.O. 5 PT, 2005 PT dongle | 0 of 4,048 | 92 |
| I.G.O. 7 ES, 2007 ES dongle | 0 of 2,048 | 21 |
| I.G.O. 2 PT, 2002 PT dongle | 1 of 11,367 | 1 |

The one left is an idle read during the BIOS's port probe. (The record reads differ only where each real record holds its
per-unit debris.) I.G.O. 6's and Italy's probes still behave as the real part: `7477`
refused, `68BB` accepted, and the records read `DE-Version2006A` and `IT-Version08IT`.

The emulator now follows the bursts for every I.G.O. release (not 2001: `10.11`), answers
the sweep from the selected table, goes silent in the session layer after a refused burst,
silences a sweep opened by a refused burst, and serves I.G.O. 5 the measured identity; the
XOR and the synthesised identity are gone.

**I.G.O. 6, 7 and Italy.** Their rows in the emulator's release table said the session layer
runs with bit 7 clear and gave no keyed round. Their library is I.G.O. 3's and 5's — bit 7
set throughout — and their `68BB` parts compute the round like any other (`10.3`). With the
rows wrong the sweep went unrecognised, the refused `7477` burst silenced nothing, and
I.G.O. 6 settled on `7477` and put a garbled banner under "wrong dongle version". With bit 7
and the `68BB` key and register, the real I.G.O. 7 ES boot's session layer agrees on 2,048
of 2,048 reads, and I.G.O. 6 refuses `7477`, accepts `68BB` and reads its record under it,
as the hardware does.

## 10.13 The CDONGLE picture function, as far as it goes

What the part computes for a picture query, from the full tables (`10.6`) and two more
batches on the 2000 PT dongle — 1,284 queries over 321 constants under each command
(`cdongle-constants.out.gz`), and 340 with every bit of 20 random constants flipped
(`cdongle-constant-bitflips.out.gz`). Every answer fits.

**Each position is a rotate and an XOR, and `AA` and `AB` are the two directions of it.**
For name byte `c` at position `j`, with one secret byte `V_j` and one rotation `R_j`:

```
AA   S_j(c) = rotl8(c XOR V_j, R_j)          -- encrypt
AB   S_j(c) = rotr8(c, R_j) XOR V_j          -- decrypt, the inverse
```

`R_j` is 1 for one pair of positions (`j` and `j+4`) and 5 for the other three; on an 8051
those are `RL A` and `RL A` + `SWAP A` (`RR A` for `AB`). The four tables the games use
reduce to these, each reproducing its full table 2,048 of 2,048:

| case | command, constant | `R_0..R_7` | `V_0..V_7` |
|---|---|---|---|
| 0 | `AA`, `038B` | 1 5 5 5 1 5 5 5 | `7C 9B 4C EF 9B 7C AB 2F` |
| 1 | `AB`, `0A8E` | 5 1 5 5 5 1 5 5 | `58 2C AB 7C D8 4C 8B 5C` |
| 2 | `AA`, `1206` | 5 1 5 5 5 1 5 5 | `DC EC EB 3C 5C 0C CB 1C` |
| 3 | `AB`, `7343` | 1 5 5 5 1 5 5 5 | `FF 18 CF 2D 1F F8 2F 6D` |

**How the constant enters.** Split it as `H` (high byte), `m` (bits 2..7) and `s` (bits 0..1),
and number the positions by slot `t = (j + s) mod 4`, positions `j` and `j+4` sharing one:

- `s` rotates the slots: slot 3 takes the odd rotation (`R = 1`), and the rotation pair
  moves one position per step of `s`.
- `m` is added into slot 2 only, through `rotl3`: flipping one of its bits flips the rotated
  bit, with an addition's carries (`20 40 80 01 02 04`, or chains such as `E3 C3 83 …`).
- `H` adds into every slot, again under `rotl3` — the top four bits exactly, `+H` in slots
  1..3 and `-H` in slot 0 — and its low bits shift the whole sequence along the positions
  (`0100` gives `0001`'s sequence one position on, `0200` gives `0002`'s two on).
- `V_j` is not linear in the constant's bits over GF(2), nor affine mod 256.

**Open:** how the eight base values arise, and exactly how `s` and the low bits of `H`
interact — the rest of the closed form. It does not matter to any game: every Photo Play
2000 and I.G.O. 4 picture is asked for under the four pairs above, all read off the part in
full, and the emulator serves those exactly and refuses any other (where a real part would
answer). If it is ever wanted, the way to finish it is the one that settled the 1999 dongle:
read the part's microcontroller, if it is an unprotected 8051 like that one, and
disassemble the routine.
