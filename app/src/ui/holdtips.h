#pragma once
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QQuickItem>
#include <QRectF>
#include <QTimer>
#include <QPair>

class QPointingDevice;
class QQuickWindow;

// Press-and-hold tooltips for finger and pen. Every control already says what it does through
// ToolTip.text, but ToolTip.visible is bound to hover, which a finger never produces. This filter
// watches touch and pen presses on the window; when one stays put for holdMs over a control that
// has a tooltip, it cancels the press (so the control does not fire on release) and emits shown()
// for the one HoldTip bubble in Main.qml to display.
//
// A control whose own hold means something (hold to record, hold to save a favourite pen) sets
// `property bool holdAction: true` and is left alone. The mouse is ignored: it can hover.
// A containment mask that contains nothing: an item wearing it cannot be pressed or released on.
class NothingMask : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE bool contains(const QPointF &) const { return false; }
};

class HoldTips : public QObject {
    Q_OBJECT
    Q_PROPERTY(int holdMs READ holdMs WRITE setHoldMs NOTIFY holdMsChanged)
public:
    explicit HoldTips(QObject *parent = nullptr);
    void attachWindow(QQuickWindow *window);
    int holdMs() const { return m_timer.interval(); }
    void setHoldMs(int ms);

    // The item whose tooltip a hold at scenePos would show, or null. Public for the tests.
    static QQuickItem *tipOwnerAt(QQuickItem *root, const QPointF &scenePos, QString *text = nullptr);

signals:
    void shown(const QString &text, const QRectF &sceneRect);
    void released();
    void holdMsChanged();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool arm(const QPointingDevice *device, int pointId, const QPointF &scenePos);
    void disarm() { m_timer.stop(); m_owner = nullptr; }
    void fire();
    bool movedTooFar(const QPointF &scenePos) const;
    void lifted();
    void hollow(QQuickItem *item);

    QPointer<QQuickWindow> m_window;
    QTimer m_timer;
    QPointer<QQuickItem> m_owner;
    QString m_text;
    const QPointingDevice *m_device = nullptr;
    int m_pointId = 0;
    QPointF m_pressPos;
    quint64 m_lastStamp = 0;
    bool m_held = false;           // the tip is up for the press still in progress
    NothingMask m_nothing;
    QList<QPair<QPointer<QQuickItem>, QPointer<QObject>>> m_hollowed;   // item, the mask it had
    bool m_penNear = false;        // a touch while the pen hovers is the writing hand's palm
};
