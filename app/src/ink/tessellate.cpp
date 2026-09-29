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

static QVector<InkPoint> centripetalStroke(const QVector<InkPoint> &raw, float spacing)
{
    const int n = raw.size();
    QVector<InkPoint> out;
    out.reserve(n * 2);
    out.append(raw[0]);
    // Barry and Goldman's pyramid, alpha 0.5. Ends get a mirrored phantom point, so the first and
    // last spans have real knots instead of a zero-length one.
    const auto knot = [](float ax, float ay, float bx, float by) { return std::max(std::sqrt(std::hypot(bx - ax, by - ay)), 1e-3f); };
    for (int i = 0; i < n - 1; ++i) {
        const InkPoint &p1 = raw[i], &p2 = raw[i + 1];
        const float p0x = i > 0 ? raw[i - 1].x : 2 * p1.x - p2.x, p0y = i > 0 ? raw[i - 1].y : 2 * p1.y - p2.y;
        const float p3x = i + 2 < n ? raw[i + 2].x : 2 * p2.x - p1.x, p3y = i + 2 < n ? raw[i + 2].y : 2 * p2.y - p1.y;
        const float t0 = 0, t1 = t0 + knot(p0x, p0y, p1.x, p1.y), t2 = t1 + knot(p1.x, p1.y, p2.x, p2.y), t3 = t2 + knot(p2.x, p2.y, p3x, p3y);
        const float segLen = std::hypot(p2.x - p1.x, p2.y - p1.y);
        const int steps = std::max(1, int(std::ceil(segLen / spacing)));
        for (int s = 1; s <= steps; ++s) {
            const float u = float(s) / steps, t = t1 + (t2 - t1) * u;
            const auto mix = [](float a, float b, float ta, float tb, float t) { return a * (tb - t) / (tb - ta) + b * (t - ta) / (tb - ta); };
            const float a1x = mix(p0x, p1.x, t0, t1, t), a1y = mix(p0y, p1.y, t0, t1, t);
            const float a2x = mix(p1.x, p2.x, t1, t2, t), a2y = mix(p1.y, p2.y, t1, t2, t);
            const float a3x = mix(p2.x, p3x, t2, t3, t), a3y = mix(p2.y, p3y, t2, t3, t);
            const float b1x = mix(a1x, a2x, t0, t2, t), b1y = mix(a1y, a2y, t0, t2, t);
            const float b2x = mix(a2x, a3x, t1, t3, t), b2y = mix(a2y, a3y, t1, t3, t);
            InkPoint p = lerp(p1, p2, u);
            p.x = mix(b1x, b2x, t1, t2, t);
            p.y = mix(b1y, b2y, t1, t2, t);
            out.append(p);
        }
    }
    return out;
}

