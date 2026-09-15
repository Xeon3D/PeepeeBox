# Phase 37 — I.G.O. 3 boots, and its games stop failing their dongle check

Found 2026-09-15/16 on `IGO3\IGO3DE-GF001-DOES-NOT-BOOT_`, by running the release's own
HASP library under unicorn (`tools/dongcap/hasplib.py`) instead of reading it. Confirmed
on screen by Marcos: the menu comes up, FIND IT plays with its photographs decrypting,
SHANGAI plays through, and FIND IT and AMORE reach their points screens without error.

The fitted picture-cipher key (`AB32E970`, phase 31) was right all along. Nothing that
stopped I.G.O. 3 was in the keyed round. There were five separate faults, each hidden
behind the one before it:

| # | what the screen said | what was wrong | § |
|---|---|---|---|
| 1 | `228.250.107 dongle error` | the liveness probe arrives as `9E`, and was answered 0 | 2 |
| 2 | (the boot block decoded to noise) | keyed-round queries were never recognised on a bit-7 release | 3 |
| 3 | `IDONGLE not found` | the record read is Microwire with bit 7 set, and never reached the decoder | 4 |
| 4 | `IDONGLE not found`, later | a newer library's anti-replay gate: two sweeps may not agree | 5 |
| 5 | `07.377.xx check dongle failed` | the games check EncodeData modes 1..4 against a known-answer table | 6 |

## 1. Method: run the library, do not read it

`MENU.EXE` links the HASP library in the clear: a 3 KB wrapper at `3AE3` over a 24 KB
core at `34ED` (image-relative). `hasplib.py` loads the MZ, relocates it, and makes the
boot check's two calls — service 5, then service `0x3C` on the 20 bytes at `DS:0x50F6` —
with the parallel port answered by a Python port of `dongle_photoplay.c`'s part. Whatever
the library then does with the answers is its own code.

Two things the harness needs that are easy to miss: Borland near pointers assume
`SS == DS`, so the stack has to live in DGROUP; and the library seeds a generator from
`time()`, which is Borland's getdate/gettime/getdate loop and spins for ever on a clock
that does not move.

The games are encrypted on disk (entropy 7.99, a wrapper stub at the entry point), so for
them the device now dumps conventional memory to `ppmem-<CS>.bin` the first time a
program drives the port, when `PEEPEEBOX_LPT_TRACE` is set. `SHANGHAI.EXE`'s library is
the menu's byte for byte, relocated nine bytes on; the dump is what §6 ran against.

## 2. `dongle error` was the liveness probe

The failing status was `-8` from `HaspEncodeData`, and the library gives up on exactly one
read: the liveness probe, which it sends as `9E` then `9C` on this release — bit 7 set,
like the rest of its session layer. The part matched the probe on the raw byte `1E`, so
`9E` fell through to the identity signature, whose address 15 is clear: 0, where a real
part answers 1. The sweep matcher already ignored bit 7; the probe matcher now does too.

## 3. The keyed round's queries, found at the read

With the probe answered, the library ran two keyed rounds — the first I.G.O. 3 had ever
issued — and they were answered as ramp noise, because query detection was off for this
release: its session payloads move bit 4, so the bit-4 edge that finds I.G.O. 2's queries
fires on ordinary payload changes here. The framing is I.G.O. 2's all the same —
payload, payload|0x10, payload, one STATUS read — so a query is recognised at the read by
the three distinct writes before it. With that, the rounds are 40 and 40 consultations
and `DS:0x50F6` decodes to `c:/foto/gamestat.old`.

## 4. The record read is Microwire with bit 7 set

The next screen was `IDONGLE not found` — bit `0x40` of the menu's dongle mask, set when
the banner built from the record does not contain `Version 2003`. The part had never
served a word: I.G.O. 3 clocks its record read with bit 7 set (`9E/BE` a clock with DI
low, `DE/FE` with DI high, `9C` dropping CS), and the device kept every bit-7 write away
from the Microwire decoder, because the session payloads would otherwise clock it. Those
frames all carry bits 2..4, which the session payloads mostly do not, and where one does
the STATUS read after every session write resets the decoder long before an instruction
could complete; so bit-7 writes with bits 2..4 set now reach it. The banner reads
`Version 2003 (DE)`.

## 5. The anti-replay gate

`IDONGLE` came back on later runs, and not from anything on the wire. I.G.O. 3's HASP
library is a newer build than I.G.O. 2's (whose `MENU.EXE` has none of this). Every five to
fourteen calls it logs in again and sweeps twice — the same 64 questions, seed 100 both
times — folds each sweep into eight bytes and, if **any** byte of the second equals the
same byte of the first, sets a flag (`DS:0x4C50`, core `0x20EA`). Five calls later every
service is refused with status 7: IsHasp answers 0, ReadBlock fails, the banner is empty.

