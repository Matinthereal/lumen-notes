#include <QPointingDevice>
#include <QSignalSpy>
#include <QTouchEvent>
#include <QtTest>
#include "canvas/inkcanvas.h"

// Drives InkCanvas through its TabletSink with synthesized samples (no window needed: an item
// outside a window maps scene == local). This is the "replay a recorded tablet stream" test.
class TstCanvas : public QObject {
    Q_OBJECT
    static TabletSample sample(TabletSample::Kind k, QPointF p, quint64 t, Qt::MouseButtons b = Qt::LeftButton, float pressure = 0.5f) {
        TabletSample s; s.kind = k; s.windowPos = p; s.timestampMs = t; s.buttons = b; s.pressure = pressure; return s;
    }
    static void stroke(InkCanvas &c, QPointF from, QPointF to, quint64 t0 = 1000, int n = 20) {
        c.tabletSample(sample(TabletSample::Kind::Press, from, t0));
        for (int i = 1; i <= n; ++i) c.tabletSample(sample(TabletSample::Kind::Move, from + (to - from) * i / n, t0 + i * 3));
        c.tabletSample(sample(TabletSample::Kind::Release, to, t0 + n * 3 + 3, Qt::NoButton));
    }
    static InkCanvas *make() {
        auto *c = new InkCanvas;
        c->setSize(QSizeF(800, 600));
        c->setZoom(1.0); c->setPan(QPointF(0, 0));
        return c;
    }
private slots:
    void penStrokeUndoRedo() {
        std::unique_ptr<InkCanvas> c(make());
        stroke(*c, {100, 100}, {300, 120});
        QCOMPARE(c->strokeCount(), 1);
        QVERIFY(c->canUndo());
        c->undo(); QCOMPARE(c->strokeCount(), 0);
        c->redo(); QCOMPARE(c->strokeCount(), 1);
        const Stroke &s = c->document()->strokes()[0];
        QVERIFY(s.points.size() >= 20);
        QCOMPARE(s.points.first().tMs, 0u);
        QVERIFY(s.points.last().tMs >= 60u);       // timestamps kept for audio sync
    }
    void coordinatesRoundTripThroughZoomAndPan() {
        std::unique_ptr<InkCanvas> c(make());
        c->setZoom(2.5); c->setPan(QPointF(40, -10));
        const QPointF p(123.4, 56.7);
        QVERIFY((c->toScreen(c->toPage(p)) - p).manhattanLength() < 1e-6);
        c->zoomAt(2.0, QPointF(200, 200));        // the page point under the cursor must stay put
        const QPointF before = QPointF(200, 200);
        QVERIFY((c->toScreen(QPointF((before.x() - 40) / 2.5, (before.y() + 10) / 2.5)) - before).manhattanLength() < 1e-6);
    }
    void strokeEraserRemovesOnRelease() {
        std::unique_ptr<InkCanvas> c(make());
        stroke(*c, {100, 100}, {300, 100});
        stroke(*c, {100, 200}, {300, 200}, 2000);
        QCOMPARE(c->strokeCount(), 2);
        c->setTool("eraser");
        stroke(*c, {200, 80}, {200, 120}, 3000);   // crosses the first line only
        QCOMPARE(c->strokeCount(), 1);
        c->undo(); QCOMPARE(c->strokeCount(), 2);
    }
    void pixelEraserSplits() {
        std::unique_ptr<InkCanvas> c(make());
        stroke(*c, {100, 100}, {300, 100});
        c->setTool("eraser");
        c->setProperty("pixelEraser", true);
        stroke(*c, {200, 80}, {200, 120}, 3000);
        QCOMPARE(c->strokeCount(), 2);             // one line became two pieces
        c->undo(); QCOMPARE(c->strokeCount(), 1);
    }
    void lassoSelectMoveDeletePaste() {
        std::unique_ptr<InkCanvas> c(make());
        stroke(*c, {100, 100}, {200, 100});
        stroke(*c, {100, 300}, {200, 300}, 2000);
        c->setTool("lasso");
        // a rectangle around the first stroke only
        c->tabletSample(sample(TabletSample::Kind::Press, {80, 80}, 3000));
        for (QPointF p : {QPointF(220, 80), QPointF(220, 120), QPointF(80, 120)}) c->tabletSample(sample(TabletSample::Kind::Move, p, 3010));
        c->tabletSample(sample(TabletSample::Kind::Release, {80, 80}, 3020, Qt::NoButton));
        QVERIFY(c->hasSelection());
        // drag it down by 50 (press inside the selection bounds)
        stroke(*c, {150, 100}, {150, 150}, 4000, 5);
        const float y = c->document()->strokes()[0].points[0].y;
        QVERIFY(std::abs(y - 150.f) < 0.5f);
        c->copySelection();
        c->paste();
        QCOMPARE(c->strokeCount(), 3);
        QVERIFY(c->hasSelection());               // the pasted strokes are selected
        c->deleteSelection();
        QCOMPARE(c->strokeCount(), 2);
        c->undo(); QCOMPARE(c->strokeCount(), 3);
        c->undo(); QCOMPARE(c->strokeCount(), 2);
        c->undo();                                // the move
        QVERIFY(std::abs(c->document()->strokes()[0].points[0].y - 100.f) < 0.5f);
    }
    void penButtonHoldErasesAndDoublePressUndoes() {
        std::unique_ptr<InkCanvas> c(make());
        stroke(*c, {100, 100}, {300, 100});
        stroke(*c, {100, 200}, {300, 200}, 2000);
        QCOMPARE(c->strokeCount(), 2);
        // hold the side button and drag across the second line: temporary eraser
        c->tabletSample(sample(TabletSample::Kind::Move, {200, 150}, 3000, Qt::MiddleButton));
        c->tabletSample(sample(TabletSample::Kind::Press, {200, 180}, 3005, Qt::LeftButton | Qt::MiddleButton));
        c->tabletSample(sample(TabletSample::Kind::Move, {200, 220}, 3010, Qt::LeftButton | Qt::MiddleButton));
        c->tabletSample(sample(TabletSample::Kind::Release, {200, 220}, 3015, Qt::MiddleButton));
        c->tabletSample(sample(TabletSample::Kind::Move, {200, 220}, 3020, Qt::NoButton)); // button released
        QCOMPARE(c->strokeCount(), 1);
        QCOMPARE(c->tool(), QStringLiteral("pen"));  // the tool itself never changed
        // double-press the button (no erase in between): undo
        QTest::qWait(400);
        c->tabletSample(sample(TabletSample::Kind::Move, {200, 150}, 4000, Qt::MiddleButton));
        c->tabletSample(sample(TabletSample::Kind::Move, {200, 150}, 4050, Qt::NoButton));
        c->tabletSample(sample(TabletSample::Kind::Move, {200, 150}, 4100, Qt::MiddleButton));
        c->tabletSample(sample(TabletSample::Kind::Move, {200, 150}, 4150, Qt::NoButton));
        QCOMPARE(c->strokeCount(), 2);
    }
    void touchIsIgnoredWhilePenNearAndPansOtherwise() {
        std::unique_ptr<InkCanvas> c(make());
        QPointingDevice dev("finger", 1, QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 10, 0);
        auto touch = [&](QEvent::Type type, QEventPoint::State st, QPointF p) {
            QEventPoint pt(1, st, p, p);
            QTouchEvent ev(type, &dev, Qt::NoModifier, {pt});
            QCoreApplication::sendEvent(c.get(), &ev);
        };
        TabletSample near; near.deviceName = "pen";
        c->tabletProximity(true, near);
        touch(QEvent::TouchBegin, QEventPoint::Pressed, {300, 300});
        touch(QEvent::TouchUpdate, QEventPoint::Updated, {340, 300});
        touch(QEvent::TouchEnd, QEventPoint::Released, {340, 300});
        QCOMPARE(c->touchIgnored(), 3);
        QCOMPARE(c->pan(), QPointF(0, 0));
        c->tabletProximity(false, near);
        c->setProperty("palmRejectMs", 0);
        QTest::qWait(5);
        touch(QEvent::TouchBegin, QEventPoint::Pressed, {300, 300});
        touch(QEvent::TouchUpdate, QEventPoint::Updated, {340, 310});
        touch(QEvent::TouchEnd, QEventPoint::Released, {340, 310});
        QCOMPARE(c->pan(), QPointF(40, 10));
    }
    void tenThousandStrokesTessellateInBudget() {
        std::unique_ptr<InkCanvas> c(make());
        QElapsedTimer t; t.start();
        c->addBenchmarkStrokes(10000);
        QCOMPARE(c->strokeCount(), 10000);
        QVERIFY2(t.elapsed() < 4000, "adding 10k strokes took too long");
    }
    void shiftRulesAStraightLineAndScratchOutRubsOut() {
        std::unique_ptr<InkCanvas> c(make());
        // Shift held: a wobbly drag lands as a clean 45° line
        auto shifted = [](TabletSample::Kind k, QPointF at, quint32 t) {
            TabletSample s = sample(k, at, t);
            s.modifiers = Qt::ShiftModifier;
            return s;
        };
        c->tabletSample(shifted(TabletSample::Kind::Press, {100, 100}, 100));
        c->tabletSample(shifted(TabletSample::Kind::Move, {140, 132}, 110));
        c->tabletSample(shifted(TabletSample::Kind::Move, {180, 168}, 120));
        c->tabletSample(shifted(TabletSample::Kind::Release, {200, 196}, 130));
        QCOMPARE(c->strokeCount(), 1);
        const Stroke &ruled = c->document()->strokes().first();
        QCOMPARE(ruled.points.size(), 2);
        const float dx = ruled.points[1].x - ruled.points[0].x, dy = ruled.points[1].y - ruled.points[0].y;
        QVERIFY2(std::abs(std::abs(dx) - std::abs(dy)) < 1.5f, "Shift should have snapped this to 45°");

        // now scribble over it: the scribble does not stay, the line goes
        for (int sweep = 0; sweep < 5; ++sweep) {
            for (int i = 0; i <= 10; ++i) {
                const float t = sweep % 2 ? 1.f - i / 10.f : i / 10.f;
                const QPointF at(100 + t * 100, 148 + (i % 2) * 3);
                const auto kind = (sweep == 0 && i == 0) ? TabletSample::Kind::Press : TabletSample::Kind::Move;
                c->tabletSample(sample(kind, at, 1000 + quint32(sweep * 100 + i * 5)));
            }
        }
        c->tabletSample(sample(TabletSample::Kind::Release, {200, 148}, 2000));
        QCOMPARE(c->strokeCount(), 0);          // the line was rubbed out and the scribble not kept
        c->undo();
        QCOMPARE(c->strokeCount(), 1);          // and it comes back
    }
    void everyPageStyleRoundTrips() {
        std::unique_ptr<InkCanvas> c(make());
        for (const QString &style : {"plain", "lined", "dotted", "grid", "cornell", "graph", "isometric", "music"}) {
            c->setPageStyle(style);
            QCOMPARE(c->pageStyle(), style);
        }
        c->setPageStyle("nonsense-style");
        QCOMPARE(c->pageStyle(), QStringLiteral("dotted"));    // anything unknown falls back, never blank
    }
};
QTEST_MAIN(TstCanvas)
#include "tst_canvas.moc"
