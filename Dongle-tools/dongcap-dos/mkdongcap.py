"""Build DONGCAP.COM -- the DOS build of the dongle capture.

Why there is a DOS build
------------------------
The Windows build asks for port access with `NtSetInformationProcess(ProcessUserModeIOPL)`.
That call needs **SeTcbPrivilege**, which on Windows XP is held by *nobody* by default --
not even Administrators -- so "Run as Administrator" was never enough and the tool could
not have worked on a stock XP. Granting that privilege means editing the local security
policy and logging out, and even then the whole thing rests on an undocumented call.

Real mode has no such gate. A .COM owns the machine: `out dx, al` is `out dx, al`. This
build runs on the cabinets themselves, on any DOS, and on a FreeDOS stick booted on the
same laptop.

There is no DOS toolchain on this machine, so the program is assembled here with keystone
and checked by disassembling the result. Same approach as DONGTEST.COM, which is where the
wire sequence below was first put on a real part.

What it does
------------
Identical to dongcap.c, entry for entry, with two differences:

  * it **streams** the work list. The lists are 200-280 KB and a .COM has one 64 KB
    segment, so entries are read a chunk at a time and results written out as they go.
  * it captures the **EncodeData blocks** as well. dongcap.c reads `nenc` out of the
    header, sizes its output as though it had captured them, and then never touches them --
    so I.G.O. 3's boot check was two dwords of uninitialised memory in every DONGCAP.BIN.

Files, all 8.3 because DOS:

    DONGCAP.LST   in    the work list, unchanged from the Windows build
    DONGCAP.BIN   out   the capture
    DONGCAP.DIA   out   written instead when no seed calibrates (DONGCAP.DIAG on Windows)

Usage:  DONGCAP            LPT1 base from the BIOS data area, else 0x378
        DONGCAP 278        base in hex
"""
import re
import sys

from capstone import Cs, CS_ARCH_X86, CS_MODE_16
from keystone import Ks, KS_ARCH_X86, KS_MODE_16

ORG    = 0x100
POLY   = 0x80500062
CB     = 0x803425C3

# Buffers live past the end of the image: DOS hands a .COM the whole 64 KB segment, so
# there is no reason to carry 8 KB of zeros in the file.
CALBUF = 0x3F00        # ncal * 8, tiny
INBUF  = 0x4000        # 512 work pairs
OUTBUF = 0x6000        # 512 result pairs, and the DIAG table
CHUNK  = 512

