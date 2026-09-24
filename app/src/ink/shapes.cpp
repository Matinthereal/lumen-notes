#include "shapes.h"
#include <algorithm>
#include "geometry.h"
#include <QPointF>
#include <cmath>
#include <numbers>

namespace {

float pathLength(const QVector<InkPoint> &p)
{
    float l = 0;
    for (int i = 1; i < p.size(); ++i) l += std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y);
    return l;
}

InkPoint at(const InkPoint &tmpl, float x, float y, quint32 t)
{
    InkPoint q = tmpl; q.x = x; q.y = y; q.tMs = t; return q;
}

// Indices of corners: points where the direction (measured over a window) turns sharply.
QVector<int> findCorners(const QVector<InkPoint> &p, float minTurnDeg)
{
    QVector<int> corners;
    const int n = p.size();
    const int w = std::max(2, n / 25);
    float bestTurn = 0; int best = -1;
    for (int i = w; i < n - w; ++i) {
        const float ax = p[i].x - p[i - w].x, ay = p[i].y - p[i - w].y;
        const float bx = p[i + w].x - p[i].x, by = p[i + w].y - p[i].y;
        const float la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        if (la < 1e-3f || lb < 1e-3f) continue;
        const float cosang = std::clamp((ax * bx + ay * by) / (la * lb), -1.f, 1.f);
        const float turn = std::acos(cosang) * 180.f / std::numbers::pi_v<float>;
        if (turn >= minTurnDeg) {
            if (turn > bestTurn) { bestTurn = turn; best = i; }
        } else if (best >= 0) {
            corners.append(best); best = -1; bestTurn = 0;
        }
    }
    if (best >= 0) corners.append(best);
    return corners;
}

float maxDeviationFromChord(const QVector<InkPoint> &p, int i0, int i1)
{
    float maxd = 0;
    for (int i = i0; i <= i1; ++i)
        maxd = std::max(maxd, std::sqrt(inkgeo::distSqPointSegment(p[i].x, p[i].y, p[i0].x, p[i0].y, p[i1].x, p[i1].y)));
    return maxd;
}

} // namespace

