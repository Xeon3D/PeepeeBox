#!/usr/bin/env python3
"""Check a release archive against the updater of PeepeeBox 1.11 and earlier.

Those builds unpack an update in 64 KiB steps and treat zlib's Z_BUF_ERROR as
fatal.  inflate() returns it when the previous call filled the 64 KiB output
buffer at exactly the moment the current 64 KiB of input ran out: the next call
has nothing to do.  Whether an entry does that is chance alignment of its
deflate stream -- the 1.11 Windows archive's PeepeeBox.exe did, and every
update to 1.11 failed with "inflate failed (-5)".  1.12 fixed the reader, but
the installed copies that will fetch the next release are the old ones, so
every release archive has to get through the old loop.

    updatezip.py ARCHIVE...         report, exit 1 if any archive would fail
    updatezip.py --fix ARCHIVE...   rewrite failing archives so they pass

--fix re-deflates each failing entry at another compression level, which moves
the alignment, and stores it uncompressed if no level passes (the old reader
handles stored entries in a plain copy).  Names, modes, symlinks and order are
kept; the rewritten archive is checked again before it replaces the original.
"""
import os
import struct
import sys
import tempfile
import zipfile
import zlib

CHUNK = 65536  # both buffers in qt_ziparchive.cpp's inflateTo()


def old_reader_fails(raw):
    """Replay 1.11's inflate loop over one raw deflate stream.

    Returns None if it gets through, or a reason."""
    d = zlib.decompressobj(-zlib.MAX_WBITS)
    pos = 0
    pending = b""
    while True:
        if not pending:
            if pos >= len(raw):
                return "stream ended early"
            pending = raw[pos:pos + CHUNK]
            pos += len(pending)
        while True:
            out = d.decompress(pending, CHUNK)
            pending = d.unconsumed_tail
            if d.eof:
                return None
            if len(out) < CHUNK:
                break  # the old loop's inner do/while ends: avail_out != 0
            # Output buffer full: the old loop calls inflate() again at once.
            # With no input left and nothing held back, that is Z_BUF_ERROR.
            if not pending and d.copy().decompress(b"", CHUNK) == b"":
                return "Z_BUF_ERROR after %d input bytes" % pos


def raw_stream(zf, fp, info):
    fp.seek(info.header_offset)
    local = fp.read(30)
    name_len, extra_len = struct.unpack("<HH", local[26:30])
    fp.seek(info.header_offset + 30 + name_len + extra_len)
    return fp.read(info.compress_size)


def failing_entries(path):
    bad = {}
    with zipfile.ZipFile(path) as zf, open(path, "rb") as fp:
        for info in zf.infolist():
            if info.compress_type != zipfile.ZIP_DEFLATED:
                continue
            why = old_reader_fails(raw_stream(zf, fp, info))
            if why:
                bad[info.filename] = why
    return bad


def deflate(data, level):
    c = zlib.compressobj(level, zlib.DEFLATED, -zlib.MAX_WBITS)
    return c.compress(data) + c.flush()


def rewrite(path, bad):
    fd, tmp = tempfile.mkstemp(suffix=".zip", dir=os.path.dirname(os.path.abspath(path)))
    os.close(fd)
    try:
        with zipfile.ZipFile(path) as src, zipfile.ZipFile(tmp, "w") as dst:
            for info in src.infolist():
                data = src.read(info)
                if info.filename not in bad:
                    dst.writestr(info, data, compress_type=info.compress_type,
                                 compresslevel=None if info.compress_type == zipfile.ZIP_STORED else 6)
                    continue
                # zipfile deflates exactly as deflate() does, so the level
                # that passes here is the one that passes in the archive.
                for level in (9, 8, 7, 5, 4, 3, 2, 1):
                    if old_reader_fails(deflate(data, level)) is None:
                        print("  %s: re-deflated at level %d" % (info.filename, level))
                        dst.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=level)
                        break
                else:
                    print("  %s: stored uncompressed" % info.filename)
                    dst.writestr(info, data, compress_type=zipfile.ZIP_STORED)
        # The entries that passed were re-deflated too, and could have moved
        # onto the bad alignment themselves; the caller goes round again.
        still = failing_entries(tmp)
        if still:
            return still
        with zipfile.ZipFile(tmp) as zf:
            broken = zf.testzip()
            if broken:
                raise RuntimeError("rewritten archive has a bad CRC in %s" % broken)
        os.replace(tmp, path)
        return {}
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)


def main(argv):
    fix = "--fix" in argv
    paths = [a for a in argv if a != "--fix"]
    if not paths:
        print(__doc__)
        return 2
    status = 0
    for path in paths:
        bad = failing_entries(path)
        if not bad:
            print("%s: passes the 1.11 updater" % path)
            continue
        for name, why in bad.items():
            print("%s: %s would fail the 1.11 updater (%s)" % (path, name, why))
        if fix:
            for _ in range(5):
                more = rewrite(path, bad)
                if not more:
                    break
                bad.update(more)
            else:
                raise RuntimeError("%s: could not make it pass: %s" % (path, more))
            print("%s: rewritten, passes the 1.11 updater" % path)
        else:
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
