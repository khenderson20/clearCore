import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// ─── MemoryPane.qml — live hex dump ───────────────────────────────────────────

ColumnLayout {
    id: pane

    required property var sim
    property int baseAddress: 0
    property var rows: []

    function refresh() {
        rows = sim.memoryRows(baseAddress, 64)
    }

    Component.onCompleted: refresh()

    Connections {
        target: pane.sim
        function onPipelineChanged() { pane.refresh() }
        function onProgramLoadedChanged() { pane.refresh() }
    }

    spacing: Theme.s2

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s2

        Text {
            text: qsTr("Base address")
            font.pixelSize: Theme.fontBody
            color: Theme.onSurfaceMuted
        }

        TextField {
            id: addrField
            Layout.preferredWidth: 140
            font.family: Theme.mono
            font.pixelSize: Theme.fontBody
            color: Theme.onSurface
            text: "0x00000000"
            selectByMouse: true
            background: Rectangle {
                radius: Theme.radius
                color: Theme.background
                border.width: addrField.activeFocus ? 2 : 1
                border.color: addrField.activeFocus ? Theme.interactive : Theme.outline
            }
            onAccepted: {
                const v = parseInt(text, 16)
                if (!isNaN(v)) { pane.baseAddress = v >>> 0; pane.refresh() }
            }
            Accessible.name: qsTr("Memory base address, hexadecimal")
        }

        CCButton { text: qsTr("Go"); onClicked: addrField.accepted() }
        Item { Layout.fillWidth: true }
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: pane.rows
        reuseItems: true

        ScrollBar.vertical: ScrollBar {}

        delegate: Text {
            required property string modelData
            text: modelData
            font.family: Theme.mono
            font.pixelSize: Theme.fontBody
            color: Theme.onSurface
            padding: 1
        }
    }
}
