#include <QPointingDevice>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QStyleHints>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QtTest>
#include "ui/holdtips.h"

// Press-and-hold tooltips: a held finger or pen shows the control's tooltip and does not fire the
// control; a tap still fires it; a control with its own hold keeps it. Real window, real delivery.
static const char *kScene = R"(
import QtQuick
import QtQuick.Controls.Basic
Item {
    id: root
    width: 400; height: 300
    property int taps: 0
    property int clicks: 0
    property int holds: 0
    property int noTipTaps: 0
    Rectangle {
        objectName: "plain"; x: 10; y: 150; width: 60; height: 60
        ToolTip.text: "Plain tip"
        ToolTip.visible: plainHover.hovered
        ToolTip.delay: 200
        HoverHandler { id: plainHover }
        Rectangle { objectName: "plainChild"; anchors.centerIn: parent; width: 20; height: 20 }
        TapHandler { onTapped: root.taps++ }
    }
    Button { objectName: "button"; x: 100; y: 10; width: 80; height: 60; text: "B"; ToolTip.text: "Button tip"; onClicked: root.clicks++ }
    Rectangle {
        objectName: "held"; x: 200; y: 10; width: 60; height: 60
        property bool holdAction: true
        ToolTip.text: "Hold to do the thing"
        Timer { id: t; interval: 400; onTriggered: root.holds++ }
        TapHandler { onPressedChanged: pressed ? t.restart() : t.stop() }
    }
    Rectangle { objectName: "noTip"; x: 10; y: 230; width: 60; height: 60; TapHandler { onTapped: root.noTipTaps++ } }
    property int areaClicks: 0
    property int rowTaps: 0
    property int innerTaps: 0
    MouseArea { objectName: "area"; x: 100; y: 150; width: 80; height: 60; ToolTip.text: "Area tip"; onClicked: root.areaClicks++ }
    Rectangle {
        objectName: "row"; x: 200; y: 150; width: 190; height: 60
        TapHandler { onTapped: root.rowTaps++ }
        Rectangle { objectName: "inner"; x: 120; y: 10; width: 40; height: 40; ToolTip.text: "Inner tip"
            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: root.innerTaps++ } }
    }
    Rectangle { objectName: "cover"; x: 300; y: 10; width: 60; height: 60; ToolTip.text: "Under"
        Rectangle { objectName: "coverTop"; anchors.fill: parent; z: 2; ToolTip.text: "On top"; TapHandler {} } }
}
)";

class TstHoldTips : public QObject {
    Q_OBJECT
    QQmlEngine engine;
    std::unique_ptr<QQuickWindow> window;
    QQuickItem *root = nullptr;
    HoldTips tips;
    QPointingDevice *touch = QTest::createTouchDevice();
    QPointingDevice stylus{QStringLiteral("test stylus"), 7001, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                           QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 1};

    QQuickItem *item(const char *name) { return root->findChild<QQuickItem *>(QLatin1String(name)); }
    QPoint centreOf(const char *name) { QQuickItem *i = item(name); return i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).toPoint(); }
    void finger(const char *name, int ms, int drift = 0)
    {
        const QPoint p = centreOf(name);
        QTest::touchEvent(window.get(), touch).press(1, p, window.get());
        if (drift) { QTest::qWait(30); QTest::touchEvent(window.get(), touch).move(1, p + QPoint(drift, 0), window.get()); }
        QTest::qWait(ms);
        QTest::touchEvent(window.get(), touch).release(1, p + QPoint(drift, 0), window.get());
        QTest::qWait(30);
    }
    void pen(QEvent::Type type, QPointF p, Qt::MouseButtons buttons)
    {
        QTabletEvent ev(type, &stylus, p, window->mapToGlobal(p.toPoint()), buttons ? 0.5 : 0.0, 0, 0, 0, 0, 0,
                        Qt::NoModifier, Qt::LeftButton, buttons);
        static QElapsedTimer clock; if (!clock.isValid()) clock.start();
        ev.setTimestamp(clock.elapsed() + 100000);          // real delivery stamps events; a 0 confuses tap timing
        QCoreApplication::sendEvent(window.get(), &ev);
    }
    void penHold(const char *name, int ms)
    {
        const QPointF p = centreOf(name);
        QTest::qWait(QGuiApplication::styleHints()->mouseDoubleClickInterval() + 50);   // two quick taps are a double tap
        pen(QEvent::TabletPress, p, Qt::LeftButton);
        QTest::qWait(ms);
        pen(QEvent::TabletRelease, p, Qt::NoButton);
        QTest::qWait(30);
    }

