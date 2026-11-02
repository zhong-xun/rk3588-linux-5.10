#include "resourcemonitor.h"

#include "qtcompat.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtConcurrent/QtConcurrent>

namespace {

constexpr qint64 kMaximumReadBytes = 64 * 1024;
constexpr int kFallbackDevfreqScanLimit = 2048;

enum class FrequencyUnit {
    Kilohertz,
    Hertz
};

struct FileRead {
    bool ok = false;
    bool exists = false;
    bool authorizationRequired = false;
    QString value;
    QString error;
};

struct ParsedCounters {
    bool valid = false;
    ResourceMonitor::CpuCounters counters;
    int coreCount = 0;
    QString error;
    bool authorizationRequired = false;
};

QString normalizedRoot(const QString &root, const QString &fallback)
{
    const QString cleaned = root.trimmed();
    return cleaned.isEmpty() ? fallback : QDir::cleanPath(cleaned);
}

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool isAuthorizationError(const QString &error)
{
    const QString normalized = error.toLower();
    return normalized.contains(QStringLiteral("permission denied"))
        || normalized.contains(QStringLiteral("operation not permitted"))
        || normalized.contains(QStringLiteral("access denied"));
}

FileRead readTextFile(const QString &path)
{
    FileRead result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        // Only stat on the failure path; successful reads skip the extra QFileInfo.
        const QFileInfo info(path);
        result.exists = info.exists();
        if (!result.exists) {
            result.error = QStringLiteral("File is not available");
            return result;
        }
        result.error = file.errorString().trimmed();
        if (result.error.isEmpty()) {
            result.error = QStringLiteral("Could not open file for reading");
        }
        result.authorizationRequired = isAuthorizationError(result.error) || !info.isReadable();
        return result;
    }

    result.exists = true;
    const QByteArray bytes = file.read(kMaximumReadBytes + 1);
    if (file.error() != QFile::NoError) {
        result.error = file.errorString().trimmed();
        if (result.error.isEmpty()) {
            result.error = QStringLiteral("Could not read file");
        }
        const QFileInfo info(path);
        result.authorizationRequired = isAuthorizationError(result.error) || !info.isReadable();
        return result;
    }
    if (bytes.size() > kMaximumReadBytes) {
        result.error = QStringLiteral("Metric file exceeds the read limit");
        return result;
    }

    result.ok = true;
    result.value = QString::fromUtf8(bytes).remove(QChar('\0')).trimmed();
    return result;
}

QVariantMap unavailableMetric(const QString &path, const FileRead &read)
{
    QVariantMap metric;
    metric.insert(QStringLiteral("path"), path);
    metric.insert(QStringLiteral("available"), false);
    metric.insert(QStringLiteral("valid"), false);
    metric.insert(QStringLiteral("authorizationRequired"), read.authorizationRequired);
    metric.insert(QStringLiteral("status"), read.authorizationRequired
                  ? QStringLiteral("authorizationRequired")
                  : QStringLiteral("unavailable"));
    metric.insert(QStringLiteral("error"), read.error.isEmpty()
                  ? QStringLiteral("Metric is unavailable")
                  : read.error);
    return metric;
}

QVariantMap textMetric(const QString &path)
{
    const FileRead read = readTextFile(path);
    if (!read.ok) {
        return unavailableMetric(path, read);
    }

    QVariantMap metric;
    metric.insert(QStringLiteral("path"), path);
    metric.insert(QStringLiteral("available"), true);
    metric.insert(QStringLiteral("valid"), true);
    metric.insert(QStringLiteral("authorizationRequired"), false);
    metric.insert(QStringLiteral("status"), QStringLiteral("ready"));
    metric.insert(QStringLiteral("value"), read.value);
    metric.insert(QStringLiteral("error"), QString());
    return metric;
}

QVariantMap frequencyMetric(const QString &path, FrequencyUnit unit = FrequencyUnit::Kilohertz)
{
    const FileRead read = readTextFile(path);
    if (!read.ok) {
        return unavailableMetric(path, read);
    }

    bool valid = false;
    const qulonglong value = read.value.toULongLong(&valid);
    if (!valid) {
        FileRead malformed = read;
        malformed.ok = false;
        malformed.error = QStringLiteral("Frequency value is not numeric");
        return unavailableMetric(path, malformed);
    }

    QVariantMap metric;
    metric.insert(QStringLiteral("path"), path);
    metric.insert(QStringLiteral("available"), true);
    metric.insert(QStringLiteral("valid"), true);
    metric.insert(QStringLiteral("authorizationRequired"), false);
    metric.insert(QStringLiteral("status"), QStringLiteral("ready"));
    if (unit == FrequencyUnit::Hertz) {
        metric.insert(QStringLiteral("valueHz"), QVariant::fromValue(value));
        metric.insert(QStringLiteral("valueMHz"), static_cast<double>(value) / 1000000.0);
        metric.insert(QStringLiteral("unit"), QStringLiteral("Hz"));
    } else {
        metric.insert(QStringLiteral("valueKHz"), QVariant::fromValue(value));
        metric.insert(QStringLiteral("valueMHz"), static_cast<double>(value) / 1000.0);
        metric.insert(QStringLiteral("unit"), QStringLiteral("kHz"));
    }
    metric.insert(QStringLiteral("raw"), read.value);
    metric.insert(QStringLiteral("error"), QString());
    return metric;
}

