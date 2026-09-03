# Phase 1 — Bring-up: does it reach the dongle?

**Scope.** One question: with the *container* fixed and nothing inside the filesystem
touched, how far does this image get on stock PeepeeBox? Phase 0 predicted it would boot
and die at the HASP gate. Confirm or falsify by observation.

**Answer: it boots cleanly, all the way to `FSYSTEM.EXE`, and stops at exactly the
predicted gate.** Two of Phase 0's three open assertions are now settled by measurement,
and the third — the HASP wire protocol — has been captured in full.

---

## 1.1 What was changed, and what was not

Only the container. `docs/research/evidence/fi-stage-image.py`:

```
in   Fixed.img
     2038063104 bytes (3980592 sectors)
     sha256 776801cd0a257527b7130622b7b77714bd1f8f56feed389505b54691ba01ef06
out  HardDisk.img
     2038063104 bytes (3980592 sectors)
     sha256 776801cd0a257527b7130622b7b77714bd1f8f56feed389505b54691ba01ef06
     PeepeeBox geometry: 3949/16/63
```

Byte-identical copy under the one name `pp_apply_disk()` will open. **`--pad` was not
needed and was not used** — see 1.3. `Fixed.img` is untouched.

Emulator-side state only: `nvr/4dps.bin` and `nvr/4dps.nvr` were removed before each run so
the Award BIOS re-detects the drive from scratch rather than trusting a CMOS written when
no disk was attached. Backup at `the workspace's nvr backup`.

## 1.2 The run

```
PeepeeBox.exe -P . -L 86box.log
```

Log: `docs/research/evidence/fi/boot-run1.log`. Screen: `docs/research/evidence/fi/screen-copyprotection.png`.

```
PP: disk F:/funny_interactive_de.img/HardDisk.img, 3949/16/63
PP: Photo Play profile applied (4dps, idx4 @ 100 MHz, 16 MB)
PP: Photo Play dongle attached, banner "Version 99 (AT)" (15 chars), dwords at +10
IB: DS1982 iButton at I/O 268, ROM 09 50 50 42 4F 58 00 FF
SC: the disk image did not identify itself; serving the ES default
SC: record "IGO" / "ES" / "IGO 08", so the guest composes "Version 08 (ES)"
SC: 2008 card reader attached to COM2 (2F8h), 9600 baud
PP: No parallel HASP part on the port (Auto: this release does not use one).
MT NVR CAL: scale_x=0.995851, scale_y=0.994475, off_x=0.002075, off_y=0.006906
```

and on screen:

```
Funny's Interactive Playworld - TouchScreen - Version 1.02
         (C)2001 OrgaControl(R) Systemhaus
 Portions (C)2001 Funny's Planet International GmbH
               All rights reserved

COPYPROTECTION. NO DONGLE WAS FOUND. PLEASE CONTACT YOUR ADMINISTRATOR !
```

That is the string at `CS:0x0212` in `FSYSTEM.EXE`, reached from the service-1 site at
`CS:0x02F3` — surface **A**, exactly as Phase 0 called it. The machine is now in the
`Sound(800)/Delay(500)/NoSound/Delay(300)` loop and stays there.

## 1.3 Settled by measurement

**Surface J — geometry: CLOSED, not a problem.** Phase 0's falsification note was right.
PeepeeBox's fixed 63 spt × 16 heads gives 3949 cylinders; the Award BIOS's LBA-assist
translation halves cylinders and doubles heads until cyl ≤ 1024 (3949 → 1974/32 → 987/64),
landing on the 63 × 64 the DR-DOS BPB was written under. The VBR's own reads all live in
cylinder 0 where the two interpretations coincide anyway. **DR-DOS boots, `CONFIG.SYS` and
`AUTOEXEC.BAT` run, `FSYSTEM.EXE` loads and prints — no disk errors of any kind.**

**Surface K — truncation: CLOSED as a boot blocker, still open as a correctness item.**
The unpadded image boots and runs. `--pad` in `stage-image.py` remains available and remains
the *correct* thing to do before anything writes to the disk in anger (the guest writes
`LASTGAME.LOG`, `CHECKMB.BAT`, `FMEMO\STATS.DAT` and score files every session), but it is
not needed to get a picture. Leaving it off keeps the deliverable to "rename the file".

**Surfaces H and I — touchscreen and coin board: not yet reached.** `FSYSTEM.EXE` runs the
dongle gate *before* `Touch Controller Search...`, so neither probe has executed. They stay
open, and their difficulty is unknown until the dongle answers.

**New observation, no phase yet:** the profile also puts things on the bus that this image
has no idea about — the DS1982 iButton at I/O 268h, and the I.G.O. 8 card reader on COM2 at
2F8h serving an "ES default" record. Neither is harmful so far (the guest never addresses
either), but COM2 being occupied is worth remembering if the cabinet turns out to want it.

