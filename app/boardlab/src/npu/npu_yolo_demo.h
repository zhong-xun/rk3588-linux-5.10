#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantMap>

class BoardProfile;
class QThread;

// A deliberately small YOLOv8 demonstration with known defaults for RK3562,
// RK3568, RK3576, and RK3588, plus explicit board-manifest configuration for
// other RKNN targets.
//
// The public header deliberately has no OpenCV or RKNN dependency.  BoardLab
// can therefore build and expose a useful unavailable state on desktop hosts
// and on boards without the optional NPU runtime.  The implementation only
// creates the RKNN detector inside its worker after discover() has verified
// the board policy and fixed demo assets. RKNN initialization is the hardware
// compatibility authority; optional telemetry does not gate inference.
class NpuYoloDemo final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap state READ status NOTIFY statusChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)

public:
    // The demo remains tied to an explicitly enabled board profile. Unknown
    // SoCs additionally require a validated npuYolo manifest object.
    explicit NpuYoloDemo(BoardProfile *boardProfile, QObject *parent = nullptr);
    ~NpuYoloDemo() override;

    QVariantMap status() const;
    QVariantMap state() const;
    bool running() const;
    QString log() const;

    // Re-resolves the fixed model, labels and non-looping sample video.  The
    // value includes absolute paths and an actionable unavailable reason.
    Q_INVOKABLE QVariantMap discover();
    // Starts one non-looping pass over the bundled AI NVR sample video.
    // There are no caller-provided paths or detector options.
    Q_INVOKABLE bool start();
    // Stops after the current frame.  RKNN calls and detector destruction
    // always remain owned by the worker thread.
    Q_INVOKABLE bool stop();

signals:
    void statusChanged();
    void runningChanged();
    void logChanged();
    void finished(bool success, const QVariantMap &status);

private:
    class Runner;

    void applyWorkerStatus(const QVariantMap &status);
    void handleRunnerFinished();
    void setStatus(const QVariantMap &status);

    QVariantMap m_status;
    QString m_log;
    QPointer<BoardProfile> m_boardProfile;
    Runner *m_runner = nullptr;
};
