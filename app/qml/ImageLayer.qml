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
        for (const im of list) pics.append({ iid: im.id, ix: im.x, iy: im.y, iw: im.w, ih: im.h, path: im.path,
                                             icropX: im.cropX, icropY: im.cropY, icropW: im.cropW, icropH: im.cropH, irotation: im.rotation })
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

    // ---- crop and turn. Neither touches the file: the picture carries the part of itself to show
    // and how far it is turned, so Reset is always one tap away.
    property var croppingId: 0
    property real cropL: 0
    property real cropT: 0
    property real cropR: 0
    property real cropB: 0
    function selectedInfo() { return selectedId ? images.image(selectedId) : ({}) }
    function startCrop() {
        if (!selectedId) return
        cropL = 0; cropT = 0; cropR = 0; cropB = 0
        croppingId = selectedId
        picLayer.toast("Drag the corners to trim the picture")
    }
    function cancelCrop() { croppingId = 0 }
    function applyCrop() {
        const id = croppingId
        if (!id) return
        const was = images.image(id)
        const fx = cropL / was.w, fy = cropT / was.h
        const fw = (was.w - cropL - cropR) / was.w, fh = (was.h - cropT - cropB) / was.h
        croppingId = 0
        if (fw > 0.999 && fh > 0.999) return                    // nothing was trimmed
        images.setCrop(id, was.cropX + was.cropW * fx, was.cropY + was.cropH * fy, was.cropW * fw, was.cropH * fh,
                       was.x + cropL, was.y + cropT, was.w - cropL - cropR, was.h - cropT - cropB)
        picLayer.toastAction("Picture trimmed", "Undo", function() {
            images.setCrop(id, was.cropX, was.cropY, was.cropW, was.cropH, was.x, was.y, was.w, was.h, was.rotation)
        })
    }
    function rotateSelected() {
        const id = selectedId
        if (!id) return
        images.rotate(id, 1)
        picLayer.toastAction("Picture turned", "Undo", function() { images.rotate(id, 3) })
    }
    function resetSelected() {
        const id = selectedId
        if (!id) return
        const was = images.image(id)
        if (was.cropW >= 1 && was.cropH >= 1 && was.rotation === 0) { picLayer.toast("This picture is whole already"); return }
        images.resetCrop(id)
        picLayer.toastAction("Picture back to the original", "Undo", function() {
            images.setCrop(id, was.cropX, was.cropY, was.cropW, was.cropH, was.x, was.y, was.w, was.h, was.rotation)
        })
    }
    onSelectedIdChanged: if (croppingId && croppingId !== selectedId) croppingId = 0

    Connections { target: images; function onChanged(pid) { if (pid === picLayer.pageId) picLayer.reload() } }
    Connections { target: images; function onFailed(message) { picLayer.toast(message) } }
    onPageIdChanged: reload()
    Component.onCompleted: reload()


    // The buttons for a trim live outside the zoomed page, so they stay the size of a fingertip.
    Rectangle {
        id: cropBar
        objectName: "chrome"
        visible: picLayer.croppingId > 0
        readonly property var pic: visible ? images.image(picLayer.croppingId) : ({})
        readonly property real centreX: visible ? (pic.x + pic.w / 2) * picLayer.z0 + picLayer.board.pan.x : 0
        readonly property real bottomY: visible ? (pic.y + pic.h) * picLayer.z0 + picLayer.board.pan.y : 0
        x: Math.max(8, Math.min(picLayer.width - width - 8, centreX - width / 2))
        y: Math.max(8, Math.min(picLayer.height - height - 8, bottomY + 12))
        z: 5
        implicitWidth: cropRow.implicitWidth + 20; implicitHeight: Ui.target + 8
        radius: height / 2
        color: Qt.alpha(pal.window, 0.96)
        border.color: Qt.alpha(pal.text, 0.18); border.width: 1
        Row {
            id: cropRow
            anchors.centerIn: parent
            spacing: 6
            component CropAction: Rectangle {
                property string label: ""
                property bool primary: false
                signal clicked()
                width: Math.max(Ui.target + 14, cropLabel.implicitWidth + 26); height: Ui.target
                radius: Ui.radiusSm
                color: primary ? pal.highlight : (cropTap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : "transparent")
                border.color: primary ? "transparent" : Qt.alpha(pal.text, Ui.borderAlpha); border.width: primary ? 0 : 1
                Accessible.role: Accessible.Button
                Accessible.name: label
                Text { id: cropLabel; anchors.centerIn: parent; text: parent.label; font.pixelSize: Ui.text
                       color: parent.primary ? Ui.onAccent(pal.highlight) : pal.windowText }
                TapHandler { id: cropTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: parent.clicked() }
            }
            CropAction { objectName: "cropDone"; label: "Trim"; primary: true; onClicked: picLayer.applyCrop() }
            CropAction { objectName: "cropCancel"; label: "Cancel"; onClicked: picLayer.cancelCrop() }
        }
    }

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
                    enabled: picLayer.croppingId !== frame.iid
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

                // ---- trimming: what is being cut away is dimmed, and the four corners move the cut
                Item {
                    id: cropUi
                    anchors.fill: parent
                    visible: picLayer.croppingId === frame.iid
                    readonly property real grip: 20 / Math.max(picLayer.z0, 0.2)
                    readonly property color shade: "#000000"
                    Rectangle { color: cropUi.shade; opacity: 0.45; x: 0; y: 0; width: parent.width; height: picLayer.cropT }
                    Rectangle { color: cropUi.shade; opacity: 0.45; x: 0; y: parent.height - picLayer.cropB; width: parent.width; height: picLayer.cropB }
                    Rectangle { color: cropUi.shade; opacity: 0.45; x: 0; y: picLayer.cropT; width: picLayer.cropL; height: parent.height - picLayer.cropT - picLayer.cropB }
                    Rectangle { color: cropUi.shade; opacity: 0.45; x: parent.width - picLayer.cropR; y: picLayer.cropT; width: picLayer.cropR; height: parent.height - picLayer.cropT - picLayer.cropB }
                    Rectangle {
                        x: picLayer.cropL; y: picLayer.cropT
                        width: parent.width - picLayer.cropL - picLayer.cropR
                        height: parent.height - picLayer.cropT - picLayer.cropB
                        color: "transparent"; border.color: pal.highlight; border.width: 2 / Math.max(picLayer.z0, 0.2)
                    }
                    component CropGrip: Rectangle {
                        property int sx: -1          // which corner: -1 left/top, 1 right/bottom
                        property int sy: -1
                        width: cropUi.grip; height: cropUi.grip; radius: width / 4
                        color: pal.highlight
                        x: (sx < 0 ? picLayer.cropL : cropUi.width - picLayer.cropR) - width / 2
                        y: (sy < 0 ? picLayer.cropT : cropUi.height - picLayer.cropB) - height / 2
                        DragHandler {
                            target: null
                            margin: cropUi.grip           // a fingertip either side of the grip itself
                            property real l0: 0
                            property real t0: 0
                            property real r0: 0
                            property real b0: 0
                            onActiveChanged: if (active) { l0 = picLayer.cropL; t0 = picLayer.cropT; r0 = picLayer.cropR; b0 = picLayer.cropB }
                            onTranslationChanged: {
                                const dx = translation.x / picLayer.z0, dy = translation.y / picLayer.z0
                                const keep = 24                     // never trim a picture to nothing
                                if (parent.sx < 0) picLayer.cropL = Math.max(0, Math.min(l0 + dx, cropUi.width - picLayer.cropR - keep))
                                else picLayer.cropR = Math.max(0, Math.min(r0 - dx, cropUi.width - picLayer.cropL - keep))
                                if (parent.sy < 0) picLayer.cropT = Math.max(0, Math.min(t0 + dy, cropUi.height - picLayer.cropB - keep))
                                else picLayer.cropB = Math.max(0, Math.min(b0 - dy, cropUi.height - picLayer.cropT - keep))
                            }
                        }
                    }
                    CropGrip { objectName: "cropGrip"; sx: -1; sy: -1 }
                    CropGrip { objectName: "cropGrip"; sx: 1; sy: -1 }
                    CropGrip { objectName: "cropGrip"; sx: -1; sy: 1 }
                    CropGrip { objectName: "cropGrip"; sx: 1; sy: 1 }
                }

                // corner grip: resize, aspect kept
                Rectangle {
                    visible: frame.selected && picLayer.croppingId !== frame.iid
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
                    visible: frame.selected && picLayer.croppingId !== frame.iid
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
