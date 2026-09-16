#pragma once
#include <QRectF>
#include <QVector>
#include "ink/inktypes.h"

// Groups pen strokes into text lines by vertical overlap, then orders each line left → right.
// Pure and deterministic (tested). Highlighter strokes are ignored.
struct InkLine { QVector<quint64> ids; QRectF box; };
QVector<InkLine> segmentLines(const QVector<Stroke> &strokes);
