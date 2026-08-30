"""Run the record the device now stores through the guest's own parser, as
disassembled from FINDIT.EXE, and compare the struct that falls out with the one
KEYN.COM hands the patched game.  KEYN is an independent artifact: if the two
structs agree, the text record is right.

  0x1F3CF  read 56 words, then unpack high byte first until a zero WORD (0x1F4D2)
  0x1F626  copy bytes [start..end], skip leading spaces, copy up to strlen
  0x3C0B   atol
"""
import struct

BANNER = 'Version 2001 (IT)'   # KEYN's release, so the two are comparable
KEY = 0x7477
START = 8
RECORD = 112
TEXT = 83

FIELDS = [(30, 6, 907), (36, 6, 98765), (42, 6, 120672), (48, 6, 170898),
          (54, 6, 75902), (60, 6, 2205), (66, 6, 160678), (72, 11, 4259233598)]

# ---- what hd_load() stores -------------------------------------------------
rec = bytearray(b'\x00' * RECORD)
rec[0:TEXT] = b' ' * TEXT
b = BANNER.encode()[:29]
rec[0:len(b)] = b
rec[len(b)] = 0
for at, cols, val in FIELDS:
    t = str(val).encode()
    rec[at + cols - len(t):at + cols] = t

mem = {}
for i in range(256):
    j = i - START
    plain = 0
    if 0 <= j and (j * 2 + 1) < RECORD:
        plain = (rec[j * 2] << 8) | rec[j * 2 + 1]
    mem[i] = plain ^ ((i - START) & 0xFFFF) ^ KEY ^ (0xFF00 if i < START else 0)

# ---- what the guest does ---------------------------------------------------
words = [mem[START + n] ^ (n & 0xFFFF) ^ KEY for n in range(56)]   # 0x37F6F

buf = bytearray()                                                  # 0x1F49D
for w in words:
    if w == 0:
        break
    buf += bytes([w >> 8, w & 0xFF])

def field(start, end):                                             # 0x1F626
    part = bytes(buf[start:end + 1])
    part = part.split(b'\x00')[0]        # the copy runs to strlen
    return part.lstrip(b' ')

got_banner = field(0, 0x1D).decode('latin1')
got = [int(field(at, at + cols - 1) or b'0') for at, cols, _ in FIELDS]

# ---- KEYN's struct, read straight out of the file --------------------------
keyn = open('KEYN.COM', 'rb').read()[2:2 + 62]
want_banner = keyn[:30].split(b'\x00')[0].decode('latin1')
want = list(struct.unpack('<8I', keyn[30:62]))

print('unpacked bytes  :', len(buf), '(the loop must reach 83)')
print('banner  got/want:', repr(got_banner), '/', repr(want_banner),
      'OK' if got_banner == want_banner else 'MISMATCH')
for n, (g, w) in enumerate(zip(got, want)):
    print('  field %d  %10d  %10d  %s' % (n, g, w, 'ok' if g == w else 'MISMATCH'))
print('RESULT:', 'record matches KEYN' if (got == want and got_banner == want_banner)
      else 'RECORD IS WRONG')
