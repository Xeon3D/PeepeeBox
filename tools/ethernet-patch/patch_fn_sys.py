#!/usr/bin/env python3
r"""patch_fn_sys.py - add an Ethernet connection option to Photo Play FN_SYS.EXE

usage:  python patch_fn_sys.py <FN_SYS.ORG> <FN_SYS.EXE> [--profile]

v4: nothing is at a fixed offset any more.  Every hook site, call target,
string and frame offset is located in the binary by structure -- string
references, instruction patterns and the program's own call sites -- so the
same patch applies to every FN_SYS.EXE build that carries the fun.net client
(Photo Play 2001 and I.G.O. 1..8, compiled for 286 or 386 alike).  Every
location is asserted; an unexpected build fails loudly instead of being
half-patched.  --profile prints what was found.

What the patched program does that the original does not
  * setting  CONNTYPE  in SETTINGS.TAB (MODEM / ETHERNET), written by
      - FN_SYS /setting=CONNTYPE;ETHERNET   (command line)
      - a 4th field "connection type" in the touch-screen dial-in settings
        wizard, a Modem / Ethernet two-button chooser.
  * switch   FN_SYS /conntype  -> errorlevel 1 when CONNTYPE=ETHERNET.
  * /export in Ethernet mode: no modem probe (no error return), WATTCP.CFG
    is neither deleted nor rewritten.
  * the transmission report shows a modem response of ETHERNET in the label
    colour instead of red.

How
  * A code segment PATCH is inserted in front of DGROUP; all MZ relocations
    are shifted.  DGROUP grows by 0x200 bytes (the two C0 start-up constants)
    for a zeroed scratch area holding buffers and the new strings.
  * Small hook sites are replaced by far calls into PATCH.
  * The pulse/tone chooser dialog, the field label/box block and the report's
    draw call are copied from the program with immediates rewritten.
  * The 3-field wizard is re-laid out to 4 fields (pitch 48 px).
"""
import re, struct, sys
from capstone import Cs, CS_ARCH_X86, CS_MODE_16

SRC, DST = sys.argv[1], sys.argv[2]
PROFILE = "--profile" in sys.argv
d = bytearray(open(SRC, 'rb').read())
md = Cs(CS_ARCH_X86, CS_MODE_16)

# ---------------------------------------------------------------- header
H = list(struct.unpack('<14H', d[:28]))
(e_cblp, e_cp, e_crlc, e_cparhdr, e_minalloc, e_maxalloc,
 e_ss, e_sp, e_csum, e_ip, e_cs, e_lfarlc, e_ovno) = H[1:]
HDR = e_cparhdr * 16
img = bytearray(d[HDR:])
orig = bytes(img)
assert orig[0] == 0xba, "expected Borland C0 entry (mov dx, DGROUP)"
DGROUP = struct.unpack('<H', orig[1:3])[0]
DSB = DGROUP * 16
SHIFT = 0x70
PATCH_SEG = DGROUP
PATCH_LEN = SHIFT * 16

def r16(buf, o): return struct.unpack('<H', buf[o:o+2])[0]
def w16(buf, o, v): buf[o:o+2] = struct.pack('<H', v)
def die(msg): raise AssertionError(msg)
def one(lst, what):
    if len(lst) != 1: die("%s: expected exactly one, got %d: %s" % (what, len(lst), [hex(x) for x in lst]))
    return lst[0]

# ---------------------------------------------------------------- generic helpers
def find_strs(s):
    """DS offsets of every copy of the NUL-terminated string s."""
    b = s.encode('latin1') + b'\0'; out = []; i = DSB
    while True:
        i = orig.find(b, i)
        if i < 0: return out
        if orig[i-1] == 0: out.append(i - DSB)
        i += 1
def xrefs(dsoff, lo=0, hi=None):
    """positions of `push imm16` == dsoff"""
    hi = DSB if hi is None else hi
    p = struct.pack('<H', dsoff); out = []; i = lo
    while True:
        i = orig.find(p, i, hi)
        if i < 0: return out
        if orig[i-1] == 0x68: out.append(i - 1)
        i += 1
def find_bytes(hexpat, lo=0, hi=None):
    """pattern with ?? wildcards -> positions"""
    hi = DSB if hi is None else hi
    rx = b"".join(b"." if t == "??" else re.escape(bytes([int(t, 16)])) for t in hexpat.split())
    return [m.start() + lo for m in re.finditer(rx, orig[lo:hi], re.S)]
def insns(start, end):
    return list(md.disasm(orig[start:end], start))
def lcall_target(pos):
    assert orig[pos] == 0x9a, "no lcall at %x" % pos
    off, seg = struct.unpack('<HH', orig[pos+1:pos+5]); return (seg, off)
def near_target(pos):
    """`push cs ; call rel16` at pos -> linear target"""
    assert orig[pos:pos+2] == b'\x0e\xe8', "no push cs/call at %x" % pos
    return pos + 4 + struct.unpack('<h', orig[pos+2:pos+4])[0]
