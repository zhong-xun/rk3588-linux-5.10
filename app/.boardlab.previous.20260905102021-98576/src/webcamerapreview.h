#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QTemporaryDir;
class QTimer;

// Owns the short-lived, browser-facing camera preview pipeline.
//
// The desktop QML preview paints frames directly into Qt Quick. Web clients
// cannot safely attach their bearer token to an <img> URL, so this service
// produces a small rolling JPEG snapshot and keeps the most recently complete
// frame in memory for LocalWebServer to return through an authenticated route.
// The requested V4L2 device is always validated by BoardBackend before it
// reaches this class.
class WebCameraPreview : public QObject
{
    Q_OBJECT

public:
    explicit WebCameraPreview(QObject *parent = nullptr);
    ~WebCameraPreview() override;

    bool start(const QString &devicePath, const QString &mode);
    bool stop();

    QVariantMap status() const;
    QByteArray latestJpeg() const;
    bool running() const;
    bool active() const;

signals:
    void changed();

private:
    bool ensureFrameDirectory();
    void clearFrames();
    void configureStart(const QString &devicePath, const QString &mode);
    void startProcess();
    void finishStopped();
    void fail(const QString &message, const QString &detail = QString());
    void updateState(const QString &state, const QString &message,
                     const QString &detail = QString(), const QString &error = QString());
    QStringList pipelineArguments() const;
    QString compactProcessOutput() const;

    void onProcessStarted();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void collectProcessOutput();
    void pollFrame();

    QProcess *m_process = nullptr;
    QTimer *m_framePollTimer = nullptr;
    QTemporaryDir *m_frameDirectory = nullptr;
    QString m_gstLaunch;
    QString m_devicePath;
    QString m_mode;
    QString m_sensorModel;
    QString m_rotateMethod;
    QString m_pendingDevicePath;
    QString m_pendingMode;
    QString m_state = QStringLiteral("idle");
    QString m_message = QStringLiteral("实时预览未启动");
    QString m_detail;
    QString m_error;
    QByteArray m_processOutput;
    QByteArray m_latestJpeg;
    QString m_latestFramePath;
    qint64 m_latestFrameModifiedMs = 0;
    quint64 m_frameSerial = 0;
    QDateTime m_lastFrameAt;
    QElapsedTimer m_frameClock;
    bool m_running = false;
    bool m_active = false;
    bool m_stopRequested = false;
    bool m_restartAfterStop = false;
    bool m_useMipiProfile = false;
    bool m_mipiFallbackUsed = false;
};
