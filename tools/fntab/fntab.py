r"""Read the fun.net .TAB tables.

Every table under `\FN_SYS\DATABASE\` is the same shape: a flat array of
fixed-size records, no header, no index, no free list.

    record = 1 byte in-use flag  +  N bytes of payload

`N` is not in the file -- it is a constant compiled into whichever binary opens
the table:

    push  dword N          ; payload size in the low word
    push  word  bufsize    ; 4096, 8192 or 32768
    push  word  path       ; "\fn_sys\database\user\player.tab"
    push  handle
    call  <db open>

so the sizes below were read out of FN_SYS.EXE, FN_MAIL.EXE, FN_MST.EXE,
FN_SMS.EXE, FN_NEWS.EXE, FN_PORT.EXE and FN_FLIRT.EXE, and every binary that
opens a shared table agrees on its size.  A file is a whole number of 4/8/32 KB
chunks, so the last partial record is slack and the flag byte is what says which
slots are live.

Strings are fixed-width, NUL-terminated, cp437, and zero-padded to the end of
their field.  Integers are little-endian.

    python fntab.py sizes
    python fntab.py derive  FN_MAIL.EXE
    python fntab.py info    PLAYER.TAB
    python fntab.py map     PLAYER.TAB
    python fntab.py read    PLAYER.TAB --limit 5
    python fntab.py read    PLAYER.TAB --limit 5 --mask     # structure only

docs/research/35-fn-tables.md has the derivation.  No third-party modules;
Python 3.8+.
"""
import argparse
import os
import re as _re
import struct
import sys

# ---------------------------------------------------------------- record sizes
#
# payload size as passed to the open call; the record is one byte longer.
PAYLOAD = {
    'CVDATA':    41,  'CVSLAVE':  111,  'FNUPDATE':  80,  'SETTINGS': 127,
    'TECHDATA': 120,  'CHGPLY':   365,  'FMCARDS':    36,  'FMFREE':    20,
    'FMHOME':    19,  'FMINDEX':   19,  'FMMSGIN':  2270,  'FMMSGOUT':2158,
    'FMSYSBOX': 109,  'LOGIN':    150,  'MONITOR':    25,  'NPDOC':      8,
    'NPITEM':   534,  'NPMAIN':    55,  'PLAYER':    364,  'HIS_NODE':  260,
    'HIS_OPER': 248,  'HIS_T100': 132,  'HISNAME':   115,  'HISPARNT':   12,
    'HISPRIZE': 117,  'HISUPDW':   16,  'JOINPUB':    19,  'MSTCRED':     8,
    'MSTMAIN':   21,  'MSTNAME':  109,  'MSTSCORE':   33,  'POTCOUNT':   12,
    'POTDEF':    48,  'TMPSCORE':  33,  'SMSAVAIL':  519,  'SMSDATA':   517,
    'SMSGROUP':  75,  'SMSMNEW':    8,  'SMSMEDIA':   77,  'SMSORDER':   62,
    'SMSPROV':  106,  'SMSREG':    63,  'SMSSERV':   586,  'FREEGAME':   33,
}

# ------------------------------------------------------------------- schemas
#
# ('name', offset, length, kind).  kind: 's' NUL-terminated cp437 string,
# 'u8'/'u16' little-endian integers, 'date' the year/day/month triple.
#
# PLAYER is the one that is fully solved: its field sizes sum to exactly 365,
# and the order matches the registration form's own INP_* labels in FN_MST.EXE
# (firstname, lastname, street, zip, city, birthdate, gender, telefon, email).
SCHEMA = {
    'PLAYER': [
        ('member_id',   1,  14, 's'),
        ('language',   15,   4, 's'),
        ('first_name', 19,  61, 's'),
        ('last_name',  80,  61, 's'),
        ('street',    141,  61, 's'),
        ('postcode',  202,  31, 's'),
        ('city',      233,  61, 's'),
        ('country',   294,   3, 's'),
        ('birth',     297,   4, 'date'),
        ('telephone', 301,  22, 's'),
        ('email',     323,  42, 's'),
    ],
    # Partially solved -- the named fields are confident, the rest are not.
    'LOGIN': [
        ('nickname',    1,  14, 's'),
        ('language',   15,   4, 's'),
        ('unknown_19', 19,   4, 'hex'),
        ('text_23',    23,  47, 's'),
        ('code_70',    70,   4, 's'),
        ('text_91',    91,   4, 's'),
        ('unknown_95', 95,  20, 'hex'),
        ('text_115',  115,  31, 's'),
        ('unknown_146', 146, 5, 'hex'),
    ],
    'TECHDATA': [
        ('key',   1,   9, 's'),
        ('value', 10, 111, 's'),
    ],
    'SETTINGS': [
        ('key',    1,  26, 's'),
        ('value', 27, 101, 's'),
    ],
}

