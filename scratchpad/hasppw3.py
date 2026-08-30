"""Extract the HASP password pair from a Photo Play / I.G.O. executable.

Anchor: every build links the same marshalling function, which stamps the signature
"lhsh" into its request struct as two word stores

    C7 46 ?? 73 68 / C7 46 ?? 6C 68

Its file offset converts to what a call would target (file = seg*16 + hdrsize + off
for a far call; site+3+rel for a near one), so the callers can be found exactly.

Some builds call it directly from game code, and some put a forwarding wrapper in
between that just re-pushes its arguments.  So the search runs two levels: callers
of the marshaller, and callers of whatever function those calls sit in.  At a real
call site the passwords are immediates -- one dword push (`66 68 lo lo hi hi`), two
word pushes (`B8 imm / 50`), or two stores into locals (`C7 46 xx imm`).
"""
import collections
import re
import struct
import sys

RET = (0xCB, 0xC3)


def hasp_entry(d):
    for pat in (rb'\xc7\x46.\x73\x68\xc7\x46.\x6c\x68',
                rb'\xc7\x46.\x6c\x68\xc7\x46.\x73\x68'):
        m = re.search(pat, d, re.S)
        if m:
            return m.start()
    return None


def calls_to(d, lo, hi, hdr):
    out = []
    for m in re.finditer(rb'\x9a(....)', d, re.S):
        off, seg = struct.unpack('<HH', m.group(1))
        if lo <= seg * 16 + hdr + off <= hi:
            out.append(m.start())
    for m in re.finditer(rb'\xe8(..)', d, re.S):
        rel = struct.unpack('<h', m.group(1))[0]
        if lo <= m.start() + 3 + rel <= hi:
            out.append(m.start())
    return out


def func_start(d, pos, back=900):
    """nearest plausible prologue before pos: enter, or push bp/mov bp,sp"""
    lo = max(0, pos - back)
    best = None
    for m in re.finditer(rb'\x55\x8b\xec|\xc8..\x00', d[lo:pos], re.S):
        cand = lo + m.start()
        if cand > 0 and d[cand - 1] in RET or cand == lo:
            best = cand
        elif best is None:
            best = cand
        else:
            best = cand
    return best


def votes_at(d, sites, window=140):
    v = collections.Counter()
    for s in sites:
        win = d[max(0, s - window):s]
        for m in re.finditer(rb'\x66\x68(....)', win, re.S):
            lo, hi = struct.unpack('<HH', m.group(1))
            if lo and hi:
                v[(lo, hi)] += 3          # unambiguous: both passwords, in order
        imms = [struct.unpack_from('<H', win, m.start() + 1)[0]
                for m in re.finditer(rb'\xb8..\x50', win, re.S)]
        for i in range(len(imms) - 1):
            if imms[i] and imms[i + 1]:
                v[(imms[i + 1], imms[i])] += 1
        # mov word [bp-2],pass1 / mov word [bp-4],pass2 -- IGO 7 at 0x2C117 and the
        # 2001 games both store pass1 FIRST, so file order is (pass1, pass2) here.
        # (The push-pair above is the opposite way round because pushes are reversed.)
        st = [struct.unpack_from('<H', win, m.start() + 3)[0]
              for m in re.finditer(rb'\xc7\x46...', win, re.S)]
        for i in range(len(st) - 1):
            if st[i] and st[i + 1] and st[i] != st[i + 1]:
                v[(st[i], st[i + 1])] += 1
    return v


def passwords(d):
    hdr = struct.unpack_from('<H', d, 8)[0] * 16
    ent = hasp_entry(d)
    if ent is None:
        return None, 'no lhsh signature'
    sites = calls_to(d, ent - 48, ent, hdr)
    if not sites:
        return None, 'entry at %06X, no calls to it' % ent

    v = votes_at(d, sites)
    best = [p for p in v.most_common(3) if p[1] >= 3]
    if best:
        return best[0][0], 'direct, %d sites, score %d' % (len(sites), best[0][1])

    # one level up: whoever calls the function these calls live in
    outer = set()
    for s in sites:
        f = func_start(d, s)
        if f is not None:
            outer.update(calls_to(d, f, f + 4, hdr))
    if not outer:
        return None, '%d sites, no outer callers' % len(sites)
    v = votes_at(d, sorted(outer))
    if not v:
        return None, '%d sites, %d outer, no immediates' % (len(sites), len(outer))
    (p, n) = v.most_common(1)[0]
    return p, 'via wrapper, %d outer sites, score %d' % (len(outer), n)


if __name__ == '__main__':
    for path in sys.argv[1:]:
        d = open(path, 'rb').read()
        pw, why = passwords(d)
        print('%-26s %-14s %s' % (path.replace('\\', '/').rsplit('/', 1)[-1],
                                  ('%04X / %04X' % pw) if pw else '--', why))
