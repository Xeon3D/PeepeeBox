# The DS1982 iButton emulation, in full

`Docs/05` recovers the *protocol*. This document explains the *implementation* — the
device inside PeepeeBox that makes the second token exist, in
`peepeebox/master/src/device/dongle_photoplay.c` (the `ib_*` half).

It is the one piece of protection hardware in this project that is **solved across every
generation**: the same device satisfies a 1999 image and a 2008 image without changes.

---

## 1. What is being emulated

Two things stacked, and keeping them separate is what makes the code small:

```
  guest  --INT/OUT on 0x268..0x26F-->  [ 16550 UART model ]
                                              |  one byte per bit slot
                                              v
                                       [ 1-Wire DS1982 slave ]
```

The cabinets carry a Dallas 1-Wire adapter on a UART's TX/RX pair — the DS9097
arrangement from Maxim app note **AN214**. The games bit-bang 1-Wire by sending one UART
byte per 1-Wire *bit* and reading the echo back. So the emulation needs a UART that is
just complete enough to be driven, and a 1-Wire slave behind it.

No interrupts are involved anywhere; the games poll.

## 2. Why no baud-rate tracking is needed

AN214 encodes bit values in *timing*, which normally means the receiver must care about
the baud divisor. Here it does not, because the host only ever puts **three literal
values** on the wire:

| Byte the host sends | Meaning |
|---|---|
| `0xF0` | 1-Wire **reset pulse** (sent at divisor 11, ~10473 baud) |
| `0xFF` | read slot, or write-1 |
| `0x00` | write-0 |

The three cases are unambiguous *by value alone*. The divisor writes are accepted and
stored so the guest can read them back, but nothing in the model consults them.

## 3. The UART model

Registered with `io_sethandler(0x268, 8, ...)` — eight ports, the standard 16550 window.

| Offset | Read | Write |
|---|---|---|
| `+0` | RBR (or DLL when DLAB) — **clears DR** | THR → a 1-Wire event (or DLL when DLAB) |
| `+1` | IER (or DLM when DLAB) | IER / DLM |
| `+2` | IIR = `0x01`, "no interrupt pending" | FCR — accepted, ignored |
| `+3` | LCR | LCR — **bit 7 is DLAB** |
| `+4` | MCR | MCR |
| `+5` | **LSR** — see below | — |
| `+6` | MSR = `0xB0` (DCD\|DSR\|CTS asserted) | — |
| `+7` | SCR | SCR |

**LSR is the interesting one.** It always reports `0x60` — `THRE | TEMT`, transmitter
always empty — because a write to THR is turned into a 1-Wire event and answered
*synchronously*, with no queue and no latency. `DR` (`0x01`) is added whenever a reply
byte is waiting:

```c
return (uint8_t) (0x60 | (ib->rx_full ? 0x01 : 0x00));
```

That is exactly the pair of bits `Docs/05` shows the games polling: they wait for
`LSR & 0x60 == 0x60` during reset setup, and `LSR & 1` before every read.

A write to THR therefore does the whole round trip in one instruction:

```c
static void ib_tx(ib_t *ib, uint8_t val)
{
    if (val == 0xF0) { ib_reset_state(ib); ib->rbr = 0xE0; }   /* presence */
    else               ib->rbr = ib_on_slot(ib, val);          /* one bit  */
    ib->rx_full = 1;
}
```

### Presence detection is a deliberately corrupted echo

On reset the host sends `0xF0` and tests `RX != 0xF0`. On real hardware a slave holding
the line low mangles the returned byte. The model returns **`0xE0`** — any value other
than `0xF0` would do — and resets the slave's framing at the same time.

## 4. The 1-Wire slave

### One byte in, one bit out

```c
static uint8_t ib_on_slot(ib_t *ib, uint8_t host)
{
    if (ib->out_pos < ib->out_len * 8) {         /* we are driving the wire */
        const int bit = (ib->outbuf[ib->out_pos >> 3] >> (ib->out_pos & 7)) & 1;
        ib->out_pos++;
        return bit ? 0xFF : 0x00;
    }
    if (host)                                     /* else the host writes to us */
        ib->inbits |= (uint8_t) (1 << ib->nbits);
    if (++ib->nbits == 8) { ... ib_on_byte(ib, v); }
    return host ? 0xFF : 0x00;
}
```

The **queued output takes precedence**. That is the whole direction-handling rule: 1-Wire
read slots and write slots look identical on the wire (the host sends `0xFF` for both), so
"if I have something to say, I am talking; otherwise you are" is sufficient. It works
because the protocol is strictly turn-based — the host never writes while a reply is
outstanding.

Bits are LSB-first in both directions, matching Dallas convention.

### The command state machine

Three states — `CMD`, `TA`, `DONE` — and three commands:

| Command | Handling |
|---|---|
| `0x33` **READ ROM** | queue the 8-byte ROM id; → `DONE` |
| `0xCC` **SKIP ROM** | stay in `CMD`: a memory command follows |
| `0xF0` **READ MEMORY** | → `TA`, collect two address bytes |

Anything else is logged as unhandled and parks in `DONE`.

