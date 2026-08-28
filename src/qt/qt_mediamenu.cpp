/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Media menu UI module.
 *
 * Authors: Joakim L. Gilje <jgilje@jgilje.net>
 *          Cacodemon345
 *          Teemu Korhonen
 *
 *          Copyright 2021 Joakim L. Gilje
 *          Copyright 2021-2022 Cacodemon345
 *          Copyright 2021-2022 Teemu Korhonen
 */
#include "qt_preferences.hpp"
#include "qt_machinestatus.hpp"

#include <QMenu>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QStringBuilder>
#include <QApplication>
#include <QStyle>
#include <QDirIterator>
#include <QTextStream>

extern "C" {
#ifdef Q_OS_WINDOWS
#    define BITMAP WINDOWS_BITMAP
#    include <windows.h>
#    include <windowsx.h>
#    undef BITMAP
#endif
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/config.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/plat.h>
#include <86box/machine.h>
#include <86box/fdd.h>
#include <86box/fdd_86f.h>
#include <86box/cdrom.h>
#include <86box/scsi_device.h>
#include <86box/mo.h>
#include <86box/sound.h>
#include <86box/ui.h>
#include <86box/thread.h>
#include <86box/network.h>
};

#include "qt_util.hpp"
#include "qt_deviceconfig.hpp"
#include "qt_mediahistorymanager.hpp"
#include "qt_newfloppydialog.hpp"
#include "qt_mediamenu.hpp"
#include "qt_iconindicators.hpp"

std::shared_ptr<MediaMenu> MediaMenu::ptr;

static QSize pixmap_size(16, 16);

MediaMenu::MediaMenu(QWidget *parent)
    : QObject(parent)
{
    parentWidget = parent;
    connect(this, &MediaMenu::onCdromUpdateUi, this, &MediaMenu::cdromUpdateUi, Qt::QueuedConnection);
}

void
MediaMenu::refresh(QMenu *parentMenu)
{
    parentMenu->clear();

    floppyMenus.clear();
    if (fdd_get_type(0)) {
        const int i = 0;
        auto *menu     = parentMenu->addMenu("");
        QIcon img_icon = fdd_is_525(i) ? QIcon(":/settings/qt/icons/floppy_525_image.ico") : QIcon(":/settings/qt/icons/floppy_35_image.ico");
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, New), tr("&New image…"), [this, i]() { floppyNewImage(i); });
        menu->addSeparator();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Browse), tr("&Existing image…"), [this, i]() { floppySelectImage(i, false); });
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, WriteProtectedBrowse), tr("Existing image (&Write-protected)…"), [this, i]() { floppySelectImage(i, true); });
        menu->addSeparator();
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            floppyImageHistoryPos[slot] = menu->children().count();
            menu->addAction(img_icon, tr("Image %1").arg(slot), [this, i, slot]() { floppyMenuSelect(i, slot); })->setCheckable(false);
        }
        menu->addSeparator();
        /* PeepeeBox: no host floppy passthrough -- images only. */
        floppyExportPos = menu->children().count();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Export), tr("E&xport to 86F…"), [this, i]() { floppyExportTo86f(i); });
        menu->addSeparator();
        floppyEjectPos = menu->children().count();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Eject), tr("E&ject"), [this, i]() { floppyEject(i); });
        floppyMenus[i] = menu;
        floppyUpdateMenu(i);
    }

    cdromMenus.clear();
    MachineStatus::iterateCDROM([this, parentMenu](int i) {
        auto *menu        = parentMenu->addMenu("");
        int   t           = cdrom[i].type;
        QIcon img_icon    = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom_image.ico")  : QIcon(":/settings/qt/icons/cdrom_image.ico");
        QIcon folder_icon = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom_folder.ico") : QIcon(":/settings/qt/icons/cdrom_folder.ico");
        QIcon host_icon   = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom_host.ico")   : QIcon(":/settings/qt/icons/cdrom_host.ico");
        cdromMutePos = menu->children().count();
        menu->addAction(QIcon(":/settings/qt/icons/cdrom_mute.ico"), tr("&Mute"), [this, i]() { cdromMute(i); })->setCheckable(true);
        menu->addSeparator();
        menu->addAction(getIconWithIndicator(img_icon, pixmap_size, QIcon::Normal, Browse), tr("&Image…"), [this, i]() { cdromMount(i, 0, nullptr); })->setCheckable(false);
        menu->addAction(getIconWithIndicator(folder_icon, pixmap_size, QIcon::Normal, Browse), tr("&Folder…"), [this, i]() { cdromMount(i, 1, nullptr); })->setCheckable(false);
        menu->addSeparator();
        for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
            cdromImageHistoryPos[slot] = menu->children().count();
            menu->addAction(tr("Image %1").arg(slot), [this, i, slot]() { cdromReload(i, slot); })->setCheckable(false);
        }
        menu->addSeparator();
#ifdef Q_OS_WINDOWS
        /* Go through all active drive letters. */
        uint32_t drives = GetLogicalDrives();
        int letterIdx = 0;
        while (drives) {
            if (drives & 1) {
                auto letter = driveLetters.at(letterIdx);
                auto drive = QString("%1:\\").arg(letter);
                /* Check if the letter is a CD-ROM drive. */
                if (GetDriveTypeA(drive.toUtf8().constData()) == DRIVE_CDROM)
                    menu->addAction(host_icon, tr("&Host CD/DVD Drive (%1)").arg(QString(letter).append(':')), [this, i, letter] { cdromMount(i, 2, QString(R"(\\.\%1:)").arg(letter)); })->setCheckable(false);
            }
            drives >>= 1;
            letterIdx++;
        }
        menu->addSeparator();
#elif defined(Q_OS_LINUX)
        /* Go through all active block devices. */
        QDirIterator it("/sys/class/block", QDir::Dirs | QDir::NoDotAndDotDot);
        while (it.hasNext()) {
            auto dev = it.next();
            /* Check if the device is a CD-ROM drive. */
            QFile file(QString("%1/device/type").arg(dev));
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QTextStream textStream(&file);
                if (textStream.readLine() == "5") {
                    auto devName = it.fileName();
                    auto devPath = QString("/dev/%1").arg(devName);
                    if (QFile::exists(devPath))
                        menu->addAction(host_icon, tr("&Host CD/DVD Drive (%1)").arg(devName), [this, i, devPath] { cdromMount(i, 2, devPath); })->setCheckable(false);
                }
                file.close();
            }
        }
        menu->addSeparator();
