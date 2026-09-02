"""Run DONGCAP.COM against a modelled dongle, and check every dword it captures.

There is no DOS on this machine and no dongle on its parallel port, so the .COM is run
under an emulated 8086 with the parallel port and INT 21h supplied here. The part is
modelled by a made-up but deterministic oracle; the same oracle drives an independent
Python implementation of the capture, and the two outputs have to agree exactly.

That does not prove the real part answers as expected -- nothing here can. What it proves
is everything else: the wire sequence, the LFSR recursion, both keyless stages, the
256-seed calibration, the streaming of a list too big for a .COM's segment, and the
output format. Those are what a hand-built real-mode program gets wrong.
"""
import os
import struct
import sys

from unicorn import Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INTR, UC_HOOK_INSN
from unicorn.x86_const import (UC_X86_INS_IN, UC_X86_INS_OUT, UC_X86_REG_AH,
                               UC_X86_REG_AL, UC_X86_REG_AX, UC_X86_REG_BX,
                               UC_X86_REG_CX, UC_X86_REG_DX, UC_X86_REG_CS,
                               UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
                               UC_X86_REG_SP, UC_X86_REG_IP)

SEG   = 0x1000
BASE  = SEG * 16
LPT   = 0x378
POLY  = 0x80500062
CB    = 0x803425C3
TRUE_SEED = 0x5A

M32 = 0xFFFFFFFF


# ------------------------------------------------------------------ the modelled part

def oracle(seed7, step, q):
    """Deterministic, and deliberately dependent on all three -- a constant answer would
    let a broken query encoding pass."""
    x = (seed7 * 0x9E3779B1 + step * 0x85EBCA6B + q * 0xC2B2AE35) & M32
    x ^= x >> 15
    return (x >> 7) & 1


# ------------------------------------------------------------------ the Python model

def rol(v, s):
    s &= 31
    return v if not s else ((v << s) | (v >> (32 - s))) & M32


def seed7_of(seed):
    """Bit 0 of the seed never reaches the wire: the frame byte is (seed & 0xFE) | 0x80
    on two of the three writes and seed | 0x81 on the third, so the part cannot see it."""
    return ((seed & 0xFE) | 0x80) & 0xFF


class Model:
    def __init__(self, seed):
        self.seed7 = seed7_of(seed)

    def keyed_round(self, v):
        step = 0
        q = v & 0xFF
        prev = oracle(self.seed7, step, q & 0x1F)
        step += 1
        for _ in range(39):
            idx = (prev & 1) | ((v & 1) << 1)
            v = ((v >> 1) ^ POLY) if ((idx ^ v) & 1) else (v >> 1)
            q = (v >> (8 * idx)) & 0xFF
            prev = oracle(self.seed7, step, q & 0x1F)
            step += 1
        return v & M32

    def b_first(self, b0, b1):
        for s in (10, 8, 6, 4, 2, 0):
            b0, b1 = (rol(b0 ^ CB, s) ^ b1), b0
        return b0

    def b_fwd_second(self, b0, b1):
        for s in (0, 2, 4, 6, 8, 10):
            b0, b1 = b1, (rol(b1 ^ CB, s) ^ b0)
        return b1


# ------------------------------------------------------------------ the emulated DOS

