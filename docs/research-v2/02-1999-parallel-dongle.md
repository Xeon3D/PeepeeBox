# 2. The 1999 dongle

Everything here is **measured**: five physical units were dumped, both firmware images
disassembled and executed against a simulated I²C EEPROM, and the result agrees line for
line with the protocol recovered independently from the game binaries.

## 2.1 The silicon

A two-chip parallel-port device.

- **MCU** — an **AT89C2051**-class MCS-51: 2 KB flash, 128 B RAM, 20 pins, only
  P1.2–P1.7 and P3.0–P3.5/P3.7 referenced, no P0/P2, no external bus. Compiled C,
  Keil toolchain. Two distinct firmware images exist (call them A and B).
- **EEPROM** — a 24Cxx-family I²C part, driven with control byte `A0h` and a **single**
  address byte, so only `0x00`–`0xFF` is reachable. Physically probably a 24C08; the
  firmware never leaves the first block.

## 2.2 Pinout and wire

| dongle pin | role | PC side |
|---|---|---|
| `P3.2` / INT0 | latches each incoming nibble | STROBE (CTRL bit 0) |
| `P1.2`–`P1.5` | incoming nibble, bits 0–3 | DATA bits 0–3 |
| `P1.6` | host acknowledge, polled | DATA bit 4 |
| `P1.7 P3.3 P3.7 P3.4` | outgoing nibble, bits 0–3 | STATUS bits 3–6 |
| `P3.5` | dongle handshake | STATUS bit 7 (BUSY) — **inverted by the PC's port** |
| `P3.0` / `P3.1` | I²C SCL / SDA | — |

**Host → dongle.** Two nibbles per byte, **low first**, on DATA bits 0–3 (the guest
holds bits 5–7 high and bit 4 low), each latched on a **STROBE rising edge**.

**Dongle → host.** Two nibbles per byte, **low first**, on STATUS bits 3–6, with STATUS
bit 7 (BUSY) as the ready flag and DATA bit 4 as the host's acknowledge:

```
BUSY=1 -> host reads nibble -> host raises ack -> BUSY=0
       -> host drops ack    -> next nibble     -> BUSY=1 ...
```

**Any movement on CTRL bits 2 or 3 must reset the framing.** The dongle has no other way
to resynchronise its nibble phase, and the host's init routine toggles those bits (while
also raising STROBE, which would otherwise latch a bogus leading nibble and put every
following byte one nibble out of phase).

One 86Box-specific trap: the guest keeps CTRL bit 5 set, which 86Box treats as the
bidirectional direction bit and therefore suppresses `write_data`. A plain SPP port
treats it as a don't-care. Clear the extended-mode flag on the attached port
(`lpt_set_ext(port, 0)`) or the dongle never sees a single nibble.

## 2.3 Grammar

Every transaction opens with a type byte. Two four-entry tables give the lengths — they
are present both in the game's data segment (`DS:0x2283` send, `DS:0x2287` receive) and
in the dongle's own Keil init table, byte-identical:

```
send:  00 0A 32 02        recv:  00 04 00 30
```

| type | host sends | dongle returns | what it is |
|---|---|---|---|
| 0 | 0 | 0 | no-op |
| 1 | 10 | 4 | **per-picture key** — `{01, NAME[8], nonce}` |
| 2 | 50 | 0 | **programming** — `{02, nonce, 48 encrypted bytes}` |
| 3 | 2 | 48 | **licence read** — `{03, nonce}` |

A byte that is not a valid type when the framer expects one means the link is out of
sync: drop it rather than framing garbage.

### Type 3 — the licence read

The dongle returns the 48-byte record XORed under a keystream seeded with the host's
nonce:

```c
k = nonce;
for (i = 0; i < 48; i++) { out[i] = record[i] ^ k; k = next3(k); }

uint8_t next3(uint8_t k) {
    k += 0x75;
    if (k < 0x28) k = 0xCB;
    if (k > 0xC8) k = 0x13;
    return k;
}
```

