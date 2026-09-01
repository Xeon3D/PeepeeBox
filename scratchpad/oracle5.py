"""Sweep the plausible readings of the 2001 cipher against a test that can fail.

The LFSR at 0x2CD0C shifts right 40 times on a 32-bit word, so its output is
    V40 = XOR over k of  b_k * (POLY >> (39-k))
and POLY = 0x80500062 is 23 bits wide, so only the last 23 branch bits reach the
result: every output must lie in a fixed 23-dimensional subspace of GF(2)^32.

That is a property of the round function alone, independent of the oracle.  So for
any candidate reading of the cipher, the f-outputs it extracts must lie in that
subspace -- ~100% if the reading is right, ~0.2% if it is not.

Variants tried: which dword is L, whether the ciphertext or the plaintext is the
side Feistel A applies to, and whether the CBC XOR goes on the plaintext or the
ciphertext.
"""
import struct
import os
import sys

# The evidence used to live outside the repository and was deleted with it; these
# two files are vendored in docs/research/evidence now.
_EV = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   os.pardir, 'docs', 'research', 'evidence')
sys.path.insert(0, _EV)
import wad

M32 = 0xFFFFFFFF
POLY = 0x80500062
CA, CB = 0x5B2C004A, 0x803425C3
ROT_A = [(5 * (5 - i)) & 31 for i in range(6)]
ROT_B = [((5 - i) * 2) & 31 for i in range(6)]
LCG = 0x08088405

I01 = r'C:\Users\xeon4\Documents\Claude\PeepeeBox\PP2001\Photo Play 2001 DE B4821\HardDisk.img'
I00 = r'C:\Users\xeon4\Documents\Claude\PeepeeBox\PP2000\Photo Play 2000 DE 6D139\HardDisk.img'
REF = os.path.join(_EV, 'findit_keys.txt')
ARCH = '/FINDIT/PICS/FOTOPLAY.WAD'

BASIS = {}
for s in range(40):
    v = (POLY >> s) & M32
    while v:
        hb = v.bit_length() - 1
        if hb in BASIS:
            v ^= BASIS[hb]
        else:
            BASIS[hb] = v
            break


def in_span(t):
    while t:
        hb = t.bit_length() - 1
        if hb not in BASIS:
            return False
        t ^= BASIS[hb]
    return True


def rol(v, k):
    k &= 31
    return ((v << k) | (v >> (32 - k))) & M32 if k else v


def fwd(L, R, c, rots):
    for k in rots:
        L, R = (R ^ rol(L ^ c, k)) & M32, L
    return L, R


def inv(L, R, c, rots):
    for k in reversed(rots):
        L, R = R, (L ^ rol(R ^ c, k)) & M32
    return L, R


def solve(L3, want, c, rots):
    b = inv(L3, 0, c, rots)[1]
    cols = [inv(L3, 1 << i, c, rots)[1] ^ b for i in range(32)]
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
    t, x = want ^ b, 0
    while t:
        hb = t.bit_length() - 1
        if hb not in basis:
            return None
        bv, bm = basis[hb]
        t ^= bv
        x ^= bm
    return x


def ks(seed, n):
    s = seed & M32
    o = bytearray()
    for _ in range(n):
        s = (s * LCG + 1) & M32
        o.append(s >> 24)
    return bytes(o)


d1 = wad.read(I01, ARCH)
d0 = wad.read(I00, ARCH)
e1 = {n: (o, s) for n, o, s in wad.entries(d1)}
ref = dict(l.split() for l in open(REF) if len(l.split()) == 2)

blocks = []
for n, o0, s in wad.entries(d0)[:2]:
    if n not in ref or n not in e1:
        continue
    pt = bytearray(d0[o0:o0 + s])
    k = ks(int(ref[n], 16), 128)
    for i in range(128):
        pt[i] ^= k[i]
    o1, _ = e1[n]
    ct = d1[o1:o1 + s]
    for off in range(128, s - 8, 8):
        prev = struct.unpack_from('<II', ct, off - 8) if off > 128 else (0, 0)
        blocks.append((struct.unpack_from('<II', ct, off),
                       struct.unpack_from('<II', pt, off), prev))

print('%d blocks' % len(blocks))
print('%-58s %s' % ('variant', 'outputs in the 23-dim subspace'))

for swapLR in (0, 1):
    for from_ct in (0, 1):
        for cbc_on in ('pt', 'ct', 'none'):
            hits = tot = 0
            for (c0, c1), (p0, p1), (q0, q1) in blocks[:3000]:
                Lc, Rc = (c1, c0) if swapLR else (c0, c1)
                Lp, Rp = (p1, p0) if swapLR else (p0, p1)
                Lq, Rq = (q1, q0) if swapLR else (q0, q1)
                if cbc_on == 'pt':
                    Lp ^= Lq
                    Rp ^= Rq
                elif cbc_on == 'ct':
                    Lc ^= Lq
                    Rc ^= Rq
                a, b = (Lc, Rc) if from_ct else (Lp, Rp)
                z, w = (Lp, Rp) if from_ct else (Lc, Rc)
                L1, R1 = fwd(a, b, CA, ROT_A)
                L3 = w
                R3 = solve(L3, L1, CB, ROT_B)
                if R3 is None:
                    continue
                L2, R2 = inv(L3, R3, CB, ROT_B)
                for o in (L2 ^ R1, z ^ R3):
                    tot += 1
                    hits += in_span(o)
            print('  swapLR=%d fromCT=%d cbc=%-4s   %6.2f%%  (%d/%d)'
                  % (swapLR, from_ct, cbc_on, 100.0 * hits / max(tot, 1), hits, tot))
