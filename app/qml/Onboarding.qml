import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// First run: four things worth knowing, then out of the way. Never shown again unless asked.
Rectangle {
    id: page
    objectName: "chrome"
    signal done()
    SystemPalette { id: pal }
    color: Qt.alpha(pal.window, 0.96)
    focus: visible
    Keys.onEscapePressed: done()
    Rectangle {
        anchors.centerIn: parent
        width: 560; height: col.implicitHeight + 56; radius: 14
        color: pal.base; border.color: Qt.alpha(pal.text, 0.16); border.width: 1
        ColumnLayout {
            id: col
            anchors { fill: parent; margins: 28 }
            spacing: 14
            Text { text: "Lumen"; color: pal.windowText; font.pixelSize: 26; font.weight: Font.DemiBold }
            Text { text: "Notes you can type or write by hand. Four things to know:"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 14 }
            component Tip: RowLayout { property string icon; property string body; spacing: 12; Layout.fillWidth: true
                Icon { name: "" + icon; implicitWidth: 24; implicitHeight: 24; Layout.alignment: Qt.AlignTop }
                Text { text: body; color: pal.text; font.pixelSize: 14; wrapMode: Text.Wrap; Layout.fillWidth: true } }
            Tip { icon: "insert-text"; body: "New pages are typed pages: just start typing. Ctrl+B, Ctrl+I and Ctrl+U format text, Ctrl+Alt+1–3 make headings, Ctrl+Shift+8 starts a list." }
            Tip { icon: "draw-freehand"; body: "Got a pen or touchscreen? Make a handwritten page from the sidebar (hold a section) or the start screen, and draw with the toolbar at the top." }
            Tip { icon: "edit-find"; body: "Ctrl+K searches everything you have typed, PDFs you have imported and, with the AI features on, recognised handwriting and transcripts." }
            Tip { icon: "folder"; body: "Notebooks hold sections, sections hold pages. Everything is saved as you go and stays on this computer." }
            Button { text: "Start"; Layout.alignment: Qt.AlignRight; onClicked: page.done() }
        }
    }
    MouseArea { anchors.fill: parent; z: -1; acceptedButtons: Qt.AllButtons; hoverEnabled: true }   // really swallow taps on the dim
}
