# Phase 2 — The protection wire, recovered

**Scope.** One question: what does the dongle have to say? Phase 1 captured a live
transaction but could only see the guest's side, and the guest never got an answer.

**Answer: it is not a HASP.** The Aladdin trappings are real — `HASPDOSDRV`, the
`hasp(service, seed, lptnum, pass1, pass2, &p1..&p4)` API, the `C6 C7 C6 80` wake — but
underneath the obfuscation the wire is **plain Microwire to a 93Cxx serial EEPROM**, on the
same pins PeepeeBox already drives for the funworld 2001 "HDONGLE". Every gate
`FSYSTEM.EXE` applies at boot now passes against a simulated part.

## 2.1 Method: run the client library offline

`docs/research/evidence/fi-p2-haspsim.py` loads `FSYSTEM.EXE` under Unicorn (16-bit real mode), applies its
relocations, and calls the library entry at segment 0 offset `0x1E7A` exactly as the
wrappers do. Port access is hooked, so a candidate dongle answers without booting anything.

Two things had to be right before it ran at all, and both are worth recording:

* **Unicorn's x86 `INTR` hook pushes nothing.** It fires with IP already past the `INT`
  and does *not* emulate the real-mode FLAGS/CS/IP push. The first version popped six
  bytes that were never pushed, which moved SP by +6 and sent the library off to
  `594A:6AA8` — `594A` being the `pass2` argument, read off the stack as a segment. Service
  interrupts in registers only.
* **The port must behave like a port.** DATA and CONTROL read back their own latches;
  only STATUS is the dongle's. Answering a constant to all three keeps the library stuck
  in port detection.

Validation: the harness reproduces the live PeepeeBox wire byte for byte —
`8B F9 DB BB 95 C9 A9 81 93 D1 B1 8D 9F DD BD` — so what it measures is what the cabinet does.

Docs/04 of the PeepeeBox corpus records that this library "resisted … the Unicorn sandbox".
It does not; the two fixes above are the whole of it.

## 2.2 The API, from the prologue at `CS:0xBBF5`

Not inferred — read off the `retf 0x1A` frame:

| Stack | Register into the core | Out |
|---|---|---|
| `[bp+0x1E]` service | `BH` | |
| `[bp+0x1C]` seed | `AX` (or `*p4` when service ≥ 0x32) | |
| `[bp+0x1A]` lptnum | `BL` | |
| `[bp+0x18]` pass1 | `CX` | |
| `[bp+0x16]` pass2 | `DX` | |
| `[bp+0x12]` `&p1` | `DI = *p1` | `AX → *p1` |
| `[bp+0x0E]` `&p2` | `SI = *p2` | `BX → *p2` |
| `[bp+0x0A]` `&p3` | `ES = *p3` | `CX → *p3` |
| `[bp+0x06]` `&p4` | | `DX → *p4` |

Confirming Phase 0's reading of the four gates: service 1 answers in `p1`, service 3 takes
the address in `p1` and returns the word in `p2`, service 4 writes, service 6 reports status
in `p3`.

## 2.3 The transport

Recovered from the decrypted library (dumped to
`docs/research/evidence/fi/hasp-library-decrypted.bin` at the first port read; the blob decrypts
itself in place and is plainly readable afterwards).

Transmit — `send_line` at `0x32E5`:

```
AL  = [cs:bp+0x1E0]      ; the byte to put on the wire
AL |= [cs:bp+0x1E3]      ; = 0x01
if clock == 0: AL &= [cs:bp+0x1E2]   ; = 0xFE
AL |= 0x80
out DATA, AL
```

Receive — `0x3295`:

```
call read_status
mov cl,[cs:bp+0x243]     ; the bit to sample
shr al,cl
and al,1
if port-type in (2,3): xor al,1
mov [cs:bp+0x1D3],al
```

Read at runtime, the sampled bit is **STATUS bit 5**, and the library alternately probes
the same line as **STATUS bit 7 inverted** (BUSY) — two wiring variants, tried equally often.

Pin map, which is **identical to PeepeeBox's existing HDONGLE**:

| line | pin |
|---|---|
| CS | DATA bit 1 (`0x02`) |
| SK | DATA bit 5 (`0x20`), sampled on the rising edge |
| DI | DATA bit 6 (`0x40`) |
| DO | STATUS bit 5 (`0x20`), also read as bit 7 inverted |

Frames are start bit, two opcode bits, then **9 address bits**, MSB first; `1 1 0` is READ,
then 16 data bits clocked out MSB first.

## 2.4 Two things that had to be measured, not guessed

**The `C6 C7 C6 80` preamble is not a password.** It is byte-identical for every service,
seed, password and address — 136 transactions per call, all the same. Nothing
parameter-dependent goes on the wire until the library is satisfied a device is there. So
the whole of Phase 1's capture was hardware detection, repeated because it kept failing.

That detection is standard HASP-4 and upstream 86Box already models it: `src/device/hasp.c`
keys on `C6 C7 C6 80`, collects the odd-valued writes as a "password", then answers a table
of even values with STATUS bit 5. Porting that file's state machine verbatim
(`docs/research/evidence/fi-p2-hasp4.py`) and giving it **the Savage Quest 14-byte password** makes
`IsHasp` return 1 — writes drop from 11 066 to 1 461 because the library stops retrying.
What matters is the **length, 14**, which sets the phase of the answer table; the byte
values are compared but a mismatch is not fatal here. Our own 15-byte preamble as the
password shifts the phase by one and fails.

