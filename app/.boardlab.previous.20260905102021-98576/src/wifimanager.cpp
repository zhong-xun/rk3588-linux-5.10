#include "wifimanager.h"

#include "qtcompat.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>

#include <QFile>
#include <QTextStream>

#include <algorithm>
#include <grp.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr int kRadioTimeoutMs = 5000;
constexpr int kDeviceStatusTimeoutMs = 7000;
constexpr int kIpv4StatusTimeoutMs = 5000;
constexpr int kScanTimeoutMs = 20000;
constexpr int kAccessPointTimeoutMs = 10000;
constexpr int kConnectTimeoutMs = 35000;
constexpr int kDisconnectTimeoutMs = 25000;
constexpr auto kRestrictedWifiHelperPath = "/usr/lib/boardlab/boardlab-wifi";
constexpr auto kRestrictedWifiHelperReadyPath = "/etc/boardlab/wifi-helper.enabled";
constexpr auto kRestrictedWifiHelperGroup = "boardlab-wifi";

bool currentProcessInGroup(const char *groupName)
{
    const group *group = getgrnam(groupName);
    if (!group) {
        return false;
    }
    if (getegid() == group->gr_gid) {
        return true;
    }

    const int count = getgroups(0, nullptr);
    if (count <= 0) {
        return false;
    }
    std::vector<gid_t> groups(static_cast<size_t>(count));
    if (getgroups(count, groups.data()) != count) {
        return false;
    }
    return std::find(groups.cbegin(), groups.cend(), group->gr_gid) != groups.cend();
}

bool processIsRoot()
{
    return geteuid() == 0;
}

QString readFirstLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QTextStream stream(&file);
    return stream.readLine().trimmed();
}

bool pathContainsInterfaceClaim(const QString &path, const QString &interfaceName)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QString needle = QStringLiteral("iface %1 ").arg(interfaceName);
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.isEmpty()) {
            continue;
        }
        if (line.startsWith(needle) || line.contains(QStringLiteral(" %1 ").arg(interfaceName))) {
            return true;
        }
    }
    return false;
}

bool ifupdownClaimsInterface(const QString &interfaceName)
{
    if (interfaceName.isEmpty() || !WifiManager::isSafeInterfaceName(interfaceName)) {
        return false;
    }
    if (pathContainsInterfaceClaim(QStringLiteral("/etc/network/interfaces"), interfaceName)) {
        return true;
    }
    if (pathContainsInterfaceClaim(
            QStringLiteral("/etc/network/interfaces.d/%1").arg(interfaceName), interfaceName)) {
        return true;
    }
    // Rockchip images often leave a renamed manual claim after BoardLab prep.
    if (QFile::exists(QStringLiteral("/etc/network/interfaces.d/%1.disabled-boardlab")
                          .arg(interfaceName))) {
        return false;
    }
    return false;
}

bool dhcpcdClaimsInterface(const QString &interfaceName)
{
    if (interfaceName.isEmpty() || !WifiManager::isSafeInterfaceName(interfaceName)) {
        return false;
    }
    QFile file(QStringLiteral("/etc/dhcpcd.conf"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    bool denied = false;
    bool explicitlyConfigured = false;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1Char('#')) || line.isEmpty()) {
            continue;
        }
        if (line.startsWith(QStringLiteral("denyinterfaces"))) {
            const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                  boardlab::kSkipEmptyParts);
            if (tokens.contains(interfaceName)) {
                denied = true;
            }
            continue;
        }
        // interface wlan0 / allowinterfaces wlan0 style claims.
        if (line.startsWith(QStringLiteral("interface "))
            || line.startsWith(QStringLiteral("allowinterfaces"))) {
            const QStringList tokens = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                                  boardlab::kSkipEmptyParts);
            if (tokens.contains(interfaceName)) {
                explicitlyConfigured = true;
            }
        }
    }
    if (denied) {
        return false;
    }
    return explicitlyConfigured;
}

bool interfaceOperStateDown(const QString &interfaceName)
{
    if (interfaceName.isEmpty() || !WifiManager::isSafeInterfaceName(interfaceName)) {
        return false;
    }
    const QString operstate = readFirstLine(
        QStringLiteral("/sys/class/net/%1/operstate").arg(interfaceName));
    return operstate.compare(QStringLiteral("down"), Qt::CaseInsensitive) == 0
        || operstate.compare(QStringLiteral("lowerlayerdown"), Qt::CaseInsensitive) == 0;
}

bool rfkillBlocksWifi()
{
    QDir dir(QStringLiteral("/sys/class/rfkill"));
    if (!dir.exists()) {
        return false;
    }
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &entry : entries) {
        const QString type = readFirstLine(entry.absoluteFilePath() + QStringLiteral("/type")).toLower();
        if (type != QStringLiteral("wlan") && type != QStringLiteral("wifi")) {
            continue;
        }
        const QString soft = readFirstLine(entry.absoluteFilePath() + QStringLiteral("/soft"));
        const QString hard = readFirstLine(entry.absoluteFilePath() + QStringLiteral("/hard"));
        if (soft == QStringLiteral("1") || hard == QStringLiteral("1")) {
            return true;
        }
    }
    return false;
}

QString nowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool isWifiType(const QString &type)
{
    return type.trimmed().compare(QStringLiteral("wifi"), Qt::CaseInsensitive) == 0;
}

bool isDisabledDeviceState(const QString &state)
{
    const QString normalized = state.trimmed().toLower();
    return normalized == QStringLiteral("unavailable")
        || normalized == QStringLiteral("unmanaged")
        || normalized == QStringLiteral("disabled");
}

bool isBssid(const QString &value)
{
    static const QRegularExpression expression(
        QStringLiteral("^[0-9A-Fa-f]{2}(?::[0-9A-Fa-f]{2}){5}$"));
    return expression.match(value.trimmed()).hasMatch();
}

bool isControlCharacter(QChar character)
{
    return character == QChar::Null
        || character.category() == QChar::Other_Control
        || character.category() == QChar::Other_Format;
}

QVariantMap messageResult(const QString &code, const QString &message, const QString &messageEn)
{
    QVariantMap result;
    result.insert(QStringLiteral("errorCode"), code);
    result.insert(QStringLiteral("message"), message);
    result.insert(QStringLiteral("messageEn"), messageEn);
    return result;
}

bool accessPointLess(const QVariant &leftVariant, const QVariant &rightVariant)
{
    const QVariantMap left = leftVariant.toMap();
    const QVariantMap right = rightVariant.toMap();
    const bool leftActive = left.value(QStringLiteral("active")).toBool();
    const bool rightActive = right.value(QStringLiteral("active")).toBool();
    if (leftActive != rightActive) {
        return leftActive;
    }

    const int leftSignal = left.value(QStringLiteral("signal")).toInt();
    const int rightSignal = right.value(QStringLiteral("signal")).toInt();
    if (leftSignal != rightSignal) {
        return leftSignal > rightSignal;
    }
    return QString::localeAwareCompare(left.value(QStringLiteral("ssid")).toString(),
                                       right.value(QStringLiteral("ssid")).toString()) < 0;
}

} // namespace

WifiManager::WifiManager(QObject *parent)
    : WifiManager(QString(), parent, 300000)
{
}

