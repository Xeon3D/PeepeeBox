/*
 * PeepeeBox   A funworld Photo Play / I.G.O. arcade cabinet emulator,
 *             forked from 86Box.
 *
 *             Automatic updates from the GitHub releases page.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 */
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QPushButton>
#include <QSysInfo>
#include <QTextStream>

#include "qt_autoupdate.hpp"
#include "qt_mainwindow.hpp"
#include "qt_ziparchive.hpp"

extern "C" {
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/version.h>
}

extern MainWindow *main_window;

AutoUpdate *AutoUpdate::self          = nullptr;
bool        AutoUpdate::restartWanted = false;

namespace {

/* How often the schedule is looked at.  The intervals are hours and up, so
   this only needs to be fine enough that "hourly" is not an hour and a half. */
constexpr int TICK_MS = 5 * 60 * 1000;

/* The first look after start-up waits for the machine to be up and running. */
constexpr int FIRST_TICK_MS = 20 * 1000;

/* A look that could not reach GitHub is tried again after this long, whatever
   the schedule says, instead of being counted as done. */
constexpr qint64 RETRY_SECONDS = 60 * 60;

const char PENDING_CLEANUP[] = "pending-cleanup.txt";
const char OLD_SUFFIX[]      = ".old";

}

AutoUpdate::AutoUpdate(QObject *parent)
    : QObject(parent)
{
    self = this;
    /* GitHub answers asset downloads with a redirect to its object store. */
    nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    connect(&timer, &QTimer::timeout, this, &AutoUpdate::tick);
}

AutoUpdate::~AutoUpdate()
{
    if (self == this)
        self = nullptr;
}

AutoUpdate *
AutoUpdate::instance()
{
    return self;
}

QString
AutoUpdate::repository()
{
    return QStringLiteral("Xeon3D/PeepeeBox");
}

QString
AutoUpdate::currentRelease()
{
    QString rel = QString::fromLatin1(PEEPEEBOX_RELEASE);
    if (rel.endsWith('+'))
        rel.chop(1);
    if (rel.isEmpty() || !rel.at(0).isDigit())
        return {};
    return rel;
}

QString
AutoUpdate::updateDir()
{
    return QDir(QString::fromUtf8(exe_path)).filePath(QStringLiteral("update"));
}

qint64
AutoUpdate::intervalSeconds(int setting)
{
    switch (setting) {
        case 1:
            return 60 * 60;
        case 2:
            return 24 * 60 * 60;
        case 3:
            return 7 * 24 * 60 * 60;
        case 4:
            return 30 * 24 * 60 * 60;
        default:
            return 0;
    }
}

QString
AutoUpdate::assetForThisPlatform()
{
    const QString arch = QSysInfo::buildCpuArchitecture();
#if defined(Q_OS_WINDOWS)
    if (arch == QLatin1String("x86_64"))
        return QStringLiteral("PeepeeBox-windows-x64.zip");
#elif defined(Q_OS_MACOS)
    if (arch == QLatin1String("arm64"))
        return QStringLiteral("PeepeeBox-macos-arm64.zip");
#elif defined(Q_OS_LINUX)
    if (arch == QLatin1String("x86_64"))
        return QStringLiteral("PeepeeBox-linux-x86_64.zip");
#endif
    return {};
}

/* Numeric, dot-separated, missing parts count as zero: 1.9 < 1.9.1 < 1.10. */
int
AutoUpdate::versionCompare(const QString &a, const QString &b)
{
    const QStringList pa = a.split('.');
    const QStringList pb = b.split('.');
    const int         n  = qMax(pa.size(), pb.size());
    for (int i = 0; i < n; i++) {
        const int va = (i < pa.size()) ? pa[i].toInt() : 0;
        const int vb = (i < pb.size()) ? pb[i].toInt() : 0;
        if (va != vb)
            return (va < vb) ? -1 : 1;
    }
    return 0;
}

void
AutoUpdate::start()
{
    QTimer::singleShot(FIRST_TICK_MS, this, &AutoUpdate::tick);
    timer.start(TICK_MS);
}

