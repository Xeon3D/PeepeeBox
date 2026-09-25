#!/usr/bin/env python3
r"""Run a release's own HASP library under unicorn against a software part.

    python tools/dongcap/hasplib.py <MENU.EXE> [-v]

I.G.O. 3's boot check is two library calls (MENU.EXE, the function that ends in
"dongle error"):

    hasp(5,    0, port, 6B91, 24A3, ...)            p3 -> the port, kept at [0x7E01]
    hasp(0x3C, 0, port, 6B91, 24A3, 0, 20, DS, 0x50F6)
                                                    p3 must be 0, and the 20 bytes
                                                    must come out as a path

This loads the real MZ, finds those call sites and the library by pattern, makes the
same two calls, and answers the wire with a Python port of the device model in
src/device/dongle_photoplay.c.  So whatever the library does with the part's answers
is the library's real code, not a reading of it -- which is how the two faults that
kept I.G.O. 3 from booting were found (docs/research/37):

  * the liveness probe arrives as 9E, with bit 7 set, and was answered from the
    signature (0) instead of as the probe (1) -- the library then refuses
    HaspEncodeData with status -8, which is the "dongle error" on screen;
  * the keyed round's queries on a bit-7 release cannot be found by the bit-4 edge,
    because the session payloads move bit 4 too; they are found at the STATUS read,
    by the three writes before it (p, p|0x10, p).

With both, the boot check's two rounds are 40 and 40 consultations and the block
decodes to "c:/foto/gamestat.old".

Needs `unicorn` (pip install unicorn).  Nothing here touches hardware.
"""
import argparse
import collections
import re
import struct
import sys

from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_INTR
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AX,
                               UC_X86_REG_BP, UC_X86_REG_CS, UC_X86_REG_CX,
                               UC_X86_REG_DS, UC_X86_REG_DX, UC_X86_REG_ES,
                               UC_X86_REG_IP, UC_X86_REG_SP, UC_X86_REG_SS)

LOAD = 0x1000
SENT = 0xF0000          # the far return lands here and emulation stops

# ---------------------------------------------------------------------------------
# The part -- a port of dongle_photoplay.c's hd_probe path, with the two fixes
# ---------------------------------------------------------------------------------
HD_WORDS, HD_START, HD_RECORD = 64, 8, 112
HD_CS, HD_SK, HD_DI, HD_DO = 0x02, 0x20, 0x40, 0x20
HD_SIGNATURE = 0xCEFF0AFFCECE0A0A
HS_SWEEP_A = 0xF57A37E78F8FBDDA
# Each password pair's sweep table, address a at bit 63 - a -- measured on the parts,
# docs/research-v2/10.2.  A sweep step answers the bit at its payload's address.
SWEEP_TABLE = {0x68BB: 0x225E9EDE445CDCDC, 0x7477: 0x423ED3BFE2BEF3BF,
               0x6B91: 0x0EE697F74CE4D5F5}
HS_SWEEP_W = bytes([
    0x78, 0x6A, 0x56, 0x26, 0x02, 0x18, 0x6E, 0x3C, 0x2E, 0x3A, 0x72, 0x52,
    0x0C, 0x64, 0x70, 0x74, 0x2E, 0x24, 0x78, 0x36, 0x22, 0x0C, 0x1C, 0x26,
    0x78, 0x28, 0x68, 0x54, 0x40, 0x0C, 0x70, 0x52, 0x0C, 0x46, 0x44, 0x2E,
    0x6A, 0x68, 0x70, 0x78, 0x6A, 0x7E, 0x08, 0x40, 0x1C, 0x1C, 0x1A, 0x16,
    0x12, 0x50, 0x36, 0x0C, 0x58, 0x2C, 0x6C, 0x30, 0x04, 0x3C, 0x4E, 0x12,
    0x20, 0x14, 0x6A, 0x44])
FIELDS = [907, 98765, 120672, 170898, 75902, 2205]

