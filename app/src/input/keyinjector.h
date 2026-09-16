#pragma once
#include <QObject>
#include <QString>

// The on-screen keyboard's hands. Qt's own virtual keyboard cannot activate on this compositor
// (KWin's text-input wins over QT_IM_MODULE — measured, see D-058), so the app draws its own
// keyboard and posts real key events to whatever has focus. A QTextField cannot tell the
// difference between this and the hardware keyboard.
class KeyInjector : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool focusIsText READ focusIsText NOTIFY focusChanged)
public:
    explicit KeyInjector(QObject *parent = nullptr);

    bool focusIsText() const;
    Q_INVOKABLE void type(const QString &text);                 // ordinary characters
    Q_INVOKABLE void key(int qtKey, int modifiers = 0);          // backspace, enter, arrows…
    Q_INVOKABLE void dropFocus();

signals:
    void focusChanged();
};
