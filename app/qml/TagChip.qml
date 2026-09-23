import QtQuick
import QtQuick.Layouts
import Lumen

// A tag, as a rounded chip. Tap it to use it; the × (when there is one) takes it off.
Rectangle {
    id: chip
    property string name: ""
    property int count: -1              // shown after the name when known
    property bool selected: false
    property bool removable: false
    property bool adding: false         // a suggestion: "+ name"
    property bool plain: false          // not a tag at all ("This section"): no #
    signal clicked()
    signal removeClicked()
    objectName: "tagChip"
    implicitHeight: Ui.target - 6
    implicitWidth: row.implicitWidth + (removable ? 16 : 26)
    radius: height / 2
    color: selected ? Qt.alpha(pal.highlight, 0.30)
         : (tap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : Qt.alpha(pal.text, hover.hovered ? 0.10 : 0.06))
    border.color: selected ? pal.highlight : "transparent"
    border.width: selected ? 1 : 0
    SystemPalette { id: pal }
    Accessible.role: Accessible.Button
    Accessible.name: (plain ? "" : "#") + name

    HoverHandler { id: hover }
    TapHandler { id: tap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: chip.clicked() }

    RowLayout {
        id: row
        anchors { left: parent.left; leftMargin: 12; verticalCenter: parent.verticalCenter }
        spacing: 4
        Text {
            text: (chip.adding ? "+ " : chip.plain ? "" : "#") + chip.name
            color: pal.windowText; font.pixelSize: Ui.small + 1
            elide: Text.ElideRight
            Layout.maximumWidth: 220
        }
        Text {
            visible: chip.count >= 0
            text: chip.count
            color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
        }
        // The remove target is the full height of the chip, not the glyph.
        Item {
            visible: chip.removable
            objectName: "tagChipRemove"
            implicitWidth: chip.height - 4; implicitHeight: chip.height - 4
            Accessible.role: Accessible.Button
            Accessible.name: "Remove tag " + chip.name
            Rectangle {
                anchors.centerIn: parent
                width: 22; height: 22; radius: 11
                color: removeTap.pressed ? Qt.alpha(pal.text, 0.25) : (removeHover.hovered ? Qt.alpha(pal.text, 0.14) : "transparent")
                Icon { anchors.centerIn: parent; name: "window-close"; implicitWidth: 12; implicitHeight: 12; weight: 2.2 }
            }
            HoverHandler { id: removeHover }
            TapHandler { id: removeTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: chip.removeClicked() }
        }
    }
}
