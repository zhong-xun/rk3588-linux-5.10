#include "npu_load_reader.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>

#include <cmath>

namespace {

constexpr int kHelperStartTimeoutMs = 250;
constexpr int kHelperFinishTimeoutMs = 900;
constexpr int kMaximumDebugfsBytes = 4096;
constexpr auto kDebugfsPath = "/sys/kernel/debug/rknpu/load";
constexpr auto kHelperPath = "/usr/lib/boardlab/boardlab-npu-load";
constexpr int kMinimumCoreCount = 1;
constexpr int kMaximumCoreCount = 3;

int normalizedCoreCount(int expectedCoreCount)
{
    return expectedCoreCount < kMinimumCoreCount ? kMinimumCoreCount
        : (expectedCoreCount > kMaximumCoreCount ? kMaximumCoreCount : expectedCoreCount);
}

NpuLoadReading unavailableReading(const QString &message, int expectedCoreCount)
{
    NpuLoadReading reading;
    reading.available = false;
    reading.cores = NpuLoadReader::unavailableCores(expectedCoreCount);
    reading.averagePercent = -1.0;
    reading.source = QStringLiteral("rknpu-debugfs");
    reading.message = message;
    return reading;
}

NpuLoadReading makeReading(const QVariantList &cores, double averagePercent)
{
    NpuLoadReading reading;
    reading.available = true;
    reading.cores = cores;
    reading.averagePercent = averagePercent;
    reading.source = QStringLiteral("rknpu-debugfs");
    reading.message = QStringLiteral("RKNPU debugfs");
    return reading;
}

NpuLoadReading parseHelperJson(const QByteArray &payload, int expectedCoreCount)
{
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return unavailableReading(QStringLiteral("RKNPU load helper returned invalid data"), coreCount);
    }

    const QJsonObject values = document.object();
    if (values.size() != coreCount) {
        return unavailableReading(QStringLiteral("RKNPU load helper returned incomplete core data"), coreCount);
    }
    QVariantList cores;
    double total = 0.0;
    for (int coreId = 0; coreId < coreCount; ++coreId) {
        const QJsonValue value = values.value(QStringLiteral("core%1").arg(coreId));
        if (!value.isDouble()) {
            return unavailableReading(QStringLiteral("RKNPU load helper returned incomplete core data"), coreCount);
        }
        const double percent = value.toDouble();
        if (!std::isfinite(percent) || std::floor(percent) != percent || percent < 0.0 || percent > 100.0) {
            return unavailableReading(QStringLiteral("RKNPU load helper returned invalid core data"), coreCount);
        }
        QVariantMap core;
        core.insert(QStringLiteral("core"), coreId);
        core.insert(QStringLiteral("percent"), percent);
        cores.append(core);
        total += percent;
    }
    return makeReading(cores, total / static_cast<double>(coreCount));
}

// RKNPU debugfs formats are generation-specific:
//   RK3562/RK3568: NPU load:  42%
//   RK3576:        NPU load: Core0: 42%, Core1: 31%,
//   RK3588:        NPU load: Core0: 42%, Core1: 31%, Core2: 18%,
// Match the expected topology exactly so a board never publishes a partial or
// incompatible load sample as valid telemetry.
NpuLoadReading parseDebugfsText(const QString &text, int expectedCoreCount)
{
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    static const QRegularExpression oneCorePattern(
        QStringLiteral("^\\s*NPU\\s+load:\\s*(0|[1-9][0-9]?|100)%,?\\s*$"));
    static const QRegularExpression twoCorePattern(
        QStringLiteral("^\\s*NPU\\s+load:\\s*Core0:\\s*(0|[1-9][0-9]?|100)%,\\s*"
                       "Core1:\\s*(0|[1-9][0-9]?|100)%,?\\s*$"));
    static const QRegularExpression threeCorePattern(
        QStringLiteral("^\\s*NPU\\s+load:\\s*Core0:\\s*(0|[1-9][0-9]?|100)%,\\s*"
                       "Core1:\\s*(0|[1-9][0-9]?|100)%,\\s*"
                       "Core2:\\s*(0|[1-9][0-9]?|100)%,?\\s*$"));
    const QRegularExpression *pattern = coreCount == 1 ? &oneCorePattern
        : (coreCount == 2 ? &twoCorePattern : &threeCorePattern);
    const QRegularExpressionMatch match = pattern->match(text);
    if (!match.hasMatch()) {
        return unavailableReading(QStringLiteral("RKNPU core load is unavailable"), coreCount);
    }

    QVariantList cores;
    double total = 0.0;
    for (int coreId = 0; coreId < coreCount; ++coreId) {
        bool valid = false;
        const int percent = match.captured(coreId + 1).toInt(&valid);
        if (!valid || percent < 0 || percent > 100) {
            return unavailableReading(QStringLiteral("RKNPU core load is unavailable"), coreCount);
        }
        QVariantMap core;
        core.insert(QStringLiteral("core"), coreId);
        core.insert(QStringLiteral("percent"), percent);
        cores.append(core);
        total += percent;
    }
    return makeReading(cores, total / static_cast<double>(coreCount));
}

