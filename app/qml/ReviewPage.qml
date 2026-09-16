import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Daily review (Phase 8): keyboard-first. Space flips, 1–4 rate, Esc leaves. Shows the interval
// each rating would give (FSRS), then a per-topic table when the queue is empty.
Rectangle {
    id: page
    objectName: "chrome"
    signal closed()
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    SystemPalette { id: pal }
    // A Rectangle accepts no buttons, so without this a drag here would draw ink on the page behind.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    color: pal.window
    focus: visible

    property var queue: []
    property int index: 0
    property bool flipped: false
    property int doneThisSession: 0
    property var current: index < queue.length ? queue[index] : null
    property var intervals: ({})

    function load() { queue = cards.due(200); index = 0; flipped = false; doneThisSession = 0; loadPreview() }
    function loadPreview() { intervals = current ? cards.preview(current.id) : {} }
    function rate(r) { if (!current || !flipped) return; cards.review(current.id, r); doneThisSession++; index++; flipped = false; if (index >= queue.length) { queue = cards.due(200); index = 0 } loadPreview() }
    function clozeFront(t) { return t.replace(/\{\{c\d+::(.+?)\}\}/g, "**[…]**") }
    function clozeBack(t) { return t.replace(/\{\{c\d+::(.+?)\}\}/g, "**$1**") }
    function md(t) { const colour = pal.text.toString(); return t.replace(/\$\$([\s\S]+?)\$\$/g, (m, x) => "\n\n![tex](image://latex/" + encodeURIComponent(x.trim()) + "?c=" + encodeURIComponent(colour) + ")\n\n").replace(/\$([^$\n]+?)\$/g, (m, x) => "![tex](image://latex/" + encodeURIComponent(x.trim()) + "?c=" + encodeURIComponent(colour) + ")") }
    onVisibleChanged: if (visible) { load(); forceActiveFocus() }
    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_Escape) { closed(); e.accepted = true }
        else if (e.key === Qt.Key_Space || e.key === Qt.Key_Return) { flipped = true; e.accepted = true }
        else if (e.key >= Qt.Key_1 && e.key <= Qt.Key_4) { rate(e.key - Qt.Key_0); e.accepted = true }
    }

    ColumnLayout {
        anchors { fill: parent; margins: 28 }
        spacing: 16
        RowLayout {
            Text { text: "Review"; color: pal.windowText; font.pixelSize: 22; font.weight: Font.DemiBold }
            Text { text: cards.dueCount + " due · " + cards.reviewedToday + " done today"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 13; Layout.fillWidth: true }
            Button { text: "Close (Esc)"; onClicked: page.closed() }
        }
        Rectangle {
            visible: page.current !== null
            Layout.fillWidth: true; Layout.fillHeight: true
            radius: 12; color: pal.base; border.color: Qt.alpha(pal.text, 0.14); border.width: 1
            ColumnLayout {
                anchors { fill: parent; margins: 24 }
                spacing: 14
                Text { text: page.current ? (page.current.tag || "") + (page.current.kind === "cloze" ? " · cloze" : "") : ""; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 }
                Text {
                    Layout.fillWidth: true
                    text: page.current ? page.md(page.current.kind === "cloze" ? (page.flipped ? page.clozeBack(page.current.front) : page.clozeFront(page.current.front)) : page.current.front) : ""
                    textFormat: Text.MarkdownText; color: pal.text; font.pixelSize: 20; wrapMode: Text.Wrap
                }
                Rectangle { visible: page.flipped && page.current && page.current.kind !== "cloze"; Layout.fillWidth: true; implicitHeight: 1; color: Qt.alpha(pal.text, 0.14) }
                Text {
                    visible: page.flipped && page.current && page.current.kind !== "cloze"
                    Layout.fillWidth: true
                    text: page.current ? page.md(page.current.back) : ""
                    textFormat: Text.MarkdownText; color: pal.text; font.pixelSize: 18; wrapMode: Text.Wrap
                }
                Item { Layout.fillHeight: true }
                Button { visible: !page.flipped; text: "Show answer (Space)"; Layout.alignment: Qt.AlignHCenter; onClicked: page.flipped = true }
                RowLayout {
                    visible: page.flipped
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 12
                    Repeater {
                        model: [{ r: 1, label: "Again", key: "again", colour: Ui.danger }, { r: 2, label: "Hard", key: "hard", colour: Ui.warning }, { r: 3, label: "Good", key: "good", colour: Ui.good }, { r: 4, label: "Easy", key: "easy", colour: "#1F3A93" }]
                        delegate: Rectangle {
                            required property var modelData
                            width: 120; height: 56; radius: 10
                            color: rh.hovered ? Qt.alpha(modelData.colour, 0.25) : Qt.alpha(modelData.colour, 0.12)
                            border.color: modelData.colour; border.width: 1
                            ColumnLayout {
                                anchors.centerIn: parent; spacing: 2
                                Text { text: modelData.label + "  (" + modelData.r + ")"; color: pal.text; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                                Text { text: page.intervals[modelData.key] || ""; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 11; Layout.alignment: Qt.AlignHCenter }
                            }
                            HoverHandler { id: rh }
                            TapHandler { onTapped: page.rate(modelData.r) }
                        }
                    }
                }
            }
        }
        // Done: stats per topic
        ColumnLayout {
            visible: page.current === null
            Layout.fillWidth: true; Layout.fillHeight: true
            Text { text: page.doneThisSession > 0 ? "All done — " + page.doneThisSession + " reviewed." : "Nothing due right now."; color: pal.text; font.pixelSize: 18; font.weight: Font.DemiBold }
            Text { text: "By topic, last 30 days"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 }
            ListView {
                Layout.fillWidth: true; Layout.fillHeight: true
                model: cards.stats(30); clip: true; spacing: 2
                header: RowLayout { width: ListView.view.width; Text { text: "Topic"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 11; Layout.preferredWidth: 220 } Text { text: "Cards"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 11; Layout.preferredWidth: 60 } Text { text: "Reviews"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 11; Layout.preferredWidth: 70 } Text { text: "Accuracy"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 11; Layout.preferredWidth: 80 } Text { text: "Due"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 11 } }
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    Text { text: modelData.tag; color: pal.text; font.pixelSize: 13; Layout.preferredWidth: 220; elide: Text.ElideRight }
                    Text { text: modelData.cards; color: pal.text; font.pixelSize: 13; Layout.preferredWidth: 60 }
                    Text { text: modelData.reviews; color: pal.text; font.pixelSize: 13; Layout.preferredWidth: 70 }
                    Text { text: modelData.accuracy < 0 ? "–" : Math.round(modelData.accuracy * 100) + "%"; color: modelData.accuracy >= 0 && modelData.accuracy < 0.7 ? Ui.danger : pal.text; font.pixelSize: 13; Layout.preferredWidth: 80 }
                    Text { text: modelData.due; color: pal.text; font.pixelSize: 13 }
                }
            }
        }
    }
}