WifiManager::WifiManager(const QString &nmcliProgram, QObject *parent,
                         int scanCacheMaximumAgeMs)
    : QObject(parent)
    , m_nmcliProgram(nmcliProgram.trimmed())
    , m_process(new QProcess(this))
    , m_timeoutTimer(new QTimer(this))
    , m_scanCacheMaximumAgeMs(qMax(0, scanCacheMaximumAgeMs))
{
    m_timeoutTimer->setSingleShot(true);
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &WifiManager::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &WifiManager::onProcessError);
    connect(m_timeoutTimer, &QTimer::timeout, this, &WifiManager::onCommandTimeout);

    m_state.insert(QStringLiteral("available"), false);
    m_state.insert(QStringLiteral("nmcliAvailable"), !resolvedNmcliProgram().isEmpty());
    m_state.insert(QStringLiteral("authorizationMode"), authorizationModeName());
    m_state.insert(QStringLiteral("authorizationHelperAvailable"), usesRestrictedWifiHelper());
    m_state.insert(QStringLiteral("radioEnabled"), QVariant());
    m_state.insert(QStringLiteral("devices"), QVariantList());
    m_state.insert(QStringLiteral("selectedDevice"), QString());
    // These aliases intentionally stay at the top level so both QML and the
    // Web API can present the newly assigned address without having to infer
    // it from a device row.  `ipv4` / `ipAddress` are plain addresses; the
    // CIDR form is available separately as `ipv4Cidr`.
    m_state.insert(QStringLiteral("ipv4"), QString());
    m_state.insert(QStringLiteral("ipv4Address"), QString());
    m_state.insert(QStringLiteral("ipAddress"), QString());
    m_state.insert(QStringLiteral("ipv4Cidr"), QString());
    m_state.insert(QStringLiteral("ipv4Prefix"), QVariant());
    m_state.insert(QStringLiteral("ipv4Addresses"), QVariantList());
    m_state.insert(QStringLiteral("ipv4Assigned"), false);
    m_state.insert(QStringLiteral("ipv4Status"), QStringLiteral("not-read"));
    m_state.insert(QStringLiteral("ipv4ReadError"), QString());
    m_state.insert(QStringLiteral("status"), QStringLiteral("idle"));
    m_state.insert(QStringLiteral("message"), QStringLiteral("尚未读取 Wi-Fi 状态。"));
    m_state.insert(QStringLiteral("messageEn"), QStringLiteral("Wi-Fi status has not been read yet."));
    m_state.insert(QStringLiteral("errorCode"), QString());
    m_state.insert(QStringLiteral("accessPointCount"), 0);
    m_state.insert(QStringLiteral("scanCacheValid"), false);
    m_state.insert(QStringLiteral("scanCacheExpiresAt"), QString());
    m_state.insert(QStringLiteral("webSessionMayDisconnect"), false);
    m_state.insert(QStringLiteral("networkSwitchWarning"), QString());
    m_state.insert(QStringLiteral("networkSwitchWarningEn"), QString());
    m_state.insert(QStringLiteral("updatedAt"), nowIso());
    m_state.insert(QStringLiteral("busy"), false);
}

WifiManager::~WifiManager()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    m_pendingPassword.clear();
}

QVariantMap WifiManager::state() const
{
    QVariantMap snapshot = m_state;
    const bool cacheFresh = !m_scanCacheInterface.isEmpty()
        && isScanCacheFresh(m_scanCacheTimestamp, QDateTime::currentDateTimeUtc(), m_scanCacheMaximumAgeMs);
    snapshot.insert(QStringLiteral("scanCacheValid"), cacheFresh);
    return snapshot;
}

QVariantList WifiManager::accessPoints() const
{
    return m_accessPoints;
}

bool WifiManager::busy() const
{
    return m_busy;
}

QStringList WifiManager::splitTerseLine(const QString &line)
{
    QStringList fields;
    QString current;
    bool escaped = false;
    for (const QChar character : line) {
        if (escaped) {
            switch (character.unicode()) {
            case ':':
                current.append(QLatin1Char(':'));
                break;
            case '\\':
                current.append(QLatin1Char('\\'));
                break;
            case 'n':
                current.append(QLatin1Char('\n'));
                break;
            case 'r':
                current.append(QLatin1Char('\r'));
                break;
            case 't':
                current.append(QLatin1Char('\t'));
                break;
            default:
                // nmcli currently escapes ':' and '\\'.  Keep an unknown
                // escape lossless so a future nmcli value is not corrupted.
                current.append(QLatin1Char('\\'));
                current.append(character);
                break;
            }
            escaped = false;
            continue;
        }
        if (character == QLatin1Char('\\')) {
            escaped = true;
        } else if (character == QLatin1Char(':')) {
            fields.append(current);
            current.clear();
        } else {
            current.append(character);
        }
    }
    if (escaped) {
        current.append(QLatin1Char('\\'));
    }
    fields.append(current);
    return fields;
}

QVariantList WifiManager::parseWifiDeviceStatus(const QString &output)
{
    QVariantList devices;
    const QStringList lines = output.split(QLatin1Char('\n'), boardlab::kSkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QStringList fields = splitTerseLine(rawLine);
        if (fields.size() < 4 || !isWifiType(fields.at(1))) {
            continue;
        }

        const QString device = fields.at(0).trimmed();
        if (!isSafeInterfaceName(device)) {
            continue;
        }
        const QString state = fields.at(2).trimmed();
        QString connection = fields.at(3);
        if (connection == QStringLiteral("--")) {
            connection.clear();
        }

        QVariantMap item;
        item.insert(QStringLiteral("device"), device);
        item.insert(QStringLiteral("type"), QStringLiteral("wifi"));
        item.insert(QStringLiteral("state"), state);
        item.insert(QStringLiteral("connection"), connection);
        item.insert(QStringLiteral("connected"), state.compare(QStringLiteral("connected"), Qt::CaseInsensitive) == 0);
        item.insert(QStringLiteral("available"), !isDisabledDeviceState(state));
        item.insert(QStringLiteral("disabled"), isDisabledDeviceState(state));
        devices.append(item);
    }
    return devices;
}

QVariantList WifiManager::parseAccessPoints(const QString &output)
{
    QVariantList points;
    const QStringList lines = output.split(QLatin1Char('\n'), boardlab::kSkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QStringList fields = splitTerseLine(rawLine);
        if (fields.size() < 9) {
            continue;
        }

        bool signalOk = false;
        int signal = fields.at(7).trimmed().toInt(&signalOk);
        if (!signalOk || signal < 0 || signal > 100) {
            signal = -1;
        }
        const QString ssid = fields.at(1);
        const QString bssid = fields.at(2).trimmed();
        if (!isBssid(bssid)) {
            continue;
        }

        QVariantMap item;
        item.insert(QStringLiteral("id"), bssid);
        item.insert(QStringLiteral("active"), fields.at(0).trimmed() == QStringLiteral("*"));
        item.insert(QStringLiteral("ssid"), ssid);
        item.insert(QStringLiteral("displayName"), ssid.isEmpty()
                    ? QStringLiteral("隐藏网络")
                    : ssid);
        item.insert(QStringLiteral("hidden"), ssid.isEmpty());
        item.insert(QStringLiteral("bssid"), bssid);
        item.insert(QStringLiteral("mode"), fields.at(3).trimmed());
        item.insert(QStringLiteral("channel"), fields.at(4).trimmed());
        item.insert(QStringLiteral("frequency"), fields.at(5).trimmed());
        item.insert(QStringLiteral("rate"), fields.at(6).trimmed());
        item.insert(QStringLiteral("signal"), signal);
        item.insert(QStringLiteral("security"), fields.at(8).trimmed());
        item.insert(QStringLiteral("secured"), !fields.at(8).trimmed().isEmpty()
                    && fields.at(8).trimmed() != QStringLiteral("--"));
        points.append(item);
    }

    std::sort(points.begin(), points.end(), accessPointLess);
    return points;
}

QVariantList WifiManager::parseIpv4Addresses(const QString &output)
{
    QVariantList addresses;
    QSet<QString> seen;
    static const QRegularExpression addressExpression(
        QStringLiteral("(?<![0-9])((?:[0-9]{1,3}\\.){3}[0-9]{1,3})(?:/([0-9]{1,2}))?(?![0-9])"));

    QRegularExpressionMatchIterator iterator = addressExpression.globalMatch(output);
    while (iterator.hasNext()) {
        const QRegularExpressionMatch match = iterator.next();
        const QString address = match.captured(1);
        const QStringList octets = address.split(QLatin1Char('.'));
        bool validAddress = octets.size() == 4;
        for (const QString &octet : octets) {
            bool ok = false;
            const int value = octet.toInt(&ok);
            if (!ok || value < 0 || value > 255) {
                validAddress = false;
                break;
            }
        }
        if (!validAddress || address == QStringLiteral("0.0.0.0")) {
            continue;
        }

        const QString prefixText = match.captured(2);
        bool prefixOk = true;
        int prefix = 0;
        if (!prefixText.isEmpty()) {
            prefix = prefixText.toInt(&prefixOk);
        }
        if (!prefixOk || prefix < 0 || prefix > 32) {
            continue;
        }

        const QString cidr = prefixText.isEmpty()
            ? address
            : QStringLiteral("%1/%2").arg(address, prefixText);
        if (seen.contains(cidr)) {
            continue;
        }
        seen.insert(cidr);

        QVariantMap item;
        item.insert(QStringLiteral("address"), address);
        item.insert(QStringLiteral("prefix"), prefixText.isEmpty() ? QVariant() : QVariant(prefix));
        item.insert(QStringLiteral("cidr"), cidr);
        addresses.append(item);
    }
    return addresses;
}

