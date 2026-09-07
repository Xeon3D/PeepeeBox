# 3. The second token — a DS1982 iButton

**There are two tokens, and both are mandatory.** A game aborts with `DS1982 FAILED`
if this one is missing, no matter how perfectly the parallel dongle answers — measured
on 1999, 2000 and 2001 images alike, in `\FN_SYS\DFU\TRANS\ERROR.LOG`.
Emulating one alone changes nothing on screen. They are entirely separate paths — the parallel dongle's firmware does not reference this device at all.

Everything below is **measured** from the game binaries and then **verified live**: the
state machine was run against the real software over a serial passthrough before it was
ported into the emulator, and the guest's `ERROR.LOG` gained no new entry.

## 3.1 Hardware

A Dallas **DS1982** (DS2502-class) 1-Wire EPROM reached through a **16550-class UART at
I/O `0x268`** — the standard "1-Wire over a UART" arrangement of Maxim AN214. The base
address is an immediate in the guest (`push dword 0x00030268`), not a probe.

The UART model needed is tiny. The games poll and use no interrupts:

- `LSR` always reports `THRE|TEMT` (`0x60`) — bytes are consumed instantly — plus `DR`
  (`0x01`) when a reply byte is waiting.
- `MSR` returns `DSR|CTS|DCD` asserted (`0xB0`), `IIR` returns `0x01` (no interrupt).
- `DLL`/`DLM` behind `LCR` bit 7 exist but nothing depends on the divisor: **baud is
  irrelevant**, because the host only ever puts three literal values on the wire, so the
  cases are unambiguous by value alone.

## 3.2 The wire

Every UART byte carries exactly **one 1-Wire bit**, LSB first, and bit 0 of the byte
handed back is the level of the wire.

| host writes | means | device answers |
|---|---|---|
| `0xF0` | reset pulse | anything **≠ `0xF0`** — a present slave corrupts the echo (`0xE0` works) |
| `0xFF` | read slot, or write-1 | the queued bit as `0xFF`/`0x00`, else echo `0xFF` |
| `0x00` | write-0 | `0x00` |

A reset also clears all framing state.

## 3.3 Commands

Three, and that is the whole of what the games issue:

| byte | command | device does |
|---|---|---|
| `0x33` | READ ROM | shift out the 8 ROM bytes |
| `0xCC` | SKIP ROM | nothing; another command follows |
| `0xF0` | READ MEMORY | read a 2-byte target address, then shift out **CRC8 of `{F0, TA0, TA1}`** followed by the page from that address to the end |

CRC8 is Maxim/Dallas, reflected polynomial `0x8C` — byte-identical to the table the
games carry at `DS:0x2176`.

The guest's real sequence, observed end to end:

```
RESET -> presence ; READ ROM        \  read twice and compared —
RESET -> presence ; READ ROM        /  an off-by-one in bit order fails here
RESET -> presence ; SKIP ROM ; READ MEMORY 0x0000 -> crc + 128 bytes
RESET -> presence ; SKIP ROM ; READ MEMORY 0x0000 -> crc + 128 bytes
```

## 3.4 The minimum viable device

```
ROM  : 09 <six bytes of anything> <crc8>   ; family 0x09, CRC8 over all 8 == 0
PAGE : 128 bytes, with "Photo Play 2000 Version 3" at offset 5
```

**The serial number is never compared** — only the CRC is checked. The page comparison
is exact and is the second gate. Both gates passing is what silences `DS1982 FAILED`.

That string does not vary with the release: 2001-generation images check the same
`Photo Play 2000 Version 3`.
