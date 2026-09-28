import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// "Handwritten or typed?" — asked only when the maker takes notes both ways. It opens beside the
// + that asked, the kind used last has the focus (so Enter repeats it), and H or T pick directly.
Popup {
    id: chooser
    objectName: "newPageChooser"
    parent: Overlay.overlay
    modal: true
    focus: true
    dim: false
    padding: 12
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    SystemPalette { id: pal }

    property string inkSize: "a4"
    property string preferred: "a4"
    property var onPick: null
    property Item source: null
    property real wantX: 0
    property real wantY: 0

    function ask(from, pick) {
        onPick = pick
        source = from || null
        inkSize = library.inkPageSize()
        preferred = library.setting("page.lastKind", "") === "typed" ? "typed" : inkSize
        open()
    }
    function choose(kind) {
        const pick = onPick
        onPick = null
        library.setSetting("page.lastKind", kind === "typed" ? "typed" : "ink")
        close()
        if (pick) pick(kind)
    }
    // Under the control that asked, above it when there is no room, centred high when a key asked.
    function place() {
        if (!parent) return
        const h = height > 0 ? height : implicitHeight, w = width > 0 ? width : implicitWidth
        if (!source) { wantX = (parent.width - w) / 2; wantY = parent.height * 0.2; return }
        const at = source.mapToItem(parent, 0, 0)
        let ny = at.y + source.height + 8
        if (ny + h > parent.height - 8) ny = at.y - h - 8
        wantX = at.x + source.width / 2 - w / 2
        wantY = ny
    }
    x: parent ? Math.max(8, Math.min(wantX, parent.width - width - 8)) : wantX
    y: parent ? Math.max(8, Math.min(wantY, parent.height - height - 8)) : wantY
    onAboutToShow: place()
    onOpened: { place(); (preferred === "typed" ? typedTile : inkTile).forceActiveFocus() }
    onClosed: { onPick = null; source = null }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Ui.quick; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Ui.quick; easing.type: Easing.OutCubic }
    }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: 90; easing.type: Easing.InCubic } }

    background: Rectangle { radius: Ui.radiusLg; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1 }

    component Tile: Rectangle {
        id: tile
        property string kind
        property string icon
        property string label
        property string detail
        property string key
        activeFocusOnTab: true
        implicitWidth: 176; implicitHeight: 128
        radius: Ui.radius
        color: tileTap.pressed ? Qt.alpha(pal.highlight, 0.24)
             : (tile.activeFocus || tileHover.hovered) ? Qt.alpha(pal.highlight, 0.12) : Qt.alpha(pal.text, 0.04)
        border.width: tile.activeFocus ? 2 : 1
        border.color: tile.activeFocus ? pal.highlight : Qt.alpha(pal.text, Ui.hairline)
        Accessible.role: Accessible.Button
        Accessible.name: label + " — " + detail
        Accessible.onPressAction: chooser.choose(tile.kind)
        Keys.onReturnPressed: chooser.choose(tile.kind)
        Keys.onEnterPressed: chooser.choose(tile.kind)
        Keys.onSpacePressed: chooser.choose(tile.kind)
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 6
            Icon { name: tile.icon; implicitWidth: 32; implicitHeight: 32; weight: 1.6
                   colour: tile.activeFocus ? pal.highlight : pal.windowText; Layout.alignment: Qt.AlignHCenter }
            Text { text: tile.label; color: pal.windowText; font.pixelSize: Ui.text + 1; font.weight: Font.DemiBold
                   Layout.alignment: Qt.AlignHCenter; Layout.topMargin: 4 }
            Text { text: tile.detail; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
                   Layout.alignment: Qt.AlignHCenter }
        }
        Rectangle {                                   // the key that picks it, for keyboard people
            anchors { top: parent.top; right: parent.right; margins: 8 }
            width: 20; height: 20; radius: 5
            color: "transparent"; border.width: 1; border.color: Qt.alpha(pal.text, Ui.hairline)
            Text { anchors.centerIn: parent; text: tile.key; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small - 1 }
        }
        HoverHandler { id: tileHover }
        TapHandler { id: tileTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: chooser.choose(tile.kind) }
    }

    contentItem: ColumnLayout {
        spacing: 10
        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_H) { chooser.choose(chooser.inkSize); e.accepted = true }
            else if (e.key === Qt.Key_T) { chooser.choose("typed"); e.accepted = true }
        }
        Text { text: "New page"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; Layout.leftMargin: 2 }
        RowLayout {
            spacing: 10
            Tile { id: inkTile; objectName: "chooseHandwritten"; kind: chooser.inkSize; icon: "draw-freehand"; key: "H"
                   label: "Handwritten"; detail: chooser.inkSize === "infinite" ? "Pen · endless page" : "Pen · A4 sheet"
                   KeyNavigation.right: typedTile; KeyNavigation.tab: typedTile }
            Tile { id: typedTile; objectName: "chooseTyped"; kind: "typed"; icon: "input-keyboard"; key: "T"
                   label: "Typed"; detail: "Keyboard · text page"
                   KeyNavigation.left: inkTile; KeyNavigation.tab: inkTile }
        }
        Text { text: "Only ever use one? Settings › Notes stops this asking."
               color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small - 1; Layout.leftMargin: 2
               Layout.maximumWidth: 362; wrapMode: Text.WordWrap }
    }
}
