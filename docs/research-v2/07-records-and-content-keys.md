# 7. The records, and the content keys inside them

**Read this before diagnosing a photo game that shows no pictures.** The same class of
bug has been re-derived from scratch three times.

## 7.1 A photo game needs two different things from the dongle

They fail in completely different ways, and almost all the time lost on this has been
spent diagnosing one while the other was broken.

| | what it is | what the dongle supplies | how it fails |
|---|---|---|---|
| **A. Level database** | a dBASE III `.DAB` listing which pictures to show | **one dword read from the licence record at a fixed offset** | game runs, UI draws, **picture panels are black**; no picture-key query is ever sent |
| **B. Per-picture key** | the LCG seed that decrypts each PCX header | a **per-name query** answered by the dongle | game reports `not a PCX-File`, or stalls on an unknown character |

**The decisive tell costs nothing: does the log show a picture-key query at all?**

- **No query** → mechanism A. The game never got a valid filename to ask about, because
  its level database decrypted to noise. Do not touch the key tables.
- **Queries, wrong answers** → mechanism B.

Zero matters too: **key 0 means "read plaintext"**, so a game whose database is not
encrypted works with no dongle value at all. A *wrong non-zero* key is worse than none,
because it takes the decrypting path.

## 7.2 The fixed-offset rule

**The games do not walk a struct.** Each reads a single absolute offset into the record
it was handed, hardcoded at compile time:

| game | offset | dword | value |
|---|---|---|---|
| `FINDIT` | `+0x1C` | `v[3]` | `0001D760` |
| `MOSAIC` | `+0x20` | `v[4]` | `00029B92` |
| `FMEMO`  | `+0x24` | `v[5]` | `0001287E` |

Those are `v[3]`, `v[4]` and `v[5]` of the record the hardware serves:

```c
char     banner[16];   /* NUL-terminated */
uint32_t v[8];         /* little-endian, from offset 16 */
```

**So the dwords always start at offset 16, whatever the banner is.** That is the whole
rule, and it holds across generations because the offsets are baked into each
executable.

### The trap

`KEYN.COM` — the software crack — serves the same fields with a **30-byte** banner,
because it replaces the whole read-and-parse routine and emits the *parsed struct*, not
what the dongle holds. Copy KEYN's layout and every dword lands 14 bytes late.

Worse, a *conditional* fix is not a fix: giving a short banner the 16-byte field and a
long one KEYN's 30 works for a 1999 banner (`Version 99 (AT)`, 15 characters) and
silently breaks 2000 (`Version 2000 (DE)`, **17**). FINDIT then reads `+0x1C` across the
boundary, gets a wrong-but-non-zero key, takes the decrypting path and produces noise.

**A banner that does not fit does not move the dwords.** Write the dwords at offset 16
and lay the banner over the start of the block. A 17-character banner clips the first two
bytes of `v[0]` — the per-unit word, uninitialised host memory on a real dongle, which no
game reads. That collision is free; the alternative costs FINDIT its level database.

Log the record at attach time and the failure is visible before a game is ever launched:

```
banner "Version 2000 (DE)" (17 chars), dwords at +10; FINDIT reads +1C = 0001D760
```

If that value is not the game's documented dword, stop — nothing downstream will work.

## 7.3 The record, by generation

### 1999 — 48 bytes, `banner[16]` + eight LE dwords

Exactly as `7.2`. Content keys: `v[1]..v[6]` are `0000038B, 000181CD, 0001D760,
00029B92, 0001287E, 0000089D`, byte-identical on every unit dumped *and across
generations* — funworld's fixed per-title content keys, not per-site values.

### 2000 — the same 48-byte shape

The record must begin with the NUL-terminated `MAIN.SET["Version"]` string
(e.g. `Version 2000 (DE)`), which satisfies both the `strstr(record, "Version 2000")`
and the exact `strcmp` the menu does. Nothing else in the 48 bytes is examined by those
checks.

### 2001 to 2007 and Italy — 112 bytes, three shapes

All copied verbatim from the nine h5dmp dumps, not inferred. Words 8..63 of the
Microwire part; see `05.2` for the scramble and byte order.

**2001** — a 30-column banner, **right-aligned**, then decimal columns:

```
13 spaces then "Version 2001 (ES)"
then 000907 098765 120672 170898 075902 002205 160678 " -35733698"
     at byte 30, 36, 42, 48, 54, 60, 66, 72 (11 columns for the last)
```

Three things about this were wrong before the dumps and are worth naming: the banner is
right-aligned not left, the numbers are zero-padded not space-padded, and the last field
is **signed** — the hardware says `" -35733698"`, and a DOS `atol` of the unsigned form
(`4259233598`) overflows.