QVariantMap utilizationMetric(const QString &path)
{
    const FileRead read = readTextFile(path);
    if (!read.ok) {
        return unavailableMetric(path, read);
    }

    static const QRegularExpression pattern(
        QStringLiteral("^(0|[1-9][0-9]?|100)@([0-9]+)Hz$"));
    const QRegularExpressionMatch match = pattern.match(read.value);
    bool validPercent = false;
    bool validFrequency = false;
    const int percent = match.hasMatch() ? match.captured(1).toInt(&validPercent) : -1;
    const qulonglong frequencyHz = match.hasMatch()
        ? match.captured(2).toULongLong(&validFrequency) : 0;
    if (!match.hasMatch() || !validPercent || !validFrequency || percent < 0 || percent > 100) {
        FileRead malformed = read;
        malformed.ok = false;
        malformed.error = QStringLiteral("Utilization value is malformed");
        return unavailableMetric(path, malformed);
    }

    QVariantMap metric;
    metric.insert(QStringLiteral("path"), path);
    metric.insert(QStringLiteral("available"), true);
    metric.insert(QStringLiteral("valid"), true);
    metric.insert(QStringLiteral("authorizationRequired"), false);
    metric.insert(QStringLiteral("status"), QStringLiteral("ready"));
    metric.insert(QStringLiteral("valuePercent"), percent);
    metric.insert(QStringLiteral("utilizationPercent"), percent);
    metric.insert(QStringLiteral("loadFrequencyHz"), QVariant::fromValue(frequencyHz));
    metric.insert(QStringLiteral("unit"), QStringLiteral("%"));
    metric.insert(QStringLiteral("raw"), read.value);
    metric.insert(QStringLiteral("error"), QString());
    return metric;
}

QVariantMap npuUtilizationMetric(const NpuLoadReading &reading)
{
    QVariantMap metric;
    metric.insert(QStringLiteral("path"), QStringLiteral("/sys/kernel/debug/rknpu/load"));
    metric.insert(QStringLiteral("available"), reading.available);
    metric.insert(QStringLiteral("valid"), reading.available);
    metric.insert(QStringLiteral("authorizationRequired"), false);
    metric.insert(QStringLiteral("status"), reading.available
                  ? QStringLiteral("ready") : QStringLiteral("unavailable"));
    metric.insert(QStringLiteral("source"), reading.source);
    metric.insert(QStringLiteral("cores"), reading.cores);
    metric.insert(QStringLiteral("averagePercent"), reading.averagePercent);
    if (reading.available) {
        metric.insert(QStringLiteral("valuePercent"), reading.averagePercent);
        metric.insert(QStringLiteral("utilizationPercent"), reading.averagePercent);
        metric.insert(QStringLiteral("unit"), QStringLiteral("%"));
        metric.insert(QStringLiteral("error"), QString());
    } else {
        metric.insert(QStringLiteral("error"), reading.message.isEmpty()
                      ? QStringLiteral("RKNPU core load is unavailable") : reading.message);
    }
    return metric;
}

QVariantMap temperatureMetric(const QString &path)
{
    const FileRead read = readTextFile(path);
    if (!read.ok) {
        return unavailableMetric(path, read);
    }

    bool valid = false;
    const double raw = read.value.toDouble(&valid);
    if (!valid) {
        FileRead malformed = read;
        malformed.ok = false;
        malformed.error = QStringLiteral("Thermal value is not numeric");
        return unavailableMetric(path, malformed);
    }

    const double celsius = qAbs(raw) >= 1000.0 ? raw / 1000.0 : raw;
    QVariantMap metric;
    metric.insert(QStringLiteral("path"), path);
    metric.insert(QStringLiteral("available"), true);
    metric.insert(QStringLiteral("valid"), true);
    metric.insert(QStringLiteral("authorizationRequired"), false);
    metric.insert(QStringLiteral("status"), QStringLiteral("ready"));
    metric.insert(QStringLiteral("valueCelsius"), celsius);
    metric.insert(QStringLiteral("raw"), read.value);
    metric.insert(QStringLiteral("unit"), QStringLiteral("C"));
    metric.insert(QStringLiteral("error"), QString());
    return metric;
}

bool metricAvailable(const QVariantMap &metric)
{
    return metric.value(QStringLiteral("available")).toBool();
}

bool metricAuthorizationRequired(const QVariantMap &metric)
{
    return metric.value(QStringLiteral("authorizationRequired")).toBool();
}

QString combinedStatus(bool anyAvailable, bool allAvailable, bool authorizationRequired)
{
    if (allAvailable && anyAvailable) {
        return QStringLiteral("ready");
    }
    if (anyAvailable) {
        return QStringLiteral("partial");
    }
    return authorizationRequired ? QStringLiteral("authorizationRequired")
                                 : QStringLiteral("unavailable");
}

QVariantMap unavailableComponent(const QString &label, const QString &reason, bool authorizationRequired = false)
{
    QVariantMap component;
    component.insert(QStringLiteral("label"), label);
    component.insert(QStringLiteral("available"), false);
    component.insert(QStringLiteral("discovered"), false);
    component.insert(QStringLiteral("authorizationRequired"), authorizationRequired);
    component.insert(QStringLiteral("status"), authorizationRequired
                     ? QStringLiteral("authorizationRequired")
                     : QStringLiteral("unavailable"));
    component.insert(QStringLiteral("error"), reason);
    component.insert(QStringLiteral("devices"), QVariantList());
    return component;
}

QVariantMap unavailableMetricWithReason(const QString &reason)
{
    FileRead read;
    read.error = reason;
    return unavailableMetric(QString(), read);
}

ParsedCounters parseCpuCounters(const QString &statPath)
{
    ParsedCounters result;
    const FileRead read = readTextFile(statPath);
    if (!read.ok) {
        result.error = read.error;
        result.authorizationRequired = read.authorizationRequired;
        return result;
    }

    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QStringList lines = read.value.split(QLatin1Char('\n'), boardlab::kSkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(whitespace, boardlab::kSkipEmptyParts);
        if (fields.isEmpty()) {
            continue;
        }
        if (fields.first() == QStringLiteral("cpu")) {
            if (fields.size() < 5) {
                result.error = QStringLiteral("CPU counter line is incomplete");
                return result;
            }
            quint64 values[8] = {};
            for (int index = 0; index < 8 && index + 1 < fields.size(); ++index) {
                bool valid = false;
                values[index] = fields.at(index + 1).toULongLong(&valid);
                if (!valid) {
                    result.error = QStringLiteral("CPU counter value is not numeric");
                    return result;
                }
            }
            result.counters.user = values[0];
            result.counters.nice = values[1];
            result.counters.system = values[2];
            result.counters.idle = values[3];
            result.counters.iowait = values[4];
            result.counters.irq = values[5];
            result.counters.softirq = values[6];
            result.counters.steal = values[7];
            result.valid = true;
        } else if (fields.first().startsWith(QStringLiteral("cpu"))) {
            bool indexValid = false;
            fields.first().mid(3).toInt(&indexValid);
            if (indexValid) {
                ++result.coreCount;
            }
        }
    }
    if (!result.valid) {
        result.error = QStringLiteral("Aggregate CPU counters are unavailable");
    }
    return result;
}

