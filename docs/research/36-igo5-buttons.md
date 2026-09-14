# Phase 36 — I.G.O. 5's garbled buttons are HaspDecodeData, and the 6B91 login never completes

Measured 2026-09-14 on `IGO5\PPIGO5PT-NSB MB001-NECAPP` at 1.9.2 (`7427735`), with the
full port trace (`PEEPEEBOX_LPT_TRACE=1`, `86box-trace.log` in that rig folder, 118,488
lines). The screen: MENU is fine, and on a game's start screen the three big buttons
(*Jogue / Hi-Score / Mudar jogo*) draw as RLE noise while the *Instruções* button beside
them is fine. This is the "menu buttons garbled" row of the README, and it is not the
menu's.

## 1. It is not the menu, and not the UI language

`docs/research/10` blamed a blank UI language from an empty record buffer. That was the
keyless-patched menu. This `MENU.EXE` is unpatched (no `INT 2Bh` site; byte-identical
across the PT, DE and NL images), it reads words 08..26 cleanly, writes `SYSTEM.INI` as
`POR ENG,POR,SPA`, and `\MENU\BUTTONS\POR\FOTOPLAY.WAD` decodes: every entry's header
comes out as a valid PCX under the `0x12345` LCG, bodies at entropy 3–5. MENU decrypts
headers at exactly two call sites (`0x2C6C3`, `0x336EF`), both with the constant seed;
the "seed writers" `0x3C02B/0x3C44F/0x3C45B/0x3C4BC` the handoff pointed at are
save/restore of the RNG around drawing.

## 2. Two files in one archive are enciphered whole

The start screen is the game's (`TOWERS.EXE`), drawn from `\FOTO\HISCORE\2005\FOTOPLAY.WAD`:

| entry | 2002 / 2003 / 2004 sets | **2005 set** |
|---|---|---|
| `INSBUTP.PCX`, `QUICKB*`, `HISCBACK`, … | header LCG, plain body (~4 bits) | same (3.4–3.9) |
| `BUTP.PCX`, `BUTR.PCX` (222×52) | header LCG, plain body (~4 bits) | header LCG, **body 7.95 / 7.96**, palette marker gone |

The archive is byte-identical on the PT, IT and NL images, so the key is not per unit.
The body is not the LCG continued (tried from 0 and from 128, seeds `0x12345` and the
eight record dwords: still 7.99), and the Feistel constants `5B2C004A` / `803425C3` are in
none of `TOWERS.EXE`, `MENU.EXE` or `\MENU\EXE`.

## 3. The game asks the HASP library to decode them

`TOWERS.EXE` `0x1BE16`..`0x1BE5E` sets `[0x2E9A] = 1`, `[0x2E9B] = 3`, loads `butR.pcx`
and `butP.pcx` through `0x15D4:080C`, then clears both. Inside the loader (`0x1A22A`):

```
01A24E  cmp [0x2E9A],0          ; enciphered-body mode?
01A267  cmp [0x24D0],0          ; not logged in yet?
01A282  push 0x24A36B91         ; pass2:pass1 = 6B91/24A3
        push [0x24D0] ; push [bp-0x12] ; push 5
01A291  lcall 2E51:0007         ; HASP service 5
01A29C  mov [0x24D0],ax         ; keep what it returned
...  per 4 KB block:
01A484  push 0x24A36B91
        push [0x24D0] ; push [bp-0x12] ; push 0x3D
01A493  lcall 2E51:0007         ; HASP service 61, HaspDecodeData
                                ; p1 = [0x2E9B] (3), p2 = length, p3:p4 = buffer
```

So the two button faces are the one thing on I.G.O. 5 that goes through the part's
block cipher — the same `EncodeData`/`DecodeData` pair I.G.O. 3's MENU calls at boot —
and the return value is never checked, which is why the game draws the ciphertext instead
of stopping.

## 4. What the wire shows: the login retried 33 times, no round ever asked

Over the whole game there is not one bit-0 consultation — no keyed round, as on I.G.O. 3
(`docs/research/32` § 9). What there is, with our answers under it, is the login loop:

```
RAMP (80..FE, 64 STATUS reads)  →  fifteen command bytes (0x1B50)  →  probe
→  64-step SWEEP  →  46  →  C7 C6 C0  →  command bytes  →  RAMP again …   × 33
```

then the record read proceeds regardless (the game is not checking). On I.G.O. 2 the
same library completes this once and goes on to 4,720 consultations; here it never
accepts the answers. The two families answer the same questions differently:

- the **measured** `68BB/1329` answers (`HD_SIGNATURE`, `HS_SWEEP_A`) make the 6B91 build
  say `wrong dongle version` before it reads anything (1.7–1.9.1);
- the **synthesised** `0x1C` rule (1.5, and 1.9.2 for I.G.O. 5 only) gets the record read
  and the menu up, but not through service 5 / 0x3D.

The obvious hypothesis — that the ramp and the sweep are the picture oracle run over their
64 inputs, which would make the 6B91 answers computable from key `AB32E970` — was tried
against the 68BB measurements with `softpart.consult` over `(w>>1)&0x1F`, `w&0x1F`,
`(w>>2)&0x1F`, four register starts and both bit orders: no match, and nothing within 8
bits of one. Whatever the session answers are, they are not that.

## 5. Where that leaves it

One measurement closes this and I.G.O. 3 together: the login exchange of a real
`6B91/24A3` part — any 2003 or 2005 dongle on the passthrough port, `dongcap` as for
I.G.O. 2. The 2005 h5dmp dumps hold that family's crypto table, but the session answers
have not been derived from a table for any family, so the dump alone does not do it.
Failing a part, the check is in the library at `TOWERS.EXE` segment `0x2E51` (file
`0x32910`), service 5, which is where it decides the ramp and sweep were wrong.

