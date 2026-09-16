import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen
import QtQuick.Dialogs

// Past papers: Subject → Year → Paper (board as a tag). Import a paper + mark-scheme pair,
// open a paper (its pages live in the subject's notebook), start an attempt, see the dashboard.
Rectangle {
    id: panel
    objectName: "chrome"
    signal openPage(var pageId)
    signal openDashboard(string subject)
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    SystemPalette { id: pal }
    color: pal.window
    ListModel { id: rows }
    function reload() {
        rows.clear()
        let lastSubject = "", lastYear = -1
        for (const p of papers.papers("")) {
            if (p.subject !== lastSubject) { rows.append({ kind: "subject", label: p.subject, id: 0, sub: "", paper: null }); lastSubject = p.subject; lastYear = -1 }
            if (p.year !== lastYear) { rows.append({ kind: "year", label: String(p.year), id: 0, sub: "", paper: null }); lastYear = p.year }
            rows.append({ kind: "paper", label: p.name, id: p.id, sub: (p.board ? p.board + " · " : "") + p.questionCount + " questions · " + p.attempts + " attempts", paper: p })
        }
    }
    Component.onCompleted: reload()
    Connections { target: papers; function onChanged() { panel.reload() } function onFailed(m) { panel.toast(m) } function onImported(pid, firstPage) { panel.toast("Paper imported — detecting questions…"); if (firstPage) panel.openPage(firstPage) } function onQuestionsDetected(pid, n) { panel.toast(n + " questions detected — check marks and topics in the paper bar") } }

    // Import: paper PDF → mark scheme PDF (optional) → details
    FileDialog { id: paperDlg; title: "Choose the question paper PDF"; nameFilters: ["PDF files (*.pdf)"]; onAccepted: { importForm.paperUrl = selectedFile; schemeDlg.open() } }
    FileDialog { id: schemeDlg; title: "Choose the mark scheme PDF (Cancel to skip)"; nameFilters: ["PDF files (*.pdf)"]; onAccepted: { importForm.schemeUrl = selectedFile; importForm.open() } onRejected: { importForm.schemeUrl = ""; importForm.open() } }
    Popup {
        id: importForm
        objectName: "chrome"
        property var paperUrl: ""
        property var schemeUrl: ""
        parent: Overlay.overlay
        x: (parent.width - width) / 2; y: 120; width: 420; padding: 16; modal: true; focus: true
        background: Rectangle { radius: 10; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha) }
        ColumnLayout {
            anchors.fill: parent; spacing: 8
            Text { text: "New past paper"; color: pal.windowText; font.pixelSize: 15; font.weight: Font.DemiBold }
            ComboBox { id: subj; Layout.fillWidth: true; model: library.notebooks().map(n => n.name); font.pixelSize: Ui.text }
            RowLayout {
                TextField { id: year; Layout.preferredWidth: 90; placeholderText: "Year"; text: String(new Date().getFullYear() - 1); font.pixelSize: Ui.text; validator: IntValidator { bottom: 1990; top: 2100 } }
                TextField { id: nameField; Layout.fillWidth: true; placeholderText: "Paper (e.g. Paper 2)"; font.pixelSize: Ui.text }
            }
            RowLayout {
                TextField { id: board; Layout.fillWidth: true; placeholderText: "board (Edexcel, AQA, OCR)"
                        function suggest() { const n = library.notebooks().find(nb => nb.name === subj.currentText); text = n ? (n.board || "") : "" }
                        Component.onCompleted: suggest()
                        Connections { target: subj; function onCurrentTextChanged() { if (!board.activeFocus) board.suggest() } } }
                TextField { id: minutes; Layout.preferredWidth: 110; placeholderText: "Minutes"; text: "120"; font.pixelSize: Ui.text; validator: IntValidator { bottom: 10; top: 300 } }
            }
            Text { text: "Questions are detected from the PDF's own numbering; you can fix regions and marks afterwards."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.Wrap; Layout.fillWidth: true }
            RowLayout {
                Item { Layout.fillWidth: true }
                Button { text: "Cancel"; onClicked: importForm.close() }
                Button { text: "Import"; enabled: subj.currentText.length > 0; onClicked: { papers.importPair(importForm.paperUrl, importForm.schemeUrl, subj.currentText, Number(year.text), nameField.text, board.text, Number(minutes.text)); importForm.close() } }
            }
        }
    }

    ColumnLayout {
        anchors { fill: parent; margins: 10 }
        spacing: 6
        RowLayout {
            Text { text: "Past papers"; color: pal.windowText; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Button { text: "Import pair"; font.pixelSize: Ui.small; onClicked: paperDlg.open() }
        }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            model: rows; clip: true
            delegate: Rectangle {
                required property string kind
                required property string label
                required property var id
                required property string sub
                required property var paper
                width: ListView.view.width
                height: kind === "paper" ? Ui.row + 8 : Ui.row - 8
                color: kind === "paper" && h.hovered ? Qt.alpha(pal.text, 0.06) : "transparent"
                radius: 6
                RowLayout {
                    anchors { fill: parent; leftMargin: kind === "subject" ? 4 : kind === "year" ? 16 : 28; rightMargin: 6 }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 1
                        Text { text: label; color: pal.windowText; font.pixelSize: kind === "subject" ? 13 : 12; font.weight: kind === "paper" ? Font.Normal : Font.DemiBold; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { visible: kind === "paper"; text: sub; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Rectangle {
                        visible: kind === "subject"; implicitWidth: 88; implicitHeight: Ui.target - 10; radius: height / 2
                        color: dashTap.pressed ? Qt.alpha(pal.highlight, 0.45) : (dh.hovered ? Qt.alpha(pal.highlight, 0.3) : Qt.alpha(pal.highlight, 0.15))
                        Text { anchors.centerIn: parent; text: "Dashboard"; color: pal.text; font.pixelSize: Ui.small }
                        HoverHandler { id: dh }
                        TapHandler { id: dashTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: panel.openDashboard(label) }
                    }
                    Rectangle {
                        // visible without hover (KDE HIG), and its tap must not also open the paper
                        visible: kind === "paper"; implicitWidth: Ui.target - 6; implicitHeight: Ui.target - 6; radius: 7
                        color: rmTap.pressed ? Qt.alpha(Ui.danger, 0.3) : (rh.hovered ? Qt.alpha(Ui.danger, 0.15) : "transparent")
                        opacity: h.hovered || rh.hovered ? 1 : 0.5
                        Icon { anchors.centerIn: parent; name: "edit-delete"; implicitWidth: Ui.icon - 4; implicitHeight: Ui.icon - 4 }
                        HoverHandler { id: rh }
                        ToolTip.visible: rh.hovered; ToolTip.delay: 600; ToolTip.text: "Remove this paper"
                        TapHandler { id: rmTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: {
                            const pid = id, plabel = label
                            papers.removePaper(pid)
                            panel.toastAction("Removed " + plabel, "Undo", function() { papers.restorePaper(pid) })
                        } }
                    }
                }
                HoverHandler { id: h }
                TapHandler { enabled: kind === "paper"; onTapped: { const pages = library.pages(paper.sectionId); if (pages.length) { papers.setActive(id, 0); panel.openPage(pages[0].id) } } }
            }
        }
        Text { visible: rows.count === 0; text: "Import a question paper and its mark scheme as a pair. Marks, topics and time per question feed the dashboard."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1; wrapMode: Text.Wrap; Layout.fillWidth: true }
    }
}
