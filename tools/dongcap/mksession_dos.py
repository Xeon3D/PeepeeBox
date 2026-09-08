"""Build SESSION.COM -- read a funworld HASP4 part's session layer, under DOS.

    python mksession_dos.py [--disasm]

There is no DOS toolchain here, so the .COM is assembled directly, the same way
`mkdongcap_dos.py` and `tools/dongtest/mkdongletest.py` do.  Every encoding carries the
mnemonic it stands for, and `--disasm` checks the result against objdump rather than
trusting the comments.

Why this exists, and why it is not DONGCAP
------------------------------------------
DONGCAP captures the *keyed round* -- thousands of consultations, a work list per release,
a binary output file.  This captures the *session layer*, which is four fixed
interrogations and 194 bits in total, and it needs no work list, no plaintext and no
per-release build.

The reason it is worth its own program: `docs/research-v2/09` § 2 records that the whole
session model comes from one boot of one `68BB/1329` part, and that **nothing shows
whether another password pair answers the same**.  That single unknown is what blocks
I.G.O. 3 (`09` § 1).  Running this against any other part settles it in one command --
and any part will do, because what the program sends is fixed.  The password lives in the
dongle, not in the program, which is why there is one build and not one per generation.

**One DOS build, every generation.**  A 2001 (`7477/7D57`), I.G.O. 3/5 (`6B91/24A3`) or
I.G.O. 2/6/7 (`68BB/1329`) part all get asked the same 194 questions.

Why DOS rather than the Win32 build
-----------------------------------
`dongcap.c` reaches the port through `NtSetInformationProcess(ProcessUserModeIOPL)`, which
needs 32-bit Windows *and* `SeTcbPrivilege` -- and an XP Administrator does not hold that
by default.  Real mode has no such gate: a .COM owns the hardware.  Boot DOS from a floppy
or a CD, run it, read the eight bytes off the screen.

What it asks, and what each answer means
----------------------------------------
1. **The identity ramp** -- writes `00, 02 ... 7E`, one STATUS read each.  A real
   `68BB/1329` part answers `HD_SIGNATURE`; the reference below is that value in this
   program's own bit order.
2. **The 64-step sweep, bit 7 clear** -- the form I.G.O. 2 sends.  The sixty-four payloads
   are not a table funworld chose: they are the output of the LCG `x = x*0x1989 + 5`
   seeded with 100, masked `0x7E`, which is what I.G.O. 3's `MENU.EXE` at `0x24FB`
   computes and what `hs_sweep_w[]` in `src/device/dongle_photoplay.c` measured.
3. **The same sweep with bit 7 set** -- the form I.G.O. 3 sends.  Whether the part cares
   about bit 7 is itself unknown, and two lines of output answer it.
4. **The liveness probe** -- `1E` then `1C`.  A real part answers 1 then 0.

If line 2 comes back as `F5 7A 37 E7 8F 8F BD DA` on a part with a different password
pair, the sweep is a property of the design and the model transfers.  If it comes back
different, it is keyed by the password, and I.G.O. 3 has been served another dongle's
answers all along.  Either way the question is closed.

No third-party modules; Python 3.8+.
"""

import os
import subprocess
import sys

ORG = 0x100
OUT = 'SESSION.COM'

code = []
labels = {}
fixups = []

# ----------------------------------------------------------------- assembler
# Same shape as mkdongcap_dos.py's, kept local so the two programs cannot drift
# into each other.


def emit(*bs):
    for b in bs:
        code.append(b & 0xFF)


def label(n):
    labels[n] = len(code)


def rel8(n):
    fixups.append((len(code), 'rel8', n))
    code.append(0)


def rel16(n):
    fixups.append((len(code), 'rel16', n))
    code.extend([0, 0])


def a16(n):
    """absolute 16-bit address of a label, for a disp16 memory operand"""
    fixups.append((len(code), 'abs16', n))
    code.extend([0, 0])


def iw(v):
    emit(v, v >> 8)


def db(bs):
    for b in bs:
        emit(b)