class Machine:
    def __init__(self, com, files):
        self.files   = files          # name -> bytearray
        self.handles = {}             # handle -> [name, pos]
        self.next_h  = 5
        self.out     = []
        self.exited  = False
        self.window  = []
        self.seed7   = 0
        self.step    = 0
        self.lastpay = 0

        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        self.uc.mem_map(0, 0x110000)
        self.uc.mem_write(BASE + 0x100, com)
        # a PSP with an empty command tail
        self.uc.mem_write(BASE + 0x80, b'\x00\x0d')
        for r in (UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            self.uc.reg_write(r, SEG)
        self.uc.reg_write(UC_X86_REG_SP, 0xFFF0)
        self.uc.hook_add(UC_HOOK_INTR, self.on_int)
        self.uc.hook_add(UC_HOOK_INSN, self.on_in, None, 1, 0, UC_X86_INS_IN)
        self.uc.hook_add(UC_HOOK_INSN, self.on_out, None, 1, 0, UC_X86_INS_OUT)

    # ---- the parallel port ----
    def on_out(self, uc, port, size, value, user):
        if port != LPT:
            return
        v = value & 0xFF
        self.window = (self.window + [v])[-7:]
        # the preamble: cmdbyte(seed), cmdbyte(0x4E), raw(0x84)
        if len(self.window) == 7 and self.window[3:] == [0xCE, 0xCF, 0xCE, 0x84]:
            self.seed7 = self.window[0]
            self.step  = 0
        self.lastpay = v

    def on_in(self, uc, port, size, user):
        if port == LPT + 1:
            pay = self.lastpay
            q = ((pay >> 1) & 7) | ((pay >> 2) & 0x18)
            bit = oracle(self.seed7, self.step, q)
            self.step += 1
            return bit << 5
        return 0xFF

    # ---- INT 21h ----
    def on_int(self, uc, intno, user):
        if intno != 0x21:
            return
        ah = uc.reg_read(UC_X86_REG_AH)
        dx = uc.reg_read(UC_X86_REG_DX)
        bx = uc.reg_read(UC_X86_REG_BX)
        cx = uc.reg_read(UC_X86_REG_CX)

        def zstr(off):
            b = bytearray()
            while True:
                c = uc.mem_read(BASE + off, 1)[0]
                if c == 0:
                    break
                b.append(c)
                off += 1
            return bytes(b).decode('latin1')

        if ah == 0x09:                                   # print $-terminated
            off = dx
            b = bytearray()
            while True:
                c = uc.mem_read(BASE + off, 1)[0]
                if c == ord('$'):
                    break
                b.append(c)
                off += 1
            self.out.append(bytes(b).decode('latin1'))
        elif ah == 0x3D:                                 # open
            name = zstr(dx)
            if name not in self.files:
                uc.reg_write(UC_X86_REG_AX, 2)
                self.set_cf(True)
                return
            h = self.next_h
            self.next_h += 1
            self.handles[h] = [name, 0]
            uc.reg_write(UC_X86_REG_AX, h)
            self.set_cf(False)
        elif ah == 0x3C:                                 # create
            name = zstr(dx)
            self.files[name] = bytearray()
            h = self.next_h
            self.next_h += 1
            self.handles[h] = [name, 0]
            uc.reg_write(UC_X86_REG_AX, h)
            self.set_cf(False)
        elif ah == 0x3F:                                 # read
            name, pos = self.handles[bx]
            data = bytes(self.files[name][pos:pos + cx])
            uc.mem_write(BASE + dx, data)
            self.handles[bx][1] = pos + len(data)
            uc.reg_write(UC_X86_REG_AX, len(data))
            self.set_cf(False)
        elif ah == 0x40:                                 # write
            name, pos = self.handles[bx]
            data = bytes(uc.mem_read(BASE + dx, cx))
            f = self.files[name]
            if len(f) < pos:
                f += b'\x00' * (pos - len(f))
            f[pos:pos + cx] = data
            self.handles[bx][1] = pos + cx
            uc.reg_write(UC_X86_REG_AX, cx)
            self.set_cf(False)
        elif ah == 0x3E:                                 # close
            self.handles.pop(bx, None)
            self.set_cf(False)
        elif ah == 0x08:                                 # getchar
            uc.reg_write(UC_X86_REG_AL, 13)
        elif ah == 0x4C:                                 # exit
            self.exited = True
            uc.emu_stop()

    def set_cf(self, on):
        from unicorn.x86_const import UC_X86_REG_EFLAGS
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        fl = (fl | 1) if on else (fl & ~1)
        self.uc.reg_write(UC_X86_REG_EFLAGS, fl)

    def run(self, limit=6_000_000_000):
        self.uc.emu_start(BASE + 0x100, BASE + 0x100 + 0xFFFF, 0, 0)


# ------------------------------------------------------------------ the test

def main():
    com = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            'DONGCAP.COM'), 'rb').read()
    mdl = Model(TRUE_SEED)

    # A list big enough to cross the 512-entry chunk boundary several times.
    import random
    random.seed(1234)
    ncal, count, nenc = 3, 1100, 1
    cal  = [random.getrandbits(32) for _ in range(ncal)]
    work = [(random.getrandbits(32), random.getrandbits(32)) for _ in range(count)]
    enc  = [(random.getrandbits(32), random.getrandbits(32)) for _ in range(nenc)]

    lst = struct.pack('<4sIII', b'DCAP', ncal, count, nenc)
    for v in cal:
        lst += struct.pack('<II', v, mdl.keyed_round(v))
    for a, b in work:
        lst += struct.pack('<II', a, b)
    for a, b in enc:
        lst += struct.pack('<II', a, b)

    # what the answer has to be
    want = struct.pack('<4sIII', b'DOUT', count, TRUE_SEED, nenc)
    for L1, R1 in work:
        f1 = mdl.keyed_round(L1)
        f2 = mdl.keyed_round(mdl.b_first(f1 ^ R1, L1))
        want += struct.pack('<II', f1, f2)
    for P0, P1 in enc:
        f1 = mdl.keyed_round(P0)
        f2 = mdl.keyed_round(mdl.b_fwd_second(f1 ^ P1, P0))
        want += struct.pack('<II', f1, f2)

    files = {'DONGCAP.LST': bytearray(lst)}
    m = Machine(com, files)
    m.run()

    print(''.join(m.out).replace('\r\n', '\n').strip())
    print('-' * 60)

    got = bytes(files.get('DONGCAP.BIN', b''))
    if 'DONGCAP.DIA' in files:
        print('FAIL: it wrote a DIAG file -- calibration did not find the seed')
        return 1
    if got == want:
        print('PASS: %d bytes, %d captured dwords, all identical to the model'
              % (len(got), (len(got) - 16) // 4))
        return 0

    print('FAIL: %d bytes out, %d expected' % (len(got), len(want)))
    for i in range(0, min(len(got), len(want)), 4):
        a = got[i:i + 4]
        b = want[i:i + 4]
        if a != b:
            print('  first difference at byte %d: got %s want %s' % (i, a.hex(), b.hex()))
            break
    return 1


if __name__ == '__main__':
    sys.exit(main())
