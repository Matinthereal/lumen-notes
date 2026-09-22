import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Typing [[ offers the pages you could link to, filtered as you type. The text field keeps the
// keyboard the whole time: arrows, Enter and Escape are passed in by the editor, and a tap on a
// row picks it. The last row makes a new page when no title matches exactly.
Popup {
    id: picker
    objectName: "chrome"
    property string query: ""
    property var excludePageId: 0
    property int currentIndex: 0
    signal chosen(var pageId, string title)
    signal createAsked(string title)

    SystemPalette { id: pal }
    modal: false
    focus: false
    closePolicy: Popup.NoAutoClose
    padding: 6
    width: 340
    background: Rectangle { radius: Ui.radiusLg; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1 }

    readonly property var candidates: visible ? library.linkCandidates(query, excludePageId, 6) : []
    readonly property bool offerNew: query.trim().length > 0 && !candidates.some(c => c.title.toLowerCase() === query.trim().toLowerCase())
    readonly property int count: candidates.length + (offerNew ? 1 : 0)
    onQueryChanged: currentIndex = 0

    // Opens with its top-left at `at` (overlay coordinates), flipped above the line when it would
    // run off the bottom, and always inside the window.
    function openAt(at, lineHeight) {
        const room = parent ? parent.height : 0
        x = parent ? Math.max(8, Math.min(at.x, parent.width - width - 8)) : at.x
        const h = implicitHeight > 0 ? implicitHeight : 200
        y = at.y + lineHeight + 6 + h > room - 8 ? Math.max(8, at.y - h - 6) : at.y + lineHeight + 6
        if (!visible) open()
    }
    function move(delta) { if (count) currentIndex = (currentIndex + delta + count) % count }
    function acceptCurrent() {
        if (currentIndex < candidates.length) { const c = candidates[currentIndex]; chosen(c.id, c.title) }
        else if (offerNew) createAsked(query.trim())
    }

    ColumnLayout {
        width: parent.width
        spacing: 2
        Text {
            text: picker.query.length ? "Link to a page" : "Link to a page — keep typing to narrow it down"
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
            elide: Text.ElideRight; Layout.fillWidth: true; Layout.margins: 6
        }
        Repeater {
            model: picker.candidates.length + (picker.offerNew ? 1 : 0)
            delegate: Rectangle {
                id: row
                required property int index
                readonly property bool isNew: index >= picker.candidates.length
                readonly property var page: isNew ? null : picker.candidates[index]
                objectName: "linkPickerRow"
                Layout.fillWidth: true
                implicitHeight: Ui.target
                radius: 8
                color: index === picker.currentIndex ? Qt.alpha(pal.highlight, 0.22) : (rowTap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : "transparent")
                RowLayout {
                    anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                    spacing: 10
                    Rectangle { visible: !row.isNew; implicitWidth: 4; implicitHeight: parent.height - 18; radius: 2; color: row.page ? row.page.colour : "transparent" }
                    Icon { visible: row.isNew; name: "document-new"; implicitWidth: Ui.icon - 2; implicitHeight: Ui.icon - 2 }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Text {
                            text: row.isNew ? "New page “" + picker.query.trim() + "”" : row.page.title
                            color: pal.windowText; font.pixelSize: Ui.text; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                        Text {
                            visible: !row.isNew
                            text: row.page ? row.page.notebookName + " › " + row.page.sectionName : ""
                            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }
                }
                TapHandler {
                    id: rowTap
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: { picker.currentIndex = row.index; Qt.callLater(picker.acceptCurrent) }
                }
            }
        }
        Text {
            visible: picker.count === 0
            text: "No titled pages yet. Give a page a title to link to it."
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
            wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.margins: 6
        }
    }
}
