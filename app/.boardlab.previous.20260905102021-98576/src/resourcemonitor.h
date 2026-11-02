#pragma once

#include "npu/npu_load_reader.h"

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QVariantMap>

// Read-only runtime telemetry for the board's CPU, RAM, devfreq devices, and
// thermal zones. RKNPU utilization is read by a bounded worker-thread call to
// BoardLab's fixed privileged helper; no telemetry operation alters state.
class ResourceMonitor final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY changed)

public:
    // Kept public only so the file-local parser can retain the previous
    // procfs sample without exposing it through QML or the snapshot API.
    struct CpuCounters {
        quint64 user = 0;
        quint64 nice = 0;
        quint64 system = 0;
        quint64 idle = 0;
        quint64 iowait = 0;
        quint64 irq = 0;
        quint64 softirq = 0;
        quint64 steal = 0;
    };

    explicit ResourceMonitor(QObject *parent = nullptr);

    // The root overload exists for deterministic tests and for embedding in a
    // restricted mount namespace.  Empty roots retain the production paths.
    ResourceMonitor(const QString &procRoot, const QString &sysRoot, QObject *parent = nullptr);

    QVariantMap snapshot() const;

    // Refreshes all telemetry synchronously.  The returned value is the same
    // object exposed through snapshot, which lets QML callers refresh and use
    // the current result without waiting for a signal.
    Q_INVOKABLE QVariantMap refresh();

    QString procRoot() const;
    QString sysRoot() const;

    // Test/embedding hook.  Calling this resets CPU delta sampling so the
    // first subsequent refresh reports utilization as initializing instead of
    // inventing a value from unrelated counters.
    void setRoots(const QString &procRoot, const QString &sysRoot);

signals:
    void changed();

private:
    void requestNpuUtilization();
    void updateSnapshotNpuUtilization();

    QString m_procRoot;
    QString m_sysRoot;
    QVariantMap m_snapshot;
    QVariantMap m_npuUtilization;
    QFutureWatcher<NpuLoadReading> m_npuLoadWatcher;
    quint64 m_npuLoadGeneration = 0;
    quint64 m_inFlightNpuLoadGeneration = 0;
    CpuCounters m_previousCpu;
    bool m_hasPreviousCpu = false;
    bool m_hasCpuUtilization = false;
    double m_lastCpuUtilization = 0.0;
    // Cached once per root pair: recursive /sys/devices/platform scans are too
    // expensive to redo on every refresh tick from the GUI thread.
    QStringList m_devfreqGpuPaths;
    QStringList m_devfreqNpuPaths;
    bool m_devfreqCacheValid = false;
};
