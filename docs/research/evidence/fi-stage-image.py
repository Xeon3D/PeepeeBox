#!/usr/bin/env python3
"""Phase 1 - stage Fixed.img as the HardDisk.img PeepeeBox insists on.

Two container-level operations, neither of which touches a single byte inside the
filesystem:

  1. copy      Fixed.img -> HardDisk.img.  pp_apply_disk() in src/photoplay.c opens
               exactly PHOTOPLAY_DISK_IMAGE ("HardDisk.img") next to the executable and
               attaches nothing if it is absent, so the name is not optional.

  2. --pad     append zeros until the file is as large as its own filesystem says it is.
               The BPB declares 63 hidden + 4 120 601 total = 4 120 664 sectors; the file
               carries 3 980 592.  The 68.4 MB shortfall is entirely free space (no
               allocated cluster lies beyond EOF - verified in Phase 0), so this adds
               nothing but the slack DOS already believes it has.

               Target 4 120 704 sectors = 2 109 800 448 B, chosen so PeepeeBox's fixed
               63 spt x 16 heads divides exactly (4088 cylinders) and the BIOS's
               LBA-assist translation lands on 63 x 64 x 1022 - the geometry the boot
               record was written under.

Prints SHA256 of input and output.  Idempotent: re-running with the same flags produces
the same output file.
"""
import argparse
import hashlib
import os
import shutil
import sys

SECTOR      = 512
TARGET_SECS = 4120704          # 63 spt * 16 hpc * 4088 cyl == 63 * 64 * 1022
TARGET_SIZE = TARGET_SECS * SECTOR


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
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--pad", action="store_true",
                    help="zero-extend to the size the BPB declares")
    a = ap.parse_args()

    src_size = os.path.getsize(a.src)
    print("in   %s" % a.src)
    print("     %d bytes (%d sectors)" % (src_size, src_size // SECTOR))
    print("     sha256 %s" % sha256(a.src))

    if os.path.abspath(a.src) == os.path.abspath(a.dst):
        sys.exit("refusing to write over the source")

    shutil.copyfile(a.src, a.dst)

    if a.pad:
        cur = os.path.getsize(a.dst)
        if cur > TARGET_SIZE:
            sys.exit("source is already larger than the target size; nothing to pad")
        with open(a.dst, "r+b") as f:
            f.seek(TARGET_SIZE - 1)
            f.write(b"\0")
        print("     padded %d -> %d bytes (+%d)" % (cur, TARGET_SIZE, TARGET_SIZE - cur))

    dst_size = os.path.getsize(a.dst)
    print("out  %s" % a.dst)
    print("     %d bytes (%d sectors)" % (dst_size, dst_size // SECTOR))
    print("     sha256 %s" % sha256(a.dst))
    print("     PeepeeBox geometry: %d/%d/%d" %
          (dst_size // SECTOR // (63 * 16), 16, 63))


if __name__ == "__main__":
    main()
