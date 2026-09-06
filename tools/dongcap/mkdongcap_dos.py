"""Build DONGCAP.COM -- the DOS capture program, for running on the cabinet itself.

There is no DOS toolchain on this machine, so the .COM is assembled here directly, the
way tools/dongtest/mkdongletest.py builds DONGTEST.COM.  Every encoding below carries the
mnemonic it stands for, and `python mkdongcap_dos.py --disasm` checks the result against
objdump rather than trusting the comments.

Why a DOS build when dongcap.c already exists
---------------------------------------------
The cabinet is the host we know works.  Its dongle is already talking to its own parallel
port -- a genuine SPP port on the hardware the protocol was written for -- so a failure
there means something about the protocol, not about ECP/EPP mode, a PCIe card's timing, or
which base a modern box put the port at.  It also means the dongle never has to move.

What it captures, and the difference from the other two
-------------------------------------------------------
DONGCAP.BIN is the same file the Win32 and Linux builds write: two keyed-round outputs per
4 KB buffer, which is everything needed to decrypt that buffer.

DONGCAP.BIT is new, and is the more valuable of the two.  Each keyed round consults the
part forty times -- one byte out, one bit back -- and this records those forty bits, five
bytes per round, MSB first.  `docs/research/23` § 2 could not derive that byte-to-bit
function from the dumped security table, and `notes/HANDOFF2001.md` § 23.2 shows why more
`f` pairs will not settle it either: 33,892 of them already exist for 2001 and the mapping
is still open.  The bits are direct observations of the oracle itself, under a table we
hold (`3B 7D B9 9E 22 71 E7 21`), so they constrain the mapping the way composite outputs
cannot -- and once it is solved it applies to the 7477/7D57 and 6B91/24A3 tables too,
whose parts are not to hand.

The wire, from I.G.O. 2's FINDIT.EXE, identical to the other two builds:

  command byte b : write (b & 0xFE)|0x80, b|0x81, (b & 0xFE)|0x80   -- DATA bit 0 clocks
  query q        : payload = ((q<<1)&0x0E) | ((q<<2)&0x60) | 0x80
                   write payload, payload|0x10, payload             -- DATA bit 4 clocks
                   answer = STATUS bit 5
  round preamble : command(seed), command(0x4E), write 0x84

State lives in memory rather than in registers throughout.  The port I/O dominates the
runtime by three orders of magnitude, so nothing is lost, and it keeps the register
pressure low enough that the code can be read.

    python mkdongcap_dos.py [--disasm]
"""
import os
import struct
import subprocess
import sys

OUT = 'DONGCAP.COM'
ORG = 0x100

POLY = 0x80500062
CA = 0x5B2C004A
CB = 0x803425C3

CHUNK = 512                      # entries per pass: 4 KB in, 4 KB out, 5 KB of bits

code = []
labels = {}
fixups = []


# ----------------------------------------------------------------- assembler

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


def id32(v):
    emit(v, v >> 8, v >> 16, v >> 24)


# --- the handful of forms this program uses ------------------------------

def mov_al_m(n):        emit(0xA0); a16(n)              # mov al,[n]
def mov_m_al(n):        emit(0xA2); a16(n)              # mov [n],al
def mov_ax_m(n):        emit(0xA1); a16(n)              # mov ax,[n]
def mov_m_ax(n):        emit(0xA3); a16(n)              # mov [n],ax
def mov_dx_m(n):        emit(0x8B, 0x16); a16(n)        # mov dx,[n]
def mov_bx_m(n):        emit(0x8B, 0x1E); a16(n)        # mov bx,[n]
def mov_m_bx(n):        emit(0x89, 0x1E); a16(n)        # mov [n],bx
def mov_cx_m(n):        emit(0x8B, 0x0E); a16(n)        # mov cx,[n]
def mov_m_cx(n):        emit(0x89, 0x0E); a16(n)        # mov [n],cx
def mov_mb_imm(n, v):   emit(0xC6, 0x06); a16(n); emit(v)          # mov byte [n],v
def mov_mw_imm(n, v):   emit(0xC7, 0x06); a16(n); iw(v)            # mov word [n],v
def cmp_mb_imm(n, v):   emit(0x80, 0x3E); a16(n); emit(v)          # cmp byte [n],v
def cmp_mw_imm(n, v):   emit(0x81, 0x3E); a16(n); iw(v)            # cmp word [n],v
def inc_mb(n):          emit(0xFE, 0x06); a16(n)                   # inc byte [n]
def dec_mw(n):          emit(0xFF, 0x0E); a16(n)                   # dec word [n]

