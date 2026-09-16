import QtQuick

// A selection outline drawn as four solid edges. A transparent Rectangle with a border, scaled
// by the page zoom, blanks its whole interior to zero alpha at high zoom, and on Wayland that
// shows the desktop through the window.
Item {
    id: frameRoot
    property color colour: "black"
    property real thickness: 1
    Rectangle { color: frameRoot.colour; x: 0; y: 0; width: frameRoot.width; height: frameRoot.thickness }
    Rectangle { color: frameRoot.colour; x: 0; y: frameRoot.height - frameRoot.thickness; width: frameRoot.width; height: frameRoot.thickness }
    Rectangle { color: frameRoot.colour; x: 0; y: 0; width: frameRoot.thickness; height: frameRoot.height }
    Rectangle { color: frameRoot.colour; x: frameRoot.width - frameRoot.thickness; y: 0; width: frameRoot.thickness; height: frameRoot.height }
}