#endif
        cdromEjectPos = menu->children().count();
        menu->addAction(tr("E&ject"), [this, i]() { cdromEject(i); })->setCheckable(false);
        cdromMenus[i] = menu;
        cdromUpdateMenu(i);
    });

    netMenus.clear();
    MachineStatus::iterateNIC([this, parentMenu](int i) {
        auto *menu    = parentMenu->addMenu("");
        netDisconnPos = menu->children().count();
        auto *action  = menu->addAction(tr("&Connected"), [this, i] { network_is_connected(i) ? nicDisconnect(i) : nicConnect(i); });
        action->setCheckable(true);
        netMenus[i] = menu;
        nicUpdateMenu(i);
    });
    parentMenu->addAction(tr("Clear image &history"), [this]() { clearImageHistory(); });
}

void
MediaMenu::floppyNewImage(int i)
{
    NewFloppyDialog dialog(NewFloppyDialog::MediaType::Floppy, parentWidget);
    switch (dialog.exec()) {
        default:
            break;
        case QDialog::Accepted:
            const QByteArray filename = dialog.fileName().toUtf8();
            floppyMount(i, filename, false);
            break;
    }
}

void
MediaMenu::floppySelectImage(int i, bool wp)
{
    auto filename = QFileDialog::getOpenFileName(
        parentWidget,
        QString(),
        getMediaOpenDirectory(),
        tr("All images") %
        util::DlgFilter({ "0??","1??","??0","86f","bin","cq?","d??","flp","hdm","im?","json","td0","*fd?","mfm","xdf" }) %
        tr("Advanced sector images") %
        util::DlgFilter({ "imd","json","td0" }) %
        tr("Basic sector images") %
        util::DlgFilter({ "0??","1??","??0","bin","cq?","d??","flp","hdm","im?","xdf","*fd?" }) %
        tr("Flux images") %
        util::DlgFilter({ "fdi" }) %
        tr("Surface images") %
        util::DlgFilter({ "86f","mfm" }) %
        tr("All files") %
        util::DlgFilter({ "*" }, true));

    if (!filename.isEmpty())
        floppyMount(i, filename, wp);
}

