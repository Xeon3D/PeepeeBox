"""Is the byte -> bit oracle a bitmap sitting in the dumped dongle?

A byte -> bit function is 256 bits = 32 bytes.  The 2001 dump has 719 bytes, so try
every 32-byte window in it, in both bit orders and both polarities, and run the real
LFSR with that as the oracle against the input/output pairs recovered in
scratchpad/oracle2.py.  A hit is unambiguous: the LFSR either reproduces the pair or
it does not.

f, from 0x2CD0C:
    b = oracle(V.byte[0])
    for i in 1..39:
        b = (b & 1) | ((V & 1) << 1)
        V = (V >> 1) ^ POLY  if (b ^ V) & 1  else  V >> 1
        b = oracle(V.byte[b])
"""
import sys

POLY = 0x80500062
DUMP = r'h5dmp/2001PT/hasp.dmp'


def run(v, oracle):
    b = oracle(v & 0xFF)
    for _ in range(39):
        b = (b & 1) | ((v & 1) << 1)
        v = ((v >> 1) ^ POLY) if ((b ^ v) & 1) else (v >> 1)
        b = oracle((v >> (8 * b)) & 0xFF)
    return v


def make(buf, off, msb, inv):
    tab = buf[off:off + 32]

    def oracle(byte):
        bit = (tab[byte >> 3] >> ((7 - (byte & 7)) if msb else (byte & 7))) & 1
        return bit ^ inv
    return oracle


pairs = [tuple(int(x, 16) for x in l.split()) for l in open('pairs.txt')][:400]
buf = open(DUMP, 'rb').read()
print('dump %d bytes, %d candidate windows, %d pairs to test'
      % (len(buf), len(buf) - 32, len(pairs)))

best = (0, None)
for off in range(len(buf) - 32):
    for msb in (0, 1):
        for inv in (0, 1):
            o = make(buf, off, msb, inv)
            hits = 0
            for v0, want in pairs[:12]:
                if run(v0, o) == want:
                    hits += 1
                else:
                    break
            if hits > best[0]:
                best = (hits, (off, msb, inv))
            if hits == 12:
                # confirm on the wider set
                full = sum(1 for v0, want in pairs if run(v0, o) == want)
                print('  HIT: offset %03X msb=%d inv=%d -> %d/%d pairs'
                      % (off, msb, inv, full, len(pairs)))
                sys.exit(0)
print('  no window reproduces even 12 pairs; best was %d at %s' % best)
