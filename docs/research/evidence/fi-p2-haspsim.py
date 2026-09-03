#!/usr/bin/env python3
"""p2-haspsim - run FSYSTEM.EXE's Aladdin HASP client offline under Unicorn.

Loads the real-mode MZ image, applies its relocations, then calls the library entry
(segment 0, offset 0x1E7A) the same way the wrappers at CS:0x0188 / CS:0x01D1 / CS:0x02F3
do -- Pascal convention, params pushed left to right:

    hasp(service, seed, lptnum, pass1, pass2, var p1, p2, p3, p4)

Every port access is logged, so varying one parameter at a time and diffing the wire tells
us where each field lands in the bit stream.  Reads answer from a pluggable responder so a
candidate dongle can be tested without booting anything.

The guest is untouched: this reads FSYSTEM.EXE out of the extracted tree and never writes.
"""
import argparse
import struct
import sys

from unicorn import *
from unicorn.x86_const import *

HASP_ENTRY = 0x1E7A          # segment 0 offset; wrappers do `push cs; call 0x1E7A`
LOAD_SEG   = 0x1000          # where we place the load image
PSP_SEG    = 0x0F00
RET_OFF    = 0xFFF0          # sentinel return address inside the code segment
MEM_SIZE   = 0x110000        # 1 MB + a little for wrap