void
AutoUpdate::tick()
{
    if ((update_check == 0) || busy())
        return;

    const qint64 now = QDateTime::currentSecsSinceEpoch();
    if (now < retryAfter)
        return;

    /* A last look in the future is a clock that has been set back; look now
       rather than wait out an interval that may never come. */
    const qint64 interval = intervalSeconds(update_check);
    if ((update_last_check > 0) && (update_last_check <= now) && (now - update_last_check < interval))
        return;

    checkNow();
}

void
AutoUpdate::report(const QString &text)
{
    lastStatus = text;
    pclog("Update: %s\n", text.toUtf8().constData());
    emit status(text);
}

void
AutoUpdate::checkNow()
{
    if (busy()) {
        report(tr("An update is already in progress."));
        return;
    }

    state = State::Checking;
    report(tr("Checking for a new release…"));

    QNetworkRequest req(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repository())));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PeepeeBox/%1").arg(QString::fromLatin1(PEEPEEBOX_RELEASE)));

    reply = nam.get(req);
    connect(reply, &QNetworkReply::finished, this, &AutoUpdate::onReleaseReply);
}

void
AutoUpdate::finishCheck(bool stamp)
{
    state = State::Idle;
    if (stamp) {
        update_last_check = QDateTime::currentSecsSinceEpoch();
        config_save_global();
    }
}

void
AutoUpdate::onReleaseReply()
{
    reply->deleteLater();
    const QByteArray body = reply->readAll();
    const int        http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError && http != 404) {
        report(tr("Could not reach GitHub: %1").arg(reply->errorString()));
        reply      = nullptr;
        retryAfter = QDateTime::currentSecsSinceEpoch() + RETRY_SECONDS;
        state      = State::Idle;
        return;
    }
    reply = nullptr;

    if (http == 404) {
        report(tr("There are no releases on GitHub yet."));
        finishCheck(true);
        return;
    }

    const QJsonObject rel = QJsonDocument::fromJson(body).object();
    Release           latest;
    latest.tag     = rel.value(QStringLiteral("tag_name")).toString();
    latest.name    = rel.value(QStringLiteral("name")).toString();
    latest.pageUrl = rel.value(QStringLiteral("html_url")).toString();
    latest.version = latest.tag.startsWith('v') ? latest.tag.mid(1) : latest.tag;
    if (latest.version.isEmpty() || !latest.version.at(0).isDigit()) {
        report(tr("GitHub's answer was not a release."));
        retryAfter = QDateTime::currentSecsSinceEpoch() + RETRY_SECONDS;
        state      = State::Idle;
        return;
    }

    const QString current = currentRelease();
    if (current.isEmpty()) {
        report(tr("This is a development build (%1); automatic updates apply to numbered releases only. The latest release is %2.")
                   .arg(QString::fromLatin1(PEEPEEBOX_RELEASE), latest.version));
        finishCheck(true);
        return;
    }

    if (versionCompare(latest.version, current) <= 0) {
        report(tr("Up to date: %1 is the latest release.").arg(current));
        finishCheck(true);
        return;
    }

    const QString wanted = assetForThisPlatform();
    for (const auto &a : rel.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject asset = a.toObject();
        if (asset.value(QStringLiteral("name")).toString() == wanted) {
            latest.assetName = wanted;
            latest.assetUrl  = QUrl(asset.value(QStringLiteral("browser_download_url")).toString());
            latest.assetSize = static_cast<qint64>(asset.value(QStringLiteral("size")).toDouble());
            break;
        }
    }
    if (wanted.isEmpty() || latest.assetUrl.isEmpty()) {
        report(tr("Release %1 is out, but has no download for this platform.").arg(latest.version));
        finishCheck(true);
        return;
    }

    pending = latest;
    beginDownload();
}

