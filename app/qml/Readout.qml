import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

Rectangle {
    id: panel
    required property PenProbeItem probe

    SystemPalette { id: pal }
    color: pal.window

    component Pill: Rectangle {
        property string label
        property bool on: false
        property color onColor: pal.highlight
        implicitHeight: 24
        implicitWidth: pillText.implicitWidth + 18
        radius: 12
        color: on ? onColor : "transparent"
        border.color: on ? onColor : pal.mid
        border.width: 1
        Text {
            id: pillText
            anchors.centerIn: parent
            text: parent.label
            font.pixelSize: 12
            font.letterSpacing: 0.6
            color: parent.on ? pal.highlightedText : pal.windowText
        }
    }

    component Row2: RowLayout {
        property string label
        property string value
        Layout.fillWidth: true
        Text { text: parent.label; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 13; Layout.preferredWidth: 96 }
        Text {
            text: parent.value; color: pal.windowText; font.pixelSize: 13
            font.family: "monospace"; Layout.fillWidth: true; elide: Text.ElideRight
        }
    }

    component Section: Text {
        color: pal.mid
        font.pixelSize: 11
        font.letterSpacing: 1.2
        font.capitalization: Font.AllUppercase
        Layout.topMargin: 10
    }

    ColumnLayout {
        anchors { fill: parent; margins: 18 }
        spacing: 6

        Text { text: "Pen probe"; font.pixelSize: 22; font.weight: Font.DemiBold; color: pal.windowText }
        Text {
            text: probe.deviceName.length ? probe.deviceName : "no pen seen yet — bring the pen near the screen"
            color: pal.windowText; font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        Text { text: probe.capabilities; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12; font.family: "monospace"; wrapMode: Text.Wrap; Layout.fillWidth: true }

        RowLayout {
            spacing: 6
            Pill { label: "near"; on: probe.inProximity }
            Pill { label: "tip down"; on: probe.tipDown }
            Pill { label: probe.pointerType === "eraser" ? "eraser" : "pen"; on: probe.pointerType === "eraser"; onColor: "#E04E6E" }
        }

        Section { text: "pressure" }
        Rectangle {
            Layout.fillWidth: true; implicitHeight: 14; radius: 3; color: pal.alternateBase; border.color: pal.mid
            Rectangle {
                implicitWidth: Math.max(0, Math.min(1, probe.pressure)) * (parent.width - 2); implicitHeight: parent.height - 2
                x: 1; y: 1; radius: 2; color: pal.highlight
            }
        }
        Row2 { label: "value"; value: probe.pressure.toFixed(4) + "   range " + probe.minPressure.toFixed(3) + " … " + probe.maxPressure.toFixed(3) }

        Section { text: "tilt" }
        RowLayout {
            spacing: 14
            Rectangle {
                implicitWidth: 84; implicitHeight: 84; radius: 42; color: pal.alternateBase; border.color: pal.mid
                Rectangle { anchors.centerIn: parent; width: 2; height: parent.height; color: Qt.alpha(pal.mid, 0.4) }
                Rectangle { anchors.centerIn: parent; height: 2; width: parent.width; color: Qt.alpha(pal.mid, 0.4) }
                Rectangle {
                    width: 12; height: 12; radius: 6; color: pal.highlight
                    x: parent.width / 2 - 6 + (probe.xTilt / 60) * 36
                    y: parent.height / 2 - 6 + (probe.yTilt / 60) * 36
                }
            }
            ColumnLayout {
                spacing: 2
                Row2 { label: "x tilt"; value: probe.xTilt.toFixed(1) + "°   " + probe.minXTilt.toFixed(0) + " … " + probe.maxXTilt.toFixed(0) }
                Row2 { label: "y tilt"; value: probe.yTilt.toFixed(1) + "°   " + probe.minYTilt.toFixed(0) + " … " + probe.maxYTilt.toFixed(0) }
                Row2 { label: "hover z"; value: probe.hoverZ.toFixed(2) + "   " + probe.minZ.toFixed(2) + " … " + probe.maxZ.toFixed(2) }
            }
        }

        Section { text: "buttons and events" }
        Row2 { label: "buttons now"; value: probe.buttons }
        Row2 { label: "side buttons seen"; value: probe.stylusButtonsSeen.length ? probe.stylusButtonsSeen : "none yet" }
        Row2 { label: "eraser seen"; value: probe.eraserSeen ? "yes" : "no" }
        Row2 { label: "last event"; value: probe.lastEvent }
        Row2 { label: "rate"; value: probe.eventsPerSecond.toFixed(0) + " events/s" }
        Row2 { label: "ink"; value: probe.strokeCount + " strokes, " + probe.pointCount + " points" }

        Section { text: "touch (palm evidence)" }
        Row2 { label: "fingers now"; value: probe.touchPoints.toString() }
        Row2 { label: "touch while pen near"; value: probe.touchWhilePen.toString() }

        Section { text: "python worker" }
        Row2 { label: "state"; value: pingWorker.state + (pingWorker.restarts ? "  (restarts " + pingWorker.restarts + ")" : "") }
        Row2 { label: "ping"; value: pingMeter.pongs ? pingMeter.rttMs.toFixed(2) + " ms  ×" + pingMeter.pongs + "  py " + pingMeter.workerPython : (pingMeter.lastError.length ? pingMeter.lastError : "waiting") }

        Item { Layout.fillHeight: true }

        Button { text: "Clear ink"; onClicked: probe.clear() }
        Text { text: "log: " + probe.logPath; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 11; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
    }
}
