#pragma once

#include <memory>
#include <QObject>
#include <QMap>
#include "qt_mediahistorymanager.hpp"

extern "C" {
#include <86box/86box.h>
}
class QMenu;

class MediaMenu : public QObject {
    Q_OBJECT
public:
    MediaMenu(QWidget *parent);

    void refresh(QMenu *parentMenu);

    static std::shared_ptr<MediaMenu> ptr;




    void floppyNewImage(int i);
    void floppySelectImage(int i, bool wp);
    void floppyMount(int i, const QString &filename, bool wp);
    void floppyEject(int i);
    void floppyMenuSelect(int index, int slot);
    void floppyExportTo86f(int i);
    void floppyUpdateMenu(int i);

    void cdromMute(int i);
    void cdromMount(int i, int dir, const QString &arg);
    void cdromMount(int i, const QString &filename);
    void cdromEject(int i);
    void cdromReload(int index, int slot);
    void updateImageHistory(int index, int slot, ui::MediaType type);
    void clearImageHistory();
    void cdromUpdateMenu(int i);




    void nicConnect(int i);
    void nicDisconnect(int i);
    void nicUpdateMenu(int i);

public slots:
    void cdromUpdateUi(int i);

signals:
    void onCdromUpdateUi(int i);

private:
    QWidget *parentWidget = nullptr;

    QMap<int, QMenu *> floppyMenus;
    QMap<int, QMenu *> cdromMenus;
    QMap<int, QMenu *> netMenus;

    QString                 getMediaOpenDirectory();
    ui::MediaHistoryManager mhm;

    const QByteArray driveLetters = QByteArrayLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ      ");




    int floppyExportPos;
    int floppyEjectPos;
    int floppyImageHistoryPos[MAX_PREV_IMAGES];

    int cdromMutePos;
    int cdromEjectPos;
    int cdromImageHistoryPos[MAX_PREV_IMAGES];




    int netDisconnPos;

    friend class MachineStatus;
};
