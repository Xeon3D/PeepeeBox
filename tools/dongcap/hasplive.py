"""Run a release's own HASP library against a REAL part, on the dongle host.

    sudo python3 hasplive.py MENU.EXE PASS1 PASS2 [--wire FILE] CALL [CALL ...]

hasplib.py runs the library under unicorn against a Python copy of the device.  This is
the same machine with the device swapped for the parallel port itself (/dev/port), so a
call made here is the call a game makes, sent by the game's own code, answered by the
part in the socket.  Nothing about the wire is assumed: the password bursts, the round
preambles and the query framing are whatever the library emits for the password given.

That is what makes one library serve every part.  I.G.O. 3's MENU.EXE is the one used,
because it is the build hasplib.py already knows how to locate; the password is set
from the command line, so a 2001 part is asked with 7477/7D57, an I.G.O. 2 part with
68BB/1329, and a part whose pair is unknown can be tried with candidates.

Calls (hex numbers):

    status              service 5 -- the probe the menus open with; p3 is the port
    read START COUNT    service 0x32, ReadBlock: COUNT words from word START
    encode MODE HEX     service 0x3C, HaspEncodeData, on the bytes HEX (length % 8 == 0)
    decode MODE HEX     service 0x3D, HaspDecodeData
    raw SVC P1 P2 P3 P4 any service, four word parameters

Every call prints p1..p4 on return, and the buffer for read/encode/decode.  --wire
records every port access (W/C/S lines, dongwire's script format), so a live exchange
can be replayed with dongwire or compared with the emulated device.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hasplib as H                                         # noqa: E402

BASE = 0x378


class RealPart:
    """The part in the socket, through /dev/port.  Offsets 0..2 = DATA, STATUS, CONTROL."""

    def __init__(self, base=BASE, wire=None):
        self.base = base
        self.fd = os.open('/dev/port', os.O_RDWR)
        self.wire = wire
        self.nw = self.nr = 0

    def write(self, off, v):
        os.pwrite(self.fd, bytes([v & 0xFF]), self.base + off)
        self.nw += 1
        if self.wire:
            self.wire.write('%s %02X\n' % ('W' if off == 0 else 'C' if off == 2 else 'X', v & 0xFF))

    def read(self, off):
        v = os.pread(self.fd, 1, self.base + off)[0]
        self.nr += 1
        if self.wire and off == 1:
            self.wire.write('S %02X\n' % v)
        return v


class LiveMenu(H.Menu):
    """H.Menu locates the library through the HaspEncodeData boot check; the password
    it finds there is replaced by the one asked for."""


def main():
    a = sys.argv[1:]
    wire = None
    if '--wire' in a:
        i = a.index('--wire')
        wire = open(a[i + 1], 'w')
        del a[i:i + 2]
    if len(a) < 4:
        print(__doc__)
        return 2
    exe, p1, p2 = a[0], int(a[1], 16), int(a[2], 16)
    menu = LiveMenu(exe)
    menu.pass1, menu.pass2 = p1, p2
    part = RealPart(wire=wire)
    m = H.Machine(menu, part)
    ds = menu.dgroup
    BUF = 0xF000                                            # scratch buffer in DGROUP

    print('library %04X:%04X  DGROUP %04X  passwords %04X/%04X  port %03X'
          % (menu.lib - H.LOAD, menu.lib_off, ds - H.LOAD, p1, p2, part.base))

    port = 0
    s5, e = m.hasp(5, 0, [0, 0, 0, 0])
    port = s5[2]
    print('status      p1..p4 %s %s' % (' '.join('%04X' % x for x in s5), e or ''))

    calls = a[3:]
    i = 0
    while i < len(calls):
        c = calls[i]
        if c == 'status':
            i += 1
            continue
        if c == 'read':
            start, count = int(calls[i + 1], 16), int(calls[i + 2], 16)
            i += 3
            m.uc.mem_write(ds * 16 + BUF, b'\0' * (2 * count))
            r, e = m.hasp(0x32, port, [start, count, ds, BUF])
            buf = bytes(m.uc.mem_read(ds * 16 + BUF, 2 * count))
            print('read %X+%X  p1..p4 %s %s' % (start, count, ' '.join('%04X' % x for x in r),
                                                 e or ''))
            print('   ', buf.hex(' '))
            print('   ', repr(buf))
        elif c in ('encode', 'decode'):
            mode, data = int(calls[i + 1], 16), bytes.fromhex(calls[i + 2])
            i += 3
            m.uc.mem_write(ds * 16 + BUF, data)
            svc = 0x3C if c == 'encode' else 0x3D
            r, e = m.hasp(svc, port, [mode, len(data), ds, BUF])
            out = bytes(m.uc.mem_read(ds * 16 + BUF, len(data)))
            print('%s mode %d  p1..p4 %s %s' % (c, mode, ' '.join('%04X' % x for x in r),
                                                  e or ''))
            print('    in ', data.hex(' '))
            print('    out', out.hex(' '))
        elif c == 'raw':
            svc = int(calls[i + 1], 16)
            p = [int(x, 16) for x in calls[i + 2:i + 6]]
            i += 6
            r, e = m.hasp(svc, port, p)
            print('svc %02X  p1..p4 %s %s' % (svc, ' '.join('%04X' % x for x in r), e or ''))
        else:
            print('unknown call %r' % c)
            return 2
    print('port accesses: %d writes, %d reads' % (part.nw, part.nr))
    return 0


if __name__ == '__main__':
    sys.exit(main())
