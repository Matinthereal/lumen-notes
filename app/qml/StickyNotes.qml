import QtQuick
import QtQuick.Controls.Basic
import Lumen

// Sticky notes: typed words on a handwritten page. Each is a coloured card in page units over
// the canvas — tap it to type, drag it to move, pull its corner to resize. While one is open a
// bar under it, outside the zoom so it stays fingertip-sized, changes its colour or deletes it.
// Notes count as chrome, so the pen moves and edits them rather than inking on them.
Item {
    id: notes
    required property InkCanvas canvas
    property var pageId: 0
    property var recordingId: 0
    property var recordingT: function() { return 0 }   // ms into the current recording, 0 when idle
    property var selectedId: 0
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    signal pageLinkActivated(string url)

    // Pen, finger, trackpad and mouse: named once so no handler here can quietly leave one out.
    readonly property int allDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
    readonly property var colours: ["#FFE9A8", "#FFC8D6", "#C9EBC4", "#C4E0FA", "#FFD5AE", "#DDD0F7"]
    readonly property var colourNames: ["Yellow", "Pink", "Green", "Blue", "Orange", "Lilac"]
    readonly property color ink: "#1E2227"            // on every note colour: ≥ 12:1
    readonly property real defaultW: 230
    readonly property real defaultH: 200
    readonly property real band: 26                     // the strip along the top you drag it by
    readonly property real pad: 14

    SystemPalette { id: pal }
    ListModel { id: noteRows }

    // Updated a row at a time: rebuilding it would throw away the note being typed into every time
    // a neighbour moved or changed colour.
    function reload() {
        const want = pageId ? textBlocks.list(pageId) : []
        const rows = want.map(b => ({ bid: b.id, bx: b.x, by: b.y, bw: b.w, bh: b.h || 0, markdown: b.markdown, colour: b.colour || "" }))
        for (let i = noteRows.count - 1; i >= 0; --i)
            if (!rows.some(r => r.bid === noteRows.get(i).bid)) noteRows.remove(i)
        rows.forEach((r, i) => {
            let at = -1
            for (let j = 0; j < noteRows.count; ++j) if (noteRows.get(j).bid === r.bid) { at = j; break }
            if (at < 0) { noteRows.insert(Math.min(i, noteRows.count), r); return }
            if (at !== i) noteRows.move(at, i, 1)
            for (const role of ["bx", "by", "bw", "bh", "colour"]) if (noteRows.get(i)[role] !== r[role]) noteRows.setProperty(i, role, r[role])
            const open = itemFor(r.bid)
            if (!(open && open.editing) && noteRows.get(i).markdown !== r.markdown) noteRows.setProperty(i, "markdown", r.markdown)
        })
        if (selectedId && !rows.some(r => r.bid === selectedId)) selectedId = 0
    }
    function itemFor(id) {
        for (let i = 0; i < repeater.count; ++i) { const it = repeater.itemAt(i); if (it && it.bid === id) return it }
        return null
    }
    // A tap with the sticky-note tool: the note's top-left corner where the pen came down, kept on the sheet.
    function addAt(page) {
        if (!pageId) return
        const sheet = canvas.pageSize
        const x = canvas.infinite ? page.x : Math.max(0, Math.min(page.x, sheet.width - defaultW))
        const y = canvas.infinite ? page.y : Math.max(0, Math.min(page.y, sheet.height - defaultH))
        const colour = library.setting("sticky.colour", colours[0])
        const id = textBlocks.create(pageId, x, y, defaultW, recordingId, recordingT(), defaultH, colour)
        reload()
        selectedId = id
        Qt.callLater(() => { const it = itemFor(id); if (it) it.startEditing() })
    }
    // Putting a note down: whatever is being typed is kept, and the bar goes away.
    function putDown() {
        const open = itemFor(selectedId)
        if (open && open.editing) open.commit()
        selectedId = 0
    }
    function recolour(id, colour) {
        library.setSetting("sticky.colour", colour)
        textBlocks.setColour(id, colour)
    }
    function removeNote(id) {
        const it = itemFor(id)
        if (it && it.editing) it.flush()
        const gone = textBlocks.block(id)
        if (!gone.id) return
        selectedId = 0
        textBlocks.remove(id)
        if (!(gone.markdown || "").trim().length) return             // an empty note is not worth an Undo
        notes.toastAction("Sticky note deleted", "Undo", function() {
            const back = textBlocks.create(gone.pageId, gone.x, gone.y, gone.w, gone.recordingId, 0, gone.h, gone.colour)
            textBlocks.setMarkdown(back, gone.markdown)
            notes.reload()
        })
    }
    Connections { target: textBlocks; function onChanged(pid) { if (pid === notes.pageId) notes.reload() } }
    onPageIdChanged: { selectedId = 0; noteRows.clear(); reload() }
    // A rename changes what a link says; the notes pick that up here.
    property int linkTick: 0
    Connections { target: library; function onChanged() { notes.linkTick++ } }

    // [[ in a note opens the page picker; the pick is written as [Title](lumen://page/id).
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
        excludePageId: notes.pageId
        onChosen: (id, title) => notes.insertLink(id)
        onCreateAsked: (title) => {
            const home = library.page(notes.pageId)
            if (!home.id) return
            const id = library.createPage(home.sectionId, "", "typed")
            library.rename("page", id, title)
            notes.insertLink(id)
        }
    }

    // Markdown → rendered: $$…$$ and $…$ become inline images from the latex worker, in the note's ink.
    function toRich(md) {
        const colour = notes.ink.toString()
        const enc = (tex) => "![tex](image://latex/" + encodeURIComponent(tex) + "?c=" + encodeURIComponent(colour) + ")"
        let out = md.replace(/\$\$([\s\S]+?)\$\$/g, (m, t) => "\n\n" + enc(t.trim()) + "\n\n")
        out = out.replace(/\$([^$\n]+?)\$/g, (m, t) => enc(t.trim()))
        return out
    }

    // ---- the bar for the open note: its colours and Delete, outside the zoom
    Rectangle {
        id: bar
        objectName: "chrome"
        readonly property Item note: notes.itemFor(notes.selectedId)
        visible: note !== null && notes.selectedId > 0
        z: 5
        // Page units to this layer, the way pageSpace maps them, so the bar follows a drag and a zoom.
        readonly property point below: note ? Qt.point((note.x + note.width / 2) * canvas.zoom + canvas.pan.x,
                                                       (note.y + note.height) * canvas.zoom + canvas.pan.y) : Qt.point(0, 0)
        readonly property real aboveY: note ? note.y * canvas.zoom + canvas.pan.y : 0
        x: Math.max(8, Math.min(notes.width - width - 8, below.x - width / 2))
        y: below.y + 10 + height < notes.height - 8 ? below.y + 10 : Math.max(8, aboveY - height - 10)
        implicitWidth: barRow.implicitWidth + 16; implicitHeight: Ui.target + 8
        radius: height / 2
        color: Qt.alpha(pal.window, 0.97)
        border.color: Qt.alpha(pal.text, 0.18); border.width: 1
        Row {
            id: barRow
            anchors.centerIn: parent
            spacing: 2
            Repeater {
                model: notes.colours
                delegate: Item {
                    id: swatch
                    required property string modelData
                    required property int index
                    objectName: "stickyColour"
                    readonly property bool on: bar.note !== null && bar.note.noteColour === modelData
                    width: Ui.target; height: Ui.target
                    Accessible.role: Accessible.Button
                    Accessible.name: notes.colourNames[index] + (on ? ", chosen" : "")
                    Rectangle {
                        anchors.centerIn: parent
                        width: 26; height: 26; radius: 13
                        color: swatch.modelData
                        border.width: swatch.on ? 3 : 1
                        border.color: swatch.on ? pal.highlight : Qt.alpha("#000000", 0.25)
                    }
                    TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: notes.recolour(notes.selectedId, swatch.modelData) }
                }
            }
            Rectangle { width: 1; height: Ui.target - 16; anchors.verticalCenter: parent.verticalCenter; color: Qt.alpha(pal.text, Ui.hairline) }
            Item {
                objectName: "stickyDelete"
                width: Ui.target + 4; height: Ui.target
                Accessible.role: Accessible.Button
                Accessible.name: "Delete this sticky note"
                Rectangle { anchors.fill: parent; anchors.margins: 3; radius: height / 2; color: delTap.pressed ? Qt.alpha(Ui.danger, 0.2) : "transparent" }
                Icon { anchors.centerIn: parent; name: "edit-delete"; colour: Ui.danger }
                TapHandler { id: delTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: notes.removeNote(notes.selectedId) }
            }
        }
    }

    Item {
        id: pageSpace
        transform: [ Scale { xScale: canvas.zoom; yScale: canvas.zoom }, Translate { x: canvas.pan.x; y: canvas.pan.y } ]

        Repeater {
            id: repeater
            model: noteRows
            delegate: Item {
                id: note
                objectName: "chrome"
                required property var bid
                required property real bx
                required property real by
                required property real bw
                required property real bh
                required property string markdown
                required property string colour
                readonly property string noteColour: colour.length ? colour : notes.colours[0]
                readonly property bool selected: notes.selectedId === bid
                property bool editing: false
                // Offsets, never the bound properties themselves — see ImageLayer for why.
                property real dx: 0
                property real dy: 0
                property real dw: 0
                property real dh: 0
                function resetDrag() { dx = 0; dy = 0; dw = 0; dh = 0 }
                readonly property real contentH: (editing ? editor.implicitHeight : view.implicitHeight) + notes.band + notes.pad
                x: bx + dx; y: by + dy
                width: Math.max(140, bw + dw)
                // No height of its own (an OCR line, a maths answer, a note from before): fit the words.
                readonly property real ownH: bh > 0 ? bh : Math.max(100, contentH)
                height: Math.max(100, ownH + dh, contentH)
                z: selected ? 1 : 0

                function startEditing() {
                    notes.selectedId = bid
                    editing = true
                    editor.text = markdown
                    editor.forceActiveFocus()
                    editor.cursorPosition = editor.length
                }
                function flush() {
                    if (editing && editor.text !== markdown) { textBlocks.setMarkdown(bid, editor.text, notes.recordingT()); markdown = editor.text }
                }
                function commit() {
                    if (!editing) return
                    editing = false
                    autosave.stop()
                    if (notes.linkEditor === editor) linkPicker.close()
                    // Read everything off the delegate first: remove() rebuilds the model, which
                    // can release this delegate mid-function.
                    const id = bid, text = library.resolveLinks(editor.text), was = markdown
                    if (text.trim().length === 0) { if (notes.selectedId === id) notes.selectedId = 0; textBlocks.remove(id); return }
                    if (text !== was) { textBlocks.setMarkdown(id, text, notes.recordingT()); markdown = text }
                }
                // Typing reaches the database within a fifth of a second, so a crash loses nothing.
                Timer { id: autosave; interval: 200; onTriggered: note.flush() }
                function commitGeometry() {
                    const sheet = canvas.pageSize
                    const w = width, h = Math.max(100, ownH + dh)
                    const nx = canvas.infinite ? x : Math.max(0, Math.min(x, sheet.width - w))
                    const ny = canvas.infinite ? y : Math.max(0, Math.min(y, sheet.height - Math.min(h, sheet.height)))
                    const id = bid
                    flush()
                    resetDrag()
                    textBlocks.setGeometry(id, nx, ny, w, h)
                }

                Rectangle {                            // a soft shadow lifts it off the paper
                    x: 0; y: 3; width: parent.width; height: parent.height; radius: 8
                    color: Qt.alpha("#000000", 0.22)
                }
                Rectangle {
                    id: card
                    anchors.fill: parent
                    radius: 8
                    color: note.noteColour
                    border.width: note.selected ? 2.5 / Math.max(canvas.zoom, 0.2) : 1 / Math.max(canvas.zoom, 0.2)
                    border.color: note.selected ? pal.highlight : Qt.darker(note.noteColour, 1.25)
                }
                Rectangle {                            // the band you drag it by, with its grip
                    x: card.border.width; y: card.border.width
                    width: parent.width - 2 * card.border.width; height: notes.band
                    radius: 7
                    color: Qt.darker(note.noteColour, 1.07)
                    Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: parent.radius; color: parent.color }
                    Row {
                        anchors.centerIn: parent
                        spacing: 5
                        Repeater { model: 3; Rectangle { width: 5; height: 5; radius: 2.5; color: Qt.alpha(notes.ink, 0.35) } }
                    }
                    DragHandler {
                        acceptedDevices: notes.allDevices
                        target: null
                        onActiveChanged: { if (active) notes.selectedId = note.bid; else note.commitGeometry() }
                        onCanceled: note.resetDrag()
                        onTranslationChanged: { note.dx = translation.x / canvas.zoom; note.dy = translation.y / canvas.zoom }
                    }
                }
                // Not typing: the whole card moves it, and a tap opens it.
                DragHandler {
                    enabled: !note.editing
                    acceptedDevices: notes.allDevices
                    target: null
                    onActiveChanged: { if (active) notes.selectedId = note.bid; else note.commitGeometry() }
                    onCanceled: note.resetDrag()
                    onTranslationChanged: { note.dx = translation.x / canvas.zoom; note.dy = translation.y / canvas.zoom }
                }
                TapHandler {
                    enabled: !note.editing
                    acceptedDevices: notes.allDevices
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: note.startEditing()
                }

                Text {
                    id: view
                    visible: !note.editing
                    x: notes.pad; y: notes.band + 6; width: parent.width - 2 * notes.pad
                    textFormat: Text.MarkdownText
                    text: note.markdown.length ? notes.toRich(notes.linkTick >= 0 ? library.resolveLinks(note.markdown) : note.markdown) : ""
                    color: notes.ink
                    linkColor: "#1C5FB8"
                    wrapMode: Text.Wrap
                    font.pixelSize: Ui.px(20)
                    lineHeight: 1.15
                    onLinkActivated: (link) => link.startsWith("lumen://") ? notes.pageLinkActivated(link) : Qt.openUrlExternally(link)
                }
                Text {
                    visible: !note.editing && !note.markdown.length
                    x: notes.pad; y: notes.band + 6
                    text: "Tap to write"
                    color: Qt.alpha(notes.ink, 0.45)
                    font.pixelSize: Ui.px(20); font.italic: true
                }
                TextArea {
                    id: editor
                    objectName: "stickyEditor"
                    visible: note.editing
                    x: notes.pad - leftPadding; y: notes.band + 6 - topPadding; width: parent.width - 2 * notes.pad + leftPadding + rightPadding
                    wrapMode: TextEdit.Wrap
                    font.pixelSize: Ui.px(20)
                    color: notes.ink
                    selectionColor: Qt.alpha(pal.highlight, 0.45)
                    selectedTextColor: notes.ink
                    placeholderText: "Write a note…"
                    placeholderTextColor: Qt.alpha(notes.ink, 0.45)
                    background: null
                    onActiveFocusChanged: if (!activeFocus) note.commit()
                    onTextChanged: if (note.editing) { autosave.restart(); Qt.callLater(notes.checkLink, editor) }
                    Keys.onPressed: (e) => {
                        const picking = linkPicker.visible && notes.linkEditor === editor
                        if (e.key === Qt.Key_Escape) { if (picking) linkPicker.close(); else { note.commit(); notes.selectedId = 0 } e.accepted = true }
                        else if (!picking) return
                        else if (e.key === Qt.Key_Down || e.key === Qt.Key_Up) { linkPicker.move(e.key === Qt.Key_Down ? 1 : -1); e.accepted = true }
                        else if (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Tab) { linkPicker.acceptCurrent(); e.accepted = true }
                    }
                }

                // corner grip: pull to resize, both ways
                Item {
                    objectName: "stickyResize"
                    width: 22; height: 22
                    x: parent.width - width; y: parent.height - height
                    Canvas {
                        anchors.fill: parent
                        opacity: note.selected ? 0.7 : 0.35
                        onPaint: {
                            const g = getContext("2d")
                            g.reset(); g.strokeStyle = notes.ink; g.lineWidth = 1.6; g.lineCap = "round"
                            for (const k of [8, 14]) { g.beginPath(); g.moveTo(width - 5, height - k); g.lineTo(width - k, height - 5); g.stroke() }
                        }
                    }
                    DragHandler {
                        acceptedDevices: notes.allDevices
                        target: null
                        margin: Ui.target / 3
                        // The card's own move handler sees the same press; the corner wins it.
                        grabPermissions: PointerHandler.CanTakeOverFromAnything
                        onActiveChanged: { if (active) notes.selectedId = note.bid; else note.commitGeometry() }
                        onCanceled: note.resetDrag()
                        onTranslationChanged: { note.dw = translation.x / canvas.zoom; note.dh = translation.y / canvas.zoom }
                    }
                }
            }
        }
    }
}
