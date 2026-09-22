import QtQuick
import QtQuick.Controls.Basic
import Lumen

// The one bubble a press-and-hold shows (HoldTips in C++ finds the control and its text). Same
// ToolTip as hover uses, so finger, pen and mouse all see the same thing. It sits above the
// control where it can, because the finger holding it covers the control and what is below it.
ToolTip {
    id: tip
    objectName: "holdTip"
    property rect anchorRect: Qt.rect(0, 0, 0, 0)
    readonly property real roomW: parent ? parent.width : 0
    readonly property real roomH: parent ? parent.height : 0
    // Above if it fits; otherwise beside a control on the window's edge (the rail), or below.
    readonly property bool above: anchorRect.y - implicitHeight - 10 >= 8
    readonly property bool right: anchorRect.x < 80 && anchorRect.x + anchorRect.width + 10 + width <= roomW - 8
    readonly property bool left: anchorRect.x + anchorRect.width > roomW - 80 && anchorRect.x - 10 - width >= 8
    x: above ? Math.max(8, Math.min(roomW - width - 8, anchorRect.x + anchorRect.width / 2 - width / 2))
             : right ? anchorRect.x + anchorRect.width + 10
             : left ? anchorRect.x - width - 10
             : Math.max(8, Math.min(roomW - width - 8, anchorRect.x + anchorRect.width / 2 - width / 2))
    y: above ? anchorRect.y - implicitHeight - 10
             : (right || left) ? Math.max(8, Math.min(roomH - implicitHeight - 8, anchorRect.y + anchorRect.height / 2 - implicitHeight / 2))
             : anchorRect.y + anchorRect.height + 10
    width: Math.min(implicitWidth, 420)
    delay: 0
    timeout: -1
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Timer { id: linger; interval: 1500; onTriggered: tip.close() }
    Connections {
        target: holdTips
        function onShown(text, rect) { linger.stop(); tip.text = text; tip.anchorRect = rect; tip.open() }
        function onReleased() { linger.restart() }
    }
}