QVariantMap WifiManager::classifyNmcliError(const QString &output, bool timedOut)
{
    if (timedOut) {
        return messageResult(QStringLiteral("timeout"),
                             QStringLiteral("Wi-Fi 操作超时。请检查网卡状态后重试。"),
                             QStringLiteral("The Wi-Fi operation timed out. Check the adapter and try again."));
    }

    const QString normalized = output.toLower();
    if (normalized.contains(QStringLiteral("not authorized"))
        || normalized.contains(QStringLiteral("permission denied"))
        || normalized.contains(QStringLiteral("not permitted"))
        || normalized.contains(QStringLiteral("authentication is required"))
        || normalized.contains(QStringLiteral("a password is required"))
        || normalized.contains(QStringLiteral("no password was provided"))
        || normalized.contains(QStringLiteral("is not allowed to run sudo"))
        || normalized.contains(QStringLiteral("a terminal is required"))
        || normalized.contains(QStringLiteral("polkit"))
        || normalized.contains(QStringLiteral("权限"))) {
        return messageResult(QStringLiteral("permission-denied"),
                             QStringLiteral("没有执行 Wi-Fi 操作的权限。请使用安装迅为开发板测试平台的桌面账户后重试。"),
                             QStringLiteral("Permission was denied. Use the desktop account installed for the Topeet development-board test platform and try again."));
    }
    if (normalized.contains(QStringLiteral("networkmanager is not running"))
        || normalized.contains(QStringLiteral("could not create nmclient"))
        || normalized.contains(QStringLiteral("networkmanager is not running"))
        || normalized.contains(QStringLiteral("network manager is not running"))) {
        return messageResult(QStringLiteral("networkmanager-unavailable"),
                             QStringLiteral("NetworkManager 未运行，无法管理 Wi-Fi。"),
                             QStringLiteral("NetworkManager is not running, so Wi-Fi cannot be managed."));
    }
    if (normalized.contains(QStringLiteral("wifi is not enabled"))
        || normalized.contains(QStringLiteral("wi-fi is not enabled"))
        || normalized.contains(QStringLiteral("wireless is disabled"))
        || normalized.contains(QStringLiteral("radio is disabled"))
        || normalized.contains(QStringLiteral("无线网络已禁用"))) {
        return messageResult(QStringLiteral("wifi-disabled"),
                             QStringLiteral("Wi-Fi 无线电已关闭。请先在系统网络设置中开启无线网络。"),
                             QStringLiteral("The Wi-Fi radio is disabled. Enable wireless networking in system settings first."));
    }
    if (normalized.contains(QStringLiteral("no network with ssid"))
        || normalized.contains(QStringLiteral("network was not found"))
        || normalized.contains(QStringLiteral("not available"))) {
        return messageResult(QStringLiteral("network-not-found"),
                             QStringLiteral("未找到目标 Wi-Fi 网络。请重新扫描后重试。"),
                             QStringLiteral("The requested Wi-Fi network was not found. Scan again and retry."));
    }
    if (normalized.contains(QStringLiteral("secrets were required"))
        || normalized.contains(QStringLiteral("no secrets"))
        || normalized.contains(QStringLiteral("password is required"))
        || normalized.contains(QStringLiteral("wrong password"))
        || normalized.contains(QStringLiteral("密码"))) {
        return messageResult(QStringLiteral("authentication-failed"),
                             QStringLiteral("Wi-Fi 密码无效、缺失，或该网络需要额外认证。"),
                             QStringLiteral("The Wi-Fi password is invalid or missing, or the network needs additional authentication."));
    }
    if (normalized.contains(QStringLiteral("activation failed"))
        || normalized.contains(QStringLiteral("failed to activate"))) {
        return messageResult(QStringLiteral("connection-failed"),
                             QStringLiteral("Wi-Fi 连接未能激活。请确认密码、信号和热点状态。"),
                             QStringLiteral("Wi-Fi activation failed. Check the password, signal, and access point."));
    }
    return messageResult(QStringLiteral("nmcli-failed"),
                         QStringLiteral("NetworkManager 未能完成 Wi-Fi 操作。"),
                         QStringLiteral("NetworkManager could not complete the Wi-Fi operation."));
}

QVariantMap WifiManager::diagnoseWifiDeviceState(const QString &state,
                                                 const QString &interfaceName)
{
    const QString normalized = state.trimmed().toLower();
    const QString iface = interfaceName.trimmed();
    const bool ifupdownClaim = ifupdownClaimsInterface(iface);
    const bool dhcpcdClaim = dhcpcdClaimsInterface(iface);
    const bool operDown = interfaceOperStateDown(iface);
    const bool rfkillBlocked = rfkillBlocksWifi();

    if (normalized == QStringLiteral("unmanaged")) {
        QString zh = QStringLiteral("所选 Wi-Fi 网卡未被 NetworkManager 管理");
        QString en = QStringLiteral("The selected Wi-Fi adapter is unmanaged by NetworkManager");
        if (ifupdownClaim || dhcpcdClaim) {
            zh += QStringLiteral("（可能被 ifupdown/dhcpcd 占用）");
            en += QStringLiteral(" (likely claimed by ifupdown/dhcpcd)");
        }
        zh += QStringLiteral("。请让 NetworkManager 接管 wlan0：禁用 /etc/network/interfaces.d/wlan0 的 manual 声明，并在 dhcpcd 中 denyinterfaces wlan0 p2p0。");
        en += QStringLiteral(". Let NetworkManager own wlan0: disable the ifupdown manual claim in /etc/network/interfaces.d/wlan0 and add denyinterfaces wlan0 p2p0 to dhcpcd.");
        return messageResult(QStringLiteral("wifi-device-unmanaged"), zh, en);
    }

    if (normalized == QStringLiteral("unavailable")) {
        if (ifupdownClaim || dhcpcdClaim) {
            return messageResult(
                QStringLiteral("wifi-device-unmanaged"),
                QStringLiteral("所选 Wi-Fi 网卡状态为 unavailable，且检测到 ifupdown/dhcpcd 声明。请禁用 interfaces.d/wlan0 的 manual 配置，并让 NetworkManager 管理 wlan0（dhcpcd: denyinterfaces wlan0 p2p0）。"),
                QStringLiteral("The selected Wi-Fi adapter is unavailable and ifupdown/dhcpcd appears to claim it. Disable the interfaces.d/wlan0 manual stanza and let NetworkManager manage wlan0 (dhcpcd: denyinterfaces wlan0 p2p0)."));
        }
        if (rfkillBlocked) {
            return messageResult(
                QStringLiteral("wifi-device-unavailable"),
                QStringLiteral("所选 Wi-Fi 网卡状态为 unavailable，且 rfkill 阻止了无线。请解除软/硬屏蔽后重试。"),
                QStringLiteral("The selected Wi-Fi adapter is unavailable and rfkill is blocking wireless. Unblock soft/hard kill switches, then retry."));
        }
        if (operDown) {
            return messageResult(
                QStringLiteral("wifi-device-unavailable"),
                QStringLiteral("所选 Wi-Fi 网卡状态为 unavailable（链路 down）。请确认驱动已加载、接口已 up，并让 NetworkManager 管理 wlan0；p2p0 冲突时可删除 p2p 接口后重试。"),
                QStringLiteral("The selected Wi-Fi adapter is unavailable (link down). Confirm the driver is loaded, the interface is up, and NetworkManager owns wlan0; if p2p0 interferes, delete the p2p interface and retry."));
        }
        return messageResult(
            QStringLiteral("wifi-device-unavailable"),
            QStringLiteral("所选 Wi-Fi 网卡状态为 unavailable。常见原因：ifupdown/dhcpcd 占用、驱动未就绪、rfkill、或 p2p0 干扰。建议由 NetworkManager 管理 wlan0（禁用 interfaces.d/wlan0 manual；dhcpcd denyinterfaces wlan0 p2p0）。"),
            QStringLiteral("The selected Wi-Fi adapter is unavailable. Common causes: ifupdown/dhcpcd ownership, driver not ready, rfkill, or p2p0 interference. Prefer NetworkManager ownership of wlan0 (disable interfaces.d/wlan0 manual; dhcpcd denyinterfaces wlan0 p2p0)."));
    }

    if (normalized == QStringLiteral("disabled")) {
        return messageResult(
            QStringLiteral("wifi-device-disabled"),
            QStringLiteral("所选 Wi-Fi 网卡已被禁用。请在系统网络设置中启用该网卡。"),
            QStringLiteral("The selected Wi-Fi adapter is disabled. Enable it in system network settings."));
    }

    return messageResult(
        QStringLiteral("wifi-device-disabled"),
        QStringLiteral("所选 Wi-Fi 网卡未启用或未受 NetworkManager 管理。请检查系统网络设置。"),
        QStringLiteral("The selected Wi-Fi adapter is disabled or unmanaged. Check system network settings."));
}

