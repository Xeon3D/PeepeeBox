/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             The hard disk image manager.
 *
 *             A cabinet library is a folder of rigs -- one folder per image,
 *             each holding its image next to its own nvr/ and roms/ -- so the
 *             scan walks the tree rather than listing one directory, and it
 *             looks for any .img rather than only HardDisk.img, because plenty
 *             of them are named something else.
 *
 *             What each image *is* comes out of the image, never out of the
 *             folder name.  photoplay_identify_ex() reads the release and the
 *             territory from \FOTO\SETTINGS\MAIN.SET, and photoplay_image_nsb()
 *             reads the build number from \MENU\NSB.NR, so an image nobody has
 *             seen before is identified on exactly the same terms as a known
 *             one.  Folder names in circulation are frequently wrong -- images
 *             filed under one release that are another, filed under one country
 *             that are another -- so they are never consulted.
 *
 *             Identifying an image means opening it and walking its FAT, which
 *             is quick for one and adds up over a library on an external disk.
 *             So the answer is written to a list file and read back the next
 *             time the window opens; "Update list" is the same scan again, over
 *             the top of what is already there.
 */
#include "qt_hddmanager.hpp"
#include "ui_qt_hddmanager.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QLocale>
#include <QMessageBox>
#include <QProgressDialog>
#include <QSettings>
#include <QTreeWidgetItem>

extern "C" {
#include <86box/86box.h>
#include <86box/photoplay.h>
}

/* The library is a few levels deep in practice: generation, then rig, then the
   image.  The cap is here so that pointing the setting at a whole drive by
   mistake cannot turn the scan into a full-disk walk. */
#define PP_SCAN_MAX_DEPTH 6

/* Enough for the longest banner MAIN.SET carries, with room to spare. */
#define PP_IDENT_BUF      160

/* Bumped whenever what a scan records changes -- a new field, or a change to
   how an existing one is spelled.  A list written by an older build is thrown
   away rather than shown, because a stale list is worse than no list: it looks
   authoritative while naming releases the way the build no longer does. */
#define PP_LIST_VERSION   2

enum {
    COL_RELEASE = 0,
    COL_TERRITORY,
    COL_NSB,
    COL_PATH,
    COL_SIZE,
    COL_COUNT
};

namespace {

/* The size column shows "1.54 GiB" but has to sort on the byte count behind it,
   or a 512 MB image sorts above a 1.6 GB one. */
class ImageItem : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;

    bool operator<(const QTreeWidgetItem &other) const override
    {
        const int column = treeWidget() ? treeWidget()->sortColumn() : 0;

        if (column == COL_SIZE)
            return data(COL_SIZE, Qt::UserRole).toLongLong()
                 < other.data(COL_SIZE, Qt::UserRole).toLongLong();

        return QTreeWidgetItem::operator<(other);
    }
};

/* photoplay_identify_ex() names the release with its territory on the end --
   "IGO 5 PT", "Junior 1.5 (NL)" -- because that is what belongs in a window
   title.  The list has a column for each, so take the territory back off.

   Some releases name a country in the release itself rather than as a suffix
   ("I.G.O. 8 Italy"); those match neither shape and are left whole, which is
   right -- it is the release that is Italian, not this copy of it. */
QString
bareRelease(const QString &label, const QString &territory)
{
    if (territory.isEmpty() || label.isEmpty())
        return label;

    const QString suffixed  = QStringLiteral(" ") + territory;
    const QString bracketed = QStringLiteral(" (") + territory + QStringLiteral(")");

    if (label.endsWith(bracketed))
        return label.left(label.size() - bracketed.size());

    if (label.endsWith(suffixed))
        return label.left(label.size() - suffixed.size());

    return label;
}

}

HddManager::HddManager(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::HddManager)
{
    ui->setupUi(this);

    ui->treeImages->setHeaderLabels({ tr("Version"), tr("Territory"), tr("NSB"),
                                      tr("Path"), tr("Size") });
    ui->treeImages->setRootIsDecorated(false);
    ui->treeImages->header()->setStretchLastSection(false);
    ui->treeImages->header()->setSectionResizeMode(COL_PATH, QHeaderView::Stretch);
    ui->treeImages->sortByColumn(COL_RELEASE, Qt::AscendingOrder);

    loadList();
    populate();
    refreshHeader();
}

HddManager::~HddManager()
{
    delete ui;
}

/* The list lives beside the configuration file rather than inside the image
   folder: that folder is the library and may sit on read-only or removable
   media, while the folder PeepeeBox runs from is writable by definition.  It
   is a cache of what was seen on disk, not a setting, so it stays a file of
   its own rather than going into 86box.cfg. */
QString
HddManager::listFilePath() const
{
    const QFileInfo settings{ QString::fromUtf8(cfg_path) };

    return settings.dir().filePath(QStringLiteral("hddimages.ini"));
}