class Sim:
    def __init__(self, exe_path, responder=None, trace=False):
        self.raw = open(exe_path, "rb").read()
        self.responder = responder or (lambda port, ctx: 0x00)
        self.trace = trace
        self.wire = []           # (kind, port, value)
        self.ints = []
        self.lpt_base = 0x378
        self.code_hook = None    # optional callback(uc, addr, size, user) for tracing
        self.extra_hooks = []    # list of (UC_HOOK_*, callback, begin, end) added to every run
        self.uc = None           # the live Uc, for hooks that want registers

    # ---- loader -------------------------------------------------------------
    def _load(self):
        h = struct.unpack_from("<14H", self.raw, 2)
        cblp, cp, crlc, cparhdr = h[0], h[1], h[2], h[3]
        ss, sp, _csum, ip, cs, lfarlc = h[6], h[7], h[8], h[9], h[10], h[11]
        hdr = cparhdr * 16
        end = (cp - 1) * 512 + (cblp or 512)
        img = bytearray(self.raw[hdr:end])

        for i in range(crlc):
            o, s = struct.unpack_from("<HH", self.raw, lfarlc + i * 4)
            at = s * 16 + o
            v = struct.unpack_from("<H", img, at)[0]
            struct.pack_into("<H", img, at, (v + LOAD_SEG) & 0xFFFF)

        self.img = bytes(img)
        self.exe_ss, self.exe_sp, self.exe_ip, self.exe_cs = ss, sp, ip, cs
        return self.img

    def _hook_in(self, uc, port, size, data):
        v = self.responder(port, self)
        self.wire.append(("in", port, v))
        if self.trace:
            print("  IN  %03X -> %02X" % (port, v))
        return v

    def _hook_out(self, uc, port, size, value, data):
        self.wire.append(("out", port, value & 0xFF))
        if self.trace:
            print("  OUT %03X <- %02X" % (port, value & 0xFF))

    def _hook_intr(self, uc, intno, data):
        """Unicorn's x86 INTR hook fires with IP already past the INT and with NOTHING
        pushed -- it does not emulate the real-mode flags/CS/IP push.  So service the
        call in registers and return; touching the stack here corrupts SP and sends the
        library off into whatever the caller happened to push."""
        ax = uc.reg_read(UC_X86_REG_AX)
        ah = ax >> 8
        self.ints.append((intno, ax))
        carry = True

        if intno == 0x21:
            if ah == 0x30:                       # get DOS version -> 7.10
                uc.reg_write(UC_X86_REG_AX, 0x0A07)
                uc.reg_write(UC_X86_REG_BX, 0)
                uc.reg_write(UC_X86_REG_CX, 0)
                carry = False
            elif ax == 0x3306:                   # get true version
                uc.reg_write(UC_X86_REG_BX, 0x0A07)
                uc.reg_write(UC_X86_REG_DX, 0)
                carry = False
            elif ah == 0x35:                     # get interrupt vector -> null
                uc.reg_write(UC_X86_REG_ES, 0)
                uc.reg_write(UC_X86_REG_BX, 0)
                carry = False
            elif ah in (0x25, 0x1A, 0x0E, 0x2A, 0x2C, 0x0B):
                carry = False
            elif ah == 0x62:                     # get PSP
                uc.reg_write(UC_X86_REG_BX, PSP_SEG)
                carry = False
            elif ah == 0x52:                     # list of lists
                uc.reg_write(UC_X86_REG_ES, 0)
                uc.reg_write(UC_X86_REG_BX, 0)
                carry = False
            elif ah == 0x3D:                     # open  -> "file not found"
                uc.reg_write(UC_X86_REG_AX, 0x0002)
            elif ah == 0x4C:
                uc.emu_stop()
                return
            else:
                uc.reg_write(UC_X86_REG_AX, 0x0001)
        elif intno == 0x2F:
            uc.reg_write(UC_X86_REG_AX, ax & 0xFF00)
            carry = False
        elif intno == 0x17:                      # BIOS printer status: ready, no error
            uc.reg_write(UC_X86_REG_AX, (ax & 0x00FF) | 0x9000)
            carry = False
        else:
            carry = False

        ef = uc.reg_read(UC_X86_REG_EFLAGS)
        uc.reg_write(UC_X86_REG_EFLAGS, (ef | 1) if carry else (ef & ~1))

    # ---- the call ------------------------------------------------------------
    def call(self, service, seed, lptnum, pass1, pass2, p1=0, p2=0, p3=0, p4=0,
             max_insn=40_000_000):
        self._load()
        uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, MEM_SIZE)
        uc.mem_write(LOAD_SEG * 16, self.img)

        # BIOS data area: LPT1..3 port addresses, and an equipment word claiming one LPT.
        uc.mem_write(0x408, struct.pack("<HHH", self.lpt_base, 0x278, 0x3BC))
        uc.mem_write(0x410, struct.pack("<H", 0x4000))
        # a plausible PSP so DOS-ish pokes do not fault
        uc.mem_write(PSP_SEG * 16, b"\xCD\x20" + b"\x00" * 254)

        code_seg = LOAD_SEG + self.exe_cs
        stack_seg = LOAD_SEG + self.exe_ss
        dgroup = LOAD_SEG + 0x0E5E
        sp = self.exe_sp

        # four out-params live just under the stack top, in the stack segment
        outs = {"p1": sp + 0x20, "p2": sp + 0x22, "p3": sp + 0x24, "p4": sp + 0x26}
        for name, off in outs.items():
            uc.mem_write(stack_seg * 16 + off, struct.pack("<H", locals()[name]))

        def push(v):
            nonlocal sp
            sp = (sp - 2) & 0xFFFF
            uc.mem_write(stack_seg * 16 + sp, struct.pack("<H", v & 0xFFFF))

        for v in (service, seed, lptnum, pass1, pass2):
            push(v)
        for name in ("p1", "p2", "p3", "p4"):
            push(stack_seg)          # `push ss`
            push(outs[name])         # `push di`
        push(code_seg)               # `push cs`  -- the far return the callee does
        push(RET_OFF)

        uc.reg_write(UC_X86_REG_CS, code_seg)
        uc.reg_write(UC_X86_REG_SS, stack_seg)
        uc.reg_write(UC_X86_REG_DS, dgroup)
        uc.reg_write(UC_X86_REG_ES, PSP_SEG)
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_BP, 0)

        uc.hook_add(UC_HOOK_INSN, self._hook_in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, self._hook_out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INTR, self._hook_intr)
        if self.code_hook is not None:
            uc.hook_add(UC_HOOK_CODE, self.code_hook)
        for spec in self.extra_hooks:
            htype, cb = spec[0], spec[1]
            if len(spec) >= 4:
                uc.hook_add(htype, cb, None, spec[2], spec[3])
            else:
                uc.hook_add(htype, cb)
        self.uc = uc

        self.wire = []
        self.ints = []
        stop = code_seg * 16 + RET_OFF
        try:
            uc.emu_start(code_seg * 16 + HASP_ENTRY, stop, count=max_insn)
            self.err = None
        except UcError as e:
            self.err = "%s at %04X:%04X" % (e, uc.reg_read(UC_X86_REG_CS),
                                            uc.reg_read(UC_X86_REG_IP))

        res = {}
        for name, off in outs.items():
            res[name] = struct.unpack("<H", uc.mem_read(stack_seg * 16 + off, 2))[0]
        res["err"] = self.err
        res["ints"] = self.ints
        return res

    # ---- wire helpers --------------------------------------------------------
    def data_writes(self):
        return [v for k, p, v in self.wire if k == "out" and p == self.lpt_base]

    def password_bytes(self):
        """The clocked (odd) bytes between the C6 C7 C6 80 wake and the read ramp."""
        w = self.data_writes()
        for i in range(len(w) - 3):
            if w[i:i + 4] == [0xC6, 0xC7, 0xC6, 0x80]:
                tail = w[i + 4:]
                out = []
                for v in tail:
                    if v & 1:
                        out.append(v)
                    elif out and v == 0xFE:   # the read ramp starts at FE
                        break
                return out
        return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("--service", type=lambda x: int(x, 0), default=1)
    ap.add_argument("--seed", type=lambda x: int(x, 0), default=0x64)
    ap.add_argument("--lpt", type=lambda x: int(x, 0), default=1)
    ap.add_argument("--pass1", type=lambda x: int(x, 0), default=0x43B5)
    ap.add_argument("--pass2", type=lambda x: int(x, 0), default=0x594A)
    ap.add_argument("--p1", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--p2", type=lambda x: int(x, 0), default=0)
    ap.add_argument("--trace", action="store_true")
    a = ap.parse_args()

    s = Sim(a.exe, trace=a.trace)
    r = s.call(a.service, a.seed, a.lpt, a.pass1, a.pass2, a.p1, a.p2)
    w = s.data_writes()
    print("err      :", r["err"])
    print("ints     :", r["ints"][:8])
    print("outs     : p1=%04X p2=%04X p3=%04X p4=%04X" % (r["p1"], r["p2"], r["p3"], r["p4"]))
    print("port outs: %d  (data-port writes %d)" % (len(s.wire), len(w)))
    pw = s.password_bytes()
    print("password :", " ".join("%02X" % v for v in pw) if pw else None)
    if a.trace:
        print("all data writes:", " ".join("%02X" % v for v in w))


if __name__ == "__main__":
    main()