bool WifiManager::isSafeInterfaceName(const QString &interfaceName)
{
    static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,62}$"));
    return expression.match(interfaceName.trimmed()).hasMatch();
}

bool WifiManager::isValidSsid(const QString &ssid)
{
    const QByteArray encoded = ssid.toUtf8();
    if (encoded.isEmpty() || encoded.size() > 32) {
        return false;
    }
    for (const QChar character : ssid) {
        if (isControlCharacter(character)) {
            return false;
        }
    }
    return true;
}

bool WifiManager::isValidPassword(const QString &password)
{
    if (password.toUtf8().size() > 256) {
        return false;
    }
    for (const QChar character : password) {
        if (isControlCharacter(character)) {
            return false;
        }
    }
    return true;
}

bool WifiManager::isScanCacheFresh(const QDateTime &scannedAt,
                                   const QDateTime &currentTime,
                                   int maximumAgeMs)
{
    if (!scannedAt.isValid() || !currentTime.isValid() || maximumAgeMs < 0) {
        return false;
    }
    const qint64 ageMs = scannedAt.msecsTo(currentTime);
    return ageMs >= 0 && ageMs <= maximumAgeMs;
}

bool WifiManager::refresh()
{
    if (!begin(Operation::Refresh)) {
        return false;
    }
    startStage(Stage::ReadRadio);
    return true;
}

bool WifiManager::scan(const QString &interfaceName)
{
    const QString requested = interfaceName.trimmed();
    if (!requested.isEmpty() && !isSafeInterfaceName(requested)) {
        return reject(QStringLiteral("scan"), QStringLiteral("invalid-interface"),
                      QStringLiteral("Wi-Fi 网卡名称无效。"),
                      QStringLiteral("The Wi-Fi interface name is invalid."));
    }
    if (!begin(Operation::Scan, requested)) {
        return false;
    }
    startStage(Stage::ReadRadio);
    return true;
}

bool WifiManager::connectToNetwork(const QString &interfaceName,
                                   const QString &networkId,
                                   const QString &password)
{
    const QString requested = interfaceName.trimmed();
    if (!isSafeInterfaceName(requested)) {
        return reject(QStringLiteral("connect"), QStringLiteral("invalid-interface"),
                      QStringLiteral("请选择有效的 Wi-Fi 网卡。"),
                      QStringLiteral("Select a valid Wi-Fi interface."));
    }
    if (!isValidPassword(password)) {
        return reject(QStringLiteral("connect"), QStringLiteral("invalid-password"),
                      QStringLiteral("Wi-Fi 密码包含不支持的控制字符，或长度超过 256 字节。"),
                      QStringLiteral("The Wi-Fi password contains an unsupported control character or is longer than 256 bytes."));
    }
    if (m_busy) {
        return reject(QStringLiteral("connect"), QStringLiteral("busy"),
                      QStringLiteral("已有 Wi-Fi 操作正在进行，请等待完成。"),
                      QStringLiteral("A Wi-Fi operation is already running. Wait for it to finish."));
    }

    const QVariantMap network = cachedNetwork(requested, networkId);
    if (network.isEmpty()) {
        return reject(QStringLiteral("connect"), QStringLiteral("network-not-in-scan-cache"),
                      QStringLiteral("所选 Wi-Fi 不在当前扫描缓存中，或扫描结果已过期。请重新扫描后再连接。"),
                      QStringLiteral("The selected Wi-Fi network is not in the current scan cache or the scan has expired. Scan again before connecting."));
    }
    if (network.value(QStringLiteral("hidden")).toBool() || !isValidSsid(network.value(QStringLiteral("ssid")).toString())) {
        return reject(QStringLiteral("connect"), QStringLiteral("hidden-network"),
                      QStringLiteral("隐藏网络未提供可连接的 SSID。请使用系统网络设置连接该网络。"),
                      QStringLiteral("The hidden network does not expose a connectable SSID. Use system network settings to connect to it."));
    }
    if (!begin(Operation::Connect, requested)) {
        return false;
    }
    m_requestedNetworkId = networkId;
    m_requestedSsid = network.value(QStringLiteral("ssid")).toString();
    m_requestedBssid = network.value(QStringLiteral("bssid")).toString();
    m_pendingPassword = password;
    startStage(Stage::ReadRadio);
    return true;
}

bool WifiManager::disconnect(const QString &interfaceName)
{
    const QString requested = interfaceName.trimmed();
    if (!isSafeInterfaceName(requested)) {
        return reject(QStringLiteral("disconnect"), QStringLiteral("invalid-interface"),
                      QStringLiteral("请选择有效的 Wi-Fi 网卡。"),
                      QStringLiteral("Select a valid Wi-Fi interface."));
    }
    if (!begin(Operation::Disconnect, requested)) {
        return false;
    }
    startStage(Stage::ReadRadio);
    return true;
}

bool WifiManager::begin(Operation operation, const QString &interfaceName)
{
    if (m_busy) {
        return reject(operationNameFor(operation),
                      QStringLiteral("busy"),
                      QStringLiteral("已有 Wi-Fi 操作正在进行，请等待完成。"),
                      QStringLiteral("A Wi-Fi operation is already running. Wait for it to finish."));
    }

    const QString nmcli = resolvedNmcliProgram();
    if (nmcli.isEmpty()) {
        return reject(operationNameFor(operation), QStringLiteral("nmcli-missing"),
                      QStringLiteral("未找到 nmcli。请安装并启动 NetworkManager 后重试。"),
                      QStringLiteral("nmcli was not found. Install and start NetworkManager, then try again."));
    }

    m_operation = operation;
    m_stage = Stage::Idle;
    m_requestedInterface = interfaceName;
    m_requestedNetworkId.clear();
    m_requestedSsid.clear();
    m_requestedBssid.clear();
    m_pendingPassword.clear();
    m_busy = true;
    emit busyChanged();

    const bool networkChange = operation == Operation::Connect || operation == Operation::Disconnect;
    QVariantMap changes;
    changes.insert(QStringLiteral("available"), true);
    changes.insert(QStringLiteral("nmcliAvailable"), true);
    changes.insert(QStringLiteral("authorizationMode"), authorizationModeName());
    changes.insert(QStringLiteral("authorizationHelperAvailable"), usesRestrictedWifiHelper());
    changes.insert(QStringLiteral("busy"), true);
    changes.insert(QStringLiteral("status"), statusForOperation(operation));
    changes.insert(QStringLiteral("message"), messageForOperation(operation));
    changes.insert(QStringLiteral("messageEn"), messageEnForOperation(operation));
    changes.insert(QStringLiteral("errorCode"), QString());
    changes.insert(QStringLiteral("webSessionMayDisconnect"), networkChange);
    changes.insert(QStringLiteral("networkSwitchWarning"), networkChange
                   ? QStringLiteral("切换或断开 Wi-Fi 可能会使当前 Web 会话断开。请记住新的 IP 地址并重新打开页面。")
                   : QString());
    changes.insert(QStringLiteral("networkSwitchWarningEn"), networkChange
                   ? QStringLiteral("Changing or disconnecting Wi-Fi may end this Web session. Note the new IP address and reopen the page.")
                   : QString());
    changes.insert(QStringLiteral("connectionCommandCompleted"), false);
    changes.insert(QStringLiteral("disconnectCommandCompleted"), false);
    changes.insert(QStringLiteral("updatedAt"), nowIso());
    if (networkChange) {
        // The previous network's address is no longer trustworthy once a
        // connect/disconnect request starts.  Clear both the top-level state
        // and its selected device row until NetworkManager reports the new
        // post-operation address.
        QVariantList clearedDevices = m_devices;
        for (QVariant &entry : clearedDevices) {
            QVariantMap device = entry.toMap();
            if (device.value(QStringLiteral("device")).toString() != interfaceName.trimmed()) {
                continue;
            }
            device.insert(QStringLiteral("ipv4"), QString());
            device.insert(QStringLiteral("ipv4Address"), QString());
            device.insert(QStringLiteral("ipAddress"), QString());
            device.insert(QStringLiteral("ipv4Cidr"), QString());
            device.insert(QStringLiteral("ipv4Prefix"), QVariant());
            device.insert(QStringLiteral("ipv4Addresses"), QVariantList());
            device.insert(QStringLiteral("ipv4Assigned"), false);
            device.insert(QStringLiteral("ipv4Status"), QStringLiteral("pending"));
            entry = device;
            break;
        }
        if (m_devices != clearedDevices) {
            m_devices = clearedDevices;
        }
        changes.insert(QStringLiteral("devices"), clearedDevices);
        changes.insert(QStringLiteral("ipv4"), QString());
        changes.insert(QStringLiteral("ipv4Address"), QString());
        changes.insert(QStringLiteral("ipAddress"), QString());
        changes.insert(QStringLiteral("ipv4Cidr"), QString());
        changes.insert(QStringLiteral("ipv4Prefix"), QVariant());
        changes.insert(QStringLiteral("ipv4Addresses"), QVariantList());
        changes.insert(QStringLiteral("ipv4Assigned"), false);
        changes.insert(QStringLiteral("ipv4Status"), QStringLiteral("pending"));
        changes.insert(QStringLiteral("ipv4ReadError"), QString());
    }
    if (operation == Operation::Scan) {
        if (!m_accessPoints.isEmpty()) {
            m_accessPoints.clear();
            emit accessPointsChanged();
        }
        m_scanCacheInterface.clear();
        m_scanCacheTimestamp = QDateTime();
        changes.insert(QStringLiteral("accessPointCount"), 0);
        changes.insert(QStringLiteral("scanCacheValid"), false);
        changes.insert(QStringLiteral("scanCacheExpiresAt"), QString());
    }
    updateState(changes);
    return true;
}

