"""The dongle in software: fit a generation's key, then answer its queries offline.

`dongcap` exists because the picture cipher asks a physical HASP for two dwords per
4 KB buffer.  This does the same job with no part attached, and it is not a lookup
table -- it answers any query, including ones no capture covered.

Two things make that possible.

**The round leaks its own answers.**  The keyed round is an LFSR whose only
non-linearity is the part, and the part enters solely as the shift decision:

    c_j = p_(j-1) XOR (v_(j-1) & 1)      v_j = (v_(j-1) >> 1) XOR (c_j ? POLY : 0)

so v_39 = XOR_j c_j * (POLY >> (39-j)).  POLY's top bit is set, which makes that system
triangular in c_8..c_39: thirty-two of the thirty-nine decisions are *forced by the
output*.  The first seven fall off the end of the register and are enumerated -- 128
candidate paths per (input, output) pair.

**The oracle is a small state machine.**  `io.hasp4` (`batteryshark/dongle-lab`, the
public project of `docs/research/23`) models it as a shift register `cur` seeded from the
password and a 32-bit key selected by the low five bits of the offered byte -- five bits
being all the wire carries, since the query framing drops three.  Its answer is
`((cur >> 11) ^ key_bit) & 1`, and the bit shifted in that step does not reach bit 11, so
bit 11 is known *before* the key bit is.  Every answer therefore names its key bit
outright, and a pair either agrees with the others or kills the hypothesis.

Nine pairs settle the key.  The inputs come from any block whose plaintext is known, so a
release with a plaintext twin in another year needs no hardware at all:

    I.G.O. 2  ciphertext, I.G.O. 4 plaintext -> 3B227944, seeded 0x7DF from 132968BB
    I.G.O. 3  ciphertext, I.G.O. 4 plaintext -> AB32E970, seeded 0x5DF from 24A36B91

I.G.O. 2's key was fitted from I.G.O. 4 alone and then checked against all 46,036 rounds
the real part answered in `capture-igo2/DONGCAP.BIN`: every one agrees.  That capture is
now a regression test rather than a dependency.

Photo Play 2001 is NOT this cipher.  Its `7477/7D57` archives admit no key under any of
the thirty-two initial states the model allows, at any block of any entry, while I.G.O. 2
and 3 lock onto one state on every pair tested.  See `docs/research/31`.

    python softpart.py verify                       -- against the real capture
    python softpart.py fit igo3 <cipher.img> <plain.img>
    python softpart.py decrypt igo3 <img> <archive path>
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                os.pardir, os.pardir, 'docs', 'research', 'evidence'))
import wad
from mklist import solve_from_plain, BUF

M32 = 0xFFFFFFFF
POLY = 0x80500062
CA, CB = 0x5B2C004A, 0x803425C3

# the password is pushed as pass2:pass1 -- FINDIT.EXE pushes 0x132968BB for 68BB/1329
GENERATIONS = {
    'igo2': dict(password=0x132968BB, key=0x3B227944),
    'igo3': dict(password=0x24A36B91, key=0xAB32E970),
    'igo5': dict(password=0x24A36B91, key=0xAB32E970),   # same pair as I.G.O. 3
}


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


def initial_state(password):
    """the part's register as the password leaves it -- only 32 values are reachable"""
    lf = 31
    p = ((password ^ 0x01081989) & M32) >> 12
    for i in range(10, 5, -1):
        nib = p & 0x0F
        lf |= (1 if (nib != 0 and nib < 0x0B) else 0) << i
        p >>= 4
    return ((lf >> 6) << 6) | 31


def keyed_round(data, key, cur):
    """what the part returns for `data` -- the whole reason the hardware was needed"""
    index = 0
    for _ in range(39):
        i5 = (data >> (8 * index)) & 0x1F
        st = (key >> i5) & 1
        b0 = i5 ^ ((st ^ 1) & (i5 >> 3)) ^ (i5 >> 4)
        b0 ^= cur >> 10
        b0 ^= cur >> 7
        if i5 & 2:
            b0 ^= cur >> 5
        if i5 & 4:
            b0 ^= cur >> 8
        cur = (((cur ^ ((i5 & 1) << 2)) << 1) | (b0 & 1)) & M32
        ans = ((cur >> 11) ^ st) & 1
        index = ((data & 1) << 1) | ans
        data = (data >> 1) if ((data & 1) == ans) else ((data >> 1) ^ POLY)
    return data & M32