// Runs a fixed no-argument helper (or sudo -n of that helper). Never uses a shell.
NpuLoadReading runNpuLoadHelper(const QString &program, const QStringList &arguments,
                                int expectedCoreCount)
{
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(kHelperStartTimeoutMs)) {
        return unavailableReading(QStringLiteral("RKNPU load helper is unavailable"), coreCount);
    }
    if (!process.waitForFinished(kHelperFinishTimeoutMs)) {
        process.kill();
        process.waitForFinished(100);
        return unavailableReading(QStringLiteral("RKNPU load helper timed out"), coreCount);
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return unavailableReading(QStringLiteral("RKNPU core load is unavailable"), coreCount);
    }
    return parseHelperJson(process.readAllStandardOutput(), coreCount);
}

} // namespace

QVariantList NpuLoadReader::unavailableCores(int expectedCoreCount)
{
    QVariantList cores;
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    for (int coreId = 0; coreId < coreCount; ++coreId) {
        QVariantMap core;
        core.insert(QStringLiteral("core"), coreId);
        core.insert(QStringLiteral("percent"), -1);
        cores.append(core);
    }
    return cores;
}

NpuLoadReading NpuLoadReader::readPrivileged(int expectedCoreCount)
{
    // Prefer the first successful available reading across:
    // 1) direct debugfs (root Buildroot kiosk / any process that can read it)
    // 2) installed helper without sudo (root, or future setuid)
    // 3) sudo -n helper (Ubuntu/Debian non-root with sudoers)
    // No shell, no OpenCV, fixed paths only.
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    const QString debugfsPath = QString::fromLatin1(kDebugfsPath);
    const QString helperPath = QString::fromLatin1(kHelperPath);

    const NpuLoadReading direct = readDebugfsFile(debugfsPath, coreCount);
    if (direct.available) {
        return direct;
    }

    const QFileInfo helperInfo(helperPath);
    if (helperInfo.isFile() && helperInfo.isExecutable()) {
        const NpuLoadReading helperReading = runNpuLoadHelper(helperPath, {}, coreCount);
        if (helperReading.available) {
            return helperReading;
        }
    }

    return runNpuLoadHelper(QStringLiteral("sudo"),
                            {QStringLiteral("-n"), helperPath}, coreCount);
}

NpuLoadReading NpuLoadReader::readDebugfsFile(const QString &path, int expectedCoreCount)
{
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return unavailableReading(QStringLiteral("RKNPU core load is unavailable"), coreCount);
    }
    const QByteArray bytes = file.read(kMaximumDebugfsBytes + 1);
    if (file.error() != QFile::NoError || bytes.size() > kMaximumDebugfsBytes) {
        return unavailableReading(QStringLiteral("RKNPU core load is unavailable"), coreCount);
    }
    return parseDebugfsText(QString::fromUtf8(bytes).remove(QChar('\0')).trimmed(), coreCount);
}
