import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Shapes
import Lumen

// Something slow is happening because you asked for it: a ring that fills as the worker reports
// progress (or turns while it cannot say), what it is doing, and a way to stop it. Sits where the
// action was started and never blocks anything around it.
Rectangle {
    id: chip
    objectName: "progressChip"
    property string text: ""
    property real progress: -1              // 0..1, or below 0 when the worker cannot say how far along
    property bool cancellable: true
    property string cancelTip: "Stop"
    signal cancelRequested()

    SystemPalette { id: pal }
    implicitWidth: row.implicitWidth + row.anchors.leftMargin + row.anchors.rightMargin
    implicitHeight: Ui.target
    radius: height / 2
    color: Qt.alpha(pal.window, 0.96)
    border.color: Qt.alpha(pal.text, 0.18)
    border.width: 1
    Accessible.role: Accessible.ProgressBar
    Accessible.name: text

    RowLayout {
        id: row
        anchors { fill: parent; leftMargin: 12; rightMargin: chip.cancellable ? 2 : 14 }
        spacing: 8
        Item {
            implicitWidth: Ui.icon; implicitHeight: Ui.icon
            Shape {
                id: ring
                anchors.fill: parent
                readonly property real sweep: chip.progress >= 0 ? Math.max(8, 360 * chip.progress) : 100
                ShapePath {
                    strokeColor: Qt.alpha(pal.text, 0.16); strokeWidth: 2.5; fillColor: "transparent"
                    PathAngleArc { centerX: ring.width / 2; centerY: ring.height / 2; radiusX: ring.width / 2 - 2; radiusY: radiusX; startAngle: 0; sweepAngle: 360 }
                }
                ShapePath {
                    strokeColor: pal.highlight; strokeWidth: 2.5; fillColor: "transparent"; capStyle: ShapePath.RoundCap
                    PathAngleArc { centerX: ring.width / 2; centerY: ring.height / 2; radiusX: ring.width / 2 - 2; radiusY: radiusX; startAngle: -90; sweepAngle: ring.sweep }
                }
                RotationAnimator on rotation {
                    running: chip.visible && chip.progress < 0
                    from: 0; to: 360; duration: 900; loops: Animation.Infinite
                }
            }
        }
        Text {
            text: chip.progress >= 0 ? chip.text + "  " + Math.round(chip.progress * 100) + "%" : chip.text
            color: pal.text; font.pixelSize: Ui.small + 1
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.maximumWidth: 360
        }
        Rectangle {
            objectName: "progressCancel"
            visible: chip.cancellable
            implicitWidth: Ui.target - 4; implicitHeight: Ui.target - 4; radius: height / 2
            color: stopTap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : (stopHover.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent")
            Accessible.role: Accessible.Button
            Accessible.name: chip.cancelTip
            Icon { anchors.centerIn: parent; name: "window-close"; implicitWidth: Ui.icon - 4; implicitHeight: Ui.icon - 4 }
            HoverHandler { id: stopHover }
            TapHandler { id: stopTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: chip.cancelRequested() }
            ToolTip.visible: stopHover.hovered; ToolTip.delay: 600; ToolTip.text: chip.cancelTip
        }
    }
}
