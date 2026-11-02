#include "gstvideopreviewitem.h"
#include "camerapreviewprofile.h"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPainter>
#include <QColor>
#include <QRegularExpression>
#include <QTimer>

#include <mutex>
#include <utility>

#ifdef BOARDLAB_HAS_GSTREAMER_APPSINK
#include <gst/video/video.h>
#endif

namespace {

bool isV4l2VideoDevice(const QString &path)
{
    static const QRegularExpression videoNodePattern(QStringLiteral("^/dev/video[0-9]+$"));
    return videoNodePattern.match(path).hasMatch();
}

bool isRockchipIspMainpathDevice(const QString &path)
{
    // Node numbers change with the enabled media graph.  Ask sysfs for the
    // actual V4L2 entity instead of assuming that /dev/video11 is always the
    // processed MIPI output.
    const QString nodeName = QFileInfo(path).fileName();
    if (nodeName.isEmpty()) {
        return false;
    }

    QFile nameFile(QStringLiteral("/sys/class/video4linux/%1/name").arg(nodeName));
    if (!nameFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    const QString entityName = QString::fromUtf8(nameFile.readAll()).trimmed();
    return entityName.contains(QStringLiteral("rkisp_mainpath"), Qt::CaseInsensitive);
}

#ifdef BOARDLAB_HAS_GSTREAMER_APPSINK
bool initializeGstreamer(QString *errorText)
{
    static std::once_flag once;
    static bool initialized = false;
    static QString initializationError;

    std::call_once(once, [] {
        GError *error = nullptr;
        initialized = gst_init_check(nullptr, nullptr, &error);
        if (!initialized) {
            initializationError = error && error->message
                                      ? QString::fromUtf8(error->message)
                                      : QStringLiteral("GStreamer 初始化失败");
        }
        if (error) {
            g_error_free(error);
        }
    });

    if (!initialized && errorText) {
        *errorText = initializationError;
    }
    return initialized;
}

QString gstreamerMessageText(GError *error, const gchar *debugText)
{
    QString text;
    if (error && error->message) {
        text = QString::fromUtf8(error->message).trimmed();
    }
    if (text.isEmpty() && debugText) {
        text = QString::fromUtf8(debugText).trimmed();
    }
    return text.isEmpty() ? QStringLiteral("GStreamer 视频管线失败") : text;
}
#endif

} // namespace

GstVideoPreviewItem::GstVideoPreviewItem(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_busTimer(new QTimer(this))
{
    setAntialiasing(false);
    m_busTimer->setInterval(100);
    connect(m_busTimer, &QTimer::timeout, this, &GstVideoPreviewItem::pollBus);

#ifdef BOARDLAB_HAS_GSTREAMER_APPSINK
    m_detailText = QStringLiteral("GStreamer appsink 内嵌预览就绪");
#else
    m_detailText = QStringLiteral("当前构建未找到 GStreamer appsink 开发库");
#endif
}

GstVideoPreviewItem::~GstVideoPreviewItem()
{
    disconnectFrameSource();
    stopPipeline();
}

QString GstVideoPreviewItem::devicePath() const
{
    return m_devicePath;
}

void GstVideoPreviewItem::setDevicePath(const QString &devicePath)
{
    const QString normalized = devicePath.trimmed();
    if (m_devicePath == normalized) {
        return;
    }

    m_devicePath = normalized;
    m_ispPreviewProfileFallbackUsed = false;
    emit devicePathChanged();
    scheduleRestart();
}

bool GstVideoPreviewItem::requested() const
{
    return m_requested;
}

void GstVideoPreviewItem::setRequested(bool requested)
{
    if (m_requested == requested) {
        return;
    }

    m_requested = requested;
    if (requested) {
        // A fresh operator request deserves another try with the efficient
        // profile; a prior fallback only applies to one open attempt.
        m_ispPreviewProfileFallbackUsed = false;
    }
    emit requestedChanged();
    scheduleRestart();
}

QString GstVideoPreviewItem::sourceTitle() const
{
    return m_sourceTitle;
}

void GstVideoPreviewItem::setSourceTitle(const QString &sourceTitle)
{
    const QString normalized = sourceTitle.trimmed().isEmpty()
                                   ? QStringLiteral("视频输入")
                                   : sourceTitle.trimmed();
    if (m_sourceTitle == normalized) {
        return;
    }

    m_sourceTitle = normalized;
    emit sourceTitleChanged();
    if (!m_requested) {
        setDetailText(QStringLiteral("GStreamer appsink 将直接打开 %1 采集节点").arg(m_sourceTitle));
    }
}

QObject *GstVideoPreviewItem::frameSource() const
{
    return m_frameSource.data();
}

void GstVideoPreviewItem::setFrameSource(QObject *frameSource)
{
    auto *source = qobject_cast<GstVideoPreviewItem *>(frameSource);
    if (source == this) {
        source = nullptr;
    }
    if (m_frameSource == source) {
        return;
    }

    disconnectFrameSource();
    m_frameSource = source;
    if (m_frameSource) {
        m_frameSourceFrameConnection = connect(m_frameSource, &GstVideoPreviewItem::frameAvailable,
                                               this, &GstVideoPreviewItem::refreshMirroredFrame);
        m_frameSourceStateConnection = connect(m_frameSource, &GstVideoPreviewItem::statusTextChanged,
                                               this, &GstVideoPreviewItem::refreshMirroredState);
        m_frameSourceRunningConnection = connect(m_frameSource, &GstVideoPreviewItem::runningChanged,
                                                 this, &GstVideoPreviewItem::refreshMirroredState);
        m_frameSourceActiveConnection = connect(m_frameSource, &GstVideoPreviewItem::activeChanged,
                                                this, &GstVideoPreviewItem::refreshMirroredState);
        m_frameSourceDestroyedConnection = connect(m_frameSource, &QObject::destroyed, this, [this] {
            disconnectFrameSource();
            emit frameSourceChanged();
            scheduleRestart();
        });
    }

    emit frameSourceChanged();
    scheduleRestart();
}

bool GstVideoPreviewItem::running() const
{
    return m_running;
}

bool GstVideoPreviewItem::active() const
{
    return m_active;
}

QString GstVideoPreviewItem::statusText() const
{
    return m_statusText;
}

QString GstVideoPreviewItem::detailText() const
{
    return m_detailText;
}

void GstVideoPreviewItem::paint(QPainter *painter)
{
    if (!painter) {
        return;
    }

    const QRectF viewport = boundingRect();
    painter->fillRect(viewport, QColor(QStringLiteral("#08131E")));

    QImage frame;
    {
        QMutexLocker locker(&m_frameMutex);
        frame = m_frame;
    }
    if (frame.isNull() || viewport.width() <= 0 || viewport.height() <= 0) {
        return;
    }

    QSizeF targetSize = frame.size();
    targetSize.scale(viewport.size(), Qt::KeepAspectRatio);
    const QRectF target(viewport.x() + (viewport.width() - targetSize.width()) / 2.0,
                        viewport.y() + (viewport.height() - targetSize.height()) / 2.0,
                        targetSize.width(),
                        targetSize.height());
    painter->drawImage(target, frame, QRectF(QPointF(), frame.size()));
}

void GstVideoPreviewItem::scheduleRestart()
{
    if (m_restartPending) {
        return;
    }

    m_restartPending = true;
    QTimer::singleShot(0, this, [this] {
        m_restartPending = false;
        restartPipeline();
    });
}

void GstVideoPreviewItem::restartPipeline()
{
    stopPipeline();

    if (!m_requested) {
        setPreviewState(QStringLiteral("实时预览未启动"), false, false);
        setDetailText(QStringLiteral("GStreamer appsink 等待打开 %1").arg(m_sourceTitle));
        return;
    }

    // The fullscreen overlay mirrors the inline item's converted frames.  It
    // intentionally owns no V4L2 handle, so one driver node is opened exactly
    // once while the user switches between embedded and enlarged preview.
    if (m_frameSource) {
        refreshMirroredState();
        return;
    }

    if (!isV4l2VideoDevice(m_devicePath)) {
        setPreviewState(QStringLiteral("预览失败: 无效的 V4L2 视频节点"), false, false);
        setDetailText(QStringLiteral("仅允许 /dev/videoN 采集节点"));
        return;
    }

#ifndef BOARDLAB_HAS_GSTREAMER_APPSINK
    setPreviewState(QStringLiteral("预览不可用: 此构建未启用 GStreamer appsink"), false, false);
    setDetailText(QStringLiteral("请安装 GStreamer 1.0 的 core、app 和 video 开发接口后重新构建"));
    return;
#else
    QString initializationError;
    if (!initializeGstreamer(&initializationError)) {
        setPreviewState(QStringLiteral("预览失败: %1").arg(initializationError), false, false);
        setDetailText(QStringLiteral("GStreamer 初始化失败，未启动 Qt CameraBin"));
        return;
    }

    // The RKISP mainpath driver otherwise fixates an unconstrained v4l2src at
    // the full sensor's first advertised mode (2592x1944 UYVY at 120 fps on
    // the RK3588 test board).  That is useful for recording but excessive for
    // a small Qt Quick preview.  Ask for the proven NV12 display envelope
    // first; if a different sensor cannot provide it, the error path below
    // reopens the same node with ordinary GStreamer negotiation.
    const bool useIspPreviewProfile = isRockchipIspMainpathDevice(m_devicePath)
                                      && !m_ispPreviewProfileFallbackUsed;
    const QString sensorModel = boardlab::cameraSensorModelForDevice(m_devicePath);
    const QString rotateMethod = boardlab::cameraPreviewRotateMethod(sensorModel);
    const QString rotateProperty = boardlab::cameraPreviewVideoflipPropertyName();
    const QString rotateValue = boardlab::cameraPreviewVideoflipValue(rotateMethod);
    const bool rotateOv5695 = !rotateValue.isEmpty();

    GstElement *pipeline = gst_pipeline_new("boardlab-v4l2-preview");
    GstElement *source = gst_element_factory_make("v4l2src", "source");
    GstElement *captureProfile = useIspPreviewProfile
                                     ? gst_element_factory_make("capsfilter", "rkisp-preview-profile")
                                     : nullptr;
    GstElement *queue = gst_element_factory_make("queue", "preview-queue");
    // appsink has no display-sink rotate-method property.  videoflip's
    // method=90l is the portable frame-level equivalent and works with the
    // GStreamer releases in both supported desktop distributions and
    // Buildroot images.
    GstElement *rotate = rotateOv5695
                            ? gst_element_factory_make("videoflip", "ov5695-rotate")
                            : nullptr;
    GstElement *convert = gst_element_factory_make("videoconvert", "preview-convert");
    GstElement *filter = gst_element_factory_make("capsfilter", "preview-format");
    GstElement *sink = gst_element_factory_make("appsink", "preview-sink");
    if (!pipeline || !source || !queue || !convert || !filter || !sink
        || (useIspPreviewProfile && !captureProfile)
        || (rotateOv5695 && !rotate)) {
        if (pipeline) {
            gst_object_unref(pipeline);
        }
        setPreviewState(QStringLiteral("预览失败: 缺少 GStreamer 视频组件"), false, false);
        setDetailText(rotateOv5695
                          ? QStringLiteral("OV5695 预览需要 v4l2src、capsfilter、videoflip、videoconvert 与 appsink")
                          : QStringLiteral("需要 v4l2src、capsfilter、videoconvert 与 appsink"));
        return;
    }

    const QByteArray deviceName = m_devicePath.toUtf8();
    g_object_set(source,
                 "device", deviceName.constData(),
                 "do-timestamp", TRUE,
                 nullptr);
    g_object_set(queue,
                 "leaky", 2,
                 "max-size-buffers", 2,
                 "max-size-bytes", 0,
                 "max-size-time", static_cast<guint64>(0),
                 nullptr);
    g_object_set(sink,
                 "emit-signals", TRUE,
                 "drop", TRUE,
                 "max-buffers", 1,
                 "sync", FALSE,
                 "enable-last-sample", FALSE,
                 nullptr);
    if (rotate) {
        const QByteArray rotatePropertyName = rotateProperty.toUtf8();
        const QByteArray rotateValueName = rotateValue.toUtf8();
        gst_util_set_object_arg(G_OBJECT(rotate),
                                rotatePropertyName.constData(),
                                rotateValueName.constData());
    }

    if (captureProfile) {
        // NV12 is mapped by v4l2src to RKISP's valid NM12 two-plane capture
        // format.  The range keeps the request adaptive to the media graph,
        // while making 1280x800@30 the normal fixation on the RK3588 ISP.
        GstCaps *captureCaps = gst_caps_from_string(
            "video/x-raw,format=(string)NV12,"
            "width=(int)[32,1280],height=(int)[32,800],"
            "framerate=(fraction)[1/1,30/1]");
        if (!captureCaps) {
            gst_object_unref(pipeline);
            setPreviewState(QStringLiteral("预览失败: 无法创建 RKISP 预览格式"), false, false);
            setDetailText(QStringLiteral("无法创建 NV12 摄像头预览能力集"));
            return;
        }
        g_object_set(captureProfile, "caps", captureCaps, nullptr);
        gst_caps_unref(captureCaps);
    }

    GstCaps *caps = gst_caps_new_simple("video/x-raw",
                                        "format", G_TYPE_STRING, "BGRA",
                                        nullptr);
    g_object_set(filter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    gst_bin_add_many(GST_BIN(pipeline), source, queue, convert, filter, sink, nullptr);
    if (captureProfile) {
        gst_bin_add(GST_BIN(pipeline), captureProfile);
    }
    if (rotate) {
        gst_bin_add(GST_BIN(pipeline), rotate);
    }
    GstElement *previous = source;
    bool linked = true;
    const auto linkNext = [&linked, &previous](GstElement *next) {
        if (linked) {
            linked = gst_element_link(previous, next);
            if (linked) {
                previous = next;
            }
        }
    };
    if (captureProfile) {
        linkNext(captureProfile);
    }
    linkNext(queue);
    if (rotate) {
        linkNext(rotate);
    }
    linkNext(convert);
    linkNext(filter);
    linkNext(sink);
    if (!linked) {
        gst_object_unref(pipeline);
        if (useIspPreviewProfile) {
            m_ispPreviewProfileFallbackUsed = true;
            setPreviewState(QStringLiteral("RKISP 预览格式不兼容，正在自动重试…"), false, false);
            setDetailText(QStringLiteral("NV12 低延迟预览未能连接，正在回退到通用格式协商"));
            scheduleRestart();
            return;
        }
        setPreviewState(QStringLiteral("预览失败: 无法协商 V4L2 视频格式"), false, false);
        setDetailText(QStringLiteral("未使用 Qt CameraBin；请检查 v4l2src 到 appsink 的格式链路"));
        return;
    }

    m_pipeline = pipeline;
    m_appSink = GST_APP_SINK(sink);
    m_bus = gst_element_get_bus(m_pipeline);
    m_usingIspPreviewProfile = useIspPreviewProfile;
    m_acceptFrames.store(true);
    m_waitingForFirstFrame = true;
    m_firstFrameWaitReported = false;
    m_firstFrameTimer.restart();
    g_signal_connect(m_appSink, "new-sample", G_CALLBACK(&GstVideoPreviewItem::onNewSample), this);

    const GstStateChangeReturn stateResult = gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    if (stateResult == GST_STATE_CHANGE_FAILURE) {
        const bool retryWithGenericProfile = m_usingIspPreviewProfile;
        stopPipeline();
        if (retryWithGenericProfile) {
            m_ispPreviewProfileFallbackUsed = true;
            setPreviewState(QStringLiteral("RKISP 预览格式不兼容，正在自动重试…"), false, false);
            setDetailText(QStringLiteral("NV12 低延迟预览无法启动，正在回退到通用格式协商"));
            scheduleRestart();
            return;
        }
        setPreviewState(QStringLiteral("预览失败: 无法启动 GStreamer V4L2 管线"), false, false);
        setDetailText(QStringLiteral("%1 未能切换到 PLAYING 状态").arg(m_devicePath));
        return;
    }

    m_busTimer->start();
    setPreviewState(QStringLiteral("正在连接%1…").arg(m_sourceTitle), true, false);
    const QString rotationDetail = rotateOv5695
                                       ? QStringLiteral("；OV5695 已应用 videoflip %1=%2（等价于 rotate-method=90l）")
                                             .arg(rotateProperty, rotateValue)
                                       : QString();
    if (useIspPreviewProfile) {
        setDetailText(QStringLiteral("GStreamer appsink: %1 优先协商 RKISP NV12 低延迟预览（最大 1280×800 / 30fps）%2")
                          .arg(m_devicePath, rotationDetail));
    } else if (m_ispPreviewProfileFallbackUsed) {
        setDetailText(QStringLiteral("GStreamer appsink: %1 正在使用 RKISP 通用格式协商（低延迟配置不受当前节点支持）%2")
                          .arg(m_devicePath, rotationDetail));
    } else {
        setDetailText(QStringLiteral("GStreamer appsink: %1 自动协商格式、分辨率与帧率后内嵌预览%2")
                          .arg(m_devicePath, rotationDetail));
    }
#endif
}

void GstVideoPreviewItem::pollBus()
{
#ifdef BOARDLAB_HAS_GSTREAMER_APPSINK
    if (!m_bus) {
        return;
    }

    while (GstMessage *message = gst_bus_pop_filtered(m_bus,
                                                        static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError *error = nullptr;
            gchar *debugText = nullptr;
            gst_message_parse_error(message, &error, &debugText);
            const QString detail = gstreamerMessageText(error, debugText);
            if (error) {
                g_error_free(error);
            }
            g_free(debugText);
            gst_message_unref(message);
            const bool retryWithGenericProfile = m_usingIspPreviewProfile
                                                 && !m_ispPreviewProfileFallbackUsed;
            stopPipeline();
            if (retryWithGenericProfile) {
                m_ispPreviewProfileFallbackUsed = true;
                setPreviewState(QStringLiteral("RKISP 预览格式不兼容，正在自动重试…"), false, false);
                setDetailText(QStringLiteral("NV12 低延迟预览启动失败（%1）；正在回退到通用格式协商")
                                  .arg(detail));
                scheduleRestart();
                return;
            }
            setPreviewState(QStringLiteral("预览失败: %1").arg(detail), false, false);
            setDetailText(QStringLiteral("GStreamer appsink 管线错误: %1").arg(m_devicePath));
            return;
        }

        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS) {
            gst_message_unref(message);
            stopPipeline();
            setPreviewState(QStringLiteral("预览结束: 视频源已停止输出"), false, false);
            setDetailText(QStringLiteral("%1 未再输出视频帧").arg(m_devicePath));
            return;
        }

        gst_message_unref(message);
    }

    // HDMI-RX advertises capture caps even when its input connector has no
    // lock. Keep the pipeline alive so a hot-plugged source starts without a
    // second button press, but replace the ambiguous connecting message with
    // an actionable status after a short first-frame grace period.
    if (m_waitingForFirstFrame
        && !m_firstFrameWaitReported
        && m_firstFrameTimer.isValid()
        && m_firstFrameTimer.elapsed() >= 3000) {
        m_firstFrameWaitReported = true;
        if (m_sourceTitle == QStringLiteral("HDMI 输入")) {
            setPreviewState(QStringLiteral("等待 HDMI 输入信号"), true, false);
            setDetailText(QStringLiteral("GStreamer appsink 已打开 %1，但尚未收到有效 HDMI 帧；请检查信号源、线缆和 HDMI-RX 输入端口")
                              .arg(m_devicePath));
        } else {
            setPreviewState(QStringLiteral("正在等待%1视频帧").arg(m_sourceTitle), true, false);
        }
    }
#endif
}

void GstVideoPreviewItem::applyFrame(const QImage &frame, std::uint64_t frameGeneration)
{
    if (frameGeneration != m_frameGeneration.load()) {
        return;
    }

    m_frameDeliveryPending.store(false);
    if (!m_requested || !m_acceptFrames.load() || frame.isNull()) {
        return;
    }

    m_waitingForFirstFrame = false;
    m_firstFrameWaitReported = false;
    {
        QMutexLocker locker(&m_frameMutex);
        m_frame = frame;
    }
    update();
    setPreviewState(QStringLiteral("实时画面正在显示"), true, true);
    emit frameAvailable();
}

void GstVideoPreviewItem::refreshMirroredFrame()
{
    if (!m_requested || !m_frameSource) {
        return;
    }

    const QImage sourceFrame = m_frameSource->latestFrame();
    {
        QMutexLocker locker(&m_frameMutex);
        m_frame = sourceFrame;
    }
    update();
}

void GstVideoPreviewItem::refreshMirroredState()
{
    if (!m_requested || !m_frameSource) {
        return;
    }

    setPreviewState(m_frameSource->statusText(), m_frameSource->running(), m_frameSource->active());
    setDetailText(QStringLiteral("复用%1的 GStreamer appsink 采集管线；大屏不会重复打开 V4L2 节点")
                      .arg(m_sourceTitle));
    refreshMirroredFrame();
}

void GstVideoPreviewItem::stopPipeline()
{
    m_acceptFrames.store(false);
    m_usingIspPreviewProfile = false;
    m_waitingForFirstFrame = false;
    m_firstFrameWaitReported = false;
    m_firstFrameTimer.invalidate();
    // Drop any GUI event queued by the old pipeline before a restart.  The
    // generation check in applyFrame prevents a stale event from clearing the
    // delivery gate of a newly opened V4L2 source.
    m_frameGeneration.fetch_add(1);
    m_frameDeliveryPending.store(false);
    if (m_busTimer) {
        m_busTimer->stop();
    }

#ifdef BOARDLAB_HAS_GSTREAMER_APPSINK
    if (m_appSink) {
        g_signal_handlers_disconnect_by_data(m_appSink, this);
    }
    if (m_pipeline) {
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_element_get_state(m_pipeline, nullptr, nullptr, GST_MSECOND * 300);
        gst_object_unref(m_pipeline);
    }
    if (m_bus) {
        gst_object_unref(m_bus);
    }
    m_pipeline = nullptr;
    m_appSink = nullptr;
    m_bus = nullptr;
#endif

    clearFrame();
    setPreviewState(m_statusText, false, false);
}

void GstVideoPreviewItem::setPreviewState(const QString &statusText, bool running, bool active)
{
    if (m_statusText != statusText) {
        m_statusText = statusText;
        emit statusTextChanged();
    }
    if (m_running != running) {
        m_running = running;
        emit runningChanged();
    }
    if (m_active != active) {
        m_active = active;
        emit activeChanged();
    }
}

void GstVideoPreviewItem::setDetailText(const QString &detailText)
{
    if (m_detailText == detailText) {
        return;
    }

    m_detailText = detailText;
    emit detailTextChanged();
}

void GstVideoPreviewItem::clearFrame()
{
    {
        QMutexLocker locker(&m_frameMutex);
        if (m_frame.isNull()) {
            return;
        }
        m_frame = QImage();
    }
    update();
}

QImage GstVideoPreviewItem::latestFrame() const
{
    QMutexLocker locker(&m_frameMutex);
    return m_frame;
}

void GstVideoPreviewItem::disconnectFrameSource()
{
    QObject::disconnect(m_frameSourceFrameConnection);
    QObject::disconnect(m_frameSourceStateConnection);
    QObject::disconnect(m_frameSourceRunningConnection);
    QObject::disconnect(m_frameSourceActiveConnection);
    QObject::disconnect(m_frameSourceDestroyedConnection);
    m_frameSourceFrameConnection = QMetaObject::Connection();
    m_frameSourceStateConnection = QMetaObject::Connection();
    m_frameSourceRunningConnection = QMetaObject::Connection();
    m_frameSourceActiveConnection = QMetaObject::Connection();
    m_frameSourceDestroyedConnection = QMetaObject::Connection();
    m_frameSource = nullptr;
}

#ifdef BOARDLAB_HAS_GSTREAMER_APPSINK
GstFlowReturn GstVideoPreviewItem::onNewSample(GstAppSink *sink, gpointer userData)
{
    auto *item = static_cast<GstVideoPreviewItem *>(userData);
    if (!item || !item->m_acceptFrames.load()) {
        return GST_FLOW_FLUSHING;
    }

    GstSample *sample = gst_app_sink_pull_sample(sink);
    if (!sample) {
        return GST_FLOW_EOS;
    }

    // Drain the appsink even when Qt is repainting a fullscreen frame, but do
    // not spend CPU converting/copying frames that cannot be delivered yet.
    if (item->m_frameDeliveryPending.load()) {
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    GstVideoInfo info;
    gst_video_info_init(&info);
    QImage ownedFrame;
    if (caps && buffer
        && gst_video_info_from_caps(&info, caps)
        && GST_VIDEO_INFO_FORMAT(&info) == GST_VIDEO_FORMAT_BGRA) {
        GstVideoFrame videoFrame;
        if (gst_video_frame_map(&videoFrame, &info, buffer, GST_MAP_READ)) {
            const int width = GST_VIDEO_FRAME_WIDTH(&videoFrame);
            const int height = GST_VIDEO_FRAME_HEIGHT(&videoFrame);
            const int stride = GST_VIDEO_FRAME_PLANE_STRIDE(&videoFrame, 0);
            const auto *pixels = GST_VIDEO_FRAME_PLANE_DATA(&videoFrame, 0);
            if (pixels && width > 0 && height > 0 && stride >= width * 4) {
                ownedFrame = QImage(static_cast<const uchar *>(pixels), width, height, stride,
                                    QImage::Format_ARGB32).copy();
            }
            gst_video_frame_unmap(&videoFrame);
        }
    }
    gst_sample_unref(sample);

    const std::uint64_t frameGeneration = item->m_frameGeneration.load();
    if (!ownedFrame.isNull() && item->m_acceptFrames.load()) {
        bool expected = false;
        if (item->m_frameDeliveryPending.compare_exchange_strong(expected, true)) {
            const bool queued = QMetaObject::invokeMethod(item,
                                                           [item, frame = std::move(ownedFrame), frameGeneration] {
                                                               item->applyFrame(frame, frameGeneration);
                                                           },
                                                           Qt::QueuedConnection);
            if (!queued && frameGeneration == item->m_frameGeneration.load()) {
                item->m_frameDeliveryPending.store(false);
            }
        }
    }
    return GST_FLOW_OK;
}
#endif