# --- the forms this program uses -----------------------------------------

def mov_al_m(n):        emit(0xA0); a16(n)                    # mov al,[n]
def mov_m_al(n):        emit(0xA2); a16(n)                    # mov [n],al
def mov_bl_m(n):        emit(0x8A, 0x1E); a16(n)              # mov bl,[n]
def mov_m_bl(n):        emit(0x88, 0x1E); a16(n)              # mov [n],bl
def mov_dx_m(n):        emit(0x8B, 0x16); a16(n)              # mov dx,[n]
def mov_di_m(n):        emit(0x8B, 0x3E); a16(n)              # mov di,[n]
def mov_m_di(n):        emit(0x89, 0x3E); a16(n)              # mov [n],di
def mov_mb_imm(n, v):   emit(0xC6, 0x06); a16(n); emit(v)     # mov byte [n],v
def mov_mw_imm(n, v):   emit(0xC7, 0x06); a16(n); iw(v)       # mov word [n],v
def cmp_mb_imm(n, v):   emit(0x80, 0x3E); a16(n); emit(v)     # cmp byte [n],v
def inc_mb(n):          emit(0xFE, 0x06); a16(n)              # inc byte [n]
def lea_di(n):          emit(0xBF); a16(n)                    # mov di,offset n

def call(n):            emit(0xE8); rel16(n)
def jmp(n):             emit(0xE9); rel16(n)
def jmps(n):            emit(0xEB); rel8(n)
def jz(n):              emit(0x74); rel8(n)
def jnz(n):             emit(0x75); rel8(n)
def jb(n):              emit(0x72); rel8(n)
def jbe(n):             emit(0x76); rel8(n)
def ja(n):              emit(0x77); rel8(n)
def loopr(n):           emit(0xE2); rel8(n)
def ret():              emit(0xC3)
def int21():            emit(0xCD, 0x21)


def dosprint(msg):
    """AH=09 print a $-terminated string.  Clobbers DX, which is why the port base
    lives in memory and is loaded fresh for every port access."""
    emit(0xB4, 0x09)                       # mov ah,9
    emit(0xBA); a16(msg)                   # mov dx,msg
    int21()


# the sixty-four sweep payloads, from the LCG in I.G.O. 3's MENU.EXE at 0x24FB
def lcg_payloads():
    x = 100
    out = []
    for _ in range(64):
        x = (x * 0x1989 + 5) & 0xFFFF
        out.append((x >> 8) & 0x7E)
    return out


LCG = lcg_payloads()

# The reference answers, in THIS program's bit order: step 0 is the most significant
# bit of the first byte.  Computed here rather than typed, because the two constants
# in the device use opposite bit orders and transcribing them by hand is how that sort
# of thing goes wrong.
HD_SIGNATURE = 0xCEFF0AFFCECE0A0A   # bit i is address i  -> LSB-first per step
HS_SWEEP_A   = 0xF57A37E78F8FBDDA   # step i is bit 63-i  -> already MSB-first


def bits_to_bytes(bits):
    out = bytearray()
    for i in range(0, len(bits), 8):
        b = 0
        for k in range(8):
            b = (b << 1) | bits[i + k]
        out.append(b)
    return bytes(out)


REF_RAMP  = bits_to_bytes([(HD_SIGNATURE >> a) & 1 for a in range(64)])
REF_SWEEP = bits_to_bytes([(HS_SWEEP_A >> (63 - i)) & 1 for i in range(64)])


# ================================================================= program

label('start')
emit(0xFC)                                 # cld

