import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Shown while a past paper's page is open: questions as chips (tap to jump to the region), marks
// scored / available, topic tags, error type, per-question timer, exam countdown, finish.
Rectangle {
    id: bar
    objectName: "chrome"
    required property var canvas
    property var paperId: 0
    property var pageId: 0
    signal openPage(var pageId)
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    SystemPalette { id: pal }
    color: pal.window
    radius: 10
    border.color: Qt.alpha(pal.text, 0.14); border.width: 1
    implicitHeight: 96
    visible: paperId > 0

    ConfirmSheet { id: qConfirm }
    ListModel { id: qs }
    property var info: ({})
    property int currentQ: -1
    property var current: currentQ >= 0 && currentQ < qs.count ? qs.get(currentQ) : null
    property int qSeconds: 0
    property int examSeconds: 0
    property bool timerRunning: false
    property bool timedMode: false          // an untimed attempt has no clock to run out
    property var answers: ({})   // questionId → {scored, seconds, error}

    function reload() {
        info = papers.paper(paperId)
        qs.clear()
        for (const q of papers.questions(paperId)) qs.append(q)
        if (currentQ >= qs.count) currentQ = -1
    }
    onPaperIdChanged: { reload(); currentQ = -1; answers = {} }
    Connections { target: papers; function onChanged() { bar.reload() } function onAttemptChanged() { if (papers.activeAttempt) { const next = {}; for (const r of papers.attemptResults(papers.activeAttempt)) if (r.scored >= 0) next[r.questionId] = { scored: r.scored, seconds: r.seconds, error: r.errorType }; bar.answers = next; bar.syncFields() } } }
    Timer { interval: 1000; running: bar.timerRunning; repeat: true; onTriggered: { bar.qSeconds++; if (bar.examSeconds > 0) bar.examSeconds--; if (bar.examSeconds === 0 && bar.timedMode && bar.info.minutes > 0 && papers.activeAttempt) { bar.timerRunning = false; bar.toast("Time is up") } } }
    function fmt(s) { return Math.floor(s / 60) + ":" + String(s % 60).padStart(2, "0") }
    function jumpTo(i) {
        // A TapHandler does not move focus, so editingFinished never fires and the typed mark would
        // be lost. Read the fields directly before moving on.
        harvestFields()
        commitCurrent()
        currentQ = i
        qSeconds = (answers[qs.get(i).id] || {}).seconds || 0
        const pages = library.pages(info.sectionId)
        const q = qs.get(i)
        const box = Qt.rect(q.x, q.y, q.w, q.h)          // snapshot: opening a page rebuilds qs
        const pageIndex = q.pageIndex
        if (pageIndex < pages.length && pages[pageIndex].id !== bar.pageId) bar.openPage(pages[pageIndex].id)
        Qt.callLater(() => canvas.flashRect(box, 1500))
    }
    onCurrentChanged: syncFields()
    function syncFields() {
        // TextFields the user types into cannot be bound one-way: after the first edit the binding
        // is gone and the fields would keep showing (and saving to) the previous question.
        const a = current ? (answers[current.id] || {}) : ({})
        scored.text = (current && a.scored !== undefined) ? String(a.scored) : ""
        avail.text = current ? String(current.marks) : ""
        topics.text = current ? current.topics : ""
        err.currentIndex = current ? Math.max(0, err.model.indexOf(a.error || "")) : 0
    }
    function harvestFields() {
        if (!current) return
        const typed = scored.text.trim()
        setAnswer(current.id, { scored: typed.length ? Number(typed) : undefined })
        const marks = Number(avail.text)
        if (avail.text.trim().length && marks !== current.marks) papers.setQuestion(current.id, current.label, marks, current.topics)
        if (topics.text !== current.topics) papers.setQuestion(current.id, current.label, current.marks, topics.text)
    }
    function setAnswer(qid, patch) {
        const next = Object.assign({}, answers)                 // a new object, so the chips re-evaluate
        next[qid] = Object.assign({}, next[qid] || {}, patch)
        answers = next
    }
    function commitCurrent() {
        if (!current || !papers.activeAttempt) return
        const a = answers[current.id] || {}
        papers.recordAnswer(papers.activeAttempt, current.id, a.scored === undefined ? -1 : a.scored, qSeconds, a.error || "")
        setAnswer(current.id, { seconds: qSeconds })
    }

    ColumnLayout {
        anchors { fill: parent; margins: 8 }
        spacing: 4
        RowLayout {
            spacing: 8
            Text { text: (info.year || "") + " " + (info.name || ""); color: pal.windowText; font.pixelSize: Ui.text; font.weight: Font.DemiBold }
            Text { text: info.totalMarks ? info.totalMarks + " marks" : ""; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
            Item { Layout.fillWidth: true }
            Text { visible: papers.activeAttempt > 0 && bar.info.minutes > 0; text: "⏱ " + bar.fmt(bar.examSeconds); color: bar.examSeconds < 300 ? Ui.danger : pal.text; font.pixelSize: Ui.text; font.family: "monospace" }
            Button { visible: papers.activeAttempt === 0; text: "Start attempt"; font.pixelSize: Ui.small; onClicked: { papers.startAttempt(bar.paperId, false); bar.timedMode = false; bar.answers = {}; bar.examSeconds = 0; bar.timerRunning = true; if (qs.count) bar.jumpTo(0) } }
            Button { visible: papers.activeAttempt === 0; text: "Timed"; font.pixelSize: Ui.small; onClicked: { papers.startAttempt(bar.paperId, true); bar.timedMode = true; bar.answers = {}; bar.examSeconds = (bar.info.minutes || 120) * 60; bar.timerRunning = true; if (qs.count) bar.jumpTo(0) }
                     ToolTip.visible: hovered; ToolTip.delay: 600; ToolTip.text: "Exam conditions: a countdown from the paper's time, per-question splits" }
            Button { visible: papers.activeAttempt > 0; text: bar.timerRunning ? "Pause" : "Resume"; font.pixelSize: Ui.small; onClicked: bar.timerRunning = !bar.timerRunning }
            Button { visible: papers.activeAttempt > 0; text: "Finish"; onPressed: bar.harvestFields(); font.pixelSize: Ui.small; onClicked: { bar.commitCurrent(); const s = papers.attemptSummary(papers.activeAttempt); papers.finishAttempt(papers.activeAttempt); bar.timerRunning = false; bar.toast("Attempt finished: " + s.scored + "/" + s.available + " (" + Math.round(s.percent) + "%)") } }
            Button { text: "Detect questions"; font.pixelSize: Ui.small; onClicked: papers.detectQuestions(bar.paperId) }
            Button { text: "Add from lasso"; font.pixelSize: Ui.small; enabled: canvas.hasSelection; onClicked: { const pages = library.pages(info.sectionId); const idx = pages.findIndex(p => p.id === bar.pageId); papers.addQuestion(bar.paperId, "Q" + (qs.count + 1), Math.max(0, idx), canvas.selectionBounds(), 0); canvas.selectNone() }
                     ToolTip.visible: hovered; ToolTip.delay: 600; ToolTip.text: "Lasso the question's area on the page first" }
        }
        RowLayout {
            spacing: 6
            Flickable {
                Layout.fillWidth: true; Layout.preferredHeight: 30; clip: true
                contentWidth: chips.width
                Row {
                    id: chips
                    spacing: 4
                    Repeater {
                        model: qs
                        delegate: Rectangle {
                            required property int index
                            required property var id
                            required property string label
                            required property int marks
                            readonly property var a: bar.answers[id]
                            height: Math.max(30, Ui.target - 8); width: chipText.implicitWidth + 20; radius: height / 2
                            color: bar.currentQ === index ? pal.highlight : (a && a.scored !== undefined ? Qt.alpha(marks > 0 && a.scored === marks ? Ui.good : Ui.warning, 0.2) : Qt.alpha(pal.text, 0.06))
                            Text { id: chipText; anchors.centerIn: parent; text: label + (marks ? " · " + (a && a.scored !== undefined ? a.scored + "/" : "") + marks : ""); color: bar.currentQ === index ? pal.highlightedText : pal.text; font.pixelSize: Ui.small }
                            TapHandler { onTapped: bar.jumpTo(index) }
                        }
                    }
                }
            }
            // editing the current question
            Text { visible: bar.current !== null; text: bar.current ? bar.current.label : ""; color: pal.text; font.pixelSize: Ui.small + 1; font.weight: Font.DemiBold }
            TextField { id: scored; visible: bar.current !== null; Layout.preferredWidth: 46; placeholderText: "got"; font.pixelSize: Ui.small + 1; validator: IntValidator { bottom: 0; top: 99 }
                        onEditingFinished: if (bar.current) { bar.setAnswer(bar.current.id, { scored: text.length ? Number(text) : undefined }); bar.commitCurrent() } }
            Text { visible: bar.current !== null; text: "/"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1 }
            TextField { id: avail; visible: bar.current !== null; Layout.preferredWidth: 46; placeholderText: "of"; font.pixelSize: Ui.small + 1; validator: IntValidator { bottom: 0; top: 99 }
                        onEditingFinished: if (bar.current && Number(text) !== bar.current.marks) papers.setQuestion(bar.current.id, bar.current.label, Number(text), bar.current.topics) }
            TextField { id: topics; visible: bar.current !== null; Layout.preferredWidth: 170; placeholderText: "topics, comma separated"; font.pixelSize: Ui.small + 1
                        onEditingFinished: if (bar.current && text !== bar.current.topics) papers.setQuestion(bar.current.id, bar.current.label, bar.current.marks, text) }
            ComboBox { id: err; visible: bar.current !== null && papers.activeAttempt > 0; Layout.preferredWidth: 140; font.pixelSize: Ui.small + 1
                       model: ["", "careless", "did not know", "ran out of time", "misread"]
                       onActivated: if (bar.current) { bar.setAnswer(bar.current.id, { error: currentText }); bar.commitCurrent() } }
            Text { visible: bar.current !== null && papers.activeAttempt > 0; text: bar.fmt(bar.qSeconds); color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1; font.family: "monospace" }
            Rectangle { visible: bar.current !== null; implicitWidth: Ui.target; implicitHeight: Ui.target; radius: 7
                        color: qdTap.pressed ? Qt.alpha(Ui.danger, 0.28) : (qdh.hovered ? Qt.alpha(Ui.danger, 0.15) : "transparent")
                        Icon { anchors.centerIn: parent; name: "edit-delete"; implicitWidth: Ui.icon - 4; implicitHeight: Ui.icon - 4 }
                        HoverHandler { id: qdh }
                        ToolTip.visible: qdh.hovered; ToolTip.delay: 600; ToolTip.text: "Delete this question"
                        TapHandler { id: qdTap; onTapped: {
                            const qid = bar.current.id, qlabel = bar.current.label
                            qConfirm.ask("Delete question " + qlabel + "?",
                                         "Its marks and topics go with it, and this cannot be undone.",
                                         "Delete question",
                                         function() { bar.currentQ = -1; papers.removeQuestion(qid) })
                        } } }
        }
    }
}
