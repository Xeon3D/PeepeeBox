dongcap — source for the three capture tools
=============================================

This folder builds the program that ships in `capture-2001/`, `capture-igo2/` and
`capture-igo3/`, and generates the work list each of them carries. You only need it to
rebuild or re-target; to actually capture, use those folders.

`dongcap.c` — the capture program
---------------------------------

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

**Calibration.** The seed byte opening a round never resolved from the binaries, so it is
not guessed: the list carries inputs whose answers are already known and the program tries
all 256 seeds until one reproduces every pair. That finds the seed *and* proves the part is
answering before it records for minutes. If none matches it writes `DONGCAP.DIAG` — every
seed against the first inputs — rather than nothing, because that failure is itself the
finding.

**EncodeData entries.** The list may carry blocks to run through the *encode* direction as
well; I.G.O. 3's boot check is one. There the first keyed input is the plaintext's second
dword and the second comes from the ascending `B` rounds, so the derivation differs and is
handled separately.

`mklist.py` — build a work list
-------------------------------

```
python mklist.py 2001|igo2|igo3 <HardDisk.img> [out.lst]
```

Walks every enciphered archive, emits `(L1, R1)` for each 4 KB buffer, appends the
calibration pairs and any EncodeData blocks. It skips an archive whose entries do not share
a first ciphertext block, since that means it is not on this cipher.

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
