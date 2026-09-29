import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// The pen toolbar: only what you touch while writing. Panels live on the rail.
Rectangle {
    id: bar
    objectName: "chrome"
    required property InkCanvas canvas
    property var pageId: 0
    property bool tablet: false
    signal exportRequested()
    signal presentRequested()
    signal exportPageRequested()
    signal pictureRequested()
    signal presetsChanged()
    signal paperChosen(string colour)
    signal penColourChosen(string colour)     // Main keeps it, per kind of paper, across launches
    signal styleChosen(string style)
    signal shapeKindChosen(string kind)
    signal toast(string message)
    signal latexRequested()

    SystemPalette { id: pal }
    // Never wider than the page it floats over: on a narrow window the row scrolls sideways
    // instead of running off the edge with the last controls unreachable.
    property real sideInset: 0             // room to leave at each edge (the breadcrumb and its arrows)
    readonly property real maxWidth: parent ? parent.width - 24 - 2 * sideInset : 1600
    implicitWidth: Math.min(row.implicitWidth + 20, maxWidth)
    implicitHeight: Ui.target + 12
    radius: height / 2                      // a floating capsule (ADR mynotes-003); buttons nest in its ends
    color: Ui.chrome
    border.color: Ui.hair
    border.width: 1
    Elevation { level: 1 }

    readonly property var palette8: ["#FFFFFF", "#18212B", "#E5383B", "#1F6FEB", "#2FA84F", "#F77F00", "#6F42C1", "#E0B800"]
    readonly property var highlighters: ["#FFE03E", "#FF7AB6", "#7BE07A", "#6CB8FF"]
    readonly property var widths: [1.0, 1.5, 2.5, 4.0]

    component IconButton: Rectangle {
        property string icon
        property string tip
        property bool on: false
        property bool tool: false          // one of the drawing tools: the sliding pill marks it, not its own fill
        property bool enabledLook: true
        signal clicked()
        implicitWidth: Ui.target; implicitHeight: Ui.target; radius: height / 2
        color: on && !tool ? Ui.accentSoft
             : (press.pressed ? Qt.alpha(pal.text, Ui.pressAlpha)
             : (hover.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent"))
        Behavior on color { ColorAnimation { duration: Ui.quick } }
        Accessible.role: Accessible.Button
        Accessible.name: tip
        opacity: enabledLook ? 1 : 0.35
        Icon { anchors.centerIn: parent; name: parent.icon; colour: parent.on ? pal.highlight : pal.windowText }
        HoverHandler { id: hover }
        TapHandler { id: press; margin: Ui.hitMargin; onTapped: parent.clicked() }
        ToolTip.visible: hover.hovered && tip.length > 0; ToolTip.text: tip; ToolTip.delay: 600
    }
    component Sep: Rectangle { implicitWidth: 1; implicitHeight: Ui.target - 12; color: Qt.alpha(pal.text, 0.14); Layout.leftMargin: 3; Layout.rightMargin: 3 }
    component Pill: Rectangle {
        id: pill
        property string label
        property string tip
        signal clicked()
        signal longPressed()
        implicitWidth: pillText.implicitWidth + 20; implicitHeight: Ui.target - 8; radius: height / 2
        color: pillHover.hovered ? Qt.alpha(pal.text, 0.09) : Qt.alpha(pal.text, 0.05)
        Text { id: pillText; anchors.centerIn: parent; text: parent.label; color: pal.text; font.pixelSize: Ui.small + 1; font.capitalization: Font.Capitalize }
        HoverHandler { id: pillHover }
        // One handler for both, or the long press fires and the release then undoes it.
        TapHandler {
            property bool longFired: false
            onActiveChanged: if (active) longFired = false
            onLongPressed: { longFired = true; pill.longPressed() }
            onTapped: if (!longFired) pill.clicked()
        }
        ToolTip.visible: pillHover.hovered && tip.length > 0; ToolTip.delay: 600; ToolTip.text: tip
    }

    Flickable {
        anchors { fill: parent; leftMargin: 10; rightMargin: 10 }
        contentWidth: row.implicitWidth
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.horizontal: ScrollBar { policy: row.implicitWidth > bar.width - 20 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff; height: Ui.scrollbar }

    // The chosen tool's pill slides to the next one on a spring (ADR mynotes-003), so a change of
    // tool is seen as a move. Only the toolbar moves: nothing here touches the ink.
    Rectangle {
        id: toolPill
        readonly property Item target: {
            for (const c of row.children) if (c.tool && c.on && c.visible) return c
            return null
        }
        property bool settled: false
        Component.onCompleted: Qt.callLater(() => toolPill.settled = true)
        visible: target !== null
        x: target ? row.x + target.x : 0
        y: target ? row.y + target.y : 0
        width: Ui.target; height: Ui.target; radius: height / 2
        color: Ui.accentSoft
        Behavior on x { enabled: toolPill.settled && !Ui.instant && !Ui.reduceMotion; SpringAnimation { spring: 4; damping: 0.4; epsilon: 0.25 } }
    }
    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        x: Math.max(0, (parent.width - implicitWidth) / 2)
        spacing: Ui.gap

        IconButton { tool: true; icon: "draw-freehand"; tip: "Pen (1)"; on: canvas.tool === "pen"; onClicked: canvas.tool = "pen" }
        IconButton { tool: true; icon: "draw-highlight"; tip: "Highlighter (2)"; on: canvas.tool === "highlighter"; onClicked: canvas.tool = "highlighter" }
        IconButton { tool: true; icon: "draw-eraser"; tip: "Eraser (3) — or hold the pen's button"; on: canvas.tool === "eraser"; onClicked: canvas.tool = "eraser" }
        IconButton { tool: true; icon: "edit-select-lasso"; tip: "Lasso (4)"; on: canvas.tool === "lasso"; onClicked: canvas.tool = "lasso" }
        IconButton { tool: true; visible: canvas.wordCount > 0; icon: "edit-select-text"; tip: "Select PDF text (5)"; on: canvas.tool === "text"; onClicked: canvas.tool = "text" }
        IconButton { tool: true; objectName: "stickyTool"; icon: "sticky-note"; tip: "Sticky note (6): tap where it goes"; on: canvas.tool === "textblock"; onClicked: canvas.tool = "textblock" }
        IconButton {
            tool: true
            icon: "draw-rectangle"; tip: "Shapes (7) — drag to draw, then resize and recolour it"
            on: canvas.tool === "shape"
            id: shapeButton
            onClicked: { if (canvas.tool === "shape") shapeMenu.openFrom(shapeButton); else canvas.tool = "shape" }
        }
        IconButton { icon: "insert-image"; tip: "Add a picture (Ctrl+Shift+G) — or drop one on the page"; onClicked: bar.pictureRequested() }
        Sep {}

        // Favourite pens (ADR mynotes-003): a tap takes up a stored pen or highlighter; holding one
        // stores the pen in hand there. Colours, widths and pen styles live behind the pen button.
        Repeater {
            model: 4
            delegate: Rectangle {
                id: fav
                required property int index
                readonly property string key: "pen.favourite." + index
                readonly property bool holdAction: true     // hold stores the pen: no hold tip here
                property var preset: null
                function reloadPreset() { preset = JSON.parse(library.setting(key, "null")) }
                readonly property bool isHighlighter: !!preset && preset.tool === "highlighter"
                readonly property bool current: !!preset && (isHighlighter
                    ? canvas.tool === "highlighter" && canvas.highlighterColor.toString().toLowerCase() === preset.colour.toLowerCase()
                    : canvas.tool === "pen" && canvas.penColor.toString().toLowerCase() === preset.colour.toLowerCase() && Math.abs(canvas.penWidth - preset.width) < 0.01)
                Component.onCompleted: reloadPreset()
                implicitWidth: Ui.target; implicitHeight: Ui.target; radius: height / 2
                color: favTap.pressed ? Qt.alpha(pal.text, 0.16) : (favHover.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent")
                border.width: current ? 2 : 0; border.color: pal.highlight
                Accessible.role: Accessible.Button
                Accessible.name: preset ? "Favourite " + (index + 1) : "Empty favourite " + (index + 1)
                Rectangle {     // the pen's mark: a dot as wide as its stroke, or a highlighter's bar
                    anchors.centerIn: parent
                    visible: !!fav.preset
                    width: fav.isHighlighter ? Ui.icon + 2 : Math.max(8, Math.min(20, (fav.preset ? fav.preset.width : 1) * 4 + 6))
                    height: fav.isHighlighter ? Math.round(Ui.icon * 0.55) : width
                    radius: fav.isHighlighter ? 4 : width / 2
                    opacity: fav.isHighlighter ? 0.8 : 1
                    color: fav.preset ? fav.preset.colour : "transparent"
                    border.width: fav.preset && fav.preset.colour.toUpperCase() === "#FFFFFF" ? 1 : 0
                    border.color: Qt.alpha(pal.text, Ui.borderAlpha)
                }
                Rectangle { anchors.centerIn: parent; visible: !fav.preset; width: Ui.icon; height: width; radius: width / 2
                            color: "transparent"; border.width: 1.5; border.color: Qt.alpha(pal.text, 0.3) }
                HoverHandler { id: favHover }
                Timer {
                    id: favHold; interval: 650
                    onTriggered: {
                        const hl = canvas.tool === "highlighter"
                        library.setSetting(fav.key, JSON.stringify({ tool: hl ? "highlighter" : "pen",
                                                                    colour: (hl ? canvas.highlighterColor : canvas.penColor).toString(),
                                                                    width: canvas.penWidth, style: canvas.penStyle, brush: canvas.brush }))
                        bar.presetsChanged()
                        bar.toast((hl ? "Highlighter" : "Pen") + " stored as favourite " + (fav.index + 1))
                    }
                }
                TapHandler {
                    id: favTap
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
                    onPressedChanged: { if (pressed) favHold.restart(); else favHold.stop() }
                    onCanceled: favHold.stop()
                    onTapped: {
                        favHold.stop()
                        const p = fav.preset
                        if (!p) { favHold.triggered(); return }        // an empty slot takes the pen you are holding
                        if (p.tool === "highlighter") { canvas.highlighterColor = p.colour; canvas.tool = "highlighter"; return }
                        bar.penColourChosen(p.colour); canvas.penWidth = p.width
                        if (p.style) canvas.penStyle = p.style
                        canvas.brush = p.brush || "ink"          // favourites from before the pencil were ink
                        canvas.tool = "pen"
                    }
                }
                ToolTip.visible: favHover.hovered; ToolTip.delay: 600
                ToolTip.text: preset ? "Favourite " + (index + 1) + " — hold to store the pen you are using here"
                                     : "Empty — tap to store the pen you are using"
                Connections { target: bar; function onPresetsChanged() { fav.reloadPreset() } }
            }
        }
        Item {
            id: penButton
            objectName: "penButton"
            readonly property color active: canvas.tool === "highlighter" ? canvas.highlighterColor : canvas.penColor
            implicitWidth: Ui.target; implicitHeight: Ui.target
            Accessible.role: Accessible.Button
            Accessible.name: "Colour, width and pen style"
            Rectangle { anchors.fill: parent; radius: height / 2
                        color: penTap.pressed ? Qt.alpha(pal.text, 0.16) : (penHover.hovered ? Qt.alpha(pal.text, 0.1) : Qt.alpha(pal.text, 0.06)) }
            Rectangle { anchors.centerIn: parent; width: Ui.icon + 2; height: width; radius: width / 2; color: penButton.active
                        border.width: 2; border.color: pal.base }
            HoverHandler { id: penHover }
            TapHandler { id: penTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: bar.showPalette() }
            ToolTip.visible: penHover.hovered; ToolTip.text: "Colour, width and pen style"; ToolTip.delay: 600
        }
        Sep {}
        IconButton { icon: "edit-undo"; tip: "Undo (Ctrl+Z, or double-press the pen button)"; enabledLook: canvas.canUndo; onClicked: canvas.undo() }
        IconButton { icon: "edit-redo"; tip: "Redo (Ctrl+Shift+Z)"; enabledLook: canvas.canRedo; onClicked: canvas.redo() }
        Sep {}
        Pill { readonly property bool holdAction: true; label: Math.round(canvas.zoom * 100) + "%"; tip: "Tap: fit page (Ctrl+0) · long-press: fit width (Ctrl+1)"
               onClicked: canvas.fitPage(); onLongPressed: canvas.fitWidth() }
        Sep {}
        Pill { visible: canvas.hasSelection && helpers; label: "∑ LaTeX"; tip: "Convert the lasso'd maths to an editable LaTeX block (Ctrl+M)"; onClicked: bar.latexRequested() }
        IconButton { visible: canvas.hasSelection; icon: "edit-delete"; tip: "Delete selection (Delete)"; onClicked: canvas.deleteSelection() }
        IconButton { id: moreButton; icon: "overflow-menu"; tip: "Page style, paper colour, clear, export"; onClicked: moreMenu.openFrom(moreButton) }
    }

    }

    function useColour(c) {
        if (canvas.hasSelection) { canvas.restyleSelection(c, 1.0); return }   // recolour what the lasso caught
        if (canvas.tool === "highlighter") canvas.highlighterColor = c
        else { bar.penColourChosen(c); if (canvas.tool !== "pen") canvas.tool = "pen" }
    }
    function showPalette() { penPopover.openFrom(penButton) }
    PenPopover {
        id: penPopover
        parent: Overlay.overlay
        canvas: bar.canvas
        colours: bar.canvas.tool === "highlighter" ? bar.highlighters : bar.palette8
        widths: bar.widths
        onColourChosen: (c) => bar.useColour(c)
        onMoreColours: { colourPicker.current = penButton.active.toString(); colourPicker.openFrom(penButton) }
    }
    ColourPicker {
        id: colourPicker
        parent: Overlay.overlay
        onChosen: (c) => bar.useColour(c)
    }
    ActionSheet {
        id: moreMenu
        parent: Overlay.overlay
        title: "This page"
        items: [
            { label: "Pen style: " + canvas.penStyle + " — tap to change", icon: "draw-freehand", action: () => {
                canvas.penStyle = canvas.penStyle === "classic" ? "fountain" : (canvas.penStyle === "fountain" ? "ballpoint" : (canvas.penStyle === "ballpoint" ? "brush" : "classic"))
                library.setSetting("pen.style", canvas.penStyle)
                bar.toast("Pen: " + canvas.penStyle)
            } },
            { label: "Page style: " + canvas.pageStyle, icon: "draw-rectangle", action: () => styleMenu.openFrom(moreButton) },
            { label: "Paper colour…", icon: "color-picker", action: () => paperMenu.openFrom(moreButton) },
            { label: "Clear the page (undoable)", icon: "edit-clear-all", action: () => canvas.clearAll() },
            { label: "Present this section (F5)", icon: "view-presentation", action: () => bar.presentRequested() },
            { label: "Export this page as PDF", icon: "document-export", action: () => bar.exportPageRequested() },
            { label: "Export this section as PDF", icon: "document-export", action: () => bar.exportRequested() }
        ]
    }
    ActionSheet {
        id: shapeMenu
        parent: Overlay.overlay
        title: "Shape to draw"
        items: [
            { label: "Rectangle", icon: "draw-rectangle", action: () => bar.shapeKindChosen("rect") },
            { label: "Ellipse", icon: "draw-ellipse", action: () => bar.shapeKindChosen("ellipse") },
            { label: "Triangle", icon: "draw-triangle", action: () => bar.shapeKindChosen("triangle") },
            { label: "Line", icon: "draw-line", action: () => bar.shapeKindChosen("line") },
            { label: "Arrow", icon: "draw-arrow", action: () => bar.shapeKindChosen("arrow") }
        ]
    }
    ActionSheet {
        id: styleMenu
        parent: Overlay.overlay
        title: "Page style"
        items: [
            { label: "Dotted", icon: "paper-dotted", action: () => bar.styleChosen("dotted") },
            { label: "Lined", icon: "paper-lined", action: () => bar.styleChosen("lined") },
            { label: "Squared", icon: "paper-grid", action: () => bar.styleChosen("grid") },
            { label: "Graph paper (2 mm)", icon: "paper-graph", action: () => bar.styleChosen("graph") },
            { label: "Isometric", icon: "paper-isometric", action: () => bar.styleChosen("isometric") },
            { label: "Music staves", icon: "paper-music", action: () => bar.styleChosen("music") },
            { label: "Cornell", icon: "paper-cornell", action: () => bar.styleChosen("cornell") },
            { label: "Plain", icon: "paper-plain", action: () => bar.styleChosen("plain") }
        ]
    }
    ActionSheet {
        id: paperMenu
        parent: Overlay.overlay
        title: "Paper colour"
        items: [
            { label: "White", swatch: "#FFFFFF", action: () => bar.paperChosen("#FFFFFF") },
            { label: "Cream (easier on the eyes)", swatch: "#F6F1E4", action: () => bar.paperChosen("#F6F1E4") },
            { label: "Cool grey", swatch: "#EDEFF2", action: () => bar.paperChosen("#EDEFF2") },
            { label: "Charcoal (light ink)", swatch: "#22262B", action: () => bar.paperChosen("#22262B") },
            { label: "Use the app default", icon: "edit-undo", action: () => bar.paperChosen("") }
        ]
    }
}