def is_func_start(i):
    return orig[i-1] in (0xcb, 0xc3) and (orig[i] == 0xc8 or orig[i:i+3] == b'\x55\x8b\xec')
def func_start_before(a):
    i = a
    while i > 0:
        if is_func_start(i): return i
        i -= 1
def func_start_after(a):
    i = a + 1
    while i < DSB:
        if is_func_start(i): return i
        i += 1

# segment map from the relocation table
segvals = set()
for k in range(e_crlc):
    off, seg = struct.unpack('<HH', d[e_lfarlc + 4*k: e_lfarlc + 4*k + 4])
    segvals.add(r16(orig, seg * 16 + off))
segbases = sorted(v * 16 for v in segvals if v < DGROUP)
def seg_of(lin): return max(b for b in segbases if b <= lin) // 16
def far(lin):
    s = seg_of(lin); return (s, lin - s * 16)

P = {}

# ---------------------------------------------------------------- C0 start-up
m = one(find_bytes("81 c7 ?? ?? 72 ?? 03 3e ?? ?? 72 ?? b1 04 d3 ef 47", 0, 0x200), "C0 stack setup")
P['c0_stack'] = m + 2
b = one(find_bytes("bf ?? ?? b9 ?? ?? 2b cf fc f3 aa", 0, 0x200), "C0 BSS clear")
P['c0_bss'] = b + 4
BSS_END = r16(orig, P['c0_stack']); assert BSS_END == r16(orig, P['c0_bss']), "C0 constants disagree"
P['bss_end'] = BSS_END
SCR_SIZE = 0x200
SCR = BSS_END
SCR_VALUE, SCR_TEXT, SCR_RESULT, SCR_NUL, SCR_FLAG, SCR_STR = SCR, SCR + 0x80, SCR + 0xc0, SCR + 0xc1, SCR + 0xc2, SCR + 0xd0
assert SCR + SCR_SIZE + 0x2000 < 0x10000, "DGROUP would exceed 64K"

# ---------------------------------------------------------------- /export: h0, h1, h2
wattcp = find_strs(r"\fn_sys\dfu\wattcp.cfg"); assert len(wattcp) == 2, wattcp
netcfg = find_strs(r"\fn_sys\dfu\net.cfg");    assert len(netcfg) == 2, netcfg
h0 = one([x for x in xrefs(wattcp[0]) + xrefs(wattcp[1])
          if orig[x+3] == 0x9a and orig[x+8] == 0x59 and orig[x-9] == 0x68 and orig[x-6] == 0x9a], "h0 remove(wattcp.cfg)")
P['h0'] = h0; P['remove'] = lcall_target(h0 + 3); P['s_wattcp_keep'] = r16(orig, h0 + 1)
P['main_seg'] = seg_of(h0); MAINB = P['main_seg'] * 16

err = one(find_strs("\nERROR: can't talk to modem\n\n"), "modem error string")
errx = one(xrefs(err), "modem error xref")
cands = find_bytes("8d 86 ?? ?? 50 8d 86 ?? ?? 50 8d 46 ?? 50 9a", errx - 0x60, errx)
assert cands, "modem probe call not found"
h1 = cands[-1]; P['h1'] = h1; P['h1_frame'] = r16(orig, h1 + 2)
assert orig[h1-5] == 0x9a, "expected a memcpy call right before h1"

h2cand = [x for x in xrefs(wattcp[0]) + xrefs(wattcp[1]) if orig[x-3] == 0x68 and orig[x+3] == 0x9a]
h2 = one(h2cand, "h2 fopen(wattcp.cfg, w+)")
P['h2'] = h2 - 3; P['s_wplus'] = r16(orig, h2 - 2); P['s_wattcp_dup'] = r16(orig, h2 + 1)
assert orig[DSB + P['s_wplus']:DSB + P['s_wplus'] + 3] == b'w+\0', "expected \"w+\" before the wattcp.cfg fopen"
dash = find_strs("-----------------------------------------------\n")
dashx = sorted(x for o in dash for x in xrefs(o, h1, P['h2']))
assert dashx, "no ----- printf in the modem block"
lastdash = dashx[-1]; assert orig[lastdash+3] == 0x9a and orig[lastdash+8] == 0x59, "modem block end shape"
P['h1_resume'] = lastdash + 9
ns = [x for o in find_strs("nameserver=%s\n") for x in xrefs(o, P['h2'], P['h2'] + 0x200)]
assert ns, "nameserver= writes not found after fopen"
j = one(find_bytes("56 9a ?? ?? ?? ?? 59", max(ns), max(ns) + 0x40) + find_bytes("57 9a ?? ?? ?? ?? 59", max(ns), max(ns) + 0x40),
        "fclose after WATTCP.CFG writes")   # the FILE* lives in si or di depending on the build
P['h2_join'] = j + 7