SRC = r"""
            mov  sp, 0xFFF0

            ; ---- LPT base: the command tail if it has one, else the BIOS area ----
            mov  si, 0x81
            xor  bx, bx                 ; accumulated hex
            xor  di, di                 ; saw a digit
parse:      lodsb
            cmp  al, 0x0D
            je   parsed
            cmp  al, ' '
            je   parse
            cmp  al, 9
            je   parse
            sub  al, '0'
            jb   parsed
            cmp  al, 9
            jbe  digit
            and  al, 0xDF               ; upper-case
            sub  al, 7
            cmp  al, 10
            jb   parsed
            cmp  al, 15
            ja   parsed
digit:      mov  cl, 4
            shl  bx, cl
            xor  ah, ah
            or   bl, al
            inc  di
            jmp  parse
parsed:     or   di, di
            jz   frombios
            mov  [base], bx
            jmp  gotbase
frombios:   push es
            mov  ax, 0x40
            mov  es, ax
            mov  bx, es:[8]
            pop  es
            or   bx, bx
            jnz  bios_ok
            mov  bx, 0x378
bios_ok:    mov  [base], bx
gotbase:
            mov  dx, msg_banner
            call print
            mov  ax, [base]
            call printhex16
            mov  dx, msg_crlf
            call print

            ; ---- open the list ----
            mov  ax, 0x3D00
            mov  dx, fn_lst
            int  0x21
            jc   no_lst
            mov  [hlst], ax

            ; header: magic, ncal, count, nenc
            mov  bx, [hlst]
            mov  ax, 0x3F00
            mov  cx, 16
            mov  dx, hdr
            int  0x21
            cmp  ax, 16
            jne  bad_lst
            mov  eax, [hdr]
            cmp  eax, 0x50414344        ; 'DCAP'
            jne  bad_lst

            ; calibration pairs straight after it
            mov  eax, [hdr+4]
            mov  [ncal], eax
            shl  ax, 3
            mov  cx, ax
            mov  bx, [hlst]
            mov  ax, 0x3F00
            mov  dx, CALBUF
            int  0x21

            ; ---- calibrate: the seed the part actually answers to ----
            mov  dx, msg_cal
            call print
            mov  byte ptr [seed], 0
calseed:    mov  esi, 0                 ; pair index
calpair:    mov  eax, [ncal]
            cmp  esi, eax
            jae  calgood                ; every pair matched
            mov  bx, si
            shl  bx, 3
            add  bx, CALBUF
            mov  eax, [bx]
            call keyed_round
            mov  bx, si
            shl  bx, 3
            add  bx, CALBUF
            cmp  eax, [bx+4]
            jne  calnext
            inc  esi
            jmp  calpair
calnext:    mov  al, [seed]
            test al, 15
            jnz  calnodot
            mov  dx, msg_dot
            call print
calnodot:   inc  byte ptr [seed]
            jnz  calseed
            jmp  caldiag                ; all 256 tried, none fits

calgood:    mov  dx, msg_seed
            call print
            mov  al, [seed]
            call printhex8
            mov  dx, msg_crlf
            call print

            ; ---- create the output and write its header ----
            mov  ax, 0x3C00
            xor  cx, cx
            mov  dx, fn_bin
            int  0x21
            jc   no_out
            mov  [hout], ax
            mov  dword ptr [obuf], 0x54554F44   ; 'DOUT'
            mov  eax, [hdr+8]
            mov  [obuf+4], eax              ; count
            xor  eax, eax
            mov  al, [seed]
            mov  [obuf+8], eax              ; seed
            mov  eax, [hdr+12]
            mov  [obuf+12], eax             ; nenc
            mov  bx, [hout]
            mov  ax, 0x4000
            mov  cx, 16
            mov  dx, obuf
            int  0x21

            mov  dx, msg_capt
            call print
            mov  eax, [hdr+8]
            mov  [left], eax
            xor  eax, eax
            mov  [done], eax

            ; ---- the work entries, a chunk at a time ----
chunk:      mov  eax, [left]
            or   eax, eax
            jz   encblocks
            mov  ebx, 512
            cmp  eax, ebx
            jbe  chunk_n
            mov  eax, ebx
chunk_n:    mov  [nthis], eax
            mov  cx, ax
            shl  cx, 3
            mov  bx, [hlst]
            mov  ax, 0x3F00
            mov  dx, INBUF
            int  0x21

            xor  si, si                 ; entry index within the chunk
onepair:    mov  eax, [nthis]
            cmp  si, ax
            jae  chunk_out
            mov  bx, si
            shl  bx, 3
            add  bx, INBUF
            mov  eax, [bx]              ; L1
            mov  ebp, [bx+4]            ; R1
            push bx
            call keyed_round            ; f1
            pop  bx
            mov  edx, eax               ; keep f1
            push edx
            xor  eax, ebp               ; f1 ^ R1
            mov  ebp, [bx]              ; L1
            mov  edx, ebp
            call b_rounds_first         ; L3
            call keyed_round            ; f2
            pop  edx                    ; f1
            mov  bx, si
            shl  bx, 3
            add  bx, OUTBUF
            mov  [bx], edx
            mov  [bx+4], eax
            inc  si
            jmp  onepair

chunk_out:  mov  eax, [nthis]
            mov  cx, ax
            shl  cx, 3
            mov  bx, [hout]
            mov  ax, 0x4000
            mov  dx, OUTBUF
            int  0x21
            mov  eax, [left]
            sub  eax, [nthis]
            mov  [left], eax
            mov  dx, msg_dot
            call print
            jmp  chunk

            ; ---- the EncodeData blocks, if the list carries any ----
encblocks:  mov  eax, [hdr+12]
            or   eax, eax
            jz   finished
            mov  [left], eax
            xor  si, si
encone:     mov  eax, [left]
            or   eax, eax
            jz   encdone
            mov  cx, 8
            mov  bx, [hlst]
            mov  ax, 0x3F00
            mov  dx, INBUF
            int  0x21
            mov  eax, [INBUF]           ; P0
            call keyed_round            ; f1
            mov  edx, eax
            push edx
            xor  eax, [INBUF+4]         ; f1 ^ P1
            mov  edx, [INBUF]           ; P0
            call b_rounds_fwd_second
            call keyed_round            ; f2
            pop  edx
            mov  [OUTBUF], edx
            mov  [OUTBUF+4], eax
            mov  bx, [hout]
            mov  ax, 0x4000
            mov  cx, 8
            mov  dx, OUTBUF
            int  0x21
            mov  eax, [left]
            dec  eax
            mov  [left], eax
            jmp  encone
encdone:
finished:   mov  bx, [hout]
            mov  ax, 0x3E00
            int  0x21
            mov  bx, [hlst]
            mov  ax, 0x3E00
            int  0x21
            mov  dx, msg_done
            call print
            jmp  bye

caldiag:    mov  dx, msg_nocal
            call print
            ; every seed against the first inputs, so a failure still leaves evidence
            mov  eax, [ncal]
            mov  ebx, 3
            cmp  eax, ebx
            jbe  nd_ok
            mov  eax, ebx
nd_ok:      mov  [nd], eax
            mov  dword ptr [OUTBUF], 0x47414944     ; 'DIAG'
            mov  eax, [nd]
            mov  [OUTBUF+4], eax
            mov  di, OUTBUF+8
            mov  byte ptr [seed], 0
dgseed:     xor  esi, esi
dgpair:     mov  eax, [nd]
            cmp  esi, eax
            jae  dgnext
            mov  bx, si
            shl  bx, 3
            add  bx, CALBUF
            mov  eax, [bx]
            call keyed_round
            mov  bx, di
            mov  [bx], eax
            add  di, 4
            inc  esi
            jmp  dgpair
dgnext:     inc  byte ptr [seed]
            jnz  dgseed
            mov  ax, 0x3C00
            xor  cx, cx
            mov  dx, fn_dia
            int  0x21
            jc   bye
            mov  bx, ax
            mov  cx, di
            sub  cx, OUTBUF
            mov  ax, 0x4000
            mov  dx, OUTBUF
            int  0x21
            mov  ax, 0x3E00
            int  0x21
            mov  dx, msg_diag
            call print
            jmp  bye

no_lst:     mov  dx, msg_nolst
            call print
            jmp  bye
bad_lst:    mov  dx, msg_badlst
            call print
            jmp  bye
no_out:     mov  dx, msg_noout
            call print

bye:        mov  dx, msg_key
            call print
            mov  ah, 8
            int  0x21
            mov  ax, 0x4C00
            int  0x21

; ------------------------------------------------------------------ the wire

; AL = byte on the DATA lines.  Two reads of port 0x80 are the traditional I/O delay.
rawout:     push ax
            push dx
            mov  dx, [base]
            out  dx, al
            in   al, 0x80
            in   al, 0x80
            pop  dx
            pop  ax
            ret

; AL = command byte; DATA bit 0 is the clock, bit 7 always set   (0x32DE2)
cmdbyte:    push bx
            mov  bl, al
            and  al, 0xFE
            or   al, 0x80
            call rawout
            mov  al, bl
            or   al, 0x81
            call rawout
            mov  al, bl
            and  al, 0xFE
            or   al, 0x80
            call rawout
            pop  bx
            ret

; AL = 5-bit query -> AL = the answer bit.  DATA bit 4 clocks it, STATUS bit 5 replies.
query:      push bx
            push dx
            mov  bl, al
            shl  al, 1
            and  al, 0x0E
            mov  bh, al
            mov  al, bl
            shl  al, 1
            shl  al, 1
            and  al, 0x60
            or   al, bh
            or   al, 0x80
            mov  bh, al
            call rawout
            mov  al, bh
            or   al, 0x10
            call rawout
            mov  al, bh
            call rawout
            mov  dx, [base]
            inc  dx
            in   al, dx
            shr  al, 1
            shr  al, 1
            shr  al, 1
            shr  al, 1
            shr  al, 1
            and  al, 1
            pop  dx
            pop  bx
            ret

; EAX = v -> EAX.  39 shift steps, 40 consultations; the byte offered to the part is
; picked by the previous answer and the bit about to leave  (0x32749).
keyed_round:
            push ebx
            push ecx
            push edx
            push esi
            push di
            mov  esi, eax
            mov  al, [seed]
            call cmdbyte
            mov  al, 0x4E
            call cmdbyte
            mov  al, 0x84
            call rawout
            mov  eax, esi
            call query
            mov  bl, al                 ; prev
            mov  di, 39
krloop:     mov  eax, esi
            and  al, 1
            shl  al, 1
            or   al, bl                 ; idx = (prev & 1) | ((v & 1) << 1)
            and  al, 3
            mov  dh, al                 ; idx
            mov  eax, esi
            and  al, 1
            xor  al, bl
            and  al, 1                  ; (idx ^ v) & 1  ==  (v ^ prev) & 1
            shr  esi, 1
            or   al, al
            jz   krnoxor
            xor  esi, 0x80500062
krnoxor:    mov  eax, esi
            mov  cl, dh
            shl  cl, 1
            shl  cl, 1
            shl  cl, 1                  ; 8 * idx
            shr  eax, cl
            call query
            mov  bl, al
            dec  di
            jnz  krloop
            mov  eax, esi
            pop  di
            pop  esi
            pop  edx
            pop  ecx
            pop  ebx
            ret

; EAX = b0, EDX = b1 -> EAX.  Six rounds descending; this is L3.
b_rounds_first:
            push ecx
            push ebx
            mov  cl, 10
brfl:       mov  ebx, eax
            xor  ebx, 0x803425C3
            rol  ebx, cl
            xor  ebx, edx
            mov  edx, eax
            mov  eax, ebx
            sub  cl, 2
            jnc  brfl
            pop  ebx
            pop  ecx
            ret

; EAX = b0, EDX = b1 -> EAX.  The same stage ascending, second output  (0x32837).
b_rounds_fwd_second:
            push ecx
            push ebx
            mov  cl, 0
brsl:       mov  ebx, edx
            xor  ebx, 0x803425C3
            rol  ebx, cl
            xor  ebx, eax
            mov  eax, edx
            mov  edx, ebx
            add  cl, 2
            cmp  cl, 12
            jb   brsl
            mov  eax, edx
            pop  ebx
            pop  ecx
            ret

; ------------------------------------------------------------------ console

print:      push ax
            mov  ah, 9
            int  0x21
            pop  ax
            ret

printhex8:  push ax
            push bx
            mov  bl, al
            shr  al, 1
            shr  al, 1
            shr  al, 1
            shr  al, 1
            call nyb
            mov  al, bl
            and  al, 0x0F
            call nyb
            pop  bx
            pop  ax
            ret

printhex16: push ax
            mov  bx, ax
            mov  al, bh
            call printhex8
            mov  ax, bx
            call printhex8
            pop  ax
            ret

nyb:        push dx
            add  al, '0'
            cmp  al, '9'
            jbe  nybout
            add  al, 7
nybout:     mov  [chbuf], al
            mov  dx, chbuf
            mov  ah, 9
            int  0x21
            pop  dx
            ret

; ------------------------------------------------------------------ data

fn_lst:     db "DONGCAP.LST", 0
fn_bin:     db "DONGCAP.BIN", 0
fn_dia:     db "DONGCAP.DIA", 0
msg_banner: db "DONGCAP for DOS -- LPT base 0x$"
msg_crlf:   db 13, 10, "$"
msg_cal:    db "Calibrating$"
msg_dot:    db ".$"
msg_seed:   db 13, 10, "Seed 0x$"
msg_capt:   db "Capturing$"
msg_done:   db 13, 10, "Done -- DONGCAP.BIN written. Send it back.", 13, 10, "$"
msg_nocal:  db 13, 10, "No seed reproduces the known answers.", 13, 10
            db "Writing DONGCAP.DIA instead.", 13, 10, "$"
msg_diag:   db "DONGCAP.DIA written. Send it back.", 13, 10, "$"
msg_nolst:  db "DONGCAP.LST not found -- keep it next to this program.", 13, 10, "$"
msg_badlst: db "DONGCAP.LST is not a capture list.", 13, 10, "$"
msg_noout:  db "Cannot create DONGCAP.BIN.", 13, 10, "$"
msg_key:    db "Press a key to close.", 13, 10, "$"
chbuf:      db "X$"
base:       dw 0
hlst:       dw 0
hout:       dw 0
seed:       db 0
hdr:        db 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
ncal:       dd 0
nthis:      dd 0
left:       dd 0
done:       dd 0
nd:         dd 0
obuf:       db 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
"""


