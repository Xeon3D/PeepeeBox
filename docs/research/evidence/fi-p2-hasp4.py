#!/usr/bin/env python3
"""A HASP-4 responder, ported from 86Box's src/device/hasp.c (RichardG / Peter Ferrie).

Same state machine, our dongle's own 15-byte preamble.  Used as the responder for
p2-haspsim so a candidate can be tested against the real client library without booting
anything.
"""

NONE, PW_BEGIN, PW_END, READ = 0, 1, 2, 3

# The clock-high wire bytes FSYSTEM.EXE's library sends after the C6 C7 C6 80 wake.
# Measured, identical in the live PeepeeBox trace and in the offline harness, and
# invariant across service / seed / password / address.
FI_PREAMBLE = bytes([0x8B, 0xF9, 0xDB, 0xBB, 0x95, 0xC9, 0xA9, 0x81,
                     0x93, 0xD1, 0xB1, 0x8D, 0x9F, 0xDD, 0xBD])

# hasp.c's Savage Quest tables.  The first set is what it answers with a matching
# password (passmode 2), the second what it answers otherwise.
PASSMODE2_ZERO = {0x94, 0x9E, 0xA4, 0xB2, 0xBE, 0xD0}
PASSMODE2_SET  = {0x8A, 0x8E, 0xCA, 0xD2, 0xE2, 0xF0, 0xFC}
GENERAL_SET = {0x88, 0x94, 0x98, 0x9C, 0x9E, 0xA0, 0xA4, 0xAA, 0xAE, 0xB0, 0xB2,
               0xBC, 0xBE, 0xC2, 0xC6, 0xC8, 0xCE, 0xD0, 0xD6, 0xD8, 0xDC, 0xE0,
               0xE6, 0xEA, 0xEE, 0xF2, 0xF6}


class Hasp4:
    def __init__(self, password=FI_PREAMBLE, prodinfo=b""):
        self.password = bytes(password)
        self.prodinfo = bytes(prodinfo)
        self.index = 0
        self.state = NONE
        self.passindex = 0
        self.passmode = 0
        self.prodindex = 0
        self.tmppass = bytearray(0x29)
        self.status = 0x80
        self.log = []

    # ---- the guest writing the data port ------------------------------------
    def write_data(self, val):
        val &= 0xFF
        if self.index == 0:
            self.index = 1 if val == 0xC6 else 0
        elif self.index == 1:
            self.index = 2 if val == 0xC7 else 0
        elif self.index == 2:
            if val == 0xC6:
                self.index = 3
            else:
                self.index = 0
                self.state = NONE
        elif self.index == 3:
            self.index = 0
            if val == 0x80:
                self.state = PW_BEGIN
                self.passindex = 0
                return                      # status deliberately left alone

        self.status = 0

        if self.state == READ:
            if self.passmode == 2:
                if val in PASSMODE2_ZERO:
                    return
                if val in PASSMODE2_SET:
                    self.status = 0x20
                    return
            if val in GENERAL_SET:
                self.status = 0x20

        elif self.state == PW_END:
            if val & 1:
                if self.passmode == 1 and val == 0x9D:
                    self.passmode = 2
                self.state = READ
            elif self.passmode == 1:
                self.tmppass[self.passindex] = val
                self.passindex += 1
                if self.passindex == len(self.tmppass):
                    if self.tmppass[0] == 0x9C and self.tmppass[1] == 0x9E:
                        i, self.prodindex = 2, 0
                        while i < len(self.tmppass):
                            self.prodindex = (self.prodindex << 1) + ((self.tmppass[i] >> 6) & 1)
                            i += 3
                        self.prodindex = (self.prodindex - 0xC08) << 4
                        if self.prodindex < (0x38 << 4):
                            self.passmode = 3
                    self.state = READ

        elif self.state == PW_BEGIN and (val & 1):
            self.tmppass[self.passindex] = val
            self.passindex += 1
            if self.passindex == len(self.password):
                self.state = PW_END
                self.passindex = 0
                self.passmode = int(bytes(self.tmppass[:len(self.password)]) == self.password)
                self.log.append("password match=%d" % self.passmode)

    # ---- the guest reading the status port ----------------------------------
    def read_status(self):
        if self.state == READ and self.passmode == 3 and self.prodinfo:
            if self.prodindex <= len(self.prodinfo) * 8:
                self.status = ((self.prodinfo[(self.prodindex - 1) >> 3]
                                >> ((8 - self.prodindex) & 7)) & 1) << 5
            else:
                self.status = (((0x534D ^ ((self.prodindex - 1) >> 4))
                                >> ((16 - self.prodindex) & 15)) & 1) << 5
            self.prodindex += 1
        return self.status
