#include "systeminfo.h"

#include "qtcompat.h"

#include <QDir>
#include <QFile>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTextStream>
#include <QThread>

#include <cmath>
#include <sys/statvfs.h>

namespace {

QString thermalRootPath()
{
    if (qEnvironmentVariable("BOARDLAB_TEST_MODE") == QStringLiteral("1")) {
        const QString testRoot = QString::fromLocal8Bit(qgetenv("BOARDLAB_THERMAL_ROOT")).trimmed();
        if (!testRoot.isEmpty()) {
            return QDir::cleanPath(testRoot);
        }
    }
    return QStringLiteral("/sys/class/thermal");
}

int thermalZonePriority(const QString &type)
{
    const QString normalized = type.trimmed().toLower();
    const QStringList excludedTokens = QStringList()
        << QStringLiteral("battery")
        << QStringLiteral("gpu")
        << QStringLiteral("npu")
        << QStringLiteral("ambient");
    for (const QString &token : excludedTokens) {
        if (normalized.contains(token)) {
            return -1;
        }
    }

    if (normalized.contains(QStringLiteral("cpu"))) {
        return 2;
    }
    if (normalized.contains(QStringLiteral("soc"))) {
        return 1;
    }
    return 0;
}

} // namespace

SystemInfo::SystemInfo(QObject *parent)
    : QObject(parent)
{
    m_kernelText = QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion();
    m_cpuCoreCount = qMax(1, QThread::idealThreadCount());
    refresh();
    m_refreshTimer.setInterval(2000);
    m_refreshTimer.setTimerType(Qt::CoarseTimer);
    connect(&m_refreshTimer, &QTimer::timeout, this, &SystemInfo::refresh);
    m_refreshTimer.start();
}

double SystemInfo::cpuUsage() const
{
    return m_cpuUsage;
}

bool SystemInfo::cpuUsageValid() const
{
    return m_cpuUsageValid;
}

int SystemInfo::cpuCoreCount() const
{
    return m_cpuCoreCount;
}

QString SystemInfo::cpuLoadText() const
{
    return m_cpuLoadText;
}

double SystemInfo::memoryUsage() const
{
    return m_memoryUsage;
}

QString SystemInfo::memoryText() const
{
    return m_memoryText;
}

double SystemInfo::diskUsage() const
{
    return m_diskUsage;
}

QString SystemInfo::diskText() const
{
    return m_diskText;
}

QString SystemInfo::temperatureText() const
{
    return m_temperatureText;
}

QString SystemInfo::uptimeText() const
{
    return m_uptimeText;
}

QString SystemInfo::kernelText() const
{
    return m_kernelText;
}

QString SystemInfo::networkText() const
{
    return m_networkText;
}

