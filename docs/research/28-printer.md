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
- Renders the paper, names every control code in a trace beside it, and writes
  every byte to `cp80-raw.bin`.

Nothing is skipped in bulk on a guess about a dialect. `PEEPEEBOX_PRN_PORT=1..4`
moves the port for a run, `PEEPEEBOX_PRN_ENQ=0` silences the keepalive, and
`PEEPEEBOX_PRN_TEST=<file>` replays a capture through the parser without booting.

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