void
MediaMenu::floppyMount(int i, const QString &filename, bool wp)
{
    auto previous_image = QFileInfo(floppyfns[i]);
    fdd_close(i);
    ui_writeprot[i] = wp ? 1 : 0;
    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();

        if (filename.left(5) == "wp://")
            ui_writeprot[i] = 1;
        else if (ui_writeprot[i])
            filenameBytes = QString::asprintf(R"(wp://%s)", filename.toUtf8().data()).toUtf8();

        fdd_load(i, filenameBytes.data());
        mhm.addImageToHistory(i, ui::MediaType::Floppy, previous_image.filePath(), QString(filenameBytes));
    } else
        mhm.addImageToHistory(i, ui::MediaType::Floppy, previous_image.filePath(), filename);
    ui_sb_update_icon_state(SB_FLOPPY | i, drive_empty[i]);
    ui_sb_update_icon_wp(SB_FLOPPY | i, ui_writeprot[i]);
    floppyUpdateMenu(i);
    ui_sb_update_tip(SB_FLOPPY | i);
    config_save();
}

void
MediaMenu::floppyEject(int i)
{
    mhm.addImageToHistory(i, ui::MediaType::Floppy, floppyfns[i], QString());
    fdd_close(i);
    ui_sb_update_icon_state(SB_FLOPPY | i, 1);
    floppyUpdateMenu(i);
    ui_sb_update_tip(SB_FLOPPY | i);
    config_save();
}

void
MediaMenu::floppyExportTo86f(int i)
{
    auto filename = QFileDialog::getSaveFileName(parentWidget, QString(), QString(), tr("Surface images") % util::DlgFilter({ "86f" }, true));
    if (!filename.isEmpty()) {
        QByteArray filenameBytes = filename.toUtf8();
        plat_pause(1);
        if (d86f_export(i, filenameBytes.data()) == 0) {
            QMessageBox::critical(parentWidget, tr("Unable to write file"), tr("Make sure the file is being saved to a writable directory"));
        }
        plat_pause(0);
    }
}

