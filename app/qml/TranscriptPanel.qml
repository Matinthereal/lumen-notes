import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Right-hand transcript for the current recording. Live lines arrive while recording; tap to seek.
Rectangle {
    id: panel
    objectName: "chrome"
    property var recordingId: 0
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    SystemPalette { id: pal }
    color: pal.window
    ListModel { id: segments }
    function reload() { segments.clear(); if (!recordingId) return; for (const s of audio.segments(recordingId)) segments.append(s); list.positionViewAtEnd() }
    onRecordingIdChanged: reload()
    Connections { target: audio; function onSegmentsChanged(rid) { if (rid === panel.recordingId) panel.reload() } }
    function fmt(ms) { const s = Math.floor(ms / 1000); return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0") }
    ColumnLayout {
        anchors { fill: parent; margins: 12 }
        spacing: 8
        RowLayout {
            Text { text: audio.recording ? "Live transcript" : "Transcript"; color: pal.windowText; font.pixelSize: Ui.text + 2; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Rectangle { visible: audio.recording; implicitWidth: 10; implicitHeight: 10; radius: 5; color: audio.listening ? Ui.good : Ui.warning; SequentialAnimation on opacity { running: audio.recording; loops: Animation.Infinite; NumberAnimation { to: 0.3; duration: 700 } NumberAnimation { to: 1; duration: 700 } } }
            Text { text: segments.count + " lines"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
            Rectangle {
                visible: panel.recordingId > 0 && !audio.recording
                implicitWidth: Ui.target - 8; implicitHeight: Ui.target - 8; radius: 7; color: mh.hovered ? Qt.alpha(pal.text, 0.1) : "transparent"
                Icon { anchors.centerIn: parent; name: "overflow-menu"; implicitWidth: Ui.icon; implicitHeight: Ui.icon }
                HoverHandler { id: mh }
                TapHandler { onTapped: menu.openFrom(mh.parent) }
                ToolTip.visible: mh.hovered; ToolTip.delay: 600; ToolTip.text: "Transcript options"
            }
        }
        ConfirmSheet { id: confirm }
        ActionSheet {
            id: menu
            parent: Overlay.overlay
            title: "This recording"
            items: [
                { label: "Re-transcribe with the larger model", icon: "view-refresh", action: () => audio.retranscribe(panel.recordingId) },
                { label: "Delete transcript (keep audio)", icon: "edit-clear-all", danger: true, action: () => {
                    const rid = panel.recordingId
                    audio.deleteTranscript(rid); panel.reload()
                    panel.toastAction("Transcript deleted — the audio is still here", "Re-transcribe", function() { audio.retranscribe(rid) })
                } },
                { label: "Delete audio (keep transcript)", icon: "edit-delete", danger: true, action: () => {
                    const rid = panel.recordingId
                    audio.deleteAudio(rid)
                    panel.toastAction("Audio moved to the trash", "Undo", function() { audio.restoreAudio(rid) })
                } },
                { label: "Delete recording entirely", icon: "edit-delete", danger: true, action: () => {
                    const rid = panel.recordingId
                    confirm.ask("Delete this recording?",
                                "The audio, its transcript and the timing that links it to your ink all go. This one cannot be undone.",
                                "Delete recording",
                                function() { audio.deleteRecording(rid); panel.reload(); panel.toast("Recording deleted") })
                } }
            ]
        }
        Text { visible: audio.status.length > 0; text: audio.status; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ListView {
            id: list
            Layout.fillWidth: true; Layout.fillHeight: true
            model: segments; clip: true; spacing: 4
            delegate: Rectangle {
                required property var id
                required property var t0
                required property var t1
                required property string text
                required property string pass
                width: list.width; height: Math.max(Ui.target, body.implicitHeight + 14); radius: 8
                readonly property bool current: audio.playbackRecordingId === panel.recordingId && audio.playbackMs >= t0 && audio.playbackMs < t1
                color: current ? Qt.alpha(pal.highlight, 0.2) : (h.hovered ? Qt.alpha(pal.text, 0.05) : "transparent")
                RowLayout {
                    anchors { fill: parent; margins: 7 }
                    spacing: 10
                    Text { text: panel.fmt(t0); color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; font.family: "monospace"; Layout.alignment: Qt.AlignTop }
                    Text { id: body; text: parent.parent.text; color: pal.text; font.pixelSize: Ui.text; wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: pass === "live" ? 0.75 : 1 }
                }
                HoverHandler { id: h }
                TapHandler { onTapped: audio.play(panel.recordingId, t0) }
            }
        }
        Text {
            visible: segments.count === 0
            text: audio.recording ? (audio.listening ? "Listening — lines appear every few seconds." : "Starting the transcriber…") : (panel.recordingId ? "No transcript for this recording." : "Record a lesson (Ctrl+R) and the transcript appears here.")
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.text; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
    }
}
