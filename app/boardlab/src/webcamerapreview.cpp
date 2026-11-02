#include "webcamerapreview.h"
#include "camerapreviewprofile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

namespace {

constexpr int kPreviewFrameRate = 8;
constexpr int kPreviewWidth = 960;
constexpr int kPreviewHeight = 600;
constexpr qint64 kMaximumJpegBytes = 4LL * 1024LL * 1024LL;

bool isVideoNodePath(const QString &path)
{
    static const QRegularExpression videoNodePattern(QStringLiteral("^/dev/video[0-9]+$"));
    return videoNodePattern.match(path.trimmed()).hasMatch();
}

bool completeJpeg(const QByteArray &data)
{
    return data.size() >= 4
        && data.startsWith(QByteArrayLiteral("\xFF\xD8"))
        && data.endsWith(QByteArrayLiteral("\xFF\xD9"));
}

} // namespace

WebCameraPreview::WebCameraPreview(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_framePollTimer(new QTimer(this))
    , m_gstLaunch(QStandardPaths::findExecutable(QStringLiteral("gst-launch-1.0")))
{
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_framePollTimer->setInterval(125);

    connect(m_process, &QProcess::started, this, &WebCameraPreview::onProcessStarted);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &WebCameraPreview::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &WebCameraPreview::onProcessError);
    connect(m_process, &QProcess::readyRead, this, &WebCameraPreview::collectProcessOutput);
    connect(m_framePollTimer, &QTimer::timeout, this, &WebCameraPreview::pollFrame);
}

WebCameraPreview::~WebCameraPreview()
{
    if (m_framePollTimer) {
        m_framePollTimer->stop();
    }
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        if (!m_process->waitForFinished(350)) {
            m_process->kill();
            m_process->waitForFinished(350);
        }
    }
    delete m_frameDirectory;
    m_frameDirectory = nullptr;
}

bool WebCameraPreview::start(const QString &devicePath, const QString &mode)
{
    const QString normalizedDevice = devicePath.trimmed();
    if (!isVideoNodePath(normalizedDevice)) {
        fail(QStringLiteral("实时预览仅允许已发现的 /dev/videoN 采集节点"));
        return false;
    }
    if (m_gstLaunch.isEmpty()) {
        fail(QStringLiteral("未找到 gst-launch-1.0，无法启动 Web 实时预览"),
             QStringLiteral("请安装 GStreamer v4l2src、videoconvert、jpegenc 与 multifilesink 组件"));
        return false;
    }
    if (!ensureFrameDirectory()) {
        fail(QStringLiteral("无法创建 Web 预览帧缓存"));
        return false;
    }

    if (m_process->state() != QProcess::NotRunning) {
        if (normalizedDevice == m_devicePath && !m_stopRequested) {
            return true;
        }
        m_pendingDevicePath = normalizedDevice;
        m_pendingMode = mode;
        m_restartAfterStop = true;
        m_stopRequested = true;
        updateState(QStringLiteral("stopping"), QStringLiteral("正在切换摄像头实时预览"),
                    QStringLiteral("当前采集节点会先安全释放，再打开新节点"));
        m_process->terminate();
        QTimer::singleShot(700, this, [this] {
            if (m_stopRequested && m_process->state() != QProcess::NotRunning) {
                m_process->kill();
            }
        });
        return true;
    }

    configureStart(normalizedDevice, mode);
    startProcess();
    return true;
}

bool WebCameraPreview::stop()
{
    m_pendingDevicePath.clear();
    m_pendingMode.clear();
    m_restartAfterStop = false;
    m_stopRequested = true;
    if (m_framePollTimer) {
        m_framePollTimer->stop();
    }
    clearFrames();

    if (m_process->state() == QProcess::NotRunning) {
        finishStopped();
        return true;
    }

    updateState(QStringLiteral("stopping"), QStringLiteral("正在停止 Web 摄像头实时预览"),
                QStringLiteral("正在释放 V4L2 采集节点"));
    m_process->terminate();
    QTimer::singleShot(700, this, [this] {
        if (m_stopRequested && m_process->state() != QProcess::NotRunning) {
            m_process->kill();
        }
    });
    return true;
}

