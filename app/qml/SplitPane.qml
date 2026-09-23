import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// The right-hand page in split view: any page — ink, typed or PDF — with its own zoom, scroll, tools
// and undo. Its ink is saved by its own PageStore (splitStore), so both sides can be written on.
Rectangle {
    id: pane
    objectName: "splitPane"
    property var pageId: 0
    signal closeRequested()
    signal pickRequested()
    signal swapRequested()
    signal pageLinkActivated(string url)
    signal toast(string message)
    SystemPalette { id: pal }
    color: pal.window

    property var info: ({})
    readonly property bool typed: info.sizeMode === "typed"
    readonly property string paperColour: (info.paper || "").length ? info.paper : library.setting("page.paper", "#22262B")
    readonly property bool paperIsDark: {
        const c = Qt.color(pane.paperColour)
        return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) < 0.45
    }
    readonly property alias canvas: splitCanvas

    function load() {
        info = library.page(pageId)
        if (!info.id) { pane.closeRequested(); return }
        splitCanvas.resetHistory()
        splitCanvas.clearBackground()
        splitStore.load(pageId)
        splitCanvas.pageStyle = info.style
        splitCanvas.infinite = info.sizeMode === "infinite"
        splitCanvas.pageSize = Qt.size(info.width, info.height)
        splitCanvas.fitPage()
        if (pdf.pageHasPdf(pageId)) { pdf.requestRender(pageId, 1.0); pdf.requestWords(pageId) }
        library.touchPage(pageId)
    }
    onPageIdChanged: if (splitCanvas.ready) load()
    Component.onCompleted: { splitBinder.attach(splitCanvas); splitCanvas.ready = true; load() }
    Component.onDestruction: { splitStore.unload(); splitBinder.detach(splitCanvas) }

    Connections {
        target: pdf
        function onRendered(pageId, scale, file, w, h) { if (pageId === pane.pageId) splitCanvas.setBackground(file, scale) }
        function onWords(pageId, words) { if (pageId === pane.pageId) splitCanvas.setWords(words) }
    }
    Connections {
        target: library
        function onChanged() {
            const now = library.page(pane.pageId)
            if (!now.id) pane.closeRequested()       // deleted, or its notebook was
            else pane.info = now
        }
    }

    component PaneButton: Rectangle {
        id: btn
        property string icon
        property string tip
        property bool on: false
        property bool enabledLook: true
        signal clicked()
        implicitWidth: Ui.target; implicitHeight: Ui.target; radius: 9
        color: on ? pal.highlight : (press.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : (hover.hovered ? Qt.alpha(pal.text, Ui.hoverAlpha) : "transparent"))
        opacity: enabledLook ? 1 : 0.35
        Accessible.role: Accessible.Button
        Accessible.name: tip
        Icon { anchors.centerIn: parent; name: btn.icon; colour: btn.on ? pal.highlightedText : pal.windowText }
        HoverHandler { id: hover }
        TapHandler { id: press; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: btn.clicked() }
        ToolTip.visible: hover.hovered && tip.length > 0; ToolTip.text: tip; ToolTip.delay: 600
    }
    component Sep: Rectangle { implicitWidth: 1; implicitHeight: Ui.target - 16; color: Qt.alpha(pal.text, Ui.hairline) }

    // ---- header: which page this is, its tools, and the ways out
    Rectangle {
        id: header
        objectName: "chrome"
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: Ui.target + 12
        color: pal.window
        z: 10
        Rectangle { anchors { left: parent.left; right: parent.right; bottom: parent.bottom } height: 1; color: Qt.alpha(pal.text, Ui.hairline) }

        RowLayout {
            anchors { fill: parent; leftMargin: 12; rightMargin: 6 }
            spacing: 2
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Layout.minimumWidth: 60
                Text {
                    text: (pane.info.title || "").length ? pane.info.title.replace("​", "") : "Untitled page"
                    color: pal.windowText; font.pixelSize: Ui.text; font.weight: Font.DemiBold
                    elide: Text.ElideRight; Layout.fillWidth: true
                }
                Text {
                    text: (pane.info.notebookName || "") + " › " + (pane.info.sectionName || "")
                    color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small
                    elide: Text.ElideRight; Layout.fillWidth: true
                }
            }
            // Writing tools for this side only; the typed page brings its own bar.
            RowLayout {
                visible: !pane.typed && pane.width > 520
                spacing: 2
                PaneButton { icon: "draw-freehand"; tip: "Pen"; on: splitCanvas.tool === "pen"; onClicked: splitCanvas.tool = "pen" }
                PaneButton { icon: "draw-highlight"; tip: "Highlighter"; on: splitCanvas.tool === "highlighter"; onClicked: splitCanvas.tool = "highlighter" }
                PaneButton { icon: "draw-eraser"; tip: "Eraser"; on: splitCanvas.tool === "eraser"; onClicked: splitCanvas.tool = "eraser" }
                PaneButton { icon: "edit-select-lasso"; tip: "Lasso"; on: splitCanvas.tool === "lasso"; onClicked: splitCanvas.tool = "lasso" }
                PaneButton { visible: splitCanvas.hasSelection; icon: "edit-delete"; tip: "Delete the selection"; onClicked: splitCanvas.deleteSelection() }
                Sep {}
                PaneButton { icon: "edit-undo"; tip: "Undo on this side"; enabledLook: splitCanvas.canUndo; onClicked: splitCanvas.undo() }
                PaneButton { icon: "edit-redo"; tip: "Redo on this side"; enabledLook: splitCanvas.canRedo; onClicked: splitCanvas.redo() }
                Sep {}
            }
            PaneButton { objectName: "splitSwap"; icon: "view-split-swap"; tip: "Swap the two sides"; onClicked: pane.swapRequested() }
            PaneButton { objectName: "splitPick"; icon: "document-open"; tip: "Show another page here"; onClicked: pane.pickRequested() }
            PaneButton { objectName: "splitClose"; icon: "window-close"; tip: "Close this side"; onClicked: pane.closeRequested() }
        }
    }

    // ---- the page
    Item {
        id: body
        anchors { left: parent.left; right: parent.right; top: header.bottom; bottom: parent.bottom }
        clip: true            // a zoomed page stays on its own side of the divider
        Rectangle {
            anchors.fill: parent
            color: splitCanvas.infinite ? splitCanvas.paperColor : (Ui.dark ? Qt.darker(pal.window, 1.35) : Qt.darker(pal.window, 1.08))
        }
        InkCanvas {
            id: splitCanvas
            objectName: "splitCanvas"
            property bool ready: false
            anchors.fill: parent
            visible: pane.pageId > 0 && !pane.typed
            enabled: visible
            topInset: 16
            bottomInset: 16
            paperColor: pane.paperColour
            dotColor: Qt.alpha(pane.paperIsDark ? "#FFFFFF" : "#000000", 0.18)
            frameColor: Qt.alpha(pane.paperIsDark ? "#FFFFFF" : "#000000", 0.55)
            penColor: pane.paperIsDark ? "#F2F2F2" : "#1A1A1A"
            accentColor: pal.highlight
            Component.onCompleted: fitPage()
        }
        ImageLayer {
            anchors.fill: parent
            visible: !pane.typed
            board: splitCanvas
            pageId: pane.typed ? 0 : pane.pageId
            onToast: (m) => pane.toast(m)
        }
        ShapeLayer {
            anchors.fill: parent
            visible: !pane.typed
            board: splitCanvas
            pageId: pane.typed ? 0 : pane.pageId
            strokeColour: splitCanvas.penColor
            onToast: (m) => pane.toast(m)
        }
        TextLayer {
            anchors.fill: parent
            visible: !pane.typed
            canvas: splitCanvas
            pageId: pane.typed ? 0 : pane.pageId
            onPageLinkActivated: (url) => pane.pageLinkActivated(url)
        }
        TypedPage {
            objectName: "splitTypedPage"
            anchors.fill: parent
            visible: pane.typed
            pageId: pane.typed ? pane.pageId : 0
            onPageLinkActivated: (url) => pane.pageLinkActivated(url)
        }
    }
}
