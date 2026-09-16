#pragma once
#include <QVector>
#include "inktypes.h"

// Auto-shape recognition for "hold the pen still at the end of a stroke" (SPEC §5).
// Returns true and fills `out` with clean points when the freehand points look like a
// line, arrow, ellipse, rectangle or triangle. Pure and deterministic (tested).
enum class ShapeKind { None, Line, Arrow, Ellipse, Rectangle, Triangle };

ShapeKind recognizeShape(const QVector<InkPoint> &pts, QVector<InkPoint> &out);

// A scratch-out: the deliberate back-and-forth you make when crossing something out. True when the
// stroke reverses direction several times inside a narrow band, which a letter or a line never does.
bool looksLikeScratchOut(const QVector<InkPoint> &pts);
