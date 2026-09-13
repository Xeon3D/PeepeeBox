"""Just enough FAT16 to read and write files in a Photo Play disk image.

The images are raw disk images (MBR + one FAT16 partition, PTS-DOS boot
sector).  8.3 names only, directories must already exist.  Replacing a file
frees its clusters and allocates afresh; creating one takes a free directory
slot, extending the directory by a cluster if it is full.
"""
import struct
import time


class Fat16:
    def __init__(self, path):
        self.f = open(path, "r+b")
        mbr = self.f.read(512)
        if mbr[510:512] != b"\x55\xaa":
            raise ValueError("no MBR signature")
        self.pstart = struct.unpack("<I", mbr[446 + 8:446 + 12])[0]
        self.f.seek(self.pstart * 512)
        bs = self.f.read(512)
        self.bps = struct.unpack("<H", bs[11:13])[0]
        self.spc = bs[13]
        res = struct.unpack("<H", bs[14:16])[0]
        self.nfats = bs[16]
        rootent = struct.unpack("<H", bs[17:19])[0]
        self.spf = struct.unpack("<H", bs[22:24])[0]
        tot = struct.unpack("<H", bs[19:21])[0] or struct.unpack("<I", bs[32:36])[0]
        if self.bps != 512 or bs[54:59] != b"FAT16":
            raise ValueError("not a FAT16 partition (%r)" % bs[54:62])
        self.fat0 = self.pstart + res
        self.root = self.fat0 + self.nfats * self.spf
        self.rootsecs = rootent * 32 // self.bps
        self.data = self.root + self.rootsecs
        self.clbytes = self.spc * self.bps
        self.nclust = (tot - (self.data - self.pstart)) // self.spc
        self.f.seek(self.fat0 * 512)
        self.fat = bytearray(self.f.read(self.spf * 512))
        self.dirty = False

    # -- FAT ------------------------------------------------------------
    def fget(self, n):
        return struct.unpack_from("<H", self.fat, n * 2)[0]

    def fset(self, n, v):
        struct.pack_into("<H", self.fat, n * 2, v)
        self.dirty = True

    def chain(self, n):
        out = []
        while 2 <= n < 0xFFF8:
            out.append(n)
            n = self.fget(n)
            if len(out) > self.nclust:
                raise ValueError("FAT chain loop")
        return out

    def csec(self, n):
        return self.data + (n - 2) * self.spc

    def read_cluster(self, n):
        self.f.seek(self.csec(n) * 512)
        return self.f.read(self.clbytes)

    def write_cluster(self, n, data):
        self.f.seek(self.csec(n) * 512)
        self.f.write(data.ljust(self.clbytes, b"\0"))

    def alloc(self, count):
        got = []
        n = 2
        while len(got) < count:
            if n >= self.nclust + 2:
                raise ValueError("disk full")
            if self.fget(n) == 0:
                got.append(n)
            n += 1
        for a, b in zip(got, got[1:] + [0xFFFF]):
            self.fset(a, b)
        return got

    # -- directories ----------------------------------------------------
    @staticmethod
    def name83(s):
        s = s.upper()
        base, _, ext = s.partition(".")
        if len(base) > 8 or len(ext) > 3 or not s.isascii():
            raise ValueError("not an 8.3 name: %s" % s)
        return (base.ljust(8) + ext.ljust(3)).encode()

    def _dir_sectors(self, clusters):
        if clusters is None:
            return [self.root + i for i in range(self.rootsecs)]
        return [self.csec(c) + i for c in clusters for i in range(self.spc)]

    def _entries(self, clusters):
        for sec in self._dir_sectors(clusters):
            self.f.seek(sec * 512)
            blk = self.f.read(512)
            for i in range(0, 512, 32):
                e = blk[i:i + 32]
                if e[0] == 0:
                    return
                yield sec * 512 + i, e

    def _find(self, clusters, n83):
        for pos, e in self._entries(clusters):
            if e[0] != 0xE5 and e[11] != 0x0F and e[:11] == n83:
                return pos, e
        return None, None

    def _free_slot(self, clusters):
        for sec in self._dir_sectors(clusters):
            self.f.seek(sec * 512)
            blk = self.f.read(512)
            for i in range(0, 512, 32):
                if blk[i] in (0, 0xE5):
                    return sec * 512 + i
        if clusters is None:
            raise ValueError("root directory full")
        new = self.alloc(1)[0]
        self.fset(clusters[-1], new)
        self.write_cluster(new, b"")
        clusters.append(new)
        return self.csec(new) * 512

    def _open_dir(self, parts):
        clusters = None
        for p in parts:
            pos, e = self._find(clusters, self.name83(p))
            if e is None or not (e[11] & 0x10):
                raise FileNotFoundError("no such directory: " + "\\".join(parts))
            clusters = self.chain(struct.unpack("<H", e[26:28])[0])
        return clusters

    def _split(self, path):
        parts = path.replace("/", "\\").strip("\\").split("\\")
        return parts[:-1], parts[-1]

    # -- files ----------------------------------------------------------
    def exists(self, path):
        d, name = self._split(path)
        try:
            pos, e = self._find(self._open_dir(d), self.name83(name))
        except FileNotFoundError:
            return False
        return e is not None

    def listdir(self, path):
        d = self._open_dir(path.replace("/", "\\").strip("\\").split("\\")) if path.strip("\\/") else None
        out = []
        for pos, e in self._entries(d):
            if e[0] != 0xE5 and e[11] != 0x0F and not (e[11] & 0x08):
                name = e[:8].decode("latin-1").rstrip() + (("." + e[8:11].decode("latin-1").rstrip()) if e[8:11].strip() else "")
                out.append((name, bool(e[11] & 0x10), struct.unpack("<I", e[28:32])[0]))
        return out

    def read(self, path):
        d, name = self._split(path)
        pos, e = self._find(self._open_dir(d), self.name83(name))
        if e is None:
            raise FileNotFoundError(path)
        size = struct.unpack("<I", e[28:32])[0]
        out = b"".join(self.read_cluster(c) for c in self.chain(struct.unpack("<H", e[26:28])[0]))
        return out[:size]

    def write(self, path, data):
        d, name = self._split(path)
        clusters = self._open_dir(d)
        n83 = self.name83(name)
        pos, e = self._find(clusters, n83)
        if e is not None:
            for c in self.chain(struct.unpack("<H", e[26:28])[0]):
                self.fset(c, 0)
            action = "replaced"
        else:
            pos = self._free_slot(clusters)
            action = "created"
        nclust = (len(data) + self.clbytes - 1) // self.clbytes
        cl = self.alloc(nclust) if nclust else []
        for i, c in enumerate(cl):
            self.write_cluster(c, data[i * self.clbytes:(i + 1) * self.clbytes])
        t = time.localtime()
        tm = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
        dt = ((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
        # 0-10 name, 11 attr, 12-21 reserved/ctime/adate/hi-cluster, 22 wtime, 24 wdate, 26 cluster, 28 size
        entry = n83 + bytes([0x20]) + b"\0" * 10 + struct.pack("<HHHI", tm, dt, cl[0] if cl else 0, len(data))
        assert len(entry) == 32
        self.f.seek(pos)
        self.f.write(entry)
        self.dirty = True
        return action

    def remove(self, path):
        """Delete a file: free its clusters, mark the directory entry unused."""
        d, name = self._split(path)
        clusters = self._open_dir(d)
        pos, e = self._find(clusters, self.name83(name))
        if e is None:
            return False
        for c in self.chain(struct.unpack("<H", e[26:28])[0]):
            self.fset(c, 0)
        self.f.seek(pos)
        self.f.write(bytes([0xE5]))
        self.dirty = True
        return True

    def close(self):
        if self.dirty:
            for k in range(self.nfats):
                self.f.seek((self.fat0 + k * self.spf) * 512)
                self.f.write(self.fat)
        self.f.flush()
        self.f.close()
