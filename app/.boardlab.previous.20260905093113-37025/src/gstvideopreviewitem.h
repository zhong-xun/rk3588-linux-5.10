#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QMetaObject>
#include <QMutex>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QString>

#include <atomic>
#include <cstdint>

#ifdef BOARDLAB_HAS_GSTREAMER_APPSINK
#include <gst/app/gstappsink.h>
#include <gst/gst.h>
#endif

class QTimer;
class QPainter;

// A small, self-contained V4L2 preview item for the Buildroot/Wayland path.
//
// Qt Multimedia 5.15 uses CameraBin for Camera/VideoOutput.  RK3588 HDMI RX
// exposes a valid multi-planar V4L2 capture node, but CameraBin's format
// negotiation still rejects its single contiguous memory plane.  This item
// owns a deliberately simple GStreamer pipeline ending in appsink, copies the
// converted BGRA frame into a QImage, and paints it inside the existing Qt
// Quick scene.  RKISP mainpath receives one display-friendly NV12 attempt
// first: leaving its unconstrained default at a full sensor 120 fps creates
// enough CPU/memory pressure to make an otherwise healthy preview look noisy.
// Other sources retain generic GStreamer negotiation, and an unsupported
// RKISP profile automatically falls back to that generic mode.
class GstVideoPreviewItem : public QQuickPaintedItem
{
    Q_OBJECT

    Q_PROPERTY(QString devicePath READ devicePath WRITE setDevicePath NOTIFY devicePathChanged)
    Q_PROPERTY(bool requested READ requested WRITE setRequested NOTIFY requestedChanged)
    Q_PROPERTY(QString sourceTitle READ sourceTitle WRITE setSourceTitle NOTIFY sourceTitleChanged)
    Q_PROPERTY(QObject *frameSource READ frameSource WRITE setFrameSource NOTIFY frameSourceChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QString detailText READ detailText NOTIFY detailTextChanged)

public:
    explicit GstVideoPreviewItem(QQuickItem *parent = nullptr);
    ~GstVideoPreviewItem() override;

    QString devicePath() const;
    void setDevicePath(const QString &devicePath);

    bool requested() const;
    void setRequested(bool requested);

    QString sourceTitle() const;
    void setSourceTitle(const QString &sourceTitle);

    QObject *frameSource() const;
    void setFrameSource(QObject *frameSource);

    bool running() const;
    bool active() const;
    QString statusText() const;
    QString detailText() const;

    void paint(QPainter *painter) override;

signals:
    void devicePathChanged();
    void requestedChanged();
    void sourceTitleChanged();
    void frameSourceChanged();
    void runningChanged();
    void activeChanged();
    void statusTextChanged();
    void detailTextChanged();
    void frameAvailable();

private slots:
    void restartPipeline();
    void pollBus();
    void applyFrame(const QImage &frame, std::uint64_t frameGeneration);
    void refreshMirroredFrame();
    void refreshMirroredState();

private:
    void scheduleRestart();
    void stopPipeline();
    void setPreviewState(const QString &statusText, bool running, bool active);
    void setDetailText(const QString &detailText);
    void clearFrame();
    QImage latestFrame() const;
    void disconnectFrameSource();

#ifdef BOARDLAB_HAS_GSTREAMER_APPSINK
    static GstFlowReturn onNewSample(GstAppSink *sink, gpointer userData);

    GstElement *m_pipeline = nullptr;
    GstAppSink *m_appSink = nullptr;
    GstBus *m_bus = nullptr;
#endif

    QString m_devicePath;
    QString m_sourceTitle = QStringLiteral("视频输入");
    QString m_statusText = QStringLiteral("实时预览未启动");
    QString m_detailText;
    QImage m_frame;
    mutable QMutex m_frameMutex;
    QTimer *m_busTimer = nullptr;
    QPointer<GstVideoPreviewItem> m_frameSource;
    QMetaObject::Connection m_frameSourceFrameConnection;
    QMetaObject::Connection m_frameSourceStateConnection;
    QMetaObject::Connection m_frameSourceRunningConnection;
    QMetaObject::Connection m_frameSourceActiveConnection;
    QMetaObject::Connection m_frameSourceDestroyedConnection;
    bool m_requested = false;
    bool m_running = false;
    bool m_active = false;
    bool m_restartPending = false;
    // Prefer the same modest RKISP format used by the validated camera test,
    // but never make a particular sensor resolution a hard application
    // requirement.  This flag records that the primary profile was rejected
    // for the current open request and the next restart must use generic caps.
    bool m_ispPreviewProfileFallbackUsed = false;
    bool m_usingIspPreviewProfile = false;
    QElapsedTimer m_firstFrameTimer;
    bool m_waitingForFirstFrame = false;
    bool m_firstFrameWaitReported = false;
    std::atomic_bool m_acceptFrames{false};
    // An appsink callback runs on a streaming thread.  Keep at most one frame
    // delivery queued to the Qt GUI thread: fullscreen painting can be more
    // expensive than capture, and an unbounded event queue otherwise makes
    // the whole desktop appear frozen.
    std::atomic_bool m_frameDeliveryPending{false};
    std::atomic_uint64_t m_frameGeneration{0};
};
