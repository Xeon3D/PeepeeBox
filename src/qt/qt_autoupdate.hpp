/*
 * PeepeeBox   A funworld Photo Play / I.G.O. arcade cabinet emulator,
 *             forked from 86Box.
 *
 *             Automatic updates from the GitHub releases page.
 *
 *             On a schedule the user picks in Preferences (hourly, daily,
 *             weekly, monthly, or never), the latest release is looked up,
 *             and when it is newer than the running build its archive for
 *             this platform is downloaded and installed over the folder the
 *             executable lives in.  The install happens while the program is
 *             running: every file being replaced is renamed aside first, which
 *             Windows allows even for the executable that is executing, and
 *             the new one is moved into its place.  The user is then offered a
 *             restart; declining it is fine, since the next start is the new
 *             release either way.
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

    /* Arm the schedule.  Nothing touches the network before the first tick,
       which is a little after start-up so the machine is up first. */
    void start();

    /* Look now, whatever the schedule says.  What happens is reported through
       status() and the log; only a finished install puts up a dialog. */
    void checkNow();

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
    void    beginDownload();
    bool    install(const QString &archivePath, QString &error);
    void    offerRestart();

    QNetworkAccessManager nam;
    QTimer                timer;
    State                 state       = State::Idle;
    qint64                retryAfter  = 0; /* Unix seconds; a failed look backs off */
    int                   lastPercent = -1;
    Release               pending;
    QNetworkReply        *reply       = nullptr;
    QFile                *download    = nullptr;
    QString               lastStatus;

    static AutoUpdate *self;
    static bool        restartWanted;
};

#endif
