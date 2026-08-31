"""HASP4 data transform, transliterated from batteryshark/dongle-lab
projects/io.hasp4/src/hasp4_core.c (HaspDecodeBlock / HaspTransformWord)."""
M32 = 0xFFFFFFFF
POLY = 0x80500062
CA, CB = 0x5B2C004A, 0x803425C3


def rol(v, k):
    k &= 31
    return v if not k else ((v << k) | (v >> (32 - k))) & M32


def sec(i, t, wide=False):
    idx = (i >> 2) if wide else ((i >> 2) & 0x0E)
    return (t[idx] >> ((31 - i) & 7)) & 1


def transform_word(data, table, password, wide=False):
    """the keyed round: 39 LFSR steps driven by a byte -> bit oracle"""
    lf = 31
    pw = (password ^ 0x01081989) & M32
    pw >>= 12
    for i in range(10, 5, -1):
        t = pw & 0x0F
        lf |= (1 if (t != 0 and t < 0x0B) else 0) << i
        pw >>= 4
    first5 = lf >> 6
    cur = ((first5 << 6) | 31) & M32
    index = 0
    for _ in range(39):
        b = (data >> (8 * index)) & 0xFF
        i5 = b & 0x1F
        st = sec(i5, table, wide)
        b0 = i5 ^ ((st ^ 1) & (i5 >> 3)) ^ (i5 >> 4)
        b0 ^= cur >> 10
        b0 ^= cur >> 7
        if i5 & 2:
            b0 ^= cur >> 5
        if i5 & 4:
            b0 ^= cur >> 8
        bit = b0 & 1
        cur ^= (i5 & 1) << 2
        cur = ((cur << 1) | bit) & M32
        bit = ((cur >> 11) ^ st) & 1
        index = ((data & 1) << 1) | bit
        data = (data >> 1) if ((data & 1) == bit) else ((data >> 1) ^ POLY)
    return data & M32


def decode_block(blk, table, password, wide=False):
    b0 = int.from_bytes(blk[0:4], 'little')
    b1 = int.from_bytes(blk[4:8], 'little')
    for s in range(25, -1, -5):
        b0, b1 = (rol(b0 ^ CA, s) ^ b1), b0
    tmp = b0
    b0 = transform_word(b0, table, password, wide)
    b0 ^= b1
    b1 = tmp
    for s in range(10, -1, -2):
        b0, b1 = (rol(b0 ^ CB, s) ^ b1), b0
    tmp = b0
    b0 = transform_word(b0, table, password, wide)
    b0 ^= b1
    b1 = tmp
    return b0.to_bytes(4, 'little') + b1.to_bytes(4, 'little')


def decode(buf, table, password, wide=False):
    out = bytearray()
    for o in range(0, len(buf) - 7, 8):
        out += decode_block(buf[o:o + 8], table, password, wide)
    return bytes(out)


def encode_block(blk, table, password, wide=False):
    b0 = int.from_bytes(blk[0:4], 'little')
    b1 = int.from_bytes(blk[4:8], 'little')
    tmp = b1
    b1 = transform_word(b1, table, password, wide)
    b1 ^= b0
    b0 = tmp
    for s in range(0, 11, 2):
        b0, b1 = b1, (rol(b1 ^ CB, s) ^ b0)
    tmp = b1
    b1 = transform_word(b1, table, password, wide)
    b1 ^= b0
    b0 = tmp
    for s in range(0, 26, 5):
        b0, b1 = b1, (rol(b1 ^ CA, s) ^ b0)
    return b0.to_bytes(4, 'little') + b1.to_bytes(4, 'little')
