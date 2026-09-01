"""Harvest exact input/output pairs of the dongle's keyed round, in bulk.

Phase 26 found the two dwords for one buffer with a 2^32 search.  The search was never
needed: the B rounds are rotations and XORs, so the value the filter tests is *affine*
in the guessed dword, and it can be solved instead.

    (L1,R1) = A_rounds(C_0)                        keyless
    (L3,R3) = B_rounds(f1 ^ R1, L1)                keyless, affine in f1
    P_0     = (f2 ^ R3, L3)

`L3` is the plaintext's second dword, so solving `Lin(f1) = L3 ^ const` gives f1 outright
and `f2 = P_0.lo ^ R3` follows.  Microseconds per buffer instead of 21 seconds, which
turns the 72 MB corpus into a generator of exact pairs

    f(L1) = f1        f(L3) = f2

for the one function this project cannot compute.  Every buffer of every entry yields two,
under whichever password pair the release carries.

Each solve is checked by decrypting the buffer it came from and comparing against
I.G.O. 4, so a pair is only recorded if it actually reproduces the plaintext.
"""
import collections
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, 'docs', 'research', 'evidence'))
from softround import schedule, soft_decode, M32
import wad

CA, CB = 0x5B2C004A, 0x803425C3
BUF = 4096


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


def _build():
    """b0 after the B rounds is affine in f1; capture the linear part and invert it"""
    base = b_rounds(0, 0)[0]
    cols = [b_rounds(1 << i, 0)[0] ^ base for i in range(32)]
    rows = [(cols[i], 1 << i) for i in range(32)]
    basis = {}
    for v, m in rows:
        while v:
            hb = v.bit_length() - 1
            if hb in basis:
                bv, bm = basis[hb]
                v ^= bv
                m ^= bm
            else:
                basis[hb] = (v, m)
                break
        else:
            raise ValueError('singular')
    inv = []
    for i in range(32):
        t, x = 1 << i, 0
        while t:
            hb = t.bit_length() - 1
            bv, bm = basis[hb]
            t ^= bv
            x ^= bm
        inv.append(x)
    return cols, inv


COLS, INV = _build()


def apply_mat(cols, x):
    r = 0
    while x:
        i = (x & -x).bit_length() - 1
        r ^= cols[i]
        x &= x - 1
    return r


def solve_buffer(ct, pt):
    """recover (f1, f2) for one buffer from its first block; None if it does not verify"""
    c0, c1 = struct.unpack_from('<II', ct, 0)
    p0, p1 = struct.unpack_from('<II', pt, 0)
    L1, R1 = a_rounds(c0, c1)

    # b0_after = Lin(f1) ^ const, where const is the value at f1 = 0
    const = b_rounds(R1, L1)[0]
    f1 = apply_mat(INV, p1 ^ const)
    L3, R3 = b_rounds((f1 ^ R1) & M32, L1)
    if L3 != p1:
        return None
    f2 = (p0 ^ R3) & M32
    return f1, f2, L1, L3


def decrypt_buffer(ct, f1, f2):
    n = len(ct) // 8
    k = schedule(f2, f1)
    out = bytearray(ct)
    prev = (0, 0)
    for i in range(max(0, n - 2)):
        c0, c1 = struct.unpack_from('<II', ct, i * 8)
        if i == 0:
            L1, R1 = a_rounds(c0, c1)
            L3, R3 = b_rounds((f1 ^ R1) & M32, L1)
            L, R = (f2 ^ R3) & M32, L3
        else:
            L, R = soft_decode(c0, c1, k)
        struct.pack_into('<II', out, i * 8, L ^ prev[0], R ^ prev[1])
        prev = (c0, c1)
    return bytes(out)


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 40
    imgs = {'IGO2': r'F:\HDDImages\IGO2\IGO 2 BE 82C81\HardDisk.img',
            'IGO3': r'F:\HDDImages\IGO3\IGO3DE-GF001-DOES-NOT-BOOT_\HardDisk.img',
            'IGO4': r'F:\HDDImages\IGO4\IGO4AT-A0006_\HardDisk.img'}
    A = '/FINDIT/PICS/FOTOPLAY.WAD'
    W = {t: wad.read(i, A) for t, i in imgs.items()}
    es = wad.entries(W['IGO4'])

    for tag in ('IGO2', 'IGO3'):
        pairs = {}
        clash = solved = failed = buffers = 0
        for name, off, size in es[:limit]:
            for b in range(0, size, BUF):
                n = min(BUF, size - b)
                if n < 64:
                    continue
                ct = W[tag][off + b:off + b + n]
                pt = W['IGO4'][off + b:off + b + n]
                buffers += 1
                r = solve_buffer(ct, pt)
                if r is None:
                    failed += 1
                    continue
                f1, f2, L1, L3 = r
                got = decrypt_buffer(ct, f1, f2)
                cl = (n // 8 - 2) * 8
                if got[:cl] != pt[:cl]:
                    failed += 1
                    continue
                solved += 1
                for a, v in ((L1, f1), (L3, f2)):
                    if a in pairs and pairs[a] != v:
                        clash += 1
                    pairs[a] = v
        print('%s: %d buffers, %d solved and verified, %d failed' % (tag, buffers, solved, failed))
        print('      %d distinct f-inputs, %d contradictions' % (len(pairs), clash))
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                               'fpairs_%s.txt' % tag.lower()), 'w') as fh:
            for a, v in sorted(pairs.items()):
                fh.write('%08X %08X\n' % (a, v))
        print('      written to scratchpad/fpairs_%s.txt' % tag.lower())


if __name__ == '__main__':
    main()
