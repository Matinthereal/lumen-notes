import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Notebooks → sections → pages. Quiet by default: notebook headers with a colour bar, sections
// indented under a guide line, pages as numbered rows. Actions live behind long-press /
// right-click / the ⋯ that appears on hover or on the current row — nothing shouts.
Rectangle {
    id: side
    objectName: "chrome"
    property var currentPageId: 0
    property var expandedNotebooks: ({})
    property var expandedSections: ({})
    signal openPage(var pageId)
    signal deleted(string kind, var id, string name)
    signal deleting(string kind, var id)   // fired before the row is removed, so neighbours can still be computed
    signal importPdf(var notebookId)
    signal exportNotebook(var notebookId)
    signal importNotebook()
    signal closePage()
    signal openBeside(var pageId)

    SystemPalette { id: pal }
    color: pal.window
    ListModel { id: rows }

    function rebuild() {
        rows.clear()
        const info = library.page(currentPageId)
        for (const nb of library.notebooks()) {
            const nbOpen = expandedNotebooks[nb.id] === undefined ? nb.id === info.notebookId : expandedNotebooks[nb.id]
            rows.append({ level: 0, kind: "notebook", id: nb.id, name: nb.name, colour: nb.colour, board: nb.board || "", secKind: "", expanded: nbOpen, count: nb.sectionCount, pageNo: 0, parentId: 0, current: false, last: false })
            if (!nbOpen) continue
            const secs = library.sections(nb.id)
            for (let si = 0; si < secs.length; ++si) {
                const sec = secs[si]
                const secOpen = expandedSections[sec.id] === undefined ? sec.id === info.sectionId : expandedSections[sec.id]
                rows.append({ level: 1, kind: "section", id: sec.id, name: sec.name, colour: nb.colour, board: "", secKind: sec.kind, expanded: secOpen, count: sec.pageCount, pageNo: 0, parentId: nb.id, current: false, last: si === secs.length - 1 })
                if (!secOpen) continue
                const pages = library.pages(sec.id)
                for (const pg of pages)
                    rows.append({ level: 2, kind: "page", id: pg.id, name: pg.title, colour: nb.colour, board: "", secKind: "", expanded: false, count: 0, pageNo: pg.index + 1, parentId: sec.id, current: pg.id === currentPageId, last: pg.index === pages.length - 1 })
            }
        }
    }
    function clearCurrent() { currentPageId = 0; rebuild() }
    function revealPage(pageId) {
        const info = library.page(pageId); if (!info.id) return
        expandedNotebooks[info.notebookId] = true; expandedSections[info.sectionId] = true
        currentPageId = pageId; rebuild()
        Qt.callLater(() => { for (let i = 0; i < rows.count; ++i) if (rows.get(i).kind === "page" && rows.get(i).id === pageId) { list.positionViewAtIndex(i, ListView.Contain); break } })
    }
    Connections { target: library; function onChanged() { side.rebuild() } }
    Component.onCompleted: rebuild()
    onCurrentPageIdChanged: rebuild()

    ActionSheet { id: sheet; parent: Overlay.overlay }
    function closeActions() { sheet.close() }
    function showActions(row, atItem) {
        row = snap(row)   // a plain copy: the list model is rebuilt underneath the sheet as soon as an action changes the library
        sheet.title = row.kind === "page" ? (row.name.length ? row.name.replace("\u200b", "") : "Page " + row.pageNo) : row.name
        sheet.items = actionsFor(row)
        const p = atItem.mapToItem(null, 0, atItem.height)
        sheet.source = null
        sheet.wantX = p.x + 24; sheet.wantY = p.y          // the sheet clamps itself to the window
        sheet.open()
    }
    function snap(row) {
        return { level: row.level, kind: row.kind, id: row.id, name: row.name || "", colour: row.colour || "", pageNo: row.pageNo || 0, parentId: row.parentId || 0, current: row.current === true }
    }
    function actionsFor(row) {
        const items = []
        if (row.kind === "notebook") {
            items.push({ label: "New section", icon: "list-add", action: () => { const s = library.createSection(row.id, "New section"); side.expandedNotebooks[row.id] = true; side.expandedSections[s] = true; side.rebuild(); renamer.begin({ kind: "section", id: s, name: "New section" }, true) } })
            items.push({ label: "Import PDF as slides", icon: "document-import", action: () => side.importPdf(row.id) })
            items.push({ label: "Export notebook…", icon: "document-export", action: () => side.exportNotebook(row.id) })
            items.push({ label: "Next colour", icon: "color-picker", action: () => { const cs = ["#1F3A93", Ui.danger, Ui.good, Ui.warning, "#5B2A86", "#0F7C8A", "#5C6B7A"]; library.setNotebookColour(row.id, cs[(cs.indexOf(row.colour) + 1) % cs.length]) } })
        } else if (row.kind === "section") {
            items.push({ label: "New typed page", icon: "list-add", action: () => { const p = library.createPage(row.id, "", "typed"); side.expandedSections[row.id] = true; side.openPage(p) } })
            items.push({ label: "New handwritten page", icon: "draw-freehand", action: () => { const p = library.createPage(row.id, "", "a4"); side.expandedSections[row.id] = true; side.openPage(p) } })
        } else {
            items.push({ label: "New page after this", icon: "list-add", action: () => { const pages = library.pages(row.parentId); const idx = pages.findIndex(p => p.id === row.id); side.openPage(library.createPage(row.parentId, "", "", idx)) } })
        }
        if (row.kind === "page") {
            const starred = library.isStarred(row.id)
            items.push({ label: starred ? "Remove star" : "Star this page", icon: starred ? "edit-clear" : "bookmarks",
                         action: () => library.setStarred(row.id, !starred) })
            items.push({ label: "Duplicate", icon: "edit-copy", action: () => { const id = library.duplicatePage(row.id); if (id) side.openPage(id) } })
            if (!row.current) items.push({ label: "Open beside", icon: "view-split", action: () => side.openBeside(row.id) })
        }
        if (row.kind === "page" && row.current) items.push({ label: "Close page", icon: "window-close", action: () => side.closePage() })
        items.push({ label: "Rename", icon: "edit-rename", action: () => renamer.begin(row) })
        items.push({ label: "Delete", icon: "edit-delete", danger: true, action: () => {
            const shown = row.name.length ? row.name.replace("\u200b", "") : (row.kind === "page" ? "Page " + row.pageNo : row.kind)
            side.deleting(row.kind, row.id)
            library.remove(row.kind, row.id)          // rebuilds the sidebar; `row` is our own copy so this is safe
            side.deleted(row.kind, row.id, shown)
        } })
        return items
    }
    Popup {
        id: renamer
        objectName: "chrome"
        property var row: null
        parent: Overlay.overlay; x: (parent.width - width) / 2; y: 140; width: 360; padding: 14; modal: true
        background: Rectangle { radius: 12; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha) }
        property bool removeIfCancelled: false
        function begin(r, isNew) { row = r; removeIfCancelled = isNew === true; field.text = r.name; open(); field.forceActiveFocus(); field.selectAll() }
        function apply() { if (renamer.row) library.rename(renamer.row.kind, renamer.row.id, field.text); removeIfCancelled = false; renamer.close() }
        function cancel() { if (removeIfCancelled && renamer.row) library.remove(renamer.row.kind, renamer.row.id); removeIfCancelled = false; renamer.close() }
        onClosed: if (removeIfCancelled && renamer.row) { library.remove(renamer.row.kind, renamer.row.id); removeIfCancelled = false }
        ColumnLayout { anchors.fill: parent; spacing: 10
            Text { text: "Rename " + (renamer.row ? renamer.row.kind : ""); color: pal.windowText; font.pixelSize: Ui.text + 1; font.weight: Font.DemiBold }
            TextField { id: field; Layout.fillWidth: true; font.pixelSize: Ui.text + 1; onAccepted: renamer.apply() }
            RowLayout { Item { Layout.fillWidth: true }
                        Button { text: "Cancel"; onClicked: renamer.cancel() }
                        Button { text: renamer.removeIfCancelled ? "Create" : "Rename"; onClicked: renamer.apply() } }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        RowLayout {
            Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 10; Layout.topMargin: 14; Layout.bottomMargin: 8
            Text { text: "Notebooks"; color: pal.windowText; font.pixelSize: Ui.text + 5; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Rectangle {
                objectName: "importNotebook"
                implicitWidth: Ui.target - 6; implicitHeight: Ui.target - 6; radius: 8; color: importHover.hovered ? Qt.alpha(pal.text, 0.1) : "transparent"
                Icon { anchors.centerIn: parent; name: "document-import"; implicitWidth: Ui.icon; implicitHeight: Ui.icon; opacity: 0.8 }
                Accessible.role: Accessible.Button
                Accessible.name: "Import a notebook"
                HoverHandler { id: importHover }
                TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: side.importNotebook() }
                ToolTip.visible: importHover.hovered; ToolTip.text: "Import a notebook from a .lumen file"; ToolTip.delay: 600
            }
            Rectangle {
                implicitWidth: Ui.target - 6; implicitHeight: Ui.target - 6; radius: 8; color: addHover.hovered ? Qt.alpha(pal.text, 0.1) : "transparent"
                Icon { anchors.centerIn: parent; name: "folder-new"; implicitWidth: Ui.icon; implicitHeight: Ui.icon; opacity: 0.8 }
                HoverHandler { id: addHover }
                TapHandler { onTapped: { const id = library.createNotebook("New notebook", "#5C6B7A", ""); side.expandedNotebooks[id] = true; side.rebuild(); renamer.begin({ kind: "notebook", id: id, name: "New notebook" }, true) } }
                ToolTip.visible: addHover.hovered; ToolTip.text: "New notebook"; ToolTip.delay: 600
            }
        }
        ListView {
            id: list
            objectName: "sidebarList"
            Layout.fillWidth: true; Layout.fillHeight: true
            model: rows; clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { implicitWidth: Ui.scrollbar; policy: Ui.tablet ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
            delegate: Item {
                id: row
                objectName: "sidebarRow"
                readonly property var rowPageId: row.kind === "page" ? row.id : 0
                readonly property string rowKind: row.kind
                readonly property bool rowCurrent: row.current
                required property int index
                required property int level
                required property string kind
                required property var id
                required property string name
                required property string colour
                required property string board
                required property string secKind
                required property bool expanded
                required property int count
                required property int pageNo
                required property var parentId
                required property bool current
                required property bool last
                width: list.width
                height: kind === "notebook" ? Ui.row + 6 : (kind === "page" ? Ui.row + 26 : Ui.row)

                // notebook: a soft card header; section/page: flat rows
                Rectangle {
                    anchors { fill: parent; leftMargin: 8; rightMargin: 8; topMargin: row.kind === "notebook" ? 6 : 0 }
                    radius: 8
                    color: rowTap.pressed ? Qt.alpha(pal.text, 0.12)
                         : row.current ? Qt.alpha(pal.highlight, 0.18)
                         : (row.kind === "notebook" ? Qt.alpha(pal.text, 0.05) : (rowHover.hovered ? Qt.alpha(pal.text, 0.05) : "transparent"))
                }
                // guide line under a notebook for its sections/pages
                Rectangle { visible: row.kind !== "notebook"; x: 22; width: 2; height: parent.height; color: Qt.alpha(row.colour, 0.35) }
                Rectangle { visible: row.current; x: 8; width: 3; height: parent.height - 8; y: 4; radius: 1.5; color: pal.highlight }

                RowLayout {
                    anchors { fill: parent; leftMargin: row.kind === "notebook" ? 16 : 34 + (row.level - 1) * 14; rightMargin: 12; topMargin: row.kind === "notebook" ? 6 : 0 }
                    spacing: 8
                    Rectangle { visible: row.kind === "notebook"; implicitWidth: 5; implicitHeight: parent.height - 14; radius: 2.5; color: row.colour }
                    Text { visible: row.kind === "notebook"; text: row.expanded ? "▾" : "▸"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; Layout.preferredWidth: 10 }
                    Icon { visible: row.kind === "section"; name: "" + (row.secKind === "slides" ? "view-presentation" : row.secKind === "paper" ? "document-edit-verify" : "folder"); implicitWidth: Ui.icon - 4; implicitHeight: Ui.icon - 4; opacity: 0.6 }
                    Rectangle {
                        visible: row.kind === "page"
                        implicitWidth: 42; implicitHeight: 58; radius: 3; color: Qt.alpha(pal.base, 0.92); border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1
                        Image {
                            id: thumb
                            anchors { fill: parent; margins: 1 }
                            fillMode: Image.PreserveAspectFit; asynchronous: true; smooth: true
                            property int v: 0
                            source: row.kind === "page" && thumbnails.pathFor(row.id).length ? "file://" + thumbnails.pathFor(row.id) + "?v=" + v : ""
                            Connections { target: thumbnails; function onChanged(pid) { if (pid === row.id) thumb.v = thumbnails.version(pid) } }
                            Component.onCompleted: if (row.kind === "page") thumbnails.ensure(row.id)
                        }
                        Text {
                            anchors { right: parent.right; bottom: parent.bottom; margins: 2 }
                            text: row.pageNo; color: "#5C6B7A"; font.pixelSize: 9; font.family: "monospace"
                        }
                    }
                    Text {
                        readonly property bool autoTitle: row.kind === "page" && (row.name || "").startsWith("\u200b")
                        text: row.kind === "page" ? ((row.name || "").length ? row.name.replace("\u200b", "") : "Page " + row.pageNo) : (row.name || "")
                        color: row.kind === "page" && (!(row.name || "").length || autoTitle) ? Qt.alpha(pal.windowText, Ui.mutedAlpha) : pal.windowText
                        font.italic: autoTitle
                        font.pixelSize: row.kind === "notebook" ? Ui.text + 1 : Ui.text
                        font.weight: row.kind === "notebook" ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight; Layout.fillWidth: true
                    }
                    Text { visible: row.kind === "page" && library.isStarred(row.id); text: "★"; color: pal.highlight; font.pixelSize: Ui.small + 1 }
                    Text { visible: row.kind === "section" && !row.expanded && row.count > 0; text: row.count; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
                    Text { visible: row.kind === "section" && row.expanded && row.count === 0; text: "empty"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small }
                    Rectangle {
                        // "add" only — a page row has no button, so a tap on it can never mean "menu".
                        // Always visible: KDE's HIG forbids hover-only row controls, and touch has no hover.
                        objectName: "rowAddButton"
                        visible: row.kind !== "page"
                        opacity: rowHover.hovered || moreHover.hovered || row.current ? 1 : 0.45
                        implicitWidth: Ui.target - 12; implicitHeight: Ui.target - 12; radius: 7
                        color: moreHover.hovered ? Qt.alpha(pal.text, 0.12) : "transparent"
                        Icon { anchors.centerIn: parent; name: "list-add"; implicitWidth: Ui.icon - 4; implicitHeight: Ui.icon - 4; opacity: 0.7 }
                        HoverHandler { id: moreHover }
                        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds   // an exclusive grab, so the row handler below does not also fire
                                     onTapped: { if (row.kind === "notebook") { const s = library.createSection(row.id, "New section"); side.expandedNotebooks[row.id] = true; side.expandedSections[s] = true; side.rebuild(); renamer.begin({ kind: "section", id: s, name: "New section" }) } else { const p = library.createPage(row.id); side.expandedSections[row.id] = true; side.openPage(p) } } }
                        ToolTip.visible: moreHover.hovered; ToolTip.delay: 600; ToolTip.text: row.kind === "notebook" ? "New section" : "New page"
                    }
                }
                // Hold-to-open-menu, run by our own timer rather than TapHandler.longPressed: on a
                // touchscreen the built-in one fires on taps it should not (the handler keeps its
                // timer when the flickable steals the point, and a stalled UI thread delays the
                // release past the threshold). Ours stops the moment the press ends, and a release
                // that arrives quickly cancels a menu the timer had already opened.
                property bool holdFired: false
                property double pressedAt: 0
                Timer {
                    id: holdTimer
                    interval: 650                       // above iOS's 500 ms: the maker reported taps opening menus
                    onTriggered: { row.holdFired = true; side.showActions(row, row) }
                }
                Rectangle {
                    visible: holdTimer.running
                    anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 8; rightMargin: 8 }
                    height: 2; radius: 1; color: Qt.alpha(pal.highlight, 0.9)
                    transformOrigin: Item.Left
                    scale: holdTimer.running ? 1 : 0
                    Behavior on scale { NumberAnimation { duration: holdTimer.interval; easing.type: Easing.Linear } }
                }
                HoverHandler { id: rowHover }
                TapHandler {
                    id: rowTap
                    acceptedButtons: Qt.LeftButton
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
                    onPressedChanged: {
                        if (pressed) { row.holdFired = false; row.pressedAt = Date.now(); holdTimer.restart(); return }
                        holdTimer.stop()
                        const held = Date.now() - row.pressedAt
                        if (row.holdFired && held < 400) {        // the timer only won because something stalled
                            side.closeActions()
                            row.holdFired = false
                        }
                    }
                    onCanceled: { holdTimer.stop(); row.holdFired = false }
                    onTapped: {
                        holdTimer.stop()
                        if (row.holdFired) { row.holdFired = false; return }   // the menu is open; this is not a select
                        if (row.kind === "page") side.openPage(row.id)
                        else if (row.kind === "notebook") { side.expandedNotebooks[row.id] = !row.expanded; side.rebuild() }
                        else { side.expandedSections[row.id] = !row.expanded; side.rebuild() }
                    }
                }
                // Mouse/trackpad only. acceptedButtons filters QSinglePointEvent buttons; a QTouchEvent
                // carries none, so without acceptedDevices this handler fires on EVERY finger tap —
                // which is exactly what made tapping a row open the menu on the touchscreen.
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: side.showActions(row, row)
                }
            }
        }
        ColumnLayout {
            visible: rows.count === 0
            Layout.fillWidth: true; Layout.margins: 16; spacing: 6
            Text { text: "No notebooks yet"; color: pal.windowText; font.pixelSize: Ui.text + 1; font.weight: Font.DemiBold }
            Text { text: "Make one with the + above — one notebook per subject works well."
                   color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        Text { Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.topMargin: 6; Layout.bottomMargin: 10
               Layout.fillWidth: true; Layout.fillHeight: false
               Layout.preferredHeight: implicitHeight            // the list takes the slack; this must keep its own height
               wrapMode: Text.WordWrap
               text: "Tap a page to open it · hold it for rename, delete and more"
               color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; visible: rows.count > 0 }
    }
}
