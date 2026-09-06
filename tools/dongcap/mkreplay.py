"""Extract every port access up to the first picture round, in order, as a replay script.

op 0 = write DATA val   op 1 = read STATUS   op 2 = read CONTROL
"""
import re, struct
line_re = re.compile(r'PPRAW\s+(\d+)\s+\S+\s+\S+\s+\S+\s+(\S+)\s+(\w+)\s+([0-9A-Fa-f]{2})')
MARK = 97602
ops = []
for ln in open('ppbox.log', encoding='latin-1'):
    m = line_re.search(ln)
    if not m:
        continue
    n = int(m.group(1))
    if n > MARK:
        break
    kind, val = m.group(3), int(m.group(4), 16)
    if kind == 'write_data':
        ops.append((0, val))
    elif kind == 'read_status':
        ops.append((1, 0))
    elif kind == 'read_ctrl':
        ops.append((2, 0))
    elif kind == 'write_ctrl':
        ops.append((3, val))
with open('replay.bin', 'wb') as f:
    f.write(struct.pack('<I', len(ops)))
    for op, val in ops:
        f.write(bytes([op, val]))
import collections
print('accesses captured before the mark: %d' % len(ops))
print('by kind: %s' % dict(collections.Counter(o for o, _ in ops)))
print('  0=write data  1=read status  2=read ctrl  3=write ctrl')
