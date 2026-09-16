import QtQuick
import QtQuick.Controls.Basic
import Lumen

// Pictures on the page. The canvas draws the pixels (under the ink, so you annotate on top); this
// layer only owns the handles. They appear with the lasso tool — with a pen or highlighter in hand
// you are writing over the picture, not moving it, which is what every ink app does.
Item {
    id: picLayer
    required property InkCanvas board   // not "canvas": that name is shadowed by the id in the parent scope
    property var pageId: 0
    readonly property real z0: board ? board.zoom : 1        // bindings in the delegates run before board is assigned
    readonly property bool editing: board ? board.tool === "lasso" : false
    property var selectedId: 0
    signal toast(string message)

    // Pen, finger, trackpad and mouse: named once so no handler here can quietly leave one out.
    readonly property int allDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus

    SystemPalette { id: pal }
    ListModel { id: pics }

    function reload() {
        pics.clear()
        const list = pageId ? images.list(pageId) : []
        for (const im of list) pics.append({ iid: im.id, ix: im.x, iy: im.y, iw: im.w, ih: im.h, path: im.path })
        board.setImages(list)
        if (selectedId && !list.some(im => im.id === selectedId)) selectedId = 0
    }
    // Drop a picture in the middle of what you are looking at, at a sane size for the page.
    function insertFile(url) {
        if (!pageId) { picLayer.toast("Open a page first"); return }
        const at = board.viewCentrePage()
        const id = images.insertFile(pageId, url, Math.max(0, at.x - 210), Math.max(0, at.y - 150), Math.min(420, board.pageSize.width * 0.6))
        if (id) { selectedId = id; board.tool = "lasso"; picLayer.toast("Picture added — drag it with the lasso tool") }
    }
    function pasteClipboard() {
        if (!pageId) { picLayer.toast("Open a page first"); return true }   // handled: do not also paste ink
        if (!images.clipboardHasImage()) return false
        const at = board.viewCentrePage()
        const id = images.insertClipboard(pageId, Math.max(0, at.x - 210), Math.max(0, at.y - 150), Math.min(420, board.pageSize.width * 0.6))
        if (id) { selectedId = id; board.tool = "lasso"; picLayer.toast("Picture pasted — drag it with the lasso tool") }
        return id > 0
    }
    signal toastAction(string message, string actionLabel, var fn)
    signal optionsAsked(rect where)
    function removeSelected() {
        if (!selectedId) return
        const gone = images.image(selectedId)      // everything needed to put it back
        images.remove(selectedId)
        selectedId = 0
        picLayer.toastAction("Picture removed", "Undo", function() {
            const id = images.insertFile(picLayer.pageId, "file://" + gone.path, gone.x, gone.y, gone.w)
            if (id) images.setGeometry(id, gone.x, gone.y, gone.w, gone.h)
        })
    }

    Connections { target: images; function onChanged(pid) { if (pid === picLayer.pageId) picLayer.reload() } }
    Connections { target: images; function onFailed(message) { picLayer.toast(message) } }
    onPageIdChanged: reload()
    Component.onCompleted: reload()


    Item {
        id: pageSpace
        transform: [ Scale { xScale: picLayer.z0; yScale: picLayer.z0 }, Translate { x: picLayer.board.pan.x; y: picLayer.board.pan.y } ]

        Repeater {
            model: pics
            delegate: Item {
                id: frame
                required property var iid
                required property real ix
                required property real iy
                required property real iw
                required property real ih
                readonly property bool selected: picLayer.selectedId === iid
                objectName: picLayer.editing ? "chrome" : ""      // only a hole for the pen while you are moving pictures
                visible: picLayer.editing
                // Drag and resize move these offsets, never x/y/width/height themselves: assigning to
                // a bound property destroys the binding, so the frame would stop following the model
                // the moment anything changed it from underneath (the ShapeLayer pattern, D-064).
                property real dx: 0
                property real dy: 0
                property real dw: 0
                property real dh: 0
                function resetDrag() { dx = 0; dy = 0; dw = 0; dh = 0 }
                function commit() {
                    const w = width, h = height
                    const sheet = picLayer.board.pageSize
                    const nx = picLayer.board.infinite ? x : Math.max(0, Math.min(x, Math.max(0, sheet.width - w)))
                    const ny = picLayer.board.infinite ? y : Math.max(0, Math.min(y, Math.max(0, sheet.height - h)))
                    const id = iid
                    resetDrag()
                    images.setGeometry(id, nx, ny, w, h)
                }
                x: ix + dx; y: iy + dy
                width: Math.max(24, iw + dw); height: Math.max(24, ih + dh)

                SelectionFrame {
                    anchors.fill: parent
                    colour: frame.selected ? pal.highlight : Qt.alpha(pal.text, 0.35)
                    thickness: (frame.selected ? 2 : 1) / Math.max(picLayer.z0, 0.2)
                }
                TapHandler {
                    acceptedDevices: picLayer.allDevices
                    gesturePolicy: TapHandler.ReleaseWithinBounds
                    onTapped: (point, button) => {
                        if (frame.selected && tapCount >= 2) {
                            const tl = frame.mapToItem(picLayer, 0, 0)
                            const br = frame.mapToItem(picLayer, frame.width, frame.height)
                            picLayer.optionsAsked(Qt.rect(tl.x, tl.y, br.x - tl.x, br.y - tl.y))
                            return
                        }
                        picLayer.selectedId = frame.iid
                    }
                }
                DragHandler {
                    id: move
                    target: null
                    onActiveChanged: {
                        if (active) { picLayer.selectedId = frame.iid; return }
                        frame.commit()
                    }
                    onCanceled: frame.resetDrag()
                    onTranslationChanged: {
                        frame.dx = translation.x / picLayer.z0
                        frame.dy = translation.y / picLayer.z0
                    }
                }

                // corner grip: resize, aspect kept
                Rectangle {
                    visible: frame.selected
                    width: 14 / Math.max(picLayer.z0, 0.2); height: width; radius: width / 2
                    x: parent.width - width / 2; y: parent.height - height / 2
                    color: pal.highlight
                    DragHandler {
                        id: size
                        target: null
                        margin: 16
                        onActiveChanged: if (!active) frame.commit()
                        onCanceled: frame.resetDrag()
                        onTranslationChanged: {
                            const w = Math.max(24, frame.iw + translation.x / picLayer.z0)
                            frame.dw = w - frame.iw
                            frame.dh = w * frame.ih / Math.max(frame.iw, 1) - frame.ih      // aspect kept
                        }
                    }
                }
                // delete
                Rectangle {
                    visible: frame.selected
                    width: 20 / Math.max(picLayer.z0, 0.2); height: width; radius: 4 / Math.max(picLayer.z0, 0.2)
                    x: parent.width - width / 2; y: -height / 2
                    color: Ui.danger
                    Text { anchors.centerIn: parent; text: "×"; color: "white"; font.pixelSize: parent.height * 0.8 }
                    TapHandler { gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: { picLayer.selectedId = frame.iid; picLayer.removeSelected() } }
                }
            }
        }
    }
}
