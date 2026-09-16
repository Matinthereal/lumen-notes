import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Lumen

// Weak-topics dashboard for one subject across all its papers: topic bars, error types, trend.
Rectangle {
    id: page
    objectName: "chrome"
    property string subject: ""
    signal closed()
    SystemPalette { id: pal }
    // A Rectangle accepts no buttons, so without this a drag here would draw ink on the page behind.
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; hoverEnabled: true; onWheel: (w) => w.accepted = true }

    color: pal.window
    focus: visible
    Keys.onEscapePressed: closed()
    property var weak: []
    property var errors: []
    property var trend: []
    function reload() { weak = papers.weakTopics(subject, 365); errors = papers.errorBreakdown(subject, 365); trend = papers.trend(subject, 365) }
    onVisibleChanged: if (visible) { reload(); forceActiveFocus() }
    onSubjectChanged: if (visible) reload()

    ColumnLayout {
        anchors { fill: parent; margins: 28 }
        spacing: 18
        RowLayout {
            Text { text: page.subject + " — past papers"; color: pal.windowText; font.pixelSize: 22; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Button { text: "Close (Esc)"; onClicked: page.closed() }
        }
        RowLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: 24
            // Weak topics
            ColumnLayout {
                // preferredWidth is pixels, not a flex ratio: both columns must fill, 3:2 by preference
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: parent.width * 0.58
                Text { text: "Weakest topics first (marks scored ÷ available, all attempts)"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 6
                    model: page.weak
                    delegate: ColumnLayout {
                        required property var modelData
                        width: ListView.view.width; spacing: 2
                        RowLayout {
                            Text { text: modelData.topic; color: pal.text; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight }
                            Text { text: Math.round(modelData.percent) + "%  ·  " + modelData.scored + "/" + modelData.available + "  ·  " + modelData.questions + " q"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 }
                        }
                        Rectangle {
                            Layout.fillWidth: true; implicitHeight: 10; radius: 5; color: Qt.alpha(pal.text, 0.08)
                            Rectangle { width: parent.width * Math.min(1, modelData.percent / 100); height: parent.height; radius: 5; color: modelData.percent < 50 ? Ui.danger : modelData.percent < 75 ? Ui.warning : Ui.good }
                        }
                    }
                }
                Text { visible: page.weak.length === 0; text: "No marked questions yet. Do a paper, enter marks per question with topic tags, and this fills in."; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 13; wrapMode: Text.Wrap; Layout.fillWidth: true }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.preferredWidth: parent.width * 0.38; spacing: 18
                ColumnLayout {
                    Text { text: "Where marks go"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 }
                    Repeater {
                        model: page.errors
                        delegate: RowLayout {
                            required property var modelData
                            Text { text: modelData.errorType; color: pal.text; font.pixelSize: 13; Layout.preferredWidth: 150 }
                            Text { text: modelData.marksLost + " marks lost · " + modelData.questions + " q"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 }
                        }
                    }
                }
                ColumnLayout {
                    Layout.fillHeight: true
                    Text { text: "Trend (finished attempts)"; color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12 }
                    Text { visible: page.trend.length === 0; text: "Finish a timed attempt and its score lands here."
                           color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12; wrapMode: Text.Wrap; Layout.fillWidth: true }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 4
                        model: page.trend
                        delegate: RowLayout {
                            required property var modelData
                            width: ListView.view.width
                            Text { text: new Date(modelData.started * 1000).toLocaleDateString(Qt.locale(), "d MMM"); color: Qt.alpha(pal.windowText, Ui.mutedAlpha); font.pixelSize: 12; Layout.preferredWidth: 60 }
                            Text { text: modelData.paper; color: pal.text; font.pixelSize: 13; Layout.fillWidth: true; elide: Text.ElideRight }
                            Rectangle { implicitWidth: 120; implicitHeight: 8; radius: 4; color: Qt.alpha(pal.text, 0.08); Rectangle { width: parent.width * Math.min(1, modelData.percent / 100); height: parent.height; radius: 4; color: pal.highlight } }
                            Text { text: Math.round(modelData.percent) + "%"; color: pal.text; font.pixelSize: 12; Layout.preferredWidth: 40; horizontalAlignment: Text.AlignRight }
                        }
                    }
                }
            }
        }
    }
}