QVariantMap WebCameraPreview::status() const
{
    QVariantMap result;
    result.insert(QStringLiteral("available"), !m_gstLaunch.isEmpty());
    result.insert(QStringLiteral("running"), m_running);
    result.insert(QStringLiteral("active"), m_active);
    result.insert(QStringLiteral("previewAvailable"), !m_latestJpeg.isEmpty());
    result.insert(QStringLiteral("status"), m_state);
    result.insert(QStringLiteral("message"), m_message);
    result.insert(QStringLiteral("detail"), m_detail);
    result.insert(QStringLiteral("error"), m_error);
    result.insert(QStringLiteral("device"), m_devicePath);
    result.insert(QStringLiteral("mode"), m_mode);
    result.insert(QStringLiteral("sensorModel"), m_sensorModel);
    result.insert(QStringLiteral("rotateMethod"), m_rotateMethod);
    result.insert(QStringLiteral("frameSerial"), QVariant::fromValue<qulonglong>(m_frameSerial));
    result.insert(QStringLiteral("frameRate"), kPreviewFrameRate);
    result.insert(QStringLiteral("previewWidth"), kPreviewWidth);
    result.insert(QStringLiteral("previewHeight"), kPreviewHeight);
    if (m_lastFrameAt.isValid()) {
        result.insert(QStringLiteral("lastFrameAt"), m_lastFrameAt.toUTC().toString(Qt::ISODateWithMs));
    }
    return result;
}

QByteArray WebCameraPreview::latestJpeg() const
{
    return m_latestJpeg;
}

bool WebCameraPreview::running() const
{
    return m_running;
}

bool WebCameraPreview::active() const
{
    return m_active;
}

bool WebCameraPreview::ensureFrameDirectory()
{
    if (m_frameDirectory && m_frameDirectory->isValid()) {
        return true;
    }

    delete m_frameDirectory;
    m_frameDirectory = new QTemporaryDir(QDir::tempPath()
                                         + QStringLiteral("/boardlab-web-camera-XXXXXX"));
    if (!m_frameDirectory->isValid()) {
        delete m_frameDirectory;
        m_frameDirectory = nullptr;
        return false;
    }
    m_frameDirectory->setAutoRemove(true);
    return true;
}

void WebCameraPreview::clearFrames()
{
    if (m_frameDirectory && m_frameDirectory->isValid()) {
        const QFileInfoList entries = QDir(m_frameDirectory->path()).entryInfoList(
            QStringList() << QStringLiteral("frame-*.jpg"), QDir::Files | QDir::NoDotAndDotDot);
        for (const QFileInfo &entry : entries) {
            QFile::remove(entry.absoluteFilePath());
        }
    }
    const bool hadFrame = !m_latestJpeg.isEmpty() || m_active;
    m_latestJpeg.clear();
    m_latestFramePath.clear();
    m_latestFrameModifiedMs = 0;
    m_lastFrameAt = QDateTime();
    m_active = false;
    if (hadFrame) {
        ++m_frameSerial;
    }
}

void WebCameraPreview::configureStart(const QString &devicePath, const QString &mode)
{
    m_devicePath = devicePath;
    m_mode = mode.trimmed().toLower();
    if (m_mode.isEmpty()) {
        m_mode = QStringLiteral("mipi");
    }
    m_stopRequested = false;
    m_mipiFallbackUsed = false;
    m_useMipiProfile = m_mode == QStringLiteral("mipi");
    m_sensorModel = m_useMipiProfile
                        ? boardlab::cameraSensorModelForDevice(devicePath)
                        : QString();
    m_rotateMethod = boardlab::cameraPreviewRotateMethod(m_sensorModel);
    m_processOutput.clear();
    clearFrames();
    m_frameClock.restart();
}

void WebCameraPreview::startProcess()
{
    if (!ensureFrameDirectory()) {
        fail(QStringLiteral("无法创建 Web 预览帧缓存"));
        return;
    }

    clearFrames();
    m_processOutput.clear();
    m_frameClock.restart();
    const QString videoflipValue = boardlab::cameraPreviewVideoflipValue(m_rotateMethod);
    const QString rotationDetail = videoflipValue.isEmpty()
                                       ? QString()
                                       : QStringLiteral("；OV5695 已应用 videoflip %1=%2（等价于 rotate-method=90l）")
                                             .arg(boardlab::cameraPreviewVideoflipPropertyName(),
                                                  videoflipValue);
    updateState(QStringLiteral("starting"), QStringLiteral("正在打开 Web 摄像头实时预览"),
                m_useMipiProfile
                    ? QStringLiteral("正在协商 RKISP NV12 低延迟预览（最大 1280 × 800 / 8 FPS）%1").arg(rotationDetail)
                    : QStringLiteral("正在协商摄像头格式并生成 960 × 600 JPEG 预览%1").arg(rotationDetail));
    m_process->setWorkingDirectory(m_frameDirectory->path());
    m_process->start(m_gstLaunch, pipelineArguments());
}

