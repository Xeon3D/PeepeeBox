# 27 — The funworld I/O card

The cabinets had a coin slot and two buttons behind the door: one for the
operator setup, one for the touchscreen calibration. None of it was emulated,
none of it is named in any executable's strings, and nothing in the earlier
research had gone looking. This is where it lives and how it was found.

## 1. The card

An ISA card, silkscreened **funworld I/O-Karte**, date sticker 1997–2000. From
photographs of a real one:

| Part | What it does |
|---|---|
| **NEC D71055C** | An 8255-compatible PPI. Three 8-bit ports and a control register at base+3. This is the whole interface. |
| **ULN2003AN** | Darlington array on the outputs — the coin acceptor's inhibit line, the mechanical coin counter, lamps. |
| **74HC14** | Schmitt inverter on the inputs. This is the coin-pulse debouncer. |
| **74LS245** | Bus transceiver. |
| **74LS682** + 8-way DIP switch | Address comparator. **This is why the base address is a setting**: the DIP switch picks it. |
| **One DB25 and one DB15**, 100K pull-up networks, clamp diodes | The looms and their protection. Photographed and confirmed 2026-09-05 — an earlier reading of "2 × DB25" was wrong. See §2. |

A **second version** of the card exists that also carries the ESS audio and its
ports on the same board.

## 2. What is wired to it

A **Coin Controls C120** validator, on a 10-way IDC. Its manual
(`Manuals/c120-coin-controls-international-manual-en.pdf`, §4.2) settles the
question of how coin value is signalled, and the answer is not pulse counts:

| Pin | Signal | Active |
|---|---|---|
| 7, 8, 9, 10 | Accept coin 1, 2, 3, 4 | Low |
| 3, 4 | Accept coin 5, 6 | Low |
| 6 | Inhibit all coins | High |
| 5 | Return (reject) | Low |

**Six separate lines, one per programmed coin.** Each is an open-collector NPN
pulled low for **100 ms ± 20%** on a good coin, and the manual is emphatic that
the host must see the line *held*:

> The host machine must look for valid credit pulses NOT LESS THAN 50 mS. It is
> not sufficient to merely detect the edges of credit pulses.

So a coin in the emulator is a timer, not a flag poked and cleared. Anything
shorter is a coin the software will not count — and it would fail silently,
which is this cabinet's speciality.

## 3. Finding the address

Nothing documents where the card sits, and the DIP switch means it could be
anywhere. So the disk was asked instead.

`PEEPEEBOX_IO_TRACE=1` (added to `src/io.c`) reports the first few accesses to
any port nothing in the build claims, with the CS:IP that made them. On an
I.G.O. 7 boot that showed a program at segment `072D` reading

```
0203 0207 0233 0237 023B 023F 0243 0247 024B 024F 0257 025B 025F 0267
0273 0277 027B 027F 0287 028B 028F 0293 0297 029B 029F 02A7 02AB 02AF
02B3 02B7 02BB 02BF 02C7 02CB 02CF 02D3 02D7 02DB 02DF 02E7 02EB 02EF
02F3 02F7
```

— every **base+3** in the range, which is an 8255's control register, at each
address the DIP switch can select. A sweep, finding nothing.

`PEEPEEBOX_IO_PROBE=<hex>` (in `src/device/funworld_io.c`) then answers that
sweep at every candidate at once. Under it, a **different** program — a resident
one at segment `06FC`, loaded before `menu.exe`, which is why none of the
executables on the disk matched the traced offsets — did this:

```
FWIO-PROBE: 0213 written 99
PP-IO: out 0211, 00  from 06FC:0000139F
PP-IO: in  0210      from 06FC:0000100B
PP-IO: in  0212      from 06FC:00000FF8
```

**Base 0x210. Control word 0x99**, which decodes as:

| | |
|---|---|
| Mode | 0 throughout |
| Port A (0x210) | **input** |
| Port B (0x211) | **output** |
| Port C (0x212) | **input**, both halves |

