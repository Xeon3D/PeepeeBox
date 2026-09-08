# Phase 34 — fun.link: cabinet-to-cabinet play, and the bus it runs on

**fun.link is a serial bus, not a parallel one.** The adapter photographs as a
box with a 25-pin D-sub going to the cabinet and a round DIN going onward, and
the parallel port is the obvious guess because that is where the dongle lives.
It is the wrong guess, and the binaries say so in plain English:

```
LINK ERROR: No serial-port found !!!!. Please check mainboard COM-settings
LINK ERROR: Unable to send on BUS. Please check LINK-adaptor and LINK-cable
```

Two strings, one library, present in every release from 1998/99 to I.G.O. 2.
The link is a **multi-drop half-duplex bus on COM1 at 115200 baud**, arbitrated
in software, with the modem-control DTR line used as the transmit enable.

funworld's own service manual settles the wiring, and names the adapter:

| port | address | IRQ | connector | what is on it |
|---|---|---|---|---|
| COM A | `3F8` | 4 | **25-pin** | **fun.link** |
| COM B | `2F8` | 3 | 9-pin | Data Print |
| COM C | `3E8` | 3 | 9-pin | SMT3, the serial touchscreen controller |
| COM D | `2E8` | 10 | 9-pin | modem (Photo Play MASTERS) |

So the 25-pin D-sub on the box is **COM A's**, not the I/O card's -- which is why
a female DB25 mates with it. The same manual's fault-finding page for "screen OK,
touchscreen does not work" says to check the jumpers on the touchscreen
controller: `Address: A1, A4, A5`, `Interrupt: I4 / fun.link I3`. A cabinet with
an adapter fitted moves its touchscreen to **IRQ 3**, because the link driver
takes IRQ 4 and does not give it back -- see section 3. IRQ 3 is free on these
machines; it was only taken when an ISA MicroTouch bus card stood in for the SMT3
serial controller. The BIOS screen agrees: onboard Serial Port 3 is *Disabled*,
because the SMT3 card provides `3E8` itself.

The protocol below was read out of the shipped binaries. The wiring above is from
the manual; what has not been done is a capture from a real adapter.

## 1. What the product was

`\MENU\ADVERT\STANDARD\FUNLINK.PCX` is funworld's own advertising slide, and it
draws four cabinets — two uprights and two countertops — joined in a ring by
arrows. The ring is the marketing picture of a shared bus, not a token ring:
the software treats every station as a peer on one wire.

