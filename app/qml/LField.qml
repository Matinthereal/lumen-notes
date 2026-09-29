import QtQuick
import QtQuick.Controls.Basic
import Lumen

// Lumen's text field (ADR mynotes-003): rounded, on the raised paper, an accent ring while typing.
// Its resting border keeps WCAG's 3:1 for a control's edge.
TextField {
    id: control
    SystemPalette { id: pal }
    implicitHeight: Math.max(Ui.target - 4, contentHeight + topPadding + bottomPadding)
    leftPadding: Ui.px(14); rightPadding: Ui.px(14)
    color: pal.text
    placeholderTextColor: Qt.alpha(pal.text, 0.5)
    selectionColor: Qt.alpha(pal.highlight, 0.3)
    selectedTextColor: pal.text
    background: Rectangle {
        radius: Ui.radiusSm + 2
        color: pal.base
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? pal.highlight : Qt.alpha(pal.text, Ui.borderAlpha)
        Behavior on border.color { ColorAnimation { duration: Ui.quick } }
    }
}
