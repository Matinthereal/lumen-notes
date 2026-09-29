import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import Lumen

// Lumen's slider (ADR mynotes-003): a thin rounded track filled in the accent, a white knob.
Slider {
    id: control
    SystemPalette { id: pal }
    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: Ui.px(200); implicitHeight: 4
        width: control.availableWidth; height: implicitHeight; radius: 2
        color: Qt.alpha(pal.text, 0.14)
        Rectangle { width: control.visualPosition * parent.width; height: parent.height; radius: 2; color: pal.highlight }
    }
    handle: Rectangle {
        id: knob
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        implicitWidth: Ui.px(24); implicitHeight: Ui.px(24); radius: width / 2
        color: "#FFFFFF"; border.width: 1; border.color: Ui.hair
        scale: control.pressed && !Ui.reduceMotion ? 1.12 : 1
        Behavior on scale { enabled: !Ui.instant; SpringAnimation { spring: 3; damping: 0.4; epsilon: 0.005 } }
        RectangularShadow { anchors.fill: parent; z: -1; radius: knob.radius; offset.y: 2; blur: 8; color: Qt.alpha("#000000", 0.22) }
    }
}