# --- port base: an optional hex argument, else 0x378 ----------------------
# The PSP command tail is a length byte at 0x80 and the text from 0x81.  Absolute
# addresses, so these are emitted raw rather than through a16.
emit(0xBE); iw(0x0081)                     # mov si,0x81
emit(0x31, 0xC0)                           # xor ax,ax
emit(0x31, 0xDB)                           # xor bx,bx            bx = digits seen
label('skipsp')
emit(0xAC)                                 # lodsb
emit(0x3C, 0x20)                           # cmp al,' '
jz('skipsp')
emit(0x3C, 0x09)                           # cmp al,9  (tab)
jz('skipsp')
emit(0x4E)                                 # dec si                un-read it
label('hexloop')
emit(0xAC)                                 # lodsb
emit(0x3C, 0x30)                           # cmp al,'0'
jb('hexdone')
emit(0x3C, 0x39)                           # cmp al,'9'
jbe('isdig')
emit(0x0C, 0x20)                           # or al,0x20            fold to lower case
emit(0x3C, 0x61)                           # cmp al,'a'
jb('hexdone')
emit(0x3C, 0x66)                           # cmp al,'f'
ja('hexdone')                              # anything past 'f' ends the number
emit(0x2C, 0x57)                           # sub al,'a'-10
jmps('gotdig')
label('isdig')
emit(0x2C, 0x30)                           # sub al,'0'
label('gotdig')
emit(0xB4, 0x00)                           # mov ah,0
emit(0xD1, 0x26); a16('base')              # shl word [base],1
emit(0xD1, 0x26); a16('base')              # shl word [base],1
emit(0xD1, 0x26); a16('base')              # shl word [base],1
emit(0xD1, 0x26); a16('base')              # shl word [base],1
emit(0x01, 0x06); a16('base')              # add [base],ax
emit(0x43)                                 # inc bx
jmps('hexloop')
label('hexdone')
emit(0x83, 0xFB, 0x00)                     # cmp bx,0
jnz('haveb')
mov_mw_imm('base', 0x378)                  # nothing given: the usual LPT1
label('haveb')

dosprint('m_head')
dosprint('m_port')
mov_al_m('base' + '_hi')                   # high byte of the base
call('hexbyte')
mov_al_m('base')
call('hexbyte')
dosprint('m_crlf')
dosprint('m_crlf')

# --- 1. the identity ramp: 00,02 ... 7E ----------------------------------
dosprint('m_ramp')
lea_di('res_ramp')
call('accinit')
emit(0xBE); iw(0)                          # mov si,0
label('rampl')
emit(0x89, 0xF0)                           # mov ax,si
emit(0xD1, 0xE0)                           # shl ax,1
call('step')
call('acc')
emit(0x46)                                 # inc si
emit(0x83, 0xFE, 0x40)                     # cmp si,64
jb('rampl')
lea_di('res_ramp')
call('show8')

# --- 2. the sweep, bit 7 clear (I.G.O. 2's form) -------------------------
dosprint('m_swa')
lea_di('res_swa')
call('accinit')
emit(0xBE); iw(0)                          # mov si,0
label('swal')
emit(0x89, 0xF3)                           # mov bx,si
emit(0x8A, 0x87); a16('lcgtab')            # mov al,[bx+lcgtab]
call('step')
call('acc')
emit(0x46)                                 # inc si
emit(0x83, 0xFE, 0x40)                     # cmp si,64
jb('swal')
lea_di('res_swa')
call('show8')

# --- 3. the same sweep with bit 7 set (I.G.O. 3's form) ------------------
dosprint('m_swb')
lea_di('res_swb')
call('accinit')
emit(0xBE); iw(0)                          # mov si,0
label('swbl')
emit(0x89, 0xF3)                           # mov bx,si
emit(0x8A, 0x87); a16('lcgtab')            # mov al,[bx+lcgtab]
emit(0x0C, 0x80)                           # or al,0x80
call('step')
call('acc')
emit(0x46)                                 # inc si
emit(0x83, 0xFE, 0x40)                     # cmp si,64
jb('swbl')
lea_di('res_swb')
call('show8')

# --- 4. the liveness probe: 1E then 1C -----------------------------------
dosprint('m_live')
emit(0xB0, 0x1E)                           # mov al,0x1E
call('step')
call('showbit')
emit(0xB0, 0x20)                           # mov al,' '
call('putc')
emit(0xB0, 0x1C)                           # mov al,0x1C
call('step')
call('showbit')
dosprint('m_crlf')