# ---------------------------------------------------------------- dispatcher: h3
exp = one(find_strs("/export"), "/export string")
h3 = one([x for x in xrefs(exp) if orig[x+3:x+7] == b'\x8d\x46\x9a\x50' and orig[x+7] == 0x9a], "h3 /export compare")
P['h3'] = h3; P['stricmp'] = lcall_target(h3 + 7); P['s_export'] = exp
P['h3_epilog'] = one(find_bytes("9a ?? ?? ?? ?? 8b 46 fe eb 00 5f 5e c9 cb", h3, h3 + 0x200), "dispatcher epilogue")

# ---------------------------------------------------------------- wizard
wx = sorted(x for o in find_strs("wahlverfahren") for x in xrefs(o))
wiz = None
for x in wx:
    fs = func_start_before(x)
    if orig[fs] == 0xc8 and orig[fs+3] == 0 and sum(1 for y in wx if fs <= y < func_start_after(fs)) == 2:
        wiz = fs
assert wiz, "dial-in settings wizard not found"
wiz_end = func_start_after(wiz)
P['wiz'] = (wiz, wiz_end)
wxs = sorted(y for y in wx if wiz <= y < wiz_end)
def next_lcall(a, lim):
    for i in insns(a, lim):
        if i.mnemonic == 'lcall': return i.address
    die("no lcall after %x" % a)
P['get'] = lcall_target(next_lcall(wxs[0], wxs[0] + 0x20))
P['set'] = lcall_target(next_lcall(wxs[1], wxs[1] + 0x20))
dp = find_strs("dial_pulse"); dt = find_strs("dial_tone")
dpx = sorted(x for o in dp for x in xrefs(o, wiz, wiz_end)); assert dpx
tx = next_lcall(dpx[0], dpx[0] + 0x20); P['text'] = lcall_target(tx)
# the text is copied into the field buffer: `push ax ; lea ax,[bp-TEXTBUF] ; push ax ; lcall strcpy`
assert orig[tx+5:tx+7] == b'\x83\xc4' and orig[tx+8:tx+11] == b'\x50\x8d\x86', "strcpy after dial mode text at %x" % tx
P['textbuf'] = r16(orig, tx + 11); P['strcpy'] = lcall_target(tx + 14)
P['text_words'] = orig[tx + 7] // 2            # text(lang, key[, subdir]): 3 words in the 386 builds, 2 in the 286 builds
assert P['text_words'] in (2, 3), "text() arity"
TB = struct.pack('<H', P['textbuf']).hex(' ')
inp = one(find_strs("INP_dialmode_exp"), "INP_dialmode_exp")
inpx = one(xrefs(inp, wiz, wiz_end), "INP_dialmode_exp xref")
blks = find_bytes("6a 00 8d 86 %s 50 9a" % TB, wiz, inpx); assert blks, "field-1 pass block"
blk = blks[-1]
fars, nears = [], []
for i in insns(blk, inpx + 0x60):
    if i.mnemonic == 'lcall': fars.append(i.address)
    if i.mnemonic == 'push' and i.op_str == 'cs': nears.append(i.address)
    if i.mnemonic == 'mov' and i.op_str.startswith('si, 1'): break
assert len(fars) >= 9 and len(nears) >= 4, ("pass block shape", len(fars), len(nears))
F = [lcall_target(a) for a in fars[:9]]
P['f_settext'], P['f_setpos'], P['f_setmask'], P['f_setfont'], P['f_setmode'], P['f_draw'], P['fillrect'] = F[:7]
assert F[7] == F[8] == P['text'], "text lookups in pass block"
N = [near_target(a) for a in nears[:4]]
assert N[1] == N[2], "explain calls"
P['dots'], P['explain'], P['chooser'] = N[0], N[1], N[3]
def pushes_before(a, n):
    out = []; i = a
    while len(out) < n:
        i -= 3
        assert orig[i] == 0x68, "expected push imm16 at %x" % i
        out.insert(0, r16(orig, i + 1))
    return out
P['masks'] = pushes_before(fars[2], 3)
P['arial'], P['langvar'] = pushes_before(fars[3], 2)
assert orig[DSB + P['arial']:DSB + P['arial'] + 6] == b'arial\0', "arial font name"
lp = one(find_bytes("47 83 ff 01 7f 03 e9", nears[3], wiz_end), "wizard pass loop"); P['h5'] = lp + 1
P['wiz_loop_body'] = lp + 9 + struct.unpack('<h', orig[lp+7:lp+9])[0]
P['wiz_loop_exit'] = lp + 9
h6 = one(find_bytes("90 0e e8 ?? ?? 5f 5e c9 cb", wxs[1], wiz_end), "wizard cleanup call"); P['h6'] = h6
P['cleanup'] = near_target(h6 + 1)
seps = [x for x in find_bytes("6a 0f", wiz, lp) if b'\x4b\x01' in orig[x:x+10] and b'\x4a\x01' in orig[x:x+14]]
h4 = one(seps, "separator line"); P['h4'] = h4
P['fam386'] = orig[h4+2:h4+4] == b'\x66\x68'
if P['fam386']:
    assert orig[h4:h4+8] == b'\x6a\x0f\x66\x68\x4b\x01\xcd\x00', "separator (386)"; P['h4_len'] = 8
