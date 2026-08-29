# Docs/11-pcx-cipher.md — Phase 11: the PCX header cipher

**Boot test 4:** CONCENT advanced from `DONGLE CODE ERROR` to **`not a PCX-File: IMG039.PCX`**
(P10 worked). FMEMO and FINDIT unchanged.

**User's question:** later versions use *a variable offset per PCX encrypted* — does this one?
**Answer: NO. In the 1999 release the encrypted region is a fixed 128 bytes.**

---

## 1. Only the PCX *header* is encrypted — validated

For every PCX entry, decoding the RLE body from offset **128** to `size-769` consumes the
input **exactly** and yields exactly `width x height` pixels, and the byte at `size-769` is a
plaintext **`0x0C`** PCX palette marker.

| WAD | PCX entries | encrypted-header length histogram |
|---|---|---|
| `CONCENT/100X135/0` | 67 | `{128: 67}` |
| `FMEMO/PICS` | 472 | `{128: 472}` |
| `FINDIT` | 4 | `{128: 4}` |

**543/543 entries: exactly 128 bytes.** Body and 769-byte palette are **plaintext**.
Entropy confirms it: first 128 bytes **6.57**, bytes 128–2048 **4.23**, palette 7.73.

**No variable offset in this release** — that scheme is a later addition. Validated as asked.

## 2. The key is per-entry, and it is dongle-derived

Header keystreams are distinct per entry (**425 distinct across 472**), with collisions in
28 groups — e.g. twelve entries all beginning `1250` share one keystream. So the key is
derived from something name-like, with a weak/colliding hash.

The decisive structural evidence is in `CONCENT.EXE` `0x17D7D`:

```
push 0x80 ; push 0x4584 ; push [bp+6]
call 0x0:0x15A0            ; plain DOS read (int 21h AH=3Fh) - 128 bytes
call 0x7CA4                ; <- get_dongle_code()  -> DWORD into [bp-0x10]
cmp  word [bp-0xC],0x80    ; read 128?
cmp  byte [0x4584],0x0A    ; PCX magic     <- FAILS -> "not a PCX-File"
cmp  byte [0x4586],1       ; encoding
cmp  byte [0x4587],8       ; bpp
cmp  byte [0x45C5],1       ; nplanes
```

`get_dongle_code()` is fetched **immediately between the header read and the header
validation**, in the same function. With P10 supplying the manufacturer default `0x12345`,
validation fails.

**Conclusion — this revises `Docs/09-cipher.md`:** the 1999 release **does** put the dongle
into the photo path. Not into the archive *directory* (that is genuinely hardcoded XOR
`0x55`, proven by decoding 472/472 entries), but into the **per-PCX header cipher**. So the
scheme the user knows from later releases has its ancestor here; the later change was making
the encrypted *extent* variable rather than a fixed 128 bytes.

**Not yet located:** where the DWORD is actually applied. `0x0:0x15A0` is a plain
`int 21h/3Fh` read with no decryption, so the transform happens elsewhere (a per-handle flag
table exists at `[bx+0x2B12]`, tested for bit 1 — an unexplored lead).

## 3. The fix does not need the dongle

Because **body and palette are plaintext and the header is fully reconstructible**:

- dimensions come from RLE-decoding the body (exact, verified 543/543);
- `bpp=8`, `planes=1`, `encoding=1`, `bytesPerLine` all follow;
- the remaining header fields are constants or zero.

So a **valid 128-byte PCX header can be synthesised offline for every entry** and written
into the WAD in plaintext, then the in-game header decryption disabled. That is a complete,
deterministic repair requiring no hardware and no key recovery.

Cost: rewriting the WADs (FMEMO 23.5 MB, FINDIT ~115 MB) plus one patch to stop the game
decrypting. Larger than a byte patch, but fully reproducible and verifiable.

**Alternative:** recover the real dongle DWORD. With 425 known keystreams and abundant known
plaintext, it is solvable *once the generator is located* — cheaper if § 2's loose end falls
quickly, riskier if not.

## 4. Status

- **Fixed:** DRM launch gate (P1/P7), `MENU.EXE` dispatch (P3), dongle-code fatal (P10).
- **Working titles:** AMORE, HEXAGON, SHANGHAI, ELEVENS — i.e. everything that never
  requests a dongle-keyed PCX.
- **Remaining:** `CONCENT.EXE`, `FMEMO.EXE`, `FINDIT.EXE` — all three blocked on this one
  mechanism, which is now understood in shape if not yet in detail.