void
AutoUpdate::beginDownload()
{
    QDir().mkpath(updateDir());
    download = new QFile(QDir(updateDir()).filePath(pending.assetName), this);
    if (!download->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        report(tr("Cannot write to %1: %2").arg(download->fileName(), download->errorString()));
        delete download;
        download = nullptr;
        finishCheck(true);
        return;
    }

    state       = State::Downloading;
    lastPercent = -1;
    report(tr("Downloading PeepeeBox %1…").arg(pending.version));
    emit main_window->statusBarMessage(tr("Downloading PeepeeBox %1…").arg(pending.version));

    QNetworkRequest req(pending.assetUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("PeepeeBox/%1").arg(QString::fromLatin1(PEEPEEBOX_RELEASE)));
    reply = nam.get(req);
    connect(reply, &QNetworkReply::readyRead, this, &AutoUpdate::onDownloadReadyRead);
    connect(reply, &QNetworkReply::downloadProgress, this, &AutoUpdate::onDownloadProgress);
    connect(reply, &QNetworkReply::finished, this, &AutoUpdate::onDownloadFinished);
}

void
AutoUpdate::onDownloadReadyRead()
{
    if (download != nullptr)
        download->write(reply->readAll());
}

void
AutoUpdate::onDownloadProgress(qint64 received, qint64 total)
{
    if (total <= 0)
        total = pending.assetSize;
    if (total <= 0)
        return;
    const int percent = static_cast<int>((received * 100) / total);
    if (percent == lastPercent)
        return;
    lastPercent        = percent;
    const QString text = tr("Downloading PeepeeBox %1… %2%").arg(pending.version).arg(percent);
    lastStatus         = text;
    emit status(text);
    emit main_window->statusBarMessage(text);
}

void
AutoUpdate::onDownloadFinished()
{
    reply->deleteLater();
    const bool    netOk   = (reply->error() == QNetworkReply::NoError);
    const QString netErr  = reply->errorString();
    const QString archive = download->fileName();

    if (netOk)
        download->write(reply->readAll());
    download->flush();
    download->close();
    reply = nullptr;
    const qint64 got = download->size();
    delete download;
    download = nullptr;

    if (!netOk) {
        QFile::remove(archive);
        report(tr("Download failed: %1").arg(netErr));
        emit main_window->statusBarMessage(tr("Update download failed."));
        finishCheck(true);
        return;
    }
    if ((pending.assetSize > 0) && (got != pending.assetSize)) {
        QFile::remove(archive);
        report(tr("Download was %1 bytes, expected %2.").arg(got).arg(pending.assetSize));
        emit main_window->statusBarMessage(tr("Update download failed."));
        finishCheck(true);
        return;
    }

    state = State::Installing;
    report(tr("Installing PeepeeBox %1…").arg(pending.version));
    emit main_window->statusBarMessage(tr("Installing PeepeeBox %1…").arg(pending.version));

    QString    error;
    const bool ok = install(archive, error);
    QFile::remove(archive);

    if (!ok) {
        report(tr("Installing %1 failed: %2").arg(pending.version, error));
        emit main_window->statusBarMessage(tr("Update failed; see the log."));
        finishCheck(true);
        return;
    }

    report(tr("PeepeeBox %1 is installed and will run from the next start.").arg(pending.version));
    emit main_window->statusBarMessage(tr("PeepeeBox %1 installed.").arg(pending.version));
    finishCheck(true);
    offerRestart();
}

/* One path inside the archive, with the top-level folder already stripped. */
static bool
safeRelativePath(const QString &rel)
{
    if (rel.isEmpty() || rel.startsWith('/') || rel.contains('\\') || rel.contains(':'))
        return false;
    for (const QString &part : rel.split('/')) {
        if (part == QLatin1String("..") || part == QLatin1String("."))
            return false;
    }
    return true;
}

static QFile::Permissions
permissionsFromMode(quint32 mode)
{
    QFile::Permissions p = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    if (mode & 0111)
        p |= QFile::ExeOwner | QFile::ExeGroup | QFile::ExeOther;
    return p;
}

