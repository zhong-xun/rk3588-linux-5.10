#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

class SystemInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double cpuUsage READ cpuUsage NOTIFY changed)
    Q_PROPERTY(bool cpuUsageValid READ cpuUsageValid NOTIFY changed)
    Q_PROPERTY(int cpuCoreCount READ cpuCoreCount CONSTANT)
    Q_PROPERTY(QString cpuLoadText READ cpuLoadText NOTIFY changed)
    Q_PROPERTY(double memoryUsage READ memoryUsage NOTIFY changed)
    Q_PROPERTY(QString memoryText READ memoryText NOTIFY changed)
    Q_PROPERTY(double diskUsage READ diskUsage NOTIFY changed)
    Q_PROPERTY(QString diskText READ diskText NOTIFY changed)
    Q_PROPERTY(QString temperatureText READ temperatureText NOTIFY changed)
    Q_PROPERTY(QString uptimeText READ uptimeText NOTIFY changed)
    Q_PROPERTY(QString kernelText READ kernelText CONSTANT)
    Q_PROPERTY(QString networkText READ networkText NOTIFY changed)

public:
    explicit SystemInfo(QObject *parent = nullptr);

    double cpuUsage() const;
    bool cpuUsageValid() const;
    int cpuCoreCount() const;
    QString cpuLoadText() const;
    double memoryUsage() const;
    QString memoryText() const;
    double diskUsage() const;
    QString diskText() const;
    QString temperatureText() const;
    QString uptimeText() const;
    QString kernelText() const;
    QString networkText() const;

    Q_INVOKABLE void refresh();

signals:
    void changed();

private:
    struct CpuSample {
        quint64 user = 0;
        quint64 nice = 0;
        quint64 system = 0;
        quint64 idle = 0;
        quint64 iowait = 0;
        quint64 irq = 0;
        quint64 softirq = 0;
        quint64 steal = 0;
    };

    static CpuSample readCpuSample();
    static QString readFirstLine(const QString &path);
    static QString formatBytes(quint64 kib);

    QTimer m_refreshTimer;
    CpuSample m_lastCpu;
    bool m_hasCpuSample = false;
    bool m_cpuUsageValid = false;
    int m_cpuCoreCount = 1;
    double m_cpuUsage = 0.0;
    QString m_cpuLoadText;
    double m_memoryUsage = 0.0;
    double m_diskUsage = 0.0;
    QString m_memoryText;
    QString m_diskText;
    QString m_temperatureText;
    QString m_uptimeText;
    QString m_kernelText;
    QString m_networkText;
};
