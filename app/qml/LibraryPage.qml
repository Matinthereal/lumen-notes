import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Effects
import Lumen

// The notebooks as covers on the desk (ADR mynotes-003), the way a shelf of real ones looks: tap a
// cover to carry on where you left off in it, hold it to rename, recolour, export or delete. The
// pages you opened last sit underneath. The drawer is still the place for sections and moving pages.
Rectangle {
    id: shelf
    objectName: "libraryPage"
    signal closed()
    signal openPage(var pageId)
    signal startNotebook(var sectionId, Item from)     // an empty notebook: its first page
    signal searchRequested()
    signal exportNotebook(var notebookId)
    signal deleteNotebook(var notebookId, string name)

    SystemPalette { id: pal }
    // The same lamp-lit desk as the window's (Main.qml), so opening this feels like looking up.
    gradient: Gradient {
        GradientStop { position: 0; color: Qt.lighter(pal.window, Ui.dark ? 1.35 : 1.035) }
        GradientStop { position: 0.55; color: pal.window }
    }
    // A Rectangle accepts no buttons, so without this a drag here would draw ink on the page behind.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    focus: visible
    Keys.onEscapePressed: shelf.closed()
    Keys.onPressed: (e) => { if (e.key === Qt.Key_N && (e.modifiers & Qt.ControlModifier)) { shelf.askNewNotebook(); e.accepted = true } }
    function askNewNotebook() { bookSheet.openNew() }

    property var books: []
    property var recent: []
    property bool entered: false          // drives the covers' arrival, once per opening
    function reload() {
        books = library.notebooks()
        recent = library.recentPages(8)
        for (const p of recent) thumbnails.ensure(p.id)
    }
    onVisibleChanged: {
        entered = false
        if (visible) { reload(); Qt.callLater(() => shelf.entered = true) }
    }
    Connections {
        target: library
        function onChanged() { if (shelf.visible) shelf.reload() }
    }

    function edited(secs) {
        if (!secs) return "Empty"
        const then = new Date(secs * 1000), now = new Date()
        const day = (d) => new Date(d.getFullYear(), d.getMonth(), d.getDate()).getTime()
        const days = Math.round((day(now) - day(then)) / 86400000)
        if (days <= 0) return "Edited today"
        if (days === 1) return "Edited yesterday"
        if (days < 7) return "Edited " + days + " days ago"
        return "Edited " + then.toLocaleDateString(Qt.locale(), "d MMM")
    }
    function open(book, from) {
        if (book.lastPageId > 0) { shelf.openPage(book.lastPageId); return }
        const secs = library.sections(book.id)
        shelf.startNotebook(secs.length ? secs[0].id : library.createSection(book.id, "Notes"), from)
    }

    readonly property int side: width < 700 ? Ui.px(16) : Ui.px(40)
    readonly property int gap: width < 700 ? Ui.px(16) : Ui.px(26)

    Flickable {
        id: flick
        anchors.fill: parent
        contentHeight: column.implicitHeight + Ui.px(40) + Ui.keyboardInset
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { width: Ui.scrollbar }

        ColumnLayout {
            id: column
            x: shelf.side; y: Ui.px(24)
            width: flick.width - 2 * shelf.side
            spacing: Ui.px(28)

            RowLayout {
                Layout.fillWidth: true
                spacing: Ui.px(12)
                Rectangle {
                    objectName: "libraryBack"
                    implicitWidth: Ui.target; implicitHeight: Ui.target; radius: height / 2
                    color: backTap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : (backHover.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent")
                    Accessible.role: Accessible.Button
                    Accessible.name: "Back to the page"
                    Icon { anchors.centerIn: parent; name: "go-previous" }
                    HoverHandler { id: backHover }
                    TapHandler { id: backTap; onTapped: shelf.closed() }
                }
                Text {
                    text: "Notebooks"
                    font.family: Ui.titleFont; font.weight: Font.Medium
                    font.pixelSize: shelf.width < 700 ? Ui.px(30) : Ui.px(40)
                    color: pal.windowText
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                // Search lives in its palette; this is the way in from here, shaped like a field.
                Rectangle {
                    visible: shelf.width >= 760
                    implicitWidth: Ui.px(240); implicitHeight: Ui.target - 2; radius: height / 2
                    color: Ui.chrome; border.width: 1; border.color: Ui.hair
                    Accessible.role: Accessible.Button
                    Accessible.name: "Search everything"
                    RowLayout {
                        anchors { fill: parent; leftMargin: 14; rightMargin: 14 }
                        spacing: 8
                        Icon { name: "edit-find"; opacity: 0.7 }
                        Text { text: "Search everything"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.text; Layout.fillWidth: true; elide: Text.ElideRight }
                    }
                    TapHandler { onTapped: shelf.searchRequested() }
                }
                LButton {
                    objectName: "libraryNewNotebook"
                    primary: true
                    text: shelf.width < 480 ? "New" : "New notebook"
                    onClicked: bookSheet.openNew(this)
                }
            }

            GridLayout {
                id: grid
                Layout.fillWidth: true
                readonly property int cols: Math.max(2, Math.floor((column.width + shelf.gap) / (Ui.px(190) + shelf.gap)))
                readonly property real cellW: (column.width - (cols - 1) * shelf.gap) / cols
                columns: cols
                rowSpacing: shelf.gap; columnSpacing: shelf.gap

                Repeater {
                    model: shelf.books
                    delegate: Item {
                        id: book
                        required property var modelData
                        required property int index
                        objectName: "notebookCover"
                        Layout.preferredWidth: grid.cellW
                        Layout.preferredHeight: cover.height + 10 + caption.implicitHeight
                        Accessible.role: Accessible.Button
                        Accessible.name: "Notebook " + modelData.name

                        // Arrive once per opening, a few at a time; reloads after an edit just appear.
                        Layout.alignment: Qt.AlignTop
                        readonly property int stagger: Ui.reduceMotion ? 0 : Math.min(index, 8) * 35
                        opacity: shelf.entered ? 1 : 0
                        Behavior on opacity { enabled: !Ui.instant
                            SequentialAnimation { PauseAnimation { duration: book.stagger }
                                                  NumberAnimation { duration: Ui.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Ui.emphasized } } }
                        transform: [
                            Translate {
                                y: shelf.entered || Ui.reduceMotion ? 0 : 14
                                Behavior on y { enabled: !Ui.instant
                                    SequentialAnimation { PauseAnimation { duration: book.stagger }
                                                          NumberAnimation { duration: Ui.slow; easing.type: Easing.BezierSpline; easing.bezierCurve: Ui.emphasized } } }
                            },
                            Translate {     // a lift under the pointer, where there is one
                                y: coverHover.hovered && !Ui.reduceMotion ? -3 : 0
                                Behavior on y { enabled: !Ui.instant; NumberAnimation { duration: Ui.quick; easing.type: Easing.BezierSpline; easing.bezierCurve: Ui.standard } }
                            }
                        ]

                        Rectangle {
                            id: cover
                            width: parent.width; height: Math.round(width * 1.18)
                            radius: 16
                            color: book.modelData.colour && book.modelData.colour.length ? book.modelData.colour : "#5C6B7A"
                            readonly property color ink: Ui.onAccent(color)
                            scale: coverTap.pressed && !Ui.reduceMotion ? 0.97 : 1
                            Behavior on scale { enabled: !Ui.instant; SpringAnimation { spring: 3; damping: 0.4; epsilon: 0.005 } }
                            // The shadow takes the cover's own colour, as a coloured object's does on paper.
                            RectangularShadow {
                                anchors.fill: parent; z: -1
                                radius: parent.radius
                                offset.y: 14; blur: 34; spread: -6
                                color: Qt.alpha(Qt.darker(cover.color, 2.4), Ui.dark ? 0.6 : 0.32)
                                cached: true
                            }
                            // The spine, and the fold where it meets the board.
                            Rectangle {
                                width: 16; height: parent.height
                                topLeftRadius: 16; bottomLeftRadius: 16; topRightRadius: 0; bottomRightRadius: 0
                                color: Qt.rgba(0, 0, 0, 0.14)
                            }
                            Rectangle { x: 16; width: 1; height: parent.height; color: Qt.rgba(1, 1, 1, 0.12) }
                            Text {
                                visible: text.length > 0
                                x: 34; y: 22; width: parent.width - 34 - 18
                                text: book.modelData.board || ""
                                font.pixelSize: Ui.px(11); font.weight: Font.DemiBold; font.letterSpacing: 1.2
                                font.capitalization: Font.AllUppercase
                                color: Qt.alpha(cover.ink, 0.8)
                                elide: Text.ElideRight
                            }
                            Text {
                                x: 34; width: parent.width - 34 - 18
                                anchors { bottom: parent.bottom; bottomMargin: 22 }
                                text: book.modelData.name
                                font.family: Ui.titleFont; font.weight: Font.Medium
                                font.pixelSize: Math.max(Ui.px(18), Math.min(Ui.px(30), cover.width * 0.13))
                                lineHeight: 1.05
                                color: cover.ink
                                wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                            }
                            HoverHandler { id: coverHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler {
                                id: coverTap
                                acceptedButtons: Qt.LeftButton
                                gesturePolicy: TapHandler.ReleaseWithinBounds
                                onTapped: shelf.open(book.modelData, cover)
                                onLongPressed: bookMenu.show(book.modelData, cover)
                            }
                            TapHandler { acceptedButtons: Qt.RightButton; onTapped: bookMenu.show(book.modelData, cover) }
                        }
                        Text {
                            id: caption
                            anchors { top: cover.bottom; topMargin: 10; left: parent.left; right: parent.right; leftMargin: 2 }
                            text: book.modelData.pageCount > 0
                                  ? shelf.edited(book.modelData.modified) + " · " + book.modelData.pageCount + (book.modelData.pageCount === 1 ? " page" : " pages")
                                  : "Empty · tap to start"
                            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
                            elide: Text.ElideRight
                        }
                    }
                }

                // The one that is not a notebook yet.
                Item {
                    objectName: "libraryNewCover"
                    Layout.preferredWidth: grid.cellW
                    Layout.preferredHeight: Math.round(grid.cellW * 1.18)
                    Layout.alignment: Qt.AlignTop
                    Accessible.role: Accessible.Button
                    Accessible.name: "New notebook"
                    Rectangle {
                        anchors.fill: parent; radius: 16
                        color: newTap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : (newHover.hovered ? Qt.alpha(pal.text, 0.04) : "transparent")
                        border.width: 1.5; border.color: Qt.alpha(pal.text, 0.18)
                        ColumnLayout {
                            anchors.centerIn: parent
                            spacing: 10
                            Icon { name: "list-add"; Layout.alignment: Qt.AlignHCenter; colour: Qt.alpha(pal.windowText, Ui.mutedAlpha) }
                            Text { text: "New notebook"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.text; font.weight: Font.DemiBold }
                        }
                    }
                    HoverHandler { id: newHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { id: newTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: bookSheet.openNew(parent) }
                }
            }

            ColumnLayout {
                visible: shelf.recent.length > 0
                Layout.fillWidth: true
                spacing: Ui.px(14)
                Text {
                    text: "Recent pages"
                    color: Qt.alpha(pal.windowText, Ui.mutedAlpha)
                    font.pixelSize: Ui.px(12); font.weight: Font.DemiBold; font.letterSpacing: 1; font.capitalization: Font.AllUppercase
                }
                GridLayout {
                    id: recentGrid
                    Layout.fillWidth: true
                    readonly property int cols: Math.max(3, Math.floor((column.width + shelf.gap) / (Ui.px(130) + shelf.gap)))
                    readonly property real cellW: (column.width - (cols - 1) * shelf.gap) / cols
                    columns: cols
                    columnSpacing: shelf.gap; rowSpacing: shelf.gap
                    Repeater {
                        model: shelf.recent.slice(0, recentGrid.cols)
                        delegate: ColumnLayout {
                            id: rp
                            required property var modelData
                            objectName: "recentPage"
                            // A nested layout fills by default when a child does; a page is its own size.
                            Layout.fillWidth: false
                            Layout.preferredWidth: recentGrid.cellW; Layout.maximumWidth: recentGrid.cellW
                            Layout.alignment: Qt.AlignTop
                            spacing: 8
                            Accessible.role: Accessible.Button
                            Accessible.name: "Open " + (modelData.title || "Untitled page")
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: Math.round(width * 1.3)
                                radius: 8
                                color: pal.base
                                scale: rpTap.pressed && !Ui.reduceMotion ? 0.97 : 1
                                Behavior on scale { enabled: !Ui.instant; SpringAnimation { spring: 3; damping: 0.4; epsilon: 0.005 } }
                                Elevation { level: 1 }
                                Image {
                                    id: thumb
                                    anchors { fill: parent; margins: 1 }
                                    fillMode: Image.PreserveAspectCrop; verticalAlignment: Image.AlignTop
                                    asynchronous: true; smooth: true
                                    property int bust: 0
                                    source: thumbnails.pathFor(rp.modelData.id).length ? "file://" + thumbnails.pathFor(rp.modelData.id) + "?v=" + thumb.bust : ""
                                    Connections { target: thumbnails; function onChanged(pid) { if (pid === rp.modelData.id) thumb.bust = thumbnails.version(pid) } }
                                }
                                // The notebook it belongs to, as a ribbon of its colour.
                                Rectangle {
                                    anchors { right: parent.right; top: parent.top; rightMargin: 12 }
                                    width: 8; height: 18
                                    bottomLeftRadius: 2; bottomRightRadius: 2
                                    color: rp.modelData.colour || "#5C6B7A"
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { id: rpTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: shelf.openPage(rp.modelData.id) }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: rp.modelData.title && rp.modelData.title.length ? rp.modelData.title.replace("​", "") : "Untitled page"
                                color: pal.windowText; font.pixelSize: Ui.text; font.weight: Font.Medium; elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true; Layout.topMargin: -6
                                text: rp.modelData.notebookName + " › " + rp.modelData.sectionName
                                color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; elide: Text.ElideRight
                            }
                        }
                    }
                }
            }
        }
    }

    ConfirmSheet { id: confirm }
    ActionSheet {
        id: bookMenu
        parent: Overlay.overlay
        property var book: ({})
        function show(b, from) {
            book = b
            title = b.name
            items = [
                { label: "Rename or recolour", icon: "edit-rename", action: () => bookSheet.openEdit(b, from) },
                { label: "Export notebook…", icon: "document-export", action: () => shelf.exportNotebook(b.id) },
                { label: "Delete notebook", icon: "edit-delete", danger: true,
                  action: () => confirm.ask("Delete “" + b.name + "”?", "Its pages go to Recently deleted, where you can bring them back.", "Delete",
                                            () => shelf.deleteNotebook(b.id, b.name)) }
            ]
            openFrom(from)
        }
    }

    // New notebook, or a notebook's name and colour: one small card.
    Popup {
        id: bookSheet
        objectName: "notebookSheet"
        parent: Overlay.overlay
        modal: true; focus: true; dim: true
        width: Math.min(parent.width - 32, Ui.px(420))
        x: (parent.width - width) / 2
        y: Math.max(16, Math.min((parent.height - height) / 2, parent.height - height - Ui.keyboardInset - 16))
        padding: Ui.px(20)
        background: Card {}
        enter: PopIn {}
        exit: PopOut {}
        property var editing: null         // null: a new one
        property string colour: "#3558D8"
        readonly property var colours: ["#3558D8", "#B85A2F", "#2F7A6B", "#5B2A86", "#9D2C4A", "#0F7C8A", "#8A6A1F", "#5C6B7A"]
        function openNew() { editing = null; nameField.text = ""; boardField.text = ""; colour = colours[shelf.books.length % colours.length]; open(); nameField.forceActiveFocus() }
        function openEdit(b) { editing = b; nameField.text = b.name; boardField.text = b.board || ""; colour = b.colour || colours[0]; open(); nameField.forceActiveFocus() }
        function commit() {
            const name = nameField.text.trim()
            if (!name.length) return
            if (editing) {
                if (name !== editing.name) library.rename("notebook", editing.id, name)
                if (colour !== editing.colour) library.setNotebookColour(editing.id, colour)
            } else {
                const id = library.createNotebook(name, colour, boardField.text.trim())
                library.createSection(id, "Notes")
            }
            close()
        }

        contentItem: ColumnLayout {
            spacing: Ui.px(14)
            Text {
                text: bookSheet.editing ? "Notebook" : "New notebook"
                font.family: Ui.titleFont; font.weight: Font.Medium; font.pixelSize: Ui.px(24)
                color: pal.windowText
            }
            // What it will look like, as it is typed.
            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: Ui.px(96); implicitHeight: Math.round(implicitWidth * 1.18); radius: 10
                color: bookSheet.colour
                Behavior on color { ColorAnimation { duration: Ui.quick } }
                RectangularShadow { anchors.fill: parent; z: -1; radius: parent.radius; offset.y: 8; blur: 20; spread: -4
                                    color: Qt.alpha(Qt.darker(parent.color, 2.4), Ui.dark ? 0.6 : 0.3); cached: true }
                Rectangle { width: 9; height: parent.height; topLeftRadius: 10; bottomLeftRadius: 10; topRightRadius: 0; bottomRightRadius: 0; color: Qt.rgba(0, 0, 0, 0.14) }
                Text {
                    x: 17; width: parent.width - 24
                    anchors { bottom: parent.bottom; bottomMargin: 10 }
                    text: nameField.text.length ? nameField.text : "Name"
                    font.family: Ui.titleFont; font.weight: Font.Medium; font.pixelSize: Ui.px(14)
                    color: Ui.onAccent(bookSheet.colour); opacity: nameField.text.length ? 1 : 0.6
                    wrapMode: Text.Wrap; maximumLineCount: 3; elide: Text.ElideRight
                }
            }
            LField {
                id: nameField
                objectName: "notebookName"
                Layout.fillWidth: true
                placeholderText: "Name, like Maths"
                onAccepted: bookSheet.commit()
            }
            LField {
                id: boardField
                visible: !bookSheet.editing
                Layout.fillWidth: true
                placeholderText: "Exam board (optional)"
                onAccepted: bookSheet.commit()
            }
            Flow {
                id: swatches
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: bookSheet.colours
                    delegate: Item {
                        required property string modelData
                        // All eight on one row: an orphan on a second row reads as a different kind.
                        width: Math.max(36, Math.min(Ui.target - 6, Math.floor((swatches.width - 7 * swatches.spacing) / 8))); height: width
                        Accessible.role: Accessible.Button
                        Accessible.name: "Cover colour " + modelData
                        Rectangle { anchors.centerIn: parent; width: parent.width - 10; height: width; radius: width / 2; color: modelData }
                        Rectangle {
                            anchors.centerIn: parent; visible: bookSheet.colour.toLowerCase() === modelData.toLowerCase()
                            width: parent.width; height: width; radius: width / 2
                            color: "transparent"; border.width: 2; border.color: pal.highlight
                        }
                        TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: bookSheet.colour = modelData }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                spacing: 8
                Item { Layout.fillWidth: true }
                LButton { text: "Cancel"; flat: true; onClicked: bookSheet.close() }
                LButton {
                    objectName: "notebookSheetDone"
                    primary: true
                    text: bookSheet.editing ? "Done" : "Create"
                    enabled: nameField.text.trim().length > 0
                    onClicked: bookSheet.commit()
                }
            }
        }
    }
}
