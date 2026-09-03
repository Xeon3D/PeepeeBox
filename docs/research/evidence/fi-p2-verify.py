#!/usr/bin/env python3
"""p2-verify - drive FSYSTEM.EXE's real protection routine against a candidate dongle.

Runs the client library offline (p2-haspsim) with the HASP-4 detection handshake
(p2-hasp4) in front of a Microwire EEPROM (p2-microwire) and checks every gate
FSYSTEM.EXE applies at boot:

    service 1  IsHasp                 -> p1 must be non-zero
    service 6  dongle ID              -> p3 must be zero
    service 3  read words 0..5        -> must spell "ORGACONTROL "

Usage:  python3 fi-p2-verify.py <path to GAME/FSYSTEM.EXE>

Resolves its sibling modules relative to this file, so it runs from anywhere.
"""
import importlib.util, os, sys
def load(n,p):
    sp=importlib.util.spec_from_file_location(n,p); m=importlib.util.module_from_spec(sp); sp.loader.exec_module(m); return m
HERE = os.path.dirname(os.path.abspath(__file__))
def here(n): return os.path.join(HERE, n)
hs = load("hs", here("fi-p2-haspsim.py"))
h4 = load("h4", here("fi-p2-hasp4.py"))
mw = load("mw", here("fi-p2-microwire.py"))
EXE = sys.argv[1] if len(sys.argv) > 1 else "funny_interactive_de/GAME/FSYSTEM.EXE"
SQ=bytes([0xc3,0xd9,0xd3,0xfb,0x9d,0x89,0xb9,0xa1,0xb3,0xc1,0xf1,0xcd,0xdf,0x9d])

class Dongle:
    """One device: the HASP-4 detection handshake, with the Microwire EEPROM taking
       over STATUS bit 5 only while it is actually clocking a word out."""
    def __init__(self, eeprom):
        self.h = h4.Hasp4(password=SQ)
        self.e = eeprom
    def write_data(self, v):
        self.h.write_data(v); self.e.write_data(v)
    def read_status(self):
        s = self.h.read_status()
        if self.e.driving:
            s = (s & ~0x20) | (0x20 if self.e.do else 0)
        return s

def mkeeprom():
    e = mw.Microwire(nwords=512, abits=9, pass1=0x43B5, addr_bias=8)
    e.store_string(0, "ORGACONTROL ")
    e.store(9, 0xBEEF)
    for w in range(13, 20): e.store(w, 0)
    e.trace.clear()
    return e

def run(service, a=0, b=0, eeprom=None):
    e = eeprom or mkeeprom(); d = Dongle(e)
    s = hs.Sim(EXE); st={"data":0xFF,"ctrl":0x0C}
    def resp(port,ctx):
        if port==0x378: return st["data"]
        if port==0x37A: return st["ctrl"]
        return d.read_status()
    s.responder=resp
    o=s._hook_out
    def hout(uc,port,size,value,dd):
        v=value&0xFF
        if port==0x378: st["data"]=v; d.write_data(v)
        if port==0x37A: st["ctrl"]=v
        return o(uc,port,size,value,dd)
    s._hook_out=hout
    r=s.call(service,0x64,1,0x43B5,0x594A,a,b,max_insn=40_000_000)
    return r, d

r,_=run(1);  print("svc1 IsHasp        p1=%04X                       (FSYSTEM needs p1!=0)"%r["p1"])
r,_=run(6);  print("svc6 dongle ID     p1=%04X p2=%04X p3=%04X       (FSYSTEM needs p3==0)"%(r["p1"],r["p2"],r["p3"]))
want=[0x4F52,0x4741,0x434F,0x4E54,0x524F,0x4C20]
allok=True
for w in range(6):
    r,_=run(3,a=w); good=(r["p2"]==want[w] and r["p3"]==0); allok&=good
    print("svc3 word %d        p2=%04X %-2s -> %r"%(w,r["p2"],"OK" if good else "!!",
          bytes([r["p2"]>>8, r["p2"]&0xFF]).decode("latin1")))
r,_=run(3,a=9); print("svc3 word 9        p2=%04X                       (INT 50h AX=1235)"%r["p2"])
e=mkeeprom()
r,d=run(4,a=13,b=0x00AB,eeprom=e)
print("svc4 write w13<-00AB p1=%04X p2=%04X p3=%04X   eeprom ops: %s"%(r["p1"],r["p2"],r["p3"],d.e.trace[-2:]))
r2,d2=run(3,a=13,eeprom=e)
print("svc3 read-back w13   p2=%04X"%r2["p2"])
print("SYSTEM CODE:", "PASS" if allok else "FAIL")
