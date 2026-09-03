# Phase 3 — The device, in PeepeeBox

**Scope.** Turn Phase 2's model into an emulated part, and get the cabinet past its own
copy-protection with **zero bytes changed inside the image**.

**Result: it boots, the protection passes, and `FUNNY.DLL` starts.** The window titles
itself *Funny's Interactive Playworld*, the guest reads `ORGACONTROL ` out of the emulated
EEPROM, and the game's splash screen and loading bar come up.

## 3.1 What was added

| File | Change |
|---|---|
| `src/device/dongle_funny.c` | **new** — the part: HASP-4 presence handshake in front of a Microwire 93Cxx EEPROM |
| `src/photoplay_ident.c` | +25 — recognise the Funny's Interactive Playworld layout |
| `src/include/86box/photoplay.h` | +7 — `PHOTOPLAY_FUNNY_{BANNER,DISPLAY,DONGLE}` |
| `src/photoplay.c` | +17 — pick the LPT token from what the image says it is |
| `src/char/char.c`, `src/include/86box/lpt.h`, `src/device/CMakeLists.txt` | +1 each — register the device |

50 added lines across the existing tree plus one self-contained device. Nothing on the
Photo Play paths changed: `dongle_photoplay` is still the default and still the only thing
a Photo Play image can get.

## 3.2 How the image chooses its own token

The two cabinets in this build carry different parts and nothing outside the image tells
them apart, so the image decides. `photoplay_identify_ex()` gains a branch before the
`\FOTO\SETTINGS\MAIN.SET` one: a root `\GAME` directory holding **both** `FSYSTEM.EXE` and
`FUNNY.DLL` is the signature — no Photo Play release has either. It reports the banner
`"Funny Interactive"`, and `pp_apply_ports()` swaps `dongle_photoplay` for `dongle_funny`
when it sees that.

Territory is deliberately left **empty**. The build to hand is German — `KEYB GR+`,
`COUNTRY=49`, `LASTGAME.LOG` says `GER` — but none of that is recorded anywhere the image
can be asked for it at runtime, and this cabinet's token does not carry a territory either.
Inventing one would be the same mistake `dongle_photoplay.c` already warns about for
Photo Play 2.0.

## 3.3 The part

`dongle_funny.c` is two layers on one port, exactly as Phase 2 measured:

* **Presence handshake** — a port of upstream `hasp.c`'s state machine: `C6 C7 C6 80`
  wake, fourteen clocked bytes, then a table of even values answered on STATUS bit 5. The
  fourteen is the phase, and the phase is what the client accepts; the byte values are not
  compared here because this client's own preamble is fifteen bytes long and nothing in it
  is checked.
* **Microwire EEPROM** — CS on DATA bit 1, SK on DATA bit 5 (rising edge), DI on DATA bit
  6, DO on STATUS bit 5 and, inverted, on bit 7 because the library probes the line both
  ways. Nine address bits. READ/WRITE/ERASE/EWEN all implemented.

The record is stored pre-scrambled, so the guest's own unscramble hands back the text:

    raw[n + 8] = plaintext(n) XOR 0x43B5 XOR n

DO stays driven from the first read bit **until CS drops**, not until the sixteenth clock.
That is the one subtle thing in the file: release it an edge early and the last bit comes
from the handshake layer instead, and `ORGACONTROL ` reads back as `OSGACONTROL `.

Word 9 — surface D, the value the game fetches through INT 50h `AX=1235` — is a device
config option (`word9`, hex string) rather than a constant, because what it is *for* is
still unknown. It is a free-form hex field rather than a spinner: `device_config_spinner_t`
bounds are `int16_t` and cannot express the top half of a 16-bit range.

## 3.4 Measured