private slots:
    void initTestCase()
    {
        QQmlComponent c(&engine);
        c.setData(kScene, QUrl());
        root = qobject_cast<QQuickItem *>(c.create());
        QVERIFY2(root, qPrintable(c.errorString()));
        window = std::make_unique<QQuickWindow>();
        window->resize(400, 300);
        root->setParentItem(window->contentItem());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
        tips.setHoldMs(300);
        tips.attachWindow(window.get());
    }
    void findsTheOwner()
    {
        QString text;
        QCOMPARE(HoldTips::tipOwnerAt(window->contentItem(), centreOf("plainChild"), &text), item("plain"));
        QCOMPARE(text, QStringLiteral("Plain tip"));
        QCOMPARE(HoldTips::tipOwnerAt(window->contentItem(), centreOf("noTip")), nullptr);
        QCOMPARE(HoldTips::tipOwnerAt(window->contentItem(), centreOf("held")), nullptr);
        QCOMPARE(HoldTips::tipOwnerAt(window->contentItem(), centreOf("cover"), &text), item("coverTop"));
        QCOMPARE(text, QStringLiteral("On top"));
    }
    void fingerTapStillTaps()
    {
        QSignalSpy shown(&tips, &HoldTips::shown);
        const int before = root->property("taps").toInt();
        finger("plain", 60);
        QCOMPARE(root->property("taps").toInt(), before + 1);
        QCOMPARE(shown.count(), 0);
    }
    void fingerHoldShowsTipAndDoesNotTap()
    {
        QSignalSpy shown(&tips, &HoldTips::shown);
        QSignalSpy released(&tips, &HoldTips::released);
        const int before = root->property("taps").toInt();
        finger("plain", 500);
        QCOMPARE(shown.count(), 1);
        QCOMPARE(shown.first().at(0).toString(), QStringLiteral("Plain tip"));
        QCOMPARE(released.count(), 1);
        QCOMPARE(root->property("taps").toInt(), before);
        QObject *handler = nullptr;
        for (QObject *o : item("plain")->children()) if (o->inherits("QQuickTapHandler")) handler = o;
        QVERIFY(handler && !handler->property("pressed").toBool());   // not left looking pressed
        finger("plain", 60);
        QCOMPARE(root->property("taps").toInt(), before + 1);
    }
    void theHoverTipDoesNotStackOnTop()
    {
        QSignalSpy shown(&tips, &HoldTips::shown);
        const QPoint p = centreOf("plain");
        QTest::touchEvent(window.get(), touch).press(1, p, window.get());
        QTest::qWait(700);                                     // past both the hold and the hover delay
        QObject *attached = nullptr;
        for (QObject *o : item("plain")->children()) if (o->inherits("QQuickToolTipAttached")) attached = o;
        QVERIFY(attached);
        QObject *shared = attached->property("toolTip").value<QObject *>();
        const bool hoverTipUp = shared && shared->property("visible").toBool() && shared->property("parent").value<QQuickItem *>() == item("plain");
        QTest::touchEvent(window.get(), touch).release(1, p, window.get());
        QTest::qWait(30);
        QCOMPARE(shown.count(), 1);
        QVERIFY(!hoverTipUp);
    }
    void fingerHoldOnAButtonDoesNotClick()
    {
        QSignalSpy shown(&tips, &HoldTips::shown);
        const int before = root->property("clicks").toInt();
        finger("button", 500);
        QCOMPARE(shown.count(), 1);
        QCOMPARE(root->property("clicks").toInt(), before);
        QVERIFY(!item("button")->property("pressed").toBool());
        finger("button", 60);
        QCOMPARE(root->property("clicks").toInt(), before + 1);
    }
    void aScrollIsNotAHold()
    {
        QSignalSpy shown(&tips, &HoldTips::shown);
        finger("plain", 500, 40);
        QCOMPARE(shown.count(), 0);
    }
    void aControlsOwnHoldIsLeftAlone()
    {
        QSignalSpy shown(&tips, &HoldTips::shown);
        const int before = root->property("holds").toInt();
        finger("held", 600);
        QCOMPARE(shown.count(), 0);
        QCOMPARE(root->property("holds").toInt(), before + 1);
    }
    void penHoldShowsTipAndDoesNotTap()
    {
        QSignalSpy shown(&tips, &HoldTips::shown);
        const int before = root->property("taps").toInt();
        penHold("plain", 500);
        QCOMPARE(shown.count(), 1);
        QCOMPARE(root->property("taps").toInt(), before);
        penHold("plain", 60);
        QCOMPARE(root->property("taps").toInt(), before + 1);
        QCOMPARE(shown.count(), 1);
    }
    void aMouseAreaIsCancelledToo()
    {
        const int before = root->property("areaClicks").toInt();
        finger("area", 500);
        QCOMPARE(root->property("areaClicks").toInt(), before);
        QVERIFY(!item("area")->property("pressed").toBool());
        finger("area", 60);
        QCOMPARE(root->property("areaClicks").toInt(), before + 1);
    }
    void neitherTheControlNorTheRowUnderItFires()
    {
        QSignalSpy shown(&tips, &HoldTips::shown);
        const int rows = root->property("rowTaps").toInt(), inner = root->property("innerTaps").toInt();
        finger("inner", 500);
        QCOMPARE(shown.count(), 1);
        QCOMPARE(root->property("innerTaps").toInt(), inner);
        QCOMPARE(root->property("rowTaps").toInt(), rows);
        finger("inner", 60);
        QCOMPARE(root->property("innerTaps").toInt(), inner + 1);
    }
    void penTapOnly()
    {
        const int before = root->property("taps").toInt();
        penHold("plain", 60);
        QCOMPARE(root->property("taps").toInt(), before + 1);
    }
    void noTipNoInterference()
    {
        QSignalSpy shown(&tips, &HoldTips::shown);
        const int before = root->property("noTipTaps").toInt();
        finger("noTip", 500);
        QCOMPARE(shown.count(), 0);
        QCOMPARE(root->property("noTipTaps").toInt(), before + 1);   // a slow tap on a plain control still taps
    }
};

QTEST_MAIN(TstHoldTips)
#include "tst_holdtips.moc"