bool
AutoUpdate::install(const QString &archivePath, QString &error)
{
    ZipArchive zip(archivePath);
    if (!zip.open()) {
        error = zip.error();
        return false;
    }

    /* The archive's layout is mapped onto the disk by where the executable
       sits in each: PeepeeBox/PeepeeBox.exe next to roms/ on Windows,
       PeepeeBox/bin/PeepeeBox on Linux, and inside the .app on macOS.  The
       folder in the archive that holds the executable is the folder on disk
       that holds this one, and the top-level folder of the archive is
       whatever lies above that by the same number of steps. */
    const QString exeFile = QCoreApplication::applicationFilePath();
    const QString exeName = QFileInfo(exeFile).fileName();
    const QString exeDir  = QFileInfo(exeFile).absolutePath();

    const ZipArchive::Entry *exeEntry = nullptr;
    for (const auto &e : zip.entries()) {
        if (!e.isDir && !e.isSymlink && (e.name.section('/', -1) == exeName)) {
            exeEntry = &e;
            break;
        }
    }
    if (exeEntry == nullptr) {
        error = tr("the archive does not contain %1").arg(exeName);
        return false;
    }

    const int     firstSlash = exeEntry->name.indexOf('/');
    const QString prefix     = (firstSlash < 0) ? QString() : exeEntry->name.left(firstSlash + 1);
    const QString zipExeDir  = exeEntry->name.mid(prefix.length()).section('/', 0, -2);

    QString root = exeDir;
    if (!zipExeDir.isEmpty()) {
        const QString suffix = QLatin1Char('/') + zipExeDir;
        if (!root.endsWith(suffix)) {
            error = tr("this program is at %1 but the archive keeps it under %2").arg(exeDir, zipExeDir);
            return false;
        }
        root.chop(suffix.length());
    }

    /* Everything is unpacked and checked before a single file on disk is
       touched, so a bad download or a full disk is found while there is still
       nothing to undo. */
    const QString stage = QDir(updateDir()).filePath(QStringLiteral("stage"));
    QDir(stage).removeRecursively();
    if (!QDir().mkpath(stage)) {
        error = tr("cannot create %1").arg(stage);
        return false;
    }

    struct Link {
        QString rel;
        QString target;
    };
    QStringList files;
    QList<Link> links;

    for (const auto &e : zip.entries()) {
        if (!e.name.startsWith(prefix))
            continue;
        const QString rel = e.name.mid(prefix.length());
        if (rel.isEmpty())
            continue;
        if (!safeRelativePath(e.isDir ? rel.chopped(1) : rel)) {
            error = tr("the archive contains an unsafe path: %1").arg(e.name);
            QDir(stage).removeRecursively();
            return false;
        }
        /* nvr/ is the CMOS the cabinet has settled into; the release carries a
           starting one for fresh folders, and replacing a running one would
           throw away whatever the BIOS has been told since. */
        if (rel.startsWith(QLatin1String("nvr/")))
            continue;
        if (e.isDir) {
            QDir().mkpath(QDir(stage).filePath(rel));
            continue;
        }

        const QString staged = QDir(stage).filePath(rel);
        QDir().mkpath(QFileInfo(staged).absolutePath());

        if (e.isSymlink) {
#ifdef Q_OS_WINDOWS
            continue;
#else
            QByteArray target;
            if (!zip.readSmall(e, target)) {
                error = zip.error();
                QDir(stage).removeRecursively();
                return false;
            }
            links.append({ rel, QString::fromUtf8(target) });
            continue;
#endif
        }

        if (!zip.extract(e, staged)) {
            error = zip.error();
            QDir(stage).removeRecursively();
            return false;
        }
        if (e.unixMode != 0)
            QFile::setPermissions(staged, permissionsFromMode(e.unixMode));
        files.append(rel);
    }

    /* Now the swap.  Each file already there is renamed aside rather than
       deleted: on Windows the running executable cannot be deleted, but it can
       be renamed, and everything renamed aside comes back if a later step
       fails. */
    struct Done {
        QString dest;
        bool    hadOld;
    };
    QList<Done> done;
    QStringList leftovers;

    auto rollback = [&]() {
        for (int i = done.size() - 1; i >= 0; i--) {
            QFile::remove(done[i].dest);
            if (done[i].hadOld)
                QFile::rename(done[i].dest + OLD_SUFFIX, done[i].dest);
        }
        QDir(stage).removeRecursively();
    };

    auto moveAside = [&](const QString &dest, bool &hadOld) -> bool {
        const QFileInfo fi(dest);
        hadOld = fi.exists() || fi.isSymLink();
        if (!hadOld)
            return true;
        QFile::remove(dest + OLD_SUFFIX);
        if (QFile::rename(dest, dest + OLD_SUFFIX))
            return true;
        error = tr("cannot move %1 aside").arg(dest);
        return false;
    };

    for (const QString &rel : files) {
        const QString dest = QDir(root).filePath(rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        bool hadOld = false;
        if (!moveAside(dest, hadOld)) {
            rollback();
            return false;
        }
        if (!QFile::rename(QDir(stage).filePath(rel), dest)) {
            error = tr("cannot put %1 in place").arg(dest);
            if (hadOld)
                QFile::rename(dest + OLD_SUFFIX, dest);
            rollback();
            return false;
        }
        done.append({ dest, hadOld });
    }
    for (const Link &l : links) {
        const QString dest = QDir(root).filePath(l.rel);
        QDir().mkpath(QFileInfo(dest).absolutePath());
        bool hadOld = false;
        if (!moveAside(dest, hadOld)) {
            rollback();
            return false;
        }
        if (!QFile::link(l.target, dest)) {
            error = tr("cannot create the link %1").arg(dest);
            if (hadOld)
                QFile::rename(dest + OLD_SUFFIX, dest);
            rollback();
            return false;
        }
        done.append({ dest, hadOld });
    }

    /* The files moved aside can mostly go now; the running executable, and on
       some systems its neighbours, cannot until the next start. */
    for (const Done &d : done) {
        if (d.hadOld && !QFile::remove(d.dest + OLD_SUFFIX))
            leftovers.append(d.dest + OLD_SUFFIX);
    }
    if (!leftovers.isEmpty()) {
        QFile list(QDir(updateDir()).filePath(QString::fromLatin1(PENDING_CLEANUP)));
        if (list.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&list);
            for (const QString &l : leftovers)
                ts << l << '\n';
        }
    }

    QDir(stage).removeRecursively();
    pclog("Update: %d files replaced under %s\n", (int) done.size(), root.toUtf8().constData());
    return true;
}

