#include "tableteventfilter.h"
#include <QCoreApplication>
#include <QEvent>
#include <QPointingDevice>
#include <QStringList>
#include <QTabletEvent>
#include <QWindow>

TabletEventFilter::TabletEventFilter(QObject *parent) : QObject(parent)
{
    QCoreApplication::instance()->installEventFilter(this);
}

void TabletEventFilter::attachWindow(QWindow *window)
{
    if (m_window)
        m_window->removeEventFilter(this);
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
}

static QString capsToString(QInputDevice::Capabilities c)
{
    QStringList out;
    using Cap = QInputDevice::Capability;
    if (c & Cap::Position) out << "position";
    if (c & Cap::Pressure) out << "pressure";
    if (c & Cap::XTilt) out << "xtilt";
    if (c & Cap::YTilt) out << "ytilt";
    if (c & Cap::Hover) out << "hover";
    if (c & Cap::ZPosition) out << "z";
    if (c & Cap::Rotation) out << "rotation";
    if (c & Cap::TangentialPressure) out << "tangential";
    if (c & Cap::MouseEmulation) out << "mouse-emulation";
    return out.join(' ');
}

TabletSample TabletEventFilter::fromEvent(const QTabletEvent *e)
{
    TabletSample s;
    switch (e->type()) {
    case QEvent::TabletPress: s.kind = TabletSample::Kind::Press; break;
    case QEvent::TabletRelease: s.kind = TabletSample::Kind::Release; break;
    case QEvent::TabletEnterProximity: s.kind = TabletSample::Kind::EnterProximity; break;
    case QEvent::TabletLeaveProximity: s.kind = TabletSample::Kind::LeaveProximity; break;
    default: s.kind = TabletSample::Kind::Move; break;
    }
    s.windowPos = e->position();
    s.pressure = e->pressure();
    s.xTilt = e->xTilt();
    s.yTilt = e->yTilt();
    s.z = e->z();
    s.rotation = e->rotation();
    s.modifiers = e->modifiers();
    s.buttons = e->buttons();
    s.button = e->button();
    s.eraser = e->pointerType() == QPointingDevice::PointerType::Eraser;
    s.timestampMs = e->timestamp();
    if (const QPointingDevice *d = e->pointingDevice()) {
        s.deviceName = d->name();
        s.capabilities = capsToString(d->capabilities());
    }
    return s;
}

bool TabletEventFilter::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::TabletEnterProximity:
    case QEvent::TabletLeaveProximity: {
        // Delivered to the application object only.
        const auto s = fromEvent(static_cast<QTabletEvent *>(event));
        emit sampleSeen(s);
        if (m_sink)
            m_sink->tabletProximity(event->type() == QEvent::TabletEnterProximity, s);
        return false;
    }
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease: {
        if (watched != m_window)
            return false;
        const auto s = fromEvent(static_cast<QTabletEvent *>(event));
        emit sampleSeen(s);
        if (m_sink && m_sink->tabletSample(s)) {
            event->accept();
            return true; // consumed: Qt Quick never sees it, no mouse synthesis
        }
        return false;
    }
    default:
        return false;
    }
}
