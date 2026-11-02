(() => {
  "use strict";

  const modules = [
    { id: "gpio", title: "GPIO", icon: "circuit-board", category: "DIGITAL I/O", description: "输入输出、电平设置与读回确认" },
    { id: "uart", title: "串口 / 485", icon: "waypoints", category: "SERIAL BUS", description: "tty 设备参数、测试帧发送与样本读取" },
    { id: "ethernet", title: "以太网", icon: "ethernet", category: "NETWORK", description: "链路发现、地址配置与连通性验证" },
    { id: "can", title: "CAN（经典帧）", icon: "git-branch", category: "FIELD BUS", description: "经典 CAN 标准数据帧配置、收发与循环测试" },
    { id: "cellular", title: "4G / 5G", icon: "radio-tower", category: "CELLULAR", description: "APN、拨号状态与数据网络验证" },
    { id: "camera", title: "摄像头", icon: "camera", category: "VIDEO CAPTURE", description: "MIPI / USB 摄像头节点采集验证" },
    { id: "hdmiin", title: "HDMI 输入", icon: "monitor-down", category: "VIDEO INPUT", description: "HDMIRX 节点选择与输入信号采集" },
    { id: "watchdog", title: "看门狗", icon: "shield-check", category: "SYSTEM SAFETY", description: "wdctl 状态与可控复位测试" },
    { id: "gpu", title: "GPU 基准", icon: "gpu", category: "GPU BENCHMARK", description: "固定离屏评分与 Qt 页面内嵌实时图形预览" },
    { id: "npu-yolo", title: "YOLOv8 NPU", icon: "scan-line", category: "NPU INFERENCE", description: "内置 MP4 的固定目标检测与结果预览" },
    { id: "wifi", title: "Wi-Fi", icon: "wifi", category: "WIRELESS NETWORK", description: "NetworkManager 状态、热点扫描与安全连接" },
    { id: "music", title: "音乐与录音", icon: "music", category: "AUDIO", description: "播放、录音回放与播放设备音量" },
    { id: "buzzer", title: "蜂鸣器", icon: "bell", category: "PWM", description: "提示音强度与心跳测试" },
    { id: "fan", title: "风扇", icon: "fan", category: "PWM FAN", description: "温控自动模式与手动转速" },
    { id: "led", title: "板载 LED", icon: "lightbulb", category: "BOARD I/O", description: "板载指示灯亮度与闪烁确认" },
    { id: "environment", title: "温湿度检测", icon: "thermometer-sun", category: "ENVIRONMENT", description: "DHT / IIO 成对采样、数据新鲜度与来源确认" },
    { id: "keys", title: "按键检测", icon: "keyboard", category: "USER INPUT", description: "仅监听设备树确认的板载用户按键" },
    { id: "backlight", title: "背光", icon: "sun-medium", category: "DISPLAY", description: "本地屏幕亮度设置与实际值确认" },
    { id: "storage", title: "存储测试", icon: "hard-drive", category: "STORAGE MEDIA", description: "U 盘、TF、NVMe 与 eMMC 分区发现、挂载和测速" },
    { id: "files", title: "文件浏览", icon: "folder-open", category: "FILESYSTEM", description: "浏览板端目录并定位测试文件" }
  ];

  const moduleById = new Map(modules.map((item) => [item.id, item]));
  const state = {
    accessToken: "",
    currentTool: "",
    board: {},
    system: {},
    refreshTimer: 0,
    refreshPromise: null,
    pendingActions: new Set(),
    rangeDrafts: Object.create(null),
    lastOperationTime: "",
    gpuBenchmarkStatus: {},
    npuYoloStatus: {},
    npuYoloPollTimer: 0,
    npuYoloPollPromise: null,
    npuYoloPollEpoch: 0,
    npuYoloPollFailureCount: 0,
    npuPreviewObjectUrl: "",
    npuPreviewSerial: 0,
    npuPreviewLoadingSerial: 0,
    npuTelemetryDrawFrame: 0,
    npuTelemetryResizeObserver: null,
    cameraPreviewStatus: {},
    cameraPreviewPollTimer: 0,
    cameraPreviewPollPromise: null,
    cameraPreviewPollEpoch: 0,
    cameraPreviewPollFailureCount: 0,
    cameraPreviewObjectUrl: "",
    cameraPreviewSerial: 0,
    cameraPreviewLoadingSerial: 0,
    wifiManagerStatus: {},
    wifiAccessPoints: [],
    ethernetPingFeedback: null,
    toolStatusFailures: Object.create(null),
    toolStatusRequestId: 0,
    storageScanRequested: false,
    keyStopOnLeavePending: false,
    fileUpload: {
      file: null,
      inFlight: false,
      progress: 0,
      feedback: "",
      feedbackMode: "idle"
    },
    authenticated: false
  };

  // The regular page refresh reads the full board inventory. Keep it slow
  // while the YOLO page uses this narrow status endpoint for live playback.
  // The backend emits latest-frame previews at 10 FPS. Poll at that cadence
  // only while this tool is active; preview fetching still permits one JPEG
  // request at a time so a slow browser or LAN never backs up the board.
  // 150–200ms is enough for live telemetry without thrashing the board JPEG path.
  const NPU_YOLO_POLL_INTERVAL_MS = 180;
  const CAMERA_PREVIEW_POLL_INTERVAL_MS = 180;
  const NPU_TELEMETRY_MAX_SAMPLES = 60;
  const NPU_TELEMETRY_MAX_CORES = 16;
  const FILE_UPLOAD_MAX_BYTES = 64 * 1024 * 1024;
  const NPU_TELEMETRY_CORE_TOKENS = ["accent", "focus", "warning"];

  const $ = (id) => document.getElementById(id);
  const isCompactNavigation = () => window.matchMedia("(max-width: 59.99rem)").matches;
  const prefersReducedMotion = () => window.matchMedia("(prefers-reduced-motion: reduce)").matches;
  const cssToken = (name) => getComputedStyle(document.documentElement)
    .getPropertyValue(`--${name}`).trim();
  const value = (id) => ($(id) ? $(id).value : "");
  const numberValue = (id, fallback = 0) => {
    const parsed = Number(value(id));
    return Number.isFinite(parsed) ? parsed : fallback;
  };
  const setText = (id, text) => {
    const element = $(id);
    if (element) element.textContent = text == null ? "--" : String(text);
  };
  const clampPercent = (input) => {
    const parsed = Number(input);
    return Number.isFinite(parsed) ? Math.max(0, Math.min(100, parsed)) : 0;
  };
  const list = (input) => Array.isArray(input) ? input : [];
  const countText = (input, unit) => `${list(input).length} ${unit}`;
  const cellularModemStatusForBoard = (board = state.board) => board?.cellularModemStatus || {};
  const preferredCellularInterfaceForBoard = (board = state.board) => {
    const modem = cellularModemStatusForBoard(board);
    const reported = String(modem.preferredInterface || board?.preferredCellularInterface || "").trim();
    if (/^(?:wwan|usb|ppp|rmnet)[A-Za-z0-9_.:-]{0,62}$/.test(reported)) return reported;
    for (const entry of list(board?.cellularDevices)) {
      const match = String(entry || "").match(/^((?:wwan|usb|ppp|rmnet)[A-Za-z0-9_.:-]{0,62})\b/);
      if (match) return match[1];
    }
    return "";
  };
  const cellularModuleLabel = (board = state.board) => {
    const modem = cellularModemStatusForBoard(board);
    if (modem.detected) {
      const label = String(modem.displayName || modem.module || "已识别模组");
      return modem.usbId ? `${label} · ${modem.usbId}` : label;
    }
    if (modem.state === "lsusb-unavailable") return "无法读取 lsusb";
    if (modem.state === "multiple-modems") return "检测到多个模组";
    if (modem.state === "identity-conflict") return "USB 身份冲突";
    return list(board?.cellularDevices).length ? "待识别 USB 模组" : "未检测到模组";
  };
  const configuredCapabilities = (board = state.board) => board?.profile?.configuredCapabilities || board?.profile?.capabilities || null;
  const profileHasCapability = (capability, board = state.board) => {
    const capabilities = configuredCapabilities(board);
    // Older BoardLab versions did not publish a profile map. Preserve their
    // runtime-discovery behavior during a rolling update, but require an
    // explicit opt-in once a profile is available.
    return !capabilities || capabilities[capability] === true;
  };
  // The fixed YOLOv8 workload is deliberately narrower than general NPU
  // telemetry.  Do not fall back to runtime capabilities here: the launcher
  // is shown only after a validated product profile explicitly opts in.
  const npuYoloDemoEnabled = (board = state.board) =>
    board?.profile?.configuredCapabilities?.npuyolodemo === true;
  // A profile declares which hardware workflows are verified for its carrier
  // board. Keep the launcher, deep-link routing, and control guard on this
  // one mapping so an RK3568 or generic board cannot look like an RK3588
  // with a collection of unusable pages.
  const moduleCapabilities = Object.freeze({
    gpio: "gpio",
    uart: "uart",
    ethernet: "ethernet",
    can: "can",
    cellular: "cellular",
    camera: "camera",
    hdmiin: "hdmiin",
    watchdog: "watchdog",
    gpu: "gpu",
    "npu-yolo": "npuyolodemo",
    wifi: "wifi",
    music: "music",
    buzzer: "buzzer",
    fan: "fan",
    led: "led",
    environment: "temperaturehumidity",
    keys: "keys",
    backlight: "backlight",
    storage: "storage",
    files: "files"
  });
  const moduleCapability = (module) => module && moduleCapabilities[module.id];
  const moduleIsVisible = (module, board = state.board) => {
    const capability = moduleCapability(module);
    if (!capability) return true;
    // Do not relax the fixed RKNN sample to the legacy no-profile fallback.
    // It is intentionally opt-in only for a validated product profile.
    if (module.id === "npu-yolo") return npuYoloDemoEnabled(board);
    return profileHasCapability(capability, board);
  };
  const buzzerUsesInputEvent = (board = state.board) => board?.buzzerControlMethod === "input-event";
  const buzzerMethodLabel = (board = state.board) => {
    if (buzzerUsesInputEvent(board)) return "输入事件";
    return "不可用";
  };
  const toneFrequencyForPercent = (percent) => {
    const clamped = Math.round(clampPercent(percent));
    return clamped === 0 ? 0 : 600 + Math.floor((clamped * 1800) / 100);
  };

  function updateBuzzerPresentation(board = state.board) {
    const percent = Math.round(clampPercent(value("beepPercent") || 50));
    const valueText = `${percent}% · ${toneFrequencyForPercent(percent)} Hz`;
    setText("buzzerConfigTitle", "pwm-beeper 蜂鸣器");
    setText("buzzerConfigDescription", "内核 pwm-beeper 驱动提供受限输入事件控制");
    setText("beepControlLabel", "提示音调");
    setText("beepPercentLabel", valueText);
    setText("buzzerControlHint", "音调范围为约 600 至 2400 Hz。双短提示会自动停止；“应用蜂鸣器”会持续响，需手动停止。");
    setText("buzzerStatusDescription", "pwm-beeper 输入事件与后台任务反馈");
    setText("beepStatusLabel", "音调");
    setText("beepStatus", valueText);
  }
  const moduleIconPaths = Object.freeze({
      "circuit-board": '<rect width="18" height="18" x="3" y="3" rx="2"/><path d="M7 7h.01M11 7h2M7 11h4M7 15h.01M15 11h2M15 15h.01"/>',
      waypoints: '<circle cx="6" cy="6" r="3"/><path d="M8.7 7.5 15.3 16.5"/><circle cx="18" cy="18" r="3"/><path d="m8.7 16.5 6.6-9"/>',
      ethernet: '<path d="M12 16v5"/><path d="M16 16v5"/><path d="M20 16v5"/><path d="M4 16v5"/><rect width="20" height="10" x="2" y="6" rx="2"/>',
      "git-branch": '<line x1="6" x2="6" y1="3" y2="15"/><circle cx="18" cy="6" r="3"/><circle cx="6" cy="18" r="3"/><path d="M18 9a9 9 0 0 1-9 9"/>',
      "radio-tower": '<path d="M4.9 19.1C1 15.2 1 8.8 4.9 4.9"/><path d="M7.8 16.2a6 6 0 0 1 0-8.5"/><circle cx="12" cy="12" r="2"/><path d="M16.2 7.8a6 6 0 0 1 0 8.5"/><path d="M19.1 4.9C23 8.8 23 15.1 19.1 19"/><path d="m12 14 4 8H8l4-8Z"/>',
      camera: '<path d="M14.5 4 16 7h4a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V9a2 2 0 0 1 2-2h4l1.5-3Z"/><circle cx="12" cy="13" r="3"/>',
      "monitor-down": '<rect width="20" height="14" x="2" y="3" rx="2"/><path d="M12 17v4M8 21h8M12 7v6M9 10l3 3 3-3"/>',
      "shield-check": '<path d="M20 13c0 5-3.5 7.5-8 9-4.5-1.5-8-4-8-9V5l8-3 8 3v8Z"/><path d="m9 12 2 2 4-4"/>',
      gpu: '<rect width="14" height="14" x="5" y="5" rx="2"/><rect width="6" height="6" x="9" y="9" rx="1"/><path d="M9 2v3M15 2v3M9 19v3M15 19v3M19 9h3M19 15h3M2 9h3M2 15h3"/>',
      "scan-line": '<path d="M4 7V5a1 1 0 0 1 1-1h2M17 4h2a1 1 0 0 1 1 1v2M20 17v2a1 1 0 0 1-1 1h-2M7 20H5a1 1 0 0 1-1-1v-2"/><rect width="8" height="8" x="8" y="8" rx="1"/><path d="m10.5 12 1 1 2-2"/>',
      wifi: '<path d="M5 12.6a10 10 0 0 1 14 0"/><path d="M8.4 16a5.2 5.2 0 0 1 7.2 0"/><path d="M11.1 19.3a1.4 1.4 0 0 1 1.8 0"/><circle cx="12" cy="21" r=".5" fill="currentColor" stroke="none"/>',
      music: '<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>',
      bell: '<path d="M18 8a6 6 0 0 0-12 0c0 7-3 7-3 9h18c0-2-3-2-3-9"/><path d="M13.7 21a2 2 0 0 1-3.4 0"/><path d="M18.5 3.5 21 6"/><path d="M5.5 3.5 3 6"/>',
      fan: '<path d="M12 12c0-4 2-7 5-7 2 0 3 2 2 4-1 2-4 3-7 3Z"/><path d="M12 12c4 0 7 2 7 5 0 2-2 3-4 2-2-1-3-4-3-7Z"/><path d="M12 12c0 4-2 7-5 7-2 0-3-2-2-4 1-2 4-3 7-3Z"/><circle cx="12" cy="12" r="2"/>',
      lightbulb: '<path d="M9 18h6"/><path d="M10 22h4"/><path d="M15.1 14.9c.6-.6 1.1-1.3 1.4-2.1A6 6 0 1 0 7.5 12.8c.3.8.8 1.5 1.4 2.1.7.7 1.1 1.6 1.1 2.6h4c0-1 .4-1.9 1.1-2.6Z"/>',
      "thermometer-sun": '<path d="M14 14.76V3a2 2 0 0 0-4 0v11.76a4 4 0 1 0 4 0Z"/><path d="M12 9v7"/><path d="M19 3v2M19 13v2M22 8h-2M18.3 4.7l-1.4 1.4M21.1 11.3l-1.4-1.4"/>',
      keyboard: '<rect width="20" height="14" x="2" y="5" rx="2"/><path d="M6 9h.01M10 9h.01M14 9h.01M18 9h.01M7 13h10"/>',
      "sun-medium": '<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/>',
      "hard-drive": '<rect width="16" height="20" x="4" y="2" rx="2"/><path d="M8 6h8"/><path d="M15 14h.01"/><path d="M15 18h.01"/>',
      "folder-open": '<path d="M6 14a2 2 0 0 1-1.7-3l1.4-2.2A2 2 0 0 1 7.4 8H10l2-2h5.6a2 2 0 0 1 1.7 1l.7 1"/><path d="M3 16.5 5.5 12a2 2 0 0 1 1.7-1h12.4a2 2 0 0 1 1.8 2.9L19.5 18a2 2 0 0 1-1.8 1H5a2 2 0 0 1-2-2.5Z"/>',
      file: '<path d="M15 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V7Z"/><path d="M14 2v6h6"/>'
  });

  const moduleIcon = (module) => {
    if (!module) return "";
    const paths = moduleIconPaths[module.icon] || moduleIconPaths["circuit-board"];
    return `<svg class="module-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${paths}</svg>`;
  };

  // Keep every hardware command visually recognizable. Text remains in the
  // button for clarity and accessibility; the icon makes repeated test flows
  // easier to scan without relying on color alone.
  const actionIconPaths = Object.freeze({
    play: '<polygon points="5 3 19 12 5 21 5 3"/>',
    microphone: '<rect width="8" height="12" x="8" y="2" rx="4"/><path d="M19 10v2a7 7 0 0 1-14 0v-2M12 19v3M8 22h8"/>',
    replay: '<path d="M3 12a9 9 0 1 0 3-6.7"/><path d="M3 4v5h5"/><polygon points="10 8 16 12 10 16 10 8"/>',
    volume: '<path d="M11 5 6 9H2v6h4l5 4V5Z"/><path d="M15.5 8.5a5 5 0 0 1 0 7M18.5 5.5a9 9 0 0 1 0 13"/>',
    stop: '<rect width="12" height="12" x="6" y="6" rx="1"/>',
    "bell-ring": moduleIconPaths.bell,
    "bell-off": '<path d="m13.7 21a2 2 0 0 1-3.4 0M18.6 18H4.4c-1.1 0-1.6-1.4-.7-2.1C5.2 14.7 6 12.7 6 9.5a6 6 0 0 1 9.5-4.9M21 21 3 3"/>',
    waveform: '<path d="M2 12h2l2-7 4 14 3-10 2 3h5"/>',
    apply: '<path d="m5 12 4 4L19 6"/><path d="M4 4h16v16H4z"/>',
    "lightbulb-off": '<path d="M9 18h6M10 22h4M8.2 14.6A6 6 0 1 1 16.5 7M21 21 3 3"/>',
    sun: moduleIconPaths["sun-medium"],
    sparkles: '<path d="m12 3-1.5 5.5L5 10l5.5 1.5L12 17l1.5-5.5L19 10l-5.5-1.5L12 3Z"/><path d="m19 15-.7 2.3L16 18l2.3.7L19 21l.7-2.3L22 18l-2.3-.7L19 15ZM5 3l-.5 1.5L3 5l1.5.5L5 7l.5-1.5L7 5l-1.5-.5L5 3Z"/>',
    refresh: '<path d="M21 12a9 9 0 0 0-15.5-6.2L3 8"/><path d="M3 3v5h5"/><path d="M3 12a9 9 0 0 0 15.5 6.2L21 16"/><path d="M16 16h5v5"/>',
    scan: '<path d="M3 7V5a2 2 0 0 1 2-2h2M17 3h2a2 2 0 0 1 2 2v2M21 17v2a2 2 0 0 1-2 2h-2M7 21H5a2 2 0 0 1-2-2v-2"/><path d="M7 12h10"/>',
    send: '<path d="m22 2-7 20-4-9-9-4Z"/><path d="M22 2 11 13"/>',
    receive: '<path d="M12 3v12M7 10l5 5 5-5"/><path d="M5 21h14"/>',
    network: moduleIconPaths.ethernet,
    save: '<path d="M19 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h11l5 5v11a2 2 0 0 1-2 2Z"/><path d="M17 21v-8H7v8M7 3v5h8"/>',
    plug: '<path d="M12 22v-5M9 8V2M15 8V2M18 8H6v4a6 6 0 0 0 12 0Z"/>',
    repeat: '<path d="m17 2 4 4-4 4"/><path d="M3 11V9a3 3 0 0 1 3-3h15M7 22l-4-4 4-4"/><path d="M21 13v2a3 3 0 0 1-3 3H3"/>',
    radio: moduleIconPaths["radio-tower"],
    camera: moduleIconPaths.camera,
    monitor: moduleIconPaths["monitor-down"],
    wifi: moduleIconPaths.wifi,
    shield: moduleIconPaths["shield-check"],
    warning: '<path d="m21.7 18-8-14a2 2 0 0 0-3.5 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.7-3Z"/><path d="M12 9v4M12 17h.01"/>',
    arrowUp: '<path d="m18 15-6-6-6 6"/><path d="M12 9v12"/>',
    folderOpen: moduleIconPaths["folder-open"],
    eject: '<path d="m12 3 7 12H5L12 3Z"/><path d="M5 21h14"/>',
    gauge: '<path d="M20.4 15a9 9 0 1 0-16.8 0"/><path d="M12 12 16.5 7.5"/><path d="M12 19h.01"/>',
    eye: '<path d="M2.062 12.348a1 1 0 0 1 0-.696C3.423 7.594 7.36 5 12 5c4.639 0 8.573 2.594 9.938 6.652a1 1 0 0 1 0 .696C20.577 16.406 16.64 19 12 19c-4.639 0-8.573-2.594-9.938-6.652Z"/><circle cx="12" cy="12" r="3"/>',
    "eye-off": '<path d="m2 2 20 20"/><path d="M6.71 6.71A10.75 10.75 0 0 0 2.062 11.652a1 1 0 0 0 0 .696C3.423 16.406 7.36 19 12 19c1.77 0 3.43-.378 4.93-1.04"/><path d="M10.733 5.076A10.744 10.744 0 0 1 12 5c4.639 0 8.573 2.594 9.938 6.652a1 1 0 0 1 0 .696 10.75 10.75 0 0 1-1.444 2.49"/><path d="M14.12 14.12a3 3 0 0 1-4.24-4.24"/>'
  });

  const actionIconNames = Object.freeze({
    "gpio-apply": "apply",
    "gpio-read": "scan",
    "uart-tx": "send",
    "uart-rx": "receive",
    "ethernet-refresh": "refresh",
    "ping-interface": "scan",
    "ethernet-dhcp": "network",
    "ethernet-static": "save",
    "can-open": "plug",
    "can-rx": "receive",
    "can-tx": "send",
    "can-close": "stop",
    "can-loop-start": "repeat",
    "can-loop-stop": "stop",
    "cellular-start": "radio",
    "cellular-status": "scan",
    "cellular-ping": "scan",
    "cellular-stop": "stop",
    "camera-start": "camera",
    "camera-stop": "stop",
    "camera-preview-start": "play",
    "camera-preview-stop": "stop",
    "hdmiin-start": "monitor",
    "hdmiin-stop": "stop",
    "watchdog-status": "shield",
    "watchdog-start": "warning",
    "gpu-start": "gauge",
    "gpu-stop": "stop",
    "gpu-preview-start": "monitor",
    "gpu-preview-stop": "stop",
    "npu-yolo-discover": "scan",
    "npu-yolo-start": "play",
    "npu-yolo-stop": "stop",
    "wifi-refresh": "refresh",
    "wifi-scan": "scan",
    "wifi-connect": "wifi",
    "wifi-disconnect": "plug",
    "audio-play-test": "play",
    "audio-record": "microphone",
    "audio-play-recorded": "replay",
    "audio-volume": "volume",
    "audio-stop-audio": "stop",
    "audio-beep-apply": "bell-ring",
    "audio-beep-heartbeat": "waveform",
    "audio-beep-stop": "bell-off",
    "fan-auto": "refresh",
    "fan-manual": "gauge",
    "led-set": "apply",
    "led-all-on": "sun",
    "led-all-off": "lightbulb-off",
    "led-blink": "sparkles",
    "environment-refresh": "refresh",
    "keys-start": "play",
    "keys-stop": "stop",
    "keys-clear": "refresh",
    "backlight-set": "sun",
    "storage-scan": "refresh",
    "storage-mount": "folderOpen",
    "storage-unmount": "eject",
    "storage-speed": "gauge",
    "files-up": "arrowUp",
    "files-open": "folderOpen"
  });

  const actionIcon = (action) => {
    const name = actionIconNames[action];
    const paths = actionIconPaths[name];
    if (!paths) return "";
    return `<svg class="action-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${paths}</svg>`;
  };

  const tokenVisibilityIcon = (visible) => {
    const paths = actionIconPaths[visible ? "eye-off" : "eye"];
    return `<svg class="chrome-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${paths}</svg>`;
  };

  const moduleArrowIcon = () => '<svg class="module-arrow-icon" viewBox="0 0 24 24" aria-hidden="true" focusable="false" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14M13 6l6 6-6 6"/></svg>';

  function addActionIcon(button, action) {
    if (button.querySelector(".action-icon")) return button;
    const icon = actionIcon(action);
    if (icon) button.insertAdjacentHTML("afterbegin", icon);
    return button;
  }

  function decorateControlActions() {
    document.querySelectorAll(".control-action[data-action], .confirm-action[data-action]").forEach((button) => {
      addActionIcon(button, button.dataset.action);
    });
  }

  function buildNavigation() {
    const sidebar = $("sidebarModules");
    const grid = $("moduleGrid");
    const sidebarFragment = document.createDocumentFragment();
    const gridFragment = document.createDocumentFragment();

    sidebar.replaceChildren();
    grid.replaceChildren();
    modules.filter((module) => moduleIsVisible(module)).forEach((module) => {
      const nav = document.createElement("a");
      nav.className = `nav-item accent-${module.id}`;
      nav.href = `#/tool/${module.id}`;
      nav.dataset.route = module.id;
      nav.innerHTML = `<span class="nav-glyph">${moduleIcon(module)}</span><span>${module.title}</span>`;
      sidebarFragment.appendChild(nav);

      const card = document.createElement("a");
      card.className = `module-card accent-${module.id}`;
      card.href = `#/tool/${module.id}`;
      card.innerHTML = `
        <div>
          <div class="module-card-head"><span class="tool-glyph">${moduleIcon(module)}</span><span class="module-count" id="module-count-${module.id}">等待发现</span></div>
          <h3>${module.title}</h3>
          <p>${module.description}</p>
        </div>
        <div class="module-card-foot"><span><i class="status-dot" id="module-dot-${module.id}"></i><span id="module-state-${module.id}">读取状态</span></span><span class="module-arrow">${moduleArrowIcon()}</span></div>`;
      gridFragment.appendChild(card);
    });

    sidebar.appendChild(sidebarFragment);
    grid.appendChild(gridFragment);
  }

  function syncModuleNavigation(board = state.board) {
    const visibleModuleIds = modules.filter((module) => moduleIsVisible(module, board)).map((module) => module.id);
    const navModuleIds = Array.from($("sidebarModules")?.querySelectorAll("[data-route]") || [])
      .map((item) => item.dataset.route);
    const cardModuleIds = modules
      .filter((module) => Boolean($("moduleGrid")?.querySelector(`.module-card.accent-${module.id}`)))
      .map((module) => module.id);
    const hasSameModules = (actual) => actual.length === visibleModuleIds.length
      && actual.every((moduleId) => visibleModuleIds.includes(moduleId));
    const changed = !hasSameModules(navModuleIds) || !hasSameModules(cardModuleIds);
    if (changed) buildNavigation();

    if (!npuYoloDemoEnabled(board) && (changed || state.currentTool === "npu-yolo")) {
      // Ignore any in-flight detail result after the profile removes this
      // fixed workload, and release a locally cached preview immediately.
      state.toolStatusRequestId += 1;
      clearNpuYoloPreview();
    }
    return changed;
  }

  function decorateSpecializedModuleHeaders() {
    ["gpu", "npu-yolo", "wifi", "music", "buzzer", "fan", "led", "environment", "keys"].forEach((moduleId) => {
      const module = moduleById.get(moduleId);
      document.querySelectorAll(`.tool-view[data-tool="${moduleId}"] .work-pane > header`).forEach((header) => {
        const step = header.querySelector(":scope > span");
        if (!module || !step || header.querySelector(".operation-module-icon")) return;

        const icon = document.createElement("span");
        icon.className = "operation-module-icon";
        icon.setAttribute("aria-hidden", "true");
        icon.innerHTML = moduleIcon(module);
        step.insertAdjacentElement("afterend", icon);
      });
    });
  }

  function authorizedHeaders(initial = {}) {
    const headers = new Headers(initial);
    if (state.accessToken) headers.set("Authorization", `Bearer ${state.accessToken}`);
    return headers;
  }

  async function apiFetch(path, options = {}) {
    let response;
    try {
      response = await fetch(path, {
        ...options,
        cache: "no-store",
        headers: authorizedHeaders(options.headers || {})
      });
    } catch (error) {
      error.network = true;
      throw error;
    }

    if (response.status === 401) {
      showAuthentication("Token 无效或服务端 Token 已变更，请重新输入。");
      const error = new Error("认证失败");
      error.authentication = true;
      error.status = 401;
      throw error;
    }
    return response;
  }

  async function getJson(path) {
    const response = await apiFetch(path);
    if (!response.ok) {
      const error = new Error(`${path} 返回 HTTP ${response.status}`);
      error.status = response.status;
      throw error;
    }
    return response.json();
  }

  function showAuthentication(message = "") {
    state.accessToken = "";
    state.authenticated = false;
    stopNpuYoloFastPolling();
    clearNpuYoloPreview();
    if (state.refreshTimer) window.clearInterval(state.refreshTimer);
    state.refreshTimer = 0;
    $("appShell").hidden = true;
    $("skipLink").hidden = true;
    $("authGate").hidden = false;
    setText("authError", message);
    $("tokenInput").value = "";
    closeMobileNavigation();
    window.setTimeout(() => $("tokenInput").focus(), 30);
  }

  function showApplication() {
    state.authenticated = true;
    $("authGate").hidden = true;
    $("appShell").hidden = false;
    $("skipLink").hidden = false;
    setText("authError", "");
    $("tokenInput").value = "";
    routeFromHash();
    restartRefreshTimer();
  }

  function restartRefreshTimer() {
    if (!state.authenticated) return;
    if (state.refreshTimer) window.clearInterval(state.refreshTimer);
    // Key feedback and the DHT11's staged second read must feel immediate.
    // Other pages keep the gentler cadence to avoid needless board polling.
    const interval = state.currentTool === "keys" || state.currentTool === "environment" ? 650 : 2500;
    state.refreshTimer = window.setInterval(refreshAll, interval);
  }

  async function authenticate(event) {
    event.preventDefault();
    const candidate = $("tokenInput").value.trim();
    if (!candidate) {
      setText("authError", "请输入访问 Token。");
      return;
    }

    state.accessToken = candidate;
    setText("authError", "正在验证并读取板端状态…");
    try {
      // Keep the application shell hidden until the profile snapshot is
      // available. This prevents a fixed-demo card from flashing on boards
      // that do not opt in to the bundled RK3588 workload.
      const [system, board] = await Promise.all([
        getJson("/api/system"),
        getJson("/api/board")
      ]);
      state.system = system;
      applyBoardSnapshot(board);
      renderSystem(system);
      renderBoard(state.board);
      hideOffline();
      const busyText = state.board.backendBusy
        ? `后端忙碌 · ${state.board.backendBusyOperation || "硬件操作"}`
        : `已连接 · ${window.location.host}`;
      setConnection(state.board.backendBusy ? "busy" : "online", busyText);
      setText("lastUpdated", `更新于 ${new Date().toLocaleTimeString("zh-CN", { hour12: false })}`);
      showApplication();
      toast("认证成功，已进入迅为开发板测试平台");
    } catch (error) {
      if (!error.authentication) {
        showAuthentication(`暂时无法连接迅为开发板测试平台服务：${error.message}`);
      }
    }
  }

  function parseRoute() {
    const match = /^#\/tool\/([a-z0-9-]+)$/.exec(window.location.hash);
    if (match && moduleById.has(match[1])) return { page: "tool", tool: match[1] };
    return { page: "home", tool: "" };
  }

  function routeFromHash() {
    if (!state.authenticated) return;
    const moveFocusToPageTitle = isCompactNavigation()
      && $("sidebar").classList.contains("open")
      && $("sidebar").contains(document.activeElement);
    let route = parseRoute();
    const requestedModule = moduleById.get(route.tool);
    if (requestedModule && !moduleIsVisible(requestedModule)) {
      // A stale bookmark or a manually edited hash must not expose a tool
      // disabled by the detected board profile. replaceState also avoids a
      // hashchange loop while keeping the browser address truthful.
      window.history.replaceState(null, "", "#/home");
      if (route.tool === "npu-yolo") {
        state.toolStatusRequestId += 1;
        clearNpuYoloPreview();
      }
      route = { page: "home", tool: "" };
    }
    const previousTool = state.currentTool;
    const onHome = route.page === "home";
    $("homePage").hidden = !onHome;
    $("toolPage").hidden = onHome;
    $("backHome").hidden = onHome;
    state.currentTool = route.tool;
    restartRefreshTimer();
    syncNpuYoloFastPolling();
    syncCameraPreviewFastPolling();
    if (previousTool === "npu-yolo" && route.tool !== "npu-yolo") clearNpuYoloPreview();
    if (previousTool === "camera" && route.tool !== "camera") clearCameraPreview();

    document.querySelectorAll(".nav-item").forEach((item) => {
      item.classList.toggle("active", onHome ? item.dataset.route === "home" : item.dataset.route === route.tool);
    });

    if (onHome) {
      setText("breadcrumb", "迅为 / 首页");
      setText("pageTitle", "迅为开发板测试平台");
      setText("pageSubtitle", "板端状态、接口发现与独立测试应用");
    } else {
      const module = moduleById.get(route.tool);
      document.querySelectorAll(".tool-view").forEach((view) => {
        view.hidden = view.dataset.tool !== route.tool;
      });
      $("toolPage").className = `route-page tool-page accent-${route.tool}`;
      setText("breadcrumb", `迅为 / ${module.category}`);
      setText("pageTitle", module.title);
      setText("pageSubtitle", module.description);
      setText("toolCategory", module.category);
      setText("toolTitle", module.title);
      setText("toolDescription", module.description);
      $("toolGlyph").innerHTML = moduleIcon(module);
      updateToolAvailability(route.tool);
      ensureStorageDiscovery();
      void refreshActiveToolStatus();
    }

    // Input monitoring is deliberately non-grabbing, but a background test
    // that is no longer visible is still confusing to an operator. Stop it
    // when the user leaves the dedicated page; the backend treats this as a
    // read-only monitor teardown and never touches the key device otherwise.
    if (previousTool === "keys" && route.tool !== "keys" && state.board.keyTestStatus?.monitoring) {
      void stopKeyMonitoringAfterLeavingPage();
    }

    closeMobileNavigation();
    if (moveFocusToPageTitle) focusCurrentPageTitle();
    window.scrollTo({ top: 0, behavior: prefersReducedMotion() ? "auto" : "smooth" });
  }

  async function stopKeyMonitoringAfterLeavingPage() {
    if (state.keyStopOnLeavePending || !state.accessToken) return;
    state.keyStopOnLeavePending = true;
    try {
      const response = await apiFetch("/api/control", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ action: "keys-stop", params: {} })
      });
      if (response.ok) {
        writeOperationLog("keys", "已离开按键检测，监听已停止");
        window.setTimeout(refreshAll, 120);
      }
    } catch (_) {
      // Navigation must remain responsive even if the board connection drops;
      // a stopped/closed backend process cannot continue to read input.
    } finally {
      state.keyStopOnLeavePending = false;
    }
  }

  function focusCurrentPageTitle() {
    const pageTitle = $("pageTitle");
    pageTitle.setAttribute("tabindex", "-1");
    pageTitle.focus({ preventScroll: true });
  }

  function openMobileNavigation() {
    if (!isCompactNavigation()) return;
    $("sidebar").classList.add("open");
    $("mobileScrim").hidden = false;
    $("mobileMenu").setAttribute("aria-expanded", "true");
    $("mobileMenu").setAttribute("aria-label", "关闭导航");
    $("sidebar").removeAttribute("aria-hidden");
    $("sidebar").removeAttribute("inert");
    $("mainShell").setAttribute("inert", "");
    window.setTimeout(() => $("sidebar").querySelector("a, button")?.focus(), 20);
  }

  function closeMobileNavigation() {
    $("sidebar").classList.remove("open");
    $("mobileScrim").hidden = true;
    $("mobileMenu").setAttribute("aria-expanded", "false");
    $("mobileMenu").setAttribute("aria-label", "打开导航");
    $("mainShell").removeAttribute("inert");
    if (isCompactNavigation()) {
      $("sidebar").setAttribute("aria-hidden", "true");
      $("sidebar").setAttribute("inert", "");
    } else {
      $("sidebar").removeAttribute("aria-hidden");
      $("sidebar").removeAttribute("inert");
    }
  }

  function syncNavigationPresentation() {
    if (!isCompactNavigation()) {
      closeMobileNavigation();
      return;
    }
    if (!$("sidebar").classList.contains("open")) {
      $("sidebar").setAttribute("aria-hidden", "true");
      $("sidebar").setAttribute("inert", "");
    }
  }

  function trapMobileNavigationFocus(event) {
    if (event.key !== "Tab" || !isCompactNavigation() || !$("sidebar").classList.contains("open")) return false;
    const focusable = Array.from($("sidebar").querySelectorAll(
      'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), [tabindex]:not([tabindex="-1"])'
    )).filter((element) => !element.hidden && element.getClientRects().length > 0);
    if (!focusable.length) return false;
    const first = focusable[0];
    const last = focusable[focusable.length - 1];
    if (event.shiftKey && document.activeElement === first) {
      event.preventDefault();
      last.focus();
      return true;
    }
    if (!event.shiftKey && document.activeElement === last) {
      event.preventDefault();
      first.focus();
      return true;
    }
    return false;
  }

  function setConnection(mode, text) {
    setText("bridgeState", text);
    $("connectionDot").className = `status-dot ${mode}`;
    $("connectionChip").dataset.state = mode;
  }

  function showOffline(message) {
    $("offlineBanner").hidden = false;
    setText("offlineMessage", message || "正在等待测试平台服务恢复…");
    setConnection("offline", "测试平台连接中断");
  }

  function hideOffline() {
    $("offlineBanner").hidden = true;
  }

  function applyBoardSnapshot(board) {
    state.board = board || {};
    state.gpuBenchmarkStatus = state.board.gpuBenchmarkStatus || state.gpuBenchmarkStatus;
    state.cameraPreviewStatus = state.board.webCameraPreviewStatus || state.cameraPreviewStatus;
    state.board.webCameraPreviewStatus = state.cameraPreviewStatus;
    state.wifiManagerStatus = mergeWifiManagerStatus(
      state.wifiManagerStatus,
      state.board.wifiManagerStatus
    );
    state.board.wifiManagerStatus = state.wifiManagerStatus;
    state.wifiAccessPoints = Array.isArray(state.board.wifiAccessPoints)
      ? state.board.wifiAccessPoints
      : state.wifiAccessPoints;
    return syncModuleNavigation(state.board);
  }

  async function refreshAll() {
    if (!state.accessToken) return null;
    if (state.refreshPromise) return state.refreshPromise;

    state.refreshPromise = (async () => {
      try {
        const [system, board] = await Promise.all([
          getJson("/api/system"),
          getJson("/api/board")
        ]);
        state.system = system;
        const navigationChanged = applyBoardSnapshot(board);
        renderSystem(system);
        renderBoard(state.board);
        syncNpuYoloFastPolling();
        syncCameraPreviewFastPolling();
        if (navigationChanged && state.authenticated) routeFromHash();
        else void refreshActiveToolStatus();
        hideOffline();
        const busyText = state.board.backendBusy
          ? `后端忙碌 · ${state.board.backendBusyOperation || "硬件操作"}`
          : `已连接 · ${window.location.host}`;
        setConnection(state.board.backendBusy ? "busy" : "online", busyText);
        setText("lastUpdated", `更新于 ${new Date().toLocaleTimeString("zh-CN", { hour12: false })}`);
      } catch (error) {
        if (!error.authentication) {
          showOffline(error.network ? "网络不可达，页面会自动重试。" : error.message);
        }
      } finally {
        state.refreshPromise = null;
      }
    })();
    return state.refreshPromise;
  }

  // The board summary carries these snapshots as well, but the dedicated
  // endpoints keep a tool page current without making any `nmcli` call.  A
  // Wi-Fi scan or refresh remains an explicit operator action only.
  function toolStatusFailure(tool, endpoint, error) {
    const title = moduleById.get(tool)?.title || "当前工具";
    const status = Number(error?.status);
    const endpointUnavailable = [404, 405, 501].includes(status);
    let summary = `${title} 状态详情暂不可读取`;
    let detail;

    if (endpointUnavailable) {
      summary = `${title} 状态详情接口暂不可用`;
      detail = `${summary}。服务可能仍在更新，页面保留最近一次概览状态；完成更新后可点击“刷新状态”重试。`;
    } else if (error?.network) {
      detail = `${summary}。页面保留最近一次概览状态；请检查浏览器与测试平台服务的连接后重试。`;
    } else if (Number.isFinite(status)) {
      detail = `${summary}（HTTP ${status}）。页面保留最近一次概览状态；请稍后点击“刷新状态”重试。`;
    } else {
      detail = `${summary}。页面保留最近一次概览状态；请稍后点击“刷新状态”重试。`;
    }

    return {
      summary,
      detail,
      signature: `${endpoint}:${error?.network ? "network" : (Number.isFinite(status) ? status : "unknown")}`
    };
  }

  function reportToolStatusFailure(tool, endpoint, error) {
    const failure = toolStatusFailure(tool, endpoint, error);
    const previous = state.toolStatusFailures[tool];
    state.toolStatusFailures[tool] = failure;
    updateToolAvailability(tool);
    updateControlAvailability();

    // Polling repeats every few seconds. Keep the latest failure visible, but
    // only add a receipt and transient notification when its cause changes.
    if (!previous || previous.signature !== failure.signature) {
      writeOperationLog(tool, `${moduleById.get(tool)?.title || "当前工具"} 状态读取失败`, failure.detail);
      toast(failure.summary, "error");
    }
  }

  function clearToolStatusFailure(tool) {
    if (!state.toolStatusFailures[tool]) return;
    delete state.toolStatusFailures[tool];
    updateToolAvailability(tool);
    updateControlAvailability();
  }

  async function refreshActiveToolStatus() {
    const tool = state.currentTool;
    if (!state.accessToken || (tool !== "gpu" && tool !== "wifi" && tool !== "npu-yolo" && tool !== "camera")) return;
    if (!moduleIsVisible(moduleById.get(tool))) return;
    if (tool === "npu-yolo") {
      return refreshNpuYoloStatus();
    }
    const requestId = ++state.toolStatusRequestId;
    const endpoint = tool === "gpu" ? "/api/gpu" : (tool === "wifi" ? "/api/wifi" : "/api/camera");
    try {
      const detail = await getJson(endpoint);
      if (requestId !== state.toolStatusRequestId || state.currentTool !== tool) return;
      clearToolStatusFailure(tool);
      if (tool === "gpu") {
        state.gpuBenchmarkStatus = detail || {};
        state.board.gpuBenchmarkStatus = state.gpuBenchmarkStatus;
        renderGpuStatus(state.gpuBenchmarkStatus);
      } else if (tool === "camera") {
        applyCameraPreviewStatus(detail);
        return;
      } else if (tool === "wifi") {
        state.wifiManagerStatus = mergeWifiManagerStatus(state.wifiManagerStatus, detail);
        state.wifiAccessPoints = list(detail?.accessPoints);
        state.board.wifiManagerStatus = state.wifiManagerStatus;
        state.board.wifiAccessPoints = state.wifiAccessPoints;
        state.board.wifiManagerBusy = Boolean(detail?.busy);
        renderWifiStatus(state.wifiManagerStatus, state.wifiAccessPoints);
      }
      updateModuleSummary(state.board);
      updateToolAvailability(tool);
      updateControlAvailability();
    } catch (error) {
      if (requestId !== state.toolStatusRequestId || state.currentTool !== tool || error.authentication) return;
      // A rolling update can briefly expose this UI before its detail endpoint.
      // Keep that scoped to the active tool instead of declaring BoardLab offline.
      reportToolStatusFailure(tool, endpoint, error);
    }
  }

  function renderSystem(system) {
    const cpu = clampPercent(system.cpuUsage);
    const memory = clampPercent(system.memoryUsage);
    const disk = clampPercent(system.diskUsage);
    setText("cpu", `${Math.round(cpu)}%`);
    $("cpuProgress").value = cpu;
    setText("temp", system.temperatureText || "温度未读取");
    setText("mem", `${Math.round(memory)}%`);
    $("memoryProgress").value = memory;
    setText("memText", system.memoryText || "--");
    setText("disk", `${Math.round(disk)}%`);
    $("diskProgress").value = disk;
    setText("diskText", system.diskText || "--");
    setText("network", system.networkText || "未发现地址");
    setText("networkState", system.networkText ? "在线" : "未连接");
    setText("uptime", system.uptimeText || "--");
  }

  function selectValue(item) {
    // ALSA discovery has both a human-readable card name and a stable device
    // selector (for example, hw:2,0). Control requests must carry the latter.
    if (item && typeof item === "object") return item.id || item.path || item.device || item.name || "";
    return item == null ? "" : String(item);
  }

  function fillSelect(id, items, labelBuilder, preferredValue = "") {
    const select = $(id);
    if (!select) return;
    const previous = select.value;
    const rows = list(items);
    const fragment = document.createDocumentFragment();

    if (!rows.length) {
      const option = document.createElement("option");
      option.value = "";
      option.textContent = "未检测到设备";
      fragment.appendChild(option);
    } else {
      rows.forEach((item) => {
        const option = document.createElement("option");
        option.value = selectValue(item);
        option.textContent = labelBuilder
          ? labelBuilder(item)
          : (item && typeof item === "object" ? item.label || item.name || item.path : String(item));
        if (item && typeof item === "object") {
          Object.entries(item).forEach(([key, data]) => {
            if (data !== null && data !== undefined && typeof data !== "object") option.dataset[key] = String(data);
          });
        }
        fragment.appendChild(option);
      });
    }

    select.replaceChildren(fragment);
    if (previous && Array.from(select.options).some((option) => option.value === previous)) {
      select.value = previous;
    } else if (preferredValue && Array.from(select.options).some((option) => option.value === preferredValue)) {
      select.value = preferredValue;
    }
  }

  function preferredAudioDevice(items) {
    const rows = list(items);
    const description = (item) => [item?.card, item?.name, item?.label, item?.device]
      .filter(Boolean)
      .join(" ")
      .toLowerCase();
    // The first ALSA card is commonly HDMI on Rockchip boards. Prefer an
    // analog codec when it is present, then a non-HDMI endpoint, while always
    // preserving a user's later explicit selection.
    const codec = rows.find((item) => /(?:es8|rt56|rk809|codec|analog|headset|speaker|microphone)/.test(description(item)));
    if (codec) return selectValue(codec);
    const nonHdmi = rows.find((item) => !/hdmi/.test(description(item)));
    return selectValue(nonHdmi || rows[0]);
  }

  function setRangeFromBackend(id, labelId, input, available = true) {
    const range = $(id);
    const percent = Math.round(clampPercent(input));
    if (!range) return;

    // A periodic status refresh must not overwrite a value the operator has
    // adjusted but has not yet applied.  Keep the local draft until the board
    // reports the same value, or the command fails and explicitly clears it.
    const draft = state.rangeDrafts[id];
    if (draft !== undefined && Number(draft) === percent) {
      delete state.rangeDrafts[id];
    }
    const effectiveValue = state.rangeDrafts[id] === undefined
      ? String(percent)
      : String(state.rangeDrafts[id]);
    if (state.rangeDrafts[id] === undefined && document.activeElement !== range) {
      range.value = effectiveValue;
    }
    setText(labelId, available ? `${effectiveValue}%` : "未发现");
  }

  // Keep the numeric result visually dominant while reserving a compact,
  // non-truncated place for the unit.  A single large monospaced string made
  // "%RH" disappear on the two-column environment card at desktop widths.
  function renderEnvironmentMeasurement(id, measurement, fallback = "--") {
    const target = $(id);
    if (!target) return;

    const hasValue = measurement
      && measurement.valid !== false
      && measurement.value !== undefined
      && measurement.value !== null
      && String(measurement.value).length > 0;

    target.replaceChildren();
    target.classList.toggle("is-placeholder", !hasValue);
    if (!hasValue) {
      target.textContent = fallback;
      return;
    }

    const value = document.createElement("span");
    value.className = "environment-measurement-value";
    value.textContent = String(measurement.value);
    target.appendChild(value);

    if (measurement.unit) {
      const unit = document.createElement("span");
      unit.className = "environment-measurement-unit";
      unit.textContent = String(measurement.unit);
      target.appendChild(unit);
    }
  }

  function measurementMeta(measurement, waitingText) {
    if (!measurement) return waitingText;
    if (measurement.valid === false) {
      const error = String(measurement.error || "").trim();
      // "not sampled" is the backend's neutral placeholder, not an error a
      // customer needs to diagnose. Keep the human-facing page in the same
      // intentional waiting state as the action flow.
      return error.toLowerCase() === "not sampled" ? waitingText : (error || "本轮未读取到数据");
    }
    const details = [];
    if (measurement.timestamp) details.push(measurement.timestamp);
    if (measurement.raw !== undefined && measurement.raw !== null && String(measurement.raw).length) details.push(`原始值 ${measurement.raw}`);
    return details.length ? details.join(" · ") : waitingText;
  }

  function environmentStatusLabel(rawStatus, sampling, available) {
    const status = String(rawStatus || "").trim();
    const labels = {
      ready: "已发现探头，等待采样",
      sampling: "正在安全采样",
      sampled: "本轮采样完成",
      partial: "本轮采样部分完成",
      "read-error": "本轮未读取到有效数据",
      unavailable: "未发现温湿度探头"
    };
    return labels[status] || (sampling ? "正在安全采样" : (available ? "等待采样" : "未发现温湿度探头"));
  }

  function keyMonitorStatusLabel(rawStatus, monitoring) {
    const status = String(rawStatus || "").trim();
    const listening = status.match(/^Listening for\s+(\d+)\s+user keys?$/i);
    if (listening) return `正在监听 ${listening[1]} 个板载用户按键`;

    const labels = {
      Ready: "准备就绪",
      "Key test passed": "全部按键测试通过",
      "Key monitoring stopped": "按键监听已停止",
      "No device-tree user-key device discovered": "未发现设备树确认的板载用户按键"
    };
    if (labels[status]) return labels[status];
    if (status.startsWith("Key input mapping changed;")) {
      return "按键事件节点已变化，监听已停止；请重新选择并开始。";
    }
    if (status.startsWith("Key input device disappeared;")) {
      return "板载按键设备已消失，监听已停止；请重新发现后开始。";
    }
    return status || (monitoring ? "正在监听板载用户按键" : "未开始监听");
  }

  function keyEventStateLabel(rawState) {
    const state = String(rawState || "").trim();
    return {
      press: "按下",
      release: "松开",
      repeat: "重复",
      unknown: "未知"
    }[state] || state || "事件已收到";
  }

  function renderEnvironmentStatus(environment) {
    const status = environment || {};
    const temperature = status.temperature || {};
    const humidity = status.humidity || status.relativeHumidity || {};
    const sampling = status.sampling === true || Boolean(status.sampling?.inFlight);
    const profileEnabled = profileHasCapability("temperaturehumidity");
    const available = Boolean(status.available);
    renderEnvironmentMeasurement("environmentTemperature", temperature, sampling ? "读取中" : "--");
    renderEnvironmentMeasurement("environmentHumidity", humidity, sampling ? "读取中" : "--");
    setText("environmentTemperatureMeta", measurementMeta(temperature, sampling ? "正在读取温度" : "等待本轮采样"));
    setText("environmentHumidityMeta", measurementMeta(humidity, sampling ? "等待湿度读取" : "等待本轮采样"));
    setText("environmentDevice", status.source || (available ? "已发现环境传感器" : "未发现"));
    setText("environmentState", environmentStatusLabel(status.status, sampling, available));
    setText("environmentUpdatedAt", status.sampledAt || (sampling ? "采样进行中" : "尚未采样"));

    const note = $("environmentAvailability");
    if (note) {
      note.hidden = profileEnabled && available;
      setText("environmentAvailability", !profileEnabled
        ? "当前板型未声明温湿度测试；仅保留只读状态，不会猜测 IIO 设备。"
        : "未发现同一 IIO 设备上的温度与相对湿度通道。请确认底板版本、设备树和 dht11 驱动。");
    }

    const channelList = $("environmentChannels");
    if (!channelList) return;
    const sensors = list(status.sensors);
    if (!sensors.length) {
      channelList.replaceChildren();
      const empty = document.createElement("p");
      empty.className = "empty-state";
      empty.textContent = sampling ? "正在发现并读取传感器…" : "等待温湿度传感器发现";
      channelList.appendChild(empty);
      return;
    }
    const fragment = document.createDocumentFragment();
    sensors.forEach((sensor) => {
      const row = document.createElement("div");
      row.className = "environment-channel-row";
      const name = document.createElement("strong");
      name.textContent = sensor.name || sensor.id || "IIO 传感器";
      const detail = document.createElement("span");
      detail.textContent = [sensor.deviceTreePath || sensor.sourcePath,
        environmentStatusLabel(sensor.status, sampling, true)].filter(Boolean).join(" · ") || "运行时发现";
      row.append(name, detail);
      fragment.appendChild(row);
    });
    channelList.replaceChildren(fragment);
  }

  function renderKeyTestStatus(keyState) {
    const status = keyState || {};
    const devices = list(status.devices).filter((device) => device && device.eligible !== false);
    // A key test is selected by the stable device-tree path, never by its
    // transient /dev/input/eventN node.  Normalize the value used by the
    // shared select helper without discarding the diagnostic event label.
    const keyOptions = devices.map((device) => ({ ...device, id: device.deviceId || device.id || "" }));
    const expectedKeyLabel = (entry) => {
      if (entry && typeof entry === "object") return entry.name || entry.label || entry.key || `KEY_${entry.code ?? "?"}`;
      return String(entry || "");
    };
    fillSelect("keyTestDevice", keyOptions, (device) => {
      const keys = list(device.supportedKeys).map(expectedKeyLabel).filter(Boolean).join(" / ");
      return `${device.name || "板载按键"}${device.event ? ` · ${device.event}` : ""}${keys ? ` · ${keys}` : ""}`;
    }, status.selectedDeviceId || "");
    setText("keyCaptureState", keyMonitorStatusLabel(status.status, Boolean(status.monitoring)));
    setText("keyPassCount", `${Number(status.passedCount || 0)} / ${list(status.expectedKeys).length || "--"}`);
    setText("keyLastEvent", status.lastEvent?.label || status.lastEvent?.key || status.lastEventText || "--");
    setText("keyExpectedKeys", list(status.expectedKeys).length
      ? list(status.expectedKeys).map(expectedKeyLabel).filter(Boolean).join(" / ")
      : "等待选择设备");

    const note = $("keysAvailability");
    if (note) {
      const profileEnabled = profileHasCapability("keys");
      note.hidden = profileEnabled && devices.length > 0;
      setText("keysAvailability", !profileEnabled
        ? "当前板型未启用按键测试，监听已锁定。"
        : "未发现设备树确认的 ADC / GPIO 用户按键。电源键、耳机键和 USB 键盘不会作为测试对象。");
    }

    const eventList = $("keyEventList");
    if (!eventList) return;
    const events = list(status.events);
    if (!events.length) {
      eventList.replaceChildren();
      const empty = document.createElement("li");
      empty.className = "empty-state";
      empty.textContent = status.monitoring ? "正在等待板载用户按键…" : "点击“开始监听”后按下开发板用户按键。";
      eventList.appendChild(empty);
      return;
    }
    const fragment = document.createDocumentFragment();
    events.slice(0, 16).forEach((event) => {
      const item = document.createElement("li");
      const title = document.createElement("strong");
      title.textContent = event.label || event.key || `KEY_${event.code ?? "?"}`;
      const detail = document.createElement("span");
      detail.textContent = [keyEventStateLabel(event.state || event.action), event.timestamp].filter(Boolean).join(" · ") || "事件已收到";
      item.append(title, detail);
      fragment.appendChild(item);
    });
    eventList.replaceChildren(fragment);
  }

  function gpuStatusForBoard(board = state.board) {
    return board?.gpuBenchmarkStatus || state.gpuBenchmarkStatus || {};
  }

  function npuYoloStatusForBoard(board = state.board) {
    return board?.npuYoloStatus || state.npuYoloStatus || {};
  }

  function npuYoloDetailRequestAllowed() {
    const module = moduleById.get("npu-yolo");
    return Boolean(state.authenticated
      && state.accessToken
      && state.currentTool === "npu-yolo"
      && module
      && moduleIsVisible(module));
  }

  function shouldPollNpuYoloFast(status = npuYoloStatusForBoard()) {
    return npuYoloDetailRequestAllowed() && npuYoloDemoEnabled() && Boolean(status?.running);
  }

  function stopNpuYoloFastPolling() {
    const hadPendingPoll = Boolean(state.npuYoloPollTimer || state.npuYoloPollPromise);
    if (state.npuYoloPollTimer) window.clearInterval(state.npuYoloPollTimer);
    state.npuYoloPollTimer = 0;
    // Ignore a response that was started for a page, token, or task state
    // that no longer exists. This also prevents an old response from putting
    // a preview back after the operator leaves the YOLO page.
    if (hadPendingPoll) state.npuYoloPollEpoch += 1;
  }

  function startNpuYoloFastPolling() {
    if (!shouldPollNpuYoloFast()) {
      stopNpuYoloFastPolling();
      return;
    }
    if (state.npuYoloPollTimer) return;

    const pollEpoch = ++state.npuYoloPollEpoch;
    const tick = () => { void refreshNpuYoloStatus(pollEpoch); };
    state.npuYoloPollTimer = window.setInterval(tick, NPU_YOLO_POLL_INTERVAL_MS);
    tick();
  }

  function syncNpuYoloFastPolling() {
    if (shouldPollNpuYoloFast()) startNpuYoloFastPolling();
    else stopNpuYoloFastPolling();
  }

  function applyNpuYoloStatus(detail) {
    state.npuYoloStatus = detail && typeof detail === "object" ? detail : {};
    state.board.npuYoloStatus = state.npuYoloStatus;
    state.npuYoloPollFailureCount = 0;
    clearToolStatusFailure("npu-yolo");
    renderNpuYoloStatus(state.npuYoloStatus);
    updateModuleSummary(state.board);
    updateToolAvailability("npu-yolo");
    updateControlAvailability();
    syncNpuYoloFastPolling();
  }

  async function refreshNpuYoloStatus(expectedEpoch = state.npuYoloPollEpoch) {
    if (!npuYoloDetailRequestAllowed() || expectedEpoch !== state.npuYoloPollEpoch) return null;
    if (state.npuYoloPollPromise) return state.npuYoloPollPromise;

    const requestEpoch = state.npuYoloPollEpoch;
    state.npuYoloPollPromise = (async () => {
      try {
        const detail = await getJson("/api/npu-yolo");
        if (requestEpoch !== state.npuYoloPollEpoch || !npuYoloDetailRequestAllowed()) return null;
        applyNpuYoloStatus(detail);
        return detail;
      } catch (error) {
        if (requestEpoch !== state.npuYoloPollEpoch || !npuYoloDetailRequestAllowed() || error.authentication) return null;
        state.npuYoloPollFailureCount += 1;
        reportToolStatusFailure("npu-yolo", "/api/npu-yolo", error);
        // A server rolling update or a dropped connection must not turn into a
        // six-per-second error loop. The normal low-frequency refresh will
        // retry after the task has settled.
        if (state.npuYoloPollFailureCount >= 3) stopNpuYoloFastPolling();
        return null;
      } finally {
        state.npuYoloPollPromise = null;
      }
    })();
    return state.npuYoloPollPromise;
  }

  function npuYoloProfileEnabled(status = npuYoloStatusForBoard(), board = state.board) {
    return npuYoloDemoEnabled(board) && status?.profileSupported !== false;
  }

  function npuYoloStateLabel(status) {
    const labels = {
      unavailable: "不可用",
      ready: "已就绪",
      starting: "正在准备",
      running: "正在推理",
      stopping: "正在停止",
      stopped: "已停止",
      completed: "已完成",
      failed: "运行失败"
    };
    const stateName = String(status?.status || "").trim();
    return labels[stateName] || stateName || "等待检查";
  }

  function npuYoloProgressText(status) {
    const total = Number(status?.framesTotal);
    const processed = Number(status?.framesProcessed);
    if (Number.isFinite(total) && total > 0 && Number.isFinite(processed)) {
      return `${Math.max(0, Math.round(processed))} / ${Math.round(total)} 帧`;
    }
    if (status?.running) return "正在等待首帧";
    if (String(status?.status || "") === "completed") return "已完成";
    return "尚未开始";
  }

  function npuYoloMetric(value, suffix = "", precision = 0) {
    const number = Number(value);
    if (!Number.isFinite(number) || number < 0) return "--";
    const multiplier = Math.pow(10, precision);
    return `${Math.round(number * multiplier) / multiplier}${suffix}`;
  }

  function npuTelemetryForStatus(status) {
    const primary = status?.npuLoad;
    const fallback = status?.npuTelemetry;
    const telemetry = primary && typeof primary === "object"
      ? primary
      : (fallback && typeof fallback === "object" ? fallback : null);
    const history = Array.isArray(status?.npuLoadHistory)
      ? status.npuLoadHistory
      : list(telemetry?.history);
    return { telemetry, history };
  }

  function npuTelemetryCoreIndex(core, fallbackIndex) {
    const candidates = [core?.id, core?.core];
    let identifierProvided = false;
    for (const candidate of candidates) {
      if (candidate === undefined || candidate === null || String(candidate).trim() === "") continue;
      identifierProvided = true;
      if (typeof candidate === "number") {
        if (Number.isInteger(candidate) && candidate >= 0 && candidate < NPU_TELEMETRY_MAX_CORES) {
          return candidate;
        }
        continue;
      }
      const text = String(candidate ?? "").trim();
      if (/^\d{1,2}$/.test(text)) {
        const direct = Number(text);
        if (direct < NPU_TELEMETRY_MAX_CORES) return direct;
        continue;
      }
      if (Number.isFinite(Number(text))) continue;
      const match = text.match(/^\D*(\d{1,2})\D*$/);
      const parsed = match ? Number(match[1]) : Number.NaN;
      if (Number.isInteger(parsed) && parsed >= 0 && parsed < NPU_TELEMETRY_MAX_CORES) return parsed;
    }
    return !identifierProvided
      && Number.isInteger(fallbackIndex)
      && fallbackIndex >= 0
      && fallbackIndex < NPU_TELEMETRY_MAX_CORES
      ? fallbackIndex
      : -1;
  }

  function npuTelemetryPercent(value) {
    const number = Number(value);
    // The backend deliberately uses -1 for a core that has not been sampled
    // yet. Do not clamp that sentinel to 0%, which would falsely imply an
    // idle but readable NPU before a task starts.
    return Number.isFinite(number) && number >= 0 && number <= 100 ? number : null;
  }

  function npuTelemetryCoreValues(telemetry) {
    const values = Array(NPU_TELEMETRY_MAX_CORES).fill(null);
    list(telemetry?.cores).forEach((core, fallbackIndex) => {
      const index = npuTelemetryCoreIndex(core, fallbackIndex);
      if (index < 0) return;
      values[index] = npuTelemetryPercent(core?.percent ?? core?.load ?? core?.usage);
    });
    return values;
  }

  function npuTelemetryCoreIds(telemetry, status) {
    const ids = [];
    const append = (candidate) => {
      const index = npuTelemetryCoreIndex({ id: candidate }, -1);
      if (index >= 0 && !ids.includes(index)) ids.push(index);
    };
    list(telemetry?.coreIds).forEach(append);
    if (!ids.length) list(status?.npuCoreIds).forEach(append);
    if (!ids.length) {
      list(telemetry?.cores).forEach((core, fallbackIndex) => {
        const index = npuTelemetryCoreIndex(core, fallbackIndex);
        if (index >= 0 && !ids.includes(index)) ids.push(index);
      });
    }
    return ids.sort((left, right) => left - right);
  }

  function npuTelemetryHistoryPoints(history) {
    return list(history).slice(-NPU_TELEMETRY_MAX_SAMPLES).map((sample) => {
      const nestedCores = list(sample?.cores);
      const values = Array.from({ length: NPU_TELEMETRY_MAX_CORES }, (_, index) => {
        const direct = sample?.[`core${index}`] ?? sample?.[`npu${index}`];
        if (direct !== undefined && direct !== null) return npuTelemetryPercent(direct);
        const nested = nestedCores.find((core, fallbackIndex) => npuTelemetryCoreIndex(core, fallbackIndex) === index);
        return npuTelemetryPercent(nested?.percent ?? nested?.load ?? nested?.usage);
      });
      return { values };
    });
  }

  function npuTelemetryFrequencyText(value) {
    const hertz = Number(value);
    if (!Number.isFinite(hertz) || hertz <= 0) return "";
    if (hertz >= 1000000000) return `${(hertz / 1000000000).toFixed(2)} GHz`;
    if (hertz >= 1000000) return `${Math.round(hertz / 1000000)} MHz`;
    return `${Math.round(hertz / 1000)} kHz`;
  }

  function npuTelemetryModeText(telemetry) {
    const mode = String(telemetry?.executionMode || "").trim().toLowerCase();
    const assigned = list(telemetry?.assignedCoreIds)
      .map((id) => npuTelemetryCoreIndex({ id }, -1))
      .filter((id, index, values) => id >= 0 && values.indexOf(id) === index);
    // Keep the established RK3588 label while still describing profiles with
    // other explicitly assigned core counts.
    if (assigned.length === 3 || /three|3-core|triple/.test(mode)) return "三核并行";
    if (assigned.length === 2) return "双核并行";
    if (assigned.length > 3) return `${assigned.length} 核并行`;
    if (/parallel/.test(mode)) return "多核并行";
    if (/auto|automatic/.test(mode)) return "自动核心调度";
    if (assigned.length) return `已分配 NPU${assigned.join(" / NPU")}`;
    return "负载采样";
  }

  function npuTelemetryDetailText(telemetry, coreValues) {
    const details = [];
    const interval = Number(telemetry?.sampleIntervalMs);
    const average = npuTelemetryPercent(telemetry?.averagePercent);
    const frequency = npuTelemetryFrequencyText(telemetry?.frequencyHz);
    const source = String(telemetry?.source || "").trim();
    const activeCoreCount = coreValues.filter((core) => core !== null).length;
    if (Number.isFinite(interval) && interval > 0) details.push(`${Math.round(interval)} ms 采样`);
    if (average !== null) details.push(`平均 ${Math.round(average)}%`);
    if (frequency) details.push(frequency);
    if (!details.length && activeCoreCount) details.push(`${activeCoreCount} 核已读到负载`);
    if (source) details.push(source.replace("/sys/kernel/debug/", ""));
    return details.join(" · ") || "等待下一次 NPU 负载采样";
  }

  function scheduleNpuTelemetryDraw() {
    if (state.npuTelemetryDrawFrame) return;
    const draw = () => {
      state.npuTelemetryDrawFrame = 0;
      drawNpuTelemetryCharts();
    };
    state.npuTelemetryDrawFrame = typeof window.requestAnimationFrame === "function"
      ? window.requestAnimationFrame(draw)
      : window.setTimeout(draw, 0);
  }

  function ensureNpuTelemetryResizeObserver() {
    const charts = Array.from(document.querySelectorAll("[data-npu-telemetry-chart]"));
    if (!charts.length || typeof window.ResizeObserver !== "function") return;
    if (!state.npuTelemetryResizeObserver) {
      state.npuTelemetryResizeObserver = new window.ResizeObserver(() => scheduleNpuTelemetryDraw());
    }
    charts.forEach((chart) => state.npuTelemetryResizeObserver.observe(chart));
  }

  function createNpuTelemetryCard(coreIndex) {
    const coreName = `NPU${coreIndex}`;
    const card = document.createElement("article");
    card.className = `npu-telemetry-card npu-core-${coreIndex}`;
    card.id = `npuTelemetryCard${coreIndex}`;
    card.dataset.npuTelemetryCore = String(coreIndex);
    card.dataset.state = "unavailable";
    card.setAttribute("aria-labelledby", `npuTelemetryCardTitle${coreIndex} npuTelemetryNpu${coreIndex}`);

    const header = document.createElement("header");
    const summary = document.createElement("div");
    const title = document.createElement("span");
    title.className = "npu-telemetry-card-title";
    title.id = `npuTelemetryCardTitle${coreIndex}`;
    title.textContent = coreName;
    const reading = document.createElement("strong");
    reading.id = `npuTelemetryNpu${coreIndex}`;
    reading.textContent = "--";
    summary.append(title, reading);

    const coreState = document.createElement("small");
    coreState.id = `npuTelemetryCoreState${coreIndex}`;
    coreState.setAttribute("role", "status");
    coreState.setAttribute("aria-live", "polite");
    coreState.textContent = "遥测不可用";
    header.append(summary, coreState);

    const chartWrap = document.createElement("div");
    chartWrap.className = "npu-telemetry-chart-wrap";
    const chart = document.createElement("canvas");
    chart.id = `npuTelemetryChart${coreIndex}`;
    chart.dataset.npuTelemetryChart = String(coreIndex);
    chart.setAttribute("role", "img");
    chart.setAttribute("aria-label", `${coreName} 利用率变化曲线`);
    const empty = document.createElement("span");
    empty.id = `npuTelemetryChartEmpty${coreIndex}`;
    empty.textContent = `${coreName} 负载遥测不可用`;
    chartWrap.append(chart, empty);

    card.append(header, chartWrap);
    return card;
  }

  function ensureNpuTelemetryCards(coreIds) {
    const grid = $("npuTelemetryCardGrid");
    if (!grid) return;
    const requestedIds = list(coreIds)
      .map((id) => npuTelemetryCoreIndex({ id }, -1))
      .filter((id, index, values) => id >= 0 && values.indexOf(id) === index)
      .sort((left, right) => left - right);
    const requested = new Set(requestedIds);
    Array.from(grid.querySelectorAll("[data-npu-telemetry-core]")).forEach((card) => {
      const coreIndex = npuTelemetryCoreIndex({ id: card.dataset.npuTelemetryCore }, -1);
      if (requested.has(coreIndex)) return;
      // Keep previously exposed core cards in the DOM and only hide them.
      // The original three-core UI used stable card nodes, and retaining that
      // lifecycle avoids breaking accessibility references and consumers that
      // observe a core while the backend changes its active-core set.
      card.hidden = true;
      card.setAttribute("aria-hidden", "true");
      const chart = card.querySelector("[data-npu-telemetry-chart]");
      if (chart && state.npuTelemetryResizeObserver) {
        state.npuTelemetryResizeObserver.unobserve(chart);
      }
    });

    requestedIds.forEach((coreIndex) => {
      let card = $(`npuTelemetryCard${coreIndex}`);
      if (!card || card.parentElement !== grid) {
        card = createNpuTelemetryCard(coreIndex);
        grid.appendChild(card);
      }
    });

    const activeOrder = Array.from(grid.querySelectorAll("[data-npu-telemetry-core]"))
      .map((card) => npuTelemetryCoreIndex({ id: card.dataset.npuTelemetryCore }, -1))
      .filter((coreIndex) => requested.has(coreIndex));
    if (activeOrder.some((coreIndex, position) => coreIndex !== requestedIds[position])) {
      // Reorder only when the active set actually changed; fast polling must
      // not move all card nodes and force a layout on every status sample.
      requestedIds.forEach((coreIndex) => grid.appendChild($(`npuTelemetryCard${coreIndex}`)));
    }
  }

  function drawNpuTelemetryChart(coreIndex, samples, available) {
    const chart = $(`npuTelemetryChart${coreIndex}`);
    if (!chart) return;
    const context = chart.getContext("2d");
    const bounds = chart.getBoundingClientRect();
    if (!context || bounds.width < 2 || bounds.height < 2) return;

    const pixelRatio = Math.min(2, Math.max(1, Number(window.devicePixelRatio) || 1));
    const width = Math.max(1, Math.round(bounds.width));
    const height = Math.max(1, Math.round(bounds.height));
    const bitmapWidth = Math.round(width * pixelRatio);
    const bitmapHeight = Math.round(height * pixelRatio);
    if (chart.width !== bitmapWidth || chart.height !== bitmapHeight) {
      chart.width = bitmapWidth;
      chart.height = bitmapHeight;
    }
    context.setTransform(pixelRatio, 0, 0, pixelRatio, 0, 0);
    context.clearRect(0, 0, width, height);
    if (!available) return;

    const plot = { left: 24, right: 8, top: 9, bottom: 13 };
    const plotWidth = Math.max(1, width - plot.left - plot.right);
    const plotHeight = Math.max(1, height - plot.top - plot.bottom);
    const baseY = plot.top + plotHeight;
    context.font = '9px "Noto Sans Mono CJK SC", "DejaVu Sans Mono", ui-monospace, monospace';
    context.textBaseline = "middle";
    context.fillStyle = cssToken("muted");
    context.fillText("100", 0, plot.top);
    context.fillText("0", 10, baseY);
    [1 / 3, 2 / 3].forEach((ratio) => {
      const y = plot.top + plotHeight * ratio;
      context.strokeStyle = cssToken("rule");
      context.lineWidth = 1;
      context.setLineDash([2, 3]);
      context.beginPath();
      context.moveTo(plot.left, y + 0.5);
      context.lineTo(width - plot.right, y + 0.5);
      context.stroke();
    });
    context.setLineDash([]);

    const segments = [];
    let segment = [];
    samples.forEach((sample, sampleIndex) => {
      const value = sample.values[coreIndex];
      if (value === null) {
        if (segment.length) segments.push(segment);
        segment = [];
        return;
      }
      const x = plot.left + (samples.length <= 1 ? plotWidth : (sampleIndex / (samples.length - 1)) * plotWidth);
      const y = plot.top + (1 - value / 100) * plotHeight;
      segment.push({ x, y });
    });
    if (segment.length) segments.push(segment);
    if (!segments.length) return;

    const style = cssToken(NPU_TELEMETRY_CORE_TOKENS[coreIndex % NPU_TELEMETRY_CORE_TOKENS.length] || "focus");

    segments.forEach((points) => {
      if (points.length < 2) return;
      context.beginPath();
      context.moveTo(points[0].x, baseY);
      points.forEach((point) => context.lineTo(point.x, point.y));
      context.lineTo(points[points.length - 1].x, baseY);
      context.closePath();
      context.save();
      context.globalAlpha = 0.12;
      context.fillStyle = style;
      context.fill();
      context.restore();

      context.beginPath();
      points.forEach((point, pointIndex) => {
        if (pointIndex === 0) context.moveTo(point.x, point.y);
        else context.lineTo(point.x, point.y);
      });
      context.strokeStyle = style;
      context.lineWidth = 2.1;
      context.lineJoin = "round";
      context.lineCap = "round";
      context.stroke();
    });

    const tail = segments[segments.length - 1][segments[segments.length - 1].length - 1];
    context.save();
    context.globalAlpha = 0.20;
    context.fillStyle = style;
    context.beginPath();
    context.arc(tail.x, tail.y, 6, 0, Math.PI * 2);
    context.fill();
    context.restore();
    context.fillStyle = cssToken("surface-raised");
    context.beginPath();
    context.arc(tail.x, tail.y, 3.4, 0, Math.PI * 2);
    context.fill();
    context.fillStyle = style;
    context.beginPath();
    context.arc(tail.x, tail.y, 2.1, 0, Math.PI * 2);
    context.fill();
  }

  function drawNpuTelemetryCharts() {
    if (state.currentTool !== "npu-yolo") return;
    const status = npuYoloStatusForBoard();
    const { telemetry, history } = npuTelemetryForStatus(status);
    const available = Boolean(telemetry && telemetry.available !== false);
    const samples = npuTelemetryHistoryPoints(history);
    npuTelemetryCoreIds(telemetry, status)
      .forEach((coreIndex) => drawNpuTelemetryChart(coreIndex, samples, available));
  }

  function renderNpuYoloTelemetry(status) {
    const panel = $("npuTelemetryPanel");
    if (!panel) return;
    const { telemetry, history } = npuTelemetryForStatus(status);
    const available = Boolean(telemetry && telemetry.available !== false);
    const coreValues = npuTelemetryCoreValues(telemetry);
    const points = npuTelemetryHistoryPoints(history);
    const coreIds = npuTelemetryCoreIds(telemetry, status);
    ensureNpuTelemetryCards(coreIds);

    panel.dataset.state = available ? (status?.running ? "running" : "ready") : "unavailable";
    panel.setAttribute("aria-label", coreIds.length ? `NPU ${coreIds.length} 核负载遥测` : "NPU 核心负载遥测");
    setText("npuTelemetryTitle", coreIds.length ? `NPU ${coreIds.length} 核利用率` : "NPU 核心利用率");
    setText("npuTelemetryMode", available ? npuTelemetryModeText(telemetry) : "遥测不可用");
    setText("npuTelemetryDetail", available
      ? npuTelemetryDetailText(telemetry, coreValues)
      : (status?.running ? "当前系统未提供 NPU 负载数据。" : "运行内置 MP4 后将读取 NPU 负载。"));
    coreIds.forEach((index) => {
      const core = coreValues[index];
      const id = `npuTelemetryNpu${index}`;
      const card = $(`npuTelemetryCard${index}`);
      if (card) {
        card.hidden = false;
        card.setAttribute("aria-hidden", "false");
      }
      const coreHasHistory = points.some((point) => point.values[index] !== null);
      const stateName = !available
        ? "unavailable"
        : (core === null ? "waiting" : (status?.running ? "running" : "ready"));
      setText(id, core === null ? "--" : `${Math.round(core)}%`);
      if (card) {
        card.dataset.state = stateName;
        card.toggleAttribute("data-active", core !== null);
      }
      const coreStateText = !available
        ? "遥测不可用"
        : (core !== null ? (status?.running ? "实时采样" : "已采样") : (status?.running ? "等待采样" : "未采样"));
      const coreState = $(`npuTelemetryCoreState${index}`);
      if (coreState && coreState.textContent !== coreStateText) coreState.textContent = coreStateText;
      const empty = $(`npuTelemetryChartEmpty${index}`);
      if (empty) {
        empty.hidden = available && coreHasHistory;
        empty.textContent = !available
          ? `NPU${index} 负载遥测不可用`
          : (status?.running ? `正在采集 NPU${index} 负载…` : `运行内置 MP4 后显示 NPU${index} 曲线`);
      }
    });
    ensureNpuTelemetryResizeObserver();
    scheduleNpuTelemetryDraw();
  }

  function clearNpuYoloPreview() {
    if (state.npuPreviewObjectUrl && window.URL && typeof window.URL.revokeObjectURL === "function") {
      window.URL.revokeObjectURL(state.npuPreviewObjectUrl);
    }
    state.npuPreviewObjectUrl = "";
    state.npuPreviewSerial = 0;
    state.npuPreviewLoadingSerial = 0;
    const preview = $("npuYoloPreview");
    if (preview) {
      preview.removeAttribute("src");
      preview.hidden = true;
    }
  }

  function setNpuYoloPreviewMessage(status, title, detail, stateName) {
    const frame = $("npuYoloPreviewFrame");
    const empty = $("npuYoloPreviewEmpty");
    if (frame) frame.dataset.state = stateName || "waiting";
    // Keep the last decoded frame visible while a new one is loading so the
    // operator does not see a blank flash between JPEG updates.
    const keepLastFrame = Boolean(state.npuPreviewObjectUrl) && stateName !== "error";
    if (empty) empty.hidden = keepLastFrame;
    if (!keepLastFrame) {
      setText("npuYoloPreviewTitle", title || "等待推理预览");
      setText("npuYoloPreviewDetail", detail || status?.message || "开始固定示例后，这里会显示后端生成的标注画面。");
    }
  }

  function presentNpuYoloPreview(status) {
    const frame = $("npuYoloPreviewFrame");
    const preview = $("npuYoloPreview");
    const empty = $("npuYoloPreviewEmpty");
    if (!state.npuPreviewObjectUrl || !preview) {
      setNpuYoloPreviewMessage(status,
        status?.running ? "正在等待推理预览" : "等待推理预览",
        status?.running ? "后端生成首个标注帧后会自动显示。" : "开始固定示例后，这里会显示后端生成的标注画面。",
        status?.running ? "running" : "waiting");
      return;
    }
    preview.hidden = false;
    if (empty) empty.hidden = true;
    if (frame) frame.dataset.state = status?.running ? "running" : "ready";
  }

  function decodePreviewBlob(objectUrl) {
    return new Promise((resolve, reject) => {
      const image = new window.Image();
      image.decoding = "async";
      image.onload = () => resolve(image);
      image.onerror = () => reject(new Error("NPU preview JPEG decode failed"));
      image.src = objectUrl;
    });
  }

  async function refreshNpuYoloPreview(status) {
    if (!npuYoloDemoEnabled() || state.currentTool !== "npu-yolo") {
      clearNpuYoloPreview();
      return;
    }
    const serial = Number(status?.previewSerial);
    if (!status?.previewAvailable || !Number.isFinite(serial) || serial <= 0) {
      // While a run is still producing frames, keep the last good image instead
      // of blanking the pane between atomic JPEG rewrites on the board.
      if (status?.running && state.npuPreviewObjectUrl) {
        presentNpuYoloPreview(status);
        return;
      }
      clearNpuYoloPreview();
      presentNpuYoloPreview(status);
      return;
    }
    if (state.npuPreviewObjectUrl && state.npuPreviewSerial === serial) {
      presentNpuYoloPreview(status);
      return;
    }
    // Do not allow a slow JPEG request to pile up behind each new inference
    // frame. The next status tick picks up the newest serial once this one
    // settles, which keeps both the browser and the board responsive.
    if (state.npuPreviewLoadingSerial) return;
    state.npuPreviewLoadingSerial = serial;
    let createdObjectUrl = "";
    try {
      const response = await apiFetch("/api/npu-yolo/preview");
      if (!response.ok) {
        const error = new Error(`/api/npu-yolo/preview 返回 HTTP ${response.status}`);
        error.status = response.status;
        throw error;
      }
      const image = await response.blob();
      if (!image.type.startsWith("image/jpeg") || !window.URL || typeof window.URL.createObjectURL !== "function") {
        throw new Error("NPU preview did not return a JPEG image");
      }
      if (state.currentTool !== "npu-yolo"
        || state.npuPreviewLoadingSerial !== serial
        || Number(npuYoloStatusForBoard().previewSerial) !== serial) return;

      createdObjectUrl = window.URL.createObjectURL(image);
      // Fully decode before swapping so the <img> never shows a blank gap.
      await decodePreviewBlob(createdObjectUrl);
      if (state.currentTool !== "npu-yolo"
        || state.npuPreviewLoadingSerial !== serial
        || Number(npuYoloStatusForBoard().previewSerial) !== serial) {
        if (window.URL && typeof window.URL.revokeObjectURL === "function") {
          window.URL.revokeObjectURL(createdObjectUrl);
        }
        createdObjectUrl = "";
        return;
      }

      const previousObjectUrl = state.npuPreviewObjectUrl;
      state.npuPreviewObjectUrl = createdObjectUrl;
      state.npuPreviewSerial = serial;
      createdObjectUrl = "";
      const preview = $("npuYoloPreview");
      if (preview) {
        preview.hidden = false;
        preview.src = state.npuPreviewObjectUrl;
      }
      presentNpuYoloPreview(npuYoloStatusForBoard());
      if (previousObjectUrl && window.URL && typeof window.URL.revokeObjectURL === "function") {
        window.URL.revokeObjectURL(previousObjectUrl);
      }
    } catch (error) {
      if (createdObjectUrl && window.URL && typeof window.URL.revokeObjectURL === "function") {
        window.URL.revokeObjectURL(createdObjectUrl);
      }
      // Keep the last good frame on transient read errors during a live run.
      if (!error.authentication && Number(npuYoloStatusForBoard().previewSerial) === serial) {
        if (status?.running && state.npuPreviewObjectUrl) {
          presentNpuYoloPreview(status);
        } else {
          clearNpuYoloPreview();
          setNpuYoloPreviewMessage(status, "预览暂不可读取",
            "当前标注图仍在生成或已更新，请等待下一次状态刷新。", "error");
        }
      }
    } finally {
      if (state.npuPreviewLoadingSerial === serial) state.npuPreviewLoadingSerial = 0;
    }
  }

  function renderNpuYoloStatus(rawStatus) {
    if (!npuYoloDemoEnabled()) {
      clearNpuYoloPreview();
      return;
    }
    const status = rawStatus || {};
    const assets = status.assets || {};
    const runtime = status.runtime || {};
    const profileEnabled = npuYoloProfileEnabled(status);
    const totalAssets = [assets.modelReady, assets.labelsReady, assets.videoReady].filter(Boolean).length;
    const previewTitle = status?.previewAvailable || state.npuPreviewObjectUrl ? "最近标注结果" : "等待推理预览";
    const previewDetail = status?.previewAvailable || state.npuPreviewObjectUrl
      ? "标注图由开发板当前固定任务生成。"
      : (status?.error || status?.message || "开始固定示例后，这里会显示后端生成的标注画面。");

    setText("npuYoloRunState", npuYoloStateLabel(status));
    setText("npuYoloProfile", status.profileSupported
      ? `已验证 · ${status.profileLabel || "iTOP 产品板"}`
      : (status.profileLabel ? `未启用 · ${status.profileLabel}` : "等待板型检查"));
    setText("npuYoloRuntime", runtime.compiled
      ? (runtime.rknpuDevice ? "RKNN 已编译 · NPU 已发现" : "RKNN 已编译 · 等待 NPU")
      : "未编译 RKNN 运行时");
    setText("npuYoloAssets", `${totalAssets} / 3 项内置资源就绪`);
    setText("npuYoloFrames", npuYoloProgressText(status));
    setText("npuYoloDetections", npuYoloMetric(status.detectionsTotal, " 个"));
    setText("npuYoloInference", npuYoloMetric(status.averageInferenceMs, " ms", 1));
    setText("npuYoloFps", npuYoloMetric(status.fps, " FPS", 1));
    const progress = Math.max(0, Math.min(100, Number(status.progressPercent) || 0));
    const progressNode = $("npuYoloProgress");
    if (progressNode) progressNode.value = progress;
    setText("npuYoloProgressText", npuYoloProgressText(status));

    const availability = $("npuYoloAvailability");
    const statusFailure = state.toolStatusFailures["npu-yolo"];
    if (availability) {
      availability.hidden = profileEnabled && Boolean(status.available) && !statusFailure;
      setText("npuYoloAvailability", statusFailure?.detail || (!profileEnabled
        ? (status.error || "当前板型未启用固定 YOLOv8 NPU 示例。")
        : (status.error || status.message || "固定 YOLOv8 示例尚未准备完成。")));
    }

    renderNpuYoloTelemetry(status);

    if (state.currentTool !== "npu-yolo") {
      clearNpuYoloPreview();
      return;
    }

    if (!status.previewAvailable) {
      if (status.running && state.npuPreviewObjectUrl) {
        // Atomic JPEG rewrite windows can briefly report no preview; keep the
        // last frame painted so the web UI does not flash empty.
        presentNpuYoloPreview(status);
      } else {
        clearNpuYoloPreview();
        setNpuYoloPreviewMessage(status, previewTitle, previewDetail,
          status.running ? "running" : (status.error ? "error" : "waiting"));
      }
    } else {
      presentNpuYoloPreview(status);
      void refreshNpuYoloPreview(status);
    }
  }

  function cameraPreviewStatusForBoard(board = state.board) {
    return board?.webCameraPreviewStatus || state.cameraPreviewStatus || {};
  }

  function cameraPreviewDetailRequestAllowed() {
    const module = moduleById.get("camera");
    return Boolean(state.authenticated
      && state.accessToken
      && state.currentTool === "camera"
      && module
      && moduleIsVisible(module));
  }

  function cameraPreviewStateLabel(status) {
    const labels = {
      unavailable: "预览不可用",
      idle: "未启动",
      starting: "正在启动",
      waiting: "等待首帧",
      running: "实时显示中",
      stopping: "正在停止",
      stopped: "已停止",
      failed: "预览失败"
    };
    const stateName = String(status?.status || "").trim();
    return labels[stateName] || stateName || "未启动";
  }

  function shouldPollCameraPreview(status = cameraPreviewStatusForBoard()) {
    if (!cameraPreviewDetailRequestAllowed()) return false;
    const stateName = String(status?.status || "").trim();
    return Boolean(status?.running || status?.active || status?.previewAvailable
      || ["starting", "waiting", "stopping"].includes(stateName));
  }

  function stopCameraPreviewFastPolling() {
    const hadPendingPoll = Boolean(state.cameraPreviewPollTimer || state.cameraPreviewPollPromise);
    if (state.cameraPreviewPollTimer) window.clearInterval(state.cameraPreviewPollTimer);
    state.cameraPreviewPollTimer = 0;
    if (hadPendingPoll) state.cameraPreviewPollEpoch += 1;
  }

  function startCameraPreviewFastPolling() {
    if (!shouldPollCameraPreview()) {
      stopCameraPreviewFastPolling();
      return;
    }
    if (state.cameraPreviewPollTimer) return;
    const pollEpoch = ++state.cameraPreviewPollEpoch;
    const tick = () => { void refreshCameraPreviewStatus(pollEpoch); };
    state.cameraPreviewPollTimer = window.setInterval(tick, CAMERA_PREVIEW_POLL_INTERVAL_MS);
    tick();
  }

  function syncCameraPreviewFastPolling() {
    if (shouldPollCameraPreview()) startCameraPreviewFastPolling();
    else stopCameraPreviewFastPolling();
  }

  async function refreshCameraPreviewStatus(requestEpoch = state.cameraPreviewPollEpoch) {
    if (!cameraPreviewDetailRequestAllowed()) return null;
    if (state.cameraPreviewPollPromise) return state.cameraPreviewPollPromise;
    state.cameraPreviewPollPromise = (async () => {
      try {
        const detail = await getJson("/api/camera");
        if (requestEpoch !== state.cameraPreviewPollEpoch || !cameraPreviewDetailRequestAllowed()) return null;
        applyCameraPreviewStatus(detail);
        return detail;
      } catch (error) {
        if (requestEpoch !== state.cameraPreviewPollEpoch || !cameraPreviewDetailRequestAllowed() || error.authentication) return null;
        state.cameraPreviewPollFailureCount += 1;
        reportToolStatusFailure("camera", "/api/camera", error);
        if (state.cameraPreviewPollFailureCount >= 3) stopCameraPreviewFastPolling();
        return null;
      } finally {
        state.cameraPreviewPollPromise = null;
      }
    })();
    return state.cameraPreviewPollPromise;
  }

  function applyCameraPreviewStatus(detail) {
    state.cameraPreviewStatus = detail && typeof detail === "object" ? detail : {};
    state.board.webCameraPreviewStatus = state.cameraPreviewStatus;
    state.cameraPreviewPollFailureCount = 0;
    clearToolStatusFailure("camera");
    renderCameraPreviewStatus(state.cameraPreviewStatus);
    updateModuleSummary(state.board);
    updateToolAvailability("camera");
    updateControlAvailability();
    syncCameraPreviewFastPolling();
  }

  function clearCameraPreview() {
    if (state.cameraPreviewObjectUrl && window.URL && typeof window.URL.revokeObjectURL === "function") {
      window.URL.revokeObjectURL(state.cameraPreviewObjectUrl);
    }
    state.cameraPreviewObjectUrl = "";
    state.cameraPreviewSerial = 0;
    state.cameraPreviewLoadingSerial = 0;
    const preview = $("cameraPreview");
    if (preview) {
      preview.removeAttribute("src");
      preview.hidden = true;
    }
  }

  function setCameraPreviewMessage(status, title, detail, stateName) {
    const frame = $("cameraPreviewFrame");
    const empty = $("cameraPreviewEmpty");
    if (frame) frame.dataset.state = stateName || "waiting";
    const keepLastFrame = Boolean(state.cameraPreviewObjectUrl) && stateName !== "error";
    if (empty) empty.hidden = keepLastFrame;
    if (!keepLastFrame) {
      setText("cameraPreviewTitle", title || "等待实时预览");
      setText("cameraPreviewDetail", detail || status?.message || "选择摄像头后启动 Web 实时预览。首帧到达后会自动显示。");
    }
  }

  function presentCameraPreview(status) {
    const preview = $("cameraPreview");
    const empty = $("cameraPreviewEmpty");
    if (!state.cameraPreviewObjectUrl || !preview) {
      const stateName = status?.error ? "error" : (status?.running ? "running" : "waiting");
      setCameraPreviewMessage(status,
        status?.running ? "正在等待摄像头首帧" : "等待实时预览",
        status?.running
          ? "GStreamer 正在采集视频，首帧到达后会自动显示。"
          : "选择摄像头后启动 Web 实时预览。首帧到达后会自动显示。",
        stateName);
      return;
    }
    preview.hidden = false;
    if (empty) empty.hidden = true;
    const frame = $("cameraPreviewFrame");
    if (frame) frame.dataset.state = status?.active ? "running" : "ready";
  }

  async function refreshCameraPreview(status) {
    if (!cameraPreviewDetailRequestAllowed()) {
      clearCameraPreview();
      return;
    }
    const serial = Number(status?.frameSerial);
    if (!status?.previewAvailable || !Number.isFinite(serial) || serial <= 0) {
      if ((status?.running || status?.active) && state.cameraPreviewObjectUrl) {
        presentCameraPreview(status);
        return;
      }
      clearCameraPreview();
      presentCameraPreview(status);
      return;
    }
    if (state.cameraPreviewObjectUrl && state.cameraPreviewSerial === serial) {
      presentCameraPreview(status);
      return;
    }
    if (state.cameraPreviewLoadingSerial) return;
    state.cameraPreviewLoadingSerial = serial;
    let createdObjectUrl = "";
    try {
      const response = await apiFetch("/api/camera/preview");
      if (!response.ok) {
        const error = new Error(`/api/camera/preview 返回 HTTP ${response.status}`);
        error.status = response.status;
        throw error;
      }
      const image = await response.blob();
      if (!image.type.startsWith("image/jpeg") || !window.URL || typeof window.URL.createObjectURL !== "function") {
        throw new Error("Camera preview did not return a JPEG image");
      }
      if (state.currentTool !== "camera"
        || state.cameraPreviewLoadingSerial !== serial
        || Number(cameraPreviewStatusForBoard().frameSerial) !== serial) return;

      createdObjectUrl = window.URL.createObjectURL(image);
      await decodePreviewBlob(createdObjectUrl);
      if (state.currentTool !== "camera"
        || state.cameraPreviewLoadingSerial !== serial
        || Number(cameraPreviewStatusForBoard().frameSerial) !== serial) {
        if (window.URL && typeof window.URL.revokeObjectURL === "function") {
          window.URL.revokeObjectURL(createdObjectUrl);
        }
        createdObjectUrl = "";
        return;
      }

      const previousObjectUrl = state.cameraPreviewObjectUrl;
      state.cameraPreviewObjectUrl = createdObjectUrl;
      state.cameraPreviewSerial = serial;
      createdObjectUrl = "";
      const preview = $("cameraPreview");
      if (preview) {
        preview.hidden = false;
        preview.src = state.cameraPreviewObjectUrl;
      }
      presentCameraPreview(cameraPreviewStatusForBoard());
      if (previousObjectUrl && window.URL && typeof window.URL.revokeObjectURL === "function") {
        window.URL.revokeObjectURL(previousObjectUrl);
      }
    } catch (error) {
      if (createdObjectUrl && window.URL && typeof window.URL.revokeObjectURL === "function") {
        window.URL.revokeObjectURL(createdObjectUrl);
      }
      if (!error.authentication && Number(cameraPreviewStatusForBoard().frameSerial) === serial) {
        if ((status?.running || status?.active) && state.cameraPreviewObjectUrl) {
          presentCameraPreview(status);
        } else {
          clearCameraPreview();
          setCameraPreviewMessage(status, "预览暂不可读取",
            "当前 JPEG 帧仍在生成或已更新，请等待下一次状态刷新。", "error");
        }
      }
    } finally {
      if (state.cameraPreviewLoadingSerial === serial) state.cameraPreviewLoadingSerial = 0;
    }
  }

  function renderCameraPreviewStatus(rawStatus) {
    const status = rawStatus || {};
    const stateName = String(status.status || "idle").trim() || "idle";
    const mode = String(status.mode || "").trim().toLowerCase();
    const previewServiceAvailable = status.available !== false;
    const cameraRuntime = moduleRuntime("camera");
    const statusFailure = state.toolStatusFailures.camera;
    setText("cameraPreviewState", cameraPreviewStateLabel(status));
    setText("cameraMode", mode === "mipi" ? "MIPI / RKISP"
      : (mode === "usb" ? "USB UVC" : (mode ? mode.toUpperCase() : "自动识别")));
    const width = Number(status.previewWidth);
    const height = Number(status.previewHeight);
    const rate = Number(status.frameRate);
    setText("cameraPreviewRate", Number.isFinite(rate) && rate > 0
      ? `${rate} FPS${Number.isFinite(width) && Number.isFinite(height) ? ` · ${width} × ${height}` : ""}`
      : "--");

    const availability = $("cameraPreviewAvailability");
    if (availability) {
      availability.hidden = cameraRuntime.available && previewServiceAvailable && !status.error && !statusFailure;
      setText("cameraPreviewAvailability", statusFailure?.detail || (!cameraRuntime.profileEnabled
        ? "当前板型未启用摄像头测试，Web 实时预览已锁定。"
        : (!previewServiceAvailable
          ? (status.error || status.message || "未找到 GStreamer 预览组件，无法生成浏览器画面。")
          : (status.error || "摄像头实时预览当前不可用。"))));
    }

    if (state.currentTool !== "camera") {
      clearCameraPreview();
      return;
    }
    if (!status.previewAvailable) {
      if ((status.running || status.active) && state.cameraPreviewObjectUrl) {
        presentCameraPreview(status);
      } else {
        clearCameraPreview();
        setCameraPreviewMessage(status,
          stateName === "failed" ? "实时预览未能启动" : (status.running ? "正在等待摄像头首帧" : "等待实时预览"),
          status.error || status.message || "选择摄像头后启动 Web 实时预览。",
          stateName === "failed" ? "error" : (status.running ? "running" : "waiting"));
      }
    } else {
      presentCameraPreview(status);
      void refreshCameraPreview(status);
    }
  }

  function wifiStatusForBoard(board = state.board) {
    return board?.wifiManagerStatus || state.wifiManagerStatus || {};
  }

  function wifiAccessPointsForBoard(board = state.board) {
    return Array.isArray(board?.wifiAccessPoints) ? board.wifiAccessPoints : list(state.wifiAccessPoints);
  }

  function wifiSnapshotHasDevice(status) {
    return list(status?.devices).some((device) => String(device?.device || "").trim().length > 0);
  }

  function wifiStatusNeedsRead(status) {
    const stateName = String(status?.status || "").trim().toLowerCase();
    const ipv4State = String(status?.ipv4Status || "").trim().toLowerCase();
    return !wifiSnapshotHasDevice(status)
      && !status?.busy
      && (stateName === "idle" || ipv4State === "not-read");
  }

  function wifiTransientEmptySnapshot(status) {
    const stateName = String(status?.status || "").trim().toLowerCase();
    return Boolean(status?.busy)
      && !wifiSnapshotHasDevice(status)
      && ["refreshing", "scanning", "connecting", "disconnecting"].includes(stateName);
  }

  function mergeWifiManagerStatus(previous, incoming) {
    const next = incoming && typeof incoming === "object" ? incoming : {};
    if (!wifiTransientEmptySnapshot(next) || !wifiSnapshotHasDevice(previous)) return next;

    // NetworkManager can briefly publish an empty device list during a state
    // transition. Keep the last confirmed adapter and IPv4 visible until the
    // next complete snapshot arrives.
    const retained = { ...next };
    ["devices", "selectedDevice", "ipv4", "ipAddress", "ipv4Address", "ipv4Addresses", "ipv4Cidr", "ipv4Prefix", "ipv4Assigned"].forEach((field) => {
      const candidate = next[field];
      const empty = candidate === undefined || candidate === null || candidate === ""
        || (Array.isArray(candidate) && candidate.length === 0) || candidate === false;
      if (empty && previous[field] !== undefined) retained[field] = previous[field];
    });
    return retained;
  }

  function truthyNetworkFlag(input) {
    return input === true || input === 1 || String(input || "").trim().toLowerCase() === "true" || String(input || "").trim() === "1";
  }

  function firstNetworkAddress(input) {
    if (input === undefined || input === null) return "";
    if (typeof input === "string" || typeof input === "number") return String(input).trim();
    if (Array.isArray(input)) {
      for (const item of input) {
        const address = firstNetworkAddress(item);
        if (address) return address;
      }
      return "";
    }
    if (typeof input === "object") {
      for (const field of ["address", "ipv4", "ipAddress"]) {
        const address = firstNetworkAddress(input[field]);
        if (address) return address;
      }
    }
    return "";
  }

  function selectedWifiDevice(status = wifiStatusForBoard()) {
    const devices = list(status?.devices);
    const selectedName = value("wifiInterface").trim() || String(status?.selectedDevice || "").trim();
    return devices.find((device) => String(device?.device || "").trim() === selectedName) || {};
  }

  // NetworkManager's top-level IPv4 fields describe status.selectedDevice.
  // Keep them out of a manually selected secondary adapter, otherwise the UI
  // can confidently show the address belonging to another Wi-Fi interface.
  function selectedWifiMatchesStatusDevice(status = wifiStatusForBoard()) {
    const selectedName = value("wifiInterface").trim();
    const statusDevice = String(status?.selectedDevice || "").trim();
    return Boolean(selectedName) && Boolean(statusDevice) && selectedName === statusDevice;
  }

  function wifiIpv4Details(status = wifiStatusForBoard()) {
    const device = selectedWifiDevice(status);
    const statusFallbackAllowed = selectedWifiMatchesStatusDevice(status);
    const addressValues = [device?.ipv4, device?.ipAddress, device?.ipv4Addresses];
    if (statusFallbackAllowed) {
      addressValues.push(status?.ipv4, status?.ipAddress, status?.ipv4Addresses);
    }
    const address = addressValues
      .map(firstNetworkAddress)
      .find(Boolean) || "";
    const assigned = truthyNetworkFlag(device?.ipv4Assigned)
      || (statusFallbackAllowed && truthyNetworkFlag(status?.ipv4Assigned));
    const deviceState = String(device?.state || "").trim().toLowerCase();
    const connected = truthyNetworkFlag(device?.connected)
      || deviceState === "connected" || deviceState === "activated" || Boolean(device?.connection);
    if (address) {
      return {
        address,
        note: "连接成功后，请使用此地址重新打开测试平台或远程桌面。",
        state: "ready"
      };
    }
    if (assigned) return { address: "已分配，正在读取地址", note: "系统已报告地址分配，下一次状态刷新会显示实际 IPv4。", state: "waiting" };
    if (connected) return { address: "已连接，等待地址分配", note: "正在等待 DHCP 或 NetworkManager 返回 IPv4。", state: "waiting" };
    return { address: "连接后显示 IPv4", note: "连接成功后会在这里显示实际 IPv4 地址。", state: "waiting" };
  }

  function ethernetInterfacesForBoard(board = state.board) {
    return list(board?.ethernetInterfaces);
  }

  function selectedEthernetInterface(board = state.board) {
    const interfaces = ethernetInterfacesForBoard(board);
    const selectedName = value("ethernetInterface").trim();
    return interfaces.find((item) => String(item?.name || "").trim() === selectedName) || interfaces[0] || {};
  }

  function ethernetCarrierPresent(iface) {
    if (iface?.carrier !== undefined && iface?.carrier !== null) return truthyNetworkFlag(iface.carrier);
    if (iface?.linkUp !== undefined && iface?.linkUp !== null) return truthyNetworkFlag(iface.linkUp);
    const stateName = String(iface?.state || "").trim().toLowerCase();
    return stateName === "up" || stateName === "connected";
  }

  function ethernetLinkDetails(iface = selectedEthernetInterface()) {
    const name = String(iface?.name || "").trim();
    const stateName = String(iface?.state || "").trim().toLowerCase();
    if (!name) return { label: "未检测到接口", state: "waiting" };
    if (iface?.carrier !== undefined || iface?.linkUp !== undefined) {
      return ethernetCarrierPresent(iface)
        ? { label: "网线已连接", state: "ready" }
        : { label: "未检测到网线链路", state: "warning" };
    }
    if (stateName === "up" || stateName === "connected") return { label: "链路已连接", state: "ready" };
    if (stateName === "down" || stateName === "disconnected") return { label: "链路未连接", state: "warning" };
    return { label: stateName || "链路状态未知", state: "waiting" };
  }

  function ethernetIpv4Details(iface = selectedEthernetInterface()) {
    const address = [iface?.ipv4, iface?.ipAddress, iface?.ipv4Addresses]
      .map(firstNetworkAddress)
      .find(Boolean) || "";
    if (address) return { address, note: "该地址来自当前选中的以太网接口。", state: "ready" };
    if (truthyNetworkFlag(iface?.ipv4Assigned)) {
      return { address: "已分配，正在读取地址", note: "系统已报告地址分配，下一次状态刷新会显示实际 IPv4。", state: "waiting" };
    }
    if (ethernetCarrierPresent(iface)) return { address: "尚未分配 IPv4", note: "物理链路正常，等待 DHCP 分配或应用静态地址。", state: "warning" };
    return { address: "等待网线连接", note: "连接网线后刷新接口状态。", state: "waiting" };
  }

  function ethernetDiagnosticLabel(iface = selectedEthernetInterface()) {
    const diagnostic = String(iface?.diagnostic || "").trim();
    if (diagnostic) return diagnostic;
    const link = ethernetLinkDetails(iface);
    const ipv4 = ethernetIpv4Details(iface);
    if (link.state === "warning") return "请检查网线、交换机端口和供电。";
    if (ipv4.state !== "ready") return "链路正常，等待 DHCP 分配 IPv4，或解锁后配置静态地址。";
    return "链路和 IPv4 已就绪，可以执行默认 Ping 百度测试。";
  }

  function ethernetPingTarget() {
    return value("pingTarget").trim() || "www.baidu.com";
  }

  function syncEthernetPingButtonLabel() {
    const button = document.querySelector('.tool-view[data-tool="ethernet"] [data-action="ping-interface"]');
    if (!button) return;
    const label = ethernetPingTarget() === "www.baidu.com" ? "Ping 百度" : "Ping 当前目标";
    const textNode = Array.from(button.childNodes).find((node) => node.nodeType === Node.TEXT_NODE && node.nodeValue.trim().length > 0);
    if (textNode) textNode.nodeValue = label;
    else button.appendChild(document.createTextNode(label));
  }

  function renderNetworkReadout(id, details) {
    const readout = $(id);
    if (!readout) return;
    readout.dataset.state = details.state || "waiting";
    const primary = readout.querySelector("strong");
    const note = readout.querySelector("small");
    if (primary) primary.textContent = details.address || "--";
    if (note) note.textContent = details.note || "";
  }

  function setEthernetPingFeedback(feedback) {
    state.ethernetPingFeedback = feedback || null;
    const result = $("ethernetPingResult");
    if (!result) return;
    const current = feedback || {
      state: "waiting",
      message: "等待 Ping 百度",
      detail: "默认目标：www.baidu.com"
    };
    result.dataset.state = current.state || "waiting";
    const primary = result.querySelector("strong");
    const detail = result.querySelector("small");
    if (primary) primary.textContent = current.message || "等待 Ping 百度";
    if (detail) detail.textContent = current.detail || "默认目标：www.baidu.com";
  }

  function renderEthernetStatus(board = state.board) {
    const iface = selectedEthernetInterface(board);
    const interfaceName = String(iface?.name || "").trim() || "未检测到接口";
    const link = ethernetLinkDetails(iface);
    const ipv4 = ethernetIpv4Details(iface);
    setText("ethernetStatus", interfaceName);
    setText("ethernetLinkInline", link.label);
    setText("ethernetLinkState", link.label);
    setText("ethernetIpv4Status", ipv4.address);
    setText("ethernetIpv4", ipv4.address);
    setText("ethernetIpv4Note", ipv4.note);
    setText("ethernetDiagnostic", ethernetDiagnosticLabel(iface));
    setText("ethernetPingTargetStatus", ethernetPingTarget());
    setText("ethernetReadySummary", link.state === "ready" && ipv4.state === "ready"
      ? `接口 ${interfaceName} 已就绪，可 Ping ${ethernetPingTarget()}。`
      : "先确认网线链路和 IPv4，再执行 Ping 测试。");
    syncEthernetPingButtonLabel();
    const readout = $("ethernetAddressReadout");
    if (readout) readout.dataset.state = ipv4.state;
    setEthernetPingFeedback(state.ethernetPingFeedback);
  }

  function gpuStateLabel(status) {
    const stateName = String(status?.status || "").trim();
    const labels = {
      idle: "等待开始",
      unavailable: "未发现 glmark2",
      ready: "可以开始",
      running: "正在运行",
      stopping: "正在停止",
      completed: "测试完成",
      failed: "测试失败",
      stopped: "已停止"
    };
    return labels[stateName] || stateName || "等待发现";
  }

  function gpuPreviewStateLabel(status) {
    const stateName = String(status?.previewStatus || "").trim();
    const labels = {
      idle: "等待检查",
      unavailable: "内嵌 GPU 预览不可用",
      ready: "可以启动内嵌 GPU 预览",
      running: "GPU 内嵌预览实时渲染中",
      stopping: "正在停止",
      completed: "GPU 内嵌预览已完成",
      failed: "GPU 内嵌预览失败",
      stopped: "GPU 内嵌预览已停止",
      blocked: "等待 GPU 任务结束"
    };
    return labels[stateName] || stateName || "等待检查";
  }

  function gpuPreviewStartLabel(status) {
    return status?.previewRunning
      ? "Qt 页面内嵌预览运行中"
      : "在 Qt GPU 测试页启动内嵌预览";
  }

  function gpuElapsedLabel(status) {
    const seconds = Number(status?.elapsedSeconds);
    const hasRun = Boolean(status?.running || status?.startedAt || status?.finishedAt || seconds > 0);
    if (!hasRun) return "--";
    if (Number.isFinite(seconds) && seconds >= 0) return `${seconds.toFixed(seconds >= 10 ? 0 : 1)} 秒`;
    const milliseconds = Number(status?.elapsedMs);
    return Number.isFinite(milliseconds) && milliseconds >= 0 ? `${Math.round(milliseconds / 100) / 10} 秒` : "--";
  }

  function gpuDisplayStrategyLabel(status) {
    const strategy = String(status?.displayStrategy || "").trim();
    const display = String(status?.display || "").trim();
    const labels = {
      inherited: "当前图形会话",
      "vnc-local-xorg": "VNC 时使用本地 Xorg",
      "configured-display": "管理员指定的 Xorg"
    };
    return [display || "未设置 DISPLAY", labels[strategy] || strategy].filter(Boolean).join(" · ");
  }

  function gpuPreviewTargetLabel(status) {
    const target = String(status?.previewTarget || "").trim();
    const display = String(status?.previewDisplay || "").trim();
    const labels = {
      "gpu-test-page": "Qt GPU 测试页内嵌窗口",
      unavailable: "未找到图形会话"
    };
    return [labels[target] || target || "等待检查", display].filter(Boolean).join(" · ");
  }

  function gpuPreviewElapsedLabel(status) {
    const seconds = Number(status?.previewElapsedSeconds);
    const hasRun = Boolean(status?.previewRunning || status?.previewStartedAt || status?.previewFinishedAt || seconds > 0);
    if (!hasRun) return "--";
    if (Number.isFinite(seconds) && seconds >= 0) return `${seconds.toFixed(seconds >= 10 ? 0 : 1)} 秒`;
    const milliseconds = Number(status?.previewElapsedMs);
    return Number.isFinite(milliseconds) && milliseconds >= 0 ? `${Math.round(milliseconds / 100) / 10} 秒` : "--";
  }

  function gpuPreviewNotice(status) {
    if (status?.previewError) return String(status.previewError);
    return "内嵌实时预览只显示在开发板原生 Qt 的 GPU 测试页中；不会创建独立 Weston 窗口，浏览器用于查看状态和离屏得分。";
  }

  function renderGpuStatus(rawStatus) {
    const status = rawStatus || {};
    const binary = String(status.binary || "").trim();
    const output = String(status.log || "").trim();
    const previewOutput = String(status.previewLog || "").trim();
    const score = Number(status.score);
    const previewNotice = $("gpuPreviewNotice");
    setText("gpuRunState", status.error ? `${gpuStateLabel(status)} · 查看输出` : gpuStateLabel(status));
    setText("gpuDisplayStrategy", gpuDisplayStrategyLabel(status));
    setText("gpuRenderer", status.renderer || "--");
    setText("gpuScore", Number.isFinite(score) && score >= 0 ? String(score) : "--");
    setText("gpuElapsed", gpuElapsedLabel(status));
    setText("gpuPreviewState", status.previewError ? `${gpuPreviewStateLabel(status)} · 查看提示` : gpuPreviewStateLabel(status));
    setText("gpuPreviewTarget", gpuPreviewTargetLabel(status));
    setText("gpuPreviewRenderer", status.previewRenderer || "--");
    setText("gpuPreviewElapsed", gpuPreviewElapsedLabel(status));
    setText("gpuPreviewStartLabel", gpuPreviewStartLabel(status));
    if (previewNotice) {
      previewNotice.dataset.target = String(status.previewTarget || "").trim();
      previewNotice.textContent = gpuPreviewNotice(status);
    }
    setText("gpuBinary", binary || "glmark2-es 未发现");
    const scoreOutput = output || (status.error || "尚未产生离屏评分输出。");
    const visibleOutput = previewOutput || (status.previewError || "尚未产生图形预览输出。");
    setText("gpuRuntimeLog", `离屏评分\n${scoreOutput}\n\n图形预览\n${visibleOutput}`);
  }

  function wifiStatusLabel(status) {
    const stateName = String(status?.status || "").trim();
    const labels = {
      idle: "等待读取",
      refreshing: "正在读取",
      scanning: "正在扫描",
      connecting: "正在连接",
      disconnecting: "正在断开",
      ready: "已就绪",
      completed: "操作完成",
      error: "操作失败"
    };
    return labels[stateName] || stateName || "等待读取";
  }

  function wifiRadioLabel(status) {
    if (status?.radioEnabled === true) return "已开启";
    if (status?.radioEnabled === false) return "已关闭";
    return "尚未读取";
  }

  function wifiScanLabel(status, points) {
    if (status?.scanCacheValid) return `${points.length} 个热点 · 有效`;
    if (status?.busy && String(status.status) === "scanning") return "正在扫描";
    if (points.length) return `${points.length} 个热点 · 已过期`;
    return "尚未扫描";
  }

  function wifiDeviceLabel(device) {
    if (!device) return "--";
    const name = String(device.device || "").trim();
    const stateName = String(device.state || "").trim();
    const connection = String(device.connection || "").trim();
    return [name, connection || stateName].filter(Boolean).join(" · ") || "--";
  }

  function renderWifiAccessPoints(rawPoints, status) {
    const container = $("wifiNetworks");
    if (!container) return;
    const points = list(rawPoints);
    const selectedInterface = value("wifiInterface").trim();
    const canUseScan = profileHasCapability("wifi")
      && Boolean(status?.nmcliAvailable)
      && Boolean(status?.scanCacheValid)
      && !status?.busy
      && !state.board.backendBusy
      && !state.pendingActions.size
      && Boolean(selectedInterface);
    const fragment = document.createDocumentFragment();

    if (!points.length) {
      const empty = document.createElement("p");
      empty.className = "empty-state";
      empty.textContent = status?.busy && String(status.status) === "scanning"
        ? "正在扫描附近 Wi-Fi 热点…"
        : (status?.scanCacheValid ? "本次扫描没有可显示的热点。" : "选择无线网卡后点击“扫描热点”。");
      fragment.appendChild(empty);
      container.replaceChildren(fragment);
      return;
    }

    points.forEach((point) => {
      const networkId = String(point?.id || point?.bssid || "").trim();
      const row = document.createElement("div");
      row.className = `wifi-network-row${point?.active ? " active" : ""}${point?.secured ? " secured" : ""}`;

      const identity = document.createElement("div");
      identity.className = "wifi-network-identity";
      const name = document.createElement("strong");
      name.textContent = point?.displayName || point?.ssid || "隐藏网络";
      name.title = point?.ssid || "隐藏网络";
      const metadata = document.createElement("span");
      const security = point?.secured ? (point.security || "已加密") : "开放网络";
      metadata.textContent = [security, point?.channel ? `信道 ${point.channel}` : ""].filter(Boolean).join(" · ");
      identity.append(name, metadata);

      const signal = document.createElement("span");
      signal.className = "wifi-signal";
      const signalPercent = Number(point?.signal);
      signal.dataset.level = Number.isFinite(signalPercent) ? String(Math.max(0, Math.min(4, Math.ceil(signalPercent / 25)))) : "0";
      signal.textContent = Number.isFinite(signalPercent) ? `${signalPercent}%` : "--";
      signal.setAttribute("aria-label", Number.isFinite(signalPercent) ? `信号 ${signalPercent}%` : "信号未知");

      const action = document.createElement("button");
      action.type = "button";
      action.className = `button ${point?.active ? "secondary" : "primary"} wifi-connect-action`;
      action.dataset.action = "wifi-connect";
      action.textContent = point?.active ? "已连接" : (point?.secured ? "输入密码" : "连接");
      const matchingInterface = !point?.interface || String(point.interface) === selectedInterface;
      action.disabled = !canUseScan || !matchingInterface || !networkId || Boolean(point?.hidden) || Boolean(point?.active);
      if (!matchingInterface) action.title = `该扫描结果属于 ${point.interface}，请切换回该网卡或重新扫描。`;
      addActionIcon(action, "wifi-connect");
      action.addEventListener("click", () => void performWifiConnect(point));

      row.append(identity, signal, action);
      fragment.appendChild(row);
    });
    container.replaceChildren(fragment);
  }

  function renderWifiStatus(rawStatus, rawPoints = wifiAccessPointsForBoard()) {
    const status = rawStatus || {};
    const points = list(rawPoints);
    const devices = list(status.devices);
    fillSelect("wifiInterface", devices, wifiDeviceLabel, status.selectedDevice || "");
    setText("wifiManagerState", status.message || wifiStatusLabel(status));
    setText("wifiRadioState", wifiRadioLabel(status));
    const selectedDevice = devices.find((device) => String(device?.device || "") === String(status.selectedDevice || ""));
    setText("wifiSelectedDevice", selectedDevice ? wifiDeviceLabel(selectedDevice) : (status.selectedDevice || "--"));
    setText("wifiScanState", wifiScanLabel(status, points));
    renderNetworkReadout("wifiAddressReadout", wifiIpv4Details(status));
    const warning = $("wifiSessionWarning");
    if (warning) {
      const warningText = String(status.networkSwitchWarning || "").trim();
      warning.hidden = !status.webSessionMayDisconnect && !warningText;
      setText("wifiSessionWarningText", warningText || "切换无线网络后，当前浏览器连接可能断开。请记住新的 IP 地址并重新打开页面。");
    }
    renderWifiAccessPoints(points, status);
  }

  function renderBoard(board) {
    const outputs = list(board.displayOutputs);
    const connectedOutputs = outputs.filter((output) => output && output.connected);
    const environment = board.environmentStatus || board.environmentState || {};
    const keyState = board.keyTestStatus || {};
    const gpuStatus = gpuStatusForBoard(board);
    const npuYoloStatus = npuYoloStatusForBoard(board);
    const cameraPreviewStatus = cameraPreviewStatusForBoard(board);
    const wifiStatus = wifiStatusForBoard(board);
    const wifiPoints = wifiAccessPointsForBoard(board);
    setText("displayCount", `${connectedOutputs.length} 路已连接`);
    setText("displayPrimary", connectedOutputs.length ? connectedOutputs.map((output) => output.connector || output.kind).join("、") : "未连接输出");
    setText("displayDetail", outputs.length ? outputs.map((output) => `${output.connector || "DRM"} · ${output.status || "unknown"}`).join("；") : "仅读取 connector 状态");
    renderEnvironmentStatus(environment);
    renderKeyTestStatus(keyState);
    renderGpuStatus(gpuStatus);
    if (npuYoloDemoEnabled(board)) renderNpuYoloStatus(npuYoloStatus);
    else clearNpuYoloPreview();
    renderCameraPreviewStatus(cameraPreviewStatus);
    renderWifiStatus(wifiStatus, wifiPoints);
    const availableTools = modules.filter((module) => moduleRuntime(module.id, board).available).length;
    setText("heroBoardState", board.backendBusy ? "板端正在执行操作" : "板端已连接");
    const diagnosticState = (entry) => entry && entry.available ? "可读" : (entry && entry.authorizationRequired ? "需授权" : "未发现");
    setText("heroToolSummary", `已发现 ${availableTools} 个可用工具 · RTC ${diagnosticState(board.rtcStatus)} · NPU ${diagnosticState(board.npuStatus)}`);
    $("heroStatusDot").className = `status-dot ${board.backendBusy ? "busy" : "online"}`;
    setText("gpioBackendState", board.backendBusy ? `忙碌：${board.backendBusyOperation || "操作中"}` : (board.statusText || "后端就绪"));
    fillSelect("serialDevice", board.serialDevices);
    fillSelect("canInterface", board.canInterfaces);
    fillSelect("watchdogDevice", list(board.watchdogDevices).length ? board.watchdogDevices : ["/dev/watchdog"]);
    fillSelect("ethernetInterface", board.ethernetInterfaces, (item) => `${item.name || ""}${item.ipv4 ? ` · ${item.ipv4}` : ""}`);
    renderEthernetStatus(board);
    fillSelect("cameraDevice", list(board.cameraCaptureDevices).length ? board.cameraCaptureDevices : board.cameraVideoDevices,
      (item) => item && typeof item === "object" ? item.label || item.path : String(item));
    fillSelect("hdmiDevice", list(board.hdmiInCaptureDevices).length ? board.hdmiInCaptureDevices : board.hdmiInVideoDevices,
      (item) => item && typeof item === "object" ? item.label || item.path : String(item));

    setRangeFromBackend("audioVolume", "audioVolumeLabel", board.audioVolumePercent, true);
    fillSelect("audioPlaybackDevice", board.audioPlaybackDevices,
      (item) => item && typeof item === "object" ? item.label || item.device : String(item),
      preferredAudioDevice(board.audioPlaybackDevices));
    fillSelect("audioCaptureDevice", board.audioCaptureDevices,
      (item) => item && typeof item === "object" ? item.label || item.device : String(item),
      preferredAudioDevice(board.audioCaptureDevices));
    setRangeFromBackend("backlightPercent", "backlightLabel", board.backlightPercent, Boolean(board.backlightAvailable));
    setText("audioStatus", `${Math.round(clampPercent(board.audioVolumePercent))}%`);
    setText("audioBusyState", board.audioOperationBusy ? "操作中" : "空闲");
    updateBuzzerPresentation(board);
    const fanStatus = board.fanStatus || {};
    setText("fanNode", fanStatus.available ? fanStatus.name : "未发现 pwmfan");
    setText("fanMode", fanStatus.mode === "auto" ? "自动温控" : (fanStatus.mode === "manual" ? "手动" : "不可用"));
    setText("fanCurrent", fanStatus.available ? `${Math.round(clampPercent(fanStatus.percent))}%` : "--");
    setText("fanAvailability", fanStatus.message || "未发现 pwmfan 风扇节点");
    setRangeFromBackend("fanPercent", "fanPercentLabel", fanStatus.percent, Boolean(fanStatus.available));
    setText("beepBusyState", board.audioOperationBusy ? "操作中" : "空闲");
    fillSelect("ledDevice", board.ledDevices, (item) => `${item.name || "LED"}${item.percent !== undefined ? ` · ${item.percent}%` : ""}`);
    setText("ledStatus", board.ledAvailable ? `${list(board.ledDevices).length} 个设备` : "未发现设备");
    const selectedLed = $("ledDevice") && $("ledDevice").options[$("ledDevice").selectedIndex];
    setText("ledCurrent", selectedLed && selectedLed.dataset.percent ? `${selectedLed.dataset.percent}%` : "--");
    syncLedPercentFromSelection(false);
    setText("backlightStatus", board.backlightAvailable ? "设备可用" : "未发现设备");
    setText("backlightCurrent", board.backlightAvailable ? `${Math.round(clampPercent(board.backlightPercent))}%` : "--");
    setText("uartStatus", countText(board.serialDevices, "个 tty"));
    setText("canStatus", countText(board.canInterfaces, "个接口"));
    const cellularModem = cellularModemStatusForBoard(board);
    const preferredCellularInterface = preferredCellularInterfaceForBoard(board);
    setText("cellularModuleStatus", cellularModuleLabel(board));
    setText("cellularDialerStatus", cellularModem.dialer
      ? `${cellularModem.dialer}${cellularModem.dialerAvailable ? "" : " · 不可用"}`
      : "未选择");
    const cellularDhcpRequired = Boolean(cellularModem.dhcpClientRequired);
    const cellularDhcpReady = !cellularDhcpRequired || Boolean(cellularModem.dhcpClientAvailable);
    setText("cellularDhcpStatus", !cellularDhcpRequired
      ? "模组内置 DHCP"
      : (cellularDhcpReady ? "dhclient 已就绪" : "缺少 dhclient"));
    setText("cellularInterfaceStatus", preferredCellularInterface || "等待拨号程序创建接口");
    const cellularInterfaceInput = $("cellularInterface");
    if (cellularInterfaceInput && !cellularInterfaceInput.value.trim() && preferredCellularInterface) {
      cellularInterfaceInput.value = preferredCellularInterface;
    }
    setText("cameraStatus", countText(list(board.cameraCaptureDevices).length ? board.cameraCaptureDevices : board.cameraVideoDevices, "个节点"));
    const hdmiIn = board.hdmiInStatus || {};
    setText("hdmiStatus", countText(list(board.hdmiInCaptureDevices).length ? board.hdmiInCaptureDevices : board.hdmiInVideoDevices, "个节点"));
    setText("hdmiSignal", hdmiIn.signalPresent ? "已检测到 HDMI 源信号" : (hdmiIn.available ? "等待 HDMI 源信号" : (hdmiIn.authorizationRequired ? "需要授权" : "未发现 HDMI 输入节点")));
    setText("watchdogStatusText", countText(board.watchdogDevices, "个设备"));
    const storageDevices = list(board.storageDevices);
    setText("storageStatus", `${storageDevices.length} 个分区`);
    setText("storageDeviceCount", `${storageDevices.length} 个`);
    renderStorageDevices(storageDevices);
    setText("currentPath", board.currentPath || "/");
    setText("fileCount", `${list(board.fileEntries).length} 项`);
    renderFiles(board.fileEntries);
    updateFileUploadUi();
    updateModuleSummary(board);
    renderLastOperation(board.lastOperation);
    updateToolAvailability(state.currentTool);
    updateControlAvailability();
  }

  function syncLedPercentFromSelection(force) {
    const select = $("ledDevice");
    const range = $("ledPercent");
    if (!select || !range) return;
    const selected = select.options[select.selectedIndex];
    const percent = Number(selected?.dataset.percent);
    if (!Number.isFinite(percent)) return;
    if (force || document.activeElement !== range) range.value = String(Math.round(clampPercent(percent)));
    setText("ledPercentLabel", `${range.value}%`);
  }

  function renderLastOperation(operation) {
    if (!operation || !operation.action || !operation.time || operation.time === state.lastOperationTime) return;
    // The backend retains its last result across sessions.  Treat the first
    // read after login as a baseline so an old action never obscures the page.
    if (!state.lastOperationTime) {
      state.lastOperationTime = operation.time;
      return;
    }
    state.lastOperationTime = operation.time;
    const tool = operationTool(operation.action);
    const detail = [operation.status, operation.result].filter(Boolean).join("\n");
    const summary = operation.success ? `${operation.action} 已完成` : `${operation.action} 执行失败`;
    if (operation.action === "ping-interface" && state.currentTool === "ethernet") {
      const iface = value("ethernetInterface").trim() || "当前接口";
      const target = ethernetPingTarget();
      const status = String(operation.status || "").trim();
      setEthernetPingFeedback({
        state: operation.success ? "success" : "error",
        message: operation.success
          ? `Ping 成功：${iface} 可访问 ${target}`
          : `Ping 未通过：${iface} 无法访问 ${target}`,
        detail: status || (operation.success ? "板端已完成连通性测试。" : "请查看操作记录中的后端反馈。")
      });
      renderEthernetStatus(state.board);
    }
    writeOperationLog(tool, summary, detail || "后端未返回额外信息");
    toast(`${moduleById.get(tool)?.title || "硬件操作"}：${operation.success ? "已完成" : "失败"}`, operation.success ? "info" : "error");
  }

  function moduleRuntime(moduleId, board = state.board) {
    const camera = list(board.cameraCaptureDevices).length ? board.cameraCaptureDevices : board.cameraVideoDevices;
    const hdmi = list(board.hdmiInCaptureDevices).length ? board.hdmiInCaptureDevices : board.hdmiInVideoDevices;
    const playbackDevices = list(board.audioPlaybackDevices);
    const captureDevices = list(board.audioCaptureDevices);
    const audioDeviceCount = playbackDevices.length + captureDevices.length;
    const audioAvailable = audioDeviceCount > 0;
    const environment = board.environmentStatus || board.environmentState || {};
    const keyState = board.keyTestStatus || {};
    const eligibleKeyDevices = list(keyState.devices).filter((device) => device && device.eligible !== false);
    const gpuStatus = gpuStatusForBoard(board);
    const npuYoloStatus = npuYoloStatusForBoard(board);
    // The fixed YOLO status is loaded only after its dedicated page is
    // opened. A validated product profile is therefore not a missing NPU
    // device simply because the initial /api/board snapshot has no detail.
    const npuYoloStatusKnown = Object.prototype.hasOwnProperty.call(npuYoloStatus, "available");
    const wifiStatus = wifiStatusForBoard(board);
    const wifiDevices = list(wifiStatus.devices);
    const wifiNeedsRead = wifiStatusNeedsRead(wifiStatus);
    const cellularModem = cellularModemStatusForBoard(board);
    const cellularUsbModemDetected = Boolean(cellularModem.detected);
    const cellularDevices = list(board.cellularDevices);
    // A supported USB modem is actionable even before its Quectel process
    // creates usb0/wwan0. Keep the tool visible for identity/status review
    // and let the backend decide whether the matched binary is runnable.
    const cellularAvailable = cellularUsbModemDetected || cellularDevices.length > 0;
    const cellularCount = cellularUsbModemDetected
      ? String(cellularModem.displayName || cellularModem.module || "已识别模组")
      : countText(cellularDevices, "节点");
    const cellularState = cellularUsbModemDetected && cellularModem.dialerAvailable === false
      ? "匹配拨号程序不可用"
      : "";
    const enabled = (capability) => profileHasCapability(capability, board);
    const npuYoloProfileEnabled = npuYoloDemoEnabled(board) && npuYoloStatus?.profileSupported !== false;
    const buzzerDeclared = enabled("buzzer");
    // Newer BoardLab backends expose a probe result after checking the PWM
    // channel and its writable control files. Keep a sensible compatibility
    // fallback for an older server during a rolling update.
    const buzzerReady = typeof board.buzzerAvailable === "boolean" ? board.buzzerAvailable : buzzerDeclared;
    const runtime = {
      gpio: { count: "I/O", available: enabled("gpio"), profileEnabled: enabled("gpio") },
      uart: { count: countText(board.serialDevices, "tty"), available: enabled("uart") && list(board.serialDevices).length > 0, profileEnabled: enabled("uart") },
      ethernet: { count: countText(board.ethernetInterfaces, "网口"), available: enabled("ethernet") && list(board.ethernetInterfaces).length > 0, profileEnabled: enabled("ethernet") },
      can: { count: countText(board.canInterfaces, "接口"), available: enabled("can") && list(board.canInterfaces).length > 0, profileEnabled: enabled("can") },
      cellular: {
        count: cellularCount,
        available: enabled("cellular") && cellularAvailable,
        profileEnabled: enabled("cellular"),
        state: cellularState
      },
      camera: { count: countText(camera, "节点"), available: enabled("camera") && list(camera).length > 0, profileEnabled: enabled("camera") },
      hdmiin: { count: countText(hdmi, "节点"), available: enabled("hdmiin") && list(hdmi).length > 0, profileEnabled: enabled("hdmiin") },
      watchdog: { count: countText(board.watchdogDevices, "设备"), available: enabled("watchdog") && list(board.watchdogDevices).length > 0, profileEnabled: enabled("watchdog") },
      gpu: {
        count: gpuStatus.running ? "运行中" : (gpuStatus.available ? "glmark2" : "未发现"),
        available: enabled("gpu") && Boolean(gpuStatus.available),
        profileEnabled: enabled("gpu")
      },
      "npu-yolo": {
        count: npuYoloStatus.running ? "推理中" : (npuYoloStatus.available ? "已就绪" : (npuYoloStatus.profileSupported === false ? "未启用" : "待检查")),
        available: npuYoloProfileEnabled && Boolean(npuYoloStatus.available),
        profileEnabled: npuYoloProfileEnabled,
        state: npuYoloProfileEnabled && !npuYoloStatusKnown ? "进入后检查" : ""
      },
      wifi: {
        count: wifiStatus.busy ? "操作中" : (wifiNeedsRead ? "状态未读取" : `${wifiDevices.length} 个网卡`),
        available: enabled("wifi") && Boolean(wifiStatus.nmcliAvailable)
          && (wifiDevices.length > 0 || wifiNeedsRead),
        profileEnabled: enabled("wifi"),
        state: wifiNeedsRead ? "进入后读取" : ""
      },
      music: { count: audioAvailable ? `ALSA ${audioDeviceCount}` : "未发现", available: enabled("music") && audioAvailable, profileEnabled: enabled("music") },
      buzzer: { count: buzzerReady ? buzzerMethodLabel(board) : (buzzerDeclared ? "待初始化" : "未声明"), available: enabled("buzzer") && buzzerReady, profileEnabled: enabled("buzzer") },
      fan: {
        count: board.fanStatus?.available ? `${Math.round(clampPercent(board.fanStatus.percent))}%` : "未发现",
        available: Boolean(board.fanStatus?.available),
        profileEnabled: enabled("fan"),
        state: board.fanStatus?.message || ""
      },
      led: { count: board.ledAvailable ? `${list(board.ledDevices).length} 路` : "未发现", available: enabled("led") && Boolean(board.ledAvailable), profileEnabled: enabled("led") },
      environment: { count: environment.available ? `${list(environment.sensors).length || 1} 个传感器` : "未发现", available: enabled("temperaturehumidity") && Boolean(environment.available), profileEnabled: enabled("temperaturehumidity") },
      keys: { count: `${eligibleKeyDevices.length} 组`, available: enabled("keys") && eligibleKeyDevices.length > 0, profileEnabled: enabled("keys") },
      backlight: { count: board.backlightAvailable ? `${Math.round(clampPercent(board.backlightPercent))}%` : "未发现", available: enabled("backlight") && Boolean(board.backlightAvailable), profileEnabled: enabled("backlight") },
      storage: { count: `${list(board.storageDevices).length} 分区`, available: enabled("storage"), profileEnabled: enabled("storage") },
      files: { count: "浏览", available: enabled("files"), profileEnabled: enabled("files") }
    };
    return runtime[moduleId] || { count: "--", available: false, profileEnabled: false };
  }

  function updateModuleSummary(board) {
    modules.filter((module) => moduleIsVisible(module, board)).forEach((module) => {
      const runtime = moduleRuntime(module.id, board);
      setText(`module-count-${module.id}`, runtime.count);
      setText(`module-state-${module.id}`, !runtime.profileEnabled
        ? "当前板型未支持"
        : (runtime.state || (runtime.available ? "可以进入" : "需要连接设备")));
      const dot = $(`module-dot-${module.id}`);
      if (dot) dot.className = `status-dot ${runtime.available ? "available" : "unavailable"}`;
    });
    const environmentCount = list((board.environmentStatus || board.environmentState || {}).sensors).length;
    const keyCount = list(board.keyTestStatus?.devices).filter((device) => device && device.eligible !== false).length;
    const gpuStatus = gpuStatusForBoard(board);
    const wifiStatus = wifiStatusForBoard(board);
    const wifiCount = list(wifiStatus.devices).length;
    const wifiInventory = wifiStatusNeedsRead(wifiStatus) ? "待读取" : wifiCount;
    setText("inventory", `ETH ${list(board.ethernetInterfaces).length} · Wi-Fi ${wifiInventory} · GPU ${gpuStatus.available ? "就绪" : "--"} · ENV ${environmentCount} · KEY ${keyCount}`);
  }

  function updateToolAvailability(toolId) {
    if (!toolId || !moduleById.has(toolId)) return;
    const runtime = moduleRuntime(toolId);
    const busy = Boolean(state.board.backendBusy);
    const statusFailure = state.toolStatusFailures[toolId];
    $("toolStatusDot").className = `status-dot ${busy ? "busy" : (statusFailure ? "unavailable" : (runtime.available ? "available" : "unavailable"))}`;
    setText("toolAvailability", busy
      ? `后端正在执行：${state.board.backendBusyOperation || "硬件操作"}`
      : (statusFailure?.summary || (!runtime.profileEnabled ? "当前板型未启用此测试，操作已锁定" : (runtime.available ? `${runtime.count} · 可开始操作` : "未发现对应设备，可查看配置但暂不建议执行"))));
  }

  function renderFiles(items) {
    const container = $("files");
    const rows = list(items);
    if (!rows.length) {
      container.innerHTML = '<p class="empty-state">当前目录为空或暂不可读</p>';
      return;
    }

    const fragment = document.createDocumentFragment();
    rows.forEach((file) => {
      const row = document.createElement("div");
      row.className = "file-row";
      const icon = document.createElement("span");
      icon.className = "file-icon";
      icon.innerHTML = moduleIcon({ icon: file.directory ? "folder-open" : "file" });
      const name = document.createElement("span");
      name.className = "file-name";
      name.textContent = file.name || file.path || "未命名";
      name.title = file.path || file.name || "";
      row.append(icon, name);
      if (file.directory) {
        const button = document.createElement("button");
        button.className = "button secondary control-action file-action";
        button.type = "button";
        button.dataset.action = "files-open";
        button.textContent = "打开";
        addActionIcon(button, button.dataset.action);
        button.addEventListener("click", () => sendControl("files-open", { path: file.path }));
        row.appendChild(button);
      }
      fragment.appendChild(row);
    });
    container.replaceChildren(fragment);
  }

  function formatFileSize(bytes) {
    const size = Number(bytes);
    if (!Number.isFinite(size) || size < 0) return "大小未知";
    if (size < 1024) return `${size} B`;
    if (size < 1024 * 1024) return `${(size / 1024).toFixed(size < 10 * 1024 ? 1 : 0)} KiB`;
    if (size < 1024 * 1024 * 1024) return `${(size / (1024 * 1024)).toFixed(size < 10 * 1024 * 1024 ? 1 : 0)} MiB`;
    return `${(size / (1024 * 1024 * 1024)).toFixed(1)} GiB`;
  }

  function fileUploadTargetPath() {
    return String(state.board?.currentPath || "/");
  }

  function ensureFileUploadUi() {
    const filePane = $("files")?.closest(".file-browser-pane");
    if (!filePane || $("fileUpload")) return;

    const upload = document.createElement("section");
    upload.className = "file-upload";
    upload.id = "fileUpload";
    upload.dataset.state = "idle";
    upload.innerHTML = `
      <label class="file-upload-dropzone" for="fileUploadInput">
        <input class="file-upload-input" id="fileUploadInput" name="fileUpload" type="file">
        <span class="file-upload-copy">
          <span class="file-upload-kicker">上传到当前路径</span>
          <strong class="file-upload-name" id="fileUploadName">拖放文件到这里，或选择文件</strong>
          <small class="file-upload-meta" id="fileUploadMeta">目标目录：/</small>
        </span>
      </label>
      <div class="file-upload-actions">
        <button class="button secondary file-upload-select" id="fileUploadSelect" type="button">选择文件</button>
        <button class="button primary file-upload-submit" id="fileUploadSubmit" type="button" disabled>上传</button>
      </div>
      <div class="file-upload-progress" id="fileUploadProgress" hidden aria-live="polite">
        <progress id="fileUploadProgressValue" max="100" value="0"></progress>
        <span id="fileUploadProgressText">0%</span>
      </div>`;

    const files = $("files");
    filePane.insertBefore(upload, files);

    const input = $("fileUploadInput");
    $("fileUploadSelect").addEventListener("click", () => input.click());
    $("fileUploadSubmit").addEventListener("click", () => void uploadSelectedFile());
    input.addEventListener("change", () => {
      const selected = input.files?.[0] || null;
      // Clear the native control after capturing the File so choosing the
      // same file again after a failed upload still emits a change event.
      input.value = "";
      selectUploadFile(selected);
    });
    ["dragenter", "dragover"].forEach((eventName) => {
      upload.addEventListener(eventName, (event) => {
        event.preventDefault();
        if (!state.fileUpload.inFlight) upload.classList.add("is-dragging");
      });
    });
    ["dragleave", "dragend"].forEach((eventName) => {
      upload.addEventListener(eventName, () => upload.classList.remove("is-dragging"));
    });
    upload.addEventListener("drop", (event) => {
      event.preventDefault();
      upload.classList.remove("is-dragging");
      if (state.fileUpload.inFlight) return;
      selectUploadFile(event.dataTransfer?.files?.[0] || null);
    });
    updateFileUploadUi();
  }

  function selectUploadFile(file) {
    if (!file || state.fileUpload.inFlight) return;
    if (file.size > FILE_UPLOAD_MAX_BYTES) {
      state.fileUpload.file = null;
      state.fileUpload.progress = 0;
      state.fileUpload.feedback = `单个文件不能超过 ${formatFileSize(FILE_UPLOAD_MAX_BYTES)}`;
      state.fileUpload.feedbackMode = "error";
      updateFileUploadUi();
      toast(state.fileUpload.feedback, "error");
      return;
    }
    state.fileUpload.file = file;
    state.fileUpload.progress = 0;
    state.fileUpload.feedback = "";
    state.fileUpload.feedbackMode = "ready";
    updateFileUploadUi();
  }

  function updateFileUploadUi() {
    const upload = $("fileUpload");
    if (!upload) return;
    const selectedFile = state.fileUpload.file;
    const inFlight = state.fileUpload.inFlight;
    const progress = Math.max(0, Math.min(100, Math.round(Number(state.fileUpload.progress) || 0)));
    const message = state.fileUpload.feedback;
    const mode = state.fileUpload.feedbackMode || "idle";
    const fileName = $("fileUploadName");
    const fileMeta = $("fileUploadMeta");
    const select = $("fileUploadSelect");
    const submit = $("fileUploadSubmit");
    const progressWrap = $("fileUploadProgress");
    const progressValue = $("fileUploadProgressValue");
    const progressText = $("fileUploadProgressText");

    upload.dataset.state = inFlight ? "uploading" : mode;
    upload.classList.toggle("is-uploading", inFlight);
    fileName.textContent = selectedFile
      ? selectedFile.name
      : (mode === "success" ? "上传完成" : (mode === "error" ? "上传失败" : "拖放文件到这里，或选择文件"));
    if (selectedFile) {
      fileMeta.textContent = `${formatFileSize(selectedFile.size)} · 将上传到 ${fileUploadTargetPath()}`;
    } else if (message) {
      fileMeta.textContent = message;
    } else {
      fileMeta.textContent = `目标目录：${fileUploadTargetPath()}`;
    }
    select.disabled = inFlight;
    submit.disabled = !selectedFile || inFlight || !state.authenticated;
    submit.textContent = inFlight ? "上传中" : "上传";
    progressWrap.hidden = !inFlight;
    progressValue.value = progress;
    progressText.textContent = `${progress}%`;
  }

  function fileUploadErrorMessage(xhr) {
    let body = {};
    try {
      body = JSON.parse(xhr.responseText || "{}");
    } catch (_) {
      body = {};
    }
    return body.message || body.error || `上传失败（HTTP ${xhr.status || "网络错误"}）`;
  }

  function sendFileUpload(file) {
    return new Promise((resolve, reject) => {
      const request = new XMLHttpRequest();
      const query = new URLSearchParams({
        filename: file.name,
        directory: fileUploadTargetPath()
      });
      request.open("POST", `/api/files/upload?${query.toString()}`, true);
      request.setRequestHeader("Authorization", `Bearer ${state.accessToken}`);
      request.setRequestHeader("Content-Type", "application/octet-stream");
      request.responseType = "text";
      request.upload.addEventListener("progress", (event) => {
        if (!event.lengthComputable) return;
        state.fileUpload.progress = (event.loaded / event.total) * 100;
        updateFileUploadUi();
      });
      request.addEventListener("load", () => {
        if (request.status >= 200 && request.status < 300) {
          let body = {};
          try {
            body = JSON.parse(request.responseText || "{}");
          } catch (_) {
            body = {};
          }
          resolve(body);
          return;
        }
        const error = new Error(fileUploadErrorMessage(request));
        error.status = request.status;
        if (request.status === 401) error.authentication = true;
        reject(error);
      });
      request.addEventListener("error", () => {
        const error = new Error("上传连接中断，请检查浏览器与开发板的网络。");
        error.network = true;
        reject(error);
      });
      request.addEventListener("abort", () => reject(new Error("上传已取消")));

      request.send(file);
    });
  }

  async function uploadSelectedFile() {
    const file = state.fileUpload.file;
    if (!file || state.fileUpload.inFlight) return;
    if (!state.accessToken) {
      showAuthentication("登录已过期，请重新输入 Token。");
      return;
    }

    state.fileUpload.inFlight = true;
    state.fileUpload.progress = 0;
    state.fileUpload.feedback = "正在上传…";
    state.fileUpload.feedbackMode = "uploading";
    updateFileUploadUi();
    writeOperationLog("files", `正在上传 ${file.name}`, `${formatFileSize(file.size)} → ${fileUploadTargetPath()}`);

    try {
      const response = await sendFileUpload(file);
      state.fileUpload.file = null;
      state.fileUpload.progress = 100;
      state.fileUpload.feedbackMode = "success";
      state.fileUpload.feedback = response.message || `${file.name} 已上传到 ${fileUploadTargetPath()}`;
      writeOperationLog("files", `上传成功：${file.name}`, state.fileUpload.feedback);
      toast(`文件已上传：${file.name}`);
      await refreshAll();
    } catch (error) {
      if (error.authentication) {
        showAuthentication("Token 无效或服务端 Token 已变更，请重新输入。");
        return;
      }
      state.fileUpload.feedbackMode = "error";
      state.fileUpload.feedback = error.message || "上传失败，请重试。";
      writeOperationLog("files", `上传失败：${file.name}`, state.fileUpload.feedback);
      toast(state.fileUpload.feedback, "error");
    } finally {
      state.fileUpload.inFlight = false;
      updateFileUploadUi();
    }
  }

  function storageTypeLabel(type) {
    const labels = {
      USB: "U 盘",
      SD: "TF 卡",
      NVME: "NVMe",
      SATA: "SATA",
      EMMC: "eMMC"
    };
    return labels[String(type || "").toUpperCase()] || "存储";
  }

  function storageState(device) {
    if (device.protected) return { text: "系统保护", kind: "protected" };
    if (device.mounted) return { text: "已挂载", kind: "mounted" };
    if (device.canMount) return { text: "可挂载", kind: "available" };
    return { text: "需要检查", kind: "unknown" };
  }

  function storageTextElement(tagName, className, text) {
    const element = document.createElement(tagName);
    element.className = className;
    element.textContent = text;
    return element;
  }

  function storageDetail(label, content) {
    const detail = document.createElement("span");
    detail.className = "storage-detail";
    detail.append(storageTextElement("small", "storage-detail-label", label));
    detail.append(storageTextElement("strong", "storage-detail-value", content || "--"));
    return detail;
  }

  function storageButton(action, label, device, options = {}) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = `button ${options.variant || "secondary"} control-action storage-action`;
    button.dataset.action = action;
    button.dataset.storageAllowed = options.allowed ? "true" : "false";
    button.textContent = label;
    addActionIcon(button, action);
    button.addEventListener("click", () => performStorageAction(action, device));
    return button;
  }

  function renderStorageDevices(devices) {
    const container = $("storageDevices");
    if (!container) return;
    const rows = list(devices);
    const fragment = document.createDocumentFragment();

    if (!rows.length) {
      const empty = document.createElement("div");
      empty.className = "storage-empty-state";
      empty.append(storageTextElement("strong", "", "尚未发现可识别分区"));
      empty.append(storageTextElement("span", "", "插入 U 盘、TF 卡或 NVMe 后点击“重新扫描”。系统盘不会作为可写测试目标。"));
      fragment.appendChild(empty);
      container.replaceChildren(fragment);
      return;
    }

    rows.forEach((device) => {
      const row = document.createElement("article");
      const currentState = storageState(device);
      row.className = `storage-device-row state-${currentState.kind}`;

      const identity = document.createElement("div");
      identity.className = "storage-device-identity";
      identity.append(storageTextElement("span", "storage-type-mark", storageTypeLabel(device.type)));
      const titleBlock = document.createElement("div");
      titleBlock.className = "storage-device-title";
      titleBlock.append(storageTextElement("strong", "", device.device || "未知设备"));
      titleBlock.append(storageTextElement("span", "", device.label || device.model || storageTypeLabel(device.type)));
      identity.append(titleBlock);

      const details = document.createElement("div");
      details.className = "storage-device-details";
      details.append(storageDetail("容量", device.size));
      details.append(storageDetail("文件系统", device.filesystem));
      details.append(storageDetail("来源", device.parentDevice || device.transport || "--"));

      const stateBlock = document.createElement("div");
      stateBlock.className = "storage-device-state";
      stateBlock.append(storageTextElement("span", `storage-state-pill ${currentState.kind}`, currentState.text));
      let description = "未识别可挂载的文件系统，请检查格式和连接。";
      if (device.protected) description = "系统分区、启动分区和 Swap 仅可查看，已禁止挂载、卸载和测速。";
      else if (device.mounted) {
        description = device.managedMount
          ? `挂载位置：${device.actualMountPoint || device.mountPoint || "--"}`
          : `系统已挂载到：${device.actualMountPoint || device.mountPoint || "--"}；为避免影响其他程序，平台不会卸载或测速。`;
      }
      else if (device.canMount) description = `可安全挂载到：${device.mountPointHint || device.mountPoint || "--"}`;
      stateBlock.append(storageTextElement("span", "storage-device-description", description));
      if (device.readSpeed || device.writeSpeed) {
        const speeds = `读 ${device.readSpeed || "--"} · 写 ${device.writeSpeed || "--"}`;
        stateBlock.append(storageTextElement("strong", "storage-speed-result", speeds));
      }

      const actions = document.createElement("div");
      actions.className = "storage-device-actions";
      const protectedDevice = Boolean(device.protected);
      const mounted = Boolean(device.mounted);
      const canMount = Boolean(device.canMount);
      const canUnmount = Boolean(device.canUnmount);
      const canBenchmark = Boolean(device.canBenchmark);
      if (!protectedDevice && !mounted && canMount) {
        actions.append(storageButton("storage-mount", "挂载", device, { allowed: true, variant: "primary" }));
      }
      if (!protectedDevice && mounted && canUnmount) {
        actions.append(storageButton("storage-unmount", "安全卸载", device, { allowed: true, variant: "stop" }));
      }
      if (!protectedDevice && mounted && canBenchmark) {
        actions.append(storageButton("storage-speed", "100 MiB 测速", device, { allowed: true, variant: "secondary" }));
      }
      if (!actions.childElementCount) {
        const actionNote = protectedDevice
          ? "受系统保护"
          : (mounted && !device.managedMount ? "非平台管理的挂载" : "当前不可操作");
        actions.append(storageTextElement("span", "storage-action-note", actionNote));
      }

      row.append(identity, details, stateBlock, actions);
      fragment.appendChild(row);
    });
    container.replaceChildren(fragment);
  }

  function ensureStorageDiscovery() {
    if (state.currentTool !== "storage" || state.storageScanRequested || state.board.backendBusy
        || state.pendingActions.has("storage-scan") || !profileHasCapability("storage")) return;
    state.storageScanRequested = true;
    void sendControl("storage-scan", {});
  }

  async function performStorageAction(action, device) {
    const mountPoint = String(device.actualMountPoint || device.mountPoint || "").trim();
    if (action === "storage-mount") {
      await sendControl(action, {
        device: String(device.device || ""),
        mountPoint: String(device.mountPointHint || device.mountPoint || "")
      });
      return;
    }
    if (action === "storage-unmount") {
      await sendControl(action, { mountPoint });
      return;
    }
    if (action === "storage-speed") {
      if (!$("storageWriteConfirm").checked) {
        toast("请先确认临时测速文件写入。", "error");
        $("storageWriteConfirm").focus();
        return;
      }
      const accepted = await requestConfirmation(
        "确认在外部存储上测速？",
        `${device.device || "所选分区"} 会在 ${mountPoint || "挂载目录"} 写入并删除一个 100 MiB 临时文件。系统分区不可测试。`
      );
      if (!accepted) return;
      await sendControl(action, { mountPoint, confirmWrite: true });
    }
  }

  function operationTool(action) {
    if (action.startsWith("gpio-")) return "gpio";
    if (action.startsWith("uart-")) return "uart";
    if (action.startsWith("ethernet-") || action === "ping-interface") return state.currentTool === "cellular" ? "cellular" : "ethernet";
    if (action.startsWith("can-")) return "can";
    if (action.startsWith("cellular-")) return "cellular";
    if (action.startsWith("camera-")) return "camera";
    if (action.startsWith("hdmiin-")) return "hdmiin";
    if (action.startsWith("watchdog-")) return "watchdog";
    if (action.startsWith("gpu-")) return "gpu";
    if (action.startsWith("npu-yolo-")) return "npu-yolo";
    if (action.startsWith("wifi-")) return "wifi";
    if (action.startsWith("audio-beep-")) return "buzzer";
    if (action.startsWith("fan-")) return "fan";
    if (action.startsWith("audio-")) return "music";
    if (action.startsWith("led-")) return "led";
    if (action.startsWith("environment-")) return "environment";
    if (action.startsWith("keys-")) return "keys";
    if (action.startsWith("backlight-")) return "backlight";
    if (action.startsWith("storage-")) return "storage";
    if (action.startsWith("files-")) return "files";
    return state.currentTool;
  }

  function writeOperationLog(toolId, message, detail = "") {
    const view = document.querySelector(`.tool-view[data-tool="${toolId}"]`);
    const log = view ? view.querySelector(".operation-log") : null;
    if (!log) return;
    const timestamp = new Date().toLocaleTimeString("zh-CN", { hour12: false });
    const block = `[${timestamp}] ${message}${detail ? `\n${detail}` : ""}`;
    const existing = log.dataset.started === "true" ? log.textContent : "";
    log.dataset.started = "true";
    log.textContent = `${block}${existing ? `\n\n${existing}` : ""}`.slice(0, 24000);
  }

  function toast(message, mode = "info") {
    // Successful hardware work is already written to the current tool's
    // evidence panel. Reserve transient messages for failures that could
    // otherwise leave an operator without an actionable explanation.
    if (mode !== "error") return;
    $("toastStack").replaceChildren();
    const element = document.createElement("div");
    element.className = `toast${mode === "error" ? " error" : ""}`;
    element.textContent = message;
    $("toastStack").appendChild(element);
    window.setTimeout(() => element.remove(), 4200);
  }

  function paramsForAction(action) {
    if (action === "gpio-apply" || action === "gpio-read") {
      return { pin: value("gpioPin").trim(), mode: value("gpioMode"), high: $("gpioHigh").checked };
    }
    if (action === "uart-tx" || action === "uart-rx") {
      return {
        device: value("serialDevice"),
        baudRate: numberValue("uartBaud", 115200),
        dataBits: numberValue("uartDataBits", 8),
        parity: value("uartParity"),
        stopBits: numberValue("uartStopBits", 1),
        flowControl: value("uartFlow")
      };
    }
    if (action.startsWith("ethernet-") || action === "ping-interface") {
      return {
        interface: value("ethernetInterface"),
        address: value("ethIp").trim(),
        netmask: value("ethMask").trim(),
        gateway: value("ethGateway").trim(),
        dns1: value("ethDns1").trim(),
        target: value("pingTarget").trim() || "www.baidu.com"
      };
    }
    if (action.startsWith("can-")) {
      return {
        interface: value("canInterface"),
        bitrate: numberValue("canBitrate", 500000),
        dataBitrate: numberValue("canDataBitrate", 2000000),
        fdMode: false,
        confirmCan: $("canAuthorize").checked
      };
    }
    if (action === "cellular-start") {
      return { apn: value("cellularApn").trim(), interface: value("cellularInterface").trim() };
    }
    if (action === "cellular-stop") {
      return { interface: value("cellularInterface").trim() };
    }
    if (action === "cellular-status") return {};
    if (action === "cellular-ping") {
      return { interface: value("cellularInterface").trim() || preferredCellularInterfaceForBoard() };
    }
    if (action === "camera-preview-start") {
      return { device: value("cameraDevice") };
    }
    if (action === "camera-preview-stop") {
      return {};
    }
    if (action.startsWith("camera-")) {
      const select = $("cameraDevice");
      const selected = select.options[select.selectedIndex];
      const params = { device: select.value };
      if (selected && selected.dataset.mode) params.mode = selected.dataset.mode;
      return params;
    }
    if (action.startsWith("hdmiin-")) {
      return { device: value("hdmiDevice"), mode: "hdmiin" };
    }
    if (action.startsWith("watchdog-")) {
      return {
        device: value("watchdogDevice") || "/dev/watchdog",
        timeout: numberValue("watchdogTimeout", 5),
        confirm: $("watchdogConfirm").checked
      };
    }
    if (action === "wifi-scan") return { interface: value("wifiInterface").trim() };
    if (action === "wifi-disconnect") return { interface: value("wifiInterface").trim() };
    if (action === "wifi-connect") return { interface: value("wifiInterface").trim(), networkId: "" };
    if (action === "audio-volume") {
      return {
        percent: numberValue("audioVolume", 50),
        playbackDevice: value("audioPlaybackDevice")
      };
    }
    if (action === "audio-play-test" || action === "audio-play-recorded") {
      return { playbackDevice: value("audioPlaybackDevice") };
    }
    if (action === "audio-record") return { captureDevice: value("audioCaptureDevice") };
    if (action === "audio-beep-apply") return { percent: numberValue("beepPercent", 50) };
    if (action === "fan-manual") return { percent: numberValue("fanPercent", 50) };
    if (action === "led-set") return { name: value("ledDevice"), percent: numberValue("ledPercent", 100) };
    if (action === "keys-start") return { deviceId: value("keyTestDevice") };
    if (action === "backlight-set") return { percent: numberValue("backlightPercent", 50) };
    return {};
  }

  function normalizedAction(action, params) {
    if (action === "cellular-ping") {
      return {
        action: "ping-interface",
        params: { interface: params.interface || preferredCellularInterfaceForBoard(), target: "www.baidu.com" }
      };
    }
    return { action, params };
  }

  function operationLogParams(action, params) {
    if (action !== "wifi-connect") return JSON.stringify(params, null, 2);
    const safeParams = { ...params };
    if (Object.prototype.hasOwnProperty.call(safeParams, "password")) {
      safeParams.password = safeParams.password ? "[已隐藏]" : "[未填写]";
    }
    return JSON.stringify(safeParams, null, 2);
  }

  function redactWifiPassword(text, password) {
    const detail = String(text || "");
    return password ? detail.split(password).join("[已隐藏]") : detail;
  }

  async function sendControl(requestedAction, suppliedParams) {
    const tool = operationTool(requestedAction);
    const module = moduleById.get(tool);
    if (module && !moduleIsVisible(module)) {
      // Buttons and routes are hidden for unsupported profiles. Retain a
      // front-end guard for scripted clicks or stale DOM references while the
      // backend continues to enforce the same capability independently.
      if (tool === "npu-yolo") clearNpuYoloPreview();
      toast(`当前板型未启用${module.title}测试。`, "error");
      return;
    }
    const initialParams = suppliedParams || paramsForAction(requestedAction);
    const request = normalizedAction(requestedAction, initialParams);
    const wifiPassword = requestedAction === "wifi-connect" ? String(request.params.password || "") : "";
    // Serialize before clearing the input so the secret only remains in the
    // in-flight request body.  It is never copied into the operation log.
    const requestBody = JSON.stringify(request);
    if (requestedAction === "wifi-connect" && $("wifiConnectPassword")) $("wifiConnectPassword").value = "";
    const draftRange = requestedAction === "audio-volume"
      ? "audioVolume"
      : (requestedAction === "backlight-set" ? "backlightPercent" : "");
    state.pendingActions.add(requestedAction);
    if (requestedAction === "ping-interface" && tool === "ethernet") {
      const iface = String(request.params.interface || value("ethernetInterface") || "当前接口").trim();
      const target = String(request.params.target || "www.baidu.com").trim() || "www.baidu.com";
      setEthernetPingFeedback({
        state: "running",
        message: `正在通过 ${iface} Ping ${target}`,
        detail: "请求已提交，等待板端返回结果。"
      });
    }
    updateControlAvailability();
    writeOperationLog(tool, `正在提交 ${requestedAction}`, operationLogParams(requestedAction, request.params));

    try {
      const response = await apiFetch("/api/control", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: requestBody
      });
      let data = {};
      try {
        data = await response.json();
      } catch (_) {
        data = { message: `HTTP ${response.status}` };
      }
      if (!response.ok) {
        const error = new Error(data.message || `HTTP ${response.status}`);
        error.status = response.status;
        error.data = data;
        throw error;
      }

      if (typeof data.backendBusy === "boolean") {
        state.board.backendBusy = data.backendBusy;
        state.board.backendBusyOperation = data.backendBusyOperation || requestedAction;
      }
      // Audio tasks use their own lock and do not necessarily make the
      // general hardware backend busy. Preserve the accepted-operation state
      // until the next board refresh so another audio command cannot slip in
      // between this response and that refresh.
      if (typeof data.audioOperationBusy === "boolean") {
        state.board.audioOperationBusy = data.audioOperationBusy;
      }
      if (data.gpuBenchmarkStatus && typeof data.gpuBenchmarkStatus === "object") {
        state.gpuBenchmarkStatus = data.gpuBenchmarkStatus;
        state.board.gpuBenchmarkStatus = state.gpuBenchmarkStatus;
        renderGpuStatus(state.gpuBenchmarkStatus);
      }
      if (data.cameraPreview && typeof data.cameraPreview === "object") {
        applyCameraPreviewStatus(data.cameraPreview);
      }
      if (data.npuYolo && typeof data.npuYolo === "object") {
        applyNpuYoloStatus(data.npuYolo);
      }
      if (data.wifiManagerStatus && typeof data.wifiManagerStatus === "object") {
        state.wifiManagerStatus = mergeWifiManagerStatus(state.wifiManagerStatus, data.wifiManagerStatus);
        state.wifiAccessPoints = list(data.wifiAccessPoints);
        state.board.wifiManagerStatus = state.wifiManagerStatus;
        state.board.wifiAccessPoints = state.wifiAccessPoints;
        renderWifiStatus(state.wifiManagerStatus, state.wifiAccessPoints);
      }
      writeOperationLog(tool, `${requestedAction} 已受理`, redactWifiPassword(data.message || "后端开始执行", wifiPassword));
      toast(`${moduleById.get(tool)?.title || "操作"}：请求已提交`);
      window.setTimeout(refreshAll, 450);
    } catch (error) {
      if (draftRange) {
        delete state.rangeDrafts[draftRange];
        void refreshAll();
      }
      if (!error.authentication) {
        if (error.data?.npuYolo && typeof error.data.npuYolo === "object") {
          applyNpuYoloStatus(error.data.npuYolo);
        }
        if (error.data?.cameraPreview && typeof error.data.cameraPreview === "object") {
          applyCameraPreviewStatus(error.data.cameraPreview);
        }
        // A 409 is also used for a board-profile or runtime-readiness gate.
        // Only replace the server's explanation when it explicitly reports
        // that another backend task owns the operation lock.
        const busy = error.status === 409 && error.data?.backendBusy === true;
        const message = busy
          ? `后端正在执行 ${error.data.backendBusyOperation || "其他任务"}，请稍后重试。`
          : (error.data?.message || error.message);
        const safeMessage = redactWifiPassword(message, wifiPassword);
        if (requestedAction === "ping-interface" && tool === "ethernet") {
          setEthernetPingFeedback({
            state: "error",
            message: "Ping 请求未提交",
            detail: safeMessage || "后端没有接受本次 Ping 测试。"
          });
        }
        writeOperationLog(tool, `${requestedAction} 执行失败`, safeMessage);
        toast(safeMessage, "error");
      }
    } finally {
      state.pendingActions.delete(requestedAction);
      updateControlAvailability();
    }
  }

  async function requestConfirmation(title, message) {
    const dialog = $("confirmDialog");
    if (!dialog || typeof dialog.showModal !== "function") return false;
    setText("confirmTitle", title);
    setText("confirmMessage", message);
    dialog.returnValue = "cancel";
    return new Promise((resolve) => {
      dialog.addEventListener("close", () => resolve(dialog.returnValue === "confirm"), { once: true });
      dialog.showModal();
    });
  }

  async function requestWifiPassword(networkName, interfaceName) {
    const dialog = $("wifiPasswordDialog");
    const input = $("wifiConnectPassword");
    if (!dialog || !input || typeof dialog.showModal !== "function") {
      toast("当前浏览器无法显示 Wi-Fi 密码输入窗口。", "error");
      return null;
    }
    setText("wifiPasswordTitle", `输入 ${networkName} 的密码`);
    setText("wifiPasswordMessage", `将通过 Wi-Fi 网卡 ${interfaceName} 连接该加密热点。密码只随本次请求发送，不会保存到操作记录。`);
    setText("wifiPasswordError", "");
    input.value = "";
    input.type = "password";
    const toggle = $("toggleWifiConnectPassword");
    if (toggle) {
      toggle.setAttribute("aria-label", "显示 Wi-Fi 密码");
      toggle.setAttribute("aria-pressed", "false");
    }
    dialog.returnValue = "cancel";
    return new Promise((resolve) => {
      dialog.addEventListener("close", () => {
        const password = dialog.returnValue === "connect" ? input.value : null;
        input.value = "";
        resolve(password);
      }, { once: true });
      dialog.showModal();
      window.setTimeout(() => input.focus(), 0);
    });
  }

  async function performWifiConnect(point) {
    const interfaceName = value("wifiInterface").trim();
    const networkId = String(point?.id || point?.bssid || "").trim();
    const networkName = String(point?.displayName || point?.ssid || "隐藏网络").trim() || "所选网络";
    if (!interfaceName) {
      toast("请先读取无线状态并选择 Wi-Fi 网卡。", "error");
      $("wifiInterface")?.focus();
      return;
    }
    if (!networkId || point?.hidden) {
      toast("该热点没有可连接的 SSID，请使用系统网络设置连接隐藏网络。", "error");
      return;
    }
    const params = { interface: interfaceName, networkId };
    if (point?.secured) {
      const password = await requestWifiPassword(networkName, interfaceName);
      if (password === null) return;
      if (!password) {
        toast("请输入 Wi-Fi 密码后再连接。", "error");
        return;
      }
      params.password = password;
    } else {
      const accepted = await requestConfirmation(
        `确认连接 ${networkName}？`,
        `Wi-Fi 网卡 ${interfaceName} 将切换网络。当前浏览器会话可能立即断开；请记住开发板新的 IP 地址并重新打开测试平台。`
      );
      if (!accepted) return;
    }
    await sendControl("wifi-connect", params);
  }

  async function performAction(action) {
    if (action === "watchdog-start") {
      if (!$("watchdogConfirm").checked) {
        toast("请先勾选“我确认允许开发板复位”", "error");
        $("watchdogConfirm").focus();
        return;
      }
      const accepted = await requestConfirmation(
        "确认启动看门狗复位测试？",
        `开发板可能在 ${numberValue("watchdogTimeout", 5)} 秒后重启，当前 Web 连接也会中断。请确认重要数据已经保存。`
      );
      if (!accepted) return;
    }
    if (action === "ethernet-static") {
      const accepted = await requestConfirmation(
        "确认应用静态网络地址？",
        `接口 ${value("ethernetInterface") || "未选择"} 将切换到 ${value("ethIp") || "未填写地址"}，浏览器当前连接可能立即断开。`
      );
      if (!accepted) return;
    }
    if (action === "can-loop-start") {
      const accepted = await requestConfirmation(
        "确认开始 CAN 循环发送？",
        "该操作会持续向真实 CAN 总线发送测试帧。请确认收发器、终端电阻和目标设备允许接收测试流量。"
      );
      if (!accepted) return;
    }
    if (action === "wifi-disconnect") {
      const interfaceName = value("wifiInterface").trim();
      if (!interfaceName) {
        toast("请先读取无线状态并选择 Wi-Fi 网卡。", "error");
        $("wifiInterface")?.focus();
        return;
      }
      const accepted = await requestConfirmation(
        "确认断开 Wi-Fi？",
        `Wi-Fi 网卡 ${interfaceName} 会断开当前无线网络。若浏览器正通过此网络访问开发板，页面将立即失去连接。`
      );
      if (!accepted) return;
    }
    await sendControl(action);
  }

  function updateControlAvailability() {
    const networkUnlocked = $("ethernetUnlock").checked;
    const canAuthorized = $("canAuthorize").checked;
    const cellularAuthorized = $("cellularAuthorize").checked;
    const backendBusy = Boolean(state.board.backendBusy);
    const pending = state.pendingActions.size > 0;
    const storageWriteConfirmed = $("storageWriteConfirm").checked;
    const storageAvailable = profileHasCapability("storage");
    const playbackAvailable = list(state.board.audioPlaybackDevices).length > 0;
    const captureAvailable = list(state.board.audioCaptureDevices).length > 0;
    const musicProfileEnabled = profileHasCapability("music");
    const buzzerDeclared = profileHasCapability("buzzer");
    const buzzerAvailable = typeof state.board.buzzerAvailable === "boolean"
      ? state.board.buzzerAvailable
      : buzzerDeclared;
    const environmentAvailable = profileHasCapability("temperaturehumidity")
      && Boolean(state.board.environmentStatus?.available || state.board.environmentState?.available);
    const environmentState = state.board.environmentStatus || state.board.environmentState || {};
    const environmentSampling = environmentState.sampling === true || Boolean(environmentState.sampling?.inFlight);
    const keyDevices = list(state.board.keyTestStatus?.devices)
      .filter((device) => device && device.eligible !== false);
    const keysAvailable = profileHasCapability("keys") && keyDevices.length > 0;
    const keyMonitoring = Boolean(state.board.keyTestStatus?.monitoring);
    const gpuStatus = gpuStatusForBoard();
    const gpuProfileEnabled = profileHasCapability("gpu");
    const gpuAvailable = gpuProfileEnabled && Boolean(gpuStatus.available);
    const gpuRunning = Boolean(gpuStatus.running);
    const gpuPreviewAvailable = gpuProfileEnabled && Boolean(gpuStatus.previewAvailable);
    const gpuPreviewRunning = Boolean(gpuStatus.previewRunning);
    const npuYoloStatus = npuYoloStatusForBoard();
    const npuYoloServiceAvailable = npuYoloStatus.serviceAvailable !== false;
    const npuYoloProfileAvailable = npuYoloProfileEnabled(npuYoloStatus);
    const npuYoloReady = npuYoloProfileAvailable && Boolean(npuYoloStatus.available);
    const npuYoloRunning = Boolean(npuYoloStatus.running);
    const npuYoloCanStop = Boolean(npuYoloStatus.canStop);
    const cameraPreview = cameraPreviewStatusForBoard();
    const cameraPreviewServiceAvailable = cameraPreview.available !== false;
    const cameraPreviewState = String(cameraPreview.status || "").trim();
    const cameraPreviewSessionActive = Boolean(cameraPreview.running || cameraPreview.active
      || ["starting", "waiting", "stopping"].includes(cameraPreviewState));
    const wifiStatus = wifiStatusForBoard();
    const wifiProfileEnabled = profileHasCapability("wifi");
    const wifiDevices = list(wifiStatus.devices);
    const wifiNmcliAvailable = Boolean(wifiStatus.nmcliAvailable);
    const wifiDeviceSelected = Boolean(value("wifiInterface").trim());
    const cellularModem = cellularModemStatusForBoard();
    const supportedCellularDetected = Boolean(cellularModem.detected);
    const cellularDialerReady = !supportedCellularDetected || Boolean(cellularModem.dialerAvailable);
    const cellularDhcpReady = !Boolean(cellularModem.dhcpClientRequired)
      || Boolean(cellularModem.dhcpClientAvailable);
    const cellularInterfaceReady = Boolean(value("cellularInterface").trim() || preferredCellularInterfaceForBoard());

    document.querySelectorAll(".control-action, .confirm-action").forEach((button) => {
      const action = button.dataset.action || "";
      const toolRuntime = moduleRuntime(operationTool(action));
      const isStop = action.endsWith("-stop") || action === "audio-stop-all" || action === "audio-stop-audio" || action === "can-loop-stop";
      const requiresDiscoveredDevice = ["uart", "ethernet", "can", "cellular", "camera", "hdmiin", "watchdog", "music", "buzzer", "led", "backlight"]
        .includes(operationTool(action));
      let disabled = (backendBusy || pending) && !isStop;
      if (!toolRuntime.profileEnabled) disabled = true;
      if (requiresDiscoveredDevice && !toolRuntime.available && !isStop) disabled = true;
      if (button.classList.contains("requires-network-unlock") && !networkUnlocked) disabled = true;
      if (button.classList.contains("can-authorized") && !canAuthorized) disabled = true;
      if (button.classList.contains("cellular-authorized") && !cellularAuthorized) disabled = true;
      if (action === "cellular-start" && !cellularDialerReady) disabled = true;
      if (action === "cellular-start" && !cellularDhcpReady) disabled = true;
      if (action === "cellular-ping" && !cellularInterfaceReady) disabled = true;
      if (action.startsWith("audio-") && state.board.audioOperationBusy && !isStop) disabled = true;
      if ((action === "audio-play-test" || action === "audio-play-recorded" || action === "audio-volume") && !playbackAvailable) disabled = true;
      if (action === "audio-record" && !captureAvailable) disabled = true;
      if (action.startsWith("audio-beep-") && !buzzerAvailable) disabled = true;
      if (action.startsWith("led-") && !state.board.ledAvailable) disabled = true;
      if (action.startsWith("fan-") && (!state.board.fanStatus?.available || state.board.fanStatus?.mode === "unsupported")) disabled = true;
      if (action === "environment-refresh" && (!environmentAvailable || environmentSampling)) disabled = true;
      if (action === "keys-start" && (!keysAvailable || keyMonitoring)) disabled = true;
      if (action === "keys-clear" && (!keysAvailable || keyMonitoring)) disabled = true;
      if (action.startsWith("storage-") && !storageAvailable) disabled = true;
      if (action.startsWith("storage-") && button.dataset.storageAllowed === "false") disabled = true;
      if (action === "storage-speed" && !storageWriteConfirmed) disabled = true;
      if (action === "gpu-start" && (!gpuAvailable || gpuRunning || gpuPreviewRunning)) disabled = true;
      if (action === "gpu-stop" && !gpuRunning) disabled = true;
      if (action === "gpu-preview-start" && (!gpuPreviewAvailable || gpuRunning || gpuPreviewRunning)) disabled = true;
      if (action === "gpu-preview-stop" && !gpuPreviewRunning) disabled = true;
      if (action === "npu-yolo-discover") disabled = !npuYoloProfileAvailable || (backendBusy || pending) || !npuYoloServiceAvailable;
      if (action === "npu-yolo-start" && (!npuYoloProfileAvailable || !npuYoloReady || npuYoloRunning || !npuYoloStatus.canStart)) disabled = true;
      if (action === "npu-yolo-stop" && (!npuYoloProfileAvailable || !npuYoloRunning || !npuYoloCanStop)) disabled = true;
      if (action === "camera-preview-start" && (!cameraPreviewServiceAvailable || cameraPreviewSessionActive)) disabled = true;
      if (action === "camera-preview-stop" && !cameraPreviewSessionActive) disabled = true;
      if (action.startsWith("wifi-") && (!wifiProfileEnabled || !wifiNmcliAvailable)) disabled = true;
      if (action === "wifi-scan" && !wifiDeviceSelected) disabled = true;
      if (action === "wifi-disconnect" && !wifiDeviceSelected) disabled = true;
      button.disabled = disabled;
    });

    ["beepPercent"].forEach((id) => { $(id).disabled = !buzzerAvailable; });
    if ($("fanPercent")) $("fanPercent").disabled = !state.board.fanStatus?.available || state.board.fanStatus?.mode === "unsupported";
    ["ledDevice", "ledPercent"].forEach((id) => { $(id).disabled = !state.board.ledAvailable; });
    if ($("keyTestDevice")) $("keyTestDevice").disabled = !keysAvailable || keyMonitoring;
    $("backlightPercent").disabled = !state.board.backlightAvailable;
    $("audioPlaybackDevice").disabled = !musicProfileEnabled || !playbackAvailable;
    $("audioCaptureDevice").disabled = !musicProfileEnabled || !captureAvailable;
    $("audioVolume").disabled = !musicProfileEnabled || !playbackAvailable;
    $("wifiInterface").disabled = !wifiProfileEnabled || !wifiNmcliAvailable || !wifiDevices.length || Boolean(wifiStatus.busy);

    const musicAvailable = musicProfileEnabled && (playbackAvailable || captureAvailable);
    $("musicAvailability").hidden = musicAvailable;
    setText("musicAvailability", musicProfileEnabled
      ? "当前系统未发现可用 ALSA 播放或录音设备，相关操作已禁用。"
      : "当前板型未启用音乐与录音测试，相关操作已锁定。");
    $("buzzerAvailability").hidden = buzzerAvailable;
    setText("buzzerAvailability", state.board.buzzerAvailabilityStatus
      || (buzzerDeclared ? "蜂鸣器尚未准备完成，启动与提示操作已禁用。" : "当前板型未声明可用蜂鸣器，启动与提示操作已禁用。"));
    const fanControllable = Boolean(state.board.fanStatus?.available) && state.board.fanStatus?.mode !== "unsupported";
    $("fanAvailability").hidden = fanControllable;
    setText("fanAvailability", state.board.fanStatus?.message
      || "未发现准确匹配的 pwmfan 风扇节点，相关操作已禁用。");
    const ledProfileEnabled = profileHasCapability("led");
    $("ledAvailability").hidden = ledProfileEnabled && Boolean(state.board.ledAvailable);
    setText("ledAvailability", ledProfileEnabled
      ? "当前板型未发现已声明的 LED 设备，相关操作已禁用。"
      : "当前板型未启用板载 LED 测试，相关操作已锁定。");
    const gpuStatusFailure = state.toolStatusFailures.gpu;
    $("gpuAvailability").hidden = gpuAvailable && !gpuStatusFailure;
    setText("gpuAvailability", gpuStatusFailure?.detail || (!gpuProfileEnabled
      ? "当前板型未启用 GPU 基准测试，相关操作已锁定。"
      : (gpuStatus.error || "未发现可执行的 glmark2-es2 或 glmark2-es。请安装 glmark2-es2 后重新打开页面。")));
    const wifiNeedsRead = wifiStatusNeedsRead(wifiStatus);
    const wifiAvailable = wifiProfileEnabled && wifiNmcliAvailable && (wifiDevices.length > 0 || wifiNeedsRead);
    const wifiStatusFailure = state.toolStatusFailures.wifi;
    $("wifiAvailability").hidden = wifiAvailable && !wifiStatusFailure;
    setText("wifiAvailability", wifiStatusFailure?.detail || (!wifiProfileEnabled
      ? "当前板型未启用 Wi-Fi 管理，相关操作已锁定。"
      : (!wifiNmcliAvailable
        ? "未找到 nmcli。请安装并启动 NetworkManager 后重新读取状态。"
        : (wifiNeedsRead
          ? "尚未读取 Wi-Fi 状态。点击“读取无线状态”检查本机网卡。"
          : "尚未发现可由 NetworkManager 管理的 Wi-Fi 网卡。"))));
    if (!buzzerAvailable) setText("beepStatus", "未就绪");

    setText("ethernetLockState", networkUnlocked ? "已解锁" : "已锁定");
    setText("canAuthState", canAuthorized ? "已授权" : "未授权");
    setText("cellularAuthState", cellularAuthorized ? "已授权" : "未授权");
    setText("watchdogConfirmState", $("watchdogConfirm").checked ? "已确认" : "未确认");
    const storageWriteText = storageWriteConfirmed ? "已确认写入" : "只读模式";
    setText("storageWriteState", storageWriteText);
    setText("storageWriteStateMirror", storageWriteConfirmed ? "已确认" : "确认前不可用");
    // Access-point rows are created dynamically, so refresh their disabled
    // state here as well. This prevents a second connection click while the
    // first request is still in flight.
    renderWifiAccessPoints(wifiAccessPointsForBoard(), wifiStatusForBoard());
  }

  function bindEvents() {
    $("authForm").addEventListener("submit", authenticate);
    $("toggleToken").addEventListener("click", () => {
      const tokenInput = $("tokenInput");
      const toggleToken = $("toggleToken");
      const visible = tokenInput.type === "password";
      tokenInput.type = visible ? "text" : "password";
      toggleToken.innerHTML = tokenVisibilityIcon(visible);
      toggleToken.setAttribute("aria-label", visible ? "隐藏 Token" : "显示 Token");
      toggleToken.setAttribute("aria-pressed", String(visible));
    });
    $("toggleWifiConnectPassword").addEventListener("click", () => {
      const input = $("wifiConnectPassword");
      const toggle = $("toggleWifiConnectPassword");
      const visible = input.type === "password";
      input.type = visible ? "text" : "password";
      toggle.setAttribute("aria-label", visible ? "隐藏 Wi-Fi 密码" : "显示 Wi-Fi 密码");
      toggle.setAttribute("aria-pressed", String(visible));
    });
    $("reauthenticate").addEventListener("click", () => showAuthentication("已锁定页面，请重新输入 Token。"));
    $("refreshButton").addEventListener("click", refreshAll);
    $("retryButton").addEventListener("click", refreshAll);
    $("discoverButton").addEventListener("click", async () => {
      await sendControl("refresh", {});
      window.setTimeout(refreshAll, 350);
    });
    $("backHome").addEventListener("click", () => { window.location.hash = "#/home"; });
    $("mobileMenu").addEventListener("click", () => {
      if ($("sidebar").classList.contains("open")) closeMobileNavigation();
      else openMobileNavigation();
    });
    $("closeMobileMenu").addEventListener("click", () => {
      closeMobileNavigation();
      $("mobileMenu").focus();
    });
    $("mobileScrim").addEventListener("click", () => {
      closeMobileNavigation();
      $("mobileMenu").focus();
    });
    window.addEventListener("hashchange", routeFromHash);
    window.addEventListener("resize", syncNavigationPresentation);
    window.addEventListener("online", refreshAll);
    window.addEventListener("offline", () => showOffline("浏览器已离线，恢复网络后会自动重试。"));
    window.addEventListener("keydown", (event) => {
      if (trapMobileNavigationFocus(event)) return;
      if (event.key === "Escape" && $("sidebar").classList.contains("open")) {
        closeMobileNavigation();
        $("mobileMenu").focus();
      }
    });

    document.querySelectorAll(".control-action, .confirm-action").forEach((button) => {
      button.addEventListener("click", () => performAction(button.dataset.action));
    });
    ["ethernetUnlock", "canAuthorize", "cellularAuthorize", "watchdogConfirm", "storageWriteConfirm"].forEach((id) => {
      $(id).addEventListener("change", updateControlAvailability);
    });
    $("gpioMode").addEventListener("change", () => {
      const output = value("gpioMode") === "out";
      $("gpioHigh").disabled = !output;
      setText("gpioModeState", output ? "输出" : "输入");
    });
    ["uartDataBits", "uartParity", "uartStopBits"].forEach((id) => {
      $(id).addEventListener("change", () => {
        const parity = { None: "N", Even: "E", Odd: "O" }[value("uartParity")] || value("uartParity").slice(0, 1);
        setText("uartFormat", `${value("uartDataBits")}-${parity}-${value("uartStopBits")}`);
      });
    });
    $("audioVolume").addEventListener("input", () => {
      state.rangeDrafts.audioVolume = value("audioVolume");
      setText("audioVolumeLabel", `${value("audioVolume")}%`);
    });
    $("beepPercent").addEventListener("input", () => updateBuzzerPresentation(state.board));
    $("ledPercent").addEventListener("input", () => setText("ledPercentLabel", `${value("ledPercent")}%`));
    $("ledDevice").addEventListener("change", () => {
      const selected = $("ledDevice").options[$("ledDevice").selectedIndex];
      setText("ledCurrent", selected && selected.dataset.percent ? `${selected.dataset.percent}%` : "--");
      syncLedPercentFromSelection(true);
    });
    $("backlightPercent").addEventListener("input", () => {
      state.rangeDrafts.backlightPercent = value("backlightPercent");
      setText("backlightLabel", `${value("backlightPercent")}%`);
    });
    $("ethernetInterface").addEventListener("change", () => {
      renderEthernetStatus(state.board);
      updateControlAvailability();
    });
    $("pingTarget").addEventListener("input", () => {
      renderEthernetStatus(state.board);
    });
    $("wifiInterface").addEventListener("change", () => {
      renderWifiAccessPoints(wifiAccessPointsForBoard(), wifiStatusForBoard());
      renderNetworkReadout("wifiAddressReadout", wifiIpv4Details(wifiStatusForBoard()));
      updateControlAvailability();
    });
  }

  buildNavigation();
  decorateSpecializedModuleHeaders();
  decorateControlActions();
  ensureFileUploadUi();
  bindEvents();
  updateControlAvailability();
  syncNavigationPresentation();
  if (!window.location.hash) window.history.replaceState(null, "", "#/home");
  showAuthentication("");
})();
