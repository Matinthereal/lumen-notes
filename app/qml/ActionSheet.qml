import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Long-press / right-click menu with finger-sized rows. items: [{label, icon, danger, action}]
Popup {
    id: sheet
    objectName: "chrome"
    property string title: ""
    property var items: []
    SystemPalette { id: pal }
    modal: true
    focus: true                 // without this a modal Popup swallows Escape instead of closing
    dim: false
    padding: 6
    width: 260
    background: Rectangle { radius: Ui.radiusLg; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1 }

    // Open under the control that asked for it, flipped above when there is no room, and always
    // fully on screen. Positioned after opening, because a popup does not know its own height until
    // its content is laid out — guessing it put menus off the bottom of the window.
    property Item source: null
    property real wantX: 8
    property real wantY: 8
    function openFrom(item) {
        source = item
        open()
        Qt.callLater(place)
    }
    function place() {
        if (!source || !parent) return
        const at = source.mapToItem(parent, 0, 0)
        const h = height > 0 ? height : implicitHeight
        let ny = at.y + source.height + 8
        if (ny + h > parent.height - 8) ny = at.y - h - 8            // flip above
        wantX = at.x + source.width / 2 - width / 2
        wantY = ny
    }
    // Clamped by binding, not once: the list under the menu can still be scrolling when it opens,
    // and a menu placed from that stale position used to hang off the bottom of the window.
    x: parent ? Math.max(8, Math.min(wantX, parent.width - width - 8)) : wantX
    y: parent ? Math.max(8, Math.min(wantY, parent.height - height - 8)) : wantY
    onHeightChanged: if (visible) place()
    onOpened: place()

    ColumnLayout {
        objectName: "actionSheetContent"
        width: parent.width
        spacing: 2
        Text { visible: sheet.title.length > 0; text: sheet.title; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.fillWidth: true; Layout.margins: 8 }
        Repeater {
            model: sheet.items
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: Ui.target + 4; radius: 8
                color: rh.hovered ? Qt.alpha(modelData.danger ? Ui.danger : pal.text, 0.1) : "transparent"
                objectName: "actionSheetItem"
                RowLayout {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                    spacing: 12
                    Icon { visible: !!modelData.icon && !modelData.swatch; name: modelData.icon || ""
                           colour: modelData.danger ? Ui.danger : pal.windowText }
                    // A row that is about a colour shows the colour; a glyph carries nothing there.
                    Rectangle { visible: !!modelData.swatch
                                implicitWidth: Ui.icon; implicitHeight: Ui.icon; radius: width / 2
                                color: modelData.swatch || "transparent"
                                border.width: 1; border.color: Qt.alpha(pal.text, Ui.borderAlpha) }
                    Text { objectName: "actionSheetLabel"; text: modelData.label; color: modelData.danger ? Ui.danger : pal.text; font.pixelSize: Ui.text; Layout.fillWidth: true }
                }
                HoverHandler { id: rh }
                // An exclusive grab, so the tap cannot also reach the row under the sheet; and the
                // popup stays modal until after the action has run and the lists have settled.
                TapHandler {
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: { const act = modelData.action; Qt.callLater(function() { sheet.close(); if (act) act() }) }
                }
            }
        }
    }
}