def mov_eax_m(n):       emit(0x66, 0xA1); a16(n)        # mov eax,[n]
def mov_m_eax(n):       emit(0x66, 0xA3); a16(n)        # mov [n],eax
def xor_eax_m(n):       emit(0x66, 0x33, 0x06); a16(n)  # xor eax,[n]
def cmp_eax_m(n):       emit(0x66, 0x3B, 0x06); a16(n)  # cmp eax,[n]
def shr_m32_1(n):       emit(0x66, 0xD1, 0x2E); a16(n)  # shr dword [n],1
def xor_m32_imm(n, v):  emit(0x66, 0x81, 0x36); a16(n); id32(v)    # xor dword [n],v
def rol_m32_imm(n, v):  emit(0x66, 0xC1, 0x06); a16(n); emit(v)    # rol dword [n],v

def mov_eax_bx():       emit(0x66, 0x8B, 0x07)          # mov eax,[bx]
def mov_eax_bx4():      emit(0x66, 0x8B, 0x47, 0x04)    # mov eax,[bx+4]
def mov_bx_eax():       emit(0x66, 0x89, 0x07)          # mov [bx],eax
def mov_bx4_eax():      emit(0x66, 0x89, 0x47, 0x04)    # mov [bx+4],eax

def call(n):            emit(0xE8); rel16(n)
def jmp(n):             emit(0xE9); rel16(n)

# A conditional branch is rel8 only, and the error handlers sit past the data, so those
# go out as "skip over a near jump" instead.  The byte is the INVERSE condition.
def jfar(inverse, n):   emit(inverse, 3); emit(0xE9); rel16(n)
def jc_far(n):          jfar(0x73, n)      # jc  -> jnc over jmp
def jnc_far(n):         jfar(0x72, n)      # jnc -> jc  over jmp
def jnz_far(n):         jfar(0x74, n)      # jnz -> jz  over jmp
def jmps(n):            emit(0xEB); rel8(n)
def jz(n):              emit(0x74); rel8(n)
def jnz(n):             emit(0x75); rel8(n)
def jc(n):              emit(0x72); rel8(n)
def ret():              emit(0xC3)
def int21():            emit(0xCD, 0x21)


def dosprint(msg):
    """AH=09 print a $-terminated string"""
    emit(0xB4, 0x09)                       # mov ah,9
    emit(0xBA); a16(msg)                   # mov dx,msg
    int21()


# ================================================================= program

# --- entry: work out the port base ---------------------------------------
label('start')
emit(0xFC)                                 # cld
emit(0xB8); iw(0x0040)                     # mov ax,0x40
emit(0x8E, 0xC0)                           # mov es,ax
# copy the BIOS parallel-port table into ports[0..3]
emit(0x31, 0xF6)                           # xor si,si
label('cp')
emit(0x26, 0x8B, 0x44, 0x08)               # mov ax,es:[si+8]
emit(0x89, 0xF3)                           # mov bx,si
emit(0x89, 0x87); a16('ports')             # mov [bx+ports],ax
emit(0x83, 0xC6, 0x02)                     # add si,2
emit(0x83, 0xFE, 0x08)                     # cmp si,8
emit(0x72); rel8('cp')                     # jb cp
cmp_mw_imm('ports', 0)
jnz('haveports')
mov_mw_imm('ports', 0x378)                 # a BIOS that lists none still gets the default
label('haveports')

# Add any standard base the BIOS did not enumerate.  A cabinet whose BIOS lists only one
# port can still have the dongle on another, and MENU.EXE carries all three of these as
# immediates and probes them itself -- so touching them is no riskier than booting the
# machine.  Anything outside this set is still left alone, the I/O card included.
mov_mw_imm('stdi', 0)
label('sb')
mov_bx_m('stdi')
emit(0xD1, 0xE3)                           # shl bx,1
emit(0x8B, 0x87); a16('stdb')              # mov ax,[bx+stdb]
mov_m_ax('want')
mov_mw_imm('freeslot', 0xFFFF)
mov_mw_imm('pi', 0)
label('sbscan')
mov_bx_m('pi')
emit(0xD1, 0xE3)                           # shl bx,1
emit(0x8B, 0x87); a16('ports')             # mov ax,[bx+ports]
emit(0x3B, 0x06); a16('want')              # cmp ax,[want]
jz('sbnext')                               # already in the list
emit(0x09, 0xC0)                           # or ax,ax
jnz('sbcont')
cmp_mw_imm('freeslot', 0xFFFF)
jnz('sbcont')
mov_ax_m('pi')
mov_m_ax('freeslot')
label('sbcont')
emit(0xFF, 0x06); a16('pi')                # inc word [pi]
cmp_mw_imm('pi', 4)
emit(0x72); rel8('sbscan')                 # jb sbscan
cmp_mw_imm('freeslot', 0xFFFF)
jz('sbnext')                               # no room left
mov_bx_m('freeslot')
emit(0xD1, 0xE3)                           # shl bx,1
mov_ax_m('want')
emit(0x89, 0x87); a16('ports')             # mov [bx+ports],ax
label('sbnext')
emit(0xFF, 0x06); a16('stdi')              # inc word [stdi]
cmp_mw_imm('stdi', 3)
emit(0x72); rel8('sb')                     # jb sb

