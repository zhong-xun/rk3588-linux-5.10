#include "npu_load_reader.h"

#include "../runtimepaths.h"

#include <QDir>
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
constexpr auto kHelperPath = "/usr/lib/boardlab/boardlab-npu-load";
constexpr int kMinimumCoreCount = 1;
constexpr int kMaximumCoreCount = 16;

int normalizedCoreCount(int expectedCoreCount)
{
    return expectedCoreCount < kMinimumCoreCount ? kMinimumCoreCount
        : (expectedCoreCount > kMaximumCoreCount ? kMaximumCoreCount : expectedCoreCount);
}

NpuLoadReading unavailableReading(const QString &message, int expectedCoreCount,
                                  const QString &source = QStringLiteral("unavailable"))
{
    NpuLoadReading reading;
    reading.available = false;
    reading.cores = NpuLoadReader::unavailableCores(expectedCoreCount);
    reading.averagePercent = -1.0;
    reading.source = source;
    reading.message = message;
    return reading;
}

NpuLoadReading makeReading(const QVariantList &cores, double averagePercent,
                           const QString &source, const QString &message)
{
    NpuLoadReading reading;
    reading.available = true;
    reading.cores = cores;
    reading.averagePercent = averagePercent;
    reading.source = source;
    reading.message = message;
    return reading;
}

NpuLoadReading parseHelperJson(const QByteArray &payload, int expectedCoreCount,
                               const QString &source)
{
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(payload.trimmed(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return unavailableReading(QStringLiteral("RKNPU load helper returned invalid data"),
                                  coreCount, source);
    }

    const QJsonObject values = document.object();
    if (values.size() != coreCount) {
        return unavailableReading(QStringLiteral("RKNPU load helper returned incomplete core data"),
                                  coreCount, source);
    }
    QVariantList cores;
    double total = 0.0;
    for (int coreId = 0; coreId < coreCount; ++coreId) {
        const QJsonValue value = values.value(QStringLiteral("core%1").arg(coreId));
        if (!value.isDouble()) {
            return unavailableReading(QStringLiteral("RKNPU load helper returned incomplete core data"),
                                      coreCount, source);
        }
        const double percent = value.toDouble();
        if (!std::isfinite(percent) || std::floor(percent) != percent || percent < 0.0 || percent > 100.0) {
            return unavailableReading(QStringLiteral("RKNPU load helper returned invalid core data"),
                                      coreCount, source);
        }
        QVariantMap core;
        core.insert(QStringLiteral("core"), coreId);
        core.insert(QStringLiteral("percent"), percent);
        cores.append(core);
        total += percent;
    }
    return makeReading(cores, total / static_cast<double>(coreCount), source,
                       QStringLiteral("RKNPU load helper"));
}

// Match the expected topology exactly. The expression is assembled from the
// bounded core count so future drivers can report more than three cores
// without weakening validation of the legacy one-, two-, and three-core text.
NpuLoadReading parsePerCoreText(const QString &text, int expectedCoreCount,
                                const QString &source, const QString &message)
{
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    static const QRegularExpression oneCorePattern(
        QStringLiteral("^\\s*NPU\\s+load:\\s*(0|[1-9][0-9]?|100)%,?\\s*$"));
    if (coreCount == 1) {
        const QRegularExpressionMatch aggregateMatch = oneCorePattern.match(text);
        if (aggregateMatch.hasMatch()) {
            QVariantMap core;
            core.insert(QStringLiteral("core"), 0);
            core.insert(QStringLiteral("percent"), aggregateMatch.captured(1).toInt());
            return makeReading(QVariantList{core}, aggregateMatch.captured(1).toInt(),
                               source, message);
        }
    }

    QString patternText = QStringLiteral("^\\s*NPU\\s+load:\\s*");
    for (int coreId = 0; coreId < coreCount; ++coreId) {
        if (coreId > 0) {
            patternText += QStringLiteral("\\s*");
        }
        patternText += QStringLiteral("Core%1:\\s*(0|[1-9][0-9]?|100)%")
                           .arg(coreId);
        patternText += coreId + 1 < coreCount
            ? QStringLiteral(",") : QStringLiteral(",?");
    }
    patternText += QStringLiteral("\\s*$");
    const QRegularExpression pattern(patternText);
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch()) {
        return unavailableReading(QStringLiteral("RKNPU core load is unavailable"),
                                  coreCount, source);
    }

    QVariantList cores;
    double total = 0.0;
    for (int coreId = 0; coreId < coreCount; ++coreId) {
        bool valid = false;
        const int percent = match.captured(coreId + 1).toInt(&valid);
        if (!valid || percent < 0 || percent > 100) {
            return unavailableReading(QStringLiteral("RKNPU core load is unavailable"),
                                      coreCount, source);
        }
        QVariantMap core;
        core.insert(QStringLiteral("core"), coreId);
        core.insert(QStringLiteral("percent"), percent);
        cores.append(core);
        total += percent;
    }
    return makeReading(cores, total / static_cast<double>(coreCount), source, message);
}

