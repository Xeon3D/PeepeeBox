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
