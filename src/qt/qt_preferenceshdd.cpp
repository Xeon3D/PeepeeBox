/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             The hard disk image manager preferences page.
 *
 *             The manager is off by default and stays off until the user says
 *             otherwise, either here or in the question asked at first start.
 *             Answering it either way here also settles that question, so a
 *             user who finds this page before the question is ever asked is
 *             not asked again afterwards.
 */
#include "qt_preferenceshdd.hpp"
#include "ui_qt_preferenceshdd.h"

#include <QApplication>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QStyle>

#include <cstring>

extern "C" {
#include <86box/86box.h>
}

PreferencesHdd::PreferencesHdd(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::PreferencesHdd)
{
    ui->setupUi(this);

    ui->pushButtonImagesPath->setIcon(QApplication::style()->standardIcon(QStyle::SP_DirIcon));

    if (hdd_images_path[0] != '\0')
        ui->lineEditImagesPath->setText(QDir::toNativeSeparators(QString(hdd_images_path)));

    ui->checkBoxHddManager->setChecked(hdd_manager > 0);
    ui->groupBoxImages->setEnabled(hdd_manager > 0);

    updateStatus();
}

PreferencesHdd::~PreferencesHdd()
{
    delete ui;
}

/* The folder is worth a word either way: an empty box and a box naming a
   folder that is not there both leave the manager with nothing to list, and
   the second one looks configured. */
void
PreferencesHdd::updateStatus()
{
    const QString path = ui->lineEditImagesPath->text().trimmed();

    if (!ui->checkBoxHddManager->isChecked())
        ui->labelImagesPathStatus->clear();
    else if (path.isEmpty())
        ui->labelImagesPathStatus->setText(tr("No folder chosen yet."));
    else if (!QFileInfo(path).isDir())
        ui->labelImagesPathStatus->setText(tr("This folder does not exist."));
    else
        ui->labelImagesPathStatus->clear();
}

void
PreferencesHdd::on_checkBoxHddManager_toggled(bool checked)
{
    ui->groupBoxImages->setEnabled(checked);

    updateStatus();
}

void
PreferencesHdd::on_pushButtonImagesPath_clicked()
{
    QFileDialog::Options options = QFileDialog::ShowDirsOnly;
#ifdef Q_OS_LINUX
    options |= QFileDialog::DontUseNativeDialog;
#endif

    const QString current   = ui->lineEditImagesPath->text().trimmed();
    const QString directory = QFileDialog::getExistingDirectory(this, tr("Choose the hard disk images folder"),
                                                                current, options);

    if (!directory.isEmpty())
        ui->lineEditImagesPath->setText(QDir::toNativeSeparators(directory));
}

void
PreferencesHdd::on_lineEditImagesPath_textChanged(const QString &text)
{
    (void) text;

    updateStatus();
}

void
PreferencesHdd::save()
{
    hdd_manager = ui->checkBoxHddManager->isChecked() ? 1 : 0;

    /* Having been here at all settles the first-start question. */
    hdd_manager_asked = 1;

    const QString path = ui->lineEditImagesPath->text().trimmed();

    if (path.isEmpty())
        hdd_images_path[0] = '\0';
    else {
        strncpy(hdd_images_path, QDir::cleanPath(path).toUtf8().constData(),
                sizeof(hdd_images_path) - 1);
        hdd_images_path[sizeof(hdd_images_path) - 1] = '\0';
    }
}
