# 28 — The DATAPRINT

The cabinets could carry a **Dataprint** — a funworld accessory that prints the
book-keeping, statistics and hiscores. The operator reaches it from a DATAPRINT
menu; with no unit connected it offers *"Connect the interfaces of the Dataprint
to the Photo Play"* and goes no further.

Everything below is read out of `/MENU/MENU.EXE` on the I.G.O. 6 image
(307,374 bytes, plain 16-bit MZ, not packed) and confirmed against a live
capture. File offsets are into that file.

## 1. How it is wired

From the I/O card's DB15 trace in [27](27-io-card.md#16):

- Two DB15 pins join a DB9 whose other three go to a serial port.
- Three wires to a UART is TxD, RxD and ground, so the card's two are the
  **12 V and ground that power the unit**.
- The data path never touches the 8255. The Dataprint is an ordinary RS-232
  peripheral that happens to be fed from the I/O card.

## 2. The link

`0x1D0E5` programs the UART by hand:

```
mov al,0x80 ; mov dx,0x2FB ; out    LCR, DLAB on
mov al,0x0C ; mov dx,0x2F8 ; out    divisor low = 12
mov al,0x00 ; mov dx,0x2F9 ; out    divisor high = 0
mov al,0x03 ; mov dx,0x2FB ; out    8 data bits, 1 stop, no parity
```

115200 / 12 = **9600 baud, 8N1, on COM2 (0x2F8)**. There is exactly one `int 14h`
in the whole binary and it is not this. `mov dx, imm` finds 0x2F8 three times and
no other port, so the port is not a setting — there is nothing to choose between.

Three helpers do all the I/O: `0x1D102` reads the LSR, `0x1D112` the RBR,
`0x1D122` writes the THR.

## 3. The protocol

The engine is the state machine at `0x1E8FC` — a state in `si`, a jump table at
`cs:0x97BB`, one step per call.

| At | State |
|---|---|
| `0x1E92B` | drain: while LSR bit 0, read RBR |
| `0x1E9A7` | wait for **ENQ**: LSR bit 0, RBR == 5, else abandon |
| `0x1E9FD` | LSR bit 5, send one byte — `0x11` |
| `0x1EA1A` | send a 6-byte command, `1B 53 13 03 0A 0A`, until index == 6 |
| `0x1EA4A` | receive a line: store each byte until one is `0x0A` |
| `0x1EA81` | take **index 15** of that line |
| `0x1EAEA` | it must be **`'C'`**, else `si = 8`, the error state |
| `0x1EA8C` | send the report body, counting to `[0x1355]` |
| `0x1EACC`, `0x1EB4B` | send one byte, then a 7-byte trailer |

So a full exchange is:

```
unit -> host   ENQ (0x05), continuously
host -> unit   11 1B 53 13 03 0A 0A       XON, ESC S, XOFF, ETX, LF LF
unit -> host   <16 chars with 'C' at index 15> LF
host -> unit   the report
host -> unit   04 1B 43 <4 hex> 16        EOT, ESC C, checksum, SYN
```

**ENQ is a keepalive, not a hello.** The send loop at `0x1D47D` resets a counter
on every ENQ and abandons the frame when it reaches `0x5DC` (1500) without one,
so the unit must keep announcing for the whole exchange.

**Only byte 15 of the reply is checked.** Nothing else in that function reads the
receive buffer. What a real unit puts in the other fifteen is unknown and
probably identifies it — `Geraete-Nr.: %ld` and `serialnumber: %ld` sit near the
DATAPRINT strings.

`di` accumulates a running sum of every byte sent at `0x1EA9E`, rendered as four
hex digits through the `"0123456789ABCDEF"` table copied in at `0x1E917`. That is
the checksum in the trailer.

## 4. The report

The formatter at `0x1D762` writes **24 characters then an LF** — `cmp $0x17` on a
zero-based index, then `addw $0x19` to the length, so 25 bytes per record. It
filters everything outside `0x20..0x3F`, `0x41..0x5A` and `0x61..0x7A` to a
space, so no high bytes reach the wire.

