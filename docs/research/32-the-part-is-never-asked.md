# Phase 32 — the software part is right, and the guest never asks it

Marcos ran the staged build: I.G.O. 2's FIND IT threw `error 60.344.42 — Invalid Handle
for PCX-File`, the photographs came up garbled with only the difference overlay drawing,
and I.G.O. 3 still did not boot.

None of that is Phase 31 going wrong. **The keyed round was never consulted once.**

## 1. The build was inert

`t_data()` logs on its first answered query and never on any later one. That line is
absent from the run's `86box.log`, which also shows the key loading correctly:

```
PP: picture cipher key 3B227944, register 7DF (Version 2002)
PP: HD2001 read word 08 -> 2DF9
...
```

`t_pending` is set only in `t_data()`, and `pp_read_status()` diverges from its previous
behaviour only when `t_pending` is set. No first-query line therefore means no query, which
means the status path behaved exactly as it did before Phase 31. **The errors Marcos saw
are what that image already did** — the buffer goes in, the same buffer comes back, and the
game draws noise, which is `notes/HANDOFF2001.md` § 19.2 verbatim.

So the question is not why the answers are wrong. It is why the guest stops before asking.

## 2. It does not stop in HaspCode, on the wire

`HANDOFF2001.md` § 20.5 left the decode blocked on `0x2B427` → `0x2ACD5`, model 4's
HaspCode, and asked whether that reaches the port. The passthrough capture answers it: of
the 19,638 wire events before the first keyed round, **not one is oracle-framed**. Every
status read in that stretch follows a write with bit 7 clear — Microwire and detection
traffic. Classified:

| pre-round status reads | framing |
|---|---|
| 4,308 | plain reads after a bit-7-clear write |
| **0** | the `pay, pay\|0x10, pay, read` of the oracle |

A real part gets past the guard without ever being asked an oracle question. So whatever
satisfies it is answered during the identity and record phase — which is the phase this
device synthesises.

## 3. The identity signature was wrong, and now it is measured

`pp_read_status()` carried this admission:

> This signature is synthesised, not measured: what a real 2001 unit puts on DO during
> that ramp has never been seen.

It has now been seen. The library walks `00, 02 … 7E`, one STATUS read per step, and
accumulates `acc = 0x7E XOR (XOR of addr<<1 where DO came back set)`. The capture contains
**96 of those ramps, byte-identical every time**, from a real `68BB/1329` part:

```
set at 1 3 9 11 17 18 19 22 23 25 26 27 30 31 32 33 34 35 36 37 38 39 41 43
       48 49 50 51 52 53 54 55 57 58 59 62 63          -- 37 of 64
acc  = 0x18
```

The device aimed at `0x1C`. Scored against those 96 ramps:

| signature | ramp reads reproduced |
|---|---|
| synthesised, `((addr % 3) == 0) != (addr == 14)` | 2,880 of 6,144 — chance |
| **measured, `HD_SIGNATURE = 0xCEFF0AFFCECE0A0A`** | **6,144 of 6,144** |

`0x1C` was chosen precisely because its handler sets the memory size unconditionally while
`0x18`'s is conditional. A real part evidently satisfies that condition, and picking the
unconditional branch is what left the library in a state where it reads the record happily
and never runs a decode.

The device now answers the ramp exactly as the part does.

## 4. What this does not yet claim

That the signature is now right is measured. That it is *sufficient* to make the decode run
is not — `0x18`'s handler is conditional and what it tests has not been read out of the
binary. The record read works today under `0x1C`, so the thing to watch on the next run is
the banner and the record: if those break, this is the change that did it.

Two further cautions:

- The signature is from a `68BB/1329` part. Whether `7477/7D57` and `6B91/24A3` units
  answer the same ramp identically is unknown; it looks like a property of the model rather
  than the key, but nothing here shows that.
- Replaying the whole capture through the device's Microwire decoder never reaches a read
  instruction, over all 12,856 status reads. Once the guest is following a real part's
  identity it addresses the part in a way this decoder does not follow, so `HD_ABITS` and
  the instruction framing are the next thing the same capture can settle.

## 5. The part is 64 words, and that is why § 3 broke the banner

Correcting the identity made I.G.O. 2 report `wrong dongle version` with a garbled banner,
which is the risk § 4 named. The capture says why, and it is not a guess either.

Splitting the wire on CS (DATA bit 1), clocking on SK (bit 5) and reading DI (bit 6) gives
189 framed sessions, and only two shapes:

```
 9 clocks   1 00 000000                        start, opcode 00, six address bits
