const legacyPendingStorageKey = "rknetupdate.pending-recovery-job.v1";
const defaultAuthMessage = "Token 仅保存在当前页面内存中，刷新页面后需要重新验证。";

class AuthenticationError extends Error {
  constructor() {
    super("设备访问 Token 不正确或服务配置已经更改");
    this.name = "AuthenticationError";
  }
}

const state = {
  token: "",
  authenticated: false,
  authenticating: false,
  device: null,
  target: "uboot",
  file: null,
  files: [],
  pollTimer: null,
  lastJob: null,
  recoveryPending: false,
  recoveryOffline: false,
  rebootPromptedJobs: new Set(),
  rebootPromptJob: null,
  rebootPromptPreviousFocus: null,
  systemRebootPending: false,
  systemRebootOffline: false,
  uploadActive: false,
  uploadComplete: false,
  uploadPercent: 0,
  connectionMode: "connecting",
};

const $ = (id) => document.getElementById(id);
const targetLabels = Object.freeze({
  uboot: "U-Boot",
  boot: "Boot",
  rootfs: "Rootfs 分区升级",
  bundle: "组合离线升级",
  updateimg: "完整系统 OTA",
});
const recoveryTargets = new Set(["rootfs", "bundle", "updateimg"]);
const activeStatuses = new Set([
  "uploading",
  "awaiting_confirmation",
  "flashing",
  "verifying",
  "preparing_recovery",
  "recovery_prepared",
  "reboot_requested",
  "system_reboot_requested",
]);
const terminalStatuses = new Set(["succeeded", "system_reboot_failed", "dry_run_complete", "failed", "cancelled"]);

const workflowProfiles = Object.freeze({
  uboot: {
    code: "BL",
    kicker: "BOOT CHAIN / DIRECT",
    title: "U-Boot 引导升级",
    description: "在线写入引导分区，并按镜像长度完成 SHA-256 回读。",
    tags: ["直接分区写入", "写后回读", "需要救砖链路"],
    ready: "分区就绪",
    safetyTitle: "引导链写入",
    safetyText: "写入错误的 uboot.img 可能导致设备无法启动；请保持串口与 Loader/Maskrom 救援链路可用。",
  },
  boot: {
    code: "KRN",
    kicker: "KERNEL / DIRECT",
    title: "Boot 内核升级",
    description: "在线写入 boot 分区，完成 fsync 后按镜像长度回读校验。",
    tags: ["直接分区写入", "SHA-256 回读", "无需 Recovery"],
    ready: "分区就绪",
    safetyTitle: "内核分区写入",
    safetyText: "请确认 boot.img 与当前 RK3588 板级设备树和内核配置匹配。",
  },
  rootfs: {
    code: "RFS",
    kicker: "FILESYSTEM / RECOVERY",
    title: "Rootfs 离线升级",
    description: "正常系统只负责校验和准备，实际文件系统写入在 Recovery 中完成。",
    tags: ["Recovery 离线", "临时 p7 暂存", "自动返回系统"],
    ready: "Recovery 就绪",
    safetyTitle: "离线文件系统升级",
    safetyText: "进入 Recovery 后网页会暂时离线；请通过串口观察真实写入进度，完成前禁止断电。",
  },
  updateimg: {
    code: "OTA",
    kicker: "SYSTEM PACKAGE / RECOVERY",
    title: "完整系统 OTA",
    description: "验证 Rockchip RKFW/RKAF 容器，在 Recovery 中按包内容更新全部系统分区。",
    tags: ["完整 update.img", "全分区写入", "自动重启"],
    ready: "OTA 链路就绪",
    safetyTitle: "完整系统写入",
    safetyText: "此操作会覆盖包内全部分区。Recovery 执行期间禁止断电；网页显示流程阶段，串口显示真实分区与百分比。",
  },
});

const directStages = Object.freeze([
  { label: "上传镜像", detail: "传输到设备" },
  { label: "格式校验", detail: "目标与容量" },
  { label: "分区写入", detail: "fsync 落盘" },
  { label: "回读比对", detail: "SHA-256" },
  { label: "任务完成", detail: "结果确认" },
]);
const rootfsStages = Object.freeze([
  { label: "上传镜像", detail: "接收 rootfs" },
  { label: "文件校验", detail: "哈希与几何" },
  { label: "准备 Recovery", detail: "BCB 复核" },
  { label: "交接 Recovery", detail: "后续只看串口", channel: "SERIAL" },
]);
const updateimgStages = Object.freeze([
  { label: "上传整包", detail: "接收 update.img" },
  { label: "完整校验", detail: "RKFW / RKAF" },
  { label: "准备 Recovery", detail: "暂存与 BCB" },
  { label: "交接 Recovery", detail: "后续只看串口", channel: "SERIAL" },
]);

function targetLabel(target) {
  return targetLabels[target] || target || "--";
}

function recoveryInProgress(job) {
  return recoveryTargets.has(job?.target) &&
    ["preparing_recovery", "recovery_prepared", "reboot_requested"].includes(job.status);
}

function recoveryPhaseText(phase) {
  return ({
    "rootfs-prepared": "Rootfs Recovery 请求已准备",
    "bundle-prepared": "组合升级 Recovery 请求已准备",
    "updateimg-prepared": "完整 OTA Recovery 请求已准备",
    "stock-pending": "已交接 Rockchip updater",
  })[phase] || phase || "--";
}

function formatBytes(value) {
  if (!Number.isFinite(value) || value <= 0) return "--";
  const units = ["B", "KiB", "MiB", "GiB"];
  let size = value;
  let unit = 0;
  while (size >= 1024 && unit < units.length - 1) {
    size /= 1024;
    unit += 1;
  }
  return `${size >= 10 || unit === 0 ? size.toFixed(0) : size.toFixed(1)} ${units[unit]}`;
}

