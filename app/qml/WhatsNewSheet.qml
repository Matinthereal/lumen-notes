import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// What a newer release brings: its notes as GitHub shows them, and the three things to do about
// it. Skipping is here rather than on the banner, next to what is being skipped.
Popup {
    id: sheet
    objectName: "chrome"
    signal skipped(string version)
    readonly property string goLabel: updater.installsItself || Qt.platform.os === "android" ? "Update" : "Get it"

    SystemPalette { id: pal }
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round((parent.height - height) / 2)
    width: Math.min(Ui.px(560), parent.width - 32)
    height: Math.min(col.implicitHeight + 2 * padding, parent.height - 48)
    modal: true
    focus: true                                    // so Escape closes it
    padding: 22
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Card {}
    enter: PopIn {}
    exit: PopOut {}

    ColumnLayout {
        id: col
        anchors.fill: parent
        spacing: 10
        Text {
            objectName: "whatsNewTitle"
            text: "Lumen " + updater.latestVersion
            color: pal.windowText; font.pixelSize: Ui.title; font.family: Ui.titleFont; font.weight: Font.Medium
            Layout.fillWidth: true
        }
        Text {
            text: "You have " + updater.currentVersion
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
            Layout.fillWidth: true; Layout.topMargin: -6
        }
        Flickable {
            id: flick
            Layout.fillWidth: true; Layout.fillHeight: true
            implicitHeight: notes.implicitHeight
            contentWidth: width; contentHeight: notes.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { implicitWidth: Ui.scrollbar; policy: flick.contentHeight > flick.height && Ui.tablet ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
            Text {
                id: notes
                objectName: "whatsNewNotes"
                width: flick.width - Ui.scrollbar - 6
                text: updater.notes.length ? updater.notes : "This release came without notes."
                textFormat: Text.MarkdownText
                color: pal.text; linkColor: pal.highlight
                font.pixelSize: Ui.text; lineHeight: 1.15
                wrapMode: Text.Wrap
                onLinkActivated: (link) => Qt.openUrlExternally(link)
            }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.topMargin: 6
            spacing: 8
            LButton {
                objectName: "updateSkip"
                flat: true; implicitHeight: Ui.target; font.pixelSize: Ui.text
                text: "Skip this version"
                onClicked: { const v = updater.latestVersion; sheet.close(); updater.skip(); sheet.skipped(v) }
            }
            Item { Layout.fillWidth: true }
            LButton {
                objectName: "whatsNewLater"
                implicitHeight: Ui.target; font.pixelSize: Ui.text
                text: "Later"
                onClicked: { sheet.close(); updater.later() }
            }
            LButton {
                objectName: "whatsNewGo"
                primary: true; implicitHeight: Ui.target; font.pixelSize: Ui.text
                text: sheet.goLabel
                onClicked: { sheet.close(); updater.install() }
            }
        }
    }
}
