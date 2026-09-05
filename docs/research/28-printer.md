# 28 — The receipt printer

The cabinets could carry a printer instead of the I.G.O. 8 serial dongle
(Marcos, 2026-09-05). This is what is known, what is guessed, and what the
emulation does about the difference.

## 1. How it is wired

From the DB15 trace in [27](27-io-card.md#16):

- The I/O card's DB15 has **two pins that join a DB9**.
- The DB9's **other three pins go to a serial port** — TxD, RxD and ground, the
  minimum for a serial device.
- So the card's two pins are the **12 V and ground that power the printer**, and
  the data path never touches the 8255.

Which means the printer is an ordinary RS-232 peripheral that happens to be fed
from the I/O card. Nothing about it needs the funworld card emulated, and
nothing about the card needs the printer.

## 2. What it speaks is not known

A CP80 is an 80-column dot matrix printer of the right era, and those are Epson
**ESC/P** almost without exception. But no manual has turned up, and a kiosk
receipt printer could as easily be **ESC/POS**. Both are ESC plus a command byte
plus parameters, so one parser covers the shape of them; the two disagree on
the *contents*, and some codes take a different number of parameters in each:

| Code | ESC/P | ESC/POS |
|---|---|---|
| ESC E | bold on, no parameter | emphasise, one |
| ESC G | double strike, none | double strike, one |
| ESC M | 12 cpi, none | select font, one |
| ESC p | proportional, one | drawer pulse, two |
| ESC c | not used | ESC c 3/4/5, two |

**Taking the wrong length desyncs the stream**, and everything after it becomes
noise that looks like data. So only codes the two dialects *agree* on are
parsed. The rest are named in the trace and stop the sequence, which is worth
more than rendering them: any one of them appearing in a real capture identifies
the dialect on its own.

## 3. What the device does

`src/device/prn_cp80.c` attaches to **COM1** by default and stands down if
something else claimed the port first. It asserts CTS, DSR and DCD at init —
a serial printer that never says it is ready is a guest that waits for it
forever, and a run producing no bytes would look exactly like a guest that never
wanted to print.

Three outputs, and the second and third matter more than the first for now:

1. **The paper** — the rendered text. Printable bytes, LF, CR, FF, HT and BS.
   Bytes above 0x7F go through **code page 437**, without which a German receipt
   is unreadable and this software is German on the image we have. `ESC R`
   selects a national set that also remaps some ASCII codes; that is not applied,
   but the selection is traced, so wrong umlauts will say why.
2. **The trace** — every control sequence, named where known and flagged where
   not.
3. **`cp80-raw.bin`** — every byte, flushed as it arrives, because the
   interesting runs are the ones that hang.

Both buffers are capped (4 MB and 1 MB) and say so once rather than growing
without limit.

## 4. Seeing it

The **Receipt printer** window shows the paper above and the control codes below.
It puts itself on screen the first time a byte reaches the printer — a receipt
that arrived while nobody had the window open is a run wasted — and there is a
toolbar button for it as well. Tear off empties both panes; Save paper writes the
text out.

`PEEPEEBOX_PRN_TEST=<file>` feeds a file through the parser at start-up as though
the guest had printed it, and dumps the result to the log as well. Two uses: it
proves the window works before the cabinet has ever printed, so **"the printer
does nothing" can be told apart from "the window does nothing"** — the exact
distinction the first port B run cost a session to learn — and once a real
`cp80-raw.bin` exists, feeding it back is how a parser change gets checked
without booting anything.

Verified against a synthetic ESC/P receipt: umlauts through CP437, tab stops,
a bare CR as a line end, bit-image data skipped rather than printed as garbage,
`ESC d` feeding, `GS V` cutting, form feed, and `ESC E` correctly refused as
ambiguous.

## 5. What is needed next

A **real capture**. Get the cabinet to print anything at all and `cp80-raw.bin`
settles the dialect in one run — at which point the parser can be finished
properly instead of hedging between two.

Open: whether the printer is on COM1 on the real cabinets. COM1 is the default
because that is where it was asked for; COM3 is the touchscreen on the I.G.O. 6
rig and COM2 is free, so all three are available and the device takes a `port`
setting.

## 6. Which port, and telling apart two ways of finding nothing

The guest puts up a dialog that waits for a printer and never leaves it, so the
software **is** looking for one -- the question is where.

The port is now a setting: **COM1 and COM2** (the default), COM1, COM2, COM3 or
COM4, on a dropdown in the printer window and overridable for one run with
`PEEPEEBOX_PRN_PORT=1|2|3|4|both`. A serial attachment is made once at machine
start, so the dropdown takes effect on the next hard reset and says so.

Listening on both by default is not a hedge. A guest stuck on "waiting for
printer" does not say which port it was looking at, so listening on both and
letting the log name it answers in one run what would otherwise take four.

**DTR is reported as well as data**, and that is the part that matters:

| Log says | Diagnosis |
|---|---|
| DTR raised, no bytes | **Right port, wrong protocol.** The software opened it and is waiting for something back -- a status reply, most likely. |
| No DTR, no bytes | **Wrong port.** It is not looking at COM1 or COM2, and LPT is the next place. |
| Bytes | `cp80-raw.bin` settles the command set. |

With only the byte stream to go on, the first two produce identical empty
captures. That is the same trap as the port B run in [27](27-io-card.md#17),
where "no counter moved" and "no coin was pressed" logged the same way -- an
instrument that cannot tell its own null result from not being used is not an
instrument.

## 7. It is a DATAPRINT, and it has to announce itself

The dialog is `DATA-PRINT — Connect the interfaces of the Dataprint to the Photo
Play`, reached from a DATAPRINT menu whose entries are bookkeeping, statistics
and hiscores. So this is not a printer the software drives blind; it is a
funworld accessory it expects to find on a port.

`/MENU/MENU.EXE` on the I.G.O. 6 image (307,374 bytes, plain 16-bit MZ, not
packed) settles the rest. Nearby strings: `DATAPRN.INI`, `can't open
DATAPRN.INI`, `Geraete-Nr.: %ld`, `serialnumber: %ld`, `PR %02u.%02u.%02u`, and
a 24-dash rule — so the printout is **24 columns**, a narrow till roll rather
than the 80 a CP80 suggests. There is **no `DATAPRN.INI` on the disk**, so the
built-in defaults are what run.

There are no `COM`, `LPT` or baud strings anywhere in the binary. The port is
hard-coded, and `mov dx, imm` finds **0x2F8 three times and nothing else**.

### The UART setup, at file offset 0x1D0E5

```
mov al,0x80 ; mov dx,0x2FB ; out   -- LCR, DLAB on
mov al,0x0C ; mov dx,0x2F8 ; out   -- divisor low  = 12
mov al,0x00 ; mov dx,0x2F9 ; out   -- divisor high = 0
mov al,0x03 ; mov dx,0x2FB ; out   -- 8 data bits, 1 stop, no parity
```

115200 / 12 = **9600 baud, 8N1, on COM2 (0x2F8)**, programmed directly — one
`int 14h` exists in the whole file and it is not here.

### The detection, at 0x1D12E

```
call 0x1D102        ; in al, 0x2FD   -- LSR
test al, 1          ; a byte waiting?
jne  ...            ; no  -> return, dialog stays up
call 0x1D112        ; in al, 0x2F8   -- RBR
cmp  al, 5          ; is it 05?
jne  ...            ; no  -> return, dialog stays up
```

That is the whole test. **The Dataprint sends ENQ (0x05) to the host
unprompted**, and the host looks for one already sitting in the receive
register when the operator opens that menu. A device that only ever listens is
never found — which is exactly what the first version did, and why the dialog
never went away no matter which port it was on.

### What the emulation does now

Defaults to **COM2**, and sends `0x05` every 250 ms. It hushes for two seconds
after the guest sends anything, so a print job is not interleaved with
announcements; what the real unit does there is unknown, and corrupting the
capture we are trying to read would be a poor trade. `PEEPEEBOX_PRN_ENQ=0`
turns it off, which is also the way to check that the detection really is what
is being satisfied.

### Still to find

What the guest sends **after** detection passes. That is the print protocol
proper, and it will land in `cp80-raw.bin` and the trace pane the moment the
menu gets past the dialog. Whether it is ESC/P, ESC/POS or something funworld
invented is still open — but at 24 columns and with an ENQ handshake, a plain
dot matrix dialect is looking less likely than a small framed protocol.

## 8. The first frame, and why it was sent three times

Past the dialog, the guest asked what to print and sent 21 bytes — which is one
seven-byte frame, three times:

```
11 1B 53 13 03 0A 0A      XON  ESC 'S'  XOFF  ETX  LF LF
```

Not ESC/P and not ESC/POS. `DC1 … DC3` bracketing an `ESC <letter>` with `ETX`
after it is a small framed protocol of funworld's own. The body is a string
constant in MENU.EXE at file offset 289737, stored NUL-terminated as
`1B 53 13 03 0A 0A 1B 43` — so there is an `ESC C` on the end of it that was
never transmitted, which is itself the clue.

### Three identical frames is a retry, not a print job

The send loop at `0x1D47D`:

```
call read_LSR ; test al, 1 ; je skip
call read_RBR ; cmp  al, 5 ; jne skip
mov  dword [timeout], 0        -- an ENQ resets the watchdog
skip:
cmp  dword [timeout], 0x5DC    -- 1500 without one and it gives up
```

**ENQ is a keepalive for the whole exchange, not a hello.** The unit is expected
to keep announcing while the host talks to it, and the host abandons the frame
if 1500 ticks pass without one.

§7's implementation hushed for two seconds whenever the guest sent us anything,
on the reasonable-sounding theory that a device would not chatter over an
incoming print job. That stopped the announcements at exactly the moment the
watchdog started counting: frame, silence, timeout, retry, three times, give up.
The capture was the sound of our own hush.

ENQ is now continuous at 100 ms and never hushes. `PEEPEEBOX_PRN_ENQ=0` still
turns it off, which is how to confirm this is the mechanism rather than
something that merely correlates with it.

### The port is settled

COM2, fixed. MENU.EXE programs `0x2F8` by hand and holds no other port as an
immediate. "COM1 and COM2" survives as a setting for an image that turns out to
differ, but it is the wrong thing to run now — the keepalive would be pushed at
a port the cabinet never had a Dataprint on.

### Unplugging

Once the software can see the unit, the DATAPRINT menu drops straight into the
print dialog, and there is no way back to the rest of it. So the toolbar has a
**Dataprint connected** toggle. Unplugging stops the keepalive *and* drops CTS,
DSR and DCD, so the guest sees what it would see with no cable rather than a
device that has merely gone quiet. The toolbar is authoritative across a hard
reset, which rebuilds the device plugged in.

### Next

`ESC S` is presumably select or status, and the unanswered `ESC C` on the end of
that constant is the next thing to understand. What the unit is supposed to send
back beyond ENQ is still unknown — the frames will now get through, so whatever
the guest does after a frame it does not abandon is the next piece of evidence.

## 9. The handshake, read out of the state machine

The protocol engine is the state machine at `0x1E8FC` in MENU.EXE: a state in
`si`, a jump table at `cs:0x97BB`, one step per call. Reading its states in
order gives the whole exchange.

| At | State does |
|---|---|
| `0x1E92B` | drain: while LSR bit 0, read RBR — flush anything stale |
| `0x1E9A7` | wait for **ENQ**: LSR bit 0, RBR == 5, else abandon |
| `0x1E9FD` | LSR bit 5 (THR empty), send one byte — the `0x11` XON |
| `0x1EA1A` | send a **6-byte buffer**, `1B 53 13 03 0A 0A`, until index == 6 |
| `0x1EA4A` | **receive a line**: store each byte until one is `0x0A` |
| `0x1EA81` | `mov al,[bp-0x4AF]` — take **index 15** of that line |
| `0x1EAEA` | `cmp byte,0x43` — it must be **'C'**, else `si = 8`, the error state |
| `0x1EA8C` | send the report body, byte at a time, counting to `[0x1355]` |
| `0x1EACC`, `0x1EB4B` | send a 7-byte trailer |

`di` accumulates a running sum of every byte sent, and at `0x1EAF0` it is
rendered as four hex digits through the `"0123456789ABCDEF"` table copied in at
`0x1E917` — so the trailer carries a **checksum**.

That also explains the frame: the 6-byte send is exactly the first six bytes of
the string constant at 289737, and the `1B 43` that follows it there is a
separate 2-byte string, not an unsent tail. `ESC C` is presumably what the reply
is built around, given 'C' is the byte the host checks.

### The reply

**Byte 15 is the entire test.** Nothing else in that function reads the receive
buffer, so the rest of the line is ours to make readable rather than to guess
at: `"DATAPRINT V1.0 C\n"`, sixteen characters with the C at index 15.

What a real unit puts in the other fifteen is unknown and probably identifies
it — `Geraete-Nr.: %ld` and `serialnumber: %ld` sit near the DATAPRINT strings,
so a device number likely lives in that line. Inventing a plausible serial
number would only make a wrong guess harder to spot later, so it says what it is
instead. The index is checked at init and complains in the log if an edit moves
the C off it.

### Two things the reply forced

It is **paced one byte per 1.5 ms**, roughly a byte time at 9600. Seventeen
bytes pushed into the receive register at once is an overrun on a UART with the
FIFO off, and the guest reads them one at a time.

And the **ENQ keepalive is suppressed while a reply is going out** — the receive
state stores *every* byte until LF, so an ENQ landing mid-reply shifts byte 15
and the check fails on a reply that was otherwise correct. That is a few tens of
milliseconds against a watchdog measured in hundreds, which is the difference
between this and the blanket hush that broke §7.

### On the name

Marcos found a manual for a **DataCard CP80**, which is a plastic card printer.
That does not obviously match a unit that prints book-keeping onto a 24-column
roll, so either the cabinet's accessory is not what the name suggests or the
name came from somewhere else. It does not change any of the above: all of it is
read out of what this software actually does, and the manual can only refine the
fifteen bytes of the reply that nothing checks.

## 10. It printed — the report, and what the tags are

The reply was accepted and the guest sent the whole thing. Captured on I.G.O. 6,
886 bytes:

```
------------------------      <- ESC K 21
funworld                      <- ESC K 22
Photo Play 2000               <- ESC K 24
serialnumber: 85
------------------------
Book-keeping
------------------------
ACTION DATE        AMOUN      <- ESC K 26
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
------------------------      <- ESC K 42
Total: EUR        894,00
------------------------
 Attention ! Datas
 will not be erased !
------------------------
04 1B 43 39 45 36 37 16       <- EOT, ESC C, "9E67", SYN
```

Every line is exactly 24 characters, as `cmp $0x17` on a zero-based index
predicted, and the trailer is the checksum `di` accumulates at `0x1EA9E`
rendered through the hex table — `9E67`.

The coin and note lines are the ten channels of [27](27-io-card.md#11), and the
amounts are the ones the money buttons booked during that testing. The report is
a second, independent readout of the same map.

### ESC K is a record tag, and it cost us the first capture

The first attempt rendered nothing because `ESC K 21 0A` was read as an Epson
**bit image** — two count bytes making 0x0A21, so 2593 bytes were skipped, which
was the entire report. That is what guessing a dialect costs.

`ESC K <tag> LF` prefixes a line: `!` before "funworld", `"` before the machine
name, `$` before the serial number, `&` before the first transaction, `B` before
the total. They are not line numbers, and **no literal `1B 4B` exists in
MENU.EXE** — they are built at run time. What they mean is still open; they are
consistently three bytes with the LF belonging to the tag rather than to the
text, and they are now consumed rather than printed.

Bulk skipping is gone from the parser entirely. An unrecognised sequence costs a
line in the trace and the stream carries on.

### On the CBM-910

The 24-column format now fits: the CBM-910 ships in 24- and 40-column variants,
and this is 24. But the link the PC drives is the **Dataprint's** protocol, not a
printer's — ENQ keepalive, XON/ESC S/XOFF/ETX framing, a reply line checked at
byte 15, and an EOT/ESC C/checksum/SYN trailer. No printer speaks that.

Where a CBM-910 manual would settle something:

- **Does it define `ESC K` with a single-byte parameter?** If yes, the Dataprint
  is passing escapes through to it and the tags are printer commands.
- **Does it define `ESC C`?** Here it introduces four checksum digits. If the
  manual gives it another meaning, the trailer is funworld's, not the printer's.
- **Does the printer ever send ENQ to the host unprompted?** If not, the ENQ
  keepalive is the Dataprint box, and the printer inside it is invisible to the
  PC — in which case the manual settles the column width and character set and
  nothing else.

`sample-receipt.bin` on the rig is now this real capture rather than a synthetic
one, so `run-printer-test.cmd` replays an actual report through the parser.
