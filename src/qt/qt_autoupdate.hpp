/*
 * PeepeeBox   A funworld Photo Play / I.G.O. arcade cabinet emulator,
 *             forked from 86Box.
 *
 *             Automatic updates from the GitHub releases page.
 *
 *             At start-up and on a schedule, both chosen in Preferences, the
 *             latest release is looked up.  When it is newer than the running
 *             build the user is told which one it is and asked; the
 *             Preferences page says the same and has an Update button.  The
 *             update itself downloads the archive for this platform and
 *             installs it over the folder the executable lives in, while the
 *             program is running: every file being replaced is renamed aside
 *             first, which Windows allows even for the executable that is
 *             executing, and the new one is moved into its place.  A restart
 *             is then offered; declining it is fine, since the next start is
 *             the new release either way.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 */
#ifndef QT_AUTOUPDATE_HPP
#define QT_AUTOUPDATE_HPP

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

class QFile;
class QNetworkReply;

class AutoUpdate final : public QObject {
    Q_OBJECT

public:
    explicit AutoUpdate(QObject *parent = nullptr);
    ~AutoUpdate() override;

    static AutoUpdate *instance();

    /* Who asked for a look.  A look from the Preferences page reports on the
       page and leaves the Update button to the user; the other two put up
       the question themselves, since nobody is watching the page. */
    enum class Trigger {
        Manual,
        Startup,
        Scheduled,
    };

    /* Arm the start-up look and the schedule.  Nothing touches the network
       before the first tick, which is a little after start-up so the machine
       is up first. */
    void start();

    /* Look now, whatever the schedule says.  What happens is reported through
       status() and the log. */
    void checkNow(Trigger trigger);

    /* A newer release the last look found, and not yet installed. */
    bool    hasAvailable() const { return !available.version.isEmpty(); }
    QString availableVersion() const { return available.version; }

    /* Its release notes, as written on the release page: Markdown. */
    QString availableNotes() const { return available.notes; }

    /* Download and install it. */
    void installAvailable();

    bool busy() const { return state != State::Idle; }

    /* The last line reported, so the Preferences page can show it on opening. */
    QString lastStatusText() const { return lastStatus; }

    /* The number this build carries, with the local-build "+" stripped, or an
       empty string for a "dev" build that has no number to compare. */
    static QString currentRelease();

    /* Where the release archives and the API live. */
    static QString repository();

    /* Remove whatever a previous update could not delete while it was the
       running program.  Called once at start-up, before the schedule. */
    static void cleanupLeftovers();

    /* A restart was accepted after an install: the main loop relaunches the
       executable once the emulator has shut down cleanly. */
    static bool restartRequested();
    static void relaunch();

signals:
    /* Progress and results in one line, for the Preferences page. */
    void status(const QString &text);

    /* hasAvailable() or busy() changed: the page's buttons follow. */
    void availableChanged();

private slots:
    void tick();
    void onReleaseReply();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadReadyRead();
    void onDownloadFinished();

private:
    enum class State {
        Idle,
        Checking,
        Downloading,
        Installing,
    };

    struct Release {
        QString tag;      /* "v1.9.2" */
        QString version;  /* "1.9.2" */
        QString name;     /* "Release 1.9.2" */
        QString pageUrl;
        QString notes;    /* the release's body, Markdown */
        QString assetName;
        QUrl    assetUrl;
        qint64  assetSize = 0;
    };

    static qint64  intervalSeconds(int setting);
    static QString assetForThisPlatform();
    static int     versionCompare(const QString &a, const QString &b);
    static QString updateDir();

    void    report(const QString &text);
    void    finishCheck(bool stamp);
    void    askToUpdate();
    void    beginDownload();
    bool    install(const QString &archivePath, QString &error);
    void    offerRestart();

    QNetworkAccessManager nam;
    QTimer                timer;
    State                 state       = State::Idle;
    Trigger               trigger     = Trigger::Manual;
    bool                  startupDone = false;
    qint64                retryAfter  = 0; /* Unix seconds; a failed look backs off */
    int                   lastPercent = -1;
    Release               available; /* found, not installed */
    Release               pending;   /* being downloaded and installed */
    QNetworkReply        *reply       = nullptr;
    QFile                *download    = nullptr;
    QString               lastStatus;

    static AutoUpdate *self;
    static bool        restartWanted;
};

#endif
