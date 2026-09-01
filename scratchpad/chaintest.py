"""Decide the chaining mode of the I.G.O. 2/3 picture cipher with a test that can fail.

Every block yields TWO observations of the same keyed round f: one whose input comes off
the ciphertext side (A0) and one off the plaintext side (C0).  Kept in separate buckets
those never collide, which is why the earlier CBC check had no power.  Merged into one
map they do: with n blocks the two streams give about n^2 / 2^32 shared inputs, and every
one of them is a place the model must agree with itself.

For the right chaining mode every collision agrees.  For a wrong one they agree only by
chance, i.e. essentially never.

The affine algebra is precomputed rather than re-solved per block.  Both Feistel stages
are rotations and XORs, so B_rounds is affine:

    B1 = M0(C0) ^ M1(C1) ^ k1        B0 = N0(C0) ^ N1(C1) ^ k0

and B1 must equal A0, so C1 = M1^-1( A0 ^ M0(C0) ^ k1 ) -- one matrix apply per block
instead of building a fresh 32-column basis.
"""
import collections
import os
import pickle
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, 'docs', 'research', 'evidence'))
import wad

M32 = 0xFFFFFFFF
CA, CB = 0x5B2C004A, 0x803425C3
RA = list(range(25, -1, -5))
RB = list(range(10, -1, -2))


def rol(v, k):
    k &= 31
    return v if not k else ((v << k) | (v >> (32 - k))) & M32


def a_rounds(b0, b1):
    for s in RA:
        b0, b1 = (rol(b0 ^ CA, s) ^ b1) & M32, b0
    return b0, b1


def b_inv(b0, b1):
    for s in reversed(RB):
        b0, b1 = b1, (rol(b1 ^ CB, s) ^ b0) & M32
    return b0, b1


def apply_mat(cols, x):
    r = 0
    while x:
        i = (x & -x).bit_length() - 1
        r ^= cols[i]
        x &= x - 1
    return r


def invert(cols):
    """invert a 32x32 GF(2) matrix given as column images of the basis vectors"""
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
    out = []
    for i in range(32):
        t, x = 1 << i, 0
        while t:
            hb = t.bit_length() - 1
            bv, bm = basis[hb]
            t ^= bv
            x ^= bm
        out.append(x)
    return out


K0, K1 = b_inv(0, 0)
M0 = [b_inv(1 << i, 0)[1] ^ K1 for i in range(32)]
M1 = [b_inv(0, 1 << i)[1] ^ K1 for i in range(32)]
N0 = [b_inv(1 << i, 0)[0] ^ K0 for i in range(32)]
N1 = [b_inv(0, 1 << i)[0] ^ K0 for i in range(32)]
M1INV = invert(M1)


def run(pt_all, ct_all, entries, mode, limit):
    seen = {}
    hits = agree = 0
    blocks = 0
    for n, o, s in entries[:limit]:
        pt = pt_all[o:o + s]
        ct = ct_all[o:o + s]
        for i in range(s // 8):
            c0, c1 = struct.unpack_from('<II', ct, i * 8)
            p0, p1 = struct.unpack_from('<II', pt, i * 8)
            if i:
                q0, q1 = struct.unpack_from('<II', ct, (i - 1) * 8)
                r0, r1 = struct.unpack_from('<II', pt, (i - 1) * 8)
            else:
                q0 = q1 = r0 = r1 = 0
            if mode == 'ecb':
                d0, d1 = p0, p1
            elif mode == 'cbc':
                d0, d1 = p0 ^ q0, p1 ^ q1
            elif mode == 'pcbc':
                d0, d1 = p0 ^ q0 ^ r0, p1 ^ q1 ^ r1
            elif mode == 'cbc-pt':
                d0, d1 = p0 ^ r0, p1 ^ r1
            else:
                raise ValueError(mode)

            A0, A1 = a_rounds(c0, c1)
            C0 = d1
            C1 = apply_mat(M1INV, A0 ^ apply_mat(M0, C0) ^ K1)
            B0 = apply_mat(N0, C0) ^ apply_mat(N1, C1) ^ K0
            blocks += 1
            for a, b in ((C0, d0 ^ C1), (A0, B0 ^ A1)):
                if a in seen:
                    hits += 1
                    agree += (seen[a] == b)
                else:
                    seen[a] = b
    return blocks, len(seen), hits, agree


def main():
    limit = int(sys.argv[1]) if len(sys.argv) > 1 else 200
    cache = os.path.join(os.path.dirname(os.path.abspath(__file__)), os.pardir,
                         'findit_wads.pkl')
    if os.path.exists(cache):
        D = pickle.load(open(cache, 'rb'))
    else:
        imgs = {'IGO2': r'F:\HDDImages\IGO2\IGO 2 BE 82C81\HardDisk.img',
                'IGO3': r'F:\HDDImages\IGO3\IGO3DE-GF001-DOES-NOT-BOOT_\HardDisk.img',
                'IGO4': r'F:\HDDImages\IGO4\IGO4AT-A0006_\HardDisk.img'}
        A = '/FINDIT/PICS/FOTOPLAY.WAD'
        D = {t: bytes(wad.read(i, A)) for t, i in imgs.items()}
        pickle.dump(D, open(cache, 'wb'))
    es = wad.entries(D['IGO4'])
    print('%-8s %-8s %9s %10s %9s %8s  %s'
          % ('cipher', 'mode', 'blocks', 'f-inputs', 'collisions', 'agree', 'verdict'))
    for tag in ('IGO2', 'IGO3'):
        for mode in ('ecb', 'cbc', 'pcbc', 'cbc-pt'):
            blocks, uniq, hits, agree = run(D['IGO4'], D[tag], es, mode, limit)
            if hits < 20:
                v = 'no power (too few collisions)'
            elif agree == hits:
                v = '*** CONSISTENT ***'
            else:
                v = 'refuted (%.1f%% agree)' % (100.0 * agree / hits)
            print('%-8s %-8s %9d %10d %9d %8d  %s'
                  % (tag, mode, blocks, uniq, hits, agree, v))


if __name__ == '__main__':
    main()
