"""Extract input/output pairs for the keyed round function of the 2001 cipher.

DECODE (0x2D04C) on an 8-byte block, L at +0 and R at +4, little-endian:

    A     6 keyless rounds, const 0x5B2C004A, rot (5*(5-i)) & 31
    f     L,R = R ^ f(L), L          <- the 40-step LFSR at 0x2CD0C, the dongle's part
    B     6 keyless rounds, const 0x803425C3, rot ((5-i)*2) & 31
    f     L,R = R ^ f(L), L

Both Feistel stages are XOR and rotation only, so they are affine over GF(2) and can
be inverted and solved rather than searched.  Writing the stages out:

    (L1,R1) = A(ciphertext)                     known
    (L2,R2) = (R1 ^ f(L1), L1)                  R2 known, L2 unknown
    (L3,R3) = B(L2,R2)
    (L4,R4) = (R3 ^ f(L3), L3) = plaintext      known

R4 = L3, so L3 is known.  B_inv(L3,R3) gives (L2,R2), and R2 must equal L1 -- 32
linear equations in the 32 unknown bits of R3.  Solve them and both keyed rounds
fall out:  f(L3) = L4 ^ R3  and  f(L1) = L2 ^ R1.

Two pairs per block, and any block whose equations are inconsistent is one this
path did not process (0x2EB26 sends only the first block of a buffer here).
"""
import struct
import sys

sys.path.insert(0, r'C:\Users\xeon4\Documents\Claude\PeepeeBox-handoff\evidence\amore-pcx\derive')
import wad

M32 = 0xFFFFFFFF
CA, CB = 0x5B2C004A, 0x803425C3
ROT_A = [(5 * (5 - i)) & 31 for i in range(6)]
ROT_B = [((5 - i) * 2) & 31 for i in range(6)]
LCG = 0x08088405

I01 = r'C:\Users\xeon4\Documents\Claude\PeepeeBox\PP2001\Photo Play 2001 DE B4821\HardDisk.img'
I00 = r'C:\Users\xeon4\Documents\Claude\PeepeeBox\PP2000\Photo Play 2000 DE 6D139\HardDisk.img'
REF = r'C:\Users\xeon4\Documents\Claude\PeepeeBox-handoff\evidence\amore-pcx\findit_keys.txt'
ARCH = '/FINDIT/PICS/FOTOPLAY.WAD'


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


def solve_r3(L3, want_R2):
    """R2 is affine in R3; solve for the R3 that gives want_R2 (clean GF(2))"""
    b = inv(L3, 0, CB, ROT_B)[1]
    cols = [inv(L3, 1 << i, CB, ROT_B)[1] ^ b for i in range(32)]
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
    t, x = want_R2 ^ b, 0
    while t:
        hb = t.bit_length() - 1
        if hb not in basis:
            return 0, False
        bv, bm = basis[hb]
        t ^= bv
        x ^= bm
    return x, True


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

obs = {}
solved = bad = 0
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
        Lc, Rc = struct.unpack_from('<II', ct, off)
        L4, R4 = struct.unpack_from('<II', pt, off)
        # CBC: P_n = D(C_n) ^ C_{n-1} (0x2ECC5), so D(C_n) = P_n ^ C_{n-1}
        if off > 128:
            pL, pR = struct.unpack_from('<II', ct, off - 8)
            L4 ^= pL
            R4 ^= pR
        L1, R1 = fwd(Lc, Rc, CA, ROT_A)
        L3 = R4
        R3, ok = solve_r3(L3, L1)
        if not ok:
            bad += 1
            continue
        L2, R2 = inv(L3, R3, CB, ROT_B)
        if R2 != L1:
            bad += 1
            continue
        solved += 1
        for which, a, b2 in ((1, L1, L2 ^ R1), (2, L3, L4 ^ R3)):
            d = obs.setdefault(which, {})
            if a in d and d[a] != b2:
                obs.setdefault('clash', {})[which] = obs.get('clash', {}).get(which, 0) + 1
            d[a] = b2

with open('pairs.txt','w') as fh:
    for which in (1, 2):
        for a, b2 in obs.get(which, {}).items():
            fh.write('%08X %08X' % (a, b2) + chr(10))
print('blocks solved %d, unsolvable %d' % (solved, bad))
for which in (1, 2):
    d = obs.get(which, {})
    print('  keyed round %d: %d distinct inputs, %d clashes'
          % (which, len(d), obs.get('clash', {}).get(which, 0)))
a = set(obs.get(1, {})) & set(obs.get(2, {}))
print('  inputs seen in BOTH rounds: %d; of those, outputs agree: %d'
      % (len(a), sum(1 for k in a if obs[1][k] == obs[2][k])))
