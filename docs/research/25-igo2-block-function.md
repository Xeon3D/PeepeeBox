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

## 5. The composition, read in full — and it is HaspEncodeBlock

`0x32837` in full, with the block as two dwords `L` at +0 and `R` at +4:

```
0x3284A  keyed round      (L,R) <- (R, f(R) ^ L)          call 0x32746 on R
0x3289E  six rounds       (L,R) <- (R, ROL32(R ^ 0x803425C3, (2i) & 31) ^ L)   i = 0..5
0x32942  keyed round      (L,R) <- (R, f(R) ^ L)
0x32996  six rounds       (L,R) <- (R, ROL32(R ^ 0x5B2C004A, (5i) & 31) ^ L)   i = 0..5
0x32A56  if the second pointer argument is non-NULL, write BOTH f values to it:
             the first at +0, the second at +4
```

Both round loops end `cmp [bp-4], 6`, so six rounds each, and both shift schedules
**ascend** — `2i` giving 0,2,4,6,8,10 for the `0x803425C3` stage and `5i` giving
0,5,10,15,20,25 for `0x5B2C004A`.

That is `HaspEncodeBlock` from io.hasp4, operation for operation, including the pair of
writes into the second buffer. **The transcription used in Phase 24 was correct**; what
was wrong there was something else.

## 6. Which routine is which, and when chaining is on

The per-block dispatcher at `0x30FD8`:

```
30FD8  cmp word [bp-0x1a], 0x13D
30FDD  jne encode
30FF9  call 0x32A81        <- 0x13D, DecodeData
031018  call 0x32837        <- everything else
```

so **`0x32A81` is decode and `0x32837` is encode**, and the picture path takes `0x32A81`.

The second pointer is conditional:

```
30FB1  cmp word [bp-0xa], 0 ; jne skip
30FB7  cmp word [bp-0xc], 2 ; jne skip
30FBD  ptr = current block
30FC3  add dx, 8            ; the NEXT block
30FC6  [bp-0x16]:[bp-0x18] = that
```

It becomes `block + 8` only when those two flags hold, and is passed as zero otherwise —
io.hasp4's `use_chained_blocks`, decided per call.

## 7. The dispatch table, and the handler's own checks

The handlers are reached through a table at file `0x038AF8`, three words per entry —
offset, segment, function code:

```
09E1 2B33 012F
1231 2B33 013C      EncodeData  -> 0x30D61
1396 2B33 013D      DecodeData  -> 0x30EC6
1396 2B33 013B      also        -> 0x30EC6
150B 2B33 FFFF      terminator
```

`0x30EC6` is therefore the DecodeData handler, and it reads its parameters out of the
request state:

| state | meaning |
|---|---|
| `+0x08`, `+0x0C` | the two buffer pointers |
| `+0x16` | the operation — must be **1 or 2**, else it returns error `0x0A` |
| `+0x18` | the function code, compared against `0x13D` |
| `+0x1A` | the status it writes back |

and the chaining test is `[bp-0xa] == 0 && [bp-0xc] == 2`, i.e. **chaining is on only when
the operation is 2**. Operation 1 — decode — never sets the second pointer.

The handler transforms **one block per call**; nothing in it loops over 512 of them.

## 8. What this settles, and what it does not

**Settled.** The cipher is HASP DecodeData; the unit is a 4 KB buffer; the keyed round is
the 40-step LFSR with polynomial `0x80500062` and a hardware oracle; the block function is
`HaspEncodeBlock`/`HaspDecodeBlock` exactly as io.hasp4 has them, six rounds per stage with
ascending shifts; `0x32A81` is decode and `0x32837` encode; chaining exists but is gated on
operation 2; and buffers of eight bytes or fewer are skipped.

**The contradiction, stated plainly.** Chaining writes the f values *over the next block*
before that block is read, which applied to file data would destroy everything after the
first block — and in any case decode runs with operation 1, where the second pointer is
never set. So decode is per-block independent, which is ECB. But the corpus refutes ECB
outright: blocks 97 and 98 carry identical plaintext and eight different ciphertexts.
Both cannot be true.

Something therefore combines blocks *outside* this handler, in whatever walks the buffer
eight bytes at a time. Phase 24 tested XOR-with-previous in three arrangements and refuted
all of them, so it is not plain CBC either.

