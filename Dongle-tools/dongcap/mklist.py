"""Build the DONGCAP.LST work list for a release.

For every 4 KB buffer of every enciphered archive it records the first keyed round's
input, which is the ciphertext's block 0 through the keyless A rounds:

    (L1, R1) = A_rounds(c0, c1)

No plaintext is involved, so this works on any encrypted archive.  The tool gets f1 from
the dongle and derives the second input itself.

It also embeds a few (input, expected output) pairs so the tool can find the preamble seed
on the hardware instead of guessing it.  Those come from buffers whose plaintext we hold:
I.G.O. 2 and 3 from the harvested pairs, Photo Play 2001 from its PCX header, which is the
same for every entry.

    python mklist.py 2001|igo2|igo3 <HardDisk.img> [out.lst]
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, os.pardir, 'docs', 'research', 'evidence'))
import wad

M32 = 0xFFFFFFFF
CA, CB = 0x5B2C004A, 0x803425C3
BUF = 4096
LST_MAGIC = 0x50414344

ARCHIVES = ['/FINDIT/PICS/FOTOPLAY.WAD', '/AMORE/COMIX/FOTOPLAY.WAD']

# 2001's every entry starts with the same plaintext PCX header
PCX_HEAD = (0x0805050A, 0x00000000)


def rol(v, s):
    s &= 31
    return v if not s else ((v << s) | (v >> (32 - s))) & M32


def a_rounds(b0, b1):
    for s in range(25, -1, -5):
        b0, b1 = (rol(b0 ^ CA, s) ^ b1) & M32, b0
    return b0, b1


def b_rounds(b0, b1):
    for s in range(10, -1, -2):
        b0, b1 = (rol(b0 ^ CB, s) ^ b1) & M32, b0
    return b0, b1


def _linmap():
    base = b_rounds(0, 0)[0]
    cols = [b_rounds(1 << i, 0)[0] ^ base for i in range(32)]
    basis = {}
    for i in range(32):
        v, m = cols[i], 1 << i
        while v:
            hb = v.bit_length() - 1
            if hb in basis:
                bv, bm = basis[hb]
                v ^= bv
                m ^= bm
            else:
                basis[hb] = (v, m)
                break
    inv = []
    for i in range(32):
        t, x = 1 << i, 0
        while t:
            hb = t.bit_length() - 1
            bv, bm = basis[hb]
            t ^= bv
            x ^= bm
        inv.append(x)
    return inv


INV = _linmap()


def apply_mat(cols, x):
    r = 0
    while x:
        i = (x & -x).bit_length() - 1
        r ^= cols[i]
        x &= x - 1
    return r


def solve_from_plain(c0, c1, p0, p1):
    """recover (L1, f1, L3, f2) for a block whose plaintext is known"""
    L1, R1 = a_rounds(c0, c1)
    const = b_rounds(R1, L1)[0]
    f1 = apply_mat(INV, p1 ^ const)
    L3, R3 = b_rounds((f1 ^ R1) & M32, L1)
    if L3 != p1:
        return None
    return L1, f1, L3, (p0 ^ R3) & M32


def calibration(gen, img):
    """a few (input, expected) pairs the tool can hunt the seed with"""
    out = []
    if gen in ('igo2', 'igo3'):
        here = os.path.dirname(os.path.abspath(__file__))
        f = os.path.join(here, os.pardir, os.pardir, 'scratchpad', 'fpairs_%s.txt' % gen)
        pairs = [tuple(int(x, 16) for x in l.split()) for l in open(f)]
        out = pairs[:3]
    else:
        d = wad.read(img, ARCHIVES[0])
        es = wad.entries(d)
        _, o, _s = es[0]
        c0, c1 = struct.unpack_from('<II', d, o)
        r = solve_from_plain(c0, c1, PCX_HEAD[0], PCX_HEAD[1])
        if r is None:
            raise SystemExit('2001: block 0 did not solve against the PCX header --\n'
                             'the plaintext assumption is wrong, refusing to emit a list')
        L1, f1, L3, f2 = r
        out = [(L1, f1), (L3, f2)]
    return out


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    gen = sys.argv[1].lower()
    img = sys.argv[2]
    outp = sys.argv[3] if len(sys.argv) > 3 else 'DONGCAP.LST'

    work = []
    for path in ARCHIVES:
        try:
            d = wad.read(img, path)
        except Exception:
            d = None
        if not d:
            print('  %-28s absent' % path)
            continue
        es = wad.entries(d)
        if not es:
            print('  %-28s not a GWAD' % path)
            continue
        heads = {bytes(d[o:o + 8]) for _, o, _s in es[:40]}
        if len(heads) > 1:
            print('  %-28s %d entries, first blocks differ -- not this cipher, skipped'
                  % (path, len(es)))
            continue
        n = 0
        for _, off, size in es:
            for b in range(0, size, BUF):
                if size - b < 64:
                    continue
                c0, c1 = struct.unpack_from('<II', d, off + b)
                work.append(a_rounds(c0, c1))
                n += 1
        print('  %-28s %d entries, %d buffers' % (path, len(es), n))

    if not work:
        raise SystemExit('nothing to capture')

    cal = calibration(gen, img)
    print('  calibration pairs: %d' % len(cal))
    for a, b in cal:
        print('     f(%08X) = %08X' % (a, b))

    with open(outp, 'wb') as fh:
        fh.write(struct.pack('<IIII', LST_MAGIC, len(cal), len(work), 0))
        for a, b in cal:
            fh.write(struct.pack('<II', a, b))
        for a, b in work:
            fh.write(struct.pack('<II', a, b))
    print('%s: %d buffers, %d keyed rounds, %d bytes'
          % (outp, len(work), len(work) * 2, os.path.getsize(outp)))


if __name__ == '__main__':
    main()
