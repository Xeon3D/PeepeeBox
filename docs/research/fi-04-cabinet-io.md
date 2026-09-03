# Phase 4 — What the cabinet actually was, and why touch is dead

**Scope.** Two questions raised by the first working boot: the machine is slow, and the
touchscreen does nothing. Both turn out to be answerable from the image itself.

**Status: diagnosis complete, fix not yet attempted.** The IRQ hypothesis below is
evidence-backed but **untested** — see 4.4.

## 4.1 The cabinet was not a 486

The software carries its own motherboard table, `SOUND\MOBO.CSV`, which `CHECKMB.EXE`
matches against the BIOS ID and writes into `SOUND\CHECKMB.BAT` as `set MB=`:

| BIOS ID | Profile | What it is |
|---|---|---|
| `ABCDEFGH` | `INFM`, `TOM1` | the "no usable ID" fallback |
| `2A4IBL13` | `TOM2` | Award ID; `2A4IB` is SiS 496/497 → a **486** board, the Tomato 4DPS class PeepeeBox emulates |
| `2A5IIC3A` | `5SFV` | `2A5II` is SiS 5511/5512/5513 → **Socket 7 Pentium** |
| `EV69MC0C` | `6WEV` | the PCI generation — see below |

**This image is configured for `6WEV`, the newest of them.** `AUTOEXEC.BAT` sets
`BLASTER=A220 I5 D1 H5 T4` and runs `PCIAUD\SETAUDIO` followed by `C3DMIX` with a parameter
string identical to `MB6WEV.BAT`'s. The 486 and Socket 7 profiles (`MBOTHER.BAT`,
`MB5SFV.BAT`) use `A220 I5 D1 T4` — no 16-bit DMA — and ESS ISA audio instead.

What `6WEV` is, from its own tooling:

* `SOUND\PCIAUD\SETAUDIO.COM` is C-Media PCI audio with an **AC'97 codec** (`PCI Config Base IO`, `Vender ID`, `AC97 Codec Version`) — a PCI board, 1999 at the earliest.
* `SOUND\6WEV\CKUSB.EXE` exists to **disable USB**, so the board has USB.
* `CHECKMB.EXE` reads a hardware monitor: `CPUFAN`, `SYSFAN`, `CPUTMP`, `SYSTMP`, `CPUVOL`, `1.5VOL`, `3.3VOL`.

And the content settles it independently: `GAME\VIDEO` is 595 MB of **Cinepak (`cvid`)
AVI at 640×480, 30 fps**. Cinepak's 486 target was 320×240 at 15 fps; 640×480 at 30 needs
Pentium-class silicon at the very least.

**Conclusion: a Slot 1 / Socket 370-class PCI board with AC'97 audio — Pentium II/III
era.** PeepeeBox pins an iDX4 at 100 MHz, which is why the menu takes about seven minutes
to load. That profile is right for Photo Play and wrong for this cabinet.

One caveat worth keeping: the last real boot (`CHECKMB.BAT`, 2018-03-16 12:26:52) wrote
`set MB=UNKN`, so whatever it was running on *that* day was not in the table at all — newer
again, or a board whose BIOS ID had changed.

## 4.2 The touchscreen: it is the IRQ

`FSYSTEM.EXE` is started as `FSYSTEM.EXE C3 I3`. The switch parser at `CS:0x1946` takes
four letters and stores each in DGROUP:

| switch | stored at | meaning |
|---|---|---|
| `C<n>` | `DS:0x0002` (byte) | COM port |
| `B<n>` | `DS:0x0004` (word) | base I/O address |
| `I<n>` | `DS:0x0006` (byte) | **IRQ** |
| `R<n>` | `DS:0x0008` (word) | baud rate |

confirmed by the status line the program prints —
`Controller found at C:%d B:%d I:%d with %d bit/s` — and by the call at `CS:0x19DC`, which
pushes port, base and IRQ straight into the serial open, then pushes `[0x8]` as a longint
baud rate.

So the cabinet asks for **COM3, IRQ 3**, with base and baud left to default. And it means
it: `CS:0xC3D9` is a general IRQ-enable routine that looks the number up in a mask table
and unmasks either the master PIC (`in al,0x21` / `and` / `out 0x21`) or the slave
(`0xA1`). The touch controller is interrupt-driven.

86Box's defaults, in `src/include/86box/serial.h`:

```
#define COM3_ADDR 0x03e8
#define COM3_IRQ  4
```

The address matches. **The IRQ does not.** And `FSYSTEM.EXE` has a message for exactly
this: `Elo-Touch found, but with no Int.`

## 4.3 It is not a MicroTouch-versus-Elo problem

Worth stating because it was the obvious first guess. `FSYSTEM.EXE` drives MicroTouch
natively — the strings `reset controller  :`, `set uart data     :`,
`set format tablet :`, `set stream mode   :` are the 3M SMT-3 command set, and the search
sequence is `Search elo touch controller...` → `No ELO-Touch found.` →
`Search micro touch controller....`. The Elo path looks for `ELODEV`'s resident interrupt
rather than the hardware; with no Elo controller present, `ELODEV 2310,3,9600,3` in
`AUTOEXEC.BAT` should fail and fall through to the MicroTouch probe, which is the one
PeepeeBox can satisfy.

