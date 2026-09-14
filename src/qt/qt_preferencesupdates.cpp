/*
 * PeepeeBox   A funworld Photo Play / I.G.O. arcade cabinet emulator,
 *             forked from 86Box.
 *
 *             The Updates preferences page: when to look for a new release,
 *             a look right now, and the Update button for one that was found.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 */
#include "qt_preferencesupdates.hpp"
#include "ui_qt_preferencesupdates.h"
#include "qt_autoupdate.hpp"

#include <QDateTime>
#include <QLocale>

extern "C" {
#include <86box/86box.h>
#include <86box/version.h>
}

PreferencesUpdates::PreferencesUpdates(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesUpdates)
{
    ui->setupUi(this);

    ui->checkBoxOnStartup->setChecked(update_on_startup > 0);
    /* The combo's rows are the setting's values in order: never, hourly,
       daily, weekly, monthly. */
    ui->comboBoxInterval->setCurrentIndex(update_check);

    if (auto *updater = AutoUpdate::instance()) {
        ui->labelStatus->setText(updater->lastStatusText());
        connect(updater, &AutoUpdate::status, this, &PreferencesUpdates::showStatus);
        connect(updater, &AutoUpdate::availableChanged, this, &PreferencesUpdates::refresh);
    }

    refresh();
}

PreferencesUpdates::~PreferencesUpdates()
{
    delete ui;
}

/* The versions line and the buttons follow the updater: which release this
   is, which one is waiting if any, and whether anything is going on. */
void
PreferencesUpdates::refresh()
{
    auto *updater = AutoUpdate::instance();
    const QString current = QString::fromLatin1(PEEPEEBOX_RELEASE);

    if (updater == nullptr) {
        ui->labelVersions->setText(tr("This is PeepeeBox %1.").arg(current));
        ui->pushButtonCheckNow->setEnabled(false);
        ui->pushButtonUpdate->setEnabled(false);
    } else if (updater->hasAvailable()) {
        ui->labelVersions->setText(tr("This is PeepeeBox %1. Release %2 is available.").arg(current, updater->availableVersion()));
        ui->pushButtonUpdate->setText(tr("Update to %1").arg(updater->availableVersion()));
        ui->pushButtonCheckNow->setEnabled(!updater->busy());
        ui->pushButtonUpdate->setEnabled(!updater->busy());
    } else {
        ui->labelVersions->setText(tr("This is PeepeeBox %1.").arg(current));
        ui->pushButtonUpdate->setText(tr("Update"));
        ui->pushButtonCheckNow->setEnabled(!updater->busy());
        ui->pushButtonUpdate->setEnabled(false);
    }

    ui->labelLastCheck->setText(update_last_check > 0
        ? tr("Last checked: %1").arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(update_last_check), QLocale::ShortFormat))
        : tr("Last checked: never"));
}

void
PreferencesUpdates::showStatus(const QString &text)
{
    ui->labelStatus->setText(text);
}

void
PreferencesUpdates::on_pushButtonCheckNow_clicked()
{
    if (auto *updater = AutoUpdate::instance())
        updater->checkNow(AutoUpdate::Trigger::Manual);
}

void
PreferencesUpdates::on_pushButtonUpdate_clicked()
{
    if (auto *updater = AutoUpdate::instance())
        updater->installAvailable();
}

void
PreferencesUpdates::save()
{
    update_on_startup = ui->checkBoxOnStartup->isChecked() ? 1 : 0;
    update_check      = ui->comboBoxInterval->currentIndex();
}