NpuLoadReading parseAggregateText(const QString &text, int expectedCoreCount,
                                  const QString &source, const QString &message)
{
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    // Linux devfreq commonly emits "42@800000000Hz"; some vendor sysfs
    // implementations expose just "42" or "42%".
    static const QRegularExpression aggregatePattern(
        QStringLiteral("^\\s*(0|[1-9][0-9]?|100)\\s*%?(?:\\s*@\\s*[0-9]+\\s*(?:Hz)?)?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = aggregatePattern.match(text);
    if (!match.hasMatch()) {
        return unavailableReading(QStringLiteral("RKNPU aggregate load is unavailable"),
                                  coreCount, source);
    }
    bool valid = false;
    const int percent = match.captured(1).toInt(&valid);
    if (!valid || percent < 0 || percent > 100) {
        return unavailableReading(QStringLiteral("RKNPU aggregate load is unavailable"),
                                  coreCount, source);
    }

    QVariantList cores = NpuLoadReader::unavailableCores(coreCount);
    if (coreCount == 1) {
        QVariantMap core;
        core.insert(QStringLiteral("core"), 0);
        core.insert(QStringLiteral("percent"), percent);
        cores[0] = core;
    }
    return makeReading(cores, percent, source, message);
}

bool readBoundedFile(const QString &path, QByteArray *payload)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray bytes = file.read(kMaximumDebugfsBytes + 1);
    if (file.error() != QFile::NoError || bytes.size() > kMaximumDebugfsBytes) {
        return false;
    }
    if (payload) {
        *payload = bytes;
    }
    return true;
}

NpuLoadReading readLoadFile(const QString &path, int expectedCoreCount,
                            const QString &source, const QString &message,
                            bool allowAggregate)
{
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    QByteArray bytes;
    if (!readBoundedFile(path, &bytes)) {
        return unavailableReading(QStringLiteral("RKNPU core load is unavailable"),
                                  coreCount, source);
    }
    const QString text = QString::fromUtf8(bytes).remove(QChar('\0')).trimmed();
    const NpuLoadReading perCore = parsePerCoreText(text, coreCount, source, message);
    if (perCore.available || !allowAggregate) {
        return perCore;
    }
    return parseAggregateText(text, coreCount, source, message);
}

QString configuredSysRoot()
{
    return boardlab::sysRoot();
}

QStringList npuSysfsLoadCandidates(const QString &sysRoot)
{
    QStringList candidates;
    const auto appendCandidate = [&candidates](const QString &path) {
        if (QFileInfo(path).isFile() && !candidates.contains(path)) {
            candidates.append(path);
        }
    };

    const QDir devfreq(QDir(sysRoot).filePath(QStringLiteral("class/devfreq")));
    const QFileInfoList devfreqEntries = devfreq.entryInfoList(
        QDir::Dirs | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : devfreqEntries) {
        QByteArray nameBytes;
        readBoundedFile(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("name")),
                        &nameBytes);
        const QString identity = entry.fileName() + QLatin1Char(' ')
            + entry.canonicalFilePath() + QLatin1Char(' ')
            + QString::fromUtf8(nameBytes);
        if (identity.contains(QStringLiteral("npu"), Qt::CaseInsensitive)) {
            appendCandidate(QDir(entry.absoluteFilePath()).filePath(QStringLiteral("load")));
        }
    }

    const QStringList fixedCandidates {
        QDir(sysRoot).filePath(QStringLiteral("class/misc/rknpu/load")),
        QDir(sysRoot).filePath(QStringLiteral("class/misc/rknpu/device/load")),
        QDir(sysRoot).filePath(QStringLiteral("devices/platform/rknpu/load")),
    };
    for (const QString &candidate : fixedCandidates) {
        appendCandidate(candidate);
    }
    return candidates;
}

