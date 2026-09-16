import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// The keyboard for tablet mode. The app draws it and posts real key events to whatever has focus,
// because on this compositor Qt's own virtual keyboard never activates (D-058). Keys are sized from
// the same tokens as everything else, so they grow with tablet mode.
Rectangle {
    id: board
    objectName: "chrome"
    property bool shifted: false
    property bool symbols: false
    signal hidden()

    SystemPalette { id: pal }
    implicitHeight: rows.implicitHeight + 20
    color: Qt.darker(pal.window, 1.12)
    border.color: Qt.alpha(pal.text, Ui.hairline); border.width: 1

    readonly property var letterRows: [
        ["q","w","e","r","t","y","u","i","o","p"],
        ["a","s","d","f","g","h","j","k","l"],
        ["z","x","c","v","b","n","m"]
    ]
    readonly property var symbolRows: [
        ["1","2","3","4","5","6","7","8","9","0"],
        ["-","/",":",";","(",")","£","&","@","\""],
        [".",",","?","!","'","+","=","%","*"]
    ]
    readonly property var activeRows: symbols ? symbolRows : letterRows

    component Key: Rectangle {
        property string cap: ""
        property string label: cap
        property bool wide: false
        property bool accent: false
        signal pressed()
        Layout.preferredWidth: wide ? Ui.target * 2 : Ui.target
        Layout.preferredHeight: Ui.target
        Layout.fillWidth: wide
        radius: Ui.radiusSm
        color: tap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha)
             : (accent ? Qt.alpha(pal.highlight, 0.25) : Qt.alpha(pal.text, 0.09))
        border.color: Qt.alpha(pal.text, Ui.hairline); border.width: 1
        Text {
            anchors.centerIn: parent
            text: parent.label
            color: pal.windowText
            font.pixelSize: parent.label.length > 2 ? Ui.small : Ui.text + 3
        }
        TapHandler {
            id: tap
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
            gesturePolicy: TapHandler.ReleaseWithinBounds
            onTapped: parent.pressed()
        }
        // Hold backspace or an arrow to repeat, the way a real keyboard does.
        Timer {
            id: repeatTimer
            interval: 90; repeat: true
            running: tap.pressed && (parent.cap === "⌫" || parent.cap === "←" || parent.cap === "→")
            triggeredOnStart: false
            onTriggered: parent.pressed()
        }
    }

    ColumnLayout {
        id: rows
        anchors { fill: parent; margins: 10 }
        spacing: Ui.gap

        Repeater {
            model: board.activeRows
            delegate: RowLayout {
                required property var modelData
                required property int index
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignHCenter
                spacing: Ui.gap
                Item { Layout.preferredWidth: index === 1 ? Ui.target / 2 : (index === 2 ? Ui.target : 0) }
                Repeater {
                    model: modelData
                    delegate: Key {
                        required property string modelData
                        cap: modelData
                        label: board.shifted && !board.symbols ? modelData.toUpperCase() : modelData
                        onPressed: {
                            keys.type(board.shifted && !board.symbols ? modelData.toUpperCase() : modelData)
                            if (board.shifted) board.shifted = false
                        }
                    }
                }
                Item { Layout.fillWidth: true; Layout.preferredWidth: 0 }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Ui.gap
            Key { cap: "⇧"; label: board.shifted ? "SHIFT" : "⇧"; accent: board.shifted; onPressed: board.shifted = !board.shifted }
            Key { cap: "?123"; label: board.symbols ? "abc" : "?123"; onPressed: board.symbols = !board.symbols }
            Key { cap: ","; onPressed: keys.type(",") }
            Key { cap: " "; label: "space"; wide: true; onPressed: keys.type(" ") }
            Key { cap: "."; onPressed: keys.type(".") }
            Key { cap: "←"; onPressed: keys.key(Qt.Key_Left) }
            Key { cap: "→"; onPressed: keys.key(Qt.Key_Right) }
            Key { cap: "⌫"; label: "⌫"; onPressed: keys.key(Qt.Key_Backspace) }
            Key { cap: "⏎"; label: "⏎"; accent: true; onPressed: keys.key(Qt.Key_Return) }
            Key { cap: "hide"; label: "Hide"; onPressed: board.hidden() }
        }
    }
}