```
PP: LPT1 token: dongle_funny
PP: disk .../HardDisk.img, 3949/16/63
PP: image identified as Funny's Interactive Playworld
FN: record loaded, "ORGACONTROL "; words 0..5 read back 4F52 4741 434F 4E54 524F 4C20, word 9 = 0000
FN: Funny's Interactive Playworld dongle attached; passwords 43B5/594A, 9-bit addressing, word bias 8
FN: read word 001 -> 0000 (guest sees BC4C)
FN: read word 000 -> 0000 (guest sees BC4D)
FN: read word 008 -> 0CE7 (guest sees 4F52)
FN: read word 009 -> 04F5 (guest sees 4741)
FN: read word 00A -> 00F8 (guest sees 434F)
FN: read word 00B -> 0DE2 (guest sees 4E54)
FN: read word 00C -> 11FE (guest sees 524F)
FN: read word 00D -> 0F90 (guest sees 4C20)
FN: read word 014 -> 43B9 (guest sees 0000)
FN: read word 015 -> 43B8 (guest sees 0000)
FN: read word 011 -> 43BC (guest sees 0000)
```

Eleven reads, no errors, and then `FSYSTEM.EXE` execs `FUNNY.DLL`: splash screen, mascot,
loading bar, and after about seven minutes of 486 the **main menu**, in German, with every
game button drawn correctly — `FunBallon`, `Malen`, `Memory`, `Funny suchen`, `FunVoice`,
`Mosaik`, `FunBlock`, `Detektiv`, `Puzzle`, `Zeichnen`, `FunMusik`, and the mascot asking
*"Wohin möchtest du jetzt gehen?"* (`docs/research/evidence/fi/screen-menu.png`).

That the menu text is **legible** is itself a result. `README.md` records I.G.O. 5 running
with "menu buttons garbled" — the signature of a record decoded under the wrong key. Ours
are clean, which is independent confirmation that `pass1 = 0x43B5` and the `^ pass1 ^ n`
descramble are right.

The two reads of words 0 and 1 *before* the record are the library probing with the address
bias not yet applied; they are answered and ignored.

The three reads after the record are the runtime phase — API words 12, 13 and 9. Word 13
is where `FSYSTEM.EXE`'s INT 50h `AX=0011` counter lives; word 9 is surface D. All three
were served 0 and the game carried on.

**Nothing inside the filesystem was touched.** `Fixed.img` is byte-identical to the image
the user supplied; `HardDisk.img` is a straight copy of it under the only name
`pp_apply_disk()` opens.

## 3.5 Adversarial check

* **No service-4 write has been observed yet, in the emulator or offline.** Phase 2 could
  not get one to reach the part, and this boot did not produce one either — the guest read
  word 13 but did not write it back. The write path is implemented and untested; the first
  game played is what will exercise it. Treat "writes work" as unproven.
* **Word 9 = 0 was accepted, but only for as far as this got.** It is read once during
  start-up. Whether the value matters shows up later — in whatever `FUNNY.DLL` decrypts
  with it, if anything.
* **The two-layer model is still a model.** On real hardware the handshake and the EEPROM
  are one device. It reproduces every answer measured so far, which is not the same as
  being what the silicon does.
* **Only the boot path has run.** Touchscreen (surface H), coin board (surface I) and
  everything `FUNNY.DLL` does on its own (surface G) are untouched by this phase.
* The presence handshake's answer table is inherited from `hasp.c`, whose author describes
  the values as guessed from disassembly rather than dumped from hardware. It satisfies
  this client; that is all that is known about it.

## 3.6 Ledger

| Surface | Before | After |
|---|---|---|
| A — `IsHasp` | solved offline | **shipped and passing in the emulator** |
| B — service 6 status | solved offline | **shipped and passing** |
| C — `ORGACONTROL ` record | solved offline | **shipped and passing** |
| D — word 9 | servable | **exposed as a config option**, default 0000, accepted so far |
| E — counters (service 4) | narrowed | implemented, **still unexercised** |
| F, G, H, I | open | unchanged |
| M — two-layer modelling risk | open | unchanged; it held for a whole boot |
