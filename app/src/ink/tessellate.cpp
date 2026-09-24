#include "tessellate.h"
#include <cmath>
#include <numbers>

float PressureCurve::widthFor(float baseWidth, float pressure) const
{
    const float t = ceiling > 0 ? std::clamp(pressure / ceiling, 0.f, 1.f) : 1.f;
    const float g = std::pow(t, gamma);
    return baseWidth * (minScale + (maxScale - minScale) * g);
}

PressureCurve PressureCurve::forStyle(PenStyle s, float ceiling)
{
    PressureCurve c; c.ceiling = ceiling;
    switch (s) {
    case PenStyle::Classic:   c.minScale = 0.5f;  c.maxScale = 1.5f; c.gamma = 1.0f;  c.speedThinning = 0.f;   c.speedFloor = 1.f; break;
    case PenStyle::Fountain:  c.minScale = 0.45f; c.maxScale = 1.6f; c.gamma = 0.75f; c.speedThinning = 0.06f; c.speedFloor = 0.65f; break;
    case PenStyle::Ballpoint: c.minScale = 0.85f; c.maxScale = 1.15f; c.gamma = 1.0f; c.speedThinning = 0.f; c.speedFloor = 1.f; break;
    case PenStyle::Brush:     c.minScale = 0.2f;  c.maxScale = 2.4f; c.gamma = 0.6f; c.speedThinning = 0.05f; c.speedFloor = 0.7f; break;
    }
    return c;
}

static InkPoint lerp(const InkPoint &a, const InkPoint &b, float t)
{
    InkPoint p;
    p.x = a.x + (b.x - a.x) * t;
    p.y = a.y + (b.y - a.y) * t;
    p.pressure = a.pressure + (b.pressure - a.pressure) * t;
    p.tiltX = a.tiltX + (b.tiltX - a.tiltX) * t;
    p.tiltY = a.tiltY + (b.tiltY - a.tiltY) * t;
    p.tMs = quint32(a.tMs + (double(b.tMs) - double(a.tMs)) * t);
    return p;
}

