"""Read the h5dmp dongle dumps: passwords, and the record as the guest sees it.

The dump opens with the two passwords little-endian (verified against the 2001 PT
dump, whose first four bytes are 77 74 57 7D = 0x7477 / 0x7D57, the pair extracted
independently from the binaries in Docs/19).

The record lives later in the dump with every 16-bit word byte-swapped, which is
why "V reisno2 00 1P()T" reads as "Version 2001 (PT)" once unswapped.  That is the
same low-byte-first order the I.G.O. builds unpack with.
"""
import glob
import os
import re
import struct

ROOT = os.path.dirname(os.path.abspath(__file__)) + '/h5dmp'


def unswap(b):
    out = bytearray(len(b))
    for i in range(0, len(b) - 1, 2):
        out[i], out[i + 1] = b[i + 1], b[i]
    return bytes(out)


for folder in sorted(glob.glob(ROOT + '/*')):
    p = os.path.join(folder, 'hasp.dmp')
    if not os.path.exists(p):
        continue
    d = open(p, 'rb').read()
    p1, p2 = struct.unpack_from('<HH', d, 0)
    sw = unswap(d)

    # the record is the printable run in the unswapped image
    best = None
    for m in re.finditer(rb'[ -~]{16,}', sw):
        if best is None or len(m.group()) > len(best.group()):
            best = m
    print('=== %-8s  passwords %04X / %04X   dump %d bytes'
          % (os.path.basename(folder), p1, p2, len(d)))
    if best:
        print('    record at %03X (%d bytes):' % (best.start(), len(best.group())))
        print('      %r' % best.group().decode('latin1'))
