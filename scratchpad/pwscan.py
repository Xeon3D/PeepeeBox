"""Extract the HASP password pair from every generation's images.

Reads an executable straight out of each disk image and runs the anchored
extractor over it.  Prints one line per image.
"""
import importlib.util as iu
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hasppw3

_s = iu.spec_from_file_location("cat", r"C:\Users\xeon4\Documents\Claude\catalog-photoplay.py")
cat = iu.module_from_spec(_s)
_s.loader.exec_module(cat)

ROOTS = [r'F:\Photoplay', r'C:\Users\xeon4\Documents\Claude\PeepeeBox\PP2001',
         r'C:\Users\xeon4\Documents\Claude\PeepeeBox\PP2000', r'F:\PPIGO8 work']


def find_image(folder):
    for dirpath, _dirs, files in os.walk(folder):
        for f in files:
            if f.lower().endswith('.img'):
                return os.path.join(dirpath, f)
        break
    for dirpath, _dirs, files in os.walk(folder):
        for f in files:
            if f.lower().endswith('.img'):
                return os.path.join(dirpath, f)
    return None


def list_exes(fs):
    """every .EXE under \\EXE and \\MENU, biggest first"""
    out = []
    for d in ('/EXE', '/MENU', '/'):
        hit = fs.find(d)
        if not hit or not hit[2]:
            continue
        for name, attr, first, sz in fs._entries(hit[0]):
            if attr & 0x10 or name[8:11].strip().upper() != 'EXE':
                continue
            out.append((sz, d.rstrip('/') + '/' + name[:8].strip() + '.EXE'))
    out.sort(reverse=True)
    return [p for _sz, p in out]


for root in ROOTS:
    if not os.path.isdir(root):
        continue
    print('===', root)
    for folder in sorted(os.listdir(root)):
        p = os.path.join(root, folder)
        if not os.path.isdir(p):
            continue
        img = find_image(p)
        if not img:
            continue
        try:
            fs = cat.Fat(img)
            paths = list_exes(fs)
            best = None
            for path in paths[:14]:          # a few, in case the first does not link it
                d = fs.read(path)
                if not d:
                    continue
                pw, why = hasppw3.passwords(d)
                if pw:
                    best = (path, pw, why)
                    break
                if best is None:
                    best = (path, None, why)
            fs.close()
        except Exception as ex:
            print('  %-34s %s' % (folder, ex))
            continue
        if best is None:
            print('  %-34s no EXE found' % folder)
            continue
        path, pw, why = best
        print('  %-34s %-16s %-13s %s'
              % (folder, path, ('%04X / %04X' % pw) if pw else '--', why))
