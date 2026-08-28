#ifndef QT_KeyBinder_HPP
#define QT_KeyBinder_HPP

#include <QDialog>

extern "C" {
struct _device_;
}

namespace Ui {
class KeyBinder;
}

class KeyBinder : public QDialog {
    Q_OBJECT

public:
    explicit KeyBinder(QWidget *parent = nullptr);
    ~KeyBinder() override;

    static QKeySequence BindKey(QWidget *widget, QString CurValue);

private:
    Ui::KeyBinder *ui;
    bool           eventFilter(QObject *obj, QEvent *event) override;
    void           showEvent(QShowEvent *event) override;
};

#endif // QT_KeyBinder_HPP