`ESC K <tag> LF` prefixes some lines. **No literal `1B 4B` exists in MENU.EXE** —
the tags are built at run time. Observed: `!` before "funworld", `"` before the
machine name, `$` before the serial number, `&` before the first transaction, `B`
before the total. What they mean is still open.

A real capture, 886 bytes:

```
------------------------
funworld
Photo Play 2000
serialnumber: 85
------------------------
Book-keeping
------------------------
ACTION DATE        AMOUN
PR 05.09.26       894,00   x5
------------------------
coin
0,10 EUR            0,30
0,20 EUR            0,20
0,50 EUR            0,50
1 EUR               1,00
2 EUR               2,00
note
5 EUR              10,00
10 EUR             10,00
20 EUR             20,00
50 EUR            850,00
------------------------
Total: EUR        894,00
------------------------
 Attention ! Datas
 will not be erased !
------------------------
04 1B 43 39 45 36 37 16
```

The coin and note lines are the ten channels of [27](27-io-card.md#11) — the
report is an independent readout of the same map.

## 5. What the emulation does

`src/device/prn_cp80.c`, on COM2, **unplugged until asked**: with the unit
visible the DATAPRINT menu drops straight into the print dialog and there is no
way back to the rest of it.

- Sends `0x05` every 100 ms while connected, suppressed only while a reply is
  going out — the guest's receive state stores every byte until LF, so a stray
  ENQ mid-reply shifts byte 15.
- Answers a frame's ETX with `"DATAPRINT V1.0 C\n"`, paced one byte per 1.5 ms
  (about a byte time at 9600; the whole line at once overruns a UART with the
  FIFO off). The index of the `C` is checked at init and complains in the log if
  an edit moves it.
- Unplugging drops CTS, DSR and DCD as well as stopping the keepalive, so the
  guest sees no cable rather than a device that has gone quiet.
- **Stands down on I.G.O. 8**, whose dongle is a serial card reader on COM2.

Nothing is skipped in bulk on a guess about a dialect. `PEEPEEBOX_PRN_PORT=1..4`
moves the port for a run, `PEEPEEBOX_PRN_ENQ=0` silences the keepalive, and
`PEEPEEBOX_PRN_TEST=<file>` replays a capture through the parser without booting.

### The window

The machine, photographed and painted, with the roll drawn on it rising out of
the slot. The visible roll length is derived from the active screen's usable
work area, so a short display scrolls sooner while a tall one exposes more paper;
once it is full, the earliest lines ride out of sight and a scrollbar inside the
paper retrieves them. The scrollbar stays hidden until the pointer is over the
receipt, and the wheel scrolls anywhere on the paper. The printer stays anchored
above the taskbar and the dialog grows upward with the receipt instead of leaving
a fixed strip beneath it. The printer canvas is measured in physical pixels and counter-scales its Qt
widget coordinates, so Windows at 125% does not enlarge the machine, paper,
print pitch or panel targets. At the vertical limit the canvas stops changing
size and surplus paper
buckles into an uneven stack of folds at the top; its ripped leading edge
remains drawn above those folds. Each platen advance is animated over the
measured 0.20 seconds, with the new line emerging from behind the slot and a
two-pixel damped settling motion at the end. The printer and window remain still;
only the paper and the pressed panel button move. The mechanism is paced from the DPU-414's
52.5-character/s normal-text rating and logical-seek distance rather than a
fixed line delay. The procedural mechanical sound uses the same timing; its
measurements and derivation are in [29](29-dpu414-sound.md).

The panel works: **ON LINE** connects and disconnects and lights the green lamp,
**OFF LINE** lights red, and **FEED** advances the roll by a line only while
OFFLINE — the DPU-414 guide's operation-panel description and hint both make
that restriction explicit. Thus pressing FEED during an ONLINE print does not
splice blank lines into the job. A short press advances immediately and holding
the switch repeats after a short delay, as required while loading and aligning
paper. It is the printer's own paper feed, so an accepted feed goes on the paper
and not down the wire. Both are invisible
buttons over the ones in the photograph, children of the picture so they travel
with it as the roll grows. While either is held, its photographed cap moves into
the panel recess and returns on release.

Tearing is independent of printing. It detaches only the exposed sheet and
removes only device-buffer bytes the UI has already copied; queued lines, bytes
that arrived since the last UI pump, the parser's partial line, and the feed
timer all continue behind the departing receipt. A short blank lip remains at
the cutter, and its shallow edge profile changes deterministically after each
tear instead of every receipt having the same silhouette.

After the last line, an odd bidirectional pass leaves the head away from its
left stop. A delayed carriage-only event returns it home with matching sound and
without feeding paper. If the final pass already ended at home, there is no
invented movement.

The receipt type is likewise measured rather than inherited from the desktop.
The manual specifies a 112 mm roll at 0.28 mm dot pitch, an 89.6 mm-wide head,
a 7 x 9 character matrix, one blank dot between characters and the default six
blank dots between 9-dot rows. The renderer therefore uses a crisp,
un-antialiased typewriter strike, places glyphs on fixed eight-dot cells and
advances lines in the same 15-dot proportion. A uniform 1.2x readability scale
then enlarges the result and recentres all 40 normal columns on the roll. The
spacing remains stable when Windows is using 125% display scaling or substitutes
a different fallback font.

It opens against the right edge of the emulator with its base just above the
taskbar — the real printer stood beside the machine, and the paper is meant to
be watched while the guest is doing something. As the receipt grows, the dialog
extends upward and leaves the printer in place. First show only; after that it
stays where it is put.

Everything in that window is painted into one pixmap rather than laid out. The
paper has a shallow cross-sheet bow, subdued fibres, slot shadow and a slightly
irregular torn leading edge, while the printer photograph remains the measured
reference for its width and exit position. Two attempts at overlapping a picture
and a text widget both failed: the machine was clipped to its top half, then
vanished entirely as the paper grew and squeezed it out. A layout asked to put
one child on top of another at a fixed offset is a layout being used as a canvas.

### The battery pack

A **BP-4005-E**: Ni-MH, 4.8 V, about 120 g, rated at **3000 lines of 40 columns
of the number "8"** (manual §2.10 and §6.1). That is the worst case, every dot
fired — so a pack is 3000 × 42 character-cells, forty columns plus the paper
movement, and **a line costs what is on it**. A space fires no dots and costs
only the motor; a 24-column report line half full of spaces costs about a third
of the manual's line. Draining a flat percentage per line would price a page of
blanks the same as a page of solid print, and the whole point of the manual's
figure is that it is about dots.

The motor is an assumption and a visible one: a feed costs what two characters
cost, so 3000 blank feeds are about 5% of a pack. It is counted into the capacity
so that 3000 full lines comes to exactly one pack rather than 105% of one.

**Each line keeps the level it was printed at.** A receipt that began on a good
pack and finished on a flat one reads that way — black at the top, faint at the
bottom. Colouring the whole roll from the present level would rewrite the earlier
lines every time a new one arrived, which is not what paper does.

Below **12%** the head weakens: the ink fades toward the paper colour and each
line's calculated carriage-and-feed interval stretches toward 1400 ms. The
manual does not quantify that; what it does say is what happens at the end.

### What happens when it runs out

Straight from §2.11, *When the Battery pack Gets Low During Printing*:

- the printer **goes OFFLINE** — the OFFLINE lamp lights and stays lit, and the
  printer drops its connection to the Photo Play;
- the **Power LED blinks once every half second** — the manual's "about once
  every 0.5 seconds" — and it is the only thing blinking to say the pack is low;
- **the ONLINE LED blinks if there is data left in the buffer** — a job that
  arrived and cannot be printed;
- the operator connects the AC adapter and **pushes ONLINE**, and the rest prints.

The two panel lamps are **drawn independently**, which they were not at first:
they were being treated as one indicator that moved between two positions, so a
job stuck in the buffer lit the ONLINE lamp and the OFFLINE lamp was never drawn
at all. That is backwards — the printer is offline, and that is the lamp that
should be on. A machine with two LEDs can light both.

The pack is called flat at **4%**, not zero: a Ni-MH pack driving a thermal head
has no useful print left well before it is empty, and the machine stays *on* at
that point — the manual has it go offline with the lamp blinking, not shut down.

There is **a percent of hysteresis**: it drops offline at 4% and will not print
again until 5%. Without it a pack nursed along on the adapter would drop offline
on every second line at the threshold; with it, it prints a burst, gives out,
charges a little and prints another, which is what nursing a flat pack is like.
Pressing ONLINE below 5% puts it online and the first print attempt puts it
straight back off, which is what the machine does and what the operator sees when
they press it too early. It says so in the log.

Coming back is deliberately two moves — connecting the adapter does not restart
the job, because the manual has the operator press ONLINE and a printer that
resumed by itself would be a surprise.

### On the adapter

**Whenever the adapter is plugged in the print is black**, whatever the pack is
at: 6.5 V at 2 A is more than the pack ever delivers, so the head is driven
properly and runs at full speed too. **Printing on the adapter costs the pack
nothing** — a full pack on the mains stays full. Unplug it and the pack dictates
the ink again.

Those are two separate rules and both hold: being plugged in does not let it
print below 5%, it only means that what it *does* print is not coming out of the
battery. So a pack at 4% with the adapter in charges up to 5%, prints black
without draining, and the charge resumes when the job finishes.

That is why the fade is stored **per line** rather than read from the pack when
the paper is drawn. A receipt half printed on the adapter and half on the pack
has to show both, and there is no single number for the roll that can.

The blink rates are worth stating as periods, because getting them wrong by a
factor of two is easy and it happened: a blink is a whole cycle, on and off, so
with a 250 ms tick a half-second blink is one tick each way and a one-second
blink is two. The first version toggled every half second, which is a
one-second blink rather than a half-second one.

| | period |
|---|---|
| Power LED, pack flat | 0.5 s |
| Power LED, charging | 1 s |
| Power LED, on and idle | steady |
| ONLINE LED, job left in the buffer | 1 s |

Charging is a state rather than a button: the adapter is plugged in, the power is
on, the pack is not full, and it is not printing. Putting the printer back online
with something to print stops the charge until the job is done, which is §2.10's
"charging is temporarily disrupted while the printer is printing". The button
plugs the adapter in and unplugs it again; the title says which of connected,
charging or neither it is.

The buffer is **28,000 characters** (§2.9). Past that it stops growing and says
so once; a real printer would be holding the host off with flow control.

**Charging** is the manual's **ten hours** from flat, so the adapter's 6.5 V /
2 A needs no arithmetic done to it. It pauses while printing and resumes after,
and will not charge with the power off — all §2.10.
`PEEPEEBOX_PRN_CHARGE=<minutes>` shortens it for testing, and the rate is
constant, so half a pack is five hours.

**The pack persists** in `nvr/dpu414.nvr` beside the machine's own nvram, written
at most every couple of seconds while it moves. A pack that starts full every
boot is not a pack. Missing or unreadable means a new one, full.

**The panel**: the Power LED is a lens in the front edge, the dark bar measuring
x 41..66 by y 411..414 — four pixels tall, which is why it is given as edges
rather than an origin and a size. `CP80_SCALE(x) + CP80_SCALE(w)` rounds twice
and the second rounding pushed the lit bar outside a lens that thin; differencing
two scaled edges rounds once and stays inside. The Power *switch* is on the
left-hand side of the machine and so is not in the photograph at all, which is
why it is a labelled button rather than an invisible one on the picture.

There is also a **large battery-shaped pack control**, which is a test control —
3000 lines to run down and ten hours to fill are not things to sit through while
checking what a threshold looks like. Its coloured interior is both the live
level indication and the draggable slider, with the percentage printed inside;
green turns amber below 20% and red at the flat threshold. A sheen distinguishes
active charging. `PEEPEEBOX_PRN_DRAIN=<multiplier>` scales the drain for the same
reason.

### The 2008 reader had to move

`igo8_reader_device` was attached on every image, on the grounds that no other
generation talks to it. True, and beside the point: it *claimed* COM2 regardless,
so on every generation but 2008 the printer found the port taken. It is now
attached for 2008 images and for images that cannot be identified — an unreadable
image keeps the old behaviour, because losing the dongle is worse than losing the
printer.

### Coming online by itself

Opening the Dataprint brings the printer online and puts the paper on screen, so
nobody has to find a switch. Getting there took two wrong signals and one
measurement.

**Not the LCR write.** MENU.EXE programs the port at `0x1D0E5` on its way to the
Dataprint, so an LCR write looked like the operator going looking. It fires
during a plain boot.

**Not polling as such.** The Dataprint screen reads the line status in a tight
loop waiting for ENQ — but so does the attract screen. The software watches that
port continuously, which is also why the printer is found the instant it starts
announcing.

**The rate, though, separates them.** `pp_serial_lsr_read` in `serial.c` counts
reads of a UART's line status; on I.G.O. 6:

| | reads/second |
|---|---|
| attract screen, nothing touched | 20,077–20,078, or 22,757–22,758 |
| after the operator setup button | 29,839–29,840 |

Each is flat to within one count for as long as the screen is up, and the two
idle figures came from different runs of the same image — so the *baseline*
moves between runs while the busy figure did not. `CP80_POLL_BUSY` is 26,000,
which clears the highest idle seen by about 14%. Two consecutive seconds are
required, because the second in which the screen changes reads low (2,242 then
17,992 in that run) and a threshold crossed once on the way past is not a screen
being opened.

Counted against an emulated-time tick, so the ratio does not move with the speed
of the host. `PEEPEEBOX_PRN_POLL=1` reports the rate every second, which is how
those numbers were got and how to check them against another image.

**What this does not yet distinguish** is the operator setup from the Dataprint
screen within it — the rate rose on the setup button and no separate figure has
been taken for the Dataprint dialog. So the printer comes online on entering the
setup, one step earlier than asked for. Harmless, and the ON LINE button
overrides it either way, but it is an approximation and not the thing itself.

## 6. The printer is a Seiko DPU-414

Identified by Marcos; the user's guide is in
`Manuals/THERMAL-PRINTER-DPU-414-USER-S-GUIDE.pdf`. A battery-capable thermal
serial dot printer:

| | |
|---|---|
| Columns | **40 normal, 80 condensed** (§6.1) |
| Speed | max 52.5 cps normal, 80 cps condensed |
| Baud | 75 to 19200, set on SWDIP 3 switches 5-8; **9600 is a setting** |
| Format | data bits, parity and flow control on SWDIP 3 switches 1-4 |
| Flow control | **H/W BUSY or XON/XOFF**, SWDIP 3 switch 4 |
| Buffer | about 28000 characters |

Its ESC sequences (§4) are Epson-like. The three that appear on our wire:

| Code | DPU-414 meaning |
|---|---|
| `ESC "S" n` | set superscript or subscript printing |
| `ESC "K" n1 n2` | set single-density bit-image graphics mode |
| `ESC "C" n` | set page length |

### The PC is not talking to the printer directly

Every one of those readings fails against the capture:

- `ESC K 21 0A` would open a bit image of 0x0A21 = 2593 dots. **Text follows,
  not image data** — the next bytes are "funworld".
- `ESC C` in the trailer is followed by `9E67`, four hex digits of checksum, not
  a page length.
- `ETX` (0x03) closes the command frame and **is not a DPU-414 control code** at
  all; its basic codes are BS, HT, LF, FF, CR, SO, SI, DC2, DC4, CAN and DEL.
- The serial connector table (§6.2) gives pin 2 as "TxD — **XON/XOFF Output**".
  The printer's only outbound traffic is flow control. **It never sends ENQ**,
  and ENQ is what the software waits for and what a missing one abandons the
  transfer over.

So something sits between the PC and the DPU-414 — the Dataprint proper — and
the framing above is its protocol, not the printer's. The 24-column records are
narrower than the printer's 40, which fits: the box formats for its own paper
and the printer just prints what it is handed.

That also means a `ESC K <tag>` in the report is the Dataprint's, whatever it
does with it. Our parser takes two parameter bytes after it and skips no data,
which happens to match the DPU-414's parameter count and renders the capture
correctly either way.

## 7. Open

- **What the `ESC K` tags mean** to the Dataprint. Five values seen, built at
  run time; no literal `1B 4B` in MENU.EXE.
- **What a real unit's reply line contains** in the fifteen bytes nothing checks.
- **Whether the box is a DPU-414 with a funworld interface board**, or a separate
  unit driving a stock printer.
- **What I.G.O. 8 cabinets did**, given their dongle owns COM2.
