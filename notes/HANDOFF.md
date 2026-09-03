# Handoff — the Photo Play 2000 dongle

Working note for whoever picks this up next. Tracked only so the references to
it resolve; delete it once the open items below are done or have moved into
`docs/research/`.

Written 2026-08-30, after the session that made the 2000 generation boot and run
games. **Updated later the same day** by the session that chased the picture
decryption; that update is § 8 onward, and it supersedes § 4's open items 3 and 4.

---

## 1. Status (see 12 for the current state)

**Menu and most games: working.** The Photo Play 2000 protection token —
`CDONGLE` to the menu, `PDONGLE` to the games, one device either way — is
emulated. An untouched 2000 image boots to its menu and runs its games.

**Photo games: AMORE works, FINDIT does not.** AMORE decrypts and displays its
pictures, confirmed on screen. FINDIT still stalls just before loading pictures,
and § 9 says exactly why — it is the fitted-tables limitation coming due, not a
new fault.

The 1999 generation is unaffected throughout.

```
git log --oneline
0f80ddf  Stream the picture key as the name arrives, and the photo games work
b528d1f  Compute the right picture keys; delivering them is still wrong
7bfceef  Serve two bytes where the library asks for two, and reach the picture load
38e50fd  release: let a version tag name the release
e0fb514  Serve the record read, and the 2000 generation runs its games
b72fff9  Record the second read routine in the write-up too
5bf61da  Answer the licence query, and serve both of the library's read routines
5559243  Correct the record: games still fail, and this document says why
a0e1dbf  A game runs the same check, and the nonce is not random after all
4a28e81  Answer the 2000 generation's dongle, and boot its menu
d3eb8e1  Research: the 2000 generation's second dongle, decoded on the wire
```

`main` is pushed and level with `origin/main`. Working tree clean apart from
this file.

Read `docs/research/15-cdongle.md` for the transport. It contradicts itself
several times on purpose — that is the house style for these notes, and the
corrections are the most useful part.

---

## 2. The protection, as it actually works

The guest reaches the dongle through a linked-in library whose public entry is a
far call:

```c
int far dongle(unsigned func, unsigned port, void far *in, void far *out);
```

`func` dispatches at `0x062D`; `port` is `0x0B` for LPT1.

### The wire command byte

Every dispatcher arm loads its own `ah` and the transform at `0x07E8` is

```
    wire = nibble_swap(ah ^ 0xA3)
```

which gives the full map. Worth having in front of you:

| func | ah | wire | | func | ah | wire |
|---|---|---|---|---|---|---|
| 1..8 | computed at `0x06D6` | `func + 0x9F` → `A0`..`A7` | | `0x14` | `79` | `AD` |
| `0x10` | `29` | `A8` | | `0x15` | `49` | `AE` |
| `0x11` | `09` | `AA` | | `0x16` | `59` | `AF` |
| `0x12` | `19` | `AB` | | `0x17` | `A8` | `B0` |
| `0x13` | `69` | `AC` | | `0x1C` | `68` | `BC` |

### Reply lengths — the trap that cost this session

**Functions 1..8 always read exactly two bytes back.** `0x06D6` hands `0x07E8`
a fixed `bx = 0x0302` — three bytes out, two in. There is no per-call length.

Serving anything else for one of those is not a harmless wrong answer, it is a
framing fault: the guest reads its two bytes and stops, the device is left
parked in the middle of whatever else was queued, and **the next transaction is
served the leftovers**. One unanswered query corrupts every transaction after
it. That is what `7bfceef` fixed, and it is why AMORE stopped at
`DONGLE CODE ERROR` long before any picture was touched.

Functions `0x11`/`0x12` take their length from the call: the payload's first
two bytes are the count, and the reply is that many bytes.

### The queries a 2000 game makes

| wire | func | payload | answered? |
|---|---|---|---|
| `AC` | `0x13` | — | yes — port autodetect, value ignored |
| `A0` | 1 | `86 2E D0` | yes — the licence query, `0x4693` |
| `AD` | `0x14` | `A6 8C 0D 00 30 00 00 00` | yes — the 48-byte record |
| `A3` | 4 | `73 07 09` | **placeholder** |
| `A4` | 5 | `76 02 27` | **placeholder** |
| `A1` | 2 | `A6 4B D0` | **placeholder** |
| `AA` | `0x11` | `08 00 8B 03` + 8-char name | **unanswered** |
| `AB` | `0x12` | ditto, other constant | **unanswered** |

The challenges live in a table in the game's data segment (`DG+0x2403` in
AMORE), three bytes each, sent reversed:

```
  0x2403  D0 2E 86      -> A0 sends 86 2E D0      the licence query
  0x2406  D0 4B A6      -> A1 sends A6 4B D0
  0x2409  09 07 73      -> A3 sends 73 07 09
  0x240C  27 02 76      -> A4 sends 76 02 27
  0x240F  00 0D 8C A6   -> the record read's value
```

`A3` and `A4` are **not checked against anything**. Their two-byte replies are
cached at `DG+0x2413` and `DG+0x2417` and used later as 16-bit constants in the
picture-key derivation, so a wrong one yields a wrong key rather than a refusal.

---

## 3. The implementation

All in `src/device/dongle_photoplay.c`, in the section headed with the CDONGLE
comment block. The 1999 half above it is untouched.

- `cd_t` — the state machine, embedded in `pp_t` as `dev->cd`.
- `cd_assemble()` — recovers a byte from the five-write nibble frame
  (`F? C? F? 9? 8?`) with a sliding window.
- `cd_write_data()` — the parser. Classifies each decoded byte once its trailer
  arrives: `D0` means command (and the byte before it was the nonce), anything
  else means ordinary.
- `cd_prepare()` — builds the reply: `cd_answers[]` first, then the two-byte
  fallback for the `A0`..`A7` range, then the record.
- `cd_ack()` — what the device drives on ACK.
- `pp_read_status()` — early-returns the CDONGLE's ACK once `cd.active` is set.

`cd_answers[]` is a lookup, not an emulation, and the comment says so. Three of
its four entries are **placeholders** and are logged as such.

---

## 4. Open items from the first session

1. **The dongle's challenge→response function.** Still unknown in general. See
   § 8 — a lot more is now known about the picture-key half of it.
2. **Only the DE image has been booted.** `PP2000/` also holds IT ×2 and NL.
3. ~~Only one game has been played~~ — AMORE has now been driven too.
4. ~~`0xFCA`'s wind-down~~ — exercised now; no off-by-one appeared.
5. **The key derivation is untested for a non-zero nonce.** The nonce is the
   BIOS floppy motor counter at `0000:0440`, always `00` on a cabinet that never
   touches a floppy. A constant nonce is **correct** here.
6. **Bring-up logging is still on** and is now very verbose.

---

## 5. How to work on this

**The menu performs the same protection sequence at boot as the games do** —
the `AC` / `A0` / `AD` exchange appears ~90 seconds after launch with nothing to
click. Iterate against that; only the final verdict needs a game.

**The user drives the guest.** Scripted clicks open the game grid but never land
on a tile. Boot the rig, say it is up, let them play, then read the log **and ask
what the screen shows**.

**Do not screenshot the emulator window** — the capture grabs whatever is on top
of that screen region.

**A completed handshake is not a passing check.** The transport succeeding and
the guest accepting the answer look identical at the transaction level.

### Build

```bash
export PATH="/c/msys64/mingw64/bin:$PATH" && cd build && ninja
```

`cc.exe` cannot spawn `cc1` unless `C:\msys64\mingw64\bin` is on PATH — it fails
**silently, exit 1, no diagnostic**, which looks like a broken source file and is
not. A fresh configure needs `-DQt5_DIR=...` and `Qt5LinguistTools_DIR` given
explicitly.

### Rigs

Under `C:\Users\xeon4\Documents\Claude\PeepeeBox-builds`:

| | |
|---|---|
| `10-amore-pcx` | current rig, pristine DE image, the AMORE work |
| `09-cdongle-2000-working` | packaged build for `e0fb514`, own pristine DE disk |
| `07-cdongle` | scratch 2000 rig; its disk has been booted and written to |
| `08-cdongle-1999-regression` | 1999, hard-linked to the shared master |

**Never point a rig at `PP2000/` itself** — those are untouched ground truth.
`PEEPEEBOX_LPT_TRACE=1` gives the raw wire log.

---

## 6. Evidence