void
HddManager::loadList()
{
    entries.clear();
    scannedFolder.clear();
    scannedWhen.clear();

    QSettings list(listFilePath(), QSettings::IniFormat);

    if (list.value(QStringLiteral("version")).toInt() != PP_LIST_VERSION)
        return;

    scannedFolder = list.value(QStringLiteral("folder")).toString();
    scannedWhen   = list.value(QStringLiteral("scanned")).toString();

    /* A list describing a different folder is not this one's list.  Drop it
       rather than showing rows pointing somewhere the user has moved on from;
       the next scan writes a fresh one. */
    const QString folder = QString::fromUtf8(hdd_images_path);

    if (!scannedFolder.isEmpty() && !folder.isEmpty() &&
        (QDir::cleanPath(scannedFolder) != QDir::cleanPath(folder))) {
        scannedFolder.clear();
        scannedWhen.clear();
        return;
    }

    const int count = list.beginReadArray(QStringLiteral("images"));

    for (int i = 0; i < count; i++) {
        list.setArrayIndex(i);

        Entry e;
        e.path      = list.value(QStringLiteral("path")).toString();
        e.release   = list.value(QStringLiteral("release")).toString();
        e.territory = list.value(QStringLiteral("territory")).toString();
        e.nsb       = list.value(QStringLiteral("nsb")).toString();
        e.banner    = list.value(QStringLiteral("banner")).toString();
        e.size      = list.value(QStringLiteral("size")).toLongLong();

        if (!e.path.isEmpty())
            entries.append(e);
    }

    list.endArray();
}

void
HddManager::saveList()
{
    QSettings list(listFilePath(), QSettings::IniFormat);

    list.clear();
    list.setValue(QStringLiteral("version"), PP_LIST_VERSION);
    list.setValue(QStringLiteral("folder"), scannedFolder);
    list.setValue(QStringLiteral("scanned"), scannedWhen);

    list.beginWriteArray(QStringLiteral("images"), entries.size());

    for (int i = 0; i < entries.size(); i++) {
        list.setArrayIndex(i);
        list.setValue(QStringLiteral("path"), entries[i].path);
        list.setValue(QStringLiteral("release"), entries[i].release);
        list.setValue(QStringLiteral("territory"), entries[i].territory);
        list.setValue(QStringLiteral("nsb"), entries[i].nsb);
        list.setValue(QStringLiteral("banner"), entries[i].banner);
        list.setValue(QStringLiteral("size"), entries[i].size);
    }

    list.endArray();

    list.sync();
}

void
HddManager::populate()
{
    ui->treeImages->setSortingEnabled(false);
    ui->treeImages->clear();

    for (const Entry &e : entries) {
        auto *item = new ImageItem(ui->treeImages);

        item->setText(COL_RELEASE, e.release);
        item->setText(COL_TERRITORY, e.territory);
        item->setText(COL_NSB, e.nsb);
        item->setText(COL_PATH, QDir::toNativeSeparators(e.path));
        item->setText(COL_SIZE, QLocale().formattedDataSize(e.size));

        item->setData(COL_RELEASE, Qt::UserRole, e.path);
        item->setData(COL_SIZE, Qt::UserRole, e.size);

        if (!e.banner.isEmpty())
            item->setToolTip(COL_RELEASE, e.banner);

        item->setToolTip(COL_PATH, QDir::toNativeSeparators(e.path));
    }

    ui->treeImages->setSortingEnabled(true);

    for (int c = 0; c < COL_COUNT; c++)
        if (c != COL_PATH)
            ui->treeImages->resizeColumnToContents(c);
}

/* The folder line, the status line, and what the scan button calls itself: it
   is "Scan for HDD images" until there is a list, and "Update list" once there
   is one to update. */
void
HddManager::refreshHeader()
{
    const QString folder = QString::fromUtf8(hdd_images_path);

    if (folder.isEmpty()) {
        ui->labelFolder->setText(tr("No hard disk images folder is set. Choose one on the "
                                    "Machine Manager page in Preferences."));
        ui->pushButtonScan->setEnabled(false);
    } else if (!QFileInfo(folder).isDir()) {
        ui->labelFolder->setText(tr("Images folder: %1 (this folder does not exist)")
                                     .arg(QDir::toNativeSeparators(folder)));
        ui->pushButtonScan->setEnabled(false);
    } else {
        ui->labelFolder->setText(tr("Images folder: %1").arg(QDir::toNativeSeparators(folder)));
        ui->pushButtonScan->setEnabled(true);
    }

    if (entries.isEmpty()) {
        ui->pushButtonScan->setText(tr("Scan for HDD images"));
        ui->labelStatus->setText(tr("No images listed yet."));
    } else {
        ui->pushButtonScan->setText(tr("Update list"));

        if (scannedWhen.isEmpty())
            ui->labelStatus->setText(tr("%1 images listed.").arg(entries.size()));
        else
            ui->labelStatus->setText(tr("%1 images listed, last scanned %2.")
                                         .arg(entries.size())
                                         .arg(scannedWhen));
    }

    on_treeImages_itemSelectionChanged();
}

