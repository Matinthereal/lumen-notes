import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// One bar for whatever is selected: a shape, lassoed ink, or a picture. It only shows the controls
// that mean something for that thing — ink has no fill, a picture has no outline.
Rectangle {
    id: bar
    objectName: "chrome"
    required property var board
    required property var shapeLayer
    required property var imageLayer
    signal toast(string message)
    signal tagRequested()

    // Ink has no single object to hang a menu on, so it keeps the bar; a shape or a picture shows
    // its own options over itself when you tap it again.
    readonly property string subject: board.hasSelection ? "ink" : ""
    property int styleTick: 0
    readonly property var shapeInfo: (bar.styleTick >= 0 && shapeLayer.selectedId) ? shapeLayer.selected() : ({})
    Connections { target: shapes; function onChanged() { bar.styleTick++ } }
    Connections { target: board; function onSelectionChanged() { bar.styleTick++ } }
    Connections { target: board; function onDocumentChanged() { bar.styleTick++ } }
    // What the current selection actually looks like, so the swatches and weights show what is on
    // it. For ink this used to read shapeInfo, which is always empty for ink — nothing ever lit up.
    readonly property real currentWidth: bar.styleTick >= 0
        ? (subject === "shape" ? (bar.shapeInfo.width || 0) : (subject === "ink" ? board.selectionWidth() : 0)) : 0
    readonly property string currentColour: bar.styleTick >= 0
        ? (subject === "shape" ? (bar.shapeInfo.stroke || "") : (subject === "ink" ? board.selectionColour() : "")) : ""
    readonly property bool canFill: subject === "shape"
    readonly property bool canOutline: subject === "shape" || subject === "ink"

    property var palette8: ["#1A1A1A", "#E0403C", "#1F6FEB", "#12855B", Ui.warning, "#7A3DB8", "#C9A227", "#F2F2F2"]

    visible: subject.length > 0
    implicitHeight: Ui.target + 12
    implicitWidth: Math.min(row.implicitWidth + 24, parent ? parent.width - 32 : 900)
    radius: Ui.radiusLg
    color: Qt.alpha(pal.window, 0.97)
    border.color: Qt.alpha(pal.text, 0.18); border.width: 1
    SystemPalette { id: pal }

    function applyOutline(colour) {
        if (subject === "shape") shapeLayer.restyle(colour, undefined, 0)
        else if (subject === "ink") board.restyleSelection(colour, 1.0)
    }
    function applyFill(colour) { if (subject === "shape") shapeLayer.restyle("", colour, 0) }
    function applyWidth(w) {
        if (subject === "shape") shapeLayer.restyle("", undefined, w)
        else if (subject === "ink") board.setSelectionWidth(w)
    }

    Flickable {
        anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
        contentWidth: row.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true

    RowLayout {
        id: row
        objectName: "styleBar"
        anchors.verticalCenter: parent.verticalCenter
        x: Math.max(0, (parent.width - implicitWidth) / 2)
        height: parent.height
        spacing: Ui.gap

        Text {
            text: bar.subject === "shape" ? "Shape" : bar.subject === "ink" ? "Selection" : "Picture"
            color: Qt.alpha(pal.windowText, 0.7); font.pixelSize: Ui.small
        }
        Rectangle { implicitWidth: 1; implicitHeight: Ui.target - 16; color: Qt.alpha(pal.text, 0.16) }

        // outline
        Text { visible: bar.canOutline; text: "line"; color: Qt.alpha(pal.windowText, 0.6); font.pixelSize: Ui.small }
        Repeater {
            model: bar.canOutline ? bar.palette8 : 0
            delegate: Rectangle {
                required property string modelData
                implicitWidth: Ui.icon + 4; implicitHeight: Ui.icon + 4; radius: width / 2
                color: modelData
                border.width: bar.currentColour.toUpperCase() === modelData.toUpperCase() ? 2 : 0; border.color: pal.highlight
                TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: bar.applyOutline(modelData) }
            }
        }
        Rectangle {
            visible: bar.canOutline
            objectName: "moreOutline"
            implicitWidth: Ui.icon + 4; implicitHeight: Ui.icon + 4; radius: width / 2
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#E5383B" }
                GradientStop { position: 0.33; color: "#E0B800" }
                GradientStop { position: 0.66; color: "#2FA84F" }
                GradientStop { position: 1.0; color: "#6F42C1" }
            }
            Text { anchors.centerIn: parent; text: "+"; color: "white"; font.pixelSize: Ui.small; font.weight: Font.Bold }
            Accessible.role: Accessible.Button
            Accessible.name: "More line colours"
            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { stylePicker.target = "outline"; stylePicker.allowNone = false; stylePicker.current = bar.currentColour; stylePicker.openFrom(parent) } }
        }

        // fill
        Rectangle { visible: bar.canFill; implicitWidth: 1; implicitHeight: Ui.target - 16; color: Qt.alpha(pal.text, 0.16) }
        Text { visible: bar.canFill; text: "fill"; color: Qt.alpha(pal.windowText, 0.6); font.pixelSize: Ui.small }
        Rectangle {
            visible: bar.canFill
            implicitWidth: Ui.icon + 4; implicitHeight: Ui.icon + 4; radius: 5
            color: "transparent"; border.width: 1.5; border.color: Qt.alpha(pal.text, 0.5)
            Text { anchors.centerIn: parent; text: "∅"; color: Qt.alpha(pal.text, 0.7); font.pixelSize: Ui.small }
            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: bar.applyFill("") }
        }
        Repeater {
            model: bar.canFill ? ["#F6C9C7", "#CFE0FA", "#CFEBDD", "#F6E7C1", "#E3D7F5", "#DDE2E8", "#1A1A1A"] : 0
            delegate: Rectangle {
                required property string modelData
                implicitWidth: Ui.icon + 4; implicitHeight: Ui.icon + 4; radius: 5
                color: modelData
                border.width: bar.shapeInfo.fill === modelData ? 2 : 0; border.color: pal.highlight
                TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: bar.applyFill(modelData) }
            }
        }
        Rectangle {
            visible: bar.canFill
            objectName: "moreFill"
            implicitWidth: Ui.icon + 4; implicitHeight: Ui.icon + 4; radius: 5
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: "#E5383B" }
                GradientStop { position: 0.33; color: "#E0B800" }
                GradientStop { position: 0.66; color: "#2FA84F" }
                GradientStop { position: 1.0; color: "#6F42C1" }
            }
            Text { anchors.centerIn: parent; text: "+"; color: "white"; font.pixelSize: Ui.small; font.weight: Font.Bold }
            Accessible.role: Accessible.Button
            Accessible.name: "More fill colours"
            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { stylePicker.target = "fill"; stylePicker.allowNone = true; stylePicker.current = (bar.shapeInfo.fill || ""); stylePicker.openFrom(parent) } }
        }

        // thickness
        Rectangle { visible: bar.canOutline; implicitWidth: 1; implicitHeight: Ui.target - 16; color: Qt.alpha(pal.text, 0.16) }
        Repeater {
            model: bar.canOutline ? [1.5, 2.5, 4, 7] : 0
            delegate: Rectangle {
                required property real modelData
                implicitWidth: Ui.target - 14; implicitHeight: Ui.target - 14; radius: 7
                color: Math.abs(bar.currentWidth - modelData) < 0.2 ? Qt.alpha(pal.highlight, 0.3) : "transparent"
                Rectangle { anchors.centerIn: parent; width: Ui.icon; height: Math.max(1.5, modelData); radius: height / 2; color: pal.windowText }
                TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: bar.applyWidth(modelData) }
            }
        }

        Rectangle { implicitWidth: 1; implicitHeight: Ui.target - 16; color: Qt.alpha(pal.text, 0.16) }
        component BarAction: Rectangle {
            property string label: ""
            property bool danger: false
            signal clicked()
            implicitWidth: at.implicitWidth + 20; implicitHeight: Ui.target - 12
            radius: 8
            color: bt.pressed ? Qt.alpha(danger ? Ui.danger : pal.text, 0.25) : "transparent"
            border.color: danger ? Qt.alpha(Ui.danger, 0.55) : Qt.alpha(pal.text, 0.22); border.width: 1
            Text { id: at; anchors.centerIn: parent; text: parent.label; color: parent.danger ? Ui.danger : pal.windowText; font.pixelSize: Ui.small + 1 }
            TapHandler { id: bt; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: parent.clicked() }
        }
        BarAction {
            objectName: "tagPill"
            visible: bar.subject === "ink"
            label: "# Tag"
            onClicked: bar.tagRequested()
            ToolTip.visible: tagHover.hovered; ToolTip.delay: 600
            ToolTip.text: "Read this handwriting and add it to the page as a tag"
            HoverHandler { id: tagHover }
        }
        BarAction {
            label: "Duplicate"
            onClicked: {
                if (bar.subject === "shape") bar.shapeLayer.duplicateSelected()
                else if (bar.subject === "ink") { bar.board.copySelection(); bar.board.paste() }
                else bar.toast("Pictures are duplicated by adding them again")
            }
        }
        BarAction {
            label: "Delete"; danger: true
            onClicked: {
                if (bar.subject === "shape") bar.shapeLayer.removeSelected()
                else if (bar.subject === "ink") bar.board.deleteSelection()
                else bar.imageLayer.removeSelected()
            }
        }
        }
    }
    ColourPicker {
        id: stylePicker
        property string target: "outline"
        parent: Overlay.overlay
        onChosen: (c) => { if (target === "fill") bar.applyFill(c); else bar.applyOutline(c) }
    }
}