# an optional hex base on the command line overrides it: DONGCAP 278
emit(0x8A, 0x0E, 0x80, 0x00)               # mov cl,[0x80]     PSP tail length
emit(0x08, 0xC9)                           # or cl,cl
jz('noarg')
emit(0xBE); iw(0x0081)                     # mov si,0x81
emit(0x31, 0xDB)                           # xor bx,bx
label('argch')
emit(0xAC)                                 # lodsb
emit(0x3C, 0x20)                           # cmp al,' '
jz('argnext')
emit(0x3C, 0x30)                           # cmp al,'0'
jc('argdone')
emit(0x3C, 0x3A)                           # cmp al,'9'+1
jc('argdig')
emit(0x24, 0xDF)                           # and al,0xDF       upper-case
emit(0x3C, 0x41)                           # cmp al,'A'
jc('argdone')
emit(0x3C, 0x47)                           # cmp al,'F'+1
emit(0x73); rel8('argdone')                # jnc argdone
emit(0x2C, 0x37)                           # sub al,'A'-10
jmps('argacc')
label('argdig')
emit(0x2C, 0x30)                           # sub al,'0'
label('argacc')
emit(0x30, 0xE4)                           # xor ah,ah
emit(0xC1, 0xE3, 0x04)                     # shl bx,4
emit(0x01, 0xC3)                           # add bx,ax
label('argnext')
emit(0xFE, 0xC9)                           # dec cl
jnz('argch')
label('argdone')
emit(0x09, 0xDB)                           # or bx,bx
jz('noarg')
mov_m_bx('ports')                          # an explicit base is the only one tried
mov_mw_imm('ports1', 0)
mov_mw_imm('ports2', 0)
mov_mw_imm('ports3', 0)
label('noarg')

dosprint('m_open')

# --- what the BIOS says is there, and what each port's STATUS reads -------
# Passive: STATUS is only read, never written, so this cannot disturb anything.
# A port with nothing on it floats high and reads FF, which is the signature to look for.
dosprint('m_ports')
mov_mw_imm('portidx', 0)
label('pt')
call('setbase')
jz('ptnext')
dosprint('m_sp')
mov_ax_m('base')
call('hexword')
dosprint('m_eq')
mov_dx_m('base')
emit(0x42)                                 # inc dx
emit(0xEC)                                 # in al,dx
call('hexbyte')
label('ptnext')
emit(0xFF, 0x06); a16('portidx')           # inc word [portidx]
cmp_mw_imm('portidx', 4)
emit(0x72); rel8('pt')                     # jb pt
dosprint('m_crlf2')

# --- open DONGCAP.LST ----------------------------------------------------
emit(0xB8); iw(0x3D00)                     # mov ax,0x3D00     open, read-only
emit(0xBA); a16('f_lst')                   # mov dx,f_lst
int21()
jc_far('e_nolst')
mov_m_ax('h_lst')

# header: magic, ncal, count, nenc
emit(0xB9); iw(16)                         # mov cx,16
emit(0xBA); a16('hdr')                     # mov dx,hdr
call('rd')
mov_eax_m('hdr')
cmp_eax_m('k_dcap')
jnz_far('e_badlst')

# counts must fit 16 bits -- 23018 does, and it keeps every loop counter a word
emit(0xA1); a16('hdr_count_hi')            # mov ax,[hdr+10]
emit(0x09, 0xC0)                           # or ax,ax
jnz_far('e_big')

mov_ax_m('hdr_ncal')
cmp_mw_imm('hdr_ncal', 9)
jnc_far('e_badlst')                        # ncal must be < 9
emit(0xD1, 0xE0)                           # shl ax,1
emit(0xD1, 0xE0)                           # shl ax,1
emit(0xD1, 0xE0)                           # shl ax,1          ncal * 8
emit(0x89, 0xC1)                           # mov cx,ax
emit(0xBA); a16('calbuf')                  # mov dx,calbuf
call('rd')

