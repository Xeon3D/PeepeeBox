dongdump — read a parallel funworld dongle for emulation
========================================================

A Windows GUI program that reads everything PeepeeBox needs out of a physical
2001 / I.G.O. 2 / 3 / 5 / 6 / 7 / Italy parallel HASP4, and writes it into a folder named
`<version>-<language>-<date and time>`.

It **never writes to the dongle**. Only Microwire READ is implemented; WRITE, ERASE,
EWEN and ERAL are deliberately absent.

**No physical dongle has answered this program yet.** Every sequence in it comes from
`docs/research-v2/05` and from the passthrough capture in `docs/research/evidence`, and
everything that can be checked without hardware has been (see *Self-test*), but the wire
itself is unproven. Treat the first run as an experiment.

Why it exists, next to dongcap
------------------------------

`tools/dongcap` captures the keyed round's **answers** for one game's inputs — a lookup
table, tens of thousands of entries, valid for that image only.

This captures the **part**: the 32-bit key itself, the memory, the passwords and the
three session gates. One run of a generation nobody here owns is enough to add it to
`src/device/dongle_photoplay.c`, with no disk image and no known plaintext.

What it reads
-------------

| layer | what comes out |
|---|---|
| session | the 64-bit identity signature and its `acc`, the 64-step sweep's reply, the liveness probe |
| memory | all 64 Microwire words, `pass1`, the 112-byte record, the byte order, the release and territory, the eight content keys |
| transform | the picture cipher's key and initial register, which framing the part uses, and every (query, answer) pair observed |

### The passwords are measured, not guessed

The record is stored as

```
word[i] = plain[i - 8] ^ (uint16)(i - 8) ^ pass1 ^ (i < 8 ? 0xFF00 : 0)
```

and words 0..7 carry no record, so each of them is `pass1` XORed with a constant the
program can compute — eight independent readings of the same 16-bit value. If all eight
agree, the memory read is right *and* the password is known; if they disagree, no number
is reported. That is also the calibration criterion for the whole memory layer, so the
idle bits and address width are settled by a real test rather than by picking.

`pass2` is never on this wire — it is an argument the library passes, not something the
part returns. It is reported only as what the known pairing implies, and labelled.

### The key falls out of linear algebra

The keyed round is a twelve-bit shift register, and every unknown in it — the 32 key bits
and the 11 bits of the initial register — enters linearly over GF(2), `st` included:
`(st ^ 1) & (i5 >> 3)` is `st ^ 1` when bit 3 of the query is set and zero otherwise, and
bit 3 of the query is ours to choose. So each observed answer is one equation in 43
unknowns, and a few thousand random queries solve the part outright. No plaintext, no
disk image, no seed-cracking.

It is self-checking in the way that matters: a misread wire does not produce a wrong key,
it produces an inconsistent system. The recovered key is then replayed against every
observation **and** against 16 rounds held back from the solve, so the report says
`5632 of 5632` or it says the model did not fit — and if it did not fit, the raw
(query, answer) log is written anyway, because that failure is the finding.

Self-test
---------

`Self-test` in the window, or `DONGDUMP.EXE /selftest` (writes `SELFTEST.txt`, and to
stdout if redirected). Needs no dongle, no port and no privilege.

```
record decode, against records read off real dongles:
  OK      Photo Play 2001 ES -> pass1 7477, 2001 (ES), high-first, FINDIT 0001D760
  OK      I.G.O. 3 PT -> pass1 6B91, 2003 (PT), low-first, FINDIT 0001D760
  OK      I.G.O. 7 ES -> pass1 68BB, 2007 (ES), low-first, FINDIT 0001D760
key recovery, against a simulated part:
  OK      Photo Play 2001 (7477/7D57) -> key CF47CB42, init 7DF (5632/5632, 704/704 held back)
  OK      I.G.O. 2 (68BB/1329) -> key 3B227944, init 7DF (5632/5632, 704/704 held back)
  OK      I.G.O. 3 (6B91/24A3) -> key AB32E970, init 5DF (5632/5632, 704/704 held back)
  OK      an arbitrary part -> key 12345678, init 123 (5632/5632, 704/704 held back)
```

