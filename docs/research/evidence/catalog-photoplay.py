#!/usr/bin/env python3
r"""Catalogue a collection of Photo Play / I.G.O. disk images.

Point it at a folder that contains one subfolder per image:

    python catalog-photoplay.py "D:\PhotoPlay"

For each subfolder it opens the disk image, works out what the image actually
is, and renames both the folder and the image to match:

    D:\PhotoPlay\
        some_dump_from_ebay\        ->  IGO 5 PT MB001\
            igo5_final.img          ->      HardDisk.img

The identification comes from \FOTO\SETTINGS\MAIN.SET inside the image, which
is where the cabinet records its own release and territory.  That file is
encrypted, but with a fixed key rather than a dongle-derived one, so it can be
read with nothing but the image.  Folder names are not trusted: one image in
circulation is filed as a 1998 Spanish build and its own settings say
"Version 2003 (ES)".

The serial comes from \MENU\NSB.NR, or NONSB when the image has no such file.

A log naming every folder before and after is written to the top-level folder.

Nothing here needs the dongle, and nothing writes inside the images -- the only
changes on disk are the two renames.  Run with --dry-run first to see what it
would do.

No third-party modules; Python 3.8+.
"""

import argparse
import hashlib
import os
import re
import struct
import sys

SET_KEY = 0x00016295
LOG_NAME = "photoplay-catalog.log"

# ---------------------------------------------------------------- FAT reading

class FatError(Exception):
    pass


class Fat:
    """Read-only FAT12/16/32 reader that works straight off a disk image."""

    def __init__(self, path):
        self.f = open(path, "rb")
        self.size = os.path.getsize(path)
        self._boot()

    def close(self):
        try:
            self.f.close()
        except Exception:
            pass

    def _at(self, off, n):
        self.f.seek(off)
        d = self.f.read(n)
        if len(d) != n:
            raise FatError("short read at %d" % off)
        return d

    def _boot(self):
        mbr = self._at(0, 512)
        self.part = 0
        if mbr[510] == 0x55 and mbr[511] == 0xAA:
            for i in range(4):
                e = mbr[0x1BE + i * 16: 0x1BE + i * 16 + 16]
                if e[4] in (0x01, 0x04, 0x06, 0x0B, 0x0C, 0x0E):
                    off = struct.unpack_from("<I", e, 8)[0] * 512
                    if off and off + 512 <= self.size:
                        self.part = off
                        break

        b = self._at(self.part, 512)
        self.bps = struct.unpack_from("<H", b, 0x0B)[0]
        self.spc = b[0x0D]
        rsvd = struct.unpack_from("<H", b, 0x0E)[0]
        nfat = b[0x10]
        self.root_ents = struct.unpack_from("<H", b, 0x11)[0]
        fatsz = struct.unpack_from("<H", b, 0x16)[0]
        tot = struct.unpack_from("<H", b, 0x13)[0]
        if tot == 0:
            tot = struct.unpack_from("<I", b, 0x20)[0]
        if fatsz == 0:
            fatsz = struct.unpack_from("<I", b, 0x24)[0]
        if not (self.bps and self.spc and rsvd and nfat and fatsz and tot):
            raise FatError("not a FAT volume")
        if self.bps not in (512, 1024, 2048, 4096) or self.spc & (self.spc - 1):
            raise FatError("not a FAT volume")

        root_secs = (self.root_ents * 32 + self.bps - 1) // self.bps
        self.fat_off = self.part + rsvd * self.bps
        self.root_off = self.fat_off + nfat * fatsz * self.bps
        self.data_off = self.root_off + root_secs * self.bps
        if self.data_off >= self.size:
            raise FatError("not a FAT volume")
        clusters = (tot - rsvd - nfat * fatsz - root_secs) // self.spc
        self.bits = 12 if clusters < 4085 else (16 if clusters < 65525 else 32)
        self.root_clus = struct.unpack_from("<I", b, 0x2C)[0] if self.bits == 32 else 0

    def _next(self, c):
        if self.bits == 16:
            return struct.unpack_from("<H", self._at(self.fat_off + c * 2, 2), 0)[0]
        if self.bits == 32:
            return struct.unpack_from("<I", self._at(self.fat_off + c * 4, 4), 0)[0] & 0x0FFFFFFF
        off = self.fat_off + (c * 3) // 2
        v = struct.unpack_from("<H", self._at(off, 2), 0)[0]
        return (v >> 4) if (c & 1) else (v & 0x0FFF)

    def _eoc(self, c):
        return c >= {12: 0x0FF8, 16: 0xFFF8, 32: 0x0FFFFFF8}[self.bits]

    def _chain(self, clus, size=None):
        csz = self.spc * self.bps
        out = bytearray()
        seen = 0
        while clus >= 2 and not self._eoc(clus) and seen < 1 << 20:
            out += self._at(self.data_off + (clus - 2) * csz, csz)
            if size is not None and len(out) >= size:
                break
            clus = self._next(clus)
            seen += 1
        return bytes(out[:size]) if size is not None else bytes(out)

    def _entries(self, clus):
        """yield (name83, attr, first_cluster, size) for one directory"""
        if clus == 0 and self.bits != 32:
            raw = self._at(self.root_off, self.root_ents * 32)
        else:
            raw = self._chain(clus or self.root_clus)
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if len(e) < 32 or e[0] == 0x00:
                return
            if e[0] == 0xE5 or e[11] == 0x0F:
                continue
            first = struct.unpack_from("<H", e, 26)[0]
            if self.bits == 32:
                first |= struct.unpack_from("<H", e, 20)[0] << 16
            yield e[:11].decode("cp437"), e[11], first, struct.unpack_from("<I", e, 28)[0]

    def find(self, path):
        """path like /MENU/NSB.NR -> (cluster, size, is_dir) or None"""
        clus, size, isdir = 0, 0, True
        for part in [p for p in path.upper().split("/") if p]:
            if not isdir:
                return None
            base, _, ext = part.partition(".")
            want = (base.ljust(8) + ext.ljust(3))[:11]
            for name, attr, first, sz in self._entries(clus):
                if name == want:
                    clus, size, isdir = first, sz, bool(attr & 0x10)
                    break
            else:
                return None
        return clus, size, isdir

    def read(self, path):
        hit = self.find(path)
        if not hit or hit[2]:
            return None
        clus, size, _ = hit
        return self._chain(clus, size) if size else b""