dosprint('m_ref')
emit(0xB8); iw(0x4C00)                     # mov ax,0x4C00
int21()

# ----------------------------------------------------------------- helpers

# step: AL = byte to write.  Writes it, settles, reads STATUS, returns AL = 0x20 or 0.
label('step')
emit(0x53)                                 # push bx
mov_dx_m('base')
emit(0xEE)                                 # out dx,al
call('delay')
mov_dx_m('base')
emit(0x42)                                 # inc dx                 STATUS = base+1
emit(0xEC)                                 # in al,dx
emit(0x24, 0x20)                           # and al,0x20
emit(0x5B)                                 # pop bx
ret()

# A settle between the write and the sample.  The part is a shift register clocked by
# the write, and a modern CPU can issue both accesses far faster than the cable and the
# 8051 behind it will follow.  Cheap insurance; the whole run is 194 steps.
label('delay')
emit(0x51)                                 # push cx
emit(0xB9); iw(0x0200)                     # mov cx,0x200
label('dly1')
loopr('dly1')
emit(0x59)                                 # pop cx
ret()

# accinit: DI = destination buffer
label('accinit')
mov_m_di('accptr')
mov_mb_imm('accbyte', 0)
mov_mb_imm('acccnt', 0)
ret()

# acc: AL = 0 or 0x20, appends one bit, MSB first
label('acc')
emit(0x53)                                 # push bx
emit(0x57)                                 # push di
mov_bl_m('accbyte')
emit(0xD0, 0xE3)                           # shl bl,1
emit(0x3C, 0x00)                           # cmp al,0
jz('acc_z')
emit(0x80, 0xCB, 0x01)                     # or bl,1
label('acc_z')
mov_m_bl('accbyte')
inc_mb('acccnt')
cmp_mb_imm('acccnt', 8)
jb('acc_out')
mov_di_m('accptr')
emit(0x88, 0x1D)                           # mov [di],bl
emit(0x47)                                 # inc di
mov_m_di('accptr')
mov_mb_imm('accbyte', 0)
mov_mb_imm('acccnt', 0)
label('acc_out')
emit(0x5F)                                 # pop di
emit(0x5B)                                 # pop bx
ret()

# show8: DI = an 8-byte buffer; print it as hex pairs then CRLF
label('show8')
emit(0x51)                                 # push cx
emit(0xB9); iw(8)                          # mov cx,8
label('sh1')
emit(0x8A, 0x05)                           # mov al,[di]
emit(0x57)                                 # push di
emit(0x51)                                 # push cx
call('hexbyte')
emit(0xB0, 0x20)                           # mov al,' '
call('putc')
emit(0x59)                                 # pop cx
emit(0x5F)                                 # pop di
emit(0x47)                                 # inc di
loopr('sh1')
emit(0x59)                                 # pop cx
dosprint('m_crlf')
ret()

# showbit: AL = 0 or 0x20 -> print '0' or '1'
label('showbit')
emit(0x3C, 0x00)                           # cmp al,0
jz('sb0')
emit(0xB0, 0x31)                           # mov al,'1'
jmps('sbp')
label('sb0')
emit(0xB0, 0x30)                           # mov al,'0'
label('sbp')
call('putc')
ret()

# putc: AL = character
label('putc')
emit(0x50)                                 # push ax
emit(0x52)                                 # push dx
emit(0x88, 0xC2)                           # mov dl,al
emit(0xB4, 0x02)                           # mov ah,2
int21()
emit(0x5A)                                 # pop dx
emit(0x58)                                 # pop ax
ret()

label('hexbyte')
emit(0x50)                                 # push ax
emit(0xB1, 0x04)                           # mov cl,4
emit(0xD2, 0xE8)                           # shr al,cl
call('hexnib')
emit(0x58)                                 # pop ax
emit(0x50)                                 # push ax
emit(0x24, 0x0F)                           # and al,0x0F
call('hexnib')
emit(0x58)                                 # pop ax
ret()

