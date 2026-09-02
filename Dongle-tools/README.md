Dongle-tools
============

Everything for the 2001–I.G.O. 3 dongle problem. Nothing here alters a disk image.

Those releases encipher their photo archives with a cipher the dongle computes, and
I.G.O. 3 will not even reach its menu without one. The cipher itself is now fully
understood — mode, block function, key schedule and wire are all read out of the binaries
(`docs/research/24`–`29`) — and archives decrypt wherever we already hold the plaintext.
The one thing missing is the dongle's own replies, because the part carries state between
rounds and no key fits any model of it.

So the tools all do the same thing: get those replies off the real hardware, so PeepeeBox
can decrypt on its own with the images left completely alone.

| folder | what it is |
|---|---|
| `capture-2001/` | run with a **Photo Play 2001** dongle attached |
| `capture-igo2/` | run with an **I.G.O. 2** dongle attached |
| `capture-igo3/` | run with an **I.G.O. 3** dongle attached — pictures *and* the boot check |
| `dongtest/` | a smaller probe that characterises the part; run this first if a capture will not calibrate |
| `dongcap/` | source for the three capture tools, and the list generator |

Each folder has its own README.

How to run any of them
----------------------

1. Attach that generation's dongle to the parallel port, nothing else on it.
2. Right-click Command Prompt → **Run as administrator**.
3. `cd` to the folder and run the `.EXE`. Add the LPT base in hex if it is not `0x378`.
4. Send the `.BIN` back.

They need **32-bit Windows XP** and Administrator. Port access comes from
`NtSetInformationProcess(ProcessUserModeIOPL)`, so there is no driver to install. If that
is refused, `dongtest/` also ships a DOS `.COM` fallback.

What gets captured, and why it is enough
----------------------------------------

Every 4 KB of an enciphered archive needs two dwords from the dongle:

```
(L1,R1) = A_rounds(ciphertext block 0)    keyless -- L1 comes from the archive alone
f1      = keyed_round(L1)                 <- the dongle
(L3,R3) = B_rounds(f1 ^ R1, L1)           keyless -- L3 follows from f1
f2      = keyed_round(L3)                 <- the dongle
```

Both keyless stages are rotations and XORs, so the work list can be built from the
**encrypted archive alone** — no plaintext anywhere — and one pass over it captures both
dwords. Everything else in the cipher PeepeeBox already computes.

| | dongle | passwords | buffers | keyed rounds |
|---|---|---|---|---|
| `capture-2001` | Photo Play 2001 | `7477/7D57` | 34,483 | 68,966 |
| `capture-igo2` | I.G.O. 2 | `68BB/1329` | 25,280 | 50,560 |
| `capture-igo3` | I.G.O. 3 | `6B91/24A3` | 25,280 | 50,562 |

Each covers **both** enciphered archives, `FINDIT/PICS` and `AMORE/COMIX`, so AMORE's photo
comics are included too.

Calibration comes first
-----------------------

The seed byte that opens a round never resolved from the binaries. Rather than guess, each
list carries a few inputs whose correct answers are already known, and the tool tries all
256 seeds until one reproduces them. That finds the seed *and* proves the part is really
answering before it spends minutes recording.

If none matches it writes `DONGCAP.DIAG` — every seed against the first inputs — instead of
nothing. That is not a hardware fault and not something to retry: it means the part answers
differently from what was predicted, and the DIAG file is what is needed to work out why.

Honest state of each
--------------------

* **I.G.O. 2** and **I.G.O. 3** — calibration pairs come from buffers that were decrypted
  and verified end to end, 507 of 507 (`docs/research/26`). Solid.
* **I.G.O. 3's boot check** — `MENU.EXE` at `0x41A32` calls EncodeData over 20 bytes at
  `DS:0x50F6` before the menu and stops on `dongle error` when it fails. DGROUP resolved to
  `0x42530` on 355 of 359 string-start votes, so those bytes are read, not guessed, and the
  block is in `capture-igo3`'s list.
* **Photo Play 2001** — its calibration pairs rest on an *assumed* plaintext (the PCX
  header). Checking that against the 2000 image **failed**: blocks 1 and 2 decrypt to the
  wrong bytes, so either 2001's plaintext differs beyond block 0 or its pipeline does. The
  work list is unaffected, since it needs no plaintext, but calibration may not take — in
  which case `DONGCAP.DIAG` is the useful output and 2001 needs another pass.

How long
--------

Several minutes each: about 50,000–69,000 keyed rounds, 40 wire transactions apiece. A dot
is printed every 512 buffers.