quint64 totalCpuTicks(const ResourceMonitor::CpuCounters &counters)
{
    return counters.user + counters.nice + counters.system + counters.idle
        + counters.iowait + counters.irq + counters.softirq + counters.steal;
}

quint64 idleCpuTicks(const ResourceMonitor::CpuCounters &counters)
{
    return counters.idle + counters.iowait;
}

QVariantMap loadMetric(const QString &path)
{
    const FileRead read = readTextFile(path);
    if (!read.ok) {
        return unavailableMetric(path, read);
    }
    const QStringList fields = read.value.split(QRegularExpression(QStringLiteral("\\s+")), boardlab::kSkipEmptyParts);
    if (fields.size() < 3) {
        FileRead malformed = read;
        malformed.ok = false;
        malformed.error = QStringLiteral("Load average line is incomplete");
        return unavailableMetric(path, malformed);
    }

    QVariantMap metric;
    metric.insert(QStringLiteral("path"), path);
    metric.insert(QStringLiteral("available"), true);
    metric.insert(QStringLiteral("valid"), true);
    metric.insert(QStringLiteral("authorizationRequired"), false);
    metric.insert(QStringLiteral("status"), QStringLiteral("ready"));
    metric.insert(QStringLiteral("oneMinute"), fields.at(0));
    metric.insert(QStringLiteral("fiveMinutes"), fields.at(1));
    metric.insert(QStringLiteral("fifteenMinutes"), fields.at(2));
    metric.insert(QStringLiteral("error"), QString());
    return metric;
}

QVariantMap buildMemory(const QString &memInfoPath)
{
    const FileRead read = readTextFile(memInfoPath);
    if (!read.ok) {
        QVariantMap memory = unavailableComponent(QStringLiteral("RAM"), read.error, read.authorizationRequired);
        memory.insert(QStringLiteral("path"), memInfoPath);
        return memory;
    }

    QMap<QString, qulonglong> values;
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    const QStringList lines = read.value.split(QLatin1Char('\n'), boardlab::kSkipEmptyParts);
    for (const QString &line : lines) {
        const QStringList fields = line.split(whitespace, boardlab::kSkipEmptyParts);
        if (fields.size() < 2 || !fields.first().endsWith(QLatin1Char(':'))) {
            continue;
        }
        bool valid = false;
        const qulonglong value = fields.at(1).toULongLong(&valid);
        if (valid) {
            values.insert(fields.first().chopped(1), value);
        }
    }

    const auto totalIt = values.constFind(QStringLiteral("MemTotal"));
    if (totalIt == values.constEnd() || totalIt.value() == 0) {
        QVariantMap memory = unavailableComponent(QStringLiteral("RAM"), QStringLiteral("MemTotal is unavailable"));
        memory.insert(QStringLiteral("path"), memInfoPath);
        return memory;
    }

    qulonglong availableKiB = 0;
    QString availableSource;
    const auto availableIt = values.constFind(QStringLiteral("MemAvailable"));
    if (availableIt != values.constEnd()) {
        availableKiB = availableIt.value();
        availableSource = QStringLiteral("MemAvailable");
    } else {
        const auto freeIt = values.constFind(QStringLiteral("MemFree"));
        const auto buffersIt = values.constFind(QStringLiteral("Buffers"));
        const auto cachedIt = values.constFind(QStringLiteral("Cached"));
        const auto reclaimableIt = values.constFind(QStringLiteral("SReclaimable"));
        const auto shmemIt = values.constFind(QStringLiteral("Shmem"));
        if (freeIt != values.constEnd() && buffersIt != values.constEnd() && cachedIt != values.constEnd()
            && reclaimableIt != values.constEnd() && shmemIt != values.constEnd()) {
            const qulonglong cache = cachedIt.value() + reclaimableIt.value();
            availableKiB = freeIt.value() + buffersIt.value() + (cache > shmemIt.value() ? cache - shmemIt.value() : 0);
            availableSource = QStringLiteral("MemFree+Buffers+Cached+SReclaimable-Shmem");
        } else {
            QVariantMap memory = unavailableComponent(QStringLiteral("RAM"),
                                                       QStringLiteral("MemAvailable is unavailable and no safe fallback can be calculated"));
            memory.insert(QStringLiteral("path"), memInfoPath);
            memory.insert(QStringLiteral("totalKiB"), QVariant::fromValue(totalIt.value()));
            return memory;
        }
    }

    const qulonglong totalKiB = totalIt.value();
    const qulonglong usedKiB = totalKiB > availableKiB ? totalKiB - availableKiB : 0;
    QVariantMap memory;
    memory.insert(QStringLiteral("label"), QStringLiteral("RAM"));
    memory.insert(QStringLiteral("path"), memInfoPath);
    memory.insert(QStringLiteral("available"), true);
    memory.insert(QStringLiteral("discovered"), true);
    memory.insert(QStringLiteral("authorizationRequired"), false);
    memory.insert(QStringLiteral("status"), QStringLiteral("ready"));
    memory.insert(QStringLiteral("totalKiB"), QVariant::fromValue(totalKiB));
    memory.insert(QStringLiteral("availableKiB"), QVariant::fromValue(availableKiB));
    memory.insert(QStringLiteral("usedKiB"), QVariant::fromValue(usedKiB));
    memory.insert(QStringLiteral("utilizationPercent"), 100.0 * static_cast<double>(usedKiB) / static_cast<double>(totalKiB));
    memory.insert(QStringLiteral("availableSource"), availableSource);
    memory.insert(QStringLiteral("error"), QString());
    return memory;
}

