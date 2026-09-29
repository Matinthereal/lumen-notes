import QtQuick
import Lumen

// …and leaves faster than it came: 150 ms, accelerating away.
Transition {
    NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Ui.ms(150); easing.type: Easing.BezierSpline; easing.bezierCurve: Ui.accelerate }
    NumberAnimation { property: "scale"; from: 1; to: Ui.reduceMotion ? 1 : 0.97; duration: Ui.ms(150); easing.type: Easing.BezierSpline; easing.bezierCurve: Ui.accelerate }
}
