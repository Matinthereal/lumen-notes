import QtQuick
import Lumen

// Phase 0's pen probe, kept as a diagnostics page (lumen --probe).
Item {
    id: root
    SystemPalette { id: pal }
    property alias probe: penProbe

    Readout {
        id: readout
        probe: penProbe
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 360
    }

    Rectangle {
        id: paper
        anchors { left: readout.right; right: parent.right; top: parent.top; bottom: parent.bottom }
        color: pal.base

        // 5 mm dot grid at 96 dpi ≈ 19 px — the default page style, so the probe already looks like a page.
        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);
                ctx.fillStyle = Qt.alpha(pal.text, 0.18);
                for (let y = 19; y < height; y += 19)
                    for (let x = 19; x < width; x += 19)
                        ctx.fillRect(x - 0.75, y - 0.75, 1.5, 1.5);
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }

        PenProbeItem {
            id: penProbe
            objectName: "penProbe"
            anchors.fill: parent
            penColor: pal.text
        }

        Text {
            anchors.centerIn: parent
            visible: penProbe.pointCount === 0
            text: "Write here with your pen"
            color: pal.mid
            font.pixelSize: 20
        }
    }
}
