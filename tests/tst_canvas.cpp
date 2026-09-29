#include <QPointingDevice>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTouchEvent>
#include <QtTest>
#include <QWindow>
#include "canvas/inkcanvas.h"
#include "input/tableteventfilter.h"

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
    // The page draws its own tip for the pen and a finger needs no pointer, so only those hide the
    // cursor; a mouse or touchpad must always have one. Through a real window, whose cursor is the
    // one a person sees.
    void cursorHidesOnlyForPenAndTouch() {
        QQuickWindow win;
        win.resize(800, 600);
        InkCanvas *c = make();
        c->setParentItem(win.contentItem());
        win.show();
        QVERIFY(QTest::qWaitForWindowExposed(&win));
        const QPoint mid(400, 300);
        QCOMPARE(win.cursor().shape(), Qt::ArrowCursor);          // nothing used yet: visible
        QTest::mouseMove(&win, mid);
        QCOMPARE(win.cursor().shape(), Qt::ArrowCursor);
        QPointingDevice *finger = QTest::createTouchDevice();
        QTest::touchEvent(&win, finger).press(0, mid);
        QTest::touchEvent(&win, finger).release(0, mid);
        QCOMPARE(win.cursor().shape(), Qt::BlankCursor);
        QTest::mouseMove(&win, mid + QPoint(12, 4));
        QCOMPARE(win.cursor().shape(), Qt::ArrowCursor);
        c->tabletSample(sample(TabletSample::Kind::Move, mid, 5000, Qt::NoButton));   // the pen hovering over the page
        QCOMPARE(win.cursor().shape(), Qt::BlankCursor);
        stroke(*c, {300, 300}, {420, 330}, 6000);
        QCOMPARE(win.cursor().shape(), Qt::BlankCursor);
        c->tabletProximity(false, sample(TabletSample::Kind::Move, mid, 6200, Qt::NoButton));
        QTest::mouseMove(&win, mid + QPoint(-30, 10));             // back on the touchpad
        QCOMPARE(win.cursor().shape(), Qt::ArrowCursor);
        c->setTool("shape");                                         // the shape layer draws no tip:
        c->tabletSample(sample(TabletSample::Kind::Move, mid, 7000, Qt::NoButton));
        QCOMPARE(win.cursor().shape(), Qt::ArrowCursor);            // the pen keeps the pointer
    }
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
    void pencilIsAPropertyOfTheStroke() {
        // A pencil stroke stays pencil through undo and redo; choosing ink again changes only what
        // comes next, never what is already on the page.
        std::unique_ptr<InkCanvas> c(make());
        c->setBrush(QStringLiteral("pencil"));
        stroke(*c, {100, 100}, {300, 120});
        c->setBrush(QStringLiteral("ink"));
        stroke(*c, {100, 200}, {300, 220}, 2000);
        QCOMPARE(c->strokeCount(), 2);
        c->undo(); c->undo(); c->redo(); c->redo();
        const auto &st = c->document()->strokes();
        QCOMPARE(int(st[0].brush), 1);
        QCOMPARE(int(st[1].brush), 0);
        c->setTool(QStringLiteral("highlighter")); c->setBrush(QStringLiteral("pencil"));
        stroke(*c, {100, 300}, {300, 320}, 3000);
        QCOMPARE(int(c->document()->strokes().last().brush), 0);   // a highlighter is never a pencil
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
    // Qt's iOS plugin sends no proximity events at all (and Android need not), so the pen is taken
    // to have gone a moment after its tip lifts; otherwise one stroke made every later finger a palm.
    void fingersPanAgainAfterAPenThatNeverSaysItLeft() {
        std::unique_ptr<InkCanvas> c(make());
        QPointingDevice dev("finger", 2, QInputDevice::DeviceType::TouchScreen, QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position, 10, 0);
        auto touch = [&](QEvent::Type type, QEventPoint::State st, QPointF p) {
            QEventPoint pt(1, st, p, p);
            QTouchEvent ev(type, &dev, Qt::NoModifier, {pt});
            QCoreApplication::sendEvent(c.get(), &ev);
        };
        c->setProperty("palmRejectMs", 0);
        stroke(*c, {100, 100}, {300, 120});
        QVERIFY(c->penNear());
        touch(QEvent::TouchBegin, QEventPoint::Pressed, {300, 300});     // the palm resting as it lifts
        touch(QEvent::TouchEnd, QEventPoint::Released, {300, 300});
        QCOMPARE(c->pan(), QPointF(0, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!c->penNear(), 1000);
        touch(QEvent::TouchBegin, QEventPoint::Pressed, {300, 300});
        touch(QEvent::TouchUpdate, QEventPoint::Updated, {340, 310});
        touch(QEvent::TouchEnd, QEventPoint::Released, {340, 310});
        QCOMPARE(c->pan(), QPointF(40, 10));
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
    void penStrokeStaysWithTheCanvasItLandedOn() {
        // Split view: two canvases side by side, one pen. A stroke that starts on the right and
        // wanders over the divider must stay on the right; the next one on the left goes left.
        std::unique_ptr<InkCanvas> left(make()), right(make());
        right->setX(800);
        TabletEventFilter filter;
        filter.setSink(left.get());
        filter.addSink(right.get());
        QWindow window;
        filter.attachWindow(&window);
        QPointingDevice pen(QStringLiteral("test pen"), 77, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1);
        quint64 t = 1000;
        const auto send = [&](QEvent::Type type, QPointF at) {
            const Qt::MouseButtons held = type == QEvent::TabletRelease ? Qt::NoButton : Qt::MouseButtons(Qt::LeftButton);
            QTabletEvent ev(type, &pen, at, at, type == QEvent::TabletRelease ? 0.0 : 0.5, 0, 0, 0, 0, 0, Qt::NoModifier, Qt::LeftButton, held);
            ev.setTimestamp(t += 8);
            QCoreApplication::sendEvent(&window, &ev);
        };
        const auto drag = [&](QPointF from, QPointF to) {
            send(QEvent::TabletPress, from);
            for (int i = 1; i <= 12; ++i) send(QEvent::TabletMove, from + (to - from) * i / 12.0);
            send(QEvent::TabletRelease, to);
        };
        drag({1000, 300}, {700, 320});
        QCOMPARE(right->strokeCount(), 1);
        QCOMPARE(left->strokeCount(), 0);
        drag({200, 300}, {400, 300});
        QCOMPARE(left->strokeCount(), 1);
        QCOMPARE(right->strokeCount(), 1);
        right->setVisible(false);                   // split closed: the right canvas declines
        drag({1000, 300}, {1100, 300});
        QCOMPARE(right->strokeCount(), 1);
    }
};
QTEST_MAIN(TstCanvas)
#include "tst_canvas.moc"