void
MediaMenu::floppyUpdateMenu(int i)
{
    QString   name = floppyfns[i];
    QFileInfo fi(floppyfns[i]);

    if (!floppyMenus.contains(i))
        return;

    auto *menu   = floppyMenus[i];
    auto  childs = menu->children();

    auto *ejectMenu  = dynamic_cast<QAction *>(childs[floppyEjectPos]);
    auto *exportMenu = dynamic_cast<QAction *>(childs[floppyExportPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    ejectMenu->setText(name.isEmpty() ? tr("E&ject") : tr("E&ject %1").arg(fi.fileName()));
    exportMenu->setEnabled(!name.isEmpty());

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++) {
        updateImageHistory(i, slot, ui::MediaType::Floppy);
    }

    int type = fdd_get_type(i);
    floppyMenus[i]->setTitle(tr("&Floppy %1 (%2): %3").arg(QString::number(i + 1), fdd_getname(type), name.isEmpty() ? tr("(empty)") : name));
    floppyMenus[i]->setToolTip(tr("Floppy %1 (%2): %3").arg(QString::number(i + 1), fdd_getname(type), name.isEmpty() ? tr("(empty)") : name));
}

void
MediaMenu::floppyMenuSelect(int index, int slot)
{
    QString filename = mhm.getImageForSlot(index, slot, ui::MediaType::Floppy);
    floppyMount(index, filename, false);
    floppyUpdateMenu(index);
    ui_sb_update_tip(SB_FLOPPY | index);
}

void
MediaMenu::cdromMute(int i)
{
    cdrom[i].sound_on ^= 1;
    config_save();
    cdromUpdateMenu(i);
    sound_cd_thread_reset();
}

void
MediaMenu::cdromMount(int i, const QString &filename)
{
    QByteArray fn        = filename.toUtf8().data();
    int        was_empty = cdrom_is_empty(i);

    cdrom_exit(i);

    memset(cdrom[i].image_path, 0, sizeof(cdrom[i].image_path));
#ifdef Q_OS_WINDOWS
    if ((fn.data() != nullptr) && (strlen(fn.data()) >= 1) && (fn.data()[strlen(fn.data()) - 1] == '/'))
        fn.data()[strlen(fn.data()) - 1] = '\\';
#else
    if ((fn.data() != NULL) && (strlen(fn.data()) >= 1) && (fn.data()[strlen(fn.data()) - 1] == '\\'))
        fn.data()[strlen(fn.data()) - 1] = '/';
#endif
    cdrom_load(&(cdrom[i]), fn.data(), 1);

    /* Signal media change to the emulated machine. */
    if (cdrom[i].insert) {
        cdrom[i].insert(cdrom[i].priv);

        /* The drive was previously empty, transition directly to UNIT ATTENTION. */
        if (was_empty)
            cdrom[i].insert(cdrom[i].priv);
    }

    if (strlen(cdrom[i].image_path) > 0)
        ui_sb_update_icon_state(SB_CDROM | i, 0);
    else
        ui_sb_update_icon_state(SB_CDROM | i, 1);
    mhm.addImageToHistory(i, ui::MediaType::Optical, cdrom[i].prev_image_path, cdrom[i].image_path);

    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
    config_save();
}

void
MediaMenu::cdromMount(int i, int dir, const QString &arg)
{
    QString   filename;
    QFileInfo fi(cdrom[i].image_path);

    if (dir > 1)
        filename = QString::asprintf(R"(ioctl://%s)", arg.toUtf8().data());
    else if (dir == 1) {
        QFileDialog::Options options = QFileDialog::ShowDirsOnly;
#ifdef Q_OS_LINUX
        options |= QFileDialog::DontUseNativeDialog;
#endif
        filename = QFileDialog::getExistingDirectory(parentWidget, QString(), getMediaOpenDirectory(), options);
    }
    else {
        filename = QFileDialog::getOpenFileName(parentWidget, QString(),
                                                getMediaOpenDirectory(),
                                                tr("CD-ROM images") % util::DlgFilter({ "iso", "cue", "toc", "ccd", "mds", "mdx", "aaruf", "aaruformat", "aif", "chd" }) % tr("All files") % util::DlgFilter({ "*" }, true));
    }

    if (filename.isEmpty())
        return;

    cdromMount(i, filename);
}

void
MediaMenu::cdromEject(int i)
{
    mhm.addImageToHistory(i, ui::MediaType::Optical, cdrom[i].image_path, QString());
    cdrom_eject(i);
    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
}

void
MediaMenu::cdromReload(int index, int slot)
{
    const QString filename = mhm.getImageForSlot(index, slot, ui::MediaType::Optical);
    cdromMount(index, filename);
    cdromUpdateMenu(index);
    ui_sb_update_tip(SB_CDROM | index);
}

void
MediaMenu::cdromUpdateUi(int i)
{
    cdrom_t *drv = &cdrom[i];

    if (strlen(cdrom[i].image_path) == 0) {
        mhm.addImageToHistory(i, ui::MediaType::Optical, drv->prev_image_path, QString());
        ui_sb_update_icon_state(SB_CDROM | i, 1);
    } else {
        mhm.addImageToHistory(i, ui::MediaType::Optical, drv->prev_image_path, drv->image_path);
        ui_sb_update_icon_state(SB_CDROM | i, 0);
    }

    cdromUpdateMenu(i);
    ui_sb_update_tip(SB_CDROM | i);
}

void
MediaMenu::updateImageHistory(int index, int slot, ui::MediaType type)
{
    QMenu      *menu;
    QAction    *imageHistoryUpdatePos;
    QObjectList children;
    QFileInfo   fi;
    QIcon       menu_icon;
    const auto  fn = mhm.getImageForSlot(index, slot, type);

    QString menu_item_name;

    switch (type) {
        default:
            menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : fn;
            return;
        case ui::MediaType::Floppy:
            if (!floppyMenus.contains(index))
                return;
            menu                  = floppyMenus[index];
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[floppyImageHistoryPos[slot]]);
            menu_icon             = fdd_is_525(index) ? QIcon(":/settings/qt/icons/floppy_525_image.ico") : QIcon(":/settings/qt/icons/floppy_35_image.ico");
            if (fn.left(5) == "wp://")
                fi.setFile(fn.right(fn.length() - 5));
            else
                fi.setFile(fn);
            if (!fi.fileName().isEmpty() && (fn.left(5) == "wp://")) {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : "🔒 " + fn.right(fn.length() - 5);
                imageHistoryUpdatePos->setIcon(getIconWithIndicator(menu_icon, pixmap_size, QIcon::Normal, WriteProtected));
            } else {
                menu_item_name = fi.fileName().isEmpty() ? tr("Reload previous image") : fn;
                imageHistoryUpdatePos->setIcon(menu_icon);
            }
            break;
        case ui::MediaType::Optical: {
            if (!cdromMenus.contains(index))
                return;
            int   t           = cdrom[index].type;
            QIcon img_icon    = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom_image.ico")  : QIcon(":/settings/qt/icons/cdrom_image.ico");
            QIcon folder_icon = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom_folder.ico") : QIcon(":/settings/qt/icons/cdrom_folder.ico");
            QIcon host_icon   = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom_host.ico")   : QIcon(":/settings/qt/icons/cdrom_host.ico");
            menu                  = cdromMenus[index];
            children              = menu->children();
            imageHistoryUpdatePos = dynamic_cast<QAction *>(children[cdromImageHistoryPos[slot]]);
            if (fn.left(8) == "ioctl://") {
                menu_icon = host_icon;
#ifdef Q_OS_WINDOWS
                menu_item_name = tr("Host CD/DVD Drive (%1)").arg(fn.right(2));
#else
                menu_item_name = tr("Host CD/DVD Drive (%1)").arg(fn.right(fn.length() - 8));
#endif
            } else {
                fi.setFile(fn);
                menu_icon      = fi.isDir() ? folder_icon : img_icon;
                menu_item_name = fn.isEmpty() ? tr("Reload previous image") : fn;
            }
            imageHistoryUpdatePos->setIcon(menu_icon);
            break;
        }
    }

#ifndef Q_OS_MACOS
    if ((slot >= 0) && (slot <= 9))
        imageHistoryUpdatePos->setText(menu_item_name.prepend("&%1 ").arg((slot == 9) ? 0 : (slot + 1)));
    else
#endif
        imageHistoryUpdatePos->setText(menu_item_name);

    if (fn.left(8) == "ioctl://")
        imageHistoryUpdatePos->setVisible(true);
    else
        imageHistoryUpdatePos->setVisible(!fn.isEmpty() && fi.exists());
}

