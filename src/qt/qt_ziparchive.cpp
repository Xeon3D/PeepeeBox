/*
 * PeepeeBox   A funworld Photo Play / I.G.O. arcade cabinet emulator,
 *             forked from 86Box.
 *
 *             A minimal reader for the release archives.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 */
#include <QBuffer>
#include <QtEndian>

#include <zlib.h>

#include "qt_ziparchive.hpp"

namespace {

constexpr quint32 SIG_EOCD    = 0x06054b50;
constexpr quint32 SIG_CENTRAL = 0x02014b50;
constexpr quint32 SIG_LOCAL   = 0x04034b50;

constexpr int EOCD_SIZE       = 22;
constexpr int CENTRAL_SIZE    = 46;
constexpr int LOCAL_SIZE      = 30;
constexpr int MAX_COMMENT     = 0xFFFF;

quint16
u16(const QByteArray &b, int at)
{
    return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(b.constData() + at));
}

quint32
u32(const QByteArray &b, int at)
{
    return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(b.constData() + at));
}

}

ZipArchive::ZipArchive(const QString &path)
    : file(path)
{
}

bool
ZipArchive::open()
{
    list.clear();
    if (!file.open(QIODevice::ReadOnly)) {
        err = QStringLiteral("cannot open %1: %2").arg(file.fileName(), file.errorString());
        return false;
    }

    /* The end-of-central-directory record is at the very end unless the archive
       carries a comment, so search backwards through the largest comment a zip
       can have. */
    const qint64 fileSize = file.size();
    const qint64 tailLen  = qMin<qint64>(fileSize, EOCD_SIZE + MAX_COMMENT);
    if (tailLen < EOCD_SIZE) {
        err = QStringLiteral("too small to be a zip archive");
        return false;
    }
    file.seek(fileSize - tailLen);
    const QByteArray tail = file.read(tailLen);
    int eocd = -1;
    for (int i = tail.size() - EOCD_SIZE; i >= 0; i--) {
        if (u32(tail, i) == SIG_EOCD) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        err = QStringLiteral("no end-of-central-directory record");
        return false;
    }

    const quint16 count    = u16(tail, eocd + 10);
    const quint32 cdSize   = u32(tail, eocd + 12);
    const quint32 cdOffset = u32(tail, eocd + 16);
    if ((count == 0xFFFF) || (cdSize == 0xFFFFFFFF) || (cdOffset == 0xFFFFFFFF)) {
        err = QStringLiteral("zip64 archives are not supported");
        return false;
    }
    if ((qint64) cdOffset + cdSize > fileSize) {
        err = QStringLiteral("central directory lies outside the file");
        return false;
    }

    file.seek(cdOffset);
    const QByteArray cd = file.read(cdSize);
    if (cd.size() != (int) cdSize) {
        err = QStringLiteral("short read on the central directory");
        return false;
    }

    int at = 0;
    for (quint16 i = 0; i < count; i++) {
        if ((at + CENTRAL_SIZE > cd.size()) || (u32(cd, at) != SIG_CENTRAL)) {
            err = QStringLiteral("central directory entry %1 is malformed").arg(i);
            return false;
        }
        const quint16 madeBy   = u16(cd, at + 4);
        const quint16 flags    = u16(cd, at + 8);
        const quint16 nameLen  = u16(cd, at + 28);
        const quint16 extraLen = u16(cd, at + 30);
        const quint16 commLen  = u16(cd, at + 32);
        if (at + CENTRAL_SIZE + nameLen + extraLen + commLen > cd.size()) {
            err = QStringLiteral("central directory entry %1 overruns the directory").arg(i);
            return false;
        }

        Entry e;
        e.method      = u16(cd, at + 10);
        e.crc32       = u32(cd, at + 16);
        e.compressed  = u32(cd, at + 20);
        e.size        = u32(cd, at + 24);
        e.localOffset = u32(cd, at + 42);
        e.name        = QString::fromUtf8(cd.constData() + at + CENTRAL_SIZE, nameLen);
        e.isDir       = e.name.endsWith('/');

        /* The high byte of "version made by" names the writer's OS; 3 is Unix,
           and only then do the external attributes carry a mode. */
        if ((madeBy >> 8) == 3) {
            e.unixMode  = u32(cd, at + 38) >> 16;
            e.isSymlink = ((e.unixMode & 0xF000) == 0xA000);
        }

        if (flags & 1) {
            err = QStringLiteral("%1 is encrypted").arg(e.name);
            return false;
        }
        if ((e.method != 0) && (e.method != 8)) {
            err = QStringLiteral("%1 uses compression method %2").arg(e.name).arg(e.method);
            return false;
        }
        if ((e.compressed == 0xFFFFFFFF) || (e.size == 0xFFFFFFFF) || (e.localOffset == 0xFFFFFFFF)) {
            err = QStringLiteral("%1 needs zip64").arg(e.name);
            return false;
        }

        list.append(e);
        at += CENTRAL_SIZE + nameLen + extraLen + commLen;
    }

    return true;
}

