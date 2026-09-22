import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen
// Bottom bar. Recording only starts from a deliberate press-and-hold on the red button (a tap
// shows the hint); touch on this bar is ignored while the pen is near the screen, so a resting
// palm cannot start anything. Idle shows nothing moving; "Test mic" shows the meter on demand.
Item {
    id: panel
    objectName: "chrome"
    required property var canvas
    property var sectionId: 0
    property var pageId: 0
    property var currentRecordingId: 0
    property bool tapToHear: false
    property bool replayInk: false
    property bool testingMic: false
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    signal transcriptRequested()
    property var marksModel: []
    function reloadMarks() { marksModel = currentRecordingId > 0 ? audio.marks(currentRecordingId) : [] }
    onCurrentRecordingIdChanged: reloadMarks()
    Connections { target: audio; function onMarksChanged(rid) { if (rid === panel.currentRecordingId) panel.reloadMarks() } }
    SystemPalette { id: pal }
    // Idle, this is one button. Everything else appears when there is something to control —
    // it was 60 px of permanent chrome on the scarce vertical axis, in the palm's resting band.
    property bool expanded: false
    readonly property bool busy: audio.recording || panel.testingMic || audio.playing || panel.currentRecordingId > 0
    implicitHeight: Ui.target + 16
    height: implicitHeight
    function fmt(ms) { const s = Math.floor(ms / 1000); return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0") }
    ListModel { id: recordingsModel }
    function refreshRecordings() {
        recordingsModel.clear()
        for (const r of audio.recordings(sectionId)) recordingsModel.append(r)
        if (!currentRecordingId && recordingsModel.count) currentRecordingId = recordingsModel.get(0).id
        if (currentRecordingId) { let found = false; for (let i = 0; i < recordingsModel.count; ++i) if (recordingsModel.get(i).id === currentRecordingId) found = true; if (!found) currentRecordingId = recordingsModel.count ? recordingsModel.get(0).id : 0 }
    }
    function startRecording() {
        if (audio.recording) return
        if (audio.startRecording(panel.sectionId, panel.pageId)) { panel.transcriptRequested(); panel.toast("Recording — tap the red button to stop") }
    }
    function testMic() { testingMic = true; audio.monitor(true); micTimer.restart() }
    Timer { id: micTimer; interval: 8000; onTriggered: { panel.testingMic = false; audio.monitor(false) } }
    onSectionIdChanged: { currentRecordingId = 0; refreshRecordings() }
    Component.onCompleted: refreshRecordings()
    Connections {
        target: audio
        function onRecordingsChanged() { panel.refreshRecordings() }
        function onStateChanged() { if (audio.recording) { panel.currentRecordingId = audio.recordingId; panel.testingMic = false; micTimer.stop() } }
        function onPlaybackChanged() { if (panel.replayInk) { canvas.replayRecordingId = audio.playbackRecordingId; canvas.replayMs = audio.playbackMs } }
        function onError(message) { panel.toast(message) }
    }
    Connections { target: canvas; function onStrokeTapped(rid, ms) { panel.currentRecordingId = rid; audio.play(rid, ms) } }
    onTapToHearChanged: canvas.seekMode = tapToHear
    onReplayInkChanged: { canvas.replayRecordingId = replayInk ? currentRecordingId : 0; canvas.replayMs = replayInk ? audio.playbackMs : -1 }
    // Palm guard for the whole bar: touch is only accepted when the pen is away from the screen.
    readonly property bool touchAllowed: !canvas.penNear
    component GuardedTap: Item {
        // two handlers so a finger works when the pen is away, and the pen/mouse always work
        id: guard
        property alias longPressThreshold: touchTap.longPressThreshold
        readonly property bool pressed: touchTap.pressed || otherTap.pressed
        signal tapped()
        signal longPressed()
        anchors.fill: parent
        TapHandler { id: touchTap; acceptedDevices: PointerDevice.TouchScreen; enabled: panel.touchAllowed; onTapped: guard.tapped(); onLongPressed: guard.longPressed() }
        TapHandler { id: otherTap; acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus | PointerDevice.TouchPad; longPressThreshold: touchTap.longPressThreshold; onTapped: guard.tapped(); onLongPressed: guard.longPressed() }
    }
    Rectangle {
        anchors.fill: parent
        color: audio.recording ? Qt.alpha(Ui.danger, 0.12) : pal.window
        Rectangle { anchors.top: parent.top; width: parent.width; height: audio.recording ? 3 : 1; color: audio.recording ? Ui.danger : Qt.alpha(pal.text, 0.14) }
        Flickable {
            anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
            contentWidth: audioRow.implicitWidth
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true
        RowLayout {
            id: audioRow
            height: parent.height
            spacing: 8
            // Record: hold to start, tap to stop.
            Rectangle {
                id: recButton
                property bool holdAction: true      // holding is how a recording starts: no hold tip here
                implicitWidth: Ui.target; implicitHeight: Ui.target; radius: Ui.target / 2
                color: audio.recording ? Ui.danger : (recHover.hovered ? Qt.alpha(Ui.danger, 0.15) : "transparent")
                border.color: Ui.danger; border.width: 2
                Rectangle { anchors.centerIn: parent; width: audio.recording ? Ui.icon - 4 : Ui.icon - 2; height: width; radius: audio.recording ? 3 : width / 2; color: audio.recording ? "white" : Ui.danger }
                // hold progress ring
                Canvas {
                    id: ring
                    anchors.fill: parent
                    property real progress: 0
                    visible: progress > 0 && !audio.recording
                    onProgressChanged: requestPaint()
                    onPaint: { const c = getContext("2d"); c.clearRect(0, 0, width, height); c.strokeStyle = Ui.danger; c.lineWidth = 3; c.beginPath(); c.arc(width / 2, height / 2, width / 2 - 1.5, -Math.PI / 2, -Math.PI / 2 + progress * 2 * Math.PI); c.stroke() }
                    NumberAnimation { id: holdAnim; target: ring; property: "progress"; from: 0; to: 1; duration: 450 }
                }
                HoverHandler { id: recHover }
                GuardedTap {
                    id: recTap
                    longPressThreshold: 0.45
                    onPressedChanged: {
                        if (pressed && !audio.recording) { ring.progress = 0; holdAnim.restart() }
                        else { holdAnim.stop(); ring.progress = 0 }
                    }
                    onTapped: { if (audio.recording) audio.stopRecording(); else panel.toast("Hold the red button to start recording") }
                    onLongPressed: { if (!audio.recording) panel.startRecording() }
                }
                ToolTip.visible: recHover.hovered; ToolTip.delay: 600
                ToolTip.text: audio.recording ? "Tap to stop recording" : "Hold to record the lesson (Ctrl+R)"
            }
            Rectangle {
                visible: audio.recording
                implicitWidth: markText.implicitWidth + 22; implicitHeight: Ui.target - 8; radius: height / 2
                color: markTap.pressed ? Qt.alpha(pal.highlight, 0.5) : Qt.alpha(pal.highlight, 0.25)
                Text { id: markText; anchors.centerIn: parent; text: "Mark"; color: pal.text; font.pixelSize: Ui.small + 1 }
                GuardedTap { id: markTap; onTapped: { audio.addMark(audio.recordingId, audio.nowMs()); panel.toast("Marked at " + panel.fmt(audio.elapsedMs)) } }
                ToolTip.visible: markHover.hovered; ToolTip.delay: 600; ToolTip.text: "Drop a mark here (Ctrl+Shift+M) — jump back to it later"
                HoverHandler { id: markHover }
            }
            Rectangle {
                visible: !panel.busy
                implicitWidth: moreText.implicitWidth + 18; implicitHeight: Ui.target - 10; radius: height / 2
                color: moreTap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : (moreHov.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent")
                Text { id: moreText; anchors.centerIn: parent; text: panel.expanded ? "Hide" : "Audio…"
                       color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
                HoverHandler { id: moreHov }
                GuardedTap { id: moreTap; onTapped: panel.expanded = !panel.expanded }
            }
            Text { visible: audio.recording; text: "REC " + panel.fmt(audio.elapsedMs); color: Ui.danger; font.pixelSize: Ui.text; font.weight: Font.DemiBold; font.family: "monospace" }
            Rectangle { visible: audio.recording; implicitWidth: 8; implicitHeight: 8; radius: 4; color: Ui.danger; SequentialAnimation on opacity { running: audio.recording; loops: Animation.Infinite; NumberAnimation { to: 0.2; duration: 600 } NumberAnimation { to: 1; duration: 600 } } }
            Text { visible: audio.recording; text: audio.listening ? "transcribing live" : "starting transcriber…"; color: audio.listening ? Ui.good : Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
            // Level meter: while recording, or during a mic test.
            Rectangle {
                visible: audio.recording || panel.testingMic
                implicitWidth: 110; implicitHeight: 10; radius: 5; color: Qt.alpha(pal.text, 0.12)
                Rectangle { width: parent.width * Math.min(1, audio.level); height: parent.height; radius: 5; color: audio.level > 0.85 ? Ui.danger : (audio.recording ? Ui.danger : Ui.good) }
            }
            Text { visible: panel.testingMic && !audio.recording; text: "speak — the bar should move"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
            Rectangle {
                visible: (panel.expanded || panel.busy) && !audio.recording && !panel.testingMic
                implicitWidth: mt.implicitWidth + 20; implicitHeight: Ui.target - 8; radius: height / 2
                color: mh.hovered ? Qt.alpha(pal.text, 0.09) : Qt.alpha(pal.text, 0.05)
                Text { id: mt; anchors.centerIn: parent; text: "Test mic"; color: pal.text; font.pixelSize: Ui.small + 1 }
                HoverHandler { id: mh }
                GuardedTap { onTapped: panel.testMic() }
                ToolTip.visible: mh.hovered; ToolTip.delay: 600; ToolTip.text: "Show the microphone level for eight seconds"
            }
            ComboBox {
                id: sourcePick
                visible: !audio.recording && (panel.expanded || panel.busy)
                Layout.preferredWidth: 300; implicitHeight: Ui.target - 6
                model: audio.sources; textRole: "description"; valueRole: "name"; font.pixelSize: Ui.small + 1
                function syncToService() { for (let i = 0; i < audio.sources.length; ++i) if (audio.sources[i].name === audio.source) { currentIndex = i; return } currentIndex = -1 }
                Component.onCompleted: syncToService()
                Connections { target: audio; function onSourcesChanged() { sourcePick.syncToService() } }   // `source` notifies through sourcesChanged
                onActivated: audio.source = currentValue
            }
            Text {
                visible: !audio.recording && !panel.testingMic && audio.micVerdict.length > 0 && audio.micVerdict !== "alive"
                text: "⚠ " + audio.micVerdict; color: Ui.warning; font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.maximumWidth: 300
            }
            Text { visible: !audio.recording && audio.status.length > 0; text: audio.status; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.maximumWidth: 240 }
            Rectangle { visible: recordingsModel.count > 0 && !audio.recording; implicitWidth: 1; implicitHeight: Ui.target - 12; color: Qt.alpha(pal.text, 0.14) }
            ComboBox {
                id: recPick
                visible: recordingsModel.count > 0 && !audio.recording
                Layout.preferredWidth: 200; implicitHeight: Ui.target - 6
                model: recordingsModel; textRole: "title"; font.pixelSize: Ui.small + 1
                function syncToPanel() { for (let i = 0; i < recordingsModel.count; ++i) if (recordingsModel.get(i).id === panel.currentRecordingId) { currentIndex = i; return } currentIndex = -1 }
                Component.onCompleted: syncToPanel()
                Connections { target: panel; function onCurrentRecordingIdChanged() { recPick.syncToPanel() } }
                onActivated: if (currentIndex >= 0) panel.currentRecordingId = recordingsModel.get(currentIndex).id
            }
            Rectangle {
                visible: panel.currentRecordingId > 0 && !audio.recording
                implicitWidth: Ui.target; implicitHeight: Ui.target; radius: Ui.target / 2; color: playHover.hovered ? Qt.alpha(pal.text, 0.1) : Qt.alpha(pal.text, 0.05)
                Icon { anchors.centerIn: parent; name: audio.playing && audio.playbackRecordingId === panel.currentRecordingId ? "media-playback-pause" : "media-playback-start"; implicitWidth: Ui.icon; implicitHeight: Ui.icon }
                HoverHandler { id: playHover }
                GuardedTap { onTapped: { if (audio.playing && audio.playbackRecordingId === panel.currentRecordingId) audio.pause(); else audio.play(panel.currentRecordingId, audio.playbackRecordingId === panel.currentRecordingId ? audio.playbackMs : 0) } }
            }
            Rectangle {
                visible: panel.currentRecordingId > 0 && !audio.recording
                implicitWidth: speedText.implicitWidth + 18; implicitHeight: Ui.target - 8; radius: height / 2
                color: speedTap.pressed ? Qt.alpha(pal.text, 0.2) : Qt.alpha(pal.text, 0.07)
                Text { id: speedText; anchors.centerIn: parent; text: audio.playbackSpeed.toFixed(2).replace(/0+$/, "").replace(/\.$/, "") + "×"
                       color: pal.text; font.pixelSize: Ui.small + 1 }
                GuardedTap { id: speedTap; onTapped: {
                    const steps = [1, 1.25, 1.5, 2, 0.75]
                    let at = 0
                    for (let i = 0; i < steps.length; ++i) if (Math.abs(steps[i] - audio.playbackSpeed) < 0.01) { at = i; break }
                    audio.setPlaybackSpeed(steps[(at + 1) % steps.length])
                } }
                ToolTip.visible: speedHover.hovered; ToolTip.delay: 600; ToolTip.text: "Playback speed"
                HoverHandler { id: speedHover }
            }
            Slider {
                id: scrub
                visible: panel.currentRecordingId > 0 && !audio.recording
                Layout.fillWidth: true
                from: 0; to: Math.max(1, audio.playbackRecordingId === panel.currentRecordingId ? audio.playbackDurationMs : (audio.recordingInfo(panel.currentRecordingId).durationMs || 1))
                onMoved: audio.seek(value)
                // dragging breaks a declarative binding for good, so playback drives the handle explicitly
                Connections { target: audio; function onPlaybackChanged() { if (!scrub.pressed) scrub.value = audio.playbackRecordingId === panel.currentRecordingId ? audio.playbackMs : 0 } }
                Connections { target: panel; function onCurrentRecordingIdChanged() { scrub.value = 0 } }
                // marks sit on the bar where they were dropped
                Repeater {
                    model: panel.marksModel
                    delegate: Rectangle {
                        required property var modelData
                        width: 2; height: 12; radius: 1; color: pal.highlight
                        y: (scrub.height - height) / 2
                        x: scrub.leftPadding + (scrub.availableWidth - 2) * Math.min(1, modelData.tMs / Math.max(1, scrub.to))
                        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; margin: 8; onTapped: audio.play(panel.currentRecordingId, modelData.tMs) }
                    }
                }
            }
            Text { visible: panel.currentRecordingId > 0 && !audio.recording; text: panel.fmt(audio.playbackRecordingId === panel.currentRecordingId ? audio.playbackMs : 0); color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; font.family: "monospace" }
            Item { visible: !(panel.currentRecordingId > 0 && !audio.recording); Layout.fillWidth: true }
            component Toggle: Rectangle {
                property string label; property string tip; property bool on: false; signal clicked()
                implicitWidth: tl.implicitWidth + 20; implicitHeight: Ui.target - 8; radius: height / 2
                color: on ? Qt.alpha(pal.highlight, 0.25) : (th.hovered ? Qt.alpha(pal.text, 0.09) : Qt.alpha(pal.text, 0.05))
                border.width: on ? 1 : 0; border.color: pal.highlight
                Text { id: tl; anchors.centerIn: parent; text: parent.label; color: pal.text; font.pixelSize: Ui.small + 1 }
                HoverHandler { id: th } GuardedTap { onTapped: parent.clicked() }
                ToolTip.visible: th.hovered && tip.length > 0; ToolTip.delay: 600; ToolTip.text: tip
            }
            Toggle { visible: panel.currentRecordingId > 0 && !audio.recording; label: "Tap to hear"; tip: "Tap ink to hear what was said as you wrote it"; on: panel.tapToHear; onClicked: panel.tapToHear = !panel.tapToHear }
            Toggle { visible: panel.currentRecordingId > 0 && !audio.recording; label: "Replay ink"; tip: "Redraw the page in time with playback"; on: panel.replayInk; onClicked: panel.replayInk = !panel.replayInk }
            Rectangle {
                visible: panel.currentRecordingId > 0 || audio.recording
                implicitWidth: Ui.target; implicitHeight: Ui.target; radius: 9; color: trHover.hovered ? Qt.alpha(pal.text, 0.1) : "transparent"
                Icon { anchors.centerIn: parent; name: "view-list-text"; implicitWidth: Ui.icon; implicitHeight: Ui.icon }
                HoverHandler { id: trHover }
                GuardedTap { onTapped: panel.transcriptRequested() }
                ToolTip.visible: trHover.hovered; ToolTip.delay: 600; ToolTip.text: "Transcript"
            }
            Rectangle {
                visible: panel.currentRecordingId > 0 && !audio.recording
                implicitWidth: Ui.target; implicitHeight: Ui.target; radius: 9; color: delHover.hovered ? Qt.alpha(Ui.danger, 0.15) : "transparent"
                Icon { anchors.centerIn: parent; name: "edit-delete"; implicitWidth: Ui.icon; implicitHeight: Ui.icon }
                HoverHandler { id: delHover }
                GuardedTap { onTapped: {
                    const rid = panel.currentRecordingId
                    audio.deleteAudio(rid)
                    panel.toastAction("Audio moved to the trash — the transcript is kept", "Undo", function() { audio.restoreAudio(rid) })
                } }
                ToolTip.visible: delHover.hovered; ToolTip.delay: 600; ToolTip.text: "Move the audio to the trash (keeps the transcript; undoable)"
            }
        }
        }
    }
}