# --- calibrate: find the port AND the seed the part answers to -----------
# A trip to the cabinet is expensive, so one run tries every parallel port the BIOS
# enumerated rather than making the operator guess a base and come back.  Only ports in
# that table are written to: the funworld I/O card is an 8255 at a DIP-selected base and
# poking arbitrary addresses could reconfigure it.
dosprint('m_cal')
mov_mw_imm('portidx', 0)
label('calport')
call('setbase')
jz('nextport')
dosprint('m_try')
mov_ax_m('base')
call('hexword')
mov_mb_imm('seed', 0)
label('calseed')
mov_mw_imm('calidx', 0)
label('calpair')
mov_bx_m('calidx')
emit(0xC1, 0xE3, 0x03)                     # shl bx,3          idx*8
emit(0x81, 0xC3); a16('calbuf')            # add bx,calbuf
mov_eax_bx()
mov_m_eax('vv')
mov_mw_imm('bitp', 0)                      # calibration bits are not kept
call('keyed')
mov_bx_m('calidx')
emit(0xC1, 0xE3, 0x03)                     # shl bx,3
emit(0x81, 0xC3); a16('calbuf')            # add bx,calbuf
mov_eax_m('vv')
emit(0x66, 0x3B, 0x47, 0x04)               # cmp eax,[bx+4]
jnz('calnext')
emit(0xFF, 0x06); a16('calidx')            # inc word [calidx]
mov_ax_m('calidx')
emit(0x3B, 0x06); a16('hdr_ncal')          # cmp ax,[hdr_ncal]
emit(0x72); rel8('calpair')                # jb calpair
jmp('calfound')
label('calnext')
inc_mb('seed')
cmp_mb_imm('seed', 0)
jnz('calseed')                             # wraps after 256
label('nextport')
emit(0xFF, 0x06); a16('portidx')           # inc word [portidx]
cmp_mw_imm('portidx', 4)
emit(0x72); rel8('calport')                # jb calport
jmp('caldiag')

label('calfound')
dosprint('m_seedok')
mov_al_m('seed')
call('hexbyte')
dosprint('m_aton')
mov_ax_m('base')
call('hexword')
dosprint('m_crlf')

# --- create the two output files -----------------------------------------
emit(0xB8); iw(0x3C00)                     # mov ax,0x3C00     create
emit(0x31, 0xC9)                           # xor cx,cx
emit(0xBA); a16('f_bin')                   # mov dx,f_bin
int21()
jc_far('e_nocreate')
mov_m_ax('h_bin')
emit(0xB8); iw(0x3C00)
emit(0x31, 0xC9)
emit(0xBA); a16('f_bit')                   # mov dx,f_bit
int21()
jc_far('e_nocreate')
mov_m_ax('h_bit')

# DOUT header: magic, count, seed, nenc
mov_eax_m('k_dout')
mov_m_eax('ohdr')
mov_eax_m('hdr_count')
mov_m_eax('ohdr_count')
emit(0x66, 0x31, 0xC0)                     # xor eax,eax
mov_al_m('seed')
mov_m_eax('ohdr_seed')
mov_eax_m('hdr_nenc')
mov_m_eax('ohdr_nenc')
mov_bx_m('h_bin')
emit(0xB9); iw(16)                         # mov cx,16
emit(0xBA); a16('ohdr')
emit(0xB4, 0x40); int21()                  # write

# --- the decode entries ---------------------------------------------------
dosprint('m_cap')
mov_mb_imm('mode', 0)
mov_ax_m('hdr_count')
mov_m_ax('remain')
call('runall')

# --- the encode entries ---------------------------------------------------
mov_mb_imm('mode', 1)
mov_ax_m('hdr_nenc')
mov_m_ax('remain')
call('runall')

mov_bx_m('h_bin'); emit(0xB4, 0x3E); int21()
mov_bx_m('h_bit'); emit(0xB4, 0x3E); int21()
mov_bx_m('h_lst'); emit(0xB4, 0x3E); int21()
dosprint('m_done')
emit(0xB8); iw(0x4C00); int21()            # exit 0

# ----------------------------------------------------------------- runall
# process [remain] entries, CHUNK at a time
label('runall')
cmp_mw_imm('remain', 0)
jnz('run_go')
ret()
label('run_go')
mov_ax_m('remain')
cmp_mw_imm('remain', CHUNK)
emit(0x72); rel8('run_n')                  # jb run_n
emit(0xB8); iw(CHUNK)                      # mov ax,CHUNK
label('run_n')
mov_m_ax('chunkn')

# read chunkn*8 bytes
emit(0x89, 0xC1)                           # mov cx,ax
emit(0xC1, 0xE1, 0x03)                     # shl cx,3
emit(0xBA); a16('inbuf')
call('rd')

