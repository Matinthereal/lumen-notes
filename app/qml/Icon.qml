import QtQuick
import QtQuick.Shapes
import Lumen

// One icon set, drawn rather than themed: every glyph is a stroked path on the same 24-unit grid
// with the same weight, so the toolbar and the rail read as one family. A name this does not know
// falls back to the desktop theme, so nothing ever disappears.
Item {
    id: icon
    property string name: ""
    property color colour: pal.windowText
    property real weight: 1.7
    property bool filled: false

    SystemPalette { id: pal }
    implicitWidth: Ui.icon
    implicitHeight: Ui.icon

    readonly property string path: {
        switch (name) {
        // ---- tools
        case "draw-freehand":   return "M 4 20 L 7 19 L 19 7 A 2.1 2.1 0 0 0 16 4 L 4 16 Z M 15 5 L 18 8"
        case "draw-highlight":  return "M 4 20 L 9 20 M 6 16 L 12 4 L 19 8 L 12 18 Z"
        case "draw-eraser":     return "M 6 19 L 19 19 M 4 14 L 10 20 L 20 10 L 14 4 Z"
        case "edit-select-lasso": return "M 12 4 C 5 4 3 9 5 13 C 7 17 12 18 12 18 C 19 18 21 13 19 9 C 17 6 14 4 12 4 M 8 18 A 2 2 0 1 0 8 22 A 2 2 0 1 0 8 18"
        case "edit-select-text": return "M 4 18 L 8 6 L 12 18 M 5.5 14 L 10.5 14 M 16 18 L 16 10 M 20 10 L 20 18 M 16 12 A 2.4 2.4 0 0 1 20 12"
        case "insert-text":     return "M 4 6 L 20 6 M 12 6 L 12 19 M 8 19 L 16 19"
        case "draw-rectangle":  return "M 4 6 L 20 6 L 20 18 L 4 18 Z"
        case "draw-ellipse":    return "M 12 5 A 8 6.5 0 1 1 11.9 5"
        case "draw-triangle":   return "M 12 5 L 20 19 L 4 19 Z"
        case "draw-line":       return "M 5 19 L 19 5"
        case "draw-arrow":      return "M 5 19 L 19 5 M 12 5 L 19 5 L 19 12"
        case "insert-image":    return "M 4 5 L 20 5 L 20 19 L 4 19 Z M 4 15 L 9 11 L 13 15 L 16 13 L 20 16 M 15.5 8.5 A 1.2 1.2 0 1 1 15.4 8.5"
        // ---- actions
        case "edit-undo":       return "M 9 7 L 4 11 L 9 15 M 4 11 L 14 11 A 5 5 0 0 1 14 21 L 11 21"
        case "edit-redo":       return "M 15 7 L 20 11 L 15 15 M 20 11 L 10 11 A 5 5 0 0 0 10 21 L 13 21"
        case "overflow-menu":   return "M 12 5.5 A 1.2 1.2 0 1 1 11.9 5.5 M 12 11.5 A 1.2 1.2 0 1 1 11.9 11.5 M 12 17.5 A 1.2 1.2 0 1 1 11.9 17.5"
        case "edit-delete":     return "M 5 7 L 19 7 M 10 7 L 10 4.5 L 14 4.5 L 14 7 M 6.5 7 L 7.5 20 L 16.5 20 L 17.5 7 M 10 11 L 10 17 M 14 11 L 14 17"
        case "edit-copy":       return "M 8 8 L 8 4 L 20 4 L 20 16 L 16 16 M 4 8 L 16 8 L 16 20 L 4 20 Z"
        case "list-add":        return "M 12 5 L 12 19 M 5 12 L 19 12"
        // page styles — each shows its own ruling, so the menu reads at a glance
        case "paper-dotted":    return "M 6 8 L 6.01 8 M 12 8 L 12.01 8 M 18 8 L 18.01 8 M 6 14 L 6.01 14 M 12 14 L 12.01 14 M 18 14 L 18.01 14 M 6 20 L 6.01 20 M 12 20 L 12.01 20 M 18 20 L 18.01 20"
        case "paper-lined":     return "M 4 7 L 20 7 M 4 12 L 20 12 M 4 17 L 20 17"
        case "paper-grid":      return "M 4 8 L 20 8 M 4 13 L 20 13 M 4 18 L 20 18 M 8 4 L 8 20 M 13 4 L 13 20 M 18 4 L 18 20"
        case "paper-graph":     return "M 4 6 L 20 6 M 4 9 L 20 9 M 4 12 L 20 12 M 4 15 L 20 15 M 4 18 L 20 18 M 6 4 L 6 20 M 12 4 L 12 20 M 18 4 L 18 20"
        case "paper-isometric": return "M 4 18 L 12 4 M 12 4 L 20 18 M 4 10 L 20 10 M 8 18 L 14 8 M 10 8 L 16 18"
        case "paper-music":     return "M 4 6 L 20 6 M 4 9 L 20 9 M 4 12 L 20 12 M 4 15 L 20 15 M 4 18 L 20 18"
        case "paper-cornell":   return "M 4 4 L 20 4 L 20 20 L 4 20 Z M 9 4 L 9 16 M 4 16 L 20 16"
        case "paper-plain":     return "M 5 3.5 L 19 3.5 L 19 20.5 L 5 20.5 Z"
        case "document-new":    return "M 6 3.5 L 14 3.5 L 19 8.5 L 19 20.5 L 6 20.5 Z M 14 3.5 L 14 8.5 L 19 8.5"
        case "document-open":   return "M 3.5 19 L 6 10 L 21 10 L 18.5 19 Z M 3.5 19 L 3.5 6 L 9 6 L 11 8.5 L 17 8.5 L 17 10"
        case "document-export": return "M 6 3.5 L 14 3.5 L 19 8.5 L 19 20.5 L 6 20.5 Z M 12 17 L 12 10 M 9 13 L 12 10 L 15 13"
        case "window-close":    return "M 6 6 L 18 18 M 18 6 L 6 18"
        case "view-refresh":    return "M 20 6 L 20 11 L 15 11 M 19.2 11 A 7.5 7.5 0 1 0 18 16"
        case "edit-clear-all":  return "M 5 6 L 19 6 M 5 12 L 15 12 M 5 18 L 11 18"
        case "edit-rename":     return "M 4 20 L 7 19 L 19 7 A 2.1 2.1 0 0 0 16 4 L 4 16 Z M 14 6 L 18 10"
        case "edit-select-all": return "M 4 8 L 4 4 L 8 4 M 16 4 L 20 4 L 20 8 M 20 16 L 20 20 L 16 20 M 8 20 L 4 20 L 4 16 M 8.5 12 L 11 14.5 L 16 9"
        case "color-picker":    return "M 4 20 L 4 16 L 14 6 L 18 10 L 8 20 Z M 13 5 L 16 2 L 22 8 L 19 11"
        case "bookmarks":       return "M 7 4 L 17 4 L 17 20 L 12 16 L 7 20 Z"
        case "edit-clear":      return "M 7 4 L 17 4 L 17 20 L 12 16 L 7 20 Z M 4 4 L 20 20"
        case "folder":          return "M 3.5 18.5 L 3.5 6 L 9 6 L 11 8.5 L 20.5 8.5 L 20.5 18.5 Z"
        case "folder-documents": return "M 3.5 18.5 L 3.5 6 L 9 6 L 11 8.5 L 20.5 8.5 L 20.5 18.5 Z M 8 12 L 16 12 M 8 15.5 L 13 15.5"
        case "text-x-generic":  return "M 6 3.5 L 14 3.5 L 19 8.5 L 19 20.5 L 6 20.5 Z M 14 3.5 L 14 8.5 L 19 8.5 M 9 13 L 16 13 M 9 16.5 L 14 16.5"
        // ---- rail
        case "view-sidetree":   return "M 3.5 5 L 20.5 5 L 20.5 19 L 3.5 19 Z M 10 5 L 10 19 M 13 9 L 18 9 M 13 13 L 17 13"
        case "edit-find":       return "M 11 4 A 6.5 6.5 0 1 1 10.9 4 M 15.6 15.6 L 20 20"
        case "view-preview":    return "M 3.5 4.5 L 10.5 4.5 L 10.5 11 L 3.5 11 Z M 13.5 4.5 L 20.5 4.5 L 20.5 11 L 13.5 11 Z M 3.5 13.5 L 10.5 13.5 L 10.5 20 L 3.5 20 Z M 13.5 13.5 L 20.5 13.5 L 20.5 20 L 13.5 20 Z"
        case "tools-wizard":    return "M 12 3 L 13.6 8.4 L 19 10 L 13.6 11.6 L 12 17 L 10.4 11.6 L 5 10 L 10.4 8.4 Z M 18 16 L 18.8 18.2 L 21 19 L 18.8 19.8 L 18 22 L 17.2 19.8 L 15 19 L 17.2 18.2 Z"
        case "view-list-text":  return "M 4 6 L 20 6 M 4 10.5 L 20 10.5 M 4 15 L 16 15 M 4 19.5 L 12 19.5"
        case "view-list-details": return "M 4.5 6.5 L 19.5 6.5 L 19.5 17.5 L 4.5 17.5 Z M 4.5 10 L 19.5 10 M 9 10 L 9 17.5"
        case "document-edit-verify": return "M 6 3.5 L 14 3.5 L 19 8.5 L 19 20.5 L 6 20.5 Z M 14 3.5 L 14 8.5 L 19 8.5 M 9 14 L 11.5 16.5 L 16 11.5"
        case "input-tablet":    return "M 5.5 3.5 L 18.5 3.5 L 18.5 20.5 L 5.5 20.5 Z M 10 17.5 L 14 17.5"
        case "user-trash":      return "M 5 7 L 19 7 M 10 7 L 10 4.5 L 14 4.5 L 14 7 M 6.5 7 L 7.5 20 L 16.5 20 L 17.5 7"
        case "configure":       return "M 12 8.5 A 3.5 3.5 0 1 1 11.9 8.5 M 12 2.5 L 12 5 M 12 19 L 12 21.5 M 4.2 7 L 6.4 8.2 M 17.6 15.8 L 19.8 17 M 4.2 17 L 6.4 15.8 M 17.6 8.2 L 19.8 7"
        case "document-import": return "M 6 3.5 L 14 3.5 L 19 8.5 L 19 20.5 L 6 20.5 Z M 12 10 L 12 17 M 9 14 L 12 17 L 15 14"
        case "view-presentation": return "M 3.5 5 L 20.5 5 L 20.5 15 L 3.5 15 Z M 12 15 L 12 19 M 8 21 L 12 19 L 16 21"
        case "media-playback-start": return "M 8 5 L 19 12 L 8 19 Z"
        case "media-playback-pause": return "M 9 5 L 9 19 M 15 5 L 15 19"
        case "folder-add": case "folder-new": return "M 3.5 18.5 L 3.5 6 L 9 6 L 11 8.5 L 20.5 8.5 L 20.5 18.5 Z M 12 11.5 L 12 16 M 9.75 13.75 L 14.25 13.75"
        default: return ""
        }
    }

    // Anything not drawn here still shows the desktop's icon rather than nothing.
    Image {
        anchors.fill: parent
        visible: icon.path.length === 0 && icon.name.length > 0
        source: visible ? "image://theme/" + icon.name : ""
        sourceSize: Qt.size(icon.width, icon.height)
        smooth: true
    }

    Shape {
        anchors.fill: parent
        visible: icon.path.length > 0
        transform: Scale { xScale: icon.width / 24; yScale: icon.height / 24 }
        ShapePath {
            strokeColor: icon.colour
            strokeWidth: icon.weight
            fillColor: icon.filled ? Qt.alpha(icon.colour, 0.22) : "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: icon.path }
        }
    }
}
