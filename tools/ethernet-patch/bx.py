"""bx.py -- disassemble a Borland 16-bit EXE around a string reference or an offset.

    python bx.py <exe> str "<text>" [before] [after]     around code refs to a DS string
    python bx.py <exe> at <hexoff> [len]                  linear disassembly
    python bx.py <exe> find "<text>"                      DS offsets + code refs of a string
"""
import re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16, x86

md = Cs(CS_ARCH_X86, CS_MODE_16); md.detail = True

def load(path):
    d = open(path, "rb").read()
    hdr = struct.unpack("<H", d[8:10])[0] * 16
    img = d[hdr:]
    ds = struct.unpack("<H", img[1:3])[0]
    return img, ds * 16

def strat(img, dsb, off, n=60):
    o = dsb + off
    if o < 0 or o >= len(img): return None
    e = img.find(b"\0", o, o + n); s = img[o:e]
    if len(s) >= 2 and all(32 <= c < 127 or c in (9, 10, 13) for c in s):
        return s.decode("latin1").replace("\n", "\\n").replace("\r", "\\r").replace("\t", "\\t")
    return None

def find_str(img, dsb, text):
    b = text.encode("latin1"); out = []; i = 0
    while True:
        i = img.find(b, i)
        if i < 0: return out
        if i > dsb and img[i-1] in (0, 10, 0x5c) or img[i-1] == 0: out.append(i - dsb)
        i += 1

def xrefs(img, dsb, dsoff):
    p = struct.pack("<H", dsoff); out = []; i = 0
    while True:
        i = img.find(p, i, dsb)
        if i < 0: return out
        if img[i-1] in (0xb8, 0x68, 0xbf, 0xbe, 0xba, 0xbb, 0xb9): out.append(i - 1)
        i += 1

def dis(img, dsb, start, length):
    for i in md.disasm(img[start:start+length], start):
        ann = ""
        for op in i.operands:
            if op.type == x86.X86_OP_IMM and 0 < op.imm < 0x4000:
                s = strat(img, dsb, op.imm)
                if s: ann = '   ; "%s"' % s[:50]
        if i.mnemonic == "lcall":
            off, seg = struct.unpack("<HH", i.bytes[1:5]); ann += "   ; -> %06x" % (seg * 16 + off)
        print("%06x  %-22s %s %s%s" % (i.address, i.bytes.hex(), i.mnemonic, i.op_str, ann))

if __name__ == "__main__":
    img, dsb = load(sys.argv[1])
    mode = sys.argv[2]
    if mode == "find":
        for off in find_str(img, dsb, sys.argv[3]):
            print("ds %04x  xrefs: %s" % (off, " ".join("%06x" % x for x in xrefs(img, dsb, off))))
    elif mode == "str":
        before = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x40
        after = int(sys.argv[5], 16) if len(sys.argv) > 5 else 0x80
        for off in find_str(img, dsb, sys.argv[3]):
            for x in xrefs(img, dsb, off):
                print("=== ref %06x to ds:%04x ===" % (x, off)); dis(img, dsb, x - before, before + after)
    elif mode == "at":
        a = int(sys.argv[3], 16); n = int(sys.argv[4], 16) if len(sys.argv) > 4 else 0x80
        dis(img, dsb, a, n)
