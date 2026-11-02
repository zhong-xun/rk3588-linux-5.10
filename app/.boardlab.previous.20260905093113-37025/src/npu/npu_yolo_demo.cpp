#include "npu_yolo_demo.h"

#include "npu_load_reader.h"

#include "../boardprofile.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QSaveFile>
#include <QStringList>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(BOARDLAB_HAS_NPU_YOLO)
#include "detector/npu_yolo_detector.h"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/videoio.hpp>
#endif

namespace {

// The exported RKNN models are target-specific.  Keep the model name, real
// hardware topology and execution policy together so a new product profile
// cannot accidentally load an RK3588 binary or inherit its three-core policy.
struct YoloSocConfig {
    const char *id;
    const char *displayName;
    const char *modelFileName;
    int workerCount;
    int telemetryCoreCount;
    bool pinWorkersToCores;
};

constexpr std::array<YoloSocConfig, 4> kYoloSocConfigs {{
    {"rk3562", "RK3562", "yolov8n_rk3562.rknn", 1, 1, false},
    {"rk3568", "RK3568", "yolov8n_rk3568.rknn", 1, 1, false},
    {"rk3576", "RK3576", "yolov8n_rk3576.rknn", 1, 2, false},
    {"rk3588", "RK3588", "yolov8n_rk3588.rknn", 3, 3, true},
}};

const YoloSocConfig *yoloSocConfigForId(const QString &id)
{
    for (const YoloSocConfig &config : kYoloSocConfigs) {
        if (id.compare(QLatin1String(config.id), Qt::CaseInsensitive) == 0) {
            return &config;
        }
    }
    return nullptr;
}

QString yoloSocIdForFamily(const QString &socFamily)
{
    const QString normalized = socFamily.trimmed().toLower();
    // RK3588S uses the same NPU model and three-core execution policy as
    // RK3588. Check the common prefix after the other independent platforms.
    if (normalized.contains(QStringLiteral("rk3562"))) {
        return QStringLiteral("rk3562");
    }
    if (normalized.contains(QStringLiteral("rk3568"))) {
        return QStringLiteral("rk3568");
    }
    if (normalized.contains(QStringLiteral("rk3576"))) {
        return QStringLiteral("rk3576");
    }
    if (normalized.contains(QStringLiteral("rk3588"))) {
        return QStringLiteral("rk3588");
    }
    return QString();
}

const YoloSocConfig *yoloSocConfigForProfile(const BoardProfile *boardProfile)
{
    if (!boardProfile || !boardProfile->hasCapability(QStringLiteral("npuyolodemo"))) {
        return nullptr;
    }
    return yoloSocConfigForId(yoloSocIdForFamily(boardProfile->socFamily()));
}

QVariantList npuCoreIds(const YoloSocConfig *config)
{
    QVariantList ids;
    if (!config) {
        return ids;
    }
    for (int coreId = 0; coreId < config->telemetryCoreCount; ++coreId) {
        ids.append(coreId);
    }
    return ids;
}

QVariantList emptyFrameCounters(const YoloSocConfig *config)
{
    QVariantList counters;
    if (!config) {
        return counters;
    }
    for (int workerIndex = 0; workerIndex < config->workerCount; ++workerIndex) {
        counters.append(0);
    }
    return counters;
}

QString executionMode(const YoloSocConfig &config)
{
    return config.pinWorkersToCores
        ? QStringLiteral("three-core-parallel")
        : QStringLiteral("single-context-auto");
}

QString executionSummary(const YoloSocConfig &config)
{
    return config.pinWorkersToCores
        ? QStringLiteral("three independent RKNN contexts pinned to NPU 0, NPU 1 and NPU 2")
        : QStringLiteral("one RKNN context with automatic NPU core scheduling");
}

QString workerLabel(const YoloSocConfig &config, int workerIndex)
{
    return config.pinWorkersToCores
        ? QStringLiteral("NPU %1").arg(workerIndex)
        : QStringLiteral("automatic NPU worker");
}

QString workerSummary(const YoloSocConfig &config)
{
    return config.pinWorkersToCores
        ? QStringLiteral("three pinned NPU workers")
        : QStringLiteral("one automatic NPU worker");
}

QString frameDistributionSummary(const YoloSocConfig &config, const QVariantList &counters)
{
    QStringList parts;
    for (int workerIndex = 0; workerIndex < config.workerCount; ++workerIndex) {
        parts.append(QStringLiteral("%1: %2")
                         .arg(workerLabel(config, workerIndex))
                         .arg(counters.value(workerIndex).toInt()));
    }
    return parts.join(QStringLiteral(", "));
}

QString isoNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

QString absolutePath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString firstExistingPath(const QString &root, const QStringList &relativeCandidates)
{
    for (const QString &relative : relativeCandidates) {
        const QString candidate = QDir(root).absoluteFilePath(relative);
        const QFileInfo candidateInfo(candidate);
        if (candidateInfo.exists() && candidateInfo.isFile() && candidateInfo.isReadable()) {
            return absolutePath(candidate);
        }
    }
    return QString();
}

QVariantMap assetPathsForRoot(const QString &root, const QString &source,
                              const YoloSocConfig *config)
{
    QVariantMap assets;
    const QFileInfo rootInfo(root);
    const QString absoluteRoot = rootInfo.exists() ? absolutePath(root) : rootInfo.absoluteFilePath();
    QStringList modelCandidates;
    if (config) {
        const QString modelFileName = QLatin1String(config->modelFileName);
        // This relative layout is the published BoardLab/topeet_test
        // contract.  Do not add old data/ or flat-file fallbacks here: every
        // supported rootfs must select the same SoC-specific asset set.
        modelCandidates = QStringList{QStringLiteral("models/") + modelFileName};
    }
    const QString model = firstExistingPath(absoluteRoot, modelCandidates);
    const QString video = firstExistingPath(absoluteRoot,
                                            {QStringLiteral("videos/person-bicycle-car-detection.mp4")});
    QStringList labelCandidates{QStringLiteral("coco_80_labels.txt")};
    if (source == QStringLiteral("source-assets")) {
        // The source repository keeps the label under labels/ for clarity;
        // CMake flattens it into the published installed layout above.
        labelCandidates.append(QStringLiteral("labels/coco_80_labels.txt"));
    }
    const QString labels = firstExistingPath(absoluteRoot, labelCandidates);

    assets.insert(QStringLiteral("source"), source);
    assets.insert(QStringLiteral("root"), absoluteRoot);
    assets.insert(QStringLiteral("platformId"), config ? QLatin1String(config->id) : QString());
    assets.insert(QStringLiteral("modelTarget"), config ? QLatin1String(config->displayName) : QString());
    assets.insert(QStringLiteral("modelFileName"), config ? QLatin1String(config->modelFileName) : QString());
    assets.insert(QStringLiteral("modelPath"), model);
    assets.insert(QStringLiteral("modelExplicitTarget"), !model.isEmpty());
    // Keep this API field for existing QML/Web clients.  New BoardLab and the
    // standalone CLI both reject legacy or cross-SoC model substitutions.
    assets.insert(QStringLiteral("modelFallback"), false);
    assets.insert(QStringLiteral("videoPath"), video);
    assets.insert(QStringLiteral("labelsPath"), labels);
    assets.insert(QStringLiteral("modelAvailable"), !model.isEmpty());
    assets.insert(QStringLiteral("videoAvailable"), !video.isEmpty());
    assets.insert(QStringLiteral("labelsAvailable"), !labels.isEmpty());
    assets.insert(QStringLiteral("available"), config && !model.isEmpty() && !video.isEmpty() && !labels.isEmpty());
    return assets;
}

QVariantMap resolveAssets(const YoloSocConfig *config)
{
    struct RootCandidate {
        QString path;
        QString source;
    };

    QList<RootCandidate> candidates;
    const QString environmentRoot = QString::fromLocal8Bit(qgetenv("BOARDLAB_NPU_ASSET_ROOT")).trimmed();
    if (!environmentRoot.isEmpty()) {
        candidates.append({environmentRoot, QStringLiteral("BOARDLAB_NPU_ASSET_ROOT")});
    }
    candidates.append({QStringLiteral("/opt/boardlab/share/boardlab/npu"), QStringLiteral("installed")});
    candidates.append({QStringLiteral("/usr/local/share/boardlab/npu"), QStringLiteral("installed")});
    candidates.append({QStringLiteral("/usr/share/boardlab/npu"), QStringLiteral("installed")});

    // Source and build-tree execution are first-class development modes.  A
    // freshly built binary therefore uses the exact assets that will be
    // installed, without depending on a sibling project or an environment
    // override.
    candidates.append({QDir::current().absoluteFilePath(QStringLiteral("assets/npu")),
                       QStringLiteral("source-assets")});

    const QString applicationDir = QCoreApplication::applicationDirPath();
    if (!applicationDir.isEmpty()) {
        candidates.append({QDir(applicationDir).absoluteFilePath(QStringLiteral("../assets/npu")),
                           QStringLiteral("source-assets")});
    }

    QVariantMap firstIncomplete;
    for (const RootCandidate &candidate : candidates) {
        const QVariantMap assets = assetPathsForRoot(candidate.path, candidate.source, config);
        if (assets.value(QStringLiteral("available")).toBool()) {
            return assets;
        }
        if (firstIncomplete.isEmpty() && QFileInfo(candidate.path).exists()) {
            firstIncomplete = assets;
        }
    }

    if (!firstIncomplete.isEmpty()) {
        return firstIncomplete;
    }
    QVariantMap missing;
    missing.insert(QStringLiteral("source"), QStringLiteral("none"));
    missing.insert(QStringLiteral("root"), QString());
    missing.insert(QStringLiteral("platformId"), config ? QLatin1String(config->id) : QString());
    missing.insert(QStringLiteral("modelTarget"), config ? QLatin1String(config->displayName) : QString());
    missing.insert(QStringLiteral("modelFileName"), config ? QLatin1String(config->modelFileName) : QString());
    missing.insert(QStringLiteral("modelPath"), QString());
    missing.insert(QStringLiteral("modelExplicitTarget"), false);
    missing.insert(QStringLiteral("modelFallback"), false);
    missing.insert(QStringLiteral("videoPath"), QString());
    missing.insert(QStringLiteral("labelsPath"), QString());
    missing.insert(QStringLiteral("modelAvailable"), false);
    missing.insert(QStringLiteral("videoAvailable"), false);
    missing.insert(QStringLiteral("labelsAvailable"), false);
    missing.insert(QStringLiteral("available"), false);
    return missing;
}

QString deviceTreeCompatible()
{
    const QString configuredRoot = QString::fromLocal8Bit(qgetenv("BOARDLAB_DEVICE_TREE_ROOT")).trimmed();
    const QString deviceTreeRoot = configuredRoot.isEmpty()
        ? QStringLiteral("/proc/device-tree")
        : configuredRoot;
    QFile compatible(QDir(deviceTreeRoot).filePath(QStringLiteral("compatible")));
    if (!compatible.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QByteArray contents = compatible.readAll();
    contents.replace('\0', '\n');
    return QString::fromUtf8(contents).trimmed();
}

bool hasRknpuAccessDevice()
{
    // Older Rockchip kernels expose /dev/rknpu.  Newer DRM-backed RK3588
    // drivers expose the NPU as a render node instead, so requiring only the
    // legacy path would reject a healthy NPU before RKNN gets a chance to
    // initialize it.  Pair the render node with the NPU's own devfreq node so
    // an unrelated GPU render node cannot satisfy this check.
    if (QFileInfo::exists(QStringLiteral("/dev/rknpu"))) {
        return true;
    }

    const QDir dri(QStringLiteral("/dev/dri"));
    const bool hasRenderNode = !dri.entryList(QStringList() << QStringLiteral("renderD*"),
                                              QDir::System | QDir::Files).isEmpty();
    if (!hasRenderNode) {
        return false;
    }

    const QDir devfreq(QStringLiteral("/sys/class/devfreq"));
    const QFileInfoList entries = devfreq.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        if (entry.fileName().contains(QStringLiteral("npu"), Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QVariantMap runtimeInfo(const YoloSocConfig *expectedConfig)
{
    QVariantMap runtime;
#if defined(BOARDLAB_HAS_NPU_YOLO)
    constexpr bool compiled = true;
#else
    constexpr bool compiled = false;
#endif
    const QString compatible = deviceTreeCompatible();
    const QString detectedPlatformId = yoloSocIdForFamily(compatible);
    const YoloSocConfig *detectedConfig = yoloSocConfigForId(detectedPlatformId);
    const bool rknpu = hasRknpuAccessDevice();
    const bool debugNode = QFileInfo::exists(QStringLiteral("/sys/kernel/debug/rknpu"));
    runtime.insert(QStringLiteral("compiled"), compiled);
    runtime.insert(QStringLiteral("compatible"), compatible);
    runtime.insert(QStringLiteral("rk3588"), detectedPlatformId == QStringLiteral("rk3588"));
    runtime.insert(QStringLiteral("platformId"), detectedPlatformId);
    runtime.insert(QStringLiteral("expectedPlatformId"), expectedConfig ? QLatin1String(expectedConfig->id) : QString());
    runtime.insert(QStringLiteral("supportedPlatform"), detectedConfig != nullptr);
    runtime.insert(QStringLiteral("profileMatchesDeviceTree"), expectedConfig
                   && detectedConfig
                   && QString::fromLatin1(expectedConfig->id) == detectedPlatformId);
    runtime.insert(QStringLiteral("rknpuDevice"), rknpu);
    runtime.insert(QStringLiteral("rknpuDebugNode"), debugNode);
    runtime.insert(QStringLiteral("platform"), detectedConfig
                   ? QLatin1String(detectedConfig->displayName)
                   : QStringLiteral("unsupported"));
    return runtime;
}

QString previewDirectory()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (root.trimmed().isEmpty()) {
        root = QDir::tempPath() + QStringLiteral("/boardlab");
    }
    const QString path = QDir(root).absoluteFilePath(QStringLiteral("npu"));
    QDir().mkpath(path);
    return absolutePath(path);
}

QString previewUrlForPath(const QString &path, qulonglong serial)
{
    QUrl url = QUrl::fromLocalFile(path);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("v"), QString::number(serial));
    url.setQuery(query);
    return url.toString(QUrl::FullyEncoded);
}

QVariantMap initialNpuLoad(const YoloSocConfig *config)
{
    QVariantMap load;
    load.insert(QStringLiteral("available"), false);
    load.insert(QStringLiteral("source"), QStringLiteral("rknpu-debugfs"));
    load.insert(QStringLiteral("message"), QStringLiteral("Waiting for an active YOLOv8 run before sampling RKNPU load"));
    load.insert(QStringLiteral("sampleIntervalMs"), 1000);
    load.insert(QStringLiteral("executionMode"), config ? executionMode(*config) : QStringLiteral("unavailable"));
    load.insert(QStringLiteral("assignedCoreIds"), config && config->pinWorkersToCores
                ? npuCoreIds(config) : QVariantList());
    load.insert(QStringLiteral("coreIds"), npuCoreIds(config));
    load.insert(QStringLiteral("cores"), config
                ? NpuLoadReader::unavailableCores(config->telemetryCoreCount) : QVariantList());
    load.insert(QStringLiteral("averagePercent"), -1.0);
    load.insert(QStringLiteral("updatedAt"), QString());
    return load;
}

void appendNpuLoadResult(QVariantMap *state, const NpuLoadReading &reading,
                         const YoloSocConfig &config)
{
    if (!state) {
        return;
    }

    QVariantMap load = state->value(QStringLiteral("npuLoad")).toMap();
    if (load.isEmpty()) {
        load = initialNpuLoad(&config);
    }
    load.insert(QStringLiteral("available"), reading.available);
    load.insert(QStringLiteral("source"), QStringLiteral("rknpu-debugfs"));
    load.insert(QStringLiteral("message"), reading.message);
    load.insert(QStringLiteral("sampleIntervalMs"), 1000);
    load.insert(QStringLiteral("executionMode"), executionMode(config));
    load.insert(QStringLiteral("assignedCoreIds"), config.pinWorkersToCores
                ? npuCoreIds(&config) : QVariantList());
    load.insert(QStringLiteral("coreIds"), npuCoreIds(&config));
    load.insert(QStringLiteral("cores"), reading.cores);
    load.insert(QStringLiteral("averagePercent"), reading.averagePercent);
    load.insert(QStringLiteral("updatedAt"), isoNow());
    state->insert(QStringLiteral("npuLoad"), load);

    if (!reading.available) {
        return;
    }

    QVariantMap sample;
    sample.insert(QStringLiteral("timestampMs"), QDateTime::currentMSecsSinceEpoch());
    for (const QVariant &coreValue : reading.cores) {
        const QVariantMap core = coreValue.toMap();
        const int coreId = core.value(QStringLiteral("core")).toInt();
        sample.insert(QStringLiteral("core%1").arg(coreId), core.value(QStringLiteral("percent")).toDouble());
    }
    QVariantList history = state->value(QStringLiteral("npuLoadHistory")).toList();
    history.append(sample);
    constexpr int kMaximumNpuLoadHistory = 60;
    while (history.size() > kMaximumNpuLoadHistory) {
        history.removeFirst();
    }
    state->insert(QStringLiteral("npuLoadHistory"), history);
}

#if defined(BOARDLAB_HAS_NPU_YOLO)
bool openSoftwareH264Video(const QString &path, cv::VideoCapture *capture)
{
    if (!capture) {
        return false;
    }

    // Rockchip's patched FFmpeg may auto-select h264_rkmpp. On older RGA
    // stacks an 8 GiB RK3588 can then hand RGA2 a dma-buf above 4 GiB and
    // corrupt kernel page accounting. Select FFmpeg's native H.264 decoder
    // for the fixed sample while keeping RKNN inference on the NPU.
    static std::mutex captureOptionsMutex;
    const std::lock_guard<std::mutex> locker(captureOptionsMutex);
    constexpr auto optionsName = "OPENCV_FFMPEG_CAPTURE_OPTIONS";
    const bool hadOptions = qEnvironmentVariableIsSet(optionsName);
    const QByteArray previousOptions = qgetenv(optionsName);
    if (!qputenv(optionsName, QByteArrayLiteral("video_codec;h264"))) {
        return false;
    }

    const auto restoreOptions = [&]() {
        if (hadOptions) {
            qputenv(optionsName, previousOptions);
        } else {
            qunsetenv(optionsName);
        }
    };
    try {
        const bool opened = capture->open(path.toStdString(), cv::CAP_FFMPEG);
        restoreOptions();
        return opened;
    } catch (...) {
        restoreOptions();
        throw;
    }
}

bool writeJpegAtomically(const QString &path, const cv::Mat &frame)
{
    if (path.isEmpty() || frame.empty()) {
        return false;
    }

    std::vector<unsigned char> encoded;
    const std::vector<int> encodingOptions {cv::IMWRITE_JPEG_QUALITY, 82};
    if (!cv::imencode(".jpg", frame, encoded, encodingOptions) || encoded.empty()) {
        return false;
    }

    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        return false;
    }
    const qint64 expected = static_cast<qint64>(encoded.size());
    if (output.write(reinterpret_cast<const char *>(encoded.data()), expected) != expected) {
        output.cancelWriting();
        return false;
    }
    return output.commit();
}
#endif

void appendLog(QVariantMap *state, const QString &line)
{
    if (!state || line.trimmed().isEmpty()) {
        return;
    }
    QString log = state->value(QStringLiteral("log")).toString();
    if (!log.isEmpty()) {
        log.append(QLatin1Char('\n'));
    }
    log.append(QStringLiteral("[%1] %2").arg(isoNow(), line.trimmed()));
    constexpr int kMaximumLogCharacters = 12000;
    if (log.size() > kMaximumLogCharacters) {
        log = log.right(kMaximumLogCharacters);
    }
    state->insert(QStringLiteral("log"), log);
}

bool terminalStatus(const QString &status)
{
    return status == QStringLiteral("completed")
        || status == QStringLiteral("failed")
        || status == QStringLiteral("stopped");
}

bool profileSupportsYoloDemo(const BoardProfile *boardProfile)
{
    // Capability is the product contract.  A matching SoC configuration is
    // also required so an arbitrary profile cannot select a model belonging to
    // another NPU generation.
    return yoloSocConfigForProfile(boardProfile) != nullptr;
}

QString profileUnavailableReason(const BoardProfile *boardProfile)
{
    if (!boardProfile) {
        return QStringLiteral("固定 YOLOv8 示例需要已验证的迅为开发板测试平台产品配置及匹配的 RKNN 模型");
    }

    if (boardProfile->hasCapability(QStringLiteral("npu"))
        && !boardProfile->hasCapability(QStringLiteral("npuyolodemo"))) {
        return QStringLiteral("NPU telemetry is available on %1, but the fixed YOLOv8 sample is enabled only for validated product boards with a matching RKNN model")
            .arg(boardProfile->displayName());
    }

    return QStringLiteral("固定 YOLOv8 示例需要已验证的 RK3562、RK3568、RK3576 或 RK3588 系列迅为开发板测试平台配置；当前配置为 %1")
        .arg(boardProfile->displayName());
}

QVariantMap discoveryState(const BoardProfile *boardProfile)
{
    const YoloSocConfig *config = yoloSocConfigForProfile(boardProfile);
    const QVariantMap assets = resolveAssets(config);
    const QVariantMap runtime = runtimeInfo(config);
    const bool compiled = runtime.value(QStringLiteral("compiled")).toBool();
    const bool profileMatchesDeviceTree = runtime.value(QStringLiteral("profileMatchesDeviceTree")).toBool();
    const bool rknpu = runtime.value(QStringLiteral("rknpuDevice")).toBool();
    const bool assetsAvailable = assets.value(QStringLiteral("available")).toBool();
    const bool profileSupported = profileSupportsYoloDemo(boardProfile);
    const QString modelTarget = config ? QLatin1String(config->displayName) : QStringLiteral("NPU");

    QString error;
    if (!profileSupported) {
        error = profileUnavailableReason(boardProfile);
    } else if (!compiled) {
        error = QStringLiteral("迅为开发板测试平台构建时未包含可选的 OpenCV 与 RKNN YOLOv8 运行时");
    } else if (!profileMatchesDeviceTree) {
        error = QStringLiteral("The validated %1 profile does not match the running device-tree NPU platform")
                    .arg(modelTarget);
    } else if (!rknpu) {
        error = QStringLiteral("%1 NPU device is not available (/dev/rknpu or an NPU-backed DRM render node)")
                    .arg(modelTarget);
    } else if (!assetsAvailable) {
        error = QStringLiteral("YOLOv8 model, COCO labels, or the fixed sample MP4 is missing");
    }

    QVariantMap state;
    state.insert(QStringLiteral("available"), error.isEmpty());
    state.insert(QStringLiteral("status"), error.isEmpty() ? QStringLiteral("ready") : QStringLiteral("unavailable"));
    state.insert(QStringLiteral("error"), error);
    state.insert(QStringLiteral("message"), error.isEmpty()
                 ? QStringLiteral("%1 YOLOv8 sample is ready").arg(modelTarget)
                 : error);
    state.insert(QStringLiteral("mode"), QStringLiteral("yolov8-rknn"));
    state.insert(QStringLiteral("videoLoop"), false);
    state.insert(QStringLiteral("running"), false);
    state.insert(QStringLiteral("canStart"), error.isEmpty());
    state.insert(QStringLiteral("canStop"), false);
    state.insert(QStringLiteral("profileSupported"), profileSupported);
    state.insert(QStringLiteral("profileId"), boardProfile ? boardProfile->id() : QString());
    state.insert(QStringLiteral("profileLabel"), boardProfile ? boardProfile->displayName() : QString());
    state.insert(QStringLiteral("platformId"), config ? QLatin1String(config->id) : QString());
    state.insert(QStringLiteral("platformLabel"), modelTarget);
    state.insert(QStringLiteral("assets"), assets);
    state.insert(QStringLiteral("runtime"), runtime);
    state.insert(QStringLiteral("progress"), 0.0);
    state.insert(QStringLiteral("progressPercent"), 0);
    state.insert(QStringLiteral("framesTotal"), 0);
    state.insert(QStringLiteral("framesProcessed"), 0);
    state.insert(QStringLiteral("detectionsTotal"), 0);
    state.insert(QStringLiteral("detectionsLastFrame"), 0);
    state.insert(QStringLiteral("inferenceMs"), 0.0);
    state.insert(QStringLiteral("averageInferenceMs"), 0.0);
    state.insert(QStringLiteral("fps"), 0.0);
    state.insert(QStringLiteral("elapsedMs"), 0);
    state.insert(QStringLiteral("executionMode"), config ? executionMode(*config) : QStringLiteral("unavailable"));
    state.insert(QStringLiteral("npuWorkerCount"), config ? config->workerCount : 0);
    state.insert(QStringLiteral("npuCoreIds"), npuCoreIds(config));
    state.insert(QStringLiteral("framesSubmitted"), 0);
    state.insert(QStringLiteral("framesQueued"), 0);
    state.insert(QStringLiteral("framesDropped"), 0);
    state.insert(QStringLiteral("framesByNpuCore"), emptyFrameCounters(config));
    state.insert(QStringLiteral("npuLoad"), initialNpuLoad(config));
    state.insert(QStringLiteral("npuLoadHistory"), QVariantList());
    state.insert(QStringLiteral("previewPath"), QString());
    state.insert(QStringLiteral("previewUrl"), QString());
    state.insert(QStringLiteral("previewSerial"), static_cast<qulonglong>(0));
    state.insert(QStringLiteral("resultPreviewPath"), QString());
    state.insert(QStringLiteral("resultPreviewUrl"), QString());
    state.insert(QStringLiteral("resultPreviewSerial"), static_cast<qulonglong>(0));
    state.insert(QStringLiteral("startedAt"), QString());
    state.insert(QStringLiteral("finishedAt"), QString());
    state.insert(QStringLiteral("updatedAt"), isoNow());
    state.insert(QStringLiteral("log"), QString());
    return state;
}

} // namespace

class NpuYoloDemo::Runner final : public QThread
{
public:
    Runner(const QVariantMap &initialState, NpuYoloDemo *owner)
        : m_state(initialState)
        , m_owner(owner)
    {
    }

