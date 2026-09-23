import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Every key the app answers to, in one place. Ctrl+/ from anywhere.
Rectangle {
    id: page
    objectName: "chrome"
    signal closed()
    SystemPalette { id: pal }
    // A Rectangle accepts no buttons, so without this a drag here would draw ink on the page behind.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    color: pal.window
    focus: visible
    onVisibleChanged: if (visible) forceActiveFocus()
    Keys.onEscapePressed: page.closed()

    readonly property var groups: [
        { title: "Writing", keys: [
            ["1 · 2 · 3 · 4", "Pen · highlighter · eraser · lasso"],
            ["6", "Text block — then tap where it goes"],
            ["Pen side button", "Hold: temporary eraser · double-press: undo"],
            ["Two-finger double-tap", "Undo"],
            ["Ctrl+Z · Ctrl+Shift+Z", "Undo · redo"],
            ["Ctrl+A · Delete", "Select everything · delete the selection"],
            ["Ctrl+C · Ctrl+X · Ctrl+V", "Copy · cut · paste (a picture on the clipboard lands as a picture)"],
            ["Hold the pen still", "Snap the shape you just drew"]
        ]},
        { title: "Pages", keys: [
            ["Ctrl+N · Ctrl+Shift+N", "New page · new section"],
            ["PgUp · PgDn", "Previous · next page"],
            ["Alt+← · Alt+→", "Back and forward through the pages you have opened"],
            ["Ctrl+P", "All pages in this section, as thumbnails"],
            ["Ctrl+Shift+S", "Split view: another page beside this one (drag the divider to resize)"],
            ["F5", "Present this section — pen draws a fading laser, ← → or tap to move, Esc to leave"],
            ["Ctrl+D", "Duplicate this page"],
            ["Ctrl+B", "Star this page"],
            ["Ctrl+W", "Close the page — nothing open is a fine place to be"],
            ["Ctrl+Shift+D", "Recently deleted"],
            ["Ctrl+0 · Ctrl+1", "Fit the page · fit the width"],
            ["Ctrl+= · Ctrl+-", "Zoom in · out"],
            ["Ctrl+Shift+G", "Add a picture"],
            ["Hold a notebook in the sidebar", "Rename, new section, import PDF, export as a .lumen file"],
            ["Hold a row in the sidebar", "Rename, delete, duplicate, close"]
        ]},
        { title: "Recording and transcript", keys: [
            ["Ctrl+R", "Start recording (or hold the red button)"],
            ["Ctrl+Shift+M", "Drop a mark at this moment"],
            ["Tap ink while playing", "Hear what was said as you wrote it"]
        ]},
        { title: "Finding things", keys: [
            ["Ctrl+K", "Search everything — notes, handwriting, transcripts, PDFs"],
            ["Ctrl+\\", "Notebooks panel"],
            ["Ctrl+J", "Claude"],
            ["Ctrl+Shift+H", "Read this page's handwriting"],
            ["[[ in any text", "Link to another page — tap the link to go there"],
            ["Ctrl+Shift+L", "This page: its tags and what links here"],
            ["#tag in Ctrl+K", "Search only pages with that tag"],
            ["Ctrl+Shift+C · Ctrl+Shift+R", "Flashcards · review"],
            ["Ctrl+Shift+P", "Past papers"]
        ]},
        { title: "The app", keys: [
            ["Ctrl+/", "This list"],
            ["Ctrl+E · Ctrl+H · Ctrl+L · Ctrl+T", "Eraser · highlighter · lasso · text block"],
            ["7", "Shapes"],
            ["Ctrl+M", "Lasso'd maths → LaTeX"],
            ["Ctrl+Shift+E · Ctrl+Shift+O", "Export section · import a PDF"],
            ["F1", "The welcome tips"],
            ["F11", "Full screen"],
            ["Ctrl+Shift+T", "Tablet mode"],
            ["Ctrl+,", "Settings"],
            ["Ctrl+S", "Save now (it saves as you write anyway)"],
            ["Escape", "Close whatever is on top"]
        ]}
    ]

    ScrollView {
        anchors { fill: parent; margins: 24 }
        clip: true
        ColumnLayout {
            width: page.width - 60
            spacing: 18
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Keys"; color: pal.windowText; font.pixelSize: Ui.title; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Rectangle {
                    implicitWidth: closeLabel.implicitWidth + 26; implicitHeight: Ui.target
                    radius: 9; color: closeTap.pressed ? Qt.alpha(pal.text, 0.16) : "transparent"
                    border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1
                    Text { id: closeLabel; anchors.centerIn: parent; text: "Close (Esc)"; color: pal.windowText; font.pixelSize: Ui.text }
                    TapHandler { id: closeTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: page.closed() }
                }
            }
            Repeater {
                model: page.groups
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 4
                    Text { text: modelData.title; color: Qt.alpha(pal.windowText, 0.6); font.pixelSize: Ui.small
                           font.capitalization: Font.AllUppercase; font.letterSpacing: 1.1; Layout.bottomMargin: 4 }
                    Repeater {
                        model: modelData.keys
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 16
                            Rectangle {
                                Layout.preferredWidth: 210
                                implicitHeight: Ui.target - 12
                                radius: 6
                                color: Qt.alpha(pal.text, 0.06)
                                Text { anchors { left: parent.left; leftMargin: 10; verticalCenter: parent.verticalCenter }
                                       text: modelData[0]; color: pal.windowText; font.pixelSize: Ui.small + 1; font.family: "monospace" }
                            }
                            Text { text: modelData[1]; color: Qt.alpha(pal.windowText, 0.85); font.pixelSize: Ui.text
                                   wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    }
}