void
AutoUpdate::cleanupLeftovers()
{
    const QDir dir(updateDir());
    if (!dir.exists())
        return;

    QFile list(dir.filePath(QString::fromLatin1(PENDING_CLEANUP)));
    if (list.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QStringList remaining;
        QTextStream ts(&list);
        while (!ts.atEnd()) {
            const QString path = ts.readLine().trimmed();
            if (path.isEmpty())
                continue;
            if (QFile::exists(path) && !QFile::remove(path))
                remaining.append(path);
        }
        list.close();
        if (remaining.isEmpty())
            list.remove();
        else if (list.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream out(&list);
            for (const QString &r : remaining)
                out << r << '\n';
        }
    }

    /* A download or a stage left behind by a crash mid-update. */
    QDir(dir.filePath(QStringLiteral("stage"))).removeRecursively();
    for (const QString &zip : dir.entryList({ QStringLiteral("*.zip") }, QDir::Files))
        QFile::remove(dir.filePath(zip));

    QDir().rmdir(dir.absolutePath()); /* only succeeds once it is empty */
}

void
AutoUpdate::offerRestart()
{
    QMessageBox box(QMessageBox::Information, QString::fromLatin1(EMU_NAME),
                    tr("PeepeeBox %1 has been installed.\n\nRestart now to run it? The emulated cabinet is shut down as if you had quit, and the new release starts in its place.")
                        .arg(pending.version),
                    QMessageBox::NoButton, main_window);
    QPushButton *restart = box.addButton(tr("Restart now"), QMessageBox::AcceptRole);
    box.addButton(tr("Later"), QMessageBox::RejectRole);
    box.setDefaultButton(restart);
    box.exec();

    if (box.clickedButton() == restart) {
        restartWanted = true;
        main_window->quitForUpdate();
    }
}

bool
AutoUpdate::restartRequested()
{
    return restartWanted;
}

void
AutoUpdate::relaunch()
{
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty())
        args.removeFirst();
    const QString exe = QCoreApplication::applicationFilePath();
    pclog("Update: relaunching %s\n", exe.toUtf8().constData());
    if (!QProcess::startDetached(exe, args, QDir::currentPath()))
        pclog("Update: relaunch failed\n");
}
