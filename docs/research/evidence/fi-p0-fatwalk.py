#!/usr/bin/env python3
"""FAT12/16 walker: full tree, timestamps, cluster chains, deleted entries, slack.
Usage: p0-fatwalk.py <image> [part_lba]
Emits JSON lines on stdout."""
import struct, sys, json, datetime

class Fat:
    def __init__(self, path, part_lba=None):
        self.f = open(path,"rb")
        self.f.seek(0,2); self.fsize = self.f.tell()
        if part_lba is None:
            self.f.seek(0); mbr = self.f.read(512)
            part_lba = struct.unpack_from("<I", mbr, 0x1BE+8)[0]
        self.plba = part_lba
        self.base = part_lba*512
        self.f.seek(self.base); bs = self.f.read(512)
        u16 = lambda o: struct.unpack_from("<H", bs, o)[0]
        u32 = lambda o: struct.unpack_from("<I", bs, o)[0]
        self.oem   = bs[3:11].decode("latin1")
        self.bps   = u16(11); self.spc = bs[13]
        self.res   = u16(14); self.nfat = bs[16]
        self.rootent = u16(17)
        self.tot   = u16(19) or u32(32)
        self.media = bs[21]
        self.fatsz = u16(22)
        self.spt   = u16(24); self.heads = u16(26)
        self.hidden= u32(28)
        self.volid = u32(39) if bs[38]==0x29 else 0
        self.label = bs[43:54].decode("latin1") if bs[38]==0x29 else ""
        self.fstype= bs[54:62].decode("latin1") if bs[38]==0x29 else ""
        self.fat_start  = self.res
        self.root_start = self.res + self.nfat*self.fatsz
        self.root_sect  = (self.rootent*32 + self.bps-1)//self.bps
        self.data_start = self.root_start + self.root_sect
        self.nclus = (self.tot - self.data_start)//self.spc
        self.f16 = self.nclus >= 4085
        self.fats = []
        for i in range(self.nfat):
            self.f.seek(self.base + (self.fat_start + i*self.fatsz)*self.bps)
            self.fats.append(self.f.read(self.fatsz*self.bps))
        self.fat = self.fats[0]
    def ent(self, n, which=0):
        fat = self.fats[which]
        if self.f16:
            return struct.unpack_from("<H", fat, n*2)[0]
        o = n + (n>>1)
        v = struct.unpack_from("<H", fat, o)[0]
        return (v>>4) if (n&1) else (v&0xFFF)
    def eoc(self, v): return v >= (0xFFF8 if self.f16 else 0xFF8)
    def bad(self, v): return v == (0xFFF7 if self.f16 else 0xFF7)
    def clus_off(self, c): return self.base + (self.data_start + (c-2)*self.spc)*self.bps
    def chain(self, c, limit=200000):
        out=[]; seen=set()
        while 2 <= c < (0xFFF8 if self.f16 else 0xFF8):
            if c in seen or len(out)>limit: out.append(-1); break
            if c-2 >= self.nclus: out.append(-2); break
            seen.add(c); out.append(c); c = self.ent(c)
        return out
    def read_chain(self, c, nbytes=None):
        data=bytearray()
        for cl in self.chain(c):
            if cl<0: break
            self.f.seek(self.clus_off(cl)); data += self.f.read(self.spc*self.bps)
            if nbytes is not None and len(data)>=nbytes: break
        return bytes(data[:nbytes]) if nbytes is not None else bytes(data)

def dosdt(d,t,ft=0):
    try:
        y=1980+((d>>9)&0x7f); mo=(d>>5)&0xf; da=d&0x1f
        h=(t>>11)&0x1f; mi=(t>>5)&0x3f; s=(t&0x1f)*2 + ft//100
        return "%04d-%02d-%02dT%02d:%02d:%02d"%(y,mo,da,h,mi,s)
    except Exception: return None

ATTR = [(0x01,"RO"),(0x02,"HID"),(0x04,"SYS"),(0x08,"VOL"),(0x10,"DIR"),(0x20,"ARC")]

def parse_dir(fs, raw, path, out, depth=0, visited=None):
    if visited is None: visited=set()
    lfn=[]
    for o in range(0, len(raw), 32):
        e = raw[o:o+32]
        if len(e)<32: break
        if e[0]==0x00: break
        attr = e[11]
        if attr == 0x0F:
            seq=e[0]&0x3F
            part=(e[1:11]+e[14:26]+e[28:32]).decode("utf-16-le","replace").split("\x00")[0]
            lfn.append((seq,part)); continue
        name = e[0:8].decode("latin1").rstrip()
        ext  = e[8:11].decode("latin1").rstrip()
        deleted = (e[0]==0xE5)
        if deleted: name = "?"+name[1:]
        fn = name + ("."+ext if ext else "")
        long = "".join(p for _,p in sorted(lfn, reverse=True)) if lfn else ""
        lfn=[]
        if attr & 0x08 and not (attr & 0x10):
            out.append(dict(path=path, name=fn, kind="vol", attr=attr)); continue
        clus = struct.unpack_from("<H", e, 26)[0]
        size = struct.unpack_from("<I", e, 28)[0]
        ctime= dosdt(struct.unpack_from("<H",e,16)[0], struct.unpack_from("<H",e,14)[0], e[13])
        atime= dosdt(struct.unpack_from("<H",e,18)[0], 0)
        mtime= dosdt(struct.unpack_from("<H",e,24)[0], struct.unpack_from("<H",e,22)[0])
        isdir= bool(attr & 0x10)
        rec = dict(path=path, name=fn, long=long, kind="dir" if isdir else "file",
                   attr="".join(f for m,f in ATTR if attr&m), attrv=attr,
                   clus=clus, size=size, mtime=mtime, ctime=ctime, atime=atime,
                   deleted=deleted)
        if not isdir and clus>=2 and not deleted:
            ch = fs.chain(clus)
            rec["nclus"]=len(ch)
            rec["frag"]= sum(1 for i in range(1,len(ch)) if ch[i]!=ch[i-1]+1)
            rec["first_lba"]= fs.plba + fs.data_start + (clus-2)*fs.spc
            rec["alloc"]= len(ch)*fs.spc*fs.bps
        out.append(rec)
        if isdir and fn not in (".","..","?.","?..") and not deleted and clus>=2:
            if clus in visited or depth>12: continue
            visited.add(clus)
            sub = fs.read_chain(clus)
            parse_dir(fs, sub, path + "/" + fn, out, depth+1, visited)

if __name__ == "__main__":
    img = sys.argv[1]
    fs = Fat(img, int(sys.argv[2]) if len(sys.argv)>2 else None)
    meta = {k:getattr(fs,k) for k in ("oem","bps","spc","res","nfat","rootent","tot","media","fatsz","spt","heads","hidden","volid","label","fstype","fat_start","root_start","root_sect","data_start","nclus","f16","plba","fsize")}
    meta["need_bytes"]=(fs.hidden+fs.tot)*fs.bps
    meta["fat_identical"]= all(x==fs.fats[0] for x in fs.fats)
    print(json.dumps({"_":"fsmeta", **meta}))
    fs.f.seek(fs.base+fs.root_start*fs.bps)
    root = fs.f.read(fs.root_sect*fs.bps)
    out=[]
    parse_dir(fs, root, "", out)
    for r in out: print(json.dumps({"_":"ent", **r}))
