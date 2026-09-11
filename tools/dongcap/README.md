dongcap — source for the three capture tools
=============================================

This folder builds the programs that ship in `capture-igo2/`, and generates the work list
it carries. You only need it to rebuild or re-target; to actually capture, use that folder.

There is one capture folder because there is one dongle. Kits for 2001 and I.G.O. 3 were
built once and were **broken**: their calibration pairs fit no key under the cipher at all
(2001's list read its pictures from byte 0 rather than 128; I.G.O. 3's used stale harvested
pairs), so no part on any port could have passed calibration. They were withdrawn and
`mklist.py` now refuses both. Nothing needs them -- both keys are fitted in `softpart.py` --
and what I.G.O. 3 still wants from a real part is `SESSION.COM`, not a capture.

`dongcap.c` — the capture program (Windows)
-------------------------------------------

Freestanding Win32: **no C runtime** (the modern one needs Vista and this must run on XP),
imports only `KERNEL32`, resolves `ntdll` at runtime, own entry point. Built for
subsystem 5.01. Port access comes from `NtSetInformationProcess(ProcessUserModeIOPL)`,
which an Administrator gets on 32-bit NT/XP — no kernel driver to install.

It speaks the wire read out of I.G.O. 2's `FINDIT.EXE` (`docs/research/29`):

```
command byte b : write (b & 0xFE)|0x80, b|0x81, (b & 0xFE)|0x80   -- DATA bit 0 clocks
query q        : payload = ((q<<1)&0x0E) | ((q<<2)&0x60) | 0x80
                 write payload, payload|0x10, payload             -- DATA bit 4 clocks
                 answer = STATUS bit 5
round preamble : command(seed), command(0x4E), write 0x84
```

and drives the keyed round itself — 39 shift steps, 40 consultations, polynomial
`0x80500062`, the byte offered to the part chosen by the previous answer and the bit about
to be shifted out.

**Why it needs no plaintext.** A 4 KB buffer needs two dwords from the part:

```
(L1,R1) = A_rounds(ciphertext block 0)    keyless -- L1 comes from the archive alone
f1      = keyed_round(L1)                 <- the dongle
(L3,R3) = B_rounds(f1 ^ R1, L1)           keyless -- L3 follows from f1
f2      = keyed_round(L3)                 <- the dongle
```

so the list only has to carry `(L1, R1)` and one pass gets both.

`dongcap_linux.c` — the same program, for Linux
-----------------------------------------------

A transliteration of `dongcap.c`, not a re-derivation: same wire, same `.LST`, same
`.BIN`, so either host produces a capture the offline tools read without knowing which
made it. If the two ever disagree, that is a bug in the Linux file.

It uses `ioperm`/`iopl` and raw `in`/`out` rather than `ppdev`. ppdev would be tidier, but
it owns the control lines and inverts some of them, and this protocol clocks on DATA bits
and reads STATUS bit 5 — going raw is what keeps the sequence byte-identical to the DOS
original. `ioperm` reaches the first 0x400 ports; a PCIe card above that gets `iopl(3)`.

```
cc -O2 -o dongcap dongcap_linux.c
sudo ./dongcap [base-in-hex]
```

**Calibration.** The seed byte opening a round never resolved from the binaries, so it is
not guessed: the list carries inputs whose answers are already known and the program tries
all 256 seeds until one reproduces every pair. That finds the seed *and* proves the part is
answering before it records for minutes. If none matches it writes `DONGCAP.DIAG` — every
seed against the first inputs — rather than nothing, because that failure is itself the
finding.

**EncodeData entries.** The list may carry blocks to run through the *encode* direction as
well; I.G.O. 3's boot check is one. There `L3` is the plaintext's second dword outright, and
`L1` falls out of the ascending `B` rounds once `L3`'s answer is in hand — so the two keyed
inputs are found in the opposite order from the decode direction. Both programs store them
in the same slots as a decode entry (`f1` is `L1`'s answer, `f2` is `L3`'s), so a consumer
reads both kinds the same way.

`b_rounds_fwd_second(L3, R3) == L1` is checked against `b_rounds_first` on 2000 random
pairs; it is the identity the encode path rests on.

*This path was declared but never implemented:* the output header and buffer allowed for
`nenc`, and the capture loop ran only `count`, so a list with encode entries would have
written a zeroed tail. Fixed in both programs.

`mklist.py` — build a work list
-------------------------------

```
python mklist.py 2001|igo2|igo3 <HardDisk.img> [out.lst]
```

Walks every enciphered archive, emits `(L1, R1)` for each 4 KB buffer, appends the
calibration pairs and any EncodeData blocks. It skips an archive whose entries do not share
a first ciphertext block, since that means it is not on this cipher.

Buffers are **de-duplicated** — the keyed round is deterministic and the tool restarts it
per round, so a repeated input would cost hardware time for an answer already in hand. For
I.G.O. 2 that is 25280 buffers down to 23018.

Several images may be given at once and the union is emitted. In practice one is enough:
all eight I.G.O. 2 territories in the collection, plus the PT cabinet's own disk, produce
**byte-identical** lists.

### The calibration pairs live outside this folder

`mklist.py` reads them from `scratchpad/fpairs_igo2.txt` and `fpairs_igo3.txt`, one
`input output` pair per line in hex. Those files were never committed and had to be
rebuilt from `docs/research/26`:

```
igo2   504EF2AE 32FC6611      012C6137 DF57708B
igo3   41E6AE33 8E90F818      012C6137 2295D802
```

They are not taken on trust. `A_rounds` over the first ciphertext block of
`FINDIT/PICS` -- `97 36 21 d0 1d 6a 2d c7`, shared by all 1397 entries -- gives
`L1 = 504EF2AE`, which is the first calibration input exactly. If that check ever stops
holding, the pairs are wrong and a capture made with them would be worthless.

### File format

```
u32 magic 'DCAP'      u32 ncal      u32 count      u32 nenc
ncal x (u32 input, u32 expected)          calibration
count x (u32 L1, u32 R1)                  one per 4 KB buffer
nenc x (u32 P0, u32 P1)                   EncodeData blocks
```

Output `DONGCAP.BIN`:

```
u32 magic 'DOUT'      u32 count      u32 seed      u32 nenc
(count + nenc) x (u32 f1, u32 f2)
```

`build.cmd`
-----------

Drives MSVC x86. It prints a harmless `vswhere.exe` warning on this machine; the build
still completes. Verify the result is XP-compatible: machine `014C`, magic `010B`,
subsystem 3, subsystem version 5.01, imports `KERNEL32.dll` only.
