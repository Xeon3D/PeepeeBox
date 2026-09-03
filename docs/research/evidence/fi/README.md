# Funny's Interactive Playworld — evidence

Raw material behind `docs/research/fi-*.md`. Grep into these; they are not meant to be
read end to end.

| | |
|---|---|
| `FSYSTEM.EXE` | The cabinet's launcher, extracted from the disk image (60 480 B, sha256 `e97ea4ef…1d1d`). Every `CS:` offset the `fi-*` docs cite is into this file. It is the whole subject of the investigation, so it is kept here rather than requiring the 2 GB image |
| `fsystem-seg0.asm` | `ndisasm` of its first code segment. Segment-0 offset = file offset − `0x5C0` |
| `fsystem-strings.txt` | Strings with file offsets — the Borland length-prefixed ones are how the messages were located |
| `image-tree.txt` | The FAT16 tree: every live file, size, mtime, fragmentation, first LBA |
| `image-layout.txt` | The same by physical cluster order, live *and* deleted — this is what showed the disk's history |
| `hashes.txt` | Ground truth: sha256 of the original image and the emulator binaries |
| `lpt-cycle.txt` | One complete dongle transaction off the wire, captured with `PEEPEEBOX_LPT_TRACE=1` |
| `verify.txt` | Output of `fi-p2-verify.py` at the point every boot gate passed |

## Reproducing the protection result

No emulator, no disk image, a couple of seconds:

```
cd docs/research/evidence
python3 fi-p2-verify.py fi/FSYSTEM.EXE
```

This loads the real client library out of `FSYSTEM.EXE`, runs it under Unicorn, and drives
it against the simulated dongle. It should print `SYSTEM CODE: PASS` with words 0–5
spelling `ORGACONTROL `.

Needs `unicorn` (`pip install unicorn`); developed against 2.1.4.

## Known-incomplete

`svc4 write` reports `p3=FFFF` and no Microwire frame reaches the part — the client frames
writes in some way the responder does not yet model. It is a runtime path (the play
counters at word 13+), not a boot gate, so everything above still passes. See
`fi-03-dongle-device.md` §3.5.

## Known-good rig configs

`rig-elo-86box.cfg` and `rig-mb540n-86box.cfg` are the `86box.cfg` files from two working
rigs: the original 4DPS/iDX4 profile with the Elo on COM3, and the later MB540N /
Pentium MMX 200 / 64 MB one. Both are kept for one line each —

    [Elo TouchSystems SmartSet (Serial)]
    port = 2

— because getting that wrong is silent. The profile stamps COM3 only when nothing has
chosen a port yet, so the Touchscreen dialog can move it; once a `port = 0` reaches the
ini it sticks, and the cabinet then prints `No ELO-Touch found.` followed by four
`faild !` with no clue that the port is the reason. See `fi-05-ppngelo-rig.md` §5.6.