So what arrives comes in on A and C; B is what drives the ULN2003 — the coin
counter and the acceptor's inhibit line. Identical on I.G.O. 7 and I.G.O. 8.

With the card fitted at 0x210 the guest configures it and polls it about
**twenty thousand times per boot**. Without it, never once.

## 4. Polarity, which was the whole problem

The first pass idled every input **high** and pulsed low. Only two of sixteen
lines answered. That is not a bit map in the wrong order — it is what a wrong
idle level looks like: a line resting at its asserted level since power-on never
makes a transition, so it stays silent whichever bit it is.

The two families are wired opposite ways:

| Lines | Idle | Asserted |
|---|---|---|
| A0 (setup), A1 (CRC) | **high** | low — a falling edge |
| The coin lines | **low** | high — a rising edge |

With `PEEPEEBOX_IO_IDLE=00` resting everything low, credits moved for the first
time. A0 and A1 keep their own idle regardless of that variable, because resting
*them* low holds them down from power-on — the first attempt at idling low walked
straight into the CRC check before the machine had finished booting.

At rest the guest now reads port A = `03` and port C = `00`, and each 100 ms hold
is seen by about nine polls, so the debounce the C120 manual demands is satisfied
with room to spare.

Port B is not idle either: it toggles `00`/`80` at roughly 3 Hz for the whole
run. B7 is something the software drives continuously — a watchdog kick or a
lamp — not a coin counter pulse.

## 5. What money the machine takes — and it is not all coins

From the operator setup, on an I.G.O. 8 ES image:

| Insert (EUR) | Credits | What it is |
|---|---|---|
| 0.10 | 0.20 | coin |
| 0.50 | 1 | coin |
| 1 | 3 | coin |
| 2 | 6 | coin |
| 5 | 15 | **a banknote — there is no 5 EUR coin** |

That last row is the one that matters, and it was nearly missed. **Four** of these
are coins and the fifth is a note, so the machine has a **bill validator** as
well as the C120, and the note channel cannot be on a C120 accept line. The
validator is a second device with its own loom — which is what the card's second
DB25 is for, and the obvious reason port C exists as an input at all.

So the earlier count of "five coin channels" was wrong, and any arithmetic built
on treating all five rows as C120 lines is wrong with it.

Also worth keeping in mind when reading results: a 0.10 coin moves the display by
0.20 credits, which is small enough to miss, and the C120's sixth line need not
be programmed at all — so a line that appears to do nothing is not necessarily a
line that is not a coin.

## 6. The line map — what is known and what is not

Established by pressing buttons on an I.G.O. 8 rig and watching the screen:

| Line | Result |
|---|---|
| **A0** | **The operator setup button.** Confirmed — it opens the setup. |
| **A1** | **Starts the CRC check.** Not the calibration. |
| **A2** | Nothing. |
| C0, C1 | Nothing. |

The tidy reading — port A is eight lines, the cabinet has two buttons and six
coins, so A0–A1 are the buttons and A2–A7 the coins — is therefore **wrong**.
A0 is a button, A1 is something else entirely, and the coins are not at A2.

`PEEPEEBOX_IO_WALK=1` makes the toolbar's coin button step through the card's
lines one per click instead of using the map, naming each in the log and in a
small always-on-top window. It skips port B (an output; nothing arrives there)
and A1 (a CRC check per click is no way to spend an afternoon), leaving fifteen:
**A0, A2..A7, C0..C7**.

Idling low, A6 and A7 both moved the credit display and A2..A5 appeared not to,
though the totals seen (5.10, then 16.70) do not decompose cleanly into the table
above and were taken during a walk whose first click had already opened the
operator setup — so the machine was not in the state a player's coin arrives in.
They are not evidence of a mapping yet, and are recorded here as unexplained
rather than fitted to a theory.

