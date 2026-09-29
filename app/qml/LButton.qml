import QtQuick
import QtQuick.Controls.Basic
import Lumen

// Lumen's button (ADR mynotes-003): a capsule. highlighted marks the chosen one of a set, in the
// soft accent; primary is the one action a screen is for, in the solid accent. Presses spring.
Button {
    id: control
    property bool primary: false
    SystemPalette { id: pal }
    implicitHeight: Math.max(Ui.target - 4, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: Ui.px(16); rightPadding: Ui.px(16)
    font.weight: highlighted || primary ? Font.DemiBold : Font.Medium
    palette.brightText: primary ? Ui.onAccent(pal.highlight) : pal.highlight
    palette.buttonText: primary ? Ui.onAccent(pal.highlight) : pal.text
    palette.windowText: pal.text
    background: Rectangle {
        radius: height / 2
        color: control.primary ? (control.down ? Qt.darker(pal.highlight, 1.12) : pal.highlight)
             : control.highlighted ? Ui.accentSoft
             : control.flat ? (control.down ? Qt.alpha(pal.text, Ui.pressAlpha) : control.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent")
             : (control.down ? Qt.alpha(pal.text, 0.14) : control.hovered ? Qt.alpha(pal.text, 0.09) : Qt.alpha(pal.text, 0.06))
        Behavior on color { ColorAnimation { duration: Ui.quick } }
    }
    scale: down && !Ui.reduceMotion ? 0.96 : 1
    Behavior on scale { enabled: !Ui.instant; SpringAnimation { spring: 3; damping: 0.4; epsilon: 0.005 } }
}
