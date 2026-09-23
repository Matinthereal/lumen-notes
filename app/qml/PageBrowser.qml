import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Every page in the section as a thumbnail, the way you flick through a real pad. Tap to open,
// hold for the menu, drag to reorder. Select several to move or delete them in one go. A tag chip
// swaps the section for every page with that tag, wherever it lives.
Rectangle {
    id: browser
    objectName: "chrome"
    property var sectionId: 0
    property var currentPageId: 0
    property string tag: ""                // "" = this section; else every page with this tag
    signal tagChosen(string tag)
    signal closed()
    signal openPage(var pageId)
    signal openBeside(var pageId)
    signal toast(string message)

    SystemPalette { id: pal }

    // A Rectangle accepts no buttons, so without this a drag here would draw ink on the page behind.

    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    color: pal.window
    focus: visible
    Keys.onEscapePressed: browser.closed()
    onVisibleChanged: if (visible) { selected = []; reload() }
    onSectionIdChanged: reload()
    onTagChanged: { selected = []; reload() }
    property var allTags: []
    // The tag as it is spelled on the pages, whatever case it was asked for in.
    readonly property string tagName: { const t = allTags.find(x => x.name.toLowerCase() === tag.toLowerCase()); return t ? t.name : tag }

    property var selected: []
    readonly property bool picking: selected.length > 0
    property var info: ({})

    function reload() {
        pages.clear()
        allTags = library.tags()
        info = sectionId ? library.section(sectionId) : ({})
        // section() has no notebook name; allSections() does, and it is what the move list uses.
        const home = library.allSections().find(sec => sec.id === sectionId)
        if (home) { info.notebookName = home.notebookName; info.name = home.name }
        for (const p of (tag.length ? library.pagesWithTag(tag) : library.pages(sectionId))) {
            thumbnails.ensure(p.id)
            pages.append({ pid: p.id, title: p.title, idx: p.index + 1, place: tag.length ? p.notebookName + " › " + p.sectionName : "" })
        }
    }
    function toggle(id) {
        const at = selected.indexOf(id)
        const next = selected.slice()
        if (at >= 0) next.splice(at, 1); else next.push(id)
        selected = next
    }
    function labelFor(row) {
        // A tagged page's number is its place in this list, not in its section, so it is no name.
        return (row.title && row.title.length) ? row.title.replace("​", "") : (tag.length ? "Untitled page" : "Page " + row.idx)
    }
    Connections {
        target: library
        function onChanged() { if (browser.visible) browser.reload() }
        function onTagsChanged() { if (browser.visible) browser.reload() }
    }

    ListModel { id: pages }
    ConfirmSheet { id: confirm }
    ActionSheet { id: sheet; parent: Overlay.overlay }
    Popup {
        id: mover
        objectName: "chrome"
        parent: Overlay.overlay
        x: (parent.width - width) / 2; y: 100; width: 380; padding: 14
        modal: true; focus: true
        background: Rectangle { radius: 12; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1 }
        property var moving: []
        function begin(ids) { moving = ids; targets.model = library.allSections(); open() }
        ColumnLayout {
            width: parent.width; spacing: 8
            Text { text: "Move " + mover.moving.length + (mover.moving.length === 1 ? " page to…" : " pages to…")
                   color: pal.windowText; font.pixelSize: Ui.text + 1; font.weight: Font.DemiBold }
            ListView {
                id: targets
                Layout.fillWidth: true; Layout.preferredHeight: 320
                clip: true; spacing: 2
                ScrollBar.vertical: ScrollBar { implicitWidth: Ui.scrollbar; policy: Ui.tablet ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width; implicitHeight: Ui.target; radius: 8
                    color: tap.pressed ? Qt.alpha(pal.text, 0.14) : (hov.hovered ? Qt.alpha(pal.text, 0.07) : "transparent")
                    RowLayout {
                        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
                        Rectangle { implicitWidth: 4; implicitHeight: parent.height - 16; radius: 2; color: modelData.colour }
                        Text { text: modelData.notebookName + " › " + modelData.name; color: pal.windowText
                               font.pixelSize: Ui.text; elide: Text.ElideRight; Layout.fillWidth: true; Layout.leftMargin: 8 }
                    }
                    HoverHandler { id: hov }
                    TapHandler { id: tap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: {
                        for (const id of mover.moving) library.movePageToSection(id, modelData.id)
                        browser.toast("Moved " + mover.moving.length + (mover.moving.length === 1 ? " page" : " pages"))
                        browser.selected = []
                        mover.close()
                    } }
                }
            }
        }
    }

    ColumnLayout {
        anchors { fill: parent; margins: 18 }
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Text { text: browser.tag.length ? "#" + browser.tagName : (browser.info.notebookName ? (browser.info.notebookName + " › " + browser.info.name) : "Pages")
                   color: pal.windowText; font.pixelSize: Ui.title; font.weight: Font.DemiBold; elide: Text.ElideRight
                   Layout.maximumWidth: browser.width * 0.45 }
            Text { text: pages.count + (pages.count === 1 ? " page" : " pages"); color: Qt.alpha(pal.windowText, 0.6); font.pixelSize: Ui.small }
            Item { Layout.fillWidth: true }

            component BarButton: Rectangle {
                property string label: ""
                property bool danger: false
                signal clicked()
                implicitWidth: t.implicitWidth + 24; implicitHeight: Ui.target
                radius: 9
                color: bt.pressed ? Qt.alpha(danger ? Ui.danger : pal.text, 0.28) : "transparent"
                border.color: danger ? Qt.alpha(Ui.danger, 0.6) : Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1
                Text { id: t; anchors.centerIn: parent; text: parent.label; color: parent.danger ? Ui.danger : pal.windowText; font.pixelSize: Ui.text }
                TapHandler { id: bt; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: parent.clicked() }
            }
            BarButton { visible: browser.picking; label: browser.selected.length + " selected · move"; onClicked: mover.begin(browser.selected) }
            BarButton {
                visible: browser.picking; label: "Delete"; danger: true
                onClicked: confirm.ask("Delete " + browser.selected.length + (browser.selected.length === 1 ? " page?" : " pages?"),
                                       "They go to the trash, where you can restore them for thirty days.", "Delete",
                                       function() {
                                           const ids = browser.selected.slice()
                                           for (const id of ids) library.remove("page", id)
                                           browser.selected = []
                                           browser.toast(ids.length + (ids.length === 1 ? " page" : " pages") + " moved to the trash")
                                       })
            }
            BarButton { visible: browser.picking; label: "Clear"; onClicked: browser.selected = [] }
            BarButton {
                visible: !browser.picking && browser.sectionId > 0
                label: browser.tag.length ? "New page #" + browser.tagName : "New page"
                onClicked: {
                    const id = library.createPage(browser.sectionId)
                    if (browser.tag.length) library.addPageTag(id, browser.tag)
                    browser.openPage(id)
                }
            }
            BarButton { label: "Close (Esc)"; onClicked: browser.closed() }
        }

        // Tags: narrow the pad to one subject across every notebook.
        Flow {
            objectName: "browserTags"
            visible: browser.allTags.length > 0
            Layout.fillWidth: true
            spacing: 6
            TagChip { name: "This section"; plain: true; selected: browser.tag.length === 0; onClicked: browser.tagChosen("") }
            Repeater {
                model: browser.allTags
                delegate: TagChip {
                    required property var modelData
                    name: modelData.name
                    count: modelData.count
                    selected: browser.tag.toLowerCase() === modelData.name.toLowerCase()
                    onClicked: browser.tagChosen(selected ? "" : modelData.name)
                }
            }
        }

        ColumnLayout {
            visible: pages.count === 0
            Layout.fillWidth: true; Layout.topMargin: 40; Layout.alignment: Qt.AlignHCenter; spacing: 6
            Text { text: browser.tag.length ? "No pages tagged #" + browser.tagName : "This section has no pages"; color: pal.windowText; font.pixelSize: Ui.text + 2; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
            Text { text: "Use New page above to start one."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.text; Layout.alignment: Qt.AlignHCenter }
        }
        GridView {
            id: grid
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            cellWidth: 190; cellHeight: 250
            model: pages
            ScrollBar.vertical: ScrollBar { implicitWidth: Ui.scrollbar; policy: Ui.tablet ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded }
            delegate: Item {
                id: cell
                objectName: "browserCell"
                required property var pid
                required property string title
                required property int idx
                required property string place
                width: grid.cellWidth - 10; height: grid.cellHeight - 10
                readonly property bool chosen: browser.selected.indexOf(pid) >= 0

                Rectangle {
                    anchors.fill: parent
                    radius: 10
                    color: cell.chosen ? Qt.alpha(pal.highlight, 0.22) : (cellHover.hovered ? Qt.alpha(pal.text, 0.06) : "transparent")
                    border.color: pid === browser.currentPageId ? pal.highlight : "transparent"
                    border.width: 2
                }
                ColumnLayout {
                    anchors { fill: parent; margins: 8 }
                    spacing: 6
                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        implicitWidth: 124; implicitHeight: 176
                        color: Qt.alpha(pal.base, 0.92); radius: 3
                        border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1
                        Image {
                            id: thumb
                            anchors { fill: parent; margins: 1 }
                            fillMode: Image.PreserveAspectFit; asynchronous: true; smooth: true
                            property int bust: 0
                            source: thumbnails.pathFor(cell.pid).length ? "file://" + thumbnails.pathFor(cell.pid) + "?v=" + thumb.bust : ""
                            Connections { target: thumbnails; function onChanged(pid) { if (pid === cell.pid) thumb.bust = thumbnails.version(pid) } }
                        }
                        Rectangle {
                            visible: cell.chosen
                            anchors { right: parent.right; top: parent.top; margins: 4 }
                            width: 22; height: 22; radius: 11; color: pal.highlight
                            Text { anchors.centerIn: parent; text: "✓"; color: pal.highlightedText; font.pixelSize: 13 }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: browser.tag.length ? browser.labelFor({ title: cell.title, idx: cell.idx }) : cell.idx + " · " + browser.labelFor({ title: cell.title, idx: cell.idx })
                        color: pal.windowText; font.pixelSize: Ui.small; elide: Text.ElideRight
                    }
                    Text {
                        visible: cell.place.length > 0
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: cell.place
                        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small - 1; elide: Text.ElideMiddle
                    }
                }
                HoverHandler { id: cellHover }
                property bool holdFired: false
                Timer {
                    id: holdTimer; interval: 650
                    onTriggered: {
                        cell.holdFired = true
                        const pid = cell.pid                       // values, not the delegate: the model rebuilds under us
                        sheet.title = browser.labelFor({ title: cell.title, idx: cell.idx })
                        sheet.items = [
                            { label: browser.picking ? "Add to selection" : "Select", icon: "edit-select-all", action: () => browser.toggle(pid) },
                            { label: "Duplicate", icon: "edit-copy", action: () => { const id = library.duplicatePage(pid); if (id) browser.toast("Page duplicated") } },
                            { label: "Move to…", icon: "folder", action: () => mover.begin([pid]) },
                            { label: "Open", icon: "document-open", action: () => browser.openPage(pid) },
                            { label: "Open beside", icon: "view-split", action: () => browser.openBeside(pid) },
                            { label: "Delete", icon: "edit-delete", danger: true, action: () => { library.remove("page", pid); browser.toast("Page moved to the trash") } }
                        ]
                        sheet.openFrom(cell)
                    }
                }
                TapHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
                    onPressedChanged: { if (pressed) { cell.holdFired = false; holdTimer.restart() } else holdTimer.stop() }
                    onCanceled: { holdTimer.stop(); cell.holdFired = false }
                    onTapped: {
                        holdTimer.stop()
                        if (cell.holdFired) { cell.holdFired = false; return }
                        if (browser.picking) browser.toggle(cell.pid); else browser.openPage(cell.pid)
                    }
                }
                TapHandler {
                    acceptedButtons: Qt.RightButton
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: holdTimer.triggered()
                }
                DragHandler {
                    id: drag
                    target: null
                    enabled: browser.tag.length === 0          // pages from several sections have no one order
                    onActiveChanged: {
                        if (active) return
                        // A delegate's x/y are already content coordinates: adding contentX/Y again
                        // double-counted the scroll and moved the page to the wrong place.
                        const to = grid.indexAt(cell.x + drag.centroid.position.x, cell.y + drag.centroid.position.y)
                        if (to >= 0 && to < pages.count) {
                            const from = grid.indexAt(cell.x + 4, cell.y + 4)
                            if (from >= 0 && from !== to) { library.movePage(cell.pid, to); browser.toast("Page moved to " + (to + 1)) }
                        }
                    }
                }
            }
        }
    }
}
