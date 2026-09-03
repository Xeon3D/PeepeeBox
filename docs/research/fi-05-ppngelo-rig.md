# Phase 5 — Merged onto the Elo build, and rigged

**Scope.** Carry the Funny's Interactive Playworld work forward onto the current PeepeeBox
— the one that emulates Elo as well as MicroTouch — and hand back a folder that runs the
image from a standing start.

**Result: `F:\funny_interactive_de.img\ppngelo\` boots a byte-identical copy of `Fixed.img`,
identifies it, serves its token, and comes up on the Elo part the disk actually asks for.**

## 5.1 The merge

The Phase 3 work sat on `502b0d1`. Upstream had exactly one commit on top —
`697b52d Add the Elo touchscreen, and let the cabinet choose which one it has` — so this
was a clean fast-forward with nothing to reconcile.

```
 src/char/char.c               |  1 +   register the device
 src/device/CMakeLists.txt     |  1 +
 src/include/86box/lpt.h       |  1 +
 src/include/86box/photoplay.h |  9 +   PHOTOPLAY_FUNNY_{BANNER,DISPLAY,DONGLE}
 src/photoplay.c               | 40 +   token and touchscreen chosen from the image
 src/photoplay_ident.c         | 29 +   recognise \GAME\{FSYSTEM.EXE,FUNNY.DLL}
 src/device/dongle_funny.c     | new    the part itself
```

Diff and device source kept at `docs/research/evidence/fi/`.

**The `COM3_IRQ` experiment was dropped.** Phase 4 patched it to 3 globally in `serial.h`
to test the IRQ theory; the theory failed, and the trace then showed the guest reading
bytes regardless. Carrying it would have been an unproven global change to a constant
Photo Play shares. Upstream behaviour is untouched.

## 5.2 The image picks its own touchscreen

`697b52d` made the touchscreen a setting — `[Photo Play] touchscreen`, holding a device
internal name — with MicroTouch as the default. This extends that one step: the *image*
supplies the default.

```c
photoplay_image_ident(banner, sizeof(banner), NULL, 0);
if (!strcmp(banner, PHOTOPLAY_FUNNY_BANNER) &&
    tablet_get_from_internal_name((char *) PHOTOPLAY_TABLET_ELO))
    dflt = PHOTOPLAY_TABLET_ELO;