KEEP = set(' !"#$%&\'()*+,-./:;<=>?@[\\]^_`{|}~')


def table_key(path):
    return os.path.splitext(os.path.basename(path))[0].upper()


def record_size(path, override=None):
    if override:
        return override
    k = table_key(path)
    if k in PAYLOAD:
        return PAYLOAD[k] + 1
    return None


def records(d, R):
    for k in range(len(d) // R):
        rec = d[k * R:(k + 1) * R]
        if rec and rec[0]:
            yield k, rec


def cstr(rec, off, ln):
    return rec[off:off + ln].split(b'\x00')[0].decode('cp437', 'replace')


def mask_text(s):
    return ''.join('x' if c.isalnum() else (c if c in KEEP else '?') for c in s)


def cmd_sizes(_args):
    print('%-12s %8s %8s' % ('table', 'payload', 'record'))
    for k in sorted(PAYLOAD):
        print('%-12s %8d %8d' % (k, PAYLOAD[k], PAYLOAD[k] + 1))
    print('\n%d tables' % len(PAYLOAD))
    return 0


# ------------------------------------------------------- sizes from a binary
#
# The table above was produced by this.  It is here rather than in a scratch
# script because an image from another generation may use different sizes, and
# re-deriving them should not mean rediscovering how.

def _dgroup(img, probes):
    """Where DGROUP sits: the base that explains the most string references.

    A 16-bit binary pushes a string as its DGROUP offset, so for a string at
    image offset S referenced by immediate I the base is S - I.  Whichever base
    explains the most of the probe strings is the real one."""
    import collections
    imms = collections.Counter()
    for m in _re.finditer(rb'\x68(..)', img, _re.S):
        imms[struct.unpack('<H', m.group(1))[0]] += 1
    for m in _re.finditer(rb'\xb8(..)', img, _re.S):
        imms[struct.unpack('<H', m.group(1))[0]] += 1
    votes = collections.Counter()
    for p in probes:
        m = _re.search(_re.escape(p) + rb'\x00', img)
        if not m:
            continue
        for imm in imms:
            if 0 <= m.start() - imm <= m.start():
                votes[m.start() - imm] += 1
    return votes.most_common(1)[0][0] if votes else None


def cmd_derive(args):
    d = open(args.binary, 'rb').read()
    if d[:2] != b'MZ':
        print('fntab: %s is not an MZ executable' % args.binary, file=sys.stderr)
        return 1
    img = d[struct.unpack_from('<H', d, 0x08)[0] * 16:]

    probes = sorted({m.group() for m in _re.finditer(rb'[\x20-\x7e]{6,}\.tab', img)})[:8]
    if not probes:
        print('fntab: no .tab path strings in %s' % args.binary, file=sys.stderr)
        return 1
    dg = _dgroup(img, probes)
    if dg is None:
        print('fntab: could not locate DGROUP', file=sys.stderr)
        return 1

    strs = {}
    for m in _re.finditer(rb'[\x20-\x7e]{2,}\x00', img):
        strs[m.start() - dg] = m.group()[:-1].decode('cp437', 'replace')

    print('%s  DGROUP %#x' % (os.path.basename(args.binary), dg))
    print('%-14s %8s %6s %8s   %s' % ('table', 'payload', 'flags', 'cache', 'record'))
    seen = set()
    for off, s in sorted(strs.items()):
        if not s.lower().endswith('.tab'):
            continue
        for m in _re.finditer(_re.escape(b'\x68' + struct.pack('<H', off & 0xFFFF)), img):
            i = m.start()
            payload = cache = None
            for blen in (3, 2):                      # the cache push
                j = i - blen
                if j < 0:
                    continue
                if blen == 3 and img[j] == 0x68:
                    c = struct.unpack_from('<H', img, j + 1)[0]
                elif blen == 2 and img[j] == 0x6a:
                    c = img[j + 1]
                else:
                    continue
                if j >= 6 and img[j - 6:j - 4] == b'\x66\x68':      # the size push
                    payload = struct.unpack_from('<I', img, j - 4)[0]
                elif j >= 3 and img[j - 3:j - 1] == b'\x66\x6a':
                    payload = img[j - 1]
                else:
                    continue
                cache = c
                break
            if payload is None:
                continue
            name = s.replace('/', '\\').split('\\')[-1]
            if name in seen:
                continue
            seen.add(name)
            print('%-14s %8d %6d %8d   %d'
                  % (name, payload & 0xFFFF, payload >> 16, cache, (payload & 0xFFFF) + 1))
    if not seen:
        print('  (no open calls matched)')
    return 0


def cmd_info(args):
    d = open(args.file, 'rb').read()
    R = record_size(args.file, args.record_size)
    if not R:
        print('fntab: no known record size for %s; pass --record-size'
              % table_key(args.file), file=sys.stderr)
        return 1
    slots = len(d) // R
    used = sum(1 for _ in records(d, R))
    print('%s' % os.path.basename(args.file))
    print('  file        %d bytes' % len(d))
    print('  record      %d bytes (1 flag + %d payload)' % (R, R - 1))
    print('  slots       %d' % slots)
    print('  in use      %d' % used)
    print('  slack       %d bytes past the last whole record' % (len(d) - slots * R))
    print('  schema      %s' % ('known' if table_key(args.file) in SCHEMA else 'not worked out'))
    return 0


def cmd_map(args):
    d = open(args.file, 'rb').read()
    R = record_size(args.file, args.record_size)
    if not R:
        print('fntab: no known record size; pass --record-size', file=sys.stderr)
        return 1
    recs = [r for _, r in records(d, R)]
    if not recs:
        print('no records in use')
        return 0
    kinds = []
    for i in range(R):
        col = [r[i] for r in recs]
        if all(b == 0 for b in col):
            kinds.append('.')
        elif all(b == 0 or 32 <= b < 127 or b >= 160 for b in col):
            kinds.append('T')
        else:
            kinds.append('B')
    runs = []
    for i, k in enumerate(kinds):
        if runs and runs[-1][0] == k and runs[-1][2] == i - 1:
            runs[-1][2] = i
        else:
            runs.append([k, i, i])
    print('%s  R=%d  %d records in use' % (os.path.basename(args.file), R, len(recs)))
    print('  offset  len  kind    detail')
    for k, a, b in runs:
        ln = b - a + 1
        if k == '.':
            print('  %5d  %4d  zero' % (a, ln))
        elif k == 'T':
            longest = max(len(r[a:b + 1].split(b'\x00')[0]) for r in recs)
            filled = sum(1 for r in recs if r[a:b + 1].split(b'\x00')[0])
            print('  %5d  %4d  text    longest %d, %d/%d filled'
                  % (a, ln, longest, filled, len(recs)))
        else:
            print('  %5d  %4d  binary  %d distinct'
                  % (a, ln, len({r[a:b + 1] for r in recs})))
    return 0


def cmd_read(args):
    d = open(args.file, 'rb').read()
    R = record_size(args.file, args.record_size)
    if not R:
        print('fntab: no known record size; pass --record-size', file=sys.stderr)
        return 1
    schema = SCHEMA.get(table_key(args.file))
    shown = 0
    for k, rec in records(d, R):
        if shown >= args.limit:
            break
        shown += 1
        print('--- slot %d' % k)
        if not schema:
            for i in range(0, R, 32):
                row = rec[i:i + 32]
                txt = ''.join(chr(b) if 32 <= b < 127 else '.' for b in row)
                if args.mask:
                    txt = mask_text(txt)
                print('   %4d  %-64s |%s|' % (i, row.hex(), txt))
            continue
        for name, off, ln, kind in schema:
            if kind == 's':
                v = cstr(rec, off, ln)
                v = mask_text(v) if args.mask else v
            elif kind == 'date':
                y, dd, mm = (struct.unpack_from('<H', rec, off)[0],
                             rec[off + 2], rec[off + 3])
                v = '%04d-%02d-%02d' % (y, mm, dd)
                if args.mask:
                    v = mask_text(v)
            elif kind == 'u16':
                v = struct.unpack_from('<H', rec, off)[0]
            else:
                v = rec[off:off + ln].hex()
            print('   %-12s %s' % (name, v))
    return 0


def main():
    ap = argparse.ArgumentParser(description='read the fun.net .TAB tables')
    ap.add_argument('--record-size', type=int, help='override the record size')
    sub = ap.add_subparsers(dest='cmd')

    sub.add_parser('sizes', help='the record size of every known table')
    p = sub.add_parser('derive', help='read the record sizes out of a fun.net binary')
    p.add_argument('binary', help='e.g. FN_MAIL.EXE, extracted from an image')
    for name, help_ in (('info', 'record size, slot count, how many are live'),
                        ('map', 'what kind of thing lives at each byte offset'),
                        ('read', 'decode records')):
        p = sub.add_parser(name, help=help_)
        p.add_argument('file')
        if name == 'read':
            p.add_argument('--limit', type=int, default=10)
            p.add_argument('--mask', action='store_true',
                           help='replace letters and digits with x -- shows the '
                                'layout without the contents')

    args = ap.parse_args()
    fn = {'sizes': cmd_sizes, 'info': cmd_info, 'map': cmd_map,
          'read': cmd_read, 'derive': cmd_derive}
    if args.cmd not in fn:
        ap.print_help()
        return 1
    return fn[args.cmd](args)


if __name__ == '__main__':
    sys.exit(main())
