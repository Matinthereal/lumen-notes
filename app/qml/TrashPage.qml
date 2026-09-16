import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Everything you deleted, for thirty days. Undo lives in a toast for eight seconds; this is where
// you go when you notice a week later. Restore puts it back where it came from; the other button
// is the only place in the app where something really goes.
Rectangle {
    id: page
    objectName: "chrome"
    signal closed()
    signal openPage(var pageId)
    signal toast(string message)
    property var rows: []

    SystemPalette { id: pal }

    // A Rectangle accepts no buttons, so without this a drag here would draw ink on the page behind.

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    color: pal.window
    focus: visible
    Keys.onEscapePressed: page.closed()
    onVisibleChanged: if (visible) reload()

    function reload() { rows = library.deletedItems(200) }
    // These run from a list delegate whose context dies the moment the model reloads, so the work
    // lives here on the root and the delegate only hands over plain values.
    function restoreItem(kind, id) {
        if (kind === "card") cards.restore(id)
        else if (kind === "paper") papers.restorePaper(id)
        else library.restore(kind, id)
        reload()
        toast("Restored")
        if (kind === "page") openPage(id)
    }
    function purgeItem(kind, id, name) {
        confirm.ask("Delete this " + kind + " for good?",
                    "“" + ((name && name.length) ? name.replace("\u200b", "") : "Untitled") + "” cannot be recovered after this.",
                    "Delete for good",
                    function() { library.purgeOne(kind, id); page.reload(); page.toast("Deleted for good") })
    }
    function ago(t) {
        const days = Math.floor((Date.now() / 1000 - t) / 86400)
        if (days <= 0) return "today"
        if (days === 1) return "yesterday"
        return days + " days ago"
    }
    function iconFor(kind) {
        return kind === "page" ? "text-x-generic" : kind === "section" ? "folder" : kind === "notebook" ? "folder-documents"
             : kind === "card" ? "view-list-details" : "document-edit-verify"
    }
    Connections { target: library; function onChanged() { if (page.visible) page.reload() } }
    Connections { target: cards; function onChanged() { if (page.visible) page.reload() } }
    Connections { target: papers; function onChanged() { if (page.visible) page.reload() } }

    ConfirmSheet { id: confirm }

    ColumnLayout {
        anchors { fill: parent; margins: 20 }
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Recently deleted"; color: pal.windowText; font.pixelSize: Ui.title; font.weight: Font.DemiBold }
            Text { text: page.rows.length + (page.rows.length === 1 ? " item" : " items") + " · kept for 30 days"
                   color: Qt.alpha(pal.windowText, 0.6); font.pixelSize: Ui.small; Layout.leftMargin: 8 }
            Item { Layout.fillWidth: true }
            Rectangle {
                visible: page.rows.length > 0
                implicitWidth: emptyLabel.implicitWidth + 26; implicitHeight: Ui.target
                radius: 9; color: emptyTap.pressed ? Qt.alpha(Ui.danger, 0.3) : "transparent"
                border.color: Qt.alpha(Ui.danger, 0.6); border.width: 1
                Text { id: emptyLabel; anchors.centerIn: parent; text: "Empty the trash"; color: Ui.danger; font.pixelSize: Ui.text }
                TapHandler { id: emptyTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped:
                    confirm.ask("Empty the trash?", "All " + page.rows.length + " items go for good. Nothing here can be recovered afterwards.",
                                "Empty it", function() {
                                    const all = page.rows.slice()
                                    for (const r of all) library.purgeOne(r.kind, r.id)
                                    page.reload(); page.toast("Trash emptied")
                                })
                }
            }
            Rectangle {
                implicitWidth: closeLabel.implicitWidth + 26; implicitHeight: Ui.target
                radius: 9; color: closeTap.pressed ? Qt.alpha(pal.text, 0.16) : "transparent"
                border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1
                Text { id: closeLabel; anchors.centerIn: parent; text: "Close (Esc)"; color: pal.windowText; font.pixelSize: Ui.text }
                TapHandler { id: closeTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: page.closed() }
            }
        }

        ColumnLayout {
            visible: page.rows.length === 0
            Layout.fillWidth: true; Layout.topMargin: 40; Layout.alignment: Qt.AlignHCenter
            spacing: 6
            Text { text: "The trash is empty"; color: pal.windowText; font.pixelSize: Ui.text + 3; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
            Text { text: "Deleted pages, sections, notebooks, cards and papers wait here for thirty days."
                   color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.text; Layout.alignment: Qt.AlignHCenter }
        }

        ListView {
            id: list
            Layout.fillWidth: true; Layout.fillHeight: true
            visible: page.rows.length > 0
            model: page.rows
            clip: true; spacing: 4
            ScrollBar.vertical: ScrollBar { implicitWidth: Ui.scrollbar; policy: Ui.tablet ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
            delegate: Rectangle {
                objectName: "trashRow"
                required property var modelData
                width: ListView.view.width
                implicitHeight: Ui.target + 12
                radius: 8
                color: rowHover.hovered ? Qt.alpha(pal.text, 0.06) : Qt.alpha(pal.text, 0.03)
                HoverHandler { id: rowHover }
                RowLayout {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 10 }
                    spacing: 12
                    Icon { name: "" + page.iconFor(modelData.kind); implicitWidth: Ui.icon; implicitHeight: Ui.icon; opacity: 0.75 }
                    ColumnLayout {
                        spacing: 0; Layout.fillWidth: true
                        Text {
                            text: (modelData.name && modelData.name.length) ? modelData.name.replace("​", "") : ("Untitled " + modelData.kind)
                            color: pal.windowText; font.pixelSize: Ui.text; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                        Text {
                            text: modelData.where + " · deleted " + page.ago(modelData.deletedAt)
                            color: Qt.alpha(pal.windowText, 0.6); font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.fillWidth: true
                        }
                    }
                    Rectangle {
                        implicitWidth: restoreLabel.implicitWidth + 22; implicitHeight: Ui.target - 6
                        radius: 8; color: restoreTap.pressed ? Qt.alpha(pal.highlight, 0.5) : Qt.alpha(pal.highlight, 0.25)
                        objectName: "trashRestore"
                        Text { id: restoreLabel; anchors.centerIn: parent; text: "Restore"; color: pal.windowText; font.pixelSize: Ui.small + 1 }
                        TapHandler { id: restoreTap; gesturePolicy: TapHandler.ReleaseWithinBounds
                                     onTapped: page.restoreItem(modelData.kind, modelData.id) }
                    }
                    Rectangle {
                        implicitWidth: Ui.target - 6; implicitHeight: Ui.target - 6; radius: 7
                        color: purgeTap.pressed ? Qt.alpha(Ui.danger, 0.35) : (purgeHover.hovered ? Qt.alpha(Ui.danger, 0.15) : "transparent")
                        Icon { anchors.centerIn: parent; name: "edit-delete"; implicitWidth: Ui.icon - 4; implicitHeight: Ui.icon - 4 }
                        HoverHandler { id: purgeHover }
                        ToolTip.visible: purgeHover.hovered; ToolTip.delay: 600; ToolTip.text: "Delete for good"
                        TapHandler { id: purgeTap; gesturePolicy: TapHandler.ReleaseWithinBounds
                                     onTapped: page.purgeItem(modelData.kind, modelData.id, modelData.name) }
                    }
                }
            }
        }
    }
}