void
MediaMenu::clearImageHistory()
{
    mhm.clearImageHistory();
    ui_sb_update_panes();
}

void
MediaMenu::cdromUpdateMenu(int i)
{
    QString name = cdrom[i].image_path;
    QString name2;
    QIcon   menu_icon;

    if (!cdromMenus.contains(i))
        return;
    auto *menu   = cdromMenus[i];
    auto  childs = menu->children();

    int   t           = cdrom[i].type;
    QIcon img_icon    = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom_image.ico")  : QIcon(":/settings/qt/icons/cdrom_image.ico");
    QIcon folder_icon = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom_folder.ico") : QIcon(":/settings/qt/icons/cdrom_folder.ico");
    QIcon host_icon   = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom_host.ico")   : QIcon(":/settings/qt/icons/cdrom_host.ico");
    QIcon drv_icon    = cdrom_is_dvd(t) ? QIcon(":/settings/qt/icons/dvdrom.ico")        : QIcon(":/settings/qt/icons/cdrom.ico");

    auto *muteMenu = dynamic_cast<QAction *>(childs[cdromMutePos]);
    muteMenu->setIcon(QIcon((cdrom[i].sound_on == 0) ? ":/settings/qt/icons/cdrom_unmute.ico" : ":/settings/qt/icons/cdrom_mute.ico"));
    muteMenu->setText((cdrom[i].sound_on == 0) ? tr("&Unmute") : tr("&Mute"));

    auto *ejectMenu = dynamic_cast<QAction *>(childs[cdromEjectPos]);
    ejectMenu->setEnabled(!name.isEmpty());
    QString menu_item_name;
    if (name.left(8) == "ioctl://") {
#ifdef Q_OS_WINDOWS
        menu_item_name = tr("Host CD/DVD Drive (%1)").arg(name.right(2));
#else
        menu_item_name = tr("Host CD/DVD Drive (%1)").arg(name.right(name.length() - 8));
#endif
        name2     = menu_item_name;
        menu_icon = host_icon;
    } else {
        QFileInfo fi(cdrom[i].image_path);

        menu_item_name = name.isEmpty() ? QString() : fi.fileName();
        name2          = name;
        if (name.isEmpty())
            menu_icon = drv_icon;
        else
            menu_icon = fi.isDir() ? folder_icon : img_icon;
    }
    ejectMenu->setIcon(getIconWithIndicator(menu_icon, pixmap_size, QIcon::Normal, Eject));
    ejectMenu->setText(name.isEmpty() ? tr("E&ject") : tr("E&ject %1").arg(menu_item_name));

    for (int slot = 0; slot < MAX_PREV_IMAGES; slot++)
        updateImageHistory(i, slot, ui::MediaType::Optical);

    QString busName = tr("Unknown Bus");
    switch (cdrom[i].bus_type) {
        default:
            break;
        case CDROM_BUS_ATAPI:
            busName = "ATAPI";
            break;
        case CDROM_BUS_SCSI:
            busName = "SCSI";
            break;
        case CDROM_BUS_MITSUMI:
            busName = "Mitsumi";
            break;
        case CDROM_BUS_MKE:
            busName = "Panasonic/MKE";
            break;
    }

    menu->setTitle(tr("&CD-ROM %1 (%2): %3").arg(QString::number(i + 1), busName, name.isEmpty() ? tr("(empty)") : name2));
    menu->setToolTip(tr("CD-ROM %1 (%2): %3").arg(QString::number(i + 1), busName, name.isEmpty() ? tr("(empty)") : name2));
}

