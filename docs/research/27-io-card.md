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

## 4. The line map — what is known and what is not

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

Still open: the coin lines, the calibration button, and whether the inhibit line
on port B has to be driven before the validator's outputs are believed.

## 5. What this corrected

The obvious shortcut — have the toolbar buttons type the keyboard shortcuts,
since `S` opens the operator setup from the menu and `C` adds a credit on a
game's start page — is wrong, and was built and reverted before it shipped.
`C` on the *menu* triggers the CRC check instead. The keys are context-dependent
and overloaded, so a button that typed one would do the wrong thing depending on
where the guest happened to be, and would do it silently.

The buttons drive the card's lines.
