"""Replay a captured 2001 wire trace through the Microwire decoder the device now
implements, and report what instructions the guest actually sent.

The trace is the guest's own traffic, captured before this decoder existed, so the
addresses it yields are a check on the pin map and the framing -- not on the device
agreeing with itself.  If bits 1/5/6 are not CS/SK/DI, this produces noise."""
import re
import sys

CS, SK, DI = 0x02, 0x20, 0x40
ABITS = 8

IDLE, OP, ADDR, READ, WRITE, DONE = range(6)

path = sys.argv[1]
ph, n, op, addr, sr = IDLE, 0, 0, 0, 0
last_sk = 0
frames = []          # (opcode, address, databits_seen)
data_reads = 0

line_re = re.compile(r'^PPRAW\s+\d+.*?\s(write_data|read_status|write_ctrl|read_ctrl|read_data)\s+([0-9A-Fa-f]{2})\s*$')

for line in open(path, encoding='utf-8', errors='replace'):
    m = line_re.match(line.strip())
    if not m:
        continue
    what, val = m.group(1), int(m.group(2), 16)

    if what == 'write_data':
        sel, clk, dat = (val & CS) != 0, (val & SK) != 0, (val & DI) != 0
        if not sel:
            ph, n = IDLE, 0
        elif clk and not last_sk:
            if ph == IDLE:
                if dat:
                    ph, n, op = OP, 0, 0
            elif ph == OP:
                op = (op << 1) | dat
                n += 1
                if n == 2:
                    ph, n, addr = ADDR, 0, 0
            elif ph == ADDR:
                addr = ((addr << 1) | dat) & 0xFF
                n += 1
                if n == ABITS:
                    n = 0
                    if op == 2:
                        ph = READ
                        frames.append([op, addr, 0])
                    elif op == 1:
                        ph, sr = WRITE, 0
                        frames.append([op, addr, 0])
                    else:
                        frames.append([op, addr, 0])
                        ph = DONE
            elif ph == READ:
                n += 1
                frames[-1][2] = n
            elif ph == WRITE:
                sr = ((sr << 1) | dat) & 0xFFFF
                n += 1
                frames[-1][2] = n
                if n == 16:
                    ph = DONE
        last_sk = clk

    elif what == 'read_status':
        if ph == READ:
            data_reads += 1
        else:
            ph, n = IDLE, 0

names = {0: 'EWEN/EWDS', 1: 'WRITE', 2: 'READ', 3: 'ERASE'}
print('frames decoded:', len(frames))
for op, a, bits in frames[:6]:
    print('  %-9s addr %3d  bits %d' % (names[op], a, bits))
if len(frames) > 12:
    print('   ...')
for op, a, bits in frames[-6:]:
    print('  %-9s addr %3d  bits %d' % (names[op], a, bits))

reads = [f for f in frames if f[0] == 2]
print('READ frames:', len(reads))
if reads:
    print('addresses  :', ','.join(str(f[1]) for f in reads[:8]), '...',
          ','.join(str(f[1]) for f in reads[-4:]))
    print('all 16 bits:', all(f[2] == 16 for f in reads))
    print('contiguous :', [f[1] for f in reads] == list(range(reads[0][1], reads[0][1] + len(reads))))
print('status reads inside a read frame:', data_reads)
