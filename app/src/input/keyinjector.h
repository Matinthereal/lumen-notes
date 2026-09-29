#pragma once
#include <QMetaObject>
#include <QObject>
#include <QString>

class QQuickItem;

// The on-screen keyboard's hands. Qt's own virtual keyboard cannot activate on this compositor
// (KWin's text-input wins over QT_IM_MODULE — measured, see D-058), so the app draws its own
// keyboard and posts real key events to whatever has focus. A QTextField cannot tell the
// difference between this and the hardware keyboard.
class KeyInjector : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool focusIsText READ focusIsText NOTIFY focusChanged)
public:
    explicit KeyInjector(QObject *parent = nullptr);

    // An editable text field has focus and can be seen. A hidden item keeps its focus in Qt (a
    // typed page when an ink or PDF page replaces it), and the keyboard must not come up for it.
    bool focusIsText() const;
    Q_INVOKABLE bool focusWithin(QQuickItem *scope) const;      // the focused item is scope or inside it
    Q_INVOKABLE void type(const QString &text);                 // ordinary characters
    Q_INVOKABLE void key(int qtKey, int modifiers = 0);          // backspace, enter, arrows…
    Q_INVOKABLE void dropFocus();

signals:
    void focusChanged();           // also when the focused item is shown or hidden

private:
    void watchFocus();
    QMetaObject::Connection m_visibility;
};