QVariantMap buildCpuPolicy(const QFileInfo &entry)
{
    const QDir directory(entry.absoluteFilePath());
    const QVariantMap current = frequencyMetric(directory.filePath(QStringLiteral("scaling_cur_freq")));
    QVariantMap minimum = frequencyMetric(directory.filePath(QStringLiteral("scaling_min_freq")));
    if (!metricAvailable(minimum)) {
        const QVariantMap fallback = frequencyMetric(directory.filePath(QStringLiteral("cpuinfo_min_freq")));
        if (metricAvailable(fallback)) {
            minimum = fallback;
        }
    }
    QVariantMap maximum = frequencyMetric(directory.filePath(QStringLiteral("scaling_max_freq")));
    if (!metricAvailable(maximum)) {
        const QVariantMap fallback = frequencyMetric(directory.filePath(QStringLiteral("cpuinfo_max_freq")));
        if (metricAvailable(fallback)) {
            maximum = fallback;
        }
    }

    const bool anyAvailable = metricAvailable(current) || metricAvailable(minimum) || metricAvailable(maximum);
    const bool allAvailable = metricAvailable(current) && metricAvailable(minimum) && metricAvailable(maximum);
    const bool authorizationRequired = metricAuthorizationRequired(current)
        || metricAuthorizationRequired(minimum) || metricAuthorizationRequired(maximum);
    QVariantMap policy;
    policy.insert(QStringLiteral("id"), entry.fileName());
    policy.insert(QStringLiteral("path"), entry.absoluteFilePath());
    policy.insert(QStringLiteral("available"), anyAvailable);
    policy.insert(QStringLiteral("authorizationRequired"), authorizationRequired);
    policy.insert(QStringLiteral("status"), combinedStatus(anyAvailable, allAvailable, authorizationRequired));
    policy.insert(QStringLiteral("currentFrequency"), current);
    policy.insert(QStringLiteral("minimumFrequency"), minimum);
    policy.insert(QStringLiteral("maximumFrequency"), maximum);

    const QStringList cpuListFiles = {QStringLiteral("related_cpus"), QStringLiteral("affected_cpus")};
    for (const QString &fileName : cpuListFiles) {
        const QVariantMap cpus = textMetric(directory.filePath(fileName));
        if (metricAvailable(cpus)) {
            policy.insert(QStringLiteral("relatedCpus"), cpus.value(QStringLiteral("value")).toString()
                          .split(QRegularExpression(QStringLiteral("\\s+")), boardlab::kSkipEmptyParts));
            break;
        }
    }
    if (anyAvailable) {
        policy.insert(QStringLiteral("error"), allAvailable ? QString()
                                                               : QStringLiteral("One or more policy frequencies are unavailable"));
    } else {
        policy.insert(QStringLiteral("error"), QStringLiteral("CPU policy frequencies are unavailable"));
    }
    return policy;
}

QVariantList discoverCpuPolicies(const QString &cpuFreqRoot)
{
    QDir root(cpuFreqRoot);
    const QFileInfoList entries = root.entryInfoList(QStringList{QStringLiteral("policy*")},
                                                     QDir::Dirs | QDir::NoDotAndDotDot,
                                                     QDir::Name);
    QVariantList policies;
    for (const QFileInfo &entry : entries) {
        policies.append(buildCpuPolicy(entry));
    }
    return policies;
}

QStringList contentTokens(const QString &path)
{
    const FileRead read = readTextFile(path);
    if (!read.ok) {
        return QStringList();
    }
    return QStringList{read.value};
}

QString deviceIdentity(const QFileInfo &entry)
{
    const QDir directory(entry.absoluteFilePath());
    QStringList values;
    values.append(entry.fileName());
    values.append(entry.absoluteFilePath());
    const QStringList candidateFiles = {
        QStringLiteral("name"),
        QStringLiteral("device/name"),
        QStringLiteral("device/of_node/name"),
        QStringLiteral("device/of_node/compatible"),
        QStringLiteral("device/uevent"),
        QStringLiteral("uevent")
    };
    for (const QString &candidate : candidateFiles) {
        values.append(contentTokens(directory.filePath(candidate)));
    }
    return values.join(QLatin1Char(' ')).toLower();
}

enum class AcceleratorKind {
    Unknown,
    Gpu,
    Npu
};

AcceleratorKind classifyAccelerator(const QString &identity)
{
    static const QStringList npuTokens = {
        QStringLiteral("rknpu"), QStringLiteral("npu"), QStringLiteral("neural"),
        QStringLiteral("ai-accelerator"), QStringLiteral("bpu")
    };
    for (const QString &token : npuTokens) {
        if (identity.contains(token)) {
            return AcceleratorKind::Npu;
        }
    }
    static const QStringList gpuTokens = {
        QStringLiteral("gpu"), QStringLiteral("mali"), QStringLiteral("panfrost"),
        QStringLiteral("adreno"), QStringLiteral("powervr"), QStringLiteral("v3d")
    };
    for (const QString &token : gpuTokens) {
        if (identity.contains(token)) {
            return AcceleratorKind::Gpu;
        }
    }
    return AcceleratorKind::Unknown;
}

struct DevfreqEntry {
    QFileInfo info;
    AcceleratorKind kind = AcceleratorKind::Unknown;
};