`PEEPEEBOX_IO_LINE=A6` pins the button to one line so it can be pressed
repeatedly from a clean boot, which is what will settle each line: five presses
of the 0.10 channel should read 1.00, and nothing else looks like that.

Still open: which lines are the four coins, which are the note validator, where
the calibration button is, and whether the inhibit line on port B has to be
driven before the validator's outputs are believed.

## 7. The coins are not on this card at all

Every line of ports A and C was driven, in both polarities, as single 100 ms
holds and as trains of 2, 4 and 10 pulses, with credits at zero and free play
off. Nothing but A0 and A1 ever answered. Ten experiments, one unreproducible
result.

The reason is that **the coin acceptor is on COM2, not on the card's DB25**
(Marcos, 2026-09-05, correcting an earlier reading of the loom). A serial
validator never touches the 8255, so no bit of it could ever have been the coin,
and the whole port A/C search was aimed at the wrong device.

**This immediately rules I.G.O. 8 out as the image to test that on.** I.G.O. 8 is
the one generation whose *dongle* is serial and lives on COM2 -- the log says so
in as many words:

```
SC: 2008 card reader attached to COM2 (2F8h), 9600 baud
```

so on an I.G.O. 8 rig COM2 is already occupied and cannot also be carrying a
validator. Every other generation up to I.G.O. 7 has a parallel-port dongle on
LPT1, which leaves COM2 free. The coin work belongs on one of those -- I.G.O. 6
is the obvious candidate, since I.G.O. 7 runs a CRC check on every boot and is
slow to test against.

What the card *is* still good for stands: A0 is the operator setup button and A1
starts the CRC check, both confirmed, both active low.

## 8. The ten lines, found on I.G.O. 6

The coins are on the card after all. They do nothing on I.G.O. 8 and answer on
**I.G.O. 6**, which is the image this work belongs on -- 8 is the odd generation
in every other respect too, and COM2 was a false trail.

Ten lines respond: **A6, A7 and C0..C7**. Ten is not a coincidence: the loom is
ten wires, and the operator setup's book-keeping page lists exactly ten channels.

| Book-keeping channel | Count after one walk |
|---|---|
| 0.10, 0.20, 0.50, 1, 2 Euro, TOKEN 10 | six **coins**, 5 each |
| 5, 10, 20, 50 Euro | four **notes**, 1 each |

Six coins is the C120's six accept outputs. Four notes is a **bill validator**,
a second device on the same loom. The credit deltas split along the same seam:

| Line | Credits added | Reads as |
|---|---:|---|
| C0 | 12.00 | note 5 |
| C1 | 20.00 | note 10 |
| C2 | 40.00 | note 20 |
| C3 | 100.00 | note 50 |
| A6, A7, C4, C5, C6, C7 | 6.80 -- 19.30 | the six coins |

C1:C2:C3 are exactly 20:40:100, which is the 10:20:50 note ratio at two credits
per euro. C0's 12 is 10 plus a coin that landed with it. So **C0..C3 are the note
validator and the other six are the C120**.

### A defect this exposed

One 100 ms hold books **five** coins on a coin line and **one** note on a note
line -- same pulse, same width. The two groups are read by different code, and
the coin side counts something per poll rather than per edge. Our pulse is right
by the C120's manual and wrong for this software. `PEEPEEBOX_IO_MS` makes the
width settable so the one that books a single coin can be measured.

### Finishing it

The book-keeping page is a better instrument than the credit total: it names the
channel and counts it, so there is no arithmetic to get wrong. Clear it, press
one line once, and see which channel goes from 0 to 1. Ten presses name all ten
without inference.

## 9. What this corrected

The obvious shortcut — have the toolbar buttons type the keyboard shortcuts,
since `S` opens the operator setup from the menu and `C` adds a credit on a
game's start page — is wrong, and was built and reverted before it shipped.
`C` on the *menu* triggers the CRC check instead. The keys are context-dependent
and overloaded, so a button that typed one would do the wrong thing depending on
where the guest happened to be, and would do it silently.