else:
    assert orig[h4:h4+5] == b'\x6a\x0f\x68\xcd\x00', "separator (286)"; P['h4_len'] = 5
# the 3rd field's label + box block: starts at the `push 0 ; push 0Fh ; push y ...` of the first
# INP_popnumber label draw -- preceded, in builds that have two label layouts, by `cmp [bp-flag],0 ; je`
pk = sorted(x for o in find_strs("INP_popnumber") for x in xrefs(o, h4 - 0x90, h4))
assert pk and len(pk) <= 2, ("INP_popnumber label draws before the separator", pk)
s0 = [x for x in find_bytes("6a 00 6a 0f", pk[0] - 0x14, pk[0])]; assert s0, "label draw argument list"
lb = s0[-1]
if orig[lb-6:lb-3] == b'\x83\x7e' + orig[lb-4:lb-3] and orig[lb-3] == 0 and orig[lb-2] == 0x74:
    lb -= 6                                    # `cmp word [bp-flag],0 ; je` guards the two variants
P['label_blk'] = (lb, h4)
P['box'] = lcall_target([i.address for i in insns(lb, h4) if i.mnemonic == 'lcall'][-1])
P['popkeys'] = pk

# ---------------------------------------------------------------- chooser
ch = P['chooser']; ch_end = func_start_after(ch); P['chooser_end'] = ch_end
assert orig[ch] == 0xc8 and orig[ch+3] == 0 and orig[ch+4:ch+9] == b'\x56\x57\x8b\x76\x08', "chooser prologue"   # enter N,0 ; push si ; push di ; mov si,[bp+8]
for i in insns(ch, ch_end):
    if i.mnemonic == 'call':      # only the 5-byte `nop ; push cs ; call` form can be carried over (copy() converts it)
        assert orig[i.address-2:i.address+1] == b'\x90\x0e\xe8', "unconvertible near call inside the chooser at %x" % i.address

# ---------------------------------------------------------------- report screen: h7a/h7b
cx = sorted(x for o in find_strs("CONNECT") for x in xrefs(o))
cx = [x for x in cx if orig[x+3:x+5] == b'\x8d\x86' and orig[x+7] == 0x50 and orig[x+8] == 0x9a]
assert 1 <= len(cx) <= 2, ("report CONNECT compares", [hex(x) for x in cx])   # one per label layout the build has
P['h7'] = []
for x in cx:
    frame = r16(orig, x + 5)
    p = x + 8 + 5 + 3
    assert orig[p:p+3] == b'\x0b\xc0\x74', "report je shape at %x" % p
    a_start = p + 4; a_end = a_start + orig[p+3]
    assert orig[a_end-2] == 0xeb, "report branch A jmp"
    join = a_end + struct.unpack('<b', orig[a_end-1:a_end])[0]
    assert orig[join:join+4] == b'\x80\x7e\xf0\x31', "report join"
    P['h7'].append((x, frame, a_start, a_end - 2, join))

# ---------------------------------------------------------------- DS strings for the new keys
def single_use(copies):
    # the copy used by fopen (preceded by push "w+"), not the one used by remove() at export start
    for c in copies:
        xs = xrefs(c)
        if len(xs) == 1 and orig[xs[0]-3] == 0x68 and orig[DSB + r16(orig, xs[0]-2):DSB + r16(orig, xs[0]-2) + 3] == b'w+' + bytes(1):
            return c, xs[0]
    die("no single-use fopen copy among %s" % [hex(c) for c in copies])
S_CONNTYPE, ncx = single_use(netcfg); keep_net = [c for c in netcfg if c != S_CONNTYPE][0]
assert len(xrefs(keep_net)) >= 1
S_SWITCH = P['s_wattcp_dup']; assert S_SWITCH != P['s_wattcp_keep']
assert len(xrefs(S_SWITCH)) == 1, "wattcp.cfg dup has other users"
P['s_conntype'], P['s_switch'], P['retarget'] = S_CONNTYPE, S_SWITCH, (ncx, keep_net)

if PROFILE:
    for k, v in P.items():
        print("  %-14s %s" % (k, ("%x" % v) if isinstance(v, int) else v))

# ================================================================ apply
w16(img, P['retarget'][0] + 1, P['retarget'][1])
img[DSB+S_CONNTYPE:DSB+S_CONNTYPE+20] = b'CONNTYPE\0'.ljust(20, b'\0')
img[DSB+S_SWITCH:DSB+S_SWITCH+23]     = b'/conntype\0'.ljust(23, b'\0')
w16(img, P['c0_stack'], BSS_END + SCR_SIZE)
w16(img, P['c0_bss'], BSS_END + SCR_SIZE)