25 clocks   1 10 001000 0011110011101011       start, READ, address 08, sixteen data bits
25 clocks   1 10 001001 0001101110111010       ...     address 09
25 clocks   1 10 001010 0000011111010000       ...     address 0A
```

**Six address bits, not eight.** The part holds 64 words. That also settles the record: the
library adds `HD_START` to the caller's word and asks for 56, and 8 + 56 is exactly 64 —
the record is the whole of the part above word 8, with nothing spare.

The device advertised 256 words because `0x1C`'s handler sets that size unconditionally.
Once the identity answer was the real `0x18`, the guest clocked six address bits into a
decoder still expecting eight, and every word came back shifted — the garbled banner.

Replaying the capture through the decoder settles it:

| address bits | read instructions decoded |
|---|---|
| 8 — what shipped | **0**, over all 12,856 status reads |
| **6 — measured** | **93**, at addresses 8, 9, 10, 11 … |

`HD_WORDS` is now 64 and `HD_ABITS` 6. The two changes belong together: the measured
identity is only correct alongside the geometry it selects.

Two of the decoded words can be checked against this device directly. The capture is a PT
unit and the failing rig is BE, so most words differ, but `09 -> 1BBA` and `0A -> 07D0` are
what our own log prints for those addresses — the same values from the same layout.

## 6. Where that leaves Phase 31

Untouched. The software part reproduces all 46,036 rounds a real dongle answered, and the
device's edge detection answers all 4,720 consultations in the capture correctly. Both
still hold — they are simply downstream of a gate that has not opened yet.

## 7. Where the gap actually is now

With `0b6efac` on the rig the banner reads correctly again and 32 words decode, so the
geometry is right. The cipher line still does not appear: the identity was necessary and
is not sufficient.

The capture localises what is left. Seeding the model with the part's *own* record — so no
content difference can account for anything — and replaying the whole wire through the
device's logic:

| phase | status reads | agreeing |
|---|---|---|
| record, over the Microwire framing | 1,488 | **1,488 — exact** |
| the keyed round | 4,722 | 4,722, answered by Phase 31 |
| everything else | 6,646 | 6,347 — **299 wrong** |

So two of the three layers on this wire now match a real part exactly, and the gap is
entirely in the third.

That third layer is not Microwire and not the oracle. It is the bit-0-clocked command byte
with bit 7 **clear** — `5A 5B 5A`, `3A 3B 3A` and so on — the HASP session and detection
traffic, which is also where the identity ramp lives. The device answers every read in that
phase from the ramp signature or the `ready` flag, and for 299 of them a real part says
something else. The wrong answers cluster on particular bytes (`1E` 59 times, `0C` 30, `78`
24, `2E` and `70` and `1C` 18 each), so it is structured traffic being answered by a rule
that does not know about it, not noise.

Modelling that layer is the next step, and the capture already contains every byte of it,
so it needs no hardware.

## 8. Three protocols, one pair of wires

Worth stating plainly, because the naming has been confusing:

| layer | framing | what it carries | state |
|---|---|---|---|
| session / detection | command bytes clocked on DATA bit 0, bit 7 clear | identity, liveness, whatever gates the decode | **299 of 6,646 reads wrong** |
| memory | Microwire — CS bit 1, SK bit 5, DI bit 6, 6 address bits, 64 words | the 112-byte record: banner, dwords, database keys | exact |
| transform | payload clocked on DATA bit 4, bit 7 set | the picture cipher's keyed round | exact |

All three answer on STATUS bit 5, which is why they were hard to tell apart. The "HASP4
key" of Phase 31 is the third row only; the second row is a memory read that no key helps
with, and the first is what is still blocking.

## 9. The session layer, modelled — 12,856 of 12,856

§ 7 left 299 reads unexplained. Taking that layer apart, it does three things and the
device answered all of them with the ramp rule:

| transaction | occurrences in one boot | what the part does |
|---|---|---|
| the identity ramp, `00,02…7E` | 96 | the measured signature — already right |
| a **64-step sweep** of a fixed byte sequence | 6 | answers the same 64 bits every time |
| a **2-step liveness probe**, `1E` then `1C` | 59 | answers **1** then **0** |

The sweep's 64 written bytes are identical on all six occurrences and so is the reply:
`F5 7A 37 E7 8F 8F BD DA`. `dongcap` calls those bits "read and discarded", which was true
of dongcap and evidently not of the game.

The liveness probe is the interesting one. Address 15 really is clear in the measured
signature, so the ramp rule answers `0` to `1E` — and `0` to `1C` as well. **That is a line
stuck at one level, which is exactly what that gate exists to reject**, and the guest ran
it 59 times in one boot.

One more thing had to change to make any of it work: the session state has to be advanced
by the *status reads*, not by the writes. Microwire traffic is bit-7-clear too, so a
tracker driven off DATA alone drifts through every record read.

With the ramp, the sweep and the liveness probe modelled, and the part's own record loaded
so content cannot flatter the result:

| phase | status reads | agreeing |
|---|---|---|
| identity ramp | 6,144 | 6,144 |
| the 64-step sweep | 384 | 384 |
| liveness | 59 | 59 |
| record | 1,488 | 1,488 |
| the keyed round | 4,722 | 4,722 |
| everything else | 59 | 59 |
| **total** | **12,856** | **12,856 — 100.00%** |

Every read a real part answered in that capture, this device now answers the same way.

**What that does not prove.** The capture is one boot of one game on one `68BB/1329` part.
The sweep reply is measured, not derived, so if those 64 bits are a function of the
password then `7477/7D57` and `6B91/24A3` parts answer differently and only the framing
carries over. The sweep's *written* bytes are the guest's, so if they differ per release
the matcher simply will not fire and the old fallback stands — wrong, but no worse than
before.
