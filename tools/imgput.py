"""Put a file into the root directory of a Photo Play disk image.

The reader in docs/research/evidence/catalog-photoplay.py is read-only, and staging a tool
onto a cabinet's disk needs the other direction: allocate clusters, chain them, write the
data, update every FAT copy, and add a directory entry.

FAT12/16 root directories only -- that is what these images are (the I.G.O. 2 PT cabinet is
FAT16, 32 KB clusters), and the fixed-size root is the part that needs no cluster
allocation of its own.  It refuses rather than guesses on anything else.

    python imgput.py <image> <file> [<file> ...]      add or replace, then verify
    python imgput.py --list <image>                   what is in the root
    python imgput.py --remove <image> NAME.EXT        delete and free its clusters

Every write is verified by reading the file back through the read-only reader and
comparing bytes, and the FAT copies are compared against each other afterwards.  Work on
a copy of the image unless you have a backup.
"""
import datetime
import importlib.util as iu
import os
import struct
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
_CAT = os.path.join(_HERE, os.pardir, 'docs', 'research', 'evidence',
                    'catalog-photoplay.py')
_spec = iu.spec_from_file_location('cat', _CAT)
cat = iu.module_from_spec(_spec)
_spec.loader.exec_module(cat)

FREE, BAD = 0x0000, 0xFFF7
EOC = 0xFFFF


class Volume:
    """read/write view of the FAT12/16 volume in a disk image"""

    def __init__(self, path):
        self.path = path
        ro = cat.Fat(path)
        self.part = ro.part
        self.bps = ro.bps
        self.spc = ro.spc
        self.bits = ro.bits
        self.root_ents = ro.root_ents
        self.fat_off = ro.fat_off
        self.root_off = ro.root_off
        self.data_off = ro.data_off
        ro.close()
        if self.bits not in (12, 16):
            raise SystemExit('imgput: FAT%d is not supported' % self.bits)
        if self.bits == 12:
            raise SystemExit('imgput: FAT12 write is not implemented -- '
                             'these images are FAT16')

        b = self._rd(self.part, 512)
        self.nfat = b[0x10]
        self.fatsz = struct.unpack_from('<H', b, 0x16)[0] or \
            struct.unpack_from('<I', b, 0x24)[0]
        rsvd = struct.unpack_from('<H', b, 0x0E)[0]
        tot = struct.unpack_from('<H', b, 0x13)[0] or \
            struct.unpack_from('<I', b, 0x20)[0]
        root_secs = (self.root_ents * 32 + self.bps - 1) // self.bps
        self.nclus = (tot - rsvd - self.nfat * self.fatsz - root_secs) // self.spc
        self.csize = self.bps * self.spc

    # ------------------------------------------------------------ raw i/o

    def _rd(self, off, n):
        with open(self.path, 'rb') as f:
            f.seek(off)
            d = f.read(n)
        if len(d) != n:
            raise SystemExit('imgput: short read at %d' % off)
        return d

    def _wr(self, off, data):
        with open(self.path, 'r+b') as f:
            f.seek(off)
            f.write(data)

    # ---------------------------------------------------------------- fat

    def fat_get(self, c):
        return struct.unpack_from('<H', self._rd(self.fat_off + c * 2, 2), 0)[0]

    def fat_set(self, c, v):
        """written into every FAT copy, so the two never drift"""
        for i in range(self.nfat):
            self._wr(self.fat_off + i * self.fatsz * self.bps + c * 2,
                     struct.pack('<H', v))

    def free_clusters(self):
        fat = self._rd(self.fat_off, self.fatsz * self.bps)
        return [c for c in range(2, self.nclus + 2)
                if struct.unpack_from('<H', fat, c * 2)[0] == FREE]

    def chain(self, first):
        out, c = [], first
        while 2 <= c < 0xFFF8 and len(out) < self.nclus:
            out.append(c)
            c = self.fat_get(c)
        return out

    def clus_off(self, c):
        return self.data_off + (c - 2) * self.csize

    # ---------------------------------------------------------- directory

    def root_raw(self):
        return bytearray(self._rd(self.root_off, self.root_ents * 32))

    def root_write(self, idx, ent):
        self._wr(self.root_off + idx * 32, ent)

    def find(self, name83):
        raw = self.root_raw()
        for i in range(self.root_ents):
            e = raw[i * 32:i * 32 + 32]
            if e[0] == 0x00:
                break
            if e[0] == 0xE5 or e[11] == 0x0F:
                continue
            if bytes(e[:11]) == name83:
                return i, e
        return None, None

    def free_slot(self):
        raw = self.root_raw()
        for i in range(self.root_ents):
            if raw[i * 32] in (0x00, 0xE5):
                return i
        raise SystemExit('imgput: the root directory is full')