`C:\Users\xeon4\Documents\Claude\PeepeeBox-handoff\`

| | |
|---|---|
| `evidence/libfull.asm` `.bin` | the 2000 protection library, the source of truth |
| `evidence/trace2000-86box.log` | the original failing capture |
| `evidence/img.py` | the FAT reader/writer |
| `ex/` | all 29 game EXEs, `MENU.EXE`, `MAIN.SET` from the DE image |
| `evidence/amore-pcx/` | **the picture-key work — see § 8** |

Routines in `libfull.asm`:

| | |
|---|---|
| `0x05B2` | public API entry, `dongle(func, port, in, out)` |
| `0x062D` | the dispatcher |
| `0x06D6` | funcs 1..8 — sets `bx = 0x0302`, three out two in |
| `0x076B` | key setup: send the nonce, `key = nonce ^ 0xD3` |
| `0x0788` | the XOR, one fixed byte for the whole transaction |
| `0x07E8` | `wire = nibble_swap(ah ^ 0xA3)` |
| `0x081D` | funcs `0x11`/`0x12` — the picture-key call |
| `0x0938` | the record read (func `0x14`) |
| `0x0F6F` | read — claims with `CF`, signs off with `BF` |
| `0x0FCA` | its twin — claims with `8F`, signs off with `FF` |
| `0x1067` | the four-pair attention handshake |
| `0x114C` | receive a byte, MSB first |
| `0x11FA` | open a transaction: nonce, then command (`D0` trailer) |

---

## 7. Repository gotchas

- The fixed hardware profile is applied at the **end of `config_load()`**.
- Disk geometry is derived from `HardDisk.img`'s size.
- `MACHINE_IS(board)` expands to `0` on purpose.
- Verify tree-wide changes by **cloning fresh and building the clone**.
- The dongle's version/territory now come from the image's own `MAIN.SET`
  (`cec3345`, `3e24a45`), verbatim, so every release/territory pair is right.

---

## 8. The picture keys — where this session got to

### The symptom

AMORE reaches its picture load and reports `not a PCX-File`. Before `7bfceef`
it reported `DONGLE CODE ERROR` instead, which was the framing fault in § 2, not
a key problem.

### How the game derives a picture key

`AMORE.EXE` `1B4C:05A0` (file `0x1F460`):

1. if `[DG+0x2413] == 0`, query `A3`; if `[DG+0x2417] == 0`, query `A4`.
   Both cache a **two-byte** reply as a dword.
2. `fnsplit()` the path, `strupr()` the basename and the extension, then
   `strcat(" ")` until the basename is 8 characters.
3. hash = `name[0]^name[1]^name[2]^name[3] ^ ext[0] ^ ext[2]`, then `& 3`.
   **`fnsplit` keeps the dot**, so for `.PCX` that is `'.' ^ 'C'`, not `'P' ^ 'X'`.
   Getting this wrong permutes the four cases and wastes a lot of time.
4. a four-way switch (table at `cs:0x741` = file `0x1F601`):

| case | code | func | challenge | constant | fold |
|---|---|---|---|---|---|
| 0 | `0x1F511` | `0x11` (`AA`) | `0x2403` | `0x038B` hardcoded | XOR |
| 1 | `0x1F553` | `0x12` (`AB`) | `0x2406` | `0x0A8E` hardcoded | XOR, aliased |
| 2 | `0x1F58E` | `0x11` (`AA`) | `0x2403` | `A3`'s reply | ADD |
| 3 | `0x1F5A6` | `0x12` (`AB`) | `0x2406` | `A4`'s reply | ADD |

5. the call's payload is `{lenhi, lenlo, constlo, consthi, name[0..7]}` — the
   count is **8**, so the device's reply **overwrites all eight name bytes**.
6. the fold turns those eight into four (`buf[i] ^= buf[4+i]`, or `+=`; case 1
   permutes and aliases — read `0x1F56E` carefully, it reuses values it has just
   written).
7. the dword at `buf[0..3]` is the LCG seed. Non-zero status from the wrapper →
   the function returns `0xFFFFFFFF` → `DONGLE CODE ERROR`.

Captured live, first time this query has ever been seen:

```
command AA   08 00 | 8B 03 | "100     "
             count   const   uppercased 8-char basename
```

### The cipher, and the keys — all recovered

`docs/research/ng-11` had it right: only the **first 128 bytes** of each PCX are
encrypted, with the Borland LCG `s = s*0x08088405 + 1`, keystream `s >> 24`,
seeded with the key.

A PCX header starts `0A 05 01 08 00 00 00 00`, which is eight known plaintext
bytes — enough to pin the seed exactly. `evidence/amore-pcx/crack.c` brute-forces
the low 24 bits of the state after one step and confirms against the rest; it
runs in about 90 ms for the whole archive.

**Validated**: run against `AMORE/GRAFIX/FOTOPLAY.WAD`, which is packed without a
dongle, it recovers `0x00012345` — the known vendor default — exactly.

**All 332 keys in `AMORE/COMIX/FOTOPLAY.WAD` are recovered**, zero failures.
They are in `evidence/amore-pcx/keys.txt`, and the full name→key characterisation
by case is in `evidence/amore-pcx/key-tables.txt`.

### What the device must produce

Cases 0 and 1 have exact closed forms, fitted and verified against all 83 files
each (`ROL8` is an 8-bit rotate left):

```
case 0   kb[0] = 8F ^ ROL8(name[0], 1)
         kb[1] = F8 ^ ROL8(name[1], 5)
         kb[2] = F8 ^ ROL8(name[2], 5)
         kb[3] = 38 ^ name[3]

