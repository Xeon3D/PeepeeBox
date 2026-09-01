"""Read a GWAD archive out of a Photo Play disk image.

Whole file is XOR 0x55.  Header: "GWAD", u32 count.  Then count entries of 21
bytes: char name[13], u32 offset, u32 size.
"""
import importlib.util as iu, os, struct

# catalog-photoplay.py sits next to this file.  It used to be loaded from a fixed
# path outside the repository, and when that folder was deleted every script that
# imports this one broke; the old path is kept only as a fallback.
_here = os.path.dirname(os.path.abspath(__file__))
_cat = os.path.join(_here, "catalog-photoplay.py")
if not os.path.exists(_cat):
    _cat = r"C:\Users\xeon4\Documents\Claude\catalog-photoplay.py"
_s = iu.spec_from_file_location("cat", _cat)
cat = iu.module_from_spec(_s); _s.loader.exec_module(cat)


def entries(data):
    d = bytes(b ^ 0x55 for b in data[:8])
    if d[:4] != b'GWAD':
        return None
    n = struct.unpack_from('<I', d, 4)[0]
    out = []
    for i in range(n):
        p = 8 + i * 21
        raw = bytes(b ^ 0x55 for b in data[p:p + 21])
        if len(raw) < 21:
            break
        name = raw[:13].split(b'\0')[0].decode('latin-1')
        off, size = struct.unpack_from('<II', raw, 13)
        out.append((name, off, size))
    return out


def head8(data, off):
    return data[off:off + 8]          # only the directory is XOR 0x55


def read(img, path):
    fs = cat.Fat(img)
    return fs.read(path)
