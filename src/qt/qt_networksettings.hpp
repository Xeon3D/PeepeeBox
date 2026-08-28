/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its two protection tokens.
 *
 *             Network configuration dialog.
 *
 *             Upstream reached the network card settings through one page of an
 *             eleven-page machine settings dialog.  Every other page of that
 *             dialog is fixed by the Photo Play profile, so the dialog is gone
 *             and this hosts the surviving page on its own.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#ifndef QT_NETWORKSETTINGS_HPP
#define QT_NETWORKSETTINGS_HPP

#include <QDialog>

class SettingsNetwork;

class NetworkSettings : public QDialog {
    Q_OBJECT

public:
    explicit NetworkSettings(QWidget *parent = nullptr);

private:
    SettingsNetwork *page;
};

#endif // QT_NETWORKSETTINGS_HPP
