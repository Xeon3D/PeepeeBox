r"""Patch bytes of a file inside a FAT image, in place, through its cluster chain.

    python imgpatch.py <image> <path> <fileoff:hex> <old:hex> <new:hex> [...]

imgput.py replaces a whole file, and only in the root directory.  This is the
other thing you want on a cabinet's disk: change a handful of bytes in a program
buried under \EXE, without touching its size, its clusters or anything else.
Give it file offsets -- the offsets a disassembler shows -- and it walks the
directory and the FAT to find where each one landed on the disk.

Every patch names the bytes it expects to find.  A mismatch is refused rather
than guessed at, an offset that already holds the new bytes is skipped, so
running it twice is safe, and swapping the old and new arguments reverts.  The
file is read back through the read-only reader afterwards and checked.

Work on a copy of the image unless you have a backup.
"""
import importlib.util as iu
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_CAT  = os.path.join(_HERE, os.pardir, 'docs', 'research', 'evidence',
                     'catalog-photoplay.py')
_spec = iu.spec_from_file_location('cat', _CAT)
cat   = iu.module_from_spec(_spec)
_spec.loader.exec_module(cat)

img, path = sys.argv[1], sys.argv[2]
patches = []
a = sys.argv[3:]
for i in range(0, len(a), 3):
    patches.append((int(a[i], 16), bytes.fromhex(a[i+1]), bytes.fromhex(a[i+2])))

fs = cat.Fat(img)
hit = fs.find(path)
if not hit or hit[2]:
    sys.exit("no such file: " + path)
clus, size, _ = hit
csz = fs.spc * fs.bps

# file offset -> absolute image offset, walking the chain
chain = []
c = clus
while c >= 2 and not fs._eoc(c) and len(chain) * csz < size + csz:
    chain.append(c)
    c = fs._next(c)

def img_off(fo):
    return fs.data_off + (chain[fo // csz] - 2) * csz + (fo % csz)

data = fs.read(path)
for fo, old, new in patches:
    assert len(old) == len(new), "length must not change"
    got = data[fo:fo+len(old)]
    if got == new:
        print("  %06X already %s" % (fo, new.hex()))
    elif got != old:
        sys.exit("  %06X expected %s, found %s -- refusing" % (fo, old.hex(), got.hex()))
fs.close()

with open(img, 'r+b') as f:
    for fo, old, new in patches:
        o = img_off(fo)
        f.seek(o); cur = f.read(len(old))
        if cur == new:
            continue
        assert cur == old, "image offset %X holds %s" % (o, cur.hex())
        f.seek(o); f.write(new)
        print("  %06X (image %08X) %s -> %s" % (fo, o, old.hex(), new.hex()))

fs = cat.Fat(img)
back = fs.read(path)
ok = all(back[fo:fo+len(new)] == new for fo, old, new in patches)
print("verified:", "yes" if ok else "NO", " size", len(back))