function logEvent(message) {
  const li = document.createElement("li");
  const time = document.createElement("time");
  time.textContent = new Date().toLocaleTimeString("zh-CN", { hour12: false });
  const text = document.createElement("span");
  text.textContent = message;
  li.append(time, text);
  $("eventList").prepend(li);
}

function setConnectionMode(mode, text) {
  state.connectionMode = mode;
  $("headerConnection").className = `connection-pill ${mode}`;
  $("headerConnectionText").textContent = text;
  $("statusLight").className = `status-light ${mode}`;
  $("statusLight").setAttribute("aria-label", text);
  if (!$("jobView").classList.contains("hidden")) {
    const badgeMode = mode === "online" ? "online" : ["recovery", "rebooting"].includes(mode) ? "recovery" : "review";
    $("connectionBadge").className = `link-state ${badgeMode}`;
    $("connectionBadge").textContent = text;
  }
}

function stopPolling() {
  clearInterval(state.pollTimer);
  state.pollTimer = null;
}

function setAuthMessage(message = defaultAuthMessage, tone = "") {
  $("authMessage").textContent = message;
  $("authMessage").className = `auth-message ${tone}`.trim();
  $("tokenInput").setAttribute("aria-invalid", tone === "error" ? "true" : "false");
}

function setAuthenticationBusy(busy) {
  state.authenticating = busy;
  $("tokenInput").disabled = busy;
  $("toggleTokenButton").disabled = busy;
  $("authSubmit").disabled = busy;
  $("authSubmit").querySelector("span").textContent = busy ? "正在验证设备 Token…" : "验证并进入控制台";
}

function setTokenVisible(visible) {
  $("tokenInput").type = visible ? "text" : "password";
  $("toggleTokenButton").textContent = visible ? "隐藏" : "显示";
  $("toggleTokenButton").setAttribute("aria-label", visible ? "隐藏 Token" : "显示 Token");
  $("toggleTokenButton").setAttribute("aria-pressed", visible ? "true" : "false");
}

function requireAuthentication(message = defaultAuthMessage, tone = "") {
  stopPolling();
  if (!$("rebootDialog").hidden) closeRebootDialog(true);
  state.token = "";
  state.authenticated = false;
  state.device = null;
  state.uploadActive = false;
  $("consoleMain").hidden = true;
  $("authGate").hidden = false;
  $("changeTokenButton").classList.add("hidden");
  $("modeBadge").textContent = "安全锁定";
  $("modeBadge").classList.remove("live");
  setConnectionMode("locked", "需要 Token");
  setTokenVisible(false);
  $("tokenInput").value = "";
  setAuthMessage(message, tone);
  requestAnimationFrame(() => $("tokenInput").focus());
}

function unlockConsole() {
  state.authenticated = true;
  $("authGate").hidden = true;
  $("consoleMain").hidden = false;
  $("changeTokenButton").classList.remove("hidden");
  $("tokenInput").value = "";
  setTokenVisible(false);
}

function applyDevice(device) {
  state.device = device;
  $("hostname").textContent = device.hostname || "Rockchip Device";
  $("model").textContent = device.model || "Rockchip Linux";
  $("osName").textContent = device.os || "Linux";
  $("runtime").textContent = device.runtime || "--";
}

function setConsoleModalInert(inert) {
  $("consoleMain").inert = inert;
  document.querySelector(".topbar").inert = inert;
  document.body.classList.toggle("modal-open", inert);
}

function directRebootVerified(job) {
  if (!job || !["uboot", "boot"].includes(job.target) || !["succeeded", "system_reboot_failed"].includes(job.status) || job.dry_run) return false;
  const uploaded = String(job.sha256 || "").toLowerCase();
  const readback = String(job.readback_sha256 || "").toLowerCase();
  return Boolean(job.id && uploaded && readback && uploaded === readback);
}

function setRebootDialogMode(mode, message = "") {
  const job = state.rebootPromptJob;
  const card = $("rebootCard");
  card.className = `reboot-card ${mode === "ready" ? "" : mode}`.trim();
  card.dataset.mode = mode;
  const copy = {
    ready: {
      title: `${targetLabel(job?.target)} 写入与回读校验完成`,
      description: "新镜像已经安全写入目标分区，需要重新启动设备后生效。",
      status: job?.status === "system_reboot_failed" ? "上次重启命令未执行，可安全重试。" : "当前设备仍保持在线，可选择稍后手动重启。",
      tone: "",
    },
    requesting: {
      title: "正在提交安全重启",
      description: "设备正在复核任务状态、回读哈希和当前升级锁。",
      status: "请保持页面打开，不要重复操作。",
      tone: "waiting",
    },
    waiting: {
      title: "设备正在重新启动",
      description: "重启指令已经由设备接受，网页连接会暂时中断并自动等待服务恢复。",
      status: "等待设备离线并重新上线…",
      tone: "waiting",
    },
    complete: {
      title: "设备已经重新上线",
      description: "RKNetUpdate 服务连接已恢复，新的 U-Boot/Boot 镜像已进入本次启动链路。",
      status: "重启流程完成，可以返回控制台继续检查设备状态。",
      tone: "success",
    },
    error: {
      title: "重启请求未执行",
      description: "分区写入结果仍然保留，但设备没有接受本次重启请求。",
      status: message || "请检查设备状态后重试，或稍后通过串口手动重启。",
      tone: "error",
    },
  }[mode];
  $("rebootDialogTitle").textContent = copy.title;
  $("rebootDialogDescription").textContent = copy.description;
  $("rebootDialogStatus").textContent = copy.status;
  $("rebootDialogStatus").className = `reboot-dialog-status ${copy.tone}`.trim();
  const pending = mode === "requesting" || mode === "waiting";
  $("rebootLaterButton").disabled = pending;
  $("rebootNowButton").disabled = pending;
  $("rebootLaterButton").classList.toggle("hidden", mode === "complete");
  $("rebootNowButton").classList.toggle("hidden", mode === "complete");
  $("rebootDoneButton").classList.toggle("hidden", mode !== "complete");
  if (mode === "complete") requestAnimationFrame(() => $("rebootDoneButton").focus());
}

