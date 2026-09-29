#include "keyinjector.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>

KeyInjector::KeyInjector(QObject *parent) : QObject(parent)
{
    connect(qGuiApp, &QGuiApplication::focusObjectChanged, this, [this] { watchFocus(); emit focusChanged(); });
}

// visibleChanged reaches an item when any of its parents is shown or hidden, too.
void KeyInjector::watchFocus()
{
    disconnect(m_visibility);
    if (auto *item = qobject_cast<QQuickItem *>(QGuiApplication::focusObject()))
        m_visibility = connect(item, &QQuickItem::visibleChanged, this, &KeyInjector::focusChanged);
}

bool KeyInjector::focusIsText() const
{
    QObject *focus = QGuiApplication::focusObject();
    if (!focus) return false;
    // Both of Qt Quick's editable text items, and nothing else — the canvas must never make the
    // keyboard appear.
    if (!focus->inherits("QQuickTextInput") && !focus->inherits("QQuickTextEdit")) return false;
    if (auto *item = qobject_cast<QQuickItem *>(focus); item && !item->isVisible()) return false;
    return !focus->property("readOnly").toBool();   // a preview you cannot type into must not raise it
}

bool KeyInjector::focusWithin(QQuickItem *scope) const
{
    for (auto *item = qobject_cast<QQuickItem *>(QGuiApplication::focusObject()); item; item = item->parentItem())
        if (item == scope) return true;
    return false;
}

void KeyInjector::type(const QString &text)
{
    QObject *focus = QGuiApplication::focusObject();
    if (!focus || text.isEmpty()) return;
    QKeyEvent press(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(focus, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_unknown, Qt::NoModifier, text);
    QCoreApplication::sendEvent(focus, &release);
}

void KeyInjector::key(int qtKey, int modifiers)
{
    QObject *focus = QGuiApplication::focusObject();
    if (!focus) return;
    const auto mods = static_cast<Qt::KeyboardModifiers>(modifiers);
    QKeyEvent press(QEvent::KeyPress, qtKey, mods);
    QCoreApplication::sendEvent(focus, &press);
    QKeyEvent release(QEvent::KeyRelease, qtKey, mods);
    QCoreApplication::sendEvent(focus, &release);
}

void KeyInjector::dropFocus()
{
    if (QObject *focus = QGuiApplication::focusObject())
        if (auto *item = qobject_cast<QQuickItem *>(focus)) item->setFocus(false);
}
