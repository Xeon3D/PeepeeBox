# 30 — DATAprint 3000 alternate printer

The official `DATAprint 3000er Serie` manual in `Manuals/Dataprint` establishes
that the two 3000-series models share the data-collector enclosure but not the
printer mechanism:

| Model | Mechanism | Paper | Power |
|---|---|---|---|
| DATAprint 3000S | Seiko LTP 3245 thermal | 57 mm thermal roll | rechargeable cells only |
| DATAprint 3000 | Epson M-160 impact dot matrix | 57 mm plain-paper roll and ribbon | rechargeable cells, batteries, game or adapter |

The DATAprint 3000 is therefore not another skin for the DPU-414.  Epson's
M-160 data gives the missing print geometry: 24 columns, a 5 x 7 face, 3.3 mm
line spacing and 0.7 line/second.  Those figures happen to fit the 24-column
records already recovered from Photo Play exactly.

## Simulator implementation

PeepeeBox now offers two mechanisms from **Tools > Printer type**:

- **Seiko DPU-414** retains the existing photographed thermal-printer model.
- **NSM DATAprint 3000** uses the same proven 9600-baud DATAPRINT controller and
  parser, but presents a separate, procedurally drawn 3000-series enclosure and
  feeds its 24-column plain-paper receipt at the M-160 rate.

The 3000 drawing is a measured top view rather than a perspective impression.
It uses a literal 3 screen-pixels/mm scale: the documented 113 x 230 mm body is
339 x 690 pixels and the 57 mm paper is 171 pixels wide.  The supplied
photographs establish the roll-cover curvature, turquoise moulding, D-shaped
black legend, slot, front card connector, cable exits and carrying-strap loops.
The separate keyboard is scaled against the same body and connected to the
printer's PC port.

The alternate face includes all eight status lamps documented in section 3.4,
in the DATAprint 3000's real top-to-bottom order: `GERÄT EIN`, `BATTERIE LEER`,
`ZUM AUTOMAT`, `FEHLER PC/AUTOMAT`, `PAPIER ENDE`, `ZUM PC`,
`SPEICHER FEHLER`, and `LADEN`.  All eight now have backing states: power,
battery, VDAI transfer, interrupted transfer, paper roll, keyboard/PC activity,
SRAM/card failure, and external charging power.

The real 3000 was intentionally designed without a conventional front control
panel.  The simulator therefore exposes the documented controls in their real
places:

- the green side-mounted `Papiervorschub` switch is clickable and can be held;
- the recessed side `RESET` control clears an operation and powers down an idle
  unit;
- the external red keys implement `+ / ja / drucken`, `init.`, and
  `- / nein / löschen`; like the documented hardware, they require both the
  detachable keyboard and AC adapter, and the window title now calls out a
  missing adapter instead of leaving an apparently inert keyboard unexplained;
- print walks through the manual's maximal, medium, short, cash-bag and custom
  format questions before starting, prints retained device records newest-first,
  and `init.` cancels that selection;
- delete and initialise use the two documented confirmations, while a deleted
  data set can be restored until a complete checksum-valid new record is saved;
- the three keys also implement the documented sequential parameter walk,
  including the `+`/`-` editing roles, status/current-settings printout and
  inactivity timeout;
- the front 256 kB SRAM card can be inserted and removed only while the unit is
  off. Its write-protect switch is modelled. Without a card, direct input is
  accepted only when the corresponding operating parameter is enabled;
- VDAI, keyboard, adapter and paper-roll controls below the illustration model
  plugging in or servicing external hardware rather than pretending to be
  switches on the printer.

The menu writes a stable mechanism name to the cabinet configuration:

```ini
[Photo Play]
printer = dataprint3000
```

`printer = dpu414` selects the Seiko.  Invalid values fall back to the DPU-414.
The printer window contains no model selector because neither physical printer
has one; it updates immediately if the application-menu choice changes.

The same menu offers **DATAprint 3000 English translation** when the 3000 is
selected. It changes the complete presentation as one language pack: all eight
top-case legends, the three-button keyboard legend, the SRAM-card orientation
label and simulator-generated print, delete, recovery, settings and
initialisation messages. It is a simulator translation, not a claim that the
exact strings came from an English firmware dump. German remains the default.
The persistent setting is:

```ini
[Photo Play]
dataprint_english = 1
```

The top-view window follows the 3000's operator-facing paper path.  Its status
and simulator controls remain above the hardware, the window's upper edge stays
fixed, and exposed paper lengthens downward until the desktop work-area limit;
older lines then remain available through the paper scrollbar.  This is kept
separate from the DPU-414 presentation, whose top-exit roll and window continue
to grow upward from a fixed lower edge.

Battery state is kept separately in `nvr/dataprint3000.nvr`.  In accordance with
the manual, a connected game or adapter can power the 3000 even with an empty
battery set, and either external source lights `LADEN`; the standard charge time
is 14 hours.

Incoming VDAI output is also retained across launches in the simulated 256 kB
SRAM card (`nvr/dataprint3000.sram`). The card image owns the card number,
storage/print/evaluation options and running data-set number; device date/time
offset, DATAprint number, PC baud and cardless-operation permission live in
`nvr/dataprint3000.settings`. Version 4's one-level recovery data is
kept beside it in `nvr/dataprint3000.undo`.  The keyboard can reprint the live
records in reverse collection order, clear them after the two documented
confirmations, restore the last deleted data, or initialise and configure the
DATAprint through its printed dialogue. Existing version-1 simulator settings
and card files are imported. New VDAI data remains pending until its `ESC C`
trailer checksum matches; only then is the timestamped, numbered data set
atomically committed, so an aborted or corrupt transfer cannot replace recovery
data or leave a partial record. A full
card lights `SPEICHER FEHLER` steadily and lets incoming data go to paper; an
actual storage failure blinks the lamp.  Paper exhaustion halts on a line
boundary and lights `PAPIER ENDE`; fitting a new 57 mm roll does not restart the
motor until the documented second press of `drucken`.

## Mechanical and signal sound

The DATAprint has a separate procedural sound path; it does not reuse the
DPU-414 thermal carriage.  The Epson M-160 technical manual supplies the
mechanical timing and topology: a single DC motor drives the 18:1 lead-cam
reduction, paper feed and ERC ribbon; four solenoids ride on a shuttle that
crosses 36 dot spaces; one dot-line takes about 150 ms; and seven printed
dot-lines plus three spacing advances produce the specified 0.7 text lines per
second.  The synthesizer consequently uses ten 7 Hz mechanical cycles per text
line.  Each of the first seven cycles offers 144 deterministic solenoid strikes
whose density follows the rendered line, followed by a shuttle return, paper
ratchet and ribbon movement.  A manual feed uses only the three spacing cycles.

The impacts, motor, geared return, ribbon friction, case resonance and paper
movement are generated at run time; there is no recording or wavetable.  The
ordinary 57 mm roll also has its own shorter tear envelope, rather than the
DPU-414's 112 mm thermal-paper event.

Sections 3.4 and 3.5 of the DATAprint manual document the acoustic indications.
They are connected to simulator state as follows:

- general internal faults repeat a double beep at one-second intervals;
- external handling faults, paper-out, interrupted connections and the more
  specifically documented blinking SRAM/card fault produce rapid beeping;
- arriving VDAI data produces the described cricket-like chirp;
- completion produces one beep per second until the VDAI cable is removed or a
  new transfer starts.

The manual specifies the patterns but not the transducer frequency, so the
piezo pitches and impact/case resonances are physically restrained synthesis
choices rather than measurements.  `PEEPEEBOX_PRN_SOUND=0` continues to mute
both printer models for testing.

## Still open

Ribbon consumption and PC DATAcontact transfers remain future work.  The V4.0x
keyboard parameter tree is present, but settings whose effects belong to a
connected DATAcontact PC are necessarily retained rather than executed locally.
For live Photo Play reports, `maximum` and `none` have exact effects; medium,
short, cash-bag and custom currently preserve the complete authentic guest
report because the meanings of its `ESC K` section tags and the original
DATAcontact custom-block definition have not been recovered. A direct
close-miked M-160 capture would improve the inferred
spectral balance and case resonance.  The serial protocol remains the observed
Photo Play DATAPRINT exchange rather than an unverified assumption about a
stock 3000's firmware.
