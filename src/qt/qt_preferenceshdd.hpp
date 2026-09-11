/*
 * PeepeeBox   A fork of 86Box that emulates the funworld Photo Play / I.G.O.
 *             arcade kiosk hardware, including its protection token.
 *
 *             Header for the hard disk image manager preferences page.
 */
#ifndef QT_PREFERENCESHDD_HPP
#define QT_PREFERENCESHDD_HPP

#include <QWidget>

namespace Ui {
class PreferencesHdd;
}

class PreferencesHdd : public QWidget {
    Q_OBJECT

public:
    explicit PreferencesHdd(QWidget *parent = nullptr);
    ~PreferencesHdd();

    void save();

private slots:
    void on_checkBoxHddManager_toggled(bool checked);
    void on_pushButtonImagesPath_clicked();
    void on_lineEditImagesPath_textChanged(const QString &text);

private:
    void updateStatus();

    Ui::PreferencesHdd *ui;
};

#endif // QT_PREFERENCESHDD_HPP
