# Handoff — the Photo Play 2001 generation

Working note for whoever picks this up. Untracked on purpose. Started 2026-08-30,
immediately after the 2000 generation was finished (see `HANDOFF.md` and
`docs/research/15` and `16`).

**Status: the transport is solved and implemented (§ 16) — HDONGLE is a Microwire
serial EEPROM — and the guest is past `HDONGLE not found` and into the games. The
record's content was wrong on the first attempt and is corrected in § 17; awaiting
a second look on screen. Read §§ 16-17 first; they correct §§ 11 and 13-15, which
had the right measurements and the wrong story.** Sections 1-2 are
read off the binaries statically, section 3 is measured off the wire. Where a thing
is measured it says so; where it is not yet known it says that too. Do not promote
an open question into a fact without checking it — the 2000 work lost a lot of
time to exactly that.

---

## 1. What the goal is

Make the five untouched 2001 images in `PP2001/` run without a physical dongle,
the way 1999 and 2000 now do. The device to extend is
`src/device/dongle_photoplay.c`.

```
PP2001/
  Photo Play 2001 AT A3735    Photo Play 2001 DE B4821
  Photo Play 2001 ES EB711    Photo Play 2001 IT A3735
  Photo Play 2001 NL F2311
```

All five are 1,655,635,968 bytes — the same geometry the existing profile derives
(63 spt, 16 hpc, cylinders = size/1008). **These are ground truth: never point a
rig at `PP2001/` itself, always at a copy.**

The folder holds nothing but the five images, matching `PP2000/`, and `*.img` in
`.gitignore` covers them.

Briefly, on 2026-08-30, each folder was instead a complete rig — `PeepeeBox.exe`,
`roms/`, `nvr/`, a config and a stale `86box.log`. Marcos has since stripped them
back to bare images. Worth knowing only because those logs, if any resurface,
were from a **1999 AT** run out of build `06-territories-releases-titlebar`
(`image identified as Photo Play 99 AT`) and are not 2001 evidence.

Binaries extracted from the **DE** image for offline work:
`PeepeeBox-handoff/ex2001/` — `MENU.EXE`, `MAIN.SET` and all 36 game EXEs.

---

## 2. Established facts

Every item here was read off the binaries, and the file offsets are given so it
can be re-checked rather than believed.

### 2.1 The banner

`/FOTO/SETTINGS/MAIN.SET` decrypts (Docs/08 cipher, key `0x00016295`) to a 52-key
table whose `Version` is:

| image | Version |
|---|---|
| AT A3735 | `Version 2001 (AT)` |
| DE B4821 | `Version 2001 (DE)` |
| ES EB711 | `Version 2001 (ES)` |
| IT A3735 | `Version 2001 (IT)` |
| NL F2311 | `Version 2001 (NL)` |

17 characters, exactly like the 2000 banners. **That matters** — it is the length
that broke the record layout for 2000 (Docs/16). The current unconditional
layout (dwords at 16, banner over the top) already handles it, but confirm rather
than assume.

`scratchpad/setread.py` decrypts and parses a `.SET` in Python.

### 2.2 The status word gained a bit

`MENU.EXE` (DE) builds a status byte and maps it to one of six messages through a
jump table at `cs:0x1AC5`. File `0x0BB8D`..`0x0BBC4`:

| bit | message | file offset of the string |
|---|---|---|
| `0x01` | `DS1982 not found` | `0x3B2A9` |
| `0x02` | `DS1425 not found` | `0x3B298` |
| `0x04` | `dongle not found` | `0x3B275` |
| `0x08` | `CDONGLE not found` | `0x3B286` |
| `0x10` | **`HDONGLE not found`** | `0x3B2BA` |

plus a sixth code for `wrong dongle version` (`0x3B2E2`), set by a separate string
compare at `0x0BB6A`.

`HDONGLE` appears in neither the 1999 nor the 2000 binaries. **Marcos's steer,
which the code agrees with: the letter changes every year, and it is never an
Aladdin HASP.** So `HDONGLE` is 2001's name for what 2000 called `CDONGLE`. There
are no `HASP`, `Aladdin`, `Sentinel` or `Rainbow` strings anywhere in `MENU.EXE`.

### 2.3 Only two bits are reachable

The check function is at `0x301E:0x08DD` = file **`0x364BD`**. Walking it end to
end, it contains exactly two `or ax,imm` sites:

- `0x364EE` → `or ax,0x10` — the HDONGLE bit
- `0x3652A` and `0x36574` → `or ax,0x01` — the DS1982 bit

`0x02`, `0x04` and `0x08` are never set. Same situation as 2000, where Docs/15
found only the iButton and CDONGLE bits live.

**So there are two things to satisfy: DS1982 (already emulated) and HDONGLE.**

### 2.4 HDONGLE is a record read plus a strstr

At `0x364D1`:

```
push 0x8e64          ; a buffer in DGROUP
call 0x36209         ; fill it from the dongle
push 0x4a8f          ; DS:0x4A8F = "Version 2001"  (file 0x3F95F)
push 0x8e64
call 0x0:0x431D      ; strstr(buffer, needle)
or ax,ax / jne       ; ax == 0  ->  or ax,0x10
```

Identical in shape to the 2000 CDONGLE check: fill a buffer, `strstr` it for the
version string, set the bit when it is not found. DGROUP is at file `0x3AED0`
(four bytes before the `Borland C++ - Copyright` banner).

There is also a **skip flag** at `DS:0x4A8D`, tested at `0x364C7` — non-zero
means the whole check is skipped.

### 2.5 The API is HASP-COMPATIBLE. The device is NOT identified

**Read this before using the word HASP about 2001.** What is established is the
*calling convention*, nothing about the hardware:

- the call really is the published HASP signature — verified by counting the
  pushes at `0x36257`: four far out-pointers plus five words, `add sp,0x1A`;
- the service numbers fit the same convention (`1` presence, `0x32` block read).

What is **not** established, and must not be assumed:

- `MENU.EXE` and the game EXEs contain **no vendor string of any kind** — not
  HASP, Aladdin, Sentinel, Rainbow, Hardlock, Marx, CryptoBox, Eutron, SmartKey,
  Matrix, Glenco, Keylok or Wibu. The only one present is `funworld`;
- the struct magic at `+0x08` is `6C 68 73 68`, `"lhsh"` — not `hasp`, and not
  any vendor mark that has been recognised.

Docs/13 exists because 1999 was called a HASP through five documents and was an
AT89C2051 with an I2C EEPROM. Marcos's standing steer is that none of these are
Aladdin parts, and nothing here contradicts him.

**A better hypothesis, held as a hypothesis:** the 2008 binary carries eleven
dongle types (`dongle`, `CDONGLE`, `HDONGLE`, `GDONGLE`, `IDONGLE`, `KDONGLE`,
`MDONGLE`, `MBDONGLE`, `NDONGLE`, `ODONGLE`, `NG-DONGLE`), which reads like a
generic multi-vendor wrapper with each build configured for one supplier. That
would explain the 11-entry device-type table in § 10.5, why most of its entries
only return errors, and the per-year letter. On that reading the HASP-style API
is the wrapper's convention, adopted because it was the industry-standard shape,
and `H` is simply the 2001 supplier.

What would settle it: compare the measured wire protocol against a genuine HASP's
documented one. If it does not match, the question closes the way Docs/13 closed
1999's.

### 2.5.1 The passwords

This is where 2001 departs from everything before it. `0x36209` fills the buffer
by calling `0x353D:0x000E` (file `0x3ADDE`) twice, with the classic nine-argument
HASP signature:

```c
hasp(service, seedcode, lptnum, pass1, pass2, int *p1, int *p2, int *p3, int *p4);
```

| call | file | service | arguments |
|---|---|---|---|
| presence | `0x36257` | `1` | seed 0, lpt 0, pass `0x7477`, `0x7D57`; success is `p1 != 0` |
| block read | `0x362C9` | `0x32` | p1 = 0 (start), p2 = `0x38` (**56 words**), p3:p4 = far pointer to the destination |

After the read, `0x362DF`..`0x3630D` unpacks each word into two bytes **high byte
first** (`idiv 0x100`, `al` then `dl`). 56 words = 112 bytes, and that byte buffer
is what gets `strstr`'d.

The passwords `0x7477` / `0x7D57` are **identical in all five images and in all 36
game EXEs** (checked by byte pattern `C7 46 FE 77 74` / `C7 46 FC 57 7D`).

### 2.6 The library's own layout

`0x353D:0x000E` marshals the nine arguments into a struct and calls
`0x30B9:0x472D` (file `0x3ACBD`), which:

- validates a signature in the struct — `es:[bx+0x0A] == 0x6873` and
  `es:[bx+0x08] == 0x686C` (the bytes spell `lhsh`); failure returns `0xFFF7`;
- on first use zeroes two DGROUP buffers, `DS:0x8EAA` (256 bytes) and `DS:0x8EA2`
  (8 bytes);
- then calls the dispatcher at file `0x399D4`.

The dispatcher has a **service table at file `0x39FC4`** listing the supported
services: `1, 2, 3, 4, 5, 6, 8, 0x32, 0x33, 0x3C, 0x3D, 0x67`, with a parallel
array of handler offsets (segment `0x30B9`, file base `0x36590`).

Service 1's handler is at offset `0x3897` = file `0x39E27`. It does **no I/O** —
it reports presence from a cached field, `es:[bx+0x1A]` of the state struct.

### 2.7 The games match the menu

