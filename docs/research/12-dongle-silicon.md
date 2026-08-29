# Phase 12 — The 1999 dongle, from the silicon

Everything below is derived **from the ten dumps in `Dongle (1999)/` alone** — the firmware
was disassembled, then executed against a simulated 24Cxx and a simulated LPT host. The
existing `Docs/` were consulted only afterwards, to confirm or falsify. Where the two
agree it is stated as *confirmed*; where they differ, the dumps win and the difference is
called out.

Working files: `analysis/p12/` — `dis51.py` (MCS-51 disassembler), `sim51.py` (8051 core +
I²C EEPROM + LPT host), `mcuA.asm`, `mcuB.asm`, `rec.py`, `matrix.py`, `selfdestruct.py`.

---

## 1. What the device is

A **two-chip parallel-port dongle**: an 8-bit MCS-51 microcontroller holding all the logic,
plus a serial I²C EEPROM holding the licence record. The MCU talks to the PC over the
parallel port by bit-banging, and to the EEPROM over a bit-banged two-wire bus.

### The MCU

| Evidence | Reading |
|---|---|
| `02 06 7D` at 0000, `02 xx xx` at 0003, `75 81 5D` (`MOV SP,#5Dh`) | MCS-51, vectors at 0/3/0B/13/1B/23 |
| 2048-byte dump, code ends at `0x0707` (A) / `0x05CB` (B), rest `FF` | 2 KB program memory, single blank region |
| Keil C51 startup (`?C_START`, init-table interpreter) and runtime tails | compiled C, Keil toolchain |
| Ports referenced: **P1.2–P1.7, P3.0–P3.5, P3.7** | — |
| **P0 and P2 never referenced; P3.6 never referenced** | a 20-pin 8051 with no external bus |
| P1.0/P1.1 also unused | those two are the analog comparator pins on the 2051 and have no internal pull-ups |
| internal RAM used to `0x5B`, SP starts at `0x5D` | 128 bytes of RAM |

That set of constraints is the **Atmel AT89C2051** (or a pin-compatible equivalent): 2 KB
flash, 128 B RAM, 20 pins, P1 + P3.0–P3.5/P3.7, no P0/P2, no external bus. The only two
`MOVX` instructions in either image sit in the C51 init-table interpreter's XDATA branch,
which is unreachable here because every init entry is of IDATA or BIT type — they are dead
library code, not evidence of an external bus.

### The EEPROM

The MCU drives it with a start condition, a control byte of **`A0h`**, **one** address
byte, then data — i.e. a 24Cxx-family I²C EEPROM with single-byte word addressing. Because
the control byte is always `A0h`, the block-select bits are always zero, so **only
addresses `0x00`–`0xFF` are reachable by the firmware**. That is exactly the region where
every non-blank byte in all five EEPROM dumps lives; `0x100`–`0x3FF` is `FF` throughout.
The dumps are 1024 bytes, so the physical part is probably a 24C08, but the firmware only
ever uses the first page-block of it.

---

## 2. The dumps, and which ones actually go together

Ten files, but only **two distinct MCU images** and **four distinct EEPROM images**:

| dump | MCU md5 | firmware | EEPROM md5 | EEPROM layout |
|---|---|---|---|---|
| `r1` | `a20e319c` | **A** | `74d979af` | 1 copy |
| `r1_alt` | `2b30cc7c` | **B** | `74d979af` | 1 copy (identical to r1) |
| `r2` | `2b30cc7c` | **B** | `5bb030a2` | 1 copy |
| `r3` | `a20e319c` | **A** | `c92e1e42` | 5 copies |
| `r3_alt` | `a20e319c` | **A** | `88271557` | 5 copies |

The two firmwares are not variants of one build — they are **two different generations**
(§5). Firmware **A** stores the record five times over and majority-votes; firmware **B**
stores it once. So the layout of an EEPROM tells you which firmware it belongs to, and
**four of the five filed pairings are self-consistent. `r1` is not.**

