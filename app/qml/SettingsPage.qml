import QtQuick
import Lumen
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs

// Settings: pen defaults, palm rejection, page defaults, backends, backups, tablet mode. All
// stored in the library's settings table and applied live.
Rectangle {
    id: page
    objectName: "chrome"
    required property var canvas
    signal closed()
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    property int paperTick: 0
    property int keyboardTick: 0
    SystemPalette { id: pal }
    // A Rectangle accepts no buttons, so without this a drag here would draw ink on the page behind.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    color: pal.window
    focus: visible
    Keys.onEscapePressed: closed()
    function save(k, v) { library.setSetting(k, String(v)) }
    FolderDialog { id: folderDlg; title: "Backup folder"; onAccepted: { const p = selectedFolder.toString().replace("file://", ""); page.save("backup.dir", p); backupPath.text = p } }

    ColumnLayout {
        anchors { fill: parent; margins: 28 }
        spacing: 14
        RowLayout {
            Text { text: "Settings"; color: pal.windowText; font.pixelSize: 22; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Button { text: "Close (Esc)"; onClicked: page.closed() }
        }
        ScrollView {
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                width: page.width - 56
                spacing: 18
                component Section: Text { color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 11; font.letterSpacing: 1.2; font.capitalization: Font.AllUppercase; Layout.topMargin: 8 }
                component RowL: RowLayout { Layout.fillWidth: true; spacing: 12 }
                component Lbl: Text { color: pal.text; font.pixelSize: 13; Layout.preferredWidth: 220 }

                Section { text: "Pen" }
                RowL { Lbl { text: "Pressure ceiling (firm stroke = full width)" } Slider { from: 0.4; to: 1.0; stepSize: 0.05; value: Number(library.setting("pen.ceiling", "0.85")); Layout.preferredWidth: 220; onMoved: { canvas.pressureCeiling = value; page.save("pen.ceiling", value) } } Text { text: canvas.pressureCeiling.toFixed(2); color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 } }
                RowL { Lbl { text: "Input smoothing (0 = raw, as approved)" } Slider { from: 0; to: 1; stepSize: 0.1; value: Number(library.setting("pen.smoothing", "0")); Layout.preferredWidth: 220; onMoved: { canvas.smoothing = value; page.save("pen.smoothing", value) } } }
                RowL { Lbl { text: "Prediction (ms ahead)" } Slider { from: 0; to: 33; stepSize: 1; value: Number(library.setting("pen.predictionMs", "16.7")); Layout.preferredWidth: 220; onMoved: { canvas.predictionMs = value; canvas.predictionEnabled = value > 0; page.save("pen.predictionMs", value) } } }
                RowL { Lbl { text: "Shape snap when the pen rests" } Switch { id: snapSwitch; checked: canvas.shapeSnap; onToggled: { canvas.shapeSnap = checked; page.save("pen.shapeSnap", checked ? 1 : 0) }
                                                                Connections { target: canvas; function onStyleChanged() { snapSwitch.checked = canvas.shapeSnap } } } }
                RowL { Lbl { text: "Highlighter opacity" } Slider { from: 0.15; to: 0.6; stepSize: 0.05; value: Number(library.setting("pen.highlighterOpacity", "0.35")); Layout.preferredWidth: 220; onMoved: { canvas.highlighterOpacity = value; page.save("pen.highlighterOpacity", value) } } }

                Section { text: "Touch" }
                RowL { Lbl { text: "Ignore touch after the pen leaves (ms)" } Slider { from: 0; to: 1500; stepSize: 50; value: Number(library.setting("touch.palmMs", "500")); Layout.preferredWidth: 220; onMoved: { canvas.palmRejectMs = value; page.save("touch.palmMs", value) } } Text { text: canvas.palmRejectMs + " ms"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 } }
                RowL { Lbl { text: "Eraser size (px)" } Slider { from: 6; to: 40; stepSize: 1; value: Number(library.setting("pen.eraserRadius", "12")); Layout.preferredWidth: 220; onMoved: { canvas.eraserRadius = value; page.save("pen.eraserRadius", value) } } }

                Section { text: "Pages" }
                RowL { Lbl { text: "Default style for new pages" } ComboBox { model: ["dotted", "grid", "lined", "cornell", "plain"]; currentIndex: model.indexOf(library.setting("page.style", "dotted")); onActivated: page.save("page.style", currentText) } }
                RowL { Lbl { text: "Paper colour for new pages" }
                       Repeater { model: [["Charcoal", "#22262B"], ["White", "#FFFFFF"], ["Cream", "#F6F1E4"], ["Cool grey", "#EDEFF2"]]
                                  delegate: Button { required property var modelData; text: modelData[0]; font.pixelSize: 11
                                                     highlighted: page.paperTick >= 0 && library.setting("page.paper", "#22262B") === modelData[1]
                                                     onClicked: { page.save("page.paper", modelData[1]); paperTick = paperTick + 1 } } } }
                RowL { Lbl { text: "New pages are" } ComboBox { textRole: "label"; valueRole: "value"; implicitWidth: 260
                                                      model: [{ label: "Typed (keyboard)", value: "typed" }, { label: "Handwritten, A4", value: "a4" }, { label: "Handwritten, infinite", value: "infinite" }]
                                                      Component.onCompleted: currentIndex = indexOfValue(library.setting("page.sizeMode", "typed"))
                                                      onActivated: page.save("page.sizeMode", currentValue) } }

                Section { text: "Tablet mode" }

                RowL { Lbl { text: "On-screen keyboard" }

                       Repeater { model: [["In tablet mode", "tablet"], ["Always", "always"], ["Never", "never"]]

                                  delegate: Button { required property var modelData; text: modelData[0]; font.pixelSize: 11

                                                     highlighted: page.keyboardTick >= 0 && library.setting("keyboard.mode", "tablet") === modelData[1]

                                                     onClicked: { page.save("keyboard.mode", modelData[1]); page.keyboardTick++ } } } }

                Text { text: "Typing on a folded-back screen: the keyboard appears when a text box asks for it. Qt draws it inside this window, because Wayland will not let an app open the system one."

                       color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.Wrap; Layout.fillWidth: true }

                RowL { Lbl { text: "Tablet mode now" } Switch { id: tabletSwitch; checked: tabletMode.tablet; onToggled: tabletMode.tablet = checked
                                                                Connections { target: tabletMode; function onTabletChanged() { tabletSwitch.checked = tabletMode.tablet } } } Text { text: tabletMode.kwinAvailable ? ("Plasma reports: " + (tabletMode.kwinTablet ? "tablet" : "laptop")) : "Plasma tablet-mode D-Bus not found"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 } }
                RowL { Lbl { text: "Rotate display" } Repeater { model: ["none", "left", "inverted", "right"]; delegate: Button { required property string modelData; text: modelData; font.pixelSize: 11; highlighted: tabletMode.rotation === modelData; onClicked: tabletMode.rotateDisplay(modelData) } } }
                Text { text: "This laptop exposes no hinge switch or accelerometer to Linux, so tablet mode and rotation are yours to set (HARDWARE.md)."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }

                Section { text: "Background services" }
                ServicesSection {
                    id: services
                    Layout.fillWidth: true
                    onToast: (m) => page.toast(m)
                    Connections { target: page; function onVisibleChanged() { if (page.visible) services.checkAll() } }
                }

                Section { text: "Transcription" }
                RowL { Lbl { text: "Live model" } ComboBox { model: ["tiny.en", "base.en", "small.en", "medium.en"]; currentIndex: model.indexOf(library.setting("audio.liveModel", "small.en")); onActivated: page.save("audio.liveModel", currentText) } }
                RowL { Lbl { text: "Re-pass model" } ComboBox { model: ["small.en", "medium.en", "large-v3-turbo", "large-v3"]; currentIndex: model.indexOf(library.setting("audio.repassModel", "large-v3-turbo")); onActivated: page.save("audio.repassModel", currentText) } }
                RowL { Lbl { text: "Backend" } Text { text: audio.backend; color: pal.text; font.pixelSize: 13 } Button { text: "Benchmark on the latest recording"; font.pixelSize: 11; onClicked: { const rs = audio.recordings(library.page(Number(library.setting("lastPage", "0"))).sectionId || 0); if (rs.length) audio.runBenchmark(rs[0].id); else page.toast("Record something in this section first") } } }
                Connections { target: audio; function onBenchmarkDone(r) { let s = "Chosen: " + r.chosen + ". "; for (const x of r.results) s += x.backend + (x.available ? " " + x.realtime_factor + "× realtime" : " unavailable (" + (x.reason || "") + ")") + "; "; page.toast(s) } }

                Section { text: "Handwriting" }
                RowL { Lbl { text: "OCR model" } ComboBox { Layout.preferredWidth: 320; model: ["microsoft/trocr-small-handwritten", "microsoft/trocr-base-handwritten"]; currentIndex: model.indexOf(library.setting("ocr.model", "microsoft/trocr-small-handwritten")); onActivated: page.save("ocr.model", currentText) } Text { text: "takes effect after restart"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 11 } }

                Section { text: "Backups" }
                RowL { Lbl { text: "Folder" } TextField { id: backupPath; Layout.fillWidth: true; text: library.setting("backup.dir", ""); placeholderText: "~/Backups/lumen"; font.pixelSize: 12; onEditingFinished: page.save("backup.dir", text) } Button { text: "Choose…"; font.pixelSize: 11; onClicked: folderDlg.open() } }
                RowL { Lbl { text: "Keep" } SpinBox { from: 1; to: 60; value: Number(library.setting("backup.keep", "7")); onValueModified: page.save("backup.keep", value) } Text { text: "nightly copies at 03:00 (systemd user timer)"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 } }
                RowL { Lbl { text: "" } Button { text: "Back up now"; onClicked: { backupRunner.run() } } Text { id: backupStatus; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 } }
                Item { id: backupRunner; function run() { backupStatus.text = "running…"; backupNow.start() } Timer { id: backupNow; interval: 10; onTriggered: { const r = backupTool.runNow(); backupStatus.text = r } } }

                Section { text: "Claude" }
                RowL { Lbl { text: "Status" } Text { text: claude.available ? claude.version : (claude.reason || "checking…"); color: pal.text; font.pixelSize: 13 } Button { text: "Re-check"; font.pixelSize: 11; onClicked: claude.refreshStatus() } }
                RowL { Lbl { text: "Online (web tools)" } Switch { id: onlineSwitch; checked: claude.online; onToggled: claude.online = checked 
                     Connections { target: claude; function onStatusChanged() { onlineSwitch.checked = claude.online } } } }
                Text { text: "Every prompt is shown before it is sent and logged under claude-log/. Audio is never sent."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }
}