The buttons drive the card's lines.

## 10. Every experiment so far was the same experiment

`PEEPEEBOX_IO_LINE`, `_MS`, `_PULSES`, `_PHASE` and `_HOLD` were **never read**.

They were parsed on the first coin-button press, inside `if (fwio_walk < 0)`.
That guard cannot be true there: `fwio_init` assigns `fwio_walk` before it
publishes `fwio_inst`, and a pulse with no `fwio_inst` returns before reaching
the guard — so by the time any press could run that block, `fwio_walk` was
already 0 or 1 and the block was skipped. It was dead from the moment the same
variable was given a default in `init`.

The consequences are worth writing down, because several conclusions above rest
on runs that did not do what they were told:

- **Pinning never took.** `PEEPEEBOX_IO_LINE=A6` did a plain full walk from A0.
  So "the coins only credit if I walk A2..A5 first" is not an enable line and not
  a handshake: A2..A5 are the clicks it takes to *reach* A6 when the pin is
  ignored. Nothing in A2..A5 is being switched on.
- **`PEEPEEBOX_IO_LINE=C` never took either**, so every port C pass still began
  with A0 opening the operator setup — the exact defect §6 was trying to avoid.
- **`PEEPEEBOX_IO_MS` never took**, so the five-coins-per-hold measurement in §8
  was always made at 100 ms whatever was set.
- **`PEEPEEBOX_IO_PULSES` and `_PHASE` never took**, so the pulse-train and
  bank-select readings in §7 were never actually tested. They are still open
  questions, not eliminated ones.
- With `_LINE` set but not `_WALK`, `init` also picked `fwio_release` for the
  release timer, which clears the *mapped* line rather than the walked one — so a
  line asserted by such a run stayed asserted for the rest of the boot.

`PEEPEEBOX_IO_IDLE` is the one variable that always worked, because it is read in
`fwio_reset`, which `init` does call. That is why §4 is sound and §6–§8 are not.

The environment is now read once, in `fwio_read_env()` from `init`, and the
settings are logged on the way past:

```
FWIO: walk 1, pin 6, only port -1, 100 ms, x1, phase -1, hold A=00 B=00 C=00
```

A variable that is read and ignored looks exactly like one that is read and
obeyed, which is how this survived ten experiments. The line is there so it
cannot happen quietly again.

### What to redo

The §8 map (A6, A7, C0..C7; C0..C3 the notes) came from a full walk, where each
line was still pressed once and named on screen, so it is probably right. But it
has never been confirmed one line at a time from a clean boot, and that is now
possible for the first time. `PEEPEEBOX_IO_LINE=A6` with the book-keeping page
cleared: five presses should read five on one channel and nothing anywhere else.

## 11. The map, measured

With `PEEPEEBOX_IO_LINE` finally being read (§10), each of the ten lines was
pinned for a whole run and pulled once from a cleared book-keeping page, on
I.G.O. 6. The notes named themselves. The coins did something stranger, and the
strangeness is the answer.

**Every coin press booked five channels — the six coins minus one.**

| Pressed | Booked | Missing |
|---|---|---|
| A6 | 0.10, 0.20, 0.50, 1, TOKEN 10 | **2.00** |
| A7 | 0.10, 0.20, 0.50, 1, 2 | **TOKEN 10** |
| C4 | 0.20, 0.50, 1, 2, TOKEN 10 | **0.10** |
| C5 | 0.10, 0.50, 1, 2, TOKEN 10 | **0.20** |
| C6 | 0.10, 0.20, 1, 2, TOKEN 10 | **0.50** |
| C7 | 0.10, 0.20, 0.50, 2, TOKEN 10 | **1.00** |

Six sets of five, and the six missing values are the six denominations with no
overlap and none left over. That is not six lines misfiring — it is the software
reading the port and booking **every coin line it finds low**, finding five of
them low because `PEEPEEBOX_IO_IDLE=00` was resting them all there, and the
pressed line being the only one we had lifted. So the coin a line carries is the
one *missing* from its set.

