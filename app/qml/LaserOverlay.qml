import QtQuick
import QtQuick.Shapes
import Lumen

// Presentation mode's pen layer: the pen (or mouse) leaves a bright line that fades a second later,
// a finger swipes or taps to move between slides, and neither touches the page underneath.
Item {
    id: laser
    property color colour: "#FF453A"
    property int holdMs: 700          // full brightness before it starts to go
    property int fadeMs: 900
    property real thickness: 4
    signal advance(int delta)
    signal tapped(point position)

    // Strokes that have been let go of, each {pts, born}; the one being drawn is `live`.
    property var trails: []
    property var live: []
    property real now: 0
    property bool drawing: pointer.active

    function alphaFor(born) {
        const age = laser.now - born - laser.holdMs
        return age <= 0 ? 1 : Math.max(0, 1 - age / laser.fadeMs)
    }
    function sweep() {
        now = Date.now()
        const kept = trails.filter(t => laser.alphaFor(t.born) > 0)
        if (kept.length !== trails.length) trails = kept
    }
    Timer {
        interval: 40
        repeat: true
        running: laser.trails.length > 0
        onTriggered: laser.sweep()
    }

    PointHandler {
        id: pointer
        // The pen and the mouse draw; a finger is for turning pages, which is what a room full of
        // people expects a swipe to do.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
        onActiveChanged: {
            if (active) { laser.live = [point.position] }
            else if (laser.live.length > 1) { laser.trails = laser.trails.concat([{ pts: laser.live, born: Date.now() }]); laser.now = Date.now(); laser.live = [] }
            else laser.live = []
        }
        onPointChanged: {
            if (!active) return
            const last = laser.live.length ? laser.live[laser.live.length - 1] : null
            if (last && Math.abs(last.x - point.position.x) + Math.abs(last.y - point.position.y) < 1.5) return
            laser.live = laser.live.concat([point.position])
        }
    }
    DragHandler {
        // Finger only: a swipe left goes on, a swipe right goes back.
        acceptedDevices: PointerDevice.TouchScreen
        target: null
        yAxis.enabled: false
        onActiveChanged: {
            if (active) return
            if (translation.x < -80) laser.advance(1)
            else if (translation.x > 80) laser.advance(-1)
        }
    }
    TapHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen | PointerDevice.Stylus
        onTapped: (ev) => laser.tapped(ev.position)
    }

    Repeater {
        model: laser.trails
        delegate: Shape {
            id: trail
            required property var modelData
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            opacity: laser.alphaFor(modelData.born)
            ShapePath {
                strokeColor: laser.colour
                strokeWidth: laser.thickness
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline { path: trail.modelData.pts }
            }
        }
    }
    Shape {
        anchors.fill: parent
        visible: laser.live.length > 1
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: laser.colour
            strokeWidth: laser.thickness
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathPolyline { path: laser.live }
        }
    }
    // The pointer itself, so a still hand still shows where you are looking.
    Rectangle {
        visible: laser.drawing && laser.live.length > 0
        x: laser.live.length ? laser.live[laser.live.length - 1].x - width / 2 : 0
        y: laser.live.length ? laser.live[laser.live.length - 1].y - height / 2 : 0
        width: laser.thickness * 3.5; height: width; radius: width / 2
        color: Qt.alpha(laser.colour, 0.85)
    }
}
