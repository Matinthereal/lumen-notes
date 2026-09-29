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

// Resample through every sample with a Catmull-Rom spline. centripetal (the "steady" option):
// knots spaced by the square root of the distance, which never overshoots or loops where the
// samples bunch up and spread out — uniform knots can, and that reads as a wobble.
QVector<InkPoint> smoothStroke(const QVector<InkPoint> &raw, float spacing, bool centripetal = false);
// Zero-lag steadying (the "steady" pen option, ADR mynotes-003): each sample moves toward a
// Gaussian average of its neighbours along the path, on both sides, so nothing trails the pen.
// The first and last samples stay exactly where the pen was — the tip is always raw — and a
// sample settles by a fraction of a pixel as the samples after it arrive. Pressure is steadied
// the same way, so width does not flicker. sigma is in page units along the path.
QVector<InkPoint> steadyStroke(const QVector<InkPoint> &raw, float sigma);

InkPoint predictPoint(const QVector<InkPoint> &pts, float dtMs, float damping = 0.6f, float maxDistance = 14.f);

// Variable-width ribbon as ONE triangle strip: miter joins, round joins where the turn is sharp,
// round caps, and a feathered rim of `feather` page units for shader-free anti-aliasing.
// centred (the "true edges" option, ADR mynotes-003): the rim straddles the stroke's edge instead
// of lying outside it, so half-coverage falls exactly at the nominal width — classic ink reads
// half a rim wider than it is — and a highlighter's square ends are feathered too.
// Per-sample extras for a ribbon: a width factor and the core's opacity (1 = solid ink).
struct RibbonShade { QVector<float> widthScale, alpha; };

void buildRibbon(const QVector<InkPoint> &pts, float baseWidth, InkTool tool, const PressureCurve &curve,
                 QVector<InkVertex> &out, float feather = 0.f, bool centred = false, const RibbonShade *shade = nullptr);

// The pencil (ADR mynotes-003): graphite is lighter with a light touch and grainy along the line,
// and a pencil leant over past writing angle shades — a wider, paler mark, as with the side of
// the lead. Tilt is degrees from upright (the maker's pen writes at 23–35°, p95; an Apple Pencil
// laid down for shading reaches 60–75°). Pressure is normalised by the ceiling like the pen's.
RibbonShade pencilShade(const QVector<InkPoint> &pts, float ceiling);
PressureCurve pencilCurve(float ceiling);

void appendStrip(QVector<InkVertex> &strip, const QVector<InkVertex> &piece);
