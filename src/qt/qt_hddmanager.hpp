/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             Header for the hard disk image manager.
 */
#ifndef QT_HDDMANAGER_HPP
#define QT_HDDMANAGER_HPP

#include <QDialog>
#include <QList>
#include <QString>
#include <QTreeWidgetItem>

namespace Ui {
class HddManager;
}

class HddManager : public QDialog {
    Q_OBJECT

public:
    explicit HddManager(QWidget *parent = nullptr);
    ~HddManager();

    /* The image the user chose, once the dialog has been accepted.  Empty if it
       was closed without choosing one. */
    QString selectedImage() const { return selected; }

    /* One row of the list: an image, and what it said about itself when it was
       last looked at.  Everything here survives in the list file.  All of it is
       read out of the image -- nothing is inferred from the folder it sits in,
       because those names are often wrong. */
    struct Entry {
        QString path;      /* full path to the image itself                   */
        QString release;   /* the release on its own, "IGO 1"                 */
        QString territory; /* the bare territory code, "NL"                   */
        QString nsb;       /* the NSB number out of MENU\NSB.NR, or empty     */
        QString banner;    /* the MAIN.SET Version line, verbatim             */
        qint64  size = 0;  /* bytes, so a truncated image is visible          */
    };

private slots:
    void on_pushButtonScan_clicked();
    void on_pushButtonLoad_clicked();
    void on_treeImages_itemDoubleClicked(QTreeWidgetItem *item, int column);
    void on_treeImages_itemSelectionChanged();

private:
    void    loadList();
    void    saveList();
    void    populate();
    void    refreshHeader();
    void    choose(QTreeWidgetItem *item);
    QString listFilePath() const;
    bool    scan();

    Ui::HddManager *ui;
    QList<Entry>    entries;
    QString         scannedFolder;
    QString         scannedWhen;
    QString         selected;
};

#endif // QT_HDDMANAGER_HPP