**The address bias is +8 and the client descrambles with the password.** With a blank part,
service 3 returns `pass1 ^ address` for every address — 0x43B5, 0x43B4, 0x43B7, 0x43B6 … for
addresses 0,1,2,3. Flipping single reply bits maps the last 16 reads of the transaction
linearly onto the returned word, MSB first. Sweeping the address width shows 9 bits, and

| API word | EEPROM address |
|---|---|
| 0 | 8 |
| 1 | 9 |
| 2 | 10 |
| 5 | 13 |

So:

    returned(n) = raw[n + 8] XOR pass1 XOR n

which is the same shape PeepeeBox documented for the 2001 part (`word[idx] ^= (idx-8) ^
password1`, library adds 8) — arrived at here independently, from a different product.

## 2.5 What the part must contain

With `pass1 = 0x43B5` and the record `FSYSTEM.EXE` demands at `CS:0x030F`:

| API word | must read as | ASCII | raw EEPROM word |
|---|---|---|---|
| 0 | `4F52` | `OR` | `[8] = 0CE7` |
| 1 | `4741` | `GA` | `[9] = 04F5` |
| 2 | `434F` | `CO` | `[10] = 00F8` |
| 3 | `4E54` | `NT` | `[11] = 0DE2` |
| 4 | `524F` | `RO` | `[12] = 11FE` |
| 5 | `4C20` | `L ` | `[13] = 0F90` |

## 2.6 Verified

`docs/research/evidence/fi-p2-verify.py`, output in `docs/research/evidence/fi/verify.txt`, drives the real library
against `p2-hasp4` + `p2-microwire`:

```
svc1 IsHasp        p1=0001                       (FSYSTEM needs p1!=0)
svc6 dongle ID     p1=434D p2=434C p3=0000       (FSYSTEM needs p3==0)
svc3 word 0        p2=4F52 OK -> 'OR'
svc3 word 1        p2=4741 OK -> 'GA'
svc3 word 2        p2=434F OK -> 'CO'
svc3 word 3        p2=4E54 OK -> 'NT'
svc3 word 4        p2=524F OK -> 'RO'
svc3 word 5        p2=4C20 OK -> 'L '
svc3 word 9        p2=BEEF                       (INT 50h AX=1235)
SYSTEM CODE: PASS
```

**Every gate `FSYSTEM.EXE` applies at boot is now satisfiable.** Word 9 — surface D, the
value the app fetches through INT 50h `AX=1235` — is servable but its *correct* value is
still unknown; the part will serve whatever we put there.

One off-by-one is worth keeping: DO must stay driven from the first read bit until CS
drops, not until the 16th clock. Releasing it one edge early hands the last bit back to the
detection layer and the record comes out as `OS GA…` instead of `OR GA…`.

## 2.7 Adversarial check

* **Service 4 (write) does not work yet.** `p3 = 0xFFFF` and no Microwire frame reaches the
  part, so the library is framing writes differently — an EWEN prefix and/or a ready poll
  the responder never satisfies. `FSYSTEM.EXE` uses service 4 for the counters at word 13+
  (INT 50h `AX=0011`, once per game), so this is a **runtime** blocker, not a boot one.
  Owned by Phase 3.
* The detection layer and the EEPROM are modelled as two objects sharing one port. On real
  hardware they are one device; the arbitration (`driving`) is a modelling convenience that
  happens to reproduce the observed answers. If Phase 3 sees drift, this is the first thing
  to distrust.
* `passmode` is 0 in every passing run — the Savage Quest password bytes do not match ours,
  and nothing checks. That means we have **not** shown that the preamble bytes are verified
  at all. A real part may check them; ours does not need to.
* Only services 1, 3, 4 and 6 have been exercised, because those are the only ones
  `FSYSTEM.EXE` calls. `FUNNY.DLL` reaches the dongle only through INT 50h, so it cannot
  call others — but that is an inference from Phase 0, not yet observed.

## 2.8 Falsified in place

* ~~"The 15 clocked bytes are the password."~~ — **reclassified Phase 2**: they are a fixed
  detection preamble, invariant across every parameter. The password never reaches the wire
  in this phase; it is used *by the client* to descramble what it reads back.
* ~~"This is an Aladdin HASP, so PeepeeBox needs genuine HASP-4 emulation."~~ —
  **reclassified Phase 2**: it needs HASP-4 *detection* (which upstream `hasp.c` already
  has) in front of a Microwire EEPROM (which `dongle_photoplay.c` already has). Phase 0's
  estimate of the work was too pessimistic by a wide margin.

## 2.9 Ledger

| Surface | Before | After |
|---|---|---|
| A — `IsHasp` | open | **solved** — HASP-4 detection, 14-byte phase |
| B — service 6 status | open | **solved** — `p3 = 0` falls out of the same handshake |
| C — words 0–5 = `ORGACONTROL ` | specified | **solved and verified** on the real library |
| D — word 9 | open | **servable**; correct value still unknown |
| E — counters (service 4) | open | **narrowed** to the write framing — Phase 3 |
| F, G | open | unchanged |
