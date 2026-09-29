import QtQuick
import QtQuick.Controls.Basic
import Lumen

// Lumen's drop-down (ADR mynotes-003): a capsule like the buttons, a chevron, and its list on a
// Card that pops in, the chosen row in the accent. A drop-in for ComboBox.
ComboBox {
    id: control
    SystemPalette { id: pal }
    implicitHeight: Math.max(Ui.target - 4, implicitContentHeight + topPadding + bottomPadding)
    leftPadding: Ui.px(16)
    rightPadding: Ui.px(16) + Ui.icon

    background: Rectangle {
        radius: height / 2
        color: control.down ? Qt.alpha(pal.text, 0.14) : control.hovered ? Qt.alpha(pal.text, 0.09) : Qt.alpha(pal.text, 0.06)
        border.width: control.visualFocus ? 2 : 0
        border.color: pal.highlight
        Behavior on color { ColorAnimation { duration: Ui.quick } }
    }
    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? pal.text : Qt.alpha(pal.text, 0.4)
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: Icon {
        x: control.width - width - Ui.px(12)
        y: (control.height - height) / 2
        implicitWidth: Ui.icon - 4; implicitHeight: Ui.icon - 4
        name: "go-next"
        rotation: control.popup.visible ? -90 : 90
        colour: Qt.alpha(pal.windowText, 0.7)
        Behavior on rotation { enabled: !Ui.instant && !Ui.reduceMotion; NumberAnimation { duration: Ui.quick; easing.type: Easing.BezierSpline; easing.bezierCurve: Ui.standard } }
    }
    delegate: ItemDelegate {
        id: row
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: Ui.target - 4
        leftPadding: Ui.px(12); rightPadding: Ui.px(12)
        text: control.textAt(index)
        font: control.font
        highlighted: control.highlightedIndex === index
        background: Rectangle {
            radius: Ui.radiusSm
            color: row.highlighted ? Ui.accentSoft : (row.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent")
        }
        contentItem: Text {
            text: row.text
            font: row.font
            color: control.currentIndex === row.index ? pal.highlight : pal.text
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    popup: Popup {
        y: control.height + 6
        width: Math.max(control.width, Ui.px(200))
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, Ui.px(360))
        padding: 6
        background: Card {}
        enter: PopIn {}
        exit: PopOut {}
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            spacing: 2
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