mov_mw_imm('bitp', 0)
mov_mw_imm('entidx', 0)
label('run_ent')
mov_bx_m('entidx')
emit(0xC1, 0xE3, 0x03)                     # shl bx,3
emit(0x81, 0xC3); a16('inbuf')             # add bx,inbuf
mov_m_bx('entp')
cmp_mb_imm('mode', 0)
jnz('run_enc')
call('do_decode')
jmps('run_st')
label('run_enc')
call('do_encode')
label('run_st')
mov_bx_m('entidx')
emit(0xC1, 0xE3, 0x03)                     # shl bx,3
emit(0x81, 0xC3); a16('outbuf')            # add bx,outbuf
mov_eax_m('f1')
mov_bx_eax()
mov_eax_m('f2')
mov_bx4_eax()
emit(0xFF, 0x06); a16('entidx')            # inc word [entidx]
mov_ax_m('entidx')
emit(0x3B, 0x06); a16('chunkn')            # cmp ax,[chunkn]
emit(0x72); rel8('run_ent')                # jb run_ent

# write both buffers
mov_bx_m('h_bin')
mov_cx_m('chunkn')
emit(0xC1, 0xE1, 0x03)                     # shl cx,3
emit(0xBA); a16('outbuf')
emit(0xB4, 0x40); int21()
mov_bx_m('h_bit')
mov_cx_m('bitp')
emit(0xBA); a16('bitbuf')
emit(0xB4, 0x40); int21()

emit(0xB4, 0x02)                           # mov ah,2
emit(0xB2, 0x2E)                           # mov dl,'.'
int21()

mov_ax_m('chunkn')
emit(0x29, 0x06); a16('remain')            # sub [remain],ax
jmp('runall')

# ------------------------------------------------------------- do_decode
# [entp] -> (L1,R1);  f1 = f(L1);  L3 = B(f1^R1, L1);  f2 = f(L3)
label('do_decode')
mov_bx_m('entp')
mov_eax_bx()
mov_m_eax('vv')
call('keyed')
mov_eax_m('vv')
mov_m_eax('f1')
mov_bx_m('entp')
emit(0x66, 0x33, 0x47, 0x04)               # xor eax,[bx+4]    f1 ^ R1
mov_m_eax('b0')
mov_bx_m('entp')
mov_eax_bx()                               # L1
mov_m_eax('b1')
call('b_first')
mov_eax_m('b0')
mov_m_eax('vv')
call('keyed')
mov_eax_m('vv')
mov_m_eax('f2')
ret()

# ------------------------------------------------------------- do_encode
# [entp] -> (P0,P1);  L3 = P1;  f2 = f(L3);  R3 = P0^f2;  L1 = Bfwd(L3,R3);  f1 = f(L1)
label('do_encode')
mov_bx_m('entp')
mov_eax_bx4()                              # P1 = L3
mov_m_eax('vv')
call('keyed')
mov_eax_m('vv')
mov_m_eax('f2')
mov_bx_m('entp')
mov_eax_bx()                               # P0
xor_eax_m('f2')                            # R3 = P0 ^ f2
mov_m_eax('b1')
mov_bx_m('entp')
mov_eax_bx4()                              # L3
mov_m_eax('b0')
call('b_fwd')
mov_eax_m('b1')                            # Bfwd leaves L1 in b1
mov_m_eax('vv')
call('keyed')
mov_eax_m('vv')
mov_m_eax('f1')
ret()

# --------------------------------------------------------------- b_first
# six rounds descending: t = ROL(b0^CB, s) ^ b1 ; b1 = b0 ; b0 = t
label('b_first')
for s in (10, 8, 6, 4, 2, 0):
    mov_eax_m('b0')
    emit(0x66, 0x35); id32(CB)             # xor eax,CB
    mov_m_eax('tt')
    if s:
        rol_m32_imm('tt', s)
    mov_eax_m('tt')
    xor_eax_m('b1')
    mov_m_eax('tt')
    mov_eax_m('b0')
    mov_m_eax('b1')
    mov_eax_m('tt')
    mov_m_eax('b0')
ret()

# ----------------------------------------------------------------- b_fwd
# the same stage ascending: t = ROL(b1^CB, s) ^ b0 ; b0 = b1 ; b1 = t
label('b_fwd')
for s in (0, 2, 4, 6, 8, 10):
    mov_eax_m('b1')
    emit(0x66, 0x35); id32(CB)             # xor eax,CB
    mov_m_eax('tt')
    if s:
        rol_m32_imm('tt', s)
    mov_eax_m('tt')
    xor_eax_m('b0')
    mov_m_eax('tt')
    mov_eax_m('b1')
    mov_m_eax('b0')
    mov_eax_m('tt')
    mov_m_eax('b1')
ret()

