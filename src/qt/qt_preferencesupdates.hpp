/*
 * PeepeeBox   A funworld Photo Play / I.G.O. arcade cabinet emulator,
 *             forked from 86Box.
 *
 *             Header for the Updates preferences page.
 *
 * Authors:    Marcos Alves
 *
 *             Copyright 2026 Marcos Alves.
 */
#ifndef QT_PREFERENCESUPDATES_HPP
#define QT_PREFERENCESUPDATES_HPP

#include <QWidget>

namespace Ui {
class PreferencesUpdates;
}

class PreferencesUpdates : public QWidget {
    Q_OBJECT

public:
    explicit PreferencesUpdates(QWidget *parent = nullptr);
    ~PreferencesUpdates();

    void save();

private slots:
    void on_pushButtonCheckNow_clicked();
    void on_pushButtonUpdate_clicked();
    void showStatus(const QString &text);
    void refresh();

private:
    Ui::PreferencesUpdates *ui;
    QString                 notesVersion; /* whose notes the page is showing */
};

#endif // QT_PREFERENCESUPDATES_HPP