bool WifiManager::reject(const QString &operation, const QString &code,
                         const QString &message, const QString &messageEn)
{
    // A rejected second click must not replace the state of an in-flight
    // operation.  Return a self-contained result to the caller instead.
    if (m_busy) {
        QVariantMap result = state();
        result.insert(QStringLiteral("operation"), operation);
        result.insert(QStringLiteral("success"), false);
        result.insert(QStringLiteral("errorCode"), code);
        result.insert(QStringLiteral("message"), message);
        result.insert(QStringLiteral("messageEn"), messageEn);
        emit operationFinished(operation, false, result);
        return false;
    }

    QVariantMap changes;
    changes.insert(QStringLiteral("status"), QStringLiteral("error"));
    changes.insert(QStringLiteral("message"), message);
    changes.insert(QStringLiteral("messageEn"), messageEn);
    changes.insert(QStringLiteral("errorCode"), code);
    changes.insert(QStringLiteral("busy"), m_busy);
    changes.insert(QStringLiteral("updatedAt"), nowIso());
    updateState(changes);

    QVariantMap result = state();
    result.insert(QStringLiteral("operation"), operation);
    result.insert(QStringLiteral("success"), false);
    emit operationFinished(operation, false, result);
    return false;
}

void WifiManager::startStage(Stage stage)
{
    m_stage = stage;
    switch (stage) {
    case Stage::ReadRadio:
        startNmcli(QStringList() << QStringLiteral("-t") << QStringLiteral("-e") << QStringLiteral("yes")
                                 << QStringLiteral("radio") << QStringLiteral("wifi"),
                   kRadioTimeoutMs);
        return;
    case Stage::ReadDevices:
        startNmcli(QStringList() << QStringLiteral("-t") << QStringLiteral("-e") << QStringLiteral("yes")
                                 << QStringLiteral("-f") << QStringLiteral("DEVICE,TYPE,STATE,CONNECTION")
                                 << QStringLiteral("device") << QStringLiteral("status"),
                   kDeviceStatusTimeoutMs);
        return;
    case Stage::ReadIpv4:
        // `device show` is read-only.  It is intentionally sequenced after
        // device status so the selected interface cannot be client supplied
        // or stale by the time it reaches nmcli.
        startNmcli(QStringList() << QStringLiteral("-t") << QStringLiteral("-e") << QStringLiteral("yes")
                                 << QStringLiteral("-f") << QStringLiteral("IP4.ADDRESS")
                                 << QStringLiteral("device") << QStringLiteral("show")
                                 << selectedWifiDevice(),
                   kIpv4StatusTimeoutMs);
        return;
    case Stage::Rescan:
        startNmcli(QStringList() << QStringLiteral("-w") << QStringLiteral("15")
                                 << QStringLiteral("device") << QStringLiteral("wifi") << QStringLiteral("rescan")
                                 << QStringLiteral("ifname") << selectedWifiDevice(),
                   kScanTimeoutMs);
        return;
    case Stage::ListAccessPoints:
        startNmcli(QStringList() << QStringLiteral("-t") << QStringLiteral("-e") << QStringLiteral("yes")
                                 << QStringLiteral("-f") << QStringLiteral("IN-USE,SSID,BSSID,MODE,CHAN,FREQ,RATE,SIGNAL,SECURITY")
                                 << QStringLiteral("device") << QStringLiteral("wifi") << QStringLiteral("list")
                                 << QStringLiteral("ifname") << selectedWifiDevice()
                                 << QStringLiteral("--rescan") << QStringLiteral("no"),
                   kAccessPointTimeoutMs);
        return;
    case Stage::Connect: {
        const bool useRestrictedHelper = usesRestrictedWifiHelper();
        const QByteArray password = m_pendingPassword.toUtf8();
        const QByteArray passwordInput = password.isEmpty()
            ? QByteArray()
            : password + QByteArrayLiteral("\n");
        QStringList arguments;
        arguments << QStringLiteral("-w") << QStringLiteral("30");
        if (!useRestrictedHelper && !password.isEmpty()) {
            arguments << QStringLiteral("--ask");
        }
        arguments << QStringLiteral("device") << QStringLiteral("wifi") << QStringLiteral("connect")
                  << m_requestedSsid;
        if (useRestrictedHelper) {
            // The helper validates the selected BSSID and Wi-Fi interface,
            // then gives the password to nmcli with --ask over stdin.
            arguments << QStringLiteral("bssid") << m_requestedBssid
                      << QStringLiteral("ifname") << selectedWifiDevice();
            startNmcli(arguments, kConnectTimeoutMs, passwordInput);
            return;
        }
        arguments << QStringLiteral("bssid") << m_requestedBssid
                  << QStringLiteral("ifname") << selectedWifiDevice();
        startNmcli(arguments, kConnectTimeoutMs, passwordInput);
        return;
    }
    case Stage::Disconnect:
        startNmcli(QStringList() << QStringLiteral("-w") << QStringLiteral("20")
                                 << QStringLiteral("device") << QStringLiteral("disconnect")
                                 << selectedWifiDevice(),
                   kDisconnectTimeoutMs);
        return;
    case Stage::Idle:
        return;
    }
}

void WifiManager::startNmcli(const QStringList &arguments, int timeoutMs,
                             const QByteArray &standardInput)
{
    const QString nmcli = resolvedNmcliProgram();
    if (nmcli.isEmpty()) {
        finish(false, QStringLiteral("nmcli-missing"),
               QStringLiteral("未找到 nmcli。请安装并启动 NetworkManager 后重试。"),
               QStringLiteral("nmcli was not found. Install and start NetworkManager, then try again."));
        return;
    }

    // Buildroot/product images often run BoardLab as root. Prefer direct nmcli
    // there so a missing sudo/helper install does not block Wi-Fi operations.
    // Ubuntu/Debian desktop accounts keep the restricted sudo helper path.
    const bool useRestrictedHelper = usesRestrictedWifiHelper() && !processIsRoot();
    QString program = nmcli;
    QStringList processArguments = arguments;
    if (useRestrictedHelper) {
        program = resolvedSudoProgram();
        processArguments.clear();
        processArguments << QStringLiteral("-n") << QString::fromLatin1(kRestrictedWifiHelperPath);
        if (!standardInput.isEmpty()) {
            processArguments << QStringLiteral("--password-stdin") << QStringLiteral("--");
        }
        processArguments.append(arguments);
    }

    m_commandActive = true;
    m_commandTimedOut = false;
    m_process->setProgram(program);
    m_process->setArguments(processArguments);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->start();
    if (!standardInput.isEmpty()) {
        m_process->write(standardInput);
        m_process->closeWriteChannel();
    }
    m_timeoutTimer->start(timeoutMs);
}