# ----------------------------------------------------------------- keyed
# vv in, vv out.  40 consultations, 39 shift steps.  Appends 5 bytes to bitbuf.
label('keyed')
mov_mb_imm('bitcnt', 0)
for i in range(5):
    mov_mb_imm('bitstage%d' % i, 0)

# preamble
mov_al_m('seed')
call('cmdbyte')
emit(0xB0, 0x4E)                           # mov al,0x4E
call('cmdbyte')
emit(0xB0, 0x84)                           # mov al,0x84
call('rawout')

# prev = query(v & 0xFF)
mov_al_m('vv')
call('query')
mov_m_al('prev')
call('putbit')

mov_mw_imm('kcnt', 39)
label('k_loop')
# idx = (prev & 1) | ((v & 1) << 1)
mov_al_m('prev')
emit(0x24, 0x01)                           # and al,1
emit(0x88, 0xC4)                           # mov ah,al
mov_al_m('vv')
emit(0x24, 0x01)                           # and al,1
emit(0xD0, 0xE0)                           # shl al,1
emit(0x08, 0xE0)                           # or al,ah
mov_m_al('idx')
# bit = (idx ^ v) & 1
emit(0x32, 0x06); a16('vv')                # xor al,[vv]
emit(0x24, 0x01)                           # and al,1
mov_m_al('fold')
shr_m32_1('vv')
cmp_mb_imm('fold', 0)
jz('k_noxor')
xor_m32_imm('vv', POLY)
label('k_noxor')
# prev = query((v >> (8*idx)) & 0xFF)
mov_al_m('idx')
emit(0xC0, 0xE0, 0x03)                     # shl al,3          idx*8
emit(0x88, 0xC1)                           # mov cl,al
mov_eax_m('vv')
emit(0x66, 0xD3, 0xE8)                     # shr eax,cl
call('query')
mov_m_al('prev')
call('putbit')
dec_mw('kcnt')
mov_ax_m('kcnt')
emit(0x09, 0xC0)                           # or ax,ax
jnz('k_loop')

# append the five staged bytes to bitbuf
mov_bx_m('bitp')
emit(0x81, 0xC3); a16('bitbuf')            # add bx,bitbuf
for i in range(5):
    mov_al_m('bitstage%d' % i)
    emit(0x88, 0x47, i)                    # mov [bx+i],al
mov_ax_m('bitp')
emit(0x83, 0xC0, 0x05)                     # add ax,5
mov_m_ax('bitp')
ret()

# ---------------------------------------------------------------- putbit
# al = 0/1, MSB first into bitstage[bitcnt>>3]
label('putbit')
emit(0x88, 0xC4)                           # mov ah,al
mov_al_m('bitcnt')
emit(0x88, 0xC1)                           # mov cl,al
emit(0x80, 0xE1, 0x07)                     # and cl,7
emit(0xB5, 0x07)                           # mov ch,7
emit(0x28, 0xCD)                           # sub ch,cl
emit(0x88, 0xE9)                           # mov cl,ch
emit(0xD2, 0xE4)                           # shl ah,cl
emit(0xC0, 0xE8, 0x03)                     # shr al,3          byte index
emit(0x30, 0xFF)                           # xor bh,bh
emit(0x88, 0xC3)                           # mov bl,al
emit(0x08, 0xA7); a16('bitstage0')         # or [bx+bitstage0],ah
inc_mb('bitcnt')
ret()

# ----------------------------------------------------------------- query
# al = q  ->  al = 0/1
label('query')
emit(0x88, 0xC4)                           # mov ah,al
emit(0xD0, 0xE0)                           # shl al,1
emit(0x24, 0x0E)                           # and al,0x0E
emit(0xC0, 0xE4, 0x02)                     # shl ah,2
emit(0x80, 0xE4, 0x60)                     # and ah,0x60
emit(0x08, 0xE0)                           # or al,ah
emit(0x0C, 0x80)                           # or al,0x80
mov_m_al('pay')
call('rawout')
mov_al_m('pay')
emit(0x0C, 0x10)                           # or al,0x10
call('rawout')
mov_al_m('pay')
call('rawout')
mov_dx_m('base')
emit(0x42)                                 # inc dx
emit(0xEC)                                 # in al,dx
emit(0xC0, 0xE8, 0x05)                     # shr al,5
emit(0x24, 0x01)                           # and al,1
ret()

# --------------------------------------------------------------- cmdbyte
label('cmdbyte')
emit(0x50)                                 # push ax
emit(0x24, 0xFE)                           # and al,0xFE
emit(0x0C, 0x80)                           # or al,0x80
call('rawout')
emit(0x58)                                 # pop ax
emit(0x50)                                 # push ax
emit(0x0C, 0x81)                           # or al,0x81
call('rawout')
emit(0x58)                                 # pop ax
emit(0x24, 0xFE)                           # and al,0xFE
emit(0x0C, 0x80)                           # or al,0x80
call('rawout')
ret()

