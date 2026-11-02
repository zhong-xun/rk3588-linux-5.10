import QtQuick 2.12

QtObject {
    id: theme

    // Native Qt Quick mirrors the portable CSS tokens in ../../tokens.css.
    readonly property color canvas: "#EEF2ED"
    readonly property color canvasAlt: "#E4EAE4"
    readonly property color canvasStrong: "#D6DED6"
    readonly property color surface: "#F9FCF8"
    readonly property color surfaceRaised: "#FFFFFF"
    readonly property color surfaceSoft: "#EAEFE9"
    readonly property color ink: "#18231B"
    readonly property color inkStrong: "#101813"
    readonly property color muted: "#526158"
    readonly property color inkFaint: "#59685F"
    readonly property color rule: "#D3DCD4"
    readonly property color ruleStrong: "#AEBCAF"
    readonly property color instrument: "#111612"
    readonly property color instrumentRaised: "#1A231C"
    readonly property color instrumentInk: "#F4F8F1"
    readonly property color instrumentMuted: "#B5C1B8"
    readonly property color accent: "#2C865D"
    readonly property color accentHover: "#1C6946"
    readonly property color accentSoft: "#DDF0E3"
    readonly property color accentInk: "#F8FCF8"
    readonly property color focus: "#278FC3"
    readonly property color info: "#2785B8"
    readonly property color infoSoft: "#E2F1F8"
    readonly property color ready: "#278557"
    readonly property color readySoft: "#E3F3E8"
    readonly property color warning: "#A96816"
    readonly property color warningSoft: "#FAEFD9"
    readonly property color danger: "#B84443"
    readonly property color dangerHover: "#963C3C"
    readonly property color dangerSoft: "#FBE7E6"
    readonly property color dangerSoftHover: "#F2D9D7"
    readonly property color disabled: "#9AA79E"
    readonly property color selected: "#DDEEF8"

    readonly property string displayFont: "Noto Sans CJK SC"
    readonly property string bodyFont: "Noto Sans CJK SC"
    readonly property string monoFont: "DejaVu Sans Mono"

    readonly property int space4: 4
    readonly property int space8: 8
    readonly property int space12: 12
    readonly property int space16: 16
    readonly property int space20: 20
    readonly property int space24: 24
    readonly property int space32: 32
    readonly property int space40: 40
    readonly property int space48: 48
    readonly property int radiusControl: 4
    readonly property int radiusPanel: 6
    readonly property int radiusPill: 999
    // Set by the application command line. A context fallback keeps isolated
    // component previews usable when they are loaded outside BoardLab.
    readonly property bool motionEnabled: typeof boardLabMotionEnabled === "boolean"
                                        ? boardLabMotionEnabled : true
    readonly property int durationFast: motionEnabled ? 140 : 0
    readonly property int durationBase: motionEnabled ? 200 : 0

    function withAlpha(color, alpha) {
        return Qt.rgba(color.r, color.g, color.b, alpha)
    }

    function motionDuration(duration) {
        return motionEnabled ? duration : 0
    }
}