Running firmware A against the single-copy `r1` EEPROM (`analysis/p12/selfdestruct.py`):

```
read 1 -> b'\xff\xff\xff\xff\xff...'          all 48 bytes blank
EEPROM writes: [(0,255),(1,255),...,(47,255), (101,255),(68,255),(80,255),(86,255)]
```

Four blank copies outvote the one good copy, and the self-repair pass (§7) then **erases
the record on the very first read**. That is not a configuration a working dongle can be
in. `r1` and `r1_alt` also share a byte-identical EEPROM — including the per-unit tail
bytes — which for two different physical dongles is not credible.

> **Conclusion:** the `r1` MCU dump is misfiled. The `74d979af` EEPROM belongs with
> firmware **B** (as `r1_alt` has it), and §9 gives independent physical proof of that.
> Treat firmware A as the r3-family firmware and firmware B as the r1/r2-family firmware.
> Note also that `_alt` is not a consistent label: `r1_alt`'s EEPROM is identical to `r1`'s,
> while `r3_alt`'s differs from `r3`'s.

---

## 3. The EEPROM record

48 bytes, at offset `0x00` (and, under firmware A, replicated at `0x30`, `0x60`, `0x90`,
`0xC0`). Layout:

| offset | size | field |
|---|---|---|
| `0x00` | 16 | NUL-terminated banner — `"Version 99 (SP)\0"`, exactly filling the field |
| `0x10` | 32 | **eight little-endian dwords**, `v[0]`…`v[7]` |

so `16 + 32 = 48`. Across all five dumps:

```
v[0]  varies per unit
v[1]  0000038B   v[2] 000181CD   v[3] 0001D760   v[4] 00029B92
v[5]  0001287E   v[6] 0000089D
v[7]  varies per unit
```

| dump | `v[0]` raw | `v[7]` raw |
|---|---|---|
| r1 / r1_alt | `5F 5E C9 C3` | `00 53 44 3B` |
| r2 | `65 74 65 72` | `CB 66 31 FB` |
| r3 | `10 00 50 00` | `37 26 1A 98` |
| r3_alt | `00 00 00 00` | `35 A1 E8 BA` |

**`v[0]` is uninitialised host memory, not data.** `65 74 65 72` is the ASCII `"eter"`;
`5F 5E C9 C3` is an x86 function epilogue (`pop di; pop si; leave; ret`). Those are
leftovers from the buffer the *programming PC* built the record in. `v[7]` is per-unit and
looks random; it is **not** a checksum — sum8, xor8, sum32 and CRC32 over every plausible
sub-range were tested and none reproduces it (`analysis/p12/rec.py`). It may be a serial
number or more uninitialised memory; the dumps cannot distinguish, and nothing in the
firmware reads or verifies it.

### Reconciling with `KEYN.COM`

`Docs/10` recorded eight dwords in the TSR and flagged "62 bytes is more than the hardware
returns" as unexplained. It resolves cleanly:

```
KEYN block  = char banner[30]; uint32 v[8];   -> 30 + 32 = 62
1999 dongle = char banner[16]; uint32 v[8];   -> 16 + 32 = 48
```

Same struct, different banner-field width — the 2001+ banners (`"Version 2001 (IT)"`) do
not fit in 16 bytes. The six constants `038B…089D` are **identical** in both, so funworld
carried the same values across generations. KEYN's extra `000273A6` / `FDDEBF3E` occupy the
`v[6]`/`v[7]` slots of a *later* dongle; a 1999 unit has the constants at `v[1..6]` instead.
`Docs/10`'s "the dwords are unexplained" stands — the dumps say what they are and where
they live, not what they mean.

---

## 4. The wire protocol, from the dongle's side

This is an independent re-derivation of `Docs/07`, and it **confirms it exactly**.

