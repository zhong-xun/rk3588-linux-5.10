.pragma library

// Keep presentation routing tied to BoardProfile's capability contract rather
// than to a particular SoC or model string.  This file is deliberately shared
// by the Qt home screen, direct-launch router, and hardware-tool picker.

var toolCapabilities = {
    "gpio": "gpio",
    "uart": "uart",
    "ethernet": "ethernet",
    "can": "can",
    "cellular": "cellular",
    "camera": "camera",
    "hdmiin": "hdmiin",
    "watchdog": "watchdog",
    "music": "music",
    "buzzer": "buzzer",
    "fan": "fan",
    "led": "led",
    "backlight": "backlight",
    "files": "files"
};

var appCapabilities = {
    "Keys": "keys",
    "Environment": "temperaturehumidity",
    "GPU": "gpu",
    "Monitor": "monitor",
    "NPU YOLO": "npuyolodemo",
    "WiFi": "wifi",
    "Storage": "storage",
    "Diagnostics": "diagnostics",
    "RK Desk": "monitor",
    "Qt Web": "web"
};

function capabilityForTool(tool) {
    return toolCapabilities[String(tool || "").toLowerCase()] || "";
}

function capabilityForApp(title) {
    var app = String(title || "");
    if (app.indexOf("Demo:") === 0 || app.indexOf("Tool:") === 0)
        return capabilityForTool(app.substring(app.indexOf(":") + 1));
    return appCapabilities[app] || "";
}

function profileAllows(profile, capability) {
    if (!capability)
        return true;
    // A BoardProfile is always injected by BoardLab.  Fail closed while QML is
    // being embedded elsewhere instead of exposing an unverified hardware
    // action before a profile arrives.
    return profile && profile.hasCapability(capability);
}

function toolEnabled(tool, profile) {
    return profileAllows(profile, capabilityForTool(tool));
}

function appEnabled(title, profile) {
    return profileAllows(profile, capabilityForApp(title));
}
