import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import Lumen

// Shapes stay objects after you draw them: drag a corner to resize, tap a colour to change the
// outline, tap a fill. Pen, finger and mouse all draw and all grab — every handler here names the
// devices it takes, because a handler that forgets is the bug that keeps coming back.
Item {
    id: shapeLayer
    required property InkCanvas board
    property var pageId: 0
    property string kind: "rect"            // what the shape tool will draw next
    property string strokeColour: "#E0403C"
    property string fillColour: ""          // "" = no fill
    property real strokeWidth: 2.5
    property var selectedId: 0
    readonly property bool drawing: board && board.tool === "shape"
    readonly property bool picking: board && (board.tool === "shape" || board.tool === "lasso")
    readonly property real zoom: board ? Math.max(board.zoom, 0.15) : 1
    signal toast(string message)
    signal toastAction(string message, string actionLabel, var fn)
    signal optionsAsked(rect where)      // double tap on a selected shape: show its options over it

    // Every pointing device this app has. Named once, used everywhere below.
    readonly property int allDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus

    SystemPalette { id: pal }
    ListModel { id: items }

    // One description of every shape, used by the finished shapes and by the live preview, so the
    // thing you drag out is exactly the thing you get.
    function pathFor(kind, w, h) {
        switch (kind) {
        case "line":
            return "M 0 0 L " + w + " " + h
        case "arrow": {
            const ang = Math.atan2(h, w), len = Math.max(10, Math.min(26, Math.hypot(w, h) * 0.22))
            const x1 = w - Math.cos(ang - 0.42) * len, y1 = h - Math.sin(ang - 0.42) * len
            const x2 = w - Math.cos(ang + 0.42) * len, y2 = h - Math.sin(ang + 0.42) * len
            return "M 0 0 L " + w + " " + h + " M " + x1 + " " + y1 + " L " + w + " " + h + " L " + x2 + " " + y2
        }
        case "ellipse":
            return "M " + (w / 2) + " 0 A " + (w / 2) + " " + (h / 2) + " 0 1 1 " + (w / 2 - 0.01) + " 0 Z"
        case "triangle":
            return "M " + (w / 2) + " 0 L " + w + " " + h + " L 0 " + h + " Z"
        default:
            return "M 0 0 L " + w + " 0 L " + w + " " + h + " L 0 " + h + " Z"
        }
    }
    function straight(kind) { return kind === "line" || kind === "arrow" }
    // A shape belongs on the paper: on a fixed page, keep it inside the sheet.
    function clampX(x, w) {
        if (!board || board.infinite) return x
        return Math.max(0, Math.min(x, Math.max(0, board.pageSize.width - Math.abs(w))))
    }
    function clampY(y, h) {
        if (!board || board.infinite) return y
        return Math.max(0, Math.min(y, Math.max(0, board.pageSize.height - Math.abs(h))))
    }

    function reload() {
        items.clear()
        if (!pageId) { selectedId = 0; return }
        const list = shapes.list(pageId)
        for (const s of list) items.append({ sid: s.id, kind: s.kind, sx: s.x, sy: s.y, sw: s.w, sh: s.h,
                                             stroke: s.stroke, fill: s.fill, thickness: s.width })
        if (selectedId && !list.some(s => s.id === selectedId)) selectedId = 0
    }
    function selected() { return selectedId ? shapes.shape(selectedId) : ({}) }
    function restyle(stroke, fill, width) {
        if (!selectedId) return false
        shapes.setStyle(selectedId, stroke, fill === undefined ? selected().fill : fill, width || 0)
        return true
    }
    function removeSelected() {
        if (!selectedId) return
        const gone = shapes.shape(selectedId)
        shapes.remove(selectedId)
        selectedId = 0
        shapeLayer.toastAction("Shape deleted", "Undo", function() {
            const id = shapes.create(gone.pageId, gone.kind, gone.x, gone.y, gone.w, gone.h, gone.stroke, gone.fill, gone.width)
            if (id) shapeLayer.selectedId = id
        })
    }
    function duplicateSelected() { if (selectedId) { const id = shapes.duplicate(selectedId); if (id) { selectedId = id; toast("Shape duplicated") } } }

    Connections { target: shapes; function onChanged(pid) { if (pid === shapeLayer.pageId) shapeLayer.reload() } }
    onPageIdChanged: reload()
    Component.onCompleted: reload()


    // ---- drawing a new one. Underneath the shapes, so a press on one you already made grabs that
    //      one instead of starting another.
    Item {
        anchors.fill: parent
        z: -1
        objectName: shapeLayer.drawing ? "chrome" : ""     // the pen places shapes instead of inking
        enabled: shapeLayer.drawing
        visible: shapeLayer.drawing
        DragHandler {
            id: draw
            target: null
            acceptedDevices: shapeLayer.allDevices
            property point startPage: Qt.point(0, 0)
            property point nowPage: Qt.point(0, 0)
            property bool putDown: false
            onActiveChanged: {
                if (active) {
                    // A drag while something is selected puts it down instead of drawing over it.
                    putDown = shapeLayer.selectedId !== 0
                    shapeLayer.selectedId = 0
                    startPage = shapeLayer.board.toPage(centroid.position)
                    nowPage = startPage
                    return
                }
                if (putDown) { putDown = false; return }
                const dx = nowPage.x - startPage.x, dy = nowPage.y - startPage.y
                if (Math.abs(dx) < 4 && Math.abs(dy) < 4) return           // a tap is not a shape
                const isLine = shapeLayer.straight(shapeLayer.kind)
                const id = shapes.create(shapeLayer.pageId, shapeLayer.kind,
                                         shapeLayer.clampX(isLine ? startPage.x : Math.min(startPage.x, nowPage.x), dx),
                                         shapeLayer.clampY(isLine ? startPage.y : Math.min(startPage.y, nowPage.y), dy),
                                         isLine ? dx : Math.abs(dx),
                                         isLine ? dy : Math.abs(dy),
                                         shapeLayer.strokeColour, shapeLayer.fillColour, shapeLayer.strokeWidth)
                if (!id) return
                shapeLayer.selectedId = id
                // Hand the tool back so the next press grabs the shape you just made instead of
                // starting another one — you almost always want to place it, then adjust it.
                shapeLayer.board.tool = "lasso"
                shapeLayer.toast("Shape placed — drag it or a corner to resize; pick the shape tool again for another")
            }
            onCentroidChanged: if (active) nowPage = shapeLayer.board.toPage(centroid.position)
        }
        // A tap on empty space puts the selection down; the next press draws as usual.
        TapHandler { acceptedDevices: shapeLayer.allDevices; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: shapeLayer.selectedId = 0 }
    }

    // ---- the shapes themselves, in page coordinates
    Item {
        id: pageSpace
        transform: [ Scale { xScale: shapeLayer.board.zoom; yScale: shapeLayer.board.zoom },
                     Translate { x: shapeLayer.board.pan.x; y: shapeLayer.board.pan.y } ]

        Repeater {
            model: items
            delegate: Item {
                id: frame
                objectName: shapeLayer.picking ? "chrome" : ""
                required property int index
                required property var sid
                required property string kind
                required property real sx
                required property real sy
                required property real sw
                required property real sh
                required property string stroke
                required property string fill
                required property real thickness

                // Dragging never writes to the model or to x/y: it moves these offsets, and the
                // committed values come back through the model. Writing to a bound property left
                // the delegate with dead bindings, which is why a reselected shape stopped resizing.
                property real dx: 0
                property real dy: 0
                property real dw: 0
                property real dh: 0
                function resetDrag() { dx = 0; dy = 0; dw = 0; dh = 0 }
                function commit() {
                    // Read and reset first: setGeometry rebuilds the model, which destroys this
                    // delegate — anything after it runs on a corpse.
                    const nw = curW, nh = curH
                    const nx = shapeLayer.clampX(curX, nw), ny = shapeLayer.clampY(curY, nh)
                    const id = sid
                    resetDrag()
                    shapes.setGeometry(id, nx, ny, nw, nh)
                }
                readonly property real curX: sx + dx
                readonly property real curY: sy + dy
                readonly property real curW: sw + dw
                readonly property real curH: sh + dh

                readonly property bool chosen: shapeLayer.selectedId === sid
                readonly property bool isLine: shapeLayer.straight(kind)
                // The box the shape lives in. A line has no thickness, so it gets a grabbable one.
                x: curX + (isLine ? Math.min(0, curW) : 0)
                y: curY + (isLine ? Math.min(0, curH) : 0)
                width: Math.max(Math.abs(curW), 1)
                height: Math.max(Math.abs(curH), 1)

                Shape {
                    x: frame.isLine && frame.curW < 0 ? frame.width : 0
                    y: frame.isLine && frame.curH < 0 ? frame.height : 0
                    width: frame.width; height: frame.height
                    ShapePath {
                        strokeColor: frame.stroke
                        strokeWidth: frame.thickness
                        fillColor: frame.fill.length ? frame.fill : "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        PathSvg { path: shapeLayer.pathFor(frame.kind, frame.isLine ? frame.curW : frame.width,
                                                                      frame.isLine ? frame.curH : frame.height) }
                    }
                }

                SelectionFrame {
                    visible: frame.chosen && shapeLayer.picking
                    anchors.fill: parent
                    anchors.margins: -6 / shapeLayer.zoom
                    colour: pal.highlight
                    thickness: 1.5 / shapeLayer.zoom
                }
                TapHandler {
                    enabled: shapeLayer.picking
                    acceptedDevices: shapeLayer.allDevices
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    margin: 14 / shapeLayer.zoom
                    onTapped: (point, button) => {
                        if (frame.chosen && tapCount >= 2) {         // already selected, tapped again: options
                            const topLeft = frame.mapToItem(shapeLayer, 0, 0)
                            const bottomRight = frame.mapToItem(shapeLayer, frame.width, frame.height)
                            shapeLayer.optionsAsked(Qt.rect(topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y))
                            return
                        }
                        shapeLayer.selectedId = frame.sid
                    }
                }
                DragHandler {
                    enabled: shapeLayer.picking
                    acceptedDevices: shapeLayer.allDevices
                    target: null
                    margin: 14 / shapeLayer.zoom
                    // Without this it takes the press away from a resize handle sitting on the
                    // corner, and the shape moves when you meant to resize it.
                    grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType | PointerHandler.ApprovesTakeOverByAnything
                    onActiveChanged: {
                        if (active) { shapeLayer.selectedId = frame.sid; return }
                        frame.commit()
                    }
                    onTranslationChanged: {
                        frame.dx = translation.x / shapeLayer.zoom
                        frame.dy = translation.y / shapeLayer.zoom
                    }
                }

                // Handles: the two ends of a line, the four corners of everything else.
                Repeater {
                    model: (frame.chosen && shapeLayer.picking) ? (frame.isLine ? 2 : 4) : 0
                    delegate: Rectangle {
                        required property int index
                        readonly property real s: 20 / shapeLayer.zoom
                        readonly property bool lineEnd: frame.isLine
                        width: s; height: s; radius: s / 2
                        color: pal.highlight
                        border.color: pal.base; border.width: 1.5 / shapeLayer.zoom
                        x: (lineEnd ? (index === 0 ? (frame.curW < 0 ? frame.width : 0) : (frame.curW < 0 ? 0 : frame.width))
                                    : (index === 1 || index === 2 ? frame.width : 0)) - s / 2
                        y: (lineEnd ? (index === 0 ? (frame.curH < 0 ? frame.height : 0) : (frame.curH < 0 ? 0 : frame.height))
                                    : (index >= 2 ? frame.height : 0)) - s / 2
                        MouseArea {
                            // A MouseArea, not a handler: it takes the press outright, so the move
                            // handler underneath can never win the corner.
                            anchors.fill: parent
                            anchors.margins: -12 / shapeLayer.zoom
                            preventStealing: true
                            property point from: Qt.point(0, 0)
                            onPressed: (m) => { from = mapToItem(pageSpace, m.x, m.y); shapeLayer.selectedId = frame.sid }
                            onPositionChanged: (m) => {
                                const now = mapToItem(pageSpace, m.x, m.y)
                                const mx = now.x - from.x, my = now.y - from.y
                                if (lineEnd) {
                                    if (index === 0) { frame.dx = mx; frame.dy = my; frame.dw = -mx; frame.dh = -my }
                                    else { frame.dw = mx; frame.dh = my }
                                    return
                                }
                                const left = index === 0 || index === 3, top = index <= 1
                                const nw = Math.max(8, frame.sw + (left ? -mx : mx))
                                const nh = Math.max(8, frame.sh + (top ? -my : my))
                                frame.dw = nw - frame.sw
                                frame.dh = nh - frame.sh
                                frame.dx = left ? frame.sw - nw : 0
                                frame.dy = top ? frame.sh - nh : 0
                            }
                            onReleased: frame.commit()
                            onCanceled: frame.resetDrag()
                        }
                    }
                }
            }
        }

        // ---- live preview: the shape you are dragging out, drawn exactly as it will be kept
        Shape {
            id: preview
            visible: draw.active && !draw.putDown
            readonly property real dx: draw.nowPage.x - draw.startPage.x
            readonly property real dy: draw.nowPage.y - draw.startPage.y
            readonly property bool isLine: shapeLayer.straight(shapeLayer.kind)
            x: isLine ? draw.startPage.x : Math.min(draw.startPage.x, draw.nowPage.x)
            y: isLine ? draw.startPage.y : Math.min(draw.startPage.y, draw.nowPage.y)
            width: Math.max(1, Math.abs(dx)); height: Math.max(1, Math.abs(dy))
            opacity: 0.8
            ShapePath {
                strokeColor: shapeLayer.strokeColour
                strokeWidth: shapeLayer.strokeWidth
                fillColor: shapeLayer.fillColour.length ? shapeLayer.fillColour : "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathSvg {
                    path: shapeLayer.pathFor(shapeLayer.kind,
                                             preview.isLine ? preview.dx : preview.width,
                                             preview.isLine ? preview.dy : preview.height)
                }
            }
        }
    }
}