bool
ZipArchive::locateData(const Entry &entry, qint64 &dataOffset)
{
    /* The local header repeats the name and may carry its own extra field, and
       both lengths can differ from the central directory's, so it has to be read
       to find where the data starts. */
    if (!file.seek(entry.localOffset)) {
        err = QStringLiteral("%1: cannot seek to local header").arg(entry.name);
        return false;
    }
    const QByteArray local = file.read(LOCAL_SIZE);
    if ((local.size() != LOCAL_SIZE) || (u32(local, 0) != SIG_LOCAL)) {
        err = QStringLiteral("%1: local header is malformed").arg(entry.name);
        return false;
    }
    dataOffset = (qint64) entry.localOffset + LOCAL_SIZE + u16(local, 26) + u16(local, 28);
    return true;
}

bool
ZipArchive::inflateTo(const Entry &entry, qint64 dataOffset, QIODevice &out)
{
    if (!file.seek(dataOffset)) {
        err = QStringLiteral("%1: cannot seek to data").arg(entry.name);
        return false;
    }

    quint32 crc       = crc32(0L, Z_NULL, 0);
    qint64  remaining = entry.compressed;
    qint64  written   = 0;
    char    in[65536];
    char    outBuf[65536];

    if (entry.method == 0) {
        while (remaining > 0) {
            const qint64 n = file.read(in, qMin<qint64>(remaining, sizeof(in)));
            if (n <= 0) {
                err = QStringLiteral("%1: short read").arg(entry.name);
                return false;
            }
            if (out.write(in, n) != n) {
                err = QStringLiteral("%1: write failed: %2").arg(entry.name, out.errorString());
                return false;
            }
            crc = crc32(crc, reinterpret_cast<const Bytef *>(in), n);
            remaining -= n;
            written += n;
        }
    } else {
        z_stream zs {};
        /* Negative window bits: raw deflate, no zlib header, which is what zip
           stores. */
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
            err = QStringLiteral("%1: inflateInit2 failed").arg(entry.name);
            return false;
        }
        int ret = Z_OK;
        do {
            if (zs.avail_in == 0) {
                if (remaining <= 0)
                    break;
                const qint64 n = file.read(in, qMin<qint64>(remaining, sizeof(in)));
                if (n <= 0) {
                    inflateEnd(&zs);
                    err = QStringLiteral("%1: short read").arg(entry.name);
                    return false;
                }
                remaining -= n;
                zs.next_in  = reinterpret_cast<Bytef *>(in);
                zs.avail_in = (uInt) n;
            }
            do {
                zs.next_out  = reinterpret_cast<Bytef *>(outBuf);
                zs.avail_out = sizeof(outBuf);
                ret          = inflate(&zs, Z_NO_FLUSH);
                /* Z_BUF_ERROR only says no progress was possible: the last
                   call filled the output buffer exactly as the input ran out,
                   so this one had nothing to do.  More input is the cure --
                   the 1.11 archive's executable hit this and failed with -5. */
                if ((ret == Z_BUF_ERROR) && (zs.avail_in == 0))
                    break;
                if ((ret != Z_OK) && (ret != Z_STREAM_END)) {
                    inflateEnd(&zs);
                    err = QStringLiteral("%1: inflate failed (%2)").arg(entry.name).arg(ret);
                    return false;
                }
                const qint64 have = sizeof(outBuf) - zs.avail_out;
                if (have > 0) {
                    if (out.write(outBuf, have) != have) {
                        inflateEnd(&zs);
                        err = QStringLiteral("%1: write failed: %2").arg(entry.name, out.errorString());
                        return false;
                    }
                    crc = crc32(crc, reinterpret_cast<const Bytef *>(outBuf), have);
                    written += have;
                }
            } while ((zs.avail_out == 0) && (ret != Z_STREAM_END));
        } while (ret != Z_STREAM_END);
        inflateEnd(&zs);
        if (ret != Z_STREAM_END) {
            err = QStringLiteral("%1: deflate stream ended early").arg(entry.name);
            return false;
        }
    }

    if ((written != entry.size) || (crc != entry.crc32)) {
        err = QStringLiteral("%1: checksum or size mismatch").arg(entry.name);
        return false;
    }
    return true;
}

bool
ZipArchive::extract(const Entry &entry, const QString &destPath)
{
    qint64 dataOffset = 0;
    if (!locateData(entry, dataOffset))
        return false;

    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        err = QStringLiteral("cannot create %1: %2").arg(destPath, out.errorString());
        return false;
    }
    const bool ok = inflateTo(entry, dataOffset, out);
    out.close();
    if (!ok)
        out.remove();
    return ok;
}

bool
ZipArchive::readSmall(const Entry &entry, QByteArray &out)
{
    qint64 dataOffset = 0;
    if (!locateData(entry, dataOffset))
        return false;
    if (entry.size > 65536) {
        err = QStringLiteral("%1: too large to read in one piece").arg(entry.name);
        return false;
    }
    out.clear();
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    return inflateTo(entry, dataOffset, buf);
}
