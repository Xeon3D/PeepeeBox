# Phase 19 — the HASP passwords, per generation

Marcos's identification: the 2001 and I.G.O. dongles are **HASP4**, and everything
measured here is consistent with it. `docs/research/13` and `HANDOFF2001.md` § 2.5
should now be read as settled in that direction for **2001 and the I.G.O. line** —
1999 and 2000 are *not* HASP (confirmed by Marcos, and they carry none of the
library's fingerprints). The 2008 generation is the one to be careful with: the
IGO 8 images use the serial reader of Phase 18, but a 2008-era build in the same
collection is parallel HASP throughout. See the last section.

Corroborating evidence for HASP4, from `HANDOFF2001.md` §§ 16 and 19:

- the memory is **112 bytes**, MemoHASP's size, and exactly the 56 words the
  library reads;
- two 16-bit passwords and the published nine-argument call
  `hasp(service, seed, port, pass1, pass2, &p1..&p4)`;
- the services used are the published ones — `1` IsHasp, `5` HaspCode,
  `0x32` ReadBlock, `0x3C`/`0x3D` HaspEncodeData / HaspDecodeData, the last two
  working in 8-byte blocks with a minimum size of 8.

## The passwords

| generation | pass1 | pass2 | how it was established |
|---|---|---|---|
| Photo Play 99 | — | — | not HASP |
| Photo Play 2000 | — | — | not HASP |
| **Photo Play 2001** | **7477** | **7D57** | 25 of 25 executables, all five territories and the keyless build; eye-verified at `FINDIT.EXE` `0x170A3` (`pushd 0x7D577477`) |
| **IGO 2** | **68BB** | **1329** | 16-20 of 20 executables in each of 8 images |
| **IGO 3** | **6B91** | **24A3** | eye-verified at `MENU.EXE` `0x33380` (`pushd 0x24A36B91`); the store form appears in 1-2 executables per image |
| IGO 4 | — | — | **no HASP library in any of 43 executables** — see below |
| **IGO 5** | **6B91** | **24A3** | 20 of 20 executables in each of 6 images; eye-verified at `QUIZPRO2.EXE` `0x337D5` |
| **IGO 6** | **0000** | **0000** | 20 of 20 executables in each of 7 images — see below |
| **IGO 7** | **68BB** | **1329** | 20 of 20 executables in each of 7 images; eye-verified at `DJACK.EXE` `0x2C117` |
| IGO 8 | — | — | not HASP; serial smart-card reader (Phase 18) |

So there are **three** password pairs across six generations: 2001 has its own, IGO
2 and IGO 7 share one, and IGO 3 and IGO 5 share another. That is consistent with
funworld buying a batch of dongles per contract rather than per year.

### Argument order

`pass1` is the fourth argument and `pass2` the fifth. Two independent forms fix the
order and agree:

- **2001 and IGO 3/5** push both at once: `66 68 77 74 57 7D` = `pushd 0x7D577477`.
  A dword push puts its low word at the lower address, and the lower address is the
  earlier argument, so `0x7477` is pass1 — which matches `HANDOFF2001.md` § 2.5.1,
  derived separately.
- **IGO 6/7** store them first and push them separately:

```
    2C10E   enter 0x18E,0                 ; the same frame 2001's read routine uses
    2C117   mov word [bp-2],0x68BB        ; pass1
    2C11C   mov word [bp-4],0x1329        ; pass2
    ...
    2C150   push [bp-4] ; push [bp-2]     ; pushed in reverse, so [bp-2] is arg 4
    2C15A   push 1                        ; service 1, IsHasp
    2C15C   call far <hasp>
```

## Two anomalies, stated as anomalies

**IGO 6 passes null passwords.** All 140 executables across all seven IGO 6 images
store `0000 / 0000` into that same routine. This is not a failed extraction — the
pattern is found, and the value is zero. Either the build genuinely calls with null
passwords, or every IGO 6 image in the collection has been neutered the same way.
Worth settling before anyone builds on it.

**IGO 4 links no HASP library at all.** The `lhsh` signature is absent from all 43
executables on the image, so the two-level search has nothing to anchor on. IGO 4
either uses a different protection library or these images are patched.

## Method

`scratchpad/hasppw3.py` and the prologue scan in this phase.

The reliable anchor is the marshalling function every build links, which stamps
`"lhsh"` into its request struct:

```
    C7 46 ?? 73 68        mov word [bp-0x3e],0x6873
    C7 46 ?? 6C 68        mov word [bp-0x40],0x686c
```

Its file offset converts to what a call targets — `seg*16 + hdrsize + off` for a far
call, `site + 3 + rel` for a near one — so its callers are found exactly rather than
guessed. Some builds call it from game code directly; others interpose a forwarding
wrapper, so the search runs two levels.

### One false trail, recorded because it was convincing

Searching the whole binary for the bare store pattern `C7 46 FE ?? ?? C7 46 FC ?? ??`
looks like a clean signature and is not: it reports `8000 / 000A` for **1999 and
2000**, which have no HASP at all, and `7530 / 8AD0` for most I.G.O. images, which
contradicts call sites read by eye. Those are ordinary constants stored into the top
two locals, and they are everywhere. The pattern is only trustworthy **anchored** —
either inside the `enter 0x18E` dongle-read prologue, or in the window before a call
site that the `lhsh` walk has already confirmed.

A second slip in the same pass: the first version of the store heuristic had pass1
and pass2 the wrong way round, which produced `1329 / 68BB` for IGO 7. Pushes are
reversed and stores are not; the two forms need opposite handling.

## The 2008 generation comes in both flavours

Phase 18 established that the IGO 8 token is a serial smart-card reader. That is
right for the IGO 8 images, and it is not the whole of the generation.

Measured across every executable on each image:

| image | executables | link the HASP library (`lhsh`) | mention `NGDONGLE` |
|---|---|---|---|
| IGO 7 AT | 51 | **51** | 0 |
| IGO 8 ES | 61 | **0** | 15, each paired with the serial base `0x2F8` |
| IGO 8 PT | 61 | **0** | 15 |
| `PPIGOITALY-NSB NONE` | 54 | **54** | 0 |

So the IGO 8 images are serial-only — no executable on them contains the parallel
library at all — while the IGO Italy build of the same era is **entirely parallel**.
Both flavours exist; which one a cabinet has is a property of the build, not the
year.

`MENU.EXE` carries a message for every dongle type the family has ever used, and
the list grows by one letter per generation:

| menu | dongle messages it carries |
|---|---|
| IGO 6 | CDONGLE, HDONGLE, MBDONGLE, … |
| IGO 7 | + ODONGLE |
| IGO 8 and IGO Italy | + NG-DONGLE |

That is why `HDONGLE FAILED` appears in an IGO 8 game's strings (`DJACK.EXE`
alongside `..\LIBS\NGDONGLE.CPP`) even though nothing on the image can talk to a
parallel HASP: the names are a shared table used for the `ERROR.LOG` line, and the
dispatch behind them is a 20-entry jump table on a message code, not the bitmask
2001 used.

**So `docs/research/18` should be read as "the IGO 8 images use a serial reader",
not "the 2008 generation abandoned the parallel port".**

### A caution about the null passwords

The IGO Italy build stores `0000 / 0000` in all 54 executables, exactly as IGO 6
does in all 140 of its. Its folder is named `…-NSB NONE-…`, which reads like a
no-dongle conversion, and that is the most likely explanation for both: the
passwords were zeroed by whoever prepared the copy. If so, the IGO 6 pair is not
`0000 / 0000` — it is unknown, and needs an unmodified IGO 6 image to recover.
Treat both zeros as "not established" rather than as a value.