The notes read correctly throughout because they are the group we happened to be
driving the right way up.

### The two groups rest opposite ways round

| Group | Idle | A coin/note |
|---|---|---|
| Six C120 coin lines, and A0/A1 | **high** | pulls **low** — as the C120 manual says: open-collector, active low |
| Four bill validator lines | **low** | drives **high** |

`fwio_idle[3] = { 0xff, 0xff, 0xf0 }` is now the default and no run script sets
`PEEPEEBOX_IO_IDLE` any more. §4's single global idle level was the right shape
of question and the wrong answer: there is no one polarity, because there are two
devices on that loom.

### The line map

| Line | What |
|---|---|
| A0 | operator setup button |
| A1 | starts the CRC check |
| A2..A5 | not connected (predicted, see §15) — **not** tested |
| **A6** | coin **2.00 EUR** |
| **A7** | coin **TOKEN 10** |
| **C0** | note **5 EUR** |
| **C1** | note **10 EUR** |
| **C2** | note **20 EUR** |
| **C3** | note **50 EUR** |
| **C4** | coin **0.10 EUR** |
| **C5** | coin **0.20 EUR** |
| **C6** | coin **0.50 EUR** |
| **C7** | coin **1.00 EUR** |

The denominations are what *this* image's operator setup is programmed to; the
channels are the wiring. The UI numbers the channels for that reason.

### What this retires

- **§8's "one hold books five coins" defect does not exist.** It was one count on
  each of five other channels, not five counts of one coin. The 100 ms hold and
  the debounce are both fine, and `PEEPEEBOX_IO_MS` is not needed to fix
  anything.
- **The COM2 forward is gone.** `funworld_io_pulse()` was sending every coin to
  `coin_c120_pulse(C120_LINE_CTS)` whenever the C120 device was present, which is
  always. All ten money lines are on the card, so that forward could only swallow
  coins the map would otherwise have delivered. The C120-on-COM2 device itself is
  now dead weight and should come out.
- **§7 is superseded**, other than its correct observation that I.G.O. 8 is the
  wrong image to test on.

Still open: where the touchscreen calibration button is. **§15 answers this from
the wiring: it is A1.**

## 12. The buttons

The toolbar carries ten money buttons — six numbered gold coins, four numbered
green notes — plus operator setup and calibrate. Ten, because the cabinet takes
ten kinds of money on ten separate wires and there is no line that means "money"
in general.

They are numbered by **channel**, not by value. The channel is the wiring and
does not move; what a channel is worth is whatever that image's operator setup
has been programmed to, and the labels carry this image's values as a hint.

## 13. COM2 is free again

The `coin_c120` device is **gone** — file deleted, `device_add` removed, and the
walk no longer carries on to four imaginary lines past C7. It attached a
validator to COM2 on the reading in §7, which §11 disproved: all ten money lines
are on the card.

That matters beyond tidiness. **These cabinets could carry a receipt printer on
COM2** instead of the I.G.O. 8 serial dongle (Marcos, 2026-09-05), and a phantom
validator sitting on that port would have been a real conflict rather than merely
dead code. COM2 now has nothing on it unless the image's own dongle claims it:

- I.G.O. 8 attaches the 2008 card reader to COM2 (2F8h). `PEEPEEBOX_NO_SC=1`
  stands it down; the log says `COM2 is free` when it does.
- Every other generation up to I.G.O. 7 has a parallel dongle on LPT1, so COM2
  is free without asking.

Anything can be bound to it today through Settings → Ports (COM & LPT) → COM2,
including 86Box's `serial_passthrough` char device. Emulating the cabinet's own
printer needs the model first.

## 14. The second door button, and what would settle it

