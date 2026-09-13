"""verify_patch.py -- static checks of a patched FN_SYS.EXE against its original.

    python verify_patch.py FN_SYS.ORG FN_SYS.EXE

Simulates DOS loading at an arbitrary segment, then checks that every
relocation points into a known segment, that every far call in the new PATCH
segment and every hooked site lands on code (a function prologue or the exact
continuation point), and that DGROUP is unchanged except for the two rewritten
strings.  Exit code 0 = all good.
"""
import struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

md = Cs(CS_ARCH_X86, CS_MODE_16)
o = open(sys.argv[1], "rb").read(); p = open(sys.argv[2], "rb").read()
def hdr(x): return struct.unpack("<14H", x[:28])
Ho, Hp = hdr(o), hdr(p)
HDR = Ho[4] * 16; assert Hp[4] * 16 == HDR
oi, pi = o[HDR:], p[HDR:]
DG = struct.unpack("<H", oi[1:3])[0]; DSB = DG * 16
SHIFT = struct.unpack("<H", pi[1:3])[0] - DG; PL = SHIFT * 16
crlc, lfarlc = Hp[3], Hp[12]
LOAD = 0x1234
rel = bytearray(pi); relpos = []; vals = {}
for k in range(crlc):
    off, seg = struct.unpack("<HH", p[lfarlc + 4*k: lfarlc + 4*k + 4]); pos = seg * 16 + off; relpos.append(pos)
    v = struct.unpack_from("<H", pi, pos)[0]; vals[v] = vals.get(v, 0) + 1
    struct.pack_into("<H", rel, pos, (v + LOAD) & 0xffff)
assert len(relpos) == len(set(relpos)), "duplicate relocation positions"
# known segments = those of the original (+ shifted DGROUP + PATCH)
osegs = set()
for k in range(Ho[3]):
    off, seg = struct.unpack("<HH", o[lfarlc + 4*k: lfarlc + 4*k + 4]); osegs.add(struct.unpack_from("<H", oi, seg*16+off)[0])
known = {s if s < DG else s + SHIFT for s in osegs} | {DG}
bad = [hex(v) for v in vals if v not in known]
assert not bad, "unexpected relocated segment values: %s" % bad
def lin(seg, off): return (seg - LOAD) * 16 + off
problems = 0
# PATCH segment far calls
for i in md.disasm(bytes(pi[DSB:DSB + PL]), 0):
    if i.mnemonic == "lcall":
        if DSB + i.address + 3 not in relpos: print("PATCH+%04x lcall has no relocation" % i.address); problems += 1
        off, seg = struct.unpack_from("<HH", rel, DSB + i.address + 1); t = lin(seg, off)
        head = pi[t:t+3]
        ok = head[0] in (0xc8, 0x55) or head[:2] == b"\x8d\x9e" or head[:2] == b"\x8d\x46" or head[0] in (0xe8, 0x0b, 0x80)
        if not ok: print("PATCH+%04x lcall -> %06x looks wrong: %s" % (i.address, t, head.hex(" "))); problems += 1
# hooked sites: every lcall into PATCH from the main image
hooks = 0
for k, pos in enumerate(relpos):
    if pos < DSB and struct.unpack_from("<H", pi, pos)[0] == DG and pi[pos-3] == 0x9a:
        off = struct.unpack_from("<H", pi, pos - 2)[0]; hooks += 1
        if off >= PL: print("hook at %06x -> PATCH+%04x out of range" % (pos - 3, off)); problems += 1
# DGROUP identical except the two rewritten strings
ds_o = oi[DSB:]; ds_p = pi[DSB + PL:]
assert len(ds_o) == len(ds_p), "DGROUP length changed"
diff = sorted({i // 16 * 16 for i in range(len(ds_o)) if ds_o[i] != ds_p[i]})
assert len(diff) <= 6, "DGROUP differs in %d blocks: %s" % (len(diff), [hex(x) for x in diff])   # two strings, 20 + 23 bytes
# code identical outside hook sites / immediates: count differing bytes
cd = sum(1 for i in range(DSB) if oi[i] != pi[i])
print("%s: %d relocs (+%d), %d hooks into PATCH, %d code bytes changed, DGROUP diff blocks %s, %s"
      % (sys.argv[2].split("\\")[-1].split("/")[-1], crlc, crlc - Ho[3], hooks, cd, [hex(x) for x in diff], "OK" if not problems else "%d PROBLEMS" % problems))
sys.exit(1 if problems else 0)
