#include <QPointingDevice>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTouchEvent>
#include <QtTest>
#include <QTemporaryDir>
#include <memory>
#include <QWindow>
#include "canvas/inkcanvas.h"
#include "media/picturepixels.h"
#include "input/stylustilt.h"
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
    // Android reports the pen's lean as a tilt and an orientation, which it makes from the
    // digitiser's x and y tilt (AOSP TouchInputMapper). Whatever x/y lean a desktop tablet reports,
    // the same pen on Android must reach the pencil as the same xTilt/yTilt.
    void androidTiltReadsAsOnTheDesktop() {
        const auto android = [](double xDeg, double yDeg) {
            const double x = qDegreesToRadians(xDeg), y = qDegreesToRadians(yDeg);
            return std::pair{float(std::acos(std::cos(x) * std::cos(y))), float(std::atan2(-std::sin(x), std::sin(y)))};
        };
        for (int x = -60; x <= 60; x += 5)
            for (int y = -60; y <= 60; y += 5) {
                const auto [tilt, orientation] = android(x, y);
                const QPointF back = stylustilt::fromAndroid(tilt, orientation);
                QVERIFY2(std::abs(back.x() - x) < 0.01 && std::abs(back.y() - y) < 0.01,
                         qPrintable(QStringLiteral("(%1, %2) came back as (%3, %4)").arg(x).arg(y).arg(back.x()).arg(back.y())));
            }
        // The same in words: which way the tip points, and so which way the top leans.
        const auto lean = [](double tiltDeg, double orientationDeg) {
            return stylustilt::fromAndroid(float(qDegreesToRadians(tiltDeg)), float(qDegreesToRadians(orientationDeg)));
        };
        const auto near = [](QPointF a, QPointF b) { return std::abs(a.x() - b.x()) < 0.01 && std::abs(a.y() - b.y()) < 0.01; };
        QVERIFY(near(lean(0, 70), {0, 0}));            // upright: the orientation means nothing
        QVERIFY(near(lean(45, -90), {45, 0}));         // tip points left, top leans right
        QVERIFY(near(lean(45, 90), {-45, 0}));         // tip points right, top leans left
        QVERIFY(near(lean(45, 0), {0, 45}));           // tip points up, top leans towards the bottom
        QVERIFY(near(lean(45, 180), {0, -45}));        // tip points down, top leans away
        // A pen laid flat along a diagonal is the extreme: still numbers, still within a quarter turn.
        const QPointF flat = lean(90, -45);
        QVERIFY(std::isfinite(flat.x()) && std::isfinite(flat.y()) && std::abs(flat.x()) < 90.001 && std::abs(flat.y()) < 90.001);
        // The pencil shades past 38° off upright (hypot of the two): a writing grip stays ink and a
        // leant-over pen shades, whichever way it leans.
        for (int o = -180; o < 180; o += 15) {
            QVERIFY(std::hypot(lean(30, o).x(), lean(30, o).y()) < 38);
            QVERIFY(std::hypot(lean(55, o).x(), lean(55, o).y()) > 38);
        }
    }
    // The Java side records each stylus event's tilt as it comes in; the tablet filter looks it up
    // by the timestamp Qt gives the same event, a few milliseconds later on another thread.
    void androidTiltIsFoundByEventTime() {
        stylustilt::Recent recent;
        QPointF got;
        QVERIFY(!recent.find(1000, &got));
        recent.record(1000, 0, 0);
        recent.record(1008, float(qDegreesToRadians(45.0)), float(qDegreesToRadians(-90.0)));
        QVERIFY(recent.find(1008, &got));
        QVERIFY(std::abs(got.x() - 45) < 0.01 && std::abs(got.y()) < 0.01);
        QVERIFY(recent.find(1000, &got));
        QCOMPARE(got, QPointF(0, 0));
        QVERIFY(!recent.find(1004, &got));                          // no sample then: Qt's zeros stand
        recent.record(1008, float(qDegreesToRadians(30.0)), 0);      // same millisecond: the newer one
        QVERIFY(recent.find(1008, &got));
        QVERIFY(std::abs(got.y() - 30) < 0.01);
        for (quint64 t = 2000; t < 2400; t += 4) recent.record(t, 0, 0);    // 100 samples, more than it keeps
        QVERIFY(!recent.find(1000, &got));                          // long gone: it keeps only the last few
        QVERIFY(recent.find(2396, &got));
    }
    // ---- pictures: what they are drawn from (picturepixels.h). The complaint was "low quality,
    // pixelated": every picture was drawn from one texture of at most 2560 px, shrunk by the
    // graphics card between two mipmap levels, however far in or out the page was zoomed.

    // Shrunk, a picture gets a texture with one texel for each screen pixel, on whole pixels.
    void aShrunkPictureIsCutToTheScreensOwnPixels() {
        const QRect view(0, 0, 1600, 900);
        const picturepixels::Sharp s = picturepixels::plan(QSize(1920, 1080), QRectF(300.4, 120.6, 555.8, 312.6), view);
        QVERIFY(s.valid());
        QCOMPARE(s.device, QRectF(300, 121, 556, 312));
        QCOMPARE(s.pixels, QSize(556, 312));
        QCOMPARE(s.source, QRectF(0, 0, 1920, 1080));
        QVERIFY(s.whole);
        // Half off the left of the screen: only what is in view is cut, from the matching pixels.
        const picturepixels::Sharp part = picturepixels::plan(QSize(1920, 1080), QRectF(-278, 100, 556, 312), view);
        QCOMPARE(part.device, QRectF(0, 100, 278, 312));
        QCOMPARE(part.source, QRectF(960, 0, 960, 1080));
        QVERIFY(!part.whole);
        QVERIFY(!picturepixels::plan(QSize(1920, 1080), QRectF(2000, 100, 556, 312), view).valid());   // out of view
    }
    // Enlarged past what the 2560 px texture holds, the part in view comes from the file's own pixels.
    void anEnlargedPhotoIsCutFromItsOwnPixels() {
        const QRect view(0, 0, 1600, 900);
        const picturepixels::Sharp s = picturepixels::plan(QSize(4000, 3000), QRectF(-2000, -1500, 8000, 6000), view);
        QVERIFY(s.valid());
        QCOMPARE(s.source, QRectF(1000, 750, 800, 450));
        QCOMPARE(s.pixels, QSize(800, 450));
        QCOMPARE(s.device, QRectF(0, 0, 1600, 900));
        // A picture the base texture holds every pixel of has nothing sharper to offer.
        QVERIFY(!picturepixels::plan(QSize(1920, 1080), QRectF(-2000, -1500, 8000, 4500), view).valid());
    }
    // One-pixel detail survives the shrink: a 2 px stripe pattern at half size is 1 px stripes, not grey.
    void shrinkingKeepsFineDetail() {
        QImage stripes(2000, 40, QImage::Format_RGB32);
        for (int y = 0; y < stripes.height(); ++y)
            for (int x = 0; x < stripes.width(); ++x) stripes.setPixel(x, y, (x / 2) % 2 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
        const picturepixels::Sharp s = picturepixels::plan(stripes.size(), QRectF(10.3, 5, 1000, 20), QRect(0, 0, 1600, 900));
        const QImage cut = picturepixels::render(stripes, s);
        QCOMPARE(cut.size(), QSize(1000, 20));
        for (int x = 0; x + 1 < cut.width(); ++x)
            QVERIFY2(std::abs(qGray(cut.pixel(x, 10)) - qGray(cut.pixel(x + 1, 10))) > 250, qPrintable(QString::number(x)));
    }
    // The part of a picture that is in view is cut from exactly the pixels a cut of the whole
    // picture would use there, so nothing shifts when the page is panned.
    void aCutOfThePartInViewMatchesTheWholeCut() {
        QImage noise(1920, 1080, QImage::Format_RGB32);
        quint32 seed = 7;
        for (int y = 0; y < noise.height(); ++y)
            for (int x = 0; x < noise.width(); ++x) { seed = seed * 1664525u + 1013904223u; const int v = seed >> 24; noise.setPixel(x, y, qRgb(v, v, v)); }
        const QRectF device(-33.7, -18.4, 1667.4, 937.9);
        const picturepixels::Sharp whole = picturepixels::plan(noise.size(), device, QRect(-100, -100, 2000, 1200));
        const picturepixels::Sharp part = picturepixels::plan(noise.size(), device, QRect(0, 0, 1600, 900));
        QVERIFY(whole.whole && !part.whole);
        const QImage a = picturepixels::render(noise, whole), b = picturepixels::render(noise, part);
        QCOMPARE(b.size(), QSize(1600, 900));
        const QPoint shift = part.device.topLeft().toPoint() - whole.device.topLeft().toPoint();
        int worst = 0;
        for (int y = 0; y < b.height(); y += 7)
            for (int x = 0; x < b.width(); x += 5) worst = std::max(worst, std::abs(qGray(a.pixel(x + shift.x(), y + shift.y())) - qGray(b.pixel(x, y))));
        QVERIFY2(worst <= 1, qPrintable(QString::number(worst)));
    }
    // The canvas asks for that texture once the view has stopped moving, and again for every zoom.
    void theCanvasCutsAPictureForTheViewItSettlesOn() {
        QTemporaryDir dir;
        const QString photo = dir.filePath("photo.png");
        QImage big(4000, 3000, QImage::Format_RGB32);
        big.fill(qRgb(40, 90, 160));
        QVERIFY(big.save(photo, "PNG", 100));
        std::unique_ptr<InkCanvas> c(make());
        c->setZoom(0.5); c->setPan(QPointF(10.2, 20.7));
        c->setImages({QVariantMap{{"id", 7}, {"path", photo}, {"x", 100.0}, {"y", 50.0}, {"w", 400.0}, {"h", 300.0}}});
        QTRY_COMPARE_WITH_TIMEOUT(c->pictureTexture(7).value("sharpWidth").toInt(), 200, 8000);
        QVariantMap t = c->pictureTexture(7);
        QCOMPARE(t.value("sourceWidth").toInt(), 4000);
        QCOMPARE(t.value("sharpHeight").toInt(), 150);
        // 400 page units at half zoom is 200 screen pixels, and it sits on whole ones.
        const QRectF onScreen(c->toScreen(t.value("sharpRect").toRectF().topLeft()), t.value("sharpRect").toRectF().size() * c->zoom());
        QVERIFY(qAbs(onScreen.x() - qRound(onScreen.x())) < 1e-6 && qAbs(onScreen.y() - qRound(onScreen.y())) < 1e-6);
        QCOMPARE(onScreen.size(), QSizeF(200, 150));

        // Panned, the cut stays up (it is still the right size) and settles onto whole pixels again.
        c->setPan(QPointF(33.6, 47.1));
        QCOMPARE(c->pictureTexture(7).value("sharpWidth").toInt(), 200);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(c->toScreen(c->pictureTexture(7).value("sharpRect").toRectF().topLeft()).x() - 84.0) < 1e-6, 4000);
        QCOMPARE(c->pictureTexture(7).value("sharpWidth").toInt(), 200);

        // Zoomed right in, the old cut goes at once (stretched, it would look worse than no cut)…
        c->setZoom(16); c->setPan(QPointF(-3000, -2000));
        QCOMPARE(c->pictureTexture(7).value("sharpWidth").toInt(), 0);
        // …and the new one holds more of the photo per page unit than the 2560 px texture can.
        QTRY_VERIFY_WITH_TIMEOUT(c->pictureTexture(7).value("sharpWidth").toInt() > 0, 8000);
        t = c->pictureTexture(7);
        QVERIFY2(t.value("sharpWidth").toDouble() / t.value("sharpRect").toRectF().width() > 2560.0 / 400.0 * 1.5,
                 qPrintable(QString::number(t.value("sharpWidth").toDouble() / t.value("sharpRect").toRectF().width())));
        QVERIFY(t.value("sharpWidth").toInt() <= 800);          // never more than the view needs

        // Moving the picture moves the cut with it rather than leaving it behind.
        c->setImages({QVariantMap{{"id", 7}, {"path", photo}, {"x", 180.0}, {"y", 50.0}, {"w", 400.0}, {"h", 300.0}}});
        QCOMPARE(c->pictureTexture(7).value("sharpWidth").toInt(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(c->pictureTexture(7).value("sharpWidth").toInt() > 0, 8000);
    }
};
QTEST_MAIN(TstCanvas)
#include "tst_canvas.moc"
