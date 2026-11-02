#include "localwebserver.h"

#include "boardbackend.h"
#include "boardprofile.h"
#include "npu/npu_yolo_demo.h"
#include "qtcompat.h"
#include "resourcemonitor.h"
#include "systeminfo.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QSysInfo>
#include <QUrl>
#include <QUrlQuery>
#include <QVariantMap>

#include <cmath>

namespace {

constexpr qint64 kMaximumRequestHeaderBytes = 32LL * 1024LL;
constexpr qint64 kMaximumUploadBytes = 64LL * 1024LL * 1024LL;
constexpr qint64 kMaximumUploadRequestBytes = kMaximumRequestHeaderBytes + kMaximumUploadBytes;
constexpr auto kFileUploadPath = "/api/files/upload";
constexpr int kMaximumNpuCoreCount = 16;
constexpr int kMaximumNpuWorkerCount = 8;

QString supportedTargetText()
{
    // Compatibility API: existing clients compare this field verbatim.
    return QStringLiteral("iTOP-RK3562/RK3568/RK3576/RK3588/RK3588S (Buildroot 2021.11/2024.02) / iTOP-RK3588/RK3588S (Debian 11/12, Ubuntu 20.04/22.04/24.04) / iTOP-RK3568 (Debian 11, Ubuntu 20.04)");
}

QString compatibilityScopeText()
{
    return QStringLiteral("Rockchip Linux SDK（Qt 5.12+；内核 4.19/5.10/6.1 及兼容后续版本；Buildroot 固定 Weston，Ubuntu/Debian 固定 X11；未验证底板由 profile 显式授权硬件）");
}

QList<int> boundedNpuCoreIds(const QVariantList &values)
{
    QList<int> coreIds;
    for (const QVariant &value : values) {
        bool valid = false;
        const int coreId = value.toInt(&valid);
        if (!valid || coreId < 0 || coreId >= kMaximumNpuCoreCount
            || coreIds.contains(coreId)) {
            continue;
        }
        coreIds.append(coreId);
    }
    return coreIds;
}

QList<int> boundedNpuCoreIdsFromReadings(const QVariantList &values)
{
    QList<int> coreIds;
    for (const QVariant &value : values) {
        const QVariantMap core = value.toMap();
        bool valid = false;
        const int coreId = core.value(QStringLiteral("core")).toInt(&valid);
        if (!valid || coreId < 0 || coreId >= kMaximumNpuCoreCount
            || coreIds.contains(coreId)) {
            continue;
        }
        coreIds.append(coreId);
    }
    return coreIds;
}

QString recognizedSocFamily(const QString &value)
{
    static const QRegularExpression expression(
        QStringLiteral("(?:^|[^a-z0-9])((?:rk|rv)[0-9]{4}[a-z0-9]*|px[0-9]{2}[a-z0-9]*)(?:[^a-z0-9]|$)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(value.trimmed());
    if (match.hasMatch()) {
        return match.captured(1).toUpper();
    }
    return QString();
}

QString profileLabel(const BoardProfile *profile, const QString &socEvidence = QString())
{
    if (profile && !profile->displayName().trimmed().isEmpty()) {
        return profile->displayName().trimmed();
    }
    if (!socEvidence.isEmpty()) {
        return socEvidence + QStringLiteral(" Board");
    }
    return QStringLiteral("Linux Board");
}

QString provenanceProfileLabel(const QJsonObject &provenance)
{
    const QString target = provenance.value(QStringLiteral("targetLabel")).toString().trimmed();
    if (!target.isEmpty()) {
        return target;
    }
    return profileLabel(nullptr, provenance.value(QStringLiteral("socEvidence")).toString());
}

QString hardwareValidationInstruction(const QJsonObject &)
{
    return QStringLiteral("在目标板运行 boardlab-hardware-validate.sh 并留存验收证据");
}

QString supportedRuntimeText()
{
    return QStringLiteral("Linux + Qt Quick 5.12+ + QtNetwork");
}

QString osReleaseValue(const QString &key)
{
    QFile osRelease(QStringLiteral("/etc/os-release"));
    if (!osRelease.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    const QString prefix = key + QLatin1Char('=');
    const QStringList lines = QString::fromLocal8Bit(osRelease.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(prefix)) {
            continue;
        }
        QString value = trimmed.mid(prefix.size()).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
            && value.endsWith(QLatin1Char('"'))) {
            value = value.mid(1, value.size() - 2);
        }
        return value;
    }
    return QString();
}

QString testRuntimeOs()
{
    if (qEnvironmentVariable("BOARDLAB_TEST_MODE") != QStringLiteral("1")) {
        return QString();
    }
    return qEnvironmentVariable("BOARDLAB_TEST_RUNTIME_OS").trimmed().toLower();
}

QString runtimeProductName()
{
    const QString testRuntime = testRuntimeOs();
    if (testRuntime.startsWith(QStringLiteral("ubuntu:"))) {
        return QStringLiteral("Ubuntu");
    }
    if (testRuntime.startsWith(QStringLiteral("debian:"))) {
        return QStringLiteral("Debian");
    }
    return QSysInfo::prettyProductName();
}

QString runtimeProductVersion()
{
    const QString testRuntime = testRuntimeOs();
    const int separator = testRuntime.indexOf(QLatin1Char(':'));
    if (separator > 0 && separator + 1 < testRuntime.size()) {
        return testRuntime.mid(separator + 1);
    }
    return QSysInfo::productVersion();
}

bool buildrootRuntime()
{
    // Runtime test fixtures can exercise the Buildroot readiness gate without
    // editing the host's os-release.  Production environment variables never
    // alter platform selection.
    const QString testRuntime = testRuntimeOs();
    if (!testRuntime.isEmpty()) {
        return testRuntime == QStringLiteral("buildroot");
    }

    const QString product = QSysInfo::prettyProductName();
    if (product.contains(QStringLiteral("Buildroot"), Qt::CaseInsensitive)) {
        return true;
    }

    const QString id = osReleaseValue(QStringLiteral("ID"));
    const QString idLike = osReleaseValue(QStringLiteral("ID_LIKE"));
    return id.compare(QStringLiteral("buildroot"), Qt::CaseInsensitive) == 0
        || idLike.split(QRegularExpression(QStringLiteral("\\s+")), boardlab::kSkipEmptyParts)
               .contains(QStringLiteral("buildroot"), Qt::CaseInsensitive);
}

QString runtimeBaselineLabel()
{
    return buildrootRuntime() ? QStringLiteral("Buildroot 产品基线")
                              : QStringLiteral("Ubuntu/Debian 基线");
}

bool supportedRuntimeOs()
{
    if (buildrootRuntime()) {
        return true;
    }

    const QString product = runtimeProductName();
    const QString version = runtimeProductVersion();
    const bool ubuntu = product.contains(QStringLiteral("Ubuntu"), Qt::CaseInsensitive);
    const bool debian = product.contains(QStringLiteral("Debian"), Qt::CaseInsensitive);
    const bool validatedUbuntu = version.startsWith(QStringLiteral("20.04"))
        || version.startsWith(QStringLiteral("22.04"))
        || version.startsWith(QStringLiteral("24.04"))
        || product.contains(QStringLiteral("20.04"))
        || product.contains(QStringLiteral("22.04"))
        || product.contains(QStringLiteral("24.04"));
    const bool validatedDebian = version.startsWith(QStringLiteral("11"))
        || version.startsWith(QStringLiteral("12"))
        || product.contains(QStringLiteral(" 11"))
        || product.contains(QStringLiteral(" 12"));
    return (ubuntu && validatedUbuntu) || (debian && validatedDebian);
}

int countToken(const QString &text, const QString &token)
{
    int count = 0;
    int index = 0;
    while ((index = text.indexOf(token, index)) >= 0) {
        ++count;
        index += token.size();
    }
    return count;
}

QString readTrimmedTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString text = QString::fromUtf8(file.readAll());
    text.replace(QChar::Null, QLatin1Char(' '));
    return text.simplified();
}

QByteArray requestHeaderValue(const QByteArray &headerBlock, const QByteArray &name)
{
    const QByteArray normalizedName = name.toLower();
    const QList<QByteArray> lines = headerBlock.split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const int separator = line.indexOf(':');
        if (separator > 0 && line.left(separator).trimmed().toLower() == normalizedName) {
            return line.mid(separator + 1).trimmed();
        }
    }
    return QByteArray();
}

bool parseContentLength(const QByteArray &headerBlock, qint64 *contentLength, bool *provided)
{
    if (!contentLength || !provided) {
        return false;
    }

    *contentLength = 0;
    *provided = false;
    const QList<QByteArray> lines = headerBlock.split('\n');
    for (int index = 1; index < lines.size(); ++index) {
        const QByteArray line = lines.at(index).trimmed();
        const int separator = line.indexOf(':');
        if (separator <= 0 || line.left(separator).trimmed().toLower() != QByteArrayLiteral("content-length")) {
            continue;
        }
        if (*provided) {
            return false;
        }
        const QByteArray value = line.mid(separator + 1).trimmed();
        bool ok = false;
        const qint64 parsed = value.toLongLong(&ok);
        if (!ok || value.isEmpty() || parsed < 0) {
            return false;
        }
        *provided = true;
        *contentLength = parsed;
    }
    return true;
}

bool isPathWithinRoot(const QString &path, const QString &root)
{
    if (path.isEmpty() || root.isEmpty()) {
        return false;
    }
    return path == root || path.startsWith(root + QLatin1Char('/'));
}

// Fixed create-only upload sandbox shared by Ubuntu/Debian desktop and
// Buildroot kiosk. Prefer BOARDLAB_UPLOAD_ROOT, then BOARDLAB_DATA_ROOT/uploads,
// then a writable product default. Home is only the last desktop fallback.
QString resolvedUploadRoot()
{
    auto ensureWritableDirectory = [](const QString &candidate) -> QString {
        const QString cleaned = QDir::cleanPath(candidate.trimmed());
        if (cleaned.isEmpty() || cleaned == QStringLiteral("/")) {
            return QString();
        }
        QDir dir(cleaned);
        if (!dir.exists() && !QDir().mkpath(cleaned)) {
            return QString();
        }
        QFileInfo info(cleaned);
        if (!info.exists() || !info.isDir() || !info.isWritable()) {
            return QString();
        }
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || canonical == QStringLiteral("/")) {
            return QString();
        }
        return canonical;
    };

    const QByteArray configured = qgetenv("BOARDLAB_UPLOAD_ROOT").trimmed();
    if (!configured.isEmpty()) {
        const QString root = ensureWritableDirectory(QString::fromLocal8Bit(configured));
        if (!root.isEmpty()) {
            return root;
        }
    }

    const QByteArray dataRoot = qgetenv("BOARDLAB_DATA_ROOT").trimmed();
    if (!dataRoot.isEmpty()) {
        const QString root = ensureWritableDirectory(
            QDir(QString::fromLocal8Bit(dataRoot)).filePath(QStringLiteral("uploads")));
        if (!root.isEmpty()) {
            return root;
        }
    }

    const QString productRoot = ensureWritableDirectory(QStringLiteral("/var/lib/boardlab/uploads"));
    if (!productRoot.isEmpty()) {
        return productRoot;
    }

    // Ubuntu/Debian desktop fallback when the product path is not prepared.
    // Never use bare "/" even if home is root on Buildroot.
    const QString home = QDir::homePath().trimmed();
    if (!home.isEmpty() && home != QStringLiteral("/")) {
        const QString homeRoot = ensureWritableDirectory(
            QDir(home).filePath(QStringLiteral("boardlab-uploads")));
        if (!homeRoot.isEmpty()) {
            return homeRoot;
        }
        const QString bareHome = ensureWritableDirectory(home);
        if (!bareHome.isEmpty() && bareHome != QStringLiteral("/root")
            && bareHome != QStringLiteral("/")) {
            return bareHome;
        }
    }

    return QString();
}

bool isSafeUploadFileName(const QString &fileName)
{
    if (fileName.isEmpty() || fileName.size() > 128 || fileName.startsWith(QLatin1Char('.'))
        || fileName == QStringLiteral(".") || fileName == QStringLiteral("..")
        || fileName.contains(QLatin1Char('/')) || fileName.contains(QLatin1Char('\\'))
        || fileName.contains(QChar::Null) || QFileInfo(fileName).fileName() != fileName) {
        return false;
    }
    for (const QChar character : fileName) {
        const ushort codePoint = character.unicode();
        if (codePoint <= 0x1f || (codePoint >= 0x7f && codePoint <= 0x9f)) {
            return false;
        }
    }
    return fileName.toUtf8().size() <= 255;
}

bool secureTokenMatches(const QByteArray &provided, const QByteArray &expected)
{
    if (provided.isEmpty() || expected.isEmpty()) {
        return false;
    }

    const QByteArray providedHash = QCryptographicHash::hash(provided, QCryptographicHash::Sha256);
    const QByteArray expectedHash = QCryptographicHash::hash(expected, QCryptographicHash::Sha256);
    unsigned char difference = 0;
    for (int i = 0; i < providedHash.size(); ++i) {
        difference |= static_cast<unsigned char>(providedHash.at(i) ^ expectedHash.at(i));
    }
    return difference == 0;
}

bool usableLanInterface(const QNetworkInterface &iface)
{
    const QString name = iface.name().toLower();
    const QNetworkInterface::InterfaceFlags flags = iface.flags();
    if (!(flags & QNetworkInterface::IsUp)
        || !(flags & QNetworkInterface::IsRunning)
        || (flags & QNetworkInterface::IsLoopBack)
        || (flags & QNetworkInterface::IsPointToPoint)) {
        return false;
    }

    return !(name.startsWith(QStringLiteral("lo"))
             || name.startsWith(QStringLiteral("docker"))
             || name.startsWith(QStringLiteral("veth"))
             || name.startsWith(QStringLiteral("virbr"))
             || name.startsWith(QStringLiteral("br-"))
             || name.startsWith(QStringLiteral("cni"))
             || name.startsWith(QStringLiteral("flannel"))
             || name.startsWith(QStringLiteral("kube"))
             || name.startsWith(QStringLiteral("podman"))
             || name.startsWith(QStringLiteral("tun"))
             || name.startsWith(QStringLiteral("tap"))
             || name.startsWith(QStringLiteral("wg"))
             || name.startsWith(QStringLiteral("zt"))
             || name.startsWith(QStringLiteral("tailscale"))
             || name.startsWith(QStringLiteral("vmnet"))
             || name.startsWith(QStringLiteral("vbox")));
}

bool usableLanAddress(const QHostAddress &address)
{
    if (address.protocol() != QAbstractSocket::IPv4Protocol) {
        return false;
    }

    const QString host = address.toString();
    return !host.isEmpty()
        && host != QStringLiteral("0.0.0.0")
        && !host.startsWith(QStringLiteral("127."))
        && !host.startsWith(QStringLiteral("169.254."));
}

QString defaultRouteInterfaceName()
{
    QFile routeFile(QStringLiteral("/proc/net/route"));
    if (!routeFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    QString selected;
    int selectedMetric = 2147483647;
    const QStringList lines = QString::fromUtf8(routeFile.readAll()).split(QLatin1Char('\n'));
    for (int i = 1; i < lines.size(); ++i) {
        const QStringList fields = lines.at(i).simplified().split(QLatin1Char(' '));
        if (fields.size() < 8 || fields.at(1) != QStringLiteral("00000000")) {
            continue;
        }

        bool flagsOk = false;
        const uint flags = fields.at(3).toUInt(&flagsOk, 16);
        if (!flagsOk || !(flags & 0x1)) {
            continue;
        }

        bool metricOk = false;
        const int metric = fields.at(6).toInt(&metricOk);
        const int routeMetric = metricOk ? metric : 0;
        if (selected.isEmpty() || routeMetric < selectedMetric) {
            selected = fields.at(0);
            selectedMetric = routeMetric;
        }
    }
    return selected;
}

QString lanAddressForInterface(const QString &interfaceName)
{
    if (interfaceName.isEmpty()) {
        return QString();
    }

    const QNetworkInterface iface = QNetworkInterface::interfaceFromName(interfaceName);
    if (!iface.isValid() || !usableLanInterface(iface)) {
        return QString();
    }

    const QList<QNetworkAddressEntry> entries = iface.addressEntries();
    for (const QNetworkAddressEntry &entry : entries) {
        if (usableLanAddress(entry.ip())) {
            return entry.ip().toString();
        }
    }
    return QString();
}

QString cpuInfoValue(const QString &key)
{
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }

    const QString prefix = key + QLatin1Char(':');
    const QStringList lines = QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(prefix, Qt::CaseInsensitive)) {
            return line.mid(prefix.size()).trimmed();
        }
    }
    return QString();
}

QJsonObject boardProvenance(const BoardProfile *profile)
{
    const QString arch = QSysInfo::currentCpuArchitecture();
    const QString deviceTreeModel = profile ? profile->deviceTreeModel() : readTrimmedTextFile(QStringLiteral("/proc/device-tree/model"));
    const QString deviceTreeCompatible = profile ? profile->deviceTreeCompatible() : readTrimmedTextFile(QStringLiteral("/proc/device-tree/compatible"));
    const QString cpuHardware = cpuInfoValue(QStringLiteral("Hardware"));
    const QString cpuModel = cpuInfoValue(QStringLiteral("model name"));
    QString explicitTarget = QString::fromLocal8Bit(qgetenv("BOARDLAB_TARGET")).trimmed();
    if (explicitTarget.isEmpty()) {
        explicitTarget = QString::fromLocal8Bit(qgetenv("RK_SHELL_QT_TARGET")).trimmed();
    }
    const QString legacyTarget = QString::fromLocal8Bit(qgetenv("RK3588_TARGET")).trimmed();
    const QString targetMarker = !explicitTarget.isEmpty() ? explicitTarget : legacyTarget;

    const QString hardwareEvidence = QStringList{deviceTreeModel, deviceTreeCompatible, cpuHardware, cpuModel}.join(QLatin1Char(' '));
    const QString profileSocFamily = profile ? recognizedSocFamily(profile->socFamily()) : QString();
    const QString hardwareSocFamily = recognizedSocFamily(hardwareEvidence);
    const QString markerSocFamily = recognizedSocFamily(targetMarker);
    // A selected BoardProfile is derived from the device tree (or an explicit
    // profile override) and therefore wins over a stale legacy target marker.
    const QString socEvidence = !profileSocFamily.isEmpty()
        ? profileSocFamily
        : (!hardwareSocFamily.isEmpty() ? hardwareSocFamily : markerSocFamily);
    const bool rk3588Evidence = socEvidence == QStringLiteral("RK3588");
    const bool rk3568Evidence = socEvidence == QStringLiteral("RK3568");
    const bool rockchipEvidence = !socEvidence.isEmpty();
    const bool armEvidence = arch.contains(QStringLiteral("arm"), Qt::CaseInsensitive)
        || arch.contains(QStringLiteral("aarch64"), Qt::CaseInsensitive);
    const bool explicitMarker = !markerSocFamily.isEmpty();
    const QString activeProfileLabel = profileLabel(profile, socEvidence);

    QJsonArray evidence;
    auto addEvidence = [&evidence](const QString &source, const QString &value) {
        if (value.trimmed().isEmpty()) {
            return;
        }
        QJsonObject item;
        item.insert(QStringLiteral("source"), source);
        item.insert(QStringLiteral("value"), value);
        evidence.append(item);
    };
    addEvidence(QStringLiteral("/proc/device-tree/model"), deviceTreeModel);
    addEvidence(QStringLiteral("/proc/device-tree/compatible"), deviceTreeCompatible);
    addEvidence(QStringLiteral("/proc/cpuinfo Hardware"), cpuHardware);
    addEvidence(QStringLiteral("/proc/cpuinfo model name"), cpuModel);
    addEvidence(QStringLiteral("BOARDLAB_TARGET, RK_SHELL_QT_TARGET or RK3588_TARGET"), targetMarker);

    QJsonObject body = profile ? profile->toJson() : QJsonObject();
    body.insert(QStringLiteral("architecture"), arch);
    body.insert(QStringLiteral("productName"), QSysInfo::prettyProductName());
    body.insert(QStringLiteral("deviceTreeModel"), deviceTreeModel);
    body.insert(QStringLiteral("deviceTreeCompatible"), deviceTreeCompatible);
    body.insert(QStringLiteral("cpuHardware"), cpuHardware);
    body.insert(QStringLiteral("cpuModel"), cpuModel);
    body.insert(QStringLiteral("targetMarker"), targetMarker);
    body.insert(QStringLiteral("evidence"), evidence);
    body.insert(QStringLiteral("rk3588Evidence"), rk3588Evidence);
    body.insert(QStringLiteral("rk3568Evidence"), rk3568Evidence);
    body.insert(QStringLiteral("rockchipEvidence"), rockchipEvidence);
    body.insert(QStringLiteral("armEvidence"), armEvidence);
    body.insert(QStringLiteral("socEvidence"), socEvidence);
    body.insert(QStringLiteral("profileId"), profile ? profile->id() : QString());
    body.insert(QStringLiteral("profileLabel"), activeProfileLabel);
    body.insert(QStringLiteral("targetLabel"), activeProfileLabel);
    body.insert(QStringLiteral("targetBoardEvidence"), !socEvidence.isEmpty());
    body.insert(QStringLiteral("supportedTarget"), supportedTargetText());
    body.insert(QStringLiteral("compatibilityScope"), compatibilityScopeText());
    body.insert(QStringLiteral("state"), profile ? profile->id()
                                                  : (rk3588Evidence ? QStringLiteral("rk3588")
                                                                     : (rk3568Evidence ? QStringLiteral("rk3568")
                                                                                       : (explicitMarker ? QStringLiteral("target-marker")
                                                                                                         : (armEvidence ? QStringLiteral("arm-preview") : QStringLiteral("desktop-preview"))))));
    return body;
}

