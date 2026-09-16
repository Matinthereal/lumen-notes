#pragma once
#include <QPointF>
#include <QPolygonF>
#include <cmath>

namespace inkgeo {

inline float distSqPointSegment(float px, float py, float ax, float ay, float bx, float by)
{
    const float dx = bx - ax, dy = by - ay;
    const float len2 = dx * dx + dy * dy;
    float t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.f;
    t = std::clamp(t, 0.f, 1.f);
    const float cx = ax + t * dx - px, cy = ay + t * dy - py;
    return cx * cx + cy * cy;
}

// Even-odd rule; robust enough for a hand-drawn lasso.
inline bool pointInPolygon(const QPolygonF &poly, float x, float y)
{
    bool inside = false;
    const int n = poly.size();
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const QPointF &a = poly[i], &b = poly[j];
        if ((a.y() > y) != (b.y() > y)) {
            const double xi = b.x() + (y - b.y()) * (a.x() - b.x()) / (a.y() - b.y());
            if (x < xi) inside = !inside;
        }
    }
    return inside;
}

} // namespace inkgeo
