import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// The activity rail: one place for every panel. Big targets, works with finger, pen and mouse.
// A floating capsule on the desk (ADR mynotes-003); its round buttons nest inside its round ends.
Rectangle {
    id: rail
    objectName: "chrome"
    property string leftPanel: "notebooks"     // notebooks | cards | papers | ""
    property string rightPanel: ""             // claude | handwriting | transcript | page | ""
    property bool tablet: false
    signal searchRequested()
    signal browserRequested()
    signal libraryRequested()
    signal splitRequested()
    property bool splitOpen: false
    property bool handwriting: true            // off when the maker only types: nothing to read
    signal trashRequested()
    signal settingsRequested()
    signal tabletRequested()
    signal reviewRequested()
    SystemPalette { id: pal }
    width: Ui.rail + 16
    color: "transparent"

    component RailButton: Rectangle {
        id: btn
        objectName: "railButton"
        property string icon
        property string tip
        property bool on: false
        property string badge: ""
        signal clicked()
        implicitWidth: Ui.rail - 12; implicitHeight: Ui.rail - 12; radius: height / 2
        color: on ? Ui.accentSoft
             : (railPress.pressed ? Qt.alpha(pal.text, Ui.pressAlpha)
             : (h.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent"))
        Behavior on color { ColorAnimation { duration: Ui.quick } }
        Accessible.role: Accessible.Button
        Accessible.name: tip
        Icon { anchors.centerIn: parent; name: btn.icon; width: Ui.railIcon; height: Ui.railIcon
               colour: btn.on ? pal.highlight : pal.windowText; opacity: btn.on ? 1 : 0.8 }
        Rectangle {
            visible: btn.badge.length > 0
            anchors { right: parent.right; top: parent.top; margins: 4 }
            width: Math.max(16, bt.implicitWidth + 8); height: 16; radius: 8; color: pal.highlight
            Text { id: bt; anchors.centerIn: parent; text: btn.badge; color: pal.highlightedText; font.pixelSize: Ui.px(10); font.weight: Font.DemiBold }
        }
        HoverHandler { id: h }
        TapHandler { id: railPress; onTapped: btn.clicked() }
        ToolTip.visible: h.hovered && tip.length > 0; ToolTip.text: tip; ToolTip.delay: 500
    }

    Rectangle {
        id: capsule
        anchors { fill: parent; margins: 8 }
        radius: width / 2
        color: Ui.chrome
        border.width: 1; border.color: Ui.hair
        Elevation { level: 1 }
    }
    ColumnLayout {
        anchors { fill: capsule; topMargin: 6; bottomMargin: 6 }
        spacing: 4
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-library"; tip: "All notebooks (Ctrl+O)"; onClicked: rail.libraryRequested() }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-sidetree"; tip: "Notebooks (Ctrl+\\)"; on: rail.leftPanel === "notebooks"; onClicked: rail.leftPanel = rail.leftPanel === "notebooks" ? "" : "notebooks" }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "edit-find"; tip: "Search everything (Ctrl+K)"; onClicked: rail.searchRequested() }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-preview"; tip: "All pages in this section (Ctrl+P)"; onClicked: rail.browserRequested() }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-split"; tip: rail.splitOpen ? "Close split view (Ctrl+Shift+S)" : "Split view: another page beside this one (Ctrl+Shift+S)"; on: rail.splitOpen; onClicked: rail.splitRequested() }
        Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: Ui.rail - 32; implicitHeight: 1; color: Ui.hair; Layout.topMargin: 4; Layout.bottomMargin: 4 }
        RailButton { visible: helpers; Layout.alignment: Qt.AlignHCenter; icon: "tools-wizard"; tip: "Claude (Ctrl+J)"; on: rail.rightPanel === "claude"; onClicked: rail.rightPanel = rail.rightPanel === "claude" ? "" : "claude" }
        RailButton { visible: helpers; Layout.alignment: Qt.AlignHCenter; icon: "view-list-text"; tip: "Transcript"; on: rail.rightPanel === "transcript"; onClicked: rail.rightPanel = rail.rightPanel === "transcript" ? "" : "transcript" }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "document-properties"; tip: "This page: tags and links (Ctrl+Shift+L)"; on: rail.rightPanel === "page"; onClicked: rail.rightPanel = rail.rightPanel === "page" ? "" : "page" }
        RailButton { visible: rail.handwriting && helpers; Layout.alignment: Qt.AlignHCenter; icon: "edit-rename"; tip: "Handwriting read from this page (Ctrl+Shift+H)"; on: rail.rightPanel === "handwriting"; onClicked: rail.rightPanel = rail.rightPanel === "handwriting" ? "" : "handwriting" }
        Rectangle { Layout.alignment: Qt.AlignHCenter; implicitWidth: Ui.rail - 32; implicitHeight: 1; color: Ui.hair; Layout.topMargin: 4; Layout.bottomMargin: 4 }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "view-list-details"; tip: "Flashcards (Ctrl+Shift+C) · review Ctrl+Shift+R"; on: rail.leftPanel === "cards"; badge: cards.dueCount > 0 ? String(cards.dueCount) : ""; onClicked: rail.leftPanel = rail.leftPanel === "cards" ? "" : "cards" }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "document-edit-verify"; tip: "Past papers (Ctrl+Shift+P)"; on: rail.leftPanel === "papers"; onClicked: rail.leftPanel = rail.leftPanel === "papers" ? "" : "papers" }
        Item { Layout.fillHeight: true }
        RailButton { visible: !mobile; Layout.alignment: Qt.AlignHCenter; icon: "input-tablet"; tip: rail.tablet ? "Tablet mode on (Ctrl+Shift+T)" : "Tablet mode (Ctrl+Shift+T)"; on: rail.tablet; onClicked: rail.tabletRequested() }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "user-trash"; tip: "Recently deleted (Ctrl+Shift+D)"; onClicked: rail.trashRequested() }
        RailButton { Layout.alignment: Qt.AlignHCenter; icon: "configure"; tip: "Settings (Ctrl+,)"; onClicked: rail.settingsRequested() }
    }
}