function openRebootDialog(job) {
  state.rebootPromptJob = job;
  state.rebootPromptPreviousFocus = document.activeElement;
  $("rebootTarget").textContent = targetLabel(job.target);
  $("rebootHash").textContent = job.readback_sha256;
  $("rebootDialog").hidden = false;
  setConsoleModalInert(true);
  setRebootDialogMode("ready");
  requestAnimationFrame(() => $("rebootLaterButton").focus());
}

function closeRebootDialog(force = false) {
  const mode = $("rebootCard").dataset.mode;
  if (!force && (mode === "requesting" || mode === "waiting")) return;
  $("rebootDialog").hidden = true;
  setConsoleModalInert(false);
  state.systemRebootPending = false;
  state.systemRebootOffline = false;
  state.rebootPromptJob = null;
  const previous = state.rebootPromptPreviousFocus;
  state.rebootPromptPreviousFocus = null;
  if (!force && previous instanceof HTMLElement && document.contains(previous)) previous.focus();
}

function maybePromptForDirectReboot(job) {
  if (!directRebootVerified(job) || state.rebootPromptedJobs.has(job.id)) return;
  state.rebootPromptedJobs.add(job.id);
  openRebootDialog(job);
}

async function requestSystemReboot() {
  const job = state.rebootPromptJob;
  if (!directRebootVerified(job)) {
    setRebootDialogMode("error", "任务缺少匹配的回读 SHA-256，已拒绝重启。");
    return;
  }
  setRebootDialogMode("requesting");
  try {
    const response = await api("/api/v1/reboot", {
      method: "POST",
      body: JSON.stringify({ job_id: job.id }),
    });
    state.systemRebootPending = true;
    state.systemRebootOffline = false;
    state.lastJob = response;
    renderJob(response);
    setConnectionMode("rebooting", "准备重新启动");
    setRebootDialogMode("waiting");
    logEvent(`${targetLabel(job.target)}已校验完成，设备接受了重启请求`);
    startPolling();
  } catch (error) {
    if (error instanceof AuthenticationError) {
      closeRebootDialog(true);
      requireAuthentication("Token 已失效或设备配置已经更改，请重新验证。", "error");
      return;
    }
    state.systemRebootPending = false;
    state.systemRebootOffline = false;
    setRebootDialogMode("error", error.message);
    logEvent(`设备重启未执行：${error.message}`);
  }
}

function handleRebootDialogKeydown(event) {
  if ($("rebootDialog").hidden) return;
  const mode = $("rebootCard").dataset.mode;
  if (event.key === "Escape") {
    if (mode !== "requesting" && mode !== "waiting") closeRebootDialog();
    return;
  }
  if (event.key !== "Tab") return;
  const buttons = [$("rebootLaterButton"), $("rebootNowButton"), $("rebootDoneButton")]
    .filter((button) => !button.disabled && !button.classList.contains("hidden"));
  if (!buttons.length) {
    event.preventDefault();
    $("rebootCard").focus();
    return;
  }
  const first = buttons[0];
  const last = buttons[buttons.length - 1];
  if (event.shiftKey && document.activeElement === first) {
    event.preventDefault();
    last.focus();
  } else if (!event.shiftKey && document.activeElement === last) {
    event.preventDefault();
    first.focus();
  }
}

function clearLegacyPendingJob() {
  try {
    sessionStorage.removeItem(legacyPendingStorageKey);
  } catch (_) {
    // No-op.
  }
}

async function api(path, options = {}) {
  const headers = new Headers(options.headers || {});
  if (state.token) headers.set("Authorization", `Bearer ${state.token}`);
  if (options.body && !(options.body instanceof FormData)) headers.set("Content-Type", "application/json");
  const response = await fetch(path, { ...options, headers });
  if (response.status === 401) throw new AuthenticationError();
  const body = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(body.error?.message || `请求失败 (${response.status})`);
  return body;
}

async function authenticate(event) {
  event.preventDefault();
  if (state.authenticating) return;
  const token = $("tokenInput").value.trim();
  if (!token) {
    setAuthMessage("请输入设备访问 Token。", "error");
    $("tokenInput").focus();
    return;
  }
  state.token = token;
  setAuthenticationBusy(true);
  setAuthMessage("正在向设备验证 Token…", "working");
  try {
    const device = await api("/api/v1/device");
    applyDevice(device);
    unlockConsole();
    setConnectionMode("online", "设备在线");
    updateTarget();
    logEvent(`已连接 ${device.hostname || "设备"}`);
    await refreshStatus();
    startPolling();
  } catch (error) {
    const message = error instanceof AuthenticationError
      ? "Token 验证失败，请检查输入或设备配置文件。"
      : `无法连接设备服务：${error.message}`;
    requireAuthentication(message, "error");
  } finally {
    setAuthenticationBusy(false);
  }
}

function selectedCapability() {
  return state.device?.targets?.find((item) => item.target === state.target);
}

function targetFilename(target = state.target) {
  if (target === "bundle") return "bundle.manifest.json";
  if (target === "updateimg") return "update.img";
  return `${target}.img`;
}

function updateWorkflowBrief() {
  const profile = workflowProfiles[state.target] || workflowProfiles.uboot;
  $("workflowCode").textContent = profile.code;
  $("workflowKicker").textContent = profile.kicker;
  $("workflowTitle").textContent = profile.title;
  $("workflowDescription").textContent = profile.description;
  $("workflowTags").replaceChildren(...profile.tags.map((label) => {
    const tag = document.createElement("span");
    tag.textContent = label;
    return tag;
  }));
  $("safetyTitle").textContent = profile.safetyTitle;
  $("safetyText").textContent = profile.safetyText;
  $("safetyNote").classList.toggle("critical", recoveryTargets.has(state.target) || state.target === "uboot");
}

