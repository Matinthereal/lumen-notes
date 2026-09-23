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
    radius: 12
    color: pal.window
    border.color: Qt.alpha(pal.text, 0.16)
    border.width: 1

    readonly property var palette8: ["#FFFFFF", "#18212B", "#E5383B", "#1F6FEB", "#2FA84F", "#F77F00", "#6F42C1", "#E0B800"]
    readonly property var highlighters: ["#FFE03E", "#FF7AB6", "#7BE07A", "#6CB8FF"]
    readonly property var widths: [1.0, 1.5, 2.5, 4.0]

    component IconButton: Rectangle {
        property string icon
        property string tip
        property bool on: false
        property bool enabledLook: true
        signal clicked()
        implicitWidth: Ui.target; implicitHeight: Ui.target; radius: 9
        color: on ? pal.highlight
             : (press.pressed ? Qt.alpha(pal.text, Ui.pressAlpha)
             : (hover.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent"))
        Accessible.role: Accessible.Button
        Accessible.name: tip
        opacity: enabledLook ? 1 : 0.35
        Icon { anchors.centerIn: parent; name: parent.icon; colour: parent.on ? pal.highlightedText : pal.windowText }
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

    RowLayout {
        id: row
        anchors.verticalCenter: parent.verticalCenter
        x: Math.max(0, (parent.width - implicitWidth) / 2)
        spacing: Ui.gap

        IconButton { icon: "draw-freehand"; tip: "Pen (1)"; on: canvas.tool === "pen"; onClicked: canvas.tool = "pen" }
        IconButton { icon: "draw-highlight"; tip: "Highlighter (2)"; on: canvas.tool === "highlighter"; onClicked: canvas.tool = "highlighter" }
        IconButton { icon: "draw-eraser"; tip: "Eraser (3) — or hold the pen's button"; on: canvas.tool === "eraser"; onClicked: canvas.tool = "eraser" }
        IconButton { icon: "edit-select-lasso"; tip: "Lasso (4)"; on: canvas.tool === "lasso"; onClicked: canvas.tool = "lasso" }
        IconButton { visible: canvas.wordCount > 0; icon: "edit-select-text"; tip: "Select PDF text (5)"; on: canvas.tool === "text"; onClicked: canvas.tool = "text" }
        IconButton { icon: "insert-text"; tip: "Text block (6): tap where it goes"; on: canvas.tool === "textblock"; onClicked: canvas.tool = "textblock" }
        IconButton {
            icon: "draw-rectangle"; tip: "Shapes (7) — drag to draw, then resize and recolour it"
            on: canvas.tool === "shape"
            id: shapeButton
            onClicked: { if (canvas.tool === "shape") shapeMenu.openFrom(shapeButton); else canvas.tool = "shape" }
        }
        IconButton { icon: "insert-image"; tip: "Add a picture (Ctrl+Shift+G) — move it with the lasso"; onClicked: bar.pictureRequested() }
        Sep {}

        Repeater {
            model: canvas.tool === "highlighter" ? bar.highlighters : bar.palette8
            delegate: Item {
                required property string modelData
                width: Ui.target - 8; height: Ui.target
                Rectangle {
                    anchors.centerIn: parent
                    readonly property bool current: (canvas.tool === "highlighter" ? canvas.highlighterColor : canvas.penColor).toString().toLowerCase() === modelData.toLowerCase()
                    width: current ? Ui.icon + 6 : Ui.icon; height: width; radius: width / 2
                    color: modelData
                    border.width: current ? 2 : 0; border.color: pal.base
                    Behavior on width { NumberAnimation { duration: 80 } }
                }
                TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: bar.useColour(modelData) }
            }
        }
        Item {
            id: moreColours
            objectName: "moreColours"
            readonly property color active: canvas.tool === "highlighter" ? canvas.highlighterColor : canvas.penColor
            readonly property bool custom: (canvas.tool === "highlighter" ? bar.highlighters : bar.palette8)
                                           .every(c => c.toLowerCase() !== active.toString().toLowerCase())
            width: Ui.target - 4; height: Ui.target
            Accessible.role: Accessible.Button
            Accessible.name: "More colours"
            Rectangle {
                anchors.centerIn: parent
                width: moreColours.custom ? Ui.icon + 6 : Ui.icon; height: width; radius: width / 2
                gradient: moreColours.custom ? null : rainbow
                color: moreColours.custom ? moreColours.active : "transparent"
                border.width: moreColours.custom ? 2 : 0; border.color: pal.base
                Gradient {
                    id: rainbow
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "#E5383B" }
                    GradientStop { position: 0.33; color: "#E0B800" }
                    GradientStop { position: 0.66; color: "#2FA84F" }
                    GradientStop { position: 1.0; color: "#6F42C1" }
                }
                Text { anchors.centerIn: parent; visible: !moreColours.custom; text: "+"; color: "white"; font.pixelSize: Ui.small; font.weight: Font.Bold }
            }
            HoverHandler { id: moreHover }
            ToolTip.visible: moreHover.hovered; ToolTip.text: "More colours"; ToolTip.delay: 600
            TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { colourPicker.current = moreColours.active.toString(); colourPicker.openFrom(moreColours) } }
        }
        Sep {}
        Repeater {
            model: bar.widths
            delegate: Rectangle {
                required property real modelData
                width: Ui.target - 8; height: Ui.target - 8; radius: 8
                color: Math.abs(canvas.penWidth - modelData) < 0.01 ? Qt.alpha(pal.highlight, 0.25) : "transparent"
                Rectangle { anchors.centerIn: parent; width: Ui.icon - 2; height: Math.max(1.5, modelData * 1.7); radius: height / 2; color: pal.text }
                TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: {
                    if (canvas.hasSelection) { canvas.setSelectionWidth(modelData); return }
                    canvas.penWidth = modelData
                } }
            }
        }
        Sep {}
        Repeater {
            model: 3
            delegate: Rectangle {
                required property int index
                readonly property string key: "pen.favourite." + index
                readonly property bool holdAction: true     // hold stores the pen: no hold tip here
                property var preset: null
                function reloadPreset() { preset = JSON.parse(library.setting(key, "null")) }
                Component.onCompleted: reloadPreset()
                implicitWidth: Ui.target - 6; implicitHeight: Ui.target - 6; radius: 8
                color: favTap.pressed ? Qt.alpha(pal.text, 0.2) : (favHover.hovered ? Qt.alpha(pal.text, 0.09) : "transparent")
                border.width: preset ? 0 : 1; border.color: Qt.alpha(pal.text, Ui.borderAlpha)
                Rectangle {
                    anchors.centerIn: parent
                    visible: !!parent.preset
                    width: Ui.icon; height: Math.max(3, (parent.preset ? parent.preset.width : 1) * 2.2); radius: height / 2
                    color: parent.preset ? parent.preset.colour : "transparent"
                }
                Text { anchors.centerIn: parent; visible: !parent.preset; text: "+"; color: Qt.alpha(pal.text, 0.5); font.pixelSize: Ui.text + 2 }
                HoverHandler { id: favHover }
                Timer {
                    id: favHold; interval: 650
                    onTriggered: {
                        library.setSetting(key, JSON.stringify({ colour: canvas.penColor.toString(), width: canvas.penWidth, style: canvas.penStyle }))
                        bar.presetsChanged()
                        bar.toast("Pen " + (index + 1) + " saved")
                    }
                }
                TapHandler {
                    id: favTap
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
                    onPressedChanged: { if (pressed) { favHold.restart(); held = false } else favHold.stop() }
                    property bool held: false
                    onCanceled: favHold.stop()
                    onTapped: {
                        favHold.stop()
                        const p = preset
                        if (!p) { favHold.triggered(); return }        // an empty slot takes the pen you are holding
                        canvas.penColor = p.colour; canvas.penWidth = p.width; canvas.penStyle = p.style
                        canvas.tool = "pen"
                    }
                }
                ToolTip.visible: favHover.hovered; ToolTip.delay: 600
                ToolTip.text: preset ? "Favourite pen " + (index + 1) + " — hold to replace it with the pen you are using"
                                     : "Empty slot — tap to store the pen you are using"
                Connections { target: bar; function onPresetsChanged() { reloadPreset() } }
            }
        }
        Sep {}
        IconButton { icon: "edit-undo"; tip: "Undo (Ctrl+Z, or double-press the pen button)"; enabledLook: canvas.canUndo; onClicked: canvas.undo() }
        IconButton { icon: "edit-redo"; tip: "Redo (Ctrl+Shift+Z)"; enabledLook: canvas.canRedo; onClicked: canvas.redo() }
        Sep {}
        Pill { readonly property bool holdAction: true; label: Math.round(canvas.zoom * 100) + "%"; tip: "Tap: fit page (Ctrl+0) · long-press: fit width (Ctrl+1)"
               onClicked: canvas.fitPage(); onLongPressed: canvas.fitWidth() }
        Sep {}
        Pill { visible: canvas.hasSelection; label: "∑ LaTeX"; tip: "Convert the lasso'd maths to an editable LaTeX block (Ctrl+M)"; onClicked: bar.latexRequested() }
        IconButton { visible: canvas.hasSelection; icon: "edit-delete"; tip: "Delete selection (Delete)"; onClicked: canvas.deleteSelection() }
        IconButton { id: moreButton; icon: "overflow-menu"; tip: "Page style, paper colour, clear, export"; onClicked: moreMenu.openFrom(moreButton) }
    }

    }

    function useColour(c) {
        if (canvas.hasSelection) { canvas.restyleSelection(c, 1.0); return }   // recolour what the lasso caught
        if (canvas.tool === "highlighter") canvas.highlighterColor = c
        else { canvas.penColor = c; if (canvas.tool !== "pen") canvas.tool = "pen" }
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
