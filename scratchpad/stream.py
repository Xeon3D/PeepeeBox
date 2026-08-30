"""Interrogate the 2001 body keystream, recovered by known plaintext.

The 2000 image holds the same pictures with a plaintext body, so for any entry
        keystream2001 = ciphertext2001 XOR plaintext2000
is exact for the whole file.  The first 128 bytes are LCG(0x12345); this looks at
what starts at 128.
"""
import subprocess
import sys

sys.path.insert(0, r'C:\Users\xeon4\Documents\Claude\PeepeeBox-handoff\evidence\amore-pcx\derive')
import wad

A = 0x08088405
I01 = r'C:\Users\xeon4\Documents\Claude\PeepeeBox\PP2001\Photo Play 2001 DE B4821\HardDisk.img'
I00 = r'C:\Users\xeon4\Documents\Claude\PeepeeBox\PP2000\Photo Play 2000 DE 6D139\HardDisk.img'
ARCH = '/FINDIT/PICS/FOTOPLAY.WAD'
REF = r'C:\Users\xeon4\Documents\Claude\PeepeeBox-handoff\evidence\amore-pcx\findit_keys.txt'


def ks(seed, n):
    s = seed & 0xFFFFFFFF
    o = bytearray()
    for _ in range(n):
        s = (s * A + 1) & 0xFFFFFFFF
        o.append(s >> 24)
    return bytes(o)


d1 = wad.read(I01, ARCH)
d0 = wad.read(I00, ARCH)
e1 = {n: (o, s) for n, o, s in wad.entries(d1)}
ents0 = wad.entries(d0)
ref = dict(l.split() for l in open(REF) if len(l.split()) == 2)

streams = {}
for n, o0, s in ents0[:6]:
    if n not in ref or n not in e1:
        continue
    p = bytearray(d0[o0:o0 + s])
    k = ks(int(ref[n], 16), 128)
    for i in range(128):
        p[i] ^= k[i]
    o1, _ = e1[n]
    streams[n] = (bytes(x ^ y for x, y in zip(d1[o1:o1 + s], p)), int(ref[n], 16), s)

names = list(streams)
print('recovered keystreams for', len(names), 'pictures\n')

a, b = names[0], names[1]
sa, sb = streams[a][0], streams[b][0]
common = min(len(sa), len(sb))
eq = sum(1 for i in range(128, common) if sa[i] == sb[i])
print('1. is the body stream the same for every picture?')
print('   %s vs %s: %d/%d bytes equal from 128 (chance would be ~%d)'
      % (a, b, eq, common - 128, (common - 128) // 256))
print('   %s[128:144] %s' % (a, sa[128:144].hex(' ')))
print('   %s[128:144] %s' % (b, sb[128:144].hex(' ')))

print('\n2. does the body stream repeat within one picture?')
s = sa
for period in (128, 256, 512, 1024, 2048, 4096, 8192):
    hit = sum(1 for i in range(128, min(len(s), 20000)) if s[i] == s[i + period]) \
        if len(s) > 20000 + period else 0
    print('   period %-5d: %d/%d match' % (period, hit, min(len(s), 20000) - 128))

print('\n3. is it the same LCG with a different byte taken, or a skipped start?')
for name in names[:2]:
    st, key, size = streams[name]
    want = st[128:136]
    for tag, shift in (('>>24', 24), ('>>16', 16), ('>>8', 8), ('>>0', 0)):
        for seed_tag, seed in (('per-name key', key), ('default', 0x12345)):
            x = seed & 0xFFFFFFFF
            got = bytearray()
            for _ in range(8):
                x = (x * A + 1) & 0xFFFFFFFF
                got.append((x >> shift) & 0xFF)
            if bytes(got) == want:
                print('   %s: MATCH  %s %s' % (name, seed_tag, tag))
    print('   %s body[128:136] = %s (per-name key %08X)' % (name, want.hex(' '), key))

print('\n4. byte distribution of the body stream (uniform => a real cipher)')
import collections
c = collections.Counter(sa[128:])
print('   distinct values %d, most common %s' % (len(c), c.most_common(3)))