strs = {}; blob = bytearray()
for name, s in [('S_ETH', b'ETHERNET'), ('S_MODEM', b'MODEM'), ('K_MODEM', b'conn_modem'),
                ('K_ETH', b'conn_ethernet'), ('K_INP', b'INP_conntype'), ('K_INP_EXP', b'INP_conntype_exp')]:
    strs[name] = SCR_STR + len(blob); blob += s + b'\0'

class Asm:
    def __init__(self): self.b = bytearray(); self.relocs = []; self.labels = {}; self.fix = []
    def here(self): return len(self.b)
    def emit(self, hexstr): self.b += bytes.fromhex(hexstr)
    def label(self, n): self.labels[n] = self.here()
    def lcall(self, tgt):
        seg, off = tgt
        self.b += b'\x9a' + struct.pack('<HH', off, seg); self.relocs.append(self.here() - 2)
    def lcall_patch(self, lab):
        self.b += b'\x9a\0\0' + struct.pack('<H', PATCH_SEG); self.relocs.append(self.here() - 2)
        self.fix.append((self.here() - 4, lab, 'abs'))
    def jcc(self, op, lab): self.b += bytes([op, 0]); self.fix.append((self.here() - 1, lab, 'rel8'))
    def jcc16(self, op, lab): self.b += bytes([0x0f, op + 0x10, 0, 0]); self.fix.append((self.here() - 2, lab, 'rel16'))
    def jmp16(self, lab): self.b += bytes([0xe9, 0, 0]); self.fix.append((self.here() - 2, lab, 'rel16'))
    def call(self, lab): self.b += b'\xe8\0\0'; self.fix.append((self.here() - 2, lab, 'rel16'))
    def push16(self, v): self.b += b'\x68' + struct.pack('<H', v & 0xffff)
    def push32(self, v): self.b += b'\x66\x68' + struct.pack('<I', v)
    def ret_to(self, lin):
        self.emit('58 5a 52'); self.push16(lin - MAINB); self.emit('cb')
    def copy(self, start, end, imm16map=None, raw=None):
        """copy original code [start,end); rewrite push immediates via imm16map (each half of a
        push imm32 too); raw = {abs_off: bytes} exact patches; far calls get relocations."""
        base = self.here(); blk = bytearray(orig[start:end]); imm16map = imm16map or {}
        for i in insns(start, end):
            if i.mnemonic == 'call':
                # `nop ; push cs ; call rel16` (5 bytes) is a far call in disguise: make it a real
                # one, since the copy lives in another segment
                a = i.address - 2
                assert orig[a:a+3] == b'\x90\x0e\xe8', "near call without nop/push cs inside copied block at %x" % i.address
                seg, off = far(near_target(a + 1))
                blk[a-start:a-start+5] = b'\x9a' + struct.pack('<HH', off, seg)
                self.relocs.append(base + (a - start) + 3)
                continue
            if i.mnemonic == 'lcall': self.relocs.append(base + (i.address - start) + 3)
            if i.mnemonic != 'push': continue
            bb = i.bytes
            if bb[0] == 0x68 and len(bb) == 3:
                v = r16(orig, i.address + 1)
                if v in imm16map: w16(blk, i.address - start + 1, imm16map[v])
            elif bb[:2] == b'\x66\x68':
                for h_ in (0, 2):
                    v = r16(orig, i.address + 2 + h_)
                    if v in imm16map: w16(blk, i.address - start + 2 + h_, imm16map[v])
            elif bb[0] == 0x6a and bb[1] in imm16map:
                assert imm16map[bb[1]] < 0x80; blk[i.address - start + 1] = imm16map[bb[1]]
        for off, nb in (raw or {}).items():
            assert start <= off < end; blk[off-start:off-start+len(nb)] = nb
        self.b += blk
    def resolve(self):
        for pos, lab, kind in self.fix:
            t = self.labels[lab]
            if kind == 'rel16': w16(self.b, pos, (t - (pos + 2)) & 0xffff)
            elif kind == 'abs': w16(self.b, pos, t)
            else:
                rel = t - (pos + 1); assert -128 <= rel < 128, (lab, rel); self.b[pos] = rel & 0xff

A = Asm()
ETH_WORDS = (0x7465, 0x6568, 0x6e72, 0x7465)   # "et" "he" "rn" "et" lower-cased

def cmp_ethernet(A, lab_no):
    """bx -> text ; jumps to lab_no unless it starts with ETHERNET (case-insensitive)"""
    for i, word in enumerate(ETH_WORDS):
        A.emit('8b 47 %02x' % (i*2) if i else '8b 07'); A.emit('0d 20 20'); A.emit('3d'); A.b += struct.pack('<H', word)
        A.jcc(0x75, lab_no)

