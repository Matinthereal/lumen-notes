import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Claude panel (D-010): every feature shows the exact prompt first; Send runs it on the maker's plan.
Rectangle {
    id: ai
    objectName: "chrome"
    required property var canvas
    property var pageId: 0
    property var recordingId: 0
    property var sectionId: 0
    signal openPage(var pageId)
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)

    SystemPalette { id: pal }
    color: pal.window

    property string pendingFeature: ""
    property string pendingPrompt: ""
    property var pendingMeta: ({})
    property string response: ""
    property string responseFeature: ""
    ListModel { id: askHistory }
    ListModel { id: proposedCards }
    ListModel { id: recModel }
    property int logTick: 0
    Connections { target: claude; function onResponded() { ai.logTick++ } function onFailed() { ai.logTick++ } }
    function refreshRecordings() {
        const keep = recPick.currentIndex >= 0 && recPick.currentIndex < recModel.count ? recModel.get(recPick.currentIndex).id : 0
        recModel.clear()
        for (const r of audio.recordings(sectionId)) recModel.append(r)
        for (let i = 0; i < recModel.count; ++i) if (recModel.get(i).id === keep) { recPick.currentIndex = i; return }
        recPick.currentIndex = recModel.count > 0 ? 0 : -1
    }
    onSectionIdChanged: refreshRecordings()
    Connections { target: audio; function onRecordingsChanged() { ai.refreshRecordings() } }
    Connections {
        target: claude
        function onPromptReady(feature, prompt, meta) { ai.pendingFeature = feature; ai.pendingPrompt = prompt; ai.pendingMeta = meta; ai.response = "" }
        function onResponded(feature, text, meta) {
            ai.responseFeature = feature; ai.response = text
            if (feature === "ask") askHistory.append({ q: meta.question, a: text })
            ai.pendingFeature = ""
        }
        function onNotesCreated(pid) { ai.toast("AI notes page created"); ai.openPage(pid) }
        function onCardsProposed(cards, meta) { proposedCards.clear(); for (const c of cards) proposedCards.append({ keep: true, kind: c.kind || "basic", front: c.front || "", back: c.back || "", tag: (c.tags && c.tags.length) ? c.tags[0] : "" }); tabs.currentIndex = 1 }
        function onFailed(feature, message) { ai.toast(message); ai.pendingFeature = "" }
    }
    function linkify(md) { return md.replace(/\[page (\d+)\]/g, (m, n) => "[page " + n + "](page:" + n + ")") }

    ColumnLayout {
        anchors { fill: parent; margins: 10 }
        spacing: 8
        RowLayout {
            Text { text: "Claude"; color: pal.windowText; font.pixelSize: 15; font.weight: Font.DemiBold }
            Text { text: claude.available ? claude.version : (claude.reason || "checking…"); color: claude.available ? Qt.alpha(pal.windowText, Ui.mutedAlpha) : Ui.danger; font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.fillWidth: true }
            Switch { id: onlineSwitch; text: "Online"; font.pixelSize: Ui.small; checked: claude.online; onToggled: claude.online = checked
                     Connections { target: claude; function onStatusChanged() { onlineSwitch.checked = claude.online } }
                     ToolTip.visible: hovered; ToolTip.delay: 600; ToolTip.text: "Allow web search and fetch (e.g. to pull an exam spec). Off = only your notes are sent." }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "Notes"; font.pixelSize: Ui.small + 1 }
            TabButton { text: "Cards"; font.pixelSize: Ui.small + 1 }
            TabButton { text: "Explain"; font.pixelSize: Ui.small + 1 }
            TabButton { text: "Ask"; font.pixelSize: Ui.small + 1 }
            TabButton { text: "Log"; font.pixelSize: Ui.small + 1 }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            currentIndex: tabs.currentIndex
            // Notes
            ColumnLayout {
                Text { text: "Turn a recording into structured notes on a new page in this section."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1; wrapMode: Text.Wrap; Layout.fillWidth: true }
                ComboBox { id: recPick; Layout.fillWidth: true; model: recModel; textRole: "title"; font.pixelSize: Ui.small + 1 }
                Button { text: "Draft notes"; enabled: recModel.count > 0 && recPick.currentIndex >= 0 && !claude.busy; onClicked: claude.prepareLectureNotes(recModel.get(recPick.currentIndex).id, ai.pageId) }
            }
            // Cards
            ColumnLayout {
                Button { text: "Generate cards from this page"; enabled: !claude.busy; onClicked: claude.prepareFlashcards(ai.pageId, ai.recordingId) }
                Text { visible: proposedCards.count === 0; text: "Proposed cards appear here for you to tick, edit and add."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Button {
                    visible: proposedCards.count > 0
                    text: { let n = 0; for (let i = 0; i < proposedCards.count; ++i) if (proposedCards.get(i).keep) ++n; return "Add " + n + " cards" }
                    onClicked: { const list = []; for (let i = 0; i < proposedCards.count; ++i) { const c = proposedCards.get(i); if (c.keep) list.push({ kind: c.kind, front: c.front, back: c.back, tags: c.tag ? [c.tag] : [] }) }
                                 const n = cards.addMany(list, ai.pageId); ai.toast(n + " cards added"); proposedCards.clear() }
                }
            }
            // Explain
            ColumnLayout {
                Text { text: !canvas ? "" : canvas.hasTextSelection ? "Explain the selected PDF text." : (canvas.hasSelection ? "Explain the lasso'd ink — a picture of it will be sent to Claude." : "Select text (tool 5) or lasso some ink (tool 4) first."); color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Button { text: "Explain selection"; enabled: !!canvas && (canvas.hasTextSelection || canvas.hasSelection) && !claude.busy
                         onClicked: { if (canvas.hasTextSelection) claude.prepareExplain(canvas.selectedText(), ai.pageId, ""); else claude.prepareExplain("", ai.pageId, canvas.renderSelectionToPng()) } }
            }
            // Ask
            ColumnLayout {
                TextField { id: askField; Layout.fillWidth: true; placeholderText: "Ask your notes…"; font.pixelSize: Ui.text; onAccepted: if (text.trim().length) claude.prepareAsk(text.trim()) }
                Button { text: "Ask"; enabled: askField.text.trim().length > 0 && !claude.busy; onClicked: claude.prepareAsk(askField.text.trim()) }
            }
            // Log
            ListView {
                clip: true
                model: ai.logTick >= 0 ? claude.recentLog(20) : []
                delegate: Text { required property var modelData; width: ListView.view.width; text: new Date(modelData.at * 1000).toLocaleString(Qt.locale(), "d MMM HH:mm") + "  " + modelData.feature + "  " + (modelData.ok ? "ok" : "failed") + "  " + modelData.promptChars + "→" + modelData.responseChars + " chars"; color: pal.text; font.pixelSize: Ui.small; font.family: "monospace"; elide: Text.ElideRight }
            }
        }

        // Proposed cards editor
        ListView {
            visible: tabs.currentIndex === 1 && proposedCards.count > 0
            Layout.fillWidth: true; Layout.fillHeight: true
            model: proposedCards
            clip: true; spacing: 6
            delegate: Rectangle {
                required property int index
                required property bool keep
                required property string kind
                required property string front
                required property string back
                required property string tag
                width: ListView.view.width; height: col.implicitHeight + 12; radius: 6
                color: Qt.alpha(pal.text, 0.04)
                ColumnLayout {
                    id: col
                    anchors { fill: parent; margins: 6 }
                    RowLayout { CheckBox { id: keepBox; checked: keep; onToggled: if (index < proposedCards.count) proposedCards.setProperty(index, "keep", checked)
                                           Connections { target: proposedCards; function onDataChanged() { keepBox.checked = keep } } } Text { text: kind + (tag.length ? " · " + tag : ""); color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small } }
                    TextArea {
                        id: frontEdit
                        Layout.fillWidth: true; text: front; font.pixelSize: Ui.small + 1; wrapMode: TextEdit.Wrap
                        background: Rectangle { color: pal.base; radius: 4 }
                        onEditingFinished: if (index < proposedCards.count) proposedCards.setProperty(index, "front", text)
                        // Typing destroys the `text: front` binding, so put it back when the model
                        // changes underneath — the CheckBox beside it already did exactly this.
                        Connections { target: proposedCards; function onDataChanged() { if (!frontEdit.activeFocus) frontEdit.text = front } }
                    }
                    TextArea {
                        id: backEdit
                        visible: kind !== "cloze"; Layout.fillWidth: true; text: back; font.pixelSize: Ui.small + 1; wrapMode: TextEdit.Wrap
                        background: Rectangle { color: pal.base; radius: 4 }
                        onEditingFinished: if (index < proposedCards.count) proposedCards.setProperty(index, "back", text)
                        Connections { target: proposedCards; function onDataChanged() { if (!backEdit.activeFocus) backEdit.text = back } }
                    }
                }
            }
        }

        // Prompt preview → Send (never sent silently)
        Rectangle {
            visible: ai.pendingFeature.length > 0
            Layout.fillWidth: true; Layout.preferredHeight: 190
            radius: 6; color: pal.base; border.color: pal.highlight; border.width: 1
            ColumnLayout {
                anchors { fill: parent; margins: 8 }
                spacing: 6
                RowLayout {
                    Text { text: "This will be sent to Claude (" + ai.pendingPrompt.length + " chars" + (claude.online ? ", web on" : "") + "):"; color: pal.text; font.pixelSize: Ui.small + 1; Layout.fillWidth: true }
                    Button { text: claude.busy ? "Working…" : "Send"; enabled: !claude.busy; onClicked: claude.send(ai.pendingFeature, ai.pendingPrompt, ai.pendingMeta) }
                    Button { text: "Cancel"; enabled: !claude.busy; onClicked: ai.pendingFeature = "" }
                }
                ScrollView { Layout.fillWidth: true; Layout.fillHeight: true; TextArea { readOnly: true; text: ai.pendingPrompt; font.pixelSize: Ui.small; font.family: "monospace"; wrapMode: TextEdit.Wrap; background: null } }
            }
        }
        // Response
        ScrollView {
            visible: ai.response.length > 0 || askHistory.count > 0
            Layout.fillWidth: true; Layout.fillHeight: true
            ColumnLayout {
                width: parent.width
                Repeater {
                    model: tabs.currentIndex === 3 ? askHistory : 0
                    delegate: ColumnLayout {
                        required property string q
                        required property string a
                        Layout.fillWidth: true
                        Text { text: "**You:** " + q; textFormat: Text.MarkdownText; color: pal.text; font.pixelSize: Ui.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
                        Text { text: ai.linkify(a); textFormat: Text.MarkdownText; color: pal.text; font.pixelSize: Ui.text; wrapMode: Text.Wrap; Layout.fillWidth: true
                               onLinkActivated: (link) => { if (link.startsWith("page:")) ai.openPage(Number(link.substring(5))); else Qt.openUrlExternally(link) } }
                    }
                }
                Text { visible: tabs.currentIndex !== 3 && ai.response.length > 0; text: ai.response; textFormat: Text.MarkdownText; color: pal.text; font.pixelSize: Ui.text; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
        }
    }
}