QList<DevfreqEntry> discoverDevfreqDirectories(const QString &sysRoot)
{
    QList<DevfreqEntry> entries;
    QSet<QString> seen;
    const auto appendEntry = [&entries, &seen](const QFileInfo &entry, AcceleratorKind kind) {
        if (!entry.exists() || !entry.isDir() || kind == AcceleratorKind::Unknown) {
            return;
        }
        const QString canonical = entry.canonicalFilePath();
        const QString identity = canonical.isEmpty() ? entry.absoluteFilePath() : canonical;
        if (seen.contains(identity)) {
            return;
        }
        seen.insert(identity);
        entries.append(DevfreqEntry{entry, kind});
    };

    QDir classRoot(QDir(sysRoot).filePath(QStringLiteral("class/devfreq")));
    const QFileInfoList classEntries = classRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                               QDir::Name);
    bool foundGpu = false;
    bool foundNpu = false;
    for (const QFileInfo &entry : classEntries) {
        const AcceleratorKind kind = classifyAccelerator(deviceIdentity(entry));
        appendEntry(entry, kind);
        foundGpu = foundGpu || kind == AcceleratorKind::Gpu;
        foundNpu = foundNpu || kind == AcceleratorKind::Npu;
    }
    if (foundGpu && foundNpu) {
        return entries;
    }

    const QString platformRoot = QDir(sysRoot).filePath(QStringLiteral("devices/platform"));
    if (!QFileInfo(platformRoot).isDir()) {
        return entries;
    }
    // Fallback scan is bounded and only used when class/devfreq did not expose
    // both GPU and NPU. Callers must cache the result — this walks thousands of
    // platform nodes and is too expensive for a 1 Hz GUI refresh.
    QDirIterator iterator(platformRoot,
                           QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);
    int inspected = 0;
    while (iterator.hasNext() && inspected < kFallbackDevfreqScanLimit) {
        const QString path = iterator.next();
        ++inspected;
        const QFileInfo entry(path);
        const QDir directory(path);
        if (!QFileInfo(directory.filePath(QStringLiteral("cur_freq"))).exists()
            && !QFileInfo(directory.filePath(QStringLiteral("available_frequencies"))).exists()) {
            continue;
        }
        const AcceleratorKind kind = classifyAccelerator(deviceIdentity(entry));
        appendEntry(entry, kind);
    }
    return entries;
}

QVariantList discoverThermalZones(const QString &thermalRoot)
{
    QDir root(thermalRoot);
    const QFileInfoList entries = root.entryInfoList(QStringList{QStringLiteral("thermal_zone*")},
                                                     QDir::Dirs | QDir::NoDotAndDotDot,
                                                     QDir::Name);
    QVariantList zones;
    for (const QFileInfo &entry : entries) {
        const QDir directory(entry.absoluteFilePath());
        const QVariantMap type = textMetric(directory.filePath(QStringLiteral("type")));
        const QVariantMap temperature = temperatureMetric(directory.filePath(QStringLiteral("temp")));
        const bool anyAvailable = metricAvailable(type) || metricAvailable(temperature);
        const bool allAvailable = metricAvailable(type) && metricAvailable(temperature);
        const bool authorizationRequired = metricAuthorizationRequired(type)
            || metricAuthorizationRequired(temperature);
        QVariantMap zone;
        zone.insert(QStringLiteral("id"), entry.fileName());
        zone.insert(QStringLiteral("path"), entry.absoluteFilePath());
        zone.insert(QStringLiteral("available"), anyAvailable);
        zone.insert(QStringLiteral("authorizationRequired"), authorizationRequired);
        zone.insert(QStringLiteral("status"), combinedStatus(anyAvailable, allAvailable, authorizationRequired));
        zone.insert(QStringLiteral("type"), type);
        zone.insert(QStringLiteral("temperature"), temperature);
        zone.insert(QStringLiteral("error"), anyAvailable
                    ? (allAvailable ? QString() : QStringLiteral("Thermal zone is only partially readable"))
                    : QStringLiteral("Thermal zone is unavailable"));
        zones.append(zone);
    }
    return zones;
}

QVariantMap thermalMatch(const QVariantList &zones, const QStringList &tokens, const QString &label)
{
    bool typeAuthorizationRequired = false;
    for (const QVariant &item : zones) {
        const QVariantMap zone = item.toMap();
        const QVariantMap type = zone.value(QStringLiteral("type")).toMap();
        if (!metricAvailable(type)) {
            typeAuthorizationRequired = typeAuthorizationRequired || metricAuthorizationRequired(type);
            continue;
        }
        const QString typeName = type.value(QStringLiteral("value")).toString().toLower();
        bool matched = false;
        for (const QString &token : tokens) {
            if (typeName.contains(token)) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            continue;
        }
        QVariantMap match = zone.value(QStringLiteral("temperature")).toMap();
        match.insert(QStringLiteral("zone"), zone.value(QStringLiteral("id")));
        match.insert(QStringLiteral("zonePath"), zone.value(QStringLiteral("path")));
        match.insert(QStringLiteral("thermalType"), typeName);
        return match;
    }

    QVariantMap missing;
    missing.insert(QStringLiteral("available"), false);
    missing.insert(QStringLiteral("valid"), false);
    missing.insert(QStringLiteral("authorizationRequired"), typeAuthorizationRequired);
    missing.insert(QStringLiteral("status"), typeAuthorizationRequired
                   ? QStringLiteral("authorizationRequired")
                   : QStringLiteral("unavailable"));
    missing.insert(QStringLiteral("error"), typeAuthorizationRequired
                   ? QStringLiteral("Thermal zone type requires authorization")
                   : QStringLiteral("No matching %1 thermal zone was discovered").arg(label));
    return missing;
}