QVector<InkPoint> smoothStroke(const QVector<InkPoint> &raw, float spacing)
{
    const int n = raw.size();
    if (n < 3 || spacing <= 0)
        return raw;
    QVector<InkPoint> out;
    out.reserve(n * 2);
    out.append(raw[0]);
    for (int i = 0; i < n - 1; ++i) {
        const InkPoint &p0 = raw[std::max(i - 1, 0)];
        const InkPoint &p1 = raw[i];
        const InkPoint &p2 = raw[i + 1];
        const InkPoint &p3 = raw[std::min(i + 2, n - 1)];
        const float segLen = std::hypot(p2.x - p1.x, p2.y - p1.y);
        const int steps = std::max(1, int(std::ceil(segLen / spacing)));
        for (int s = 1; s <= steps; ++s) {
            const float t = float(s) / steps, t2 = t * t, t3 = t2 * t;
            InkPoint p = lerp(p1, p2, t);
            p.x = 0.5f * ((2 * p1.x) + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 + (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
            p.y = 0.5f * ((2 * p1.y) + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 + (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
            out.append(p);
        }
    }
    return out;
}

InkPoint predictPoint(const QVector<InkPoint> &pts, float dtMs, float damping, float maxDistance)
{
    const int n = pts.size();
    if (n < 2 || dtMs <= 0)
        return n ? pts.last() : InkPoint{};
    const InkPoint &b = pts[n - 1];
    const InkPoint &a = pts[std::max(0, n - 4)];
    const float dt = float(b.tMs) - float(a.tMs);
    if (dt <= 0)
        return b;
    float vx = (b.x - a.x) / dt, vy = (b.y - a.y) / dt;
    if (n >= 3) {
        const InkPoint &m = pts[n - 2];
        const float d1x = m.x - pts[std::max(0, n - 3)].x, d1y = m.y - pts[std::max(0, n - 3)].y;
        const float d2x = b.x - m.x, d2y = b.y - m.y;
        const float l1 = std::hypot(d1x, d1y), l2 = std::hypot(d2x, d2y);
        if (l1 > 1e-3f && l2 > 1e-3f) {
            const float cosang = (d1x * d2x + d1y * d2y) / (l1 * l2);
            const float turn = std::clamp(1.f - cosang, 0.f, 2.f);
            const float k = std::max(0.f, 1.f - turn * 1.5f);
            vx *= k; vy *= k;
        }
    }
    float ex = vx * dtMs * damping, ey = vy * dtMs * damping;
    const float len = std::hypot(ex, ey);
    if (len > maxDistance) { ex *= maxDistance / len; ey *= maxDistance / len; }
    InkPoint p = b;
    p.x = b.x + ex;
    p.y = b.y + ey;
    p.tMs = b.tMs + quint32(dtMs);
    return p;
}

void appendStrip(QVector<InkVertex> &strip, const QVector<InkVertex> &piece)
{
    if (piece.isEmpty()) return;
    if (!strip.isEmpty()) {
        strip.append(strip.last());
        strip.append(piece.first());
        if (strip.size() % 2 == 1) strip.append(piece.first());
    }
    strip += piece;
}

namespace {

// Fan around (cx,cy) as a strip: c, p0, c, p1 … ; then a feathered rim if feather > 0.
void appendArc(QVector<InkVertex> &out, float cx, float cy, float r, float a0, float a1, int segs, float feather)
{
    QVector<InkVertex> fan;
    for (int i = 0; i <= segs; ++i) {
        const float a = a0 + (a1 - a0) * float(i) / segs;
        fan.append({cx, cy, 1.f});
        fan.append({cx + r * std::cos(a), cy + r * std::sin(a), 1.f});
    }
    appendStrip(out, fan);
    if (feather > 0) {
        QVector<InkVertex> rim;
        for (int i = 0; i <= segs; ++i) {
            const float a = a0 + (a1 - a0) * float(i) / segs;
            const float c = std::cos(a), s = std::sin(a);
            rim.append({cx + r * c, cy + r * s, 1.f});
            rim.append({cx + (r + feather) * c, cy + (r + feather) * s, 0.f});
        }
        appendStrip(out, rim);
    }
}

struct Side { QVector<InkVertex> l, r; }; // per-point left/right core edge

// Emit a ribbon run: left feather, core, right feather — three sub-strips.
void flushRun(QVector<InkVertex> &out, const Side &run, float feather, const QVector<float> &nx, const QVector<float> &ny)
{
    const int n = run.l.size();
    if (n == 0) return;
    QVector<InkVertex> core; core.reserve(n * 2);
    for (int i = 0; i < n; ++i) { core.append(run.l[i]); core.append(run.r[i]); }
    if (feather > 0) {
        QVector<InkVertex> lf, rf; lf.reserve(n * 2); rf.reserve(n * 2);
        for (int i = 0; i < n; ++i) {
            lf.append({run.l[i].x + nx[i] * feather, run.l[i].y + ny[i] * feather, 0.f});
            lf.append(run.l[i]);
            rf.append(run.r[i]);
            rf.append({run.r[i].x - nx[i] * feather, run.r[i].y - ny[i] * feather, 0.f});
        }
        appendStrip(out, lf);
        appendStrip(out, core);
        appendStrip(out, rf);
    } else {
        appendStrip(out, core);
    }
}

} // namespace

void buildRibbon(const QVector<InkPoint> &pts, float baseWidth, InkTool tool, const PressureCurve &curve,
                 QVector<InkVertex> &out, float feather)
{
    out.clear();
    const int n = pts.size();
    if (n == 0) return;
    const bool pen = tool == InkTool::Pen;

    // Half-width per point: pressure curve, then speed thinning smoothed along the stroke.
    QVector<float> hw(n);
    for (int i = 0; i < n; ++i) {
        float w = pen ? curve.widthFor(baseWidth, pts[i].pressure) : baseWidth;
        if (pen && curve.speedThinning > 0 && i > 0) {
            // Speed over a 40 ms window, not per sample: ms-resolution timestamps make the
            // per-sample estimate jump by ±50 % and the edges pulse (the maker saw it).
            int j = i - 1;
            while (j > 0 && float(pts[i].tMs) - float(pts[j].tMs) < 40.f) --j;
            float dist = 0;
            for (int k = j + 1; k <= i; ++k) dist += std::hypot(pts[k].x - pts[k - 1].x, pts[k].y - pts[k - 1].y);
            const float dt = std::max(8.f, float(pts[i].tMs) - float(pts[j].tMs));
            w *= std::clamp(1.f - curve.speedThinning * (dist / dt), curve.speedFloor, 1.f);
        }
        hw[i] = std::max(w, 0.3f) * 0.5f;
    }

    if (n == 1) {
        appendArc(out, pts[0].x, pts[0].y, hw[0], 0.f, 2.f * std::numbers::pi_v<float>, 14, feather);
        return;
    }

    if (pen) {
        const float ang = std::atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x);
        appendArc(out, pts[0].x, pts[0].y, hw[0], ang + std::numbers::pi_v<float> / 2, ang + 3 * std::numbers::pi_v<float> / 2, 7, feather);
    }

    Side run; QVector<float> nxs, nys;
    run.l.reserve(n); run.r.reserve(n); nxs.reserve(n); nys.reserve(n);
    for (int i = 0; i < n; ++i) {
        const InkPoint &p = pts[i];
        float dx, dy;
        if (i == 0)          { dx = pts[1].x - p.x;              dy = pts[1].y - p.y; }
        else if (i == n - 1) { dx = p.x - pts[i - 1].x;          dy = p.y - pts[i - 1].y; }
        else                 { dx = pts[i + 1].x - pts[i - 1].x; dy = pts[i + 1].y - pts[i - 1].y; }
        float len = std::hypot(dx, dy);
        if (len < 1e-5f) { dx = 1; dy = 0; len = 1; }
        dx /= len; dy /= len;
        float nx = -dy, ny = dx;
        float scale = hw[i];
        if (i > 0 && i < n - 1) {
            const float sx = p.x - pts[i - 1].x, sy = p.y - pts[i - 1].y;
            const float sl = std::hypot(sx, sy);
            const float ex = pts[i + 1].x - p.x, ey = pts[i + 1].y - p.y;
            const float el = std::hypot(ex, ey);
            if (sl > 1e-5f && el > 1e-5f) {
                const float turn = 1.f - (sx * ex + sy * ey) / (sl * el); // 0 straight … 2 reversal
                if (turn > 0.15f) {
                    // Sharp corner: end the run here, drop a round joint, start a new run.
                    run.l.append({p.x + nx * hw[i], p.y + ny * hw[i], 1.f});
                    run.r.append({p.x - nx * hw[i], p.y - ny * hw[i], 1.f});
                    nxs.append(nx); nys.append(ny);
                    flushRun(out, run, feather, nxs, nys);
                    run = Side{}; nxs.clear(); nys.clear();
                    appendArc(out, p.x, p.y, hw[i], 0.f, 2.f * std::numbers::pi_v<float>, 10, feather);
                    continue;
                }
                const float cosHalf = std::abs((-sy / sl) * nx + (sx / sl) * ny);
                scale = hw[i] / std::max(cosHalf, 0.6f);
            }
        }
        run.l.append({p.x + nx * scale, p.y + ny * scale, 1.f});
        run.r.append({p.x - nx * scale, p.y - ny * scale, 1.f});
        nxs.append(nx); nys.append(ny);
    }
    flushRun(out, run, feather, nxs, nys);

    if (pen) {
        const float ang = std::atan2(pts[n - 1].y - pts[n - 2].y, pts[n - 1].x - pts[n - 2].x);
        appendArc(out, pts[n - 1].x, pts[n - 1].y, hw[n - 1], ang - std::numbers::pi_v<float> / 2, ang + std::numbers::pi_v<float> / 2, 7, feather);
    } else if (feather > 0) {
        // Highlighter: square ends get a feathered rim too.
    }
}
