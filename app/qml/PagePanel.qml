import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Everything about the open page that is not on the page itself: what links here.
Rectangle {
    id: panel
    objectName: "chrome"
    property var pageId: 0
    signal openPage(var pageId)
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    SystemPalette { id: pal }
    color: pal.window

    property var backlinks: []
    function reload() { backlinks = pageId > 0 ? library.backlinks(pageId) : [] }
    onPageIdChanged: reload()
    Component.onCompleted: reload()
    Connections {
        target: library
        function onLinksChanged() { panel.reload() }
        function onChanged() { panel.reload() }
    }

    component Caption: Text {
        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; font.capitalization: Font.AllUppercase
        Layout.leftMargin: 4; Layout.topMargin: 10
    }
    component Hint: Text {
        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
        wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 4
    }

    Flickable {
        anchors.fill: parent
        contentHeight: body.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { implicitWidth: Ui.scrollbar }

        ColumnLayout {
            id: body
            x: 12; y: 12
            width: parent.width - 24
            spacing: 4

            Text { text: "This page"; color: pal.windowText; font.pixelSize: 14; font.weight: Font.DemiBold }
            Hint { visible: panel.pageId === 0; text: "Open a page to see what links to it." }

            Caption { visible: panel.pageId > 0; text: "Linked from" }
            Hint {
                visible: panel.pageId > 0 && panel.backlinks.length === 0
                text: "Nothing links here yet. Type [[ in any text to link one page to another."
            }
            Repeater {
                model: panel.pageId > 0 ? panel.backlinks : []
                delegate: BacklinkRow {
                    required property var modelData
                    Layout.fillWidth: true
                    link: modelData
                    onOpen: panel.openPage(modelData.id)
                }
            }
        }
    }
}