void WifiManager::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_commandActive) {
        return;
    }
    m_timeoutTimer->stop();
    m_commandActive = false;
    const QString output = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    const QString errors = QString::fromLocal8Bit(m_process->readAllStandardError());
    // QProcess retains its argument list after completion.  Clear it before
    // emitting any result so an accepted Wi-Fi password has the shortest
    // practical lifetime in this object.
    m_process->setArguments(QStringList());
    handleStageResult(exitCode, exitStatus, output, errors, m_commandTimedOut);
}

void WifiManager::onProcessError(QProcess::ProcessError error)
{
    if (!m_commandActive || error != QProcess::FailedToStart) {
        return;
    }
    m_timeoutTimer->stop();
    m_commandActive = false;
    m_process->setArguments(QStringList());
    if (m_stage == Stage::ReadIpv4) {
        // Address reporting must never turn a completed association into a
        // false connection failure.  Keep a machine-readable reason and
        // continue with the connection result.
        handleIpv4Addresses(QString(),
                            QStringLiteral("Could not start the read-only IPv4 status query."));
        return;
    }
    finish(false, QStringLiteral("nmcli-start-failed"),
           QStringLiteral("无法启动 nmcli。请检查 NetworkManager 安装和执行权限。"),
           QStringLiteral("Could not start nmcli. Check the NetworkManager installation and execute permission."));
}

void WifiManager::onCommandTimeout()
{
    if (!m_commandActive) {
        return;
    }
    m_commandTimedOut = true;
    m_process->kill();
}

void WifiManager::handleStageResult(int exitCode, QProcess::ExitStatus exitStatus,
                                    const QString &standardOutput, const QString &standardError,
                                    bool timedOut)
{
    const QString combined = standardError.trimmed().isEmpty()
        ? standardOutput
        : (standardOutput.trimmed().isEmpty()
           ? standardError
           : standardOutput + QLatin1Char('\n') + standardError);
    if (timedOut || exitStatus != QProcess::NormalExit || exitCode != 0) {
        if (m_stage == Stage::ReadIpv4) {
            const QVariantMap error = classifyNmcliError(combined, timedOut);
            handleIpv4Addresses(QString(), error.value(QStringLiteral("messageEn")).toString());
            return;
        }
        const QVariantMap error = classifyNmcliError(combined, timedOut);
        finish(false, error.value(QStringLiteral("errorCode")).toString(),
               error.value(QStringLiteral("message")).toString(),
               error.value(QStringLiteral("messageEn")).toString());
        return;
    }

    switch (m_stage) {
    case Stage::ReadRadio: {
        const QString value = standardOutput.trimmed().toLower();
        const bool enabled = value == QStringLiteral("enabled") || value == QStringLiteral("yes") || value == QStringLiteral("on");
        QVariantMap changes;
        changes.insert(QStringLiteral("radioEnabled"), enabled);
        changes.insert(QStringLiteral("updatedAt"), nowIso());
        updateState(changes);
        if (!enabled) {
            finish(false, QStringLiteral("wifi-disabled"),
                   QStringLiteral("Wi-Fi 无线电已关闭。请先在系统网络设置中开启无线网络。"),
                   QStringLiteral("The Wi-Fi radio is disabled. Enable wireless networking in system settings first."));
            return;
        }
        startStage(Stage::ReadDevices);
        return;
    }
    case Stage::ReadDevices:
        handleDeviceStatus(standardOutput);
        return;
    case Stage::ReadIpv4:
        handleIpv4Addresses(standardOutput);
        return;
    case Stage::Rescan:
        startStage(Stage::ListAccessPoints);
        return;
    case Stage::ListAccessPoints: {
        QVariantList points = parseAccessPoints(standardOutput);
        const QDateTime scannedAt = QDateTime::currentDateTimeUtc();
        for (QVariant &entry : points) {
            QVariantMap point = entry.toMap();
            point.insert(QStringLiteral("interface"), selectedWifiDevice());
            point.insert(QStringLiteral("scannedAt"), scannedAt.toString(Qt::ISODateWithMs));
            entry = point;
        }
        if (m_accessPoints != points) {
            m_accessPoints = points;
            emit accessPointsChanged();
        }
        m_scanCacheInterface = selectedWifiDevice();
        m_scanCacheTimestamp = scannedAt;
        QVariantMap changes;
        changes.insert(QStringLiteral("accessPointCount"), points.size());
        changes.insert(QStringLiteral("scanCacheValid"), true);
        changes.insert(QStringLiteral("scanCacheExpiresAt"), scannedAt.addMSecs(m_scanCacheMaximumAgeMs).toString(Qt::ISODateWithMs));
        changes.insert(QStringLiteral("updatedAt"), nowIso());
        updateState(changes);
        finish(true, QString(),
               QStringLiteral("扫描完成，发现 %1 个 Wi-Fi 网络。").arg(points.size()),
               QStringLiteral("Scan completed. Found %1 Wi-Fi networks.").arg(points.size()));
        return;
    }
    case Stage::Connect:
        // The password is no longer needed as soon as nmcli returns.  Do not
        // retain it while reading the resulting NetworkManager device state.
        m_pendingPassword.clear();
        updateState(QVariantMap{{QStringLiteral("connectionCommandCompleted"), true}});
        startStage(Stage::ReadDevices);
        return;
    case Stage::Disconnect:
        updateState(QVariantMap{{QStringLiteral("disconnectCommandCompleted"), true}});
        startStage(Stage::ReadDevices);
        return;
    case Stage::Idle:
        return;
    }
}

void WifiManager::handleDeviceStatus(const QString &output)
{
    const QVariantList devices = parseWifiDeviceStatus(output);
    if (m_devices != devices) {
        m_devices = devices;
    }

    const QString selected = selectedWifiDevice();
    QVariantMap changes;
    changes.insert(QStringLiteral("available"), !devices.isEmpty());
    changes.insert(QStringLiteral("devices"), devices);
    changes.insert(QStringLiteral("selectedDevice"), selected);
    changes.insert(QStringLiteral("updatedAt"), nowIso());
    updateState(changes);

    // A refresh reports the currently assigned address, while a connection
    // confirms it after NetworkManager has completed association.  Scanning
    // deliberately skips this query so an RF scan remains focused and fast.
    const bool needsIpv4 = m_operation == Operation::Refresh
        || (m_operation == Operation::Connect
            && m_state.value(QStringLiteral("connectionCommandCompleted")).toBool());
    if (needsIpv4 && !selected.isEmpty()) {
        startStage(Stage::ReadIpv4);
        return;
    }
    if (needsIpv4) {
        // A refresh with no eligible interface must also clear a previously
        // published address instead of leaving a stale Wi-Fi IP on screen.
        handleIpv4Addresses(QString());
        return;
    }

    if (m_operation == Operation::Disconnect
        && m_state.value(QStringLiteral("disconnectCommandCompleted")).toBool()) {
        // Do not leave the previous network's address visible after a
        // successful disconnect.
        handleIpv4Addresses(QString());
        return;
    }

    continueAfterDeviceStatus();
}

