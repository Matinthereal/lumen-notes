import QtQuick
import QtQuick.Controls.Basic
import Lumen

// Typed Markdown blocks positioned in page units over the canvas. Tap a block to edit the raw
// Markdown (with $…$ / $$…$$ LaTeX); tap outside to render. Drag the top grip to move, the
// right grip to resize. Blocks count as chrome, so the pen edits rather than inks on them.
Item {
    id: textPage
    required property InkCanvas canvas
    property var pageId: 0
    property var recordingId: 0
    property var recordingT: function() { return 0 }   // ms into the current recording, 0 when idle

    // Pen, finger, trackpad and mouse: named once so no handler here can quietly leave one out.
    readonly property int allDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus

    SystemPalette { id: pal }
    ListModel { id: blocks }

    function reload() {
        blocks.clear()
        if (!pageId) return
        for (const b of textBlocks.list(pageId)) blocks.append({ bid: b.id, bx: b.x, by: b.y, bw: b.w, markdown: b.markdown })
    }
    function addAt(page) {
        const id = textBlocks.create(pageId, page.x, page.y, 380, recordingId, recordingT())
        reload()
        Qt.callLater(() => { for (let i = 0; i < repeater.count; ++i) { const it = repeater.itemAt(i); if (it && it.bid === id) it.startEditing() } })
    }
    Connections { target: textBlocks; function onChanged(pid) { if (pid === textPage.pageId) textPage.reload() } }
    signal pageLinkActivated(string url)
    // A rename changes what a link says; the rendered blocks pick that up here.
    property int linkTick: 0
    Connections { target: library; function onChanged() { textPage.linkTick++ } }

    // [[ in a block's Markdown opens the page picker; the pick is written as [Title](lumen://page/id).
    property Item linkEditor: null
    property int linkStart: -1
    function checkLink(editor) {
        const pos = editor.cursorPosition
        const line = editor.getText(Math.max(0, pos - 80), pos).split("\n").pop()
        const open = line.lastIndexOf("[[")
        if (!editor.activeFocus || open < 0 || line.indexOf("]]", open) >= 0) { if (linkEditor === editor) linkPicker.close(); return }
        linkEditor = editor
        linkStart = pos - (line.length - open)
        linkPicker.query = line.slice(open + 2)
        const r = editor.positionToRectangle(linkStart)
        linkPicker.openAt(editor.mapToItem(linkPicker.parent, r.x, r.y), r.height * canvas.zoom)
    }
    function insertLink(pageId) {
        const editor = linkEditor
        linkPicker.close()
        if (!editor) return
        const label = library.displayTitle(pageId).replace(/([\\\[\]*_`])/g, "\\$1")
        const md = "[" + label + "](lumen://page/" + pageId + ")"
        editor.remove(linkStart, editor.cursorPosition)
        editor.insert(linkStart, md)
        editor.cursorPosition = linkStart + md.length
        editor.forceActiveFocus()
    }
    LinkPicker {
        id: linkPicker
        parent: Overlay.overlay
        excludePageId: textPage.pageId
        onChosen: (id, title) => textPage.insertLink(id)
        onCreateAsked: (title) => {
            const home = library.page(textPage.pageId)
            if (!home.id) return
            const id = library.createPage(home.sectionId, "", "typed")
            library.rename("page", id, title)
            textPage.insertLink(id)
        }
    }
    onPageIdChanged: reload()

    // Markdown → rendered: $$…$$ and $…$ become inline images from the latex worker.
    function toRich(md) {
        const colour = pal.text.toString()
        const enc = (tex) => "![tex](image://latex/" + encodeURIComponent(tex) + "?c=" + encodeURIComponent(colour) + ")"
        let out = md.replace(/\$\$([\s\S]+?)\$\$/g, (m, t) => "\n\n" + enc(t.trim()) + "\n\n")
        out = out.replace(/\$([^$\n]+?)\$/g, (m, t) => enc(t.trim()))
        return out
    }

    Item {
        id: pageSpace
        transform: [ Scale { xScale: canvas.zoom; yScale: canvas.zoom }, Translate { x: canvas.pan.x; y: canvas.pan.y } ]

        Repeater {
            id: repeater
            model: blocks
            delegate: Item {
                id: block
                objectName: "chrome"
                required property var bid
                required property real bx
                required property real by
                required property real bw
                required property string markdown
                property bool editing: false
                // Offsets, never the bound properties themselves — see ImageLayer for why.
                property real dx: 0
                property real dy: 0
                property real dw: 0
                function resetDrag() { dx = 0; dy = 0; dw = 0 }
                x: bx + dx; y: by + dy; width: Math.max(120, bw + dw)
                height: editing ? editor.implicitHeight + 34 : Math.max(view.implicitHeight + 16, 40)

                function startEditing() { editing = true; editor.text = markdown; editor.forceActiveFocus(); editor.cursorPosition = editor.length }
                function commit() {
                    if (!editing) return
                    editing = false
                    autosave.stop()
                    // Read everything off the delegate first: remove() rebuilds the model, which
                    // releases this delegate and invalidates its context mid-function.
                    const id = bid, text = library.resolveLinks(editor.text), was = markdown
                    if (text.trim().length === 0) { textBlocks.remove(id); return }
                    if (text !== was) { textBlocks.setMarkdown(id, text, textPage.recordingT()); markdown = text }
                }
                // Typing is not committed until you tap away, so a crash mid-page used to lose the
                // lot. setMarkdown is a quiet UPDATE (it emits nothing), so this cannot disturb the
                // editor you are typing into. Rnote autosaves at 120 s, Xournal++ at 180 s.
                Timer {
                    id: autosave
                    interval: 200
                    onTriggered: if (block.editing && editor.text !== block.markdown) {
                        textBlocks.setMarkdown(block.bid, editor.text, textPage.recordingT())
                        block.markdown = editor.text
                    }
                }
                // Moving or resizing rebuilds the model, so flush the text before it does.
                function commitGeometry() {
                    const id = bid, nx = x, ny = y, nw = width
                    if (editing && editor.text !== markdown) textBlocks.setMarkdown(id, editor.text, textPage.recordingT())
                    resetDrag()
                    textBlocks.setGeometry(id, nx, ny, nw)
                }

                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: editing ? Qt.alpha(pal.base, 0.96) : (hover.hovered ? Qt.alpha(pal.highlight, 0.06) : "transparent")
                    border.width: editing || hover.hovered ? 1 : 0
                    border.color: editing ? pal.highlight : Qt.alpha(pal.text, 0.25)
                }
                // move grip (top) and resize grip (right edge)
                Rectangle {
                    id: grip
                    // dimmed, never hidden: a touchscreen has no hover to reveal it with
                    opacity: editing || hover.hovered ? 1 : 0.4
                    width: 44; height: 6; radius: 3; x: (parent.width - width) / 2; y: 4
                    color: Qt.alpha(pal.text, 0.35)
                    DragHandler {
                        acceptedDevices: textPage.allDevices
                        margin: Ui.target / 2          // the paint stays slim; the grab area is finger-sized
                        target: null
                        onActiveChanged: if (!active) block.commitGeometry()
                        onCanceled: block.resetDrag()
                        onTranslationChanged: { block.dx = translation.x / canvas.zoom; block.dy = translation.y / canvas.zoom }
                    }
                }
                Rectangle {
                    opacity: editing || hover.hovered ? 1 : 0.4
                    width: 6; height: 44; radius: 3; x: parent.width - 8; y: (parent.height - height) / 2
                    color: Qt.alpha(pal.text, 0.35)
                    DragHandler {
                        acceptedDevices: textPage.allDevices
                        margin: Ui.target / 2
                        target: null
                        xAxis.enabled: true; yAxis.enabled: false
                        onActiveChanged: if (!active) block.commitGeometry()
                        onCanceled: block.resetDrag()
                        onTranslationChanged: block.dw = translation.x / canvas.zoom
                    }
                }

                Text {
                    id: view
                    visible: !editing
                    x: 8; y: 12; width: parent.width - 16
                    textFormat: Text.MarkdownText
                    text: markdown.length ? textPage.toRich(textPage.linkTick >= 0 ? library.resolveLinks(markdown) : markdown) : "*Empty block — tap to type*"
                    color: pal.text
                    wrapMode: Text.Wrap
                    font.pixelSize: 15
                    linkColor: pal.highlight
                    onLinkActivated: (link) => link.startsWith("lumen://") ? textPage.pageLinkActivated(link) : Qt.openUrlExternally(link)
                }
                TextArea {
                    id: editor
                    visible: editing
                    x: 8; y: 12; width: parent.width - 16
                    wrapMode: TextEdit.Wrap
                    font.family: "monospace"
                    font.pixelSize: 14
                    color: pal.text
                    placeholderText: "Markdown, with $x^2$ or $$\\int_0^1 x\\,dx$$ for maths"
                    background: null
                    onActiveFocusChanged: if (!activeFocus) { if (textPage.linkEditor === editor) linkPicker.close(); block.commit() }
                    onTextChanged: if (block.editing) { autosave.restart(); Qt.callLater(textPage.checkLink, editor) }
                    Keys.onPressed: (e) => {
                        const picking = linkPicker.visible && textPage.linkEditor === editor
                        if (e.key === Qt.Key_Escape) { if (picking) linkPicker.close(); else block.commit(); e.accepted = true }
                        else if (!picking) return
                        else if (e.key === Qt.Key_Down || e.key === Qt.Key_Up) { linkPicker.move(e.key === Qt.Key_Down ? 1 : -1); e.accepted = true }
                        else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Tab) { linkPicker.acceptCurrent(); e.accepted = true }
                    }
                }
                HoverHandler { id: hover }
                TapHandler { enabled: !block.editing; onTapped: block.startEditing() }
            }
        }
    }
}