// Runs a fixed no-argument helper (or sudo -n of that helper). Never uses a shell.
NpuLoadReading runNpuLoadHelper(const QString &program, const QStringList &arguments,
                                int expectedCoreCount, const QString &source)
{
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(kHelperStartTimeoutMs)) {
        return unavailableReading(QStringLiteral("RKNPU load helper is unavailable"),
                                  coreCount, source);
    }
    if (!process.waitForFinished(kHelperFinishTimeoutMs)) {
        process.kill();
        process.waitForFinished(100);
        return unavailableReading(QStringLiteral("RKNPU load helper timed out"),
                                  coreCount, source);
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        return unavailableReading(QStringLiteral("RKNPU core load is unavailable"),
                                  coreCount, source);
    }
    return parseHelperJson(process.readAllStandardOutput(), coreCount, source);
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
    // 2) an NPU devfreq/sysfs aggregate load node
    // 3) installed helper without sudo (root, or future setuid)
    // 4) sudo -n helper (Ubuntu/Debian non-root with sudoers)
    // No shell, no OpenCV, fixed paths only.
    const int coreCount = normalizedCoreCount(expectedCoreCount);
    const QString sysRoot = configuredSysRoot();
    const QString debugfsPath = QDir(sysRoot).filePath(
        QStringLiteral("kernel/debug/rknpu/load"));
    const QString helperPath = QString::fromLatin1(kHelperPath);

    const NpuLoadReading direct = readDebugfsFile(debugfsPath, coreCount);
    if (direct.available) {
        return direct;
    }

    for (const QString &candidate : npuSysfsLoadCandidates(sysRoot)) {
        const NpuLoadReading sysfs = readLoadFile(
            candidate, coreCount, QStringLiteral("npu-devfreq-sysfs"),
            QStringLiteral("NPU devfreq aggregate load"), true);
        if (sysfs.available) {
            return sysfs;
        }
    }

    // A test/sysroot override is an isolation boundary. Never fall through to
    // privileged host helpers when the caller is inspecting a fixture tree.
    if (boardlab::fixtureRootConfigured("BOARDLAB_SYS_ROOT")) {
        return unavailableReading(QStringLiteral("RKNPU load telemetry is unavailable"),
                                  coreCount);
    }

    const QFileInfo helperInfo(helperPath);
    if (helperInfo.isFile() && helperInfo.isExecutable()) {
        const NpuLoadReading helperReading = runNpuLoadHelper(
            helperPath, {}, coreCount, QStringLiteral("rknpu-helper"));
        if (helperReading.available) {
            return helperReading;
        }
        const NpuLoadReading sudoReading = runNpuLoadHelper(
            QStringLiteral("sudo"), {QStringLiteral("-n"), helperPath}, coreCount,
            QStringLiteral("sudo-rknpu-helper"));
        if (sudoReading.available) {
            return sudoReading;
        }
    }

    return unavailableReading(QStringLiteral("RKNPU load telemetry is unavailable"),
                              coreCount);
}

NpuLoadReading NpuLoadReader::readDebugfsFile(const QString &path, int expectedCoreCount)
{
    return readLoadFile(path, expectedCoreCount, QStringLiteral("rknpu-debugfs"),
                        QStringLiteral("RKNPU debugfs"), false);
}
