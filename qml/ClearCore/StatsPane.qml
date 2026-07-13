import QtQuick
import QtQuick.Layouts

// ─── StatsPane.qml — cycles, CPI, hazard counters ─────────────────────────────

Flow {
    id: pane

    required property var sim

    spacing: Theme.s3
    padding: Theme.s1

    component StatCard: Rectangle {
        property string label: ""
        property string value: ""
        property color tint: Theme.accent

        width: 170
        height: 76
        radius: Theme.radius
        color: Theme.surfaceRaised
        border.width: 1
        border.color: Theme.outline

        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("%1: %2").arg(label).arg(value)

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.s3
            spacing: Theme.s1

            Text {
                text: label
                font.pixelSize: Theme.fontCaption
                color: Theme.onSurfaceMutedAAA
            }
            Text {
                text: value
                font.family: Theme.mono
                font.pixelSize: Theme.fontDisplay
                font.weight: Font.Medium
                color: tint
            }
        }
    }

    StatCard { label: qsTr("Cycles");           value: pane.sim.cycles }
    StatCard { label: qsTr("Instructions");     value: pane.sim.instructions }
    StatCard { label: qsTr("CPI");              value: pane.sim.cpi.toFixed(2); tint: Theme.interactive }
    StatCard { label: qsTr("Data hazards");     value: pane.sim.dataHazards;    tint: Theme.warning }
    StatCard { label: qsTr("Control hazards");  value: pane.sim.controlHazards; tint: Theme.warning }
    StatCard { label: qsTr("Forwarding");       value: pane.sim.forwards;       tint: Theme.violet }
    StatCard { label: qsTr("Stalls");           value: pane.sim.stalls;         tint: Theme.warning }
    StatCard { label: qsTr("Flushes");          value: pane.sim.flushes;        tint: Theme.danger }
}
