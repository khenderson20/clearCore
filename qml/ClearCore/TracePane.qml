import QtQuick
import QtQuick.Controls.Basic

// ─── TracePane.qml — instruction × cycle grid (P&H pipeline diagram) ──────────

ListView {
    id: list

    required property var sim

    clip: true
    model: sim.traceRows
    reuseItems: true
    spacing: 1

    ScrollBar.vertical: ScrollBar {}
    ScrollBar.horizontal: ScrollBar {}
    flickableDirection: Flickable.HorizontalAndVerticalFlick
    contentWidth: 340 + 30 * 40   // label + up to ~40 visible cycle cells

    delegate: Row {
        required property var modelData

        spacing: 2

        Text {
            width: 330
            text: modelData.label
            font.family: Theme.mono
            font.pixelSize: Theme.fontCaption
            color: Theme.onSurface
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }

        // Leading offset — instruction entered at start cycle
        Repeater {
            model: Math.max(0, Math.min(Number(modelData.start) - 1, 200))
            delegate: Item { width: 28; height: 20 }
        }

        Repeater {
            model: modelData.cells

            delegate: Rectangle {
                required property string modelData

                readonly property color cellColor:
                    modelData === "**" ? Theme.warning
                    : modelData.trim().length > 0 ? Theme.accent
                    : "transparent"

                width: 28; height: 20
                radius: 3
                color: modelData.trim().length > 0 ? Theme.alpha(cellColor, 0.18) : "transparent"
                border.width: modelData.trim().length > 0 ? 1 : 0
                border.color: cellColor

                Text {
                    anchors.centerIn: parent
                    text: parent.modelData
                    font.family: Theme.mono
                    font.pixelSize: 10
                    color: parent.cellColor
                }
            }
        }
    }

    Text {
        anchors.centerIn: parent
        visible: list.count === 0
        text: qsTr("Step or run a program to build the pipeline trace.")
        font.pixelSize: Theme.fontBody
        color: Theme.onSurfaceMuted
    }
}
