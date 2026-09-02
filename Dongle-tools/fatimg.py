"""Minimal FAT16 reader/writer for Photo Play HardDisk.img files.

Read side is the same walk `src/photoplay_ident.c` does.  The write side deliberately
supports one operation only -- **overwrite a file with content of exactly the same
length** -- which needs no FAT or directory changes at all, just the existing cluster
chain rewritten in place.  Everything this toolkit does happens to be same-length, and
refusing anything else keeps a bug from corrupting a 1.6 GB image.
"""
import struct

SEC = 512


class Fat:
    def __init__(self, path, write=False):
        self.f = open(path, 'r+b' if write else 'rb')
        sec = self._rd(0, SEC)
        self.part = 0
        if sec[510] == 0x55 and sec[511] == 0xAA:
            for i in range(4):
                e = sec[0x1BE + i * 16:0x1BE + i * 16 + 16]
                if e[4] in (0x01, 0x04, 0x06, 0x0E):
                    self.part = struct.unpack_from('<I', e, 8)[0] * SEC
                    break
        b = self._rd(self.part, SEC)
        self.bps = struct.unpack_from('<H', b, 0x0B)[0]
        self.spc = b[0x0D]
        rsvd = struct.unpack_from('<H', b, 0x0E)[0]
        self.nfat = b[0x10]
        self.root_ents = struct.unpack_from('<H', b, 0x11)[0]
        self.spf = struct.unpack_from('<H', b, 0x16)[0]
        self.fat_off = self.part + rsvd * self.bps
        self.root_off = self.fat_off + self.nfat * self.spf * self.bps
        self.data_off = self.root_off + self.root_ents * 32

    def close(self):
        self.f.close()

    # ---- raw ----
    def _rd(self, off, n):
        self.f.seek(off)
        return self.f.read(n)

    def _wr(self, off, data):
        self.f.seek(off)
        self.f.write(data)

    def _clus_off(self, c):
        return self.data_off + (c - 2) * self.spc * self.bps

    def _next(self, c):
        return struct.unpack_from('<H', self._rd(self.fat_off + c * 2, 2), 0)[0]

    # ---- directory ----
    def _entries(self, clus):
        if clus == 0:
            data = self._rd(self.root_off, self.root_ents * 32)
            for i in range(0, len(data), 32):
                yield self.root_off + i, data[i:i + 32]
            return
        c = clus
        while 2 <= c < 0xFFF0:
            base = self._clus_off(c)
            data = self._rd(base, self.spc * self.bps)
            for i in range(0, len(data), 32):
                yield base + i, data[i:i + 32]
            c = self._next(c)

    @staticmethod
    def _name11(part):
        part = part.upper()
        if '.' in part:
            stem, ext = part.rsplit('.', 1)
        else:
            stem, ext = part, ''
        return (stem[:8].ljust(8) + ext[:3].ljust(3)).encode()

    def find(self, path):
        """returns (dir_entry_offset, first_cluster, size, is_dir) or None"""
        clus = 0
        ent = None
        for part in [p for p in path.replace('\\', '/').split('/') if p]:
            want = self._name11(part)
            ent = None
            for off, e in self._entries(clus):
                if e[0] in (0, 0xE5) or e[11] == 0x0F:
                    continue
                if e[0:11] == want:
                    ent = (off, struct.unpack_from('<H', e, 26)[0],
                           struct.unpack_from('<I', e, 28)[0], bool(e[11] & 0x10))
                    break
            if ent is None:
                return None
            clus = ent[1]
        return ent

    def read(self, path):
        hit = self.find(path)
        if hit is None:
            return None
        _, clus, size, isdir = hit
        if isdir:
            return None
        out = bytearray()
        c = clus
        while len(out) < size and 2 <= c < 0xFFF0:
            out += self._rd(self._clus_off(c), self.spc * self.bps)
            c = self._next(c)
        return bytes(out[:size])

    def overwrite(self, path, data):
        """replace a file's contents; the length must match exactly"""
        hit = self.find(path)
        if hit is None:
            raise KeyError(path)
        _, clus, size, isdir = hit
        if isdir:
            raise IsADirectoryError(path)
        if len(data) != size:
            raise ValueError('%s is %d bytes, refusing to write %d' % (path, size, len(data)))
        step = self.spc * self.bps
        c = clus
        pos = 0
        while pos < size and 2 <= c < 0xFFF0:
            chunk = data[pos:pos + step]
            if len(chunk) < step:
                chunk = chunk + self._rd(self._clus_off(c) + len(chunk), step - len(chunk))
            self._wr(self._clus_off(c), chunk)
            pos += step
            c = self._next(c)
        if pos < size:
            raise IOError('cluster chain for %s ended early' % path)
        self.f.flush()
