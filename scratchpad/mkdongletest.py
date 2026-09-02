"""Build DONGTEST.COM -- a DOS probe for a real Photo Play / I.G.O. parallel dongle.

There is no DOS toolchain on this machine, so the .COM is assembled here directly.  The
program is small and the encodings are checked by disassembling the result.

What it does, using the wire read out of I.G.O. 2's FINDIT.EXE (docs/research/29):

  a command byte b   ->  write (b & 0xFE) | 0x80, then b | 0x81, then (b & 0xFE) | 0x80
                         i.e. DATA bit 0 pulsed as the clock, bit 7 always set   (0x32DE2)
  a 5-bit query q    ->  payload = ((q << 1) & 0x0E) | ((q << 2) & 0x60) | 0x80
                         write payload, payload | 0x10, payload           bit 4 is the clock
                         then read STATUS and take bit 5                  (0x32F59, 0x32D17)
  a round opens with ->  command(seed), command(0x4E), raw write 0x84     (0x32FC2)

The seed constant did not resolve from the binary (two DGROUP candidates gave 0x61 and
0x8B), so the probe simply sweeps **all 256** possible seeds rather than betting on one.

Output, written to DONGTEST.BIN next to the program:

  offset 0x000, 256 bytes   phase 1: DATA = v for v in 0..255, STATUS read back after each.
                            Shows whether the part answers combinationally at all, and what
                            the idle STATUS looks like.
  offset 0x100, 2048 bytes  phase 2: for each seed 0..255, open a round with it and then
                            ask query 0 sixty-four times, packing the 64 answer bits into
                            8 bytes.  A part that is really being driven should give a
                            structured, non-constant reply for the right seed.

Run it on the cabinet laptop with the dongle on LPT1:

    DONGTEST.COM            (uses the LPT1 base from the BIOS data area, else 0x378)

then send DONGTEST.BIN back.
"""
import struct
import sys

OUT = 'DONGTEST.COM'
ORG = 0x100

code = []          # list of ints, or ('rel8', label), ('rel16', label), ('abs16', label)
labels = {}


def emit(*bs):
    for b in bs:
        code.append(b)


def label(name):
    labels[name] = len(code)


def rel8(name):
    code.append(('rel8', name))


def rel16(name):
    code.append(('rel16', name))
    code.append(('rel16hi', name))


def abs16(name):
    code.append(('abs16', name))
    code.append(('abs16hi', name))


def imm16(v):
    emit(v & 0xFF, (v >> 8) & 0xFF)


# ---------------------------------------------------------------- program
# --- entry ---------------------------------------------------------------
emit(0xB8); imm16(0x0040)              # mov ax,0x40
emit(0x8E, 0xC0)                       # mov es,ax
emit(0x26, 0x8B, 0x16, 0x08, 0x00)     # mov dx,es:[8]      LPT1 base
emit(0x09, 0xD2)                       # or dx,dx
emit(0x75, 0x03)                       # jnz +3
emit(0xBA); imm16(0x0378)              # mov dx,0x378
emit(0x89, 0x16); abs16('base')        # mov [base],dx

# --- phase 1: raw DATA -> STATUS -----------------------------------------
emit(0x31, 0xF6)                       # xor si,si
label('p1')
emit(0x8B, 0x16); abs16('base')        # mov dx,[base]
emit(0x89, 0xF0)                       # mov ax,si
emit(0xEE)                             # out dx,al
emit(0xE8); rel16('delay')             # call delay
emit(0x8B, 0x16); abs16('base')        # mov dx,[base]
emit(0x42)                             # inc dx
emit(0xEC)                             # in al,dx
emit(0x89, 0xF3)                       # mov bx,si
emit(0x88, 0x87); abs16('buf')         # mov [bx+buf],al
emit(0x46)                             # inc si
emit(0x81, 0xFE); imm16(0x0100)        # cmp si,256
emit(0x72); rel8('p1')                 # jb p1