QJsonObject hardwareValidationSummary()
{
    const QString path = QStringLiteral("/tmp/boardlab-hardware-validation.txt");
    const QString jsonPath = QStringLiteral("/tmp/boardlab-hardware-validation.json");
    QFile jsonFile(jsonPath);
    if (jsonFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QJsonDocument document = QJsonDocument::fromJson(jsonFile.readAll());
        if (document.isObject()) {
            QJsonObject summary = document.object();
            summary.insert(QStringLiteral("path"), path);
            summary.insert(QStringLiteral("jsonPath"), jsonPath);
            summary.insert(QStringLiteral("present"), QFile::exists(path));
            summary.insert(QStringLiteral("jsonPresent"), true);
            return summary;
        }
    }

    QFile file(path);
    QJsonObject summary;
    summary.insert(QStringLiteral("path"), path);
    summary.insert(QStringLiteral("jsonPath"), jsonPath);
    summary.insert(QStringLiteral("present"), file.exists());
    summary.insert(QStringLiteral("jsonPresent"), QFile::exists(jsonPath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        summary.insert(QStringLiteral("pass"), 0);
        summary.insert(QStringLiteral("warn"), 0);
        summary.insert(QStringLiteral("fail"), 0);
        summary.insert(QStringLiteral("manual"), 0);
        summary.insert(QStringLiteral("state"), QStringLiteral("not-run"));
        return summary;
    }

    const QString text = QString::fromUtf8(file.readAll());
    const int fail = countToken(text, QStringLiteral("[FAIL]"));
    const int warn = countToken(text, QStringLiteral("[WARN]"));
    const int manual = countToken(text, QStringLiteral("[MANUAL]"));
    summary.insert(QStringLiteral("pass"), countToken(text, QStringLiteral("[PASS]")));
    summary.insert(QStringLiteral("warn"), warn);
    summary.insert(QStringLiteral("fail"), fail);
    summary.insert(QStringLiteral("manual"), manual);
    summary.insert(QStringLiteral("state"), fail > 0 ? QStringLiteral("fail") : (manual > 0 || warn > 0 ? QStringLiteral("needs-review") : QStringLiteral("clear")));
    return summary;
}

QString scrubAbsolutePaths(const QString &value)
{
    QString scrubbed;
    scrubbed.reserve(value.size());
    for (int index = 0; index < value.size();) {
        const QChar current = value.at(index);
        const QChar previous = index > 0 ? value.at(index - 1) : QChar();
        const bool pathStart = current == QLatin1Char('/')
            && (index == 0 || previous.isSpace()
                || QStringLiteral("([{:='").contains(previous));
        if (!pathStart) {
            scrubbed.append(current);
            ++index;
            continue;
        }

        int end = index + 1;
        while (end < value.size()) {
            const QChar part = value.at(end);
            if (part.isSpace() || QStringLiteral(")]},;\"'").contains(part)) {
                break;
            }
            ++end;
        }
        scrubbed.append(QStringLiteral("<path>"));
        index = end;
    }
    return scrubbed;
}

QString boundedScrubbedText(const QString &value, const QString &fallback = QString())
{
    QString scrubbed = scrubAbsolutePaths(value).trimmed();
    if (scrubbed.isEmpty()) {
        return fallback;
    }

    constexpr int kMaximumCharacters = 360;
    if (scrubbed.size() > kMaximumCharacters) {
        scrubbed = scrubbed.left(kMaximumCharacters - 3) + QStringLiteral("...");
    }
    return scrubbed;
}

QString npuPreviewDirectory()
{
    QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (root.trimmed().isEmpty()) {
        root = QDir::tempPath() + QStringLiteral("/boardlab");
    }
    return QDir(root).absoluteFilePath(QStringLiteral("npu"));
}

bool isGeneratedNpuPreviewName(const QString &name)
{
    constexpr auto kPreviewPrefix = "yolov8-preview-";
    constexpr auto kResultPrefix = "yolov8-result-";
    constexpr auto kSuffix = ".jpg";
    const QString prefix = name.startsWith(QLatin1String(kPreviewPrefix))
        ? QLatin1String(kPreviewPrefix)
        : (name.startsWith(QLatin1String(kResultPrefix))
            ? QLatin1String(kResultPrefix) : QString());
    if (prefix.isEmpty() || !name.endsWith(QLatin1String(kSuffix))) {
        return false;
    }

    const QString serialText = name.mid(prefix.size(), name.size() - prefix.size() - 4);
    bool serialOk = false;
    const qulonglong serial = serialText.toULongLong(&serialOk);
    return serialOk && serial > 0;
}

QString validatedNpuPreviewPath(const QVariantMap &status)
{
    const QString requestedPath = status.value(QStringLiteral("previewPath")).toString().trimmed();
    if (requestedPath.isEmpty()) {
        return QString();
    }

    const QFileInfo previewInfo(requestedPath);
    if (!previewInfo.exists() || !previewInfo.isFile() || !previewInfo.isReadable()
        || previewInfo.isSymLink() || !isGeneratedNpuPreviewName(previewInfo.fileName())) {
        return QString();
    }

    constexpr qint64 kMaximumPreviewBytes = 16LL * 1024LL * 1024LL;
    if (previewInfo.size() < 3 || previewInfo.size() > kMaximumPreviewBytes) {
        return QString();
    }

    const QString previewRoot = QDir(npuPreviewDirectory()).canonicalPath();
    const QString canonicalPath = previewInfo.canonicalFilePath();
    if (previewRoot.isEmpty() || canonicalPath.isEmpty()) {
        return QString();
    }

    const QString normalizedRoot = QDir::cleanPath(previewRoot);
    const QString rootPrefix = normalizedRoot + QLatin1Char('/');
    if (!canonicalPath.startsWith(rootPrefix)) {
        return QString();
    }
    return canonicalPath;
}

} // namespace

LocalWebServer::LocalWebServer(SystemInfo *systemInfo, BoardBackend *boardBackend, BoardProfile *boardProfile,
                               ResourceMonitor *resourceMonitor, NpuYoloDemo *npuYoloDemo, QObject *parent)
    : QObject(parent)
    , m_systemInfo(systemInfo)
    , m_boardBackend(boardBackend)
    , m_boardProfile(boardProfile)
    , m_resourceMonitor(resourceMonitor)
    , m_npuYoloDemo(npuYoloDemo)
{
    connect(&m_server, &QTcpServer::newConnection, this, &LocalWebServer::handleConnection);
    connect(m_boardBackend, &BoardBackend::operationFinished, this,
            [this](const QString &operation, bool success, const QString &result, const QString &status) {
        m_lastOperation = QJsonObject{
            {QStringLiteral("action"), operation},
            {QStringLiteral("success"), success},
            {QStringLiteral("result"), result},
            {QStringLiteral("status"), status},
            {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}
        };
        emit changed();
    });
    if (m_npuYoloDemo) {
        connect(m_npuYoloDemo, &NpuYoloDemo::statusChanged, this, &LocalWebServer::changed);
    }
    m_statusText = QStringLiteral("Local Web disabled");
}

LocalWebServer::~LocalWebServer()
{
    m_server.close();
    closeClientSockets();
}

bool LocalWebServer::running() const
{
    return m_server.isListening();
}

QString LocalWebServer::bindAddress() const
{
    return m_bindAddress.toString();
}

quint16 LocalWebServer::port() const
{
    return running() ? m_server.serverPort() : m_port;
}

QString LocalWebServer::url() const
{
    if (!running()) {
        return QStringLiteral("disabled");
    }
    const QString host = m_bindAddress == QHostAddress::Any || m_bindAddress == QHostAddress::AnyIPv4
        ? lanHostAddress()
        : m_bindAddress.toString();
    return QStringLiteral("http://%1:%2").arg(host).arg(port());
}

QString LocalWebServer::lanHostAddress() const
{
    const QString routedHost = lanAddressForInterface(defaultRouteInterfaceName());
    if (!routedHost.isEmpty()) {
        return routedHost;
    }

    const QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!usableLanInterface(iface)) {
            continue;
        }

        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        for (const QNetworkAddressEntry &entry : entries) {
            if (usableLanAddress(entry.ip())) {
                return entry.ip().toString();
            }
        }
    }
    return QStringLiteral("127.0.0.1");
}

QString LocalWebServer::statusText() const
{
    return m_statusText;
}

bool LocalWebServer::start(const QHostAddress &address, quint16 port, const QByteArray &accessToken)
{
    if (running()) {
        stop();
    }

    m_accessToken = accessToken.trimmed();
    if (m_accessToken.isEmpty()) {
        m_statusText = QStringLiteral("Local Web refused: access token is required");
        emit changed();
        return false;
    }

    m_bindAddress = address;
    m_port = port;
    if (!m_server.listen(address, port)) {
        m_statusText = QStringLiteral("Local Web failed: %1").arg(m_server.errorString());
        emit changed();
        return false;
    }

    m_port = m_server.serverPort();
    m_statusText = QStringLiteral("Local Web listening on %1 (Bearer Token protected)").arg(url());
    emit changed();
    return true;
}

void LocalWebServer::stop()
{
    m_server.close();
    closeClientSockets();
    m_accessToken.clear();
    m_statusText = QStringLiteral("Local Web disabled");
    emit changed();
}

void LocalWebServer::closeClientSockets()
{
    const QList<QTcpSocket *> sockets = m_server.findChildren<QTcpSocket *>();
    for (QTcpSocket *socket : sockets) {
        // The socket is normally deleted after disconnect. Remove callbacks
        // into this object first, because QTcpServer owns the socket and may
        // destroy it after LocalWebServer members begin tearing down.
        QObject::disconnect(socket, nullptr, this, nullptr);
        socket->abort();
        socket->deleteLater();
    }
    m_requestBuffers.clear();
}