# ---------------------------------------------------------------- rawout
label('rawout')
mov_dx_m('base')
emit(0xEE)                                 # out dx,al
emit(0x50)                                 # push ax
emit(0xE4, 0x80)                           # in al,0x80        the traditional I/O delay
emit(0xE4, 0x80)                           # in al,0x80
emit(0x58)                                 # pop ax
ret()

# -------------------------------------------------------------------- rd
# cx bytes from h_lst to dx; a short read is fatal
label('rd')
emit(0x53)                                 # push bx
emit(0x51)                                 # push cx
mov_bx_m('h_lst')
emit(0xB4, 0x3F)                           # mov ah,0x3F
int21()
emit(0x59)                                 # pop cx
emit(0x39, 0xC8)                           # cmp ax,cx
emit(0x5B)                                 # pop bx
jnz_far('e_short')
ret()

# --------------------------------------------------------------- setbase
# base = ports[portidx]; returns with ZF set when that slot is empty
label('setbase')
mov_bx_m('portidx')
emit(0xD1, 0xE3)                           # shl bx,1
emit(0x8B, 0x87); a16('ports')             # mov ax,[bx+ports]
mov_m_ax('base')
emit(0x09, 0xC0)                           # or ax,ax
ret()

# --------------------------------------------------------------- hexword
label('hexword')
emit(0x50)                                 # push ax
emit(0x88, 0xE0)                           # mov al,ah
call('hexbyte')
emit(0x58)                                 # pop ax
call('hexbyte')
ret()

# --------------------------------------------------------------- hexbyte
label('hexbyte')
emit(0x50)                                 # push ax
emit(0xC0, 0xE8, 0x04)                     # shr al,4
call('hexnib')
emit(0x58)                                 # pop ax
emit(0x24, 0x0F)                           # and al,0x0F
label('hexnib')
emit(0x3C, 0x0A)                           # cmp al,10
emit(0x72, 0x02)                           # jb +2
emit(0x04, 0x07)                           # add al,7
emit(0x04, 0x30)                           # add al,'0'
emit(0x88, 0xC2)                           # mov dl,al
emit(0xB4, 0x02)                           # mov ah,2
int21()
ret()

# ----------------------------------------------------------------- caldiag
label('caldiag')
dosprint('m_nocal')
emit(0xB8); iw(0x3C00)
emit(0x31, 0xC9)
emit(0xBA); a16('f_dia')
int21()
jc_far('e_nocreate')
mov_m_ax('h_bin')

# one section per parallel port: the base as a word, then 256 dwords, one per seed
mov_mw_imm('portidx', 0)
label('dgport')
call('setbase')
jz('dgnext')
mov_bx_m('h_bin')
emit(0xB9); iw(2)                          # mov cx,2
emit(0xBA); a16('base')
emit(0xB4, 0x40); int21()
mov_mb_imm('seed', 0)
mov_mw_imm('entidx', 0)
label('dg_loop')
mov_eax_m('calbuf')
mov_m_eax('vv')
mov_mw_imm('bitp', 0)
call('keyed')
mov_bx_m('entidx')
emit(0xC1, 0xE3, 0x02)                     # shl bx,2
emit(0x81, 0xC3); a16('outbuf')            # add bx,outbuf
mov_eax_m('vv')
mov_bx_eax()
emit(0xFF, 0x06); a16('entidx')            # inc word [entidx]
inc_mb('seed')
cmp_mb_imm('seed', 0)
jnz('dg_loop')
mov_bx_m('h_bin')
emit(0xB9); iw(1024)                       # mov cx,1024
emit(0xBA); a16('outbuf')
emit(0xB4, 0x40); int21()
emit(0xB4, 0x02)                           # mov ah,2
emit(0xB2, 0x2E)                           # mov dl,'.'
int21()
label('dgnext')
emit(0xFF, 0x06); a16('portidx')           # inc word [portidx]
cmp_mw_imm('portidx', 4)
emit(0x72); rel8('dgport')                 # jb dgport
mov_bx_m('h_bin')
emit(0xB4, 0x3E); int21()
dosprint('m_diag')
emit(0xB8); iw(0x4C02); int21()

# ------------------------------------------------------------------ errors
def errexit(lbl, msg):
    label(lbl)
    dosprint(msg)
    emit(0xB8); iw(0x4C01)
    int21()


errexit('e_nolst', 'm_nolst')
errexit('e_badlst', 'm_badlst')
errexit('e_big', 'm_big')
errexit('e_short', 'm_short')
errexit('e_nocreate', 'm_nocreate')