QVector<InkPoint> smoothStroke(const QVector<InkPoint> &raw, float spacing, bool centripetal)
{
    const int n = raw.size();
    if (n < 3 || spacing <= 0)
        return raw;
    if (centripetal)
        return centripetalStroke(raw, spacing);
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

QVector<InkPoint> steadyStroke(const QVector<InkPoint> &raw, float sigma)
{
    const int n = raw.size();
    if (n < 3 || sigma <= 0) return raw;
    QVector<float> s(n, 0.f);                                   // arc length at each sample
    for (int i = 1; i < n; ++i) s[i] = s[i - 1] + std::hypot(raw[i].x - raw[i - 1].x, raw[i].y - raw[i - 1].y);
    const float reach = 3.f * sigma, inv = 1.f / (2.f * sigma * sigma);
    QVector<InkPoint> out = raw;
    for (int i = 1; i < n - 1; ++i) {
        // Near an end the window is cut to what exists on both sides, so the average stays
        // centred on the sample and never pulls the line toward one side.
        const float half = std::min({reach, s[i] - s[0], s[n - 1] - s[i]});
        if (half <= 1e-4f) continue;
        float w = 0, x = 0, y = 0, pr = 0;
        for (int j = i; j >= 0 && s[i] - s[j] <= half; --j) {
            const float d = s[i] - s[j], k = std::exp(-d * d * inv);
            w += k; x += k * raw[j].x; y += k * raw[j].y; pr += k * raw[j].pressure;
        }
        for (int j = i + 1; j < n && s[j] - s[i] <= half; ++j) {
            const float d = s[j] - s[i], k = std::exp(-d * d * inv);
            w += k; x += k * raw[j].x; y += k * raw[j].y; pr += k * raw[j].pressure;
        }
        out[i].x = x / w; out[i].y = y / w; out[i].pressure = pr / w;
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
void appendArc(QVector<InkVertex> &out, float cx, float cy, float r, float a0, float a1, int segs, float feather, float core = 1.f)
{
    QVector<InkVertex> fan;
    for (int i = 0; i <= segs; ++i) {
        const float a = a0 + (a1 - a0) * float(i) / segs;
        fan.append({cx, cy, core});
        fan.append({cx + r * std::cos(a), cy + r * std::sin(a), core});
    }
    appendStrip(out, fan);
    if (feather > 0) {
        QVector<InkVertex> rim;
        for (int i = 0; i <= segs; ++i) {
            const float a = a0 + (a1 - a0) * float(i) / segs;
            const float c = std::cos(a), s = std::sin(a);
            rim.append({cx + r * c, cy + r * s, core});
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
                 QVector<InkVertex> &out, float feather, bool centred, const RibbonShade *shade)
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
        if (shade && i < shade->widthScale.size()) w *= shade->widthScale[i];
        hw[i] = std::max(w, 0.3f) * 0.5f;
    }
    const auto coreA = [&](int i) { return shade && i < shade->alpha.size() ? shade->alpha[i] : 1.f; };
    // True edges: pull the solid core in by half the rim, so the rim's midpoint is the edge. A line
    // thinner than the rim keeps a sliver of core and fades, as it would under a real rasteriser.
    if (centred && feather > 0)
        for (float &h : hw) h = std::max(h - feather * 0.5f, 0.05f);

    if (n == 1) {
        appendArc(out, pts[0].x, pts[0].y, hw[0], 0.f, 2.f * std::numbers::pi_v<float>, 14, feather, coreA(0));
        return;
    }

    if (pen) {
        const float ang = std::atan2(pts[1].y - pts[0].y, pts[1].x - pts[0].x);
        appendArc(out, pts[0].x, pts[0].y, hw[0], ang + std::numbers::pi_v<float> / 2, ang + 3 * std::numbers::pi_v<float> / 2, 7, feather, coreA(0));
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
                    run.l.append({p.x + nx * hw[i], p.y + ny * hw[i], coreA(i)});
                    run.r.append({p.x - nx * hw[i], p.y - ny * hw[i], coreA(i)});
                    nxs.append(nx); nys.append(ny);
                    flushRun(out, run, feather, nxs, nys);
                    run = Side{}; nxs.clear(); nys.clear();
                    appendArc(out, p.x, p.y, hw[i], 0.f, 2.f * std::numbers::pi_v<float>, 10, feather, coreA(i));
                    continue;
                }
                const float cosHalf = std::abs((-sy / sl) * nx + (sx / sl) * ny);
                scale = hw[i] / std::max(cosHalf, 0.6f);
            }
        }
        run.l.append({p.x + nx * scale, p.y + ny * scale, coreA(i)});
        run.r.append({p.x - nx * scale, p.y - ny * scale, coreA(i)});
        nxs.append(nx); nys.append(ny);
    }
    flushRun(out, run, feather, nxs, nys);

    if (pen) {
        const float ang = std::atan2(pts[n - 1].y - pts[n - 2].y, pts[n - 1].x - pts[n - 2].x);
        appendArc(out, pts[n - 1].x, pts[n - 1].y, hw[n - 1], ang - std::numbers::pi_v<float> / 2, ang + std::numbers::pi_v<float> / 2, 7, feather, coreA(n - 1));
    } else if (centred && feather > 0) {
        // Highlighter: its square ends get a rim as well, with a corner piece joining it to the
        // sides' rims, or the ends stay stair-stepped while the sides are smooth.
        const auto endRim = [&](int i, float tx, float ty) {
            const float nx = -ty, ny = tx, h = hw[i];
            const InkVertex l{pts[i].x + nx * h, pts[i].y + ny * h, 1.f}, r{pts[i].x - nx * h, pts[i].y - ny * h, 1.f};
            const float ox = tx * feather, oy = ty * feather;
            appendStrip(out, {l, {l.x + ox, l.y + oy, 0.f}, r, {r.x + ox, r.y + oy, 0.f}});
            appendStrip(out, {l, {l.x + nx * feather, l.y + ny * feather, 0.f}, {l.x + ox, l.y + oy, 0.f},
                              {l.x + nx * feather + ox, l.y + ny * feather + oy, 0.f}});
            appendStrip(out, {r, {r.x + ox, r.y + oy, 0.f}, {r.x - nx * feather, r.y - ny * feather, 0.f},
                              {r.x - nx * feather + ox, r.y - ny * feather + oy, 0.f}});
        };
        const auto unit = [](float dx, float dy) { const float l = std::max(std::hypot(dx, dy), 1e-5f); return std::pair{dx / l, dy / l}; };
        const auto [sx, sy] = unit(pts[0].x - pts[1].x, pts[0].y - pts[1].y);             // pointing back out of the start
        const auto [ex, ey] = unit(pts[n - 1].x - pts[n - 2].x, pts[n - 1].y - pts[n - 2].y); // and on out of the end
        endRim(0, sx, sy);
        endRim(n - 1, ex, ey);
    }
}

namespace {
float smoothstep(float e0, float e1, float x) { const float t = std::clamp((x - e0) / (e1 - e0), 0.f, 1.f); return t * t * (3 - 2 * t); }
// Value noise along the line: the same arc length always gives the same grain, so a stroke does
// not shimmer as it grows or when it is redrawn.
float grain(float s)
{
    const auto hash = [](int i) { quint32 h = quint32(i) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return float(h & 0xffff) / 65535.f; };
    const int i = int(std::floor(s)); const float f = s - float(i), u = f * f * (3 - 2 * f);
    return hash(i) + (hash(i + 1) - hash(i)) * u;
}
} // namespace

PressureCurve pencilCurve(float ceiling)
{
    // A pencil's line barely widens with pressure; it darkens (pencilShade does that).
    PressureCurve c; c.ceiling = ceiling; c.minScale = 0.8f; c.maxScale = 1.15f; c.gamma = 1.f;
    return c;
}

RibbonShade pencilShade(const QVector<InkPoint> &pts, float ceiling)
{
    RibbonShade shade;
    const int n = pts.size();
    shade.widthScale.resize(n); shade.alpha.resize(n);
    float s = 0;
    for (int i = 0; i < n; ++i) {
        if (i > 0) s += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
        const float p = ceiling > 0 ? std::clamp(pts[i].pressure / ceiling, 0.f, 1.f) : 1.f;
        const float lean = smoothstep(38.f, 58.f, std::hypot(pts[i].tiltX, pts[i].tiltY));   // 0 writing … 1 shading
        shade.widthScale[i] = 1.f + 2.6f * lean;
        const float tone = (0.38f + 0.57f * p) * (1.f - 0.5f * lean);
        shade.alpha[i] = std::clamp(tone * (0.8f + 0.2f * grain(s / 1.6f)), 0.05f, 1.f);
    }
    return shade;
}