## 6. The service-5 check, read — and the two answers it refused

`TOWERS.EXE`'s HASP library (segment `0x2E51` wraps, `0x285A` is the core) does the
following, all measured against the trace:

- **Ramp** (`0x2EE15`): 64 writes of `i<<1`, `acc = 0x7E ^ XOR{i<<1 : DO set}` — as
  documented for the 2001 library.
- **Identity table** (`0x2D1D9`, data at `0x2D27C`): the same four keys `08 0C 18 1C`, but
  the 2005 handlers are not the 2001 ones:

  | acc | type (`+0x82`) | memory (`+0x10`) |
  |---|---|---|
  | `0x08` | 1 | 1 = 64 words, or 0 |
  | `0x0C` | 1 | 4 = 256 words |
  | `0x18` (measured 68BB) | 3 | 0 |
  | `0x1C` (synthesised) | 5 | 0 |

  Type 5 is still given 256 words for memory reads (`0x2D789`), which is why `0x1C` reads
  the record. The memory/data services check `+0x10` (`0x2DE84`, `0x2DFC3`) and return
  error 3 on zero.
- **Sweep** (`0x2EEA7`): 64 bytes of `x = x*0x1989+5` from 100, one bit each into four
  words, then byte-swapped; rejected only if all-zero or all-one, plus a heuristic in
  `0x2E89D` that flags a result with any byte equal to the constant at `DS:0x40EA`
  (eight zero bytes). Nothing in it is password-derived.
- **Command bytes** (`0x2E4FC`): the fifteen are constants from `DS:0x40CA`, the same
  for every family; the password only builds a 27-byte table that is applied on top when
  non-zero, and the trace shows the 2001 sequence verbatim on this build.

`MENU.EXE` carries the identical table (`0x3FF29`). So `0x0C` and `0x08` were tried, as
the two answers that name a MemoHASP with memory. **Both fail sooner than `0x1C`:** the
menu runs ramp → command bytes → probe → sweep, twice, and says `wrong dongle version`
without one memory read, where `0x1C` runs the same cycle three times and then reads.
Whatever the type-1 path checks between the sweep and the first read is not the sweep
and not the ramp, and is not yet located. `0x1C` is back in the build.

## 7. The type-1 path, and why it does not matter: the decode call is refused before the wire

Following the menu's own check (`MENU.EXE` `0x3A879`): `hasp(1)` must find a part,
`hasp(5)`, then `hasp(0x32)` ReadBlock of 56 words from word 0, then the banner is formatted
and compared. With `0x0C` or `0x08` the trace ends after the second detection: ReadBlock
never reaches the wire, so the buffer the menu formats is whatever was on the stack. The
read handler (`0x2D67F`) is common to types 1 and 5 once the size is set, so the difference
sits in the login (`0x2E89D`) or in a type-1 memory primitive this device has never seen a
part answer; it is not resolved here, and it does not need to be, because of what follows.

**The 2005 library's `HaspEncodeData`/`HaspDecodeData` take one or two 8-byte blocks per
call, and nothing else.** The packet translator (`0x30C9A`, cases `0x3C`/`0x3D` at
`0x30E4A`/`0x30EFC`) requires `p1 <= 4` and `p2 >= 8`, and stores `p2 >> 3` — the block
count — at `ctx+0x16`. The handler (`0x2DDB7`) then checks, before any detection or port
access:

```
02DE28  cmp [bp-0xA],0 ; cmp [bp-0xC],2 ; je ok      ; ctx+0x16 == 2  (16 bytes)
02DE34  cmp [bp-0xA],0 ; cmp [bp-0xC],1 ; je ok      ; ctx+0x16 == 1  ( 8 bytes)
02DE40  mov es:[bx+0x1A],0xA  ; error 10, invalid parameter
```

and the cipher itself (`0x2FBEE`, the `0x803425C3` Feistel) runs on one block, with the
second at `+8` when there are two. That is the API I.G.O. 2's FINDIT uses correctly — its
own `0x2D04C` loops over the picture and hands the library eight bytes at a time — and it
is what I.G.O. 3's boot check uses for its 20 bytes (two blocks).

`TOWERS.EXE` (Version 4.0, dated 04.09.2000 in `TOWERS.INF`) does not do that. Its loader
(`0x1A22A`) reads the file in 4 KB chunks and passes each whole chunk to service `0x3D`:
`p2 = bytes read = 0x1000`, so `ctx+0x16 = 512`, so **error 10, on every chunk, before
the library has looked for a dongle at all**. The return value is never checked, the
ciphertext is drawn, and the three buttons come out as noise. No identity answer, no
sweep, no captured 6B91 part changes that: the refusal is arithmetic on the caller's own
arguments.

So the garbled buttons on I.G.O. 5 are, on this reading, **what a real cabinet shows
too**: a 2000-vintage game calling a 2005 library with a buffer it cannot take, over two
files funworld packaged enciphered in the 2005 set and plain in every other. Three things
would falsify it, in order of cost: a photograph or video of a real I.G.O. 5 start
screen with clean *Play / Hi-Score / Change game* buttons; a game in the set whose loader
chunks at 8 or 16 bytes; or a `0x3D` call in the trace that reaches the wire. None of
the traces taken today has the last, and the disassembly says none can.

What this does settle for the emulator: `0x1C` is the right synthesised answer for this
build, the record read is the only dongle traffic the game needs, and the README row's
"menu buttons garbled" is a property of the image, not of the emulation. I.G.O. 3 is
untouched by any of this: its boot check is a two-block call, which the library accepts,
and it still fails on the answers themselves.
