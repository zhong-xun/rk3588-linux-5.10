#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QVariantMap>

class QTimer;

// A deliberately narrow wrapper around the GLES2 glmark2 benchmark.
//
// The service never accepts a program name or command-line parameters from a
// caller. It runs one short, off-screen preset for repeatable scoring. The
// separate preview state drives the Qt Quick ShaderEffect embedded in the GPU
// page, so it never creates a second Weston/X11 window or launches a second
// glmark2 process.
class GpuBenchmark final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap state READ status NOTIFY statusChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool previewRunning READ previewRunning NOTIFY previewRunningChanged)
    Q_PROPERTY(QString log READ log NOTIFY logChanged)

public:
    explicit GpuBenchmark(QObject *parent = nullptr);
    ~GpuBenchmark() override;

    QVariantMap status() const;
    // Compatibility-friendly alias for consumers that use the common
    // BoardLab "state" convention.
    QVariantMap state() const;
    bool running() const;
    bool previewRunning() const;
    QString log() const;

    // These are intentionally fixed. Callers must not add scenarios, display
    // settings, or arbitrary command-line switches. fixedPreviewArguments()
    // is descriptive metadata for the embedded Qt Quick surface, not a child
    // process command line.
    static QStringList candidatePrograms();
    static QStringList fixedArguments();
    static QStringList fixedPreviewArguments();
    static int parseScore(const QString &text);
    static QString parseRenderer(const QString &text);

    // Re-checks the fixed executable candidates.  It is safe to call while
    // idle; an active benchmark keeps its current runtime state unchanged.
    Q_INVOKABLE QVariantMap discover();
    // Starts one fixed short benchmark.  Returns false synchronously when
    // unavailable or already active; completion is delivered by finished().
    Q_INVOKABLE bool start();
    // Requests a graceful stop, then kills the child once after a short
    // grace period if it did not exit.  It never starts another process.
    Q_INVOKABLE bool stop();
    // Starts the Qt-page embedded real-time preview. It is continuous until
    // stopPreview() is called, and remains mutually exclusive with the fixed
    // off-screen score benchmark.
    Q_INVOKABLE bool startPreview();
    Q_INVOKABLE bool stopPreview();

signals:
    void statusChanged();
    void runningChanged();
    void previewRunningChanged();
    void logChanged();
    void finished(bool success, const QVariantMap &status);
    void previewFinished(bool success, const QVariantMap &status);

private slots:
    void onStandardOutputReady();
    void onStandardErrorReady();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onProcessError(QProcess::ProcessError error);
    void onElapsedTimer();
    void onStopGraceExpired();
    void onPreviewElapsedTimer();

private:
    struct DisplayPlan {
        QProcessEnvironment environment;
        QString display;
        QString strategy;
        QString target;
        QString message;
        bool usable = true;
    };

    QString resolveProgram() const;
    DisplayPlan displayPlan() const;
    void resetRunState(const QString &program, const DisplayPlan &displayPlan);
    void resetEmbeddedPreviewState();
    void appendLog(const QByteArray &bytes);
    void refreshParsedOutput();
    void publishElapsed();
    void publishPreviewElapsed();
    void updateStatus(const QVariantMap &changes);
    void finishRun(bool success, const QString &nextStatus, const QString &error,
                   int exitCode, QProcess::ExitStatus exitStatus);
    void finishPreview(bool success, const QString &nextStatus, const QString &error,
                       int exitCode, QProcess::ExitStatus exitStatus);

    QProcess *m_process = nullptr;
    QTimer *m_elapsedTimer = nullptr;
    QTimer *m_stopGraceTimer = nullptr;
    QTimer *m_previewElapsedTimer = nullptr;
    QElapsedTimer m_elapsedClock;
    QElapsedTimer m_previewElapsedClock;
    QVariantMap m_status;
    QByteArray m_logBytes;
    QString m_parseBuffer;
    bool m_stopRequested = false;
};