QVariantMap buildAcceleratorDevice(const QFileInfo &entry,
                                   const QVariantList &thermalZones,
                                   const QStringList &thermalTokens,
                                   const QString &label,
                                   AcceleratorKind kind)
{
    const QDir directory(entry.absoluteFilePath());
    const QVariantMap current = frequencyMetric(directory.filePath(QStringLiteral("cur_freq")), FrequencyUnit::Hertz);
    QVariantMap minimum = frequencyMetric(directory.filePath(QStringLiteral("min_freq")), FrequencyUnit::Hertz);
    if (!metricAvailable(minimum)) {
        const QVariantMap fallback = frequencyMetric(directory.filePath(QStringLiteral("scaling_min_freq")), FrequencyUnit::Hertz);
        if (metricAvailable(fallback)) {
            minimum = fallback;
        }
    }
    QVariantMap maximum = frequencyMetric(directory.filePath(QStringLiteral("max_freq")), FrequencyUnit::Hertz);
    if (!metricAvailable(maximum)) {
        const QVariantMap fallback = frequencyMetric(directory.filePath(QStringLiteral("scaling_max_freq")), FrequencyUnit::Hertz);
        if (metricAvailable(fallback)) {
            maximum = fallback;
        }
    }

    QVariantMap runtime = textMetric(directory.filePath(QStringLiteral("device/power/runtime_status")));
    if (!metricAvailable(runtime)) {
        const QVariantMap fallback = textMetric(directory.filePath(QStringLiteral("power/runtime_status")));
        if (metricAvailable(fallback)) {
            runtime = fallback;
        }
    }
    if (!metricAvailable(runtime)) {
        const QVariantMap fallback = textMetric(directory.filePath(QStringLiteral("runtime_status")));
        if (metricAvailable(fallback)) {
            runtime = fallback;
        }
    }

    QVariantMap deviceName = textMetric(directory.filePath(QStringLiteral("name")));
    if (!metricAvailable(deviceName)) {
        const QVariantMap fallback = textMetric(directory.filePath(QStringLiteral("device/name")));
        if (metricAvailable(fallback)) {
            deviceName = fallback;
        }
    }

    const bool hasDriverUtilization = kind == AcceleratorKind::Gpu;
    const QVariantMap utilization = hasDriverUtilization
        ? utilizationMetric(directory.filePath(QStringLiteral("load")))
        : unavailableMetricWithReason(QStringLiteral("NPU utilization is read from RKNPU core telemetry"));

    const bool anyAvailable = metricAvailable(current) || metricAvailable(minimum)
        || metricAvailable(maximum) || metricAvailable(runtime) || metricAvailable(utilization);
    const bool allFrequencyAvailable = metricAvailable(current) && metricAvailable(minimum)
        && metricAvailable(maximum);
    const bool allTelemetryAvailable = allFrequencyAvailable && metricAvailable(runtime)
        && (!hasDriverUtilization || metricAvailable(utilization));
    const bool authorizationRequired = metricAuthorizationRequired(current)
        || metricAuthorizationRequired(minimum) || metricAuthorizationRequired(maximum)
        || metricAuthorizationRequired(runtime) || metricAuthorizationRequired(utilization);
    QVariantMap device;
    device.insert(QStringLiteral("id"), entry.fileName());
    device.insert(QStringLiteral("name"), metricAvailable(deviceName)
                  ? deviceName.value(QStringLiteral("value")).toString() : entry.fileName());
    device.insert(QStringLiteral("path"), entry.absoluteFilePath());
    device.insert(QStringLiteral("discovered"), true);
    device.insert(QStringLiteral("available"), anyAvailable);
    device.insert(QStringLiteral("authorizationRequired"), authorizationRequired);
    device.insert(QStringLiteral("status"), combinedStatus(anyAvailable,
                                                             allTelemetryAvailable,
                                                             authorizationRequired));
    device.insert(QStringLiteral("currentFrequency"), current);
    device.insert(QStringLiteral("minimumFrequency"), minimum);
    device.insert(QStringLiteral("maximumFrequency"), maximum);
    device.insert(QStringLiteral("runtimeStatus"), runtime);
    device.insert(QStringLiteral("utilization"), utilization);
    device.insert(QStringLiteral("thermal"), thermalMatch(thermalZones, thermalTokens, label));
    device.insert(QStringLiteral("error"), anyAvailable
                  ? (allTelemetryAvailable ? QString() : QStringLiteral("One or more %1 telemetry values are unavailable").arg(label))
                  : QStringLiteral("%1 telemetry values are unavailable").arg(label));
    return device;
}

QVariantMap buildAccelerator(const QStringList &paths,
                             AcceleratorKind desiredKind,
                             const QVariantList &thermalZones,
                             const QString &label)
{
    const QStringList thermalTokens = desiredKind == AcceleratorKind::Gpu
        ? QStringList{QStringLiteral("gpu"), QStringLiteral("mali"), QStringLiteral("panfrost")}
        : QStringList{QStringLiteral("npu"), QStringLiteral("rknpu"), QStringLiteral("neural")};
    QVariantList devices;
    for (const QString &path : paths) {
        const QFileInfo entry(path);
        if (!entry.exists() || !entry.isDir()) {
            continue;
        }
        devices.append(buildAcceleratorDevice(entry, thermalZones, thermalTokens, label, desiredKind));
    }
    if (devices.isEmpty()) {
        const QString reason = QStringLiteral("No named %1 devfreq device was discovered").arg(label);
        QVariantMap component = unavailableComponent(label, reason);
        component.insert(QStringLiteral("currentFrequency"), unavailableMetricWithReason(reason));
        component.insert(QStringLiteral("minimumFrequency"), unavailableMetricWithReason(reason));
        component.insert(QStringLiteral("maximumFrequency"), unavailableMetricWithReason(reason));
        component.insert(QStringLiteral("runtimeStatus"), unavailableMetricWithReason(reason));
        component.insert(QStringLiteral("utilization"), unavailableMetricWithReason(reason));
        component.insert(QStringLiteral("thermal"), unavailableMetricWithReason(reason));
        return component;
    }

    const QVariantMap selected = devices.first().toMap();
    QVariantMap component;
    component.insert(QStringLiteral("label"), label);
    component.insert(QStringLiteral("discovered"), true);
    component.insert(QStringLiteral("available"), selected.value(QStringLiteral("available")));
    component.insert(QStringLiteral("authorizationRequired"), selected.value(QStringLiteral("authorizationRequired")));
    component.insert(QStringLiteral("status"), selected.value(QStringLiteral("status")));
    component.insert(QStringLiteral("error"), selected.value(QStringLiteral("error")));
    component.insert(QStringLiteral("name"), selected.value(QStringLiteral("name")));
    component.insert(QStringLiteral("path"), selected.value(QStringLiteral("path")));
    component.insert(QStringLiteral("currentFrequency"), selected.value(QStringLiteral("currentFrequency")));
    component.insert(QStringLiteral("minimumFrequency"), selected.value(QStringLiteral("minimumFrequency")));
    component.insert(QStringLiteral("maximumFrequency"), selected.value(QStringLiteral("maximumFrequency")));
    component.insert(QStringLiteral("runtimeStatus"), selected.value(QStringLiteral("runtimeStatus")));
    component.insert(QStringLiteral("utilization"), selected.value(QStringLiteral("utilization")));
    component.insert(QStringLiteral("thermal"), selected.value(QStringLiteral("thermal")));
    component.insert(QStringLiteral("devices"), devices);
    return component;
}