`SKIP ROM` staying in `CMD` rather than advancing is what lets the real sequence work:

```
reset -> SKIP ROM (CC) -> READ MEMORY (F0) -> TA1 -> TA2 -> stream
```

### READ MEMORY

Once both address bytes arrive, the device answers with **its own CRC8 of
command+address**, then the page from that address onward:

```c
uint8_t hdr[3] = { 0xF0, ta1, ta2 };
buf[0] = ib_crc8(hdr, 3, 0);
memcpy(buf + 1, ib->mem + ta, IB_MEMSIZE - ta);
```

The games verify that CRC byte before accepting the stream, which is why it must be
computed rather than faked.

## 5. Device identity

### The ROM id

```
byte 0    : 0x09           family code for DS1982 / DS2502
bytes 1-6 : 50 50 42 4F 58 00     ("PPBOX")
byte 7    : CRC8 of the first seven
```

The games check **only that CRC8 over all eight bytes is zero** — the serial is never
compared against anything (`Docs/05`). So the id is free choice; `PPBOX` is a signature,
not a requirement.

### The memory page

128 bytes, zero-filled, with the expected string at **offset 5**:

```
Photo Play 2000 Version 3
```

The games compare `page[5:]` against a constant stored XOR-0x7C in their own binaries.
This was recovered from IGO6 and later confirmed unchanged in 2008: `TOWERS.EXE` carries
that constant at file offset `0x2FA50`. The same page satisfies both.

Bytes `0..4` are never examined.

## 6. CRC8

Maxim/Dallas CRC8, reflected polynomial `0x8C`, initial value 0 — generated at startup
rather than embedded:

```c
c = (c & 1) ? ((c >> 1) ^ 0x8C) : (c >> 1);
```

`Docs/05` verified this is **byte-identical** to the 256-entry table the games carry at
`DS:0x2176`. No custom or obfuscated variant is involved.

## 7. How it is brought up

The iButton is started by the **LPT dongle device's** `init`, not as a separate entry in
any device list:

```c
if (device_get_config_int("ibutton"))
    ib_start();
```

Both tokens are mandatory — `Docs/03` measured that a HASP-only emulator aborts every game
with `DS1982 FAILED` — so wanting one without the other is never useful. It is exposed as
a checkbox (*"Emulate the DS1982 iButton at I/O 268h"*, default on) purely so it can be
turned off for experiments.

This also settles the question left open in `CLAUDE.md` — *"can 86Box host a serial port at
base 0x268?"* The answer is that it does not need to: the UART is modelled directly, so
there is no serial port, no passthrough, and no host-side helper.

## 8. Evidence it works

**1999 (`PP1999AT-81519`, untouched image):** full sequence served, and the guest reached
the Photo Play menu with no `DS1982` error.

**2008 (`IGO8ES-VM007`, untouched image):** identical device, no changes:

```
IB: DS1982 iButton at I/O 268, ROM 09 50 50 42 4F 58 00 FF
IB: READ ROM
IB: SKIP ROM
IB: READ MEMORY
IB: addr 0000, crc 8D, streaming 128 bytes
IB: SKIP ROM
IB: READ MEMORY
IB: addr 0000, crc 8D, streaming 128 bytes
```

and again, in full, when a game was launched. No DS1982 error appears anywhere in a 2008
run — the failures there are all NG-DONGLE (`Docs/09`).

**Independent corroboration from the I/O scanner:** with unclaimed-port logging enabled,
the guest is seen scanning `0x203, 0x207, 0x20B … 0x2FF` — one read every four ports,
i.e. `base+3` (LCR) for each candidate UART base. The scan **visibly skips
`0x268-0x26F`** because this device claims that range. The software discovers it exactly
the way it would discover real hardware.

## 9. Honest limitations

- **No timing model.** Bit slots are decided by byte value, not by baud or pulse width. A
  host that distinguished write-1 from a read slot by timing rather than by value would
  not be served correctly. None of these games do.
- **`0xF0` is assumed to be READ MEMORY** once a ROM command has been dealt with. In
  standard 1-Wire, `0xF0` is also SEARCH ROM. The games never issue SEARCH ROM, so context
  disambiguates — but a different host could confuse this model.
- **Read-only.** No programming, write, or status commands are implemented. The DS1982 is
  an EPROM part and the games only ever read it.
- **One fixed page.** The 128 bytes are built at init and never change. There is no
  per-image customisation, because none has been needed: the same page satisfies 1999
  through 2008.
- **`rx_full` has no depth.** One reply byte is held at a time, which matches a protocol
  that is strictly request/response.

## 10. Relationship to the Python reference

`scripts/ds1982sim.py` is the original, validated against real software on 2026-08-27 over
an 86Box serial passthrough. The C device is a direct port of its state machine, and the
two agree structurally: `OneWire.on_slot` / `on_byte` map onto `ib_on_slot` / `ib_on_byte`.

The Python version remains useful for testing against a **stock** 86Box, where it still
needs `scripts/ibutton_retarget.py` to move the port from `0x268` to a COM base the
emulator can host. With PeepeeBox neither script is required, and no byte of the guest
image is modified.