The button labelled "Calibrate touchscreen" ran the CRC check, because it pulses
A1 and A1 is what the menu answers with a CRC check. Calling A1 the calibration
button was never a finding — it came from the cabinet having two door buttons and
the card having two confirmed inputs, which is arithmetic, not evidence. The
action is now labelled **Second door button** and says so.

Two readings are still open and they are not distinguishable from the menu:

1. **A1 is the calibration button**, and its function is context-dependent the
   way the keyboard shortcuts are (§9: `C` is a credit on a game's start page and
   the CRC check on the menu). Testable: press it from inside the operator setup
   or from a game rather than from the menu.
2. **A1 is a CRC-check button** and the calibration is somewhere else.

**§15 settles this in favour of (1) on a count of wires.** Reading (2) survives
only if the DB25 carries an input we have not accounted for.

### The wiring would settle it outright

Marcos has traced both mechanical coin counters and the operator setup button to
the card's **DB15**. Two things come out of that immediately:

- The **counters are outputs**, driven through the ULN2003 from port B, and port
  B is the one port nothing is known about — only that B7 runs a ~3 Hz square
  wave. A counter traced to a ULN2003 input pin names a port B bit directly.
- The **setup button is the anchor.** It is A0, confirmed on the rig. If the
  trace from its DB15 pin through the 74HC14 lands on 8255 pin 4, the same
  method reads off every other pin on that connector without another rig run.

And the question that decides between the two readings above: **does the DB15
carry a second input besides the setup button?** If it does, that is the
calibration line. If it does not, the calibration is on the other connector and
port A is not where to look.

D71055C pin to port bit, for reading traces off the board (standard 8255A):

| Port | Pins |
|---|---|
| PA0..PA3 | 4, 3, 2, 1 |
| PA4..PA7 | 40, 39, 38, 37 |
| PB0..PB7 | 18, 19, 20, 21, 22, 23, 24, 25 |
| PC0..PC3 | 14, 15, 16, 17 |
| PC4..PC7 | 13, 12, 11, 10 |

Note the two reversals: PA0..PA3 descend, and PC4..PC7 descend while PC0..PC3
ascend. Reading either backwards produces a plausible wrong map.

One discrepancy to settle while the board is in hand: §1 records **2 × DB25**
from photographs, and the trace is to a **DB15**. Either the card carries a DB15
as well and §1 is incomplete, or the connector in §1 is misidentified.

## 15. The two connectors, and what the wire count says

Photographed 2026-09-05, and §1's "2 × DB25" was wrong. The card carries **one
DB25 and one DB15**:

| Connector | Carries |
|---|---|
| **DB25** | the acceptor loom — the ten money lines of §11 |
| **DB15** | two mechanical coin **counters** (outputs, through the ULN2003); the **two door buttons** — operator setup and calibration; and **two pins that join a serial connector** |

### That names the calibration button without another rig run

Count the inputs. Ten money lines plus two buttons is **twelve**. Ports A and C
have **sixteen** input bits between them. A0 is the operator setup button,
confirmed. Every one of the ten money lines is placed. So:

- the calibration button is **A1**, being the only other input with anything on
  it, and
- **A2..A5 are the four spare bits**, with nothing wired to them.

Which makes A1 doing a CRC check a **context dependence, not a contradiction** —
the same thing §9 records for the keyboard, where `C` is a credit on a game's
start page and the CRC check on the menu. Pressed from the menu, the second door
button starts a CRC check. Whether it calibrates from elsewhere has not been
tried.

### A correction to §11

§11's table said A2..A5 "did nothing, each pulled on its own". **They were never
pulled on their own.** Every run that touched them either rested port A low —
which holds an active-low input asserted from power-on, so it can never make the
transition anything is watching for — or silently ignored the pin and walked
(§10). With the idle levels right, nobody has pressed A2..A5 yet.

So "A2..A5 are unconnected" is now a **prediction from the wire count**, and
pulling them one at a time on the current build is what would falsify it. Four
presses. `run-line-A2.cmd` … `run-line-A5.cmd` on the rig.

