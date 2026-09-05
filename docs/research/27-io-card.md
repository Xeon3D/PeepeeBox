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
| 2 × DB25, 100K pull-up networks, clamp diodes | The looms and their protection. |

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

## 8. What this corrected

The obvious shortcut — have the toolbar buttons type the keyboard shortcuts,
since `S` opens the operator setup from the menu and `C` adds a credit on a
game's start page — is wrong, and was built and reverted before it shipped.
`C` on the *menu* triggers the CRC check instead. The keys are context-dependent
and overloaded, so a button that typed one would do the wrong thing depending on
where the guest happened to be, and would do it silently.

The buttons drive the card's lines.