| dongle pin | role | PC side |
|---|---|---|
| `P3.2` / INT0 | latches each incoming nibble | STROBE (`CTRL` bit 0) |
| `P1.2 P1.3 P1.4 P1.5` | incoming nibble, bits 0–3 | `DATA` bits 0–3 |
| `P1.6` | host acknowledge, polled | `DATA` bit 4 |
| `P1.7 P3.3 P3.7 P3.4` | outgoing nibble, bits 0–3 | `STATUS` bits 3–6 |
| `P3.5` | dongle handshake | `STATUS` bit 7 (BUSY) — **inverted by the PC's port** |
| `P3.0` | I²C SCL | — |
| `P3.1` | I²C SDA | — |

**Receive** (`0x004D` in A, `0x0048` in B) is the INT0 handler. A flag bit toggles between
the two halves, so the host's **low nibble arrives first**:

```
if !phase: byte  = P1.2..P1.5;        phase = 1
else:      byte |= P1.2..P1.5 << 4;   phase = 0;  deliver(byte)
```

**Transmit** (`sub_00CA` in A) mirrors the host's `read_byte` precisely:

```
if P1.6 != 0: return failure          ; host must not already be acking
drive low nibble on P1.7/P3.3/P3.7/P3.4
P3.5 = 0                              ; host sees BUSY = 1  (port inverts)
wait for P1.6 == 1                    ; host has taken the nibble
P3.5 = 1                              ; host sees BUSY = 0
wait for P1.6 == 0
...repeat for the high nibble...
```

That is bit-for-bit the counterpart of the host routine at `0x1D044`. It also confirms
`Docs/07`'s late correction: any movement on the control lines must reset framing, because
the dongle has no other way to resynchronise its nibble phase.

### The length table is in the dongle too

The Keil init table at `0x0402` (A) / `0x02DD` (B) is interpreted at reset. Executed, it
produces:

```
firmware A:  RAM[25h..28h] = 00 0A 32 02
firmware B:  RAM[27h..2Ah] = 00 0A 32 02
             RAM[2Bh..2Eh] = 00 04 00 30
```

`00 0A 32 02` is **byte-identical to `table_send` at `DS:0x2283`** in the game binary, and
`00 04 00 30` to `table_recv` at `DS:0x2287` (`Docs/07`). The dongle uses the send table to
know how many bytes a command still owes it; firmware B additionally carries the receive
table, though nothing reads it. Two independent artefacts, the same two tables.

---

## 5. The two firmwares

| | **A** (`a20e319c`, r3 family) | **B** (`2b30cc7c`, r1/r2 family) |
|---|---|---|
| code size | `0x708` | `0x5CC` |
| command buffer | RAM `0x2A` | RAM `0x30` |
| record copies | **5**, majority-voted + self-repaired | **1** |
| dispatch | inside the INT0 ISR | ISR sets a flag; main loop dispatches |
| ISR saves | ACC,B,DPH,DPL,PSW,R0–R7 | ACC,PSW,R0 |
| I²C half-bit delay | 1 unit | 10 units |
| EEPROM write settling | **none** | `~1000` units after the stop |
| boot-time EEPROM access | none | yes — and it is buggy (§9) |
| init table | send lengths only | send **and** receive lengths |

Neither is strictly newer. **A** has the redundancy scheme and the tighter ISR;
**B** has the safer I²C timing (a 10× slower clock and a real write-cycle delay, which
firmware A omits entirely) and moves the 48-byte reply out of interrupt context. They look
like two independent answers to field reliability problems.

Main loop, firmware A:

```
0x03F9: init();  i2c_park();  for(;;);      ; everything happens in the ISR
```

---

## 6. Command dispatch

`sub_03DE` (A) / `sub_02AF` (B), on `buffer[0]`:

| type | host sends | host reads | handler (A) | what it does |
|---|---|---|---|---|
| 0 | 0 | 0 | — | no-op |
| 1 | 10 | 4 | `0x0190` | keyed hash of an 8-byte name |
| 2 | 50 | 0 | `0x026A` | **programs the EEPROM** |
| 3 | 2 | 48 | `0x02D2` | returns the licence record |

Confirms `Docs/07`'s table from the other end.

---

## 7. Type 3 — the licence read