# ------------------------------------------------------------- MAIN.SET

def _decrypt(buf, seed=SET_KEY):
    """dst[i] ^= keystream[i]; the keystream restarts at `seed` every call."""
    out = bytearray(buf)
    s = seed
    for i in range(len(out)):
        s = (s * 0x08088405 + 1) & 0xFFFFFFFF
        out[i] ^= (s >> 24) & 0xFF
    return bytes(out)


def parse_settings(raw):
    """-> {key: value} from a \\FOTO\\SETTINGS\\*.SET blob"""
    if not raw or len(raw) < 6:
        return {}
    count = struct.unpack("<H", _decrypt(raw[0:2]))[0]
    size1 = struct.unpack("<H", _decrypt(raw[2:4]))[0]
    size2 = struct.unpack("<H", _decrypt(raw[4:6]))[0]
    if not size1 or not size2 or 6 + size1 + size2 > len(raw) + 64:
        return {}
    keys = _decrypt(raw[6:6 + size1])
    pool = _decrypt(raw[6 + size1:6 + size1 + size2])
    out, p = {}, 0
    for _ in range(count):
        e = keys.find(b"\x00", p)
        if e < 0 or e + 3 > len(keys):
            break
        name = keys[p:e].decode("latin1")
        off = struct.unpack("<H", keys[e + 1:e + 3])[0]
        out[name] = pool[off:].split(b"\x00")[0].decode("latin1", "replace")
        p = e + 3
    return out