void WifiManager::handleIpv4Addresses(const QString &output, const QString &error)
{
    const QVariantList addresses = parseIpv4Addresses(output);
    const QVariantMap primary = addresses.isEmpty() ? QVariantMap() : addresses.first().toMap();
    const QString address = primary.value(QStringLiteral("address")).toString();
    const QString cidr = primary.value(QStringLiteral("cidr")).toString();
    const QVariant prefix = primary.value(QStringLiteral("prefix"));
    const QString selected = selectedWifiDevice();
    const QString compactError = error.simplified().left(180);

    QVariantList updatedDevices = m_devices;
    for (QVariant &entry : updatedDevices) {
        QVariantMap device = entry.toMap();
        if (device.value(QStringLiteral("device")).toString() != selected) {
            continue;
        }
        device.insert(QStringLiteral("ipv4"), address);
        device.insert(QStringLiteral("ipv4Address"), address);
        device.insert(QStringLiteral("ipAddress"), address);
        device.insert(QStringLiteral("ipv4Cidr"), cidr);
        device.insert(QStringLiteral("ipv4Prefix"), prefix);
        device.insert(QStringLiteral("ipv4Addresses"), addresses);
        device.insert(QStringLiteral("ipv4Assigned"), !address.isEmpty());
        device.insert(QStringLiteral("ipv4Status"), !compactError.isEmpty()
                      ? QStringLiteral("unavailable")
                      : (!address.isEmpty() ? QStringLiteral("assigned")
                                            : QStringLiteral("not-assigned")));
        entry = device;
        break;
    }
    if (m_devices != updatedDevices) {
        m_devices = updatedDevices;
    }

    QVariantMap changes;
    changes.insert(QStringLiteral("devices"), updatedDevices);
    changes.insert(QStringLiteral("selectedDevice"), selected);
    changes.insert(QStringLiteral("ipv4"), address);
    changes.insert(QStringLiteral("ipv4Address"), address);
    changes.insert(QStringLiteral("ipAddress"), address);
    changes.insert(QStringLiteral("ipv4Cidr"), cidr);
    changes.insert(QStringLiteral("ipv4Prefix"), prefix);
    changes.insert(QStringLiteral("ipv4Addresses"), addresses);
    changes.insert(QStringLiteral("ipv4Assigned"), !address.isEmpty());
    changes.insert(QStringLiteral("ipv4Status"), !compactError.isEmpty()
                   ? QStringLiteral("unavailable")
                   : (!address.isEmpty() ? QStringLiteral("assigned")
                                         : QStringLiteral("not-assigned")));
    changes.insert(QStringLiteral("ipv4ReadError"), compactError);
    changes.insert(QStringLiteral("updatedAt"), nowIso());
    updateState(changes);

    continueAfterDeviceStatus();
}

void WifiManager::continueAfterDeviceStatus()
{
    const QString selected = selectedWifiDevice();
    if (m_operation == Operation::Refresh) {
        if (m_devices.isEmpty()) {
            finish(false, QStringLiteral("no-wifi-device"),
                   QStringLiteral("未发现 Wi-Fi 网卡。请确认驱动已加载且 NetworkManager 正在运行。"),
                   QStringLiteral("No Wi-Fi adapter was found. Confirm the driver is loaded and NetworkManager is running."));
        } else if (selected.isEmpty()) {
            // All reported adapters are unavailable/unmanaged/disabled: surface
            // a concrete readiness diagnostic instead of a bare success string.
            const QVariantMap first = m_devices.first().toMap();
            const QVariantMap diagnosis = diagnoseWifiDeviceState(
                first.value(QStringLiteral("state")).toString(),
                first.value(QStringLiteral("device")).toString());
            finish(false, diagnosis.value(QStringLiteral("errorCode")).toString(),
                   diagnosis.value(QStringLiteral("message")).toString(),
                   diagnosis.value(QStringLiteral("messageEn")).toString());
        } else {
            const QVariantMap selectedState = selectedDeviceState();
            const QString selectedStateName = selectedState.value(QStringLiteral("state")).toString();
            if (isDisabledDeviceState(selectedStateName)) {
                const QVariantMap diagnosis = diagnoseWifiDeviceState(selectedStateName, selected);
                finish(false, diagnosis.value(QStringLiteral("errorCode")).toString(),
                       diagnosis.value(QStringLiteral("message")).toString(),
                       diagnosis.value(QStringLiteral("messageEn")).toString());
                return;
            }
            const QString address = m_state.value(QStringLiteral("ipv4")).toString();
            const QString status = !selected.isEmpty() && !address.isEmpty()
                ? QStringLiteral("已读取 %1 个 Wi-Fi 网卡状态；%2 IPv4：%3。").arg(m_devices.size()).arg(selected, address)
                : QStringLiteral("已读取 %1 个 Wi-Fi 网卡状态。").arg(m_devices.size());
            const QString statusEn = !selected.isEmpty() && !address.isEmpty()
                ? QStringLiteral("Read %1 Wi-Fi adapter(s); %2 IPv4: %3.").arg(m_devices.size()).arg(selected, address)
                : QStringLiteral("Read the state of %1 Wi-Fi adapter(s).").arg(m_devices.size());
            finish(true, QString(), status, statusEn);
        }
        return;
    }

    if (m_devices.isEmpty()) {
        finish(false, QStringLiteral("no-wifi-device"),
               QStringLiteral("未发现 Wi-Fi 网卡。请确认驱动已加载且 NetworkManager 正在运行。"),
               QStringLiteral("No Wi-Fi adapter was found. Confirm the driver is loaded and NetworkManager is running."));
        return;
    }
    if (selected.isEmpty()) {
        // Prefer diagnosing the requested interface, then the first known row.
        QString diagnosisInterface = m_requestedInterface;
        QString diagnosisState;
        for (const QVariant &entry : m_devices) {
            const QVariantMap device = entry.toMap();
            if (!diagnosisInterface.isEmpty()
                && device.value(QStringLiteral("device")).toString() == diagnosisInterface) {
                diagnosisState = device.value(QStringLiteral("state")).toString();
                break;
            }
        }
        if (diagnosisState.isEmpty()) {
            const QVariantMap first = m_devices.first().toMap();
            diagnosisInterface = first.value(QStringLiteral("device")).toString();
            diagnosisState = first.value(QStringLiteral("state")).toString();
        }
        if (!diagnosisState.isEmpty()) {
            const QVariantMap diagnosis = diagnoseWifiDeviceState(diagnosisState, diagnosisInterface);
            finish(false, diagnosis.value(QStringLiteral("errorCode")).toString(),
                   diagnosis.value(QStringLiteral("message")).toString(),
                   diagnosis.value(QStringLiteral("messageEn")).toString());
            return;
        }
        finish(false, QStringLiteral("interface-unavailable"),
               QStringLiteral("所选 Wi-Fi 网卡当前不可用。请重新读取状态后重试。"),
               QStringLiteral("The selected Wi-Fi adapter is unavailable. Refresh status and try again."));
        return;
    }

    const QVariantMap selectedState = selectedDeviceState();
    if (isDisabledDeviceState(selectedState.value(QStringLiteral("state")).toString())) {
        const QVariantMap diagnosis = diagnoseWifiDeviceState(
            selectedState.value(QStringLiteral("state")).toString(), selected);
        finish(false, diagnosis.value(QStringLiteral("errorCode")).toString(),
               diagnosis.value(QStringLiteral("message")).toString(),
               diagnosis.value(QStringLiteral("messageEn")).toString());
        return;
    }

    if (m_operation == Operation::Scan) {
        startStage(Stage::Rescan);
        return;
    }
    if (m_operation == Operation::Connect) {
        // An open network has an empty password, so a dedicated state marker
        // distinguishes the initial validation read from the post-connect
        // status (which may have gone through ReadIpv4).
        if (m_state.value(QStringLiteral("connectionCommandCompleted")).toBool()) {
            if (!selectedState.value(QStringLiteral("connected")).toBool()) {
                finish(false, QStringLiteral("connection-not-established"),
                       QStringLiteral("NetworkManager 未确认 Wi-Fi 已连接。请检查密码、信号和 DHCP 后重试。"),
                       QStringLiteral("NetworkManager did not confirm that Wi-Fi is connected. Check the password, signal and DHCP, then try again."));
                return;
            }
            const QString address = m_state.value(QStringLiteral("ipv4")).toString();
            const bool ipv4ReadFailed = !m_state.value(QStringLiteral("ipv4ReadError")).toString().isEmpty();
            const QString addressSuffix = !address.isEmpty()
                ? QStringLiteral("，IPv4：%1").arg(address)
                : (ipv4ReadFailed
                   ? QStringLiteral("，但 IPv4 状态暂时无法读取")
                   : QStringLiteral("，但尚未获得 IPv4 地址"));
            const QString addressSuffixEn = !address.isEmpty()
                ? QStringLiteral(", IPv4: %1").arg(address)
                : (ipv4ReadFailed
                   ? QStringLiteral(", but the IPv4 state could not be read")
                   : QStringLiteral(", but no IPv4 address has been assigned yet"));
            finish(true, QString(),
                   QStringLiteral("已连接到 Wi-Fi：%1%2。").arg(m_requestedSsid, addressSuffix),
                   QStringLiteral("Connected to Wi-Fi: %1%2.").arg(m_requestedSsid, addressSuffixEn));
            return;
        }
        startStage(Stage::Connect);
        return;
    }
    if (m_operation == Operation::Disconnect) {
        if (m_state.value(QStringLiteral("disconnectCommandCompleted")).toBool()) {
            finish(true, QString(),
                   QStringLiteral("已断开 Wi-Fi 网卡：%1。").arg(selected),
                   QStringLiteral("Disconnected Wi-Fi adapter: %1.").arg(selected));
            return;
        }
        startStage(Stage::Disconnect);
        return;
    }
}

