import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// The activity rail: one place for every panel. Big targets, works with finger, pen and mouse.
Rectangle {
    id: rail
    objectName: "chrome"
    property string leftPanel: "notebooks"     // notebooks | cards | papers | ""
    property string rightPanel: ""             // claude | handwriting | transcript | page | ""
    property bool tablet: false
    signal searchRequested()
    signal browserRequested()
    signal splitRequested()
    property bool splitOpen: false
    signal trashRequested()
    signal settingsRequested()
    signal tabletRequested()
    signal reviewRequested()
    SystemPalette { id: pal }
    width: Ui.rail
    color: pal.window

    component RailButton: Rectangle {
        id: btn
        property string icon
        property string tip
        property bool on: false
        property string badge: ""
        signal clicked()
        implicitWidth: Ui.rail - 8; implicitHeight: Ui.rail - 8; radius: Ui.radius
        color: on ? Qt.alpha(pal.highlight, 0.22)
             : (railPress.pressed ? Qt.alpha(pal.text, Ui.pressAlpha)
             : (h.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent"))
        Accessible.role: Accessible.Button
        Accessible.name: tip
        Rectangle { visible: on; width: 3; height: parent.height - 14; radius: 1.5; x: -4; y: 7; color: pal.highlight }
        Icon { anchors.centerIn: parent; name: btn.icon; width: Ui.railIcon; height: Ui.railIcon
               colour: btn.on ? pal.highlight : pal.windowText; opacity: btn.on ? 1 : 0.85 }
        Rectangle {
            visible: btn.badge.length > 0
            anchors { right: parent.right; top: parent.top; margins: 4 }
            width: Math.max(16, bt.implicitWidth + 8); height: 16; radius: 8; color: pal.highlight
            Text { id: bt; anchors.centerIn: parent; text: btn.badge; color: pal.highlightedText; font.pixelSize: 10; font.weight: Font.DemiBold }
        }
        HoverHandler { id: h }
        TapHandler { id: railPress; onTapped: btn.clicked() }
        ToolTip.visible: h.hovered && tip.length > 0; ToolTip.text: tip; ToolTip.delay: 500
    }

    ColumnLayout {
        anchors { fill: parent; topMargin: 8; bottomMargin: 8 }
        spacing: 4
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-sidetree"; tip: "Notebooks (Ctrl+\\)"; on: rail.leftPanel === "notebooks"; onClicked: rail.leftPanel = rail.leftPanel === "notebooks" ? "" : "notebooks" }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "edit-find"; tip: "Search everything (Ctrl+K)"; onClicked: rail.searchRequested() }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-preview"; tip: "All pages in this section (Ctrl+P)"; onClicked: rail.browserRequested() }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-split"; tip: rail.splitOpen ? "Close split view (Ctrl+Shift+S)" : "Split view: another page beside this one (Ctrl+Shift+S)"; on: rail.splitOpen; onClicked: rail.splitRequested() }
        Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: Ui.rail - 24; implicitHeight: 1; color: Qt.alpha(pal.text, 0.14); Layout.topMargin: 4; Layout.bottomMargin: 4 }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "tools-wizard"; tip: "Claude (Ctrl+J)"; on: rail.rightPanel === "claude"; onClicked: rail.rightPanel = rail.rightPanel === "claude" ? "" : "claude" }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-list-text"; tip: "Transcript"; on: rail.rightPanel === "transcript"; onClicked: rail.rightPanel = rail.rightPanel === "transcript" ? "" : "transcript" }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "document-properties"; tip: "This page: tags and links (Ctrl+Shift+L)"; on: rail.rightPanel === "page"; onClicked: rail.rightPanel = rail.rightPanel === "page" ? "" : "page" }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "edit-rename"; tip: "Handwriting read from this page (Ctrl+Shift+H)"; on: rail.rightPanel === "handwriting"; onClicked: rail.rightPanel = rail.rightPanel === "handwriting" ? "" : "handwriting" }
        Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: Ui.rail - 24; implicitHeight: 1; color: Qt.alpha(pal.text, 0.14); Layout.topMargin: 4; Layout.bottomMargin: 4 }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-list-details"; tip: "Flashcards (Ctrl+Shift+C) · review Ctrl+Shift+R"; on: rail.leftPanel === "cards"; badge: cards.dueCount > 0 ? String(cards.dueCount) : ""; onClicked: rail.leftPanel = rail.leftPanel === "cards" ? "" : "cards" }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "document-edit-verify"; tip: "Past papers (Ctrl+Shift+P)"; on: rail.leftPanel === "papers"; onClicked: rail.leftPanel = rail.leftPanel === "papers" ? "" : "papers" }
        Item { Layout.fillHeight: true }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "input-tablet"; tip: rail.tablet ? "Tablet mode on (Ctrl+Shift+T)" : "Tablet mode (Ctrl+Shift+T)"; on: rail.tablet; onClicked: rail.tabletRequested() }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "user-trash"; tip: "Recently deleted (Ctrl+Shift+D)"; onClicked: rail.trashRequested() }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "configure"; tip: "Settings (Ctrl+,)"; onClicked: rail.settingsRequested() }
    }
    Rectangle { anchors { right: parent.right; top: parent.top; bottom: parent.bottom } width: 1; color: Qt.alpha(pal.text, 0.14) }
}