ShapeKind recognizeShape(const QVector<InkPoint> &pts, QVector<InkPoint> &out)
{
    out.clear();
    const int n = pts.size();
    if (n < 8) return ShapeKind::None;
    const float len = pathLength(pts);
    if (len < 30.f) return ShapeKind::None;

    float minX = pts[0].x, maxX = minX, minY = pts[0].y, maxY = minY;
    for (const InkPoint &p : pts) { minX = std::min(minX, p.x); maxX = std::max(maxX, p.x); minY = std::min(minY, p.y); maxY = std::max(maxY, p.y); }
    const float bw = maxX - minX, bh = maxY - minY;
    const float diag = std::hypot(bw, bh);
    const float closeDist = std::hypot(pts.last().x - pts[0].x, pts.last().y - pts[0].y);
    const bool closed = closeDist < std::max(0.18f * diag, 12.f);
    const InkPoint &tmpl = pts[0];
    float meanP = 0; for (const InkPoint &p : pts) meanP += p.pressure; meanP /= n;
    InkPoint t0 = tmpl; t0.pressure = meanP;

    // --- straight line: everything close to the chord
    const float chord = std::hypot(pts.last().x - pts[0].x, pts.last().y - pts[0].y);
    if (!closed && chord > 0.85f * len && maxDeviationFromChord(pts, 0, n - 1) < std::max(0.04f * chord, 2.5f)) {
        out = {at(t0, pts[0].x, pts[0].y, 0), at(t0, pts.last().x, pts.last().y, pts.last().tMs)};
        return ShapeKind::Line;
    }

    const QVector<int> corners = findCorners(pts, 55.f);

    // --- arrow: a line whose tail folds back into a head (one or two sharp corners in the last 35 %)
    if (!closed && !corners.isEmpty() && corners.first() > int(n * 0.55)) {
        const int k = corners.first();
        const float shaft = std::hypot(pts[k].x - pts[0].x, pts[k].y - pts[0].y);
        if (shaft > 25.f && maxDeviationFromChord(pts, 0, k) < std::max(0.05f * shaft, 3.f)) {
            const float ang = std::atan2(pts[k].y - pts[0].y, pts[k].x - pts[0].x);
            const float head = std::clamp(shaft * 0.22f, 8.f, 40.f);
            const float a1 = ang + std::numbers::pi_v<float> - 0.5f, a2 = ang + std::numbers::pi_v<float> + 0.5f;
            const InkPoint tip = at(t0, pts[k].x, pts[k].y, pts[k].tMs);
            out = {at(t0, pts[0].x, pts[0].y, 0), tip,
                   at(t0, tip.x + head * std::cos(a1), tip.y + head * std::sin(a1), tip.tMs + 1), tip,
                   at(t0, tip.x + head * std::cos(a2), tip.y + head * std::sin(a2), tip.tMs + 2)};
            return ShapeKind::Arrow;
        }
    }

    if (!closed) return ShapeKind::None;

    // --- polygon with 3 or 4 corners (start/end counts as one corner when the stroke closes there)
    if (corners.size() == 2 || corners.size() == 3) {
        QVector<QPointF> c;
        c.append(QPointF((pts[0].x + pts.last().x) / 2, (pts[0].y + pts.last().y) / 2));
        for (int i : corners) c.append(QPointF(pts[i].x, pts[i].y));
        bool straight = true;
        int prev = 0;
        for (int i : corners) { if (maxDeviationFromChord(pts, prev, i) > std::max(0.08f * diag, 4.f)) straight = false; prev = i; }
        if (maxDeviationFromChord(pts, prev, n - 1) > std::max(0.08f * diag, 4.f)) straight = false;
        if (straight) {
            if (c.size() == 4) {
                // Rectangle: snap to the axis-aligned box when the sides are near horizontal/vertical.
                bool axis = true;
                for (int i = 0; i < 4; ++i) {
                    const QPointF d = c[(i + 1) % 4] - c[i];
                    const float a = std::abs(std::atan2(d.y(), d.x())) * 180.f / std::numbers::pi_v<float>;
                    const float off = std::min({a, std::abs(a - 90.f), std::abs(a - 180.f)});
                    if (off > 12.f) axis = false;
                }
                if (axis) c = {QPointF(minX, minY), QPointF(maxX, minY), QPointF(maxX, maxY), QPointF(minX, maxY)};
            }
            for (int i = 0; i <= c.size(); ++i) { const QPointF &q = c[i % c.size()]; out.append(at(t0, float(q.x()), float(q.y()), quint32(i * 10))); }
            return c.size() == 4 ? ShapeKind::Rectangle : ShapeKind::Triangle;
        }
    }

    // --- ellipse: points at a consistent normalised radius from the bbox centre
    if (corners.isEmpty() && bw > 12.f && bh > 12.f) {
        const float cx = (minX + maxX) / 2, cy = (minY + maxY) / 2, rx = bw / 2, ry = bh / 2;
        float maxErr = 0;
        for (const InkPoint &p : pts) {
            const float r = std::hypot((p.x - cx) / rx, (p.y - cy) / ry);
            maxErr = std::max(maxErr, std::abs(r - 1.f));
        }
        if (maxErr < 0.22f) {
            const int segs = 64;
            for (int i = 0; i <= segs; ++i) {
                const float a = float(i) / segs * 2 * std::numbers::pi_v<float> - std::numbers::pi_v<float> / 2;
                out.append(at(t0, cx + rx * std::cos(a), cy + ry * std::sin(a), quint32(i * 4)));
            }
            return ShapeKind::Ellipse;
        }
    }
    return ShapeKind::None;
}

bool looksLikeScratchOut(const QVector<InkPoint> &pts)
{
    if (pts.size() < 12) return false;

    // The stroke's own axis: crossing out is a scribble along one direction, however it is tilted.
    float minX = pts[0].x, maxX = pts[0].x, minY = pts[0].y, maxY = pts[0].y;
    for (const InkPoint &p : pts) {
        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
    }
    const float w = maxX - minX, h = maxY - minY;
    const float longSide = std::max(w, h), shortSide = std::min(w, h);
    if (longSide < 24.f) return false;                 // too small to be a deliberate cross-out
    if (shortSide > longSide * 0.75f) return false;    // a blob, not a band

    const bool horizontal = w >= h;
    // Count reversals along the long axis, ignoring jitter under a fifth of the band.
    const float noise = std::max(3.f, longSide * 0.05f);
    int reversals = 0, dir = 0;
    float anchor = horizontal ? pts[0].x : pts[0].y;
    float travel = 0.f;
    for (int i = 1; i < pts.size(); ++i) {
        const float v = horizontal ? pts[i].x : pts[i].y;
        const float prev = horizontal ? pts[i - 1].x : pts[i - 1].y;
        travel += std::abs(v - prev);
        const float delta = v - anchor;
        if (std::abs(delta) < noise) continue;
        const int d = delta > 0 ? 1 : -1;
        if (dir != 0 && d != dir) ++reversals;
        dir = d;
        anchor = v;
    }
    // Four reversals is two full back-and-forths, and the pen must have travelled much further
    // than the band is wide — that is what separates a scribble from a zig-zag drawing.
    return reversals >= 4 && travel > longSide * 2.2f;
}
