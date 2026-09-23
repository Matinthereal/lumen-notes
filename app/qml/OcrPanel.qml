import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Recognised handwriting for the current page: tap a line to flash its ink, edit to correct
// (corrections stick and re-index search). Runs in the background on its own; "Read now" forces it.
Rectangle {
    id: panel
    objectName: "chrome"
    required property var canvas
    property var pageId: 0
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    SystemPalette { id: pal }
    color: pal.window
    property var lineList: []
    function reload() { lineList = pageId ? ocr.results(pageId) : [] }
    signal insertText(string text, real x, real y)
    // Samsung's shape: recognised handwriting is offered, never substituted — the ink stays put.
    function insertAll() {
        if (!lineList.length) return
        let lines = [], left = 1e9, bottom = 0
        for (const l of lineList) { lines.push(l.text); left = Math.min(left, l.x); bottom = Math.max(bottom, l.y + l.h) }
        panel.insertText(lines.join("\n"), left, bottom + 24)
        panel.toast("Handwriting inserted as a text block")
    }
    function insertOne(text, x, y, h) { panel.insertText(text, x, y + h + 12); panel.toast("Line inserted as text") }

    // Apple's Math Notes: write the sum, ask for the answer. Earlier lines on the page that define
    // a value ("v = 3") are passed as context, so later lines can use them.
    property var pendingSolve: null
    function solveLine(text, x, y, h) {
        const context = []
        for (const l of lineList) if (l.text !== text && l.text.indexOf("=") > 0) context.push(l.text)
        pendingSolve = { x: x, y: y + h + 10 }
        maths.solve(text, context)
        panel.toast("Working it out…")
    }
    Connections {
        target: maths
        function onSolved(input, result, latexText, kind) {
            if (!panel.pendingSolve) return
            const at = panel.pendingSolve
            panel.pendingSolve = null
            panel.insertText(kind === "value" ? "= " + result : result, at.x, at.y)
            panel.toast(result)
        }
        function onFailed(reason) { if (panel.pendingSolve) { panel.pendingSolve = null; panel.toast(reason) } }
    }
    onPageIdChanged: reload()
    Connections { target: ocr; function onPageRecognized(pid, n) { if (pid === panel.pageId) panel.reload() } function onFailed(m) { panel.toast(m) } }
    ColumnLayout {
        anchors { fill: parent; margins: 10 }
        spacing: 6
        RowLayout {
            Text { text: "Handwriting"; color: pal.windowText; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Button { text: "Read now"; font.pixelSize: Ui.small; enabled: ocr.ready || ocr.status.length === 0; onClicked: ocr.recognizeNow(panel.pageId) }
            Button {
                text: "Insert as text"; font.pixelSize: Ui.small
                enabled: panel.lineList.length > 0
                ToolTip.visible: hovered; ToolTip.delay: 600
                ToolTip.text: "Put the whole page's handwriting on the page as a typed block — your ink is untouched"
                onClicked: panel.insertAll()
            }
        }
        ProgressChip {
            visible: ocr.busy
            Layout.fillWidth: true
            text: ocr.status.length ? ocr.status : "Reading handwriting…"
            progress: ocr.progress
            cancelTip: "Stop reading handwriting"
            onCancelRequested: { ocr.cancel(); panel.toast("Stopped reading handwriting") }
        }
        Text { visible: !ocr.busy && ocr.status.length > 0; text: ocr.status; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.Wrap; Layout.fillWidth: true }
        Text { visible: panel.lineList.length === 0 && ocr.status.length === 0; text: "Nothing recognised yet. Handwriting is read ten seconds after you stop writing; corrections you make here are kept."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1; wrapMode: Text.Wrap; Layout.fillWidth: true }
        ListView {
            Layout.fillWidth: true; Layout.fillHeight: true
            model: panel.lineList; clip: true; spacing: 4
            delegate: Rectangle {
                required property var modelData        // {id, x, y, w, h, text, confidence, corrected} — x/y are final on Item, so no required-property aliases
                readonly property rect box: Qt.rect(modelData.x, modelData.y, modelData.w, modelData.h)
                width: ListView.view.width; height: field.implicitHeight + 10; radius: 6
                color: Qt.alpha(pal.text, 0.04)
                border.width: 1; border.color: modelData.corrected ? pal.highlight : (modelData.confidence < 0.6 ? Qt.alpha(Ui.danger, 0.5) : "transparent")
                RowLayout {
                    anchors { fill: parent; margins: 5 }
                    Rectangle { implicitWidth: 6; implicitHeight: 6; radius: 3; color: modelData.corrected ? pal.highlight : (modelData.confidence >= 0.8 ? Ui.good : modelData.confidence >= 0.6 ? Ui.warning : Ui.danger) }
                    TextField {
                        id: field
                        Layout.fillWidth: true
                        // Driven, not bound: the binding dies on the first correction typed here.
                        Component.onCompleted: text = modelData.text
                        Connections { target: ocr; function onPageRecognized(pid) { if (pid === panel.pageId) field.text = modelData.text } }
                        font.pixelSize: Ui.text
                        background: null
                        onEditingFinished: if (text !== modelData.text) { ocr.correct(modelData.id, text); panel.toast("Corrected") }
                        onActiveFocusChanged: if (activeFocus) canvas.flashRect(box)
                    }
                }
                RowLayout {
                    anchors { right: parent.right; verticalCenter: parent.verticalCenter; rightMargin: 6 }
                    Rectangle {
                        implicitWidth: Ui.target - 12; implicitHeight: Ui.target - 12; radius: 6
                        color: insTap.pressed ? Qt.alpha(pal.highlight, 0.4) : (insHover.hovered ? Qt.alpha(pal.highlight, 0.2) : "transparent")
                        Text { anchors.centerIn: parent; text: "↧"; color: pal.windowText; font.pixelSize: Ui.text }
                        HoverHandler { id: insHover }
                        ToolTip.visible: insHover.hovered; ToolTip.delay: 600; ToolTip.text: "Insert this line as typed text below it"
                        TapHandler { id: insTap; gesturePolicy: TapHandler.ReleaseWithinBounds
                                     onTapped: panel.insertOne(field.text, modelData.x, modelData.y, modelData.h) }
                    }
                    Rectangle {
                        visible: /[0-9]/.test(field.text) && /[-+*/=^]/.test(field.text)
                        implicitWidth: Ui.target - 12; implicitHeight: Ui.target - 12; radius: 6
                        color: solveTap.pressed ? Qt.alpha(pal.highlight, 0.4) : (solveHover.hovered ? Qt.alpha(pal.highlight, 0.2) : "transparent")
                        Text { anchors.centerIn: parent; text: "="; color: pal.windowText; font.pixelSize: Ui.text + 1; font.weight: Font.DemiBold }
                        HoverHandler { id: solveHover }
                        ToolTip.visible: solveHover.hovered; ToolTip.delay: 600; ToolTip.text: "Work this line out and write the answer under it"
                        TapHandler { id: solveTap; gesturePolicy: TapHandler.ReleaseWithinBounds
                                     onTapped: panel.solveLine(field.text, modelData.x, modelData.y, modelData.h) }
                    }
                }
                TapHandler { onTapped: { canvas.flashRect(box); field.forceActiveFocus() } }
            }
        }
    }
}
