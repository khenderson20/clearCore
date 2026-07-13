pragma Singleton
import QtQuick

// ─── Theme.qml — role-based design tokens ─────────────────────────────────────
// Solarized-derived palette matching the clearCore brand badges.
// Every color in the UI must come from here; no raw hex in components.

QtObject {
    id: theme

    property bool dark: true

    // ── Type scale — base 15 px, major second (1.125), data-dense ─────────────
    readonly property real fontCaption: 12
    readonly property real fontBody:    15
    readonly property real fontH2:      17
    readonly property real fontH1:      19
    readonly property real fontDisplay: 24
    readonly property string mono: "monospace"

    // ── Spacing scale ──────────────────────────────────────────────────────────
    readonly property int s1: 4
    readonly property int s2: 8
    readonly property int s3: 12
    readonly property int s4: 16
    readonly property int s5: 24
    readonly property int radius: 6

    // ── Motion ─────────────────────────────────────────────────────────────────
    property bool reducedMotion: false
    readonly property int durFast:   reducedMotion ? 0 : 120
    readonly property int durMedium: reducedMotion ? 0 : 220

    // ── Surfaces ───────────────────────────────────────────────────────────────
    readonly property color background:     dark ? "#00212b" : "#fdf6e3"
    readonly property color surface:        dark ? "#073642" : "#eee8d5"
    readonly property color surfaceRaised:  dark ? "#0a4050" : "#f5efdc"
    readonly property color outline:        dark ? "#144655" : "#d9d2c0"

    // ── Content ────────────────────────────────────────────────────────────────
    readonly property color onSurface:      dark ? "#c7d2d4" : "#40484a"
    readonly property color onSurfaceMuted: dark ? "#657b83" : "#93a1a1"
    readonly property color onAccent:       dark ? "#00212b" : "#fdf6e3"

    // ── Roles ──────────────────────────────────────────────────────────────────
    readonly property color interactive:    "#268bd2"   // buttons, links, focus
    readonly property color accent:         "#2aa198"   // brand / active stage
    readonly property color success:        "#859900"   // valid, retired, Run
    readonly property color warning:        "#b58900"   // stall bubbles
    readonly property color danger:         "#dc322f"   // faults, flush bubbles
    readonly property color violet:         "#6c71c4"   // forwarding wires

    // Semantic pipeline colors
    readonly property color stageValid:   accent
    readonly property color stageStalled: warning
    readonly property color stageFlushed: danger
    readonly property color stageEmpty:   onSurfaceMuted

    // ── Accessibility: darker semantic colors for light theme ─────────────────
    // Light bg needs darker colors to meet WCAG AAA (7:1). These are used for
    // badges, focus rings, and semantic state in light mode only.
    readonly property color accentDark:       dark ? accent       : "#16635e"   // darker teal
    readonly property color interactiveDark:  dark ? interactive  : "#0d4a9b"   // darker blue
    readonly property color warningDark:      dark ? warning      : "#8b6f00"   // darker gold
    readonly property color dangerDark:       dark ? danger       : "#a51e1e"   // darker red
    readonly property color successDark:      dark ? success      : "#5a6f00"   // darker green

    // Muted text: in light theme, needs to be darker for AA contrast
    readonly property color onSurfaceMutedAAA: dark ? onSurfaceMuted : "#4a5568"

    // ── Helpers ────────────────────────────────────────────────────────────────
    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a) }
}
