pragma Singleton
import QtQuick

// Sizing and colour tokens. Every panel reads these rather than hard-coding, so the app can be
// resized, re-themed and made touch-friendly in one place. Sizes follow the system font, because
// the one setting a KDE user expects to work is the font size (Kirigami derives its grid unit the
// same way). Colours that carry meaning are derived from the theme's brightness, so nothing ends
// up as dark red on a dark ground.
QtObject {
    property bool tablet: false
    property bool dark: true                              // set from Main, from the real palette
    // Left-handed: the rail and the chrome a writing hand would cover move to the other side.
    property bool leftHanded: false

    readonly property real k: tablet ? 1.25 : 1.0
    readonly property int base: {
        // The KDE user's font size is the one setting they expect an app to honour. Guarded so a
        // platform that does not expose it cannot throw.
        const f = (typeof Application !== "undefined" && Application.font) ? Application.font : null
        return (f && f.pixelSize > 0) ? Math.max(11, Math.min(20, f.pixelSize)) : 13
    }

    // ---- type
    readonly property int text: Math.round(base * k)
    readonly property int small: Math.round((base - 1) * k)
    readonly property int title: Math.round(base * 1.7 * k)   // one size for every full-screen heading

    // ---- targets and rhythm
    readonly property int target: Math.round(46 * k)      // Apple 44 pt · Windows 44 epx · Material 48 dp
    readonly property int row: Math.round(46 * k)
    readonly property int icon: Math.round(20 * k)
    readonly property int railIcon: Math.round(22 * k)
    readonly property int rail: Math.round(56 * k)
    readonly property int panel: Math.round(300 * k)
    readonly property int rightPanel: Math.round(380 * k)
    // How much of the bottom edge the on-screen keyboard covers right now (Main.qml writes it).
    // Anything holding a text field lifts itself above this, so the keyboard never hides the field.
    property real keyboardInset: 0

    readonly property int gap: tablet ? 8 : 4             // Material asks ≥8 dp between touch targets
    readonly property int hitMargin: tablet ? 10 : 6      // PointerHandler.margin for small controls
    readonly property int scrollbar: tablet ? 12 : 8      // 6 px is 0.9 mm at this screen's 165 PPI

    // ---- shape: three radii, not fourteen
    readonly property int radiusSm: 6
    readonly property int radius: 10
    readonly property int radiusLg: 14

    // ---- state layers (Material's numbers) and lines that must be seen (WCAG 1.4.11 asks 3:1)
    readonly property real hoverAlpha: 0.08
    readonly property real pressAlpha: 0.16
    readonly property real hairline: 0.14
    readonly property real borderAlpha: 0.37
    readonly property real mutedAlpha: 0.68               // secondary text: ≥4.5:1 on this palette

    // ---- meaning, at a contrast that survives the theme
    readonly property color danger: dark ? "#FB4934" : "#9D0006"
    readonly property color warning: dark ? "#FE8019" : "#AF3A03"
    readonly property color good: dark ? "#B8BB26" : "#79740E"

    // ---- motion: nothing here may ever sit between the pen and the ink
    readonly property int quick: 120
    readonly property int slow: 220
}
