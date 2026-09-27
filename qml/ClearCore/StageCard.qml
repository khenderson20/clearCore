import QtQuick
import QtQuick.Layouts

// ─── StageCard.qml — one pipeline stage ───────────────────────────────────────
// State encoding (color + text, never color alone):
//   valid   → accent border, disassembly shown
//   stalled → warning border, "bubble (stall)"
//   flushed → danger border,  "bubble (flush)"
//   empty   → muted

Rectangle {
    id: card

    required property string stageName
    property var info: null   // { valid, stalled, flushed, pc, text }

    readonly property bool valid:   info ? info.valid   === true : false
    readonly property bool stalled: info ? info.stalled === true : false
    readonly property bool flushed: info ? info.flushed === true : false

    // Use darker colors in light theme for WCAG AAA contrast
    readonly property color stateColor:
        valid ? (Theme.dark ? Theme.stageValid : Theme.accentDark)
        : stalled ? (Theme.dark ? Theme.stageStalled : Theme.warningDark)
        : flushed ? (Theme.dark ? Theme.stageFlushed : Theme.dangerDark)
        : Theme.onSurfaceMutedAAA

    implicitHeight: col.implicitHeight + Theme.s3 * 2
    radius: Theme.radius
    color: valid ? Theme.alpha(Theme.stageValid, 0.08) : Theme.surfaceRaised
    border.width: valid || stalled || flushed ? 2 : 1
    border.color: valid || stalled || flushed ? stateColor : Theme.outline

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%1 stage: %2").arg(stageName).arg(info ? info.text : qsTr("empty"))

    Behavior on border.color {
        enabled: !Theme.reducedMotion
        ColorAnimation { duration: Theme.durFast }
    }

    ColumnLayout {
        id: col
        anchors.fill: parent
        anchors.margins: Theme.s3
        spacing: Theme.s1

        RowLayout {
            Layout.fillWidth: true

            Text {
                text: card.stageName
                font.pixelSize: Theme.fontH2
                font.weight: Font.Medium
                color: card.stateColor
            }
            Item { Layout.fillWidth: true }
            Text {
                text: card.info && card.info.pc ? card.info.pc : ""
                font.family: Theme.mono
                font.pixelSize: Theme.fontCaption
                color: Theme.onSurfaceMutedAAA
            }
        }

        Text {
            Layout.fillWidth: true
            text: card.info ? card.info.text : "\u2014"
            font.family: Theme.mono
            font.pixelSize: Theme.fontBody
            color: card.valid ? Theme.onSurface : Theme.onSurfaceMutedAAA
            elide: Text.ElideRight
        }
    }
}
