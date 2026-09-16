import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen
import QtQuick.Dialogs

// Card list: add by hand, search, delete, export/import Anki, open the review.
Rectangle {
    id: panel
    objectName: "chrome"
    property var pageId: 0
    signal review()
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    SystemPalette { id: pal }
    color: pal.window
    ListModel { id: list }
    function reload() { list.clear(); for (const c of cards.all(filter.text, 300)) list.append(c) }
    Connections { target: cards; function onChanged() { panel.reload() } function onExported(f, n) { panel.toast("Exported " + n + " cards to " + f) } function onImported(n) { panel.toast("Imported " + n + " cards") } function onFailed(m) { panel.toast(m) } }
    Component.onCompleted: reload()
    FileDialog { id: exportDlg; title: "Export cards to Anki"; fileMode: FileDialog.SaveFile; nameFilters: ["Anki package (*.apkg)"]; defaultSuffix: "apkg"; onAccepted: cards.exportAnki(selectedFile) }
    FileDialog { id: importDlg; title: "Import an Anki package"; nameFilters: ["Anki package (*.apkg)"]; onAccepted: cards.importAnki(selectedFile, panel.pageId) }
    ColumnLayout {
        anchors { fill: parent; margins: 10 }
        spacing: 6
        RowLayout {
            Text { text: "Flashcards"; color: pal.windowText; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Button { text: "Review " + (cards.dueCount ? "(" + cards.dueCount + ")" : ""); font.pixelSize: Ui.small; onClicked: panel.review() }
        }
        RowLayout {
            TextField { id: filter; Layout.fillWidth: true; placeholderText: "Filter…"; font.pixelSize: Ui.small + 1; onTextChanged: panel.reload() }
            Button { text: "Export"; font.pixelSize: Ui.small; onClicked: exportDlg.open() }
            Button { text: "Import"; font.pixelSize: Ui.small; onClicked: importDlg.open() }
        }
        // add by hand
        Rectangle {
            Layout.fillWidth: true; radius: 6; color: Qt.alpha(pal.text, 0.04); implicitHeight: addCol.implicitHeight + 12
            ColumnLayout {
                id: addCol
                anchors { fill: parent; margins: 6 }
                spacing: 4
                TextField { id: front; Layout.fillWidth: true; placeholderText: "Front — or a cloze: The {{c1::integral}} of x is …"; font.pixelSize: Ui.small + 1 }
                TextField { id: back; Layout.fillWidth: true; placeholderText: "Back (blank for a cloze)"; font.pixelSize: Ui.small + 1 }
                RowLayout {
                    TextField { id: tag; Layout.fillWidth: true; placeholderText: "Topic tag"; font.pixelSize: Ui.small + 1 }
                    CheckBox { id: rev; text: "Reverse too"; font.pixelSize: Ui.small }
                    Button { text: "Add"; font.pixelSize: Ui.small; enabled: front.text.trim().length > 0
                             onClicked: { cards.add(front.text.includes("{{c1::") ? "cloze" : "basic", front.text.trim(), back.text.trim(), panel.pageId, tag.text.trim(), rev.checked); front.text = ""; back.text = ""; panel.toast("Card added") } }
                }
            }
        }
        ColumnLayout {
            visible: list.count === 0
            Layout.fillWidth: true; Layout.topMargin: 14; spacing: 4
            Text { text: "No cards yet"; color: pal.windowText; font.pixelSize: Ui.text; font.weight: Font.DemiBold }
            Text { text: "Add one above, or ask Claude to make a set from a page you have written."
                   color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.Wrap; Layout.fillWidth: true }
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            model: list; clip: true; spacing: 4
            delegate: Rectangle {
                required property var id
                required property string kind
                required property string front
                required property string back
                required property string tag
                required property var due
                required property int reps
                width: ListView.view.width; height: Math.max(Ui.row, t.implicitHeight + 14); radius: 8; color: Qt.alpha(pal.text, 0.04)
                RowLayout {
                    anchors { fill: parent; margins: 6 }
                    ColumnLayout {
                        id: t
                        Layout.fillWidth: true; spacing: 2
                        Text { text: front; color: pal.text; font.pixelSize: Ui.small + 1; wrapMode: Text.Wrap; Layout.fillWidth: true; maximumLineCount: 2; elide: Text.ElideRight }
                        Text { text: (kind === "cloze" ? "cloze" : back) + (tag.length ? " · " + tag : "") + " · " + reps + " reviews"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Rectangle {
                        implicitWidth: Ui.target; implicitHeight: Ui.target; radius: 7
                        color: delTap.pressed ? Qt.alpha(Ui.danger, 0.28) : (dh.hovered ? Qt.alpha(Ui.danger, 0.15) : "transparent")
                        Icon { anchors.centerIn: parent; name: "edit-delete"; implicitWidth: Ui.icon - 4; implicitHeight: Ui.icon - 4 }
                        HoverHandler { id: dh }
                        TapHandler { id: delTap; onTapped: {
                            const cid = id
                            cards.remove(cid)
                            panel.toastAction("Card deleted", "Undo", function() { cards.restore(cid) })
                        } }
                    }
                }
            }
        }
        Text { text: cards.total + " cards"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
    }
}