# pass1 -> (v6, v7, picture key, register after the preamble, session carries bit 7)
PARTS = {
    0x68BB: (160678, -371202944, 0x3B227944, 0x7DF, 0),     # I.G.O. 2
    0x6B91: (160678, -738037894, 0xAB32E970, 0x5DF, 1),     # I.G.O. 3
}

IDLE, OP, ADDR, READ, WRITE, DONE = range(6)


def mode_term(mode, cur, i5):
    """What HaspEncodeData modes 1..4 add to the round's feedback bit -- the same shift
    register, key and starting state as mode 0, one term more.  Read off a 6B91/24A3 part:
    every one of 4,648 complete rounds in 2,000 live calls fits (docs/research-v2/10.10)."""
    p = bin(i5 & 0x1F).count('1') & 1
    if mode == 1:
        return (cur >> 3) & 1
    if mode == 2:
        return 1 ^ p
    if mode == 3:
        return (cur ^ (cur >> 3)) & 1
    if mode == 4:
        return (1 ^ p ^ (cur >> 6)) & 1
    return 0


class Part:
    def __init__(self, pass1, territory='DE', base=0x378, old_probe=False,
                 edge_only=False, old_mw=False, fixed_sweep=False, modes=None):
        v6, v7, self.key, self.init, self.sess_hi = PARTS[pass1]
        self.base = base
        self.old_probe = old_probe          # reproduce the pre-fix probe match
        self.readtime = self.sess_hi and not edge_only
        self.mw_hi = not old_mw
        self.alt_sweep = self.sess_hi and not fixed_sweep
        self.sweep_serial = 0
        self.sweep_table = SWEEP_TABLE.get(pass1, SWEEP_TABLE[0x68BB])
        rec = bytearray(HD_RECORD)
        rec[0:2] = territory.encode()
        rec[3:17] = b'sion 2000 (SP)'
        for n, v in enumerate(FIELDS + [v6, v7]):
            rec[30 + 4 * n:34 + 4 * n] = struct.pack('<I', v & 0xFFFFFFFF)
        self.mem = []
        for i in range(HD_WORDS):
            j = i - HD_START
            plain = (rec[j * 2 + 1] << 8) | rec[j * 2] if 0 <= j and j * 2 + 1 < HD_RECORD else 0
            self.mem.append((plain ^ ((i - HD_START) & 0xFFFF) ^ pass1
                             ^ (0xFF00 if i < HD_START else 0)) & 0xFFFF)
        self.data, self.ctrl = 0, 0x0C
        self.ph = self.n = self.op = self.addr = self.sr = self.sk = self.do = 0
        self.ready = self.wen = 0
        self.ramping = self.ramp_prev = self.sweep = 0
        self.t_last = self.t_cur = self.t_pending = self.t_ans = self.t_hold = 0
        self.recent = []
        self.burst, self.bursts = 0, []
        self.log = collections.Counter()
        # EncodeData modes 1..4, answered from the known-answer table: mode -> {L: g(L)}
        self.modes = modes or {}
        self.modeclk = self.mode = self.qn = 0
        self.cand = None
        self.forced_cache = {}

    def forced(self, g):
        """decisions c_8..c_39 of a round whose output is g (softpart._forced)"""
        c = self.forced_cache.get(g)
        if c is None:
            c, acc = {}, 0
            for b in range(31, -1, -1):
                if ((g ^ acc) >> b) & 1:
                    c[8 + b] = 1
                    acc ^= (0x80500062 >> (31 - b)) & 0xFFFFFFFF
                else:
                    c[8 + b] = 0
            self.forced_cache[g] = c
        return c

    def query(self, val):
        """one consultation.  Mode 0 is the shift register; modes 1..4 are answered so
        that the host's round reproduces the table, for whichever table input the queries
        show the host is working on."""
        i5 = ((val >> 1) & 7) | ((val >> 2) & 0x18)
        ans = self.step(val)
        if self.qn == 0:
            t = self.modes.get(self.mode)
            self.cand = [[L, g, L, 0] for L, g in t.items()] if t else None
            if self.mode:
                self.log['mode%d round' % self.mode] += 1
        if self.cand is not None:
            self.cand = [c for c in self.cand if ((c[2] >> (8 * c[3])) & 0x1F) == i5]
            if self.cand and 7 <= self.qn <= 38:
                L, g, data, idx = self.cand[0]
                ans = self.forced(g)[self.qn + 1] ^ (data & 1)
            for c in self.cand:
                data = c[2]
                c[3] = ((data & 1) << 1) | ans
                c[2] = (data >> 1) if (data & 1) == ans else ((data >> 1) ^ 0x80500062)
            if not self.cand:
                self.cand = None
                self.log['mode%d not in table' % self.mode] += 1
            elif self.qn == 38:
                self.log['mode%d answered from table' % self.mode] += 1
        self.qn += 1
        return ans

    def step(self, val):
        i5 = ((val >> 1) & 7) | ((val >> 2) & 0x18)
        st = (self.key >> i5) & 1
        b0 = i5 ^ ((st ^ 1) & (i5 >> 3)) ^ (i5 >> 4)
        b0 ^= self.t_cur >> 10
        b0 ^= self.t_cur >> 7
        if i5 & 2:
            b0 ^= self.t_cur >> 5
        if i5 & 4:
            b0 ^= self.t_cur >> 8
        b0 ^= mode_term(self.mode, self.t_cur, i5)
        self.t_cur = (((self.t_cur ^ ((i5 & 1) << 2)) << 1) | (b0 & 1)) & 0xFFFFFFFF
        self.burst += 1
        self.log['query'] += 1
        return ((self.t_cur >> 11) ^ st) & 1

    def t_data(self, val):
        rose = val & ~self.t_last & 0xFF
        if val == 0x84:                                    # the 84/A4 run opens a round
            self.modeclk = 0
        elif val == 0xDA and self.t_last == 0xCA:          # one mode clock
            self.modeclk += 1
        if self.sess_hi and val != self.t_hold:
            self.t_pending = 0
        if val & 0x80:
            if rose & 1:                                   # command byte: preamble
                if self.burst:
                    self.bursts.append(self.burst)
                self.burst, self.t_cur, self.t_pending = 0, self.init, 0
                # the command byte that opens a round comes after the mode clocks, so
                # latch here: the round's own first query can be a CA/DA triple too
                self.qn, self.cand, self.mode = 0, None, self.modeclk
            elif not self.sess_hi and rose & 0x10:         # I.G.O. 2's edge query
                self.t_ans, self.t_pending, self.t_hold = self.step(val), 1, val
        self.t_last = val

    def mw(self, val):
        sel, clk, dat = val & HD_CS, val & HD_SK, 1 if val & HD_DI else 0
        if not sel:
            self.ph, self.n = IDLE, 0
        elif clk and not self.sk:
            if self.ph == IDLE:
                if dat:
                    self.ph, self.n, self.op, self.ready = OP, 0, 0, 0
            elif self.ph == OP:
                self.op = (self.op << 1) | dat
                self.n += 1
                if self.n == 2:
                    self.ph, self.n, self.addr = ADDR, 0, 0
            elif self.ph == ADDR:
                self.addr = (self.addr << 1) | dat
                self.n += 1
                if self.n == 6:
                    self.n = 0
                    if self.op == 2:
                        self.sr, self.ph = self.mem[self.addr], READ
                        self.log['record word'] += 1
                    elif self.op == 1:
                        self.sr, self.ph = 0, WRITE
                    elif self.op == 3:
                        if self.wen:
                            self.mem[self.addr] = 0xFFFF
                        self.ready, self.ph = 1, DONE
                    else:
                        self.wen, self.ph = (self.addr >> 4) == 3, DONE
            elif self.ph == READ:
                if self.n < 16:
                    self.do = (self.sr >> (15 - self.n)) & 1
                self.n += 1
            elif self.ph == WRITE:
                self.sr = ((self.sr << 1) | dat) & 0xFFFF
                self.n += 1
                if self.n == 16:
                    if self.wen:
                        self.mem[self.addr] = self.sr
                    self.ready, self.ph = 1, DONE
        self.sk = clk

    def write(self, off, v):
        if off == 0:
            if not self.recent or self.recent[-1] != v:
                self.recent = (self.recent + [v])[-3:]
            self.data = v
            self.t_data(v)
            # I.G.O. 3 clocks its record read with bit 7 set too (9E/BE, DE/FE, 9C);
            # those frames carry bits 2..4 set, which keeps the session payloads out.
            if not self.sess_hi or not v & 0x80 or (self.mw_hi and (v & 0x1C) == 0x1C):
                self.mw(v)
        elif off == 2:
            self.ctrl = v

    def status(self):
        r, self.recent = self.recent, []
        if self.readtime and len(r) == 3:
            p, q, p2 = r
            if p == p2 and q == p | 0x10 and p & 0x80 and not p & 0x11:
                self.ph = self.n = 0
                return HD_DO if self.query(q) else 0
        if self.t_pending:
            if not self.sess_hi:
                self.t_pending = 0
            return HD_DO if self.t_ans else 0
        if self.ph == READ:
            return HD_DO if self.do else 0
        w = self.data
        addr = (w >> 1) & 0x3F
        sw = w & 0x7F if self.sess_hi else w
        self.ph = self.n = 0
        if self.ramping and w == (self.ramp_prev + 2) & 0xFF:
            self.ramp_prev, self.sweep, lab = w, 0, 'ramp'
            bit = (HD_SIGNATURE >> addr) & 1
        elif w in (0x00, 0x80):
            self.ramping, self.ramp_prev, self.sweep, lab = 1, w, 0, 'ramp'
            bit = (HD_SIGNATURE >> addr) & 1
        elif self.sweep < 64 and sw == HS_SWEEP_W[self.sweep]:
            bit = self.sweep_bit(self.sweep)
            self.sweep, self.ramping, lab = self.sweep + 1, 0, 'sweep'
        elif sw == HS_SWEEP_W[0]:
            bit = self.sweep_bit(0)
            self.sweep, self.ramping, lab = 1, 0, 'sweep'
        elif (w if self.old_probe else sw) == 0x1E:
            bit, self.ramping, self.sweep, lab = 1, 0, 0, 'probe'
        else:
            bit = (HD_SIGNATURE >> addr) & 1
            self.ramping, self.sweep, lab = 0, 0, 'other'
        self.log[lab] += 1
        return HD_DO if (self.ready or bit) else 0

    def sweep_bit(self, n):
        # I.G.O. 3's library sweeps twice and flags the part if any folded byte repeats
        # (core 0x20EA); a real part never answers the same twice.  XOR every byte of
        # sweep k with k mod 255, so any two sweeps within 255 differ in all eight.
        bit = (self.sweep_table >> (63 - (HS_SWEEP_W[n] >> 1))) & 1
        if self.alt_sweep:
            bit ^= (self.sweep_serial >> (7 - (n & 7))) & 1
            if n == 63:
                self.sweep_serial = (self.sweep_serial + 1) % 255
        return bit

    def read(self, off):
        return {0: self.data, 1: self.status(), 2: self.ctrl}.get(off, 0xFF)