function updateEmptyState() {
  const previews = {
    uboot: {
      label: "STANDBY",
      title: "等待 U-Boot 镜像",
      description: "载入 uboot.img 后，设备将完成格式检查、分区写入和 SHA-256 回读。",
      steps: ["上传", "校验", "写入", "回读"],
    },
    boot: {
      label: "STANDBY",
      title: "等待 Boot 镜像",
      description: "载入 boot.img 后，设备将直接写入内核分区并完成回读校验。",
      steps: ["上传", "校验", "写入", "回读"],
    },
    rootfs: {
      label: "RECOVERY",
      title: "Rootfs 离线升级已选择",
      description: "上传后设备会进入 Recovery 离线替换文件系统；网页将暂时离线，串口显示实时进度。",
      steps: ["上传", "校验", "交接 Recovery", "后续查看串口"],
    },
    updateimg: {
      label: "OTA READY",
      title: "完整系统 OTA 已选择",
      description: "上传前请先打开串口终端。进入 Recovery 后 Web 无法确认写入结果，必须以串口打印为准。",
      steps: ["整包上传", "结构校验", "交接 Recovery", "后续查看串口"],
    },
  };
  const preview = previews[state.target] || previews.uboot;
  $("standbyLabel").textContent = preview.label;
  $("emptyTitle").textContent = preview.title;
  $("emptyDescription").textContent = preview.description;
  $("emptySequence").replaceChildren(...preview.steps.map((label) => {
    const item = document.createElement("li");
    item.textContent = label;
    return item;
  }));
  $("emptyState").classList.remove("returned");
  $("emptyState").classList.toggle("critical", state.target === "updateimg");
}

function actionLabel(live) {
  if (state.target === "updateimg") return live ? "上传并启动完整系统 OTA" : "上传并开始 OTA 演练";
  if (state.target === "rootfs") return live ? "上传并启动 Rootfs 离线升级" : "上传并开始 Rootfs 演练";
  return live ? "上传并开始写入" : "上传并开始演练";
}

function updateTarget() {
  const cap = selectedCapability();
  const profile = workflowProfiles[state.target] || workflowProfiles.uboot;
  document.querySelector(".control-panel").dataset.target = state.target;
  $("devicePath").textContent = cap?.device || "--";
  $("resolvedPath").textContent = cap?.resolved_device || "--";
  $("parentDevice").textContent = cap?.parent_device || "--";
  $("expectedParent").textContent = state.device?.expected_parent_device || "dry-run 未锁定";
  $("deviceSizeLabel").textContent = state.target === "updateimg" ? "Rootfs 参考容量" : "分区容量";
  $("deviceSize").textContent = cap?.available ? formatBytes(cap.device_size) : "不可用";
  $("uploadLimit").textContent = formatBytes(cap?.max_upload_bytes || state.device?.max_upload_bytes);
  const live = Boolean(cap?.write_enabled);
  $("modeBadge").textContent = live ? "LIVE / 写入已启用" : "DRY RUN / 禁止写入";
  $("modeBadge").classList.toggle("live", live);
  $("targetAvailability").textContent = cap?.available ? profile.ready : "目标不可用";
  $("targetAvailability").className = `availability ${cap?.available ? "ok" : "bad"}`;
  $("targetAvailability").title = cap?.error || "";
  $("dropTitle").textContent = state.target === "updateimg" ? "选择完整系统 update.img" : `选择 ${targetFilename()}`;
  $("uploadButton").textContent = actionLabel(live);
  updateWorkflowBrief();
  updateEmptyState();
  resetFile();
}

function fileMetaHint() {
  if (state.target === "updateimg") return "仅接受完整 Rockchip update.img；上传后自动校验包结构并进入 Recovery";
  if (state.target === "rootfs") return "仅接受 rootfs.img；Recovery 离线写入完成后自动返回系统";
  return "仅接受当前目标对应的镜像文件";
}

function resetFile() {
  state.file = null;
  state.files = [];
  state.uploadActive = false;
  state.uploadComplete = false;
  state.uploadPercent = 0;
  $("imageInput").value = "";
  $("imageInput").multiple = false;
  $("dropMeta").textContent = fileMetaHint();
  $("dropzone").classList.remove("selected");
  $("uploadButton").disabled = true;
  $("uploadProgress").classList.add("hidden");
  $("uploadPercent").textContent = "0%";
  $("uploadBar").value = 0;
}

function chooseFiles(files) {
  const selected = Array.from(files || []);
  if (!selected.length) return;
  const file = selected[0];
  const expected = targetFilename();
  state.file = selected.length === 1 && file.name === expected ? file : null;
  $("dropTitle").textContent = file.name;
  $("dropMeta").textContent = state.file
    ? `${formatBytes(file.size)} · 已就绪，等待上传`
    : selected.length !== 1 ? "每次只能选择一个镜像" : `文件名必须为 ${expected}`;
  $("dropzone").classList.toggle("selected", Boolean(state.file));
  $("uploadButton").textContent = actionLabel(Boolean(selectedCapability()?.write_enabled));
  $("uploadButton").disabled = !state.file || !selectedCapability()?.available;
}

function transientUploadJob() {
  if (!state.file) return null;
  const cap = selectedCapability();
  return {
    target: state.target,
    filename: state.file.name,
    size: state.file.size,
    sha256: "",
    device: cap?.device || "--",
    resolved_device: cap?.resolved_device || "--",
    parent_device: cap?.parent_device || "--",
    status: "uploading",
    progress: state.uploadPercent,
    validation: {},
    dry_run: !cap?.write_enabled,
  };
}

function restoreUploadButton() {
  $("uploadButton").textContent = actionLabel(Boolean(selectedCapability()?.write_enabled));
  $("uploadButton").disabled = !state.file || !selectedCapability()?.available;
}

