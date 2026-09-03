#!/usr/bin/env python3
"""Phase 4 experiment - blank the ELODEV line in a working copy's AUTOEXEC.BAT.

DIAGNOSTIC ONLY.  This changes bytes inside the filesystem, which the deliverable must
never do; it exists to answer one question and then be reverted by re-running
analysis/p1/sh/stage-image.py.  It refuses to touch anything called Fixed.img.

AUTOEXEC.BAT line 8 is

    C:\\TOUCH\\ELO\\ELODEV 2310,3,9600,3 -C7,4074,4062,199,1,255

which loads Elo's DOS TSR on a hook FSYSTEM.EXE then probes for (CS:0x16FD calls the Elo
detector before it ever looks for a MicroTouch, and if the detector answers, CS:0x177E
forces the port variable to COM1).  Replacing the line with spaces -- same length, so no
FAT or directory entry changes -- makes the Elo branch unreachable and forces the
MicroTouch path.

Prints SHA256 before and after, and the byte offset it touched.
"""
import argparse
import hashlib
import os
import sys

NEEDLE = b"C:\\TOUCH\\ELO\\ELODEV"


def sha256(path, chunk=1 << 22):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                break
            h.update(b)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("image", help="the WORKING COPY to edit (never Fixed.img)")
    ap.add_argument("--yes", action="store_true", help="actually write")
    a = ap.parse_args()

    if os.path.basename(a.image).lower().startswith("fixed"):
        sys.exit("refusing to modify the pristine image")

    print("in   %s" % a.image)
    print("     sha256 %s" % sha256(a.image))

    with open(a.image, "r+b") as f:
        # AUTOEXEC.BAT is one 32 KB cluster at LBA 984; read a window around it.
        base = 984 * 512
        f.seek(base)
        buf = f.read(4096)
        i = buf.find(NEEDLE)
        if i < 0:
            sys.exit("ELODEV line not found at the expected cluster")
        end = buf.find(b"\r", i)
        if end < 0:
            sys.exit("no line ending after the ELODEV line")
        n = end - i
        print("     found at byte %d, line is %d chars:" % (base + i, n))
        print("       %r" % buf[i:end].decode("latin1"))
        if not a.yes:
            print("     (dry run -- pass --yes to write)")
            return
        f.seek(base + i)
        f.write(b" " * n)

    print("out  %s" % a.image)
    print("     sha256 %s" % sha256(a.image))
    print("     ELODEV line blanked; revert with analysis/p1/sh/stage-image.py")


if __name__ == "__main__":
    main()
