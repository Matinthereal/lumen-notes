#pragma once
#include <QColor>
#include <QFile>
#include <QQuickItem>
#include <QTextStream>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include "input/tabletsample.h"

class QTimer;

// Phase 0 deliverable: shows every tablet and touch value live, draws a pressure-width trail on the
// scene graph, and logs everything to ~/.local/state/lumen/pen-probe.log so the ranges this
// digitizer really delivers end up in HARDWARE.md.
class PenProbeItem : public QQuickItem, public TabletSink {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString deviceName MEMBER m_deviceName NOTIFY changed)
    Q_PROPERTY(QString capabilities MEMBER m_capabilities NOTIFY changed)
    Q_PROPERTY(QString pointerType MEMBER m_pointerType NOTIFY changed)
    Q_PROPERTY(qreal pressure MEMBER m_pressure NOTIFY changed)
    Q_PROPERTY(qreal xTilt MEMBER m_xTilt NOTIFY changed)
    Q_PROPERTY(qreal yTilt MEMBER m_yTilt NOTIFY changed)
    Q_PROPERTY(qreal hoverZ MEMBER m_z NOTIFY changed)
    Q_PROPERTY(QString buttons MEMBER m_buttons NOTIFY changed)
    Q_PROPERTY(bool inProximity MEMBER m_inProximity NOTIFY changed)
    Q_PROPERTY(bool tipDown MEMBER m_tipDown NOTIFY changed)
    Q_PROPERTY(QString lastEvent MEMBER m_lastEvent NOTIFY changed)
    Q_PROPERTY(qreal eventsPerSecond MEMBER m_eventsPerSecond NOTIFY changed)
    Q_PROPERTY(int touchPoints MEMBER m_touchPoints NOTIFY changed)
    Q_PROPERTY(int touchWhilePen MEMBER m_touchWhilePen NOTIFY changed)
    Q_PROPERTY(int strokeCount MEMBER m_strokeCount NOTIFY changed)
    Q_PROPERTY(int pointCount MEMBER m_pointCount NOTIFY changed)
    Q_PROPERTY(qreal minPressure MEMBER m_minPressure NOTIFY changed)
    Q_PROPERTY(qreal maxPressure MEMBER m_maxPressure NOTIFY changed)
    Q_PROPERTY(qreal minXTilt MEMBER m_minXTilt NOTIFY changed)
    Q_PROPERTY(qreal maxXTilt MEMBER m_maxXTilt NOTIFY changed)
    Q_PROPERTY(qreal minYTilt MEMBER m_minYTilt NOTIFY changed)
    Q_PROPERTY(qreal maxYTilt MEMBER m_maxYTilt NOTIFY changed)
    Q_PROPERTY(qreal minZ MEMBER m_minZ NOTIFY changed)
    Q_PROPERTY(qreal maxZ MEMBER m_maxZ NOTIFY changed)
    Q_PROPERTY(bool eraserSeen MEMBER m_eraserSeen NOTIFY changed)
    Q_PROPERTY(QString stylusButtonsSeen MEMBER m_stylusButtonsSeen NOTIFY changed)
    Q_PROPERTY(QString logPath MEMBER m_logPath NOTIFY changed)
    Q_PROPERTY(QColor penColor MEMBER m_penColor NOTIFY changed)
public:
    explicit PenProbeItem(QQuickItem *parent = nullptr);
    ~PenProbeItem() override;

    Q_INVOKABLE void clear();

    // TabletSink
    bool tabletSample(const TabletSample &s) override;
    void tabletProximity(bool entering, const TabletSample &s) override;

signals:
    void changed();

protected:
    QSGNode *updatePaintNode(QSGNode *old, UpdatePaintNodeData *) override;
    void touchEvent(QTouchEvent *event) override;

private:
    struct Point { float x, y, w; };
    struct Stroke { QVector<Point> pts; bool eraser = false; };

    void recordRanges(const TabletSample &s);
    void openLog();
    void logLine(const QString &line);
    void writeSummary();

    QVector<Stroke> m_strokes;
    int m_firstDirtyStroke = 0;     // strokes at/after this index are (re)tessellated next frame
    bool m_clearRequested = false;

    // readouts
    QString m_deviceName, m_capabilities, m_pointerType, m_buttons, m_lastEvent, m_stylusButtonsSeen, m_logPath;
    qreal m_pressure = 0, m_xTilt = 0, m_yTilt = 0, m_z = 0, m_eventsPerSecond = 0;
    bool m_inProximity = false, m_tipDown = false, m_eraserSeen = false;
    int m_touchPoints = 0, m_touchWhilePen = 0, m_strokeCount = 0, m_pointCount = 0;
    qreal m_minPressure = 1, m_maxPressure = 0, m_minXTilt = 0, m_maxXTilt = 0, m_minYTilt = 0, m_maxYTilt = 0, m_minZ = 0, m_maxZ = 0;
    bool m_anyRange = false;
    QColor m_penColor = QColor(0x18, 0x21, 0x2B);

    // rate
    int m_eventsThisWindow = 0;
    QTimer *m_rateTimer = nullptr;

    // log
    QFile m_log;
    QTextStream m_logStream;
    bool m_headerWritten = false;
    int m_linesSinceFlush = 0;
};