def name83(fn):
    base, _, ext = os.path.basename(fn).upper().partition('.')
    if len(base) > 8 or len(ext) > 3 or not base:
        raise SystemExit('imgput: "%s" is not an 8.3 name' % fn)
    for ch in base + ext:
        if ch in '"*+,/:;<=>?[' + chr(92) + ']|' or ord(ch) < 0x20:
            raise SystemExit('imgput: "%s" has a character DOS will not take' % fn)
    return (base.ljust(8) + ext.ljust(3)).encode('cp437')


def dos_stamp(when):
    d = ((when.year - 1980) << 9) | (when.month << 5) | when.day
    t = (when.hour << 11) | (when.minute << 5) | (when.second // 2)
    return t, d


def release(vol, first):
    n = 0
    for c in vol.chain(first):
        vol.fat_set(c, FREE)
        n += 1
    return n


def put(vol, path):
    data = open(path, 'rb').read()
    nm = name83(path)
    need = (len(data) + vol.csize - 1) // vol.csize

    idx, ent = vol.find(nm)
    if ent is not None:
        old = struct.unpack_from('<H', ent, 26)[0]
        freed = release(vol, old) if old else 0
        print('  replacing %s (freed %d cluster%s)'
              % (nm.decode(), freed, '' if freed == 1 else 's'))
    else:
        idx = vol.free_slot()

    free = vol.free_clusters()
    if len(free) < need:
        raise SystemExit('imgput: %d clusters free, %s needs %d'
                         % (len(free), nm.decode(), need))
    use = free[:need]

    # data first, then the chain, then the directory entry: an interruption
    # leaves clusters that are still marked free rather than a half-linked file
    for i, c in enumerate(use):
        chunk = data[i * vol.csize:(i + 1) * vol.csize]
        vol._wr(vol.clus_off(c), chunk + b'\0' * (vol.csize - len(chunk)))
    for i, c in enumerate(use):
        vol.fat_set(c, EOC if i == len(use) - 1 else use[i + 1])

    t, d = dos_stamp(datetime.datetime.now())
    ent = bytearray(32)
    ent[0:11] = nm
    ent[11] = 0x20                                  # archive
    struct.pack_into('<HH', ent, 14, t, d)          # created
    struct.pack_into('<H', ent, 18, d)              # accessed
    struct.pack_into('<HH', ent, 22, t, d)          # written
    struct.pack_into('<H', ent, 26, use[0] if use else 0)
    struct.pack_into('<I', ent, 28, len(data))
    vol.root_write(idx, bytes(ent))

    print('  %-12s %7d bytes  %d cluster%s  first %d  slot %d'
          % (nm.decode(), len(data), need, '' if need == 1 else 's',
             use[0] if use else 0, idx))
    base = nm[:8].decode().strip()
    ext = nm[8:].decode().strip()
    return (base + '.' + ext if ext else base), data


def verify(image, wrote):
    """read every file back through the read-only reader and compare"""
    fs = cat.Fat(image)
    ok = True
    for nm, data in wrote:
        path = '/' + nm
        got = fs.read(path)
        if got is None:
            print('  FAIL %s: not found by the reader' % path)
            ok = False
        elif got != data:
            print('  FAIL %s: %d bytes back, %d written' % (path, len(got), len(data)))
            ok = False
        else:
            print('  ok   %s  %d bytes, byte-identical' % (path, len(got)))
    fs.close()
    return ok


def fats_agree(vol):
    n = vol.fatsz * vol.bps
    first = vol._rd(vol.fat_off, n)
    for i in range(1, vol.nfat):
        if vol._rd(vol.fat_off + i * n, n) != first:
            return False
    return True


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)

    if args[0] == '--list':
        fs = cat.Fat(args[1])
        for name, attr, first, sz in fs._entries(0):
            print('  %-11s attr %02X  clus %5d  %8d' % (name, attr, first, sz))
        fs.close()
        return

    if args[0] == '--remove':
        vol = Volume(args[1])
        nm = name83(args[2])
        idx, ent = vol.find(nm)
        if ent is None:
            raise SystemExit('imgput: %s is not there' % args[2])
        first = struct.unpack_from('<H', ent, 26)[0]
        n = release(vol, first) if first else 0
        e = bytearray(ent)
        e[0] = 0xE5
        vol.root_write(idx, bytes(e))
        print('removed %s, %d clusters freed' % (args[2], n))
        return

    image, files = args[0], args[1:]
    if not files:
        raise SystemExit(__doc__)
    vol = Volume(image)
    before = len(vol.free_clusters())
    print('%s: FAT%d, %d byte clusters, %d free (%.1f MB)'
          % (os.path.basename(image), vol.bits, vol.csize, before,
             before * vol.csize / 1048576.0))

    wrote = [put(vol, f) for f in files]

    after = len(vol.free_clusters())
    print('free clusters %d -> %d' % (before, after))
    print('FAT copies agree: %s' % fats_agree(vol))
    print('verifying by reading back:')
    if not verify(image, wrote):
        raise SystemExit('imgput: VERIFY FAILED -- restore the image from backup')
    print('all files verified')


if __name__ == '__main__':
    main()
