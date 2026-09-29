import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import Lumen

// Lumen's switch (ADR mynotes-003): the accent track when on, a white knob that springs across.
Switch {
    id: control
    SystemPalette { id: pal }
    indicator: Rectangle {
        implicitWidth: Ui.px(52); implicitHeight: Ui.px(32)
        x: control.text.length ? control.leftPadding : (control.width - width) / 2
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: control.checked ? pal.highlight : Qt.alpha(pal.text, 0.18)
        Behavior on color { ColorAnimation { duration: Ui.quick } }
        Rectangle {
            id: knob
            width: parent.height - 6; height: width; radius: width / 2; y: 3
            x: control.checked ? parent.width - width - 3 : 3
            color: "#FFFFFF"
            Behavior on x { enabled: !Ui.instant; SpringAnimation { spring: 3; damping: 0.45; epsilon: 0.25 } }
            RectangularShadow { anchors.fill: parent; z: -1; radius: knob.radius; offset.y: 1; blur: 5; color: Qt.alpha("#000000", 0.25) }
        }
    }
}