QVariantMap withNpuUtilization(QVariantMap component, const QVariantMap &utilization)
{
    component.insert(QStringLiteral("utilization"), utilization);
    component.insert(QStringLiteral("npuLoad"), utilization);
    if (!metricAvailable(utilization)) {
        return component;
    }

    const bool devfreqAvailable = component.value(QStringLiteral("available")).toBool();
    component.insert(QStringLiteral("available"), true);
    component.insert(QStringLiteral("discovered"), true);
    component.insert(QStringLiteral("authorizationRequired"), false);
    if (!devfreqAvailable) {
        component.insert(QStringLiteral("status"), QStringLiteral("partial"));
        component.insert(QStringLiteral("error"), QStringLiteral("NPU utilization is available; devfreq metadata is unavailable"));
        if (component.value(QStringLiteral("name")).toString().isEmpty()) {
            component.insert(QStringLiteral("name"), QStringLiteral("RKNPU"));
        }
    }
    return component;
}

} // namespace

ResourceMonitor::ResourceMonitor(QObject *parent)
    : ResourceMonitor(QString(), QString(), parent)
{
}

ResourceMonitor::ResourceMonitor(const QString &procRoot, const QString &sysRoot, QObject *parent)
    : QObject(parent)
    , m_procRoot(normalizedRoot(procRoot, QStringLiteral("/proc")))
    , m_sysRoot(normalizedRoot(sysRoot, QStringLiteral("/sys")))
{
    connect(&m_npuLoadWatcher, &QFutureWatcher<NpuLoadReading>::finished,
            this, [this]() {
        if (m_inFlightNpuLoadGeneration == 0) {
            return;
        }
        m_npuUtilization = npuUtilizationMetric(m_npuLoadWatcher.result());
        m_inFlightNpuLoadGeneration = 0;
        updateSnapshotNpuUtilization();
        emit changed();
    });
    refresh();
}

QVariantMap ResourceMonitor::snapshot() const
{
    return m_snapshot;
}

QString ResourceMonitor::procRoot() const
{
    return m_procRoot;
}

QString ResourceMonitor::sysRoot() const
{
    return m_sysRoot;
}

void ResourceMonitor::setRoots(const QString &procRoot, const QString &sysRoot)
{
    m_procRoot = normalizedRoot(procRoot, QStringLiteral("/proc"));
    m_sysRoot = normalizedRoot(sysRoot, QStringLiteral("/sys"));
    m_npuUtilization.clear();
    m_hasPreviousCpu = false;
    m_hasCpuUtilization = false;
    m_lastCpuUtilization = 0.0;
    m_devfreqGpuPaths.clear();
    m_devfreqNpuPaths.clear();
    m_devfreqCacheValid = false;
}

void ResourceMonitor::requestNpuUtilization()
{
    // Test fixtures and restricted mount namespaces do not represent the
    // running board. Never spawn the privileged helper for those roots.
    if (m_procRoot != QStringLiteral("/proc") || m_sysRoot != QStringLiteral("/sys")
        || m_npuLoadWatcher.isRunning()) {
        return;
    }

    ++m_npuLoadGeneration;
    m_inFlightNpuLoadGeneration = m_npuLoadGeneration;
    m_npuLoadWatcher.setFuture(QtConcurrent::run([]() {
        return NpuLoadReader::readPrivileged();
    }));
}

void ResourceMonitor::updateSnapshotNpuUtilization()
{
    if (m_snapshot.isEmpty() || m_npuUtilization.isEmpty()) {
        return;
    }

    QVariantMap refreshed = m_snapshot;
    const QVariantMap npu = refreshed.value(QStringLiteral("npu")).toMap();
    refreshed.insert(QStringLiteral("npu"), withNpuUtilization(npu, m_npuUtilization));
    refreshed.insert(QStringLiteral("generatedAt"), nowIso());
    m_snapshot = refreshed;
}