The three record vectors are real ones, from `PhotoPlay2000_h5dmp`, with the per-unit
dword blanked; the four key cases plant a known key in a software part and ask whether
the same key comes back. Run this once on a new machine, so that a later failure can only
be the dongle or the wiring.

It has already earned its place twice. It found that variant B could never observe key
bit 19 — that payload is byte-for-byte the round preamble — and it found that the eight
content dwords start at record byte 30 with the 1999 layout's `v[1]`, so FINDIT's `+1C`
key is dword **2**, not dword 3.

Running it
----------

0. **Help** — how to get port access on this machine, and which address to use.
1. **Self-test** — no hardware needed.
2. **Detect** — the session gates only. If the identity ramp answers the same bit 64
   times, the port address or the cable is wrong and a full dump would only be a large
   wrong file. Stop there.
3. **Dump everything** — writes the folder.

Port access comes from one of two backends, driver first:

- **inpout32.dll / inpoutx64.dll**, if either is beside the program or on the path. Ships
  a signed kernel driver, so this is the one that works on 64-bit Windows 7/10/11 under
  WOW64. Needs Administrator. Nothing is installed or extracted by this program — install
  InpOutx64 yourself if you want this path.
- **`NtSetInformationProcess(ProcessUserModeIOPL)`**, 32-bit NT/XP only. Installs
  nothing, and is the fast one. Note that being an Administrator is *not* enough: the
  account needs "Act as part of the operating system", and a log off and back on for it
  to take.

**Help** in the window writes both procedures into the log, step by step — secpol.msc on
XP Professional, `ntrights.exe -u MACHINE\username +r SeTcbPrivilege` on XP Home, and the
InpOut32 setup — along with what each failure status means, how to find the real port
address, and why the port must be in SPP mode. `DONGDUMP.EXE /help` prints the same text
to `HELP.txt` and to stdout, for pasting into a mail to whoever has to grant the
privilege.

The single most common failure is skipping the log off: a privilege enters the token when
the session is created, so granting it changes nothing until you log out and back in.

Output
------

```
<version>-<language>-<YYYYMMDD-HHMMSS>/
  SUMMARY.txt          read this first, and send this back
  session.txt          the ramp, the sweep and the liveness probe, step by step
  memory.raw           the 64 words as read, little-endian
  memory.txt           the same, with each word descrambled
  record.bin           the 112-byte record
  record.txt           hex dump, and the eight content keys
  transform.txt        key, initial register, fit, and every observation
  transform-raw.bin    two bytes per observation, for solving offline
  wire.log             every port access, capped at 4 MB
```

`SUMMARY.txt` ends with a proposed `hd_keys[]` row for
`src/device/dongle_photoplay.c` — `pass1`, `swap`, shape, `v6`, `v7`, `tkey`, `tinit`,
`tclk`.

### What is deliberately not written

An h5dmp-shaped `hasp.dmp`. Those carry a 166-byte crypto table at offset `0x009` which
no part of the game's protocol ever puts on the wire, so this program cannot fill it.
Writing the file with that region zeroed would produce something indistinguishable from a
real dump at a glance and wrong exactly where it matters. `transform.txt` carries the
functional equivalent — the key that table computes with.

Building
--------

`build.cmd` — MSVC x86, `/NODEFAULTLIB`, own entry point, no C runtime, because the
modern one needs Vista and the machines with working parallel ports are XP. Imports
KERNEL32, USER32, GDI32 and SHELL32 only.

Verify the result is XP-compatible: machine `014C`, magic `010B`, subsystem 2 (GUI),
subsystem version 5.01.

Reading the results
-------------------

Three things in `SUMMARY.txt` say "this is new, look at it":

- the identity signature is not `CEFF0AFFCECE0A0A`;
- the sweep answer is not `F57A37E78F8FBDDA` — it has only ever been measured on one
  part, and if those 64 bits are a function of the password then other parts differ;
- `pass1` is not `7477`, `68BB` or `6B91`.

And the thing that makes all of this provisional: **a completed handshake is not a
passing check.** Nothing here is proven until a cabinet boots on it with pictures on
screen.