# ---------------------------------------------------------------------------------
# The guest
# ---------------------------------------------------------------------------------
class Menu:
    """MENU.EXE, relocated to LOAD, with the boot check's call sites located."""

    def __init__(self, path):
        d = open(path, 'rb').read()
        self.d = d
        self.hdr = struct.unpack('<H', d[8:10])[0] * 16
        nrel, rtab = struct.unpack('<H', d[6:8])[0], struct.unpack('<H', d[24:26])[0]
        img = bytearray(d[self.hdr:])
        for i in range(nrel):
            o, s = struct.unpack('<HH', d[rtab + 4 * i:rtab + 4 * i + 4])
            p = s * 16 + o
            img[p:p + 2] = struct.pack('<H', (struct.unpack('<H', img[p:p + 2])[0] + LOAD) & 0xFFFF)
        self.img = bytes(img)

        # push pass2:pass1; push [port]; push [bp-0xa]; push 0x3C; lcall lib
        # add sp,0x1a; cmp [bp-6],0; je; push <"dongle error">
        m = re.search(rb'\x66\x68(..)(..)\xff\x36(..)\xff\x76\xf6\x6a\x3c\x9a(....)'
                      rb'\x83\xc4\x1a\x83\x7e\xfa\x00\x74.\x68(..)', d, re.S)
        if not m:
            raise SystemExit('no HaspEncodeData boot check found in %s' % path)
        self.pass1, self.pass2 = struct.unpack('<HH', m.group(1) + m.group(2))
        self.lib_off, lib_seg = struct.unpack('<HH', m.group(4))
        self.lib = lib_seg + LOAD
        err_off = struct.unpack('<H', m.group(5))[0]
        at = d.find(b'dongle error')
        self.dgroup = (at - err_off - self.hdr) // 16 + LOAD
        # the 20-byte block: mov [bp-4],0x14 ... mov [bp-8],<off>  in the 0x3C/0x3D callers
        b = re.search(rb'\xc7\x46\xfc\x14\x00\x8c\x5e\xfa\xc7\x46\xf8(..)', d, re.S)
        self.block = struct.unpack('<H', b.group(1))[0] if b else 0x50F6


