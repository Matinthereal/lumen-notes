import QtQuick
import Lumen

// A popover grows in where it was opened: 250 ms on Material's standard curve (ADR mynotes-003).
Transition {
    NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Ui.ms(250); easing.type: Easing.BezierSpline; easing.bezierCurve: Ui.standard }
    NumberAnimation { property: "scale"; from: Ui.reduceMotion ? 1 : 0.94; to: 1; duration: Ui.ms(250); easing.type: Easing.BezierSpline; easing.bezierCurve: Ui.standard }
}