The parser (`0x1F626`) copies a byte range, skips leading spaces, and `atol`s it, which
is what the right-alignment is for. Two consequences:

- **The padding must be spaces, never NUL.** The unpack loop at `0x1F4D2` stops at the
  first zero *word*, so a NUL-padded banner ends the record at byte 18 and every field
  after it reads as garbage — which on screen looks exactly like a wrong content key.
- The banner itself must be NUL-terminated (`strlen` stops there), and everything after
  it must be spaces, through byte 83.

**I.G.O. 2 and 3** — the territory written over the first three characters of a 2000-era
banner:

```
"PT" 00 "sion 2000 (SP)" 00
```

**I.G.O. 5, 6 and 7** (and Italy):

```
"PT" '-' "Version" "2005B" 00 ')' 00
```

Both I.G.O. shapes are read by `MENU.EXE 0x3B300` / `FINDIT.EXE 0x23C0B`, which take the
territory from bytes 0..1, the release word from 3..9 and the version token from 10..14
and format `"%s %s (%c%c)"`. So writing the banner in as plain text cannot match — byte 0
would be `V`, and the territory comes from bytes 0 and 1.

For these shapes the content keys are **binary**: eight little-endian dwords at byte 30,
the same slots 2001 fills from its columns. `v0..v5` are identical on every dongle
dumped; `v6` is `160678` up to 2003 and **zero** from 2005 on; `v7` is per-unit and no
game is known to read it.

I.G.O. Italy is not a special case in the record — `MENU.EXE 0x3C322` is the same filler
every I.G.O. build uses. What is special is only the needle it then looks for,
`Version 08IT`.

### 2008 — 100 bytes, and no content keys at all

See `06.5`. The reader supplies the banner; the six content keys are compile-time
constants inside each executable.

## 7.4 The picture ciphers, by generation

| generation | how a PCX is protected | key comes from |
|---|---|---|
| 1999 | first **128** bytes of each PCX under a Borland LCG; body and 769-byte palette plaintext | a **type 1** dongle query on the filename (`02.3`) |
| 2000 | same LCG header layer | the **picture-key query** on the filename (`04.4`) |
| 2001 | `[128-byte header under an XOR keystream][body under the block cipher]` | the header layer is *not* the dongle's; the body needs the keyed round (`05.4`) |
| I.G.O. 2, 3 | body under the block cipher | the keyed round |
| I.G.O. 4, 5, 6, 7 | none — plain `GIF87a` | — |
| 2008 | content databases, keys compiled in | — |

The LCG is the one the whole product leans on:

```c
s = s * 0x08088405 + 1;   keystream byte = s >> 24;
```

**Archives packed without a dongle are keyed with the vendor default `0x00012345`.**
FMEMO's `GRAFIX` archive, all of FINDIT's 1999 pictures, and 2001's QUIZPRO2 are like
that — so finding one archive that needs no dongle proves nothing about the others.

## 7.5 Recovering ground truth for a generation you do not have

Both mechanisms are checkable **offline, with no emulator and no dongle**, because both
plaintexts are known.

- **PCX.** A header begins `0A 05 01 08` and then, *usually*, `00 00 00 00` for the
  origin. Eight known bytes pin the LCG seed exactly. Beware: some pictures have a
  non-zero origin — 320 of MOSAIC's 669 — and fail that assumption. Run the narrow
  cracker first and a widened one (four-byte signature only, scoring the rest of the
  header for plausibility) on its failures; on four known bytes alone the widened one
  goes ambiguous where the narrow one is exact. Those 320 turned out to be the only
  samples covering several table entries.
- **dBASE III.** A `.DAB` begins `03`, then a date and a record count.
- **A block cipher key** needs only nine (plaintext, ciphertext) block pairs — see
  `05.4`. I.G.O. 4 ships in the clear the same `FOTOPLAY.WAD` that 2 and 3 encrypt:
  1397 of 1397 entries agree on name, offset and size, so the three are byte-aligned.

Always sanity-check the method against an archive packed **without** a dongle: it must
recover `0x00012345` exactly.

## 7.6 Checklist

1. Does the game show a picture-key query in the log?
   - **No** → mechanism A. Check the attach line's `+1C` value against `7.2`. Fix the
     record layout, not the key tables.
   - **Yes** → mechanism B. Compare a served key against a cracked one.
2. Is the archive dongle-keyed at all? Crack it; `0x00012345` means it never needed one.
3. Is the banner longer than sixteen characters? Confirm the dwords did **not** move.
4. Only then look at the per-name function.

And the thing that made all three of these bugs survive a clean log: **a completed
handshake is not a passing check.** The offline cracker is self-verifying — a wrong key
cannot produce a valid PCX header — but the other half of the verification is somebody
playing the game.