All 36 game EXEs on the DE image contain `HDONGLE`, the same two passwords, and
`Version 2001`. None contains `PDONGLE` (2000's name for the same check). So the
games use the same mechanism as the menu, as in every previous generation.

---

## 3. It already boots, and the wire has been measured

**The 2001 DE image runs on the current build with no changes.** Rig
`PeepeeBox-builds/12-2001-recon`, staged from a *copy* of the image. What the
existing device already gets right, from the log:

```
PP: image identified as IGO 1 DE
PP: Photo Play dongle attached, banner "Version 2001 (DE)" (17 chars), dwords at +10
IB: DS1982 iButton at I/O 268 ... READ ROM / SKIP ROM / READ MEMORY x2
```

So the release list already maps 2001 to `IGO 1`, the 17-character banner is laid
out correctly by the unconditional record layout from Docs/16, and **the DS1982
half — status bit `0x01` — is already being answered.** That leaves bit `0x10`,
HDONGLE, exactly as § 2.3 predicted.

Full trace preserved: `PeepeeBox-handoff/ex2001/trace2001-de-first-boot.log`
(`PEEPEEBOX_LPT_TRACE=1`). 323 parallel-port accesses, then the guest gives up.

### The 2001 wire protocol, as measured

Three phases. This is read off the trace, not inferred from the binary.

**Phase 1 — presence probe, run twice.**

```
WFF  C00  W5A  r5A  W00  WA5  rA5  W00
```

Write `0x5A` to DATA and read it back, then `0xA5`. A data-line echo test. This is
the same `5A`/`A5` probe `Docs/09` recorded on IGO8 and `Docs/15` identified as
the 2000 library's. **The current device already satisfies it** — it returns the
write latch, so both readbacks match.

**Phase 2 — sixteen clocked values.** After `C08`, `C0C`, each value is written
three times as `V`, `V|1`, `V`, so **DATA bit 0 is the clock** and the payload is
bits 1..7:

```
raw   46 0A 78 5A 3A 14 48 28 00 12 50 30 0C 1E 5C 3C
>>1   23 05 3C 2D 1D 0A 24 14 00 09 28 18 06 0F 2E 1E
```

Sixteen six-bit values. What they encode is **not established**. They are constant
across the two boots seen so far. The obvious candidate is the password pair
`0x7477`/`0x7D57` (32 bits) in some encoding, but that is a guess and has not been
checked — do not write it down as fact until it is.

**Phase 3 — a 64-step read.** Then the host walks `W00 S00 W02 S00 W04 S00 …
W7E S00`: sixty-four writes stepping by 2 — i.e. an address `0..63` shifted left
one, the clock bit again — each followed by **one STATUS read**. Sixty-four bits.

Every status read returns `00`, because the device does not recognise this framing
and drives nothing. That is why the guest stops: it is reading a line nobody
drives, exactly the situation the 2000 generation started from (`Docs/15`).

### What this settles, and what it does not

- The transport is **not** the 2000 nibble protocol. 2000 framed bytes as
  `F? C? F? 9? 8? DF` with a `D0`-trailed command; 2001 clocks on DATA bit 0 and
  reads one bit per address from STATUS. Different shape, measured both ways.
- It is **not** the 1999 protocol either (no STROBE framing, no BUSY handshake).
- The probe is shared with 2000/2008, so at least that much of the family carries.
- **Unknown: which STATUS bit carries the data, and what the 64 bits must be.**
  Nothing can be concluded from the current trace because the device returns
  `0x00` for every read — there is no signal in it to read.

## 4. The acceptance condition, and what STATUS does not do

### Confirmed on screen

The 2001 DE image boots to a red `COPY PROTECTION / HDONGLE not found`. Bit `0x10`
and nothing else — every other half of the check is already satisfied by the
existing device.

### Driving STATUS changes nothing

A research toggle was added for this (§ 7). With it on, **all 65 status reads
returned `FF` instead of `00`, and the guest's behaviour was byte-for-byte
identical** — same 323 accesses, same sequence, same failure.

That is a useful negative. The guest does **not** branch on STATUS during the
exchange. It collects its 64 bits, evaluates them afterwards, and gives up without
retrying, so an all-zero and an all-one answer fail identically. Sweeping STATUS
values blindly will therefore teach you nothing; the answer has to be *correct*,
not merely non-zero.

### What has to be true

The detection routine is `0x37605`. It makes three calls and then one comparison:

```
call 0x37975      ; phase 1
call 0x37B80      ; phase 2
call 0x37E1D      ; phase 3  -> al
mov  ah,0 ; [bp-2] = ax
cmp  WORD PTR [bp-2], 0x7E
jne  <return 1>   ; anything else: not detected
     <return 0>   ; 0x7E: detected
```

Its caller at `0x36ABD` turns that into the device-type field: return 0 sets
`es:[bx+0x1A] = 7`, non-zero sets it to `0`. Service 1 (`IsHasp`) then reports
presence straight from that field, which is why service 1 does no I/O.

**So the whole of HDONGLE reduces to: make `0x37E1D` return `0x7E`.**

`0x37E1D` decoded in full:

```c
uint8_t acc = 0x7E;                       /* [bp-3], seeded with the answer */
for (uint16_t i = 0; i < 0x40; i++) {     /* [bp-2] */
    write_address(i << 1);                /* 0x3767F -- the observed W00..W7E ramp */
    uint8_t b = read();                   /* 0x377B3 */
    if (bit5(b))                          /* 0x3786A -> ds:0x4AEC -> 0x37FCD */
        acc ^= (uint8_t)(i << 1);
}
return acc;                               /* must be 0x7E */
```

`0x3786A` calls through the runtime pointer at `ds:0x4AEC`, which `0x3766F`
installs as `0x30B9:0x1A3D` (file `0x37FCD`), and that routine is three
instructions: `and ax,0x20 ; sar ax,5` — **it returns bit 5** of its argument.

### 0x7E is the FAILURE value — confirmed on the machine

`XOR` of `(i << 1)` over all `i` in `0..63` is zero, so **any constant answer**,
all ones or all zeros, leaves `acc` at exactly `0x7E`. Follow that through:

```
0x37E1D returns 0x7E  ->  0x37605 returns 0  ->  es:[bx+0x1A] = 7
0x37E1D returns else  ->  0x37605 returns 1  ->  es:[bx+0x1A] = 0
```

and then look at what those two device types do, at the dispatch in § 6.1:

| type | handler | what it does |
|---|---|---|
| 7 | `0x3A1AC` | **always returns an error** — `0xFFE4` (-28) or `0xFFFD` (-3) |
| 0 | `0x3A14E` | clears both result words — **no error** |

So `0x7E` selects the type whose every service call fails. It is the **failure**
value, not the success value, and a constant STATUS lands on it exactly. That is
why forcing `FF` behaved identically to `00`: both passed *this* arithmetic and
both were then rejected.

**Verified empirically.** Making the device set bit 5 at exactly one non-zero
address (so `acc = 0x7E ^ 0x04 = 0x7A`) changed the run completely:

```
323 parallel-port accesses  ->  12,531
 65 status reads            ->     193
```

The guest sails past detection and starts a much longer conversation. `HDONGLE
not found` is still on screen, so the *next* gate now fails — but it is a
different gate, and that is the first forward motion on 2001.

Trace: `PeepeeBox-handoff/ex2001/trace2001-de-past-detection.log`.

### The phase that detection was hiding

After the 64-step ramp the guest runs **128 groups** of:

```
write V, thirty-two times      (V always even -- bit 0 is still the clock)
read STATUS, once
```

`V >> 1` is a six-bit address again, but the order is pseudo-random rather than a
ramp: `35 2B 13 01 0C 37 1E 17 1D 39 29 06 32 38 3A 17 12 3C 1B 11 …`. Repeats
occur, so it is not a permutation.

This is the real data exchange, and **the device has to answer each of those 128
reads correctly**. Nothing is known yet about what the right bits are. The
32-times repetition is presumably a settle or clock-stretch; only the single
STATUS read after it is sampled.

### The 64-bit response, and what rejects it

`0x37EA7` is the routine behind the pseudo-random phase, and it is fully decoded:

```c
x = seed;                                   /* [bp+0xa] */
for (si = 0; si < 0x40; si++) {             /* 64 bits */
    x = x * 0x1989 + 5;                     /* LCG */
    bit = read_bit(x >> 8, 0x7E);           /* 0x37878 -- address is x>>8 */
    w = &buf[(si >> 4) * 2];                /* four 16-bit words, 16 bits each */
    *w = (*w << 1) + bit;                   /* MSB first */
}
for (si = 0; si < 4; si++)
    byteswap(&buf[si]);                     /* 0x37FE6 swaps the two bytes */

if (buf[0..3] all == 0x0000) return 0;
if (buf[0..3] all == 0xFFFF) return 0;
return 1;
```

So the address order is an LCG — `x = x*0x1989 + 5`, address `x >> 8` — which is
exactly the pseudo-random six-bit sequence seen on the wire, and it explains the
repeats (an LCG over a 6-bit output is not a permutation).

**The test at the end rejects a constant line in both directions**: all-zero and
all-one both return 0, anything else returns 1. It is a "is this line actually
doing something" check. Whichever way round the caller reads that, the device
cannot satisfy it by holding STATUS at a fixed level — the same lesson as `0x7E`,
in a second place.

### Both gates are liveness checks, and they share a polarity

The top level at `0x368F0`..`0x36940` wires both together:

```
call 0x37EA7 ; or ax,ax ; jne <continue>    ; must return 1 (a varied line)
  else   es:[bx+0x1A] = 7                   ; type 7 = every service errors
call 0x37605 ; or ax,ax ; je  <fail>        ; must return non-zero (acc != 0x7E)
```

Neither checks a *value*. Both reject a line that is not doing anything.

### The symmetry trap — worth knowing before trying the obvious thing

The natural first answer is to echo one bit of the address. **It cannot work.**
Any single-address-bit selector picks a set that is symmetric in the address bits,
and the XOR of `(i << 1)` over such a set is zero, so the accumulator lands right
back on `0x7E`:

```
echo address bit 0..5  ->  ramp XOR = 00 in every case  ->  acc = 0x7E  ->  FAIL
```

Balanced pseudo-random patterns fail the same way — `popcount(i) & 1` and
`(i * 0x9E >> 3) & 1` both give XOR `00`. The pattern has to be *asymmetric*.
`addr % 3 == 0` works: XOR `0x7E`, so `acc = 0`, and about a third of the bits set
so the second gate sees variation.

Measured, on the DE image:

| answer | accesses | status reads |
|---|---|---|
| constant `00` or `FF` | 323 | 65 |
| bit 5 at one address | 12,531 | 193 (65 + 2 x 64) |
| `addr % 3 == 0` | 8,635 | **321** (65 + 4 x 64) |

More passes means it is getting further, not that it is happy: it still ends on a
ramp and stops. Trace: `ex2001/trace2001-de-mod3.log`.

### Passing both gates is necessary but NOT sufficient

Confirmed on screen: with `addr % 3` clearing both liveness gates, the machine
still shows `HDONGLE not found`. So the gates only decide whether the library
will talk to the device at all. The bit is set by the *content* test in § 2.4 —
the 112-byte buffer has to contain `"Version 2001"` — and that is untouched by
anything done so far.

### Where the 112 bytes come from — and a correction

There are **two** service dispatch tables, and it is easy to read the wrong one.

| table | at | what it is |
|---|---|---|
| `cs:0x383B` (file `0x39DCB`) | used by `0x399D4` | the **real** service handlers |
| `cs:0x3A34` (file `0x39FC4`) | used by the tail | a second-level table whose entries are mostly bare jumps to the common tail |

An earlier pass through this document read the second one and concluded that
service `0x32` "fetches nothing". That was wrong — it was reading the wrong
table. The real handlers are:

| service | offset | file |
|---|---|---|
| `01` | `0x3488` | `0x39A18` |
| `02` | `0x34AC` | `0x39A3C` |
| `32` | `0x35AA` | **`0x39B3A`** |
| `33` | `0x361E` | `0x39BAE` |
| `67` | `0x37B7` | `0x39D47` |

Service `0x32`'s real handler at `0x39B3A` marshals a request rather than doing
I/O itself:

```
es:[bx+0x18] = 0x17          ; the wire opcode for a block read
+0x20 -> +0x08               ; start
+0x24 -> +0x0C               ; count
+0x28 -> +0x10
+0x42:+0x40 -> +0x14:+0x12   ; the destination far pointer
then a low-3-bits alignment test on +0x3E:+0x3C, else error
```

So **`0x17` is the block-read opcode**, and the request struct carries start,
count and destination. What transmits that struct is the next thing to find; it is
what puts the 112 bytes into the caller's buffer, and it is what has to end up
containing `"Version 2001"`.

`DS:0x8EAA` (256 bytes) and `DS:0x8EA2` (8 bytes) are still the likely working
buffers — the wrapper at `0x3ACBD` zeroes both on first use and passes both into
`0x399D4`, and they are referenced only in `0x3ACFD`..`0x3ADD0`.

### The call chain, as far as it has been followed

```
game            hasp(service, seed, lpt=0, 0x7477, 0x7D57, &p1..&p4)
0x3ADDE         marshal nine args into a struct, check the "lhsh" signature
0x3ACBD         zero DS:0x8EAA (256 b) and DS:0x8EA2 (8 b) on first use
0x399D4         dispatch on service via cs:0x383B
0x39B3A           service 0x32: set opcode 0x17, start, count, dest; alignment check
0x39D83         tail: call 0x39FF4
0x39FF4           dispatch on a port-ish selector:
                    0 or 0x3C      -> return 0 immediately, no I/O
                    0x65/0x66/0x67 -> table at DS:0x4AE8
                    1, 2, 3        -> 0x3A0BE
0x3A0BE           look the LPT base up in the 3-entry table at DS:0x4BB8, store
                  it in the struct, return 1.  Still no I/O.
```

Everything in that chain is **setup and marshalling**. None of it touches a port.
The wire traffic that has been captured all comes from the *detection* routines
(`0x37605`, `0x37EA7` and the helpers in the table below), which do their own I/O
directly.

That is the open end: **it has not been established how a service request, once
marshalled, reaches the wire and writes the caller's buffer.** `0x39FF4`'s first
branch returns success with no I/O at all for selector 0, which is worth pinning
down before anything else — if that is the branch being taken, the block read
would "succeed" while leaving the buffer untouched, and the `strstr` would fail
exactly as observed.

### Constants and helpers now known

| | |
|---|---|
| `0x1989` | the protection's constant. LCG multiplier here; also an XOR key at `0x37990` |
| `0x37990` | unpacks a 32-bit value into nibbles after `xor 0x1989`, folding each as `(n + 7) & 0x0F` |
| `0x37975` | phase 1 — sends the single byte `0x46` via `0x378DF`, which matches the first clocked value on the wire |
| `0x37FE6` | byte-swap a 16-bit word |
| `0x37FCD` | `and 0x20 ; sar 5` — extract STATUS bit 5. Reached through the runtime pointer `ds:0x4AEC` |
| `0x37878` | read one bit at an address |
| `0x3767F` | write an address |

## 5. Next steps, in order

1. **Settle which branch `0x39FF4` takes.** Its selector comes from
   `es:[bx+0x1C]` of the input struct, and `0x399D4` writes `1` into `+0x1C` of the
   *output* struct at `0x399EC` — those may or may not be the same object, and the
   answer decides everything. Selector 0 returns success with no I/O, which would
   leave the buffer untouched and produce exactly the failure seen. A breakpoint or
   a log line beats more disassembly here.

   **Consider stopping the layer-by-layer descent at this point.** Five successive
   rounds each found one more layer without reaching a working state. Two
   alternatives are likely to pay better:
   - **hunt the constant.** The 2000 licence query collapsed the moment the literal
     `cmp DWORD PTR [bp-6], 0x4693` was found in a game binary. Search the 2001
     EXEs for the equivalent — comparisons against literals near the protection
     code, and anything near `0x1989`.
   - **ask what a real dongle does.** Docs/12 solved the 1999 device by dumping
     five units and executing the firmware. If a 2001 unit is available, that is a
     far shorter path than inferring the protocol from the host side.
2. **Then work out what the LCG-read bits must be**, since they are the likely
   source for that buffer. `0x37B80` is the one phase still unread, and `0x1989`
   is the obvious constant to search around — the 2000 licence query turned out to
   be a literal `cmp DWORD PTR [bp-6], 0x4693` (Docs/15), so look for the
   equivalent here before assuming it must be derived.
2. **Watch out for a second detection gate.** Whatever answers those 128 reads may
   itself be checked by an accumulator like `0x37E1D`'s. Do not assume a constant
   will do — `0x7E` is the standing lesson.
3. **Then the block read.** Service `0x32` still has to deliver 112 bytes
   containing `"Version 2001"` (§ 2.5). It is not yet known whether that comes
   over the wire or from something cached by this exchange.
4. **Implement, then check on screen.** The message to make disappear is
   `HDONGLE not found`.
5. Only then the record content and the picture keys (§ 6.2).

## 6. Open questions

### 6.1 Where the I/O lives in the binary

Static tracing stalled and is **not** the recommended route — the measured trace
above was far cheaper. For the record, so nobody repeats it:

- `MENU.EXE` contains no direct port I/O that could be found. Searched and absent
  in the protection segment: `B0 ?? EE` (`mov al,imm; out dx,al`), `EC A8`,
  `EC 24`, `42 EC 4A`. Every bare `EC`/`EE` hit examined turned out to be data
  inside a jump table.
- Calls go through **runtime-installed far pointers**. `0x3766F` writes
  `0x30B9:0x1A3D` into `ds:0x4AEC`, and `0x37871` calls through it. Following that
  pointer led to a two-line bit-extraction helper, not the transport.
- There is a **second dispatch on device type**: `0x3A124` looks the cached type
  up in an 11-entry table at `cs:0x3CBE` and jumps via `cs:[bx+0x16]`. Eleven
  types matches Docs/13's "ten dongle types MENU.EXE learned to probe for".
- All services funnel through a common tail at offset `0x39FA8`, which calls
  `0x3A124`.

### 6.2 Content keys

Not looked at at all. Before assuming the 2000 answer carries, check the
`push dword [abs]` sites in a 2001 game EXE against its own record buffer — the
record here is **112 bytes**, not 48, so the 2000 offsets (`+0x1C`, `+0x20`,
`+0x24`) may not transfer. Docs/16 is the checklist.

### 6.3 Everything else

- The `wrong dongle version` path (message code 6) is untraced.
- Only the DE image has been booted. The other four are unexamined beyond their
  banners and the password check.
- No 2001 photo-game archive has been opened.

## 7. Method notes carried over — these are not optional

From the 2000 session, at real cost:

- **A clean log is not a passing check.** Ask Marcos what the screen shows. A
  completed handshake means the transport worked, not that the guest accepted the
  answer. `HANDOFF.md` § 5 and the memory note both say this because it was
  reported wrongly once.
- **Marcos drives the guest.** Scripted clicks open the game grid but never land
  on a tile, and each attempt costs a credit.
- **Do not screenshot the emulator window** — the capture grabs whatever is on
  top of that screen region.
- **The menu runs the same protection sequence at boot as the games do**, so the
  device can be iterated without launching a game. Only the verdict needs Marcos.
- **Two independent mechanisms** can fail in a photo game (Docs/16): the record
  dword that decrypts the level database, and the per-picture key. No picture-key
  query in the log means the record, not the keys.

---

## 8. Tools that already exist and apply here

The `hd2001` device option (default off) makes STATUS answer a fixed byte taken
from `hdsweep.txt`, self-advancing like `ngsweep`. Enable it by adding to the
rig's `86box.cfg`:

```
[Protection Dongle for Photo Play / I.G.O.]
hd2001 = 1
```

It disturbs the 1999 and 2000 paths, so it belongs on a 2001 rig only. As § 4
records, sweeping it is not informative on its own — it is there for the moment
the required answer is known and needs trying.


| | |
|---|---|
| `PeepeeBox-handoff/evidence/img.py` | FAT reader/writer for the images, read and write in place |
| `scratchpad/setread.py` | decrypt and parse a `.SET` |
| `evidence/amore-pcx/derive/wad.py` | GWAD reader (directory XOR `0x55`, 21-byte entries) |
| `evidence/amore-pcx/crack.c` | PCX key recovery, 8 known bytes |
| `evidence/amore-pcx/derive/crack2.c` | the same for non-zero-origin PCXs, 4 known bytes |
| `evidence/amore-pcx/derive/checkimg.py` | predict every picture key for an image, offline |
| `objdump -D -b binary -m i8086 -M intel,i8086` | with `--start-address`; file offset = `seg*16 + hdrsize + off` |

`hdrsize` for the DE `MENU.EXE` is `0x5A00` (`e_cparhdr * 16`, at file `0x08`).

---

## 9. Repository state

One change for 2001 so far: the `hd2001` research toggle in
`src/device/dongle_photoplay.c` (§ 7). Nothing else. `main` is otherwise at the
2000 work, released as v1.3.

Artifacts outside the repository:

| | |
|---|---|
| `PeepeeBox-handoff/ex2001/` | `MENU.EXE`, `MAIN.SET` and all 36 game EXEs from the DE image |
| `PeepeeBox-handoff/ex2001/trace2001-de-first-boot.log` | the measured wire trace |
| `PeepeeBox-builds/12-2001-recon/` | the rig, staged from a **copy** of the DE image |

---

## 10. Caller tracing — and four corrections it forced

Commit `dee9fee`. The trace now records, for every port access, the guest's
CS:IP **and** the far return address out of the stub's own frame, so it names the
routine that wanted the port rather than the library helper that performed it. It
also dumps the code at the first non-BIOS segment to touch the port.

One boot settled what five rounds of static reading had not. **Two of the leads
this document ranked first are dead, and two of its facts were wrong.**

### 10.1 The 5A/A5 probe is the BIOS, not the library

§ 3 records phase 1 as "the same `5A`/`A5` probe Docs/09 recorded on IGO8 and
Docs/15 identified as the 2000 library's", and treats the current device
satisfying it as meaningful. It is not the library at all:

```
E000:AA33 write_data / read_data   the 5A / A5 echo pair
E000:AA4B, E000:AA52, E000:AA59
F000:F457, F000:F469, F000:F43E
```

`E000` and `F000` are BIOS ROM. This is the **BIOS probing for parallel ports at
POST**, before the guest runs. It says nothing about the dongle and shares nothing
with 2000 or 2008. Docs/15's claim about the 2000 library should be re-checked on
the same basis before it is trusted either.

### 10.2 There is no hidden port I/O — it is Borland's inportb/outportb

§ 6.1 concluded that `MENU.EXE` "contains no direct port I/O that could be found"
and that "calls go through runtime-installed far pointers". The first half is
right and the second is a red herring. Segment `30B9` opens with three
far-callable stubs, dumped straight out of guest memory:

```
30B9:000B   push bp / mov bp,sp / mov dx,[bp+6] / in al,dx / pop bp / retf
30B9:0014   push bp / mov bp,sp / mov dx,[bp+6] / mov al,[bp+8] / out dx,al / retf
30B9:0020   a delay loop
```

That is `inportb()`, `outportb()` and a spin. **Every** protection access goes
through them, which is exactly why searching the protection code for `EC`/`EE`
found only ModRM bytes. Do not search for port instructions again; search for
calls to these three.

### 10.3 The load base is 0x0835

Runtime addresses now convert to file offsets with no guessing:

```
    file = (CS - 0x0835) * 16 + 0x5A00 + IP
```

Confirmed by locating the `inportb` stub's bytes in `MENU.EXE`: runtime segment
`38EE` is static segment `30B9`, file base `0x36590`, which matches the segment
this document already used for the handler table.

### 10.4 The transport primitives, and where the 32 repeats come from

| runtime | file | what |
|---|---|---|
| `30B9:00BD` | `0x3664D` | write a byte to the port **N times** |
| `30B9:1299` | `0x37829` | read the port **N times**, keeping the last |

Both take their repeat count out of the state struct — the writer from
`es:[bx+0x20]`. § 4 noted "write V, thirty-two times ... presumably a settle or
clock-stretch" with no explanation; that is it, and it is a field the device can
expect to vary rather than a constant of the protocol.

### 10.5 Two leads that are now closed

- **§ 5 step 1, `0x39FF4`.** It is *port resolution*, not transport: it maps the
  `lptnum` argument to a base address, and selector 0 means "auto", returning 0.
  That return is not a failure and settling it would not have explained the
  untouched buffer. This was the document's top-ranked next step.
- **The device-type table.** Making the library report a type whose handler does
  real I/O cannot work: the 11-entry table at `cs:0x3CBE` is **error-code dispatch
  only**. Type `0x2B`, the one entry that reads the opcode field, returns `0xFFE6`
  or `0xFFFE`; type `7` returns `0xFFE4`/`0xFFFD`; type `0` returns success. None
  of them performs I/O.

### 10.6 Where to pick up

The transport is now addressable by name, so the next step is mechanical rather
than exploratory: **trace one boot and read off the call sites**. Every caller of
`30B9:00BD` and `30B9:1299` is a protection routine, the log gives them in
execution order, and § 10.3 converts each to a file offset. That is the map § 6.1
was trying to build by hand.

The specific question to answer with it: **which routine assembles the 112 bytes**,
and what addresses it reads them from. Everything needed to answer it is now in
one log rather than spread across the disassembly.

Trace with callers: `PeepeeBox-handoff/ex2001/trace2001-de-with-callers.log`
(8,635 accesses, the `addr % 3` gates active).

---

## 11. The keyless image — the record content, solved

Marcos supplied the first keyless 2001 release, `PP2001/2001ITk-A3735 KEYN/`,
**the same build as the untouched `Photo Play 2001 IT A3735`**. A perfect diff
pair, and it answers the content question outright.

### The whole crack, in three parts

| | |
|---|---|
| `\MENU\KEYN.COM` | **added**, 114 bytes — a TSR software dongle |
| `\AUTOPTS.BAT` | **changed** — runs `keyn.com` before `main.com` |
| 37 EXEs | **patched**, 5 bytes each; `MENU.EXE` is *substituted*, not patched |

52 EXEs are byte-identical, so only the ones that check are touched.

### The per-game patch is three bytes plus two flags

`FINDIT.EXE`, and every other game the same shape:

```
0x1F699   E8 33 FD   call <dongle read>      ->   CD 2B 90   int 2Bh ; nop
0x0B0C4   01 -> 00                                a skip flag
0x1F6EF   01 -> 00                                a skip flag
```

The call target is `0x1F3CF` — the game's dongle-read routine, the thing our
device has to satisfy. `MENU.EXE` is a different build entirely (163,948 bytes
differ, 240 bytes longer, gains no `int 2Bh`), so the cracker swapped the menu
rather than patching it.

### KEYN.COM decoded in full

```
0x100   jmp 0x15C

0x102   "Version 2001 (IT)" + NUL   padded with zeros to 30 bytes
0x120   038B  181CD  1D760  29B92  1287E  89D  273A6  FDDEBF3E     8 dwords
                                                                  = 62 bytes

0x140   the INT 2Bh handler:
          push cx/ds/es ; ds := cs ; es := ds
          di = [ss:sp+0x0E]        ; the caller's buffer, off the stack
          si = 0x102 ; cx = 0x3E   ; 62 bytes
          rep movsb                ; hand over the record
          iret

0x15C   set the INT 2Bh vector to 0x140 (AH=25h), then TSR (AH=31h)
```

That is the **same mechanism Docs/01 and Docs/10 describe for 1999** — a 114-byte
TSR on `INT 2Bh` — and the same 30-byte banner padding Docs/14 recorded for KEYN
and distinguished from the hardware's 16.

### The record content, which was the open question

**Banner padded to 30, then eight dwords.** Six of the eight are the content keys
already served for 1999:

```
KEYN 2001 IT : 038B  181CD  1D760  29B92  1287E  89D   273A6      FDDEBF3E
our 1999 set : 038B  181CD  1D760  29B92  1287E  89D   BAE8A135   (and a leading 0)
                ^-------- identical --------^          ^-- 2001-only --^
```

So `0x000273A6` and `0xFDDEBF3E` are new in 2001 and the rest carry over. This
agrees with the boot log's `FINDIT reads +1C = 0001D760`: at offset `0x1C` with
the dwords at 16, that is index 3 — and it is the value both sets share.

### What this does and does not settle

**Settled: what the device must contain.** The games want banner-plus-dwords, the
same shape as 1999 and 2000, with the two extra dwords above.

**Not settled: how it reaches them.** KEYN answers a patched `int 2Bh`; it says
nothing about the wire. An unpatched image still needs the transport of §§ 3-4
and § 10. But the target is now a known 62-byte payload rather than an unknown
112, and § 2.5's "56 words" was read out of `MENU.EXE` — worth re-checking
against a *game*, since the game path is the one that matters and the keyless
patch shows the games expect 62.

Artifacts: `ex2001/keyless/KEYN.COM`, `FINDIT_orig.EXE`, `FINDIT_keyn.EXE`.

### One more thing the untouched image gave up

`\FN_SYS\DFU\TRANS\ERROR.LOG` exists and is written by the games:

```
29.8.2026 21:43 in C:\EXE\FINDIT.EXE --> HDONGLE FAILED
```

`HANDOFF.md` § 4 lists reading `ERROR.LOG` as the cheapest untried step for the
1999 stall and notes it had never been created. It works, the games do write to
it, and it names the failing check from the guest's own point of view.

---

## 12. The game's own path, and where the library actually stops

### The games call three services, not two

`FINDIT.EXE` at file `0x1F3CF` — the routine the keyless patch replaced with
`int 2Bh`, so this is definitively *the* dongle read:

```
0x1F3CF  enter 0x18E                       ; a 0x10E-byte local buffer
0x1F3D8  pass1 = 0x7477 ; pass2 = 0x7D57 ; lpt = 0
0x1F41D  hasp(service 1,  ...)             ; presence; fails unless [bp-8] != 0
0x1F450  hasp(service 5,  ...)             ; NEW -- not previously recorded
0x1F48F  hasp(service 0x32, start=0, count=0x38, dest=ss:[bp-0x10E])
0x1F497  cmp [bp-0xc],0 ; jne <fail>
0x1F49D  then unpack the words into bytes
```

So the games read **56 words = 112 bytes**, exactly as `MENU.EXE` does — § 2.5 is
right for both. And **service 5 sits between presence and the read**; it was not
in the earlier map and has to be answered too.

### Only the first 62 of those 112 bytes carry meaning

KEYN copies 62 bytes into a buffer the game sized for 112, and the games accept
it. So the tail is padding as far as the guest is concerned, and the device only
has to get `banner(30) + 8 dwords` right. That is the same record our device
already builds for 1999 and 2000.

### Where the exchange dies

From the call-stack trace, with the `addr % 3` gates passing, the **last** access
is:

```
PPRAW 8635  38EE:12AA 38EE:18EF 38EE:0652 38EE:099C  read_status 20
```

Converting with § 10.3, the outermost frame is file `0x36F2C`, which is the return
site of `call 0x36AFE` at `0x36F29`. So **`0x36AFE` is the final gate**, and it is
the routine whose result feeds the bounds checks at `0x36F45`..`0x36FBA` that end
in `es:[bx+0x1A] = 0x2B` — the device type whose every service call returns an
error (§ 10.5).

Its caller sets up, just before the call:

```
0x36EE8  si       = es:[bx+0x10]
0x36EEC  [bp-0x1a] = (es:[bx+0x16] << 3 - es:[bx+0x3E]) >> 1
0x36F02  [bp-0x20] = 8
0x36F07  [bp-0x22] = es:[bx+0x18]        ; the opcode field
```

**That is the thing to work on next.** Everything upstream now passes; this is
the routine that decides the library has found a device it cannot use. The 321
status reads in that run are nowhere near the ~896 a bit-serial 112-byte read
would need, which confirms the block read is never attempted.

### Phase order, for orientation

Call stacks in order of first appearance, outermost frame converted to a file
offset:

| first seen | outermost | file | what |
|---|---|---|---|
| 36 | `38EE:13FB` | `0x3798B` | phase 1, the `0x46` byte |
| 42 | `38EE:159F` | `0x37B2F` | inside `0x37B80` — the phase § 5 calls unread |
| 132 | `38EE:10AC` | `0x3763C` | — |
| 3804 | `38EE:194A` | `0x37EDA` | the LCG 64-bit phase (`0x37EA7`) |
| 7100 | `38EE:0652` | `0x36BE2` | inside the final gate `0x36AFE` |

`0x37B80`, which § 5 lists as the one phase never read, is running early and
driving 4,260 accesses. It is no longer optional to understand it.

---

## 13. The identity gate, the block read, and where it now stops

Commit `1160f6e`. Two real advances and one hard negative.

### 13.1 The accumulator is an IDENTITY, not a liveness check

This corrects § 4, which reads it as a "line is doing something" test. `0x36B02`:

```
    si = 0x37E1D()                 ; acc = 0x7E ^ XOR{ addr<<1 : bit 5 set at addr }
    cmp si,0x7E   -> fail
    look si up in a FOUR-ENTRY TABLE at cs:0x06FC   =  0008  000C  0018  001C
    no match      -> fail, same as 0x7E
    match         -> jmp [cs:bx+8], and the handler sets es:[bx+0x10],
                     the dongle's MEMORY SIZE
```

| acc | handler | sets `es:[bx+0x10]` |
|---|---|---|
| `0008` | `0x36C5A` | 0 — fails the later bounds check |
| `000C` | `0x36C6C` | — |
| `0018` | `0x36C19` | 1, but only if `[bp-0xc] & 1` |
| `001C` | `0x36C48` | **4 — 256 words, unconditional** |

The caller then bounds-checks the request (start 0, count 56) against that size at
`0x36F77`. **`addr % 3` lands the accumulator on 0**, which is not in the table,
so it failed the lookup for the same reason a constant answer fails — which is
why "varied" bought nothing. Sweeping was never going to find this; the value has
to be one of four.

### 13.2 Hitting 0x001C, and what it opened

XOR over `{ addr : addr % 3 == 0 }` is `0x7E`, and toggling one more address `a`
changes the XOR by `a << 1`. Adding address 14 contributes `0x1C`:

```
    bit 5 set  <=>  (addr % 3 == 0) != (addr == 14)      acc = 0x1C
```

| answer | accesses | status reads |
|---|---|---|
| `addr % 3` (acc 0) | 8,635 | 321 |
| `+ addr 14` (acc `0x1C`) | **48,267** | **1,217** |

`1217 - 321 = 896 = 112 x 8`. **The block read happens** — it had never been
reached before.

### 13.3 The block read is a serial stream

Not addressed. The bus value is a clock: about a dozen writes toggling DATA bit 5
between reads, one bit per read, 16 bits per word, 56 words. Confirmed against the
assembly at `0x385AB`:

```
    si = 0                 ; then sixteen times:
    si <<= 1 ; si += bit   ; MSB first
```

and the caller unpacks each word high byte first (§ 2.5), so the whole thing is a
plain MSB-first byte stream. The device streams the record over it.

Phase boundary: the block read starts after **321** status reads on the path where
detection succeeds. Keying on the number of writes between reads does **not** work
— the LCG phase also repeats each value about thirty times and gets mistaken for
the stream, which advances the cursor before the read begins. Tried, and it
scrambles the record.

### 13.4 The hard negative: the library drops it

The bits are right on the wire — decoded straight out of the trace they reassemble
to `"Version 2001 (DE)"` followed by the dwords, with the dwords where FINDIT
expects them. The guest still shows `HDONGLE not found`.

Reading guest memory back settles why. `MENU.EXE`'s check buffer is
`DGROUP:0x8E64` (DGROUP file `0x3AED0`, static seg `0x354D`, runtime `0x3D82`,
linear `0x46684`) and it is **all zeros** after the read. Scanning `0x10000`
through `0xA0000` for the record's leading dwords finds them **nowhere**.

**So the library never stores what it read.** It is discarding the data, not
mis-delivering it. That is the next question, and it is a much narrower one than
before: something between `0x385C1` returning a word and the caller's buffer
rejects the transfer. Candidates, in order:

1. a per-word check — parity, complement, or a repeated-read comparison — that
   the stream has to satisfy as well as carrying the payload;
2. the service returning an error after the read, so the copy is skipped;
3. the 896 reads being a verification pass rather than the payload transfer,
   with the real transfer elsewhere.

`0x3716D` is the routine that owns the read; that is where to look.

### 13.5 A method note

The reassembly in § 13.4 was very nearly reported as proof that `strstr` would
pass. It was not: it decoded the trace with the same bit order used to encode it,
so it could only ever agree with itself. The check that mattered was reading the
guest's memory. Docs and handoffs here keep saying a clean log is not a passing
check; a self-consistent decode is not one either.

Trace: `ex2001/trace2001-de-blockread.log`.

---

## 14. The 896 reads are a MEMORY TEST, not the payload

This is the answer to § 13.4, and it retires the idea that streaming the record
over that phase could ever have worked.

`0x37065`, the loop that owns those reads:

```
    ax = [bp-0x14] * 2            ; word index
    les bx,[bp-0x18]              ; a buffer already in memory
    ax = es:[bx]                  ; take a word OUT of it
    [bp-0x1c] = ax                ; stash it
    call 0x37F6F                  ; WRITE that word to the dongle
    call 0x37F6F                  ; read it back, into es:[bx]
    ax = es:[bx]
    cmp ax,[bp-0x1c]              ; must equal what was written
    jz  next
    es:[bx+0x1a] = 0x2D           ; mismatch -> device type 0x2D, give up
```

So the library **writes a word, reads it back, and demands the same value**. It is
a "is this really a memory device" probe. The device currently answers every read
with the next bit of the record, so the read-back never matches what was written
and the library rejects it — which is exactly why the record was found nowhere in
guest RAM (§ 13.4). It was never a transfer to drop.

That also explains the shape: 56 words tested, 16 bits each, 896 reads, matching
the count of the block read the game asks for but for a different reason.

### What the device has to become

A **writable memory**, not a stream:

- `uint16_t mem[256]` — 256 words, the size selector `0x001C` advertises (§ 13.1);
- preloaded with the record so that words 0..55 read back as banner-plus-dwords
  for the real service `0x32`;
- writes captured off the wire and stored, so the read-back test returns exactly
  what was written.

The open question is whether the test walks the *same* words the record occupies.
If it does, the record has to survive it — either the test runs before the real
read and the library restores the contents, or the region tested is scratch.
`[bp-0x14]` is the word index and the loop runs while `si + [bp-0x1a] > di`;
reading those bounds off the setup at `0x37020`..`0x37065` will say which.

### What is still not known

**How a write is framed on the wire.** Everything measured so far has treated the
data-port writes as a clock, and for the read phases that was right. The write
half has never been decoded, and it must be, because the device now has to
recognise the value the guest is sending rather than just count edges.
`0x37F6F` is the primitive to read; it is called twice per word, once to write and
once to read, so the two calls' arguments are what distinguish them.

### Correction carried

§ 13 says "the device now plays the record out over it" and reports the block read
as reached. Both are true, but the framing there — that the phase is the payload
transfer — is wrong, and § 5's step 3 ("the block read still has to deliver 112
bytes") is describing a phase that does not exist yet. The payload read comes
*after* this test passes, and has not been seen on the wire even once.

---

## 15. The write half — it exists, and 0x37F6F is not it

### 15.1 Correction to § 14

§ 14 names `0x37F6F` as the primitive that writes a word and reads it back. **That
is wrong.** `0x37F6F` performs no I/O at all. Disassembled end to end it is an
in-place scrambler over a word buffer:

```
    cx = start * 2
    for each even cx in range:
        dx = buf[i]
        if cx < 0x10:  dx ^= 0xFF00
        ax = (cx >> 1) - 8
        ax ^= key                     ; key is the caller's [bp+6]
        dx ^= ax
        buf[i] = dx
```

Index- and key-derived XOR, nothing more. The conclusion § 14 drew from it — that
the 896 reads are a write-then-read-back test — happens to be **right**, but the
routine cited as evidence is not the one doing it.

### 15.2 The library does write to the device

Asked to confirm that nothing is written, the answer is no. `0x385F2` takes the
value as its last argument, and at `0x38708`:

```
    [bp-1] = 0
loop:
    ax = si & 0x8000          ; the top bit of the value
    ...normalised to 0 or 1
    push ax
    call 0x38344              ; emit that bit
    si <<= 1
    inc [bp-1] ; cmp 0x10 ; jc loop      ; sixteen bits, MSB first
```

Sixteen bits of the value are shifted out MSB first and each one handed to
`0x38344`. That is a serial write.

**But nothing is programmed.** It is a transient challenge — a word goes out, the
same word is expected back, and nothing persists. There is no EEPROM-write
service anywhere in this path. So "the dongle is never written to" is wrong on the
wire and right about its contents.

### 15.3 The per-word sequence, as verified

Inside the loop at `0x37065`, for each word:

```
    ax = buf[i]                       ; buf is the far pointer at [bp-0x18]
    [bp-0x1c] = ax                    ; stash the expected value
    call 0x37F6F                      ; scramble the stashed word (index di, key)
    call 0x385F2                      ; WRITE 16 bits, then read
      -> non-zero result: es:[bx+0x1a] = 0x2A, give up
    call 0x37F6F                      ; unscramble
    cmp buf[i], [bp-0x1c]             ; must match
      -> mismatch:      es:[bx+0x1a] = 0x2D, give up
```

Two distinct failure device-types worth recognising in a trace: **`0x2A`** for the
transfer itself failing, **`0x2D`** for the read-back not matching.

### 15.4 The wire agrees

Across the whole phase the guest writes exactly six values:

```
    1C  1E  3E  5C  5E  7E
```

which vary in exactly three bits — **1 (0x02), 5 (0x20) and 6 (0x40)**. A pure
clock would need one. Three varying lines is what a clock, a data line and a
strobe look like, which is consistent with 15.2 and not with a read-only
protocol.

Counts, for reference: `1E` x6216, `5E` x6004, `3E` x3052, `7E` x2948, `1C` x336,
`5C` x324, over 18,880 writes for 896 reads — about 21 writes per read.

### 15.5 What the device has to become

Not a stream. It has to:

1. decode the write line and assemble 16-bit words from it;
2. store them, and return the same word when the matching read follows;
3. keep the record in whatever words the real service `0x32` later asks for.

Streaming the record over this phase, which is what the device does today, can
never pass — it answers a read-back with record bits instead of the word just
written, so the compare at `0x3710C` fails and the library stops at type `0x2D`.
That is exactly the observed behaviour and why the record was found nowhere in
guest RAM.

### 15.6 The next thing to read

**`0x38344`** — it emits one bit, and it is the routine that says which of bits 1,
5 and 6 is data, which is clock and which is strobe. It is small, and it is the
last unknown before the device can be written properly. Its counterpart on the
read side is already known: `0x37FCD`, `and 0x20 / sar 5`, STATUS bit 5.

Once that is settled the shape is clear, and it is a memory device rather than a
responder — which also means the § 13.3 phase-boundary hack (counting 321 reads)
goes away, because reads and writes will be distinguishable by the protocol
itself.

---

## 16. It is a Microwire EEPROM. The protocol is solved, and implemented

`0x38344` was the last unknown (§ 15.6). Reading it, and the three routines beside
it, closes the transport question outright — and retires most of §§ 13-15, which
had the right observations and the wrong story.

### 16.1 The pin map, read off the library's own primitives

Each line has a one-line routine that drives it through the shadow helper at
`0x3767F` (`0x3767F` updates a shadow word and optionally writes its low byte to
the port; `mask`/`value` are its arguments):

| routine | mask | line | DATA bit |
|---|---|---|---|
| `0x38007` | `0x02` | **CS** | 1 |
| `0x380A1` | `0x20` | **SK** (clock) | 5 |
| `0x38344` | `0x60` | **DI**, then a clock pulse | 6 |
| `0x381BA` | — | clock low, high, low, then sample **DO** | STATUS bit 5 |

That is Microwire — the 93C46/93C66 shape — and § 15.4's "three varying bits, which
is what a clock, a data line and a strobe look like" was reading CS, SK and DI.

`0x381BA` samples **after** taking the clock low again, so a bit has to be on the
line from the rising edge onwards.

### 16.2 The three frames

| routine | start | opcode | then |
|---|---|---|---|
| `0x38492` | 1 | `10` = READ | address, then 16 clocks sampling DO, MSB first |
| `0x385F2` | 1 | `01` = WRITE | address, 16 data bits, CS low/high, poll DO for ready |
| `0x387D8` | 1 | `00` = EWEN/EWDS group | address `0xFF` = EWEN |

The address is **8 bits**, because the identity answer of § 13.1 advertises 256
words. `0x37065`'s loop picks 6 or 8 from that same size field.

### 16.3 What the record is, and how it is stored

`0x37F6F` — § 15.1 read it correctly and drew the wrong conclusion from it. It is
the record's **scrambler**, applied after a read and before a write:

```
word[idx] ^= (idx - 8) ^ password1          and also ^ 0xFF00 when idx < 8
```

`password1` is `0x7477`. The chain is: the caller's 4th `hasp()` argument →
`S+0x20` (`0x3AE16`) → `state+0x08` (`0x39B53`) → `[bp-0xC]` (`0x36EB6`) →
`0x37F6F`'s key argument (`0x371A1`).

The library also adds **8** to the requested start word (`0x36FAA`), so a game
asking for words 0..55 is served words 8..63 and the scramble index cancels back
to a plain 0..55.

### 16.4 What §§ 13-15 got wrong, and what they got right

- **§ 14 and § 15 call the 896 status reads a write-then-read-back memory test.**
  They are not. They are the block read itself — 56 words of 16 bits — and the
  DATA writes that looked like a clock-stretch are the instruction being shifted
  out, three port writes per bit (each inflated further by `0x3664D`'s repeat
  count, § 10.4). `[bp-0x22]` is the **opcode**: `0x18` selects the write loop,
  and `0x17`, the block read, takes the read loop at `0x37151`.
- **§ 15.3's per-word sequence is the write path**, which the game path never
  runs. `0x385F2` writes and polls; it does not read anything back.
- **§ 13.3 is right that the phase is a serial stream and wrong that it is
  unaddressed.** Every word carries its own address. The 321-read phase-boundary
  hack is gone, and so is the reason for it.
- **§ 13.1's identity table stands**, and so does everything about `0x1C`.
- **§ 13.4 stands**: the library really did drop the data. It was XORing what the
  old device streamed with `idx ^ key` and getting noise, exactly as it should.

There is **no challenge-response and no crypto on the wire**. The part is a plain
serial EEPROM holding a scrambled record. That is also the strongest evidence yet
for § 2.5's position: an off-the-shelf 93Cxx behind a generic wrapper is what the
"eleven dongle types" reads like, and it is not a HASP.

### 16.5 The check that was run — and it is not a self-consistent one

`scratchpad/replay.py` replays a captured trace through the decoder. The trace is
`ex2001/trace2001-de-blockread.log`, recorded **before** this decoder existed, so
it is the guest's own traffic and cannot agree with the device by construction.
If bits 1/5/6 were not CS/SK/DI it would produce noise. It produces:

```
frames decoded: 57
  EWEN/EWDS addr 255  bits 0
  READ      addr   8  bits 16
  ...
  READ      addr  63  bits 16
READ frames: 56    all 16 bits: True    contiguous: True
status reads inside a read frame: 896
```

One EWEN at address `0xFF` (top two bits `11` = write enable), then reads of words
**8 through 63** — which independently confirms the pin map, the 8-bit address,
the opcode encoding, the `+8` start offset and the 56-word count, all of which
were read statically before the replay was written. Every one of the 896 status
reads lands inside a read's data phase, which is what licenses the decoder's rule
that a status read outside one means the half-collected bits were never an
instruction.

### 16.6 What is implemented

`src/device/dongle_photoplay.c`, still behind the `hd2001` option because driving
STATUS bit 5 disturbs the 1999 and 2000 paths:

- `hd_write_data()` — the Microwire slave: CS/SK/DI decode, START + opcode +
  8-bit address, READ/WRITE/ERASE/EWEN, DO driven MSB first;
- `hd_load()` — 256 words, preloaded with the record already scrambled, so the
  guest's own unscramble hands it the plaintext;
- `pp_read_status()` — DO comes from the part during a read, from the write-ready
  answer after a write, and otherwise from the § 13.1 identity pattern, which is
  unchanged;
- the `hdsweep.txt` sweep is deleted. Sweeping was never informative (§ 4).

The record is built KEYN's way — banner padded to 30, then the eight dwords —
because the keyless 2001 release proves the 2001 games accept that layout (§ 11).
It is **not** the 1999 layout this file serves elsewhere, where the dwords sit at
a fixed 16, so it is built separately rather than sharing `dev->block`.

### 16.7 What is still unverified

Two things ride on the static read alone and a boot will settle both:

1. **the key is `0x7477`** — if the argument order is the other way round it is
   `0x7D57`. Every word comes out XORed by `0x0920` if so;
2. **the record layout** — KEYN's 30-byte banner. If the 2001 games read the
   dwords at 16 like the 1999 ones, the banner check still passes and only the
   picture keys are wrong, which shows up as § 7's "photo game stalls loading
   pictures", not as `HDONGLE not found`.

So the screen distinguishes them: **`HDONGLE not found` gone** means the transport,
the key and the banner are all right. A game that then stalls on pictures means
the layout. Rig staged at `PeepeeBox-builds/12-2001-recon` with the new build.

### 16.8 Next

1. Boot the rig and ask what the screen says. The log now names every word the
   guest reads (`HD2001 read word NN -> XXXX`, first eight).
2. If the message is gone, launch a photo game and check § 6.2's content keys
   against a 2001 game EXE — the `push dword [abs]` sites, per Docs/16.
3. Then the other four images, which have only ever been checked for their
   banners.
4. Once it is confirmed, drop the `hd2001` option and select the transport from
   the identified release instead, the way the 1999 and 2000 halves already are.

---

## 17. The record is TEXT, and KEYN is its output rather than its content

Reported from the rig: with § 16's device the machine gets past `HDONGLE not found`
and into the games, but **AMORE draws scrambled pictures and FINDIT stalls
loading**. That is § 7's "the record dword, not the picture key" symptom in both
games at once, and the cause is one thing.

### 17.1 What the games actually parse

`FINDIT.EXE` `0x1F3CF` — the routine the keyless patch replaces — reads its 56
words, unpacks them high byte first, and then pulls **nine fields out of the byte
buffer by column**, each through `0x1F626` and `atol` (`0x0:0x3C0B`):

| bytes | columns | goes to | 1999's equivalent |
|---|---|---|---|
| `0x00`..`0x1D` | 30 | the banner, `strcpy`d to `si+0` | banner |
| `0x1E`..`0x23` | 6 | `si+0x1E` | `v[1]` |
| `0x24`..`0x29` | 6 | `si+0x22` | `v[2]` |
| `0x2A`..`0x2F` | 6 | `si+0x26` | `v[3]` — FINDIT's database key |
| `0x30`..`0x35` | 6 | `si+0x2A` | `v[4]` |
| `0x36`..`0x3B` | 6 | `si+0x2E` | `v[5]` |
| `0x3C`..`0x41` | 6 | `si+0x32` | `v[6]` |
| `0x42`..`0x47` | 6 | `si+0x36` | — new in 2001 |
| `0x48`..`0x52` | **11** | `si+0x3A` | — new in 2001 |

`0x1F626(buf, start, end)` copies the range, **skips leading spaces** (`0x1F65C`)
and then copies up to the range's `strlen`. So the record is **ASCII**: numbers
right-aligned in fixed columns, and a banner that is NUL-terminated with the rest
of its 30 columns padded.

The same table, byte for byte and once each, is in **all 37 executables** on the DE
image — the 36 games and `MENU.EXE`. Checked by searching for the `pushd` operands
(`66 68 1E 00 23 00` and friends). There is no per-game variation to chase, which
is a real difference from 1999, where every game had its own absolute offset.

### 17.2 Why the first cut failed, in two ways

1. **The padding must not be NUL.** The unpack loop at `0x1F4D2` stops at the first
   zero **word**. A banner NUL-padded to 30 ends the buffer at byte 18, so every
   numeric field reads as garbage. Space padding after the terminator fixes it:
   `strlen` still trims the banner, and no zero word appears before byte 84.
2. **KEYN.COM is not the record.** Its 62 bytes are banner + eight little-endian
   dwords at `+0x1E`, `+0x22` ... `+0x3A` — which is exactly the **parsed struct**
   the table above produces. The crack replaces the whole read-and-parse routine
   with `int 2Bh`, so it never needs the text form. **§ 11 read KEYN's output as
   the dongle's content**, and this device copied that reading straight onto the
   wire. Corrected here, not silently: the mistake is why both games misbehaved.

So the record is:

```
Version 2001 (DE)\0<spaces to column 30>   907 98765120672170898 75902  2205160678 4259233598
```

with each number right-aligned in its columns, and zeros from byte 84 on so the
unpack loop terminates.

### 17.3 The check

`scratchpad/record.py` builds the record the device stores, runs it through the
guest's parser as disassembled above, and compares the struct that falls out with
**KEYN.COM's**, read straight from the file. KEYN is an independent artifact, so
agreement is not the device agreeing with itself:

```
unpacked bytes  : 84
banner  got/want: 'Version 2001 (IT)' / 'Version 2001 (IT)' OK
  field 0 .. 7    all ok
RESULT: record matches KEYN
```

It earned its keep immediately: field 7 was wrong on the first run, because
`0xFDDEBF3E` had been converted to decimal by hand as 4259266366 instead of
4259233598. That would have shipped as one silently wrong content key.

### 17.4 What this says about the values

The first six numbers are 1999's `v[1]`..`v[6]` in the same order — the fixed
funworld content keys, unchanged across three generations. `120672` is `0x1D760`,
which Docs/14 identified as FINDIT's level-database key, and it is the third field
here rather than 1999's `block+0x1C`. The last two, `160678` and `4259233598`, are
2001's own.

Everything is from the IT release, so it is possible a field is per-territory. If
a game misbehaves on one image and not another, that is where to look first.

### 17.5 Where it stands

`hd_load()` now builds the text record; the rig at `PeepeeBox-builds/12-2001-recon`
has the build. The transport (§ 16) is unchanged and needs no revisiting — the
games were reading the dongle correctly all along and being handed the wrong bytes.

---

## 18. The dongle side is finished. The pictures are a second cipher, and it is new

With § 17's text record the DE image reaches the game grid, FINDIT loads its level
database and plays, and AMORE runs. Both then draw **scrambled pictures**. This
section is what that is, measured rather than reasoned.

### 18.1 The dongle is done talking

One full session traced (`PEEPEEBOX_LPT_TRACE=1`, menu plus two games launched),
161,369 port accesses, replayed through `scratchpad/replay.py`:

```
frames decoded: 171   =  3 x (EWEN + 56 READ)
READ addresses: 8..63, every frame 16 bits
status reads inside a read frame: 2688  = 3 x 896
```

Three programs touched the port — runtime segments `38EE` (MENU), `2E6B` and
`3017` (the games) — and **each did exactly one record read and nothing else**.
There is no per-picture dongle transaction in 2001, unlike 1999, where the picture
key was a dongle query (Docs/11, Docs/14). Whatever keys the pictures, the guest
already has it in RAM.

### 18.2 What the 2001 archives actually are

The 2000 images carry **the same 1397 FINDIT pictures at the same byte sizes**, and
there the body is plaintext: RLE from 128 decodes to exactly 300 x 240 = 72,000
pixels, ending exactly at `size-769` on a `0x0C` palette marker. So for any entry

```
    keystream2001 = ciphertext2001 XOR plaintext2000
```

is exact for the whole file, with no guessing. Against that keystream:

- bytes **0..127** are `LCG(0x12345)` exactly — the Borland generator
  `s = s*0x08088405 + 1`, byte `s >> 24`, seeded with the **manufacturer default**.
  Confirmed independently by decrypting: a clean PCX header, 8bpp, 300 bytes per
  line;
- at byte **128 a second keystream starts**. It is different for every picture,
  uniform in distribution, does not repeat at any period tried (128..8192), and is
  **not** that LCG: sliding an 8-byte window across the body and cracking each one
  finds a seed only for the window that still lies inside the header.

So 2001 encrypts the whole file where 1999 and 2000 encrypted only the 128-byte
header, and the body uses a generator that is not the one the header uses.

### 18.3 It is not per-territory

The picture bodies are **byte-identical across the DE, IT and AT images**. Whatever
seeds them is the same on every 2001 machine, so it is not a per-site or
per-territory value.

### 18.4 The game's own cipher primitive, for whoever picks this up

`FINDIT.EXE` (segment map: `file = seg*16 + 0x4400 + off`):

| | |
|---|---|
| `0923:0A13` (file `0xE043`) | `for i in 0..len-1: buf[i] ^= random(256)`, seeding `ds:0x5694` from the key argument and restoring it after |
| `0923:0DD8F` (file `0xDD8F`) | `random(n)`: `s = s*0x08088405 + 1; return (s * n) >> 32` — for n = 256 that is exactly `s >> 24`, the same generator `crack.c` models |
| `ds:0x5E53` | the picture key global, **set to the literal `0x12345`** at `0x182FD` and only saved/restored elsewhere (`0x17D3A`) |
| `0x17011` | returns `0x12345` when that global is set, and decrypts 128 bytes at `ds:0x5AD2` — the PCX header path |

FINDIT's main segment calls the cipher exactly twice, both with the constant
`0x12345` and lengths `0x2C` and `0x80`. **The body decrypt is not in that
segment** — it is in one of the overlays (`0x1789`, `0x19D8`, `0x0CD2` are on the
picture path) and has not been found yet.

### 18.5 What is still open, stated as open

The dongle makes no picture-time request, but that does **not** prove the record is
uninvolved: the game could derive the body key from the numbers it read at startup
without touching the port again. § 18.3 says such a key would have to be the same
on every image, which the six shared content keys are.

Six of our eight numbers are corroborated by the 1999 dongle dumps (Docs/12). The
two 2001-only ones, `160678` and `4259233598`, rest on KEYN alone — and KEYN
bypasses the parser, so it proves what the *struct* should contain, not that a
photo game is happy with it. Marcos's steer is to try 1999's values there; 1999's
`v[7]` is `0xBAE8A135` = 3135340597, which also fits the 11-column field.

Two ways forward, in cost order:

1. **Find the body decryptor** in the overlay segments and read what it seeds with.
   That answers the question instead of sampling it, and § 18.4 has the segment
   arithmetic to get there.
2. **Try the 1999 value** in field 7 and look at the screen. One constant, one
   boot — but it is a guess, and a wrong guess looks identical to a wrong cipher.

---

## 19. The pictures are HASP data encryption, and the algorithm is in the game

This is the answer to § 18.5's first option, and it changes the shape of the
remaining work. **Marcos identifies the part as a HASP4, and everything below is
consistent with that** — § 2.5 should be read as settled in that direction now: the
memory is 112 bytes (MemoHASP's size, and exactly the 56 words the library reads),
the two 16-bit passwords and the nine-argument call are the published convention,
and the services used are the published ones.

### 19.1 The picture loader's three dongle calls

`FINDIT.EXE` `0x1704B` is the routine that loads one picture, and it calls
`hasp()` (its own entry, `0x2ABB:0x0001`) three ways:

| site | service | when |
|---|---|---|
| `0x170B2` | **5** | once, guarded by `ds:0x1592 == 0`; the result `p3` is stored into `ds:0x1592` and passed to every later call |
| `0x172B5`, `0x17380`, `0x174CD` | **0x3D** | **once per 4 KB block of the picture**, each followed immediately by `0x0:0x5AA9`, which writes that block into the frame buffer |

Service `0x3C` is HaspEncodeData and `0x3D` is HaspDecodeData. So the picture body
is not keyed by a name-derived value the way 1999's was (Docs/14): **it is HASP
data encryption, decoded 4 KB at a time.**

That also settles § 18's open question. The dongle is asked at picture time after
all — but through a service that our device answers without any wire traffic,
which is why the trace showed nothing.

### 19.2 Why the screen shows noise, exactly

`0x39CD0` is service `0x3D`'s handler: it sets opcode `0x13D`, rejects a size below
8, and falls into the common tail, which dispatches on the cached device type
through the 11-entry table at `cs:0x3CBE`. Our identity answer leaves that type at
**0**, and type 0's handler is `0x3A14E`:

```
[bp-2] = 0 ; [bp-4] = 0 ; return          ; success, and nothing else
```

**Success with the buffer untouched.** The game hands over 4 KB of ciphertext, gets
the same 4 KB back, and draws it. That is the noise, and it is not a wrong key —
it is no decryption at all.

### 19.3 The cipher is in the game's own binary

The games carry a **fuller build of the library than `MENU.EXE` does**, which is why
this was invisible from the menu side. `FINDIT.EXE`'s entry at file `0x2EEB1`
handles `0x3C` and `0x3D` itself:

```
    0x2EF44   service == 0x3C -> call 0x2E813(state, buf, size, ds:0x72A4)
    0x2EF6C   service == 0x3D -> call 0x2EB26(state, buf, size, ds:0x72A4)
```

`0x2EB26` decoded: it works in **8-byte blocks** — `(size+7) >> 3` of them, with a
`size & 7` remainder and a minimum of 8, which is exactly the size floor the
service handler enforces — reading each block as two dwords and passing it with the
256-byte state to the block function at `0x2E44D`.

A 64-bit block cipher over a 256-byte state, in software, with a minimum buffer of
8: that is HASP4's data encoding, and **the whole algorithm is already in the
binary**. What is missing is only the key material the state carries.

`ds:0x72A4` (256 bytes) and `ds:0x729C` (8 bytes) are zeroed on first use at
`0x2EEC7` and are the same pair `MENU.EXE` keeps at `DS:0x8EAA` and `DS:0x8EA2`
(§ 2.6).

### 19.4 What has to be found next

The question is now narrow and does not involve the wire:

1. **What fills the key material in the 256-byte state?** Candidates, in order: the
   service 5 (HaspCode) result, which our device currently returns as zero because
   type 0 clears both result words; the passwords; and the 112-byte record we
   already serve.
2. **What must service 5 return?** On real hardware HaspCode is the dongle's
   proprietary function of a seed. If this library computes it in software the
   answer is in the binary; if not, it has to come from a unit or be solved.

And there is an unusually strong lever for step 2: **the 2000 images hold the same
1397 FINDIT pictures with plaintext bodies** (§ 18.2), so every picture is a
known-plaintext/ciphertext pair for the 64-bit block cipher with these passwords.
Any candidate key can be tested offline against 1397 pictures without booting
anything.

### 19.5 What is done

The dongle transport, the identity, the record and its text layout are finished and
verified; three programs read the record and accept it, and FINDIT's level database
decrypts. Everything remaining is the data-encryption service, which is a separate
mechanism that happens to be reached through the same dongle.

---

## 20. The picture cipher is the dongle itself, not software

§ 19 said the algorithm was "already in the binary". That was half right: the
*framing* is in the binary, but the keyed step is a wire transaction. Traced end
to end in `FINDIT.EXE`:

```
0x2EF6C  service 0x3D -> 0x2EB26        split the buffer into 8-byte blocks
0x2E44D  marshal one block, state[+0x16] = 2, then the dispatcher
0x2D821  dispatch on state[+0x18] via the model-4 table at DS:0x3A70
0x2B572  the 0x13C/0x13D handler        (both share it)
0x2D04C  DECODE                          (0x2CDFD is ENCODE)
```

### 20.1 The two stages, decoded

**Stage 1, `0x2D04C` — a six-round Feistel, and it carries no key.** Treating the
block as two dwords L (at +0) and R (at +4):

```c
for (i = 0; i < 6; i++) {
    k    = (5 * (5 - i)) & 0x1F;              /* 25, 20, 15, 10, 5, 0 */
    rot  = ROL32(L ^ 0x5B2C004A, k);
    newL = R ^ rot;
    newR = L;
    L = newL; R = newR;
}
```

**Stage 2, `0x2CD0C` — a 40-step LFSR whose feedback comes from the dongle.**

```c
V    = L;                       /* the dword the Feistel produced */
poly = 0x00628050;
bit  = oracle(V & 0xFF);        /* 0x2D651 */
for (i = 1; i < 0x28; i++) {    /* 40 steps */
    bit |= (V & 1) << 1;
    if ((bit ^ V) & 1)  V = (V >> 1) ^ poly;
    else                V = (V >> 1);
    bit = oracle(((uint8_t *)&V)[bit]);
}
```

### 20.2 The oracle is the dongle, on the same two wires

`0x2D651` is not a table lookup. It shuffles the byte's bits
(`<<1 & 0x0E | <<2 & 0x60`, clear `0x10`), writes it to the dongle three times
through `0x2D35B` -> `0x2D2A5`, and then reads **one bit** back through
`0x2D2F3`, which is `read_port` followed by `sar ax,5 ; and al,1` — **STATUS bit
5, the same DO line the record read uses** (§ 16.1).

So the picture cipher is computed *inside the HASP4*. The dongle is a bit oracle:
feed it a byte, get one bit, forty times per eight bytes of picture. That is why
no amount of reading the binary was going to yield a key — there is no key in the
binary to find.

This also retires § 19.3's "the whole algorithm is already in the binary".

### 20.3 Why the trace showed nothing

The measured session (§ 18.1) has three programs doing one 56-word record read
each and nothing else — no encode/decode traffic at all. So the handler at
`0x2B572` is returning before it reaches the wire. It has three guards:

```
0x2B5D4   state[+0x16] must be 1 or 2      else error 0x0A
0x2B5F8   state[+0x08] and state[+0x0C]    both non-zero, else error 7   (the passwords)
0x2B620   state[+0x10] non-zero after calling 0x2B427   else error 3     (the memory size)
```

**Finding which one rejects is the next step**, and it decides everything: if a
guard is failing for a reason the device controls, the library has never even
tried, and the oracle question is premature.

### 20.4 The lever, if it does come to the oracle

The oracle is a byte -> bit function. If it is stateless within a block — plausible,
since `0x2D6BA` resets the dongle at the start of each — it is **256 unknown bits**,
and every 8-byte block yields 40 observations of it.

The 2000 images hold the same 1397 FINDIT pictures with plaintext bodies (§ 18.2),
so for every block both the input and the output are known. Running the two stages
backwards over even one picture gives thousands of constraints on those 256 bits.
That is an offline recovery with no hardware and no booting, and it is the first
thing to try if § 20.3 says the library is genuinely reaching the wire.

### 20.5 The decode is entered — the chain stops inside HaspCode

§ 20.3 asked which guard rejects. Two of the three candidates are now excluded and
the chain is one step longer than it looked.

**The decode is not skipped.** `0x2EF04` skips it only when the dispatcher returns
zero, and the dispatcher's tail (`0x2DF56`..`0x2DF97`) returns **1** on the normal
path. It also sets the state up correctly on the way in (`0x2DBB9`: model id 4,
matching the single entry in the model table; `0x2DBBF`: `+0x1C = 1`, which is what
`0x2D821`'s guard tests), and service `0x3D`'s entry copies both passwords into
`+0x08` and `+0x0C` before dispatching. So the model lookup, the opcode lookup and
the password guard all pass.

**The remaining guard is the third one, and it hides a call.** `0x2B620` tests
`state[+0x10]` after `0x2B427`, and `0x2B427` is not a local check: at `0x2B49F` it
calls **`0x2ACD5`, which is model 4's handler for opcode `0x12D` — HaspCode**. If
that leaves `state[+0x1A]` non-zero, `0x2B427` returns error 7 and the decode never
starts.

So the picture path depends on HaspCode twice over: once explicitly, in the game's
own `hasp(5, ...)` call before the first picture (§ 19.1), and once implicitly,
inside every decode.

**Whether HaspCode reaches the wire is NOT yet known, and an earlier draft of this
section said it does not.** That claim was withdrawn on inspection. Breaking the
measured session down by calling frame shows the games do have traffic the menu
does not -- `3017:0D37` with 448 status reads, `2E6B:0D42` with 256 -- but reading
it, that is the detection ramp again (writes `00 02 04 06 ...`, four times each,
one status read per value; 448 is seven passes of 64), not an oracle.

The simpler explanation is that **the traced session never loaded a picture.** It
was booted to check the record, and the three programs in it each did detection
plus one record read, which is exactly what a start-up does. Nothing in the trace
says a picture was ever drawn, so it cannot say anything about the decode path.

So the measurement to make is a trace that definitely contains a picture load. If
the oracle traffic is there, § 20.4's recovery is the next step. If it is genuinely
absent, then `0x2ACD5` bails early and that is the thing to read.

Note this is now a coherent story rather than three loose ends: HaspCode is the
dongle's challenge-response function, the decode is built on it, and the device
answers neither. Whatever makes `0x2ACD5` bail early is likely to be something the
device can satisfy -- it satisfies everything else on that path already.

### 20.6 With a picture actually loaded: the decode is entered, and still no oracle

Marcos ran the rig, opened a garbled picture and closed it, so this trace
(`ex2001/trace2001-de-picture-load.log`) definitely contains a picture load.
102,811 accesses, 2,761 status reads. Broken down by the routine that wanted the
port -- converting with `file = seg*16 + hdrsize + off`, the game's library being
static segment `0x2636` against runtime `0x2E6B`, the same 0x835 load base MENU
uses -- the tail of the run is:

| frame | file | what it is |
|---|---|---|
| `2E6B:0D42` | `0x2B4A2` | the return of `call 0x2ACD5` in `0x2B427` — **HaspCode ran** |
| `2E6B:0EBE` | `0x2B61E` | the return of `call 0x2B427` in `0x2B572` — **the decode handler ran** |
| `2E6B:0655` | `0x2ADB5` | inside HaspCode, the identity scan |

The mapping is corroborated: MENU's 896-read frame converts to `0x3716D`, which
§ 13.4 independently identified as the routine that owns the block read, and the
game's converts to the same routine 3 bytes along in its own build.

**So the decode path is entered, and HaspCode with it.** What it does on the wire
is the 64-step identity ramp and nothing else — the tail's writes are
`30 30 30 R20 / 32 32 32 32 R00 / 34 …`, the ramp with our `addr % 3` answers.

And the identity gate inside HaspCode passes. `0x2ADB2` computes the accumulator,
rejects `0x7E`, and looks the result up in a four-entry table at **`cs:0x06FF`**,
which is byte-for-byte MENU's table at `cs:0x06FC`: `0008 000C 0018 001C`. Our
`0x1C` matches, and its handler at `0x2AE1B` sets `state[+0x10] = 4` and
`state[+0x82] = 1` — so the memory-size guard at `0x2B620` is satisfied too.

Everything on the path therefore passes, and yet the oracle never runs: a 4 KB
decode would be ~20,000 oracle calls, ~60,000 writes, and the whole tail is 4,112
accesses with 200 status reads.

**That is the open question, and it is now a narrow one.** Between `0x2B63D`
(where the guards are done and the error field is cleared) and `0x2D04C` (the
decode itself) there is `0x2A797` and the opcode test at `0x2B66D`. One of those
is where it stops — or the decode does run and gives up after a couple of oracle
calls because the answers are wrong, which the 8 reads at `0x2B61E` would be
consistent with. Reading `0x2A797` is the next step; it is small and it is called
on both sides of the crypto.

---

## 21. The IGO 7 keyless pair cannot help, and why — plus a second corpus

Marcos supplied `F:\PPIGO7 Keyless\` — `Original.img` and the KEYN-patched
`Harddisk.img`, whose pictures load correctly — to see whether its crack solves the
picture cipher. **It does not, because IGO 7 has no picture cipher.**

- the picture archives are **byte-identical** in both images
  (`/FINDIT/PICS/FOTOPLAY.WAD`, 72,373,757 bytes, same SHA-256), so the crack does
  not pre-decrypt anything;
- and in the **original**, the entries are plain `GIF87a`. They were never
  encrypted.

The conversion itself is the familiar recipe: `MENU/KEYN.COM` added (114 bytes,
the same size as 2001's record TSR), `MENU.EXE` substituted, `AUTOPTS.BAT`
changed, plus incidental files. No EXE changes size.

### 21.1 Which generations actually use the data-encryption service

Counting `hasp()` call sites by service across the largest twelve executables of
each image:

| image | IsHasp | HaspCode | ReadBlock | Encode `0x3C` | Decode `0x3D` | pictures |
|---|---|---|---|---|---|---|
| Photo Play 2001 | 251 | 82 | 29 | 6 | **50** | encrypted |
| IGO 2 | 225 | 101 | 60 | 2 | **49** | encrypted |
| IGO 5 | 706 | 152 | 148 | 0 | 51 | **plain GIF** |
| IGO 7 | 723 | 188 | 170 | 0 | **0** | **plain GIF** |

So funworld used the feature in 2001 and IGO 2, stopped encrypting pictures by
IGO 5, and had dropped the calls entirely by IGO 7 — which is why its keyless
release needed nothing more than a record TSR.

### 21.2 What the survey did turn up

**IGO 2 is a second known-plaintext corpus.** Its pictures are encrypted, and
IGO 5 and IGO 7 carry the *same* archives — same names, same 1397 and 1367 entry
counts — in the clear. So IGO 2 ciphertext against IGO 5 plaintext is a full pair,
exactly as 2000 plaintext is for 2001, but under a different dongle
(`68BB / 1329` rather than `7477 / 7D57`). Two independent corpora under two keys
is a much better position for attacking the oracle than one.

**The cipher is chained, not ECB.** Two IGO 2 files whose plaintexts begin
identically have identical ciphertext prefixes, which looks like ECB until it is
tested properly. Over 33,877 blocks of a 2001 picture, 21 repeated plaintext
blocks all encrypt **differently**. So the first block of each buffer matches
because the chain starts fresh, and everything after it does not — which agrees
with `0x2EB26`, where the first block goes through `0x2E44D` and the rest take the
branch at `0x2ECA9`.

Parked here at Marcos's direction.

---

## 22. The I.G.O. generations, and I.G.O. 5 brought up

The HASP generations share 2001's transport, identity table and scrambler but not
its record. Four things vary, all now keyed off the identified release:

| | 2001 | I.G.O. 3 / 5 / 7 |
|---|---|---|
| scramble key | `7477` | the release's own pass1 (Docs/19) |
| DATA bits used | 1..6, top bit clear | **bit 7 also set** on every write |
| word unpack | high byte first | **low byte first** |
| record shape | banner(30) + decimal columns | structured, below |

**The bit-7 difference cost the most.** I.G.O. 5 drives 2001's *exact* sequence
with `0x80` added: its sixteen clocked values are 2001's `46 0A 78 5A ...` plus
0x80, and its ramp runs `80 82 84 ... FE`. The device took the address as
`last_data >> 1`, which put it in 64..127, so the identity answer landed outside
the four-entry table and the guest never reached the record at all -- four
detection ramps and nothing else. Masking to six bits fixes it for both.

### 22.1 The I.G.O. record

`MENU.EXE` `0x3B300` and `FINDIT.EXE` `0x23C0B` parse it identically:

```
    bytes 0..1     the territory, as two characters
    byte  2        unused -- the copy starts at 3
    bytes 3..9     the release word, seven characters: "Version"
    bytes 10..14   the version token, five: "2005B"
```

formatted with `"%s %s (%c%c)"` (DS:0x4CB7) into `"Version 2005B (AT)"`, which is
then `strstr`'d for the build's own version. Feeding that parser 2001's record
prints **`sion 20 05B ( (Ve)`** -- bytes 3..9, 10..14 and 0..1 of the 2001 form --
which is how the layout was found.

The content keys are **binary** here rather than decimal text: eight little-endian
dwords at bytes 30, 34 ... 58, which the guest reads as two-word pairs at start
`0x0F + 2i` (`0x23C8D`) and files at `di+0x1E, +0x22 ... +0x3A` -- the same struct
slots 2001 fills from its columns. The guest's reads stop at word 38, byte 61,
exactly the last byte of the eighth dword.

**The eight values are the same as 2001's.** Confirmed independently: the keyless
I.G.O. 7 image's `KEYN.COM` serves `Version 2007 (PT)` and then
`038B 181CD 1D760 29B92 1287E 89D 273A6 FDDEBF3E` -- byte for byte 2001's set.

### 22.2 Where I.G.O. 5 stands

Menu boots and accepts the dongle; AMORE and FINDIT both load their pictures.
**The menu's own button graphics are still garbled.** What is known about that:

- it is not the content dwords -- § 22.1 confirms those against a keyless image;
- it is not a missing record field: all three programs read exactly words 8..38
  and nothing else, decoded from the wire;
- `/FOTO/FOTOPLAY.WAD` holds one entry, `LOGO.PCX`, encrypted with the **same
  LCG header cipher and the same manufacturer-default seed `0x12345`** as 2001's
  picture headers. Proof: its first bytes are `d2 46 3a 28 cf f9 62 d6`, and
  bytes 4..7 match 2001's recovered keystream exactly (the PCX plaintext is zero
  there), while `d8 43 3b 20 ^ 0A 05 01 08` gives its bytes 0..3;
- I.G.O. 5's menu does **not** contain the `PCXHeader_decode: DONGLE CODE ERROR`
  string that 1999 and I.G.O. 4 have, so it is not that code path.

The generator is `random(n)` at `0x32B5A` over a seed at `DS:0x7AAA`, the same
Borland LCG the other generations use. The seed is written at `0x3C02B`,
`0x3C44F`, `0x3C45B` and `0x3C4BC`; reading those is the next step, and it will
say what the menu thinks the key is.

---

## 23. The oracle: the cipher is mapped, and the keyed round is isolated

### 23.1 The whole cipher

`0x2D04C` (DECODE) on an 8-byte block, L at +0 and R at +4, little-endian:

```
    A   six rounds, keyless:  L,R = R ^ ROL32(L ^ 0x5B2C004A, (5*(5-i)) & 31), L
    f   one round:            L,R = R ^ f(L), L          <- the dongle's part
    B   six rounds, keyless:  L,R = R ^ ROL32(L ^ 0x803425C3, ((5-i)*2) & 31), L
    f   one round:            L,R = R ^ f(L), L
```

**Two keyed rounds, not one** — § 20.1 stopped after the first and missed the pair at
`0x2D226`..`0x2D26F`. `f` is the 40-step LFSR at `0x2CD0C`.

And the mode is **CBC**: `0x2EBB6` saves each ciphertext block, `0x2ECC5` XORs the
previous one into the plaintext, and the IV starts at zero. Only the first block of
each buffer goes through `0x2E44D`; the rest use `0x2E682` (`0x2EBD4` chooses).

### 23.2 The keyed round, extracted

Both Feistel stages are XOR and rotation only, so they are **affine over GF(2)** and
invert exactly. Writing the stages out, with `(L4,R4)` the plaintext XOR the previous
ciphertext block:

```
    (L1,R1) = A(ciphertext)
    (L2,R2) = (R1 ^ f(L1), L1)
    (L3,R3) = B(L2,R2)
    (L4,R4) = (R3 ^ f(L3), L3)
```

`R4 = L3`, so `L3` is known; `R3` follows by solving 32 GF(2) equations from
`R2 == L1`; then `f(L3) = L4 ^ R3` and `f(L1) = L2 ^ R1`.

`scratchpad/oracle2.py` does this over the 2000/2001 known-plaintext corpus:
**16,946 blocks, 33,892 input/output pairs for `f`.** Accounting for CBC dropped the
contradiction count from 491 to 0.

### 23.3 Three validators that turned out to be vacuous — do not reuse them

Worth recording, because each looked convincing:

1. **"`RB == LA` is a free check."** It is not: `R3` is a free 32-bit variable and the
   Feistel is a bijection, so a solution always exists. The check is satisfied by
   construction.
2. **"the clash count fell to zero, so CBC is right."** The clashes fell because with
   CBC every input became distinct -- there were no repeats left to contradict. It is
   consistent with the model, not evidence for it.
3. **"a chain with no satisfying oracle assignment falsifies the model."** Also
   vacuous, for a reason that is itself the useful finding below. The DFS in
   `scratchpad/oracle3.py` times out at 400k nodes on every pair anyway.

### 23.4 The useful structural fact

The LFSR shifts right **forty** times on a 32-bit word, so the input is entirely
shifted out. Its output is therefore

```
    V40 = XOR over steps i of (poly shifted) for the steps whose branch bit is 1
```

— a **GF(2)-linear function of the 40 branch bits alone**, independent of `V0`. The
input only enters through the branch decisions, `branch_i = oracle_bit_i ^ (V_i & 1)`.

So each block's `f` pair gives 32 linear equations in that block's 40 branch bits,
and the oracle table is what ties the blocks together: `oracle_bit_i` is
`oracle(V_i.byte[idx])`, the same byte -> bit function everywhere.

### 23.5 Where to pick up

The attack is now a constraint problem with a clean shape rather than a search:
32 equations per block over 40 branch bits, 16,946 blocks, coupled through a
256-entry byte -> bit table. The byte each step consults depends on the branch
prefix, which is what stops it being one big linear system -- so the likely route is
to fix the early branches (the first step's byte is `V0 & 0xFF`, known outright) and
propagate.

The dumped tables (Docs/20) are the other half: three 166-byte tables, one per
password pair. If the oracle is a lookup into that table rather than something
derived, 256 unknown bits collapse to a known 166-byte constant and the whole thing
falls out. **That comparison has not been tried yet and is the cheapest next step.**

### 23.6 The extraction is WRONG, and here is the test that shows it

Two corrections and one validator, all from trying to use § 23.2's pairs.

**The polynomial is `0x80500062`, not `0x00628050`.** `0x2CD12` stores `0x8050` at
`[bp-2]` and `0x0062` at `[bp-4]`, and `0x2CD94` XORs `[bp-4]` into the value's LOW
word and `[bp-2]` into its HIGH word. The halves were transposed here, which also
retires § 23.4: with a 32-bit-wide polynomial the reachable span is the full 32
dimensions, so the "must lie in a 23-dimensional subspace" argument is vacuous too.
Four for four on vacuous validators in this section.

**The one that works: input collisions.** `f` must be a function, so if two blocks
give the same extracted input they must give the same output. Over 150 pictures --
2,516,766 pairs, 2,516,055 distinct inputs -- there are **711 collisions and 0
agreements**. Chance agreement would be ~0, so this cannot be luck: the extraction
in § 23.2 is wrong. Any future reading of this cipher should be run through this
test first; it is cheap, it scales with the corpus, and unlike everything else tried
here it can fail.

**What is most likely wrong.** Not the known-plaintext premise: 1397 entries match
byte-for-byte in size between the 2000 and 2001 archives, which for re-encrypted
identical plaintexts is expected and for different images would be a miracle. So the
error is in the cipher reading -- the stage order, the Feistel parameters, or more
likely something in `0x2E44D`'s marshalling that transforms the buffer before
`0x2D04C` sees it. Twelve variants of L/R assignment, direction and CBC placement
were swept and all sit at chance, so it is not a simple transposition.

Where that leaves the oracle: the byte -> bit function cannot be tested against the
dumped tables until the extraction is right, because every candidate is checked
against pairs that are themselves wrong. A 32-byte-bitmap-in-the-dump hypothesis was
tried anyway and found nothing, which means nothing yet.

---

## 24. Only the first block of each buffer touches the dongle

This is what § 23.6 was looking for, and it invalidates the model that section
tested rather than any of its arithmetic.

### 24.1 `0x2E44D` — the dongle path, and it carries TWO buffers

```
    0x2E44D(buf1, buf2, unused, state)
      if buf2 != NULL:
          memcpy(local_A = [bp-0x10], buf1, 8)      ; local_A and local_B are
          memcpy(local_B = [bp-0x08], buf2, 8)      ; ADJACENT in the frame
          state[+0x16] = 2 ; state[+0x12] = &local_A
      else:
          state[+0x16] = 1 ; state[+0x12] = buf1
      dispatch                                       ; -> 0x2B572 -> 0x2D04C
      if buf2 != NULL: copy local_A and local_B back
```

So with sub-op 2 the crypto sees **16 contiguous bytes**, and `0x2D04C`'s tail
(`0x2D282`) writes its two LFSR results into the second half. `0x2B646`'s
`dest = data + 8` is the same thing seen from the handler's side.

`0x2EBF4` calls it as `0x2E44D(block, &[bp-0x2C], 0, state)` — so `[bp-0x2C]`
receives the dongle's output, and the caller reads it straight back.

### 24.2 `0x2E682` — every other block, in software

Twelve rounds, add/subtract and data-dependent rotates, keyless **except for a
round-key array passed in `[bp+0xA]`**, indexed `4*(2i+1)`. In `0x2ECA9` that array
is the caller's local at `[bp-0x94]`.

### 24.3 The dongle contributes ONE dword per buffer

`0x2EBFB`..`0x2EC8B`, immediately after the first block's dongle call:

```
    schedule[0] = D                       ; D = the dword the dongle returned
    for i = 1 .. 0x19:                    ; 26 round keys in all
        schedule[i] = src[i] + acc
        D = ROR32(D, src[i].byte0 & 0x1F)
        schedule[1] ^= D
```

built from `D`, a source array at `[bp-0x98]` and an accumulator. **Everything after
the first block is software keyed by that one 32-bit value.**

### 24.4 Why § 23.2 could not work, and the better attack

That extraction ran the Feistel/LFSR model over *every* block. Only the first block
of each 4 KB buffer goes that way; the other 511 go through `0x2E682` with the
schedule. So the pairs it produced were mostly nonsense, which is exactly what the
collision test reported.

The attack this opens is much better than chasing the oracle directly:

1. `0x2E682` is invertible and keyless apart from the schedule, and the schedule is a
   published function of one unknown dword `D`. For a 4 KB buffer with known
   plaintext, **`D` is a single 32-bit unknown constrained by 511 blocks** — solvable,
   and self-verifying: the wrong `D` fails immediately, the right one decrypts the
   whole buffer.
2. That yields clean `(first-block input -> D)` pairs, which is what an attack on the
   byte -> bit oracle actually needs. Every earlier attempt was scoring candidates
   against pairs that were themselves wrong.
3. And a device that can produce `D` is enough to decrypt everything, which is the
   emulation goal -- the oracle only has to be right in so far as it produces `D`.

Do step 1 before anything else, and keep § 23.6's collision test as the gate.

### 24.5 The software round in full, and a correction: the dongle returns TWO dwords

§ 24.3 said the dongle contributes one dword. It contributes **two**. `[bp-0x28]` is
not a local -- it is `[bp-0x2C] + 4`, the second half of the 8-byte buffer
`0x2E44D` hands to the dongle and copies back, and `0x2D04C` writes both of its LFSR
results there. So:

```
    D   = out[0]      acc = out[1]
    schedule[0] = D
    for i = 1 .. 0x19:                       /* 26 round keys */
        schedule[i]  = schedule[i-1] + acc
        D            = ROR32(D, schedule[i-1] & 0x1F)
        schedule[1] ^= D
```

The `[bp-0x98]` that looked like a separate source array is just `schedule[i-1]`:
`&[bp-0x98] + 4i` is `&[bp-0x94] + 4(i-1)`.

`0x2E682`, decoded end to end -- block as two little-endian dwords, L at +0, R at +4,
`i` counting down from 12:

```
    rot1 = (R >> 7) & 0x1F
    L    = ROR32(L - schedule[2i+1], rot1) ^ R      /* subtract */
    rot2 = (L >> 4) & 0x1F                          /* the NEW L */
    R    = ROR32(R + schedule[2i],   rot2) ^ L      /* add */
```

Two half-rounds per iteration, 26 keys in all, and every step is invertible.

### 24.6 What is solved and what is not

**Solved:** the entire software half. Given `D` and `acc`, every block of a buffer
after the first can be decrypted with no dongle at all.

**Not solved:** `D` and `acc` themselves. Last turn's framing -- "one 32-bit unknown
constrained by 511 blocks" -- was wrong: it is **64 bits**, and the round function
mixes adds with data-dependent rotates, so it is not a linear solve. One known block
gives 64 bits of constraint, which is enough information in principle, but not by
brute force.

Routes worth trying, cheapest first:

1. **Meet in the middle on the last iteration.** `i = 1` uses `schedule[2]` and
   `schedule[3]`, both `D + k*acc`; inverting the final half-round from known
   plaintext constrains `D + 3*acc` directly.
2. **Guess `acc`, derive `D`.** For a fixed `acc` the schedule is an arithmetic
   progression and the first half-round may invert to a unique `D` -- 2^32 candidates
   with an immediate reject, which is a C-sized job rather than a Python one.
3. **Check whether `D` and `acc` repeat across buffers.** The dongle answers per
   first-block, so identical first blocks give identical pairs; if a picture's buffers
   share them, far fewer unknowns exist than 64 bits per buffer suggests.

The § 23.6 collision test stays the gate on anything built from this.
