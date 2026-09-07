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