The player-facing side is a complete UI, shipped as its own asset tree in
`\FOTO\LINK\` (later `\FOTO\LINK\<year>\`):

| | |
|---|---|
| `LINKLOGO`, `LINKBUT`, `SOLOBUT`, `LNKSOLBA` | the choice between a solo game and a link game |
| `INVITED.PCX` + `PHONE.WAV` | **the other cabinet invites you, and yours rings** |
| `LINKGAME`, `LINKBACK`, `BACKGR`, `GAUGE`, `TIMEBG` | the linked game screen |
| `SCOREBRD`, `LHISCORE`, `LGAMEOVR`, `HISCORE.WAV` | shared scoreboard and hiscore |
| `MSGBOX`, `STOP` | link messages, and dropping out |

The engine also carries a fifth start-screen icon beside `1PLAY`..`4PLAY` —
`\foto\icons\<year>\funlink.pcx` — and the menu's player list has a fifth entry
labelled **"Fun Link"** after "Player 1".."Player 4".

The instruction page for a linked game, `INSTR\LINK.ION`, describes a
head-to-head falling-blocks game whose specials are TetriNET's, item for item:
*Add Line*, *Remove Blocks*, *Clear Specials*, *Gravity*, *Random Blocks*,
*Switch Field*. In 1999 it ships under `\TOUCHDN\` and is fully localised
(`>TDNINSlink2<` and friends); the copy under `\MEMO\` in 2001 still has the raw
template keys (`link2`, `add`, `rem`), so that one is a copy of the template
rather than a second linked game.

## 2. The transport, from the code

The link is one C++ class, linked into `MENU.EXE` and into every game
executable. It is not a TSR, not a driver on disk, and not `INT 60h`.

### The port

The class holds a base address and an IRQ, taken by index from a compile-time
table in the library's data segment:

| index | base | IRQ |
|---|---|---|
| 1 | `0x3F8` | 4 |
| 2 | `0x2F8` | 3 |
| 3 | `0x2F8` | 4 |
| 4 | `0x2E8` | 3 |

(Index 3 really is `0x2F8` in the binary, not the `0x3E8` a PC would use. Read
as written; nothing ever selects index 3.)

**Every build calls the initialiser with index 1 and a baud of `0x1C200`** —
`COM1, 0x3F8, IRQ 4, 115200 baud`. The call is the same byte sequence in 1999,
2000, 2001, I.G.O. 1 and I.G.O. 2:

```
66 68 00 C2 01 00   push dword 0x1C200      ; baud = 115200
6A 01               push 1                  ; port index 1 -> 0x3F8 / IRQ 4
1E 68 xx xx         push ds:offset object
9A xx xx xx xx      lcall link_init
```

That fits the rest of the cabinet exactly: COM3 is the touchscreen and COM4 is
the fun.net modem (`docs/hardware.md`), so COM1 is the port nothing else uses.

### Opening it

A textbook 8250/16450 probe runs first — write `0xAA` to LCR and read it back,
`0x55` to IER, check IIR, `0xF5` to MCR and expect `0x15`. If it fails, the
game prints *"No serial-port found"*. Then the divisor is computed as
`115200 / baud` (so divisor 1), written to DLL/DLM with DLAB set, DLAB cleared,
and LCR set to `3` — **8N1**. An ISR is hooked on the IRQ, and reception is
interrupt-driven: the handler polls `LSR & 0x01` (DR), pushes each byte into a
**1024-byte ring buffer** in the object, and calls a virtual byte handler.

### Half duplex, with DTR as the transmit enable

Sending one byte is:

```
outportb(base+4, 0x0B)              ; MCR = DTR | RTS | OUT2   -- assert DTR
delay
wait for LSR & 0x40 (TEMT), up to 10000 spins
outportb(base+0, byte)              ; THR
delay
outportb(base+4, 0x0A)              ; MCR = RTS | OUT2         -- release DTR
```

DTR is raised for the duration of a transmission and dropped afterwards. That
is what a half-duplex differential driver wants, and it is the strongest single
hint about what is inside the fun.link box: **a line driver whose enable sits on
DTR**. Whether the wire itself is RS-485, a current loop, or something funworld
rolled themselves is not established here — the box has not been opened.

### Arbitration

Before a frame the sender watches its own receive counter. While that counter is
moving, the bus is busy: it waits three ticks, and if the counter changed it
backs off `rand() % 90 + 10` ticks and tries again, up to 50 times. Fifty
failures is *"Unable to send on BUS"*. This is CSMA with random backoff — no
master, no token, every cabinet equal.

### Framing

A frame is built as:

- **16 bytes of `0x55`** — the preamble (`rep stosw` of `0x5555`, 8 words);
- a **24-byte header**, which both magics belong to;
- the payload.

The header, read off the wire and confirmed against the builder at `0x19F86`:

| offset | | |
|---|---|---|
| `+0x00` | `"PHDR"` | `0x52444850` |
| `+0x04` | length | of the payload, not the frame |
| `+0x08` | station | the sender's own number |
| `+0x0C` | word | the caller's, unchanged in transit |
| `+0x10` | checksum | over the payload, from `0x110F:0x0008` |
| `+0x14` | `"PHND"` | `0x444E4850` |

So `PHND` closes the *header*; there is no trailer. The receiver at `0x19E55`
collects `length` bytes, checksums them as they arrive, and compares its running
value against `+0x10`: equal calls the object's `vtable[0x1C]`, different calls
`vtable[0x10]`. Both ends of a linked Touchdown produce headers that pass this,
so the framing here is right.

The receiver keeps a rolling 32-bit shift register (`>>= 8` per byte) and matches
the magics out of the byte stream, which is why the `0x55` preamble is there — it
gives the far end's UART something to sync on before the header arrives.

### What turns it on

The menu launches a game as `\EXE\<name>.EXE /IPX=<n>`. The game parses `/IPX=`
into a 32-bit value and logs it as `fotoplay_netipx %ld`. **The link object is
only constructed and opened when that value is non-zero** — a game started
without it never touches COM1. The debug line `Linkplayers: %02d` reports how
many stations answered.

### And what turns *that* on: a second iButton, in the adapter

The station number is not a setting. It comes out of a **DS1982 on COM1** — the
same port the bus runs on — and the menu will not touch fun.link without it.

Before any link init, the 1998/99 menu constructs a 1-Wire object, points it at
port **`0x03F8`**, and drives it at divisor 11 (~10.4 kbaud): `0xF0` reset,
`0x33` READ ROM, `0xFF` read slots. It then makes three checks, and all three
must pass:

| | |
|---|---|
| ROM byte 0 | `0x91` |
| ROM word at offset 5, `>> 4` | `0x5E7` |
| memory page byte 0 | `0x37`, with the dword at offset 1 `0x112A` |
| memory page from offset 5 | the literal blob `Seiringer<CR>Pichler<CR>Hutmacher<CR>Obermair<CR>Lukarsch<CR>` |

Only then is the dword at ROM offset 1 taken as the station id, passed through
`INT 60h` with `AX=0x0507`, and — if it survives that — used to open the bus at
115200 and later handed to games as `/IPX=`. Zero at any step and the menu
silently has no link.

`push dword 0x03F8` into that port setter appears exactly once in the 1998/99,
2000 and 2001 menus and **not at all** from I.G.O. 1 on, which is the same
boundary as the rest of the link session.

**This is what the fun.link box is.** It is not a cable with line drivers in it:
it carries the token that licenses the feature and gives the cabinet its number
on the bus, on the same wire pair, separated from the 115200 traffic by baud
rate alone. The cabinet's own DS1982 is a different part at a different address
(`0x268`, which every generation still uses) — there are two.

**PeepeeBox answers it.** `char_funlink.c` is the token as well as the wire: a 1-Wire slave carrying exactly the record above, on the same COM1 the bus runs on. The two cannot be told apart by baud -- the reset pulse is slow but the slots run at the bus's own 115200 -- so the rule is what each protocol actually puts on the wire. A reset is `0xF0` sent slowly and nothing else the cabinet does looks like that; after one, every slot is `0x00` or `0xFF` and nothing else, and the first byte that is neither is the link driver starting to talk. A transaction that has gone quiet for a second also releases the port, so a stale 1-Wire mode cannot swallow the first byte of a frame.

The station number is the one part of the record that is ours rather than funworld's, because each cabinet had its own adapter and two stations answering to the same number cannot be told apart on the bus. Left at 0 it follows who ended up hosting -- 1 for the host, 2 for a client, which is right for the usual pair -- and a third and fourth cabinet set it by hand.

Measured: both cabinets read the token, take distinct numbers, and the link driver comes up behind it -- its 8250 probe passes, the port lands on 115200 8N1, and MCR settles at `0x0A`, the driver's idle state. Whether the fifth icon then appears on a game's start screen is the on-screen test.

The name is a fossil: there is no IPX stack anywhere on any image — no `IPXODI`,
no ODI MLID, no `INT 7Ah` in any binary. funworld modelled the API on IPX and
kept the word.

## 3. Which images can actually do it

**Carrying the driver is not enough — the menu has to launch a game in link
mode.** That is a separate piece of code and it was removed a generation before
the driver was, so there is a middle band of releases that look linkable and are
not. Two tests, in this order.

### The menu's side: can a linked game be started at all?

`MENU.EXE` launches a game as `\EXE\<game>.EXE /IPX=<station>`, and the game
opens COM1 only when that number is non-zero. So the question is whether any of
the launcher's call sites passes something other than a literal zero.

- **1998/99**: the launcher has four call sites. Two push `66 6A 00` — `push
  dword 0`, an ordinary launch — and one pushes `dword [bp-0xC]`, a variable.
  That is the link launch.
- **I.G.O. 2**: the launcher has **two** call sites and **both push a literal
  zero**. No game is ever started in link mode.

The link *session* — the invitation, the ringing, the station bookkeeping — goes
with it. `LINKCOPY`, `\menu\pictures\funlink.pcx` and `\foto\link\phone.wav`
are all in the 1998/99, 2000 and 2001 menus and all absent from I.G.O. 1 and 2.

| menu link session | |
|---|---|
| 1998/99, 2000, 2001 (incl. Masters) | **present** |
| I.G.O. 1, I.G.O. 2 | gone |
| I.G.O. 3 onward | gone, and so is the driver |

The `Fun Link` string still in the I.G.O. 2 menu is not an offer, it is a
**statistics label**. `GAMES.DBF` carries a `PLAYERL` column counting sessions
played in link mode, displayed beside `PLAYER1`..`PLAYER4` as "Player 1".."Player
4", "Fun Link". It reads **zero in every image in the collection**, on every
generation — none of these cabinets ever played a linked game.

(`GAMES.DBF` also has a logical field called `LINK`, and it is a trap. It marks
rows that alias another executable — `FQ_SPOR`→`FUNQUIZ`, `FS_AT`→`FSCHEIN`,
`SPACEACE`→`PIZZA` — and has nothing to do with fun.link.)

### The games' side: is the driver linked in?

Counting `LINK ERROR: No serial-port found` per image is a clean test, because
the string exists only inside the driver. Every hit is one executable.

| generation | executables carrying the driver |
|---|---|
| 1998/99 | 24-25 — **all of them** |
| 2000 | 30 — all |
| 2001 (incl. Masters) | 37-43 — all |
| I.G.O. 1 | 32-36 in DE/IT/NL; **2** in BE |
| I.G.O. 2 | 32-36 in DE, GR, IT-75G75, NL-85A99; **0-2** in BE-82C81, BE-EA881, IT-EDA21, NL-EDA22, **and 0 in the PT cabinet** |
| I.G.O. Italy | 0 |
| I.G.O. 3, 4, 5, 6, 7, 8 | **0 — the driver is gone** |
| Touchtoy / Junior | 1-2 |

Two things follow.

**The driver was dropped, not the feature.** I.G.O. 4 still ships the whole
`\FOTO\LINK\2002`, `\2003` and `\2004` asset trees, and its `MENU.EXE` still has
the "Fun Link" label and the ` /IPX=%ld` launcher — but the serial code is not in
the binary any more. From I.G.O. 3 onward, fun.link is dead weight on the disk.

**The two I.G.O. 2 engine builds differ.** `AMORE.EXE` at 221,718 bytes has the
driver; the newer 230,320-byte build does not, and its link class is a vtable of
do-nothing stubs — the five entry points the game calls are `return 0` and bare
`retf`. Disassembling that build first is what made this look, wrongly, like a
feature that had never been implemented at all.

The I.G.O. 2 PT cabinet image is the 230,320-byte build. But that distinction
turns out not to matter: **no I.G.O. 1 or 2 image can link at all**, because
whichever engine build its games carry, its menu will never launch one in link
mode. Test on 1998/99, 2000 or 2001.

### Which games

In every release the link screens themselves are engine-wide, so what
distinguishes a game is link art and code of its own. Diffing each executable's
link strings against the set shared by all of them isolates that, and the method
checks out on 1998/99, where `TOUCHDN` is independently known to be a link game
— it is the only one with a localised `INSTR\LINK.ION`, a head-to-head
falling-blocks game whose specials are TetriNET's item for item.

| release | games with link code of their own |
|---|---|
| 1998/99 | `TOUCHDN`, `AMORE`, `CONCENT`, `PYRAMID`, `JEOPARDY` (and `MASTERS`, which has " Link Diagnostics" — an operator tool) |
| 2001 | every game references the link screens; these add their own art: `AMORE`, `CONCENT`, `ESCOBA`, `FSCHEIN`, `GUINNESS`, `KREUZ`, `ONE`, `PYRAMID`, `SAME`, `SHANGHAI`, `SWIM`, `TOWERS2` |

Two of them name the mode outright and cannot be anything else: `ESCOBA` has
`Link-Error, Playerindex above 2!` and `SWIM` has `Link error - wrong cards
choosen`. Watch for false positives on a bare substring search — `EggBlink.pcx`
and `Clink.wav` are not link assets.

## 4. What PeepeeBox does with it

The emulation problem turned out to be small and well shaped, because the bus is
ordinary 8250 traffic. `src/char/char_funlink.c` is the whole of it:

- **The port was free.** ppbox uses COM3 for the touchscreen and COM4 for the
  modem, so COM1 at `0x3F8`/IRQ 4 was unclaimed and 86Box already had the UART.
  The adapter is fitted from **Tools → fun.link…**, persisted as `funlink` in
  `[Photo Play]`.
- **The wire is a TCP connection.** One instance listens and the others connect;
  the listener repeats what it hears from one peer to all the others, which is
  what makes three and four cabinets a bus rather than a pair of pipes. On the
  default setting an instance tries to connect and becomes the listener if
  nothing answers, so two copies pointed at the same address find each other
  whichever starts first.
- **DTR is ignored.** Gating transmission on it would drop bytes — the byte sits
  in the emulated THR for a bit-time after the guest has already lowered the
  line — and the enable only exists to stop two drivers fighting over one pair,
  which a TCP stream has no equivalent of.
- **Collisions never happen.** The guests still run their backoff; they just
  always win first time, which is the good case on real hardware too.
- **Received bytes are paced at a character time each**, which is not a detail.
  `serial_receive_timer()` asks the attached device for a byte once per *bit*
  time and writes whatever it gets straight into the receive register, so a
  device that answers every call feeds the guest ten bytes for every one a
  115200 line carries. A modem never notices. This bus does: it is CSMA with no
  master, and a cabinet decides the wire is free by watching how long its own
  receiver has been quiet. Delivered ten times too fast, a 473-byte frame that
  should hold the wire for 41 ms clears in four, the other 37 look idle, and both
  cabinets transmit into each other. `funlink_read()` therefore counts bit times
  and hands over one byte per character, from the port's own `data_bits`,
  `stop_bits` and parity.
- **A cabinet does not hear itself**, because that is the guess the box has not
  been opened to settle. `echo` in the device's options switches it.

What is not yet known, and would need either the box opened or a capture from two
real cabinets:

- the DIN pinout, and whether the electrical layer is RS-485 or a current loop;
- whether the box is passive line drivers or has a microcontroller of its own —
  if it repeats or re-times frames, an emulated pipe is not equivalent;
- how `MENU.EXE` chooses the `/IPX=` value;
- what the two stations are each waiting for once the invitation has been
  accepted — see below.

None of it blocked the implementation, because the cabinets talk to each other
rather than to us: a transparent bus between COM1s carries payloads nobody here
has to understand. What is verified so far is the wire and nothing above it —
two instances find each other, a third joins, and a frame sent by one arrives at
the others and not back at its sender. Whether two guests then agree to play is
the on-screen test, and it needs one of the images from §3.

### What two cabinets actually said to each other

The device can write a timestamped trace of both directions to
`funlink-capture.txt` beside the executable (`capture` in its options). A linked
Touchdown between the two 1999 rigs, taken to the point where both screens go
black, produced this — the first thing here that is a measurement of the
protocol above the wire rather than of the wire.

Station 1 invites. Every ~79 ms it puts out the same 433-byte payload: a `0x0907`
word, its own station number, `"TOUCHDN"`, the player's name, and twenty name
slots of which nineteen are the game's `UNKNOWN` placeholder, followed by a table
of the dwords 0..15. Nothing in it changes, before or after the other cabinet
answers.

Station 2 receives all 133 of them intact, and 6.5 s in — the player pressing
accept — begins replying, also every ~79 ms, with the same six bytes: `97 57`
then its own station number. That never changes either.

So both ends are framing correctly, both are being heard, every header passes its
own checksum, and neither state machine moves. Two things in the trace say why
the timing is what is wrong rather than the content:

- **both stations transmit at once**, repeatedly, with receive and transmit
  bursts sharing a millisecond — which a half-duplex pair cannot do;
- **a station abandons its own frame mid-header** and restarts from the preamble
  28 bytes in, twice, each time while a burst from the other cabinet is landing.

That is a bus whose arbitration never settles, and the cause was on this side:
receive was running at ten times line rate (above). The lobby loop at `0x1CA94`
gives it 60 seconds — timer 3 against `0x3C`, or an early exit when the byte at
`[bp-0x141]` reaches 2 — which is the window the two have to agree in.

## 5. Where the evidence is

Offsets are file offsets in `\EXE\TOUCHDN.EXE` from `1999\1999AT-81519_`, which
is the richest build of the library.

| claim | where |
|---|---|
| the two `LINK ERROR` strings | `0x29897`, `0x298E2` |
| UART probe (`0xAA` / `0x55` / IIR / `0xF5`) | `0x16F22` |
| divisor = `115200 / baud`, LCR = 8N1 | `0x1703C` |
| MCR `0x0B` / `0x0A` around each byte | `0x19A73`; MCR writer at `0x1713E` |
| LSR accessors (DR `0x01`, TEMT `0x40`) | `0x17100`, `0x17111` |
| CSMA backoff, 50 attempts | `0x19B34` |
| `0x55` preamble, header builder | `0x19F86` onwards |
| header check and checksum compare | `0x19E55`–`0x19F1F` |
| the link lobby loop, and its 60 s | `0x1CA94`–`0x1CCBA` |
| its step names (`SEND NETGAME DATAS` …) | `0x29CA7` onwards; DGROUP at `0x24C20` |
| receive ISR and 1024-byte ring | `0x19D86`, ISR tail at `0x1988C` |
| port / IRQ table | `DS:0x1C5E` / `DS:0x1C68` (`f803 f802 f802 e802 .. 0400 0300 0400 0300`) |
| init call, port 1 at 115200 | `0x1CEAE`; the same byte sequence appears in the 2000, 2001, I.G.O. 1 and I.G.O. 2 builds |
| the all-stub build | `IGO 2 PT` `\EXE\AMORE.EXE`, vtable at `DS:0x22DA`, called entries `:0004 :004E :0096 :009B :00A0` |
