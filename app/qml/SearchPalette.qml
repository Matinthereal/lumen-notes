import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Ctrl+K: one query across typed text, OCR'd ink, transcripts and PDF text (FTS5). A tag chip, or
// "#tag" at the start of the query, narrows it to pages with that tag.
Popup {
    id: pal_
    objectName: "chrome"
    signal openResult(var pageId, string kind, var refId)
    SystemPalette { id: pal }
    modal: false
    focus: true
    width: 620
    height: Math.min(560, 70 + (allTags.length ? tagFlow.implicitHeight + 6 : 0) + Math.max(list.contentHeight, empty.visible ? empty.implicitHeight + 16 : 0) + 12)
    x: (parent.width - width) / 2
    y: 80
    padding: 0
    background: Rectangle { radius: 12; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1 }
    onOpened: { field.text = ""; filter = ""; tag = ""; allTags = library.tags(); field.forceActiveFocus(); run("") }

    ListModel { id: results }
    property string filter: ""          // "" = everything; else one of text/ocr/transcript/pdf
    property int hits: 0
    property string tag: ""             // "" = every page
    property var allTags: []
    function run(q) {
        results.clear()
        const hash = q.match(/^\s*#([^\s#]+)\s*(.*)$/)
        const useTag = hash ? hash[1] : tag
        if (hash) q = hash[2]
        if (q.trim().length < 2 && useTag.length) {
            for (const p of library.pagesWithTag(useTag))
                results.append({ kind: "tag", pageId: p.id, refId: 0, snippet: (p.title || "").replace("\u200b", "") || "untitled page", score: 0,
                                 pageTitle: (p.title || "").replace("\u200b", ""), sectionName: p.sectionName, notebookName: p.notebookName })
            list.currentIndex = results.count ? 0 : -1
            hits = results.count
            return
        }
        if (q.trim().length < 2) {
            for (const r of library.starredPages(4)) results.append({ kind: "starred", pageId: r.id, refId: 0, snippet: (r.title || "").replace("\u200b", "") || "untitled page", score: 0, pageTitle: (r.title || "").replace("\u200b", ""), sectionName: r.sectionName, notebookName: r.notebookName })
            for (const r of library.recentPages(8)) results.append({ kind: "recent", pageId: r.id, refId: 0, snippet: r.title.replace("\u200b", "") || "untitled page", score: 0, pageTitle: r.title.replace("\u200b", ""), sectionName: r.sectionName, notebookName: r.notebookName })
            list.currentIndex = results.count ? 0 : -1
            hits = 0
            return
        }
        const all = library.search(q, 60, useTag)
        hits = all.length
        for (const r of all) if (!filter.length || r.kind === filter) results.append(r)
        list.currentIndex = results.count ? 0 : -1
    }
    function openCurrent() { if (list.currentIndex >= 0) { const r = results.get(list.currentIndex); pal_.openResult(r.pageId, r.kind, r.refId); pal_.close() } }
    function kindLabel(k) { return k === "text" ? "typed" : k === "ocr" ? "handwriting" : k === "transcript" ? "audio" : k === "pdf" ? "PDF" : k === "recent" ? "recent" : k === "starred" ? "starred" : k === "tag" ? "tagged" : k }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        TextField {
            id: field
            Layout.fillWidth: true
            Layout.margins: 10
            placeholderText: "Search notes, handwriting, transcripts and PDFs…"
            font.pixelSize: 16
            background: Rectangle { radius: 8; color: pal.base; border.color: Qt.alpha(pal.text, 0.18) }
            leftPadding: 12; topPadding: 10; bottomPadding: 10
            onTextChanged: pal_.run(text)
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onUpPressed: list.decrementCurrentIndex()
            Keys.onEnterPressed: pal_.openCurrent()
            Keys.onReturnPressed: if (list.currentIndex >= 0) { const r = results.get(list.currentIndex); pal_.openResult(r.pageId, r.kind, r.refId); pal_.close() }
            Keys.onEscapePressed: pal_.close()
        }
        Flow {
            id: tagFlow
            objectName: "searchTags"
            Layout.fillWidth: true
            Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 6
            spacing: 6
            visible: pal_.allTags.length > 0
            Repeater {
                model: pal_.allTags
                delegate: TagChip {
                    required property var modelData
                    name: modelData.name
                    selected: pal_.tag.toLowerCase() === modelData.name.toLowerCase()
                    onClicked: { pal_.tag = selected ? "" : modelData.name; pal_.run(field.text); field.forceActiveFocus() }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 6
            spacing: 6
            visible: field.text.trim().length >= 2
            Repeater {
                model: [["Everything", ""], ["Typed", "text"], ["Handwriting", "ocr"], ["Audio", "transcript"], ["PDF", "pdf"]]
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: chipText.implicitWidth + 20; implicitHeight: Ui.target - 14; radius: height / 2
                    readonly property bool on: pal_.filter === modelData[1]
                    color: on ? Qt.alpha(pal.highlight, 0.35) : (chipTap.pressed ? Qt.alpha(pal.text, 0.18) : Qt.alpha(pal.text, 0.07))
                    Text { id: chipText; anchors.centerIn: parent; text: modelData[0]; color: pal.windowText; font.pixelSize: Ui.small }
                    TapHandler { id: chipTap; gesturePolicy: TapHandler.ReleaseWithinBounds
                                 onTapped: { pal_.filter = modelData[1]; pal_.run(field.text) } }
                }
            }
            Item { Layout.fillWidth: true }
            Text { visible: pal_.hits > results.count; text: pal_.hits + " in all"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 6
            model: results
            clip: true
            delegate: Rectangle {
                required property int index
                required property var pageId
                required property string kind
                required property var refId
                required property string snippet
                required property string pageTitle
                required property string sectionName
                required property string notebookName
                width: list.width; height: Math.max(Ui.row + 12, 54); radius: 8
                color: ListView.isCurrentItem ? Qt.alpha(pal.highlight, 0.18) : (h.hovered ? Qt.alpha(pal.text, 0.05) : "transparent")
                RowLayout {
                    anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                    spacing: 12
                    Rectangle {
                        implicitWidth: 74; implicitHeight: 22; radius: 11; color: Qt.alpha(pal.highlight, 0.15)
                        Text { anchors.centerIn: parent; text: pal_.kindLabel(kind); color: pal.text; font.pixelSize: Ui.small; font.letterSpacing: 0.4 }
                    }
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Text { text: notebookName + " › " + sectionName + (pageTitle.length ? " › " + pageTitle : ""); color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: snippet.replace(/\n/g, " "); color: pal.text; font.pixelSize: Ui.text; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                }
                HoverHandler { id: h }
                TapHandler { onTapped: { pal_.openResult(pageId, kind, refId); pal_.close() } }
            }
        }
        Text {
            Layout.margins: 10
            visible: results.count === 0 && field.text.trim().length < 2 && pal_.tag.length === 0
            text: "Type at least two letters. Everything is searched: typed notes, handwriting once it is read, transcripts and PDF text."
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
        Text {
            id: empty
            Layout.margins: 10
            visible: results.count === 0 && (field.text.trim().length >= 2 || pal_.tag.length > 0)
            text: pal_.tag.length && field.text.trim().length < 2 ? "No pages have that tag yet." : "Nothing yet — typed text is indexed as you write; handwriting after OCR runs; PDFs on import."
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small + 1; wrapMode: Text.Wrap; Layout.fillWidth: true
        }
    }
}
