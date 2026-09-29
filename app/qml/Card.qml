import QtQuick
import Lumen

// The background of a menu, popover or sheet (ADR mynotes-003): raised paper, a corner the rows
// inside nest into (22 − 8 padding = the rows' 14), a hairline and the deepest of the three shadows.
Rectangle {
    SystemPalette { id: pal }
    radius: Ui.radiusLg
    color: pal.base
    border.width: 1
    border.color: Ui.hair
    Elevation { level: 2 }
}