case 1   kb[0] = D7 ^ name[3]
         kb[1] = F7 ^ ROL8(name[2], 3)
         kb[2] = 2E ^ ROL8(name[2], 3)
         kb[3] = 8A ^ name[3]
```

(`kb` is the key, little-endian: `key = kb[0] | kb[1]<<8 | kb[2]<<16 | kb[3]<<24`.
Case 1 genuinely does not depend on `name[0]` or `name[1]` — that is the aliased
fold, not a mistake.)

Cases 2 and 3 are **not** closed-form under any rotate/add/xor model tried. Each
key byte *is* a pure function of the corresponding name byte, and those maps are
tabulated completely in `key-tables.txt` for every character that occurs (digits
and space). They show additive structure with carries, which is what you would
expect if the byte seen is a merge of two device nibbles — `0x081D` ends with a
backwards nibble-merge (`and ah,0xF0 / and al,0x0F / or al,ah`).

### The insight that makes cases 2 and 3 tractable

**The device does not need the real `A3`/`A4` values.** Whatever it returns for
those becomes the constant the game sends back in the `AA`/`AB` payload, so the
constant is simply a *tag* saying which case the game is in. Pick two distinct
values, recognise them on the way back in, and answer with whatever reply makes
the fold produce the key the data was actually encrypted with.

Constructing the reply is free: the fold has eight inputs and four outputs, so
set `r[4..7] = 0` and `r[0..3] = kb[0..3]` for the XOR and ADD folds. Case 1's
aliasing needs `r6 = r5 = 0`, `r3 = kb[0]`, `r2 = kb[1]`, `r4 = kb[1]^kb[2]`,
`r7 = kb[0]^kb[3]`.

### Implemented, and working

`b528d1f` computes the keys; `0f80ddf` makes them arrive. Verified two ways:
offline, all **332 of 332** keys reproduce through the game's own fold; and on
the rig, `835.PCX` asks and is given `98947AF2`, exactly the key cracked from its
ciphertext. AMORE displays its pictures.

**The request shape, which is what took the longest to see.** The name is *not*
part of the payload. `0x081D` sends four header bytes — the count, then the
16-bit constant — and then repeats, eight times over:

```
    push one name byte  ->  read one reply byte
```

with a ninth read through `0x0FCA` at the end. The device's own log shows this
plainly: one argument logged between each read. Waiting for a twelve-byte payload
means answering nothing for the first seven reads, and the guest quietly collects
whatever else was queued — it read `V`, the first byte of the banner record,
seven times.

That the reply must be produced a byte at a time, before the rest of the name
exists, turns out not to matter: reply byte *i* depends on `name[0..i]` and no
further, so it is always computable when it is asked for. `cd_prepare_picture()`
refreshes the queued bytes on every name byte **without touching `tx_bit`**, so
the guest's read position survives.

**Two wrong readings, recorded because both looked convincing.** A decoder that
counted the attention handshake's own status polls as data bits reported the
guest receiving a repeating five-byte pattern; decoded properly the very first
byte was already correct, and the "delivery is broken" conclusion drawn from it
was false. And the twelve bytes after the command were read as one payload when
four are header and eight are interleaved name bytes. If something here looks
broken, decode the wire **and** read the device's own argument log — the two
together would have caught both of these immediately.

### What is left

1. **The dongle's own arithmetic is still not recovered.** The closed form behind
   `a[]` is unknown, and so is the constant-to-transform rule that would give the
   real `A3` and `A4`. What is in the device is a characterisation that reproduces
   every shipped picture exactly; it is not the function the silicon runs. Anyone
   recovering that must match these tables.
2. **The 2001+ and 2008 generations are untouched.** Docs/09's NG dongle is a
   different token again, though Docs/15 found the 2008 probe is this same
   library's, byte for byte, so the transport work should carry.
3. **MOSAIC has 320 pictures with a non-zero PCX origin.** Handled -- `crack2.c`
   recovers them and they are covered -- but if a future archive will not crack,
   that is the first thing to check rather than writing the entries off.

Everything else is done and seen working. All four 2000 images (DE, IT x2, NL)
and every photo game have been played manually and display pictures.
