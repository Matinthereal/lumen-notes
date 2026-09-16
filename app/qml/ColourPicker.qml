import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Every colour in one place: a full grid, the colours you used recently, and a hex box for the rest.
Popup {
    id: picker
    objectName: "chrome"
    property string current: ""
    property bool allowNone: false
    signal chosen(string colour)
    SystemPalette { id: pal }
    modal: true
    focus: true
    dim: false
    padding: 12
    width: grid.implicitWidth + 2 * padding
    background: Rectangle { radius: Ui.radiusLg; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1 }

    // Columns are hues, rows run light to dark; the first column is white → black.
    readonly property var colours: [
        "#FFFFFF", "#FFD6D6", "#FFE3C7", "#FFF6BF", "#D9F5CC", "#CCF1EC", "#D4E8FF", "#E4DCFF", "#FAD4EE",
        "#D0D4D9", "#FF8A8A", "#FFB266", "#FFE34D", "#8FDB6E", "#5ED3C4", "#6FB1FF", "#A98BFF", "#F07CCB",
        "#8C939B", "#E5383B", "#F77F00", "#E0B800", "#2FA84F", "#139A8E", "#1F6FEB", "#6F42C1", "#C2327F",
        "#4A5058", "#A4161A", "#B35C00", "#9C7A00", "#1B6E33", "#0B635B", "#123F8C", "#45287F", "#7E1D52",
        "#000000", "#5E0B0D", "#6B3700", "#5C4800", "#0F3D1C", "#063A35", "#0A2552", "#29174B", "#4A1030"
    ]
    readonly property int columns: 9

    property Item source: null
    function openFrom(item) { source = item; recent = loadRecent(); hex.text = current; open(); Qt.callLater(place) }
    function place() {
        if (!source || !parent) return
        const at = source.mapToItem(parent, 0, 0)
        const h = height > 0 ? height : implicitHeight
        let ny = at.y + source.height + 8
        if (ny + h > parent.height - 8) ny = at.y - h - 8
        x = Math.max(8, Math.min(at.x + source.width / 2 - width / 2, parent.width - width - 8))
        y = Math.max(8, Math.min(ny, parent.height - h - 8))
    }
    onHeightChanged: if (visible) place()

    property var recent: []
    function loadRecent() {
        try { const r = JSON.parse(library.setting("colours.recent", "[]")); return Array.isArray(r) ? r.slice(0, columns) : [] }
        catch (e) { return [] }
    }
    function pick(c) {
        const colour = c.length ? String(Qt.color(c)).toUpperCase() : ""
        if (colour.length) {
            const r = [colour].concat(loadRecent().filter(x => x.toUpperCase() !== colour)).slice(0, columns)
            library.setSetting("colours.recent", JSON.stringify(r))
        }
        close()
        chosen(colour)
    }

    component Swatch: Rectangle {
        property string value
        implicitWidth: Ui.target - 6; implicitHeight: Ui.target - 6
        radius: 8
        color: value.length ? value : "transparent"
        border.width: picker.current.toUpperCase() === value.toUpperCase() ? 3 : 1
        border.color: picker.current.toUpperCase() === value.toUpperCase() ? pal.highlight : Qt.alpha(pal.text, 0.18)
        Accessible.role: Accessible.Button
        Accessible.name: value.length ? value : "No colour"
        Text { visible: !parent.value.length; anchors.centerIn: parent; text: "∅"; color: pal.text; font.pixelSize: Ui.text }
        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: picker.pick(parent.value) }
    }

    ColumnLayout {
        id: grid
        spacing: 8
        Text { text: "Colours"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
        GridLayout {
            columns: picker.columns; rowSpacing: 4; columnSpacing: 4
            Repeater { model: picker.colours; delegate: Swatch { required property string modelData; value: modelData } }
        }
        Text { visible: picker.recent.length > 0; text: "Recent"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
        RowLayout {
            visible: picker.recent.length > 0 || picker.allowNone; spacing: 4
            Swatch { visible: picker.allowNone; value: "" }
            Repeater { model: picker.recent; delegate: Swatch { required property string modelData; value: modelData } }
        }
        RowLayout {
            spacing: 8
            Rectangle {
                implicitWidth: Ui.target - 6; implicitHeight: Ui.target - 6; radius: 8
                color: /^#?[0-9a-fA-F]{6}$/.test(hex.text) ? (hex.text[0] === "#" ? hex.text : "#" + hex.text) : "transparent"
                border.width: 1; border.color: Qt.alpha(pal.text, 0.18)
            }
            TextField {
                id: hex
                objectName: "colourHex"
                Layout.fillWidth: true
                implicitHeight: Ui.target
                placeholderText: "#RRGGBB"
                font.pixelSize: Ui.text
                maximumLength: 7
                validator: RegularExpressionValidator { regularExpression: /^#?[0-9a-fA-F]{0,6}$/ }
                onAccepted: if (/^#?[0-9a-fA-F]{6}$/.test(text)) picker.pick(text[0] === "#" ? text : "#" + text)
            }
            Button {
                text: "Use"
                implicitHeight: Ui.target
                enabled: /^#?[0-9a-fA-F]{6}$/.test(hex.text)
                onClicked: picker.pick(hex.text[0] === "#" ? hex.text : "#" + hex.text)
            }
        }
    }
}