The hardware the cabinet actually shipped with was an **Elo E271-2310** on COM3 at 9600
baud — that is what the `ELODEV` line says, and `TOUCH\ELO` is a full Elo DOS driver kit
dated 2002. `TOUCH\MT` (3M MicroTouch) is present too, so both were supported in the field.

## 4.4 The IRQ theory is dead, and the transport is proven good

Tested by setting `COM3_IRQ` to 3 and rebuilding. **Touch still does not work.** Then, with
the Elo line blanked as well (4.5) and the MicroTouch device instrumented, the whole
exchange became visible — and every part of it is correct.

Initialisation, all four steps `FSYSTEM.EXE` prints, each acknowledged and each reply read
by the guest:

```
MT: attached to serial port index 2 -> ok
MT: rx 01 52 0D              <SOH>R<CR>        reset controller
MT: reset complete, queueing <SOH>0<CR>
MT: tx 01 30 0D
MT: rx 01 50 4E 38 31 32 0D  <SOH>PN812<CR>    8N1, 9600 baud
MT: tx 01 30 0D
MT: rx 01 46 54 0D           <SOH>FT<CR>       format tablet
MT: tx 01 30 0D
MT: rx 01 4D 53 0D           <SOH>MS<CR>       mode stream
MT: tx 01 30 0D
```

The expected reply is a literal in the guest at `CS:0x52C` — `\x01 '0' \x0D` — byte-identical
to what `mtouch_reset_complete()` queues. And a touch on the *Malen* button produces:

```
MT: TOUCHDOWN at 0.371,0.055  format=4 mode=4 pen=3
MT: tx C0 3B 2F 01 79     (x6)
MT: tx STALLED, guest has not read (lsr=61, 4 queued)   (x2)
MT: liftoff at 0.371,0.055
```

`C0 3B 2F 01 79` decodes as x = 6075/16383 = 0.371, y = 1 − 15489/16383 = 0.055 — exactly
where the click was, inside the button. Six packets went out and were consumed; only the
last two stalled as the press ended.

**So the guest receives well-formed touch packets at the right coordinates and does not
act on them.** The problem is above the driver.

Two hypotheses died cheaply on the way:

* **The reset race.** `FSYSTEM.EXE` waits exactly 500 ms after `R` and 86Box's reset timer
  is also 500 ms, so the reply should arrive too late. It does not matter: the init routine
  at `CS:0x57A` ends in a bare `ret` at `CS:0x92B` and sets **no flag**. The `ok` / `faild !`
  it prints is cosmetic and the app proceeds identically either way.
* **The coin board.** `CS:0x1BFF` and `CS:0x1C59` install two interrupt handlers and then
  print `CoinControl (6chanel) found at port …` **unconditionally**. There is no probe, so a
  missing coin board gates nothing.

## 4.5 The Elo branch is a real trap, even though it was not this bug

Worth keeping because it would have bitten later. `CS:0x16FD` calls the Elo detector before
anything else. If it answers:

```
1709  push [0x24E]           ; ELODEV's software interrupt
177E  mov byte [0x2], 1      ; force the port variable to COM1
```

— the MicroTouch on COM3 is never opened, and the `C3 I3` switches are never even parsed,
because that loop only exists in the MicroTouch branch at `CS:0x18F3`. The auto-search at
`CS:0x11D9` is also unreachable here: it runs only when `ParamStr(1) == "AUTO"`.

`docs/research/evidence/fi-disable-elodev.py` blanks the line with spaces in a working copy — same
length, so no FAT or directory-entry changes — and refuses to touch `Fixed.img`. Diagnostic
only; `docs/research/evidence/fi-stage-image.py` restores.

## 4.6 What has *not* been shown

* **Whether the IRQ matters at all is now unknown.** COM3 is on IRQ 3 in the current build
  and touch still fails, but the trace shows the guest reading bytes either way — so it may
  be polling, and the change may be unnecessary. One log of the UART's IER would settle it.
  Until then, **`COM3_IRQ` is patched globally in `serial.h`, which is wrong as a fix**:
  Photo Play shares COM3. If it turns out to be needed, `serial_init` at
  `src/device/serial.c:1131` is the clean place to make it conditional on the image.
* **What the app does with the packets is not known.** `FSYSTEM.EXE` installs a per-port
  serial ISR (`CS:0xC391` hooks the vector `CS:0xC116` derives from the IRQ, then unmasks
  the PIC at `CS:0xC3D9`) and then execs `FUNNY.DLL`. How the 32-bit half collects the
  buffered touches is not visible — the INT 50h functions decoded so far (`0011`, `0020`,
  `1234`, `1235`, `00FF`) contain nothing touch-shaped, and the rest of the dispatcher has
  not been enumerated. This is now **Phase 5** work: unpack `FUNNY.DLL`.
* Calibration is unexamined. `TOUCH\ELO\ELOGRAPH.CAL` and the `-C7,4074,4062,199,1,255`
  argument on the `ELODEV` line are Elo calibration; whether the MicroTouch path wants an
  equivalent, and what it does without one, is untested.
* Whether the CPU can simply be raised is unknown. `PHOTOPLAY_CPU_FAMILY` is `idx4` on a
  SiS 496 486 board; a Pentium II/III profile means a different machine entirely, which is
  a much larger change than the dongle was, and would need its own ROM.
