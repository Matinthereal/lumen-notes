import QtQuick
import QtQuick.Layouts
import Lumen

// One page that links to this one: its name, where it lives, and the line the link sits in.
Rectangle {
    id: row
    property var link: ({})
    signal open()
    objectName: "backlinkRow"
    implicitHeight: Math.max(Ui.target + 6, col.implicitHeight + 14)
    radius: 8
    color: tap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : (hover.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent")
    SystemPalette { id: pal }
    Accessible.role: Accessible.Link
    Accessible.name: link.title || ""

    RowLayout {
        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
        spacing: 10
        Rectangle { implicitWidth: 4; implicitHeight: col.implicitHeight - 4; radius: 2; color: row.link.colour || pal.mid }
        ColumnLayout {
            id: col
            spacing: 1
            Layout.fillWidth: true
            Text {
                text: (row.link.title || "").length ? row.link.title : "Untitled page"
                color: pal.windowText; font.pixelSize: Ui.text; elide: Text.ElideRight; Layout.fillWidth: true
            }
            Text {
                visible: text.length > 0
                text: row.link.context || ""
                color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
                wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight; Layout.fillWidth: true
            }
            Text {
                text: (row.link.notebookName || "") + " › " + (row.link.sectionName || "")
                color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small - 1; elide: Text.ElideRight; Layout.fillWidth: true
            }
        }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: row.open() }
}
