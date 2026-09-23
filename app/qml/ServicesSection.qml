import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Settings › Background services: every Python helper, what it is doing, and a way to restart it.
// The AI features are an optional add-on, so a missing add-on is a normal state with its own
// explanation and the command that installs it — not a wall of "crashed".
ColumnLayout {
    id: section
    spacing: 8
    signal toast(string message)
    SystemPalette { id: pal }

    // What each helper is for, in the words the rest of the app uses. The add-on ones are marked.
    readonly property var about: ({
        ping: { title: "Python", purpose: "Checks that the helpers can run at all", addon: false },
        pdf: { title: "PDF", purpose: "Imports, draws and searches PDFs", addon: false },
        audio: { title: "Recording and transcription", purpose: "Records lessons; with the AI add-on, writes the transcript", addon: true },
        ocr: { title: "Handwriting", purpose: "Reads handwriting for search, and lasso'd maths as LaTeX", addon: true },
        maths: { title: "Maths solver", purpose: "Works out the sums you write", addon: false },
        latex: { title: "Maths typesetting", purpose: "Draws LaTeX blocks", addon: false },
        cards: { title: "Flashcards", purpose: "Imports and exports Anki decks", addon: false },
        claude: { title: "Claude", purpose: "Asks Claude about your notes (needs the Claude Code CLI)", addon: false }
    })
    readonly property var aiPackages: ["torch", "transformers", "PIL", "pix2tex", "faster_whisper"]
    // Reading w.missing for every helper makes this follow each of them as they report in.
    readonly property var aiMissing: {
        const out = []
        for (const w of workers) {
            if (!section.about[w.name] || !section.about[w.name].addon) continue
            for (const m of w.missing.concat(w.missingOptional)) if (section.aiPackages.indexOf(m) >= 0 && out.indexOf(m) < 0) out.push(m)
        }
        return out
    }
    readonly property string installCommand: {
        const venv = "~/.local/share/lumen/venv"
        return "python3 -m venv " + venv + " && " + venv + "/bin/pip install -r " + (workers.length ? workers[0].workersDir : "workers") + "/requirements.txt"
    }
    // Ask every helper what it has; one that is not running starts just long enough to say.
    function checkAll() { for (const w of workers) w.check() }

    component StatusPill: Rectangle {
        property string status
        readonly property color tone: status === "ready" ? Ui.good
                                    : status === "not installed" ? Ui.warning
                                    : status === "crashed" ? Ui.danger
                                    : (status === "busy" || status === "loading model" || status === "starting") ? pal.highlight
                                    : Qt.alpha(pal.windowText, Ui.mutedAlpha)
        implicitWidth: pillRow.implicitWidth + 16; implicitHeight: 26; radius: 13
        color: Qt.alpha(tone, 0.14)
        Row {
            id: pillRow
            anchors.centerIn: parent; spacing: 6
            Rectangle { width: 8; height: 8; radius: 4; color: parent.parent.tone; anchors.verticalCenter: parent.verticalCenter }
            Text { text: status === "stopped" ? "idle — starts when needed" : status; color: pal.text; font.pixelSize: Ui.small }
        }
    }

    // ---- the add-on, when it is not there
    Rectangle {
        objectName: "addOnBanner"
        visible: section.aiMissing.length > 0
        Layout.fillWidth: true
        implicitHeight: banner.implicitHeight + 24
        radius: Ui.radius
        color: Qt.alpha(Ui.warning, 0.10)
        border.color: Qt.alpha(Ui.warning, 0.45); border.width: 1
        ColumnLayout {
            id: banner
            anchors { fill: parent; margins: 12 }
            spacing: 6
            Text { text: "The AI add-on is not installed"; color: pal.windowText; font.pixelSize: Ui.text; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true; wrapMode: Text.Wrap
                color: pal.text; font.pixelSize: Ui.small + 1
                text: "Handwriting search, reading maths from the lasso and lesson transcripts need it. Everything else — typing, ink, PDFs, recording, flashcards — works without it. It is a separate, larger download: PyTorch, plus the handwriting and speech models on first use (missing here: " + section.aiMissing.join(", ") + ")."
            }
            Text { text: "To add it, run this in a terminal, then press Check again:"; color: pal.text; font.pixelSize: Ui.small + 1; Layout.fillWidth: true; wrapMode: Text.Wrap }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                TextField {
                    id: command
                    Layout.fillWidth: true
                    readOnly: true; selectByMouse: true
                    text: section.installCommand
                    font.family: "monospace"; font.pixelSize: Ui.small
                }
                Button { text: "Copy"; onClicked: { command.selectAll(); command.copy(); command.deselect(); section.toast("Command copied") } }
                Button { objectName: "checkAgain"; text: "Check again"; onClicked: { for (const w of workers) if (w.status === "not installed") w.check(); section.toast("Checking the helpers again") } }
            }
        }
    }

    // ---- one row per helper
    Repeater {
        model: workers
        delegate: Rectangle {
            id: rowItem
            required property var modelData
            readonly property var info: section.about[modelData.name] || { title: modelData.name, purpose: "", addon: false }
            objectName: "serviceRow"
            Layout.fillWidth: true
            implicitHeight: rowCol.implicitHeight + 16
            radius: Ui.radiusSm
            color: Qt.alpha(pal.text, 0.04)
            RowLayout {
                anchors { fill: parent; leftMargin: 12; rightMargin: 8 }
                spacing: 12
                ColumnLayout {
                    id: rowCol
                    Layout.fillWidth: true
                    spacing: 2
                    RowLayout {
                        spacing: 8
                        Text { text: rowItem.info.title; color: pal.windowText; font.pixelSize: Ui.text; font.weight: Font.DemiBold }
                        Text { visible: rowItem.info.addon; text: "AI add-on"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small - 1
                               font.capitalization: Font.AllUppercase; font.letterSpacing: 0.8 }
                    }
                    Text { text: rowItem.info.purpose; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    Text {
                        readonly property var gone: rowItem.modelData.missing.concat(rowItem.modelData.missingOptional)
                        visible: gone.length > 0 || (rowItem.modelData.status === "crashed" && rowItem.modelData.lastError.length > 0) || rowItem.modelData.activity.length > 0
                        text: rowItem.modelData.status === "crashed" ? rowItem.modelData.lastError
                            : gone.length > 0 ? "Missing: " + gone.join(", ")
                            : rowItem.modelData.activity + (rowItem.modelData.progress >= 0 ? " — " + Math.round(rowItem.modelData.progress * 100) + "%" : "")
                        color: rowItem.modelData.status === "crashed" ? Ui.danger : pal.text
                        font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.fillWidth: true
                    }
                }
                StatusPill { status: rowItem.modelData.status }
                Button {
                    objectName: "serviceRestart"
                    text: rowItem.modelData.status === "not installed" ? "Check again" : "Restart"
                    enabled: rowItem.modelData.status !== "starting"
                    implicitHeight: Ui.target
                    onClicked: {
                        if (rowItem.modelData.status === "not installed") rowItem.modelData.check()
                        else rowItem.modelData.restart()
                        section.toast("Restarting " + rowItem.info.title.toLowerCase())
                    }
                }
            }
        }
    }
    Text {
        Layout.fillWidth: true; wrapMode: Text.Wrap
        text: "Helpers marked idle start when something needs them and restart themselves after a crash. Python: " + (workers.length ? workers[0].python : "")
        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
    }
}
