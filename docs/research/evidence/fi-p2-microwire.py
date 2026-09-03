#!/usr/bin/env python3
"""A Microwire (93Cxx) serial-EEPROM responder on the parallel port.

What FSYSTEM.EXE's "HASP" library actually talks to.  Despite the Aladdin trappings
(HASPDOSDRV, the hasp(service, seed, lpt, pass1, pass2, ...) API and the C6 C7 C6 80
wake), the wire underneath is plain Microwire, with the same pin map PeepeeBox already
implements for the funworld 2001 "HDONGLE" in dongle_photoplay.c:

    CS  = DATA bit 1   (0x02)
    SK  = DATA bit 5   (0x20)      clock; sampled on the rising edge
    DI  = DATA bit 6   (0x40)
    DO  = STATUS bit 5 (0x20)      the library also probes STATUS bit 7 inverted

Frames are start bit, two opcode bits, then the address, MSB first:

    1 1 0  READ    -> address, then 16 data bits clocked out MSB first
    1 0 1  WRITE   -> address, then 16 data bits clocked in
    1 0 0  EWEN / EWDS / ERASE group

The client scrambles what it reads:  returned = raw ^ pass1 ^ address, measured against
the live library (see Docs/02).  store()/fetch() do that conversion so callers work in
plaintext.
"""

IDLE, OP, ADDR, RDATA, WDATA = range(5)

CS = 0x02
SK = 0x20
DI = 0x40
DO = 0x20        # in the STATUS byte


class Microwire:
    def __init__(self, nwords=256, abits=8, pass1=0x43B5, addr_bias=0):
        self.nwords = nwords
        self.abits = abits
        self.pass1 = pass1
        self.addr_bias = addr_bias
        self.mem = [0x0000] * nwords
        self.prev = 0x00
        self.reset()
        self.trace = []

    def reset(self):
        self.state = IDLE
        self.bits = 0
        self.acc = 0
        self.op = 0
        self.addr = 0
        self.shift = 0
        self.do = 1          # idle high / ready
        self.driving = False # DO is ours from the first read bit until CS drops

    # ---- plaintext helpers ---------------------------------------------------
    def store(self, addr, value):
        """Put `value` where a client reading word `addr` will see it."""
        self.mem[(addr + self.addr_bias) % self.nwords] = value ^ self.pass1 ^ addr

    def fetch(self, addr):
        return self.mem[(addr + self.addr_bias) % self.nwords] ^ self.pass1 ^ addr

    def store_string(self, addr, text, pad=" "):
        """The record FSYSTEM.EXE checks is ASCII in big-endian words."""
        b = text.encode("latin1")
        for i in range(0, len(b), 2):
            hi = b[i]
            lo = b[i + 1] if i + 1 < len(b) else ord(pad)
            self.store(addr + i // 2, (hi << 8) | lo)

    # ---- the wire ------------------------------------------------------------
    def write_data(self, val):
        val &= 0xFF
        prev, self.prev = self.prev, val

        if not (val & CS):
            self.reset()
            self.driving = False
            return
        if not (prev & CS):          # CS just went high: a new frame
            self.reset()

        rising = (val & SK) and not (prev & SK)
        if not rising:
            return
        bit = 1 if (val & DI) else 0

        if self.state == IDLE:
            if bit:                              # start bit
                self.state = OP
                self.acc = 0
                self.bits = 0
        elif self.state == OP:
            self.acc = (self.acc << 1) | bit
            self.bits += 1
            if self.bits == 2:
                self.op = self.acc
                self.state = ADDR
                self.acc = 0
                self.bits = 0
        elif self.state == ADDR:
            self.acc = (self.acc << 1) | bit
            self.bits += 1
            if self.bits == self.abits:
                self.addr = self.acc % self.nwords
                self.bits = 0
                if self.op == 0b10:              # READ
                    self.shift = self.mem[self.addr]
                    self.state = RDATA
                    self.driving = True
                    self.trace.append(("read", self.addr, self.shift))
                elif self.op == 0b01:            # WRITE
                    self.acc = 0
                    self.state = WDATA
                else:                            # EWEN / EWDS / ERASE group
                    self.state = IDLE
        elif self.state == RDATA:
            self.do = (self.shift >> 15) & 1
            self.shift = (self.shift << 1) & 0xFFFF
            self.bits += 1
            if self.bits == 16:
                self.state = IDLE
        elif self.state == WDATA:
            self.acc = ((self.acc << 1) | bit) & 0xFFFF
            self.bits += 1
            if self.bits == 16:
                self.mem[self.addr] = self.acc
                self.trace.append(("write", self.addr, self.acc))
                self.state = IDLE
                self.do = 1                      # write finished / ready

    def read_status(self):
        # DO on STATUS bit 5; bit 7 is BUSY, which the library reads inverted, so
        # drive both consistently.  Bit 7 high == BUSY inactive.
        s = DO if self.do else 0x00
        s |= 0x00 if self.do else 0x80
        return s

    def read_status_simple(self):
        return DO if self.do else 0x00
