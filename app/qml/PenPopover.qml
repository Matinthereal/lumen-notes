import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Colour, width and pen style in one place (ADR mynotes-003). The toolbar keeps only the tools,
// the favourite pens and this, so it stays short enough for a tablet held upright.
Popup {
    id: pop
    objectName: "penPopover"
    required property InkCanvas canvas
    property var colours: []
    property var widths: []
    signal colourChosen(string colour)
    signal moreColours()
    SystemPalette { id: pal }
    padding: 16
    // Focus, so Escape and Android's Back close it: without it Back went past it and left the app.
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Card {}
    enter: PopIn {}
    exit: PopOut {}

    function openFrom(item) {
        const p = item.mapToItem(parent, 0, item.height)
        x = Math.max(8, Math.min(parent.width - implicitWidth - 8, p.x + item.width / 2 - implicitWidth / 2))
        y = p.y + 10
        open()
    }
    readonly property color active: canvas.tool === "highlighter" ? canvas.highlighterColor : canvas.penColor

    contentItem: ColumnLayout {
        spacing: 14
        GridLayout {
            columns: 5
            rowSpacing: 10; columnSpacing: 10
            Repeater {
                model: pop.colours
                delegate: Item {
                    required property string modelData
                    readonly property bool current: pop.active.toString().toLowerCase() === modelData.toLowerCase()
                    implicitWidth: Ui.target; implicitHeight: Ui.target
                    Accessible.role: Accessible.Button
                    Accessible.name: "Colour " + modelData
                    Rectangle {
                        anchors.centerIn: parent
                        width: Ui.px(34); height: width; radius: width / 2
                        color: modelData
                        border.width: modelData.toUpperCase() === "#FFFFFF" ? 1 : 0; border.color: Qt.alpha(pal.text, Ui.borderAlpha)
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        visible: parent.current
                        width: Ui.px(42); height: width; radius: width / 2
                        color: "transparent"; border.width: 2; border.color: pal.highlight
                    }
                    TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: pop.colourChosen(modelData) }
                }
            }
            Item {
                objectName: "moreColours"
                implicitWidth: Ui.target; implicitHeight: Ui.target
                Accessible.role: Accessible.Button
                Accessible.name: "More colours"
                Rectangle {
                    anchors.centerIn: parent
                    width: Ui.px(34); height: width; radius: width / 2
                    color: Qt.alpha(pal.text, 0.06); border.width: 1; border.color: Qt.alpha(pal.text, Ui.borderAlpha)
                    Icon { anchors.centerIn: parent; name: "list-add"; implicitWidth: Ui.icon - 4; implicitHeight: Ui.icon - 4 }
                }
                TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { pop.close(); pop.moreColours() } }
            }
        }
        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Ui.hair }
        RowLayout {
            spacing: 6
            Repeater {
                model: pop.widths
                delegate: Rectangle {
                    required property real modelData
                    readonly property bool current: Math.abs(pop.canvas.penWidth - modelData) < 0.01
                    implicitWidth: Ui.target + 8; implicitHeight: Ui.target - 4; radius: height / 2
                    color: current ? Ui.accentSoft : (widthTap.pressed ? Qt.alpha(pal.text, 0.14) : Qt.alpha(pal.text, 0.06))
                    Accessible.role: Accessible.Button
                    Accessible.name: "Width " + modelData
                    Rectangle { anchors.centerIn: parent; width: Ui.icon + 4; height: Math.max(1.5, modelData * 1.8); radius: height / 2
                                color: parent.current ? pal.highlight : pal.text }
                    TapHandler {
                        id: widthTap
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onTapped: { if (pop.canvas.hasSelection) pop.canvas.setSelectionWidth(modelData); else pop.canvas.penWidth = modelData }
                    }
                }
            }
        }
        // Ink or pencil: a pencil stroke stays a pencil stroke, and a page can hold both. The ink
        // styles below are the ink's; a pencil darkens with pressure and shades when leant over.
        RowLayout {
            visible: pop.canvas.tool !== "highlighter"
            spacing: 6
            Repeater {
                model: [["Ink", "ink"], ["Pencil", "pencil"]]
                delegate: LButton {
                    required property var modelData
                    objectName: "brushButton"
                    text: modelData[0]
                    highlighted: pop.canvas.brush === modelData[1]
                    Accessible.name: text + (highlighted ? ", chosen" : "")
                    onClicked: { pop.canvas.brush = modelData[1]; library.setSetting("pen.brush", modelData[1]) }
                }
            }
        }
        Text {
            visible: pop.canvas.tool !== "highlighter" && pop.canvas.brush === "pencil"
            text: "Press lightly for a pale line; lean the pen right over to shade."
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
            Layout.maximumWidth: 280; wrapMode: Text.Wrap
        }
        RowLayout {
            visible: pop.canvas.tool !== "highlighter" && pop.canvas.brush !== "pencil"
            spacing: 6
            Repeater {
                model: [["Classic", "classic"], ["Fountain", "fountain"], ["Ballpoint", "ballpoint"], ["Brush", "brush"]]
                delegate: LButton {
                    required property var modelData
                    text: modelData[0]
                    highlighted: pop.canvas.penStyle === modelData[1]
                    font.pixelSize: Ui.small
                    onClicked: { pop.canvas.penStyle = modelData[1]; library.setSetting("pen.style", modelData[1]) }
                }
            }
        }
        Text {
            text: "Hold a favourite pen in the bar to store this one there."
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
            Layout.maximumWidth: 280; wrapMode: Text.Wrap
        }
    }
}
