# Phase 29 — the dongle's wire, completely

Branch: `picturedecryptingtest`. From I.G.O. 2's `EXE\FINDIT.EXE`.

Phase 28 established that the keyed round cannot be computed from its input, because the
part carries state between rounds. That makes the *wire* the thing the emulated device
has to get right, and it is now read out end to end.

## 1. One query — `0x32F59`

```
032F5F  al = (byte << 1) & 0x0E        payload bits 0,1,2 -> DATA bits 1,2,3
032F66  dl = (byte << 2) & 0x60        payload bits 3,4   -> DATA bits 5,6
032F70  al |= dl
032F75  and [bp+6], 0xEF               DATA bit 4 low
032F84  call 0x32DB2                   write
032F8D  or dl, 0x10 ; call 0x32DB2     write with DATA bit 4 high
032FA9  call 0x32DB2                   write with DATA bit 4 low again
032FB6  call 0x32D17                   read the answer
```

so **five payload bits are held on DATA lines 1, 2, 3, 5 and 6 while bit 4 is pulsed as a
clock**, and one bit comes back. Bits 5..7 of the caller's byte are discarded, which is
exactly `in_5_bit = byte & 0x1F`.

## 2. Every write — `0x32DB2`

```
032DBB  if state[+0x14] == 0:  al = (byte & 0xFE) | 0x80
032DCE  else:                  al =  byte        | 0x81
```

**DATA bit 7 is always set**, and **bit 0 is a framing flag**: cleared while `state[+0x14]`
is zero, set once it is not. `0x32DE2` is the routine that zeroes `state[+0x14]` before
writing, so a byte sent through it opens a frame and everything after it continues one.

So the byte on the wire is

```
bit 7  always 1
bit 6  payload bit 4
bit 5  payload bit 3
bit 4  clock
bit 3  payload bit 2
bit 2  payload bit 1
bit 1  payload bit 0
bit 0  0 = first byte of a frame, 1 = continuation
```

## 3. Every answer — `0x32D17`

```
032D30  if state[+0x4C] & 4:  shift = 7   else  shift = 5
032D49  call 0x32CE9                       read the port
032D53  sar ax, shift ; and al, 1
032D59  if shift == 7: al = (al == 0)      bit 7 is returned inverted
```

The answer is **STATUS bit 5**, or bit 7 inverted when that mode flag is set — the
inversion being the usual one for the BUSY line. That matches what this project has
served on STATUS bit 5 since the 2001 work.

## 4. What opens a keyed round — `0x32FC2`

Before the 40 queries, the round sends:

1. a **seed byte** from `0x32F36`, which is
   `((T[fn] ^ 0x7E) & ((T[fn] ^ 0x7E) - 2)) ^ K` — a byte-indexed table `T` at `DS:0x2F94`
   looked up by the low byte of the function code (`0x3D` for DecodeData), and a constant
   `K` at `DS:0x2FB1`;
2. the byte `0x4E`;
3. for the argument the keyed round passes (2), the byte `0x04`.

Both bytes 1 and 2 go through `0x32DE2`, so they open the frame.

> The two constants are **not pinned**. This module's DGROUP base did not resolve
> unambiguously — two candidates fit the pointer at `DS:0x3080`, giving `T[0x3D]=B8, K=A5`
> (seed `0x61`) or `T[0x3D]=08, K=FF` (seed `0x8B`), and neither makes the neighbouring
> code tables read cleanly. Recorded as unresolved rather than guessed; a live trace would
> settle it in one run.

## 5. Why this matters more than the algorithm

The device cannot compute the answers — Phase 28 closed that off. But it now knows exactly
what it will be asked: a framed stream of 5-bit queries, one bit of reply each, 40 per
keyed round, two rounds per 4 KB buffer, opened by a fixed three-byte preamble. Anything
that can produce the right bit stream — a captured trace, a solved table, a model of the
part — plugs in at a known interface.

And the harvesting of Phase 27 gives the other half: for any buffer whose plaintext is
known, the exact answers are recoverable. Trace and ground truth now meet at the same
boundary.
