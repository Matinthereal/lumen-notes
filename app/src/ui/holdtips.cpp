#include "holdtips.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QPointingDevice>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QLoggingCategory>
#include <algorithm>

Q_LOGGING_CATEGORY(lcHoldTips, "lumen.holdtips")

namespace {

// Would this item take a press? Decoration (text, icons, backgrounds) does not, and neither does
// the popup overlay when no popup is open, though it covers the whole window.
bool takesPresses(QQuickItem *item)
{
    if (item->inherits("QQuickOverlay")) return false;
    if (item->acceptedMouseButtons() != Qt::NoButton || item->acceptTouchEvents()) return true;
    for (QObject *o : item->children())
        if (o->inherits("QQuickPointerHandler") && !o->inherits("QQuickHoverHandler")) return true;
    return false;
}

// The topmost item under the point that takes presses, in paint order: children by z, later
// siblings above earlier — roughly the item Qt would deliver the press to.
QQuickItem *topmostAt(QQuickItem *item, const QPointF &scenePos)
{
    if (!item->isVisible() || item->opacity() <= 0.01) return nullptr;
    const QPointF local = item->mapFromScene(scenePos);
    if (item->clip() && !item->contains(local)) return nullptr;
    QList<QQuickItem *> kids = item->childItems();
    std::stable_sort(kids.begin(), kids.end(), [](QQuickItem *a, QQuickItem *b) { return a->z() < b->z(); });
    for (auto it = kids.crbegin(); it != kids.crend(); ++it)
        if (QQuickItem *hit = topmostAt(*it, scenePos)) return hit;
    return item->contains(local) && takesPresses(item) ? item : nullptr;
}

// ToolTip's attached object is parented to the item it is attached to.
QObject *attachedToolTip(QQuickItem *item)
{
    for (QObject *o : item->children())
        if (o->inherits("QQuickToolTipAttached")) return o;
    return nullptr;
}

} // namespace

HoldTips::HoldTips(QObject *parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(500);      // Android's long-press tooltip and iOS's context menu both sit here
    connect(&m_timer, &QTimer::timeout, this, &HoldTips::fire);
    // On the application rather than the window: pen proximity is only ever sent to the application.
    QCoreApplication::instance()->installEventFilter(this);
}

void HoldTips::attachWindow(QQuickWindow *window)
{
    m_window = window;       // the filter itself is on the application, which sees the window's events first
}

void HoldTips::setHoldMs(int ms)
{
    if (ms == m_timer.interval()) return;
    m_timer.setInterval(ms);
    emit holdMsChanged();
}

QQuickItem *HoldTips::tipOwnerAt(QQuickItem *root, const QPointF &scenePos, QString *text)
{
    QQuickItem *hit = root ? topmostAt(root, scenePos) : nullptr;
    for (QQuickItem *item = hit; item; item = item->parentItem()) {
        if (item->property("holdAction").toBool()) return nullptr;
        if (QObject *tip = attachedToolTip(item)) {
            const QString t = tip->property("text").toString();
            if (t.isEmpty()) continue;
            if (text) *text = t;
            return item;
        }
    }
    return nullptr;
}

bool HoldTips::arm(const QPointingDevice *device, int pointId, const QPointF &scenePos)
{
    disarm();
    m_held = false;
    if (!m_window) return false;
    m_text.clear();
    QQuickItem *owner = tipOwnerAt(m_window->contentItem(), scenePos, &m_text);
    qCDebug(lcHoldTips) << "press at" << scenePos << "by" << device->name() << "owner" << owner << m_text;
    if (!owner) return false;
    m_owner = owner;
    m_device = device;
    m_pointId = pointId;
    m_pressPos = scenePos;
    m_timer.start();
    return true;
}

bool HoldTips::movedTooFar(const QPointF &scenePos) const
{
    const int slop = m_device && m_device->type() == QInputDevice::DeviceType::TouchScreen
                         ? QGuiApplication::styleHints()->startDragDistance() * 2 : QGuiApplication::styleHints()->startDragDistance();
    return (scenePos - m_pressPos).manhattanLength() > slop;
}

