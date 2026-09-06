# Phase 30 — the oracle wire, corrected against I.G.O. 2's own binary

Written 2026-09-06, after `DONGCAP` came back empty on the cabinet **while that same
cabinet renders its photos perfectly**. The part is alive; our frame is wrong. These are
the discrepancies found by reading `FINDIT.EXE` out of the PT cabinet's own disk
(`F:\HDDImages\IGO2\IGO 2 PT Real Machine\PPIGO2PT.img`, `/EXE/FINDIT.EXE`, 235,090 bytes)
rather than working from the earlier transcription.

All offsets are file offsets in that binary.

## 1. What was already right

Three things checked out exactly, and should not be re-litigated:

| routine | what it does | matches |
|---|---|---|
| `0x32db2` | the cooked writer: `flag ? (b \| 0x81) : ((b & 0xFE) \| 0x80)` | our `raw()` byte for byte |
| `0x32cc9` / `0x32ce9` | writes DATA at `base`, reads STATUS at `base + 1` | yes |
| `0x32f59` | query payload `((q<<1)&0x0E) \| ((q<<2)&0x60)`, bit 4 cleared, then written three times with bit 4 pulsed | yes |

So the query framing and the byte formation were never the problem.

## 2. The preamble is `0x48`, not `0x84`

`0x32fc2` is the round preamble. Its tail, for mode `1`:

```
    3300c   mov dl,0x48
    33016   call 0x32db2          ; the cooked writer
```

**Every tool in this repo writes `0x84`.** `tools/dongcap/dongcap.c`, `dongcap_linux.c`
and `mkdongcap_dos.py` all carry it, and so does the wire summary in
`tools/dongcap/README.md`. It is a transposition, carried since the wire was first
recovered, and it has never been tested against hardware.

A caution against fixing it blindly: `0x48` is what **mode 1** writes. Mode 2
(`0x3301b`) writes `0x04` then `0x24`, and which mode the picture oracle runs under is
**not** established. `tools/dongcap/dongprobe2.c` sweeps `48 / 84 / C8 / 04 / 24` for
exactly this reason. Settle it on a part that answers before changing the capture tools.

## 3. The seed is computed, not a constant

`0x32fc2` does not use a fixed seed byte. It calls `0x32f36`, which is

```
    al = table_2f94[ state[0x18] ]
    dl = al ^ 0x7E
    dl = dl & (dl + 0xFE)
    return dl ^ [DS:0x2FB1]
```

and passes the result to `cmdbyte`. So the "seed byte that never resolved from the
binaries" is a **function of `state[0x18]`** through a table at `DS:0x2F94`. Sweeping all
256 values is still a legitimate stand-in — the right byte is somewhere in the sweep — but
the framing in `dongcap.c`'s header comment is wrong, and the table is the honest way in.

## 4. There is an init we never call

`0x32fc2` opens with

```
    32fcc   mov byte es:[bx+0x17],0x0F
    32fd6   call 0x32e2f
```

`0x32e2f` writes a bare `0x80` to DATA through the **raw** writer (`0x32cc9`, no bit
munging), then branches on state into `0x3306b` and `0x33102`. Parts of the detection
around it (`0x32d0f`, `0x32d14`) are stubs in this build — an empty function and
`mov al,0 / retf` — so the chain is shorter than it looks, but it is not nothing, and no
capture tool performs any of it.

## 5. The answer line has two variants

`0x32d17`, the bit reader:

```
    test word es:[bx+0x4c],0x4
      clear -> shift 5, take the bit as-is
      set   -> shift 7, take the bit INVERTED   (neg/sbb/inc is a logical NOT)
```

**STATUS bit 5 straight, or STATUS bit 7 (BUSY) inverted**, chosen at runtime. Every tool
here implements only the first. It did not matter in any measurement so far — with STATUS
frozen, bit 5 reads 1 and bit 7 inverted also reads 1, giving the same answer — but it
matters the moment a line moves.

## 6. What the hardware said, and why none of the above is confirmed

On the Linux host (Debian 13, `parport0` PC-style at `0x378 (0x778)`, irq 7,
`PCSPP,TRISTATE,EPP`) with the dongle fitted:

- the DATA latch reads back every pattern written — `00/55/AA/FF/0F/F0` all correct, so
  writes reach the port register;
- the ECR at `0x77A` read `0x35` (PS/2 mode) and accepts being forced to SPP;
- **STATUS never changes.** `0x78` for all 256 DATA values, under all 32 CONTROL values,
  in both modes, and across 20 preamble variants x 256 seeds in `dongprobe2`.

On the cabinet the same sweep gave a constant `0x7F` — a different idle value, because it
is a different port chipset, but equally frozen.

A frozen status register cannot distinguish a wrong frame from a disconnected part, so
**nothing in sections 2–5 has been confirmed against hardware.** They are corrections to
the reading of the binary, and they stay unproven until some frame makes a line move.