function lockTargetControls(locked) {
  document.querySelectorAll('input[name="target"]').forEach((input) => { input.disabled = locked; });
  $("imageInput").disabled = locked;
}

function uploadImage() {
  if (!state.authenticated || !state.token) {
    requireAuthentication("认证状态已失效，请重新输入 Token。", "error");
    return;
  }
  if (!state.file) return;
  const form = new FormData();
  form.append("image", state.file, state.file.name);
  const xhr = new XMLHttpRequest();
  xhr.open("POST", `/api/v1/uploads?target=${encodeURIComponent(state.target)}`);
  xhr.setRequestHeader("Authorization", `Bearer ${state.token}`);
  state.uploadActive = true;
  state.uploadComplete = false;
  state.uploadPercent = 0;
  lockTargetControls(true);
  $("uploadProgress").classList.remove("hidden");
  $("uploadButton").disabled = true;
  $("uploadButton").textContent = "正在上传并校验…";
  renderJob(transientUploadJob());

  xhr.upload.onprogress = (event) => {
    if (!event.lengthComputable) return;
    state.uploadPercent = Math.round((event.loaded / event.total) * 100);
    $("uploadPercent").textContent = `${state.uploadPercent}%`;
    $("uploadBar").value = state.uploadPercent;
    const current = transientUploadJob();
    if (current) renderJob(current);
  };
  xhr.upload.onload = () => {
    state.uploadComplete = true;
    state.uploadPercent = 100;
    $("uploadPercent").textContent = "100% · 服务端校验中";
    $("uploadBar").value = 100;
    $("dropMeta").textContent = "上传完成 · 正在校验镜像并准备升级环境";
    const current = transientUploadJob();
    if (current) renderJob(current);
  };
  xhr.onload = () => {
    let body = {};
    try {
      body = JSON.parse(xhr.responseText || "{}");
    } catch (_) {
      // Keep a useful transport error below.
    }
    state.uploadActive = false;
    state.uploadComplete = false;
    if (xhr.status === 401) {
      logEvent("设备访问令牌与服务配置不一致");
      lockTargetControls(false);
      restoreUploadButton();
      requireAuthentication("Token 已失效或设备配置已经更改，请重新验证。", "error");
      return;
    }
    if (xhr.status < 200 || xhr.status >= 300) {
      const message = body.error?.message || `HTTP ${xhr.status}`;
      logEvent(`升级未启动：${message}`);
      renderJob({ ...transientUploadJob(), status: "failed", error: message });
      lockTargetControls(false);
      restoreUploadButton();
      return;
    }
    renderJob(body);
    $("uploadButton").disabled = true;
    $("uploadButton").textContent = "升级任务执行中";
    if (recoveryTargets.has(body.target) && body.status === "reboot_requested") {
      logEvent(`${targetLabel(body.target)}已完成校验，设备正在进入 Recovery`);
    } else {
      logEvent(`${body.filename} 已校验，写入任务已自动启动`);
    }
  };
  xhr.onerror = () => {
    state.uploadActive = false;
    logEvent("上传连接中断");
    renderJob({ ...transientUploadJob(), status: "failed", error: "上传连接中断" });
    lockTargetControls(false);
    restoreUploadButton();
  };
  xhr.send(form);
}

function statusText(status) {
  const labels = {
    uploading: state.uploadComplete ? "服务端校验中" : "上传中",
    awaiting_confirmation: "检查通过",
    flashing: "正在写入",
    verifying: "回读校验",
    preparing_recovery: "准备 Recovery",
    recovery_prepared: "Recovery 已准备",
    reboot_requested: state.recoveryOffline ? "已交接串口" : "正在进入 Recovery",
    system_reboot_requested: state.systemRebootOffline ? "设备重启中" : "准备重启",
    system_reboot_failed: "重启未执行",
    dry_run_complete: "演练完成",
    succeeded: "写入成功",
    failed: "任务失败",
    cancelled: "已取消",
  };
  return labels[status] || "空闲";
}

function missionCopy(job) {
  if (job.status === "uploading") {
    return state.uploadComplete
      ? { title: "镜像已上传，正在执行完整校验", message: "设备正在计算哈希、解析镜像结构并核对目标分区。此阶段无需重复点击。", kicker: "SERVER VALIDATION" }
      : { title: `正在上传 ${job.filename}`, message: "镜像正通过局域网传输到设备，页面显示浏览器可观测的真实上传百分比。", kicker: "IMAGE TRANSFER" };
  }
  const copies = {
    awaiting_confirmation: { title: "镜像检查通过，正在启动任务", message: "文件名、容量、格式和目标设备已核对。", kicker: "VALIDATION PASSED" },
    flashing: { title: "正在写入目标分区", message: "数据正在落盘，完成前请勿断电或重启设备。", kicker: "PARTITION WRITE" },
    verifying: { title: "写入完成，正在回读校验", message: "设备正在按镜像实际长度重新计算 SHA-256。", kicker: "READBACK VERIFY" },
    preparing_recovery: { title: "正在准备 Recovery 升级环境", message: "升级包已验证，正在生成清单、准备 BCB 并复核离线写入请求。", kicker: "RECOVERY PREP" },
    recovery_prepared: { title: "Recovery 已准备完成", message: "设备即将正常重启并切换到离线升级环境。", kicker: "HANDOFF READY" },
    reboot_requested: state.recoveryOffline
      ? { title: "Web 跟踪已交接串口", message: "本页不显示或推断离线烧写进度。请查看串口；设备重新上线后，任务面板会自动清空。", kicker: "SERIAL HANDOFF" }
      : { title: "设备即将切换到 Recovery", message: "网页连接会暂时中断；这是完整 OTA 的预期阶段，请保持供电并观察串口。", kicker: "REBOOT TO RECOVERY" },
    system_reboot_requested: state.systemRebootOffline
      ? { title: "设备正在重新启动", message: "网络服务暂时离线，页面正在自动等待设备重新上线。", kicker: "SYSTEM REBOOT ACTIVE" }
      : { title: "安全重启请求已接受", message: "U-Boot/Boot 已完成写入与回读校验，设备即将重新启动。", kicker: "REBOOT REQUESTED" },
    system_reboot_failed: { title: "镜像已写入，但设备重启未执行", message: "写入与回读校验结果仍然有效；请查看错误后重新尝试安全重启。", kicker: "REBOOT RETRY AVAILABLE" },
    dry_run_complete: { title: "演练完成，未写入任何分区", message: "镜像格式、容量和设备映射检查已通过。", kicker: "DRY RUN COMPLETE" },
    succeeded: { title: "写入与回读校验完成", message: "设备返回的镜像哈希与上传文件一致。", kicker: "UPDATE COMPLETE" },
    failed: { title: "升级任务已停止", message: "系统已按 fail-closed 策略停止后续写入，请查看错误和事件记录。", kicker: "UPDATE STOPPED" },
    cancelled: { title: "升级任务已取消", message: "未继续执行写入。", kicker: "UPDATE CANCELLED" },
  };
  return copies[job.status] || { title: "正在处理升级任务", message: "请等待设备返回下一阶段状态。", kicker: "UPGRADE MISSION" };
}