**And the oracle stays hardware.** `0x32F59` is a byte-in, bit-out call against the dongle
state. Reading this binary does not give up its key material.

## 9. Next

1. **Find the loop.** The handler does one block; the multi-block walk is above it, in the
   library's packet layer around `0x35037`, or in `FINDIT.EXE`'s own caller. That loop
   holds the combination step, and it is the last structural unknown.
2. Re-run the Phase 24 consistency test with whatever it does. The extractor is validated
   and the corpus is 72 MB, so it is one run to confirm or kill.
3. The oracle's key material remains separate, and nothing in `FINDIT.EXE` will supply it.

---

# Phase 25b — the loop, found

`0x30EC6` was a red herring: the packet layer never reaches the generic dispatcher for
this service. At `0x35171` it branches on the service byte in the request:

```
035174  cmp byte es:[bx+0x16], 0x3C ; call 0x349A0     EncodeData walker
035196  cmp byte es:[bx+0x16], 0x3D ; call 0x34CC0     DecodeData walker
0351B8  else                          call 0x3328A     the generic one-request path
```

## The walker — `0x34CC0`

```
034CC8  block_count = (length + 7) >> 3            lcall 0,0x13D0 with cl=3
034CE1  remainder   = length & 7 ; if 0 then 8
034D0D  prevL = prevR = 0                          the IV is zero
034D2F  if block_count < 2: skip the whole loop
034D43  i = 0
loop:
  034D50  save the block's ORIGINAL ciphertext:  Cl = [bx+0], Cr = [bx+4]
  034D6E  if i != 0 goto 0x34E50
          -- i == 0 --
  034D8F  call 0x34629(buffer, &[bp-0x2c], state)   the dongle path; returns D and acc
  034DA3  j = 1
  034DB0  while j < 0x1A:                            build a TWENTY-SIX entry schedule
            sched[j]  = sched[j-1] + acc            [bp-0x28]:[bp-0x26] is acc
            cl        = sched_bytes[j] & 0x1F
            D         = ROR32(D, cl)                shr by cl, shl by 32-cl, OR
            running  ^= D
            j++
          -- i != 0 --
  034E5D  call 0x3483D(buffer, &[bp-0x94])           a pure SOFTWARE round on the schedule
          -- both --
  034E63  block.L ^= prevL ;  block.R ^= prevR       <-- the combination
  034E81  prevL, prevR = the original ciphertext saved at the top
  034E99  buffer += 8 ; i++
034EA5  while i < block_count - 2                    the last two blocks are NOT transformed
```

## What this settles

1. **The mode is CBC with a zero IV**, and it is applied *after* the transform:
   `P_i = T(C_i) ^ C_{i-1}`, with `prev` seeded to zero and updated from the block's
   original ciphertext. That much Phase 24 guessed right.

2. **`T` is not one function.** Block 0 goes through `0x34629` — the dongle path, the
   Feistel-and-keyed-round chain of Phase 25 — and returns **two dwords**. Blocks 1
   onwards go through `0x3483D`, a **pure software round** driven by a **26-entry
   schedule** built from those two dwords. That is why Phase 24 found zero agreement
   everywhere: it applied the Feistel transform to every block, and it is right for
   exactly one block in 512.

3. **The last two blocks are left alone** (`i < block_count - 2`), which is why entries
   measured 99.6% different rather than 100%.

This is `HANDOFF2001.md` § 24's model — first block to the dongle, the rest through a
software round over a 26-entry schedule derived from two dongle dwords — confirmed here
for I.G.O. 2 from its own binary, having been derived for 2001.

## Why this is the way in

Only **two dwords per 4 KB buffer** come from the dongle. Everything else is arithmetic
this project can run. And all 1397 entries share ciphertext block 0, so the first buffer's
pair is the *same for every picture in the archive*.

With I.G.O. 4's plaintext, `D` and `acc` are recoverable by solving rather than guessing —
blocks 1 and 2 give 128 bits of known plaintext against a 64-bit unknown — and once
recovered, the whole archive decrypts with no dongle at all.

Next: transcribe `0x3483D`, then solve for `D` and `acc` against the corpus.