### Open: the two serial pins

Two DB15 pins join a DB9 whose other three pins go to a serial port. Three pins
to a UART is TxD/RxD/GND — the minimum for a serial device — so the DB9 is a
serial peripheral and the card is contributing two wires to it. Which two decides
whether this matters:

- **Power and ground** (+12 V / +5 V from the ISA bus, out through the DB15) —
  the card is only feeding a peripheral, and there is nothing to emulate.
- **8255 port bits** — the card is driving or reading handshake lines on that
  connector, which is a real signal path and would have to be emulated. With a
  receipt printer on the other end (§13), a BUSY or paper-out line is exactly the
  shape of thing that would be wired this way.

To tell them apart: which DB15 pins, which DB9 pins, and whether they run to the
D71055C / ULN2003 / 74HC14 or to a power rail.

## 16. The DB15 loom, and an instrument for port B

Traced by Marcos, 2026-09-05, and drawn **from the solder side** — so the pin
numbering in that drawing is mirrored left-to-right against the mating face. To
keep everyone counting the same way:

| Connector | Front (mating face) | Solder side, left to right |
|---|---|---|
| DA-15 | top row 1..8, bottom row 9..15 | top **8..1**, bottom **15..9** |
| DE-9 | top row 1..5, bottom row 6..9 | top **5..1**, bottom **9..6** |

### What is on the loom

- **Two mechanical coin counters.** Their two upper terminals are strapped
  together to a shared common, and each has its own return. Two returns to the
  DB15, which is two ULN2003 channels, which is **two port B bits**.
- **The operator setup button**, two wires to the DB15. It is A0, confirmed on
  the rig, so whichever DB15 pin carries its signal *is* PA0 — the one anchor
  that converts DB15 pin numbers into 8255 bits for everything else on it.
- **A link to the DB9 extension.** The DB9 has three pins going to a serial
  port — TxD/RxD/GND, a serial peripheral — and its two remaining pins carry the
  counters' common and a wire back to the DB15.

### The serial pins are probably not signal

That last point answers §14's open question, provisionally: the two non-serial
DB9 pins are carrying the **counter common and a link to the DB15**, which is a
supply and return being distributed through a spare connector, not the 8255
reading or driving handshake lines. If that holds, **there is nothing there to
emulate** and the DB9 is only borrowing the bracket. Worth confirming against a
meter — +12 V and ground on those two pins would settle it in one measurement.

### The calibration button is not in the drawing

The DB15 was described as carrying two buttons; the drawing shows only the
operator setup. So §15's prediction — the calibration button is A1 — is
untouched by this and still rests on the wire count.

### Port B: let the guest name the counter bits

`fwio_log_out_b()` now reports every change on port B bits 0..6, always on:

```
FWIO-OUT: port B bit 3 -> 1
FWIO-OUT: port B bit 3 -> 0
```

B7 is excluded — it is the ~3 Hz square wave and would bury everything. Each
other bit is capped at 200 transitions, then says so once and goes quiet, so a
bit that turns out to be chatty costs a line rather than a gigabyte.

This makes the counters name themselves, without tracing anything: **book a coin
and watch which bit pulses.** The two that move are the two counters, and the
rest of port B is the inhibit line and the lamps. It works on any image, rather
than on the one cabinet whose loom is in front of us.

Baseline measured on a plain I.G.O. 6 boot: the software writes port B exactly
once, `00`, and bits 0..6 are otherwise silent. So anything that appears in a run
is an event, not noise.

## 17. A run that could not answer, and two fixes

The first port B run came back with no `FWIO-OUT` lines — and that was not a
result, because the log had no way to say a button had been pressed. Presses went
through `fwio_log()`, which is gated on `PEEPEEBOX_IO_TRACE`, and `run-play.cmd`
deliberately sets no diagnostics. So "no counter moved" and "no coin was
inserted" produced identical logs. An instrument that cannot distinguish its own
null result from not being used is not an instrument.