`BDC587AC` is what the keyed round returns when the oracle answers 1 every time; it is the
signature of a dead line, not of a wrong key. Any future run reporting it should be read
that way.

## 7. The QEMU passthrough rig, and what it cost to get booting

Since no hand-written frame could be made to work, the approach changed: run the cabinet's
own image under QEMU on the Linux host with `-parallel /dev/parport0`, so the game itself
drives the real dongle, and log the ppdev traffic. Four things had to be solved first, none
of them obvious, all recorded here so nobody pays for them twice.

**Geometry.** `-drive ...,cyls=` is rejected by QEMU 10; geometry belongs on `-device
ide-hd`. With the image's own BPB figures (63 sectors, 16 heads, 3208 cylinders) and
`bios-chs-trans=none`, PTS-DOS boots as far as its funworld splash and then says
*Bad or missing command interpreter*. The reason is in `CONFIG.PTS`, whose shell line points
at `PTSDOS\COMMAND.COM` — and that file sits at cluster 30681, about 1 GB in, which with 16
heads is **cylinder 1948**. INT 13h CHS stops at 1023. The kernel loads fine only because it
lives at cylinder 18.

That is also what the MBR's end-CHS of head 143 has been saying all along: the original BIOS
presented **144 heads** so the whole disk fell inside 1024 cylinders. QEMU's `ide-hd` refuses
more than 16, so SeaBIOS has to translate — `bios-chs-trans=large` doubles heads until it
fits: 3208/16 to 1604/32 to **802/64**.

The catch: the boot sector computes CHS from the BPB, so with the BIOS on 64 heads and the
BPB on 16 they disagree and the VBR loads garbage — which is why plain `large` and `lba` died
*earlier* than `none` did. The BPB's head count has to be patched to 64 to match
(`qemu-igo2.sh fixbpb`, at offset 32256 + 0x1A).

**A floppy is not optional.** `AUTOPTS.BAT` calls `UPDATE\CHECK.BAT`, which opens with
`checkupd a:`. With no drive at all DOS sits in a floppy timeout behind the logo and never
reaches `MENU\MAIN.COM`. The cabinet has a 3.5 inch drive as A: (`docs/hardware.md`), so an
empty formatted 1.44 MB image as `-fda` is the faithful fix.

**Do not trace with strace.** `strace -e trace=ioctl -p <qemu>` stops the process on every
ioctl including the display's — thousands a second on a GTK window — and on the Atom 330
host (no VT-x, so TCG only) that alone turned a slow boot into a stalled one.
`tools/dongcap/pptrace.c` interposes `ioctl()` instead and logs only ppdev calls, one line
per port access with its value.

**Touch and sound init can go.** `tch_init.bat` and `snd_init.bat` are commented out in the
working copy of the image (original kept as `AUTOPTS.ORG`); ELODEV errors out anyway on a
machine with no MicroTouch controller, exactly as it does on real hardware.

## 8. The wire, captured at last — and it is not what the tools implement

2026-09-06. PeepeeBox was built on the dongle host with an LPT passthrough
(`PEEPEEBOX_LPT_PASSTHRU=378`), the cabinet's own image was booted under it, and Marcos
played FIND IT and AMORE with the real dongle answering. `pp_raw` recorded every access
with the guest's call chain.

**448,801 port accesses**, of which **351,199 after the game started**, including **8,678
status reads with bit 5 moving** — 5,419 high against 4,651 low. The part answers, and it
discriminates. Trace kept at `docs/research/evidence/igo2-dongle-wire-2026-09-06.log.gz`.

That alone settles section 6: every silent probe in this document was a wrong frame, not a
dead part.

### 8.1 The repeat count is real, and it is 4

Raw writes outnumber answers about 17 to 1 because the transport writes each byte several
times (`notes/HANDOFF2001.md` § 10.4, `0x3664D`). In this capture the run lengths are
**4** (33,160 runs) and **8** (1,777) — not the 32 § 4 once guessed. Collapsing those runs
is what makes the protocol legible at all; 155,538 framing accesses become 43,617 logical
ones.

### 8.2 Two phases, and no 0x80  -- SUPERSEDED, see section 9.1

The picture path, at guest sites `3372:1C1E` then `3372:07E8` / `3372:394D`:

```
phase 1   58 59 58   1A 1B 1A   7A 7B 7A   54 55 54   08 09 08   68 69 68 ...
          triples of X, X|1, X -- bit 0 pulsed, a byte per triple
phase 2   W 00 -> R 58   W 02 -> R 78   W 04 -> R 58   W 06 -> R 78 ...
          a counter stepping by two, one status read after each
```

`58` and `78` differ in bit `0x20`, so **DO is STATUS bit 5**, as § 5 said.

But **none of these writes has bit 7 set.** Every capture tool in this repo emits the
cooked form `(b & 0xFE) | 0x80` from `0x32db2`, which forces bit 7 high on every byte.
The picture path does not go through that writer at all. That is why `dongprobe2`'s twenty
frame variants — all of which carried bit 7 — could not make a single line move.