# "Version 2005B" -> "IGO 5".  Longest banner first so 2005B beats 2005.
RELEASES = [
    ("Version 2005B", "IGO 5"),
    ("Version 99",    "Photo Play 99"),
    ("Version 2000",  "Photo Play 2000"),
    ("Version 2001",  "Photo Play 2001"),
    ("Version 2002",  "IGO 2"),
    ("Version 2003",  "IGO 3"),
    ("Version 2004",  "IGO 4"),
    ("Version 2005",  "IGO 5"),
    ("Version 2006",  "IGO 6"),
    ("Version 2007",  "IGO 7"),
    ("Version 2008",  "IGO 8"),
]


def identify(fs):
    """-> (release, territory, raw_banner); release is None if unidentified"""
    raw = fs.read("/FOTO/SETTINGS/MAIN.SET")
    settings = parse_settings(raw) if raw else {}
    banner = settings.get("Version", "")
    if not banner:
        return None, "", ""
    terr = settings.get("Land", "")
    if not terr:
        m = re.search(r"\(([^)]+)\)", banner)
        terr = m.group(1) if m else ""
    for prefix, name in RELEASES:
        if banner.startswith(prefix):
            return name, terr.strip(), banner
    return None, terr.strip(), banner


# ------------------------------------------------------------- per folder

SAFE = re.compile(r'[<>:"/\\|?*\x00-\x1f]')


def safe_name(s):
    return SAFE.sub("_", s).strip().rstrip(".") or "UNNAMED"


def unique_dir(parent, name, taken):
    """`taken` carries the names already claimed in this run, so a dry run
    previews exactly the names an apply would produce rather than showing the
    same target twice."""
    cand = os.path.join(parent, name)
    if not os.path.exists(cand) and cand.lower() not in taken:
        taken.add(cand.lower())
        return cand
    for n in range(2, 1000):
        cand = os.path.join(parent, "%s (%d)" % (name, n))
        if not os.path.exists(cand) and cand.lower() not in taken:
            taken.add(cand.lower())
            return cand
    raise FatError("cannot find a free name for %r" % name)


def find_image(folder):
    """-> (path, error).  Ambiguity is an error; we do not guess."""
    imgs = []
    for root, _dirs, files in os.walk(folder):
        for fn in files:
            if fn.lower().endswith(".img"):
                imgs.append(os.path.join(root, fn))
    if not imgs:
        return None, "no .img file"
    if len(imgs) > 1:
        rel = ", ".join(sorted(os.path.relpath(i, folder) for i in imgs))
        return None, "%d .img files (%s)" % (len(imgs), rel)
    return imgs[0], None


def inspect(img):
    """-> dict of everything we learn from the image"""
    fs = Fat(img)
    try:
        release, terr, banner = identify(fs)

        nsb = "NONSB"
        raw = fs.read("/MENU/NSB.NR")
        if raw:
            token = raw.decode("latin1", "replace").split()
            if token:
                nsb = safe_name(token[0])

        keyn = fs.find("/MENU/KEYN.COM")
        keyn_present = keyn is not None and not keyn[2]
        keyn_size = keyn[1] if keyn_present else 0

        # Presence alone is misleading: an image can carry an inert KEYN.COM
        # that nothing runs.  What matters is whether the boot batch calls it.
        keyn_called = False
        bat = fs.read("/AUTOPTS.BAT")
        if bat:
            for line in bat.decode("latin1", "replace").splitlines():
                stripped = line.strip().lower()
                if stripped.startswith(("rem", "::")):
                    continue
                if "keyn" in stripped:
                    keyn_called = True
                    break

        menu = fs.read("/MENU/MENU.EXE")
        menu_md5 = hashlib.md5(menu).hexdigest() if menu else ""

        return {
            "release": release, "territory": terr, "banner": banner,
            "nsb": nsb, "keyn_present": keyn_present, "keyn_size": keyn_size,
            "keyn_called": keyn_called, "menu_md5": menu_md5,
        }
    finally:
        fs.close()


def describe_keyn(info):
    if not info["keyn_present"]:
        return "no"
    bits = []
    if info["keyn_size"] == 0:
        bits.append("empty")
    bits.append("called from AUTOPTS" if info["keyn_called"] else "not called")
    return "yes (%s)" % ", ".join(bits)