class Machine:
    def __init__(self, menu, part):
        self.menu, self.part = menu, part
        self.uc = uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x110000)
        uc.mem_write(LOAD * 16, menu.img)
        uc.mem_write(0x408, struct.pack('<HHH', part.base, 0, 0))   # BDA: LPT1
        uc.mem_write(0x410, struct.pack('<H', 0x4061))
        self.clock = 0
        self.writes = self.reads = 0
        uc.hook_add(UC_HOOK_INSN, self.on_in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self.on_out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INTR, self.on_intr)

    def on_in(self, uc, port, size, ud):
        self.reads += 1
        off = port - self.part.base
        return self.part.read(off) if 0 <= off <= 2 else 0xFF

    def on_out(self, uc, port, size, value, ud):
        self.writes += 1
        off = port - self.part.base
        if 0 <= off <= 2:
            self.part.write(off, value & 0xFF)

    def on_intr(self, uc, v, ud):
        # The library seeds a generator from time(), which is Borland's getdate /
        # gettime / getdate-again loop; give it a clock that moves.
        ah = uc.reg_read(UC_X86_REG_AX) >> 8
        if v == 0x21 and ah == 0x2C:
            self.clock += 1
            cs = self.clock + 12 * 360000
            uc.reg_write(UC_X86_REG_CX, ((cs // 360000) % 24) << 8 | (cs // 6000) % 60)
            uc.reg_write(UC_X86_REG_DX, ((cs // 100) % 60) << 8 | cs % 100)
        elif v == 0x21 and ah == 0x2A:
            uc.reg_write(UC_X86_REG_CX, 2003)
            uc.reg_write(UC_X86_REG_DX, 0x0701)
            uc.reg_write(UC_X86_REG_AX, 2)

    def call(self, seg, off, args=(), maxinsn=200_000_000):
        """far-call seg:off (image-relative seg) with word args; return AX, DX, error"""
        uc, ds = self.uc, self.menu.dgroup
        w = lambda o, v: uc.mem_write(ds * 16 + o, struct.pack('<H', v & 0xFFFF))
        sp = 0xFF00
        for a in list(reversed(list(args))) + [SENT >> 4]:
            sp -= 2
            w(sp, a)
        sp -= 2
        w(sp, 0)
        for r, v in ((UC_X86_REG_SS, ds), (UC_X86_REG_SP, sp), (UC_X86_REG_BP, 0),
                     (UC_X86_REG_DS, ds), (UC_X86_REG_ES, ds), (UC_X86_REG_CS, seg + LOAD)):
            uc.reg_write(r, v)
        err = None
        try:
            uc.emu_start((seg + LOAD) * 16 + off, SENT, count=maxinsn)
        except UcError as e:
            err = str(e)
        at = uc.reg_read(UC_X86_REG_CS) * 16 + uc.reg_read(UC_X86_REG_IP)
        if err is None and at != SENT:
            err = 'did not return (%04X:%04X)' % (uc.reg_read(UC_X86_REG_CS) - LOAD,
                                                   uc.reg_read(UC_X86_REG_IP))
        return uc.reg_read(UC_X86_REG_AX), uc.reg_read(UC_X86_REG_DX), err

    def hasp(self, service, port, p, maxinsn=50_000_000):
        uc, ds = self.uc, self.menu.dgroup
        w = lambda off, v: uc.mem_write(ds * 16 + off, struct.pack('<H', v & 0xFFFF))
        P = 0xFE00                                  # SS == DS: Borland near pointers
        for i in range(4):
            w(P + 2 * i, p[i])
        args = [service, 0, port, self.menu.pass1, self.menu.pass2]
        for i in range(4):
            args += [P + 2 * i, ds]
        sp = 0xFF00
        for a in list(reversed(args)) + [SENT >> 4]:
            sp -= 2
            w(sp, a)
        sp -= 2
        w(sp, 0)                                    # return offset
        for r, v in ((UC_X86_REG_SS, ds), (UC_X86_REG_SP, sp), (UC_X86_REG_BP, 0),
                     (UC_X86_REG_DS, ds), (UC_X86_REG_ES, ds), (UC_X86_REG_CS, self.menu.lib)):
            uc.reg_write(r, v)
        err = None
        try:
            uc.emu_start(self.menu.lib * 16 + self.menu.lib_off, SENT, count=maxinsn)
        except UcError as e:
            err = str(e)
        at = uc.reg_read(UC_X86_REG_CS) * 16 + uc.reg_read(UC_X86_REG_IP)
        if err is None and at != SENT:
            err = 'did not return'
        out = [struct.unpack('<H', uc.mem_read(ds * 16 + P + 2 * i, 2))[0] for i in range(4)]
        return out, err


def boot_check(path, territory='DE', **kw):
    menu = Menu(path)
    if menu.pass1 not in PARTS:
        raise SystemExit('no part for pass1 %04X' % menu.pass1)
    part = Part(menu.pass1, territory, **kw)
    m = Machine(menu, part)
    s5, e5 = m.hasp(5, 0, [0, 0, 0, 0])
    ds = menu.dgroup
    before = bytes(m.uc.mem_read(ds * 16 + menu.block, 20))
    part.bursts.clear()
    s3c, e3c = m.hasp(0x3C, s5[2], [0, 20, ds, menu.block])
    part.bursts.append(part.burst)
    after = bytes(m.uc.mem_read(ds * 16 + menu.block, 20))
    return menu, part, (s5, e5), (s3c, e3c), before, after


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('exe')
    ap.add_argument('--old-probe', action='store_true',
                    help='match the liveness probe on the raw byte, as before the fix')
    ap.add_argument('--edge-only', action='store_true',
                    help='no read-time query detection, as before the fix')
    a = ap.parse_args()
    menu, part, (s5, e5), (s3c, e3c), before, after = boot_check(
        a.exe, old_probe=a.old_probe, edge_only=a.edge_only)
    print('library  %04X:%04X   DGROUP %04X   passwords %04X/%04X   block DS:%04X'
          % (menu.lib - LOAD, menu.lib_off, menu.dgroup - LOAD, menu.pass1, menu.pass2,
             menu.block))
    print('hasp(5)     p1..p4 %s  %s' % (' '.join('%04X' % x for x in s5), e5 or ''))
    print('hasp(0x3C)  p1..p4 %s  %s' % (' '.join('%04X' % x for x in s3c), e3c or ''))
    st = s3c[2]
    print('            status %d -> %s' % (st - 0x10000 if st & 0x8000 else st,
                                          'passes' if st == 0 else '"dongle error"'))
    print('reads by phase     %s' % dict(part.log))
    print('rounds (queries between command bytes, non-zero only): %s'
          % [b for b in part.bursts if b])
    print('block before  %s' % before.hex(' '))
    print('block after   %s  %r' % (after.hex(' '), after.split(b'\0')[0]))
    return 0 if st == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