function stagesFor(job) {
  if (job.target === "updateimg") return updateimgStages;
  if (job.target === "rootfs" || job.target === "bundle") return rootfsStages;
  return directStages;
}

function stageIndexFor(job) {
  const recovery = recoveryTargets.has(job.target);
  if (recovery) {
    return ({
      uploading: state.uploadComplete ? 1 : 0,
      awaiting_confirmation: 1,
      preparing_recovery: 2,
      recovery_prepared: 2,
      reboot_requested: 3,
      dry_run_complete: 3,
      succeeded: 3,
      failed: 1,
      cancelled: 0,
    })[job.status] ?? 0;
  }
  return ({
    uploading: state.uploadComplete ? 1 : 0,
    awaiting_confirmation: 1,
    flashing: 2,
    verifying: 3,
    system_reboot_requested: 4,
    system_reboot_failed: 4,
    dry_run_complete: 4,
    succeeded: 4,
    failed: 1,
    cancelled: 0,
  })[job.status] ?? 0;
}

function renderStageRail(job) {
  const stages = stagesFor(job);
  const index = stageIndexFor(job);
  const recovery = recoveryTargets.has(job.target);
  const handedOff = recovery && state.recoveryOffline;
  $("stageBlock").classList.toggle("handoff-only", handedOff);
  $("stageRail").classList.toggle("hidden", handedOff);
  $("flashProgress").classList.toggle("hidden", handedOff);
  $("stageTitle").textContent = handedOff
    ? "Web 阶段已结束"
    : job.target === "updateimg" ? "完整系统 OTA 流程" : `${targetLabel(job.target)}流程`;
  $("stageCounter").textContent = handedOff ? "离线阶段不在 Web 显示" : `阶段 ${index + 1} / ${stages.length}`;
  $("stageRail").replaceChildren(...stages.map((stage, position) => {
    const item = document.createElement("li");
    const serialOnly = stage.channel === "SERIAL";
    const reviewOnly = stage.channel === "REVIEW";
    const webConfirmable = !serialOnly && !reviewOnly;
    const done = webConfirmable && (position < index || (position === index && ["succeeded", "dry_run_complete", "system_reboot_requested"].includes(job.status)));
    if (serialOnly) item.classList.add("serial-only");
    if (reviewOnly) item.classList.add("review-only");
    if (done) item.classList.add("done");
    if (position === index && !done) item.classList.add(["failed", "system_reboot_failed"].includes(job.status) ? "failed" : "active");
    if (position === index) item.setAttribute("aria-current", "step");
    const number = document.createElement("span");
    number.className = "stage-number";
    number.textContent = String(position + 1).padStart(2, "0");
    const channel = document.createElement("span");
    channel.className = "stage-channel";
    channel.textContent = stage.channel || "WEB";
    const label = document.createElement("b");
    label.textContent = stage.label;
    const detail = document.createElement("small");
    detail.textContent = stage.detail;
    item.append(number, channel, label, detail);
    return item;
  }));

  $("serialNotice").classList.toggle("hidden", !recovery);
  if (recovery) {
    if (state.recoveryOffline) {
      $("serialNoticeTitle").textContent = "Web 跟踪已停止，请只查看串口终端";
      $("serialNoticeText").textContent = "页面不再显示阶段动画，也不会推断写入、checking、cleanup 或重启是否完成；设备重新上线后任务区自动重置。";
    } else {
      $("serialNoticeTitle").textContent = "请保持串口终端打开";
      $("serialNoticeText").textContent = "Web 只负责上传、校验和交接 Recovery。交接后的分区写入、校验、清理与重启结果仅以串口为准。";
    }
  }
  let progress = 0;
  let progressValue = "0%";
  let label = "升级流程";
  let hint = "任务启动后显示流程进度。";
  let progressKnown = true;
  if (job.status === "uploading") {
    progress = state.uploadActive ? state.uploadPercent : Number(job.progress) || 0;
    progressValue = `${progress}%`;
    label = state.uploadComplete ? "镜像已接收 · 服务端校验中" : "镜像上传进度";
    hint = state.uploadComplete ? "上传已完成，设备正在解析镜像并执行安全检查。" : "这里显示浏览器到设备的真实传输百分比。";
  } else if (recovery) {
    progress = Math.round((index / Math.max(stages.length - 1, 1)) * 100);
    progressValue = `${index + 1} / ${stages.length}`;
    label = "流程阶段（非写入百分比）";
    if (state.recoveryOffline) {
      progressKnown = false;
      progressValue = "请查看串口";
      label = "Recovery 离线执行 · Web 不作完成判定";
      hint = "分区写入、checking 和 cleanup 结果仅以串口输出为准；请勿根据此页面判断升级完成。";
    } else if (job.status === "reboot_requested") {
      hint = "设备即将重启；进入 Recovery 后页面会暂时离线，并持续自动等待回连。";
    } else {
      hint = "这里显示 Web 可确认的流程阶段；Recovery 分区级写入百分比以串口为准。";
    }
  } else {
    progress = ["succeeded", "system_reboot_failed", "dry_run_complete", "system_reboot_requested"].includes(job.status) ? 100 : Number(job.progress) || 0;
    progressValue = `${progress}%`;
    label = job.status === "system_reboot_requested"
      ? "写入与回读已完成 · 正在重启"
      : job.status === "verifying" ? "回读校验进度" : "设备写入进度";
    hint = job.status === "system_reboot_requested"
      ? "设备正在切换启动链路，网页会自动等待服务重新上线。"
      : "单分区任务显示设备返回的实际写入与回读进度。";
  }
  if (progressKnown) {
    $("flashBar").value = Math.max(0, Math.min(100, progress));
  } else {
    $("flashBar").removeAttribute("value");
  }
  $("flashPercent").textContent = progressValue;
  $("progressLabel").textContent = label;
  $("progressHint").textContent = hint;
  $("flashProgress").classList.toggle("offline", state.recoveryOffline || state.systemRebootOffline || ["reboot_requested", "system_reboot_requested"].includes(job.status));
  $("flashProgress").classList.toggle("unverified", recovery && state.recoveryOffline);
  $("flashProgress").classList.toggle("failed", ["failed", "system_reboot_failed"].includes(job.status));
}