A.label('is_eth')
A.emit('55 8b ec 81 ec 80 00 53')
A.emit('6a 00 8d 46 80 50'); A.push16(S_CONNTYPE); A.lcall(P['get']); A.emit('83 c4 06')
A.emit('8d 5e 80 33 c9')
cmp_ethernet(A, 'is_eth_done')
A.emit('80 7f 08 00'); A.jcc(0x75, 'is_eth_done'); A.emit('41')
A.label('is_eth_done'); A.emit('8b c1 5b 8b e5 5d cb')

A.label('h0'); A.call('is_eth_near'); A.emit('0b c0'); A.jcc(0x75, 'h0_skip')
A.push16(P['s_wattcp_keep']); A.lcall(P['remove']); A.emit('59'); A.label('h0_skip'); A.emit('cb')

A.label('h1'); A.call('is_eth_near'); A.emit('0b c0'); A.jcc(0x75, 'h1_eth')
A.emit('58 5a 8d 9e'); A.b += struct.pack('<H', P['h1_frame']); A.emit('53 52 50 cb')
A.label('h1_eth'); A.ret_to(P['h1_resume'])

A.label('h2'); A.call('is_eth_near'); A.emit('0b c0'); A.jcc(0x75, 'h2_eth')
A.emit('58 5a'); A.push16(P['s_wplus']); A.push16(P['s_wattcp_keep']); A.emit('52 50 cb')
A.label('h2_eth'); A.ret_to(P['h2_join'])

A.label('h3'); A.emit('8d 46 9a 50'); A.push16(S_SWITCH); A.lcall(P['stricmp']); A.emit('83 c4 04 0b c0'); A.jcc(0x75, 'h3_not')
A.call('is_eth_near'); A.emit('89 46 fe'); A.ret_to(P['h3_epilog'])
A.label('h3_not'); A.emit('5b 5a'); A.push16(P['s_export']); A.emit('52 53 8d 46 9a cb')
A.label('is_eth_near'); A.lcall_patch('is_eth'); A.emit('c3')

A.label('init_scr')
A.emit('80 3e'); A.b += struct.pack('<H', SCR_FLAG); A.emit('00'); A.jcc(0x75, 'init_done')
A.emit('56 57 1e 06 fc 1e 07')
A.emit('bf'); A.b += struct.pack('<H', SCR_STR)
A.emit('be'); A.fix.append((A.here(), 'strblob', 'abs')); A.emit('00 00')
A.emit('b9'); A.b += struct.pack('<H', len(blob))
A.emit('0e 1f f3 a4 07 1f')
A.emit('c6 06'); A.b += struct.pack('<H', SCR_FLAG); A.emit('01')
A.emit('5f 5e')
A.label('init_done'); A.emit('c3')

Y4 = 0xb1
A.label('h4')
A.call('init_scr')
A.emit('6a 00'); A.push16(SCR_VALUE); A.push16(S_CONNTYPE); A.lcall(P['get']); A.emit('83 c4 06')
A.emit('bb'); A.b += struct.pack('<H', strs['K_MODEM'])
A.emit('c6 06'); A.b += struct.pack('<H', SCR_RESULT); A.emit('4d')
A.call('is_eth_near'); A.emit('0b c0'); A.jcc(0x74, 'h4_txt')
A.emit('bb'); A.b += struct.pack('<H', strs['K_ETH'])
A.emit('c6 06'); A.b += struct.pack('<H', SCR_RESULT); A.emit('45')
A.label('h4_txt')
if P['text_words'] == 3: A.emit('6a 00')
A.emit('53'); A.push16(SCR_NUL); A.lcall(P['text']); A.emit('83 c4 %02x' % (2 * P['text_words']))
A.emit('50'); A.push16(SCR_TEXT); A.lcall(P['strcpy']); A.emit('83 c4 04')
keymap = {0x99: Y4, 0xaf: Y4 + 0x16, 0xcd: Y4 + 0x34}
for x in P['popkeys']:
    keymap[r16(orig, x + 1)] = strs['K_INP']
    keymap[r16(orig, x + 4)] = SCR_NUL
A.copy(P['label_blk'][0], P['label_blk'][1], keymap)
A.emit('58 5a 6a 0f')
if P['fam386']: A.push32(((Y4 + 0x34) << 16) | 0x014b)
else:           A.push16(Y4 + 0x34)
A.emit('52 50 cb')