# --- phase 2: every seed, then 64 answers to query 0 ----------------------
emit(0xBF); imm16(0x0100)              # mov di,0x100        write pointer
emit(0xC6, 0x06); abs16('seed'); emit(0x00)   # mov byte [seed],0
label('p2seed')
emit(0xA0); abs16('seed')              # mov al,[seed]
emit(0xE8); rel16('cmdbyte')           # call cmdbyte
emit(0xB0, 0x4E)                       # mov al,0x4E
emit(0xE8); rel16('cmdbyte')           # call cmdbyte
emit(0xB0, 0x84)                       # mov al,0x84
emit(0xE8); rel16('rawout')            # call rawout
emit(0xBE); imm16(0x0008)              # mov si,8            eight bytes of bits
label('p2byte')
emit(0x30, 0xFF)                       # xor bh,bh
emit(0xB3, 0x08)                       # mov bl,8
label('p2bit')
emit(0xB0, 0x00)                       # mov al,0            query value 0
emit(0xE8); rel16('query')             # call query -> al in {0,1}
emit(0xD0, 0xE7)                       # shl bh,1
emit(0x08, 0xC7)                       # or bh,al
emit(0xFE, 0xCB)                       # dec bl
emit(0x75); rel8('p2bit')              # jnz p2bit
emit(0x88, 0xF8)                       # mov al,bh
emit(0x89, 0xFB)                       # mov bx,di
emit(0x88, 0x87); abs16('buf')         # mov [bx+buf],al
emit(0x47)                             # inc di
emit(0x4E)                             # dec si
emit(0x75); rel8('p2byte')             # jnz p2byte
emit(0xFE, 0x06); abs16('seed')        # inc byte [seed]
emit(0x80, 0x3E); abs16('seed'); emit(0x00)   # cmp byte [seed],0
emit(0x75); rel8('p2seed')             # jnz p2seed          wraps after 256

# --- write the file ------------------------------------------------------
emit(0xB4, 0x3C)                       # mov ah,0x3C         create
emit(0x31, 0xC9)                       # xor cx,cx
emit(0xBA); abs16('fname')             # mov dx,fname
emit(0xCD, 0x21)                       # int 21h
emit(0x72); rel8('done')               # jc done
emit(0x89, 0xC3)                       # mov bx,ax           handle
emit(0xB4, 0x40)                       # mov ah,0x40         write
emit(0xB9); imm16(0x0900)              # mov cx,0x900        256 + 2048
emit(0xBA); abs16('buf')               # mov dx,buf
emit(0xCD, 0x21)                       # int 21h
emit(0xB4, 0x3E)                       # mov ah,0x3E         close
emit(0xCD, 0x21)                       # int 21h
label('done')
emit(0xB4, 0x09)                       # mov ah,9
emit(0xBA); abs16('msg')               # mov dx,msg
emit(0xCD, 0x21)                       # int 21h
emit(0xCD, 0x20)                       # int 20h

# --- rawout: AL is the finished DATA byte --------------------------------
label('rawout')
emit(0x50)                             # push ax
emit(0x52)                             # push dx
emit(0x8B, 0x16); abs16('base')        # mov dx,[base]
emit(0xEE)                             # out dx,al
emit(0xE8); rel16('delay')             # call delay
emit(0x5A)                             # pop dx
emit(0x58)                             # pop ax
emit(0xC3)                             # ret

# --- cmdbyte: AL = b, clocked on DATA bit 0 ------------------------------
label('cmdbyte')
emit(0x50)                             # push ax
emit(0x24, 0xFE)                       # and al,0xFE
emit(0x0C, 0x80)                       # or  al,0x80
emit(0xE8); rel16('rawout')
emit(0x0C, 0x01)                       # or  al,0x01
emit(0xE8); rel16('rawout')
emit(0x24, 0xFE)                       # and al,0xFE
emit(0xE8); rel16('rawout')
emit(0x58)                             # pop ax
emit(0xC3)                             # ret