def decode_block(c0, c1, key, cur):
    """one 8-byte block through the part -- the only place hardware was ever used"""
    b0, b1 = a_rounds(c0, c1)
    tmp = b0
    b0 = keyed_round(b0, key, cur) ^ b1
    b1 = tmp
    b0, b1 = b_rounds(b0, b1)
    tmp = b0
    b0 = keyed_round(b0, key, cur) ^ b1
    return b0, tmp


# ------------------------------------------------------------------ fitting

def _forced(vout):
    """decisions c_8..c_39, back-substituted from the round's output"""
    c, acc = {}, 0
    for b in range(31, -1, -1):
        if ((vout ^ acc) >> b) & 1:
            c[8 + b] = 1
            acc ^= (POLY >> (31 - b)) & M32
        else:
            c[8 + b] = 0
    return c


def keys_allowed_by(v0, vout, cur0):
    """every partial key consistent with this one (input, output) pair"""
    base = _forced(vout)
    out = []
    for free in range(128):
        c = dict(base)
        for k in range(1, 8):
            c[k] = (free >> (k - 1)) & 1
        data, cur, index, key, ok = v0, cur0, 0, {}, True
        for it in range(39):
            i5 = (data >> (8 * index)) & 0x1F
            ans = c[it + 1] ^ (data & 1)
            pre = (cur ^ ((i5 & 1) << 2)) & M32
            st = ans ^ ((pre >> 10) & 1)
            if key.setdefault(i5, st) != st:
                ok = False
                break
            b0 = i5 ^ ((st ^ 1) & (i5 >> 3)) ^ (i5 >> 4)
            b0 ^= cur >> 10
            b0 ^= cur >> 7
            if i5 & 2:
                b0 ^= cur >> 5
            if i5 & 4:
                b0 ^= cur >> 8
            cur = ((pre << 1) | (b0 & 1)) & M32
            index = ((data & 1) << 1) | ans
            data = (data >> 1) if ((data & 1) == ans) else ((data >> 1) ^ POLY)
        if ok and data == vout:
            out.append(key)
    return out


def fit_key(pairs, cur0):
    """intersect the hypotheses until one 32-bit key is left"""
    live = None
    for n, (v0, vout) in enumerate(pairs):
        cands = keys_allowed_by(v0, vout, cur0)
        if not cands:
            return None, 'pair %d admits no key -- wrong plaintext, or not this cipher' % n
        if live is None:
            live = cands
        else:
            merged, seen = [], set()
            for m in live:
                for c in cands:
                    if any(c[k] != v for k, v in m.items() if k in c):
                        continue
                    d = dict(m)
                    d.update(c)
                    t = tuple(sorted(d.items()))
                    if t not in seen:
                        seen.add(t)
                        merged.append(d)
            if not merged:
                return None, 'pair %d contradicts every surviving key' % n
            live = merged
        full = {tuple(sorted(m.items())) for m in live if len(m) == 32}
        if len(full) == 1 and n >= 8:
            k = dict(full.pop())
            return sum(k[i] << i for i in range(32)), 'settled after %d pairs' % (n + 1)
    return None, 'did not settle (%d hypotheses left)' % len(live or [])


