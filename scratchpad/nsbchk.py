import os, struct, glob
def rd(b,o,n): return int.from_bytes(b[o:o+n],'little')
class V:
    def __init__(s,fn):
        s.f=open(fn,'rb'); mbr=s.f.read(512); s.part=rd(mbr,0x1BE+8,4)
        s.f.seek(s.part*512); bs=s.f.read(512)
        s.bps=rd(bs,0x0B,2); s.spc=bs[0x0D]; s.nfat=bs[0x10]; s.spf=rd(bs,0x16,2)
        s.fat_lba=s.part+rd(bs,0x0E,2); s.root=s.fat_lba+s.nfat*s.spf
        s.rsec=rd(bs,0x11,2)*32//s.bps; s.data=s.root+s.rsec
        s.f.seek(s.fat_lba*s.bps); s.fat=s.f.read(s.spf*s.bps)
    def clba(s,c): return s.data+(c-2)*s.spc
    def rdsec(s,l,n): s.f.seek(l*s.bps); return s.f.read(n*s.bps)
    def find(s,dc,name):
        while True:
            lba,n=(s.clba(dc),s.spc) if dc else (s.root,s.rsec)
            buf=s.rdsec(lba,n)
            for i in range(len(buf)//32):
                e=buf[i*32:i*32+32]
                if e[0] in (0,0xE5): continue
                if e[:11]==name.encode(): return rd(e,0x1A,2), rd(e,0x1C,4)
            if not dc: return None
            dc=rd(s.fat,dc*2,2)
            if dc<2 or dc>=0xFFF0: return None
    def small(s,dc,name):
        r=s.find(dc,name)
        if not r or r[0]<2 or r[1]==0: return None
        return s.rdsec(s.clba(r[0]),s.spc)[:r[1]]
for d in sorted(glob.glob('/mnt/f/HDDImages/*/harddisk.img')+glob.glob('/mnt/f/HDDImages/*/*.img')):
    pass

import sys
for img in sys.argv[1:]:
    try:
        v=V(img)
        out=[]
        m=v.find(0,'MENU       ')
        if m: out.append('NSB.NR='+repr(v.small(m[0],'NSB     NR ')))
        n=v.find(0,'MAIN       ')
        if n: out.append('KEY.DAT='+repr(v.small(n[0],'KEY     DAT')))
        print(os.path.basename(os.path.dirname(img)), '|', '  '.join(out) or '(neither)')
    except Exception as e:
        print(img,'ERR',e)
