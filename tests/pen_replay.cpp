// Replays real strokes through each pen pipeline and measures what the hand feels: how far the ink
// trails the pen tip (lag), how far it strays across the path (shape), how much its direction
// twitches (wobble), and where it ends. The rule for any pen change (ADR mynotes-003): no added lag
// or wobble against today's raw ink, proven here on the maker's own strokes, before it is offered.
//
//   pen_replay <copy of lumen.db> [spacing]
//
// Reads a COPY: the stroke blobs are only decoded, nothing is written. Not a ctest — it needs a
// library of real handwriting, which the repository does not carry.
#include <QCoreApplication>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <functional>
#include "ink/onefilter.h"
#include "ink/tessellate.h"
#include "storage/database.h"
#include "storage/strokecodec.h"

namespace {

struct Metrics {
    double lagMs = 0, lagPx95 = 0, acrossPx95 = 0, acrossPxMax = 0, wobble = 0, flips = 0, endPx = 0, settlePx = -1;
};

// The drawn line resampled every 1 px along its length, so wobble is compared at one density
// whatever each pipeline emits.
QVector<InkPoint> everyPixel(const QVector<InkPoint> &r)
{
    QVector<InkPoint> out;
    if (r.isEmpty()) return out;
    out.append(r[0]);
    double carry = 0;
    for (int i = 1; i < r.size(); ++i) {
        const double dx = r[i].x - r[i - 1].x, dy = r[i].y - r[i - 1].y, l = std::hypot(dx, dy);
        double at = 1.0 - carry;
        while (at <= l) { InkPoint p = r[i - 1]; p.x = float(r[i - 1].x + dx * at / l); p.y = float(r[i - 1].y + dy * at / l); out.append(p); at += 1.0; }
        carry = l - (at - 1.0);
    }
    return out;
}

// A filter pass: the points the canvas would store, sample by sample, from the raw samples.
using Filter = std::function<QVector<InkPoint>(const QVector<InkPoint> &)>;

double percentile(QVector<double> v, double p)
{
    if (v.isEmpty()) return 0;
    std::sort(v.begin(), v.end());
    return v[std::clamp(int(p * (v.size() - 1) + 0.5), 0, int(v.size() - 1))];
}

double distToSegment(double px, double py, const InkPoint &a, const InkPoint &b)
{
    const double dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy;
    const double t = l2 > 0 ? std::clamp(((px - a.x) * dx + (py - a.y) * dy) / l2, 0.0, 1.0) : 0.0;
    return std::hypot(px - (a.x + t * dx), py - (a.y + t * dy));
}

// Distance from a point to the raw polyline, searched near where it should be.
double acrossPath(double x, double y, const QVector<InkPoint> &raw, int near)
{
    double best = 1e9;
    for (int k = std::max(0, near - 20); k < std::min(int(raw.size()) - 1, near + 20); ++k)
        best = std::min(best, distToSegment(x, y, raw[k], raw[k + 1]));
    return best;
}

// Total variation of the turning angle per 100 px of ink, and how often the turn changes sign:
// a clean curve turns steadily one way, a wobbly one keeps correcting.
void wobbleOf(const QVector<InkPoint> &r, double &variation, double &flips)
{
    variation = 0; flips = 0;
    double length = 0, prevTurn = 0; int prevSign = 0;
    for (int j = 1; j + 1 < r.size(); ++j) {
        const double ax = r[j].x - r[j - 1].x, ay = r[j].y - r[j - 1].y, bx = r[j + 1].x - r[j].x, by = r[j + 1].y - r[j].y;
        const double la = std::hypot(ax, ay), lb = std::hypot(bx, by);
        length += la;
        if (la < 1e-4 || lb < 1e-4) continue;
        const double turn = std::atan2(ax * by - ay * bx, ax * bx + ay * by);
        if (j > 1) variation += std::abs(turn - prevTurn);
        const int sign = turn > 0.02 ? 1 : (turn < -0.02 ? -1 : 0);
        if (sign && prevSign && sign != prevSign) flips += 1;
        if (sign) prevSign = sign;
        prevTurn = turn;
    }
    if (length > 1) { variation *= 100.0 / length; flips *= 100.0 / length; }
}

Metrics measure(const QVector<InkPoint> &raw, const Filter &filter, bool centripetal, float spacing, float sigma, bool settle)
{
    Metrics m;
    const QVector<InkPoint> stored = filter(raw);
    const QVector<InkPoint> shaped = sigma > 0 ? steadyStroke(stored, sigma) : stored;
    // Lag while drawing: stored point i is what is on screen when the pen is at raw point i.
    // (The filters here keep one stored point per raw sample until the pen lifts.)
    QVector<double> lagMs, lagPx, across;
    for (int i = 3; i < raw.size() && i < stored.size(); ++i) {
        const double d = std::hypot(stored[i].x - raw[i].x, stored[i].y - raw[i].y);
        lagPx.append(d);
        across.append(acrossPath(shaped[i].x, shaped[i].y, raw, i));
        int j = i; while (j > 0 && double(raw[i].tMs) - double(raw[j].tMs) < 20.0) --j;
        double path = 0; for (int k = j + 1; k <= i; ++k) path += std::hypot(raw[k].x - raw[k - 1].x, raw[k].y - raw[k - 1].y);
        const double dt = std::max(1.0, double(raw[i].tMs) - double(raw[j].tMs));
        const double speed = path / dt;                                  // px per ms
        if (speed > 0.1) lagMs.append(d / speed);
    }
    double sum = 0; for (double v : lagMs) sum += v;
    m.lagMs = lagMs.isEmpty() ? 0 : sum / lagMs.size();
    m.lagPx95 = percentile(lagPx, 0.95);
    m.acrossPx95 = percentile(across, 0.95);
    m.acrossPxMax = across.isEmpty() ? 0 : *std::max_element(across.begin(), across.end());
    const QVector<InkPoint> rendered = smoothStroke(shaped, spacing, centripetal);
    wobbleOf(everyPixel(rendered), m.wobble, m.flips);
    // Settle: how far a sample moves after it is first drawn, as the samples after it arrive.
    if (settle && sigma > 0) {
        m.settlePx = 0;
        for (int len = 3; len < stored.size(); len += 3) {
            const QVector<InkPoint> then = steadyStroke(stored.mid(0, len), sigma);
            for (int k = 0; k < len; ++k)
                m.settlePx = std::max(m.settlePx, std::hypot(double(then[k].x - shaped[k].x), double(then[k].y - shaped[k].y)));
        }
    }
    m.endPx = rendered.isEmpty() ? 0 : std::hypot(rendered.last().x - raw.last().x, rendered.last().y - raw.last().y);
    return m;
}

QVector<InkPoint> rawPass(const QVector<InkPoint> &raw) { return raw; }

// Today's opt-in smoothing (the slider): 1€ on each axis apart, EMA on pressure.
Filter legacy(double strength)
{
    return [strength](const QVector<InkPoint> &raw) {
        const double minCutoff = 6.0 - 4.5 * strength;
        OneEuroFilter fx(minCutoff, 0.03), fy(minCutoff, 0.03);
        QVector<InkPoint> out;
        for (const InkPoint &p : raw) {
            InkPoint q = p;
            q.x = float(fx.filter(p.x, p.tMs / 1000.0));
            q.y = float(fy.filter(p.y, p.tMs / 1000.0));
            out.append(q);
        }
        return out;
    };
}

// The candidate: one cutoff from the speed along the path, and the tail caught up on lift.
Filter steady(double minCutoff, double beta)
{
    return [minCutoff, beta](const QVector<InkPoint> &raw) {
        OneEuroFilter2D f(minCutoff, beta);
        QVector<InkPoint> out;
        for (const InkPoint &p : raw) {
            InkPoint q = p; double x, y;
            f.filter(p.x, p.y, p.tMs / 1000.0, x, y);
            q.x = float(x); q.y = float(y);
            out.append(q);
        }
        if (!raw.isEmpty()) out.append(raw.last());      // the pen lifted here: the ink ends here
        return out;
    };
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    if (argc < 2) { out << "usage: pen_replay <copy of lumen.db> [spacing]\n"; return 2; }
    const float spacing = argc > 2 ? QString::fromLocal8Bit(argv[2]).toFloat() : 2.f;
    Database db;
    if (!db.open(QString::fromLocal8Bit(argv[1]))) { out << "cannot open " << argv[1] << "\n"; return 1; }

    // Real pen strokes only: the benchmark's synthetic ones tick at exactly 3 ms.
    QVector<QVector<InkPoint>> strokes;
    Database::Query q(db, "SELECT data FROM stroke_blob");
    while (q.step()) {
        QVector<Stroke> page;
        if (!strokecodec::decode(q.blob(0), page)) continue;
        for (const Stroke &s : page) {
            if (s.tool != InkTool::Pen || s.points.size() < 8) continue;
            bool synthetic = true;
            for (int i = 1; i < s.points.size() && synthetic; ++i) synthetic = s.points[i].tMs - s.points[i - 1].tMs == 3;
            if (!synthetic) strokes.append(s.points);
        }
    }
    out << "strokes: " << strokes.size() << " real pen strokes, spline spacing " << spacing << " px\n\n";
    if (strokes.isEmpty()) return 1;

    struct Candidate { QString name; Filter filter; bool centripetal; float sigma = 0; };
    QVector<Candidate> candidates{
        {"raw (today's default)", rawPass, false},
        {"raw + centripetal spline", rawPass, true},
        {"legacy slider 0.5 (per-axis)", legacy(0.5), false},
        {"2D 1€ mc=3 beta=0.03", steady(3.0, 0.03), true},
        {"2D 1€ mc=5 beta=0.08", steady(5.0, 0.08), true},
    };
    for (float sigma : {0.6f, 1.0f, 1.5f, 2.0f, 3.0f})
        candidates.append({QStringLiteral("zero-lag steady σ=%1 px").arg(sigma), rawPass, true, sigma});

    out << "| pipeline | lag ms (mean) | lag px p95 | across-path px p95 | across max | wobble /100px | turn flips /100px | end px (p95) | settle px p95 |\n";
    out << "|---|---|---|---|---|---|---|---|---|\n";
    for (const Candidate &c : candidates) {
        QVector<double> lag, lag95, acr, acrMax, wob, flips, end, settled;
        for (int i = 0; i < strokes.size(); ++i) {
            const Metrics m = measure(strokes[i], c.filter, c.centripetal, spacing, c.sigma, i % 10 == 0);
            lag.append(m.lagMs); lag95.append(m.lagPx95); acr.append(m.acrossPx95); acrMax.append(m.acrossPxMax);
            wob.append(m.wobble); flips.append(m.flips); end.append(m.endPx);
            if (m.settlePx >= 0) settled.append(m.settlePx);
        }
        const auto mean = [](const QVector<double> &v) { double s = 0; for (double x : v) s += x; return v.isEmpty() ? 0 : s / v.size(); };
        out << "| " << c.name << " | " << QString::number(mean(lag), 'f', 2) << " | " << QString::number(mean(lag95), 'f', 2)
            << " | " << QString::number(mean(acr), 'f', 3) << " | " << QString::number(percentile(acrMax, 0.95), 'f', 2)
            << " | " << QString::number(mean(wob), 'f', 1) << " | " << QString::number(mean(flips), 'f', 2)
            << " | " << QString::number(percentile(end, 0.95), 'f', 2)
            << " | " << (settled.isEmpty() ? QStringLiteral("—") : QString::number(percentile(settled, 0.95), 'f', 2)) << " |\n";
    }
    return 0;
}