void WebCameraPreview::finishStopped()
{
    m_running = false;
    m_active = false;
    m_stopRequested = false;
    clearFrames();
    updateState(QStringLiteral("stopped"), QStringLiteral("Web 摄像头实时预览已停止"),
                QStringLiteral("V4L2 采集节点已释放"));
}

void WebCameraPreview::fail(const QString &message, const QString &detail)
{
    if (m_framePollTimer) {
        m_framePollTimer->stop();
    }
    m_running = false;
    m_active = false;
    m_stopRequested = false;
    clearFrames();
    updateState(QStringLiteral("failed"), message, detail, message);
}

void WebCameraPreview::updateState(const QString &state, const QString &message,
                                   const QString &detail, const QString &error)
{
    const bool changedState = m_state != state || m_message != message || m_detail != detail || m_error != error;
    m_state = state;
    m_message = message;
    m_detail = detail;
    m_error = error;
    if (changedState) {
        emit changed();
    }
}

QStringList WebCameraPreview::pipelineArguments() const
{
    const QString outputPattern = QDir(m_frameDirectory->path())
                                      .absoluteFilePath(QStringLiteral("frame-%05d.jpg"));
    const QString videoflipValue = boardlab::cameraPreviewVideoflipValue(m_rotateMethod);
    QStringList arguments;
    arguments << QStringLiteral("-q")
              << QStringLiteral("v4l2src")
              << QStringLiteral("device=%1").arg(m_devicePath)
              << QStringLiteral("do-timestamp=true");
    if (m_useMipiProfile) {
        arguments << QStringLiteral("!")
                  << QStringLiteral("video/x-raw,format=NV12,width=1280,height=800,framerate=8/1");
    }
    arguments << QStringLiteral("!")
              << QStringLiteral("queue")
              << QStringLiteral("leaky=2")
              << QStringLiteral("max-size-buffers=2")
              << QStringLiteral("max-size-bytes=0")
              << QStringLiteral("max-size-time=0")
              << QStringLiteral("!");
    if (!videoflipValue.isEmpty()) {
        arguments << QStringLiteral("videoflip")
                  << QStringLiteral("%1=%2")
                         .arg(boardlab::cameraPreviewVideoflipPropertyName(),
                              videoflipValue)
                  << QStringLiteral("!");
    }
    arguments << QStringLiteral("videorate")
              << QStringLiteral("drop-only=true")
              << QStringLiteral("!")
              << QStringLiteral("video/x-raw,framerate=8/1")
              << QStringLiteral("!")
              << QStringLiteral("videoconvert")
              << QStringLiteral("!")
              << QStringLiteral("videoscale")
              << QStringLiteral("!")
              << QStringLiteral("video/x-raw,width=960,height=600")
              << QStringLiteral("!")
              << QStringLiteral("jpegenc")
              << QStringLiteral("quality=76")
              << QStringLiteral("!")
              << QStringLiteral("multifilesink")
              << QStringLiteral("location=%1").arg(outputPattern)
              << QStringLiteral("max-files=3");
    return arguments;
}

QString WebCameraPreview::compactProcessOutput() const
{
    QString output = QString::fromLocal8Bit(m_processOutput).simplified();
    if (output.size() > 360) {
        output.truncate(360);
    }
    return output;
}

void WebCameraPreview::onProcessStarted()
{
    m_running = true;
    m_stopRequested = false;
    if (m_framePollTimer) {
        m_framePollTimer->start();
    }
    updateState(QStringLiteral("starting"), QStringLiteral("正在等待摄像头首帧"),
                QStringLiteral("GStreamer 已打开 %1；首帧到达后会自动显示%2")
                    .arg(m_devicePath, m_rotateMethod.isEmpty()
                                             ? QString()
                                             : QStringLiteral("；OV5695 已应用 90l 旋转")));
    emit changed();
}

