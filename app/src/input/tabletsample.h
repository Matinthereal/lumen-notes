#pragma once
#include <QPointF>
#include <QString>
#include <Qt>

// One tablet event, flattened. Coordinates are WINDOW coordinates (the item maps them).
struct TabletSample {
    enum class Kind { Press, Move, Release, EnterProximity, LeaveProximity };
    Kind kind = Kind::Move;
    QPointF windowPos;
    qreal pressure = 0;       // 0..1
    qreal xTilt = 0;          // degrees
    qreal yTilt = 0;          // degrees
    qreal z = 0;              // hover distance if the device reports it
    qreal rotation = 0;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;
    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::MouseButton button = Qt::NoButton; // the button that changed on press/release
    bool eraser = false;
    quint64 timestampMs = 0;
    QString deviceName;
    QString capabilities;     // human-readable list from QPointingDevice::capabilities()
};

// Something that wants raw tablet input (the ink canvas, the pen probe).
class TabletSink {
public:
    virtual ~TabletSink() = default;
    // Return true to consume the event (nothing else in Qt Quick sees it).
    virtual bool tabletSample(const TabletSample &s) = 0;
    virtual void tabletProximity(bool entering, const TabletSample &s) = 0;
};