function validationItems(validation) {
  if (Array.isArray(validation?.items)) return validation.items.filter(Boolean);
  const match = validation?.summary?.match(/：\[([^\]]+)\]/);
  return match ? match[1].trim().split(/\s+/).filter(Boolean) : [];
}

function renderPayload(job) {
  const items = validationItems(job.validation);
  const show = job.target === "updateimg" && items.length > 0;
  $("payloadBlock").classList.toggle("hidden", !show);
  $("payloadCount").textContent = `${items.length} ITEMS`;
  $("payloadList").replaceChildren(...items.map((name) => {
    const item = document.createElement("li");
    item.textContent = name;
    return item;
  }));
}

function renderArtifacts(job) {
  const artifacts = Array.isArray(job.artifacts) ? job.artifacts : [];
  $("artifactBlock").classList.toggle("hidden", artifacts.length === 0);
  $("artifactList").replaceChildren(...artifacts.map((artifact) => {
    const item = document.createElement("li");
    const target = document.createElement("b");
    target.textContent = artifact.target || "--";
    const hash = document.createElement("code");
    hash.textContent = artifact.sha256 || "--";
    const size = document.createElement("small");
    size.textContent = formatBytes(artifact.size);
    item.append(target, hash, size);
    return item;
  }));
}

function renderJob(job) {
  if (!job) {
    state.lastJob = null;
    state.recoveryPending = false;
    $("emptyState").classList.remove("hidden");
    $("jobView").classList.add("hidden");
    $("jobStatus").textContent = "空闲";
    $("jobStatus").className = "job-status idle";
    $("machineState").textContent = "空闲";
    $("changeTokenButton").disabled = false;
    lockTargetControls(false);
    return;
  }
  state.lastJob = job;
  const recoveryBusy = recoveryInProgress(job);
  state.recoveryPending = recoveryBusy;
  if (terminalStatuses.has(job.status)) clearLegacyPendingJob();

  $("emptyState").classList.add("hidden");
  $("jobView").classList.remove("hidden");
  $("jobTarget").textContent = targetLabel(job.target);
  $("jobFilename").textContent = job.filename || "--";
  $("jobSize").textContent = formatBytes(job.size);
  $("jobHash").textContent = job.sha256 || "上传完成后生成";
  $("jobDevice").textContent = job.device || "--";
  $("jobResolvedDevice").textContent = job.resolved_device || "--";
  $("jobParentDevice").textContent = job.parent_device || "--";
  $("recoveryPhase").textContent = recoveryPhaseText(job.recovery_phase);
  $("readbackHash").textContent = job.readback_sha256 || "--";
  $("jobValidation").textContent = job.validation?.summary || "等待设备完成格式与兼容性检查";
  $("validationTitle").textContent = job.validation?.detected_type === "rockchip-update-img"
    ? "完整系统包结构已通过"
    : job.validation?.detected_type ? `${job.validation.detected_type} 镜像已识别` : "等待检查";
  renderPayload(job);
  renderArtifacts(job);
  renderStageRail(job);

  const mission = missionCopy(job);
  $("missionCode").textContent = workflowProfiles[job.target]?.code || "JOB";
  $("missionKicker").textContent = mission.kicker;
  $("missionTitle").textContent = mission.title;
  $("missionMessage").textContent = mission.message;
  const missionClass = ["failed", "system_reboot_failed"].includes(job.status)
    ? "failed"
    : ["succeeded", "dry_run_complete"].includes(job.status)
      ? "success"
      : state.recoveryOffline ? "recovery" : activeStatuses.has(job.status) ? "active" : "";
  $("missionBanner").className = `mission-banner ${missionClass}`.trim();

  const dangerous = ["flashing", "verifying", "preparing_recovery", "recovery_prepared", "reboot_requested"].includes(job.status);
  $("powerWarning").classList.toggle("hidden", !dangerous);
  $("connectionBadge").className = `link-state ${state.recoveryOffline ? "recovery" : "online"}`;
  $("connectionBadge").textContent = state.recoveryOffline ? "后续请查看串口" : "设备在线";

  const status = statusText(job.status);
  $("jobStatus").textContent = status;
  const statusClass = ["succeeded", "dry_run_complete"].includes(job.status)
    ? "success"
    : ["failed", "system_reboot_failed"].includes(job.status) ? "failed" : activeStatuses.has(job.status) ? "active" : "idle";
  $("jobStatus").className = `job-status ${statusClass}`;
  $("machineState").textContent = status;
  $("changeTokenButton").disabled = activeStatuses.has(job.status);
  lockTargetControls(activeStatuses.has(job.status));
  $("jobError").textContent = job.error || "";
  $("jobError").classList.toggle("hidden", !job.error);
  maybePromptForDirectReboot(job);
}