    void requestStop()
    {
        m_stopRequested.store(true, std::memory_order_relaxed);
    }

    QVariantMap finalStatus() const
    {
        QMutexLocker locker(&m_finalMutex);
        return m_finalStatus;
    }

protected:
    void run() override
    {
#if defined(BOARDLAB_HAS_NPU_YOLO)
        runYoloDemo();
#else
        QVariantMap state = m_state;
        state.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
        state.insert(QStringLiteral("available"), false);
        state.insert(QStringLiteral("running"), false);
        state.insert(QStringLiteral("canStart"), false);
        state.insert(QStringLiteral("canStop"), false);
        state.insert(QStringLiteral("error"),
                     QStringLiteral("迅为开发板测试平台构建时未包含可选的 OpenCV 与 RKNN YOLOv8 运行时"));
        state.insert(QStringLiteral("message"), state.value(QStringLiteral("error")));
        state.insert(QStringLiteral("finishedAt"), isoNow());
        appendLog(&state, state.value(QStringLiteral("error")).toString());
        publishFinal(state);
#endif
    }

private:
    void publish(const QVariantMap &state)
    {
        const QPointer<NpuYoloDemo> owner = m_owner;
        if (!owner) {
            return;
        }
        QMetaObject::invokeMethod(owner.data(), [owner, state]() {
            if (owner) {
                owner->applyWorkerStatus(state);
            }
        }, Qt::QueuedConnection);
    }

