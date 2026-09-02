"""ppictfix -- restore FIND IT's photographs on an I.G.O. 2 image, with no dongle.

Why this works
--------------
I.G.O. 2, 3 and 4 ship `FINDIT/PICS/FOTOPLAY.WAD` at the same 72,373,757 bytes with the
same 1397 entries at the same offsets and sizes -- the directories are byte-identical.
I.G.O. 4 is a CDONGLE release and ships them **in the clear** ("GIF87a"), while 2 and 3
ship them enciphered with the HASP data cipher, which needs the dongle.

So the plaintext already exists, in a release we can read.  This copies it in and stops
the game trying to decipher what is already plain.

What it changes
---------------
1. `FINDIT/PICS/FOTOPLAY.WAD` is overwritten with I.G.O. 4's copy.  Identical length,
   so the cluster chain and directory are untouched.
2. `EXE/FINDIT.EXE` -- at each of the five DecodeData call sites the guard
   `cmp word [bp-XX], 8 ; jbe skip` is turned into an unconditional `jmp skip`, one byte
   each (`0x76` -> `0xEB`).  The buffer is then never handed to the cipher.

That second step cannot make anything worse: those calls already fail, because the
device cannot answer them.  Files that the game does *not* decipher never reach the
guard and are unaffected.

What it does NOT fix, and why
-----------------------------
* **AMORE's comics.**  `AMORE/COMIX/FOTOPLAY.WAD` is enciphered too, but no release from
  I.G.O. 4 onwards ships it -- they all moved to AMORE2, whose archive is a different
  666 KB file.  There is no plaintext to copy, so AMORE's photo comics stay blank.
* **I.G.O. 3.**  The data swap would work -- its archive is directory-identical too -- but
  its `FINDIT.EXE` is **packed** (entropy 7.99 against 6.9, a 32-byte MZ header, a third
  of the strings), so the five guards are not in the file to patch.  The tool detects
  this and refuses rather than half-applying.  Unpacking it first would make I.G.O. 3
  work the same way.
* **Photo Play 2001.**  Its archives are PCX, not GIF, and no later release carries them;
  the 2000 image has the same pictures but its directory differs, so it is not a
  drop-in.  Out of scope here.

Usage
-----
    python ppictfix.py --check  <target.img>
    python ppictfix.py --source <IGO4 HardDisk.img> <target.img>

Work on a COPY.  The tool writes in place and says so.
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fatimg import Fat

WAD = '/FINDIT/PICS/FOTOPLAY.WAD'
EXE = '/EXE/FINDIT.EXE'
GIF = b'GIF8'


def wad_entries(data):
    """the GWAD directory; the header and entries are XOR 0x55, the bodies are not"""
    d = bytes(b ^ 0x55 for b in data[:8])
    if d[:4] != b'GWAD':
        return None
    n = int.from_bytes(d[4:8], 'little')
    out = []
    for i in range(n):
        p = 8 + i * 21
        raw = bytes(b ^ 0x55 for b in data[p:p + 21])
        if len(raw) < 21:
            break
        out.append((raw[:13].split(b'\0')[0].decode('latin-1'),
                    int.from_bytes(raw[13:17], 'little'),
                    int.from_bytes(raw[17:21], 'little')))
    return out


def find_guards(exe):
    """the five 'cmp word [bp-XX],8 ; jbe' guards in front of each DecodeData call"""
    out = []
    for m in re.finditer(rb'\x6a\x3d\x9a', exe):
        s = m.start()
        for back in range(6, 80):
            o = s - back
            if o < 0:
                break
            # 0x76 is the original jbe, 0xEB one we have already turned into a jmp
            if (exe[o] == 0x83 and exe[o + 1] == 0x7E and exe[o + 3] == 0x08
                    and exe[o + 4] in (0x76, 0xEB)):
                out.append(o + 4)
                break
    return out


def looks_packed(exe):
    import collections
    import math
    c = collections.Counter(exe)
    n = len(exe)
    e = -sum((x / n) * math.log2(x / n) for x in c.values())
    return e > 7.5


def report(target, source=None, apply_fix=False):
    t = Fat(target, write=apply_fix)
    try:
        hit = t.find(WAD)
        if hit is None:
            print('  %s: not found -- is this an I.G.O. image?' % WAD)
            return 2
        print('  %-28s %d bytes' % (WAD, hit[2]))
        wad = t.read(WAD)
        ents = wad_entries(wad)
        if not ents:
            print('  archive is not a GWAD')
            return 2
        n, off, sz = ents[0]
        plain = wad[off:off + 4] == GIF
        print('  first entry %-14s %s' % (n, 'PLAINTEXT already' if plain else
                                          'enciphered (%s)' % wad[off:off + 8].hex(' ')))

        exe = t.read(EXE)
        if exe is None:
            print('  %s: not found' % EXE)
            return 2
        if looks_packed(exe):
            print('  %s is PACKED -- the guards are not in the file.' % EXE)
            print('  This is I.G.O. 3\'s case; unpack it first. Refusing to touch the image.')
            return 3
        guards = find_guards(exe)
        done = sum(1 for g in guards if exe[g] == 0xEB)
        print('  %-28s %d DecodeData guards, %d already patched'
              % (EXE, len(guards), done))

        if not apply_fix:
            if plain and done == len(guards) and guards:
                print('  -> already fixed')
            else:
                print('  -> fixable' if guards else '  -> no guards found, cannot patch')
            return 0

        if not guards:
            print('  no guards found; refusing to write')
            return 3

        s = Fat(source)
        try:
            src = s.read(WAD)
        finally:
            s.close()
        if src is None:
            print('  source image has no %s' % WAD)
            return 2
        if len(src) != len(wad):
            print('  source archive is %d bytes, target %d -- not a drop-in'
                  % (len(src), len(wad)))
            return 2
        se = wad_entries(src)
        if se != ents:
            print('  source and target directories differ -- not a drop-in')
            return 2
        if src[se[0][1]:se[0][1] + 4] != GIF:
            print('  source archive is not plaintext either')
            return 2

        print('  writing %d bytes of plaintext into %s ...' % (len(src), WAD))
        t.overwrite(WAD, src)
        patched = bytearray(exe)
        for g in guards:
            patched[g] = 0xEB
        t.overwrite(EXE, bytes(patched))
        print('  patched %d guards in %s' % (len(guards), EXE))

        back = t.read(WAD)
        chk = wad_entries(back)
        ok = back[chk[0][1]:chk[0][1] + 4] == GIF
        exe2 = t.read(EXE)
        ok2 = all(exe2[g] == 0xEB for g in guards)
        print('  verify: archive plaintext %s, guards patched %s'
              % ('yes' if ok else 'NO', 'yes' if ok2 else 'NO'))
        return 0 if (ok and ok2) else 4
    finally:
        t.close()


def main():
    ap = argparse.ArgumentParser(description='Restore FIND IT photographs without a dongle')
    ap.add_argument('target', help='the HardDisk.img to inspect or repair (use a COPY)')
    ap.add_argument('--source', help='an I.G.O. 4 HardDisk.img to take the plaintext from')
    ap.add_argument('--check', action='store_true', help='report only, change nothing')
    a = ap.parse_args()

    print('target: %s' % a.target)
    if a.check or not a.source:
        rc = report(a.target)
        if not a.source and not a.check:
            print('\ngive --source <IGO4 image> to apply the fix')
        return rc
    print('source: %s' % a.source)
    print('NOTE: writing in place. Stop now if this is not a copy.')
    return report(a.target, a.source, apply_fix=True)


if __name__ == '__main__':
    sys.exit(main())