A.label('h5')
A.call('init_scr')
A.emit('0b ff'); A.jcc16(0x74, 'h5_next')
A.emit('0b f6'); A.jcc16(0x75, 'h5_next')
A.emit('6a 00'); A.push16(SCR_TEXT); A.lcall(P['f_settext']); A.emit('83 c4 04')
A.push32(((Y4 + 0x1b) | 0x200) << 16 | 0x28); A.push32(0x00140104); A.push32((Y4 + 0x1b) << 16 | 0x28)
A.lcall(P['f_setpos']); A.emit('83 c4 0c')
A.emit('6a 00')
for m_ in P['masks']: A.push16(m_)
A.lcall(P['f_setmask']); A.emit('83 c4 08')
A.emit('66 6a 00 6a 0e'); A.push16(P['arial']); A.push16(P['langvar']); A.lcall(P['f_setfont']); A.emit('83 c4 0a')
A.emit('6a 01 6a 07 6a 00'); A.lcall(P['f_setmode']); A.emit('83 c4 06')
A.emit('83 ff 01'); A.jcc(0x75, 'h5_p1')
A.emit('6a ff'); A.lcall(P['f_draw']); A.emit('59'); A.jmp16('h5_next')
A.label('h5_p1')
A.emit('83 ff 02'); A.jcc16(0x75, 'h5_next')
A.emit('6a 10'); A.push32(0x01df027f); A.push32(0x00eb0000); A.lcall(P['fillrect']); A.emit('83 c4 0a')
A.push32(0x00030004); A.emit('6a 20'); A.lcall(far(P['dots'])); A.emit('83 c4 06')
def explain(key, flag):
    """text(key) -> explain(text, flag): the flag word is pushed first and survives text()'s cleanup"""
    if P['text_words'] == 3:                            # low word: text()'s 3rd arg, high word: the flag
        A.emit('66 6a 00') if flag == 0 else A.push32(flag << 16)
    else:                    A.emit('6a %02x' % flag)
    A.push16(key); A.push16(SCR_NUL); A.lcall(P['text']); A.emit('83 c4 %02x' % (2 * P['text_words']))
    A.emit('50'); A.lcall(far(P['explain'])); A.emit('83 c4 04')
explain(strs['K_INP'], 0)
explain(strs['K_INP_EXP'], 1)
A.push16(SCR_RESULT); A.push16(SCR_TEXT); A.lcall_patch('chooser'); A.emit('83 c4 04')
A.emit('0b c0'); A.jcc(0x75, 'h5_ok'); A.emit('be 01 00')
A.label('h5_ok')
A.emit('bb'); A.b += struct.pack('<H', strs['S_MODEM'])
A.emit('80 3e'); A.b += struct.pack('<H', SCR_RESULT); A.emit('45'); A.jcc(0x75, 'h5_val')
A.emit('bb'); A.b += struct.pack('<H', strs['S_ETH'])
A.label('h5_val'); A.emit('53'); A.push16(SCR_VALUE); A.lcall(P['strcpy']); A.emit('83 c4 04')
A.label('h5_next')
A.emit('83 ff 01'); A.jcc(0x7f, 'h5_exit'); A.ret_to(P['wiz_loop_body'])
A.label('h5_exit'); A.ret_to(P['wiz_loop_exit'])

A.label('h6')
A.emit('0b f6'); A.jcc(0x75, 'h6_skip')
A.emit('6a 00'); A.push16(SCR_VALUE); A.push16(S_CONNTYPE); A.lcall(P['set']); A.emit('83 c4 06')
A.label('h6_skip'); A.lcall(far(P['cleanup'])); A.emit('cb')

for n, (site, frame, a_start, a_end, join) in enumerate(P['h7']):
    lab = 'h7%s' % 'ab'[n]
    A.label(lab)
    A.emit('8d 9e'); A.b += struct.pack('<H', frame)
    cmp_ethernet(A, lab + '_no')
    colour = [i.address for i in insns(a_start, a_end) if i.bytes == b'\x6a\x0a']
    A.copy(a_start, a_end, raw={one(colour, "colour push in report branch"): b'\x6a\x0f'})
    A.ret_to(join)
    A.label(lab + '_no')
    A.emit('5b 5a'); A.push16(r16(orig, site + 1)); A.emit('8d 86'); A.b += struct.pack('<H', frame); A.emit('50 52 53 cb')

A.label('chooser')
raw = {}
ch, ch_end = P['chooser'], P['chooser_end']
first_store = one(find_bytes("c7 46 fe 01 00 c6 04 54", ch, ch + 0x20), "chooser default store") + 5
raw[first_store] = b'\x90\x90\x90'
for pat, rep in (("80 3f 54", b'\x80\x3f\x4d'), ("80 3f 50", b'\x80\x3f\x45'), ("c6 04 50", b'\xc6\x04\x45')):
    for x in find_bytes(pat, ch, ch_end): raw[x] = rep
for x in find_bytes("c6 04 54", ch, ch_end):
    if x != first_store: raw[x] = b'\xc6\x04\x4d'
assert sum(1 for x in raw if orig[x:x+3] == b'\xc6\x04\x54') == 2 and sum(1 for x in raw if orig[x:x+3] == b'\xc6\x04\x50') == 1, "chooser result stores"
assert sum(1 for x in raw if orig[x:x+3] == b'\x80\x3f\x54') == 1 and sum(1 for x in raw if orig[x:x+3] == b'\x80\x3f\x50') == 1, "chooser key compares"
kmap = {}
for o in dt: kmap[o] = strs['K_MODEM']; kmap[o - 1] = SCR_NUL
for o in dp: kmap[o] = strs['K_ETH'];   kmap[o - 1] = SCR_NUL
A.copy(ch, ch_end, kmap, raw)

