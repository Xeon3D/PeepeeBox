# 9. What is genuinely not known

Everything in the other files is measured, verified or explicitly recorded. This is the
complement: the gaps, stated as gaps so nobody has to rediscover that they are gaps.

## 9.1 I.G.O. 3 does not boot

It now asks the keyed round with its own key `AB32E970` and stops at
`error number 228.250.107, in module MENU, dongle error`. Asking and being refused is a
narrower failure than never asking, but it is still a failure.

The round is not the suspect — `AB32E970` transforms I.G.O. 3's boot-check block to
`c:/foto/`, which a wrong key does not do. What is unmeasured is everything around it:

- **The session layer's sweep reply is a `68BB/1329` part's** (`05.3`). If those 64 bits
  depend on the password, this device is handing I.G.O. 3 another dongle's answer.
- **I.G.O. 3's session traffic looks different in kind.** Its writes in the logged window
  are `8A/8B`, `94/95`, `BA/BB`, `DA/DB` — the *cooked* form with bit 7 **set** — where
  I.G.O. 2's session layer is bit-7-clear throughout. So the model may not transfer, and
  those bytes also trip the keyed round's preamble rule and reset its register, which is
  right for a round preamble and wrong if they are something else.

That window is 300 writes with repeats collapsed, so it shows the shape of the traffic
and not the whole boot. **A passthrough capture of an I.G.O. 3 boot would settle it**, the
way the I.G.O. 2 capture settled I.G.O. 2.

## 9.2 Whether the session layer generalises

The whole session model (`05.3`) comes from **one boot of one game on one `68BB/1329`
part**. Three specific things are unverified beyond it:

- Whether a `7477/7D57` or `6B91/24A3` part answers the identity ramp with the same 64
  bits. It looks like a property of the model rather than the key, but nothing shows it.
- Whether the 64-step sweep's *reply* is a function of the password.
- Whether the sweep's *written* bytes are the same in every release. If they differ the
  matcher simply will not fire and the old fallback stands — wrong, but no worse.

## 9.3 The 166-byte crypto table in the dumps

Bytes `0x009`..`0x0AE` of each h5dmp dump are **identical for every dongle that shares a
password pair**, across generations — so it is the customer's key material, not per-unit
or per-year:

| dongles | passwords | table begins |
|---|---|---|
| 2001 ES, 2001 PT | `7477 / 7D57` | `7B 6E AF E5 32 3B 5D 5D` |
| 2002 PT, 2006 PT, 2007 ES | `68BB / 1329` | `3B 7D B9 9E 22 71 E7 21` |
| 2003 ES/PT, 2005 ES/PT | `6B91 / 24A3` | `5E D7 BC 7D 32 5A D7 57` |

**How it drives the byte-to-bit oracle is not known.** The fitted key `3B227944` does not
appear anywhere in the 2002 PT dump under any of the four plausible table-indexing modes,
at any of the 703 offsets. Whatever that block is, it is not those 32 bits laid out
plainly — and the working route (`05.4`) turned out not to need it.

## 9.4 2001's initial register

`0x7DF` is **recorded, not derived**. The password schedule that turns `0x132968BB` into
I.G.O. 2's `0x7DF` does not produce it from either order of `7477/7D57` (they give
`0x55F` and `0x5DF`). The 2001 library is a different build and already differs in word
order, so that is where to look.

## 9.5 I.G.O. 5's picture-cipher key

Listed as `AB32E970` because it shares I.G.O. 3's password pair. **That is the pair
talking, not a measurement** — I.G.O. 5's FINDIT is plain, so nothing here could check
it.

## 9.6 The 2000 generation's unrecovered functions

- **The challenge→response function.** The licence pair `A0 {86 2E D0} -> 93 46` is the
  only one ever seen. Nothing can compute an answer for a challenge that has not been
  observed.
- **The closed form behind the picture-key table.** `S(c) = a[hi] + b[lo]` with both
  halves linear in the bits is a *characterisation*, not the device's own arithmetic.
  Anyone who recovers the real function must reproduce the shipped table (3765 of 3765).
- **The real constants the `A3` and `A4` queries answer with.** Not needed — the constant
  is only a case tag — but not known either.

## 9.7 The 1999 record's per-unit words

`v[0]` is uninitialised host memory from the programming PC (one dump contains the ASCII
`"eter"`, another an x86 function epilogue). `v[7]` varies per unit and is **not** a
checksum — sum8, xor8, sum32 and CRC32 over every plausible sub-range were tested and
none reproduces it. Nothing in the firmware reads or verifies it, so it may be a serial
number or more debris; the dumps cannot distinguish.

Also unexplained, though settled as fact: `v[1]..v[6]` are byte-identical across four
different physical dongles *and* across generations, so they are not per-site anything.
What the numbers *mean* is unknown; what they *do* is `07`.

## 9.8 Which dongle type 2000–2007 nominally is

The `MENU.EXE` type table grows by a letter per generation, which makes `M`/`MB` for
I.G.O. 5, `N` for I.G.O. 6 and `O` for I.G.O. 7 the obvious guesses. **They are guesses.**
Booting each image without a dongle names its type on screen — that is the cheap way to
settle it and it has not been done. It changes nothing about the emulation, which is
driven by what is on the wire.

## 9.9 The 2008 parallel probe

Black-box search on the IGO 8 parallel probe was exhausted: every distinct STATUS value
and every DATA-readback transform fails, and no further traffic exists to observe. This
turned out not to matter — the IGO 8 token is not on the parallel port at all (`06`) —
and the sweep options left in the code are research scaffolding, not a working path.
Do not restart that search.

## 9.10 Hardware that is available for these questions

A `68BB/1329` dongle on a parallel port, and an unpatched original I.G.O. 2 PT cabinet.
The passthrough rig and capture tooling are in `tools/dongcap/`. Several of the questions
above — `9.1` and `9.2` in particular — are capture problems, not analysis problems.
