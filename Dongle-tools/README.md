Dongle-tools
============

Everything for the 2001–I.G.O. 3 dongle problem, in one place.

Those releases encipher their photo archives with a cipher the dongle computes. The
cipher itself is now fully understood — mode, block function, key schedule and wire are
all read out of the binaries (`docs/research/24`–`29`) — and the archives decrypt where
we already hold the plaintext. What is *not* solved is computing the dongle's replies,
because the part carries state between rounds and no key fits any model of it.

So there are two tools: one that reads the part's replies off real hardware, and one that
sidesteps the cipher entirely where a plaintext copy of the data exists.

---

`ppictfix.py` — restore FIND IT's photographs, no dongle needed
---------------------------------------------------------------

I.G.O. 2, 3 and 4 ship `FINDIT/PICS/FOTOPLAY.WAD` at the **same 72,373,757 bytes with
byte-identical directories** — 1397 entries, same names, offsets and sizes. I.G.O. 4 is a
CDONGLE release and ships them **in the clear**. So the plaintext already exists; this
copies it in and stops the game deciphering what is already plain.

```
python ppictfix.py --check  <target.img>
python ppictfix.py --source <IGO4 HardDisk.img> <target.img>
```

**Work on a copy.** It writes in place.

It changes two things: the archive is overwritten with I.G.O. 4's copy (identical length,
so the cluster chain and directory are untouched), and the five DecodeData guards in
`EXE/FINDIT.EXE` — `cmp word [bp-XX],8 ; jbe skip` — become unconditional jumps, one byte
each. That cannot make anything worse: those calls already fail today.

Verified end to end on a copy of I.G.O. 2 BE: all 400 entries checked start with `GIF8`
**and** end with the `0x3B` GIF trailer, and `MAIN.SET`, `MENU.EXE` and the other archives
read back unchanged.

### What it does not fix, and why

| | |
|---|---|
| **AMORE's comics** | `AMORE/COMIX/FOTOPLAY.WAD` is enciphered too, but no release from I.G.O. 4 on ships it — they all moved to AMORE2, a different 666 KB archive. There is no plaintext to copy. |
| **I.G.O. 3** | The data swap would work, but its `FINDIT.EXE` is **packed** (entropy 7.99 against 6.9, a 32-byte MZ header, a third of the strings), so the guards are not in the file. The tool detects this and refuses rather than half-applying. Unpack it and the same fix works. |
| **Photo Play 2001** | Its archives are PCX and no later release carries them. The 2000 image has the same 1397 pictures but a different directory, so it is not a drop-in. |
| **I.G.O. 3's boot failure** | Separate problem — it calls service 0x3C before the menu and stops on `dongle error`. Not addressed here. |

---

`dongtest/` — read the dongle's replies off real hardware
----------------------------------------------------------

The one thing still missing. Run it on the XP laptop with the dongle attached and send
`DONGTEST.BIN` back. `dongtest/README.txt` has the full instructions; briefly:

* `DONGTEST.EXE` — 32-bit, subsystem 5.01, freestanding (no C runtime, since the modern
  one needs Vista; imports only KERNEL32). **Run as Administrator.** Gets port access via
  `NtSetInformationProcess(ProcessUserModeIOPL)`, which works on 32-bit XP without a driver.
* `DONGTEST.COM` — 302-byte DOS fallback if the EXE is refused or Windows is 64-bit.

It dumps four phases: a raw DATA→STATUS scan, a sweep of **all 256 preamble seeds** (that
constant never resolved from the binary, so it is swept rather than guessed), both
candidate seeds against every query, and one run repeated to show whether the preamble
resets the part — which is the open question from `docs/research/28`.

`dongtest.c` and `mkdongletest.py` build the two; `build.cmd` drives MSVC.

---

`fatimg.py`
-----------

FAT16 read/write for the disk images. The write side supports exactly one operation —
overwrite a file with content of the same length — which needs no FAT or directory
changes. Refusing anything else keeps a bug from wrecking a 1.6 GB image.
