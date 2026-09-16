import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// The options for the thing you have selected, floating just above it: outline colour, fill,
// thickness, duplicate, delete. Opened by a double tap on a selected shape or picture — the way
// every drawing app does it — so the page is not permanently fenced in by a bar.
Popup {
    id: menu
    objectName: "chrome"
    property string subject: "shape"          // shape | picture
    property var info: ({})
    signal outlineChosen(string colour)
    signal fillChosen(string colour)
    signal widthChosen(real width)
    signal duplicateAsked()
    signal deleteAsked()

    SystemPalette { id: pal }
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    padding: 10
    background: Rectangle {
        radius: Ui.radiusLg
        color: pal.window
        border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1
    }

    readonly property var outlines: ["#1A1A1A", "#F2F2F2", "#E0403C", "#1F6FEB", "#12855B", "#B04A12", "#7A3DB8", "#C9A227"]
    readonly property var fills: ["", "#F6C9C7", "#CFE0FA", "#CFEBDD", "#F6E7C1", "#E3D7F5", "#DDE2E8"]

    // Open centred over the object, above it where there is room, below it otherwise.
    function openOver(rect) {
        const w = implicitWidth > 0 ? implicitWidth : 320
        const h = implicitHeight > 0 ? implicitHeight : 120
        let ny = rect.y - h - 12
        if (ny < 8) ny = Math.min(rect.y + rect.height + 12, parent.height - h - 8)
        x = Math.max(8, Math.min(rect.x + rect.width / 2 - w / 2, parent.width - w - 8))
        y = Math.max(8, ny)
        open()
    }

    ColumnLayout {
        objectName: "objectMenuContent"
        spacing: 8

        RowLayout {
            visible: menu.subject === "shape"          // a picture has no outline to set
            spacing: Ui.gap
            Text { text: "Outline"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; Layout.rightMargin: 4 }
            Repeater {
                model: menu.outlines
                delegate: Rectangle {
                    required property string modelData
                    implicitWidth: Ui.icon + 6; implicitHeight: Ui.icon + 6; radius: width / 2
                    color: modelData
                    border.width: (menu.info.stroke === modelData) ? 2.5 : 1
                    border.color: (menu.info.stroke === modelData) ? pal.highlight : Qt.alpha(pal.text, Ui.hairline)
                    TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: menu.outlineChosen(modelData) }
                }
            }
        }
        RowLayout {
            visible: menu.subject === "shape"
            spacing: Ui.gap
            Text { text: "Fill"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; Layout.rightMargin: 4 }
            Repeater {
                model: menu.fills
                delegate: Rectangle {
                    required property string modelData
                    implicitWidth: Ui.icon + 6; implicitHeight: Ui.icon + 6; radius: Ui.radiusSm
                    color: modelData.length ? modelData : "transparent"
                    border.width: (menu.info.fill === modelData) ? 2.5 : 1
                    border.color: (menu.info.fill === modelData) ? pal.highlight : Qt.alpha(pal.text, Ui.borderAlpha)
                    Text { anchors.centerIn: parent; visible: !modelData.length; text: "∅"
                           color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
                    TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: menu.fillChosen(modelData) }
                }
            }
        }
        RowLayout {
            visible: menu.subject === "shape"
            spacing: Ui.gap
            Text { text: "Line"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; Layout.rightMargin: 4 }
            Repeater {
                model: [1.5, 2.5, 4, 7, 11]
                delegate: Rectangle {
                    required property real modelData
                    implicitWidth: Ui.target - 12; implicitHeight: Ui.target - 14; radius: Ui.radiusSm
                    color: Math.abs((menu.info.width || 0) - modelData) < 0.2 ? Qt.alpha(pal.highlight, 0.3) : "transparent"
                    border.color: Qt.alpha(pal.text, Ui.hairline); border.width: 1
                    Rectangle { anchors.centerIn: parent; width: parent.width - 12; height: Math.max(1.5, modelData)
                                radius: height / 2; color: pal.windowText }
                    TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: menu.widthChosen(modelData) }
                }
            }
        }
        RowLayout {
            spacing: Ui.gap
            Layout.topMargin: 2
            component MenuAction: Rectangle {
                property string label: ""
                property bool danger: false
                signal clicked()
                implicitWidth: t.implicitWidth + 22; implicitHeight: Ui.target - 10
                radius: Ui.radiusSm
                color: press.pressed ? Qt.alpha(danger ? Ui.danger : pal.text, Ui.pressAlpha) : "transparent"
                border.color: danger ? Qt.alpha(Ui.danger, 0.6) : Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1
                Text { id: t; anchors.centerIn: parent; text: parent.label; color: parent.danger ? Ui.danger : pal.windowText; font.pixelSize: Ui.small + 1 }
                TapHandler { id: press; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: parent.clicked() }
            }
            MenuAction { label: "Duplicate"; onClicked: { menu.duplicateAsked(); menu.close() } }
            MenuAction { label: "Delete"; danger: true; onClicked: { menu.deleteAsked(); menu.close() } }
            Item { Layout.fillWidth: true }
            MenuAction { label: "Done"; onClicked: menu.close() }
        }
    }
}