# ------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(
        description="Identify Photo Play / I.G.O. disk images and rename their "
                    "folders to <release> <territory> <NSB>.")
    ap.add_argument("folder", help="folder containing one subfolder per image")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would happen; rename nothing")
    ap.add_argument("--log", default=None,
                    help="log file (default: %s in the target folder)" % LOG_NAME)
    args = ap.parse_args()

    root = os.path.abspath(args.folder)
    if not os.path.isdir(root):
        sys.exit("not a folder: %s" % root)

    subs = sorted(d for d in os.listdir(root) if os.path.isdir(os.path.join(root, d)))
    if not subs:
        sys.exit("no subfolders in %s" % root)

    rows, problems, taken = [], 0, set()
    for original in subs:
        folder = os.path.join(root, original)
        img, err = find_image(folder)
        if err:
            problems += 1
            rows.append((original, None, None, None, None, err))
            print("  %-40s SKIPPED: %s" % (original, err))
            continue

        try:
            info = inspect(img)
        except Exception as ex:
            problems += 1
            rows.append((original, None, None, None, None, "unreadable (%s)" % ex))
            print("  %-40s SKIPPED: unreadable (%s)" % (original, ex))
            continue

        if info["release"]:
            version = "%s %s" % (info["release"], info["territory"]) if info["territory"] \
                      else info["release"]
        else:
            version = info["banner"] or "UNKNOWN"

        note = ""
        # Rename the image first, while the folder path is still known good.
        target_img = os.path.join(os.path.dirname(img), "HardDisk.img")
        if os.path.abspath(img) != os.path.abspath(target_img):
            if os.path.exists(target_img):
                note = "left image name alone (HardDisk.img already exists)"
                problems += 1
            elif not args.dry_run:
                os.rename(img, target_img)

        # Then the folder, but only if we actually identified it -- renaming a
        # pile of unidentified images to the same name helps nobody.
        renamed = original
        if info["release"]:
            want = safe_name("%s %s" % (version, info["nsb"]))
            # A folder already carrying this name -- or this name with a "(2)"
            # suffix, because a duplicate release had claimed the plain one --
            # is correct as it stands; renaming it again would just walk the
            # suffix upwards on every run.
            already = (original == want
                       or re.match(re.escape(want) + r" \(\d+\)$", original))
            if already:
                taken.add(os.path.join(root, original).lower())
            else:
                dest = unique_dir(root, want, taken)
                if not args.dry_run:
                    os.rename(folder, dest)
                renamed = os.path.basename(dest)
        else:
            note = (note + "; " if note else "") + "not identified, folder left alone"
            problems += 1

        rows.append((original, renamed, version, describe_keyn(info),
                     info["menu_md5"] or "(no MENU.EXE)", note))
        print("  %-40s -> %-34s %s" % (original, renamed, version))

    log_path = args.log or os.path.join(root, LOG_NAME)
    with open(log_path, "w", encoding="utf-8") as fh:
        fh.write("Photo Play image catalogue\n")
        fh.write("Folder : %s\n" % root)
        fh.write("Mode   : %s\n" % ("DRY RUN - nothing renamed" if args.dry_run else "applied"))
        fh.write("Entries: %d (%d needing attention)\n\n" % (len(rows), problems))
        fh.write("Original folder - Renamed folder - Version and country - "
                 "KEYN.COM - MENU.EXE md5\n")
        fh.write("=" * 100 + "\n")
        for orig, new, ver, keyn, md5, note in rows:
            if ver is None:
                fh.write("%s - SKIPPED: %s\n" % (orig, note))
            else:
                fh.write("%s - %s - %s - %s - %s%s\n"
                         % (orig, new, ver, keyn, md5,
                            ("  [%s]" % note) if note else ""))

    print("\n%d folders, %d needing attention" % (len(rows), problems))
    print("log: %s" % log_path)
    if args.dry_run:
        print("dry run - nothing was renamed; re-run without --dry-run to apply")


if __name__ == "__main__":
    main()