## 1.4 The HASP wire protocol, captured

The shipped binary honours `PEEPEEBOX_LPT_TRACE=1`, which logs every port access with the
guest's caller chain. That needed no rebuild. 53 928 events captured in
`docs/research/evidence/fi/lpt-cycle.txt`; one complete transaction extracted to
`docs/research/evidence/fi/lpt-cycle.txt`. The guest repeats it forever — 276 identical cycles in
55 seconds — because nothing answers.

**A transaction is 146 `write_data` writes in three phases:**

```
 phase 1  wake      C6 C7 C6 80
 phase 2  password  15 clocked bytes, each sent as a  X, X|1, X  triplet with
                    BE / 80 framing bytes between groups:
                        8B F9 DB BB 95 C9 A9 81 93 D1 B1 8D 9F DD BD
 phase 3  read      a descending even ramp, 64 values, FE FC FA ... 84 82 80,
                    with one read_status after each
```

Every `read_status` returns `00` because PeepeeBox has no responder on the port; the reply
the library wants is **STATUS bit 5 (`0x20`) per ramp step** — 64 bits of answer.

This is the same protocol family as upstream 86Box's `src/device/hasp.c` (the Savage Quest
HASP4), which is strong corroboration: that file's state machine keys on exactly
`0xC6, 0xC7, 0xC6, 0x80`, collects odd-valued bytes as the password, then answers a table of
even values with status `0x20`. Its password is 14 bytes of the same shape
(`c3 d9 d3 fb 9d 89 b9 a1 b3 c1 f1 cd df 9d`); ours is 15. **`hasp.c` is the shape of the
answer, but it is not attached by the Photo Play profile and its password and contents are a
different dongle's.**

Partial decode of the password bytes (evidence, not yet a conclusion): bit 7 is always set,
bit 0 is the clock, and bits 6/5 form a 2-bit framing symbol that runs
`00 11 (10 01) 00 (10 01) 00 00 (10 01) 00 00 (10 01)`, with bits 4..1 carrying
`5 C D D A 4 4 0 9 8 8 6 F E E` — the repeated nibbles lining up with the repeated `10 01`
symbols, i.e. each value is clocked twice under two different framing bits. Phase 0 recovered
the plaintext this must encode: `pass1 = 0x43B5`, `pass2 = 0x594A`, service 1, seed 0x64,
LPT 1. **Fitting that plaintext to these 15 bytes is Phase 2's first job**, and getting it
right is what makes services 3, 4 and 6 (which we have never seen on the wire) implementable
rather than guessable.

## 1.5 Adversarial check

What this phase could not see, and does not claim:

* Only **one** service has ever been on the wire — service 1. Services 3, 4 and 6 are still
  pure static inference from `FSYSTEM.EXE`. A responder built only from this capture would
  satisfy `IsHasp` and nothing else.
* `read_status` returning a constant `00` means the guest may have **abandoned** the
  transaction early. The 64-step ramp completing suggests otherwise — it looks like a full
  cycle — but a real dongle may drive extra handshaking we will only see once something
  answers. Expect the capture to grow in Phase 2.
* The caller chain in the trace (`981C:E409 / 76FF:463A / 16F6:7E8D`) names the Borland
  `inportb`/`outportb` stubs and their callers inside the obfuscated Aladdin library. Not
  yet mapped to the wrappers at `CS:0x0188` / `CS:0x01D1`; do that in Phase 2 if the
  password fit is ambiguous.
* Nothing here touches surface **G** — whatever `FUNNY.DLL` checks on its own. The image has
  not run a single instruction of it.

## 1.6 Tooling note

The build path is available: MSYS2 at `C:\msys64` with mingw64 `gcc`, `cmake`, `ninja` and
`mingw64/qt5-static`, matching the README's
`cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTATIC_BUILD=ON -DUSE_QT6=OFF`.
Source at `%TEMP%\claude\…\scratchpad\ppb` (github.com/Xeon3D/PeepeeBox).

Window capture needs **Windows PowerShell 5.1** (`powershell.exe`), not PowerShell 7 —
`System.Drawing.Common` is not loadable in the latter. Script kept at
`scratchpad/grab.ps1`.

## 1.7 Ledger

| Surface | Before | After |
|---|---|---|
| A — `IsHasp` | open | **confirmed live and fatal**; wire capture in hand |
| J — geometry | needs proof | **closed** — BIOS LBA-assist lands on 63/64, boots clean |
| K — truncation | open | **closed as a boot blocker**; still the right fix before sustained writes |
| B, C, D, E, F, G | open | unchanged — not reached |
| H, I | open | unchanged — `FSYSTEM.EXE` gates on the dongle first |
