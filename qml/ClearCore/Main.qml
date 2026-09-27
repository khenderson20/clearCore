import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Layouts
import ClearCore

// ─── Main.qml — clearCore Qt Quick UI ─────────────────────────────────────────
// Redesign rationale (vs. the six-tab widget GUI):
//   • The pipeline datapath is the product — it is permanently visible, not a
//     tab. Editor sits beside it so code ↔ hardware cause-and-effect is one
//     glance, not two clicks (Performance Load / Wayfinding).
//   • Only secondary inspectors (Registers / Memory / Trace / Stats) share a
//     tab bar in the bottom dock (Progressive Disclosure).
//   • One filled primary action (Run/Pause); everything keyboard-reachable:
//     F5 run/pause · F10 step · Ctrl+B assemble+load · Ctrl+R reset.

ApplicationWindow {
    id: window

    width: 1440
    height: 900
    minimumWidth: 960
    minimumHeight: 620
    visible: true
    title: qsTr("clearCore — MIPS CPU emulator")
    color: Theme.background

    Simulator { id: sim }

    // ── Keyboard transport ─────────────────────────────────────────────────────
    Shortcut { sequence: "F5";     onActivated: sim.runPause() }
    Shortcut { sequence: "F10";    onActivated: sim.step() }
    Shortcut { sequence: "Ctrl+B"; onActivated: editorPane.assemble() }
    Shortcut { sequence: "Ctrl+R"; onActivated: sim.reset() }

    header: TransportBar {
        sim: sim
        onAssembleRequested: editorPane.assemble()
        onLoadExampleRequested: (index) => {
            editorPane.source = sim.exampleSource(index)
            editorPane.assemble()
        }
    }

    // ── Content ────────────────────────────────────────────────────────────────
    SplitView {
        anchors.fill: parent
        anchors.margins: Theme.s3
        orientation: Qt.Horizontal

        handle: Rectangle {
            implicitWidth: 6
            color: T.SplitHandle.hovered || T.SplitHandle.pressed
                       ? Theme.alpha(Theme.interactive, 0.35) : "transparent"
        }

        // ── Left: code ─────────────────────────────────────────────────────────
        EditorPane {
            id: editorPane
            sim: sim
            SplitView.preferredWidth: 460
            SplitView.minimumWidth: 320
        }

        // ── Right: pipeline (primary) + inspector dock (secondary) ─────────────
        ColumnLayout {
            SplitView.fillWidth: true
            SplitView.minimumWidth: 560
            spacing: Theme.s3

            DatapathStrip {
                sim: sim
                Layout.fillWidth: true
            }

            // ── Inspector dock ─────────────────────────────────────────────────
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.surface
                radius: Theme.radius
                border.width: 1
                border.color: Theme.outline

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.s3
                    spacing: Theme.s2

                    TabBar {
                        id: dockTabs
                        Layout.fillWidth: false

                        background: Item {}

                        component DockTab: TabButton {
                            id: tab
                            width: implicitWidth
                            contentItem: Text {
                                text: tab.text
                                font.pixelSize: Theme.fontBody
                                font.weight: tab.checked ? Font.Medium : Font.Normal
                                color: tab.checked ? (Theme.dark ? Theme.accent : Theme.accentDark) : Theme.onSurfaceMutedAAA
                                horizontalAlignment: Text.AlignHCenter
                            }
                            background: Rectangle {
                                color: "transparent"
                                Rectangle {
                                    width: parent.width; height: 2
                                    anchors.bottom: parent.bottom
                                    color: tab.checked ? (Theme.dark ? Theme.accent : Theme.accentDark) : "transparent"
                                }
                                Rectangle {
                                    anchors.fill: parent
                                    radius: Theme.radius
                                    color: "transparent"
                                    border.width: tab.activeFocus ? 2 : 0
                                    border.color: Theme.interactive
                                }
                            }
                        }

                        DockTab { text: qsTr("Registers") }
                        DockTab { text: qsTr("Memory") }
                        DockTab { text: qsTr("Pipeline trace") }
                        DockTab { text: qsTr("Statistics") }
                    }

                    StackLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        currentIndex: dockTabs.currentIndex

                        RegistersPane { sim: sim }
                        MemoryPane    { sim: sim }
                        TracePane     { sim: sim }
                        StatsPane     { sim: sim }
                    }
                }
            }
        }
    }
}