void HoldTips::fire()
{
    if (!m_owner || !m_window) return;
    qCDebug(lcHoldTips) << "hold on" << m_owner.data() << "— showing" << m_text;
    m_held = true;
    // Take the press away from the control without faking any input, so Qt's own record of what is
    // pressed stays true and the lift still arrives as usual:
    //  - an exclusive grabber (a Button, a MouseArea, Qt's mouse synthesised from the pen) has the
    //    grab taken over, which it hears as a cancel;
    //  - a passive grabber (a TapHandler) keeps its grab, but the item it sits on stops containing
    //    anything until the lift has been delivered, so the lift reads as "released outside" —
    //    which TapHandler treats as a cancel, not a tap.
    QList<const QPointingDevice *> devices{m_device};
    if (m_device->type() != QInputDevice::DeviceType::TouchScreen && m_device != QPointingDevice::primaryPointingDevice())
        devices << QPointingDevice::primaryPointingDevice();
    for (const QPointingDevice *dev : std::as_const(devices)) {
        const int id = dev == m_device ? m_pointId : 0;
        QTouchEvent probe(QEvent::TouchUpdate, dev, Qt::NoModifier, {QEventPoint(id, QEventPoint::State::Stationary, m_pressPos, m_pressPos)});
        const QEventPoint &pt = probe.point(0);
        if (probe.exclusiveGrabber(pt)) {
            QObject thief;
            probe.setExclusiveGrabber(pt, &thief);
            probe.setExclusiveGrabber(pt, nullptr);
        }
        for (const QPointer<QObject> &g : probe.passiveGrabbers(pt))
            if (g && g->inherits("QQuickPointerHandler")) hollow(g->property("parent").value<QQuickItem *>());
    }
    const QRectF box = m_owner->mapRectToScene(QRectF(0, 0, m_owner->width(), m_owner->height()));
    // A touch counts as hover to HoverHandler, so the control's own hover tooltip may be on its way
    // too. Put it away, or the two bubbles stack.
    if (QObject *tip = attachedToolTip(m_owner)) QMetaObject::invokeMethod(tip, "hide");
    emit shown(m_text, box);
}

void HoldTips::hollow(QQuickItem *item)
{
    if (!item) return;
    for (const auto &h : std::as_const(m_hollowed)) if (h.first == item) return;
    m_hollowed.append({item, item->containmentMask()});
    item->setContainmentMask(&m_nothing);
}

void HoldTips::lifted()
{
    disarm();
    if (!m_held) return;
    m_held = false;
    // The lift is being delivered right now (and, for the pen, the mouse release Qt makes from it
    // straight after): give the items back their shape once that is done.
    QMetaObject::invokeMethod(this, [this] {
        for (const auto &h : std::as_const(m_hollowed)) if (h.first) h.first->setContainmentMask(h.second);
        m_hollowed.clear();
    }, Qt::QueuedConnection);
    emit released();
}

bool HoldTips::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::TabletEnterProximity || event->type() == QEvent::TabletLeaveProximity) {
        m_penNear = event->type() == QEvent::TabletEnterProximity;
        return false;
    }
    if (watched != m_window) return false;
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd: {
        auto *te = static_cast<QTouchEvent *>(event);
        if (te->device()->type() != QInputDevice::DeviceType::TouchScreen) return false;
        m_lastStamp = te->timestamp();
        const auto &pts = te->points();
        if (event->type() == QEvent::TouchBegin) {
            if (pts.size() == 1 && !m_penNear) arm(te->pointingDevice(), pts.first().id(), pts.first().scenePosition());   // near pen: a palm
        } else if (event->type() == QEvent::TouchEnd) {
            lifted();
        } else if (m_timer.isActive() && (pts.size() != 1 || movedTooFar(pts.first().scenePosition()))) {
            disarm();          // a pinch or a scroll
        }
        return false;
    }
    case QEvent::TouchCancel:
        lifted();
        return false;
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease: {
        auto *te = static_cast<QTabletEvent *>(event);
        m_lastStamp = te->timestamp();
        if (event->type() == QEvent::TabletPress) arm(te->pointingDevice(), te->point(0).id(), te->scenePosition());
        else if (event->type() == QEvent::TabletRelease) lifted();
        else if (m_timer.isActive() && te->buttons() != Qt::NoButton && movedTooFar(te->scenePosition())) disarm();
        return false;
    }
    default:
        return false;
    }
}