Firmware A, `sub_02D2`, for each of the 48 byte positions `i`:

1. read `EEPROM[i]`, `EEPROM[0x30+i]`, `EEPROM[0x60+i]`, `EEPROM[0x90+i]`, `EEPROM[0xC0+i]`
2. count duplicates among the five and take the most common value
3. if the vote was **not** unanimous, write the winner back over one dissenting copy
4. emit `winner XOR k`, then advance `k`

Firmware B, `sub_0266`, is steps 1/4 only against the single copy.

The keystream is seeded with the host's nonce (`buffer[1]`) and is exactly the one
`Docs/07` recovered from the game's decryptor:

```
k += 0x75;  if (k < 0x28) k = 0xCB;  if (k > 0xC8) k = 0x13;
```

`CJNE R6,#30h` bounds the loop at **48** bytes.

**Executed** (`analysis/p12/sim51.py`), firmware B + the r2 EEPROM, nonce `0x89`:

```
raw       : df 76 fa 60 e1 7c e6 33 b1 2a a8 3b db 43 a1 13 ed 67 ed 61 ...
decrypted : 56 65 72 73 69 6f 6e 20 39 39 20 28 53 50 29 00 65 74 65 72 ...
as text   : b'Version 99 (SP)\x00'
MATCH slot0 : True
```

The full matrix (`matrix.py`), nonce `0x5A`:

```
r1      A(vote)   ee=74d979af  48 bytes  matches_slot0=False   <- self-erases
r1_alt  B(single) ee=74d979af  48 bytes  matches_slot0=True
r2      B(single) ee=5bb030a2  48 bytes  matches_slot0=True
r3      A(vote)   ee=c92e1e42  48 bytes  matches_slot0=True
r3_alt  A(vote)   ee=88271557  48 bytes  matches_slot0=True
```

Nonces `0x89` and `0xA3` against the same dongle produce ciphertexts differing **only in
byte 0** — confirming `Docs/07`'s note that the keystream collapses to a `0x13`/`0x88`
two-cycle and the nonce really only masks the first byte and picks the phase.

---

## 8. Type 1 — recovered in full (new)

`Docs/07` had this as "an 8-byte name in, a dword out", contents unknown, and PeepeeBox
answers it with zeros. The firmware gives the whole algorithm. With
`n[0..7]` = the name and all arithmetic mod 256:

```c
v0 = 4*n0 + 0x11 + 3*n1;
v1 = 7*n2 + 0xA7 + 2*n3;
v2 = 4*n4 + 0x75 + 7*n5;
v3 =   n6 + 0x17 + 4*n7;

switch ((v0 + v1) & 3) {              /* one extra round, selected by the data */
  case 0: v3 = 6*v3 + v1 + 0x75;           break;
  case 1: v2 = v0 + 2*v3 + 0x0C;           break;
  case 2: v1 = 4*v0 + 0x37 + 4*v1;         break;
  case 3: v0 = 5*v2 + 0x64;                break;
}
/* then XOR v0..v3 under the 4-byte keystream seeded with the nonce */
```

The 4-byte keystream is the one `Docs/07` found at `0x1D258`:
`k += 0x25; if (k < 0x1E) k = 0x7B; if (k > 0xAE) k = 0x17;`

It touches **no EEPROM state at all** — type 1 is a pure function of the name and the
nonce, identical on every dongle of this generation.

Verified by execution against the derivation:

| name | nonce | emulator (decrypted) | formula |
|---|---|---|---|
| `ABCDEFGH` | `0x11`, `0x55` | `A3 04 73 7E` | `A3 04 73 7E` ✓ |
| `PHOTOPLA` | `0x11`, `0x55` | `29 78 03 67` | `29 78 03 67` ✓ |

(The decrypted value is nonce-independent, as it must be.)

---

## 9. Type 2 — the programming command, and the bug that proves the pairings