# --- query: AL = q (0..31), clocked on DATA bit 4, answer = STATUS bit 5 --
label('query')
emit(0x53)                             # push bx
emit(0x52)                             # push dx
emit(0x88, 0xC3)                       # mov bl,al
emit(0xD0, 0xE0)                       # shl al,1
emit(0x24, 0x0E)                       # and al,0x0E         bits 0,1,2 -> 1,2,3
emit(0x88, 0xC7)                       # mov bh,al
emit(0x88, 0xD8)                       # mov al,bl
emit(0xD0, 0xE0)                       # shl al,1
emit(0xD0, 0xE0)                       # shl al,1
emit(0x24, 0x60)                       # and al,0x60         bits 3,4 -> 5,6
emit(0x08, 0xF8)                       # or  al,bh
emit(0x0C, 0x80)                       # or  al,0x80         bit 7 always set
emit(0x88, 0xC7)                       # mov bh,al           keep the payload
emit(0xE8); rel16('rawout')            # bit 4 low
emit(0x88, 0xF8)                       # mov al,bh
emit(0x0C, 0x10)                       # or  al,0x10         bit 4 high
emit(0xE8); rel16('rawout')
emit(0x88, 0xF8)                       # mov al,bh
emit(0xE8); rel16('rawout')            # bit 4 low again
emit(0x8B, 0x16); abs16('base')        # mov dx,[base]
emit(0x42)                             # inc dx              STATUS
emit(0xEC)                             # in al,dx
emit(0xD0, 0xE8)                       # shr al,1
emit(0xD0, 0xE8)                       # shr al,1
emit(0xD0, 0xE8)                       # shr al,1
emit(0xD0, 0xE8)                       # shr al,1
emit(0xD0, 0xE8)                       # shr al,1            bit 5 -> bit 0
emit(0x24, 0x01)                       # and al,1
emit(0x5A)                             # pop dx
emit(0x5B)                             # pop bx
emit(0xC3)                             # ret

# --- delay: a short settle ------------------------------------------------
label('delay')
emit(0x51)                             # push cx
emit(0xB9); imm16(0x0060)              # mov cx,0x60
label('dly')
emit(0xE2); rel8('dly')                # loop dly
emit(0x59)                             # pop cx
emit(0xC3)                             # ret

# --- data -----------------------------------------------------------------
label('fname')
for ch in b'DONGTEST.BIN\0':
    emit(ch)
label('msg')
for ch in b'Done -- DONGTEST.BIN written.$':
    emit(ch)
label('base')
emit(0, 0)
label('seed')
emit(0)
label('buf')
# the buffer is BSS; a .COM gets the whole segment, so nothing is emitted for it

# ---------------------------------------------------------------- link
out = bytearray()
for i, item in enumerate(code):
    out.append(0)
for i, item in enumerate(code):
    if isinstance(item, int):
        out[i] = item
for i, item in enumerate(code):
    if isinstance(item, tuple):
        kind, name = item
        tgt = labels[name]
        if kind == 'rel8':
            d = tgt - (i + 1)
            if not -128 <= d <= 127:
                raise SystemExit('rel8 out of range to %s (%d)' % (name, d))
            out[i] = d & 0xFF
        elif kind == 'rel16':
            d = tgt - (i + 2)
            out[i] = d & 0xFF
        elif kind == 'rel16hi':
            d = labels[name] - (i + 1)
            out[i] = (d >> 8) & 0xFF
        elif kind == 'abs16':
            a = ORG + tgt
            out[i] = a & 0xFF
        elif kind == 'abs16hi':
            a = ORG + labels[name]
            out[i] = (a >> 8) & 0xFF

open(OUT, 'wb').write(bytes(out))
print('%s: %d bytes' % (OUT, len(out)))
print('labels: %s' % ', '.join('%s=%04X' % (k, ORG + v) for k, v in sorted(labels.items(), key=lambda x: x[1])))
