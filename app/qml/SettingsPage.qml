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
    signal handChanged(bool left)
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    property int paperTick: 0
    property int handTick: 0
    property int keyboardTick: 0
    // How notes are taken: "both" asks for each new page, "ink" and "typed" never ask, and the
    // settings that only matter to the other kind step out of the way.
    property string mode: library.notesMode()
    property string inkSize: library.inkPageSize()
    onVisibleChanged: if (visible) { mode = library.notesMode(); inkSize = library.inkPageSize() }
    SystemPalette { id: pal }
    property alias flick: scroller.contentItem      // for --shot-js and tests: scroll to a section
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
            Text { text: "Settings"; color: pal.windowText; font.pixelSize: Ui.title; font.family: Ui.titleFont; font.weight: Font.Medium; Layout.fillWidth: true }
            LButton { text: Ui.keys("Close (Esc)"); onClicked: page.closed() }
        }
        ScrollView {
            id: scroller
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                width: page.width - 56
                spacing: 18
                component Section: Text { color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(11); font.letterSpacing: 1.2; font.capitalization: Font.AllUppercase; Layout.topMargin: 8 }
                component RowL: RowLayout { Layout.fillWidth: true; spacing: 12 }
                component Lbl: Text { color: pal.text; font.pixelSize: Ui.px(13); Layout.preferredWidth: 220; Layout.maximumWidth: 220; wrapMode: Text.WordWrap }

                Section { text: "Appearance" }
                RowL { Lbl { text: "Colours" }
                       // "&&": a button reads a single & as a keyboard-shortcut marker and shows "_Lamp".
                       Repeater { model: [["Paper && Lamp", "paper"], ["Follow the system", "system"]]
                                  delegate: LButton { required property var modelData; text: modelData[0]; font.pixelSize: Ui.small
                                                      Accessible.name: modelData[0].replace("&&", "&")
                                                      highlighted: theme.colours === modelData[1]; onClicked: theme.colours = modelData[1] } } }
                RowL { visible: theme.colours === "paper"; Lbl { text: "Light or dark" }
                       Repeater { model: [["Automatic", "auto"], ["Light", "light"], ["Dark", "dark"]]
                                  delegate: LButton { required property var modelData; text: modelData[0]; font.pixelSize: Ui.small
                                                      highlighted: theme.appearance === modelData[1]; onClicked: theme.appearance = modelData[1] } } }
                RowL { Lbl { text: "Reduce motion" }
                       LSwitch { checked: Ui.reduceMotion
                                 onToggled: { Ui.reduceMotion = checked; library.setSetting("ui.reduceMotion", checked ? "1" : "0") } }
                       Text { text: "Menus and panels fade instead of moving"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(12) } }

                Section { text: "Notes" }
                RowL { Lbl { text: "I take notes by" }
                       Repeater { model: [["Handwriting and typing", "both"], ["Handwriting only", "ink"], ["Typing only", "typed"]]
                                  delegate: LButton { required property var modelData; objectName: "modeButton"; text: modelData[0]; font.pixelSize: Ui.px(12)
                                                     implicitHeight: Ui.target; highlighted: page.mode === modelData[1]
                                                     Accessible.name: text + (highlighted ? ", chosen" : "")
                                                     onClicked: { page.save("notes.mode", modelData[1]); page.mode = modelData[1] } } } }
                Text { text: page.mode === "both" ? "The + for a new page asks whether it is handwritten or typed."
                           : page.mode === "ink" ? "New pages are always handwritten, and the pen settings are all here. Typed pages you already have still open."
                           : "New pages are always typed, and the pen settings are put away. Handwritten pages you already have still open."
                       color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.Wrap; Layout.fillWidth: true; Layout.leftMargin: 232; Layout.topMargin: -10 }
                RowL { visible: page.mode !== "typed"; Lbl { text: "Handwritten pages are" }
                       Repeater { model: [["A4 sheets", "a4"], ["Endless (infinite canvas)", "infinite"]]
                                  delegate: LButton { required property var modelData; objectName: "inkSizeButton"; text: modelData[0]; font.pixelSize: Ui.px(12)
                                                     implicitHeight: Ui.target; highlighted: page.inkSize === modelData[1]
                                                     onClicked: { page.save("page.inkSize", modelData[1]); page.inkSize = modelData[1] } } } }

                ColumnLayout { Layout.fillWidth: true; spacing: 18; visible: page.mode !== "typed"
                Section { text: "Pen" }
                RowL { Lbl { text: "Pressure ceiling (firm stroke = full width)" } LSlider { from: 0.4; to: 1.0; stepSize: 0.05; value: Number(library.setting("pen.ceiling", "0.85")); Layout.preferredWidth: 220; onMoved: { canvas.pressureCeiling = value; page.save("pen.ceiling", value) } } Text { text: canvas.pressureCeiling.toFixed(2); color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(12) } }
                RowL { Lbl { text: "Input smoothing (0 = raw)" } LSlider { from: 0; to: 1; stepSize: 0.1; value: Number(library.setting("pen.smoothing", "0")); Layout.preferredWidth: 220; onMoved: { canvas.smoothing = value; page.save("pen.smoothing", value) } } }
                RowL { Lbl { text: "Prediction (ms ahead)" } LSlider { from: 0; to: 33; stepSize: 1; value: Number(library.setting("pen.predictionMs", "16.7")); Layout.preferredWidth: 220; onMoved: { canvas.predictionMs = value; canvas.predictionEnabled = value > 0; page.save("pen.predictionMs", value) } } }
                RowL { Lbl { text: "Shape snap when the pen rests" } LSwitch { id: snapSwitch; checked: canvas.shapeSnap; onToggled: { canvas.shapeSnap = checked; page.save("pen.shapeSnap", checked ? 1 : 0) }
                                                                Connections { target: canvas; function onStyleChanged() { snapSwitch.checked = canvas.shapeSnap } } } }
                RowL { Lbl { text: "Highlighter opacity" } LSlider { from: 0.15; to: 0.6; stepSize: 0.05; value: Number(library.setting("pen.highlighterOpacity", "0.35")); Layout.preferredWidth: 220; onMoved: { canvas.highlighterOpacity = value; page.save("pen.highlighterOpacity", value) } } }
                }

                Section { text: "Touch" }
                RowL { Lbl { text: "Writing hand" }
                       Repeater { model: [["Right", "0"], ["Left", "1"]]
                                  delegate: LButton { required property var modelData; objectName: "handButton"; text: modelData[0]; font.pixelSize: Ui.px(11)
                                                     implicitHeight: Ui.target
                                                     highlighted: page.handTick >= 0 && library.setting("ui.leftHanded", "0") === modelData[1]
                                                     onClicked: { page.save("ui.leftHanded", modelData[1]); page.handTick++; page.handChanged(modelData[1] === "1") } } }
                       Text { text: "Moves the tool rail, the page arrows and the panels to the other side, out from under your hand."
                              color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.Wrap; Layout.fillWidth: true } }
                RowL { Lbl { text: "Ignore touch after the pen leaves (ms)" } LSlider { from: 0; to: 1500; stepSize: 50; value: Number(library.setting("touch.palmMs", "500")); Layout.preferredWidth: 220; onMoved: { canvas.palmRejectMs = value; page.save("touch.palmMs", value) } } Text { text: canvas.palmRejectMs + " ms"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(12) } }
                RowL { Lbl { text: "Eraser size (px)" } LSlider { from: 6; to: 40; stepSize: 1; value: Number(library.setting("pen.eraserRadius", "12")); Layout.preferredWidth: 220; onMoved: { canvas.eraserRadius = value; page.save("pen.eraserRadius", value) } } }

                Section { text: "Pages" }
                RowL { Lbl { text: "Default style for new pages" } ComboBox { model: ["dotted", "grid", "lined", "cornell", "plain"]; currentIndex: model.indexOf(library.setting("page.style", "dotted")); onActivated: page.save("page.style", currentText) } }
                RowL { Lbl { text: "Paper colour for new pages" }
                       Repeater { model: [["Charcoal", "#22262B"], ["White", "#FFFFFF"], ["Cream", "#F6F1E4"], ["Cool grey", "#EDEFF2"]]
                                  delegate: LButton { required property var modelData; text: modelData[0]; font.pixelSize: Ui.px(11)
                                                     highlighted: page.paperTick >= 0 && library.setting("page.paper", "#22262B") === modelData[1]
                                                     onClicked: { page.save("page.paper", modelData[1]); paperTick = paperTick + 1 } } } }

                Section { visible: !mobile; text: "Tablet mode" }

                RowL { visible: !mobile; Lbl { text: "On-screen keyboard" }

                       Repeater { model: [["In tablet mode", "tablet"], ["Always", "always"], ["Never", "never"]]

                                  delegate: LButton { required property var modelData; text: modelData[0]; font.pixelSize: Ui.px(11)

                                                     highlighted: page.keyboardTick >= 0 && library.setting("keyboard.mode", "tablet") === modelData[1]

                                                     onClicked: { page.save("keyboard.mode", modelData[1]); page.keyboardTick++ } } } }

                Text { visible: !mobile; text: "Typing on a folded-back screen: the keyboard appears when a text box asks for it. Qt draws it inside this window, because Wayland will not let an app open the system one."

                       color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.Wrap; Layout.fillWidth: true }

                RowL { visible: !mobile; Lbl { text: "Tablet mode now" } LSwitch { id: tabletSwitch; checked: tabletMode.tablet; onToggled: tabletMode.tablet = checked
                                                                Connections { target: tabletMode; function onTabletChanged() { tabletSwitch.checked = tabletMode.tablet } } } Text { text: tabletMode.kwinAvailable ? ("Plasma reports: " + (tabletMode.kwinTablet ? "tablet" : "laptop")) : "Plasma tablet-mode D-Bus not found"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(12) } }
                RowL { visible: !mobile && tabletMode.canRotate; Lbl { text: "Rotate display" } Repeater { model: ["none", "left", "inverted", "right"]; delegate: LButton { required property string modelData; text: modelData; font.pixelSize: Ui.px(11); highlighted: tabletMode.rotation === modelData; onClicked: tabletMode.rotateDisplay(modelData) } } }
                Text { visible: !mobile; text: "If your 2-in-1 does not switch to tablet mode by itself when you fold it, switch it here or press Ctrl+Shift+T anywhere."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(12); wrapMode: Text.Wrap; Layout.fillWidth: true }

                Section { visible: helpers; text: "Background services" }
                ServicesSection {
                    id: services
                    visible: helpers
                    Layout.fillWidth: true
                    onToast: (m) => page.toast(m)
                    Connections { target: page; function onVisibleChanged() { if (page.visible) services.checkAll() } }
                }

                Section { visible: helpers; text: "Transcription" }
                RowL { visible: helpers; Lbl { text: "Live model" } ComboBox { model: ["tiny.en", "base.en", "small.en", "medium.en"]; currentIndex: model.indexOf(library.setting("audio.liveModel", "small.en")); onActivated: page.save("audio.liveModel", currentText) } }
                RowL { visible: helpers; Lbl { text: "Re-pass model" } ComboBox { model: ["small.en", "medium.en", "large-v3-turbo", "large-v3"]; currentIndex: model.indexOf(library.setting("audio.repassModel", "large-v3-turbo")); onActivated: page.save("audio.repassModel", currentText) } }
                RowL { visible: helpers; Lbl { text: "Backend" } Text { text: audio.backend; color: pal.text; font.pixelSize: Ui.px(13) } LButton { text: "Benchmark on the latest recording"; font.pixelSize: Ui.px(11); onClicked: { const rs = audio.recordings(library.page(Number(library.setting("lastPage", "0"))).sectionId || 0); if (rs.length) audio.runBenchmark(rs[0].id); else page.toast("Record something in this section first") } } }
                Connections { target: audio; function onBenchmarkDone(r) { let s = "Chosen: " + r.chosen + ". "; for (const x of r.results) s += x.backend + (x.available ? " " + x.realtime_factor + "× realtime" : " unavailable (" + (x.reason || "") + ")") + "; "; page.toast(s) } }

                Section { visible: page.mode !== "typed" && helpers; text: "Handwriting" }
                RowL { visible: page.mode !== "typed" && helpers; Lbl { text: "OCR model" } ComboBox { Layout.preferredWidth: 320; model: ["microsoft/trocr-small-handwritten", "microsoft/trocr-base-handwritten"]; currentIndex: model.indexOf(library.setting("ocr.model", "microsoft/trocr-small-handwritten")); onActivated: page.save("ocr.model", currentText) } Text { text: "takes effect after restart"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(11) } }

                Section { text: "Backups" }
                RowL { visible: !mobile; Lbl { text: "Folder" } LField { id: backupPath; Layout.fillWidth: true; text: library.setting("backup.dir", ""); placeholderText: "~/Backups/lumen"; font.pixelSize: Ui.px(12); onEditingFinished: page.save("backup.dir", text) } LButton { text: "Choose…"; font.pixelSize: Ui.px(11); onClicked: folderDlg.open() } }
                RowL { Lbl { text: "Keep" } SpinBox { from: 1; to: 60; value: Number(library.setting("backup.keep", "7")); onValueModified: page.save("backup.keep", value) } Text { text: Qt.platform.os === "linux" ? "nightly copies at 03:00 (systemd user timer)" : "a copy a day, made while Lumen is open"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(12) } }
                RowL { Lbl { text: "" } LButton { text: "Back up now"; onClicked: { backupRunner.run() } } Text { id: backupStatus; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(12) } }
                Item { id: backupRunner; function run() { backupStatus.text = "running…"; backupNow.start() } Timer { id: backupNow; interval: 10; onTriggered: { const r = backupTool.runNow(); backupStatus.text = r } } }

                Section { visible: helpers; text: "Claude" }
                RowL { visible: helpers; Lbl { text: "Status" } Text { text: claude.available ? claude.version : (claude.reason || "checking…"); color: pal.text; font.pixelSize: Ui.px(13) } LButton { text: "Re-check"; font.pixelSize: Ui.px(11); onClicked: claude.refreshStatus() } }
                RowL { visible: helpers; Lbl { text: "Online (web tools)" } LSwitch { id: onlineSwitch; checked: claude.online; onToggled: claude.online = checked
                     Connections { target: claude; function onStatusChanged() { onlineSwitch.checked = claude.online } } } }
                Text { visible: helpers; text: "Every prompt is shown before it is sent and logged under claude-log/. Audio is never sent."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.px(12); wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }
}