function showRecoveryReturnNotice() {
  clearLegacyPendingJob();
  state.recoveryPending = false;
  state.recoveryOffline = false;
  renderJob(null);
  resetFile();
  $("emptyState").classList.remove("critical");
  $("emptyState").classList.add("returned");
  $("standbyLabel").textContent = "ONLINE";
  $("emptyTitle").textContent = "设备已重新上线，任务面板已重置";
  $("emptyDescription").textContent = "Web 不保留或判断 Recovery 离线烧写结果。请以串口完成提示和当前系统版本为准；现在可以开始新的升级任务。";
  $("emptySequence").replaceChildren(...["任务状态已清空", "结果以串口为准"].map((label) => {
    const item = document.createElement("li");
    item.textContent = label;
    return item;
  }));
}

async function refreshStatus() {
  try {
    const wasOffline = state.recoveryOffline;
    const recoveryWasPending = state.recoveryPending && recoveryTargets.has(state.lastJob?.target);
    const status = await api("/api/v1/status");
    state.recoveryOffline = false;
    const previous = state.lastJob?.status;
    if (state.systemRebootPending && ["failed", "system_reboot_failed"].includes(status.job?.status)) {
      state.systemRebootPending = false;
      state.systemRebootOffline = false;
      state.rebootPromptJob = status.job;
      setConnectionMode("online", "设备在线");
      renderJob(status.job);
      setRebootDialogMode("error", status.job.error || "设备拒绝了重启请求。");
      logEvent(`设备重启未执行：${status.job.error || "未知错误"}`);
      return;
    }
    if (state.systemRebootPending && !status.job) {
      state.systemRebootPending = false;
      state.systemRebootOffline = false;
      setConnectionMode("online", "设备已重新上线");
      renderJob(null);
      setRebootDialogMode("complete");
      logEvent("设备已完成重启并重新上线");
      return;
    }
    if (state.systemRebootPending) {
      state.systemRebootOffline = false;
      setConnectionMode("rebooting", "准备重新启动");
    } else {
      setConnectionMode("online", "设备在线");
    }
    if (status.job) {
      renderJob(status.job);
      if (previous !== status.job.status && terminalStatuses.has(status.job.status)) {
        logEvent(`${status.job.filename}：${statusText(status.job.status)}`);
      }
      return;
    }
    if (recoveryWasPending) {
      showRecoveryReturnNotice();
      if (wasOffline) logEvent("设备已重新上线；Web 任务面板已自动重置，离线结果请核对串口");
      return;
    }
    renderJob(null);
  } catch (error) {
    if (error instanceof AuthenticationError) {
      requireAuthentication("Token 已失效或设备配置已经更改，请重新验证。", "error");
      return;
    }
    if (state.systemRebootPending) {
      const firstOffline = !state.systemRebootOffline;
      state.systemRebootOffline = true;
      setConnectionMode("rebooting", "设备正在重启");
      $("machineState").textContent = "等待设备重新上线";
      if (state.lastJob) renderJob(state.lastJob);
      if (!$("rebootDialog").hidden) setRebootDialogMode("waiting");
      if (firstOffline) logEvent("设备连接已中断，正在等待重启完成");
      return;
    }
    const pending = state.recoveryPending ? state.lastJob : null;
    if (pending) {
      const firstOffline = !state.recoveryOffline;
      state.recoveryOffline = true;
      state.recoveryPending = true;
      setConnectionMode("recovery", "Recovery 阶段 · 查看串口");
      renderJob(pending);
      if (firstOffline) logEvent("设备已进入 Recovery；Web 跟踪已停止，后续结果仅查看串口");
      return;
    }
    if (state.connectionMode !== "offline") {
      setConnectionMode("offline", "设备连接失败");
      $("machineState").textContent = "连接失败";
      if (error.message !== "设备访问令牌与服务配置不一致") logEvent(error.message);
    }
  }
}

function startPolling() {
  stopPolling();
  if (!state.authenticated || !state.token) return;
  state.pollTimer = setInterval(refreshStatus, 1000);
}

$("authForm").addEventListener("submit", authenticate);
$("toggleTokenButton").addEventListener("click", () => setTokenVisible($("tokenInput").type === "password"));
$("changeTokenButton").addEventListener("click", () => requireAuthentication("请输入新的设备访问 Token。"));
$("rebootLaterButton").addEventListener("click", () => closeRebootDialog());
$("rebootNowButton").addEventListener("click", requestSystemReboot);
$("rebootDoneButton").addEventListener("click", () => closeRebootDialog());
$("rebootDialog").addEventListener("keydown", handleRebootDialogKeydown);
document.querySelectorAll('input[name="target"]').forEach((input) => input.addEventListener("change", (event) => {
  state.target = event.target.value;
  updateTarget();
}));
$("imageInput").addEventListener("change", (event) => chooseFiles(event.target.files));
$("uploadButton").addEventListener("click", uploadImage);
$("clearLogButton").addEventListener("click", () => $("eventList").replaceChildren());
const dropzone = $("dropzone");
["dragenter", "dragover"].forEach((type) => dropzone.addEventListener(type, (event) => {
  event.preventDefault();
  dropzone.classList.add("dragging");
}));
["dragleave", "drop"].forEach((type) => dropzone.addEventListener(type, (event) => {
  event.preventDefault();
  dropzone.classList.remove("dragging");
}));
dropzone.addEventListener("drop", (event) => chooseFiles(event.dataTransfer.files));

clearLegacyPendingJob();
requireAuthentication();