The boot-time traffic *does* show the cooked form (`C6`/`C7`, bit 7 set), so both writers
are in use for different services. The tools implement the wrong one for this job.

### 8.3 Where this leaves sections 2-5

- § 2's `0x48` vs `0x84`: neither. The two most-written bytes in the game's traffic are
  `84` (16,336) and `A4` (8,536), whose pre-cooked originals are **`0x04` and `0x24`** —
  the **mode 2** tail at `0x3301b`, not mode 1's `0x48`. The caution in § 2 against
  changing the tools blindly was right.
- § 5's two answer-line variants: bit 5 straight is the one in use here.
- § 3's computed seed and § 4's uncalled init remain unproven, but are now testable
  against a real trace rather than by inference.

### 8.4 What has not been done

The capture exists; the cryptanalysis does not. Phase 2's stepped counter and the phase 1
byte sequence have not been mapped onto `HaspDecodeBlock`'s inputs, and no `f` value has
been checked against phase 26's `f(504EF2AE) = 32FC6611`. That is the next piece of work,
and for the first time it can be done against recorded hardware behaviour instead of a
model.

## 9. The round is confirmed, the preamble is recovered, and § 8.2 was wrong

Continuing from § 8, on the same capture.

### 9.1 Correction: the query framing was right all along

§ 8.2 said "none of these writes has bit 7 set" and concluded the tools used the wrong
writer. **That is wrong.** It was read off phase 1 (guest site `3372:1C1E`), which is a
different phase of the transaction. The keyed round's queries live at
`3372:3457 / 346B / 347C` with the answer read at `3372:2CD5`, and they look like this:

```
   3457  AC      payload
   346B  BC      payload | 0x10   -- the clock
   347C  AC      payload
   2CD5  r58     answer, bit 5
```

Every payload has bit 7 set. The framing is exactly `0x32f59`'s and exactly what
`dongcap` emits. § 8.2's diagnosis should be disregarded; the rest of § 8 stands.

### 9.2 The keyed round reproduces phase 26 on hardware

4,720 query groups, in **118 runs of exactly 40** -- 118 keyed rounds, 59 buffers.
Replaying the documented walk over the captured answers, and checking at every step that
the payload the model would emit equals the payload actually on the wire:

```
run 0:  f(504EF2AE) = 32FC6611   expected 32FC6611   MATCH
run 1:  f(012C6137) = DF57708B   expected DF57708B   MATCH
```

Both of phase 26's offline-solved values, reproduced by the part itself, with all 40
payloads matching in each run. So the LFSR walk, the index selection
`(prev & 1) | ((v & 1) << 1)`, the polynomial `0x80500062` and the byte selection
`(v >> (8*idx)) & 0xFF` are **confirmed against hardware**. The round was never the
problem.

### 9.3 The preamble, which is what was missing

Identical before all 118 rounds:

```
   B4 B5 B4          cmdbyte(0x34)     bit 0 clocked
   FC FD FC          cmdbyte(0x7C)
   CE CF CE          cmdbyte(0x4E)
   (84 A4 84) x16    sixteen SK pulses -- bit 5 clocked, DI (bit 6) low
   CE CF CE          cmdbyte(0x4E)
   [40 queries]
```

Cooked bytes decode by `(b & 0xFE) | 0x80`, so `B4 -> 0x34`, `FC -> 0x7C`, `CE -> 0x4E`,
`84 -> 0x04`. The `x16` pulses clock on **bit 5**, which § 16.1 identifies as SK, not on
bit 0 like a command byte -- two different clocks in one preamble.

Against this, `dongcap` opens with `cmdbyte(seed), cmdbyte(0x4E), raw(0x84)`. It is missing
two of the three command bytes and all sixteen clock pulses, and it sweeps a seed byte that
does not exist: the first command is a constant `0x34`. **That is why 256 seeds x 20 frame
variants never moved a line** -- the part was never taken through its opening sequence, so
it never reached the state where a query means anything.

The sixteen pulses are almost certainly the part clocking out something the library
discards, or a fixed setup interval; nothing here proves which, and the count is constant
across every round in the capture.

### 9.4 What this makes possible

`dongcap` needs its `preamble()` replaced by the sequence above and its seed sweep deleted.
With that, a capture run against the real part should reproduce `32FC6611` at calibration
and then simply work -- which is the whole of what `capture-igo2/` was built for, and it
would give I.G.O. 2 its photos without the cipher being broken at all.

Not done here, and worth saying plainly: the tools have **not** been changed to match, and
nothing in § 9.3 has been re-tested by driving the part from a tool rather than watching the
game do it.

Related: `docs/research/20` § 3, `notes/HANDOFF2001.md` §§ 16, 20, 23-24,
`tools/dongcap/`.
