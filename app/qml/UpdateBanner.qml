import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// A newer Lumen is out: a capsule in the corner that says so and blocks nothing. It becomes the
// download's progress, then the offer to restart. Later puts it away until the next daily check;
// skipping the version is in What's new.
Rectangle {
    id: banner
    objectName: "chrome"
    signal notesRequested()

    readonly property bool downloading: updater.stage === "downloading"
    readonly property bool ready: updater.stage === "ready"
    readonly property bool failed: updater.stage === "failed"
    // Where Lumen cannot replace itself, the button leads to the download instead.
    readonly property string goLabel: updater.installsItself || Qt.platform.os === "android" ? "Update" : "Get it"

    SystemPalette { id: pal }
    visible: updater.offered
    implicitWidth: row.implicitWidth + 24
    implicitHeight: Math.max(Ui.target + 12, row.implicitHeight + 12)
    radius: Math.min(height / 2, Ui.radiusLg + 8)
    color: pal.base
    border.width: 1
    border.color: Ui.hair
    Elevation { level: 2 }
    // A Rectangle accepts no buttons, so without this a press between the buttons drew on the page.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

    RowLayout {
        id: row
        anchors { fill: parent; leftMargin: 18; rightMargin: 6 }
        spacing: 4
        Rectangle { visible: !banner.downloading; implicitWidth: 10; implicitHeight: 10; radius: 5; color: banner.failed ? Ui.danger : Ui.lamp }
        ProgressChip {
            objectName: "updateProgress"
            visible: banner.downloading
            Layout.leftMargin: -12
            color: "transparent"; border.width: 0
            text: updater.status
            progress: updater.progress
            cancelTip: "Stop the download"
            onCancelRequested: updater.cancel()
        }
        Text {
            objectName: "updateBannerText"
            visible: !banner.downloading
            text: banner.ready || banner.failed ? updater.status : "Lumen " + updater.latestVersion + " is available"
            color: pal.windowText; font.pixelSize: Ui.text; font.weight: Font.Medium
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.maximumWidth: Ui.px(banner.failed ? 380 : 300)
            Layout.leftMargin: 8; Layout.rightMargin: 10
        }
        LButton {
            objectName: "updateNotes"
            visible: !banner.downloading && !banner.ready && !banner.failed
            flat: true; implicitHeight: Ui.target; font.pixelSize: Ui.text
            text: "What’s new"
            onClicked: banner.notesRequested()
        }
        LButton {
            visible: banner.failed
            flat: true; implicitHeight: Ui.target; font.pixelSize: Ui.text
            text: "Release page"
            onClicked: updater.openReleasePage()
        }
        LButton {
            objectName: "updateLater"
            visible: !banner.downloading
            flat: true; implicitHeight: Ui.target; font.pixelSize: Ui.text
            text: "Later"
            onClicked: updater.later()
        }
        LButton {
            objectName: "updateGo"
            visible: !banner.downloading
            primary: true; implicitHeight: Ui.target; font.pixelSize: Ui.text
            text: banner.ready ? "Restart now" : banner.failed ? "Try again" : banner.goLabel
            // On Windows "ready" is the installer starting, and Lumen is already on its way out.
            enabled: !banner.ready || Qt.platform.os !== "windows"
            onClicked: banner.ready ? updater.restart() : updater.install()
        }
    }
}
