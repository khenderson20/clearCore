import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

// ─── DatapathStrip.qml — live 5-stage pipeline, always visible ────────────────
// Replaces the old buried "Datapath tab": the pipeline is the app's primary
// content, so it is permanently on screen. Five stage cards joined by arrows;
// forwarding paths light up as labeled violet badges, hazards as warning /
// danger badges. Color is always paired with a text label. Horizontally scrolls
// if needed to prevent the WB stage from being cut off.

Rectangle {
    id: strip

    required property var sim

    color: Theme.surface
    radius: Theme.radius
    border.width: 1
    border.color: Theme.outline
    implicitHeight: flickable.implicitHeight + Theme.s4 * 2

    Flickable {
        id: flickable
        anchors.fill: parent
        anchors.margins: Theme.s4
        contentWidth: content.implicitWidth
        contentHeight: content.implicitHeight
        clip: true
        implicitHeight: content.implicitHeight

        ColumnLayout {
            id: content
            width: Math.max(flickable.width, implicitWidth)
            spacing: Theme.s3

            // ── Stage cards row ────────────────────────────────────────────────────
            RowLayout {
                spacing: 0

                Repeater {
                    model: 5

                    delegate: RowLayout {
                        id: stageWrap
                        required property int index

                        readonly property var st:
                            strip.sim.stages.length === 5 ? strip.sim.stages[index] : null

                        spacing: 0

                        StageCard {
                            Layout.preferredWidth: 150
                            stageName: ["IF", "ID", "EX", "MEM", "WB"][stageWrap.index]
                            info: stageWrap.st
                        }

                        // Inter-stage arrow (skipped after WB)
                        Text {
                            visible: stageWrap.index < 4
                            text: "\u2192"
                            font.pixelSize: Theme.fontH1
                            color: Theme.onSurfaceMutedAAA
                            leftPadding: Theme.s1
                            rightPadding: Theme.s1
                            Accessible.ignored: true
                        }
                    }
                }
            }

            // ── Forwarding / hazard badges ─────────────────────────────────────────
            Flow {
                Layout.fillWidth: true
                spacing: Theme.s2

                HazardBadge {
                    label: qsTr("EX→EX fwd A")
                    active: strip.sim.hazards.fwdExA === true
                    badgeColor: Theme.violet
                    Accessible.name: qsTr("EX to EX forward A: %1").arg(active ? qsTr("active") : qsTr("inactive"))
                }
                HazardBadge {
                    label: qsTr("EX→EX fwd B")
                    active: strip.sim.hazards.fwdExB === true
                    badgeColor: Theme.violet
                    Accessible.name: qsTr("EX to EX forward B: %1").arg(active ? qsTr("active") : qsTr("inactive"))
                }
                HazardBadge {
                    label: qsTr("MEM→EX fwd A")
                    active: strip.sim.hazards.fwdMemA === true
                    badgeColor: Theme.violet
                    Accessible.name: qsTr("MEM to EX forward A: %1").arg(active ? qsTr("active") : qsTr("inactive"))
                }
                HazardBadge {
                    label: qsTr("MEM→EX fwd B")
                    active: strip.sim.hazards.fwdMemB === true
                    badgeColor: Theme.violet
                    Accessible.name: qsTr("MEM to EX forward B: %1").arg(active ? qsTr("active") : qsTr("inactive"))
                }
                HazardBadge {
                    label: qsTr("load-use stall")
                    active: strip.sim.hazards.loadStall === true
                    badgeColor: Theme.warningDark
                    Accessible.name: qsTr("load-use stall: %1").arg(active ? qsTr("active") : qsTr("inactive"))
                }
                HazardBadge {
                    label: qsTr("branch flush")
                    active: strip.sim.hazards.branchFlush === true
                    badgeColor: Theme.dangerDark
                    Accessible.name: qsTr("branch flush: %1").arg(active ? qsTr("active") : qsTr("inactive"))
                }
            }
        }

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
    }
}