Three changes, all of them making a run self-describing:

**Presses are logged, always.** `FWIO-IN` names the line, the port and the bit:

```
FWIO-IN: coin 1 -- port C bit 4, held 100 ms
```

Human-rate, so there is no volume argument against it. Counting these against
what was actually pressed is now the first thing to do with any log.

**B7 is no longer excluded.** It was skipped on §4's ~3 Hz square wave, which was
recorded elsewhere — a plain I.G.O. 6 boot writes port B exactly once, `00`, so
that wave is not a property of this image. Blanking one of eight bits while
hunting for *two* counters was a bad trade, and the 200-change cap already solves
the noise it was guarding against. Every bit is logged now.

**The control word is logged**, decoded:

```
FWIO: control 99 -- port A in, port B out, port C upper in, lower in
```

Confirmed on the rig, and it matches the 0x99 decoded off the disk in §3. A run
where this differs is a run whose every other reading needs re-examining.

### The baseline

A plain boot with nothing pressed produces exactly two FWIO lines — the settings
line and the control line above. Everything past them is a press or the card
answering one.

## 18. B7 is a counter, and §4's square wave was never a square wave

The self-describing run, on I.G.O. 6 from the menu:

```
FWIO-IN: coin 1 -- port C bit 4, held 100 ms      <- 0.10 EUR
FWIO-IN: note 1 -- port C bit 0, held 100 ms      <- 5 EUR
FWIO-OUT: port B bit 7 -> 1
FWIO-OUT: port B bit 7 -> 0        x5 pulses in total
FWIO-IN: setup button -- port A bit 0, held 100 ms
```

Three presses, three `FWIO-IN` lines, so the buttons reach the card. Then **five
clean pulses on port B bit 7**, and nothing on any other bit. The setup button
produced no output at all, as it should not.

**B7 is a mechanical coin counter output.** It is also the bit that had been
excluded from the log on the strength of §4 — one more run with that exclusion in
place and this would have come back empty a second time.

### What §4 actually saw

§4 recorded B7 as "a ~3 Hz square wave for the whole run, thousands of
transitions ... a watchdog kick or a bank select". It was the counter. During
that era every coin press booked **five** channels (§11), so every press drove
the counter five times, and a log with no timestamps and no record of what was
pressed makes five pulses per press indistinguishable from a free-running square
wave. Two of this card's long-standing mysteries — the square wave and the
"five coins per hold" — are the same wrong idle level seen from two directions.

The B7-is-a-bank-select theory that `PEEPEEBOX_IO_PHASE` was written for is
therefore dead. The variable can go.

### What five pulses means, and the run that decides

0.10 EUR plus a 5 EUR note is **5.10 EUR**, and five pulses came out. The obvious
reading is **one pulse per 1.00 EUR taken**, with fractions accumulating — which
is ordinary for a mechanical cash counter, and would mean the 0.10 contributed
nothing visible and the note contributed all five.

One data point, so it is a hypothesis. Four presses separate it from everything
else:

| Press | Per-EUR model predicts |
|---|---|
| coin 4 (1.00) once | 1 pulse |
| coin 5 (2.00) once | 2 pulses |
| coin 1 (0.10) ten times | 1 pulse, on the tenth |
| note 2 (10 EUR) once | 10 pulses |

### The second counter

The DB15 carries **two** counters and only B7 moved. So the other one counts
something that did not happen in this run — plays started, or notes as against
coins. It should appear on another port B bit the first time a game is launched.

### Timestamps

`FWIO-IN` and `FWIO-OUT` now carry milliseconds since the card's first log line.
Five pulses is a number; five pulses 120 ms apart and 60 ms wide is a counter
solenoid being driven, and five pulses seconds apart is something else. Without
the stamp those read identically — which is how §4's square wave got written
down in the first place.
