import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// The confirmation for things that cannot be undone. Everything reversible gets a toast instead;
// this exists only where the data is genuinely gone afterwards.
Popup {
    id: sheet
    objectName: "chrome"
    property string title: ""
    property string body: ""
    property string confirmLabel: "Delete"
    property var onConfirmed: null
    SystemPalette { id: pal }
    parent: Overlay.overlay
    x: (parent.width - width) / 2
    y: Math.round(parent.height / 3)
    width: Math.min(420, parent.width - 48)
    modal: true
    focus: true                                    // so Escape closes it
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { radius: 14; color: pal.window; border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1 }

    function ask(t, b, label, fn) { title = t; body = b; confirmLabel = label || "Delete"; onConfirmed = fn; open() }

    ColumnLayout {
        width: parent.width
        spacing: 12
        Text { text: sheet.title; color: pal.windowText; font.pixelSize: Ui.text + 3; font.weight: Font.DemiBold; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        Text { text: sheet.body; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.text; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        RowLayout {
            Layout.topMargin: 6
            Layout.fillWidth: true
            spacing: 10
            Item { Layout.fillWidth: true }
            Rectangle {
                objectName: "confirmCancel"
                implicitWidth: cancelText.implicitWidth + 30; implicitHeight: Ui.target + 4
                radius: 9; color: cancelTap.pressed ? Qt.alpha(pal.text, 0.14) : "transparent"
                border.color: Qt.alpha(pal.text, Ui.borderAlpha); border.width: 1
                Text { id: cancelText; anchors.centerIn: parent; text: "Cancel"; color: pal.windowText; font.pixelSize: Ui.text }
                TapHandler { id: cancelTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: sheet.close() }
            }
            Rectangle {
                objectName: "confirmGo"
                implicitWidth: goText.implicitWidth + 30; implicitHeight: Ui.target + 4
                radius: 9; color: goTap.pressed ? Qt.darker(Ui.danger, 1.2) : Ui.danger
                Text { id: goText; anchors.centerIn: parent; text: sheet.confirmLabel; color: "white"; font.pixelSize: Ui.text; font.weight: Font.DemiBold }
                TapHandler { id: goTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { const fn = sheet.onConfirmed; Qt.callLater(function() { sheet.close(); if (fn) fn() }) } }
            }
        }
    }
}