A.label('strblob'); A.b += blob
A.resolve()
patch = bytes(A.b)
assert len(patch) <= PATCH_LEN, len(patch)
patch = patch.ljust(PATCH_LEN, b'\0')

# ---------------------------------------------------------------- hook sites and immediates
hooked = []
def hook(site, oldlen, lab):
    code = (b'\x9a' + struct.pack('<HH', A.labels[lab], PATCH_SEG)).ljust(oldlen, b'\x90')
    img[site:site+oldlen] = code; hooked.append((site, site + oldlen)); return site + 3
new_relocs = [hook(P['h0'], 9, 'h0'), hook(P['h1'], 5, 'h1'), hook(P['h2'], 6, 'h2'), hook(P['h3'], 6, 'h3'),
              hook(P['h4'], P['h4_len'], 'h4'), hook(P['h5'], 8, 'h5'), hook(P['h6'], 5, 'h6')]
for n, (site, frame, a_start, a_end, join) in enumerate(P['h7']):
    new_relocs.append(hook(site, 7, 'h7%s' % 'ab'[n]))

YMAP = {0x5d: 0x51, 0x99: 0x81, 0x73: 0x67, 0x91: 0x85, 0xaf: 0x97, 0xcd: 0xb5,
        0x78: 0x6c, 0xb4: 0x9c, 0x278: 0x26c, 0x2b4: 0x29c}
nlay = 0
for i in insns(wiz, lp):                       # the field area only: not the separator hook, not the yes/no dialog
    if i.mnemonic != 'push' or P['h4'] <= i.address < P['h4'] + P['h4_len']: continue
    bb = i.bytes
    if bb[0] == 0x68:
        v = r16(orig, i.address + 1)
        if v in YMAP: w16(img, i.address + 1, YMAP[v]); nlay += 1
    elif bb[0] == 0x6a and bb[1] in YMAP:
        img[i.address + 1] = YMAP[bb[1]]; nlay += 1
    elif bb[:2] == b'\x66\x68':
        for h_ in (0, 2):
            v = r16(orig, i.address + 2 + h_)
            if v in YMAP: w16(img, i.address + 2 + h_, YMAP[v]); nlay += 1
nexp = 8 + 2 * len(P['popkeys'])           # 2 labels x layouts + 2 boxes x 2 + 2 inputs x 2
assert nlay == nexp, "expected %d layout immediates in the wizard, found %d" % (nexp, nlay)
ndots = 0; prev = []
for i in insns(wiz, wiz_end):
    if i.mnemonic == 'push' and i.op_str == 'cs' and orig[i.address+1] == 0xe8 and near_target(i.address) == P['dots']:
        for pj in reversed(prev[-3:]):
            bb = pj.bytes
            if bb[:3] == b'\x66\x6a\x03': img[pj.address + 2] = 4; ndots += 1; break
            if bb[:4] == b'\x66\x68\x03\x00': img[pj.address + 2] = 4; ndots += 1; break
            if bb == b'\x6a\x03': img[pj.address + 1] = 4; ndots += 1; break
    prev.append(i)
assert ndots == 4, "expected 4 step-dot count pushes, found %d" % ndots

relocs = []; dropped = 0
for k in range(e_crlc):
    off, seg = struct.unpack('<HH', d[e_lfarlc + 4*k: e_lfarlc + 4*k + 4]); pos = seg * 16 + off
    if any(a <= pos < b for a, b in hooked): dropped += 1; continue
    val = r16(orig, pos)
    if val >= DGROUP: w16(img, pos, val + SHIFT)
    if pos >= DSB: seg += SHIFT
    relocs.append((off, seg))
assert dropped == 1, "expected to drop the one relocation of remove() at h0, dropped %d" % dropped
newimg = img[:DSB] + patch + img[DSB:]
for pos in new_relocs: relocs.append((pos - MAINB, P['main_seg']))
for pos in A.relocs:   relocs.append((pos, PATCH_SEG))

out = bytearray(d[:HDR])
assert e_lfarlc + 4*len(relocs) <= HDR, "no room in the header for the new relocations"
for k, (off, seg) in enumerate(relocs): out[e_lfarlc + 4*k: e_lfarlc + 4*k + 4] = struct.pack('<HH', off, seg)
total = HDR + len(newimg)
w16(out, 2, total % 512); w16(out, 4, (total + 511) // 512); w16(out, 6, len(relocs)); w16(out, 14, e_ss + SHIFT)
out += newimg
open(DST, 'wb').write(out)
print("wrote %s: %d bytes, %d relocs (+%d), PATCH used %d/%d, DGROUP %04x -> %04x, SCR at DS:%04x, %s family"
      % (DST, len(out), len(relocs), len(relocs) - e_crlc, len(A.b), PATCH_LEN, DGROUP, DGROUP + SHIFT, SCR, "386" if P['fam386'] else "286"))
