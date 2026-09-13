"""survey.py -- what fun.net client each disk image carries.

    python survey.py F:\HDDImages

Folders starting with an underscore are skipped.  Read-only.
"""
import glob
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fat16  # noqa: E402

FILES = ("FN_SYS/FN_SYS.EXE", "FN_SYS/DFU/CLIENT.EXE", "FN_SYS/DFU/TRANSMIT.BAT", "FN_SYS/DFU/PPP.EXE", "MENU/MENU.EXE")


def survey(root):
    for img in sorted(glob.glob(os.path.join(root, "**", "*.img"), recursive=True)):
        rel = os.path.relpath(img, root)
        if any(p.startswith("_") for p in rel.split(os.sep)[:-1]):
            continue
        try:
            fs = fat16.Fat16(img)
        except Exception as e:
            yield rel, None, str(e)
            continue
        info = {}
        for name in FILES:
            try:
                d = fs.read(name)
                info[name] = (len(d), hashlib.sha256(d).hexdigest())
            except Exception:
                info[name] = None
        try:
            info["dfu"] = sorted(n for n, isdir, s in fs.listdir("FN_SYS/DFU") if not isdir)
        except Exception:
            info["dfu"] = None
        fs.f.close()
        yield rel, info, None


if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else r"F:\HDDImages"
    groups = {}
    for rel, info, err in survey(root):
        tag = os.path.dirname(rel)
        if err:
            print("%-28s ERROR %s" % (tag, err))
            continue
        cells = []
        for name in FILES[:3]:
            v = info[name]
            cells.append("%d/%s" % (v[0], v[1][:8]) if v else "-")
        print("%-28s FN_SYS=%-18s CLIENT=%-18s TRANSMIT=%-14s dfu=%s" % (
            tag, cells[0], cells[1], cells[2], "none" if info["dfu"] is None else len(info["dfu"])))
        v = info["FN_SYS/FN_SYS.EXE"]
        if v:
            groups.setdefault(v, []).append(tag)
    print("\ndistinct FN_SYS.EXE builds:")
    for (size, h), tags in sorted(groups.items()):
        print("  %7d  %s  %s" % (size, h, ", ".join(tags)))
