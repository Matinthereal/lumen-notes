import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// A page you type on: one real text editor with headings, lists, checklists and the usual keys.
// Built for a keyboard and a mouse, no pen or touchscreen needed. The text is kept as Markdown in
// the page's single text block, so search, export and "ask lumen" read it like any other text.
Item {
    id: typed
    objectName: "typedPage"
    property var pageId: 0
    property var blockId: 0
    property bool loading: false
    readonly property alias editor: area
    readonly property alias formatter: fmt
    // Main puts the page's name and whether it is saved under the format bar; this is the room it takes.
    property real infoHeight: 0
    readonly property real barBottom: formatBar.y + formatBar.height
    SystemPalette { id: pal }

    function save() {
        if (!typed.blockId || typed.loading) return
        saveTimer.stop()
        textBlocks.setMarkdown(typed.blockId, fmt.markdown())
        typed.words = fmt.wordCount()
    }
    function load() {
        saveTimer.stop()
        typed.loading = true
        typed.blockId = 0
        if (typed.pageId > 0) {
            const blocks = textBlocks.list(typed.pageId)
            typed.blockId = blocks.length ? blocks[0].id : textBlocks.create(typed.pageId, 0, 0, 794)
            fmt.setMarkdown(blocks.length ? (blocks[0].markdown || "") : "")
        } else {
            fmt.setMarkdown("")
        }
        area.cursorPosition = 0
        typed.words = fmt.wordCount()
        typed.loading = false
    }
    property int words: 0
    onPageIdChanged: { load(); Qt.callLater(() => { if (typed.visible) area.forceActiveFocus() }) }
    Component.onDestruction: save()
    onVisibleChanged: if (!visible) save()

    Timer { id: saveTimer; interval: 700; onTriggered: typed.save() }

    DocumentFormatter {
        id: fmt
        document: area.textDocument
        selectionStart: area.selectionStart
        selectionEnd: area.selectionEnd
        onModified: { if (!typed.loading) saveTimer.restart() }
    }

    component FormatButton: Rectangle {
        id: btn
        property string label
        property string icon: ""
        property string tip
        property bool on: false
        property bool bold: false
        property bool italic: false
        property bool underline: false
        property bool strike: false
        signal clicked()
        objectName: "formatButton"
        implicitWidth: Math.max(Ui.target - 6, lbl.implicitWidth + 16); implicitHeight: Ui.target - 6
        radius: 8
        color: on ? Qt.alpha(pal.highlight, 0.28)
             : (tap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : (hover.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent"))
        Accessible.role: Accessible.Button
        Accessible.name: tip
        Text {
            id: lbl
            visible: btn.icon.length === 0
            anchors.centerIn: parent
            text: btn.label
            color: pal.windowText
            font.pixelSize: Ui.text
            font.bold: btn.bold; font.italic: btn.italic; font.underline: btn.underline; font.strikeout: btn.strike
        }
        Icon { visible: btn.icon.length > 0; anchors.centerIn: parent; name: btn.icon }
        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
        // Buttons must not take focus away from the text, or the selection they act on is gone.
        TapHandler { id: tap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { btn.clicked(); area.forceActiveFocus() } }
        ToolTip.visible: hover.hovered && tip.length > 0; ToolTip.text: tip; ToolTip.delay: 600
    }
    component Sep: Rectangle { implicitWidth: 1; implicitHeight: Ui.target - 18; color: Qt.alpha(pal.text, 0.16) }

    Rectangle {
        id: formatBar
        objectName: "chrome"
        anchors { top: parent.top; horizontalCenter: parent.horizontalCenter; topMargin: 12 }
        width: Math.min(parent.width - 24, bar.implicitWidth + 16)
        height: Ui.target + 4
        radius: Ui.radiusLg
        color: pal.window
        border.color: Qt.alpha(pal.text, 0.16)
        clip: true
        Flickable {
            anchors.fill: parent; anchors.margins: 8
            contentWidth: bar.implicitWidth; contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            RowLayout {
                id: bar
                height: parent.height
                spacing: 2
                FormatButton { icon: "edit-undo"; tip: "Undo (Ctrl+Z)"; onClicked: area.undo() }
                FormatButton { icon: "edit-redo"; tip: "Redo (Ctrl+Shift+Z)"; onClicked: area.redo() }
                Sep {}
                FormatButton { label: "Text"; tip: "Body text (Ctrl+Alt+0)"; on: fmt.heading === 0 && fmt.list === ""; onClicked: fmt.setHeading(0) }
                FormatButton { label: "H1"; bold: true; tip: "Title (Ctrl+Alt+1)"; on: fmt.heading === 1; onClicked: fmt.setHeading(fmt.heading === 1 ? 0 : 1) }
                FormatButton { label: "H2"; bold: true; tip: "Heading (Ctrl+Alt+2)"; on: fmt.heading === 2; onClicked: fmt.setHeading(fmt.heading === 2 ? 0 : 2) }
                FormatButton { label: "H3"; bold: true; tip: "Subheading (Ctrl+Alt+3)"; on: fmt.heading === 3; onClicked: fmt.setHeading(fmt.heading === 3 ? 0 : 3) }
                Sep {}
                FormatButton { label: "B"; bold: true; tip: "Bold (Ctrl+B)"; on: fmt.bold; onClicked: fmt.toggleBold() }
                FormatButton { label: "I"; italic: true; tip: "Italic (Ctrl+I)"; on: fmt.italic; onClicked: fmt.toggleItalic() }
                FormatButton { label: "U"; underline: true; tip: "Underline (Ctrl+U)"; on: fmt.underline; onClicked: fmt.toggleUnderline() }
                FormatButton { label: "S"; strike: true; tip: "Strikethrough (Ctrl+Shift+X)"; on: fmt.strikeout; onClicked: fmt.toggleStrikeout() }
                Sep {}
                FormatButton { label: "•  List"; tip: "Bulleted list (Ctrl+Shift+8)"; on: fmt.list === "bullet"; onClicked: fmt.toggleList("bullet") }
                FormatButton { label: "1.  List"; tip: "Numbered list (Ctrl+Shift+7)"; on: fmt.list === "number"; onClicked: fmt.toggleList("number") }
                FormatButton { label: "☐  To-do"; tip: "Checklist (Ctrl+Shift+9) — click a box to tick it"; on: fmt.list === "check"; onClicked: fmt.toggleList("check") }
                FormatButton { label: "⇤"; tip: "Outdent (Shift+Tab)"; onClicked: fmt.indent(-1) }
                FormatButton { label: "⇥"; tip: "Indent (Tab)"; onClicked: fmt.indent(1) }
                Sep {}
                Text {
                    objectName: "wordCount"
                    text: typed.words + (typed.words === 1 ? " word" : " words")
                    color: Qt.alpha(pal.windowText, Ui.mutedAlpha)
                    font.pixelSize: Ui.small
                    Layout.leftMargin: 8; Layout.rightMargin: 8
                }
            }
        }
    }

    // A plain Flickable with an overlay scroll bar: a ScrollView's bar takes width when it appears,
    // which re-wraps the text, which changes the height, which hides the bar again.
    Flickable {
        id: scroller
        anchors { top: formatBar.bottom; bottom: parent.bottom; left: parent.left; right: parent.right; topMargin: 12 + typed.infoHeight }
        contentWidth: width
        contentHeight: sheet.height + 48
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { width: Ui.scrollbar }

        Item {
            width: scroller.width
            height: scroller.contentHeight
            Rectangle {
                id: sheet
                anchors.horizontalCenter: parent.horizontalCenter
                y: 8
                width: Math.min(parent.width - 32, 860)
                height: Math.max(area.implicitHeight + 96, scroller.height - 40)
                radius: 10
                color: pal.base
                border.color: Qt.alpha(pal.text, 0.10)

                TextArea {
                    id: area
                    objectName: "typedEditor"
                    x: 56; y: 48
                    width: parent.width - 112
                    padding: 0
                    background: null
                    textFormat: TextEdit.RichText
                    wrapMode: TextEdit.Wrap
                    selectByMouse: true
                    persistentSelection: true
                    color: pal.text
                    selectionColor: pal.highlight
                    selectedTextColor: pal.highlightedText
                    font.pixelSize: Math.round(Ui.text * 1.15)
                    placeholderText: "Start typing…"
                    placeholderTextColor: Qt.alpha(pal.text, 0.45)
                    Accessible.name: "Page text"

                    // Keep the caret on screen as you type past the bottom.
                    onCursorRectangleChanged: {
                        const flick = scroller
                        const top = sheet.y + area.y + cursorRectangle.y
                        const bottom = top + cursorRectangle.height
                        if (bottom > flick.contentY + flick.height - 24) flick.contentY = bottom - flick.height + 24
                        else if (top < flick.contentY + 8) flick.contentY = Math.max(0, top - 8)
                    }

                    Keys.onPressed: (e) => {
                        const ctrl = e.modifiers & Qt.ControlModifier, shift = e.modifiers & Qt.ShiftModifier, alt = e.modifiers & Qt.AltModifier
                        if (ctrl && alt && e.key >= Qt.Key_0 && e.key <= Qt.Key_3) { fmt.setHeading(e.key - Qt.Key_0); e.accepted = true; return }
                        if (ctrl && !shift && e.key === Qt.Key_B) { fmt.toggleBold(); e.accepted = true; return }
                        if (ctrl && !shift && e.key === Qt.Key_I) { fmt.toggleItalic(); e.accepted = true; return }
                        if (ctrl && !shift && e.key === Qt.Key_U) { fmt.toggleUnderline(); e.accepted = true; return }
                        if (ctrl && shift && e.key === Qt.Key_X) { fmt.toggleStrikeout(); e.accepted = true; return }
                        if (ctrl && shift && (e.key === Qt.Key_8 || e.key === Qt.Key_Asterisk)) { fmt.toggleList("bullet"); e.accepted = true; return }
                        if (ctrl && shift && (e.key === Qt.Key_7 || e.key === Qt.Key_Ampersand)) { fmt.toggleList("number"); e.accepted = true; return }
                        if (ctrl && shift && (e.key === Qt.Key_9 || e.key === Qt.Key_ParenLeft)) { fmt.toggleList("check"); e.accepted = true; return }
                        if (fmt.list.length && (e.key === Qt.Key_Tab || e.key === Qt.Key_Backtab)) {
                            fmt.indent(e.key === Qt.Key_Backtab || shift ? -1 : 1); e.accepted = true; return
                        }
                        if ((e.key === Qt.Key_Return || e.key === Qt.Key_Enter) && !shift && fmt.continueList()) { e.accepted = true; return }
                        if (e.key === Qt.Key_Tab && !ctrl) { area.insert(area.cursorPosition, "\t"); e.accepted = true; return }
                    }

                    // A click left of a checklist line's text ticks its box.
                    TapHandler {
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
                        gesturePolicy: TapHandler.DragThreshold
                        onTapped: (point) => {
                            const pos = area.positionAt(point.position.x, point.position.y)
                            const start = fmt.blockStart(pos)
                            const r = area.positionToRectangle(start)
                            if (point.position.x < r.x && Math.abs(point.position.y - (r.y + r.height / 2)) < r.height)
                                fmt.toggleCheckAt(start)
                        }
                    }
                }
            }
        }
    }
}
