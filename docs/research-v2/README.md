# Dongle emulation — the settled version

`docs/research/` is a **trail**: thirty-odd numbered notes written as the work went,
which contradict each other on purpose and record every wrong turn. It is the
provenance and it stays exactly as it is.

**This folder is the distillation.** It contains only what has been established and
holds today: the transports, the grammars, the constants, the record layouts and the
keys. Superseded framings, abandoned hypotheses, sweeps that taught nothing and
values later measured to be wrong are not repeated here — they are in the trail if
anyone needs to know why a thing was tried.

If you want to emulate these dongles, everything you need is in these files. If you
want to know how it was found, or why an earlier attempt failed, go to
`docs/research/`.

## The files

| | |
|---|---|
| `01-generations.md` | Which token each release has, and what state each one is in |
| `02-1999-parallel-dongle.md` | The funworld 8051 dongle: silicon, wire, grammar, all three commands |
| `03-ds1982-ibutton.md` | The second token — a DS1982 on 1-Wire over a UART |
| `04-2000-cdongle.md` | The 2000 generation's parallel token: transport, licence query, picture keys |
| `05-2001-2007-parallel-hasp4.md` | HASP4 on the parallel port: three protocol layers on one pair of wires |
| `06-2008-serial-reader.md` | The 2008 token: an ISO 7816 card reader on COM2 |
| `07-records-and-content-keys.md` | What the record must contain, and the content keys the games read out of it |
| `08-not-a-dongle.md` | Four things that look like dongle problems and are not |
| `09-open-questions.md` | What is genuinely still unknown, stated as unknown |

## What "established" means here

Every claim in this folder is in one of three states, and each file says which:

- **measured** — read off hardware, off a wire capture, or out of a binary by eye;
- **verified** — reproduces a known result exactly, with the count given
  (e.g. "1397 of 1397 pictures decrypt");
- **recorded** — a value copied from an observation that has not been derived, and
  which nothing here can compute for an input that has not been seen.

Nothing else is stated. Where a thing is not known, `09` says so.

## Where the artefacts are

| | |
|---|---|
| the emulation | `src/device/dongle_photoplay.c`, `src/device/dongle_igo8.c` |
| release identification from the disk image | `src/photoplay_ident.c` |
| nine dumped dongles | `PhotoPlay2000_h5dmp/*.7z` (h5dmp, 719 bytes each) |
| a real dongle's whole boot, on the wire | `docs/research/evidence/igo2-dongle-wire-2026-09-06.log.gz` |
| the software HASP4 part, and its verifier | `tools/dongcap/softpart.py` |
| capture and replay tooling | `tools/dongcap/` |
| archive/FAT16 readers, cracked FIND IT keys | `docs/research/evidence/` |

Disk images are not in the repository. They live in `F:\HDDImages\<release>\`, and an
emulator writes to an image — always run against a copy.