LABEL_RE = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*):\s*(.*)$')
WORD_RE  = re.compile(r'[A-Za-z_][A-Za-z0-9_]*')


def parse_data(op, rest, labels):
    """db/dw/dd, with strings and numbers -- keystone has no data directives."""
    out = bytearray()
    size = {'db': 1, 'dw': 2, 'dd': 4}[op]
    for item in re.findall(r"""\"[^\"]*\"|'[^']*'|[^,\s][^,]*""", rest):
        item = item.strip()
        if not item:
            continue
        if item[0] in "\"'":
            out += item[1:-1].encode('latin1')
        else:
            v = labels.get(item)
            if v is None:
                v = int(item, 0)
            out += v.to_bytes(size, 'little')
    return bytes(out)


def build():
    ks = Ks(KS_ARCH_X86, KS_MODE_16)
    lines = []
    for raw in SRC.splitlines():
        line = raw.split(';')[0].rstrip()
        if not line.strip():
            continue
        m = LABEL_RE.match(line.strip())
        if m:
            lines.append(('label', m.group(1)))
            if m.group(2).strip():
                lines.append(('code', m.group(2).strip()))
        else:
            lines.append(('code', line.strip()))

    consts = {'CALBUF': CALBUF, 'INBUF': INBUF, 'OUTBUF': OUTBUF,
              'POLY': POLY, 'CB': CB}
    labels = dict(consts)

    # Every name the source defines, so a forward reference can be given a placeholder
    # on the first pass instead of failing -- registers and mnemonics are left alone.
    names = set(consts) | {t for k, t in lines if k == 'label'}

    # Instruction lengths can move when a label's value changes an encoding, so the
    # layout is iterated to a fixed point rather than assumed after one pass.
    for _ in range(8):
        out    = bytearray()
        found  = dict(consts)
        addr   = ORG
        for kind, text in lines:
            if kind == 'label':
                found[text] = addr
                continue
            op = text.split()[0].lower()
            if op in ('db', 'dw', 'dd'):
                blob = parse_data(op, text[len(op):], labels)
            else:
                def sub(m):
                    w = m.group(0)
                    if w not in names:
                        return w
                    return hex(labels.get(w, 0x7FFF))
                enc, _ = ks.asm(bytes(WORD_RE.sub(sub, text), 'utf-8'), addr)
                blob = bytes(enc)
            out  += blob
            addr += len(blob)
        if found == labels:
            return bytes(out), labels
        labels = found
    raise SystemExit('layout did not settle')


def main():
    com, labels = build()
    out = sys.argv[1] if len(sys.argv) > 1 else 'DONGCAP.COM'
    open(out, 'wb').write(com)
    print('%s: %d bytes' % (out, len(com)))

    # Read it back the way the CPU will, as a check on the encodings.
    md = Cs(CS_ARCH_X86, CS_MODE_16)
    n = 0
    for _ in md.disasm(com, ORG):
        n += 1
    print('disassembles to %d instructions' % n)


if __name__ == '__main__':
    main()