void WifiManager::finish(bool success, const QString &code,
                         const QString &message, const QString &messageEn)
{
    const QString operation = operationName();
    m_timeoutTimer->stop();
    m_commandActive = false;
    if (m_process->state() == QProcess::NotRunning) {
        m_process->setArguments(QStringList());
    }
    m_pendingPassword.clear();

    if (m_busy) {
        m_busy = false;
        emit busyChanged();
    }
    m_operation = Operation::None;
    m_stage = Stage::Idle;
    m_requestedInterface.clear();
    m_requestedNetworkId.clear();
    m_requestedSsid.clear();
    m_requestedBssid.clear();

    QVariantMap changes;
    changes.insert(QStringLiteral("busy"), false);
    changes.insert(QStringLiteral("status"), success ? QStringLiteral("ready") : QStringLiteral("error"));
    changes.insert(QStringLiteral("message"), message);
    changes.insert(QStringLiteral("messageEn"), messageEn);
    changes.insert(QStringLiteral("errorCode"), code);
    changes.insert(QStringLiteral("updatedAt"), nowIso());
    updateState(changes);

    QVariantMap result = state();
    result.insert(QStringLiteral("operation"), operation);
    result.insert(QStringLiteral("success"), success);
    emit operationFinished(operation, success, result);
}

void WifiManager::updateState(const QVariantMap &changes)
{
    QVariantMap next = m_state;
    for (auto it = changes.constBegin(); it != changes.constEnd(); ++it) {
        next.insert(it.key(), it.value());
    }
    if (next == m_state) {
        return;
    }
    m_state = next;
    emit stateChanged();
}

QString WifiManager::resolvedNmcliProgram() const
{
    if (!m_nmcliProgram.isEmpty()) {
        const QFileInfo info(m_nmcliProgram);
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
        if (!m_nmcliProgram.contains(QLatin1Char('/'))) {
            return QStandardPaths::findExecutable(m_nmcliProgram);
        }
        return QString();
    }
    return QStandardPaths::findExecutable(QStringLiteral("nmcli"));
}

QString WifiManager::resolvedSudoProgram() const
{
    const QStringList candidates = {
        QStringLiteral("/usr/bin/sudo"),
        QStringLiteral("/bin/sudo")
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) {
            return info.absoluteFilePath();
        }
    }
    return QString();
}

bool WifiManager::usesRestrictedWifiHelper() const
{
    // A test fixture supplies an explicit nmcli executable and must never
    // reach a machine-installed privileged helper.
    if (!m_nmcliProgram.isEmpty()) {
        return false;
    }
    // Root already has NetworkManager privileges on Buildroot product images.
    // Do not require the Ubuntu/Debian sudo helper path when euid==0.
    if (processIsRoot()) {
        return false;
    }
    const QFileInfo helper(QString::fromLatin1(kRestrictedWifiHelperPath));
    const QFileInfo readyMarker(QString::fromLatin1(kRestrictedWifiHelperReadyPath));
    return helper.isFile()
        && helper.isExecutable()
        && readyMarker.isFile()
        && currentProcessInGroup(kRestrictedWifiHelperGroup)
        && !resolvedSudoProgram().isEmpty();
}

QString WifiManager::authorizationModeName() const
{
    if (processIsRoot() && !resolvedNmcliProgram().isEmpty()) {
        return QStringLiteral("root-nmcli");
    }
    if (usesRestrictedWifiHelper()) {
        return QStringLiteral("restricted-sudo-helper");
    }
    return QStringLiteral("networkmanager-polkit");
}

QString WifiManager::selectedWifiDevice() const
{
    if (!m_requestedInterface.isEmpty()) {
        for (const QVariant &entry : m_devices) {
            const QVariantMap device = entry.toMap();
            if (device.value(QStringLiteral("device")).toString() == m_requestedInterface) {
                return m_requestedInterface;
            }
        }
        return QString();
    }
    for (const QVariant &entry : m_devices) {
        const QVariantMap device = entry.toMap();
        if (device.value(QStringLiteral("connected")).toBool() && device.value(QStringLiteral("available")).toBool()) {
            return device.value(QStringLiteral("device")).toString();
        }
    }
    for (const QVariant &entry : m_devices) {
        const QVariantMap device = entry.toMap();
        if (device.value(QStringLiteral("available")).toBool()) {
            return device.value(QStringLiteral("device")).toString();
        }
    }
    return QString();
}

QVariantMap WifiManager::cachedNetwork(const QString &interfaceName, const QString &networkId) const
{
    const QString requestedInterface = interfaceName.trimmed();
    const QString requestedId = networkId.trimmed();
    if (requestedInterface.isEmpty() || requestedId.isEmpty()
        || requestedInterface != m_scanCacheInterface
        || !isScanCacheFresh(m_scanCacheTimestamp, QDateTime::currentDateTimeUtc(), m_scanCacheMaximumAgeMs)) {
        return QVariantMap();
    }
    for (const QVariant &entry : m_accessPoints) {
        const QVariantMap network = entry.toMap();
        if (network.value(QStringLiteral("interface")).toString() == requestedInterface
            && network.value(QStringLiteral("id")).toString() == requestedId
            && network.value(QStringLiteral("bssid")).toString() == requestedId) {
            return network;
        }
    }
    return QVariantMap();
}

QVariantMap WifiManager::selectedDeviceState() const
{
    const QString selected = selectedWifiDevice();
    for (const QVariant &entry : m_devices) {
        const QVariantMap device = entry.toMap();
        if (device.value(QStringLiteral("device")).toString() == selected) {
            return device;
        }
    }
    return QVariantMap();
}

QString WifiManager::operationName() const
{
    return operationNameFor(m_operation);
}

QString WifiManager::operationNameFor(Operation operation)
{
    switch (operation) {
    case Operation::Refresh:
        return QStringLiteral("refresh");
    case Operation::Scan:
        return QStringLiteral("scan");
    case Operation::Connect:
        return QStringLiteral("connect");
    case Operation::Disconnect:
        return QStringLiteral("disconnect");
    case Operation::None:
        return QStringLiteral("wifi");
    }
    return QStringLiteral("wifi");
}

QString WifiManager::statusForOperation(Operation operation)
{
    switch (operation) {
    case Operation::Refresh:
        return QStringLiteral("refreshing");
    case Operation::Scan:
        return QStringLiteral("scanning");
    case Operation::Connect:
        return QStringLiteral("connecting");
    case Operation::Disconnect:
        return QStringLiteral("disconnecting");
    case Operation::None:
        return QStringLiteral("idle");
    }
    return QStringLiteral("idle");
}

QString WifiManager::messageForOperation(Operation operation)
{
    switch (operation) {
    case Operation::Refresh:
        return QStringLiteral("正在读取 Wi-Fi 状态...");
    case Operation::Scan:
        return QStringLiteral("正在扫描 Wi-Fi 网络...");
    case Operation::Connect:
        return QStringLiteral("正在连接 Wi-Fi 网络...");
    case Operation::Disconnect:
        return QStringLiteral("正在断开 Wi-Fi 网络...");
    case Operation::None:
        return QStringLiteral("Wi-Fi 空闲。");
    }
    return QStringLiteral("Wi-Fi 空闲。");
}

QString WifiManager::messageEnForOperation(Operation operation)
{
    switch (operation) {
    case Operation::Refresh:
        return QStringLiteral("Reading Wi-Fi status...");
    case Operation::Scan:
        return QStringLiteral("Scanning Wi-Fi networks...");
    case Operation::Connect:
        return QStringLiteral("Connecting to Wi-Fi...");
    case Operation::Disconnect:
        return QStringLiteral("Disconnecting Wi-Fi...");
    case Operation::None:
        return QStringLiteral("Wi-Fi is idle.");
    }
    return QStringLiteral("Wi-Fi is idle.");
}
