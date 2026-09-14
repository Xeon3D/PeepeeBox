/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             Network configuration dialog.
 *
 * Authors:    The HUEG PP team.
 *
 *             Released under the GNU General Public License version 2 or
 *             later.  See COPYING for more information.
 */
#include <QDialogButtonBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "qt_networksettings.hpp"
#include "qt_settingsnetwork.hpp"

#include <cstddef>
#include <cstdint>

extern "C" {
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/network.h>
#include <86box/photoplay.h>
}

NetworkSettings::NetworkSettings(QWidget *parent)
    : QDialog(parent)
    , page(new SettingsNetwork(this))
{
    setWindowTitle(tr("Network"));

    const auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    const auto layout = new QVBoxLayout(this);
    layout->addWidget(page);
    layout->addWidget(buttons);

    /* SettingsNetwork::save() writes straight into net_cards_conf[], so it must
       only run when the user accepts.  NIC 1 is also the switch for the card
       being fitted at all -- "None" there leaves the cabinet without one -- and
       the profile reads that back from [Photo Play] at the next start. */
    connect(this, &QDialog::accepted, this, [this]() {
        page->save(0);
        photoplay_set_net_enabled(net_cards_conf[0].device_num > 0);
    });
}
