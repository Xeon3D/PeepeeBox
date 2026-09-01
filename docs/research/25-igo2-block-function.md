# Phase 25 — E, read out of I.G.O. 2's FINDIT.EXE

Branch: `picturedecryptingtest`. Binary: `EXE\FINDIT.EXE` from
`F:\HDDImages\IGO2\IGO 2 BE 82C81`, 235,090 bytes, MZ header `0x4800`.

Phase 24 refuted the assumed block function by measurement. This is the block function
read out of the binary instead of guessed at.

## 1. The call sites — it really is HASP DecodeData over 4 KB

Five sites, all the same shape (`0xDEDB`, `0x1DF5C`, `0x1E027`, `0x1E17D`, `0x1E2F3`):

```
lcall 0,0x5FE2                 read the buffer
cmp  word [bp-8], 8
jbe  skip                      a buffer of 8 bytes or less is NOT transformed
push ss/&p4  ss/&p3  ss/&p2  ss/&p1
push 0x132968BB                pass2:pass1 = 1329:68BB
push word [0x1704]             lptnum
push word [bp-0x12]            seed
push 0x3D                      service 61 = DecodeData
lcall 0x30A1:0x000B
```

and the surrounding loop indexes buffers with `shl ax, 0x0C` and toggles
`xor byte [bp-1], 1` — **4096-byte buffers, double-buffered**. So the transform restarts
every 512 blocks, which is exactly why block 0 of every entry decodes identically
(Phase 24 § 6.4).

`lcall 0x30A1:0x000B` is the same `hlsh` shim I.G.O. 6's `MENU.EXE` carries: it builds a
request at `[bp-0x48]` with `'hs'`/`'hl'` and hands it to the driver at `0x2B33:0x5507`.

I.G.O. 4 — which ships the same archive in the clear — has **zero** `push 0x3D` sites.
That is the whole difference between the two.

## 2. Why the constants were invisible

A dword search for `0x5B2C004A` finds nothing in this binary. It is 16-bit code, so the
constant is applied a half at a time:

```
0329BA  35 4a 00        xor ax, 0x004A
0329BD  81 f2 2c 5b     xor dx, 0x5B2C        ; DX:AX ^= 0x5B2C004A
```

Both Feistel constants and the LFSR polynomial are present this way, and
**I.G.O. 4 has no `25C3` and no `8034` at all** — the cipher is in the two releases that
decrypt and absent from the one that does not.

## 3. The keyed round — `0x32749`

```
32749  sub sp,0xC
3274C  mov [bp-2],0x8050 ; mov [bp-4],0x0062     poly = 0x80500062, stored hi:lo
32756  push [bp+0x10] [bp+0xE] ; push 2
32762  call 0x32FC2                              oracle init, argument 2
32768  V = the dword at the caller's pointer     [bp-6]=high, [bp-8]=low
32778  push state ; push (V & 0xFF)
32784  call 0x32F59                              cl = oracle(byte 0 of V)
       counter = 1
loop:
  cl |= (V & 1) << 1                             index = (prev bit) | ((V&1)<<1)
  if ((cl ^ V) & 1):  V = (V >> 1) ^ poly
  else:               V = V >> 1
  push state ; push (byte of V at offset cl)
  call 0x32F59 ; cl = al                         the next oracle bit
  counter++
while counter < 0x28                             39 shift steps, 40 oracle calls
32823  store V back
```

This is `HaspTransformWord` line for line: same polynomial, same branch rule, same
`(prev_bit) | ((V & 1) << 1)` byte index, same 40 consultations. **The structure is
confirmed from the guest side.**

`0x32F59` takes the state pointer and one byte and returns one bit. It is the wire
oracle — the dongle's part — and no key material for it lives in this routine.

## 4. The Feistel stage — `0x32837`, and it ascends

```
329B4  DX:AX = R ^ 0x5B2C004A
329C9  ax=5 ; lcall 0,0x122F        s = 5 * i
329D3  and al,0x1F                  s &= 31
329D9  lcall 0,0x13AF               DX:AX << s
329F0  … the same value again
032A04  mov cl,0x20 ; sub cl,al     32 - s
032A0A  lcall 0,0x13D0              DX:AX >> (32 - s)
032A11  or cx,ax ; or bx,dx         the two halves OR'd  ->  ROL32
032A1D  xor with the dword at +0    ^ L
032A24  store to +4                 R = ROL32(R ^ CA, s) ^ L
032A3A  i++
032A4D  cmp [bp-4],6 ; jae exit     SIX rounds, i = 0..5
```

so the shift schedule is `(5 * i) & 31` for **i ascending 0, 5, 10, 15, 20, 25** — where
the model tested in Phase 24 used the descending 25 … 0 that io.hasp4's *decode* uses.
Ascending is what io.hasp4 calls *encode*. The second stage, at `0x328C7` / `0x32BCB`,
carries `0x803425C3` the same way.

The stage's tail is also informative:

```
032A5E  if the second pointer arg is non-NULL:
032A61     store [bp-0xa]:[bp-0xc] at +0
032A74     store [bp-6]:[bp-8]     at +4
```

— a two-dword write into a *second* buffer, which is io.hasp4's `next_block` chaining
write. So chaining is real in this build and is not a decompiler artefact.

## 5. What this settles, and what it does not

**Settled.** The cipher is HASP DecodeData; the unit is a 4 KB buffer; the keyed round is
the 40-step LFSR with polynomial `0x80500062`; the Feistel stages use `0x5B2C004A` and
`0x803425C3` over six rounds with shifts `5i` and (at the other stage) the second
constant; a chaining write into a following block exists; and buffers of eight bytes or
fewer are left alone.

**Not settled.** The order the two stages and the two keyed rounds are composed in, which
is the one thing Phase 24 needed and the one thing still being inferred rather than read.
Four `0x5B2C004A` sites and four `0x803425C3` sites exist, which is consistent with a
separate encode and decode path each carrying both stages; only one has been transcribed.

**And the oracle stays hardware.** `0x32F59` is a byte-in, bit-out call against the
dongle state. Emulating it is the same problem as before — the security table and how it
is keyed — and reading this binary does not give it up.

## 6. Next

1. Transcribe the remaining three stage bodies at `0x32837`, `0x32A81`, `0x32B60`-ish and
   the caller that sequences them, to get the exact composition order.
2. Re-run the Phase 24 consistency test with that order. The extractor is validated and
   the corpus is 72 MB, so it is one run to confirm or kill.
3. The oracle's key material remains the last unknown, and nothing in `FINDIT.EXE` will
   supply it — it has to come from the security table in the dumps, or from watching a
   real unit.
