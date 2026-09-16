#pragma once
#include <QVector>
#include "inktypes.h"

// Pure functions, deterministic for a given input (tested). No Qt scene graph types here so the
// same code serves rendering, export (SVG/PDF) and the tests.

// "classic" is the Phase 1 pen the maker approved: linear pressure, no speed effect. It is the
// default. The others exist as options; speed thinning was the cause of wobbly fast lines when it
// was on by default (ms timestamps make per-sample speed noisy), so it now averages over 40 ms.
enum class PenStyle : quint8 { Classic = 0, Fountain = 1, Ballpoint = 2, Brush = 3 };

struct PressureCurve {
    float ceiling = 0.85f;    // D-015: the maker's firmest stroke; pressure/ceiling is clamped to 1
    float minScale = 0.5f;    // width factor at zero pressure
    float maxScale = 1.5f;    // width factor at/above the ceiling
    float gamma = 1.0f;       // 1 = linear (classic); <1 makes light touches count for more
    float speedThinning = 0.f; // width loss per px/ms of tip speed; 0 = off (classic, ballpoint)
    float speedFloor = 1.f;   // never thinner than this fraction because of speed
    float widthFor(float baseWidth, float pressure) const;
    static PressureCurve forStyle(PenStyle s, float ceiling = 0.85f);
};

// Vertex of a ribbon: position plus an edge alpha (1 in the core, 0 on the feathered rim).
struct InkVertex { float x, y, a = 1.f; };

QVector<InkPoint> smoothStroke(const QVector<InkPoint> &raw, float spacing);
InkPoint predictPoint(const QVector<InkPoint> &pts, float dtMs, float damping = 0.6f, float maxDistance = 14.f);

// Variable-width ribbon as ONE triangle strip: miter joins, round joins where the turn is sharp,
// round caps, and a feathered rim of `feather` page units for shader-free anti-aliasing.
void buildRibbon(const QVector<InkPoint> &pts, float baseWidth, InkTool tool, const PressureCurve &curve,
                 QVector<InkVertex> &out, float feather = 0.f);

void appendStrip(QVector<InkVertex> &strip, const QVector<InkVertex> &piece);