Type 2 is how funworld personalised a dongle: the host sends `{02, key, 48 encrypted
bytes}` (50 bytes — matching `table_send[2] = 0x32`), the dongle XORs them with the *same*
48-byte keystream (the XOR is its own inverse) and writes the result to EEPROM — to all
five copies under firmware A, to the single copy under firmware B. Nothing is returned.

**There is no authentication on this command.** Any host that can reach the port can
rewrite the licence record.

### The boot-time write bug

Firmware B runs `sub_0510` once at reset. Its intent is clearly to read EEPROM bytes 0 and
1 and write them straight back — a liveness check. But its two callers disagree about the
argument convention. Everywhere else, `sub_0364` is called as `write(R5 = address,
R7 = data)`; `sub_0510` calls it as if the operands were the other way round. What it
actually executes is:

```
write(address = EEPROM[0], data = 0)
write(address = EEPROM[1], data = 1)
```

With a programmed record, `EEPROM[0]` is `'V'` = `0x56` and `EEPROM[1]` is `'e'` = `0x65`,
so **every power-up scribbles `0x00` into address `0x56` and `0x01` into address `0x65`.**

The emulator reproduces this: `EEPROM WRITES during boot: [(0x56, 0x00), (0x65, 0x01)]`.

And those are exactly the stray bytes physically present in the dumps:

| EEPROM | strays outside the record area | explained by |
|---|---|---|
| `r1`/`r1_alt`/`r2` (firmware B) | `[0x56]=0x00`, `[0x65]=0x01` | the bug, with a programmed record |
| same | `[0xFF]=0x01` | the bug on a **blank** part: `EEPROM[0]=EEPROM[1]=0xFF`, so `0xFF` gets `0x00` then `0x01` |
| same | `[0x44]=0x01`, `[0x50]=0x00` | the bug at a boot when bytes 0/1 held `0x50`/`0x44` — provenance unknown |
| `r3`/`r3_alt` (firmware A) | **none** | firmware A has no boot-time EEPROM access |

Three of the five stray bytes are accounted for exactly, the mechanism is proven by
execution, and the fingerprint is present on precisely the EEPROMs that belong to firmware
B and absent from those that belong to firmware A. That is the independent confirmation of
the pairing argument in §2 — and it means those strays are **firmware damage, not data**.
They are harmless only because they land above the five record slots (`0x00`–`0xEF`).

---

## 10. Confirmed / denied / new

| Prior claim | Source | Verdict from the dumps |
|---|---|---|
| Nibble on DATA 0–3 out, STATUS 3–6 in, BUSY handshake, DATA bit 4 ack | `Docs/07` | **confirmed** — exact pin-level mirror |
| Control-line movement must reset framing | `Docs/07` | **confirmed** — the dongle has no other resync |
| `table_send = 00 0A 32 02`, `table_recv = 00 04 00 30` | `Docs/07` | **confirmed** — both present in the dongle's own init table |
| 4 transaction types, type 3 = challenge/response | `Docs/07` | **confirmed** |
| Type-3 keystream `+0x75 / <0x28→0xCB / >0xC8→0x13`, 48 bytes | `Docs/07` | **confirmed** — same constants, `CJNE R6,#30h` |
| Type-1 keystream `+0x25 / <0x1E→0x7B / >0xAE→0x17`, 4 bytes | `Docs/07` | **confirmed** — `CJNE R6,#04h` |
| The nonce effectively only masks byte 0 | `Docs/07` | **confirmed** by differential run |
| Type-1 payload unknown | `Docs/07`, PeepeeBox | **resolved** — §8 |
| The six dwords are "almost certainly per-site counters or licence values" | `Docs/10` | **partly denied** — they are byte-identical across four different physical dongles *and* across generations, so they are not per-site anything. Still unexplained as to meaning |
| "62 bytes is more than the hardware returns" | `Docs/10` | **resolved** — same struct, wider banner field; the 1999 block genuinely is 48 |
| Banner must equal `MAIN.SET["Version"]` | `Docs/08` | **consistent** — the dumps read `"Version 99 (SP)"`, and `versions.json` has `PP1999SP-NSB C5193` with exactly that banner |
| — | new | Hardware is an AT89C2051-class 8051 + 24Cxx I²C EEPROM |
| — | new | Two firmware generations; five-copy majority vote with self-repair in one of them |
| — | new | Type 2 = unauthenticated EEPROM programming |
| — | new | Firmware B corrupts two EEPROM bytes on every power-up |
| — | new | The `r1` MCU/EEPROM pairing as filed is impossible |