void WebCameraPreview::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    collectProcessOutput();
    if (m_framePollTimer) {
        m_framePollTimer->stop();
    }
    const bool restart = m_restartAfterStop;
    const QString nextDevice = m_pendingDevicePath;
    const QString nextMode = m_pendingMode;
    m_restartAfterStop = false;
    m_pendingDevicePath.clear();
    m_pendingMode.clear();

    if (m_stopRequested) {
        if (restart && !nextDevice.isEmpty()) {
            configureStart(nextDevice, nextMode);
            startProcess();
            return;
        }
        finishStopped();
        return;
    }

    m_running = false;
    const QString output = compactProcessOutput();
    if (m_useMipiProfile && !m_mipiFallbackUsed && !m_active) {
        m_mipiFallbackUsed = true;
        m_useMipiProfile = false;
        updateState(QStringLiteral("starting"), QStringLiteral("RKISP 预览格式不兼容，正在自动重试"),
                    QStringLiteral("NV12 低延迟配置未能启动，正在回退到通用视频格式协商"));
        startProcess();
        return;
    }

    const QString reason = exitStatus == QProcess::CrashExit
        ? QStringLiteral("GStreamer 摄像头预览异常退出")
        : QStringLiteral("GStreamer 摄像头预览已退出（代码 %1）").arg(exitCode);
    fail(reason, output.isEmpty()
         ? QStringLiteral("请确认摄像头节点未被其他预览占用，并检查视频输入链路")
         : output);
}

void WebCameraPreview::onProcessError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart || m_stopRequested) {
        return;
    }
    collectProcessOutput();
    fail(QStringLiteral("无法启动 GStreamer 摄像头预览"),
         compactProcessOutput().isEmpty()
             ? QStringLiteral("请检查 gst-launch-1.0 及其视频插件是否已安装")
             : compactProcessOutput());
}

void WebCameraPreview::collectProcessOutput()
{
    if (!m_process) {
        return;
    }
    m_processOutput += m_process->readAll();
    constexpr int kMaximumOutputBytes = 4096;
    if (m_processOutput.size() > kMaximumOutputBytes) {
        m_processOutput = m_processOutput.right(kMaximumOutputBytes);
    }
}

void WebCameraPreview::pollFrame()
{
    if (!m_frameDirectory || !m_frameDirectory->isValid()) {
        return;
    }

    const QFileInfoList frames = QDir(m_frameDirectory->path()).entryInfoList(
        QStringList() << QStringLiteral("frame-*.jpg"), QDir::Files | QDir::NoDotAndDotDot, QDir::Time);
    if (!frames.isEmpty()) {
        const QFileInfo frame = frames.constFirst();
        const qint64 modifiedMs = frame.lastModified().toMSecsSinceEpoch();
        if (frame.absoluteFilePath() != m_latestFramePath || modifiedMs != m_latestFrameModifiedMs) {
            const qint64 size = frame.size();
            if (size > 0 && size <= kMaximumJpegBytes) {
                QFile file(frame.absoluteFilePath());
                if (file.open(QIODevice::ReadOnly)) {
                    const QByteArray jpeg = file.readAll();
                    if (completeJpeg(jpeg)) {
                        m_latestJpeg = jpeg;
                        m_latestFramePath = frame.absoluteFilePath();
                        m_latestFrameModifiedMs = modifiedMs;
                        m_lastFrameAt = QDateTime::currentDateTimeUtc();
                        m_active = true;
                        ++m_frameSerial;
                        updateState(QStringLiteral("running"), QStringLiteral("实时画面正在显示"),
                                    QStringLiteral("浏览器通过已认证 JPEG 帧预览 %1 × %2 · %3 FPS")
                                        .arg(kPreviewWidth).arg(kPreviewHeight).arg(kPreviewFrameRate));
                        emit changed();
                        return;
                    }
                }
            }
        }
    }

    if (!m_frameClock.isValid()) {
        return;
    }
    if (!m_active && m_frameClock.elapsed() >= 3000) {
        updateState(QStringLiteral("waiting"), QStringLiteral("正在等待摄像头视频帧"),
                    QStringLiteral("GStreamer 已打开 %1；请检查 MIPI 摄像头、排线和 ISP 链路").arg(m_devicePath));
    } else if (m_active && m_lastFrameAt.isValid()
               && m_lastFrameAt.msecsTo(QDateTime::currentDateTimeUtc()) >= 3000) {
        m_active = false;
        updateState(QStringLiteral("waiting"), QStringLiteral("摄像头视频帧暂时停止"),
                    QStringLiteral("等待新的摄像头帧；可检查视频输入是否被其他程序占用"));
        emit changed();
    }
}
