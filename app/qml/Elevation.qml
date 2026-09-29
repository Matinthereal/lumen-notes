import QtQuick
import QtQuick.Effects
import Lumen

// The soft shadow under a floating surface (ADR mynotes-003): a distance-field shadow, cheap on a
// tablet's GPU, never a blur pass. Put it inside the surface, filling it; it draws behind.
RectangularShadow {
    property int level: 1              // 0: the page · 1: bars and chips · 2: menus and popovers
    anchors.fill: parent
    z: -1
    radius: parent && parent.radius !== undefined ? parent.radius : 0
    offset.y: level === 2 ? 16 : (level === 0 ? 10 : 8)
    blur: level === 2 ? 48 : (level === 0 ? 30 : 26)
    spread: -4
    color: Qt.alpha(Ui.shadow, Ui.dark ? 0.5 : (level === 2 ? 0.2 : 0.13))
    cached: true
}