    void publishFinal(QVariantMap state)
    {
        state.insert(QStringLiteral("updatedAt"), isoNow());
        {
            QMutexLocker locker(&m_finalMutex);
            m_finalStatus = state;
        }
        publish(state);
    }

#if defined(BOARDLAB_HAS_NPU_YOLO)
    void runYoloDemo()
    {
        QVariantMap state = m_state;
        const YoloSocConfig *config = yoloSocConfigForId(
            state.value(QStringLiteral("platformId")).toString());
        if (!config) {
            state.insert(QStringLiteral("status"), QStringLiteral("failed"));
            state.insert(QStringLiteral("message"), QStringLiteral("YOLOv8 demo failed"));
            state.insert(QStringLiteral("error"), QStringLiteral("No matching YOLOv8 NPU platform configuration is available"));
            state.insert(QStringLiteral("running"), false);
            state.insert(QStringLiteral("canStart"), true);
            state.insert(QStringLiteral("canStop"), false);
            state.insert(QStringLiteral("finishedAt"), isoNow());
            appendLog(&state, state.value(QStringLiteral("error")).toString());
            publishFinal(state);
            return;
        }
        const QVariantMap assets = state.value(QStringLiteral("assets")).toMap();
        const QString modelPath = assets.value(QStringLiteral("modelPath")).toString();
        const QString labelPath = assets.value(QStringLiteral("labelsPath")).toString();
        const QString videoPath = assets.value(QStringLiteral("videoPath")).toString();

        const qulonglong runSerial = state.value(QStringLiteral("previewSerial")).toULongLong();
        const QString previewPath = QDir(previewDirectory()).absoluteFilePath(
            QStringLiteral("yolov8-preview-%1.jpg").arg(runSerial));
        const QString resultPreviewPath = QDir(previewDirectory()).absoluteFilePath(
            QStringLiteral("yolov8-result-%1.jpg").arg(runSerial));
        state.insert(QStringLiteral("previewPath"), previewPath);
        state.insert(QStringLiteral("previewUrl"), QString());
        state.insert(QStringLiteral("status"), QStringLiteral("running"));
        state.insert(QStringLiteral("message"),
                     QStringLiteral("Loading the fixed %1 YOLOv8 model")
                         .arg(QLatin1String(config->displayName)));
        state.insert(QStringLiteral("running"), true);
        state.insert(QStringLiteral("canStart"), false);
        state.insert(QStringLiteral("canStop"), true);
        state.insert(QStringLiteral("startedAt"), isoNow());
        state.insert(QStringLiteral("updatedAt"), isoNow());
        state.insert(QStringLiteral("executionMode"), executionMode(*config));
        state.insert(QStringLiteral("npuWorkerCount"), config->workerCount);
        state.insert(QStringLiteral("npuCoreIds"), npuCoreIds(config));
        state.insert(QStringLiteral("framesQueued"), 0);
        state.insert(QStringLiteral("framesDropped"), 0);
        state.insert(QStringLiteral("framesByNpuCore"), emptyFrameCounters(config));
        state.insert(QStringLiteral("npuLoad"), initialNpuLoad(config));
        state.insert(QStringLiteral("npuLoadHistory"), QVariantList());
        appendLog(&state, QStringLiteral("Starting %1 YOLOv8 sample: %2")
                      .arg(QLatin1String(config->displayName), videoPath));
        publish(state);

        struct FrameTask {
            int frameNumber = 0;
            int assignedWorkerIndex = -1;
            cv::Mat frame;
        };
        struct FrameResult {
            int frameNumber = 0;
            int workerIndex = -1;
            cv::Mat frame;
            std::vector<boardlab::npu::Detection> detections;
            qint64 inferenceMs = 0;
        };
        struct Coordinator {
            std::mutex taskMutex;
            std::condition_variable taskReady;
            std::deque<FrameTask> tasks;
            bool inputClosed = false;
            bool cancelled = false;

            std::mutex resultMutex;
            std::condition_variable resultReady;
            std::map<int, FrameResult> results;
            std::deque<int> completedFrameNumbers;

            std::mutex lifecycleMutex;
            std::condition_variable lifecycleChanged;
            int initializedWorkers = 0;
            QString workerError;
        } coordinator;

        struct TelemetryCoordinator {
            std::mutex mutex;
            std::condition_variable changed;
            bool stopRequested = false;
            bool hasUnreadResult = false;
            NpuLoadReading latest;
        } telemetry;

        std::vector<std::thread> workers;
        std::thread telemetryWorker;
        auto requestCancellation = [&coordinator]() {
            {
                std::lock_guard<std::mutex> locker(coordinator.taskMutex);
                coordinator.cancelled = true;
                coordinator.inputClosed = true;
                coordinator.tasks.clear();
            }
            coordinator.taskReady.notify_all();
            coordinator.resultReady.notify_all();
        };
        auto closeInput = [&coordinator]() {
            {
                std::lock_guard<std::mutex> locker(coordinator.taskMutex);
                coordinator.inputClosed = true;
            }
            coordinator.taskReady.notify_all();
        };
        auto joinWorkers = [&workers]() {
            for (std::thread &worker : workers) {
                if (worker.joinable()) {
                    worker.join();
                }
            }
        };
        auto reportWorkerFailure = [&coordinator, &requestCancellation](const QString &message) {
            {
                std::lock_guard<std::mutex> locker(coordinator.lifecycleMutex);
                if (coordinator.workerError.isEmpty()) {
                    coordinator.workerError = message;
                }
            }
            requestCancellation();
            coordinator.lifecycleChanged.notify_all();
        };
        auto workerFailure = [&coordinator]() {
            std::lock_guard<std::mutex> locker(coordinator.lifecycleMutex);
            return coordinator.workerError;
        };
        auto startTelemetryWorker = [&telemetry, &telemetryWorker, config]() {
            if (telemetryWorker.joinable()) {
                return;
            }
            telemetryWorker = std::thread([&telemetry, config]() {
                while (true) {
                    const NpuLoadReading reading = NpuLoadReader::readPrivileged(
                        config->telemetryCoreCount);
                    {
                        std::lock_guard<std::mutex> locker(telemetry.mutex);
                        if (telemetry.stopRequested) {
                            return;
                        }
                        telemetry.latest = reading;
                        telemetry.hasUnreadResult = true;
                    }
                    telemetry.changed.notify_all();

                    std::unique_lock<std::mutex> locker(telemetry.mutex);
                    if (telemetry.changed.wait_for(locker, std::chrono::seconds(1), [&telemetry]() {
                            return telemetry.stopRequested;
                        })) {
                        return;
                    }
                }
            });
        };
        auto stopTelemetryWorker = [&telemetry, &telemetryWorker]() {
            {
                std::lock_guard<std::mutex> locker(telemetry.mutex);
                telemetry.stopRequested = true;
            }
            telemetry.changed.notify_all();
            if (telemetryWorker.joinable()) {
                telemetryWorker.join();
            }
        };
        auto takeTelemetryResult = [&telemetry](NpuLoadReading *reading) {
            if (!reading) {
                return false;
            }
            std::lock_guard<std::mutex> locker(telemetry.mutex);
            if (!telemetry.hasUnreadResult) {
                return false;
            }
            *reading = telemetry.latest;
            telemetry.hasUnreadResult = false;
            return true;
        };

        try {
            if (m_stopRequested.load(std::memory_order_relaxed)) {
                finishStopped(&state, QStringLiteral("Stopped before model initialization"));
                return;
            }

            state.insert(QStringLiteral("status"), QStringLiteral("initializing"));
            state.insert(QStringLiteral("message"),
                         QStringLiteral("Initialising %1").arg(executionSummary(*config)));
            state.insert(QStringLiteral("updatedAt"), isoNow());
            appendLog(&state, QStringLiteral("Initialising %1")
                      .arg(executionSummary(*config)));
            publish(state);

            const int workerCount = config->workerCount;
            constexpr size_t kMaximumQueuedFrames = 6;
            const int maximumInFlightFrames = workerCount + static_cast<int>(kMaximumQueuedFrames);
            for (int workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
                workers.emplace_back([this, &coordinator, &reportWorkerFailure, modelPath, labelPath, config, workerIndex]() {
                    try {
                        boardlab::npu::YoloV8Options options;
                        options.modelPath = modelPath.toStdString();
                        options.labelPath = labelPath.toStdString();
                        options.inputWidth = 640;
                        options.inputHeight = 640;
                        options.classCount = 80;
                        options.confidenceThreshold = 0.25f;
                        options.nmsThreshold = 0.45f;
                        options.rgbInput = true;
                        options.letterbox = true;
                        // rknn_set_core_mask is valid only on RK3588.  Other
                        // SoCs use one context with RKNN's automatic scheduler
                        // and must keep this at the auto-selection sentinel.
                        options.npuCoreId = config->pinWorkersToCores ? workerIndex : -1;

                        boardlab::npu::YoloV8RknnDetector detector(options);
                        detector.initialize();
                        {
                            std::lock_guard<std::mutex> locker(coordinator.lifecycleMutex);
                            ++coordinator.initializedWorkers;
                        }
                        coordinator.lifecycleChanged.notify_all();

                        while (true) {
                            FrameTask task;
                            {
                                std::unique_lock<std::mutex> locker(coordinator.taskMutex);
                                coordinator.taskReady.wait(locker, [&coordinator, workerIndex]() {
                                    return coordinator.cancelled || coordinator.inputClosed
                                        || std::any_of(coordinator.tasks.cbegin(), coordinator.tasks.cend(), [workerIndex](const FrameTask &candidate) {
                                            return candidate.assignedWorkerIndex == workerIndex;
                                        });
                                });
                                const auto taskIt = std::find_if(coordinator.tasks.begin(), coordinator.tasks.end(), [workerIndex](const FrameTask &candidate) {
                                    return candidate.assignedWorkerIndex == workerIndex;
                                });
                                if (coordinator.cancelled || taskIt == coordinator.tasks.end()) {
                                    return;
                                }
                                task = std::move(*taskIt);
                                coordinator.tasks.erase(taskIt);
                            }
                            coordinator.taskReady.notify_all();

                            QElapsedTimer inferenceTimer;
                            inferenceTimer.start();
                            std::vector<boardlab::npu::Detection> detections = detector.infer(task.frame);
                            const qint64 inferenceMs = inferenceTimer.elapsed();

                            {
                                std::lock_guard<std::mutex> locker(coordinator.taskMutex);
                                if (coordinator.cancelled) {
                                    return;
                                }
                            }
                            FrameResult result;
                            result.frameNumber = task.frameNumber;
                            result.workerIndex = workerIndex;
                            result.frame = std::move(task.frame);
                            result.detections = std::move(detections);
                            result.inferenceMs = inferenceMs;
                            {
                                std::lock_guard<std::mutex> locker(coordinator.resultMutex);
                                coordinator.results.emplace(result.frameNumber, std::move(result));
                                coordinator.completedFrameNumbers.push_back(task.frameNumber);
                            }
                            coordinator.resultReady.notify_one();
                        }
                    } catch (const std::exception &error) {
                        reportWorkerFailure(QStringLiteral("%1 failed: %2")
                                                .arg(workerLabel(*config, workerIndex))
                                                .arg(QString::fromLocal8Bit(error.what())));
                    } catch (...) {
                        reportWorkerFailure(QStringLiteral("%1 failed with an unknown RKNN error")
                                                .arg(workerLabel(*config, workerIndex)));
                    }
                });
            }

            // Initialization may take a moment on the first use.  Keep the
            // status responsive and fail the whole run if a required context
            // cannot be created.  We never silently change the configured
            // execution policy after the operator starts the sample.
            while (true) {
                {
                    std::unique_lock<std::mutex> locker(coordinator.lifecycleMutex);
                    if (coordinator.initializedWorkers == workerCount) {
                        break;
                    }
                    if (!coordinator.workerError.isEmpty()) {
                        const QString failure = coordinator.workerError;
                        locker.unlock();
                        requestCancellation();
                        joinWorkers();
                        throw std::runtime_error(failure.toStdString());
                    }
                    coordinator.lifecycleChanged.wait_for(locker, std::chrono::milliseconds(80));
                }
                if (m_stopRequested.load(std::memory_order_relaxed)) {
                    requestCancellation();
                    joinWorkers();
                    finishStopped(&state, QStringLiteral("Stopped while initialising NPU worker contexts"));
                    return;
                }
            }

            if (m_stopRequested.load(std::memory_order_relaxed)) {
                requestCancellation();
                joinWorkers();
                finishStopped(&state, QStringLiteral("Stopped after NPU worker initialisation"));
                return;
            }

            cv::VideoCapture video;
            if (!openSoftwareH264Video(videoPath, &video)) {
                throw std::runtime_error(
                    "failed to open the fixed YOLOv8 sample MP4 with the required FFmpeg software H.264 decoder");
            }
            const double reportedFrameCount = video.get(cv::CAP_PROP_FRAME_COUNT);
            int frameTotal = reportedFrameCount > 0.0
                ? static_cast<int>(std::llround(reportedFrameCount))
                : 0;
            state.insert(QStringLiteral("framesTotal"), frameTotal);
            state.insert(QStringLiteral("status"), QStringLiteral("running"));
            state.insert(QStringLiteral("message"),
                         QStringLiteral("Analysing the sample video once with %1")
                             .arg(workerSummary(*config)));
            appendLog(&state, frameTotal > 0
                      ? QStringLiteral("Opened sample video with %1 frames using the FFmpeg software H.264 decoder").arg(frameTotal)
                      : QStringLiteral("Opened sample video with an unknown frame count using the FFmpeg software H.264 decoder"));
            appendLog(&state, config->pinWorkersToCores
                      ? QStringLiteral("Frames are distributed once across the pinned NPU workers; no frame is inferred three times")
                      : QStringLiteral("Frames are inferred once by the automatic RKNN worker"));
            publish(state);
            // Debugfs reads must not pause the decode/dispatch thread. The
            // sampler runs independently and publishes its bounded result on
            // the next coordinator iteration.
            startTelemetryWorker();

            QElapsedTimer elapsed;
            elapsed.start();
            QElapsedTimer previewTimer;
            previewTimer.start();
            QElapsedTimer publishTimer;
            publishTimer.start();
            qint64 inferenceTotalMs = 0;
            int processed = 0;
            int detectionTotal = 0;
            int submitted = 0;
            int nextExpectedFrame = 1;
            bool inputAtEnd = false;
            bool previewWritten = false;
            bool resultPreviewWritten = false;
            qulonglong previewSequence = runSerial;
            QVariantList framesByWorker = emptyFrameCounters(config);

            while (true) {
                // Cancellation is only acted on between frames.  Workers are
                // asked to finish their active RKNN call and are then joined;
                // the runtime is never force-terminated from another thread.
                if (m_stopRequested.load(std::memory_order_relaxed)) {
                    requestCancellation();
                    joinWorkers();
                    stopTelemetryWorker();
                    state.insert(QStringLiteral("framesDropped"), 0);
                    state.insert(QStringLiteral("framesSkippedOnStop"), qMax(0, submitted - processed));
                    finishStopped(&state, QStringLiteral("Stopped after the active NPU inference frame"));
                    return;
                }
                const QString failure = workerFailure();
                if (!failure.isEmpty()) {
                    requestCancellation();
                    joinWorkers();
                    stopTelemetryWorker();
                    throw std::runtime_error(failure.toStdString());
                }

                bool queuedFrame = false;
                // Bound every location that can retain a decoded cv::Mat:
                // worker-owned frames, the input queue, and completed frames
                // waiting for an earlier sequence number. A slow worker cannot
                // turn the reorder map into an unbounded video buffer.
                if (!inputAtEnd && submitted - processed < maximumInFlightFrames) {
                    bool hasQueueCapacity = false;
                    {
                        std::lock_guard<std::mutex> locker(coordinator.taskMutex);
                        hasQueueCapacity = !coordinator.cancelled
                            && coordinator.tasks.size() < kMaximumQueuedFrames;
                    }
                    if (hasQueueCapacity) {
                        cv::Mat frame;
                        if (!video.read(frame) || frame.empty()) {
                            // CAP_PROP_FRAME_COUNT/POS_FRAMES come from container metadata.
                            // With the bundled MP4, the decoder can legitimately reach
                            // EOF after all queued frames while POS_FRAMES still reports
                            // one frame behind.  Treat a successful decoder EOF as the
                            // authoritative end of this fixed sample instead of turning a
                            // completed NPU run into a false failure.
                            if (frameTotal > 0 && submitted != frameTotal) {
                                appendLog(&state,
                                          QStringLiteral("Video metadata reported %1 frames, but the decoder delivered %2; using the decoded frame count")
                                              .arg(frameTotal)
                                              .arg(submitted));
                                frameTotal = submitted;
                                state.insert(QStringLiteral("framesTotal"), frameTotal);
                            }
                            inputAtEnd = true;
                            closeInput();
                        } else {
                            FrameTask task;
                            task.frameNumber = submitted + 1;
                            task.assignedWorkerIndex = (task.frameNumber - 1) % workerCount;
                            task.frame = std::move(frame);
                            int queuedDepth = 0;
                            {
                                std::lock_guard<std::mutex> locker(coordinator.taskMutex);
                                if (!coordinator.cancelled) {
                                    coordinator.tasks.push_back(std::move(task));
                                    ++submitted;
                                    queuedDepth = static_cast<int>(coordinator.tasks.size());
                                    queuedFrame = true;
                                }
                            }
                            if (queuedFrame) {
                                coordinator.taskReady.notify_all();
                                state.insert(QStringLiteral("framesSubmitted"), submitted);
                                state.insert(QStringLiteral("framesQueued"), queuedDepth);
                            }
                        }
                    }
                }

                // Live preview is intentionally decoupled from ordered score
                // aggregation. A temporarily slow worker must not hold back a
                // newer annotated frame already completed by another worker.
                // The result map still retains every frame for ordered counters
                // and final totals below.
                FrameResult latestPreviewResult;
                FrameResult firstDetectionPreviewResult;
                bool hasLatestPreviewResult = false;
                bool hasFirstDetectionPreviewResult = false;
                {
                    std::lock_guard<std::mutex> locker(coordinator.resultMutex);
                    while (!coordinator.completedFrameNumbers.empty()) {
                        const int frameNumber = coordinator.completedFrameNumbers.front();
                        coordinator.completedFrameNumbers.pop_front();
                        const auto resultIt = coordinator.results.find(frameNumber);
                        if (resultIt == coordinator.results.end()) {
                            continue;
                        }
                        latestPreviewResult = resultIt->second;
                        hasLatestPreviewResult = true;
                        if (!resultPreviewWritten && !hasFirstDetectionPreviewResult
                            && !resultIt->second.detections.empty()) {
                            firstDetectionPreviewResult = resultIt->second;
                            hasFirstDetectionPreviewResult = true;
                        }
                    }
                }

                bool previewUpdated = false;
                // Keep the annotated preview at the same 10 FPS cadence used
                // by the AI NVR.  The inference workers remain independent:
                // only the newest completed frame is encoded, so a slow JPEG
                // write never grows the NPU task queue or replays old frames.
                constexpr qint64 kPreviewIntervalMs = 100;
                const bool writeLivePreview = hasLatestPreviewResult
                    && (!previewWritten || previewTimer.elapsed() >= kPreviewIntervalMs);
                if (writeLivePreview) {
                    cv::Mat annotatedFrame = latestPreviewResult.frame.clone();
                    boardlab::npu::drawDetections(&annotatedFrame, latestPreviewResult.detections);
                    if (writeJpegAtomically(previewPath, annotatedFrame)) {
                        previewWritten = true;
                        previewTimer.restart();
                        const qulonglong previewSerial = ++previewSequence;
                        state.insert(QStringLiteral("previewSerial"), previewSerial);
                        state.insert(QStringLiteral("previewUrl"), previewUrlForPath(previewPath, previewSerial));
                        previewUpdated = true;
                    } else {
                        state.insert(QStringLiteral("previewError"),
                                     QStringLiteral("Could not atomically write the annotated YOLOv8 preview image"));
                    }
                }

                // Keep the first confirmed detection as a stable completion
                // artifact. It uses a clone so the ordered statistics retain
                // their original decoded frame without extra work per frame.
                if (!resultPreviewWritten && hasFirstDetectionPreviewResult) {
                    cv::Mat annotatedResult = firstDetectionPreviewResult.frame.clone();
                    boardlab::npu::drawDetections(&annotatedResult, firstDetectionPreviewResult.detections);
                    if (writeJpegAtomically(resultPreviewPath, annotatedResult)) {
                        resultPreviewWritten = true;
                        const qulonglong resultSerial = ++previewSequence;
                        state.insert(QStringLiteral("resultPreviewPath"), resultPreviewPath);
                        state.insert(QStringLiteral("resultPreviewUrl"), previewUrlForPath(resultPreviewPath, resultSerial));
                        state.insert(QStringLiteral("resultPreviewSerial"), resultSerial);
                        previewUpdated = true;
                    } else {
                        state.insert(QStringLiteral("previewError"),
                                     QStringLiteral("Could not atomically write the annotated YOLOv8 result image"));
                    }
                }
                if (previewUpdated) {
                    state.insert(QStringLiteral("updatedAt"), isoNow());
                    publish(state);
                    publishTimer.restart();
                }

                bool mergedResult = false;
                while (true) {
                    FrameResult result;
                    {
                        std::lock_guard<std::mutex> locker(coordinator.resultMutex);
                        const auto resultIt = coordinator.results.find(nextExpectedFrame);
                        if (resultIt == coordinator.results.end()) {
                            break;
                        }
                        result = std::move(resultIt->second);
                        coordinator.results.erase(resultIt);
                    }
                    mergedResult = true;
                    ++nextExpectedFrame;
                    ++processed;
                    detectionTotal += static_cast<int>(result.detections.size());
                    inferenceTotalMs += result.inferenceMs;
                    if (result.workerIndex >= 0 && result.workerIndex < framesByWorker.size()) {
                        framesByWorker[result.workerIndex] = framesByWorker.at(result.workerIndex).toInt() + 1;
                    }

                    const qint64 elapsedMs = elapsed.elapsed();
                    state.insert(QStringLiteral("framesProcessed"), processed);
                    state.insert(QStringLiteral("detectionsLastFrame"), static_cast<int>(result.detections.size()));
                    state.insert(QStringLiteral("detectionsTotal"), detectionTotal);
                    state.insert(QStringLiteral("inferenceMs"), static_cast<double>(result.inferenceMs));
                    state.insert(QStringLiteral("averageInferenceMs"),
                                 static_cast<double>(inferenceTotalMs) / static_cast<double>(processed));
                    state.insert(QStringLiteral("fps"), elapsedMs > 0
                                 ? 1000.0 * static_cast<double>(processed) / static_cast<double>(elapsedMs)
                                 : 0.0);
                    state.insert(QStringLiteral("elapsedMs"), elapsedMs);
                    state.insert(QStringLiteral("framesByNpuCore"), framesByWorker);
                    const double progress = frameTotal > 0
                        ? std::min(1.0, static_cast<double>(processed) / static_cast<double>(frameTotal))
                        : 0.0;
                    state.insert(QStringLiteral("progress"), progress);
                    state.insert(QStringLiteral("progressPercent"), static_cast<int>(std::round(progress * 100.0)));
                    state.insert(QStringLiteral("message"),
                                 QStringLiteral("Frame %1 by %2: %3 detections")
                                     .arg(processed)
                                     .arg(workerLabel(*config, result.workerIndex))
                                     .arg(result.detections.size()));
                    state.insert(QStringLiteral("updatedAt"), isoNow());
                    if (processed == 1 || publishTimer.elapsed() >= 220) {
                        publish(state);
                        publishTimer.restart();
                    }
                }

                {
                    std::lock_guard<std::mutex> locker(coordinator.taskMutex);
                    state.insert(QStringLiteral("framesQueued"), static_cast<int>(coordinator.tasks.size()));
                }

                NpuLoadReading telemetryReading;
                if (takeTelemetryResult(&telemetryReading)) {
                    appendNpuLoadResult(&state, telemetryReading, *config);
                    state.insert(QStringLiteral("updatedAt"), isoNow());
                    publish(state);
                }

                if (inputAtEnd && processed == submitted) {
                    break;
                }
                if (!queuedFrame && !mergedResult) {
                    std::unique_lock<std::mutex> locker(coordinator.resultMutex);
                    coordinator.resultReady.wait_for(locker, std::chrono::milliseconds(12));
                }
            }

            closeInput();
            joinWorkers();
            stopTelemetryWorker();

            state.insert(QStringLiteral("status"), QStringLiteral("completed"));
            state.insert(QStringLiteral("message"),
                         QStringLiteral("Completed one non-looping YOLOv8 sample pass using %1")
                             .arg(workerSummary(*config)));
            state.insert(QStringLiteral("running"), false);
            state.insert(QStringLiteral("canStart"), true);
            state.insert(QStringLiteral("canStop"), false);
            state.insert(QStringLiteral("progress"), 1.0);
            state.insert(QStringLiteral("progressPercent"), 100);
            state.insert(QStringLiteral("elapsedMs"), elapsed.elapsed());
            if (resultPreviewWritten) {
                state.insert(QStringLiteral("previewPath"), state.value(QStringLiteral("resultPreviewPath")));
                state.insert(QStringLiteral("previewUrl"), state.value(QStringLiteral("resultPreviewUrl")));
                state.insert(QStringLiteral("previewSerial"), state.value(QStringLiteral("resultPreviewSerial")));
            }
            state.insert(QStringLiteral("finishedAt"), isoNow());
            appendLog(&state, QStringLiteral("Completed %1 frames, %2 detections, worker frame split %3")
                      .arg(processed)
                      .arg(detectionTotal)
                      .arg(frameDistributionSummary(*config, framesByWorker)));
            publishFinal(state);
        } catch (const std::exception &error) {
            requestCancellation();
            joinWorkers();
            stopTelemetryWorker();
            state.insert(QStringLiteral("status"), QStringLiteral("failed"));
            state.insert(QStringLiteral("message"), QStringLiteral("YOLOv8 demo failed"));
            state.insert(QStringLiteral("error"), QString::fromLocal8Bit(error.what()));
            state.insert(QStringLiteral("running"), false);
            state.insert(QStringLiteral("canStart"), true);
            state.insert(QStringLiteral("canStop"), false);
            state.insert(QStringLiteral("finishedAt"), isoNow());
            appendLog(&state, state.value(QStringLiteral("error")).toString());
            publishFinal(state);
        } catch (...) {
            requestCancellation();
            joinWorkers();
            stopTelemetryWorker();
            state.insert(QStringLiteral("status"), QStringLiteral("failed"));
            state.insert(QStringLiteral("message"), QStringLiteral("YOLOv8 demo failed"));
            state.insert(QStringLiteral("error"), QStringLiteral("Unknown YOLOv8 worker failure"));
            state.insert(QStringLiteral("running"), false);
            state.insert(QStringLiteral("canStart"), true);
            state.insert(QStringLiteral("canStop"), false);
            state.insert(QStringLiteral("finishedAt"), isoNow());
            appendLog(&state, state.value(QStringLiteral("error")).toString());
            publishFinal(state);
        }
    }

