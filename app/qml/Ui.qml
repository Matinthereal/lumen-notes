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
        // A phone or tablet's application font is set to the tablet body size in main.cpp; the
        // scale below starts from the desktop one, or tablets would scale twice.
        if (Qt.platform.os === "android" || Qt.platform.os === "ios") return 13
        // The KDE user's font size is the one setting they expect an app to honour. Guarded so a
        // platform that does not expose it cannot throw.
        const f = (typeof Application !== "undefined" && Application.font) ? Application.font : null
        return (f && f.pixelSize > 0) ? Math.max(11, Math.min(20, f.pixelSize)) : 13
    }

    // ---- type
    readonly property int text: Math.round(base * k)
    readonly property int small: Math.round((base - 1) * k)
    readonly property int title: Math.round(base * 1.7 * k)   // one size for every full-screen heading
    // A size a screen chose for itself, scaled like the rest for touch: without this those stayed
    // desktop-small on a tablet (11 px at arm's length).
    function px(n) { return Math.round(n * k) }
    // Shortcut hints are for a keyboard: an iPad's says ⌘ ⌥ ⇧, and Android's tablets usually have none.
    function keys(text) {
        if (Qt.platform.os === "ios") return text.replace(/Ctrl\+/g, "⌘").replace(/Alt\+/g, "⌥").replace(/Shift\+/g, "⇧")
        if (Qt.platform.os === "android") return text.replace(/\s*·?\s*(review )?\(?(Esc|Ctrl\+[^\s)]*|F\d+)\)?/g, "").replace(/\s+—\s*$/, "")
        return text
    }

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

    // ---- type faces (ADR mynotes-003): Figtree is the application font; Newsreader sets titles
    readonly property string titleFont: "Newsreader"

    // ---- shape: five radii, always nested — a corner inside a card is the card's minus the gap
    readonly property int radiusPaper: 6
    readonly property int radiusSm: 10
    readonly property int radius: 14
    readonly property int radiusLg: 22

    // ---- Paper & Lamp surfaces, from the palette Theme sets (theme.cpp)
    property SystemPalette pal: SystemPalette {}
    readonly property color chrome: Qt.alpha(pal.base, dark ? 0.9 : 0.86)   // frosted bars
    readonly property color hair: Qt.alpha(pal.text, 0.10)
    readonly property color accentSoft: Qt.alpha(pal.highlight, dark ? 0.18 : 0.13)
    readonly property color lamp: dark ? "#F0B25A" : "#E3962B"
    readonly property color shadow: dark ? "#000000" : "#3C2814"

    // ---- state layers (Material's numbers) and lines that must be seen (WCAG 1.4.11 asks 3:1)
    readonly property real hoverAlpha: 0.08
    readonly property real pressAlpha: 0.16
    readonly property real hairline: 0.14
    readonly property real borderAlpha: 0.37
    readonly property real mutedAlpha: 0.68               // secondary text: ≥4.5:1 on this palette

    // Text on the accent: the theme's highlightedText measured 1.96:1 on this highlight, so pick
    // the readable one from the accent's own brightness instead (WCAG 1.4.3 wants 4.5).
    function onAccent(accent) {
        return (0.299 * accent.r + 0.587 * accent.g + 0.114 * accent.b) > 0.5 ? Qt.rgba(0.08, 0.08, 0.08, 1) : Qt.rgba(1, 1, 1, 1)
    }

    // ---- meaning, at a contrast that survives the theme
    readonly property color danger: dark ? "#FB4934" : "#9D0006"
    readonly property color warning: dark ? "#FE8019" : "#AF3A03"
    readonly property color good: dark ? "#B8BB26" : "#79740E"

    // ---- motion: nothing here may ever sit between the pen and the ink. Structure moves on
    // Material's curves; only presses and tool changes spring. Reduce motion: every one a quick fade.
    property bool reduceMotion: false
    property bool instant: false          // --uitest: it checks what happens, not how it moves
    function ms(n) { return instant ? 0 : reduceMotion ? Math.min(n, 90) : n }
    readonly property int quick: ms(120)
    readonly property int slow: ms(220)
    readonly property var standard: [0.2, 0, 0, 1, 1, 1]
    readonly property var emphasized: [0.05, 0.7, 0.1, 1, 1, 1]
    readonly property var accelerate: [0.3, 0, 0.8, 0.15, 1, 1]
}
