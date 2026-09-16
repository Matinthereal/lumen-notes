import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Lasso → LaTeX: recognised locally (pix2tex), previewed live, inserted as a text block next to
// the ink, or improved by Claude (the image is sent only when you press that button).
Popup {
    id: pop
    objectName: "chrome"
    property string latex: ""
    property string png: ""
    property rect anchorRect: Qt.rect(0, 0, 0, 0)
    signal insert(string latex, rect where)
    signal insertAnswer(string text, rect where)
    signal improve(string png, string draft)
    SystemPalette { id: pal }
    width: 420
    padding: 12
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: 10; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1 }
    onLatexChanged: { field.text = latex; answer = ""; answerLatex = ""; solveError = "" }
    property string answer: ""
    property string answerLatex: ""
    property string solveError: ""
    property bool awaitingAnswer: false
    onClosed: { awaitingAnswer = false; answer = ""; solveError = "" }
    Connections {
        target: maths
        function onSolved(input, result, latexText, kind) {
            if (!pop.visible || !pop.awaitingAnswer) return      // the handwriting panel asks too
            pop.awaitingAnswer = false
            pop.answer = result; pop.answerLatex = latexText; pop.solveError = ""
        }
        function onFailed(reason) { if (pop.visible && pop.awaitingAnswer) { pop.awaitingAnswer = false; pop.answer = ""; pop.solveError = reason } }
    }
    Connections { target: claude; function onResponded(feature, text, meta) { if (feature === "latex-improve" && pop.visible) field.text = text.replace(/^\$+|\$+$/g, "").trim() } }
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        Text { text: "Handwritten maths → LaTeX"; color: pal.windowText; font.pixelSize: 13; font.weight: Font.DemiBold }
        TextField { id: field; Layout.fillWidth: true; font.family: "monospace"; font.pixelSize: 13; text: pop.latex }
        Image {
            Layout.fillWidth: true; Layout.preferredHeight: Math.min(120, implicitHeight)
            fillMode: Image.PreserveAspectFit; horizontalAlignment: Image.AlignLeft
            source: field.text.trim().length ? "image://latex/" + encodeURIComponent(field.text.trim()) + "?c=" + encodeURIComponent(pal.text.toString()) : ""
            asynchronous: true
        }
        RowLayout {
            visible: pop.answer.length > 0 || pop.solveError.length > 0
            Text { text: pop.answer.length ? "=" : "·"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 15 }
            Text {
                text: pop.answer.length ? pop.answer : pop.solveError
                color: pop.answer.length ? pal.windowText : Ui.warning
                font.pixelSize: 15; font.weight: pop.answer.length ? Font.DemiBold : Font.Normal
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            Button {
                visible: pop.answer.length > 0
                text: "Write the answer"
                onClicked: { pop.insertAnswer(pop.answer, pop.anchorRect); pop.close() }
            }
        }
        RowLayout {
            Button { text: "Work it out"; enabled: field.text.trim().length > 0 && !maths.busy
                     onClicked: { pop.awaitingAnswer = true; maths.solve(field.text.trim(), []) }
                     ToolTip.visible: hovered; ToolTip.delay: 600; ToolTip.text: "Evaluate it, or solve it for its unknown — offline, in sympy" }
            Button { text: "Insert"; enabled: field.text.trim().length > 0; onClicked: { pop.insert(field.text.trim(), pop.anchorRect); pop.close() } }
            Button { text: claude.busy ? "Asking Claude…" : "Improve with Claude"; enabled: claude.available && !claude.busy; onClicked: pop.improve(pop.png, field.text)
                     ToolTip.visible: hovered; ToolTip.delay: 600; ToolTip.text: "Sends the picture of the selection to Claude (Read tool) — only when you press this" }
            Item { Layout.fillWidth: true }
            Button { text: "Cancel"; onClicked: pop.close() }
        }
    }
}
