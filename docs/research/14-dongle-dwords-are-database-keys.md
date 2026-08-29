# Phase 14 — The dongle dwords are database decryption keys

**Hypothesis under test:** the eight little-endian dwords in the dongle block are a
decryption key for the games that use pictures/photos.

**Verdict: CONFIRMED**, and sharper than the hypothesis. Not the eight collectively —
**each photo game reads one specific dword** out of the dongle buffer and uses it as the
LCG seed that decrypts its picture database. Proven by decrypting real shipped data.

**And it un-parks a bug.** `Docs/07` recorded the 1999 photo-game stall as
"PARKED — and it is not the dongle." That conclusion is now falsified: both `KEYN.COM`
and PeepeeBox lay the dwords out **14 bytes too late**, so every photo game gets a wrong,
non-zero key and its database decrypts to garbage. See §5.

---

## 1. The mechanism

Every 1999 game links the same little database library. Each database object carries a
32-bit key at `+0x10`, and every record read is guarded by it — `FINDIT.EXE`, file
`0x1675A`:

```asm
0001675A  66837C1000    cmp dword [si+0x10],0x0
0001675F  751C          jnz 0x677d
                        ; --- key == 0: plain read
00016765  FF740C        push word [si+0xc]          ; length
00016768  FFB45A04      push word [si+0x45a]        ; buffer seg
0001676C  FFB45804      push word [si+0x458]        ; buffer off
00016770  FF740E        push word [si+0xe]          ; handle
00016773  9AFA100000    call 0:10fa                 ; _dos_read
                        ; --- key != 0: decrypting read
0001677D  66FF7410      push dword [si+0x10]        ; <-- THE KEY
00016781  8D46FE        lea ax,[bp-0x2]
...
00016793  9A57087018    call 1870:0857
```

and `1870:0857` (file `0x1BB57`) is exactly what its name would be:

```asm
0001BB6B  9AFA100000    call 0:10fa                 ; _dos_read(handle, buf, len, &nread)
0001BB73  66FF7610      push dword [bp+0x10]        ; the key
0001BB80  E86901        call 0xbcec                 ; <-- the cipher at file 0x1BCEC
```

`0x1BCEC` is **the same cipher `Docs/08` identified** for `\FOTO\SETTINGS\*.SET`:
`dst[i] ^= rand(0x100)` over the Borland LCG `seed = seed*0x08088405 + 1`, with the global
seed set from the key on entry and restored on exit — so **every read restarts the
keystream from the seed**. The only difference is where the seed comes from: the `.SET`
loader passes the hardcoded `0x00016295`; the database loader passes a dongle dword.

### Where the key is loaded

`FINDIT.EXE` file `0x8717`, three consecutive calls:

```asm
00008717  66FF36F051    push dword [0x51f0]        ; <-- read straight out of the dongle buffer
0000871C  68FE2C        push word 0x2cfe           ; database object
0000871F  680701        push word 0x107            ; "data\level1.dab"
00008722  9A19009413    call 1394:0019
          ... x3, for level1 / level2 / level3
```

and the callee stores it into the object:

```asm
00016573  668B460A      mov eax,[bp+0xa]
00016577  66894410      mov [si+0x10],eax          ; object.key = the dongle dword
```

`DS:0x51D4` is `check_dongle`'s 62-byte destination buffer (`push word 0x51d4` at
`0x1D650`, then `strstr(buf, "Version 99")`). So `0x51F0` is **buffer + 0x1C**.

Under the real 1999 record layout from `Docs/12` — `char banner[16]; uint32 v[8]` —
buffer + 0x1C is **`v[3]`**.

---

## 2. Proof: real shipped data decrypts

`/FINDIT/DATA/LEVEL1.DAB` is a **dBASE III file**: `03` signature, date 1998-09-15,
426 records, 737-byte header, 485-byte records — and `737 + 426*485 + 1 = 207348`, the
exact file size. The header and field descriptors (`BILDNAME` type `C` len 64, `LEVEL`
type `N`) are **plaintext**; only the records are encrypted, which is precisely what the
code does (the header reads in the open routine are unconditional plain reads).

Decrypting record 1 with each candidate:

| key | value | printable |
|---|---|---|
| `v[1]` | `0000038B` | 37.5 % |
| `v[2]` | `000181CD` | 35.5 % |
| **`v[3]`** | **`0001D760`** | **100.0 %** |
| `v[4]` | `00029B92` | 38.4 % |
| `v[5]` | `0001287E` | 38.1 % |
| `v[6]` | `0000089D` | 33.0 % |
| `.SET` key | `00016295` | 39.4 % |

```
v[3] -> b' 385000                                     '     (BILDNAME field, space-padded)
```

Verified across `LEVEL1/2/3.DAB` and records 0, 1 and 200 — 100 % printable every time,
each record a 6-digit picture id.

---

## 3. Each game gets its own dword

The buffer offset differs per game. Predicted from each binary's `push dword [abs]` list
against its own `check_dongle` buffer, then **confirmed by decryption**:

| Game | buffer offset | dword | value | database | plaintext recovered |
|---|---|---|---|---|---|
| `FINDIT` | `+0x1C` | `v[3]` | `0001D760` | `/FINDIT/DATA/LEVEL*.DAB` | `250064`, `385000`, `551044` — picture ids |
| `MOSAIC` | `+0x20` | `v[4]` | `00029B92` | `/MOSAIC/DATEN/D/LEVEL1.DAB` | `Was siehst Du auf diesem Bild?` |
| `FMEMO` | `+0x24` | `v[5]` | `0001287E` | `/FMEMO/DATEN/D/LEVEL1.DAB` | `Was war außer dem Fallschirmspringer noch z…` |

Three games, three different dwords, each predicted before it was tested. That is what the
six constants are **for**: a per-title key schedule, shipped in the dongle, identical on
every unit of the generation.

This also finally explains why `Docs/10` found the six values byte-identical across four
different physical dongles *and* across generations — they are not per-site anything, they
are the vendor's fixed content keys.

`AMORE`'s `.DAB` files are encrypted but decrypt under **none** of the six, and its
`check_dongle` buffer offsets do not appear in its push list — it uses a different key
source. Unresolved; out of scope here.

---

## 4. Which games are affected

The `cmp dword [+0x10],0` guard is in **all 22** 1999 executables (it is library code), but
only the titles that ship an encrypted `.DAB` actually depend on a correct key:
`FINDIT`, `FMEMO`, `MOSAIC`, `FUNQUIZ`, `TRIVIA`, `AMORE`. `FMEMO` and `MOSAIC` are the two
that carry the `Database %s may be corrupted!` string.

The zero case matters: **key 0 means "read plaintext"**, so an unencrypted database still
works with no dongle value at all.

---

## 5. This is the parked photo-game stall

`KEYN.COM` serves `char banner[30]; uint32 v[8]` (62 bytes). The 1999 hardware serves
`char banner[16]; uint32 v[8]` (48 bytes). Same struct, different banner width
(`Docs/12` §3) — so **the dwords sit 14 bytes later in KEYN's block than in a real one.**

PeepeeBox inherits the same layout; its own source says so:

```c
/* Banner NUL-padded to 30 bytes, then the eight dwords, exactly as KEYN.COM serves them. */
```

What each game therefore reads:

| Game | offset | real dongle | KEYN / PeepeeBox | |
|---|---|---|---|---|
| `FINDIT` | `+0x1C` | `0001D760` | `038B0000` | wrong, non-zero |
| `MOSAIC` | `+0x20` | `00029B92` | `81CD0000` | wrong, non-zero |
| `FMEMO` | `+0x24` | `0001287E` | `D7600001` | wrong, non-zero |

Non-zero, so the guard takes the **decrypting** path; wrong, so every record comes out as
noise. `FMEMO` then prints `Database %s may be corrupted!` with `%s` taken from a
mis-decrypted field — which is exactly the reported symptom, "a garbage `%s` pointing into
the Borland copyright string" (`Docs/07`).

It also explains why every hypothesis in that document's rule-out table failed to catch it:

- *"the KEYN-patched original fails identically"* — of course; KEYN is one of the two
  sources of the bad layout, and PeepeeBox copied it.
- *"all picture archives read back at full size"* — sizes were checked, not decryptability.
- *"zero idle polls, every transaction fully drained"* — the transport was fine. The
  **payload offset** was wrong, which no amount of transport tracing would reveal.

### The fix

Serve the 1999 block the way the hardware does: **16-byte banner field, then the eight
dwords** — i.e. the real 48-byte record recovered in `Docs/12`. Every 1999 banner is
`"Version 99 (XX)"` = 15 chars + NUL = exactly 16, so the field is unambiguous for this
generation.

- **PeepeeBox**: replace the 30-byte pad with 16 for the 1999 device, or simply load the
  `r3`/`r3_alt`/`r2` EEPROM record verbatim.
- **`ppkeyless.py --mode keyn`**: `build_keyn()` cannot be fixed by banner width alone,
  because the TSR is also used for 2001+ images where the banner needs 30. It needs a
  per-generation layout.

**Not yet run in the emulator.** The decryption proof is offline and complete, but the
end-to-end "photo game now loads its pictures" test has not been done. That is the next
step and it is cheap.

---

## 6. What this does not settle

- **`v[0]` and `v[7]`** are still unexplained, and still vary per unit. No game found so
  far reads `+0x10` or `+0x2C`.
- **`v[2]` (`000181CD`) and `v[6]` (`0000089D`)** have no consumer yet. `v[1]` (`0000038B`)
  is likewise unclaimed. Six constants, three consumers found — the remaining three
  presumably key `FUNQUIZ`, `TRIVIA` and one more, unverified.
- **`AMORE`** uses something else entirely.
- **2000+ generations** are a different dongle family (`Docs/13`) and were not examined.
  Whether they use the same trick is unknown.
- The type-1 command (`Docs/12` §8) is still not called by any 1999 binary, so the
  name→dword query is *not* how these keys are fetched. They come straight out of the
  type-3 block.
