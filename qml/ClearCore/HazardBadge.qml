import QtQuick

// ─── HazardBadge.qml — labeled forwarding / hazard indicator ──────────────────

Rectangle {
    id: badge

    required property string label
    property bool active: false
    property color badgeColor: Theme.violet

    implicitWidth: txt.implicitWidth + Theme.s3 * 2
    implicitHeight: 22
    radius: height / 2
    color: active ? Theme.alpha(badgeColor, 0.2) : "transparent"
    border.width: 1
    border.color: active ? badgeColor : Theme.outline

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("%1: %2").arg(label).arg(active ? qsTr("active") : qsTr("inactive"))

    Text {
        id: txt
        anchors.centerIn: parent
        text: badge.label
        font.pixelSize: Theme.fontCaption
        font.weight: badge.active ? Font.Medium : Font.Normal
        color: badge.active ? badge.badgeColor : Theme.onSurfaceMutedAAA
    }

    Behavior on border.color {
        enabled: !Theme.reducedMotion
        ColorAnimation { duration: Theme.durFast }
    }
}
