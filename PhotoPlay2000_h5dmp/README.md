# Dumped dongles

h5dmp dumps of **nine** physical funworld dongles, supplied by Marcos. Each archive
holds one 719-byte `hasp.dmp`.

| file | release | territory | passwords |
|---|---|---|---|
| `2001ES.7z` | Photo Play 2001 | ES | `7477 / 7D57` |
| `2001PT.7z` | Photo Play 2001 | PT | `7477 / 7D57` |
| `2002PT.7z` | I.G.O. 2 | PT | `68BB / 1329` |
| `2003ES.7z` | I.G.O. 3 | ES | `6B91 / 24A3` |
| `2003PT.7z` | I.G.O. 3 | PT | `6B91 / 24A3` |
| `2005ES.7z` | I.G.O. 5 | ES | `6B91 / 24A3` |
| `2005PT.7z` | I.G.O. 5 | PT | `6B91 / 24A3` |
| `2006PT.7z` | I.G.O. 6 | PT | `68BB / 1329` |
| `2007ES.7z` | I.G.O. 7 | ES | `68BB / 1329` |

## Layout of a dump

| offset | bytes | what |
|---|---|---|
| `0x000` | 4 | the two passwords, little-endian |
| `0x004` | 5 | `01 01 01 00 00` |
| `0x009` | ~166 | the crypto table — identical for every dongle sharing a password pair |
| `0x0AF` | 8 | per-unit; the only bytes that differ between two dongles of the same generation |
| `0x0C3` | 112 | the record |
| `0x133` | — | `FF` padding |

## Why they are in the repository

They are the hardware behind three things the emulation depends on, and they replaced
inference with measurement in each case:

- **the passwords**, including I.G.O. 6's `68BB / 1329`, which no disk image could give
  — every I.G.O. 6 image carries `0000 / 0000` and is therefore a neutered copy;
- **the byte order**, independently: a 2001 dump reads as its record only after every
  16-bit word is byte-swapped, an I.G.O. dump reads directly — the same high-first /
  low-first split the two families' unpack loops use;
- **all three record shapes**, now copied verbatim rather than guessed.

They did **not** give the picture cipher's key; see `docs/research-v2/09` § 3.

The documentation that uses them is `docs/research-v2/05` and `07`; the narrative of
what they settled is `docs/research/20-dongle-dumps.md`.
