/*
 * PeepeeBox   A funworld Photo Play / I.G.O. arcade cabinet emulator,
 *             forked from 86Box.
 *
 *             A minimal reader for the release archives.
 *
 *             The three release zips are written by Compress-Archive, Info-ZIP
 *             and macOS zip, and between them use exactly two methods: stored
 *             and deflate.  The macOS bundle also carries symlinks.  That is
 *             the whole of what this reads; encryption, zip64 and the other
 *             compression methods are refused rather than handled, and any
 *             archive that needs them is not one of ours.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 */
#ifndef QT_ZIPARCHIVE_HPP
#define QT_ZIPARCHIVE_HPP

#include <QFile>
#include <QList>
#include <QString>

class ZipArchive {
public:
    struct Entry {
        QString name;         /* as stored, forward slashes */
        quint32 crc32       = 0;
        quint32 compressed  = 0;
        quint32 size        = 0;
        quint32 localOffset = 0;
        quint16 method      = 0; /* 0 stored, 8 deflate */
        quint32 unixMode    = 0; /* 0 when the writer was not Unix */
        bool    isDir       = false;
        bool    isSymlink   = false;
    };

    explicit ZipArchive(const QString &path);

    /* Read the central directory.  Every entry is validated against what the
       extractor can do, so a false here means the archive is not usable at all,
       not that one entry is odd. */
    bool open();

    const QList<Entry> &entries() const { return list; }
    const QString      &error() const { return err; }

    /* Write one entry's data out.  The destination's directory must exist.
       The CRC is checked, so a true means the bytes on disk are the bytes the
       archive promised. */
    bool extract(const Entry &entry, const QString &destPath);

    /* For symlinks: the entry's data is the link target. */
    bool readSmall(const Entry &entry, QByteArray &out);

private:
    bool locateData(const Entry &entry, qint64 &dataOffset);
    bool inflateTo(const Entry &entry, qint64 dataOffset, QIODevice &out);

    QFile        file;
    QList<Entry> list;
    QString      err;
};

#endif
