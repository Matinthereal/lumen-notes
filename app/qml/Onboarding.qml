import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// First run: what this machine has to write with decides the defaults, and you can change any of
// them here rather than hunting through Settings. Then four things worth knowing, and out of the
// way. F1 brings it back.
Rectangle {
    id: page
    objectName: "chrome"
    signal done()
    signal handChosen(bool left)
    SystemPalette { id: pal }
    color: Qt.alpha(pal.window, 0.96)
    focus: visible
    Keys.onEscapePressed: done()

    property bool hasPen: false
    property bool hasTouch: false
    // What was found, and what that makes sensible. Nothing is written until Start. Read when it
    // is shown, not when it is built: a pen or a touchscreen can appear after the app starts.
    property string pageKind: "typed"
    property string keyboardMode: "never"
    property bool leftHanded: false
    function look() {
        hasPen = tabletMode.penAvailable
        hasTouch = tabletMode.touchAvailable
        const kind = library.setting("page.sizeMode", "")
        pageKind = kind.length ? kind : (hasPen ? "a4" : "typed")
        const keyboard = library.setting("keyboard.mode", "")
        keyboardMode = keyboard.length ? keyboard : (hasTouch ? "tablet" : "never")
        leftHanded = library.setting("ui.leftHanded", "0") === "1"
    }
    onVisibleChanged: if (visible) look()
    Component.onCompleted: if (visible) look()
    readonly property string found: {
        const bits = []
        if (hasPen) bits.push("a pen")
        if (hasTouch) bits.push("a touchscreen")
        bits.push("a keyboard")
        return bits.join(", ").replace(/, ([^,]*)$/, " and $1")
    }

    function start() {
        library.setSetting("page.sizeMode", page.pageKind)
        library.setSetting("keyboard.mode", page.keyboardMode)
        library.setSetting("ui.leftHanded", page.leftHanded ? "1" : "0")
        page.handChosen(page.leftHanded)
        page.done()
    }

    component Choice: Rectangle {
        property string label: ""
        property bool on: false
        signal chosen()
        implicitWidth: choiceText.implicitWidth + 26; implicitHeight: Ui.target
        radius: Ui.radiusSm
        color: on ? pal.highlight : (choiceTap.pressed ? Qt.alpha(pal.text, Ui.pressAlpha) : "transparent")
        border.color: on ? "transparent" : Qt.alpha(pal.text, Ui.borderAlpha); border.width: on ? 0 : 1
        Accessible.role: Accessible.RadioButton
        Accessible.name: label
        Text { id: choiceText; anchors.centerIn: parent; text: parent.label; font.pixelSize: Ui.text
               color: parent.on ? Ui.onAccent(pal.highlight) : pal.windowText }
        TapHandler { id: choiceTap; gesturePolicy: TapHandler.ReleaseWithinBounds; onTapped: parent.chosen() }
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(640, page.width - 48); height: Math.min(col.implicitHeight + 56, page.height - 32); radius: Ui.radiusLg
        color: pal.base; border.color: Qt.alpha(pal.text, 0.16); border.width: 1
        ColumnLayout {
            id: col
            anchors { fill: parent; margins: 28 }
            spacing: 12
            Text { text: "Lumen"; color: pal.windowText; font.pixelSize: 26; font.weight: Font.DemiBold }
            Text { text: "This computer has " + page.found + ", so Lumen has set itself up this way:"
                   color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.text; wrapMode: Text.Wrap; Layout.fillWidth: true }

            component SetupRow: RowLayout { spacing: 8; Layout.fillWidth: true }
            component SetupLabel: Text { color: pal.text; font.pixelSize: Ui.text; Layout.preferredWidth: 150 }

            SetupRow {
                SetupLabel { text: "New pages are" }
                Choice { objectName: "setupTyped"; label: "Typed"; on: page.pageKind === "typed"; onChosen: page.pageKind = "typed" }
                Choice { objectName: "setupInk"; label: "Handwritten"; on: page.pageKind === "a4"; onChosen: page.pageKind = "a4" }
                Item { Layout.fillWidth: true }
            }
            SetupRow {
                visible: page.hasPen || page.hasTouch
                SetupLabel { text: "You write with" }
                Choice { objectName: "setupRight"; label: "Right hand"; on: !page.leftHanded; onChosen: page.leftHanded = false }
                Choice { objectName: "setupLeft"; label: "Left hand"; on: page.leftHanded; onChosen: page.leftHanded = true }
                Item { Layout.fillWidth: true }
            }
            SetupRow {
                visible: page.hasTouch
                SetupLabel { text: "On-screen keyboard" }
                Choice { label: "In tablet mode"; on: page.keyboardMode === "tablet"; onChosen: page.keyboardMode = "tablet" }
                Choice { label: "Always"; on: page.keyboardMode === "always"; onChosen: page.keyboardMode = "always" }
                Choice { label: "Never"; on: page.keyboardMode === "never"; onChosen: page.keyboardMode = "never" }
                Item { Layout.fillWidth: true }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Qt.alpha(pal.text, Ui.hairline); Layout.topMargin: 4 }

            component Tip: RowLayout { property string icon; property string body; spacing: 12; Layout.fillWidth: true
                Icon { name: "" + icon; implicitWidth: 24; implicitHeight: 24; Layout.alignment: Qt.AlignTop }
                Text { text: body; color: pal.text; font.pixelSize: Ui.small + 1; wrapMode: Text.Wrap; Layout.fillWidth: true } }
            Tip { icon: "insert-text"; body: "Just start typing on a typed page. Ctrl+B, Ctrl+I and Ctrl+U format text, Ctrl+Alt+1–3 make headings." }
            Tip { visible: page.hasPen || page.hasTouch; icon: "draw-freehand"
                  body: "On a handwritten page the toolbar at the top holds the pen, highlighter, eraser and lasso. Hold any button to see what it does." }
            Tip { icon: "edit-find"; body: "Ctrl+K searches everything you have typed and every PDF you have imported — and, with the AI add-on, your handwriting and lesson transcripts." }
            Tip { icon: "folder"; body: "Notebooks hold sections, sections hold pages. Everything is saved as you go and stays on this computer." }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Press F1 to see this again."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: Ui.small; Layout.fillWidth: true }
                Button { objectName: "onboardingStart"; text: "Start"; implicitHeight: Ui.target; onClicked: page.start() }
            }
        }
    }
    MouseArea { anchors.fill: parent; z: -1; acceptedButtons: Qt.AllButtons; hoverEnabled: true }   // really swallow taps on the dim
}