It collapses to a `0x13`/`0x88` two-cycle after the first step, so the nonce really only
masks byte 0 and picks the phase. Weak, but it is what the code does — two nonces against
the same dongle produce ciphertexts differing only in byte 0.

Firmware A stores five copies of the record (at `0x00, 0x30, 0x60, 0x90, 0xC0`), reads
all five per byte, takes a majority vote and writes the winner back over a dissenter.
Firmware B keeps one copy. Neither behaviour is visible to the host.

### Type 1 — the per-picture key

The host sends `{01, NAME[8], nonce}`: the uppercased, space-padded 8-character basename
of a file it is about to open, then a random byte. The dongle answers four bytes derived
from the **name alone** — it touches no EEPROM state, so the answer is identical on every
dongle of this generation — XORed under a second keystream:

```c
uint8_t next1(uint8_t k) {
    k += 0x25;
    if (k < 0x1E) k = 0x7B;
    if (k > 0xAE) k = 0x17;
    return k;
}
```

The hash, all arithmetic mod 256, `n[0..7]` the name:

```c
v0 = 4*n0 + 0x11 + 3*n1;
v1 = 7*n2 + 0xA7 + 2*n3;
v2 = 4*n4 + 0x75 + 7*n5;
v3 =   n6 + 0x17 + 4*n7;

switch ((v0 + v1) & 3) {           /* one extra round, selected by the data */
  case 0: v3 = 6*v3 + v1 + 0x75;   break;
  case 1: v2 = v0 + 2*v3 + 0x0C;   break;
  case 2: v1 = 4*v0 + 0x37 + 4*v1; break;
  case 3: v0 = 5*v2 + 0x64;        break;
}
/* reply = v0 v1 v2 v3, low byte first */
```

**This is the picture key, not a curiosity.** The call sits between reading a PCX header
and validating it. The dword it returns, little-endian, is the seed for the Borland LCG
that decrypts the first 128 bytes of that PCX — body and 769-byte palette are plaintext,
and the encrypted region is a fixed 128 bytes in this generation, never a variable
offset. Get it wrong and the game reports `not a PCX-File`.

*Verified:* all 731 pictures in `FMEMO/PICS/FOTOPLAY.WAD` decrypt to a valid 320×220
8-bit PCX header using the code this returns, and five keys recovered independently by
seed-cracking reproduce exactly.

Not every archive goes through this path: archives packed **without** a dongle are keyed
with the vendor default `0x00012345` — FMEMO's own `GRAFIX` archive and all of FINDIT's
pictures are like that.

### Type 2 — programming

The host sends the 48-byte record encrypted under the *same* keystream type 3 answers
with (the XOR is its own inverse) and the dongle writes it to EEPROM. **There is no
authentication on this command.** That is how the real hardware behaves. Only funworld's
own programming tool is known to send it; an emulator can apply it in memory and rebuild
from configuration on reset.

## 2.4 The record

48 bytes, at EEPROM offset `0x00`:

```c
char     banner[16];   /* NUL-terminated, exactly filling the field */
uint32_t v[8];         /* little-endian, from offset 16 */
```

Values across all five dumped units:

```
v[0]  varies per unit — uninitialised host memory on the programming PC, not data
v[1]  0000038B   v[2] 000181CD   v[3] 0001D760   v[4] 00029B92
v[5]  0001287E   v[6] 0000089D
v[7]  varies per unit — not a checksum; nothing in the firmware reads it
```

`v[1]`..`v[6]` are byte-identical on every unit *and across generations*: they are
funworld's fixed per-title content keys, not per-site values. See `07` for which game
reads which, and for the fixed-offset rule that makes the layout non-negotiable.

The dongle knows nothing about the DS1982. Neither firmware references it, a second
serial channel, or any port outside the table above — the two tokens are entirely
separate paths.
