import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// ─── TransportBar.qml — top toolbar ───────────────────────────────────────────
// Layout follows the Z-pattern: identity top-left, transport centre,
// live status top-right. Primary CTA = Run/Pause (single filled button).

Rectangle {
    id: bar

    required property var sim          // Simulator instance
    signal loadExampleRequested(int index)
    signal assembleRequested()

    implicitHeight: 48
    color: Theme.surface
    border.width: 0

    Rectangle {                        // bottom divider
        width: parent.width; height: 1
        anchors.bottom: parent.bottom
        color: Theme.outline
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s4
        anchors.rightMargin: Theme.s4
        spacing: Theme.s3

        // ── Identity ───────────────────────────────────────────────────────────
        Text {
            text: qsTr("clearCore")
            font.pixelSize: Theme.fontH1
            font.weight: Font.Medium
            color: Theme.accent
        }

        Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.outline }

        // ── Processor model ────────────────────────────────────────────────────
        ComboBox {
            id: modelBox
            Layout.preferredWidth: 150
            model: [qsTr("5-stage pipeline"), qsTr("Single cycle")]
            currentIndex: bar.sim.model === "pipelined" ? 0 : 1
            onActivated: bar.sim.model = currentIndex === 0 ? "pipelined" : "single"
            font.pixelSize: Theme.fontBody
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Processor model")
        }

        // ── Examples ───────────────────────────────────────────────────────────
        ComboBox {
            id: exampleBox
            Layout.preferredWidth: 190
            font.pixelSize: Theme.fontBody
            model: [qsTr("Load example…")].concat(bar.sim.exampleNames())
            onActivated: (idx) => {
                if (idx > 0) bar.loadExampleRequested(idx - 1)
                currentIndex = 0
            }
        }

        CCButton {
            text: qsTr("Assemble + Load")
            onClicked: bar.assembleRequested()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Ctrl+B")
        }

        Item { Layout.fillWidth: true }

        // ── Transport ──────────────────────────────────────────────────────────
        CCButton {
            text: qsTr("Reset")
            enabled: bar.sim.programLoaded
            onClicked: bar.sim.reset()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Ctrl+R")
        }
        CCButton {
            text: qsTr("Step")
            enabled: bar.sim.programLoaded && !bar.sim.running
            onClicked: bar.sim.step()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("F10")
        }
        CCButton {
            kind: "primary"
            text: bar.sim.running ? qsTr("Pause") : qsTr("Run")
            enabled: bar.sim.programLoaded
            onClicked: bar.sim.runPause()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("F5")
        }

        // ── Speed ──────────────────────────────────────────────────────────────
        RowLayout {
            spacing: Theme.s1
            Text {
                text: qsTr("Speed")
                font.pixelSize: Theme.fontCaption
                color: Theme.onSurfaceMutedAAA
            }
            Slider {
                id: speedSlider
                Layout.preferredWidth: 110
                from: 0; to: 100; stepSize: 1
                value: bar.sim.speed
                onMoved: bar.sim.speed = Math.round(value)
                Accessible.name: qsTr("Execution speed")
            }
        }

        Item { Layout.fillWidth: true }

        // ── Live status ────────────────────────────────────────────────────────
        Text {
            text: qsTr("cycle %1").arg(bar.sim.cycles)
            font.family: Theme.mono
            font.pixelSize: Theme.fontBody
            color: Theme.onSurface
        }

        StatusPill { sim: bar.sim }

        CCButton {
            text: Theme.dark ? qsTr("Light") : qsTr("Dark")
            onClicked: Theme.dark = !Theme.dark
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Toggle color scheme")
        }
    }
}