# =================================================================== data

def dbytes(name, bs):
    label(name)
    emit(*bs)


def dstr(name, s):
    dbytes(name, list(s.encode('cp437')) + [ord('$')])


def dword(name, v=0):
    label(name)
    id32(v)


def dw(name, v=0):
    label(name)
    iw(v)


def db(name, v=0):
    label(name)
    emit(v)


dstr('m_open',   'DONGCAP -- reading DONGCAP.LST\r\n')
dstr('m_cal',    'Calibrating -- every seed on every port')
dstr('m_ports',  '\r\nBIOS parallel ports (base=STATUS):')
dstr('m_sp',     '  ')
dstr('m_eq',     '=')
dstr('m_try',    '\r\n  port ')
dstr('m_aton',   ' at port ')
dstr('m_crlf2',  '\r\n')
dstr('m_seedok', '\r\nSeed 0x')
dstr('m_crlf',   ' reproduces every calibration pair.\r\n')
dstr('m_cap',    'Capturing ')
dstr('m_done',   '\r\nDone -- DONGCAP.BIN and DONGCAP.BIT written. Send both back.\r\n')
dstr('m_nocal',  '\r\nNo seed reproduces the known answers.\r\n'
                 'Writing DONGCAP.DIA instead -- every seed against the first input.\r\n')
dstr('m_diag',   'DONGCAP.DIA written. Send it back -- it says what the part answers.\r\n')
dstr('m_nolst',  'DONGCAP.LST not found -- keep it next to this program.\r\n')
dstr('m_badlst', 'DONGCAP.LST is not a capture list.\r\n')
dstr('m_big',    'This list is too large for the DOS build.\r\n')
dstr('m_short',  'DONGCAP.LST is truncated.\r\n')
dstr('m_nocreate', 'Cannot create the output file.\r\n')

dbytes('f_lst', list(b'DONGCAP.LST') + [0])
dbytes('f_bin', list(b'DONGCAP.BIN') + [0])
dbytes('f_bit', list(b'DONGCAP.BIT') + [0])
dbytes('f_dia', list(b'DONGCAP.DIA') + [0])

dword('k_dcap', 0x50414344)                # 'DCAP'
dword('k_dout', 0x54554F44)                # 'DOUT'

label('hdr'); id32(0)
label('hdr_ncal'); iw(0)
dw('hdr_ncal_hi')
label('hdr_count'); iw(0)
label('hdr_count_hi'); iw(0)
label('hdr_nenc'); iw(0)
dw('hdr_nenc_hi')

label('ohdr'); id32(0)
dword('ohdr_count')
dword('ohdr_seed')
dword('ohdr_nenc')

dw('base')
dw('portidx')
dw('ports')
dw('ports1')
dw('ports2')
dw('ports3')
dw('stdi')
dw('pi')
dw('want')
dw('freeslot')
label('stdb'); iw(0x03BC); iw(0x0378); iw(0x0278)
dw('scratch')
db('seed')
db('prev')
db('pay')
db('idx')
db('fold')
db('mode')
db('bitcnt')
db('bitstage0'); db('bitstage1'); db('bitstage2'); db('bitstage3'); db('bitstage4')
dw('bitp')
dw('kcnt')
dw('calidx')
dw('entidx')
dw('chunkn')
dw('remain')
dw('entp')
dw('h_lst')
dw('h_bin')
dw('h_bit')
dword('vv')
dword('b0')
dword('b1')
dword('tt')
dword('f1')
dword('f2')

label('calbuf')
for _ in range(64):
    emit(0)
label('inbuf')
for _ in range(CHUNK * 8):
    emit(0)
label('outbuf')
for _ in range(CHUNK * 8):
    emit(0)
label('bitbuf')
for _ in range(CHUNK * 10):
    emit(0)


# =================================================================== link

def link():
    out = bytearray(code)
    for at, kind, name in fixups:
        if name not in labels:
            raise SystemExit('undefined label: %s' % name)
        tgt = labels[name]
        if kind == 'rel8':
            d = tgt - (at + 1)
            if not -128 <= d <= 127:
                raise SystemExit('rel8 out of range to %s (%d)' % (name, d))
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
    print('%s: %d bytes (code+data), buffers %d/%d/%d'
          % (OUT, len(blob), CHUNK * 8, CHUNK * 8, CHUNK * 10))
    print('   entry %04X  keyed %04X  query %04X  rawout %04X'
          % (ORG, labels['keyed'] + ORG, labels['query'] + ORG, labels['rawout'] + ORG))
    if '--disasm' in sys.argv:
        end = labels['m_open']
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
