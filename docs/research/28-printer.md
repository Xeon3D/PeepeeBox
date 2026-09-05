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
