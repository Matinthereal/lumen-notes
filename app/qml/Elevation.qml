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
    // A cached shadow is an offscreen texture of its own size, drawn again whenever that size
    // changes. The page's shadow has the size of the zoomed page, so every zoom step made a new
    // texture of the whole page (about 15 million pixels at 400%, with the window's 4x MSAA) and
    // zooming in got slower the further it went. Drawn directly, only what is on screen is shaded.
    // A typed page's sheet grows as you type, so it is left uncached too.
    cached: level !== 0
}