void
MediaMenu::nicConnect(int i)
{
    network_connect(i, 1);
    ui_sb_update_icon_state(SB_NETWORK | i, 0);
    nicUpdateMenu(i);
    config_save();
}

void
MediaMenu::nicDisconnect(int i)
{
    network_connect(i, 0);
    ui_sb_update_icon_state(SB_NETWORK | i, 1);
    nicUpdateMenu(i);
    config_save();
}

void
MediaMenu::nicUpdateMenu(int i)
{
    if (!netMenus.contains(i))
        return;

    QString netType;
    switch (net_cards_conf[i].net_type) {
        default:
            netType = tr("Null Driver");
            break;
        case NET_TYPE_SLIRP:
            netType = "SLiRP";
            break;
        case NET_TYPE_PCAP:
            netType = "PCAP";
            break;
        case NET_TYPE_VDE:
            netType = "VDE";
            break;
        case NET_TYPE_TAP:
            netType = "TAP";
            break;
        case NET_TYPE_NLSWITCH:
            netType = tr("Local Switch");
            break;
        case NET_TYPE_NRSWITCH:
            netType = tr("Remote Switch");
            break;
    }

    QString devName = DeviceConfig::DeviceName(network_card_getdevice(net_cards_conf[i].device_num), network_card_get_internal_name(net_cards_conf[i].device_num), 1);

    auto *menu            = netMenus[i];
    auto  childs          = menu->children();
    auto *connectedAction = dynamic_cast<QAction *>(childs[netDisconnPos]);
    connectedAction->setChecked(network_is_connected(i));

    menu->setTitle(tr("&NIC %1 (%2) %3").arg(QString::number(i + 1), netType, devName));
    menu->setToolTip(tr("NIC %1 (%2) %3").arg(QString::number(i + 1), netType, devName));
}

QString
MediaMenu::getMediaOpenDirectory()
{
    static bool firstCall = true;
    QString     openDirectory;

    if (open_dir_usr_path > 0 && firstCall) {
        openDirectory = QString::fromUtf8(usr_path);
        firstCall     = false;
    }

    return openDirectory;
}

// callbacks from 86box C code
extern "C" {
void
floppy_mount(uint8_t id, char *fn, uint8_t wp)
{
    MediaMenu::ptr->floppyMount(id, QString(fn), wp);
}

void
floppy_eject(uint8_t id)
{
    MediaMenu::ptr->floppyEject(id);
}

void
cdrom_mount(uint8_t id, char *fn)
{
    MediaMenu::ptr->cdromMount(id, QString(fn));
}

void
plat_cdrom_ui_update(uint8_t id, uint8_t reload)
{
    emit MediaMenu::ptr->onCdromUpdateUi(id);
}

}
