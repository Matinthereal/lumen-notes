import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Everything about the open page that is not on the page itself: its headings, its tags, and what
// links here.
Rectangle {
    id: panel
    objectName: "chrome"
    property var pageId: 0
    signal openPage(var pageId)
    signal showTagged(string tag)
    signal goToHeading(var blockId, string text)
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    SystemPalette { id: pal }
    color: pal.window

    property var backlinks: []
    property var outline: []
    property var tags: []
    property var allTags: []
    function reload() {
        backlinks = pageId > 0 ? library.backlinks(pageId) : []
        outline = pageId > 0 ? textBlocks.outline(pageId) : []
        reloadTags()
    }
    function reloadTags() {
        tags = pageId > 0 ? library.pageTags(pageId) : []
        allTags = library.tags()
    }
    function addTag(name) {
        const pid = panel.pageId
        const had = library.pageTags(pid).map(t => t.name.toLowerCase())
        const id = library.addPageTag(pid, name)
        tagField.text = ""
        if (!id) return
        const tag = library.pageTags(pid).find(t => t.id === id)
        if (tag && had.indexOf(tag.name.toLowerCase()) < 0)
            panel.toastAction("Tagged #" + tag.name, "Undo", function() { library.removePageTag(pid, id) })
    }
    function removeTag(tag) {
        const pid = panel.pageId, id = tag.id, name = tag.name
        library.removePageTag(pid, id)
        panel.toastAction("Removed #" + name, "Undo", function() { library.addPageTag(pid, name) })
    }
    // Tags you already use elsewhere, matching what is typed, that this page does not have yet.
    readonly property var suggestions: {
        const typed = tagField.text.trim().replace(/^#+/, "").toLowerCase()
        const mine = tags.map(t => t.name.toLowerCase())
        return allTags.filter(t => mine.indexOf(t.name.toLowerCase()) < 0 && (!typed.length || t.name.toLowerCase().indexOf(typed) >= 0)).slice(0, 8)
    }
    onPageIdChanged: reload()
    Component.onCompleted: reload()
    Connections {
        target: library
        function onLinksChanged() { panel.reload() }
        function onChanged() { panel.reload() }
        function onTagsChanged() { panel.reloadTags() }
    }
    Connections {
        target: textBlocks
        function onChanged(pid) { if (pid === panel.pageId) panel.outline = textBlocks.outline(pid) }
    }

    component Caption: Text {
        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; font.capitalization: Font.AllUppercase
        Layout.leftMargin: 4; Layout.topMargin: 10
    }
    component Hint: Text {
        color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
        wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 4
    }

    Flickable {
        anchors.fill: parent
        contentHeight: body.implicitHeight + 24
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { implicitWidth: Ui.scrollbar }

        ColumnLayout {
            id: body
            x: 12; y: 12
            width: parent.width - 24
            spacing: 4

            Text { text: "This page"; color: pal.windowText; font.pixelSize: 14; font.weight: Font.DemiBold }
            Hint { visible: panel.pageId === 0; text: "Open a page to see its tags and what links to it." }

            Caption { visible: panel.pageId > 0; text: "Outline" }
            Hint {
                visible: panel.pageId > 0 && panel.outline.length === 0
                text: "Headings on this page show up here. Make one with H1, H2 or H3."
            }
            Repeater {
                model: panel.pageId > 0 ? panel.outline : []
                delegate: Rectangle {
                    required property var modelData
                    objectName: "outlineRow"
                    Layout.fillWidth: true
                    implicitHeight: Ui.target - 6
                    radius: 8
                    color: headTap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : (headHover.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent")
                    Accessible.role: Accessible.Link
                    Accessible.name: modelData.text
                    Text {
                        anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter
                                  leftMargin: 10 + (modelData.level - 1) * 14; rightMargin: 10 }
                        text: modelData.text
                        color: pal.windowText
                        font.pixelSize: modelData.level === 1 ? Ui.text + 1 : Ui.text
                        font.weight: modelData.level === 1 ? Font.DemiBold : Font.Normal
                        opacity: modelData.level === 3 ? Ui.mutedAlpha : 1
                        elide: Text.ElideRight
                    }
                    HoverHandler { id: headHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { id: headTap; gesturePolicy: TapHandler.ReleaseWithinBounds
                                 onTapped: panel.goToHeading(modelData.blockId, modelData.text) }
                }
            }

            Caption { visible: panel.pageId > 0; text: "Tags" }
            Flow {
                visible: panel.pageId > 0 && panel.tags.length > 0
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: panel.tags
                    delegate: TagChip {
                        required property var modelData
                        name: modelData.name
                        removable: true
                        onClicked: panel.showTagged(modelData.name)
                        onRemoveClicked: { const t = { id: modelData.id, name: modelData.name }; Qt.callLater(() => panel.removeTag(t)) }
                    }
                }
            }
            TextField {
                id: tagField
                objectName: "tagField"
                visible: panel.pageId > 0
                Layout.fillWidth: true
                Layout.topMargin: 4
                implicitHeight: Ui.target - 4
                placeholderText: "Add a tag"
                font.pixelSize: Ui.text
                leftPadding: 12
                background: Rectangle { radius: 8; color: pal.base; border.color: tagField.activeFocus ? pal.highlight : Qt.alpha(pal.text, Ui.borderAlpha) }
                onAccepted: if (text.trim().length) panel.addTag(text)
            }
            Flow {
                visible: panel.pageId > 0 && panel.suggestions.length > 0
                Layout.fillWidth: true
                Layout.topMargin: 2
                spacing: 6
                Repeater {
                    model: panel.suggestions
                    delegate: TagChip {
                        required property var modelData
                        name: modelData.name
                        adding: true
                        onClicked: { const n = modelData.name; Qt.callLater(() => panel.addTag(n)) }
                    }
                }
            }
            Hint {
                visible: panel.pageId > 0
                text: "Or lasso a handwritten word and tap # Tag on the bar that appears. Tap a tag to see every page that has it."
                Layout.topMargin: 2
            }

            Caption { visible: panel.pageId > 0; text: "Linked from" }
            Hint {
                visible: panel.pageId > 0 && panel.backlinks.length === 0
                text: "Nothing links here yet. Type [[ in any text to link one page to another."
            }
            Repeater {
                model: panel.pageId > 0 ? panel.backlinks : []
                delegate: BacklinkRow {
                    required property var modelData
                    Layout.fillWidth: true
                    link: modelData
                    onOpen: panel.openPage(modelData.id)
                }
            }
        }
    }
}