label('hexnib')
emit(0x04, 0x30)                           # add al,'0'
emit(0x3C, 0x39)                           # cmp al,'9'
jbe('hn_ok')
emit(0x04, 0x07)                           # add al,7
label('hn_ok')
call('putc')
ret()

# ----------------------------------------------------------------- data

# The port base is a 16-bit word, little-endian, so its two bytes get a label each:
# `base` addresses the word AND its low byte, `base_hi` the high byte one along.  These
# have to be emitted as two separate bytes -- writing iw(0) and labelling after it puts
# base_hi two bytes along, which prints the wrong digits.
label('base')
emit(0)                                    # low byte
label('base_hi')
emit(0)                                    # high byte

label('accptr')
iw(0)
label('accbyte')
emit(0)
label('acccnt')
emit(0)

label('res_ramp')
db([0] * 8)
label('res_swa')
db([0] * 8)
label('res_swb')
db([0] * 8)

label('lcgtab')
db(LCG)


def dollar(s):
    return [ord(c) for c in s] + [ord('$')]


label('m_head')
db(dollar("SESSION -- funworld HASP4 session layer, from the part itself\r\n"
          "Works on any dongle: what it sends is fixed, the password is in the part.\r\n\r\n"))
label('m_port')
db(dollar("port base   : "))
label('m_ramp')
db(dollar("identity ramp   : "))
label('m_swa')
db(dollar("sweep bit7=0    : "))
label('m_swb')
db(dollar("sweep bit7=1    : "))
label('m_live')
db(dollar("liveness 1E,1C  : "))
label('m_crlf')
db(dollar("\r\n"))
label('m_ref')
db(dollar("\r\nA 68BB/1329 part answered:\r\n"
          "identity ramp   : " + " ".join("%02X" % b for b in REF_RAMP) + "\r\n"
          "sweep bit7=0    : " + " ".join("%02X" % b for b in REF_SWEEP) + "\r\n"
          "liveness 1E,1C  : 1 0\r\n"
          "\r\nDifferent numbers mean the session layer is keyed by the password.\r\n"
          "Write all four lines down exactly as shown.\r\n"))


# ================================================================= link

def link():
    out = bytearray(code)
    for at, kind, name in fixups:
        if name not in labels:
            raise SystemExit('undefined label: %s' % name)
        tgt = labels[name]
        if kind == 'rel8':
            d = tgt - (at + 1)
            if not -128 <= d <= 127:
                raise SystemExit('rel8 out of range to %s' % name)
            out[at] = d & 0xFF
        elif kind == 'rel16':
            d = tgt - (at + 2)
            out[at] = d & 0xFF
            out[at + 1] = (d >> 8) & 0xFF
        else:
            a = tgt + ORG
            out[at] = a & 0xFF
            out[at + 1] = (a >> 8) & 0xFF
    return bytes(out)


def main():
    blob = link()
    here = os.path.dirname(os.path.abspath(__file__))
    path = os.path.join(here, OUT)
    with open(path, 'wb') as f:
        f.write(blob)
    print('%s: %d bytes' % (OUT, len(blob)))
    print('   entry %04X  step %04X  lcgtab %04X'
          % (ORG, labels['step'] + ORG, labels['lcgtab'] + ORG))
    print('   sweep payloads: %s ...' % " ".join("%02X" % b for b in LCG[:8]))
    print('   reference ramp : %s' % " ".join("%02X" % b for b in REF_RAMP))
    print('   reference sweep: %s' % " ".join("%02X" % b for b in REF_SWEEP))
    if '--disasm' in sys.argv:
        end = labels['base']
        with open(path + '.text', 'wb') as f:
            f.write(blob[:end])
        try:
            print(subprocess.run(
                ['objdump', '-D', '-b', 'binary', '-m', 'i386',
                 '-M', 'intel,addr16,data16', '--adjust-vma=0x100', path + '.text'],
                capture_output=True, text=True, check=True).stdout)
        except (OSError, subprocess.CalledProcessError) as e:
            print('objdump unavailable: %s' % e)


if __name__ == '__main__':
    main()
