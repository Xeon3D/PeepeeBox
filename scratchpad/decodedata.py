"""HASP4 DecodeData (service 0x3D) over a buffer, all transform modes.

I.G.O. 2's FINDIT.EXE calls the library like this, at five sites:

    lcall 0,0x5FE2                    read 4096 bytes
    cmp  [bp-8], 8 ; jbe skip         buffers of 8 or less are not transformed
    push &p4 &p3 &p2 &p1
    push 0x132968BB                   pass2:pass1 = 1329:68BB
    push [0x1704]                     lptnum
    push [bp-0x12]                    seed
    push 0x3D                         DecodeData
    lcall 0x30A1:0x000B

and indexes its buffers with `shl ax, 0x0C`, so the unit is 4 KB = 512 blocks.
The transform therefore restarts every 4096 bytes, which is why block 0 of every
entry decodes the same way.

io.hasp4 makes the service's behaviour a per-dongle mode 0..18 selecting the
operation, whether blocks chain, whether the tail state is kept, and which form of
the password is used.  That is the space swept here.
"""
import struct

M32 = 0xFFFFFFFF
POLY = 0x80500062
CA, CB = 0x5B2C004A, 0x803425C3


def rol(v, s):
    s &= 31
    return v if not s else ((v << s) | (v >> (32 - s))) & M32


def op_of(mode):
    if mode in (1, 4, 7, 12, 13, 14, 15, 18):
        return 1                     # decode
    if mode in (2, 5, 6, 8, 9, 10, 11):
        return 2                     # encode
    return 0


def chained(mode):
    return mode in (6, 7, 10, 11, 14, 15, 18)


def tail_state(mode):
    return mode == 18


def pw_of(mode, pw1, pw2, device_pw):
    if mode in (8, 10, 12, 14):
        return ((pw1 << 16) | pw2) & M32
    if mode in (9, 11, 13, 15):
        return ((pw2 << 16) | pw1) & M32
    return device_pw                 # HaspResolveTransformPassword's fallback


def sec(i, t, idx):
    if idx == 0:
        return (t[(i >> 2) & 0x0e] >> ((31 - i) & 7)) & 1
    if idx == 1:
        return (t[(i >> 2) & 7] >> ((31 - i) & 7)) & 1
    if idx == 2:
        return (t[i >> 3] >> (7 - (i & 7))) & 1
    return (t[i >> 3] >> (i & 7)) & 1


def transform_word(data, tab, password, idx):
    lf = 31
    pw = (password ^ 0x01081989) & M32
    pw >>= 12
    for i in range(10, 5, -1):
        t = pw & 0x0F
        lf |= (1 if (t != 0 and t < 0x0B) else 0) << i
        pw >>= 4
    cur = (((lf >> 6) << 6) | 31) & M32
    index = 0
    for _ in range(39):
        b = (data >> (8 * index)) & 0xFF
        i5 = b & 0x1F
        st = sec(i5, tab, idx)
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


def decode_block(buf, off, nxt, tab, pw, idx):
    b0, b1 = struct.unpack_from('<II', buf, off)
    for s in range(25, -1, -5):
        b0, b1 = (rol(b0 ^ CA, s) ^ b1) & M32, b0
    tmp = b0
    b0 = transform_word(b0, tab, pw, idx)
    if nxt is not None:
        struct.pack_into('<I', buf, nxt + 4, b0)
    b0 ^= b1
    b1 = tmp
    for s in range(10, -1, -2):
        b0, b1 = (rol(b0 ^ CB, s) ^ b1) & M32, b0
    tmp = b0
    b0 = transform_word(b0, tab, pw, idx)
    if nxt is not None:
        struct.pack_into('<I', buf, nxt, b0)
    b0 ^= b1
    b1 = tmp
    struct.pack_into('<II', buf, off, b0, b1)


def encode_block(buf, off, nxt, tab, pw, idx):
    b0, b1 = struct.unpack_from('<II', buf, off)
    tmp = b1
    b1 = transform_word(b1, tab, pw, idx)
    if nxt is not None:
        struct.pack_into('<I', buf, nxt, b1)
    b1 ^= b0
    b0 = tmp
    for s in range(0, 11, 2):
        b0, b1 = b1, (rol(b1 ^ CB, s) ^ b0) & M32
    tmp = b1
    b1 = transform_word(b1, tab, pw, idx)
    if nxt is not None:
        struct.pack_into('<I', buf, nxt + 4, b1)
    b1 ^= b0
    b0 = tmp
    for s in range(0, 26, 5):
        b0, b1 = b1, (rol(b1 ^ CA, s) ^ b0) & M32
    struct.pack_into('<II', buf, off, b0, b1)


def transform_buffer(data, mode, tab, pw1, pw2, device_pw, idx):
    """HaspTransformBlocks over one buffer; returns a new bytes object"""
    op = op_of(mode)
    if not op:
        return bytes(data)
    buf = bytearray(data)
    n = len(buf) // 8
    pw = pw_of(mode, pw1, pw2, device_pw)
    ch, tl = chained(mode), tail_state(mode)
    for i in range(n):
        nxt = (i + 1) * 8 if (ch and i + 1 < n) else None
        if op == 1:
            decode_block(buf, i * 8, nxt, tab, pw, idx)
        else:
            encode_block(buf, i * 8, nxt, tab, pw, idx)
        if tl and nxt is not None and i + 2 == n:
            break
    return bytes(buf)
