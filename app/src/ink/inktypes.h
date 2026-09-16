#pragma once
#include <QColor>
#include <QRectF>
#include <QVector>
#include <cstdint>

// One raw pen sample in PAGE units (1 unit = 1 px at 100 % zoom = 1/96 in). Timestamps are what
// audio sync and "replay ink" are built on (SPEC §4), so they are kept from the very first phase.
struct InkPoint {
    float x = 0, y = 0;
    float pressure = 0;      // 0..1 as delivered by the digitizer
    float tiltX = 0, tiltY = 0;
    quint32 tMs = 0;         // relative to the page's clock (Phase 5: to the recording)
};

enum class InkTool : quint8 { Pen = 0, Highlighter = 1 };

struct Stroke {
    quint64 id = 0;
    InkTool tool = InkTool::Pen;
    QRgb color = 0xff000000;
    float width = 1.5f;      // base width in page units (0.4 mm ≈ 1.5 px)
    quint64 recordingId = 0; // 0 = not written during a recording
    quint32 startMs = 0;     // stroke start relative to the recording (Phase 5); points carry offsets
    QVector<InkPoint> points;
    QRectF bounds;           // includes the widest possible ribbon; kept current by updateBounds()

    void updateBounds();
};

inline void Stroke::updateBounds()
{
    if (points.isEmpty()) { bounds = QRectF(); return; }
    float minX = points[0].x, maxX = minX, minY = points[0].y, maxY = minY;
    for (const InkPoint &p : points) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    const float pad = width * 1.5f + 1.0f;
    bounds = QRectF(minX - pad, minY - pad, (maxX - minX) + 2 * pad, (maxY - minY) + 2 * pad);
}
