"""The software round of the I.G.O. 2 picture cipher, transcribed from FINDIT.EXE.

Only the FIRST block of each 4 KB buffer goes to the dongle.  That call returns two
dwords -- call them D and acc -- and every later block in the buffer is decoded by this
routine, which is pure arithmetic.  So two dwords per 4096 bytes are all the hardware
ever contributes.

Sources, all from `EXE\\FINDIT.EXE` of `F:\\HDDImages\\IGO2\\IGO 2 BE 82C81`
(235,090 bytes, MZ header 0x4800):

    0x34CC0   the DecodeData buffer walker: CBC, zero IV, last two blocks untouched
    0x34D8F   call 0x34629 -> the dongle path for block 0, returns D and acc
    0x34DA3   the schedule build
    0x3483D   this routine
"""

M32 = 0xFFFFFFFF


def ror32(v, s):
    s &= 31
    return v if not s else (((v >> s) | (v << (32 - s))) & M32)


def schedule(D, acc):
    """0x34DA3.

        sched[0] = D
        for j in 1..25:
            sched[j]  = sched[j-1] + acc
            sched[1] ^= ROR32(D, sched[j-1] & 31)

    D is re-read from its slot every iteration, so it is NOT folded cumulatively --
    the rotate is always of the original D.  sched[1] is updated in place after
    sched[j] is computed, so iterations from j = 2 on read the already-updated value.
    """
    k = [0] * 26
    k[0] = D & M32
    for j in range(1, 26):
        prev = k[j - 1]
        k[j] = (prev + acc) & M32
        k[1] ^= ror32(D & M32, prev & 31)
        k[1] &= M32
    return k


def soft_decode(L, R, k):
    """0x3483D -- twelve iterations of two half-rounds, then the whitening.

        di = 12
        do {
            L = ROR32(L - k[2i+1], (R >> 7) & 31) ^ R
            R = ROR32(R + k[2i],   (L >> 4) & 31) ^ L
        } while (--di > 0)
        L -= k[1]
        R -= k[0]

    `or di,di / jbe` exits when di reaches zero, so the body runs for di = 12 down to 1
    and touches schedule entries 25,24 down to 3,2.  Note the asymmetry that makes the
    round non-involutive: the L half SUBTRACTS its key and rotates by a field of the old
    R, the R half ADDS its key and rotates by a field of the NEW L.
    """
    for i in range(12, 0, -1):
        L = ror32((L - k[2 * i + 1]) & M32, (R >> 7) & 31) ^ R
        R = ror32((R + k[2 * i]) & M32, (L >> 4) & 31) ^ L
    L = (L - k[1]) & M32
    R = (R - k[0]) & M32
    return L, R


def soft_encode(L, R, k):
    """the inverse, for checking soft_decode round-trips"""
    L = (L + k[1]) & M32
    R = (R + k[0]) & M32
    for i in range(1, 13):
        # undo the R half first: it was computed from the new L
        R = ((((R ^ L) << ((L >> 4) & 31)) | ((R ^ L) >> (32 - ((L >> 4) & 31)))) & M32
             if ((L >> 4) & 31) else (R ^ L))
        R = (R - k[2 * i]) & M32
        s = (R >> 7) & 31
        L = ((((L ^ R) << s) | ((L ^ R) >> (32 - s))) & M32) if s else (L ^ R)
        L = (L + k[2 * i + 1]) & M32
    return L, R


if __name__ == '__main__':
    import random
    random.seed(3)
    ok = 0
    for _ in range(500):
        D, acc = random.getrandbits(32), random.getrandbits(32)
        k = schedule(D, acc)
        L, R = random.getrandbits(32), random.getrandbits(32)
        ok += (soft_encode(*soft_decode(L, R, k), k) == (L, R))
    print('soft_decode/soft_encode round-trip: %d/500' % ok)
    k = schedule(0x12345678, 0x9ABCDEF0)
    print('schedule[0..3] = %08X %08X %08X %08X' % tuple(k[:4]))
    print('decode(0,0)    = %08X %08X' % soft_decode(0, 0, k))
