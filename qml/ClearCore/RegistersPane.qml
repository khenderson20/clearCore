import QtQuick
import QtQuick.Controls.Basic

// ─── RegistersPane.qml — 32 registers, last-written highlighted ───────────────

GridView {
    id: grid

    required property var sim

    clip: true
    model: sim.registers
    cellWidth: Math.max(210, width / Math.max(1, Math.floor(width / 230)))
    cellHeight: 34

    ScrollBar.vertical: ScrollBar {}

    delegate: Item {
        required property int number
        required property string name
        required property string hex
        required property string dec
        required property bool changed

        width: grid.cellWidth
        height: grid.cellHeight

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: Theme.radius
            color: changed ? Theme.alpha(Theme.success, 0.15) : Theme.surfaceRaised
            border.width: changed ? 1 : 0
            border.color: Theme.success

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.s2
                spacing: Theme.s2

                Text {
                    text: name
                    width: 46
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontBody
                    font.weight: changed ? Font.Medium : Font.Normal
                    color: changed ? Theme.success : Theme.accent
                }
                Text {
                    text: hex
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontBody
                    color: Theme.onSurface
                }
                Text {
                    text: dec
                    font.family: Theme.mono
                    font.pixelSize: Theme.fontCaption
                    color: Theme.onSurfaceMutedAAA
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
        }
    }
}