void LocalWebServer::handleConnection()
{
    while (m_server.hasPendingConnections()) {
        QTcpSocket *socket = m_server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleReadyRead(socket); });
        connect(socket, &QTcpSocket::destroyed, this, [this, socket]() { m_requestBuffers.remove(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void LocalWebServer::handleReadyRead(QTcpSocket *socket)
{
    QByteArray &buffer = m_requestBuffers[socket];
    buffer += socket->readAll();
    const QByteArray request = buffer;
    if (request.size() > kMaximumUploadRequestBytes) {
        m_requestBuffers.remove(socket);
        sendResponse(socket, 413, "application/json", "{\"error\":\"request_too_large\"}\n");
        return;
    }

    int headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        headerEnd = request.indexOf("\n\n");
    }
    if (headerEnd < 0) {
        if (request.size() > kMaximumRequestHeaderBytes) {
            m_requestBuffers.remove(socket);
            sendResponse(socket, 413, "application/json", "{\"error\":\"request_headers_too_large\"}\n");
        }
        return;
    }

    if (headerEnd > kMaximumRequestHeaderBytes) {
        m_requestBuffers.remove(socket);
        sendResponse(socket, 413, "application/json", "{\"error\":\"request_headers_too_large\"}\n");
        return;
    }

    const int headerLength = request.mid(headerEnd, 4) == QByteArray("\r\n\r\n") ? 4 : 2;
    const QList<QByteArray> lines = request.split('\n');
    if (lines.isEmpty()) {
        m_requestBuffers.remove(socket);
        sendNotFound(socket);
        return;
    }

    const QList<QByteArray> parts = lines.constFirst().trimmed().split(' ');
    if (parts.size() < 2) {
        m_requestBuffers.remove(socket);
        sendResponse(socket, 400, "application/json", "{\"error\":\"bad_request\"}\n");
        return;
    }

    const QByteArray method = parts.value(0);
    const QUrl requestUrl = QUrl::fromEncoded(parts.value(1), QUrl::StrictMode);
    if (!requestUrl.isValid()) {
        m_requestBuffers.remove(socket);
        sendResponse(socket, 400, "application/json", "{\"error\":\"invalid_request_target\"}\n");
        return;
    }
    const QString path = requestUrl.path(QUrl::FullyDecoded);
    if (path.startsWith(QStringLiteral("/api/")) && !isAuthorized(request.left(headerEnd))) {
        m_requestBuffers.remove(socket);
        sendUnauthorized(socket);
        return;
    }

    qint64 contentLength = 0;
    bool contentLengthProvided = false;
    if (!parseContentLength(request.left(headerEnd), &contentLength, &contentLengthProvided)) {
        m_requestBuffers.remove(socket);
        sendResponse(socket, 400, "application/json", "{\"error\":\"invalid_content_length\"}\n");
        return;
    }

    const bool isFileUpload = method == "POST" && path == QLatin1String(kFileUploadPath);
    if (isFileUpload && !contentLengthProvided) {
        m_requestBuffers.remove(socket);
        sendResponse(socket, 400, "application/json", "{\"error\":\"content_length_required\"}\n");
        return;
    }
    const qint64 maximumBodyBytes = isFileUpload
        ? kMaximumUploadBytes
        : kMaximumRequestHeaderBytes - headerEnd - headerLength;
    if (contentLength > maximumBodyBytes) {
        m_requestBuffers.remove(socket);
        sendResponse(socket, 413, "application/json", "{\"error\":\"request_too_large\"}\n");
        return;
    }
    const qint64 totalRequestBytes = static_cast<qint64>(headerEnd) + headerLength + contentLength;
    if (request.size() < totalRequestBytes) {
        return;
    }
    m_requestBuffers.remove(socket);

    if (isFileUpload) {
        const QByteArray body = request.mid(headerEnd + headerLength, contentLength);
        int uploadStatus = 201;
        const QByteArray uploadBody = uploadFileJson(requestUrl, body, &uploadStatus);
        sendResponse(socket, uploadStatus, "application/json; charset=utf-8", uploadBody);
    } else if (method == "POST" && path == QStringLiteral("/api/control")) {
        const QByteArray body = request.mid(headerEnd + headerLength, contentLength);
        int controlStatus = 200;
        const QByteArray controlBody = controlJson(body, &controlStatus);
        sendResponse(socket, controlStatus, "application/json; charset=utf-8", controlBody);
    } else if (method != "GET") {
        sendResponse(socket, 405, "application/json", "{\"error\":\"method_not_allowed\"}\n");
    } else if (path == QStringLiteral("/") || path == QStringLiteral("/index.html")) {
        const QByteArray dashboard = htmlDashboard();
        if (dashboard.isEmpty()) {
            sendResponse(socket, 500, "application/json; charset=utf-8",
                         "{\"error\":\"web_assets_missing\",\"message\":\"迅为开发板测试平台 Web 资源不可用，请重新安装当前版本\"}\n");
        } else {
            sendResponse(socket, 200, "text/html; charset=utf-8", dashboard,
                         "Content-Security-Policy: default-src 'self'; style-src 'self'; script-src 'self'; img-src 'self' data: blob:; connect-src 'self'; frame-ancestors 'none'\r\n"
                         "X-Frame-Options: DENY");
        }
    } else if (path == QStringLiteral("/assets/tokens.css")) {
        QFile asset(QStringLiteral(":/web/tokens.css"));
        if (asset.open(QIODevice::ReadOnly)) {
            sendResponse(socket, 200, "text/css; charset=utf-8", asset.readAll());
        } else {
            sendNotFound(socket);
        }
    } else if (path == QStringLiteral("/assets/styles.css")) {
        QFile asset(QStringLiteral(":/web/styles.css"));
        if (asset.open(QIODevice::ReadOnly)) {
            sendResponse(socket, 200, "text/css; charset=utf-8", asset.readAll());
        } else {
            sendNotFound(socket);
        }
    } else if (path == QStringLiteral("/assets/app.js")) {
        QFile asset(QStringLiteral(":/web/app.js"));
        if (asset.open(QIODevice::ReadOnly)) {
            sendResponse(socket, 200, "application/javascript; charset=utf-8", asset.readAll());
        } else {
            sendNotFound(socket);
        }
    } else if (path == QStringLiteral("/favicon.ico")) {
        QFile iconFile(QStringLiteral(":/icons/boardlab.svg"));
        if (iconFile.open(QIODevice::ReadOnly)) {
            sendResponse(socket, 200, "image/svg+xml", iconFile.readAll());
        } else {
            sendNotFound(socket);
        }
    } else if (path == QStringLiteral("/api/system")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", systemJson());
    } else if (path == QStringLiteral("/api/board")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", boardJson());
    } else if (path == QStringLiteral("/api/environment")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", environmentJson());
    } else if (path == QStringLiteral("/api/keys")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", keyTestJson());
    } else if (path == QStringLiteral("/api/gpu")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", gpuJson());
    } else if (path == QStringLiteral("/api/camera")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", cameraJson());
    } else if (path == QStringLiteral("/api/camera/preview")) {
        QByteArray preview;
        if (webCameraPreviewJpeg(&preview)) {
            sendResponse(socket, 200, "image/jpeg", preview,
                         "Content-Disposition: inline; filename=boardlab-camera-preview.jpg");
        } else {
            sendNotFound(socket);
        }
    } else if (path == QStringLiteral("/api/wifi")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", wifiJson());
    } else if (path == QStringLiteral("/api/npu-yolo")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", npuYoloJson());
    } else if (path == QStringLiteral("/api/npu-yolo/preview")) {
        QByteArray preview;
        if (npuYoloPreviewJpeg(&preview)) {
            sendResponse(socket, 200, "image/jpeg", preview,
                         "Content-Disposition: inline; filename=boardlab-npu-yolo-preview.jpg");
        } else {
            sendNotFound(socket);
        }
    } else if (path == QStringLiteral("/api/readiness")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", readinessJson());
    } else if (path == QStringLiteral("/api/manifest")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", manifestJson());
    } else if (path == QStringLiteral("/api/handoff")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", handoffJson());
    } else if (path == QStringLiteral("/healthz")) {
        sendResponse(socket, 200, "application/json; charset=utf-8", healthJson());
    } else {
        sendNotFound(socket);
    }
}

void LocalWebServer::sendResponse(QTcpSocket *socket, int statusCode, const QByteArray &contentType, const QByteArray &body,
                                  const QByteArray &extraHeaders) const
{
    const QByteArray reason = statusCode == 200 ? "OK"
        : (statusCode == 201 ? "Created"
                             : (statusCode == 400 ? "Bad Request"
                                                  : (statusCode == 401 ? "Unauthorized"
                                                                       : (statusCode == 403 ? "Forbidden"
                                                                                            : (statusCode == 405 ? "Method Not Allowed"
                                                                                                                 : (statusCode == 409 ? "Conflict"
                                                                                                                                      : (statusCode == 413 ? "Payload Too Large"
                                                                                                                                                           : (statusCode == 500 ? "Internal Server Error" : "Not Found"))))))));
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + reason + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Cache-Control: no-store\r\n";
    response += "X-Content-Type-Options: nosniff\r\n";
    if (!extraHeaders.isEmpty()) {
        response += extraHeaders;
        if (!extraHeaders.endsWith("\r\n")) {
            response += "\r\n";
        }
    }
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

void LocalWebServer::sendUnauthorized(QTcpSocket *socket) const
{
    sendResponse(socket, 401, "application/json; charset=utf-8",
                 "{\"error\":\"unauthorized\",\"message\":\"valid bearer token required\"}\n",
                 "WWW-Authenticate: Bearer realm=\"TopeetBoardTestPlatform\"");
}

void LocalWebServer::sendNotFound(QTcpSocket *socket) const
{
    sendResponse(socket, 404, "application/json", "{\"error\":\"not_found\"}\n");
}

bool LocalWebServer::isAuthorized(const QByteArray &headerBlock) const
{
    const QByteArray authorization = requestHeaderValue(headerBlock, QByteArrayLiteral("authorization"));
    const QByteArray prefix("Bearer ");
    if (authorization.size() <= prefix.size()
        || authorization.left(prefix.size()).toLower() != prefix.toLower()) {
        return false;
    }
    return secureTokenMatches(authorization.mid(prefix.size()).trimmed(), m_accessToken);
}

QByteArray LocalWebServer::controlJson(const QByteArray &body, int *statusCode)
{
    if (statusCode) {
        *statusCode = 200;
    }
    auto makeResponse = [](bool accepted, const QString &action, const QString &message, const QJsonObject &extra = QJsonObject()) {
        QJsonObject response = extra;
        response.insert(QStringLiteral("accepted"), accepted);
        response.insert(QStringLiteral("action"), action);
        response.insert(QStringLiteral("message"), message);
        response.insert(QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        return QJsonDocument(response).toJson(QJsonDocument::Indented);
    };
    auto reject = [statusCode, &makeResponse](int code,
                                               const QString &action,
                                               const QString &message,
                                               const QJsonObject &extra) {
        if (statusCode) {
            *statusCode = code;
        }
        return makeResponse(false, action, message, extra);
    };

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return reject(400, QString(), QStringLiteral("Invalid JSON control request"), QJsonObject());
    }

    const QJsonObject request = document.object();
    const QString action = request.value(QStringLiteral("action")).toString().trimmed();
    const QVariantMap parameters = request.value(QStringLiteral("params")).toObject().toVariantMap();
    if (action.isEmpty()) {
        return reject(400, action, QStringLiteral("Missing action"), QJsonObject());
    }

    if (action.startsWith(QStringLiteral("npu-yolo-"))) {
        const auto npuYoloExtra = [this]() {
            QJsonObject extra;
            extra.insert(QStringLiteral("npuYolo"), npuYoloStatusObject());
            return extra;
        };
        const QJsonValue rawParameters = request.value(QStringLiteral("params"));
        if (!rawParameters.isUndefined()
            && (!rawParameters.isObject() || !rawParameters.toObject().isEmpty())) {
            return reject(400, action,
                          QStringLiteral("NPU YOLO 操作仅使用迅为开发板测试平台内置的固定示例，不接受外部参数"),
                          npuYoloExtra());
        }
        if (!m_npuYoloDemo) {
            return reject(409, action,
                          QStringLiteral("当前迅为开发板测试平台进程未提供 NPU YOLO 服务"),
                          npuYoloExtra());
        }

        if (action == QStringLiteral("npu-yolo-discover")) {
            m_npuYoloDemo->discover();
            return makeResponse(true, action, QStringLiteral("NPU YOLO status refreshed"), npuYoloExtra());
        }
        if (action == QStringLiteral("npu-yolo-start")) {
            if (m_npuYoloDemo->start()) {
                return makeResponse(true, action, QStringLiteral("NPU YOLO sample started"), npuYoloExtra());
            }
            const QJsonObject status = npuYoloStatusObject();
            QString reason = status.value(QStringLiteral("error")).toString();
            if (reason.isEmpty()) {
                reason = status.value(QStringLiteral("message")).toString();
            }
            return reject(409, action,
                          reason.isEmpty() ? QStringLiteral("NPU YOLO sample was not ready") : reason,
                          npuYoloExtra());
        }
        if (action == QStringLiteral("npu-yolo-stop")) {
            if (m_npuYoloDemo->stop()) {
                return makeResponse(true, action, QStringLiteral("NPU YOLO stop requested"), npuYoloExtra());
            }
            const QJsonObject status = npuYoloStatusObject();
            const QString reason = status.value(QStringLiteral("message")).toString();
            return reject(409, action,
                          reason.isEmpty() ? QStringLiteral("NPU YOLO sample is not running") : reason,
                          npuYoloExtra());
        }
        return reject(400, action, QStringLiteral("Unsupported NPU YOLO control action"), npuYoloExtra());
    }

    if (action == QStringLiteral("refresh")) {
        m_systemInfo->refresh();
        m_boardBackend->refresh();
        return makeResponse(true, action, QStringLiteral("Refresh submitted"));
    }

    if (action == QStringLiteral("environment-refresh")) {
        if (!m_boardBackend->operationAllowed(action)) {
            return reject(409, action, m_boardBackend->operationUnavailableReason(action), QJsonObject());
        }
        if (!m_boardBackend->refreshEnvironmentAsync()) {
            QJsonObject extra;
            extra.insert(QStringLiteral("environmentStatus"),
                         QJsonObject::fromVariantMap(m_boardBackend->environmentStatus()));
            return reject(409, action, QStringLiteral("Environment sampling is already in progress"), extra);
        }
        QJsonObject extra;
        extra.insert(QStringLiteral("environmentStatus"),
                     QJsonObject::fromVariantMap(m_boardBackend->environmentStatus()));
        return makeResponse(true, action, QStringLiteral("Environment sample submitted"), extra);
    }

    if (action == QStringLiteral("fan-auto") || action == QStringLiteral("fan-manual")) {
        if (!m_boardBackend->operationAllowed(action)) {
            QJsonObject extra;
            extra.insert(QStringLiteral("fanStatus"),
                         QJsonObject::fromVariantMap(m_boardBackend->fanStatus()));
            return reject(409, action, m_boardBackend->operationUnavailableReason(action), extra);
        }
        const QJsonValue rawParameters = request.value(QStringLiteral("params"));
        if (!rawParameters.isUndefined() && !rawParameters.isObject()) {
            return reject(400, action, QStringLiteral("风扇操作参数必须为对象"), QJsonObject());
        }
        if (action == QStringLiteral("fan-auto") && !rawParameters.toObject().isEmpty()) {
            return reject(400, action, QStringLiteral("fan-auto 不接受参数"), QJsonObject());
        }
        int percent = 0;
        if (action == QStringLiteral("fan-manual")) {
            const QJsonObject paramsObject = rawParameters.toObject();
            if (paramsObject.size() != 1 || !paramsObject.contains(QStringLiteral("percent"))
                || !paramsObject.value(QStringLiteral("percent")).isDouble()) {
                return reject(400, action, QStringLiteral("fan-manual 仅接受整数 percent 参数"), QJsonObject());
            }
            const double requestedPercent = paramsObject.value(QStringLiteral("percent")).toDouble();
            percent = paramsObject.value(QStringLiteral("percent")).toInt(-1);
            if (requestedPercent != percent || percent < 0 || percent > 100) {
                return reject(400, action, QStringLiteral("风扇转速百分比必须在 0-100 之间"), QJsonObject());
            }
        }
        const bool accepted = action == QStringLiteral("fan-auto")
            ? m_boardBackend->setFanAuto()
            : m_boardBackend->setFanManualPercent(percent);
        const QVariantMap fan = m_boardBackend->fanStatus();
        QJsonObject extra;
        extra.insert(QStringLiteral("fanStatus"), QJsonObject::fromVariantMap(fan));
        if (!accepted) {
            return reject(409, action, m_boardBackend->statusText(), extra);
        }
        return makeResponse(true, action, m_boardBackend->statusText(), extra);
    }

    if (action == QStringLiteral("keys-start")
        || action == QStringLiteral("keys-stop")
        || action == QStringLiteral("keys-clear")) {
        const auto keyStatusExtra = [this]() {
            QJsonObject extra;
            extra.insert(QStringLiteral("keyTestStatus"),
                         QJsonObject::fromVariantMap(m_boardBackend->keyTestStatus()));
            return extra;
        };

        // Stopping is deliberately allowed even after a profile change.  It is
        // a read-only monitor teardown and must never leave a listener active.
        if (action != QStringLiteral("keys-stop") && !m_boardBackend->operationAllowed(action)) {
            return reject(409, action, m_boardBackend->operationUnavailableReason(action), keyStatusExtra());
        }

        if (action == QStringLiteral("keys-start")) {
            const QJsonValue rawParameters = request.value(QStringLiteral("params"));
            if (!rawParameters.isObject()) {
                return reject(400, action, QStringLiteral("keys-start requires params.deviceId"), keyStatusExtra());
            }

            const QJsonObject keyParameters = rawParameters.toObject();
            if (keyParameters.contains(QStringLiteral("event"))
                || keyParameters.contains(QStringLiteral("eventPath"))
                || request.contains(QStringLiteral("event"))
                || request.contains(QStringLiteral("eventPath"))) {
                return reject(400, action,
                              QStringLiteral("keys-start accepts a stable params.deviceId; event and eventPath are not accepted"),
                              keyStatusExtra());
            }
            if (keyParameters.size() != 1 || !keyParameters.contains(QStringLiteral("deviceId"))) {
                return reject(400, action, QStringLiteral("keys-start only accepts params.deviceId"), keyStatusExtra());
            }

            const QJsonValue rawDeviceId = keyParameters.value(QStringLiteral("deviceId"));
            const QString deviceId = rawDeviceId.toString().trimmed();
            if (!rawDeviceId.isString() || deviceId.isEmpty()) {
                return reject(400, action, QStringLiteral("keys-start requires a non-empty params.deviceId"), keyStatusExtra());
            }
            if (deviceId.startsWith(QStringLiteral("/dev/input/"))) {
                return reject(400, action,
                              QStringLiteral("keys-start requires a stable device-tree deviceId, not a runtime input path"),
                              keyStatusExtra());
            }

            if (!m_boardBackend->startKeyMonitoring(deviceId)) {
                const QVariantMap status = m_boardBackend->keyTestStatus();
                QString message = status.value(QStringLiteral("lastError")).toString().trimmed();
                if (message.isEmpty()) {
                    message = status.value(QStringLiteral("status")).toString().trimmed();
                }
                if (message.isEmpty()) {
                    message = QStringLiteral("Unable to start board key monitoring");
                }
                QJsonObject extra;
                extra.insert(QStringLiteral("keyTestStatus"), QJsonObject::fromVariantMap(status));
                return reject(409, action, message, extra);
            }
            return makeResponse(true, action, QStringLiteral("Key monitoring started"), keyStatusExtra());
        }

        if (action == QStringLiteral("keys-stop")) {
            m_boardBackend->stopKeyMonitoring();
            return makeResponse(true, action, QStringLiteral("Key monitoring stopped"), keyStatusExtra());
        }

        m_boardBackend->clearKeyTestEvents();
        return makeResponse(true, action, QStringLiteral("Key test events cleared"), keyStatusExtra());
    }

    if (action == QStringLiteral("gpu-start") || action == QStringLiteral("gpu-stop")
        || action == QStringLiteral("gpu-preview-start") || action == QStringLiteral("gpu-preview-stop")) {
        const auto gpuStatusExtra = [this]() {
            QJsonObject extra;
            extra.insert(QStringLiteral("gpuBenchmarkStatus"),
                         QJsonObject::fromVariantMap(m_boardBackend->gpuBenchmarkStatus()));
            extra.insert(QStringLiteral("backendBusy"), m_boardBackend->backendBusy());
            extra.insert(QStringLiteral("backendBusyOperation"), m_boardBackend->backendBusyOperation());
            return extra;
        };
        if (!parameters.isEmpty()) {
            return reject(400, action,
                          QStringLiteral("%1 does not accept parameters").arg(action),
                          gpuStatusExtra());
        }
        const bool isStartAction = action == QStringLiteral("gpu-start")
            || action == QStringLiteral("gpu-preview-start");
        if (isStartAction && !m_boardBackend->operationAllowed(action)) {
            return reject(409, action, m_boardBackend->operationUnavailableReason(action), gpuStatusExtra());
        }

        bool accepted = false;
        if (action == QStringLiteral("gpu-start")) {
            accepted = m_boardBackend->startGpuBenchmark();
        } else if (action == QStringLiteral("gpu-stop")) {
            accepted = m_boardBackend->stopGpuBenchmark();
        } else if (action == QStringLiteral("gpu-preview-start")) {
            accepted = m_boardBackend->startGpuPreview();
        } else {
            accepted = m_boardBackend->stopGpuPreview();
        }
        if (!accepted) {
            const QVariantMap gpu = m_boardBackend->gpuBenchmarkStatus();
            const bool isPreviewAction = action.startsWith(QStringLiteral("gpu-preview-"));
            QString message = gpu.value(isPreviewAction
                                        ? QStringLiteral("previewError")
                                        : QStringLiteral("error")).toString().trimmed();
            if (message.isEmpty()) {
                message = m_boardBackend->statusText().trimmed();
            }
            if (message.isEmpty()) {
                message = action == QStringLiteral("gpu-start")
                    ? QStringLiteral("GPU benchmark was not accepted")
                    : action == QStringLiteral("gpu-stop")
                        ? QStringLiteral("GPU benchmark is not running")
                        : action == QStringLiteral("gpu-preview-start")
                            ? QStringLiteral("Embedded GPU preview was not accepted")
                            : QStringLiteral("Embedded GPU preview is not running");
            }
            return reject(409, action, message, gpuStatusExtra());
        }
        return makeResponse(true, action,
                            action == QStringLiteral("gpu-start")
                                ? QStringLiteral("GPU benchmark started")
                                : action == QStringLiteral("gpu-stop")
                                    ? QStringLiteral("GPU benchmark stop requested")
                                    : action == QStringLiteral("gpu-preview-start")
                                        ? QStringLiteral("Embedded GPU preview started in the Qt GPU test page")
                                        : QStringLiteral("Embedded GPU preview stop requested"),
                            gpuStatusExtra());
    }

    if (action == QStringLiteral("camera-preview-start") || action == QStringLiteral("camera-preview-stop")) {
        const auto cameraPreviewStatusExtra = [this]() {
            QJsonObject extra;
            extra.insert(QStringLiteral("cameraPreview"),
                         QJsonObject::fromVariantMap(m_boardBackend->webCameraPreviewStatus()));
            return extra;
        };
        const bool startAction = action == QStringLiteral("camera-preview-start");
        if (startAction && !m_boardBackend->operationAllowed(action)) {
            return reject(409, action, m_boardBackend->operationUnavailableReason(action),
                          cameraPreviewStatusExtra());
        }

        const QJsonValue rawParameters = request.value(QStringLiteral("params"));
        if (!rawParameters.isObject()) {
            return reject(400, action,
                          startAction
                              ? QStringLiteral("camera-preview-start accepts an optional string params.device")
                              : QStringLiteral("camera-preview-stop does not accept parameters"),
                          cameraPreviewStatusExtra());
        }
        const QJsonObject cameraParameters = rawParameters.toObject();
        if (startAction) {
            if (cameraParameters.size() > 1
                || (cameraParameters.size() == 1 && !cameraParameters.contains(QStringLiteral("device")))
                || (cameraParameters.contains(QStringLiteral("device"))
                    && !cameraParameters.value(QStringLiteral("device")).isString())) {
                return reject(400, action,
                              QStringLiteral("camera-preview-start accepts only an optional string params.device"),
                              cameraPreviewStatusExtra());
            }
        } else if (!cameraParameters.isEmpty()) {
            return reject(400, action,
                          QStringLiteral("camera-preview-stop does not accept parameters"),
                          cameraPreviewStatusExtra());
        }

        const bool accepted = startAction
            ? m_boardBackend->startWebCameraPreview(
                  cameraParameters.value(QStringLiteral("device")).toString())
            : m_boardBackend->stopWebCameraPreview();
        if (!accepted) {
            const QVariantMap preview = m_boardBackend->webCameraPreviewStatus();
            QString message = preview.value(QStringLiteral("error")).toString().trimmed();
            if (message.isEmpty()) {
                message = preview.value(QStringLiteral("message")).toString().trimmed();
            }
            if (message.isEmpty()) {
                message = m_boardBackend->statusText().trimmed();
            }
            return reject(409, action,
                          message.isEmpty()
                              ? (startAction
                                  ? QStringLiteral("Web 摄像头实时预览未能启动")
                                  : QStringLiteral("Web 摄像头实时预览未在运行"))
                              : message,
                          cameraPreviewStatusExtra());
        }
        return makeResponse(true, action,
                            startAction
                                ? QStringLiteral("Web 摄像头实时预览启动请求已提交")
                                : QStringLiteral("Web 摄像头实时预览停止请求已提交"),
                            cameraPreviewStatusExtra());
    }

    if (action == QStringLiteral("wifi-refresh")
        || action == QStringLiteral("wifi-scan")
        || action == QStringLiteral("wifi-connect")
        || action == QStringLiteral("wifi-disconnect")) {
        const auto wifiStatusExtra = [this]() {
            QJsonObject extra;
            extra.insert(QStringLiteral("wifiManagerStatus"),
                         QJsonObject::fromVariantMap(m_boardBackend->wifiManagerStatus()));
            extra.insert(QStringLiteral("wifiAccessPoints"),
                         QJsonArray::fromVariantList(m_boardBackend->wifiAccessPoints()));
            extra.insert(QStringLiteral("backendBusy"), m_boardBackend->backendBusy());
            extra.insert(QStringLiteral("backendBusyOperation"), m_boardBackend->backendBusyOperation());
            return extra;
        };
        if (!m_boardBackend->operationAllowed(action)) {
            return reject(409, action, m_boardBackend->operationUnavailableReason(action), wifiStatusExtra());
        }

        const QJsonValue rawParameters = request.value(QStringLiteral("params"));
        if (!rawParameters.isObject()) {
            return reject(400, action, QStringLiteral("Wi-Fi actions require an object params value"), wifiStatusExtra());
        }
        const QJsonObject wifiParameters = rawParameters.toObject();
        const auto onlyAllows = [&wifiParameters](const QStringList &allowed) {
            for (auto it = wifiParameters.constBegin(); it != wifiParameters.constEnd(); ++it) {
                if (!allowed.contains(it.key())) {
                    return false;
                }
            }
            return true;
        };

        bool accepted = false;
        if (action == QStringLiteral("wifi-refresh")) {
            if (!wifiParameters.isEmpty()) {
                return reject(400, action, QStringLiteral("wifi-refresh does not accept parameters"), wifiStatusExtra());
            }
            accepted = m_boardBackend->refreshWifiAsync();
        } else if (action == QStringLiteral("wifi-scan")) {
            if (!onlyAllows(QStringList() << QStringLiteral("interface"))) {
                return reject(400, action, QStringLiteral("wifi-scan only accepts an optional params.interface"), wifiStatusExtra());
            }
            const QJsonValue interfaceValue = wifiParameters.value(QStringLiteral("interface"));
            if (!interfaceValue.isUndefined() && !interfaceValue.isString()) {
                return reject(400, action, QStringLiteral("wifi-scan params.interface must be a string"), wifiStatusExtra());
            }
            accepted = m_boardBackend->scanWifiAsync(interfaceValue.toString());
        } else if (action == QStringLiteral("wifi-connect")) {
            if (!onlyAllows(QStringList() << QStringLiteral("interface") << QStringLiteral("networkId") << QStringLiteral("password"))
                || !wifiParameters.value(QStringLiteral("interface")).isString()
                || !wifiParameters.value(QStringLiteral("networkId")).isString()
                || (!wifiParameters.value(QStringLiteral("password")).isUndefined()
                    && !wifiParameters.value(QStringLiteral("password")).isString())) {
                return reject(400, action,
                              QStringLiteral("wifi-connect accepts string params.interface, params.networkId and optional params.password"),
                              wifiStatusExtra());
            }
            accepted = m_boardBackend->connectWifiAsync(
                wifiParameters.value(QStringLiteral("interface")).toString(),
                wifiParameters.value(QStringLiteral("networkId")).toString(),
                wifiParameters.value(QStringLiteral("password")).toString());
        } else {
            if (!onlyAllows(QStringList() << QStringLiteral("interface"))
                || !wifiParameters.value(QStringLiteral("interface")).isString()) {
                return reject(400, action, QStringLiteral("wifi-disconnect requires string params.interface"), wifiStatusExtra());
            }
            accepted = m_boardBackend->disconnectWifiAsync(
                wifiParameters.value(QStringLiteral("interface")).toString());
        }

        if (!accepted) {
            QString message = m_boardBackend->wifiManagerStatus().value(QStringLiteral("message")).toString().trimmed();
            if (message.isEmpty()) {
                message = m_boardBackend->statusText().trimmed();
            }
            if (message.isEmpty()) {
                message = QStringLiteral("Wi-Fi operation was not accepted");
            }
            return reject(409, action, message, wifiStatusExtra());
        }
        return makeResponse(true, action, QStringLiteral("Wi-Fi operation submitted"), wifiStatusExtra());
    }

    // Reject client-selected modem executables, identities, and PIDs before
    // the profile gate. The HTTP contract must stay fail-closed even when a
    // profile has disabled the hardware action on the current board.
    if (action == QStringLiteral("cellular-start")
        || action == QStringLiteral("cellular-status")
        || action == QStringLiteral("cellular-stop")) {
        const QJsonValue rawParameters = request.value(QStringLiteral("params"));
        if (!rawParameters.isUndefined() && !rawParameters.isObject()) {
            return reject(400, action, QStringLiteral("蜂窝操作参数必须为对象"), QJsonObject());
        }
        const QJsonObject cellularParameters = rawParameters.toObject();
        const QStringList allowed = action == QStringLiteral("cellular-start")
            ? (QStringList() << QStringLiteral("apn") << QStringLiteral("interface"))
            : (action == QStringLiteral("cellular-stop")
               ? (QStringList() << QStringLiteral("interface"))
               : QStringList());
        for (auto it = cellularParameters.constBegin(); it != cellularParameters.constEnd(); ++it) {
            if (!allowed.contains(it.key()) || !it.value().isString()) {
                return reject(400, action,
                              action == QStringLiteral("cellular-start")
                                  ? QStringLiteral("cellular-start 仅接受可选字符串参数 apn 和 interface")
                                  : (action == QStringLiteral("cellular-stop")
                                     ? QStringLiteral("cellular-stop 仅接受可选字符串参数 interface")
                                     : QStringLiteral("cellular-status 不接受参数")),
                              QJsonObject());
            }
        }
    }

    if (!m_boardBackend->operationAllowed(action)) {
        return reject(409, action, m_boardBackend->operationUnavailableReason(action), QJsonObject());
    }

    if (action == QStringLiteral("ping-interface")) {
        const QString interfaceName = parameters.value(QStringLiteral("interface")).toString().trimmed();
        const QString target = parameters.value(QStringLiteral("target"), QStringLiteral("www.baidu.com")).toString().trimmed();
        if (interfaceName.isEmpty() || interfaceName.startsWith(QStringLiteral("No "))) {
            return reject(400, action, QStringLiteral("No interface selected"), QJsonObject());
        }
        if (!m_boardBackend->pingInterfaceAsync(interfaceName, target.isEmpty() ? QStringLiteral("www.baidu.com") : target)) {
            QJsonObject extra;
            extra.insert(QStringLiteral("backendBusy"), m_boardBackend->backendBusy());
            extra.insert(QStringLiteral("backendBusyOperation"), m_boardBackend->backendBusyOperation());
            return reject(409, action,
                          QStringLiteral("Backend busy: %1").arg(m_boardBackend->backendBusyOperation()),
                          extra);
        }
        return makeResponse(true, action, QStringLiteral("Ping submitted"));
    }

    if (action == QStringLiteral("files-open")) {
        m_boardBackend->openPathAsync(parameters.value(QStringLiteral("path")).toString());
        return makeResponse(true, action, QStringLiteral("File browser open submitted"));
    }

    if (action == QStringLiteral("files-up")) {
        m_boardBackend->goUpAsync();
        return makeResponse(true, action, QStringLiteral("File browser up submitted"));
    }

    if (action == QStringLiteral("storage-scan")
        || action == QStringLiteral("storage-mount")
        || action == QStringLiteral("storage-unmount")
        || action == QStringLiteral("storage-speed")) {
        if (!m_boardProfile || !m_boardProfile->hasCapability(QStringLiteral("storage"))) {
            return reject(409, action, QStringLiteral("Storage testing is not enabled for this board profile"), QJsonObject());
        }

        if (action == QStringLiteral("storage-speed")
            && !parameters.value(QStringLiteral("confirmWrite")).toBool()) {
            return reject(400, action,
                          QStringLiteral("storage-speed requires confirmWrite=true because it writes a temporary test file"),
                          QJsonObject());
        }

        bool accepted = false;
        if (action == QStringLiteral("storage-scan")) {
            accepted = m_boardBackend->scanStorageDevicesAsync();
        } else if (action == QStringLiteral("storage-mount")) {
            accepted = m_boardBackend->mountStorageDeviceAsync(
                parameters.value(QStringLiteral("device")).toString(),
                parameters.value(QStringLiteral("mountPoint")).toString());
        } else if (action == QStringLiteral("storage-unmount")) {
            accepted = m_boardBackend->unmountStorageDeviceAsync(
                parameters.value(QStringLiteral("mountPoint")).toString());
        } else {
            accepted = m_boardBackend->testStorageSpeed(
                parameters.value(QStringLiteral("mountPoint")).toString());
        }

        if (!accepted) {
            QJsonObject extra;
            extra.insert(QStringLiteral("backendBusy"), m_boardBackend->backendBusy());
            extra.insert(QStringLiteral("backendBusyOperation"), m_boardBackend->backendBusyOperation());
            return reject(409, action,
                          QStringLiteral("Backend busy: %1").arg(m_boardBackend->backendBusyOperation()),
                          extra);
        }

        QJsonObject extra;
        extra.insert(QStringLiteral("backendBusy"), m_boardBackend->backendBusy());
        extra.insert(QStringLiteral("backendBusyOperation"), m_boardBackend->backendBusyOperation());
        return makeResponse(true, action, QStringLiteral("Storage operation submitted"), extra);
    }

    if (action.startsWith(QStringLiteral("audio-"))) {
        const QString audioOperation = action.mid(6);
        const bool buzzerOperation = audioOperation == QStringLiteral("beep-apply")
            || audioOperation == QStringLiteral("beep-heartbeat")
            || audioOperation == QStringLiteral("beep-stop");
        if (buzzerOperation && !m_boardBackend->buzzerAvailable()) {
            return reject(409, action, m_boardBackend->buzzerAvailabilityStatus(), QJsonObject());
        }
        int value = -1;
        if (audioOperation == QStringLiteral("volume") || audioOperation == QStringLiteral("beep-apply")) {
            value = qBound(0, parameters.value(QStringLiteral("percent"), m_boardBackend->audioVolumePercent()).toInt(), 100);
        }
        const auto isDiscoveredAudioDevice = [](const QString &device, const QVariantList &devices) {
            if (device.trimmed().isEmpty()) {
                return true;
            }
            for (const QVariant &item : devices) {
                if (item.toMap().value(QStringLiteral("device")).toString() == device.trimmed()) {
                    return true;
                }
            }
            return false;
        };
        const QString playbackDevice = parameters.value(QStringLiteral("playbackDevice")).toString();
        const QString captureDevice = parameters.value(QStringLiteral("captureDevice")).toString();
        if (!isDiscoveredAudioDevice(playbackDevice, m_boardBackend->audioPlaybackDevices())
            || !isDiscoveredAudioDevice(captureDevice, m_boardBackend->audioCaptureDevices())) {
            return reject(400, action, QStringLiteral("Audio device is not in the current discovery list"), QJsonObject());
        }
        if (!m_boardBackend->runAudioOperationAsync(audioOperation, value,
                                                    playbackDevice, captureDevice)) {
            QJsonObject extra;
            extra.insert(QStringLiteral("backendBusy"), m_boardBackend->backendBusy());
            extra.insert(QStringLiteral("backendBusyOperation"), m_boardBackend->backendBusyOperation());
            extra.insert(QStringLiteral("audioOperationBusy"), m_boardBackend->audioOperationBusy());
            const QString status = m_boardBackend->statusText().trimmed();
            return reject(409, action,
                          status.isEmpty() ? QStringLiteral("Audio operation was not accepted") : status,
                          extra);
        }
        QJsonObject extra;
        extra.insert(QStringLiteral("backendBusy"), m_boardBackend->backendBusy());
        extra.insert(QStringLiteral("backendBusyOperation"), m_boardBackend->backendBusyOperation());
        extra.insert(QStringLiteral("audioOperationBusy"), m_boardBackend->audioOperationBusy());
        return makeResponse(true, action, QStringLiteral("Audio operation submitted"), extra);
    }

    static const QStringList hardwareActions = {
        QStringLiteral("gpio-apply"),
        QStringLiteral("gpio-read"),
        QStringLiteral("led-set"),
        QStringLiteral("led-all-on"),
        QStringLiteral("led-all-off"),
        QStringLiteral("led-blink"),
        QStringLiteral("backlight-set"),
        QStringLiteral("ethernet-dhcp"),
        QStringLiteral("ethernet-static"),
        QStringLiteral("ethernet-refresh"),
        QStringLiteral("uart-tx"),
        QStringLiteral("uart-rx"),
        QStringLiteral("can-open"),
        QStringLiteral("can-close"),
        QStringLiteral("can-tx"),
        QStringLiteral("can-rx"),
        QStringLiteral("can-loop-start"),
        QStringLiteral("can-loop-stop"),
        QStringLiteral("cellular-start"),
        QStringLiteral("cellular-status"),
        QStringLiteral("cellular-stop"),
        QStringLiteral("camera-start"),
        QStringLiteral("camera-stop"),
        QStringLiteral("hdmiin-start"),
        QStringLiteral("hdmiin-stop"),
        QStringLiteral("watchdog-status"),
        QStringLiteral("watchdog-start")
    };

    if (!hardwareActions.contains(action)) {
        return reject(400, action, QStringLiteral("Unsupported control action"), QJsonObject());
    }

    if (action == QStringLiteral("watchdog-start") && !parameters.value(QStringLiteral("confirm")).toBool()) {
        return reject(400, action, QStringLiteral("watchdog-start requires confirm=true"), QJsonObject());
    }
    const bool canBusControl = action == QStringLiteral("can-open")
        || action == QStringLiteral("can-close")
        || action == QStringLiteral("can-tx")
        || action == QStringLiteral("can-loop-start");
    if (canBusControl && !parameters.value(QStringLiteral("confirmCan")).toBool()) {
        return reject(400, action,
                      QStringLiteral("%1 requires confirmCan=true because it controls a real CAN bus").arg(action),
                      QJsonObject());
    }
    if (action == QStringLiteral("can-open") && parameters.value(QStringLiteral("fdMode")).toBool()) {
        return reject(400, action,
                      QStringLiteral("当前平台参考帧测试不支持 CAN FD；请使用经典 CAN"),
                      QJsonObject());
    }

    QVariantMap workerParameters = parameters;
    if (action == QStringLiteral("camera-start") && !workerParameters.contains(QStringLiteral("mode"))) {
        const QString device = workerParameters.value(QStringLiteral("device")).toString().trimmed();
        QString mode;
        const QVariantList devices = m_boardBackend->cameraCaptureDevices();
        for (const QVariant &item : devices) {
            const QVariantMap row = item.toMap();
            if (row.value(QStringLiteral("path")).toString() == device) {
                mode = row.value(QStringLiteral("mode"), mode.isEmpty() ? QStringLiteral("mipi") : mode).toString();
                break;
            }
        }
        workerParameters.insert(QStringLiteral("mode"), mode.isEmpty() ? QStringLiteral("mipi") : mode);
    }

    if (!m_boardBackend->runHardwareOperationAsync(action, workerParameters)) {
        QJsonObject extra;
        const bool busy = m_boardBackend->backendBusy();
        const QString busyOperation = m_boardBackend->backendBusyOperation();
        extra.insert(QStringLiteral("backendBusy"), busy);
        extra.insert(QStringLiteral("backendBusyOperation"), busyOperation);
        const QString status = m_boardBackend->statusText().trimmed();
        const QString message = busy
            ? QStringLiteral("Backend busy: %1").arg(busyOperation)
            : (status.isEmpty() ? QStringLiteral("Hardware operation was not accepted") : status);
        return reject(409, action, message, extra);
    }
    QJsonObject extra;
    extra.insert(QStringLiteral("backendBusy"), m_boardBackend->backendBusy());
    extra.insert(QStringLiteral("backendBusyOperation"), m_boardBackend->backendBusyOperation());
    return makeResponse(true, action, QStringLiteral("Hardware operation submitted"), extra);
}

QByteArray LocalWebServer::uploadFileJson(const QUrl &requestUrl, const QByteArray &body, int *statusCode)
{
    if (statusCode) {
        *statusCode = 201;
    }
    const auto reject = [statusCode](int code, const QString &error, const QString &message) {
        if (statusCode) {
            *statusCode = code;
        }
        QJsonObject response;
        response.insert(QStringLiteral("ok"), false);
        response.insert(QStringLiteral("error"), error);
        response.insert(QStringLiteral("message"), message);
        return QJsonDocument(response).toJson(QJsonDocument::Indented);
    };

    if (body.size() > kMaximumUploadBytes) {
        return reject(413, QStringLiteral("file_too_large"),
                      QStringLiteral("Upload exceeds the 64 MiB limit"));
    }

    const QUrlQuery query(requestUrl);
    QString fileName;
    QString requestedDirectory;
    int fileNameCount = 0;
    int directoryCount = 0;
    for (const auto &item : query.queryItems(QUrl::FullyDecoded)) {
        if (item.first == QStringLiteral("filename")) {
            ++fileNameCount;
            fileName = item.second;
        } else if (item.first == QStringLiteral("directory")) {
            ++directoryCount;
            requestedDirectory = item.second;
        } else {
            return reject(400, QStringLiteral("invalid_upload_request"),
                          QStringLiteral("Only filename and optional directory query parameters are accepted"));
        }
    }
    if (fileNameCount != 1 || directoryCount > 1 || !isSafeUploadFileName(fileName)) {
        return reject(400, QStringLiteral("invalid_filename"),
                      QStringLiteral("filename must be one non-hidden basename without path separators"));
    }

    const QString uploadRoot = resolvedUploadRoot();
    if (uploadRoot.isEmpty()) {
        return reject(500, QStringLiteral("upload_root_unavailable"),
                      QStringLiteral("The fixed upload root is unavailable; set BOARDLAB_UPLOAD_ROOT or create /var/lib/boardlab/uploads"));
    }

    if (directoryCount == 0 && m_boardBackend) {
        requestedDirectory = m_boardBackend->currentPath();
    }
    if (requestedDirectory.trimmed().isEmpty()) {
        requestedDirectory = uploadRoot;
    }
    QFileInfo destinationDirectory(requestedDirectory);
    if (destinationDirectory.isRelative()
        || !destinationDirectory.exists() || !destinationDirectory.isDir()) {
        return reject(400, QStringLiteral("invalid_directory"),
                      QStringLiteral("directory must be an existing absolute directory under the upload root (%1)")
                          .arg(uploadRoot));
    }

    const QString absoluteDirectory = QDir::cleanPath(destinationDirectory.absoluteFilePath());
    const QString canonicalDirectory = destinationDirectory.canonicalFilePath();
    // Requiring the supplied directory to already be canonical rejects both a
    // final symlink and symlinked parents, so the temporary file and rename
    // remain in the validated directory tree.
    if (!isPathWithinRoot(canonicalDirectory, uploadRoot) || absoluteDirectory != canonicalDirectory) {
        return reject(403, QStringLiteral("directory_not_allowed"),
                      QStringLiteral("directory is outside the fixed upload root (%1)")
                          .arg(uploadRoot));
    }

    const QString targetPath = QDir(canonicalDirectory).absoluteFilePath(fileName);
    QFileInfo targetInfo(targetPath);
    // QFileInfo::exists() is false for a dangling symlink, so check both
    // conditions before and after the rename. Uploads are create-only.
    if (targetInfo.exists() || targetInfo.isSymLink()) {
        return reject(409, QStringLiteral("file_exists"),
                      QStringLiteral("A file with this filename already exists; uploads never overwrite files"));
    }

    QTemporaryFile temporary(QDir(canonicalDirectory).absoluteFilePath(
        QStringLiteral(".boardlab-upload-XXXXXX")));
    temporary.setAutoRemove(true);
    if (!temporary.open()) {
        return reject(500, QStringLiteral("temporary_file_failed"),
                      QStringLiteral("Could not create the upload temporary file"));
    }

    qint64 written = 0;
    while (written < body.size()) {
        const qint64 result = temporary.write(body.constData() + written, body.size() - written);
        if (result <= 0) {
            return reject(500, QStringLiteral("upload_write_failed"),
                          QStringLiteral("Could not write the upload temporary file"));
        }
        written += result;
    }
    if (!temporary.flush()) {
        return reject(500, QStringLiteral("upload_flush_failed"),
                      QStringLiteral("Could not flush the upload temporary file"));
    }

    const QString temporaryPath = temporary.fileName();
    temporary.close();
    if (!QFile::rename(temporaryPath, targetPath)) {
        targetInfo.refresh();
        if (targetInfo.exists() || targetInfo.isSymLink()) {
            return reject(409, QStringLiteral("file_exists"),
                          QStringLiteral("A file with this filename already exists; uploads never overwrite files"));
        }
        return reject(500, QStringLiteral("upload_commit_failed"),
                      QStringLiteral("Could not atomically publish the uploaded file"));
    }

    QJsonObject response;
    response.insert(QStringLiteral("ok"), true);
    response.insert(QStringLiteral("created"), true);
    response.insert(QStringLiteral("filename"), fileName);
    response.insert(QStringLiteral("bytes"), static_cast<double>(written));
    response.insert(QStringLiteral("overwritten"), false);
    return QJsonDocument(response).toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::htmlDashboard() const
{
    m_systemInfo->refresh();
    QFile resource(QStringLiteral(":/web/index.html"));
    if (!resource.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QByteArray();
    }

    QString html = QString::fromUtf8(resource.readAll());
    html.replace(QStringLiteral("{{WEB_STATUS}}"), m_statusText.toHtmlEscaped());
    html.replace(QStringLiteral("{{WEB_URL}}"), url().toHtmlEscaped());
    html.replace(QStringLiteral("{{KERNEL_TEXT}}"), m_systemInfo->kernelText().toHtmlEscaped());
    html.replace(QStringLiteral("{{PRODUCT_NAME}}"), QSysInfo::prettyProductName().toHtmlEscaped());
    return html.toUtf8();
}

QByteArray LocalWebServer::legacyHtmlDashboard() const
{
    m_systemInfo->refresh();

    QString html = QString::fromUtf8(R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="/favicon.ico" type="image/svg+xml">
<title>迅为开发板测试平台 · 局域网控制台</title>
<style>
:root{color-scheme:dark;--bg:#10151c;--bg2:#18212c;--panel:#f8fbff;--ink:#17202a;--muted:#64748b;--line:#d9e2ec;--white:#f8fafc;--soft:#eef3f7;--accent:#0a84ff;--green:#18a058;--orange:#e59f20;--red:#d84c4c;--pink:#d84c7f;--violet:#5e5ce6}
*{box-sizing:border-box}html{scroll-behavior:smooth}body{margin:0;background:linear-gradient(180deg,#111821 0%,#1b2430 48%,#26303d 100%);color:var(--white);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif}button,input,select{font:inherit}button{cursor:pointer}main{width:min(1420px,100%);margin:0 auto;padding:22px}.topbar{height:58px;display:flex;align-items:center;justify-content:space-between;gap:14px}.brand{display:flex;align-items:center;gap:12px;min-width:0}.mark{width:42px;height:42px;border-radius:8px;background:#f8fafc;color:#111821;display:grid;place-items:center;font-weight:900;letter-spacing:0}.title{margin:0;font-size:22px;font-weight:900;line-height:1.08}.sub{margin:4px 0 0;color:#b8c2ce;font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.status-chip{border:1px solid rgba(248,250,252,.18);border-radius:8px;background:rgba(248,250,252,.08);padding:9px 12px;color:#dce5ef;font-size:12px;font-weight:800}.hero{display:grid;grid-template-columns:minmax(0,1.3fr) minmax(300px,.7fr);gap:14px;margin:18px 0}.hero-panel{border:1px solid rgba(248,250,252,.12);border-radius:8px;background:rgba(12,18,26,.76);padding:18px;box-shadow:0 18px 50px rgba(0,0,0,.22)}.hero-title{font-size:30px;font-weight:900;margin:0 0 9px}.hero-copy{margin:0;color:#b8c2ce;font-size:13px;line-height:1.55}.url{margin-top:10px;font-size:22px;font-weight:900;word-break:break-all;color:#ffffff}.api-row{display:flex;flex-wrap:wrap;gap:7px;margin-top:14px}.pill{display:inline-flex;align-items:center;border-radius:7px;padding:5px 8px;background:rgba(10,132,255,.14);color:#d9ecff;border:1px solid rgba(10,132,255,.22);font-size:11px;font-weight:900}.toolbar{display:flex;align-items:center;gap:8px;flex-wrap:wrap;margin-top:14px}.telemetry-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:10px}.telemetry-card{border:1px solid rgba(248,250,252,.12);border-radius:8px;background:rgba(248,250,252,.92);color:var(--ink);padding:14px;min-height:96px}.telemetry-card h2{font-size:13px;margin:0;color:#516173}.telemetry-head{display:flex;align-items:flex-start;justify-content:space-between;gap:10px}.metric{font-size:30px;font-weight:900;line-height:1}.muted{color:var(--muted);font-size:12px;line-height:1.45}.trend-card{min-height:156px;display:grid;grid-template-rows:auto 1fr auto;gap:9px}.trend-value{text-align:right}.trend-label{display:block;color:var(--muted);font-size:11px;font-weight:800;margin-top:3px}.trend-chart{height:72px;border:1px solid #e1e8f0;border-radius:8px;background:linear-gradient(180deg,#ffffff 0%,#f3f7fb 100%);overflow:hidden}.trend-chart svg{display:block;width:100%;height:100%}.trend-grid{stroke:#dfe7ef;stroke-width:1}.trend-fill{opacity:.18}.trend-line{fill:none;stroke-width:3;stroke-linecap:round;stroke-linejoin:round}.trend-zero{font-size:11px;color:#718096;margin:0}.module-area{margin-top:16px}.section-head{display:flex;align-items:flex-end;justify-content:space-between;gap:12px;margin:0 0 10px}.section-head h2{margin:0;font-size:22px}.module-grid{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px}.module-tile{min-height:126px;text-align:left;border:1px solid rgba(248,250,252,.12);border-radius:8px;color:#fff;padding:14px;background:linear-gradient(145deg,var(--top,#0a84ff),var(--bottom,#0058aa));box-shadow:0 14px 34px rgba(0,0,0,.20);transition:transform .16s,border-color .16s,box-shadow .16s}.module-tile:hover,.module-tile.active{transform:translateY(-2px);border-color:rgba(255,255,255,.44);box-shadow:0 20px 42px rgba(0,0,0,.28)}.module-top{display:flex;justify-content:space-between;gap:10px;align-items:flex-start}.glyph{width:42px;height:42px;border-radius:8px;background:rgba(255,255,255,.18);display:grid;place-items:center;font-weight:900}.module-name{font-size:18px;font-weight:900;margin-top:12px}.module-detail{color:rgba(255,255,255,.82);font-size:12px;line-height:1.35;margin-top:4px}.module-count{font-size:11px;font-weight:900;padding:4px 7px;border-radius:7px;background:rgba(255,255,255,.16)}.workspace-section{margin-top:16px;border:1px solid rgba(248,250,252,.12);border-radius:8px;background:rgba(12,18,26,.64);padding:14px}.workspace-head{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:12px}.workspace-title{font-size:24px;font-weight:900}.workspace-subtitle{color:#b8c2ce;font-size:12px;margin-top:3px}.module-tabs{display:flex;gap:7px;overflow:auto;padding-bottom:3px}.module-tab{white-space:nowrap;height:34px;border:1px solid rgba(248,250,252,.15);border-radius:7px;background:rgba(248,250,252,.08);color:#dce5ef;padding:0 10px;font-size:12px;font-weight:900}.module-tab.active{background:#f8fafc;color:#111821}.workspace-shell{min-height:420px}.module-view{display:none;grid-template-columns:minmax(260px,.88fr) minmax(360px,1.35fr) minmax(280px,.9fr);gap:12px}.module-view.active{display:grid}.workspace-pane{background:var(--panel);color:var(--ink);border:1px solid var(--line);border-radius:8px;padding:14px;min-width:0;box-shadow:0 12px 28px rgba(15,23,42,.08)}.workspace-pane h3{margin:0 0 12px;font-size:15px}.stack{display:grid;gap:10px}.row{display:flex;gap:8px;align-items:center}.row.wrap{flex-wrap:wrap}.grow{flex:1}.small{width:88px}.field{display:grid;gap:5px}label{font-size:12px;color:#607082;font-weight:800}input,select{width:100%;height:38px;border-radius:7px;border:1px solid #cbd5e1;background:#fbfdff;color:#17202a;padding:0 10px;font-size:13px}input[type=range]{padding:0}input[type=checkbox]{width:18px;height:18px;accent-color:#0a84ff}.action-button,.secondary,.stop{height:38px;border-radius:7px;border:1px solid #17202a;padding:0 12px;font-size:13px;font-weight:900}.action-button{background:#17202a;color:#fff}.secondary{background:#fff;color:#17202a;border-color:#cbd5e1}.stop{background:var(--red);color:#fff;border-color:var(--red)}.log{min-height:170px;max-height:310px;overflow:auto;border-radius:8px;background:#0c141d;color:#d9f8e9;padding:11px;font:12px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;white-space:pre-wrap}.status-list{display:grid;gap:8px}.status-row{display:flex;justify-content:space-between;gap:10px;border-bottom:1px solid #edf2f7;padding-bottom:7px;font-size:12px}.status-row:last-child{border-bottom:0}.danger{color:#b42318}.files{max-height:290px;overflow:auto;border:1px solid var(--line);border-radius:8px}.file{display:flex;justify-content:space-between;gap:10px;padding:9px 10px;border-bottom:1px solid #eef2f7;font-size:12px}.file:last-child{border-bottom:0}.file button{height:30px;padding:0 9px}@media(max-width:1120px){main{padding:16px}.hero,.module-view.active{grid-template-columns:1fr}.module-grid,.telemetry-grid{grid-template-columns:repeat(2,minmax(0,1fr))}.workspace-shell{min-height:0}}@media(max-width:620px){.topbar{height:auto;align-items:flex-start;flex-direction:column}.hero-title{font-size:24px}.module-grid,.telemetry-grid{grid-template-columns:1fr}.workspace-head{align-items:flex-start;flex-direction:column}.row{align-items:stretch;flex-direction:column}.row.wrap{flex-direction:row}.url{font-size:17px}.sub{white-space:normal}}
.auth-gate{min-height:100vh;display:grid;place-items:center;padding:24px;background:#10151c}.auth-panel{width:min(420px,100%);border:1px solid rgba(248,250,252,.16);border-radius:8px;background:#f8fbff;color:#17202a;padding:28px;box-shadow:0 24px 70px rgba(0,0,0,.32)}.auth-mark{width:48px;height:48px;border-radius:8px;display:grid;place-items:center;background:#17202a;color:#fff;font-weight:900;margin-bottom:20px}.auth-panel h1{font-size:24px;margin:0 0 8px}.auth-copy{margin:0 0 22px;color:#64748b;font-size:13px;line-height:1.55}.auth-form{display:grid;gap:12px;position:relative}.auth-form input{height:44px}.auth-username{position:absolute!important;width:1px!important;height:1px!important;opacity:0;pointer-events:none}.auth-submit{height:44px;border:1px solid #17202a;border-radius:7px;background:#17202a;color:#fff;font-weight:900}.auth-error{min-height:20px;margin:0;color:#b42318;font-size:12px}.auth-note{margin:16px 0 0;color:#64748b;font-size:11px;line-height:1.5}.top-actions{display:flex;align-items:center;gap:8px}.token-button{height:36px;border-color:rgba(248,250,252,.22);background:rgba(248,250,252,.08);color:#f8fafc}[hidden]{display:none!important}
</style>
</head>
<body>
<section class="auth-gate" id="authGate">
<div class="auth-panel">
<div class="auth-mark">RK</div>
<h1>开发板测试平台访问认证</h1>
<p class="auth-copy">输入开发板 Web 访问 Token，认证后才能读取状态或执行硬件控制。板卡出厂默认 Token 为 topeet。</p>
<form class="auth-form" id="authForm">
<input class="auth-username" type="text" name="username" autocomplete="username" value="boardlab" aria-hidden="true" tabindex="-1">
<label for="tokenInput">访问 Token</label>
<input id="tokenInput" type="password" autocomplete="current-password" spellcheck="false" required>
<button class="auth-submit" type="submit">解锁控制台</button>
<p class="auth-error" id="authError"></p>
</form>
<p class="auth-note">Token 只保存在当前页面内存中，刷新或关闭页面后需要重新输入。</p>
</div>
</section>
<main id="consoleShell" hidden>
<header class="topbar">
<div class="brand"><div class="mark">RK</div><div><p class="title">局域网硬件控制台</p><p class="sub">{{PRODUCT_NAME}} / {{KERNEL_TEXT}}</p></div></div>
<div class="top-actions"><div class="status-chip" id="bridgeState">{{WEB_STATUS}}</div><button class="secondary token-button" id="changeToken" type="button">更换 Token</button></div>
</header>
<section class="hero">
<div class="hero-panel">
<p class="hero-title">迅为开发板测试平台 Web</p>
<p class="hero-copy">模块化布局与 Qt 端测试应用保持一致。先从模块网格进入单项工具，再在配置、操作、状态日志三个工作区完成板端控制。</p>
<div class="api-row"><span class="pill">/api/system</span><span class="pill">/api/board</span><span class="pill">/api/readiness</span><span class="pill">POST /api/control</span></div>
<div class="toolbar"><button class="action-button" onclick="sendControl('refresh')">刷新发现</button><button class="secondary" onclick="refresh()">刷新页面数据</button><span class="muted" id="inventory">等待板端数据</span></div>
</div>
<div class="hero-panel">
<p class="hero-copy">板端地址</p>
<div class="url" id="urlText">{{WEB_URL}}</div>
<p class="hero-copy">扫码或在同一局域网浏览器打开。控制动作会提交到当前 Qt 进程。</p>
</div>
</section>
<section class="telemetry-grid">
<div class="telemetry-card trend-card"><div class="telemetry-head"><h2>CPU</h2><div class="trend-value"><div class="metric" id="cpu">--</div><span class="trend-label">实时负载</span></div></div><div class="trend-chart" aria-label="CPU 使用率趋势"><svg id="cpuChart" viewBox="0 0 220 72" preserveAspectRatio="none"><line class="trend-grid" x1="0" y1="18" x2="220" y2="18"/><line class="trend-grid" x1="0" y1="36" x2="220" y2="36"/><line class="trend-grid" x1="0" y1="54" x2="220" y2="54"/><polygon id="cpuFill" class="trend-fill" fill="#0a84ff" points="0,72 220,72"/><polyline id="cpuLine" class="trend-line" stroke="#0a84ff" points=""/></svg></div><p class="trend-zero" id="temp">--</p></div>
<div class="telemetry-card trend-card"><div class="telemetry-head"><h2>内存</h2><div class="trend-value"><div class="metric" id="mem">--</div><span class="trend-label">占用比例</span></div></div><div class="trend-chart" aria-label="内存使用率趋势"><svg id="memoryChart" viewBox="0 0 220 72" preserveAspectRatio="none"><line class="trend-grid" x1="0" y1="18" x2="220" y2="18"/><line class="trend-grid" x1="0" y1="36" x2="220" y2="36"/><line class="trend-grid" x1="0" y1="54" x2="220" y2="54"/><polygon id="memoryFill" class="trend-fill" fill="#18a058" points="0,72 220,72"/><polyline id="memoryLine" class="trend-line" stroke="#18a058" points=""/></svg></div><p class="trend-zero" id="memText">--</p></div>
<div class="telemetry-card"><h2>网络</h2><div class="metric" id="network">--</div><p class="muted" id="uptime">--</p></div>
<div class="telemetry-card"><h2>后端</h2><div class="metric" id="busy">--</div><p class="muted" id="statusText">--</p></div>
</section>
<section class="module-area">
<div class="section-head"><h2>测试应用</h2><span class="muted">与 Qt 端独立 demo 模块对应</span></div>
<div class="module-grid">
<button class="module-tile active" data-module="GPIO" style="--top:#635BFF;--bottom:#4038A8" onclick="openModule('GPIO')"><span class="module-top"><span class="glyph">IO</span><span class="module-count" id="module-count-GPIO">--</span></span><div class="module-name">GPIO</div><div class="module-detail">输入输出与电平读回</div></button>
<button class="module-tile" data-module="UART" style="--top:#00A6B3;--bottom:#007782" onclick="openModule('UART')"><span class="module-top"><span class="glyph">RX</span><span class="module-count" id="module-count-UART">--</span></span><div class="module-name">串口</div><div class="module-detail">tty 设备 TX/RX 样本</div></button>
<button class="module-tile" data-module="Ethernet" style="--top:#18A058;--bottom:#0B7D3E" onclick="openModule('Ethernet')"><span class="module-top"><span class="glyph">EN</span><span class="module-count" id="module-count-Ethernet">--</span></span><div class="module-name">以太网</div><div class="module-detail">链路、DHCP、静态 IP、Ping</div></button>
<button class="module-tile" data-module="CAN" style="--top:#E59F20;--bottom:#9A6200" onclick="openModule('CAN')"><span class="module-top"><span class="glyph">CA</span><span class="module-count" id="module-count-CAN">--</span></span><div class="module-name">CAN</div><div class="module-detail">SocketCAN 与循环发送</div></button>
<button class="module-tile" data-module="Cellular" style="--top:#D84C7F;--bottom:#8C274D" onclick="openModule('Cellular')"><span class="module-top"><span class="glyph">4G</span><span class="module-count" id="module-count-Cellular">--</span></span><div class="module-name">4G / 5G</div><div class="module-detail">APN、拨号、联网测试</div></button>
<button class="module-tile" data-module="Camera" style="--top:#0A84FF;--bottom:#0058AA" onclick="openModule('Camera')"><span class="module-top"><span class="glyph">CM</span><span class="module-count" id="module-count-Camera">--</span></span><div class="module-name">摄像头</div><div class="module-detail">V4L2 采集验证</div></button>
<button class="module-tile" data-module="HDMIIN" style="--top:#5E5CE6;--bottom:#2F2C8F" onclick="openModule('HDMIIN')"><span class="module-top"><span class="glyph">HI</span><span class="module-count" id="module-count-HDMIIN">--</span></span><div class="module-name">HDMI 输入</div><div class="module-detail">HDMIRX 采集确认</div></button>
<button class="module-tile" data-module="Watchdog" style="--top:#4F6378;--bottom:#2D3E50" onclick="openModule('Watchdog')"><span class="module-top"><span class="glyph">WD</span><span class="module-count" id="module-count-Watchdog">--</span></span><div class="module-name">看门狗</div><div class="module-detail">wdctl 与复位测试</div></button>
<button class="module-tile" data-module="Audio" style="--top:#E35432;--bottom:#A93B24" onclick="openModule('Audio')"><span class="module-top"><span class="glyph">AU</span><span class="module-count" id="module-count-Audio">ALSA</span></span><div class="module-name">音频 / 蜂鸣器</div><div class="module-detail">耳机、录音、pwm-beeper 蜂鸣器</div></button>
<button class="module-tile" data-module="Backlight" style="--top:#C58A00;--bottom:#805A00" onclick="openModule('Backlight')"><span class="module-top"><span class="glyph">BL</span><span class="module-count" id="module-count-Backlight">--</span></span><div class="module-name">背光</div><div class="module-detail">亮度读写与确认</div></button>
<button class="module-tile" data-module="Files" style="--top:#5E6A78;--bottom:#344050" onclick="openModule('Files')"><span class="module-top"><span class="glyph">FS</span><span class="module-count" id="module-count-Files">浏览</span></span><div class="module-name">文件</div><div class="module-detail">板端目录浏览</div></button>
</div>
</section>
<section class="workspace-section">
<div class="workspace-head"><div><div class="workspace-title" id="activeModuleTitle">GPIO</div><div class="workspace-subtitle" id="activeModuleSubtitle">sysfs 输入输出、电平读回</div></div><div class="module-tabs"><button class="module-tab active" data-module="GPIO" onclick="openModule('GPIO')">GPIO</button><button class="module-tab" data-module="UART" onclick="openModule('UART')">串口</button><button class="module-tab" data-module="Ethernet" onclick="openModule('Ethernet')">以太网</button><button class="module-tab" data-module="CAN" onclick="openModule('CAN')">CAN</button><button class="module-tab" data-module="Cellular" onclick="openModule('Cellular')">4G/5G</button><button class="module-tab" data-module="Camera" onclick="openModule('Camera')">摄像头</button><button class="module-tab" data-module="HDMIIN" onclick="openModule('HDMIIN')">HDMIIN</button><button class="module-tab" data-module="Watchdog" onclick="openModule('Watchdog')">看门狗</button><button class="module-tab" data-module="Audio" onclick="openModule('Audio')">音频</button><button class="module-tab" data-module="Backlight" onclick="openModule('Backlight')">背光</button><button class="module-tab" data-module="Files" onclick="openModule('Files')">文件</button></div></div>
<pre class="log" id="resultLog">等待控制操作</pre>
<div class="workspace-shell">
<section class="module-view active" data-module="GPIO"><div class="workspace-pane config-pane"><h3>GPIO 配置</h3><div class="stack"><div class="field"><label>GPIO 引脚</label><input id="gpioPin" value="GPIO1_B1"></div><label class="row"><input id="gpioHigh" type="checkbox" checked> 输出 HIGH</label><label class="row"><input id="gpioInput" type="checkbox" checked> 输入模式</label></div></div><div class="workspace-pane action-pane"><h3>操作</h3><div class="row wrap"><button class="action-button" onclick="gpioApply()">应用 GPIO</button><button class="secondary" onclick="gpioRead()">读取 GPIO</button></div></div><div class="workspace-pane status-pane"><h3>状态日志</h3><div class="status-list"><div class="status-row"><span>后端</span><strong id="gpioBackendState">--</strong></div><div class="status-row"><span>最近操作</span><strong>POST /api/control</strong></div></div><pre class="log">GPIO 状态等待刷新</pre></div></section>
<section class="module-view" data-module="UART"><div class="workspace-pane config-pane"><h3>串口配置</h3><div class="stack"><div class="field"><label>设备</label><select id="serialDevice"></select></div><div class="row"><div class="field grow"><label>波特率</label><input id="uartBaud" value="115200"></div><div class="field small"><label>数据位</label><select id="uartDataBits"><option>8</option><option>7</option><option>6</option><option>5</option></select></div></div><div class="row"><div class="field grow"><label>校验</label><select id="uartParity"><option>None</option><option>Even</option><option>Odd</option></select></div><div class="field small"><label>停止位</label><select id="uartStopBits"><option>1</option><option>2</option></select></div></div></div></div><div class="workspace-pane action-pane"><h3>TX / RX</h3><div class="row wrap"><button class="action-button" onclick="uart('uart-tx')">发送测试帧</button><button class="secondary" onclick="uart('uart-rx')">读取样本</button></div></div><div class="workspace-pane status-pane"><h3>状态</h3><div class="status-row"><span>检测设备</span><strong id="uartStatus">--</strong></div><pre class="log">55 AA 01 00 FE</pre></div></section>
<section class="module-view" data-module="Ethernet"><div class="workspace-pane config-pane"><h3>网络配置</h3><div class="stack"><div class="field"><label>接口</label><select id="ethernetInterface"></select></div><div class="row"><input id="ethIp" class="grow" value="192.168.1.100"><input id="ethMask" class="grow" value="255.255.255.0"></div><div class="row"><input id="ethGateway" class="grow" value="192.168.1.1"><input id="ethDns1" class="grow" value="8.8.8.8"></div></div></div><div class="workspace-pane action-pane"><h3>链路操作</h3><div class="row wrap"><button class="action-button" onclick="ethernet('ethernet-dhcp')">DHCP</button><button class="secondary" onclick="ethernet('ethernet-refresh')">刷新接口</button><button class="action-button" onclick="ethernet('ethernet-static')">应用静态 IPv4</button></div><div class="row"><input id="pingTarget" class="grow" value="www.baidu.com"><button class="secondary" onclick="pingSelected()">Ping</button></div></div><div class="workspace-pane status-pane"><h3>接口状态</h3><div class="status-row"><span>接口数</span><strong id="ethernetStatus">--</strong></div><pre class="log">等待以太网操作</pre></div></section>
<section class="module-view" data-module="CAN"><div class="workspace-pane config-pane"><h3>CAN 配置</h3><div class="stack"><div class="field"><label>接口</label><select id="canInterface"></select></div><div class="row"><select id="canBitrate" class="grow"><option value="50000">50k</option><option value="100000">100k</option><option value="125000">125k</option><option value="250000">250k</option><option value="500000">500k</option><option value="1000000" selected>1M</option></select><label class="row"><input id="canFd" type="checkbox"> FD</label></div><input id="canDataBitrate" value="3000000"></div></div><div class="workspace-pane action-pane"><h3>总线操作</h3><div class="row wrap"><button class="action-button" onclick="can('can-open')">打开</button><button class="secondary" onclick="can('can-rx')">RX</button><button class="action-button" onclick="can('can-tx')">TX</button><button class="stop" onclick="can('can-close')">关闭</button><button class="secondary" onclick="can('can-loop-start')">循环发送</button><button class="stop" onclick="can('can-loop-stop')">停止循环</button></div></div><div class="workspace-pane status-pane"><h3>状态</h3><div class="status-row"><span>CAN 接口</span><strong id="canStatus">--</strong></div><pre class="log">STD/EFF/RTR 0x11</pre></div></section>
<section class="module-view" data-module="Cellular"><div class="workspace-pane config-pane"><h3>蜂窝配置</h3><div class="stack"><input id="cellularApn" placeholder="APN 可留空"><input id="cellularInterface" placeholder="接口由拨号后自动发现"></div></div><div class="workspace-pane action-pane"><h3>拨号操作</h3><div class="row wrap"><button class="action-button" onclick="cellular('cellular-start')">初始化</button><button class="secondary" onclick="cellular('cellular-status')">检查</button><button class="stop" onclick="cellular('cellular-stop')">停止</button><button class="secondary" onclick="sendControl('ping-interface',{interface:val('cellularInterface'),target:'www.baidu.com'})">联网测试</button></div></div><div class="workspace-pane status-pane"><h3>状态</h3><div class="status-row"><span>设备</span><strong id="cellularStatus">--</strong></div><pre class="log">等待 4G/5G 状态</pre></div></section>
<section class="module-view" data-module="Camera"><div class="workspace-pane config-pane"><h3>摄像头节点</h3><select id="cameraDevice"></select></div><div class="workspace-pane action-pane"><h3>采集</h3><div class="row wrap"><button class="action-button" onclick="cameraStart()">启动摄像头</button><button class="stop" onclick="sendControl('camera-stop')">停止摄像头</button></div></div><div class="workspace-pane status-pane"><h3>状态</h3><div class="status-row"><span>摄像头</span><strong id="cameraStatus">--</strong></div><pre class="log">等待摄像头采集</pre></div></section>
<section class="module-view" data-module="HDMIIN"><div class="workspace-pane config-pane"><h3>HDMI 输入节点</h3><select id="hdmiDevice"></select></div><div class="workspace-pane action-pane"><h3>采集</h3><div class="row wrap"><button class="action-button" onclick="hdmiStart()">启动 HDMIIN</button><button class="stop" onclick="sendControl('hdmiin-stop')">停止 HDMIIN</button></div></div><div class="workspace-pane status-pane"><h3>状态</h3><div class="status-row"><span>HDMIIN</span><strong id="hdmiStatus">--</strong></div><pre class="log">等待 HDMIIN 采集</pre></div></section>
<section class="module-view" data-module="Watchdog"><div class="workspace-pane config-pane"><h3>看门狗配置</h3><div class="stack"><select id="watchdogDevice"></select><input id="watchdogTimeout" value="5"><label class="row"><input id="watchdogConfirm" type="checkbox"> 确认允许复位</label></div></div><div class="workspace-pane action-pane"><h3>危险操作</h3><div class="row wrap"><button class="secondary" onclick="watchdogStatus()">读取状态</button><button class="stop" onclick="watchdogStart()">启动测试</button></div><p class="muted danger">启动测试可能触发板卡复位。</p></div><div class="workspace-pane status-pane"><h3>状态</h3><div class="status-row"><span>设备</span><strong id="watchdogStatusText">--</strong></div><pre class="log">等待 wdctl 状态</pre></div></section>
<section class="module-view" data-module="Audio"><div class="workspace-pane config-pane"><h3>音频 / 蜂鸣器</h3><div class="stack"><label>系统音量 <span id="audioVolumeLabel">--</span></label><input id="audioVolume" type="range" min="0" max="100" value="50" oninput="setText('audioVolumeLabel',this.value+'%')"><label>蜂鸣器 <span id="beepLabel">50%</span></label><input id="beepPercent" type="range" min="0" max="100" value="50" oninput="setText('beepLabel',this.value+'%')"></div></div><div class="workspace-pane action-pane"><h3>播放 / 录音</h3><div class="row wrap"><button class="action-button" onclick="sendControl('audio-volume',{percent:num('audioVolume')})">应用音量</button><button class="secondary" onclick="sendControl('audio-play-test')">播放测试</button><button class="secondary" onclick="sendControl('audio-record')">录音</button><button class="secondary" onclick="sendControl('audio-play-recorded')">放录音</button><button class="stop" onclick="sendControl('audio-stop-all')">停止</button><button class="action-button" onclick="sendControl('audio-beep-apply',{percent:num('beepPercent')})">应用蜂鸣器</button><button class="secondary" onclick="sendControl('audio-beep-heartbeat')">心跳</button><button class="stop" onclick="sendControl('audio-beep-stop')">停止蜂鸣器</button></div></div><div class="workspace-pane status-pane"><h3>状态</h3><div class="status-row"><span>音量</span><strong id="audioStatus">--</strong></div><pre class="log">等待音频操作</pre></div></section>
<section class="module-view" data-module="Backlight"><div class="workspace-pane config-pane"><h3>背光配置</h3><div class="stack"><label>背光 <span id="backlightLabel">--</span></label><input id="backlightPercent" type="range" min="1" max="100" value="50" oninput="setText('backlightLabel',this.value+'%')"></div></div><div class="workspace-pane action-pane"><h3>亮度操作</h3><button class="action-button" onclick="sendControl('backlight-set',{percent:num('backlightPercent')})">应用背光</button></div><div class="workspace-pane status-pane"><h3>状态</h3><div class="status-row"><span>可用</span><strong id="backlightStatus">--</strong></div><pre class="log">等待背光操作</pre></div></section>
<section class="module-view" data-module="Files"><div class="workspace-pane config-pane"><h3>当前目录</h3><div class="status-row"><span>路径</span><strong id="currentPath">/</strong></div></div><div class="workspace-pane action-pane"><h3>文件浏览</h3><button class="secondary" onclick="sendControl('files-up')">上一级</button><div class="files" id="files"></div></div><div class="workspace-pane status-pane"><h3>状态</h3><pre class="log">等待文件操作</pre></div></section>
</div>
</section>
</main>
<script>
let board={};let activeModule='GPIO';let accessToken='';let refreshTimer=null;const chartSamples={cpu:[],memory:[]};const chartLimit=42;
const moduleMeta={GPIO:['GPIO','sysfs 输入输出、电平读回'],UART:['串口 / 485','tty 设备、波特率、数据位、校验、TX/RX 样本'],Ethernet:['以太网','链路、MAC、IPv4、DHCP、静态 IP、Ping'],CAN:['CAN','SocketCAN、bitrate、RX、TX、循环发送'],Cellular:['4G / 5G','APN、接口、状态与联网测试'],Camera:['摄像头','选择 rkisp/USB 摄像头采集节点验证'],HDMIIN:['HDMI 输入','自动选择 rk_hdmirx HDMI 输入节点验证'],Watchdog:['看门狗','wdctl 状态与现场可控复位测试'],Audio:['音频 / 蜂鸣器','耳机测试、录音回放、pwm-beeper 蜂鸣器'],Backlight:['背光','背光设备、亮度写入和状态确认'],Files:['文件','板端文件浏览和目录定位']};
const $=id=>document.getElementById(id);const val=id=>($(id)||{}).value||'';const num=id=>Number(val(id))||0;function setText(id,value){const e=$(id);if(e)e.textContent=value;}
function clampPercent(value){const n=Number(value);if(!Number.isFinite(n))return 0;return Math.max(0,Math.min(100,n));}
function pushChartSample(name,value){const samples=chartSamples[name];samples.push(clampPercent(value));while(samples.length>chartLimit)samples.shift();}
function renderTrendChart(name){const samples=chartSamples[name];const line=$(name+'Line');const fill=$(name+'Fill');if(!line||!fill)return;if(!samples.length){line.setAttribute('points','');fill.setAttribute('points','0,72 220,72');return;}const width=220;const height=72;const step=samples.length>1?width/(samples.length-1):width;const points=samples.map((value,index)=>`${(index*step).toFixed(1)},${(height-height*value/100).toFixed(1)}`).join(' ');line.setAttribute('points',points);fill.setAttribute('points',`0,${height} ${points} ${width},${height}`);}
function setValue(id,value){const e=$(id);if(e)e.value=value;}
function openModule(name){activeModule=name;document.querySelectorAll('.module-tile,.module-tab').forEach(e=>e.classList.toggle('active',e.dataset.module===name));document.querySelectorAll('.module-view').forEach(e=>e.classList.toggle('active',e.dataset.module===name));setText('activeModuleTitle',moduleMeta[name][0]);setText('activeModuleSubtitle',moduleMeta[name][1]);document.querySelector('.workspace-section').scrollIntoView({block:'start'});}
function showAuthentication(message=''){accessToken='';if(refreshTimer){clearInterval(refreshTimer);refreshTimer=null;}$('consoleShell').hidden=true;$('authGate').hidden=false;$('authError').textContent=message;$('tokenInput').value='';$('tokenInput').focus();}
function showConsole(){$('authGate').hidden=true;$('consoleShell').hidden=false;$('authError').textContent='';$('tokenInput').value='';if(!refreshTimer)refreshTimer=setInterval(refresh,2500);}
function authorizedHeaders(headers={}){const result=new Headers(headers);if(accessToken)result.set('Authorization','Bearer '+accessToken);return result;}
async function apiFetch(path,options={}){const request={...options,cache:'no-store',headers:authorizedHeaders(options.headers||{})};const response=await fetch(path,request);if(response.status===401){showAuthentication('Token 无效或已变更，请重新输入。');const error=new Error('认证失败');error.authentication=true;throw error;}return response;}
async function authenticate(event){event.preventDefault();const candidate=$('tokenInput').value.trim();if(!candidate){$('authError').textContent='请输入访问 Token。';return;}accessToken=candidate;$('authError').textContent='正在验证...';try{const response=await apiFetch('/api/system');if(!response.ok)throw new Error('HTTP '+response.status);await response.json();showConsole();await refresh();}catch(error){if(!error.authentication)showAuthentication('暂时无法连接迅为开发板测试平台服务：'+error.message);}}
async function getJson(path){const r=await apiFetch(path);if(!r.ok)throw new Error(path+' '+r.status);return r.json();}
async function sendControl(action,params={}){try{const r=await apiFetch('/api/control',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({action,params})});const data=await r.json();if(!r.ok)throw new Error(data.message||('HTTP '+r.status));$('resultLog').textContent=JSON.stringify(data,null,2);setTimeout(refresh,650);}catch(e){if(!e.authentication)$('resultLog').textContent='CONTROL FAILED '+e.message;}}
function fillSelect(id,items,label){const s=$(id);if(!s)return;const selected=s.value;s.innerHTML='';(items&&items.length?items:['']).forEach(item=>{const o=document.createElement('option');if(typeof item==='object'){o.value=item.path||item.name||'';o.textContent=label?label(item):(item.label||item.name||item.path||'');Object.keys(item).forEach(k=>o.dataset[k]=item[k]);}else{o.value=item;o.textContent=item||'未检测';}s.appendChild(o);});if(selected)s.value=selected;}
function renderFiles(items){const list=$('files');if(!list)return;list.innerHTML='';(items||[]).forEach(f=>{const row=document.createElement('div');row.className='file';const name=document.createElement('span');name.textContent=f.name||f.path;row.appendChild(name);if(f.directory){const b=document.createElement('button');b.className='secondary';b.textContent='打开';b.onclick=()=>sendControl('files-open',{path:f.path});row.appendChild(b);}list.appendChild(row);});}
function countText(items,unit){return (items&&items.length?items.length:0)+' '+unit;}
function renderBoard(b){board=b;setText('busy',b.backendBusy?'忙':'空闲');setText('statusText',b.statusText||'后端就绪');setText('gpioBackendState',b.statusText||'就绪');setText('inventory',`ETH ${(b.ethernetInterfaces||[]).length} / CAN ${(b.canInterfaces||[]).length} / UART ${(b.serialDevices||[]).length} / Camera ${(b.cameraCaptureDevices||[]).length} / HDMIIN ${(b.hdmiInCaptureDevices||[]).length}`);setValue('audioVolume',b.audioVolumePercent||50);setText('audioVolumeLabel',val('audioVolume')+'%');setText('audioStatus',(b.audioVolumePercent||50)+'%');setValue('backlightPercent',b.backlightPercent||50);setText('backlightLabel',b.backlightAvailable?(b.backlightPercent+'%'):'未发现');setText('backlightStatus',b.backlightAvailable?'可用':'未发现');fillSelect('serialDevice',b.serialDevices);fillSelect('canInterface',b.canInterfaces);fillSelect('watchdogDevice',b.watchdogDevices&&b.watchdogDevices.length?b.watchdogDevices:['/dev/watchdog']);fillSelect('ethernetInterface',b.ethernetInterfaces||[],x=>`${x.name||''} ${x.ipv4||''}`);fillSelect('cameraDevice',b.cameraCaptureDevices&&b.cameraCaptureDevices.length?b.cameraCaptureDevices:b.cameraVideoDevices,x=>x.label||x.path);fillSelect('hdmiDevice',b.hdmiInCaptureDevices&&b.hdmiInCaptureDevices.length?b.hdmiInCaptureDevices:b.hdmiInVideoDevices,x=>x.label||x.path);setText('currentPath',b.currentPath||'/');renderFiles(b.fileEntries);setText('module-count-GPIO','IO');setText('module-count-UART',countText(b.serialDevices,'tty'));setText('module-count-Ethernet',countText(b.ethernetInterfaces,'eth'));setText('module-count-CAN',countText(b.canInterfaces,'can'));setText('module-count-Cellular',countText(b.cellularDevices,'节点'));setText('module-count-Camera',countText(b.cameraCaptureDevices&&b.cameraCaptureDevices.length?b.cameraCaptureDevices:b.cameraVideoDevices,'项'));setText('module-count-HDMIIN',countText(b.hdmiInCaptureDevices&&b.hdmiInCaptureDevices.length?b.hdmiInCaptureDevices:b.hdmiInVideoDevices,'项'));setText('module-count-Watchdog',countText(b.watchdogDevices,'wd'));setText('module-count-Backlight',b.backlightAvailable?(b.backlightPercent+'%'):'无');setText('uartStatus',countText(b.serialDevices,'tty'));setText('ethernetStatus',countText(b.ethernetInterfaces,'eth'));setText('canStatus',countText(b.canInterfaces,'can'));setText('cellularStatus',countText(b.cellularDevices,'节点'));setText('cameraStatus',countText(b.cameraCaptureDevices&&b.cameraCaptureDevices.length?b.cameraCaptureDevices:b.cameraVideoDevices,'项'));setText('hdmiStatus',countText(b.hdmiInCaptureDevices&&b.hdmiInCaptureDevices.length?b.hdmiInCaptureDevices:b.hdmiInVideoDevices,'项'));setText('watchdogStatusText',countText(b.watchdogDevices,'wd'));}
async function refresh(){try{const [system,b]=await Promise.all([getJson('/api/system'),getJson('/api/board')]);const cpu=clampPercent(system.cpuUsage);const mem=clampPercent(system.memoryUsage);pushChartSample('cpu',cpu);pushChartSample('memory',mem);renderTrendChart('cpu');renderTrendChart('memory');setText('cpu',Math.round(cpu)+'%');setText('temp',system.temperatureText||'');setText('mem',Math.round(mem)+'%');setText('memText',system.memoryText||'');setText('network',system.networkText||'无');setText('uptime',system.uptimeText||'');setText('bridgeState','运行于 '+location.host);renderBoard(b);}catch(e){setText('bridgeState','刷新失败 '+e.message);}}
function gpioParams(){return{pin:val('gpioPin'),mode:$('gpioInput').checked?'in':'out',high:$('gpioHigh').checked};}function gpioApply(){sendControl('gpio-apply',gpioParams());}function gpioRead(){sendControl('gpio-read',gpioParams());}
function uart(action){sendControl(action,{device:val('serialDevice'),baudRate:num('uartBaud'),dataBits:num('uartDataBits'),parity:val('uartParity'),stopBits:num('uartStopBits'),flowControl:'None'});}
function ethernet(action){const p={interface:val('ethernetInterface'),address:val('ethIp'),netmask:val('ethMask'),gateway:val('ethGateway'),dns1:val('ethDns1')};sendControl(action,p);}function pingSelected(){sendControl('ping-interface',{interface:val('ethernetInterface'),target:val('pingTarget')||'www.baidu.com'});}
function can(action){sendControl(action,{interface:val('canInterface'),bitrate:num('canBitrate'),dataBitrate:num('canDataBitrate'),fdMode:$('canFd').checked});}
function cellular(action){if(action==='cellular-status')return sendControl(action,{});if(action==='cellular-stop')return sendControl(action,{interface:val('cellularInterface')});sendControl(action,{apn:val('cellularApn'),interface:val('cellularInterface')});}
function cameraStart(){const s=$('cameraDevice');const opt=s.options[s.selectedIndex];const params={device:s.value};if(opt&&opt.dataset.mode)params.mode=opt.dataset.mode;sendControl('camera-start',params);}
function hdmiStart(){sendControl('hdmiin-start',{device:val('hdmiDevice'),mode:'hdmiin'});}
function watchdogStatus(){sendControl('watchdog-status',{device:val('watchdogDevice')||'/dev/watchdog'});}function watchdogStart(){sendControl('watchdog-start',{device:val('watchdogDevice')||'/dev/watchdog',timeout:num('watchdogTimeout')||5,confirm:$('watchdogConfirm').checked});}
$('authForm').addEventListener('submit',authenticate);$('changeToken').addEventListener('click',()=>showAuthentication(''));showAuthentication('');
</script>
</body>
</html>)HTML");
    html.replace(QStringLiteral("{{WEB_STATUS}}"), m_statusText.toHtmlEscaped());
    html.replace(QStringLiteral("{{WEB_URL}}"), url().toHtmlEscaped());
    html.replace(QStringLiteral("{{KERNEL_TEXT}}"), m_systemInfo->kernelText().toHtmlEscaped());
    html.replace(QStringLiteral("{{PRODUCT_NAME}}"), QSysInfo::prettyProductName().toHtmlEscaped());
    return html.toUtf8();
}

QByteArray LocalWebServer::systemJson() const
{
    m_systemInfo->refresh();
    QJsonObject body;
    body.insert(QStringLiteral("cpuUsage"), m_systemInfo->cpuUsage());
    body.insert(QStringLiteral("memoryUsage"), m_systemInfo->memoryUsage());
    body.insert(QStringLiteral("memoryText"), m_systemInfo->memoryText());
    body.insert(QStringLiteral("diskUsage"), m_systemInfo->diskUsage());
    body.insert(QStringLiteral("diskText"), m_systemInfo->diskText());
    body.insert(QStringLiteral("temperatureText"), m_systemInfo->temperatureText());
    body.insert(QStringLiteral("uptimeText"), m_systemInfo->uptimeText());
    body.insert(QStringLiteral("kernelText"), m_systemInfo->kernelText());
    body.insert(QStringLiteral("networkText"), m_systemInfo->networkText());
    body.insert(QStringLiteral("hostName"), QSysInfo::machineHostName());
    body.insert(QStringLiteral("productName"), QSysInfo::prettyProductName());
    body.insert(QStringLiteral("productVersion"), QSysInfo::productVersion());
    body.insert(QStringLiteral("cpuArchitecture"), QSysInfo::currentCpuArchitecture());
    body.insert(QStringLiteral("qtRuntime"), QString::fromLatin1(qVersion()));
    return QJsonDocument(body).toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::boardJson() const
{
    QJsonObject body;
    QJsonObject profile = m_boardProfile ? m_boardProfile->toJson() : QJsonObject();
    if (!profile.isEmpty()) {
        // Keep the original API field for existing Qt/Web clients while the
        // runtime capability view reflects endpoints that were actually
        // discovered on this SDK/board.
        const QJsonObject configuredCapabilities =
            profile.value(QStringLiteral("capabilities")).toObject();
        QJsonObject runtimeCapabilities = configuredCapabilities;
        runtimeCapabilities.insert(QStringLiteral("buzzer"), m_boardBackend->buzzerAvailable());
        profile.insert(QStringLiteral("configuredCapabilities"), configuredCapabilities);
        profile.insert(QStringLiteral("capabilities"), runtimeCapabilities);
    }
    body.insert(QStringLiteral("profile"), profile);
    body.insert(QStringLiteral("statusText"), m_boardBackend->statusText());
    body.insert(QStringLiteral("backlightAvailable"), m_boardBackend->backlightAvailable());
    body.insert(QStringLiteral("backlightPercent"), m_boardBackend->backlightPercent());
    body.insert(QStringLiteral("audioVolumePercent"), m_boardBackend->audioVolumePercent());
    body.insert(QStringLiteral("audioPlaybackDevices"), QJsonArray::fromVariantList(m_boardBackend->audioPlaybackDevices()));
    body.insert(QStringLiteral("audioCaptureDevices"), QJsonArray::fromVariantList(m_boardBackend->audioCaptureDevices()));
    body.insert(QStringLiteral("buzzerAvailable"), m_boardBackend->buzzerAvailable());
    body.insert(QStringLiteral("buzzerAvailabilityStatus"), m_boardBackend->buzzerAvailabilityStatus());
    body.insert(QStringLiteral("buzzerControlMethod"), m_boardBackend->buzzerControlMethod());
    body.insert(QStringLiteral("fanStatus"), QJsonObject::fromVariantMap(m_boardBackend->fanStatus()));
    body.insert(QStringLiteral("backendBusy"), m_boardBackend->backendBusy());
    body.insert(QStringLiteral("backendBusyOperation"), m_boardBackend->backendBusyOperation());
    body.insert(QStringLiteral("lastOperation"), m_lastOperation);
    body.insert(QStringLiteral("audioOperationBusy"), m_boardBackend->audioOperationBusy());
    body.insert(QStringLiteral("ledAvailable"), m_boardBackend->ledAvailable());
    body.insert(QStringLiteral("ledDevices"), QJsonArray::fromVariantList(m_boardBackend->ledDevices()));
    body.insert(QStringLiteral("rtcStatus"), QJsonObject::fromVariantMap(m_boardBackend->rtcStatus()));
    body.insert(QStringLiteral("npuStatus"), QJsonObject::fromVariantMap(m_boardBackend->npuStatus()));
    body.insert(QStringLiteral("wifiStatus"), QJsonObject::fromVariantMap(m_boardBackend->wifiStatus()));
    body.insert(QStringLiteral("wifiManagerStatus"), QJsonObject::fromVariantMap(m_boardBackend->wifiManagerStatus()));
    body.insert(QStringLiteral("wifiAccessPoints"), QJsonArray::fromVariantList(m_boardBackend->wifiAccessPoints()));
    body.insert(QStringLiteral("wifiManagerBusy"), m_boardBackend->wifiManagerBusy());
    body.insert(QStringLiteral("bluetoothStatus"), QJsonObject::fromVariantMap(m_boardBackend->bluetoothStatus()));
    body.insert(QStringLiteral("gpuBenchmarkStatus"), QJsonObject::fromVariantMap(m_boardBackend->gpuBenchmarkStatus()));
    body.insert(QStringLiteral("webCameraPreviewStatus"),
                QJsonObject::fromVariantMap(m_boardBackend->webCameraPreviewStatus()));
    body.insert(QStringLiteral("inputDevices"), QJsonArray::fromVariantList(m_boardBackend->inputDevices()));
    body.insert(QStringLiteral("displayOutputs"), QJsonArray::fromVariantList(m_boardBackend->displayOutputs()));
    body.insert(QStringLiteral("environmentStatus"), QJsonObject::fromVariantMap(m_boardBackend->environmentStatus()));
    body.insert(QStringLiteral("environmentState"), QJsonObject::fromVariantMap(m_boardBackend->environmentState()));
    body.insert(QStringLiteral("keyTestStatus"), QJsonObject::fromVariantMap(m_boardBackend->keyTestStatus()));
    body.insert(QStringLiteral("hdmiInStatus"), QJsonObject::fromVariantMap(m_boardBackend->hdmiInStatus()));
    body.insert(QStringLiteral("storageDevices"), QJsonArray::fromVariantList(m_boardBackend->storageDevices()));
    body.insert(QStringLiteral("serialDevices"), QJsonArray::fromStringList(m_boardBackend->serialDevices()));
    body.insert(QStringLiteral("canInterfaces"), QJsonArray::fromStringList(m_boardBackend->canInterfaces()));
    body.insert(QStringLiteral("cellularDevices"), QJsonArray::fromStringList(m_boardBackend->cellularDevices()));
    body.insert(QStringLiteral("cellularModemStatus"),
                QJsonObject::fromVariantMap(m_boardBackend->cellularModemStatus()));
    body.insert(QStringLiteral("preferredCellularInterface"), m_boardBackend->preferredCellularInterface());
    body.insert(QStringLiteral("videoDevices"), QJsonArray::fromStringList(m_boardBackend->videoDevices()));
    body.insert(QStringLiteral("cameraVideoDevices"), QJsonArray::fromStringList(m_boardBackend->cameraVideoDevices()));
    body.insert(QStringLiteral("hdmiInVideoDevices"), QJsonArray::fromStringList(m_boardBackend->hdmiInVideoDevices()));
    body.insert(QStringLiteral("cameraCaptureDevices"), QJsonArray::fromVariantList(m_boardBackend->cameraCaptureDevices()));
    body.insert(QStringLiteral("hdmiInCaptureDevices"), QJsonArray::fromVariantList(m_boardBackend->hdmiInCaptureDevices()));
    body.insert(QStringLiteral("cameraVideoDevice"), m_boardBackend->cameraVideoDevice());
    body.insert(QStringLiteral("hdmiInVideoDevice"), m_boardBackend->hdmiInVideoDevice());
    body.insert(QStringLiteral("watchdogDevices"), QJsonArray::fromStringList(m_boardBackend->watchdogDevices()));
    body.insert(QStringLiteral("ethernetInterfaces"), QJsonArray::fromVariantList(m_boardBackend->ethernetInterfaces()));
    body.insert(QStringLiteral("fileEntries"), QJsonArray::fromVariantList(m_boardBackend->fileEntries()));
    body.insert(QStringLiteral("currentPath"), m_boardBackend->currentPath());
    body.insert(QStringLiteral("ethernetInterfaceCount"), m_boardBackend->ethernetInterfaces().size());
    return QJsonDocument(body).toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::environmentJson() const
{
    return QJsonDocument(QJsonObject::fromVariantMap(m_boardBackend->environmentStatus()))
        .toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::keyTestJson() const
{
    return QJsonDocument(QJsonObject::fromVariantMap(m_boardBackend->keyTestStatus()))
        .toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::gpuJson() const
{
    return QJsonDocument(QJsonObject::fromVariantMap(m_boardBackend->gpuBenchmarkStatus()))
        .toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::cameraJson() const
{
    return QJsonDocument(QJsonObject::fromVariantMap(m_boardBackend->webCameraPreviewStatus()))
        .toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::wifiJson() const
{
    QJsonObject body = QJsonObject::fromVariantMap(m_boardBackend->wifiManagerStatus());
    body.insert(QStringLiteral("accessPoints"),
                QJsonArray::fromVariantList(m_boardBackend->wifiAccessPoints()));
    body.insert(QStringLiteral("busy"), m_boardBackend->wifiManagerBusy());
    return QJsonDocument(body).toJson(QJsonDocument::Indented);
}

QJsonObject LocalWebServer::npuYoloStatusObject() const
{
    QJsonObject body;
    body.insert(QStringLiteral("serviceAvailable"), m_npuYoloDemo != nullptr);
    if (!m_npuYoloDemo) {
        body.insert(QStringLiteral("available"), false);
        body.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
        body.insert(QStringLiteral("message"), QStringLiteral("当前迅为开发板测试平台进程未提供 NPU YOLO 服务"));
        body.insert(QStringLiteral("error"), QStringLiteral("当前迅为开发板测试平台进程未提供 NPU YOLO 服务"));
        body.insert(QStringLiteral("running"), false);
        body.insert(QStringLiteral("canStart"), false);
        body.insert(QStringLiteral("canStop"), false);
        body.insert(QStringLiteral("profileSupported"), false);
        body.insert(QStringLiteral("previewAvailable"), false);
        return body;
    }

    const QVariantMap state = m_npuYoloDemo->status();
    const QVariantMap assets = state.value(QStringLiteral("assets")).toMap();
    const QVariantMap runtime = state.value(QStringLiteral("runtime")).toMap();
    const QVariantMap npuLoadState = state.value(QStringLiteral("npuLoad")).toMap();
    QList<int> npuCoreIds = boundedNpuCoreIds(state.value(QStringLiteral("npuCoreIds")).toList());
    if (npuCoreIds.isEmpty()) {
        npuCoreIds = boundedNpuCoreIds(npuLoadState.value(QStringLiteral("coreIds")).toList());
    }
    if (npuCoreIds.isEmpty()) {
        npuCoreIds = boundedNpuCoreIdsFromReadings(npuLoadState.value(QStringLiteral("cores")).toList());
    }
    const int npuWorkerCount = qBound(0,
        state.value(QStringLiteral("npuWorkerCount")).toInt(),
        kMaximumNpuWorkerCount);
    if (npuCoreIds.isEmpty()) {
        for (int coreId = 0; coreId < npuWorkerCount; ++coreId) {
            npuCoreIds.append(coreId);
        }
    }
    const QString fallbackMessage = QStringLiteral("NPU YOLO sample status is available");

    body.insert(QStringLiteral("available"), state.value(QStringLiteral("available")).toBool());
    body.insert(QStringLiteral("status"), state.value(QStringLiteral("status")).toString());
    body.insert(QStringLiteral("message"), boundedScrubbedText(state.value(QStringLiteral("message")).toString(), fallbackMessage));
    const QString error = boundedScrubbedText(state.value(QStringLiteral("error")).toString());
    if (!error.isEmpty()) {
        body.insert(QStringLiteral("error"), error);
    }
    body.insert(QStringLiteral("mode"), state.value(QStringLiteral("mode")).toString());
    body.insert(QStringLiteral("videoLoop"), state.value(QStringLiteral("videoLoop")).toBool());
    body.insert(QStringLiteral("running"), state.value(QStringLiteral("running")).toBool());
    body.insert(QStringLiteral("canStart"), state.value(QStringLiteral("canStart")).toBool());
    body.insert(QStringLiteral("canStop"), state.value(QStringLiteral("canStop")).toBool());
    body.insert(QStringLiteral("profileSupported"), state.value(QStringLiteral("profileSupported")).toBool());
    body.insert(QStringLiteral("profileId"), state.value(QStringLiteral("profileId")).toString());
    body.insert(QStringLiteral("profileLabel"), state.value(QStringLiteral("profileLabel")).toString());
    body.insert(QStringLiteral("progress"), qBound(0.0, state.value(QStringLiteral("progress")).toDouble(), 1.0));
    body.insert(QStringLiteral("progressPercent"), qBound(0, state.value(QStringLiteral("progressPercent")).toInt(), 100));
    body.insert(QStringLiteral("framesTotal"), qMax(0, state.value(QStringLiteral("framesTotal")).toInt()));
    body.insert(QStringLiteral("framesProcessed"), qMax(0, state.value(QStringLiteral("framesProcessed")).toInt()));
    body.insert(QStringLiteral("detectionsTotal"), qMax(0, state.value(QStringLiteral("detectionsTotal")).toInt()));
    body.insert(QStringLiteral("detectionsLastFrame"), qMax(0, state.value(QStringLiteral("detectionsLastFrame")).toInt()));
    body.insert(QStringLiteral("inferenceMs"), qMax(0.0, state.value(QStringLiteral("inferenceMs")).toDouble()));
    body.insert(QStringLiteral("averageInferenceMs"), qMax(0.0, state.value(QStringLiteral("averageInferenceMs")).toDouble()));
    body.insert(QStringLiteral("fps"), qMax(0.0, state.value(QStringLiteral("fps")).toDouble()));
    body.insert(QStringLiteral("elapsedMs"), qMax(0, state.value(QStringLiteral("elapsedMs")).toInt()));
    body.insert(QStringLiteral("executionMode"), boundedScrubbedText(
                    state.value(QStringLiteral("executionMode")).toString(), QStringLiteral("unavailable")));
    body.insert(QStringLiteral("npuWorkerCount"), npuWorkerCount);
    body.insert(QStringLiteral("npuTelemetryCoreCount"), qBound(
                    0, state.value(QStringLiteral("npuTelemetryCoreCount")).toInt(),
                    kMaximumNpuCoreCount));
    body.insert(QStringLiteral("npuWorkersPinned"),
                state.value(QStringLiteral("npuWorkersPinned")).toBool());
    QJsonArray exposedCoreIds;
    for (const int coreId : npuCoreIds) {
        exposedCoreIds.append(coreId);
    }
    body.insert(QStringLiteral("npuCoreIds"), exposedCoreIds);
    body.insert(QStringLiteral("framesSubmitted"), qMax(0, state.value(QStringLiteral("framesSubmitted")).toInt()));
    body.insert(QStringLiteral("framesQueued"), qMax(0, state.value(QStringLiteral("framesQueued")).toInt()));
    body.insert(QStringLiteral("framesDropped"), qMax(0, state.value(QStringLiteral("framesDropped")).toInt()));
    QJsonArray framesByNpuCore;
    const QVariantList framesByNpuCoreState = state.value(QStringLiteral("framesByNpuCore")).toList();
    for (int workerIndex = 0; workerIndex < npuWorkerCount; ++workerIndex) {
        framesByNpuCore.append(qMax(0, framesByNpuCoreState.value(workerIndex).toInt()));
    }
    body.insert(QStringLiteral("framesByNpuCore"), framesByNpuCore);
    body.insert(QStringLiteral("startedAt"), state.value(QStringLiteral("startedAt")).toString());
    body.insert(QStringLiteral("finishedAt"), state.value(QStringLiteral("finishedAt")).toString());
    body.insert(QStringLiteral("updatedAt"), state.value(QStringLiteral("updatedAt")).toString());
    body.insert(QStringLiteral("previewAvailable"), !validatedNpuPreviewPath(state).isEmpty());
    body.insert(QStringLiteral("previewSerial"), static_cast<double>(state.value(QStringLiteral("previewSerial")).toULongLong()));

    QJsonObject assetState;
    assetState.insert(QStringLiteral("ready"), assets.value(QStringLiteral("available")).toBool());
    assetState.insert(QStringLiteral("modelReady"), assets.value(QStringLiteral("modelAvailable")).toBool());
    assetState.insert(QStringLiteral("labelsReady"), assets.value(QStringLiteral("labelsAvailable")).toBool());
    assetState.insert(QStringLiteral("videoReady"), assets.value(QStringLiteral("videoAvailable")).toBool());
    body.insert(QStringLiteral("assets"), assetState);

    QJsonObject runtimeState;
    runtimeState.insert(QStringLiteral("compiled"), runtime.value(QStringLiteral("compiled")).toBool());
    runtimeState.insert(QStringLiteral("rk3588"), runtime.value(QStringLiteral("rk3588")).toBool());
    runtimeState.insert(QStringLiteral("platformId"), boundedScrubbedText(
                            runtime.value(QStringLiteral("platformId")).toString()));
    runtimeState.insert(QStringLiteral("expectedPlatformId"), boundedScrubbedText(
                            runtime.value(QStringLiteral("expectedPlatformId")).toString()));
    runtimeState.insert(QStringLiteral("profileMatchesDeviceTree"),
                        runtime.value(QStringLiteral("profileMatchesDeviceTree")).toBool());
    runtimeState.insert(QStringLiteral("rknpuDevice"), runtime.value(QStringLiteral("rknpuDevice")).toBool());
    runtimeState.insert(QStringLiteral("rknpuDebugNode"), runtime.value(QStringLiteral("rknpuDebugNode")).toBool());
    runtimeState.insert(QStringLiteral("platform"), runtime.value(QStringLiteral("platform")).toString());
    body.insert(QStringLiteral("runtime"), runtimeState);

    // Expose only the bounded, validated telemetry shape. The privileged helper
    // owns the debugfs read; the browser never receives a local path, helper
    // stderr, or arbitrary process output.
    QJsonObject npuLoad;
    npuLoad.insert(QStringLiteral("available"), npuLoadState.value(QStringLiteral("available")).toBool());
    // Compatibility field: older Web clients use this as the logical metric
    // name. Keep the actual reader/provider in a separate sanitized field.
    npuLoad.insert(QStringLiteral("source"), QStringLiteral("rknpu-debugfs"));
    npuLoad.insert(QStringLiteral("provider"), boundedScrubbedText(
                       npuLoadState.value(QStringLiteral("source")).toString(),
                       QStringLiteral("unavailable")));
    npuLoad.insert(QStringLiteral("message"), boundedScrubbedText(
                       npuLoadState.value(QStringLiteral("message")).toString(),
                       QStringLiteral("RKNPU core load is unavailable")));
    npuLoad.insert(QStringLiteral("sampleIntervalMs"), qBound(250,
                   npuLoadState.value(QStringLiteral("sampleIntervalMs")).toInt(), 10000));
    npuLoad.insert(QStringLiteral("executionMode"), boundedScrubbedText(
                   npuLoadState.value(QStringLiteral("executionMode")).toString(),
                   QStringLiteral("unavailable")));
    npuLoad.insert(QStringLiteral("coreIds"), exposedCoreIds);
    QJsonArray assignedCoreIds;
    const QVariantList assignedCoreIdsState = npuLoadState.value(QStringLiteral("assignedCoreIds")).toList();
    for (const int coreId : npuCoreIds) {
        if (assignedCoreIdsState.contains(coreId)) {
            assignedCoreIds.append(coreId);
        }
    }
    npuLoad.insert(QStringLiteral("assignedCoreIds"), assignedCoreIds);
    QJsonArray npuCores;
    const QVariantList npuCoresState = npuLoadState.value(QStringLiteral("cores")).toList();
    for (const int coreId : npuCoreIds) {
        bool hasPercent = false;
        double percent = -1.0;
        for (const QVariant &coreValue : npuCoresState) {
            const QVariantMap core = coreValue.toMap();
            if (core.value(QStringLiteral("core")).toInt() != coreId) {
                continue;
            }
            bool candidateOk = false;
            const double candidate = core.value(QStringLiteral("percent")).toDouble(&candidateOk);
            if (candidateOk && std::isfinite(candidate)
                && candidate >= 0.0 && candidate <= 100.0) {
                percent = candidate;
                hasPercent = true;
            }
            break;
        }
        QJsonObject core;
        core.insert(QStringLiteral("core"), coreId);
        core.insert(QStringLiteral("id"), coreId);
        core.insert(QStringLiteral("percent"), hasPercent ? percent : -1.0);
        npuCores.append(core);
    }
    npuLoad.insert(QStringLiteral("cores"), npuCores);
    bool averageOk = false;
    const double averagePercent = npuLoadState.value(
        QStringLiteral("averagePercent")).toDouble(&averageOk);
    npuLoad.insert(QStringLiteral("averagePercent"),
                   averageOk && std::isfinite(averagePercent)
                       && averagePercent >= 0.0 && averagePercent <= 100.0
                   ? averagePercent : -1.0);
    body.insert(QStringLiteral("npuLoad"), npuLoad);

    QJsonArray npuLoadHistory;
    const QVariantList npuLoadHistoryState = state.value(QStringLiteral("npuLoadHistory")).toList();
    const int firstHistoryIndex = qMax(0, npuLoadHistoryState.size() - 60);
    for (int index = firstHistoryIndex; index < npuLoadHistoryState.size(); ++index) {
        const QVariantMap pointState = npuLoadHistoryState.at(index).toMap();
        QJsonObject point;
        bool hasLoadPoint = false;
        for (const int coreId : npuCoreIds) {
            const QString key = QStringLiteral("core%1").arg(coreId);
            bool candidateOk = false;
            const double candidate = pointState.value(key).toDouble(&candidateOk);
            if (candidateOk && std::isfinite(candidate)
                && candidate >= 0.0 && candidate <= 100.0) {
                point.insert(key, candidate);
                hasLoadPoint = true;
            }
        }
        bool pointAverageOk = false;
        const double pointAverage = pointState.value(
            QStringLiteral("averagePercent")).toDouble(&pointAverageOk);
        if (pointAverageOk && std::isfinite(pointAverage)
            && pointAverage >= 0.0 && pointAverage <= 100.0) {
            point.insert(QStringLiteral("averagePercent"), pointAverage);
            hasLoadPoint = true;
        }
        if (hasLoadPoint) {
            npuLoadHistory.append(point);
        }
    }
    body.insert(QStringLiteral("npuLoadHistory"), npuLoadHistory);
    return body;
}

QByteArray LocalWebServer::npuYoloJson() const
{
    return QJsonDocument(npuYoloStatusObject()).toJson(QJsonDocument::Indented);
}

bool LocalWebServer::npuYoloPreviewJpeg(QByteArray *jpeg) const
{
    if (!jpeg) {
        return false;
    }
    jpeg->clear();
    if (!m_npuYoloDemo) {
        return false;
    }

    const QString previewPath = validatedNpuPreviewPath(m_npuYoloDemo->status());
    if (previewPath.isEmpty()) {
        return false;
    }

    QFile preview(previewPath);
    if (!preview.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray contents = preview.readAll();
    if (contents.size() < 4
        || static_cast<unsigned char>(contents.at(0)) != 0xff
        || static_cast<unsigned char>(contents.at(1)) != 0xd8
        || static_cast<unsigned char>(contents.at(2)) != 0xff
        || static_cast<unsigned char>(contents.at(contents.size() - 2)) != 0xff
        || static_cast<unsigned char>(contents.at(contents.size() - 1)) != 0xd9) {
        return false;
    }
    *jpeg = contents;
    return true;
}

bool LocalWebServer::webCameraPreviewJpeg(QByteArray *jpeg) const
{
    if (!jpeg || !m_boardBackend) {
        return false;
    }
    jpeg->clear();
    const QByteArray preview = m_boardBackend->webCameraPreviewJpeg();
    constexpr qint64 kMaximumPreviewBytes = 4LL * 1024LL * 1024LL;
    if (preview.size() < 4 || preview.size() > kMaximumPreviewBytes
        || static_cast<unsigned char>(preview.at(0)) != 0xff
        || static_cast<unsigned char>(preview.at(1)) != 0xd8
        || static_cast<unsigned char>(preview.at(preview.size() - 2)) != 0xff
        || static_cast<unsigned char>(preview.at(preview.size() - 1)) != 0xd9) {
        return false;
    }
    *jpeg = preview;
    return true;
}

QVariantMap LocalWebServer::readinessData() const
{
    return cachedReadinessObject().toVariantMap();
}

QJsonObject LocalWebServer::cachedReadinessObject() const
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_readinessCacheMs > 0 && now - m_readinessCacheMs < 2000 && !m_readinessCache.isEmpty()) {
        return m_readinessCache;
    }
    m_readinessCache = readinessObject();
    m_readinessCacheMs = now;
    return m_readinessCache;
}

QJsonObject LocalWebServer::readinessObject() const
{
    m_systemInfo->refresh();

    const int cpu = qRound(m_systemInfo->cpuUsage());
    const int memory = qRound(m_systemInfo->memoryUsage());
    const int disk = qRound(m_systemInfo->diskUsage());
    const int hardwareCount = m_boardBackend->ethernetInterfaces().size()
        + m_boardBackend->canInterfaces().size()
        + m_boardBackend->cellularDevices().size()
        + m_boardBackend->serialDevices().size()
        + (m_boardBackend->backlightAvailable() ? 1 : 0);
    const QJsonObject provenance = boardProvenance(m_boardProfile);
    const bool targetBoard = provenance.value(QStringLiteral("targetBoardEvidence")).toBool();
    const bool armTarget = provenance.value(QStringLiteral("armEvidence")).toBool();
    const QString activeProfileLabel = provenanceProfileLabel(provenance);
    const QString activeSocFamily = provenance.value(QStringLiteral("socEvidence")).toString();
    const bool supportedOs = supportedRuntimeOs();
    const bool localhostOnly = m_bindAddress == QHostAddress::LocalHost || m_bindAddress == QHostAddress::LocalHostIPv6;
    const QJsonObject hardwareValidation = hardwareValidationSummary();
    const int validationFailCount = hardwareValidation.value(QStringLiteral("fail")).toInt();
    const bool validationPresent = hardwareValidation.value(QStringLiteral("present")).toBool();

    QJsonArray gates;
    auto addGate = [&gates](const QString &id, const QString &label, const QString &state, const QString &detail, int points) {
        QJsonObject gate;
        gate.insert(QStringLiteral("id"), id);
        gate.insert(QStringLiteral("label"), label);
        gate.insert(QStringLiteral("state"), state);
        gate.insert(QStringLiteral("detail"), detail);
        gate.insert(QStringLiteral("points"), points);
        gates.append(gate);
    };

    int score = 0;
    const int runtimePoints = (cpu < 85 && memory < 90 && disk < 92) ? 18 : 8;
    score += runtimePoints;
    addGate(QStringLiteral("runtime"), QStringLiteral("运行余量"), runtimePoints == 18 ? QStringLiteral("就绪") : QStringLiteral("观察"),
            QStringLiteral("CPU %1%，RAM %2%，磁盘 %3%，%4").arg(cpu).arg(memory).arg(disk).arg(m_systemInfo->temperatureText()), runtimePoints);

    const int hardwarePoints = hardwareCount >= 3 ? 18 : (hardwareCount > 0 ? 10 : 4);
    score += hardwarePoints;
    addGate(QStringLiteral("hardware"), QStringLiteral("%1 硬件入口").arg(activeProfileLabel), hardwarePoints == 18 ? QStringLiteral("就绪") : QStringLiteral("部分"),
            QStringLiteral("已发现 %1 个入口：以太网、CAN、4G、UART、背光").arg(hardwareCount), hardwarePoints);

    const int bridgePoints = running() ? 16 : 4;
    score += bridgePoints;
    addGate(QStringLiteral("web_bridge"), QStringLiteral("Qt + Web 交接"), running() ? QStringLiteral("就绪") : QStringLiteral("未启用"),
            running() ? QStringLiteral("%1 正在提供状态 JSON 和 POST 控制端点").arg(url()) : QStringLiteral("使用 --web 启动 LAN Web 控制台"), bridgePoints);

    const int targetPoints = targetBoard ? 14 : (armTarget ? 8 : 4);
    score += targetPoints;
    addGate(QStringLiteral("target"), QStringLiteral("%1 运行目标").arg(activeProfileLabel),
            targetBoard ? QStringLiteral("目标板") : (armTarget ? QStringLiteral("ARM 预览") : QStringLiteral("桌面预览")),
            QStringLiteral("架构 %1，板型 %2，板端证据 %3，Qt %4，%5")
                .arg(QSysInfo::currentCpuArchitecture(),
                     activeSocFamily.isEmpty() ? QStringLiteral("未识别") : activeSocFamily,
                     provenance.value(QStringLiteral("state")).toString(),
                     QString::fromLatin1(qVersion()),
                     m_systemInfo->kernelText()),
            targetPoints);

    const int osPoints = supportedOs ? 12 : 3;
    score += osPoints;
    addGate(QStringLiteral("os"), runtimeBaselineLabel(), supportedOs ? QStringLiteral("就绪") : QStringLiteral("复核"),
            QStringLiteral("%1，版本 %2").arg(runtimeProductName(), runtimeProductVersion()), osPoints);

    const int validationWarnCount = hardwareValidation.value(QStringLiteral("warn")).toInt();
    const int validationManualCount = hardwareValidation.value(QStringLiteral("manual")).toInt();
    const int validationPoints = validationPresent ? (validationFailCount > 0 ? 2 : (validationWarnCount > 0 || validationManualCount > 0 ? 6 : 12)) : 0;
    score += validationPoints;
    addGate(QStringLiteral("hardware_validation"), QStringLiteral("硬件验收证据"),
            validationPresent ? (validationFailCount > 0 ? QStringLiteral("失败") : (validationWarnCount > 0 || validationManualCount > 0 ? QStringLiteral("需复核") : QStringLiteral("已留存"))) : QStringLiteral("未运行"),
            validationPresent ? QStringLiteral("PASS %1，WARN %2，FAIL %3，MANUAL %4")
                                    .arg(hardwareValidation.value(QStringLiteral("pass")).toInt())
                                    .arg(validationWarnCount)
                                    .arg(validationFailCount)
                                    .arg(validationManualCount)
                              : hardwareValidationInstruction(provenance),
            validationPoints);

    const int safetyPoints = localhostOnly ? 6 : 10;
    score += safetyPoints;
    addGate(QStringLiteral("safety"), QStringLiteral("板端安全姿态"), localhostOnly ? QStringLiteral("本机限制") : QStringLiteral("局域网演示"),
            localhostOnly ? QStringLiteral("Web 控制台当前仅绑定 loopback，所有 API 仍要求 Bearer Token")
                          : QStringLiteral("Web 控制台通过 %1 提供局域网访问，所有 API 均由 Bearer Token 保护").arg(url()),
            safetyPoints);

    QJsonObject body;
    body.insert(QStringLiteral("score"), qBound(0, score, 100));
    body.insert(QStringLiteral("target"), supportedTargetText());
    body.insert(QStringLiteral("supportedTarget"), supportedTargetText());
    body.insert(QStringLiteral("compatibilityScope"), compatibilityScopeText());
    body.insert(QStringLiteral("targetLabel"), activeProfileLabel);
    body.insert(QStringLiteral("profileLabel"), activeProfileLabel);
    body.insert(QStringLiteral("socFamily"), activeSocFamily);
    body.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    body.insert(QStringLiteral("gates"), gates);
    body.insert(QStringLiteral("boardProvenance"), provenance);
    body.insert(QStringLiteral("hardwareValidation"), hardwareValidation);
    body.insert(QStringLiteral("summary"), QStringLiteral("原生 Qt 桌面、局域网 Web 控制台、板端遥测和发布门禁由同一个进程提供。"));
    return body;
}

QByteArray LocalWebServer::readinessJson() const
{
    return QJsonDocument(cachedReadinessObject()).toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::manifestJson() const
{
    auto readFirstExistingFile = [](const QStringList &paths, const QString &fallback) {
        for (const QString &path : paths) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                const QString value = QString::fromUtf8(file.readAll()).trimmed();
                if (!value.isEmpty()) {
                    return value;
                }
            }
        }
        return fallback;
    };

    const QString appDir = QCoreApplication::applicationDirPath();
    const QString version = readFirstExistingFile({
            QDir::cleanPath(appDir + QStringLiteral("/../share/boardlab/VERSION")),
            QDir::cleanPath(appDir + QStringLiteral("/../VERSION")),
            QDir::cleanPath(QDir::currentPath() + QStringLiteral("/VERSION")),
        },
        QStringLiteral("0.0.0-dev"));
    const QJsonObject provenance = boardProvenance(m_boardProfile);
    const QString activeProfileLabel = provenanceProfileLabel(provenance);
    const QString activeSocFamily = provenance.value(QStringLiteral("socEvidence")).toString();
    const QString validationInstruction = hardwareValidationInstruction(provenance);

    QJsonArray endpoints;
    auto addEndpoint = [&endpoints](const QString &method, const QString &path, const QString &purpose) {
        QJsonObject endpoint;
        endpoint.insert(QStringLiteral("method"), method);
        endpoint.insert(QStringLiteral("path"), path);
        endpoint.insert(QStringLiteral("purpose"), purpose);
        endpoints.append(endpoint);
    };
    addEndpoint(QStringLiteral("GET"), QStringLiteral("/healthz"), QStringLiteral("Bridge health and bind metadata"));
    addEndpoint(QStringLiteral("GET"), QStringLiteral("/api/system"), QStringLiteral("CPU, memory, disk, thermal, network and OS telemetry"));
    addEndpoint(QStringLiteral("GET"), QStringLiteral("/api/board"), QStringLiteral("%1 peripheral inventory from the Qt backend").arg(activeProfileLabel));
    addEndpoint(QStringLiteral("GET"), QStringLiteral("/api/npu-yolo"), QStringLiteral("Sanitized fixed YOLOv8 NPU sample status"));
    addEndpoint(QStringLiteral("GET"), QStringLiteral("/api/npu-yolo/preview"), QStringLiteral("Current server-owned annotated JPEG preview"));
    addEndpoint(QStringLiteral("GET"), QStringLiteral("/api/readiness"), QStringLiteral("Release readiness score and live gate states"));
    addEndpoint(QStringLiteral("GET"), QStringLiteral("/api/manifest"), QStringLiteral("Project feature map and delivery contract"));
    addEndpoint(QStringLiteral("GET"), QStringLiteral("/api/handoff"), QStringLiteral("%1 demo runbook and evidence bundle checklist").arg(activeProfileLabel));
    addEndpoint(QStringLiteral("POST"), QStringLiteral("/api/control"), QStringLiteral("Submit hardware actions and fixed parameter-free NPU YOLO discover/start/stop actions"));
    addEndpoint(QStringLiteral("POST"), QStringLiteral("/api/files/upload?filename=<basename>&directory=<absolute-directory>"),
                QStringLiteral("Bearer-protected create-only raw upload, limited to 64 MiB under BOARDLAB_UPLOAD_ROOT or /var/lib/boardlab/uploads"));

    QJsonArray features;
    auto addFeature = [&features](const QString &name, const QString &surface, const QString &detail) {
        QJsonObject feature;
        feature.insert(QStringLiteral("name"), name);
        feature.insert(QStringLiteral("surface"), surface);
        feature.insert(QStringLiteral("detail"), detail);
        features.append(feature);
    };
    addFeature(QStringLiteral("Topeet development-board test shell"), QStringLiteral("QML"), QStringLiteral("Direct home screen, independent demo apps and QA-only engineering overlays"));
    addFeature(QStringLiteral("RK Desk"), QStringLiteral("QML/C++"), QStringLiteral("Board-grade overview for %1 telemetry, hardware surfaces and bring-up workflow").arg(activeProfileLabel));
    addFeature(QStringLiteral("Independent demo apps"), QStringLiteral("QML/C++"), QStringLiteral("Standalone GPIO, UART with full serial parameters, Ethernet, CAN/CAN FD, 4G, camera, HDMIIN, watchdog, audio, beep, backlight and files surfaces"));
    addFeature(QStringLiteral("Qt Web Console"), QStringLiteral("QML/Web"), QStringLiteral("Native Web control surface without QtWebEngine or Chromium"));
    addFeature(QStringLiteral("LAN Web control"), QStringLiteral("QtNetwork"), QStringLiteral("Single-process LAN HTTP dashboard, JSON API and hardware control dispatcher"));
    addFeature(QStringLiteral("YOLOv8 NPU sample"), QStringLiteral("QML/Web"), QStringLiteral("Runtime-selected SoC model with sanitized status, server-owned preview and no caller-provided model or video paths"));
    addFeature(QStringLiteral("Handoff runbook"), QStringLiteral("QML/Web/API"), QStringLiteral("Machine-readable %1 demo steps, evidence bundle and board safety notes").arg(activeProfileLabel));
    addFeature(QStringLiteral("Deployment evidence"), QStringLiteral("scripts/docs"), QStringLiteral("Build, install layout and board-profile hardware validation"));

    QJsonArray targets;
    targets.append(supportedTargetText());
    targets.append(compatibilityScopeText());
    targets.append(activeProfileLabel);
    targets.append(QStringLiteral("Qt 5.12+ / Qt Quick Controls 2"));
    targets.append(QStringLiteral("Buildroot: Weston/Wayland; Ubuntu/Debian: X11/xcb"));
    targets.append(QStringLiteral("LAN-visible Web control posture"));

    QJsonArray releaseEvidence;
    auto addEvidence = [&releaseEvidence](const QString &name, const QString &kind, const QString &path, const QString &source) {
        QJsonObject evidence;
        evidence.insert(QStringLiteral("name"), name);
        evidence.insert(QStringLiteral("kind"), kind);
        evidence.insert(QStringLiteral("path"), path);
        evidence.insert(QStringLiteral("source"), source);
        releaseEvidence.append(evidence);
    };
    addEvidence(QStringLiteral("Build command"), QStringLiteral("command"), QStringLiteral("./scripts/build.sh"), QStringLiteral("source tree"));
    addEvidence(QStringLiteral("Install layout"), QStringLiteral("command"), QStringLiteral("./scripts/deploy.sh"), QStringLiteral("source tree"));
    addEvidence(QStringLiteral("Board hardware validation"), QStringLiteral("report"), QStringLiteral("/tmp/boardlab-hardware-validation.txt and .json"), validationInstruction);
    addEvidence(QStringLiteral("Demo handoff bundle"), QStringLiteral("api"), QStringLiteral("/api/handoff"), QStringLiteral("Qt Web / Handoff"));

    QJsonObject package;
    package.insert(QStringLiteral("name"), QStringLiteral("boardlab install layout"));
    package.insert(QStringLiteral("version"), version);
    package.insert(QStringLiteral("arch"), QSysInfo::currentCpuArchitecture());
    package.insert(QStringLiteral("prefix"), QStringLiteral("/opt/boardlab"));

    QJsonObject body;
    body.insert(QStringLiteral("name"), QStringLiteral("迅为开发板测试平台"));
    body.insert(QStringLiteral("version"), version);
    body.insert(QStringLiteral("target"), supportedTargetText());
    body.insert(QStringLiteral("supportedTarget"), supportedTargetText());
    body.insert(QStringLiteral("compatibilityScope"), compatibilityScopeText());
    body.insert(QStringLiteral("targetLabel"), activeProfileLabel);
    body.insert(QStringLiteral("profileLabel"), activeProfileLabel);
    body.insert(QStringLiteral("socFamily"), activeSocFamily);
    body.insert(QStringLiteral("runtime"), supportedRuntimeText());
    body.insert(QStringLiteral("safety"), QStringLiteral("LAN Web APIs require a Bearer Token; control submits explicit POST actions to the Qt backend"));
    body.insert(QStringLiteral("boardProvenance"), provenance);
    body.insert(QStringLiteral("features"), features);
    body.insert(QStringLiteral("endpoints"), endpoints);
    body.insert(QStringLiteral("targets"), targets);
    body.insert(QStringLiteral("package"), package);
    body.insert(QStringLiteral("releaseEvidence"), releaseEvidence);
    body.insert(QStringLiteral("updatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return QJsonDocument(body).toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::handoffJson() const
{
    m_systemInfo->refresh();

    const QJsonObject validation = hardwareValidationSummary();
    const QJsonObject provenance = boardProvenance(m_boardProfile);
    const QString activeProfileLabel = provenanceProfileLabel(provenance);
    const QString activeSocFamily = provenance.value(QStringLiteral("socEvidence")).toString();
    const QString validationInstruction = hardwareValidationInstruction(provenance);
    const bool localhostOnly = m_bindAddress == QHostAddress::LocalHost || m_bindAddress == QHostAddress::LocalHostIPv6;

    QJsonArray runbook;
    auto addStep = [&runbook](const QString &id, const QString &title, const QString &surface, const QString &action, const QString &evidence) {
        QJsonObject step;
        step.insert(QStringLiteral("id"), id);
        step.insert(QStringLiteral("title"), title);
        step.insert(QStringLiteral("surface"), surface);
        step.insert(QStringLiteral("action"), action);
        step.insert(QStringLiteral("evidence"), evidence);
        runbook.append(step);
    };
    addStep(QStringLiteral("boot"), QStringLiteral("Boot native shell"), QStringLiteral("Qt Quick"),
            QStringLiteral("Launch the Topeet development-board test platform in framed preview or %1 kiosk mode and confirm Home and independent demo apps render.").arg(activeProfileLabel),
            QStringLiteral("target-board observation"));
    addStep(QStringLiteral("telemetry"), QStringLiteral("Confirm live telemetry"), QStringLiteral("Qt/C++ backend"),
            QStringLiteral("Open the Topeet development-board test platform home and compare CPU, RAM, disk, temperature and active network values with the LAN dashboard."),
            QStringLiteral("/api/system and /api/board"));
    addStep(QStringLiteral("hardware"), QStringLiteral("Exercise hardware demos"), QStringLiteral("Independent demo apps"),
            QStringLiteral("Validate GPIO, UART parameters, Ethernet, CAN/CAN FD, 4G, camera, HDMIIN, watchdog, audio, beep and backlight in native or Web control surfaces."),
            QStringLiteral("/api/control and target-board observation"));
    addStep(QStringLiteral("web"), QStringLiteral("Capture Web handoff"), QStringLiteral("QtNetwork"),
            QStringLiteral("Start with --web, open the LAN dashboard or scan the QR code, then fetch readiness, manifest and handoff JSON."),
            QStringLiteral("/api/readiness, /api/manifest and /api/handoff"));
    addStep(QStringLiteral("install"), QStringLiteral("Install runtime layout"), QStringLiteral("scripts"),
            QStringLiteral("Run deploy or autorun, then %1.").arg(validationInstruction),
            QStringLiteral("/tmp/boardlab-hardware-validation.json"));

    QJsonArray evidence;
    auto addEvidence = [&evidence](const QString &name, const QString &path, const QString &required) {
        QJsonObject item;
        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("path"), path);
        item.insert(QStringLiteral("required"), required);
        evidence.append(item);
    };
    addEvidence(QStringLiteral("Readiness endpoint"), QStringLiteral("/api/readiness"), QStringLiteral("yes"));
    addEvidence(QStringLiteral("Manifest endpoint"), QStringLiteral("/api/manifest"), QStringLiteral("yes"));
    addEvidence(QStringLiteral("Handoff endpoint"), QStringLiteral("/api/handoff"), QStringLiteral("yes"));
    addEvidence(QStringLiteral("Hardware validation JSON"), QStringLiteral("/tmp/boardlab-hardware-validation.json"), QStringLiteral("target-board"));

    QJsonArray safety;
    safety.append(QStringLiteral("Web control actions require POST /api/control and use the same asynchronous backend as the native Qt tools."));
    safety.append(QStringLiteral("All /api/* requests require an Authorization: Bearer Token header; the login page and /healthz remain public."));
    safety.append(localhostOnly ? QStringLiteral("Bridge is in restricted local mode for this run because it is bound to a loopback address.")
                                : QStringLiteral("Bridge is LAN-visible for trusted demo networks; scan the QR code or open the LAN URL."));
    safety.append(QStringLiteral("Watchdog reset requires an explicit confirm flag before the backend receives the request."));
    safety.append(QStringLiteral("Hardware validation can be needs-review when WARN or MANUAL items remain; do not present that as fully passed."));

    QJsonObject body;
    body.insert(QStringLiteral("target"), supportedTargetText());
    body.insert(QStringLiteral("supportedTarget"), supportedTargetText());
    body.insert(QStringLiteral("compatibilityScope"), compatibilityScopeText());
    body.insert(QStringLiteral("targetLabel"), activeProfileLabel);
    body.insert(QStringLiteral("profileLabel"), activeProfileLabel);
    body.insert(QStringLiteral("socFamily"), activeSocFamily);
    body.insert(QStringLiteral("generatedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    body.insert(QStringLiteral("bridgeUrl"), url());
    body.insert(QStringLiteral("readinessEndpoint"), QStringLiteral("/api/readiness"));
    body.insert(QStringLiteral("manifestEndpoint"), QStringLiteral("/api/manifest"));
    body.insert(QStringLiteral("runbook"), runbook);
    body.insert(QStringLiteral("evidenceBundle"), evidence);
    body.insert(QStringLiteral("safetyNotes"), safety);
    body.insert(QStringLiteral("boardProvenance"), provenance);
    body.insert(QStringLiteral("hardwareValidation"), validation);
    body.insert(QStringLiteral("summary"), QStringLiteral("Field handoff plan for demonstrating the native Qt shell, %1 hardware tools and LAN Web control as one deliverable.").arg(activeProfileLabel));
    return QJsonDocument(body).toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::healthJson() const
{
    QJsonObject body;
    body.insert(QStringLiteral("ok"), true);
    body.insert(QStringLiteral("service"), QStringLiteral("boardlab-local-web"));
    body.insert(QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    body.insert(QStringLiteral("bindAddress"), bindAddress());
    body.insert(QStringLiteral("port"), static_cast<int>(port()));
    body.insert(QStringLiteral("url"), url());
    body.insert(QStringLiteral("authenticationRequired"), true);
    body.insert(QStringLiteral("authenticationScheme"), QStringLiteral("Bearer"));
    return QJsonDocument(body).toJson(QJsonDocument::Indented);
}

QByteArray LocalWebServer::jsonEscape(const QString &value) const
{
    QByteArray out = "\"";
    const QString text = value;
    for (const QChar ch : text) {
        switch (ch.unicode()) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += QString(ch).toUtf8();
            break;
        }
    }
    out += "\"";
    return out;
}

QByteArray LocalWebServer::jsonStringList(const QStringList &items) const
{
    QByteArray out = "[";
    for (int i = 0; i < items.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += jsonEscape(items.at(i));
    }
    out += "]";
    return out;
}
