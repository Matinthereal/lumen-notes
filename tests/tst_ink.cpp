#include <QtTest>
#include <numbers>
#include <cmath>
#include "ink/inkdocument.h"
#include "ink/onefilter.h"
#include "ink/shapes.h"
#include "ink/tessellate.h"
#include "ink/undostack.h"

static QVector<InkPoint> line(float x0, float y0, float x1, float y1, int n, float pressure = 0.5f)
{
    QVector<InkPoint> v;
    for (int i = 0; i < n; ++i) {
        const float t = n > 1 ? float(i) / (n - 1) : 0.f;
        v.append({x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, pressure, 0, 0, quint32(i * 3)});
    }
    return v;
}
static Stroke mk(const QVector<InkPoint> &pts, float w = 1.5f) { Stroke s; s.points = pts; s.width = w; s.updateBounds(); return s; }

class TstInk : public QObject {
    Q_OBJECT
private slots:
    void pressureCurveFromTheProbe() {
        const PressureCurve c = PressureCurve::forStyle(PenStyle::Classic);   // the default: Phase 1's pen
        QCOMPARE(c.widthFor(1.5f, 0.f), 0.75f);
        QVERIFY(std::abs(c.widthFor(1.5f, 0.85f) - 2.25f) < 1e-5f);
        QVERIFY(std::abs(c.widthFor(1.5f, 1.0f) - 2.25f) < 1e-5f);   // clamped at the ceiling
        const PressureCurve f = PressureCurve::forStyle(PenStyle::Fountain);
        QVERIFY(f.widthFor(1.5f, 0.3f) > f.minScale * 1.5f + (f.maxScale - f.minScale) * 1.5f * 0.3f / 0.85f); // gamma < 1
    }
    void tessellationIsDeterministicAndFinite() {
        QVector<InkPoint> pts = line(0, 0, 200, 40, 30);
        pts[10].y += 30; pts[20].y -= 30; // a couple of corners
        QVector<InkVertex> a, b;
        buildRibbon(smoothStroke(pts, 2.f), 1.5f, InkTool::Pen, PressureCurve{}, a);
        buildRibbon(smoothStroke(pts, 2.f), 1.5f, InkTool::Pen, PressureCurve{}, b);
        QVERIFY(a.size() > 60);
        QCOMPARE(a.size(), b.size());
        for (int i = 0; i < a.size(); ++i) {
            QCOMPARE(a[i].x, b[i].x); QCOMPARE(a[i].y, b[i].y);
            QVERIFY(std::isfinite(a[i].x) && std::isfinite(a[i].y));
        }
    }
    void fastStraightLineStaysStraightAndEven() {
        // 2 px per ms with ms-quantised timestamps (2,3,4 ms gaps), like the real pen at speed
        QVector<InkPoint> pts; float x = 0; quint32 t = 0; const int gaps[] = {2, 3, 4, 3, 2, 3};
        for (int i = 0; i < 60; ++i) { pts.append({x, 100.f, 0.6f, 0, 0, t}); const int g = gaps[i % 6]; t += g; x += 2.f * g; }
        for (PenStyle style : {PenStyle::Classic, PenStyle::Fountain}) {
            QVector<InkVertex> v;
            buildRibbon(smoothStroke(pts, 2.f), 1.5f, InkTool::Pen, PressureCurve::forStyle(style), v, 0.f);
            float minY = 1e9f, maxY = -1e9f;
            for (const InkVertex &q : v) if (q.x > 20 && q.x < x - 20 && q.a == 1.f) { minY = std::min(minY, q.y); maxY = std::max(maxY, q.y); }
            const float half = (maxY - minY) / 2;
            QVERIFY2(std::abs((minY + maxY) / 2 - 100.f) < 0.05f, "centreline drifted");
            // every core vertex sits on one of the two edges: no pulsing width
            for (const InkVertex &q : v) if (q.x > 20 && q.x < x - 20 && q.a == 1.f)
                QVERIFY2(std::abs(std::abs(q.y - 100.f) - half) < 0.06f * half + 0.02f, "edge wobbles");
        }
    }
    void singlePointBecomesADot() {
        QVector<InkVertex> v;
        buildRibbon({{10, 10, 0.5f, 0, 0, 0}}, 2.f, InkTool::Pen, PressureCurve{}, v);
        QVERIFY(v.size() >= 24);
    }
    void smoothingResamplesAtSpacing() {
        const auto out = smoothStroke(line(0, 0, 100, 0, 3), 2.f);
        QVERIFY(out.size() >= 48);
        for (int i = 1; i < out.size(); ++i)
            QVERIFY(std::hypot(out[i].x - out[i - 1].x, out[i].y - out[i - 1].y) <= 3.1f);
        QCOMPARE(out.first().x, 0.f);
        QVERIFY(std::abs(out.last().x - 100.f) < 1e-3f);
    }
    void predictionLeadsOnStraightAndDampsOnReversal() {
        const auto straight = line(0, 0, 90, 0, 31); // 3 px per 3 ms → 1 px/ms
        const InkPoint p = predictPoint(straight, 16.f, 1.f);
        QVERIFY(p.x > 90.f + 10.f && p.x < 90.f + 17.f);
        QVector<InkPoint> flip = straight;
        flip.append({80, 0, 0.5f, 0, 0, 93});          // the sample where the direction reverses
        const InkPoint q = predictPoint(flip, 16.f, 1.f);
        QVERIFY(std::abs(q.x - 80.f) < 1.f);            // damped to a stop: no overshoot past the tip
        flip.append({70, 0, 0.5f, 0, 0, 96});          // two consistent backward samples → predict backward
        const InkPoint r = predictPoint(flip, 16.f, 1.f);
        QVERIFY(r.x < 70.f);
    }
    void featherAddsRimVerticesWithZeroAlpha() {
        QVector<InkVertex> plain, feathered;
        const auto pts = smoothStroke(line(0, 0, 100, 20, 12), 2.f);
        buildRibbon(pts, 1.5f, InkTool::Pen, PressureCurve{}, plain, 0.f);
        buildRibbon(pts, 1.5f, InkTool::Pen, PressureCurve{}, feathered, 0.8f);
        QVERIFY(feathered.size() > plain.size() * 2);
        int rim = 0; for (const InkVertex &v : feathered) if (v.a == 0.f) ++rim;
        QVERIFY(rim > plain.size() / 2);
        for (const InkVertex &v : plain) QCOMPARE(v.a, 1.f);
    }
    void trueEdgesKeepTheNominalWidthAndTheLine() {
        // Classic's rim lies outside the stroke; true edges' straddles it, so half-coverage is at
        // the nominal edge. The centreline (where the ink is) must not move at all: no new wobble.
        QVector<InkPoint> pts; for (int i = 0; i < 40; ++i) pts.append({10.f + i * 2.f, 100.f, 0.6f, 0, 0, quint32(i * 4)});
        const float f = 0.5f;
        const PressureCurve curve;
        const float half = std::max(curve.widthFor(1.5f, 0.6f), 0.3f) * 0.5f;
        const auto edges = [&](bool centred, float &core, float &rim, float &mid) {
            QVector<InkVertex> v;
            buildRibbon(smoothStroke(pts, 2.f), 1.5f, InkTool::Pen, curve, v, f, centred);
            core = 0; rim = 0; float lo = 1e9f, hi = -1e9f;
            for (const InkVertex &q : v) {
                if (q.x < 20 || q.x > 80) continue;
                const float d = std::abs(q.y - 100.f);
                if (q.a == 1.f) { core = std::max(core, d); lo = std::min(lo, q.y); hi = std::max(hi, q.y); }
                else rim = std::max(rim, d);
            }
            mid = (lo + hi) / 2;
        };
        float c0, r0, m0, c1, r1, m1;
        edges(false, c0, r0, m0);
        edges(true, c1, r1, m1);
        QVERIFY2(std::abs(c0 - half) < 1e-3f && std::abs(r0 - (half + f)) < 1e-3f, "classic changed");
        QVERIFY2(std::abs((c1 + r1) / 2 - half) < 1e-3f, "half-coverage is not at the nominal edge");
        QVERIFY2(std::abs(r1 - c1 - f) < 1e-3f, "the rim is not one feather wide");
        QCOMPARE(m0, m1);
    }
    void trueEdgesFeatherHighlighterEnds() {
        const auto pts = line(10, 50, 90, 50, 20);
        const float f = 0.5f;
        QVector<InkVertex> classic, centred;
        buildRibbon(pts, 6.f, InkTool::Highlighter, PressureCurve{}, classic, f, false);
        buildRibbon(pts, 6.f, InkTool::Highlighter, PressureCurve{}, centred, f, true);
        float cMin = 1e9f, cMax = -1e9f, tMin = 1e9f, tMax = -1e9f;
        for (const InkVertex &v : classic) { cMin = std::min(cMin, v.x); cMax = std::max(cMax, v.x); }
        for (const InkVertex &v : centred) {
            QVERIFY(std::isfinite(v.x) && std::isfinite(v.y));
            if (v.a == 0.f) { tMin = std::min(tMin, v.x); tMax = std::max(tMax, v.x); }
        }
        QVERIFY2(cMin > 10.f - 0.01f && cMax < 90.f + 0.01f, "classic highlighter ends changed");
        QVERIFY2(tMin < 10.f - 0.9f * f && tMax > 90.f + 0.9f * f, "square ends have no rim");
    }
    void centripetalPassesThroughEverySampleWithoutLooping() {
        // Bunched then spread samples, as a pen slowing into a turn and leaving it fast: uniform
        // knots can overshoot here; centripetal ones must not, and must still hit every sample.
        QVector<InkPoint> raw;
        const float xs[] = {0, 12, 24, 24.6f, 25.1f, 25.4f, 40, 60, 80};
        for (int i = 0; i < 9; ++i) raw.append({xs[i], i < 5 ? 0.f : float(i - 4) * 0.6f, 0.5f, 0, 0, quint32(i * 3)});
        const auto out = smoothStroke(raw, 0.5f, true);
        for (const InkPoint &p : raw) {
            bool hit = false;
            for (const InkPoint &q : out) hit = hit || (std::abs(q.x - p.x) < 1e-3f && std::abs(q.y - p.y) < 1e-3f);
            QVERIFY2(hit, "a sample is not on the curve");
        }
        for (int i = 1; i < out.size(); ++i) {
            QVERIFY(std::isfinite(out[i].x) && std::isfinite(out[i].y));
            QVERIFY2(out[i].x >= out[i - 1].x - 1e-3f, "the curve doubles back: a loop");
        }
    }
    void oneEuro2DKeepsTheShapeWherePerAxisBendsIt() {
        // A circle at handwriting speed. One cutoff for both axes shrinks it evenly (lag along the
        // path); a cutoff per axis changes with each axis's speed, so the radius wobbles.
        const double r0 = 40, hz = 330, rev = 0.6;          // 0.6 s a turn ≈ 420 px/s
        OneEuroFilter fx(3.0, 0.03), fy(3.0, 0.03);
        OneEuroFilter2D f2(3.0, 0.03);
        double lo1 = 1e9, hi1 = 0, lo2 = 1e9, hi2 = 0;
        for (int i = 0; i < int(hz * rev * 3); ++i) {
            const double t = i / hz, a = 2 * std::numbers::pi * t / rev;
            const double x = 100 + r0 * std::cos(a), y = 100 + r0 * std::sin(a);
            const double ax = fx.filter(x, t), ay = fy.filter(y, t);
            double bx, by; f2.filter(x, y, t, bx, by);
            if (t < rev) continue;                          // settled after one turn
            const double r1 = std::hypot(ax - 100, ay - 100), r2 = std::hypot(bx - 100, by - 100);
            lo1 = std::min(lo1, r1); hi1 = std::max(hi1, r1); lo2 = std::min(lo2, r2); hi2 = std::max(hi2, r2);
        }
        QVERIFY2(hi2 - lo2 < 0.05, qPrintable(QStringLiteral("2D radius varies by %1").arg(hi2 - lo2)));
        QVERIFY2(hi1 - lo1 > 4 * (hi2 - lo2), qPrintable(QStringLiteral("per-axis %1 vs 2D %2").arg(hi1 - lo1).arg(hi2 - lo2)));
    }
    void steadyStrokeHasNoLagAndStaysOnTheLine() {
        // A straight line with ±0.3 px of digitiser noise across it.
        QVector<InkPoint> raw;
        for (int i = 0; i < 200; ++i) raw.append({float(i) * 0.7f, 50.f + ((i * 7919) % 13 - 6) * 0.05f, 0.4f + 0.1f * float(i % 3), 0, 0, quint32(i * 3)});
        const auto out = steadyStroke(raw, 1.f);
        QCOMPARE(out.size(), raw.size());
        QCOMPARE(out.first().x, raw.first().x); QCOMPARE(out.first().y, raw.first().y);
        QCOMPARE(out.last().x, raw.last().x); QCOMPARE(out.last().y, raw.last().y);   // the tip is the pen: no lag
        float rawDev = 0, outDev = 0;
        for (int i = 20; i < 180; ++i) { rawDev += std::abs(raw[i].y - 50.f); outDev += std::abs(out[i].y - 50.f); }
        QVERIFY2(outDev < 0.5f * rawDev, "the noise is not steadied");
        for (const InkPoint &p : out) QVERIFY(std::abs(p.y - 50.f) <= 0.31f);      // never further off than the noise
        // As a prefix, the last sample is still exactly the pen: what is on screen never trails.
        const auto live = steadyStroke(raw.mid(0, 90), 1.f);
        QCOMPARE(live.last().x, raw[89].x); QCOMPARE(live.last().y, raw[89].y);
    }
    void penStylesDiffer() {
        const PressureCurve f = PressureCurve::forStyle(PenStyle::Fountain), b = PressureCurve::forStyle(PenStyle::Ballpoint);
        QVERIFY(f.widthFor(1.5f, 0.1f) < b.widthFor(1.5f, 0.1f));   // a light fountain touch is thin
        QVERIFY(f.widthFor(1.5f, 0.85f) > b.widthFor(1.5f, 0.85f)); // a firm one is wide
        QVERIFY(std::abs(b.widthFor(1.5f, 0.85f) - b.widthFor(1.5f, 0.1f)) < 0.5f);
    }
    void oneEuroIsDeterministicAndFollowsFastMotion() {
        OneEuroFilter a(2.5, 0.03), b(2.5, 0.03);
        double xa = 0, xb = 0;
        for (int i = 0; i < 100; ++i) { xa = a.filter(i * 2.0, i * 0.003); xb = b.filter(i * 2.0, i * 0.003); }
        QCOMPARE(xa, xb);
        QVERIFY(std::abs(xa - 198.0) < 6.0); // fast, steady motion: nearly no lag
        OneEuroFilter j(2.5, 0.03); double last = 0;
        for (int i = 0; i < 300; ++i) last = j.filter((i % 2) ? 10.4 : 10.0, i * 0.003); // 0.4 px jitter at rest
        QVERIFY(std::abs(last - 10.2) < 0.12);                                          // …is smoothed away
    }
    void shapesAreRecognised() {
        QVector<InkPoint> out;
        // wobbly line
        QVector<InkPoint> ln; for (int i = 0; i <= 40; ++i) ln.append({i * 5.f, 100 + ((i % 3) - 1) * 1.5f, 0.5f, 0, 0, quint32(i * 3)});
        QCOMPARE(int(recognizeShape(ln, out)), int(ShapeKind::Line)); QCOMPARE(out.size(), 2);
        // rough circle
        QVector<InkPoint> ci; for (int i = 0; i <= 60; ++i) { const float a = i / 60.f * 6.2832f; ci.append({200 + 50 * std::cos(a) + ((i % 2) ? 2.f : -2.f), 200 + 45 * std::sin(a), 0.5f, 0, 0, quint32(i * 3)}); }
        QCOMPARE(int(recognizeShape(ci, out)), int(ShapeKind::Ellipse)); QVERIFY(out.size() > 30);
        // rectangle drawn as four sides
        QVector<InkPoint> re; int t = 0;
        auto edge = [&](float x0, float y0, float x1, float y1) { for (int i = 0; i < 15; ++i) re.append({x0 + (x1 - x0) * i / 15.f, y0 + (y1 - y0) * i / 15.f, 0.5f, 0, 0, quint32(t++ * 3)}); };
        edge(0, 0, 120, 0); edge(120, 0, 120, 80); edge(120, 80, 0, 80); edge(0, 80, 0, 3);
        QCOMPARE(int(recognizeShape(re, out)), int(ShapeKind::Rectangle)); QCOMPARE(out.size(), 5);
        // arrow: shaft then a head folding back
        QVector<InkPoint> ar; t = 0;
        for (int i = 0; i <= 40; ++i) ar.append({i * 4.f, 50.f, 0.5f, 0, 0, quint32(t++ * 3)});
        for (int i = 1; i <= 8; ++i) ar.append({160 - i * 2.5f, 50 - i * 2.5f, 0.5f, 0, 0, quint32(t++ * 3)});
        QCOMPARE(int(recognizeShape(ar, out)), int(ShapeKind::Arrow)); QCOMPARE(out.size(), 5);
        // scribble stays freehand
        QVector<InkPoint> sc; for (int i = 0; i < 60; ++i) sc.append({i * 3.f, float(((i * 7919) % 40)), 0.5f, 0, 0, quint32(i * 3)});
        QCOMPARE(int(recognizeShape(sc, out)), int(ShapeKind::None));
    }
    void documentHitAndLasso() {
        InkDocument d;
        const quint64 a = d.addStroke(mk(line(0, 0, 100, 0, 11)));
        const quint64 b = d.addStroke(mk(line(0, 50, 100, 50, 11)));
        QCOMPARE(d.count(), 2);
        QCOMPARE(d.hitCircle(QPointF(50, 2), 4.f), QVector<quint64>{a});
        QVERIFY(d.hitCircle(QPointF(50, 25), 4.f).isEmpty());
        QPolygonF lasso; lasso << QPointF(-5, 40) << QPointF(105, 40) << QPointF(105, 60) << QPointF(-5, 60);
        QCOMPARE(d.insidePolygon(lasso), QVector<quint64>{b});
    }
    void splitByCircleMakesTwoPieces() {
        const Stroke s = mk(line(0, 0, 100, 0, 21), 2.f);
        const auto pieces = InkDocument::splitByCircle(s, QPointF(50, 0), 6.f);
        QCOMPARE(pieces.size(), 2);
        QVERIFY(pieces[0].points.last().x < 44.f);
        QVERIFY(pieces[1].points.first().x > 56.f);
    }
    void undoRedoKeepsOrderAndIds() {
        InkDocument d; UndoStack u(&d);
        auto c1 = std::make_unique<AddStrokeCommand>(mk(line(0, 0, 10, 0, 3)));
        auto c2 = std::make_unique<AddStrokeCommand>(mk(line(0, 5, 10, 5, 3)));
        auto c3 = std::make_unique<AddStrokeCommand>(mk(line(0, 9, 10, 9, 3)));
        u.push(std::move(c1)); u.push(std::move(c2)); u.push(std::move(c3));
        const quint64 idMiddle = d.strokes()[1].id;
        QCOMPARE(d.count(), 3);
        u.push(std::make_unique<RemoveStrokesCommand>(QVector<quint64>{idMiddle}));
        QCOMPARE(d.count(), 2);
        u.undo();
        QCOMPARE(d.count(), 3);
        QCOMPARE(d.strokes()[1].id, idMiddle); // back in the middle, same id
        u.undo(); u.undo();
        QCOMPARE(d.count(), 1);
        u.redo(); u.redo(); u.redo();
        QCOMPARE(d.count(), 2);
        QVERIFY(!u.canRedo());
    }
    void replaceAndTransformRoundTrip() {
        InkDocument d; UndoStack u(&d);
        u.push(std::make_unique<AddStrokeCommand>(mk(line(0, 0, 100, 0, 21), 2.f)));
        const quint64 id = d.strokes()[0].id;
        const auto pieces = InkDocument::splitByCircle(*d.stroke(id), QPointF(50, 0), 6.f);
        u.push(std::make_unique<ReplaceStrokesCommand>(QVector<quint64>{id}, pieces));
        QCOMPARE(d.count(), 2);
        u.undo();
        QCOMPARE(d.count(), 1); QCOMPARE(d.strokes()[0].id, id);
        u.redo();
        const QVector<quint64> ids{d.strokes()[0].id, d.strokes()[1].id};
        u.push(std::make_unique<TransformStrokesCommand>(ids, QTransform::fromTranslate(10, 20)));
        QVERIFY(std::abs(d.strokes()[0].points[0].y - 20.f) < 1e-4f);
        u.undo();
        QVERIFY(std::abs(d.strokes()[0].points[0].y) < 1e-4f);
    }
    void batchRemovalWithNoMatchesLeavesStrokesIntact() {
        InkDocument d;
        for (int i = 0; i < 3; ++i) { Stroke s; s.id = d.nextId(); s.points = {{0, 0, 1, 0, 0, 0}, {10, float(i), 1, 0, 0, 1}}; s.updateBounds(); d.addStroke(s); }
        QCOMPARE(d.removeStrokes({}).size(), 0);
        QCOMPARE(d.removeStrokes({999}).size(), 0);
        for (const Stroke &s : d.strokes()) QCOMPARE(s.points.size(), 2);   // nothing may be moved-from
        const auto gone = d.removeStrokes({2, 999});
        QCOMPARE(gone.size(), 1); QCOMPARE(gone[0].first, 1); QCOMPARE(gone[0].second.id, quint64(2));
        QCOMPARE(d.count(), 2); QCOMPARE(d.indexOf(3), 1);
        UndoStack u(&d);
        u.push(std::make_unique<RemoveStrokesCommand>(QVector<quint64>{1, 3}));
        QCOMPARE(d.count(), 0);
        u.undo();
        QCOMPARE(d.count(), 2); QCOMPARE(d.strokes()[0].id, quint64(1)); QCOMPARE(d.strokes()[1].id, quint64(3));
        QCOMPARE(d.strokes()[1].points.size(), 2);
    }
    void undoStackIsBounded() {
        InkDocument d; UndoStack u(&d);
        for (int i = 0; i < 2400; ++i) { Stroke s; s.points = {{0, 0, 1, 0, 0, 0}}; u.push(std::make_unique<AddStrokeCommand>(s)); }
        int undone = 0; while (u.canUndo()) { u.undo(); ++undone; }
        QVERIFY2(undone < 2400 && undone >= 1800, qPrintable(QString::number(undone)));
    }
    void scratchOutIsToldFromWriting() {
        const auto strokeOf = [](std::initializer_list<QPointF> pts) {
            QVector<InkPoint> out;
            for (const QPointF &p : pts) out.append({float(p.x()), float(p.y()), 1.f, 0, 0, quint32(out.size() * 8)});
            return out;
        };
        // a deliberate cross-out: four sweeps back and forth along a narrow band
        QVector<InkPoint> scribble;
        for (int sweep = 0; sweep < 5; ++sweep) {
            for (int i = 0; i <= 10; ++i) {
                const float t = sweep % 2 ? 1.f - i / 10.f : i / 10.f;
                scribble.append({40.f + t * 160.f, 100.f + (i % 2) * 4.f, 1.f, 0, 0, quint32(scribble.size() * 8)});
            }
        }
        QVERIFY2(looksLikeScratchOut(scribble), "a five-sweep scribble is a cross-out");

        // a straight line is not
        QVector<InkPoint> line;
        for (int i = 0; i <= 40; ++i) line.append({40.f + i * 4.f, 100.f, 1.f, 0, 0, quint32(i * 8)});
        QVERIFY(!looksLikeScratchOut(line));

        // a zig-zag drawing that keeps advancing is not: it never doubles back
        QVector<InkPoint> zigzag;
        for (int i = 0; i <= 40; ++i) zigzag.append({40.f + i * 5.f, 100.f + (i % 2 ? 30.f : 0.f), 1.f, 0, 0, quint32(i * 8)});
        QVERIFY2(!looksLikeScratchOut(zigzag), "a zig-zag that travels is a drawing, not a cross-out");

        // a written "w" reverses twice, not four times
        QVERIFY(!looksLikeScratchOut(strokeOf({{40, 100}, {50, 140}, {60, 105}, {70, 140}, {80, 100},
                                               {80, 100}, {80, 100}, {80, 100}, {80, 100}, {80, 100}, {80, 100}, {80, 100}})));

        // a tiny scribble (a dot of hesitation) is left alone
        QVector<InkPoint> tiny;
        for (int sweep = 0; sweep < 6; ++sweep)
            for (int i = 0; i <= 3; ++i) tiny.append({50.f + (sweep % 2 ? 3 - i : i) * 2.f, 100.f, 1.f, 0, 0, quint32(tiny.size() * 8)});
        QVERIFY(!looksLikeScratchOut(tiny));
    }
};
QTEST_GUILESS_MAIN(TstInk)
#include "tst_ink.moc"