QVariantMap ResourceMonitor::refresh()
{
    const QString statPath = QDir(m_procRoot).filePath(QStringLiteral("stat"));
    const ParsedCounters parsedCpu = parseCpuCounters(statPath);
    QVariantMap cpu;
    cpu.insert(QStringLiteral("label"), QStringLiteral("CPU"));
    cpu.insert(QStringLiteral("path"), statPath);
    cpu.insert(QStringLiteral("utilizationValid"), false);
    cpu.insert(QStringLiteral("authorizationRequired"), false);
    cpu.insert(QStringLiteral("available"), false);
    cpu.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
    if (!parsedCpu.valid) {
        cpu.insert(QStringLiteral("error"), parsedCpu.error.isEmpty()
                   ? QStringLiteral("CPU counters are unavailable") : parsedCpu.error);
        cpu.insert(QStringLiteral("authorizationRequired"), parsedCpu.authorizationRequired);
        cpu.insert(QStringLiteral("status"), parsedCpu.authorizationRequired
                   ? QStringLiteral("authorizationRequired") : QStringLiteral("unavailable"));
    } else {
        const quint64 currentTotal = totalCpuTicks(parsedCpu.counters);
        const quint64 currentIdle = idleCpuTicks(parsedCpu.counters);
        cpu.insert(QStringLiteral("coreCount"), parsedCpu.coreCount);
        cpu.insert(QStringLiteral("available"), true);
        if (!m_hasPreviousCpu) {
            cpu.insert(QStringLiteral("status"), QStringLiteral("initializing"));
            cpu.insert(QStringLiteral("error"), QStringLiteral("Waiting for a second CPU counter sample"));
        } else {
            const quint64 previousTotal = totalCpuTicks(m_previousCpu);
            const quint64 previousIdle = idleCpuTicks(m_previousCpu);
            if (currentTotal < previousTotal || currentIdle < previousIdle) {
                m_hasCpuUtilization = false;
                cpu.insert(QStringLiteral("status"), QStringLiteral("initializing"));
                cpu.insert(QStringLiteral("error"), QStringLiteral("CPU counters reset; waiting for a second sample"));
            } else if (currentTotal == previousTotal) {
                if (m_hasCpuUtilization) {
                    cpu.insert(QStringLiteral("utilizationPercent"), m_lastCpuUtilization);
                    cpu.insert(QStringLiteral("utilizationValid"), true);
                    cpu.insert(QStringLiteral("sampleFresh"), false);
                    cpu.insert(QStringLiteral("status"), QStringLiteral("ready"));
                    cpu.insert(QStringLiteral("error"), QStringLiteral("CPU counters have not advanced since the last sample"));
                } else {
                    cpu.insert(QStringLiteral("status"), QStringLiteral("initializing"));
                    cpu.insert(QStringLiteral("error"), QStringLiteral("CPU counters have not advanced yet"));
                }
            } else {
                const quint64 totalDelta = currentTotal - previousTotal;
                const quint64 idleDelta = currentIdle - previousIdle;
                const double utilization = qBound(0.0,
                                                  100.0 * static_cast<double>(totalDelta - qMin(totalDelta, idleDelta))
                                                      / static_cast<double>(totalDelta),
                                                  100.0);
                m_lastCpuUtilization = utilization;
                m_hasCpuUtilization = true;
                cpu.insert(QStringLiteral("utilizationPercent"), utilization);
                cpu.insert(QStringLiteral("utilizationValid"), true);
                cpu.insert(QStringLiteral("sampleFresh"), true);
                cpu.insert(QStringLiteral("status"), QStringLiteral("ready"));
                cpu.insert(QStringLiteral("error"), QString());
            }
        }
        m_previousCpu = parsedCpu.counters;
        m_hasPreviousCpu = true;
    }
    const QVariantMap load = loadMetric(QDir(m_procRoot).filePath(QStringLiteral("loadavg")));
    if (metricAvailable(load)) {
        cpu.insert(QStringLiteral("load"), load);
    }

    const QVariantMap memory = buildMemory(QDir(m_procRoot).filePath(QStringLiteral("meminfo")));
    const QVariantList cpuPolicies = discoverCpuPolicies(
        QDir(m_sysRoot).filePath(QStringLiteral("devices/system/cpu/cpufreq")));
    const QVariantList thermalZones = discoverThermalZones(
        QDir(m_sysRoot).filePath(QStringLiteral("class/thermal")));
    if (!m_devfreqCacheValid) {
        m_devfreqGpuPaths.clear();
        m_devfreqNpuPaths.clear();
        const QList<DevfreqEntry> discovered = discoverDevfreqDirectories(m_sysRoot);
        for (const DevfreqEntry &entry : discovered) {
            const QString path = entry.info.absoluteFilePath();
            if (entry.kind == AcceleratorKind::Gpu) {
                m_devfreqGpuPaths.append(path);
            } else if (entry.kind == AcceleratorKind::Npu) {
                m_devfreqNpuPaths.append(path);
            }
        }
        m_devfreqCacheValid = true;
    }
    const QVariantMap gpu = buildAccelerator(m_devfreqGpuPaths, AcceleratorKind::Gpu, thermalZones, QStringLiteral("GPU"));
    const QVariantMap npu = withNpuUtilization(
        buildAccelerator(m_devfreqNpuPaths, AcceleratorKind::Npu, thermalZones, QStringLiteral("NPU")),
        m_npuUtilization);

    QVariantMap refreshed;
    refreshed.insert(QStringLiteral("readOnly"), true);
    refreshed.insert(QStringLiteral("generatedAt"), nowIso());
    refreshed.insert(QStringLiteral("sources"), QVariantMap{
        {QStringLiteral("procRoot"), m_procRoot},
        {QStringLiteral("sysRoot"), m_sysRoot}
    });
    refreshed.insert(QStringLiteral("cpu"), cpu);
    refreshed.insert(QStringLiteral("memory"), memory);
    refreshed.insert(QStringLiteral("cpuPolicies"), cpuPolicies);
    refreshed.insert(QStringLiteral("thermalZones"), thermalZones);
    refreshed.insert(QStringLiteral("gpu"), gpu);
    refreshed.insert(QStringLiteral("npu"), npu);

    const bool cpuReady = cpu.value(QStringLiteral("available")).toBool();
    const bool memoryReady = memory.value(QStringLiteral("available")).toBool();
    refreshed.insert(QStringLiteral("status"), cpuReady && memoryReady
                     ? QStringLiteral("ready")
                     : (cpuReady || memoryReady ? QStringLiteral("partial") : QStringLiteral("unavailable")));
    if (!cpuReady && !memoryReady) {
        refreshed.insert(QStringLiteral("error"), QStringLiteral("CPU and RAM telemetry are unavailable"));
    } else if (!cpuReady || !memoryReady) {
        refreshed.insert(QStringLiteral("error"), QStringLiteral("Some core telemetry is unavailable"));
    } else {
        refreshed.insert(QStringLiteral("error"), QString());
    }

    m_snapshot = refreshed;
    requestNpuUtilization();
    emit changed();
    return m_snapshot;
}