void SystemInfo::refresh()
{
    const bool oldCpuUsageValid = m_cpuUsageValid;
    const double oldCpuUsage = m_cpuUsage;
    const double oldMemoryUsage = m_memoryUsage;
    const double oldDiskUsage = m_diskUsage;
    const QString oldCpuLoadText = m_cpuLoadText;
    const QString oldMemoryText = m_memoryText;
    const QString oldDiskText = m_diskText;
    const QString oldTemperatureText = m_temperatureText;
    const QString oldUptimeText = m_uptimeText;
    const QString oldNetworkText = m_networkText;

    const CpuSample current = readCpuSample();
    const quint64 lastTotal = m_lastCpu.user + m_lastCpu.nice + m_lastCpu.system + m_lastCpu.idle
        + m_lastCpu.iowait + m_lastCpu.irq + m_lastCpu.softirq + m_lastCpu.steal;
    const quint64 currentTotal = current.user + current.nice + current.system + current.idle
        + current.iowait + current.irq + current.softirq + current.steal;
    const quint64 lastIdle = m_lastCpu.idle + m_lastCpu.iowait;
    const quint64 currentIdle = current.idle + current.iowait;

    if (m_hasCpuSample && currentTotal > lastTotal) {
        const quint64 totalDelta = currentTotal - lastTotal;
        const quint64 idleDelta = currentIdle > lastIdle ? currentIdle - lastIdle : 0;
        const double sampledUsage = totalDelta > 0 ? (100.0 * static_cast<double>(totalDelta - idleDelta) / static_cast<double>(totalDelta)) : 0.0;
        m_cpuUsage = m_cpuUsageValid ? (m_cpuUsage * 0.35 + sampledUsage * 0.65) : sampledUsage;
        m_cpuUsageValid = true;
    }
    m_lastCpu = current;
    m_hasCpuSample = true;

    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    QFile loadFile(QStringLiteral("/proc/loadavg"));
    if (loadFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList fields = QString::fromUtf8(loadFile.readLine()).split(whitespace, boardlab::kSkipEmptyParts);
        if (fields.size() >= 3) {
            m_cpuLoadText = QStringLiteral("%1 %2 %3 / %4 cores")
                                .arg(fields.at(0), fields.at(1), fields.at(2))
                                .arg(m_cpuCoreCount);
        }
    } else {
        m_cpuLoadText = QStringLiteral("load unavailable / %1 cores").arg(m_cpuCoreCount);
    }

    QFile memFile(QStringLiteral("/proc/meminfo"));
    quint64 memTotal = 0;
    quint64 memAvailable = 0;
    if (memFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QStringList lines = QString::fromUtf8(memFile.readAll()).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            // Prefix match avoids recompiling QRegExp / QRegularExpression on
            // every /proc/meminfo line; only two keys are consumed.
            if (line.startsWith(QStringLiteral("MemTotal:"))) {
                const QStringList parts = line.split(whitespace, boardlab::kSkipEmptyParts);
                if (parts.size() >= 2) {
                    memTotal = parts.at(1).toULongLong();
                }
            } else if (line.startsWith(QStringLiteral("MemAvailable:"))) {
                const QStringList parts = line.split(whitespace, boardlab::kSkipEmptyParts);
                if (parts.size() >= 2) {
                    memAvailable = parts.at(1).toULongLong();
                }
            }
            if (memTotal > 0 && memAvailable > 0) {
                break;
            }
        }
    }

    if (memTotal > 0) {
        const quint64 used = memTotal > memAvailable ? memTotal - memAvailable : 0;
        m_memoryUsage = 100.0 * static_cast<double>(used) / static_cast<double>(memTotal);
        m_memoryText = formatBytes(used) + QStringLiteral(" / ") + formatBytes(memTotal);
    } else {
        m_memoryUsage = 0.0;
        m_memoryText = QStringLiteral("Unavailable");
    }

    struct statvfs vfs;
    if (::statvfs("/", &vfs) == 0 && vfs.f_blocks > 0) {
        const quint64 totalKiB = static_cast<quint64>(vfs.f_blocks) * vfs.f_frsize / 1024;
        const quint64 freeKiB = static_cast<quint64>(vfs.f_bavail) * vfs.f_frsize / 1024;
        const quint64 usedKiB = totalKiB > freeKiB ? totalKiB - freeKiB : 0;
        m_diskUsage = 100.0 * static_cast<double>(usedKiB) / static_cast<double>(totalKiB);
        m_diskText = formatBytes(usedKiB) + QStringLiteral(" / ") + formatBytes(totalKiB);
    } else {
        m_diskUsage = 0.0;
        m_diskText = QStringLiteral("Unavailable");
    }

    m_temperatureText = QStringLiteral("Unavailable");
    QDir thermalDir(thermalRootPath());
    const QFileInfoList zones = thermalDir.entryInfoList(QStringList(QStringLiteral("thermal_zone*")), QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    int selectedPriority = -1;
    double selectedCelsius = 0.0;
    for (const QFileInfo &zone : zones) {
        const int priority = thermalZonePriority(
            readFirstLine(zone.absoluteFilePath() + QStringLiteral("/type")));
        if (priority < 0 || priority <= selectedPriority) {
            continue;
        }

        const QString raw = readFirstLine(zone.absoluteFilePath() + QStringLiteral("/temp"));
        bool ok = false;
        const double thermalValue = raw.toDouble(&ok);
        if (!ok || !std::isfinite(thermalValue)) {
            continue;
        }
        selectedPriority = priority;
        selectedCelsius = qAbs(thermalValue) >= 1000.0 ? thermalValue / 1000.0 : thermalValue;
    }
    if (selectedPriority >= 0) {
        m_temperatureText = QStringLiteral("%1 C").arg(selectedCelsius, 0, 'f', 1);
    }

    QFile uptimeFile(QStringLiteral("/proc/uptime"));
    if (uptimeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QByteArray data = uptimeFile.readLine().trimmed();
        const int seconds = static_cast<int>(data.split(' ').value(0).toDouble());
        const int days = seconds / 86400;
        const int hours = (seconds % 86400) / 3600;
        const int minutes = (seconds % 3600) / 60;
        if (days > 0) {
            m_uptimeText = QStringLiteral("%1d %2h %3m").arg(days).arg(hours).arg(minutes);
        } else {
            m_uptimeText = QStringLiteral("%1h %2m").arg(hours).arg(minutes);
        }
    } else {
        m_uptimeText = QStringLiteral("Unavailable");
    }

    QStringList names;
    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || iface.flags() & QNetworkInterface::IsLoopBack) {
            continue;
        }
        names.append(iface.humanReadableName());
    }
    m_networkText = names.isEmpty() ? QStringLiteral("No active interface") : names.join(QStringLiteral(", "));

    const bool changedData = oldCpuUsageValid != m_cpuUsageValid
        || qAbs(oldCpuUsage - m_cpuUsage) >= 0.5
        || qAbs(oldMemoryUsage - m_memoryUsage) >= 0.5
        || qAbs(oldDiskUsage - m_diskUsage) >= 0.5
        || oldCpuLoadText != m_cpuLoadText
        || oldMemoryText != m_memoryText
        || oldDiskText != m_diskText
        || oldTemperatureText != m_temperatureText
        || oldUptimeText != m_uptimeText
        || oldNetworkText != m_networkText;
    if (changedData) {
        emit changed();
    }
}

SystemInfo::CpuSample SystemInfo::readCpuSample()
{
    CpuSample sample;
    QFile file(QStringLiteral("/proc/stat"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return sample;
    }

    QTextStream stream(&file);
    const QString line = stream.readLine();
    QString cpuLine = line;
    QTextStream parser(&cpuLine, QIODevice::ReadOnly);
    QString label;
    parser >> label >> sample.user >> sample.nice >> sample.system >> sample.idle
           >> sample.iowait >> sample.irq >> sample.softirq >> sample.steal;
    return sample;
}

QString SystemInfo::readFirstLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readLine()).trimmed();
}

QString SystemInfo::formatBytes(quint64 kib)
{
    const double mib = static_cast<double>(kib) / 1024.0;
    if (mib >= 1024.0) {
        return QStringLiteral("%1 GB").arg(mib / 1024.0, 0, 'f', 1);
    }
    return QStringLiteral("%1 MB").arg(mib, 0, 'f', 0);
}