/* Walk the library for disk images, then ask each one what it is.  The walk is
   cheap and the identification is not, so they are separate passes and the
   progress bar can count something real. */
bool
HddManager::scan()
{
    const QString root = QString::fromUtf8(hdd_images_path);

    if (root.isEmpty() || !QFileInfo(root).isDir())
        return false;

    /* Pass one: collect candidates, breadth-first with a depth cap. */
    QStringList candidates;
    QStringList level;

    level << root;

    for (int depth = 0; (depth <= PP_SCAN_MAX_DEPTH) && !level.isEmpty(); depth++) {
        QStringList next;

        for (const QString &dirPath : level) {
            QDir dir(dirPath);

            const auto files = dir.entryInfoList({ QStringLiteral("*.img") },
                                                 QDir::Files | QDir::Readable, QDir::Name);

            for (const QFileInfo &fi : files)
                candidates << fi.absoluteFilePath();

            const auto subdirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                                   QDir::Name);

            for (const QFileInfo &fi : subdirs)
                next << fi.absoluteFilePath();
        }

        level = next;
    }

    /* Pass two: ask each candidate what it is.  Anything that cannot answer is
       not a Photo Play image, and is left out rather than listed as unknown. */
    QProgressDialog progress(tr("Identifying hard disk images..."), tr("Cancel"),
                             0, candidates.size(), this);

    progress.setWindowTitle(tr("Scanning"));
    progress.setWindowModality(Qt::WindowModal);
    /* Up straight away rather than after the usual delay: reading the FAT of
       every image in a library takes long enough to look like a hang. */
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);
    progress.show();
    QApplication::processEvents();

    QList<Entry> found;

    for (int i = 0; i < candidates.size(); i++) {
        const QFileInfo fi(candidates[i]);

        progress.setLabelText(tr("Identifying image %1 of %2:\n%3")
                                  .arg(i + 1)
                                  .arg(candidates.size())
                                  .arg(QDir::toNativeSeparators(fi.absoluteFilePath())));
        progress.setValue(i);
        QApplication::processEvents();

        if (progress.wasCanceled())
            return false;

        const QByteArray imagePath = fi.absoluteFilePath().toUtf8();

        char label[PP_IDENT_BUF]  = { 0 };
        char banner[PP_IDENT_BUF] = { 0 };
        char terr[32]             = { 0 };
        char nsb[64]              = { 0 };

        if (!photoplay_identify_ex(imagePath.constData(), label, sizeof(label),
                                   banner, sizeof(banner), terr, sizeof(terr)))
            continue;

        /* A second walk of the same image, for the one field MAIN.SET does not
           carry.  Not every image has one, and a missing NSB is not a reason to
           leave the image out -- the path still tells it apart. */
        photoplay_image_nsb(imagePath.constData(), nsb, sizeof(nsb));

        Entry e;

        e.path      = fi.absoluteFilePath();
        e.territory = QString::fromUtf8(terr);
        e.release   = bareRelease(QString::fromUtf8(label), e.territory);
        e.nsb       = QString::fromUtf8(nsb);
        e.banner    = QString::fromUtf8(banner);
        e.size      = fi.size();

        found.append(e);
    }

    progress.setValue(candidates.size());
    progress.close();

    entries       = found;
    scannedFolder = QDir::cleanPath(root);
    scannedWhen   = QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat);

    return true;
}

void
HddManager::on_pushButtonScan_clicked()
{
    if (!scan())
        return;

    saveList();
    populate();
    refreshHeader();
}

/* Nothing can be loaded until a row is picked. */
void
HddManager::on_treeImages_itemSelectionChanged()
{
    const auto picked = ui->treeImages->selectedItems();

    ui->pushButtonLoad->setEnabled(!picked.isEmpty() &&
                                   !picked.first()->data(COL_RELEASE, Qt::UserRole).toString().isEmpty());
}

/* Take the row as the choice.  The dialog only reports it -- swapping the disk
   and restarting the machine is the main window's job, alongside the other
   things that reset the cabinet. */
void
HddManager::choose(QTreeWidgetItem *item)
{
    if (item == nullptr)
        return;

    const QString path = item->data(COL_RELEASE, Qt::UserRole).toString();

    if (path.isEmpty())
        return;

    /* The list can outlive the images it describes. */
    if (!QFileInfo(path).isFile()) {
        QMessageBox::warning(this, tr("Machine Manager"),
                             tr("%1 is no longer there.\n\nUpdate the list to see what "
                                "the folder holds now.")
                                 .arg(QDir::toNativeSeparators(path)));
        return;
    }

    selected = path;

    accept();
}

void
HddManager::on_pushButtonLoad_clicked()
{
    const auto picked = ui->treeImages->selectedItems();

    if (!picked.isEmpty())
        choose(picked.first());
}

void
HddManager::on_treeImages_itemDoubleClicked(QTreeWidgetItem *item, int column)
{
    (void) column;

    choose(item);
}
