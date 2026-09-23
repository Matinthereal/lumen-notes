import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Choose a page to show beside this one: recent pages first, or type to find one by name.
Popup {
    id: picker
    objectName: "chrome"
    property string title: "Open beside"
    property var excludePageId: 0
    signal chosen(var pageId)
    SystemPalette { id: pal }
    parent: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(460, parent ? parent.width - 32 : 460)
    height: Math.min(520, parent ? parent.height - 120 : 520)
    x: parent ? (parent.width - width) / 2 : 0
    y: 80
    padding: 10
    background: Rectangle { radius: Ui.radiusLg; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1 }
    onOpened: { field.text = ""; field.forceActiveFocus(); refresh() }

    property var rows: []
    property int currentIndex: 0
    function refresh() {
        const q = field.text.trim()
        const list = q.length ? library.linkCandidates(q, excludePageId, 20) : library.recentPages(20)
        rows = list.filter(p => p.id !== excludePageId)
        currentIndex = 0
    }
    function pick(i) { if (i >= 0 && i < rows.length) { const id = rows[i].id; close(); chosen(id) } }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Text { text: picker.title; color: pal.windowText; font.pixelSize: Ui.text + 1; font.weight: Font.DemiBold; Layout.leftMargin: 4 }
        TextField {
            id: field
            Layout.fillWidth: true
            implicitHeight: Ui.target
            placeholderText: "Find a page by name…"
            font.pixelSize: Ui.text
            leftPadding: 12
            background: Rectangle { radius: 8; color: pal.base; border.color: field.activeFocus ? pal.highlight : Qt.alpha(pal.text, Ui.borderAlpha) }
            onTextChanged: picker.refresh()
            Keys.onDownPressed: picker.currentIndex = Math.min(picker.rows.length - 1, picker.currentIndex + 1)
            Keys.onUpPressed: picker.currentIndex = Math.max(0, picker.currentIndex - 1)
            Keys.onReturnPressed: picker.pick(picker.currentIndex)
            Keys.onEnterPressed: picker.pick(picker.currentIndex)
            Keys.onEscapePressed: picker.close()
        }
        Text {
            visible: field.text.trim().length === 0 && picker.rows.length > 0
            text: "Recent"
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; font.capitalization: Font.AllUppercase
            Layout.leftMargin: 4
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: picker.rows
            spacing: 2
            ScrollBar.vertical: ScrollBar { implicitWidth: Ui.scrollbar }
            delegate: Rectangle {
                required property var modelData
                required property int index
                objectName: "pagePickerRow"
                width: ListView.view.width
                implicitHeight: Ui.target + 6
                radius: 8
                color: index === picker.currentIndex ? Qt.alpha(pal.highlight, 0.22) : (rowTap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : "transparent")
                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    spacing: 10
                    Rectangle { implicitWidth: 4; implicitHeight: parent.height - 18; radius: 2; color: modelData.colour || pal.mid }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Text {
                            text: (modelData.title || "").length ? modelData.title.replace("​", "") : "Untitled page"
                            color: pal.windowText; font.pixelSize: Ui.text; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                        Text {
                            text: modelData.notebookName + " › " + modelData.sectionName
                            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }
                }
                TapHandler { id: rowTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { const i = index; Qt.callLater(() => picker.pick(i)) } }
            }
        }
        Text {
            visible: picker.rows.length === 0
            text: field.text.trim().length ? "No page has that in its name." : "No other pages yet."
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1
            Layout.leftMargin: 4
        }
    }
}
