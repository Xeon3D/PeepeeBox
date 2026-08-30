"""Crack the picture keys straight out of the 2001 image's own archives and compare
them with the keys already cracked from the 1999/2000 images.

Self-verifying on both sides: crack.exe only reports a key that decrypts to a valid
PCX header.  No model is involved, so this says whether the two generations key the
same picture the same way -- nothing about how the key is delivered."""
import os
import subprocess
import sys

sys.path.insert(0, r'C:\Users\xeon4\Documents\Claude\PeepeeBox-handoff\evidence\amore-pcx\derive')
import wad

EV = r'C:\Users\xeon4\Documents\Claude\PeepeeBox-handoff\evidence\amore-pcx'
CRACK = os.path.join(os.environ.get('TEMP', '.'), 'crack.exe')
IMG = sys.argv[1] if len(sys.argv) > 1 else \
    r'C:\Users\xeon4\Documents\Claude\PeepeeBox\PP2001\Photo Play 2001 DE B4821\HardDisk.img'
LIMIT = int(sys.argv[2]) if len(sys.argv) > 2 else 200

PAIRS = [('/FINDIT/PICS/FOTOPLAY.WAD', 'findit_keys.txt'),
         ('/AMORE/COMIX/FOTOPLAY.WAD', 'keys.txt'),
         ('/FND_MORD/PICS/FOTOPLAY.WAD', 'fndmord_keys.txt')]

for arch, ref in PAIRS:
    data = wad.read(IMG, arch)
    if data is None:
        print('%-30s absent' % arch)
        continue
    ents = wad.entries(data)[:LIMIT]
    inp = ''.join('%s %s\n' % (n, data[o:o + 8].hex()) for n, o, sz in ents)
    out = subprocess.run([CRACK], input=inp, capture_output=True, text=True).stdout
    got = dict(l.split() for l in out.splitlines() if len(l.split()) == 2)

    want = {}
    p = os.path.join(EV, ref)
    if os.path.exists(p):
        want = dict(l.split() for l in open(p) if len(l.split()) == 2)

    same = diff = only = unc = 0
    examples = []
    for n, k in got.items():
        if k == '????????' or k.startswith('amb'):
            unc += 1
            continue
        if n not in want:
            only += 1
            continue
        if k.upper() == want[n].upper():
            same += 1
        else:
            diff += 1
            if len(examples) < 5:
                examples.append('%s 2001=%s  1999=%s' % (n, k, want[n]))
    print('%-30s %4d cracked: %d SAME as %s, %d differ, %d not in ref, %d uncracked'
          % (arch, len(got), same, ref, diff, only, unc))
    for e in examples:
        print('        ', e)