```

The justification is on the disk: `AUTOEXEC.BAT` loads `ELODEV 2310,3,9600,3` — an Elo
E271-2310 on COM3 at 9600 — and `FSYSTEM.EXE` probes for Elo at `CS:0x16FD` before it will
look at anything else. Photo Play keeps MicroTouch, and an explicit choice in the ini still
wins; this only fills in the default when nothing has chosen.

Confirmed on the first boot of the rig, from a config file it wrote itself:

```
[Elo TouchSystems SmartSet (Serial)]
port = 2
tablet_type = elo_touchscreen
```

## 5.3 What is in the folder

`F:\funny_interactive_de.img\ppngelo\`, 2.2 GB:

| | |
|---|---|
| `PeepeeBox.exe` | built from the merge, MSYS2 mingw64, `-DSTATIC_BUILD=ON -DUSE_QT6=OFF` |
| `ppfix.exe` | as the README expects it, beside the emulator |
| `HardDisk.img` | **byte-identical copy of `Fixed.img`**, sha256 `776801cd…1ef06` — ELODEV line intact, nothing patched |
| `roms\` | |
| `nvr\4dps.{bin,nvr}` | **seeded** — see below |
| `discord_game_sdk.dll`, `gsdll64.dll`, `libaaruformat.dll`, `mdsx.dll` | as the reference rig ships them |
| `run.cmd` | `PeepeeBox.exe -P . -L 86box.log` |

**The NVR has to be seeded.** The first attempt shipped without one and the machine stopped
at `CMOS checksum error - Defaults loaded ... Press F1 to continue` — correct behaviour for
a battery-backed clock that has never been written, and fatal for an unattended rig.
Copying a `4dps.bin` / `4dps.nvr` from a machine that has booted once fixes it.

Incidental, and a nice corroboration of Phase 4: the BIOS identifies itself as
`07/08/97-SiS-496-497/A/B-2A4IB21AC-00`, ID `2A4IBZ1A`. The cabinet's own
`SOUND\MOBO.CSV` expects `2A4IBL13` for its 486 profile, so `CHECKMB.EXE` will write
`set MB=UNKN` here — exactly what the last real boot of this disk recorded in 2018.

## 5.4 Verified

```
PP: image identified as Funny's Interactive Playworld
PP: LPT1 token: dongle_funny
FN: record loaded, "ORGACONTROL "; words 0..5 read back 4F52 4741 434F 4E54 524F 4C20, word 9 = 0000
FN: Funny's Interactive Playworld dongle attached; passwords 43B5/594A, 9-bit addressing, word bias 8
```

then POST, DR-DOS, `FSYSTEM.EXE`, and the game's loading bar — **with zero bytes changed
inside the filesystem.**

## 5.5 The INT 50h dispatcher, enumerated — and why MicroTouch could never have worked

Full set of function codes in the handler `FSYSTEM.EXE` installs:
**`0x01, 0x02, 0x10, 0x11, 0x20, 0x1234, 0x1235, 0xFF`**. Frame is `[bp+0x10]` = AX,
`[bp+0x0E]` = BX, `[bp+0x0C]` = CX, `[bp+0x0A]` = DX, `[bp+0x08]` = SI, `[bp+0x06]` = DI.

| AX | What it does |
|---|---|
| `0001` | **the touch data path** — returns the COM ring buffer far pointer from `DS:[0x22A + com*4]`, plus the addresses of its head and tail indices at `0x23C + com*2` and `0x244 + com*2` |
| `0002` | returns the far pointer at `DS:[0x200]` |
| `0010` | returns `DS:0x1B0` |
| `0011` | read dongle word `BX+13`, increment, write back |
| `0020` | print a string |
| `1234` | the six system-code words in `AX BX CX DX SI DI` |
| `1235` | dongle word 9 |
| `00FF` | **"is there a touchscreen?"** |

So `FUNNY.DLL` never asks for touches — it maps `FSYSTEM.EXE`'s serial ring buffer through
`AX=0001` and parses the raw bytes itself.

And `AX=00FF` is the gate:

```
0CFC  cmp byte [0x1fe],0
0D01  jz  0xd0a
0D03  mov word [bp+0x10],1     ; AX = 1
0D0A  xor ax,ax                ; AX = 0
```

`[0x1FE]` is written in exactly two places in the entire binary:

```
16DD  mov byte [0x1fe],0     ; cleared before the search
1773  mov byte [0x1fe],1     ; set ONLY inside the Elo-found branch
```

**The MicroTouch branch never sets it.** `CS:0x1E50` corroborates: on exit, `[0x1FE] == 0`
tears the serial port back down. So Phase 4's puzzle is fully explained — the MicroTouch
packets were well-formed, correctly addressed, delivered and read, and the app ignored them
because as far as it was concerned no touchscreen existed. **This cabinet is Elo-only in
practice**, whatever `FSYSTEM.EXE`'s MicroTouch code suggests.

## 5.6 A config footgun, and the Elo path working

The rig's `86box.cfg` drifted to `[Photo Play] touchscreen = microtouch_touchpen` with
**both** touchscreens on `port = 0` — COM1. That alone produces the observed
`No ELO-Touch found.` followed by four `faild !`: nothing was on COM3 for either part.

It is not a mistake anyone made carelessly. Upstream deliberately stamps COM3 **only when
nothing has chosen a port**, so the Touchscreen dialog can move it — which means once a
`port = 0` reaches the ini it sticks, and the profile will not correct it.

With `port = 2` restored and Elo selected, ELODEV runs a full SmartSet interrogation and
the emulated part answers every command:

```
rx 55 61 00 60   'a' ack query   -> tx 55 41 00 40
rx 55 51 02 ..   'Q' set 02      -> tx 55 41 00 40
rx 55 69 00 68   'i' identity    -> tx 55 49 30 80 01 02 00 2B
rx ..            'o' owner       -> tx 55 4F "EloInc." B6
rx ..            'd' diagnostics
rx ..            'M' set 00 87   mode
```

and the cabinet says so itself:

```
Search elo touch controller...  Controller 2310 found. Version: 1.7
CoinControl (6chanel) found at port 0210
Blaster at A0220 I5 D1
```

**`CS:0x1773` is reached, so `[0x1FE] = 1` and `AX=00FF` now answers yes.** No MicroTouch
fallback, no `faild !`, no `Elo-Touch found, but with no Int.` — the driver stack is
working end to end.

Worth noting the identity mismatch that did *not* matter: the emulated part reports
`IFlag = 0` (E271-2200-class) while ELODEV is invoked as `2310`. ELODEV accepts it and
reports `Controller 2310 found`.

## 5.7 Adversarial check

* **Whether touch now works is not yet shown.** The Elo device is selected and configured on
  COM3; whether `ELODEV` installs against it, and whether the app then takes the input, is
  the open question this rig exists to answer.
* If it does work, the Elo path changes behaviour elsewhere: `CS:0x177E` sets the port
  variable to COM1 on the Elo branch, and the `C3 I3` switches are never parsed. That is the
  guest's own logic, not a problem, but it means the COM3/IRQ3 findings from Phase 4 apply
  only to the MicroTouch path.
* The seeded NVR came from a machine booted by the *previous* build. Same machine profile,
  so it should be portable, but it has not been checked against a from-scratch CMOS written
  by this build.
* `word9` is still `0000` and still unexamined (surface D), and no service-4 write has ever
  been observed (surface E).