A real part evidently never answers a sweep the same way twice; this one replayed the
single reply captured off the `68BB/1329` dongle. The sweep routine itself rejects only an
all-zero or all-one fold (core `0x257B`), and nothing else reads the answers, so every
byte of sweep *k* is now XORed with *k* mod 255: any two sweeps within 255 differ in all
eight bytes, and the first is still exactly the capture. Only the bit-7 releases do this.

`docs/research-v2/09.1` recorded "the gate at `0x20BB` is not this failure" because forcing
it changed nothing on screen. It changed nothing *yet* — the penalty lands five calls
later.

## 6. `07.377.xx check dongle failed` — the known-answer table

With the menu up, every game failed a few minutes in: mid-game in SHANGAI, at the points
screen in FIND IT and AMORE, all logged in `\FN_SYS\DFU\TRANS\ERROR.LOG` as
`07.377.xx = check dongle failed`.

`\FOTO\GAMESTAT.OLD` — the file the boot check decrypts the name of — poses as statistics.
It is a dBASE III table (header dated 2002-11-13, 1,922 rows of 51 bytes; rows 0–1911 are
byte-identical on the DE, IT, NL and ZA images, and each install rewrites the last few)
with five 10-byte fields, each XORed with a fixed mask: a random challenge `R`, and for
each of `HaspEncodeData`'s modes `p1` = 1..4 the block `F_k` whose encoding in that mode
is `R`, recorded off a real `6B91/24A3` part. At start-up the game's framework (`3880:0137`
in SHANGAI) decrypts the path, and one start in ten encodes a random row's `F_k` in the
mode of the day and compares it with `R`. A mismatch sets `DS:0x4B6B` with a timestamp;
the in-game check reports it once enough time has passed, and the points screen reports
it outright. The menu walks the whole table in the current mode as well.

The library announces the mode on the wire. After the `84/A4` run that opens a round, mode
0 sends `CE` and queries; mode *k* sends `CE CE CC 96 9C 9C CC CE`, clocks bit 4 *k* times
on payload `CA` with no read, sends `CE`, and queries. The part had never modelled that,
so every mode came out as mode 0. What a real part does in modes 1..4 is **not known**:
fitting the table against our shift register — encode or decode, either direction, any
32-bit key, every starting register — finds nothing (`docs/research-v2/09.11`).

What the table does give is the answer to every question the games ask in those modes.
EncodeData is the keyed round twice around keyless A and B rounds, and B is affine over
GF(2), so each row yields both rounds' (input, output) pairs with one 32×32 solve — 15,376
pairs from the whole table, identical in the device's C and in Python. During a mode-*k*
round the host puts five bits of its 32-bit register on the wire each query; the part
follows every table input still consistent with the queries so far and, once one is
left, answers so the round produces that input's recorded output — decisions 8..39 are
forced by the output (`docs/research-v2/05.4`), the first seven are free and get the
shift register's answer. A round whose input is not in the table is random data, which
nothing compares, and gets the shift register.

The mode is latched on the command byte that opens the round, not counted up to the
first query: that query's own payload can be `CA` too, and counting it once mislabelled
a mode-1 round as mode 2.

Verified before it went near the rig: SHANGAI's own check, run in unicorn from its
decrypted memory with DOS emulated around it, verifies 200 random rows in each of the four
modes, and sixty runs of its start-up check leave the flag clear, where the shift register
alone sets it within four. On the rig: one menu session answered 1,586 mode-2 rounds from
the table and none from outside it.

## 7. What is still not known

- **The part's real function in modes 1..4.** The table covers every question the shipped
  software asks, which is all an emulator needs; it is not the function. A capture of a
  `6B91/24A3` part in those modes would settle it.
- **The session answers of a `6B91/24A3` part.** Still the `68BB/1329` capture, now with the
  sweep varied. It boots, which says the library accepts it, not that it is what the part
  says.
- **The real sweep reply's variation.** §5's XOR satisfies the gate; how a real part
  varies is unmeasured.

## 8. Where it lives

- `src/device/dongle_photoplay.c` — the probe match, `t_read_query()`, the bit-7
  Microwire gate, `hs_sweep_bit()`'s per-sweep XOR, and the known-answer table
  (`kat_load()`, `t_answer()`).
- `src/photoplay_ident.c` — `photoplay_read_file()`, which the table is read with.
- `tools/dongcap/hasplib.py` — the harness, with the Python part it answers from.
- Diagnostics, all behind `PEEPEEBOX_LPT_TRACE`: ten-frame call stacks in the trace, a
  settable line cap (`PEEPEEBOX_LPT_TRACE=<n>`), and the `ppmem-<CS>.bin` memory dumps.