Nothing in either firmware references the DS1982 iButton, a second serial channel, or any
port other than the ones in §4. The dongle is only ever the LPT half of the pair — which is
consistent with `Docs/03`'s finding that the two tokens are entirely separate paths.

---

## 11. Assessment: running unmodified Photo Play 99 SP on 86Box

*Requested as an assessment only. Nothing here has been built or changed.*

**Short answer: yes for the software, no for stock 86Box.** The dumps remove the last
guesswork from the LPT half, but they do not touch the second token, and stock 86Box has
no device that can answer either.

What the dumps change:

- **The exact SP payload is now known.** PeepeeBox currently *synthesises* a block from
  KEYN's 30-byte-banner layout and serves the first 48 bytes, which puts the dwords at the
  wrong offsets for a 1999 unit. It works only because the game does `strstr(buf,
  "Version 99")`. Feeding the real 48-byte record from `r3`/`r3_alt` (or `r2`) makes the
  device faithful instead of merely sufficient — and a genuine `"Version 99 (SP)"` record
  is what `PP1999SP-NSB C5193` wants.
- **Type 1 can stop returning zeros** (§8). No game is yet known to call it on the boot
  path, so this is correctness, not a blocker.
- **Type 2 could be implemented**, which would let a virtual dongle be re-personalised to
  any territory rather than rebuilt per banner.
- **The transport needs no further work** — `Docs/07` already validated it live on
  2026-08-28, and §4 independently confirms every line of it.

What still stands in the way:

1. **The second token.** `Docs/03` measured that HASP-only emulation still aborts with
   `DS1982 FAILED`; it is both-or-nothing. The iButton half is separately done
   (`Docs/05`, `Docs/11`) but it is not in these dumps and must be present in the same
   build.
2. **Stock 86Box cannot host this.** It has no `dongle_photoplay` LPT device. Running an
   unmodified image therefore requires **PeepeeBox**, the fork in `peepeebox/`. "Unmodified
   game software" is achievable; "unmodified emulator" is not, short of upstreaming the
   device.
3. **The SP image is not on this machine.** `versions.json` knows
   `PP1999SP-NSB C5193-Xeon3D` but no such `.img` is present here — only the AT/NL/PT 1999
   images. Testing the SP dongle against the SP image needs that disk.
4. **Config detail already learned the hard way:** the test folder must set
   `serial3_enabled = 1` or the MicroTouch gets no port and touch silently dies.

So the honest position is: the 1999 SP dongle is now fully specified — a from-scratch
reimplementation could be written from §3–§9 with no hardware present — and the remaining
work is integration, not reverse engineering.

---

## 12. Limitations

- **Timing is not modelled.** `sim51.py` counts instructions, not machine cycles, and the
  host handshake is a state machine that responds instantly. The firmware's real I²C and
  LPT timings (§5) are therefore unverified against a clock. This matters for a hardware
  reimplementation; it does not affect any of the payload conclusions.
- **`v[7]` is unexplained.** Not a checksum by any test tried. Whether it is a serial
  number or more uninitialised memory cannot be decided from four samples.
- **The six constants are still meaningless.** Knowing where they live is not knowing what
  they are.
- **Type 0 does nothing** in both firmwares, and nothing observed sends it.
- **No dongle was measured.** Everything is static analysis plus simulation of dumps whose
  provenance is a filename. §2 shows that at least one of those filenames is wrong.
- **The `r1`/`r1_alt` MCU mix-up is an inference**, strong but circumstantial: it rests on
  the self-destruct argument and the boot-bug fingerprint, not on re-reading the parts.