def pairs_from_twin(cipher_img, plain_img, archive, limit=40):
    """(input, output) pairs from an archive whose plaintext ships in another release"""
    dc = wad.read(cipher_img, archive)
    dp = wad.read(plain_img, archive)
    if not dc or not dp:
        return []
    ec, ep = wad.entries(dc), wad.entries(dp)
    plain = {n: (o, s) for n, o, s in ep}
    out = []
    for name, off, size in ec:
        if name not in plain or plain[name][1] != size:
            continue
        po = plain[name][0]
        for b in range(0, size, BUF):
            if size - b < 64:
                continue
            c0, c1 = struct.unpack_from('<II', dc, off + b)
            p0, p1 = struct.unpack_from('<II', dp, po + b)
            r = solve_from_plain(c0, c1, p0, p1)
            if r:
                out.append((r[0], r[1]))
                out.append((r[2], r[3]))
        if len(out) >= limit:
            break
    return out


# --------------------------------------------------------------------- cli

def cmd_verify():
    here = os.path.dirname(os.path.abspath(__file__))
    lst = open(os.path.join(here, 'capture-igo2', 'DONGCAP.LST'), 'rb').read()
    bn = open(os.path.join(here, 'capture-igo2', 'DONGCAP.BIN'), 'rb').read()
    _, ncal, count, _nenc = struct.unpack_from('<4I', lst, 0)
    w = struct.unpack_from('<%dI' % ((len(lst) - 16) // 4), lst, 16)
    work = w[ncal * 2: ncal * 2 + count * 2]
    f = struct.unpack_from('<%dI' % ((len(bn) - 16) // 4), bn, 16)
    g = GENERATIONS['igo2']
    cur = initial_state(g['password'])
    bad = n = 0
    for i in range(count):
        L1, R1 = work[i * 2], work[i * 2 + 1]
        f1 = f[i * 2]
        n += 1
        if keyed_round(L1, g['key'], cur) != f1:
            bad += 1
        L3, _ = b_rounds((f1 ^ R1) & M32, L1)
        n += 1
        if keyed_round(L3, g['key'], cur) != f[i * 2 + 1]:
            bad += 1
    print('I.G.O. 2 software part against the real one: %d of %d rounds agree, %d wrong'
          % (n - bad, n, bad))
    return 1 if bad else 0


def cmd_fit(gen, cipher_img, plain_img):
    if gen not in GENERATIONS:
        raise SystemExit('unknown generation %r' % gen)
    cur0 = initial_state(GENERATIONS[gen]['password'])
    for a in ('/FINDIT/PICS/FOTOPLAY.WAD', '/AMORE/COMIX/FOTOPLAY.WAD'):
        pairs = pairs_from_twin(cipher_img, plain_img, a)
        if not pairs:
            print('%-28s no entry matches by name and size' % a)
            continue
        key, why = fit_key(pairs, cur0)
        if key is None:
            print('%-28s %s' % (a, why))
            continue
        bad = sum(1 for v, o in pairs if keyed_round(v, key, cur0) != o)
        print('%-28s key %08X, state 0x%03X (%s); %d pairs re-checked, %d wrong'
              % (a, key, cur0, why, len(pairs), bad))


def cmd_decrypt(gen, img, archive):
    g = GENERATIONS[gen]
    cur = initial_state(g['password'])
    d = wad.read(img, archive)
    if not d:
        raise SystemExit('%s not found in %s' % (archive, img))
    es = wad.entries(d)
    good = 0
    for _name, off, _size in es:
        c0, c1 = struct.unpack_from('<II', d, off)
        p0, p1 = decode_block(c0, c1, g['key'], cur)
        head = struct.pack('<II', p0, p1)
        if head[:6] in (b'GIF87a', b'GIF89a') or head[:2] == b'\x0a\x05':
            good += 1
    print('%s: %d of %d entries decrypt to a picture header' % (archive, good, len(es)))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    cmd = sys.argv[1]
    if cmd == 'verify':
        sys.exit(cmd_verify())
    elif cmd == 'fit' and len(sys.argv) == 5:
        cmd_fit(sys.argv[2].lower(), sys.argv[3], sys.argv[4])
    elif cmd == 'decrypt' and len(sys.argv) == 5:
        cmd_decrypt(sys.argv[2].lower(), sys.argv[3], sys.argv[4])
    else:
        raise SystemExit(__doc__)


if __name__ == '__main__':
    main()
