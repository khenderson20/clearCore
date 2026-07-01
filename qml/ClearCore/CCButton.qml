import QtQuick
import QtQuick.Controls.Basic

// ─── CCButton.qml — themed toolbar/action button ──────────────────────────────
// `kind`: "primary" (filled, one per group), "danger", or "quiet" (default).

Button {
    id: control

    property string kind: "quiet"

    readonly property color baseColor:
        kind === "primary" ? Theme.interactive
        : kind === "danger" ? Theme.danger
        : Theme.surfaceRaised

    implicitHeight: 32
    leftPadding: Theme.s3
    rightPadding: Theme.s3
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    contentItem: Text {
        text: control.text
        font.pixelSize: Theme.fontBody
        font.weight: control.kind === "quiet" ? Font.Normal : Font.Medium
        color: control.kind === "quiet" ? Theme.onSurface : Theme.onAccent
        opacity: control.enabled ? 1.0 : 0.4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Qt.darker(control.baseColor, 1.2)
             : control.hovered ? Qt.lighter(control.baseColor, 1.1)
             : control.baseColor
        opacity: control.enabled ? 1.0 : 0.4
        border.width: control.activeFocus ? 2 : (control.kind === "quiet" ? 1 : 0)
        border.color: control.activeFocus ? Theme.interactive : Theme.outline

        Behavior on color {
            enabled: !Theme.reducedMotion
            ColorAnimation { duration: Theme.durFast }
        }
    }
}
