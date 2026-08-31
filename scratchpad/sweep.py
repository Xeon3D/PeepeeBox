"""Sweep readings of 0x2E682 and the schedule against a known (acc, D).

The first block of a buffer gives (acc, D) without involving 0x2E682 at all, so
those values are fixed while the software round is varied.  Block 1 of the buffer
is then a 64-bit test: exactly one variant should reproduce it.
"""
import itertools
import struct

M32 = 0xFFFFFFFF


def rol(v, k):
    k &= 31
    return ((v << k) | (v >> (32 - k))) & M32 if k else v


def ror(v, k):
    k &= 31
    return ((v >> k) | (v << (32 - k))) & M32 if k else v


def sched(D, acc, xor1, prog):
    k = [0] * 26
    k[0] = D
    d = D
    for i in range(1, 26):
        k[i] = (k[i - 1] + acc) & M32 if prog else (D + i * acc) & M32
        d = ror(d, k[i - 1] & 0x1F)
        if xor1:
            k[1] ^= d
    return k


def soft(L, R, k, order, ascend, sub_first, rot_src, inverse):
    rng = range(1, 13) if ascend else range(12, 0, -1)
    if inverse:
        rng = list(reversed(list(rng)))
    for i in rng:
        k1 = k[2 * i + 1] if order else k[2 * i]
        k2 = k[2 * i] if order else k[2 * i + 1]
        if not inverse:
            r1 = ((R if rot_src else L) >> 7) & 0x1F
            L = ror((L - k1) & M32 if sub_first else (L + k1) & M32, r1) ^ R
            r2 = ((L if rot_src else R) >> 4) & 0x1F
            R = ror((R + k2) & M32 if sub_first else (R - k2) & M32, r2) ^ L
        else:
            r2 = ((L if rot_src else R) >> 4) & 0x1F
            R = (rol(R ^ L, r2) - k2) & M32 if sub_first else (rol(R ^ L, r2) + k2) & M32
            r1 = ((R if rot_src else L) >> 7) & 0x1F
            L = (rol(L ^ R, r1) + k1) & M32 if sub_first else (rol(L ^ R, r1) - k1) & M32
    return L, R


CASES = [
    # base 128
    (0x5EB5B48C, 0x5444A619, 'a4b5fddc9fad9a20', '2813b48a265d865f',
     '27048a4054a3a977', '48598a3853597026'),
    (0xAC2FD990, 0xE243EC89, 'f240025fec6e5dcf', '53139759868ba29b',
     '583bb162ca5a64f8', '86a88bc28670c2b4'),
]


def h2(s):
    b = bytes.fromhex(s)
    return struct.unpack('<II', b)


found = []
for acc, D, c0, p0, c1, p1 in CASES:
    C0 = h2(c0)
    P0 = h2(p0)
    C1 = h2(c1)
    P1 = h2(p1)
    for xor1, prog, order, ascend, sub_first, rot_src, inverse in itertools.product(
            (0, 1), (0, 1), (0, 1), (0, 1), (0, 1), (0, 1), (0, 1)):
        k = sched(D, acc, xor1, prog)
        for iv in ('c0', 'none', 'p0'):
            L, R = soft(C1[0], C1[1], k, order, ascend, sub_first, rot_src, inverse)
            if iv == 'c0':
                L ^= C0[0]
                R ^= C0[1]
            elif iv == 'p0':
                L ^= P0[0]
                R ^= P0[1]
            if (L, R) == P1:
                found.append((acc, xor1, prog, order, ascend, sub_first, rot_src,
                              inverse, iv))
print('variants that reproduce block 1: %d' % len(found))
for f in found[:10]:
    print('   acc=%08X xor1=%d prog=%d order=%d asc=%d sub=%d rotsrc=%d inv=%d iv=%s'
          % f)