    void finishStopped(QVariantMap *state, const QString &reason)
    {
        if (!state) {
            return;
        }
        state->insert(QStringLiteral("status"), QStringLiteral("stopped"));
        state->insert(QStringLiteral("message"), reason);
        state->insert(QStringLiteral("error"), QString());
        state->insert(QStringLiteral("running"), false);
        state->insert(QStringLiteral("canStart"), true);
        state->insert(QStringLiteral("canStop"), false);
        state->insert(QStringLiteral("finishedAt"), isoNow());
        appendLog(state, reason);
        publishFinal(*state);
    }
#endif

    QVariantMap m_state;
    QPointer<NpuYoloDemo> m_owner;
    std::atomic_bool m_stopRequested {false};
    mutable QMutex m_finalMutex;
    QVariantMap m_finalStatus;
};

NpuYoloDemo::NpuYoloDemo(BoardProfile *boardProfile, QObject *parent)
    : QObject(parent)
    , m_status(discoveryState(boardProfile))
    , m_log(m_status.value(QStringLiteral("log")).toString())
    , m_boardProfile(boardProfile)
{
}

NpuYoloDemo::~NpuYoloDemo()
{
    if (!m_runner) {
        return;
    }
    m_runner->requestStop();
    // A stop is observed after the active frame.  Do not terminate the
    // thread: an interrupted RKNN call can leave the runtime in an unsafe
    // state and would destroy the detector from the wrong context.
    m_runner->wait();
    delete m_runner;
    m_runner = nullptr;
}

QVariantMap NpuYoloDemo::status() const
{
    return m_status;
}

QVariantMap NpuYoloDemo::state() const
{
    return status();
}

bool NpuYoloDemo::running() const
{
    return m_status.value(QStringLiteral("running")).toBool();
}

QString NpuYoloDemo::log() const
{
    return m_log;
}

QVariantMap NpuYoloDemo::discover()
{
    if (m_runner) {
        return m_status;
    }

    const QVariantMap discovered = discoveryState(m_boardProfile.data());
    const QString oldState = m_status.value(QStringLiteral("status")).toString();
    if (terminalStatus(oldState) && discovered.value(QStringLiteral("available")).toBool()) {
        // A refresh should never discard a completed score/preview just
        // because the caller asked to re-check assets after a run.
        QVariantMap preserved = m_status;
        preserved.insert(QStringLiteral("available"), true);
        preserved.insert(QStringLiteral("canStart"), true);
        preserved.insert(QStringLiteral("assets"), discovered.value(QStringLiteral("assets")));
        preserved.insert(QStringLiteral("runtime"), discovered.value(QStringLiteral("runtime")));
        preserved.insert(QStringLiteral("updatedAt"), isoNow());
        setStatus(preserved);
    } else {
        setStatus(discovered);
    }
    return m_status;
}

bool NpuYoloDemo::start()
{
    if (m_runner) {
        return false;
    }

    QVariantMap nextState = discoveryState(m_boardProfile.data());
    if (!nextState.value(QStringLiteral("available")).toBool()) {
        setStatus(nextState);
        return false;
    }

    const qulonglong nextSerial = qMax<qulonglong>(1,
        m_status.value(QStringLiteral("previewSerial")).toULongLong() + 1);
    nextState.insert(QStringLiteral("previewSerial"), nextSerial);
    nextState.insert(QStringLiteral("status"), QStringLiteral("starting"));
    nextState.insert(QStringLiteral("message"),
                     QStringLiteral("Preparing %1 YOLOv8 sample")
                         .arg(nextState.value(QStringLiteral("platformLabel")).toString()));
    nextState.insert(QStringLiteral("running"), true);
    nextState.insert(QStringLiteral("canStart"), false);
    nextState.insert(QStringLiteral("canStop"), true);
    nextState.insert(QStringLiteral("startedAt"), isoNow());
    appendLog(&nextState, QStringLiteral("Queued fixed YOLOv8 sample run"));
    setStatus(nextState);

    m_runner = new Runner(nextState, this);
    connect(m_runner, &QThread::finished, this, &NpuYoloDemo::handleRunnerFinished);
    m_runner->start();
    return true;
}

bool NpuYoloDemo::stop()
{
    if (!m_runner) {
        return false;
    }
    m_runner->requestStop();
    QVariantMap stopping = m_status;
    stopping.insert(QStringLiteral("status"), QStringLiteral("stopping"));
    stopping.insert(QStringLiteral("message"), QStringLiteral("Stopping after the current video frame"));
    stopping.insert(QStringLiteral("canStart"), false);
    stopping.insert(QStringLiteral("canStop"), false);
    stopping.insert(QStringLiteral("updatedAt"), isoNow());
    appendLog(&stopping, QStringLiteral("Stop requested; waiting for the active frame"));
    setStatus(stopping);
    return true;
}

void NpuYoloDemo::applyWorkerStatus(const QVariantMap &status)
{
    setStatus(status);
}

void NpuYoloDemo::handleRunnerFinished()
{
    // Runner has no Q_OBJECT on purpose: it is only a thread owner and does
    // not expose a second signal API.  This connection is made exclusively
    // from m_runner, so the sender is known to be a Runner.
    Runner *finishedRunner = static_cast<Runner *>(sender());
    if (!finishedRunner) {
        return;
    }

    const QVariantMap finalState = finishedRunner->finalStatus();
    if (!finalState.isEmpty()) {
        setStatus(finalState);
    }
    if (m_runner == finishedRunner) {
        m_runner = nullptr;
    }

    const bool success = m_status.value(QStringLiteral("status")).toString() == QStringLiteral("completed");
    emit finished(success, m_status);
    finishedRunner->deleteLater();
}

void NpuYoloDemo::setStatus(const QVariantMap &status)
{
    const bool oldRunning = running();
    const QString oldLog = m_log;
    m_status = status;
    m_log = m_status.value(QStringLiteral("log")).toString();
    emit statusChanged();
    if (oldRunning != running()) {
        emit runningChanged();
    }
    if (oldLog != m_log) {
        emit logChanged();
    }
}
