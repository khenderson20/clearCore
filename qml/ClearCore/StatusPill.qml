import QtQuick

// ─── StatusPill.qml — execution status indicator ──────────────────────────────
// State is carried by color + shape (dot) + text, per accessibility rules.

Rectangle {
    id: pill

    required property var sim

    readonly property color stateColor:
        sim.running                                 ? (Theme.dark ? Theme.success : Theme.successDark)
        : sim.status.startsWith("Fault")            ? (Theme.dark ? Theme.danger : Theme.dangerDark)
        : sim.status.startsWith("Halted")           ? (Theme.dark ? Theme.interactive : Theme.interactiveDark)
        : sim.status.startsWith("Breakpoint")       ? (Theme.dark ? Theme.warning : Theme.warningDark)
        : Theme.onSurfaceMutedAAA

    implicitWidth: row.implicitWidth + Theme.s3 * 2
    implicitHeight: 28
    radius: height / 2
    color: Theme.alpha(stateColor, 0.15)
    border.width: 1
    border.color: stateColor

    Accessible.role: Accessible.StaticText
    Accessible.name: sim.status

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.s2

        Rectangle {
            width: 8; height: 8; radius: 4
            anchors.verticalCenter: parent.verticalCenter
            color: pill.stateColor

            SequentialAnimation on opacity {
                running: pill.sim.running && !Theme.reducedMotion
                loops: Animation.Infinite
                alwaysRunToEnd: true
                NumberAnimation { to: 0.3; duration: 450 }
                NumberAnimation { to: 1.0; duration: 450 }
            }
        }

        Text {
            text: pill.sim.status
            font.pixelSize: Theme.fontCaption
            font.weight: Font.Medium
            color: pill.stateColor
        }
    }
}
