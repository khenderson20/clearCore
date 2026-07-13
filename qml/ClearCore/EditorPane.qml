import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// ─── EditorPane.qml — MIPS assembly editor ────────────────────────────────────
// Lives beside the datapath (no tab hopping between code and pipeline).
// Assembly errors surface in a persistent strip under the editor, not a modal.

Rectangle {
    id: pane

    required property var sim
    property alias source: editor.text
    property string errorText: ""

    function assemble() {
        errorText = pane.sim.assembleAndLoad(editor.text)
    }

    color: Theme.surface
    radius: Theme.radius
    border.width: 1
    border.color: Theme.outline

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0

        // ── Header ─────────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.s3

            Text {
                text: qsTr("Assembly")
                font.pixelSize: Theme.fontH2
                font.weight: Font.Medium
                color: Theme.onSurface
            }
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("%1 lines").arg(editor.text.split("\n").length)
                font.pixelSize: Theme.fontCaption
                color: Theme.onSurfaceMuted
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.outline }

        // ── Editor ─────────────────────────────────────────────────────────────
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TextArea {
                id: editor
                font.family: Theme.mono
                font.pixelSize: Theme.fontBody
                color: Theme.onSurface
                selectionColor: Theme.alpha(Theme.interactive, 0.4)
                placeholderText: qsTr("# Write MIPS assembly here, or pick an example above.\n# Supported: add, addi, lw, sw, beq, bne, j, jal, shifts, …")
                placeholderTextColor: Theme.onSurfaceMuted
                wrapMode: TextArea.NoWrap
                tabStopDistance: 4 * fontMetrics.advanceWidth("0")
                background: Rectangle { color: Theme.background; radius: 0 }

                FontMetrics { id: fontMetrics; font: editor.font }
            }
        }

        // ── Error strip (persistent, not modal) ────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? errRow.implicitHeight + Theme.s2 * 2 : 0
            visible: pane.errorText.length > 0
            color: Theme.alpha(Theme.danger, 0.12)

            RowLayout {
                id: errRow
                anchors.fill: parent
                anchors.leftMargin: Theme.s3
                anchors.rightMargin: Theme.s3
                spacing: Theme.s2

                Text {
                    text: "\u26a0"
                    color: Theme.danger
                    font.pixelSize: Theme.fontBody
                    Accessible.ignored: true
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Assembly error — %1").arg(pane.errorText)
                    color: Theme.danger
                    font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap
                }
                CCButton {
                    text: qsTr("Dismiss")
                    onClicked: pane.errorText = ""
                }
            }
        }
    }
}
