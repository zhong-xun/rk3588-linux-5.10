#include "boardbackend.h"
#include "boardprofile.h"
#include "environmentmonitor.h"
#include "gpubenchmark.h"
#include "keydevicediscovery.h"
#include "keymonitor.h"
#include "linux_gpio_uapi_compat.h"
#include "qtcompat.h"
#include "runtimepaths.h"
#include "webcamerapreview.h"
#include "wifimanager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QProcess>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSet>
#include <QSysInfo>
#include <QThread>
#include <QThreadPool>
#include <QTextStream>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <QVariantMap>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <system_error>
#include <thread>
#include <fcntl.h>
#include <arpa/inet.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/input.h>
#include <linux/videodev2.h>
#include <linux/watchdog.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <signal.h>
#include <termios.h>
#include <unistd.h>

namespace {

QString runProcessResponsive(QProcess *process, int timeoutMs, int *exitCode)
{
    if (!process->waitForStarted(1500)) {
        if (exitCode) {
            *exitCode = -1;
        }
        return process->errorString();
    }

    if (!process->waitForFinished(timeoutMs > 0 ? timeoutMs : -1)) {
        process->kill();
        process->waitForFinished(1000);
        if (exitCode) {
            *exitCode = -1;
        }
        const QByteArray partial = process->readAllStandardOutput() + process->readAllStandardError();
        const QString trimmed = QString::fromLocal8Bit(partial).trimmed();
        return trimmed.isEmpty() ? QStringLiteral("command timed out") : trimmed;
    }

    if (exitCode) {
        *exitCode = process->exitCode();
    }
    const QByteArray out = process->readAllStandardOutput();
    const QByteArray err = process->readAllStandardError();
    return QString::fromLocal8Bit(out + err).trimmed();
}

int openCanSocket(const QString &interfaceName, QString *errorText)
{
    const QByteArray ifaceName = interfaceName.trimmed().toLocal8Bit();
    if (ifaceName.isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("empty CAN interface");
        }
        return -1;
    }

    const int socketFd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (socketFd < 0) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        return -1;
    }

    ifreq request;
    std::memset(&request, 0, sizeof(request));
    std::strncpy(request.ifr_name, ifaceName.constData(), IFNAMSIZ - 1);
    if (::ioctl(socketFd, SIOCGIFINDEX, &request) < 0) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        ::close(socketFd);
        return -1;
    }

    sockaddr_can address;
    std::memset(&address, 0, sizeof(address));
    address.can_family = AF_CAN;
    address.can_ifindex = request.ifr_ifindex;
    if (::bind(socketFd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        ::close(socketFd);
        return -1;
    }

    return socketFd;
}

QString formatCanFrame(const can_frame &frame)
{
    const bool extended = frame.can_id & CAN_EFF_FLAG;
    const bool remote = frame.can_id & CAN_RTR_FLAG;
    const canid_t id = extended ? (frame.can_id & CAN_EFF_MASK) : (frame.can_id & CAN_SFF_MASK);

    QString line = extended
        ? QStringLiteral("<0x%1>").arg(id, 8, 16, QLatin1Char('0'))
        : QStringLiteral("<0x%1>").arg(id, 3, 16, QLatin1Char('0'));
    line += QStringLiteral("[%1]").arg(frame.can_dlc);
    for (int i = 0; i < frame.can_dlc && i < CAN_MAX_DLEN; ++i) {
        line += QStringLiteral("%1").arg(static_cast<unsigned int>(frame.data[i]), 2, 16, QLatin1Char('0'));
    }
    if (remote) {
        line += QStringLiteral(" 远程请求");
    }
    return line.toUpper();
}

QStringList splitEscapedNmcliLine(const QString &line)
{
    QStringList parts;
    QString current;
    bool escaped = false;
    for (const QChar ch : line) {
        if (escaped) {
            current.append(ch);
            escaped = false;
            continue;
        }
        if (ch == QLatin1Char('\\')) {
            escaped = true;
            continue;
        }
        if (ch == QLatin1Char(':')) {
            parts.append(current);
            current.clear();
            continue;
        }
        current.append(ch);
    }
    if (escaped) {
        current.append(QLatin1Char('\\'));
    }
    parts.append(current);
    return parts;
}

bool isVideoNodePath(const QString &path)
{
    const QFileInfo info(QDir::cleanPath(path.trimmed()));
    static const QRegularExpression nameExpression(QStringLiteral("^video[0-9]+$"));
    const QString devRoot = boardlab::devRoot();
    return nameExpression.match(info.fileName()).hasMatch()
        && QDir::cleanPath(info.absolutePath()) == devRoot;
}

QString videoDeviceName(const QString &path)
{
    const QString baseName = QFileInfo(path).fileName();
    const QString videoRoot = QDir(boardlab::sysRoot()).filePath(
        QStringLiteral("class/video4linux"));
    QFile file(QDir(videoRoot).filePath(baseName + QStringLiteral("/name")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll()).trimmed().toLower();
}

QString commandPathForHelper(const QString &command)
{
    const QString pathEnv = QString::fromLocal8Bit(qgetenv("PATH"));
    QStringList paths = pathEnv.split(QChar(':'), boardlab::kSkipEmptyParts);
    paths << QStringLiteral("/usr/local/sbin")
          << QStringLiteral("/usr/local/bin")
          << QStringLiteral("/usr/sbin")
          << QStringLiteral("/usr/bin")
          << QStringLiteral("/sbin")
          << QStringLiteral("/bin");
    paths.removeDuplicates();
    for (const QString &path : paths) {
        const QString candidate = QDir(path).filePath(command);
        if (QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QString();
}

QString runCommandForHelper(const QString &program, const QStringList &arguments, int timeoutMs, int *exitCode)
{
    QProcess process;
    const QString executable = program.contains(QChar('/')) ? program : commandPathForHelper(program);
    process.setProgram(executable.isEmpty() ? program : executable);
    process.setArguments(arguments);
    process.start();
    return runProcessResponsive(&process, timeoutMs, exitCode);
}

void appendUnique(QStringList *list, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (!trimmed.isEmpty() && !list->contains(trimmed)) {
        list->append(trimmed);
    }
}

QString compactCommandOutput(const QString &output, int maxLength = 180)
{
    QString text = output.simplified();
    if (text.isEmpty()) {
        text = QStringLiteral("no command output");
    }
    if (text.size() > maxLength) {
        return text.left(qMax(0, maxLength - 3)) + QStringLiteral("...");
    }
    return text;
}

void fillReferenceCanFrame(can_frame *frame)
{
    std::memset(frame, 0, sizeof(*frame));
    // Keep the built-in test frame deliberately conservative.  Some vendor
    // BSPs document EFF/RTR handling defects, and BoardLab does not yet
    // implement canfd_frame TX/RX validation.  One standard data frame is a
    // truthful Classic CAN smoke test rather than a mixed-frame claim.
    frame->can_id = 0x11;
    frame->can_dlc = 2;
    frame->data[0] = 0x01;
    frame->data[1] = 0x02;
}

bool writeReferenceCanFrame(int socketFd, QString *errorText)
{
    can_frame frame;
    fillReferenceCanFrame(&frame);
    const ssize_t written = ::write(socketFd, &frame, sizeof(frame));
    if (written != static_cast<ssize_t>(sizeof(frame))) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        return false;
    }
    return true;
}

struct CanLoopSenderState {
    std::atomic_bool stopRequested {false};
    std::thread worker;
    QString interfaceName;
    int bitrate = 0;
};

std::mutex &canLoopMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<CanLoopSenderState> &canLoopSender()
{
    static std::shared_ptr<CanLoopSenderState> state;
    return state;
}

bool stopCanLoopSender(QString *statusText)
{
    std::shared_ptr<CanLoopSenderState> state;
    {
        std::lock_guard<std::mutex> lock(canLoopMutex());
        state = canLoopSender();
        canLoopSender().reset();
    }

    if (!state) {
        if (statusText) {
            *statusText = QStringLiteral("CAN reference sender is not running");
        }
        return false;
    }

    state->stopRequested.store(true);
    if (state->worker.joinable()) {
        state->worker.join();
    }
    if (statusText) {
        *statusText = QStringLiteral("CAN reference sender stopped: %1").arg(state->interfaceName);
    }
    return true;
}

struct WatchdogTestState {
    std::atomic_bool stopRequested {false};
    std::thread worker;
    QString device;
    int timeout = 0;
};

std::mutex &watchdogTestMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::shared_ptr<WatchdogTestState> &watchdogTest()
{
    static std::shared_ptr<WatchdogTestState> state;
    return state;
}

void stopWatchdogTest()
{
    std::shared_ptr<WatchdogTestState> state;
    {
        std::lock_guard<std::mutex> lock(watchdogTestMutex());
        state = watchdogTest();
        watchdogTest().reset();
    }

    if (!state) {
        return;
    }

    state->stopRequested.store(true);
    if (state->worker.joinable()) {
        state->worker.join();
    }
}

void closeWatchdogDeviceSafely(int fd)
{
    if (fd < 0) {
        return;
    }
    int options = WDIOS_DISABLECARD;
    ::ioctl(fd, WDIOC_SETOPTIONS, &options);
    // Some watchdog drivers require the magic close byte. It is harmless on
    // drivers without that feature and gives cancellable tests the best chance
    // to disarm before the descriptor is released.
    const char magicClose = 'V';
    const ssize_t ignored = ::write(fd, &magicClose, 1);
    Q_UNUSED(ignored)
    ::close(fd);
}

QString deviceRootForHelper()
{
    return boardlab::devRoot();
}

QString networkClassRootForHelper()
{
    return boardlab::fixtureRoot("BOARDLAB_SYS_CLASS_NET_ROOT",
                                 QStringLiteral("/sys/class/net"));
}

QString rtcClassRootForHelper()
{
    return boardlab::fixtureRoot("BOARDLAB_RTC_SYSFS_ROOT",
                                 QStringLiteral("/sys/class/rtc"));
}

QString rknpuDebugRootForHelper()
{
    return boardlab::fixtureRoot("BOARDLAB_RKNPU_DEBUG_ROOT",
                                 QStringLiteral("/sys/kernel/debug/rknpu"));
}

QString drmClassRootForHelper()
{
    return boardlab::fixtureRoot("BOARDLAB_SYS_CLASS_DRM_ROOT",
                                 QStringLiteral("/sys/class/drm"));
}

QString networkInterfacePathForHelper(const QString &interfaceName)
{
    return QDir(networkClassRootForHelper()).filePath(interfaceName.trimmed());
}

bool isCellularNetworkInterfaceName(const QString &interfaceName)
{
    const QString name = interfaceName.trimmed();
    return name.startsWith(QStringLiteral("wwan"))
        || name.startsWith(QStringLiteral("wwp"))
        || name.startsWith(QStringLiteral("usb"))
        || name.startsWith(QStringLiteral("ppp"))
        || name.startsWith(QStringLiteral("rmnet"))
        || name.startsWith(QStringLiteral("ccmni"));
}

QString cellularNetworkInterfaceFromDiscovery(const QString &entry)
{
    const QString interfaceName = entry.section(QChar(' '), 0, 0).trimmed();
    return isCellularNetworkInterfaceName(interfaceName) ? interfaceName : QString();
}

QString preferredCellularInterfaceFromDiscovery(const QStringList &devices)
{
    for (const QString &entry : devices) {
        const QString interfaceName = cellularNetworkInterfaceFromDiscovery(entry);
        if (!interfaceName.isEmpty()) {
            return interfaceName;
        }
    }
    // A Quectel connection manager can be the process that creates the data
    // interface.  Do not invent wwan0 while no kernel-owned interface exists:
    // RM500U CDC-NCM boards commonly expose usb0 instead.
    return QString();
}

QString topeetTestRootForHelper()
{
    // The override is used by the hermetic backend test. Production callers
    // always resolve the historical target layout rooted at /topeet_test.
    return boardlab::fixtureRoot("BOARDLAB_TOPEET_TEST_ROOT",
                                 QStringLiteral("/topeet_test"));
}

QStringList preferredLegacyDirectories(const QString &socFamily,
                                       const QString &olderDirectory,
                                       const QString &newerDirectory)
{
    const QString normalized = socFamily.trimmed().toUpper();
    QStringList directories;
    if (normalized == QStringLiteral("RK3562")
        || normalized == QStringLiteral("RK3568")
        || normalized == QStringLiteral("RK3576")) {
        directories << olderDirectory << newerDirectory;
    } else if (normalized == QStringLiteral("RK3588") || normalized == QStringLiteral("RK3588S")) {
        directories << newerDirectory << olderDirectory;
    } else {
        directories << olderDirectory << newerDirectory;
    }
    directories.removeDuplicates();
    return directories;
}

QStringList helperFileCandidates(const QString &fileName,
                                 const QStringList &preferredDirectories,
                                 const QString &requiredParentSuffix,
                                 bool requireExecutable)
{
    const QString root = topeetTestRootForHelper();
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    if (canonicalRoot.isEmpty()) {
        return QStringList();
    }

    QStringList candidates;
    const auto appendCandidate = [&](const QString &path) {
        const QFileInfo info(path);
        const QString canonicalPath = info.canonicalFilePath();
        if (!info.isFile() || (requireExecutable && !info.isExecutable())
            || canonicalPath.isEmpty()
            || (canonicalPath != canonicalRoot
                && !canonicalPath.startsWith(canonicalRoot + QLatin1Char('/')))
            || (!requiredParentSuffix.isEmpty()
                && !info.dir().dirName().endsWith(requiredParentSuffix, Qt::CaseInsensitive))) {
            return;
        }
        if (!candidates.contains(canonicalPath)) {
            candidates.append(canonicalPath);
        }
    };

    for (const QString &directory : preferredDirectories) {
        appendCandidate(QDir(root).filePath(directory + QLatin1Char('/') + fileName));
    }

    QStringList discovered;
    QDirIterator iterator(root, QStringList{fileName}, QDir::Files,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        discovered.append(iterator.next());
    }
    discovered.sort(Qt::CaseInsensitive);
    for (const QString &path : discovered) {
        appendCandidate(path);
    }
    return candidates;
}

QString headphoneSamplePathForSocFamily(const QString &socFamily)
{
    const QStringList candidates = helperFileCandidates(
        QStringLiteral("1.wav"),
        preferredLegacyDirectories(socFamily, QStringLiteral("05_headphone"),
                                    QStringLiteral("09_headphone")),
        QStringLiteral("_headphone"), false);
    return candidates.isEmpty() ? QString() : candidates.first();
}

QString modemDialerPathForSocFamily(const QString &socFamily, const QString &dialer,
                                    QString *legacyDirectory)
{
    const QString root = topeetTestRootForHelper();
    const QStringList candidates = helperFileCandidates(
        dialer,
        preferredLegacyDirectories(socFamily, QStringLiteral("03_4g"),
                                    QStringLiteral("06_4g5g")),
        QString(), true);
    if (candidates.isEmpty()) {
        return QString();
    }
    if (legacyDirectory) {
        *legacyDirectory = QDir(root).relativeFilePath(QFileInfo(candidates.first()).absolutePath());
    }
    return candidates.first();
}

QString modemKindFromUsbId(const QString &usbId)
{
    const QString normalized = usbId.trimmed().toLower();
    static const QSet<QString> em05Ids = {
        QStringLiteral("2c7c:030a"), QStringLiteral("2c7c:030c"),
        QStringLiteral("2c7c:030e"), QStringLiteral("2c7c:0310"),
        QStringLiteral("2c7c:0311"), QStringLiteral("2c7c:0312"),
        QStringLiteral("2c7c:0313"), QStringLiteral("2c7c:0314")
    };
    if (em05Ids.contains(normalized)) {
        return QStringLiteral("EM05");
    }
    if (normalized == QStringLiteral("2c7c:6005")) {
        return QStringLiteral("EC200A");
    }
    if (normalized == QStringLiteral("2c7c:0900")) {
        return QStringLiteral("RM500U");
    }
    return QString();
}

QString modemKindFromUsbProductText(const QString &line)
{
    if (line.contains(QStringLiteral("EM05"), Qt::CaseInsensitive)) {
        return QStringLiteral("EM05");
    }
    if (line.contains(QStringLiteral("EC200A"), Qt::CaseInsensitive)) {
        return QStringLiteral("EC200A");
    }
    if (line.contains(QStringLiteral("RM500U"), Qt::CaseInsensitive)) {
        return QStringLiteral("RM500U");
    }
    return QString();
}

QString modemDialerForKind(const QString &kind)
{
    return kind == QStringLiteral("EM05")
        ? QStringLiteral("quectel-CM-V1.6.2-EM05")
        : ((kind == QStringLiteral("EC200A") || kind == QStringLiteral("RM500U"))
           ? QStringLiteral("quectel-CM-V1.6.8")
           : QString());
}

bool modemDialerRequiresDhclient(const QString &dialer)
{
    // V1.6.8 uses the literal command below for ECM/RNDIS/NCM modules:
    //   dhclient -4 -d --no-pid <interface>
    // Keep this tied to the binary ABI instead of guessing from the modem
    // name. EM05's V1.6.2 binary uses BusyBox udhcpc instead.
    return dialer == QStringLiteral("quectel-CM-V1.6.8");
}

QString modemDisplayNameForKind(const QString &kind)
{
    if (kind == QStringLiteral("EM05")) {
        return QStringLiteral("EM05 4G");
    }
    if (kind == QStringLiteral("EC200A")) {
        return QStringLiteral("EC200A 4G");
    }
    if (kind == QStringLiteral("RM500U")) {
        return QStringLiteral("RM500U 5G");
    }
    return QStringLiteral("未识别模组");
}

bool isPathInsideCanonicalRoot(const QString &path, const QString &root)
{
    const QString canonicalRoot = QFileInfo(root).canonicalFilePath();
    const QString canonicalPath = QFileInfo(path).canonicalFilePath();
    if (canonicalRoot.isEmpty() || canonicalPath.isEmpty()) {
        return false;
    }
    return canonicalPath == canonicalRoot || canonicalPath.startsWith(canonicalRoot + QLatin1Char('/'));
}

struct CellularModemSelection {
    bool lsusbAvailable = false;
    bool detected = false;
    bool dialerAvailable = false;
    QString state = QStringLiteral("not-detected");
    QString module;
    QString displayName;
    QString usbId;
    QString usbLine;
    QString productText;
    QString socFamily;
    QString legacyDirectory;
    QString dialer;
    QString dialerPath;
    QString reason;
};

QString lsusbOutputForHelper(bool *available, QString *errorText)
{
    if (available) {
        *available = false;
    }
    if (errorText) {
        errorText->clear();
    }

    // Keep fixture injection strictly test-only. It exists so modem mapping
    // regressions can be verified without a USB device or an AArch64 binary.
    if (qEnvironmentVariable("BOARDLAB_TEST_MODE") == QStringLiteral("1")
        && qEnvironmentVariableIsSet("BOARDLAB_LSUSB_OUTPUT")) {
        if (available) {
            *available = true;
        }
        return QString::fromLocal8Bit(qgetenv("BOARDLAB_LSUSB_OUTPUT"));
    }

    if (commandPathForHelper(QStringLiteral("lsusb")).isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("lsusb 未安装，无法识别 USB 4G/5G 模组");
        }
        return QString();
    }

    int exitCode = -1;
    const QString output = runCommandForHelper(QStringLiteral("lsusb"), QStringList(), 3000, &exitCode);
    if (exitCode != 0) {
        if (errorText) {
            *errorText = QStringLiteral("lsusb 执行失败：%1").arg(compactCommandOutput(output, 120));
        }
        return QString();
    }
    if (available) {
        *available = true;
    }
    return output;
}

CellularModemSelection selectCellularModemForHelper(const QString &socFamily)
{
    CellularModemSelection selection;
    selection.socFamily = socFamily.trimmed().toUpper();

    bool lsusbAvailable = false;
    QString lsusbError;
    const QString lsusbOutput = lsusbOutputForHelper(&lsusbAvailable, &lsusbError);
    selection.lsusbAvailable = lsusbAvailable;
    if (!lsusbAvailable) {
        selection.state = QStringLiteral("lsusb-unavailable");
        selection.reason = lsusbError;
        return selection;
    }

    struct Candidate {
        QString module;
        QString usbId;
        QString line;
        QString productText;
    };
    QList<Candidate> candidates;
    QStringList conflicts;
    QSet<QString> seenLines;
    static const QRegularExpression usbIdExpression(
        QStringLiteral("\\bID\\s+([0-9A-Fa-f]{4}:[0-9A-Fa-f]{4})\\b"));

    const QStringList lines = lsusbOutput.split(QChar('\n'), boardlab::kSkipEmptyParts);
    for (const QString &rawLine : lines) {
        const QString line = rawLine.trimmed();
        const QRegularExpressionMatch match = usbIdExpression.match(line);
        if (!match.hasMatch()) {
            continue;
        }
        const QString usbId = match.captured(1).toLower();
        // Only a known Quectel VID:PID can select a binary. Product text is
        // retained for display and conflict detection, but is never a
        // fallback selector: EP06 (0306) and RM500Q (0800) must not become an
        // EM05 or RM500U merely because a vendor string is stale or malformed.
        if (!usbId.startsWith(QStringLiteral("2c7c:"), Qt::CaseInsensitive)) {
            continue;
        }
        const QString byId = modemKindFromUsbId(usbId);
        const QString byName = modemKindFromUsbProductText(line);
        if (byId.isEmpty()) {
            continue;
        }
        if (!byName.isEmpty() && byId != byName) {
            conflicts.append(QStringLiteral("%1（PID=%2，名称=%3）").arg(usbId, byId, byName));
            continue;
        }
        const QString module = byId;
        const QString uniqueKey = line.toLower();
        if (seenLines.contains(uniqueKey)) {
            continue;
        }
        seenLines.insert(uniqueKey);
        Candidate candidate;
        candidate.module = module;
        candidate.usbId = usbId;
        candidate.line = line;
        candidate.productText = line.mid(match.capturedEnd()).trimmed();
        candidates.append(candidate);
    }

    if (!conflicts.isEmpty()) {
        selection.state = QStringLiteral("identity-conflict");
        selection.reason = QStringLiteral("USB PID 与产品名称冲突，拒绝选择拨号程序：%1")
                               .arg(conflicts.join(QStringLiteral("；")));
        return selection;
    }
    if (candidates.isEmpty()) {
        selection.reason = QStringLiteral("lsusb 未识别到受支持的 EM05、EC200A 或 RM500U 模组");
        return selection;
    }
    if (candidates.size() != 1) {
        selection.state = QStringLiteral("multiple-modems");
        QStringList found;
        for (const Candidate &candidate : candidates) {
            found.append(QStringLiteral("%1 (%2)").arg(candidate.module, candidate.usbId));
        }
        selection.reason = QStringLiteral("同时检测到多个受支持 4G/5G 模组：%1；请仅保留一个模组后拨号")
                               .arg(found.join(QStringLiteral("，")));
        return selection;
    }

    const Candidate candidate = candidates.first();
    selection.detected = true;
    selection.module = candidate.module;
    selection.displayName = modemDisplayNameForKind(candidate.module);
    selection.usbId = candidate.usbId;
    selection.usbLine = candidate.line;
    selection.productText = candidate.productText;
    selection.dialer = modemDialerForKind(candidate.module);
    selection.dialerPath = modemDialerPathForSocFamily(
        selection.socFamily, selection.dialer, &selection.legacyDirectory);

    const QFileInfo dialerInfo(selection.dialerPath);
    if (!dialerInfo.isFile() || !dialerInfo.isExecutable()) {
        selection.state = QStringLiteral("dialer-missing");
        selection.reason = QStringLiteral("已识别 %1（%2），但在 %3 中未找到可执行的 %4")
                               .arg(selection.displayName, selection.usbId,
                                    topeetTestRootForHelper(), selection.dialer);
        return selection;
    }
    if (!isPathInsideCanonicalRoot(selection.dialerPath, topeetTestRootForHelper())) {
        selection.state = QStringLiteral("unsafe-dialer-path");
        selection.reason = QStringLiteral("匹配拨号程序不在受控 /topeet_test 目录内：%1").arg(selection.dialerPath);
        return selection;
    }

    selection.dialerAvailable = true;
    selection.state = QStringLiteral("ready");
    selection.reason = QStringLiteral("已识别 %1（%2），将使用 %3")
                           .arg(selection.displayName, selection.usbId, selection.dialer);
    return selection;
}

QVariantMap cellularModemStatusMap(const CellularModemSelection &selection,
                                   const QStringList &devices)
{
    QVariantMap status;
    const QString preferredInterface = preferredCellularInterfaceFromDiscovery(devices);
    const bool dhcpClientRequired = modemDialerRequiresDhclient(selection.dialer);
    const QString dhcpClientPath = dhcpClientRequired
        ? commandPathForHelper(QStringLiteral("dhclient")) : QString();
    status.insert(QStringLiteral("detected"), selection.detected);
    status.insert(QStringLiteral("lsusbAvailable"), selection.lsusbAvailable);
    status.insert(QStringLiteral("state"), selection.state);
    status.insert(QStringLiteral("module"), selection.module);
    status.insert(QStringLiteral("displayName"), selection.displayName);
    status.insert(QStringLiteral("usbId"), selection.usbId);
    status.insert(QStringLiteral("usbLine"), selection.usbLine);
    status.insert(QStringLiteral("productText"), selection.productText);
    status.insert(QStringLiteral("socFamily"), selection.socFamily);
    status.insert(QStringLiteral("legacyDirectory"), selection.legacyDirectory);
    status.insert(QStringLiteral("dialer"), selection.dialer);
    status.insert(QStringLiteral("dialerPath"), selection.dialerPath);
    status.insert(QStringLiteral("dialerAvailable"), selection.dialerAvailable);
    status.insert(QStringLiteral("dhcpClientRequired"), dhcpClientRequired);
    status.insert(QStringLiteral("dhcpClientAvailable"), !dhcpClientRequired || !dhcpClientPath.isEmpty());
    status.insert(QStringLiteral("dhcpClientPath"), dhcpClientPath);
    status.insert(QStringLiteral("preferredInterface"), preferredInterface);
    status.insert(QStringLiteral("interfaceAvailable"), !preferredInterface.isEmpty());
    status.insert(QStringLiteral("message"), selection.reason);
    return status;
}

int alsaCardIndexFromDevice(const QString &device)
{
    static const QRegularExpression expression(QStringLiteral("^(?:plug)?hw:([0-9]{1,3}),[0-9]{1,3}$"),
                                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(device.trimmed());
    if (!match.hasMatch()) {
        return -1;
    }

    bool ok = false;
    const int card = match.captured(1).toInt(&ok);
    return ok ? card : -1;
}

QString speedFromDdOutput(const QString &output)
{
    QRegularExpression speedExpression(QStringLiteral("([0-9]+(?:\\.[0-9]+)?\\s*(?:[KMGT]?i?B|[KMGT]?B)/s)"));
    QRegularExpressionMatchIterator matches = speedExpression.globalMatch(output);
    QString speed;
    while (matches.hasNext()) {
        speed = matches.next().captured(1).simplified();
    }
    return speed;
}

struct BackendOperationResult {
    bool success = false;
    QString result;
    QString status;
    QVariantList storageDevices;
    QVariantList fileEntries;
    QString currentPath;
    QStringList serialDevices;
    QVariantList ethernetInterfaces;
    QStringList canInterfaces;
    QStringList cellularDevices;
    QVariantMap cellularModemStatus;
    QStringList watchdogDevices;
    QVector<qint64> audioPids;
    QVector<qint64> beepPids;
    int audioVolumePercent = -1;
    bool refreshNetworkDevices = false;
    bool storageDevicesValid = false;
    bool fileEntriesValid = false;
    bool serialDevicesValid = false;
    bool ethernetInterfacesValid = false;
    bool canInterfacesValid = false;
    bool cellularDevicesValid = false;
    bool cellularModemStatusValid = false;
    bool watchdogDevicesValid = false;
    qint64 cellularDialerPid = 0;
    QString cellularDialerPath;
    bool cellularDialerPidValid = false;
    bool clearCellularDialerPid = false;
    bool backlightValid = false;
    QString backlightName;
    QString backlightPath;
    int backlightMaxBrightness = 0;
    int backlightBrightness = 0;
};

BackendOperationResult startCanLoopSender(const QString &interfaceName, int bitrate)
{
    BackendOperationResult result;
    const QString iface = interfaceName.trimmed();
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No CAN"))) {
        result.status = QStringLiteral("No CAN interface selected");
        return result;
    }

    QString stopStatus;
    stopCanLoopSender(&stopStatus);

    QString errorText;
    const int testSocket = openCanSocket(iface, &errorText);
    if (testSocket < 0) {
        result.status = QStringLiteral("CAN socket open failed: %1").arg(errorText);
        return result;
    }
    ::close(testSocket);

    const int clampedBitrate = qBound(50000, bitrate, 1000000);
    auto state = std::make_shared<CanLoopSenderState>();
    state->interfaceName = iface;
    state->bitrate = clampedBitrate;
    state->worker = std::thread([state]() {
        QString threadError;
        const int socketFd = openCanSocket(state->interfaceName, &threadError);
        if (socketFd < 0) {
            return;
        }

        while (!state->stopRequested.load()) {
            QString writeError;
            if (!writeReferenceCanFrame(socketFd, &writeError)) {
                break;
            }
            for (int i = 0; i < 10 && !state->stopRequested.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        ::close(socketFd);
    });

    {
        std::lock_guard<std::mutex> lock(canLoopMutex());
        canLoopSender() = state;
    }

    result.success = true;
    result.result = iface;
    result.status = QStringLiteral("CAN reference sender started: %1 %2 bps").arg(iface).arg(clampedBitrate);
    return result;
}

BackendOperationResult startWatchdogTest(const QString &device, const QStringList &devices, int timeoutSeconds)
{
    BackendOperationResult result;
    QString node = device.trimmed();
    if (devices.isEmpty()) {
        result.status = QStringLiteral("未发现可用的看门狗设备，拒绝启动复位测试");
        return result;
    }
    if (node.isEmpty()) {
        node = devices.first();
    }
    if (!devices.contains(node)) {
        result.status = QStringLiteral("看门狗设备不在当前发现列表中：%1").arg(node);
        return result;
    }
    const int requestedTimeout = qBound(5, timeoutSeconds, 120);

    stopWatchdogTest();

    const QByteArray nodeBytes = node.toLocal8Bit();
    int fd = ::open(nodeBytes.constData(), O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        result.status = QStringLiteral("watchdog open failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }

    int timeout = requestedTimeout;
    if (::ioctl(fd, WDIOC_SETTIMEOUT, &timeout) < 0) {
        const QString error = QString::fromLocal8Bit(std::strerror(errno));
        closeWatchdogDeviceSafely(fd);
        result.status = QStringLiteral("watchdog timeout set failed: %1").arg(error);
        return result;
    }
    if (::ioctl(fd, WDIOC_GETTIMEOUT, &timeout) < 0) {
        timeout = requestedTimeout;
    }

    int keepalive = 0;
    if (::ioctl(fd, WDIOC_KEEPALIVE, &keepalive) < 0) {
        const QString error = QString::fromLocal8Bit(std::strerror(errno));
        closeWatchdogDeviceSafely(fd);
        result.status = QStringLiteral("watchdog keepalive failed: %1").arg(error);
        return result;
    }

    auto state = std::make_shared<WatchdogTestState>();
    state->device = node;
    state->timeout = timeout;
    state->worker = std::thread([state, fd]() {
        while (!state->stopRequested.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        // A manually cancelled test should try to disarm the watchdog before
        // releasing the descriptor. Drivers configured with nowayout may
        // refuse this; those drivers still obey the explicitly requested
        // reset-test semantics.
        closeWatchdogDeviceSafely(fd);
    });

    {
        std::lock_guard<std::mutex> lock(watchdogTestMutex());
        watchdogTest() = state;
    }

    result.success = true;
    result.result = node;
    result.status = QStringLiteral("已确认看门狗复位测试：%1 在约 %2 秒后将停止喂狗并触发系统复位；迅为开发板测试平台与 Web 连接会中断")
                        .arg(node)
                        .arg(timeout);
    return result;
}

bool processIsAlive(qint64 pid);
bool waitForProcessAlive(qint64 pid, int settleMs);
bool processStoppedAfterDelay(qint64 pid, unsigned long delayMs);
bool terminateProcess(qint64 pid);
bool processCommandMatches(qint64 pid, const QStringList &allowedCommandTokens);
QStringList audioProcessKillTokens();
void stopProcessList(QVector<qint64> pids, const QStringList &allowedCommandTokens = QStringList());
QString readTextFileForHelper(const QString &path);
bool writeTextFileForHelper(const QString &path, const QString &text, QString *errorText);
bool startDetachedWithTimeoutForHelper(const QString &program, const QStringList &arguments, int timeoutSeconds, qint64 *pid);

QThreadPool *audioThreadPool()
{
    static QThreadPool pool;
    static bool configured = false;
    if (!configured) {
        pool.setMaxThreadCount(2);
        pool.setExpiryTimeout(30000);
        configured = true;
    }
    return &pool;
}

constexpr auto kDefaultPingTarget = "www.baidu.com";
constexpr int kPingPacketCount = 3;
constexpr int kPingReplyTimeoutSeconds = 2;
constexpr int kPingDeadlineSeconds = 8;
constexpr int kPingProcessTimeoutMs = 9500;

bool isSafeNetworkInterfaceName(const QString &interfaceName)
{
    static const QRegularExpression expression(
        QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.:-]{0,62}$"));
    return expression.match(interfaceName.trimmed()).hasMatch();
}

bool isEthernetInterfaceName(const QString &interfaceName)
{
    const QString name = interfaceName.trimmed();
    return name.startsWith(QStringLiteral("eth"), Qt::CaseInsensitive)
        || name.startsWith(QStringLiteral("en"), Qt::CaseInsensitive);
}

bool isValidIpv4AddressForHelper(const QString &address)
{
    const QStringList octets = address.trimmed().split(QLatin1Char('.'));
    if (octets.size() != 4) {
        return false;
    }
    for (const QString &octetText : octets) {
        if (octetText.isEmpty() || octetText.size() > 3) {
            return false;
        }
        bool ok = false;
        const int octet = octetText.toInt(&ok);
        if (!ok || octet < 0 || octet > 255) {
            return false;
        }
    }
    return true;
}

bool isOptionalIpv4AddressForHelper(const QString &address)
{
    return address.trimmed().isEmpty() || isValidIpv4AddressForHelper(address);
}

bool isSafePingTarget(const QString &target)
{
    QString host = target.trimmed();
    if (host.endsWith(QLatin1Char('.'))) {
        host.chop(1);
    }
    if (host.isEmpty() || host.size() > 253 || host.startsWith(QLatin1Char('-'))
        || host.contains(QRegularExpression(QStringLiteral("[\\s/\\\\:]")))) {
        return false;
    }
    if (isValidIpv4AddressForHelper(host)) {
        return true;
    }

    const QStringList labels = host.split(QLatin1Char('.'));
    if (labels.isEmpty()) {
        return false;
    }
    static const QRegularExpression labelExpression(
        QStringLiteral("^[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?$"));
    for (const QString &label : labels) {
        if (!labelExpression.match(label).hasMatch()) {
            return false;
        }
    }
    return true;
}

bool isExistingNetworkInterface(const QString &interfaceName)
{
    return isSafeNetworkInterfaceName(interfaceName)
        && QFileInfo::exists(networkInterfacePathForHelper(interfaceName));
}

QString ethernetDiagnosticState(bool linkUp, bool ipv4Assigned)
{
    if (!linkUp) {
        return QStringLiteral("link-down");
    }
    if (!ipv4Assigned) {
        return QStringLiteral("link-up-no-ipv4");
    }
    return QStringLiteral("ready");
}

QString interfaceIpv4AddressForHelper(const QString &interfaceName)
{
    const QByteArray ifaceName = interfaceName.trimmed().toLocal8Bit();
    if (ifaceName.isEmpty()) {
        return QString();
    }

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return QString();
    }

    ifreq request;
    std::memset(&request, 0, sizeof(request));
    request.ifr_addr.sa_family = AF_INET;
    std::strncpy(request.ifr_name, ifaceName.constData(), IFNAMSIZ - 1);

    QString address;
    if (::ioctl(fd, SIOCGIFADDR, &request) == 0) {
        const auto *socketAddress = reinterpret_cast<sockaddr_in *>(&request.ifr_addr);
        char buffer[INET_ADDRSTRLEN] = {0};
        if (::inet_ntop(AF_INET, &socketAddress->sin_addr, buffer, sizeof(buffer))) {
            address = QString::fromLatin1(buffer);
        }
    }

    ::close(fd);
    return address;
}

BackendOperationResult scanCachedBoardDevicesWithCommand(const QString &socFamily = QString())
{
    BackendOperationResult result;

    QDir devDir(deviceRootForHelper());
    const QStringList serialPatterns = {
        QStringLiteral("ttyS*"),
        QStringLiteral("ttyAMA*"),
        QStringLiteral("ttyTHS*"),
        QStringLiteral("ttySC*"),
        QStringLiteral("ttyLP*"),
        QStringLiteral("ttyFIQ*"),
        QStringLiteral("ttyUSB*"),
        QStringLiteral("ttyACM*")
    };
    for (const QString &pattern : serialPatterns) {
        const QFileInfoList infos = devDir.entryInfoList(QStringList(pattern), QDir::System | QDir::Files, QDir::Name);
        for (const QFileInfo &info : infos) {
            appendUnique(&result.serialDevices, info.absoluteFilePath());
        }
    }
    result.serialDevicesValid = true;

    QDir netDir(networkClassRootForHelper());
    const QFileInfoList interfaces = netDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &iface : interfaces) {
        const QString name = iface.fileName();
        const QString path = iface.absoluteFilePath();
        if (name == QStringLiteral("lo")) {
            continue;
        }

        const QString operstate = readTextFileForHelper(path + QStringLiteral("/operstate"));
        bool linkTypeOk = false;
        const int linkType = readTextFileForHelper(
            path + QStringLiteral("/type")).toInt(&linkTypeOk);
        const bool cellularInterface = isCellularNetworkInterfaceName(name)
            || name.startsWith(QStringLiteral("wwp"));
        const bool wirelessInterface = QFileInfo::exists(path + QStringLiteral("/wireless"))
            || readTextFileForHelper(path + QStringLiteral("/uevent"))
                   .contains(QStringLiteral("DEVTYPE=wlan"));
        const bool physicalInterface = QFileInfo::exists(path + QStringLiteral("/device"))
            || QFileInfo(path + QStringLiteral("/device")).isSymLink();

        if ((linkTypeOk && linkType == 280)
            || (!linkTypeOk && (name.startsWith(QStringLiteral("can"))
                                || name.startsWith(QStringLiteral("vcan"))))) {
            appendUnique(&result.canInterfaces, name);
            continue;
        }

        const bool ethernetInterface = !wirelessInterface && !cellularInterface
            && ((linkTypeOk && linkType == 1 && physicalInterface)
                || (!linkTypeOk && (name.startsWith(QStringLiteral("eth"))
                                    || name.startsWith(QStringLiteral("en")))));
        if (ethernetInterface) {
            const QString address = readTextFileForHelper(path + QStringLiteral("/address"));
            const QString ipv4 = interfaceIpv4AddressForHelper(name);
            const QString carrier = readTextFileForHelper(path + QStringLiteral("/carrier"));
            const bool linkUp = carrier == QStringLiteral("1");
            const bool ipv4Assigned = !ipv4.isEmpty();
            const QString diagnostic = ethernetDiagnosticState(linkUp, ipv4Assigned);
            QVariantMap row;
            row.insert(QStringLiteral("name"), name);
            row.insert(QStringLiteral("interface"), name);
            row.insert(QStringLiteral("state"), operstate.isEmpty() ? QStringLiteral("unknown") : operstate);
            row.insert(QStringLiteral("address"), address);
            row.insert(QStringLiteral("macAddress"), address);
            // `ipv4` is deliberately empty when DHCP/static configuration has
            // not produced an address.  Consumers can use ipv4Display for a
            // human label without mistaking "Not assigned" for an address.
            row.insert(QStringLiteral("ipv4"), ipv4);
            row.insert(QStringLiteral("ipv4Address"), ipv4);
            row.insert(QStringLiteral("ipAddress"), ipv4);
            row.insert(QStringLiteral("ipv4Assigned"), ipv4Assigned);
            row.insert(QStringLiteral("ipv4Display"), ipv4Assigned ? ipv4 : QStringLiteral("Not assigned"));
            row.insert(QStringLiteral("carrier"), carrier);
            row.insert(QStringLiteral("linkUp"), linkUp);
            row.insert(QStringLiteral("linkDetected"), linkUp);
            row.insert(QStringLiteral("diagnosticCode"), diagnostic);
            row.insert(QStringLiteral("diagnostic"), diagnostic == QStringLiteral("ready")
                       ? QStringLiteral("链路和 IPv4 已就绪，可执行 Ping 测试")
                       : (diagnostic == QStringLiteral("link-up-no-ipv4")
                          ? QStringLiteral("网线链路已连接，等待 DHCP 分配 IPv4")
                          : QStringLiteral("未检测到网线链路，请检查网线和交换机端口")));
            row.insert(QStringLiteral("status"), diagnostic == QStringLiteral("ready")
                       ? QStringLiteral("Link and IPv4 are ready")
                       : (diagnostic == QStringLiteral("link-up-no-ipv4")
                          ? QStringLiteral("Ethernet link is up; IPv4 is not assigned")
                          : QStringLiteral("Ethernet cable/link is down")));
            result.ethernetInterfaces.append(row);
        }

        if (cellularInterface) {
            appendUnique(&result.cellularDevices,
                         QStringLiteral("%1 (%2)").arg(name, operstate.isEmpty() ? QStringLiteral("unknown") : operstate));
        }
    }

    const QFileInfoList modemDevices = devDir.entryInfoList(QStringList()
                                                                << QStringLiteral("ttyUSB*")
                                                                << QStringLiteral("ttyACM*")
                                                                << QStringLiteral("cdc-wdm*")
                                                                << QStringLiteral("mhi_*")
                                                                << QStringLiteral("wwan*"),
                                                            QDir::System | QDir::Files,
                                                            QDir::Name);
    for (const QFileInfo &device : modemDevices) {
        appendUnique(&result.cellularDevices, device.absoluteFilePath());
    }
    result.ethernetInterfacesValid = true;
    result.canInterfacesValid = true;
    result.cellularDevicesValid = true;
    const CellularModemSelection modemSelection = selectCellularModemForHelper(socFamily);
    result.cellularModemStatus = cellularModemStatusMap(modemSelection, result.cellularDevices);
    result.cellularModemStatusValid = true;

    const QFileInfoList watchdogInfos = devDir.entryInfoList(QStringList() << QStringLiteral("watchdog*"),
                                                             QDir::System | QDir::Files,
                                                             QDir::Name);
    for (const QFileInfo &info : watchdogInfos) {
        appendUnique(&result.watchdogDevices, info.absoluteFilePath());
    }
    result.watchdogDevicesValid = true;

    QDir backlightDir(QStringLiteral("/sys/class/backlight"));
    const QFileInfoList backlightEntries = backlightDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : backlightEntries) {
        const QString path = entry.absoluteFilePath();
        const int maxBrightness = readTextFileForHelper(path + QStringLiteral("/max_brightness")).toInt();
        const int brightness = readTextFileForHelper(path + QStringLiteral("/brightness")).toInt();
        if (maxBrightness > 0) {
            result.backlightValid = true;
            result.backlightName = entry.fileName();
            result.backlightPath = path;
            result.backlightMaxBrightness = maxBrightness;
            result.backlightBrightness = qBound(0, brightness, maxBrightness);
            break;
        }
    }

    result.success = true;
    return result;
}

BackendOperationResult refreshCachedBoardStateWithCommand(const QString &socFamily)
{
    BackendOperationResult result;

    const BackendOperationResult devices = scanCachedBoardDevicesWithCommand(socFamily);
    result.serialDevices = devices.serialDevices;
    result.ethernetInterfaces = devices.ethernetInterfaces;
    result.canInterfaces = devices.canInterfaces;
    result.cellularDevices = devices.cellularDevices;
    result.cellularModemStatus = devices.cellularModemStatus;
    result.watchdogDevices = devices.watchdogDevices;
    result.serialDevicesValid = devices.serialDevicesValid;
    result.ethernetInterfacesValid = devices.ethernetInterfacesValid;
    result.canInterfacesValid = devices.canInterfacesValid;
    result.cellularDevicesValid = devices.cellularDevicesValid;
    result.cellularModemStatusValid = devices.cellularModemStatusValid;
    result.watchdogDevicesValid = devices.watchdogDevicesValid;
    result.backlightValid = devices.backlightValid;
    result.backlightName = devices.backlightName;
    result.backlightPath = devices.backlightPath;
    result.backlightMaxBrightness = devices.backlightMaxBrightness;
    result.backlightBrightness = devices.backlightBrightness;

    const auto mixerVolume = []() -> int {
        int exitCode = -1;
        QString output;
        if (!commandPathForHelper(QStringLiteral("pactl")).isEmpty()) {
            output = runCommandForHelper(QStringLiteral("pactl"),
                                         QStringList() << QStringLiteral("get-sink-volume")
                                                       << QStringLiteral("@DEFAULT_SINK@"),
                                         3000,
                                         &exitCode);
        }

        if ((output.isEmpty() || exitCode != 0) && !commandPathForHelper(QStringLiteral("amixer")).isEmpty()) {
            output = runCommandForHelper(QStringLiteral("amixer"),
                                         QStringList() << QStringLiteral("get") << QStringLiteral("Master"),
                                         3000,
                                         &exitCode);
        }

        const QRegularExpression percentExpression(QStringLiteral("([0-9]{1,3})%"));
        const QRegularExpressionMatch match = percentExpression.match(output);
        if (!match.hasMatch()) {
            return -1;
        }

        bool ok = false;
        const int percent = match.captured(1).toInt(&ok);
        return ok ? qBound(0, percent, 100) : -1;
    };

    result.audioVolumePercent = mixerVolume();
    result.success = true;
    result.status = devices.status.isEmpty() ? QStringLiteral("Board backend ready") : devices.status;
    return result;
}

BackendOperationResult scanFileEntriesWithCommand(const QString &path)
{
    BackendOperationResult result;
    const QString requestedPath = path.trimmed().isEmpty() ? QDir::homePath() : path.trimmed();
    QFileInfo info(requestedPath);
    if (!info.exists() || !info.isDir()) {
        result.result = requestedPath;
        result.status = QStringLiteral("Not a directory: %1").arg(requestedPath);
        return result;
    }

    QDir dir(info.absoluteFilePath());
    const QFileInfoList infos = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot,
                                                  QDir::DirsFirst | QDir::Name);
    QVariantList entries;
    const int maxEntries = qMin(infos.size(), 24);
    for (int i = 0; i < maxEntries; ++i) {
        const QFileInfo &entryInfo = infos.at(i);
        QVariantMap row;
        row.insert(QStringLiteral("name"), entryInfo.fileName() + (entryInfo.isDir() ? QStringLiteral("/") : QString()));
        row.insert(QStringLiteral("path"), entryInfo.absoluteFilePath());
        row.insert(QStringLiteral("directory"), entryInfo.isDir());
        row.insert(QStringLiteral("size"), entryInfo.isDir()
                                         ? QStringLiteral("Folder")
                                         : QStringLiteral("%1 KB").arg(qMax<qint64>(1, entryInfo.size() / 1024)));
        entries.append(row);
    }

    result.success = true;
    result.result = dir.absolutePath();
    result.status = QStringLiteral("Opened %1").arg(dir.absolutePath());
    result.currentPath = dir.absolutePath();
    result.fileEntries = entries;
    result.fileEntriesValid = true;
    return result;
}

speed_t serialSpeedForHelper(int baudRate)
{
    switch (baudRate) {
    case 4800:
        return B4800;
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 230400:
#ifdef B230400
        return B230400;
#else
        return 0;
#endif
    case 460800:
#ifdef B460800
        return B460800;
#else
        return 0;
#endif
    case 921600:
#ifdef B921600
        return B921600;
#else
        return 0;
#endif
    case 115200:
    default:
        return B115200;
    }
}

bool configureSerialPortForHelper(int fd, int baudRate, int dataBits, const QString &parity, int stopBits, const QString &flowControl, QString *errorText)
{
    termios options;
    if (::tcgetattr(fd, &options) != 0) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        return false;
    }

    ::cfmakeraw(&options);
    const speed_t speed = serialSpeedForHelper(baudRate);
    if (speed == 0) {
        if (errorText) {
            *errorText = QStringLiteral("unsupported baud rate on this platform: %1").arg(baudRate);
        }
        return false;
    }
    ::cfsetispeed(&options, speed);
    ::cfsetospeed(&options, speed);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~CSIZE;
    switch (qBound(5, dataBits, 8)) {
    case 5:
        options.c_cflag |= CS5;
        break;
    case 6:
        options.c_cflag |= CS6;
        break;
    case 7:
        options.c_cflag |= CS7;
        break;
    case 8:
    default:
        options.c_cflag |= CS8;
        break;
    }

    const QString normalizedParity = parity.trimmed().toLower();
    if (normalizedParity == QStringLiteral("even") || normalizedParity == QStringLiteral("e") || normalizedParity == QStringLiteral("偶校验")) {
        options.c_cflag |= PARENB;
        options.c_cflag &= ~PARODD;
    } else if (normalizedParity == QStringLiteral("odd") || normalizedParity == QStringLiteral("o") || normalizedParity == QStringLiteral("奇校验")) {
        options.c_cflag |= PARENB;
        options.c_cflag |= PARODD;
    } else {
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~PARODD;
    }

    if (stopBits >= 2) {
        options.c_cflag |= CSTOPB;
    } else {
        options.c_cflag &= ~CSTOPB;
    }

    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    const QString normalizedFlow = flowControl.trimmed().toLower();
    if (normalizedFlow == QStringLiteral("software") || normalizedFlow == QStringLiteral("xonxoff") || normalizedFlow == QStringLiteral("xon/xoff")) {
        options.c_iflag |= (IXON | IXOFF | IXANY);
    }
#ifdef CRTSCTS
    if (normalizedFlow == QStringLiteral("hardware") || normalizedFlow == QStringLiteral("rtscts") || normalizedFlow == QStringLiteral("rts/cts")) {
        options.c_cflag |= CRTSCTS;
    } else {
        options.c_cflag &= ~CRTSCTS;
    }
#endif
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    if (::tcsetattr(fd, TCSANOW, &options) != 0) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        return false;
    }
    return true;
}

int netmaskPrefixLengthForHelper(const QString &netmask)
{
    const QString trimmed = netmask.trimmed();
    if (trimmed.isEmpty()) {
        return 24;
    }

    bool prefixOk = false;
    const int directPrefix = trimmed.toInt(&prefixOk);
    if (prefixOk && directPrefix >= 0 && directPrefix <= 32) {
        return directPrefix;
    }

    const QStringList octets = trimmed.split(QChar('.'));
    if (octets.size() != 4) {
        return -1;
    }

    int prefix = 0;
    bool zeroSeen = false;
    for (const QString &octetText : octets) {
        bool ok = false;
        const int octet = octetText.toInt(&ok);
        if (!ok || octet < 0 || octet > 255) {
            return -1;
        }
        for (int bit = 7; bit >= 0; --bit) {
            const bool one = (octet & (1 << bit)) != 0;
            if (one && zeroSeen) {
                return -1;
            }
            if (one) {
                ++prefix;
            } else {
                zeroSeen = true;
            }
        }
    }
    return prefix;
}

QString nmcliConnectionNameForDeviceForHelper(const QString &interfaceName)
{
    const QString iface = interfaceName.trimmed();
    if (iface.isEmpty() || commandPathForHelper(QStringLiteral("nmcli")).isEmpty()) {
        return QString();
    }

    int exitCode = -1;
    const QString activeOutput = runCommandForHelper(QStringLiteral("nmcli"),
                                                     QStringList() << QStringLiteral("-t")
                                                                   << QStringLiteral("-e") << QStringLiteral("yes")
                                                                   << QStringLiteral("-f") << QStringLiteral("NAME,DEVICE")
                                                                   << QStringLiteral("connection") << QStringLiteral("show")
                                                                   << QStringLiteral("--active"),
                                                     5000,
                                                     &exitCode);
    const QStringList activeLines = activeOutput.split(QChar('\n'), boardlab::kSkipEmptyParts);
    for (const QString &line : activeLines) {
        const QStringList parts = splitEscapedNmcliLine(line);
        if (parts.value(1).trimmed() == iface) {
            return parts.value(0);
        }
    }

    const QString deviceOutput = runCommandForHelper(QStringLiteral("nmcli"),
                                                     QStringList() << QStringLiteral("-t")
                                                                   << QStringLiteral("-e") << QStringLiteral("yes")
                                                                   << QStringLiteral("-f") << QStringLiteral("DEVICE,CONNECTION")
                                                                   << QStringLiteral("device") << QStringLiteral("status"),
                                                     5000,
                                                     &exitCode);
    const QStringList deviceLines = deviceOutput.split(QChar('\n'), boardlab::kSkipEmptyParts);
    for (const QString &line : deviceLines) {
        const QStringList parts = splitEscapedNmcliLine(line);
        if (parts.value(0).trimmed() == iface) {
            const QString connection = parts.value(1).trimmed();
            if (!connection.isEmpty() && connection != QStringLiteral("--")) {
                return connection;
            }
        }
    }

    return QString();
}

BackendOperationResult configureEthernetWithNmcli(const QString &operation, const QVariantMap &parameters)
{
    BackendOperationResult result;
    result.refreshNetworkDevices = true;
    const QString iface = parameters.value(QStringLiteral("interface")).toString().trimmed();
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No Ethernet"))) {
        result.status = QStringLiteral("No Ethernet interface selected");
        return result;
    }
    if (!isEthernetInterfaceName(iface) || !isExistingNetworkInterface(iface)) {
        result.status = QStringLiteral("Ethernet interface is unavailable or invalid: %1").arg(iface.left(64));
        return result;
    }
    if (commandPathForHelper(QStringLiteral("nmcli")).isEmpty()) {
        result.status = QStringLiteral("nmcli not found; install NetworkManager");
        return result;
    }

    QString connection = nmcliConnectionNameForDeviceForHelper(iface);
    QString bootstrapOutput;
    if (connection.isEmpty() && operation == QStringLiteral("ethernet-dhcp")) {
        int connectExit = -1;
        bootstrapOutput = runCommandForHelper(QStringLiteral("nmcli"),
                                              QStringList() << QStringLiteral("-w") << QStringLiteral("12")
                                                            << QStringLiteral("device") << QStringLiteral("connect") << iface,
                                              12000,
                                              &connectExit);
        connection = nmcliConnectionNameForDeviceForHelper(iface);
    }
    if (connection.isEmpty()) {
        const QString profileName = QStringLiteral("rk-shell-%1").arg(iface);
        int addExit = -1;
        const QString addOutput = runCommandForHelper(QStringLiteral("nmcli"),
                                                      QStringList() << QStringLiteral("connection") << QStringLiteral("add")
                                                                    << QStringLiteral("type") << QStringLiteral("ethernet")
                                                                    << QStringLiteral("ifname") << iface
                                                                    << QStringLiteral("con-name") << profileName,
                                                      8000,
                                                      &addExit);
        if (!addOutput.trimmed().isEmpty()) {
            bootstrapOutput = bootstrapOutput.isEmpty() ? addOutput : bootstrapOutput + QStringLiteral("; ") + addOutput;
        }
        connection = nmcliConnectionNameForDeviceForHelper(iface);
        if (connection.isEmpty() && addExit == 0) {
            connection = profileName;
        }
    }
    if (connection.isEmpty()) {
        result.status = QStringLiteral("No NetworkManager profile for %1: %2").arg(iface, compactCommandOutput(bootstrapOutput));
        return result;
    }

    int exitCode = -1;
    QString output;
    if (operation == QStringLiteral("ethernet-dhcp")) {
        const QString dns1 = parameters.value(QStringLiteral("dns1")).toString().trimmed();
        const QString dns2 = parameters.value(QStringLiteral("dns2")).toString().trimmed();
        if (!isOptionalIpv4AddressForHelper(dns1) || !isOptionalIpv4AddressForHelper(dns2)) {
            result.status = QStringLiteral("DHCP DNS must contain valid IPv4 addresses");
            return result;
        }
        QString dnsValue;
        if (!dns1.isEmpty() && !dns2.isEmpty()) {
            dnsValue = dns1 + QStringLiteral(" ") + dns2;
        } else if (!dns1.isEmpty()) {
            dnsValue = dns1;
        } else if (!dns2.isEmpty()) {
            dnsValue = dns2;
        }

        QStringList modifyArgs;
        modifyArgs << QStringLiteral("connection") << QStringLiteral("modify") << connection
                   << QStringLiteral("ipv4.method") << QStringLiteral("auto")
                   << QStringLiteral("ipv4.addresses") << QString()
                   << QStringLiteral("ipv4.gateway") << QString();
        if (!dnsValue.isEmpty()) {
            modifyArgs << QStringLiteral("ipv4.dns") << dnsValue
                       << QStringLiteral("ipv4.ignore-auto-dns") << QStringLiteral("yes");
        } else {
            modifyArgs << QStringLiteral("ipv4.dns") << QString()
                       << QStringLiteral("ipv4.ignore-auto-dns") << QStringLiteral("no");
        }

        output = runCommandForHelper(QStringLiteral("nmcli"), modifyArgs, 8000, &exitCode);
        if (exitCode != 0) {
            result.status = QStringLiteral("DHCP profile update failed: %1").arg(output.left(120));
            return result;
        }

        output = runCommandForHelper(QStringLiteral("nmcli"),
                                     QStringList() << QStringLiteral("-w") << QStringLiteral("20")
                                                   << QStringLiteral("connection") << QStringLiteral("up") << connection
                                                   << QStringLiteral("ifname") << iface,
                                     20000,
                                     &exitCode);
        if (exitCode == 0) {
            result.success = true;
            result.result = iface;
            result.status = QStringLiteral("DHCP applied on %1 via %2").arg(iface, connection);
            return result;
        }

        result.status = QStringLiteral("DHCP request failed: %1").arg(output.left(120));
        return result;
    }

    const QString ipAddress = parameters.value(QStringLiteral("address")).toString().trimmed();
    const QString mask = parameters.value(QStringLiteral("netmask")).toString().trimmed().isEmpty()
        ? QStringLiteral("255.255.255.0")
        : parameters.value(QStringLiteral("netmask")).toString().trimmed();
    const QString gateway = parameters.value(QStringLiteral("gateway")).toString().trimmed();
    const QString dns1 = parameters.value(QStringLiteral("dns1")).toString().trimmed();
    const QString dns2 = parameters.value(QStringLiteral("dns2")).toString().trimmed();
    if (ipAddress.isEmpty()) {
        result.status = QStringLiteral("Static IP address is empty");
        return result;
    }
    if (!isValidIpv4AddressForHelper(ipAddress)) {
        result.status = QStringLiteral("Invalid static IPv4 address: %1").arg(ipAddress.left(64));
        return result;
    }
    if (!isOptionalIpv4AddressForHelper(gateway)) {
        result.status = QStringLiteral("Invalid IPv4 gateway: %1").arg(gateway.left(64));
        return result;
    }
    if (!isOptionalIpv4AddressForHelper(dns1) || !isOptionalIpv4AddressForHelper(dns2)) {
        result.status = QStringLiteral("Static DNS must contain valid IPv4 addresses");
        return result;
    }
    const int prefix = netmaskPrefixLengthForHelper(mask);
    if (prefix < 0) {
        result.status = QStringLiteral("Invalid IPv4 netmask or prefix: %1").arg(mask);
        return result;
    }

    QString dnsValue;
    if (!dns1.isEmpty() && !dns2.isEmpty()) {
        dnsValue = dns1 + QStringLiteral(" ") + dns2;
    } else if (!dns1.isEmpty()) {
        dnsValue = dns1;
    } else if (!dns2.isEmpty()) {
        dnsValue = dns2;
    }

    QStringList modifyArgs;
    modifyArgs << QStringLiteral("connection") << QStringLiteral("modify") << connection
               << QStringLiteral("ipv4.method") << QStringLiteral("manual")
               << QStringLiteral("ipv4.addresses") << QStringLiteral("%1/%2").arg(ipAddress).arg(prefix)
               << QStringLiteral("ipv4.gateway") << gateway;
    if (!dnsValue.isEmpty()) {
        modifyArgs << QStringLiteral("ipv4.dns") << dnsValue;
    } else {
        modifyArgs << QStringLiteral("ipv4.dns") << QString();
    }
    output = runCommandForHelper(QStringLiteral("nmcli"), modifyArgs, 8000, &exitCode);
    if (exitCode != 0) {
        result.status = QStringLiteral("Static IP failed: %1").arg(output.left(120));
        return result;
    }

    output = runCommandForHelper(QStringLiteral("nmcli"),
                                 QStringList() << QStringLiteral("-w") << QStringLiteral("20")
                                               << QStringLiteral("connection") << QStringLiteral("up") << connection
                                               << QStringLiteral("ifname") << iface,
                                 20000,
                                 &exitCode);
    if (exitCode == 0) {
        result.success = true;
        result.result = QStringLiteral("%1/%2").arg(ipAddress).arg(prefix);
        result.status = QStringLiteral("%1 static IP set to %2/%3 via %4").arg(iface, ipAddress).arg(prefix).arg(connection);
        return result;
    }

    result.status = QStringLiteral("Static IP apply failed: %1").arg(output.left(120));
    return result;
}

BackendOperationResult refreshEthernetWithCommand(const QVariantMap &parameters)
{
    BackendOperationResult result;
    result.success = true;
    result.refreshNetworkDevices = true;
    result.result = parameters.value(QStringLiteral("interface")).toString().trimmed();
    result.status = result.result.isEmpty()
        ? QStringLiteral("Network status refresh requested")
        : QStringLiteral("Network status refresh requested: %1").arg(result.result);
    return result;
}

BackendOperationResult sendSerialTestFrameConfiguredWithDevice(const QVariantMap &parameters)
{
    BackendOperationResult result;
    const QString device = parameters.value(QStringLiteral("device")).toString().trimmed();
    if (device.isEmpty() || device.startsWith(QStringLiteral("No serial")) || device == QStringLiteral("No UART")) {
        result.status = QStringLiteral("No serial device selected");
        return result;
    }

    const int baudRate = parameters.value(QStringLiteral("baudRate"), 115200).toInt();
    const int dataBits = parameters.value(QStringLiteral("dataBits"), 8).toInt();
    const QString parity = parameters.value(QStringLiteral("parity"), QStringLiteral("None")).toString();
    const int stopBits = parameters.value(QStringLiteral("stopBits"), 1).toInt();
    const QString flowControl = parameters.value(QStringLiteral("flowControl"), QStringLiteral("None")).toString();

    const QByteArray devicePath = device.toLocal8Bit();
    const int fd = ::open(devicePath.constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        result.status = QStringLiteral("Serial open failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }

    QString error;
    if (!configureSerialPortForHelper(fd, baudRate, dataBits, parity, stopBits, flowControl, &error)) {
        ::close(fd);
        result.status = QStringLiteral("Serial configure failed: %1").arg(error);
        return result;
    }

    const unsigned char frame[] = {0x55, 0xAA, 0x01, 0x00, 0xFE};
    const ssize_t written = ::write(fd, frame, sizeof(frame));
    const int writeErrno = errno;
    ::tcdrain(fd);
    ::close(fd);

    if (written != static_cast<ssize_t>(sizeof(frame))) {
        result.status = QStringLiteral("Serial write failed: %1").arg(QString::fromLocal8Bit(std::strerror(writeErrno)));
        return result;
    }

    const QString normalizedParity = parity.trimmed();
    const QString normalizedFlow = flowControl.trimmed().isEmpty() ? QStringLiteral("None") : flowControl.trimmed();
    result.success = true;
    result.result = device;
    result.status = QStringLiteral("Serial TX %1 @%2 %3%4%5 flow=%6")
                        .arg(device)
                        .arg(baudRate)
                        .arg(dataBits)
                        .arg(normalizedParity.isEmpty() || normalizedParity.toLower() == QStringLiteral("none") ? QStringLiteral("N") : normalizedParity.left(1).toUpper())
                        .arg(stopBits)
                        .arg(normalizedFlow);
    return result;
}

BackendOperationResult readSerialSampleConfiguredWithDevice(const QVariantMap &parameters)
{
    BackendOperationResult result;
    const QString device = parameters.value(QStringLiteral("device")).toString().trimmed();
    if (device.isEmpty() || device.startsWith(QStringLiteral("No serial")) || device == QStringLiteral("No UART")) {
        result.status = QStringLiteral("No serial device selected");
        return result;
    }

    const int baudRate = parameters.value(QStringLiteral("baudRate"), 115200).toInt();
    const int dataBits = parameters.value(QStringLiteral("dataBits"), 8).toInt();
    const QString parity = parameters.value(QStringLiteral("parity"), QStringLiteral("None")).toString();
    const int stopBits = parameters.value(QStringLiteral("stopBits"), 1).toInt();
    const QString flowControl = parameters.value(QStringLiteral("flowControl"), QStringLiteral("None")).toString();

    const QByteArray devicePath = device.toLocal8Bit();
    const int fd = ::open(devicePath.constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        result.status = QStringLiteral("Serial open failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        return result;
    }

    QString error;
    if (!configureSerialPortForHelper(fd, baudRate, dataBits, parity, stopBits, flowControl, &error)) {
        ::close(fd);
        result.status = QStringLiteral("Serial configure failed: %1").arg(error);
        return result;
    }

    QByteArray data;
    char buffer[128];
    for (int i = 0; i < 5; ++i) {
        const ssize_t bytes = ::read(fd, buffer, sizeof(buffer));
        if (bytes > 0) {
            data.append(buffer, bytes);
            break;
        }
        QThread::msleep(20);
    }
    ::close(fd);

    result.success = true;
    if (data.isEmpty()) {
        result.result = QStringLiteral("<no data>");
        result.status = QStringLiteral("Serial read: no data available");
        return result;
    }

    result.result = QString::fromLocal8Bit(data.toHex(' ').toUpper());
    result.status = QStringLiteral("Serial read %1 byte(s)").arg(data.size());
    return result;
}

QString canInterfaceFromParameters(const QVariantMap &parameters)
{
    return parameters.value(QStringLiteral("interface")).toString().trimmed();
}

BackendOperationResult configureCanInterfaceWithCommand(const QVariantMap &parameters)
{
    BackendOperationResult result;
    result.refreshNetworkDevices = true;
    const QString iface = canInterfaceFromParameters(parameters);
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No CAN"))) {
        result.status = QStringLiteral("No CAN interface selected");
        return result;
    }
    const bool fdMode = parameters.value(QStringLiteral("fdMode")).toBool();
    if (fdMode) {
        result.status = QStringLiteral("当前平台参考帧测试不支持 CAN FD；请使用经典 CAN");
        return result;
    }
    if (commandPathForHelper(QStringLiteral("ip")).isEmpty()) {
        result.status = QStringLiteral("ip command not found; install iproute2");
        return result;
    }

    const int clampedBitrate = qBound(50000, parameters.value(QStringLiteral("bitrate"), 1000000).toInt(), 1000000);
    int exitCode = -1;
    runCommandForHelper(QStringLiteral("ip"), QStringList() << QStringLiteral("link") << QStringLiteral("set") << iface << QStringLiteral("down"), 3000, &exitCode);

    QStringList configureArgs;
    configureArgs << QStringLiteral("link") << QStringLiteral("set") << iface << QStringLiteral("type") << QStringLiteral("can")
                  << QStringLiteral("bitrate") << QString::number(clampedBitrate);
    const QString configureOutput = runCommandForHelper(QStringLiteral("ip"), configureArgs, 5000, &exitCode);
    if (exitCode != 0) {
        result.status = QStringLiteral("CAN configure failed: %1").arg(configureOutput.left(120));
        return result;
    }

    const QString upOutput = runCommandForHelper(QStringLiteral("ip"),
                                                 QStringList() << QStringLiteral("link") << QStringLiteral("set") << iface << QStringLiteral("up"),
                                                 5000,
                                                 &exitCode);
    if (exitCode != 0) {
        result.status = QStringLiteral("CAN up failed: %1").arg(upOutput.left(120));
        return result;
    }

    result.success = true;
    result.result = iface;
    result.status = QStringLiteral("%1 opened at %2 bps Classic CAN").arg(iface).arg(clampedBitrate);
    return result;
}

BackendOperationResult closeCanInterfaceWithCommand(const QVariantMap &parameters)
{
    BackendOperationResult result;
    result.refreshNetworkDevices = true;
    const QString iface = canInterfaceFromParameters(parameters);
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No CAN"))) {
        result.status = QStringLiteral("No CAN interface selected");
        return result;
    }
    if (commandPathForHelper(QStringLiteral("ip")).isEmpty()) {
        result.status = QStringLiteral("ip command not found; install iproute2");
        return result;
    }

    int exitCode = -1;
    const QString output = runCommandForHelper(QStringLiteral("ip"),
                                               QStringList() << QStringLiteral("link") << QStringLiteral("set") << iface << QStringLiteral("down"),
                                               5000,
                                               &exitCode);
    result.success = exitCode == 0;
    result.result = iface;
    result.status = result.success ? QStringLiteral("%1 closed").arg(iface)
                                   : QStringLiteral("CAN close failed: %1").arg(output.left(120));
    return result;
}

BackendOperationResult sendCanTestFrameWithSocket(const QVariantMap &parameters)
{
    BackendOperationResult result;
    const QString iface = canInterfaceFromParameters(parameters);
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No CAN"))) {
        result.status = QStringLiteral("No CAN interface selected");
        return result;
    }

    QString errorText;
    const int socketFd = openCanSocket(iface, &errorText);
    if (socketFd < 0) {
        result.status = QStringLiteral("CAN socket open failed: %1").arg(errorText);
        return result;
    }

    QString writeError;
    const bool sentAll = writeReferenceCanFrame(socketFd, &writeError);
    ::close(socketFd);

    result.success = sentAll;
    result.result = iface;
    result.status = sentAll ? QStringLiteral("CAN TX %1 Classic frame: std 0x11 [01 02]").arg(iface)
                            : QStringLiteral("CAN TX failed: %1").arg(writeError);
    return result;
}

BackendOperationResult readCanSampleWithSocket(const QVariantMap &parameters)
{
    BackendOperationResult result;
    const QString iface = canInterfaceFromParameters(parameters);
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No CAN"))) {
        result.status = QStringLiteral("No CAN interface selected");
        return result;
    }

    QString errorText;
    const int socketFd = openCanSocket(iface, &errorText);
    if (socketFd < 0) {
        result.status = QStringLiteral("CAN socket open failed: %1").arg(errorText);
        return result;
    }

    QStringList lines;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 1000;
    while (lines.size() < 5 && QDateTime::currentMSecsSinceEpoch() < deadline) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketFd, &readSet);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 120000;

        const int ready = ::select(socketFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(socketFd, &readSet)) {
            can_frame frame;
            std::memset(&frame, 0, sizeof(frame));
            const ssize_t readBytes = ::read(socketFd, &frame, sizeof(frame));
            if (readBytes == static_cast<ssize_t>(sizeof(frame))) {
                lines << QStringLiteral("%1 %2").arg(iface, formatCanFrame(frame));
            }
        } else if (ready < 0) {
            const QString error = QString::fromLocal8Bit(std::strerror(errno));
            ::close(socketFd);
            result.status = QStringLiteral("CAN RX failed: %1").arg(error);
            return result;
        }
    }
    ::close(socketFd);

    result.success = true;
    if (lines.isEmpty()) {
        result.result = QStringLiteral("<no CAN frames>");
        result.status = QStringLiteral("CAN RX: no frames");
        return result;
    }

    result.result = lines.join(QChar('\n'));
    result.status = QStringLiteral("CAN RX sample captured on %1").arg(iface);
    return result;
}

BackendOperationResult startCanReferenceSenderWithCommand(const QVariantMap &parameters)
{
    const int clampedBitrate = qBound(50000, parameters.value(QStringLiteral("bitrate"), 1000000).toInt(), 1000000);
    return startCanLoopSender(canInterfaceFromParameters(parameters), clampedBitrate);
}

BackendOperationResult stopCanReferenceSenderWithCommand(const QVariantMap &parameters)
{
    Q_UNUSED(parameters)
    BackendOperationResult result;
    result.success = stopCanLoopSender(&result.status);
    return result;
}

BackendOperationResult watchdogStatusWithCommand(const QVariantMap &parameters)
{
    BackendOperationResult result;
    const QStringList devices = parameters.value(QStringLiteral("devices")).toStringList();
    QStringList lines;
    lines << QStringLiteral("Watchdog status");
    lines << QStringLiteral("Devices: %1").arg(devices.isEmpty() ? QStringLiteral("none") : devices.join(QStringLiteral(", ")));
    int exitCode = -1;
    if (!commandPathForHelper(QStringLiteral("wdctl")).isEmpty()) {
        lines << QString();
        lines << QStringLiteral("[wdctl]");
        lines << runCommandForHelper(QStringLiteral("wdctl"), QStringList(), 5000, &exitCode);
    } else {
        lines << QStringLiteral("wdctl: missing");
    }

    result.success = true;
    result.result = lines.join(QChar('\n'));
    result.status = QStringLiteral("Watchdog status refreshed");
    return result;
}

BackendOperationResult startWatchdogReferenceTestWithCommand(const QVariantMap &parameters)
{
    if (!parameters.value(QStringLiteral("confirm")).toBool()) {
        BackendOperationResult result;
        result.status = QStringLiteral("watchdog-start requires confirm=true because this is an intentional board reset test");
        return result;
    }
    return startWatchdogTest(parameters.value(QStringLiteral("device")).toString(),
                             parameters.value(QStringLiteral("devices")).toStringList(),
                             parameters.value(QStringLiteral("timeout"), 5).toInt());
}

BackendOperationResult cellularStatusReportWithCommand(const QVariantMap &parameters)
{
    BackendOperationResult result;
    const QStringList devices = parameters.value(QStringLiteral("devices")).toStringList();
    const CellularModemSelection selection = selectCellularModemForHelper(
        parameters.value(QStringLiteral("socFamily")).toString());
    result.cellularModemStatus = cellularModemStatusMap(selection, devices);
    result.cellularModemStatusValid = true;

    QStringList lines;
    lines << QStringLiteral("Cellular / 4G / 5G status");
    lines << QStringLiteral("Detected devices: %1").arg(devices.isEmpty() ? QStringLiteral("none") : devices.join(QStringLiteral(", ")));
    lines << QString();

    lines << QStringLiteral("[USB modem selection]");
    lines << QStringLiteral("State: %1").arg(selection.state);
    lines << QStringLiteral("SoC layout: %1").arg(selection.legacyDirectory.isEmpty()
                                                       ? QStringLiteral("unsupported")
                                                       : QDir(topeetTestRootForHelper()).filePath(selection.legacyDirectory));
    lines << QStringLiteral("Module: %1").arg(selection.detected
                                                   ? QStringLiteral("%1 (%2)").arg(selection.displayName, selection.usbId)
                                                   : QStringLiteral("not recognized"));
    if (!selection.usbLine.isEmpty()) {
        lines << QStringLiteral("lsusb: %1").arg(selection.usbLine);
    }
    lines << QStringLiteral("Dialer: %1").arg(selection.dialer.isEmpty() ? QStringLiteral("not selected") : selection.dialer);
    lines << QStringLiteral("Dialer path: %1").arg(selection.dialerPath.isEmpty() ? QStringLiteral("not selected") : selection.dialerPath);
    lines << QStringLiteral("Dialer ready: %1").arg(selection.dialerAvailable ? QStringLiteral("yes") : QStringLiteral("no"));
    if (modemDialerRequiresDhclient(selection.dialer)) {
        const QString dhclientPath = commandPathForHelper(QStringLiteral("dhclient"));
        lines << QStringLiteral("DHCP client: %1").arg(dhclientPath.isEmpty()
                                                             ? QStringLiteral("missing (install isc-dhcp-client)")
                                                             : dhclientPath);
    }
    const QString dataInterface = preferredCellularInterfaceFromDiscovery(devices);
    lines << QStringLiteral("Data interface: %1").arg(dataInterface.isEmpty()
                                                         ? QStringLiteral("waiting for dialer discovery")
                                                         : dataInterface);
    lines << QStringLiteral("Reason: %1").arg(selection.reason);
    lines << QString();

    const QStringList commands = {
        QStringLiteral("mmcli"),
        QStringLiteral("nmcli"),
        QStringLiteral("ip"),
        QStringLiteral("ifconfig")
    };
    for (const QString &command : commands) {
        const QString path = commandPathForHelper(command);
        lines << QStringLiteral("%1: %2").arg(command, path.isEmpty() ? QStringLiteral("missing") : path);
    }

    int exitCode = -1;
    if (!commandPathForHelper(QStringLiteral("mmcli")).isEmpty()) {
        lines << QString();
        lines << QStringLiteral("[mmcli -L]");
        lines << runCommandForHelper(QStringLiteral("mmcli"), QStringList() << QStringLiteral("-L"), 5000, &exitCode);
    }
    if (!commandPathForHelper(QStringLiteral("nmcli")).isEmpty()) {
        lines << QString();
        lines << QStringLiteral("[nmcli device]");
        lines << runCommandForHelper(QStringLiteral("nmcli"), QStringList() << QStringLiteral("device"), 5000, &exitCode);
    }
    if (!commandPathForHelper(QStringLiteral("ip")).isEmpty()) {
        lines << QString();
        lines << QStringLiteral("[ip link]");
        lines << runCommandForHelper(QStringLiteral("ip"), QStringList() << QStringLiteral("-brief") << QStringLiteral("link"), 5000, &exitCode);
        lines << QString();
        lines << QStringLiteral("[ip addr]");
        lines << runCommandForHelper(QStringLiteral("ip"), QStringList() << QStringLiteral("-brief") << QStringLiteral("addr"), 5000, &exitCode);
    }

    result.success = true;
    result.result = lines.join(QChar('\n'));
    result.status = QStringLiteral("Cellular status refreshed");
    return result;
}

BackendOperationResult startCellularWithCommand(const QVariantMap &parameters)
{
    BackendOperationResult result;
    result.refreshNetworkDevices = true;
    const QStringList devices = parameters.value(QStringLiteral("devices")).toStringList();
    const CellularModemSelection selection = selectCellularModemForHelper(
        parameters.value(QStringLiteral("socFamily")).toString());
    result.cellularModemStatus = cellularModemStatusMap(selection, devices);
    result.cellularModemStatusValid = true;

    const QString apn = parameters.value(QStringLiteral("apn")).toString().trimmed();
    const QRegularExpression apnExpression(QStringLiteral("^[A-Za-z0-9._-]{1,64}$"));
    if (!apn.isEmpty() && !apnExpression.match(apn).hasMatch()) {
        result.status = QStringLiteral("APN format is invalid; use letters, digits, dot, underscore or hyphen");
        return result;
    }

    const QString requestedInterface = parameters.value(QStringLiteral("interface")).toString().trimmed();
    const QString fallbackInterface = requestedInterface.isEmpty()
        ? parameters.value(QStringLiteral("preferredInterface"), preferredCellularInterfaceFromDiscovery(devices)).toString().trimmed()
        : requestedInterface;

    // The supported Topeet modules are deliberately handled before
    // NetworkManager. RM500U CDC-NCM commonly exposes usb0 rather than a GSM
    // device, so nmcli cannot replace the vendor connection manager there.
    if (selection.detected) {
        if (!selection.dialerAvailable) {
            result.status = selection.reason;
            return result;
        }

        if (modemDialerRequiresDhclient(selection.dialer)
            && commandPathForHelper(QStringLiteral("dhclient")).isEmpty()) {
            result.status = QStringLiteral("%1 需要 dhclient 为 %2 申请 IPv4；请安装 isc-dhcp-client 后重试")
                                .arg(selection.dialer,
                                     fallbackInterface.isEmpty() ? QStringLiteral("蜂窝数据接口") : fallbackInterface);
            return result;
        }

        const qint64 ownedPid = parameters.value(QStringLiteral("ownedDialerPid")).toLongLong();
        const QString ownedPath = parameters.value(QStringLiteral("ownedDialerPath")).toString();
        if (ownedPid > 0 && processIsAlive(ownedPid)) {
            if (!processCommandMatches(ownedPid, QStringList() << ownedPath)) {
                result.status = QStringLiteral("已跟踪的蜂窝拨号进程命令不匹配，拒绝启动或终止未知进程");
                return result;
            }
            result.success = true;
            result.result = fallbackInterface;
            result.status = QStringLiteral("%1 已由迅为开发板测试平台启动（PID %2）：%3")
                                .arg(selection.displayName).arg(ownedPid).arg(selection.dialer);
            return result;
        }

        QStringList arguments;
        if (!apn.isEmpty()) {
            arguments << QStringLiteral("-s") << apn;
        }
        qint64 pid = 0;
        if (!startDetachedWithTimeoutForHelper(selection.dialerPath, arguments, 0, &pid) || pid <= 0) {
            result.status = QStringLiteral("启动匹配拨号程序失败：%1").arg(selection.dialerPath);
            return result;
        }
        if (!waitForProcessAlive(pid, 120)) {
            result.status = QStringLiteral("匹配拨号程序启动后立即退出：%1；请检查 SIM、天线和 USB 驱动")
                                .arg(selection.dialer);
            return result;
        }

        result.result = fallbackInterface;

        // QConnectManager V1.6.8 can live briefly even when its downstream
        // DHCP invocation fails. For an already discovered usb*/wwan* port,
        // wait for a real IPv4 lease before reporting a successful dial-up.
        // The worker thread is deliberately used here so the Qt/Web UI stays
        // responsive during modem registration and DHCP.
        if (!fallbackInterface.isEmpty() && isExistingNetworkInterface(fallbackInterface)) {
            constexpr int kCellularIpv4WaitMs = 12000;
            constexpr int kCellularIpv4PollMs = 250;
            const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + kCellularIpv4WaitMs;
            QString ipv4Address;
            while (QDateTime::currentMSecsSinceEpoch() < deadline) {
                ipv4Address = interfaceIpv4AddressForHelper(fallbackInterface);
                if (!ipv4Address.isEmpty() || !processIsAlive(pid)) {
                    break;
                }
                QThread::msleep(kCellularIpv4PollMs);
            }

            if (ipv4Address.isEmpty()) {
                if (!processIsAlive(pid)) {
                    result.clearCellularDialerPid = true;
                    result.status = QStringLiteral("%1 在 %2 获取 IPv4 前退出；请检查 APN、SIM、天线和 DHCP 客户端")
                                        .arg(selection.dialer, fallbackInterface);
                } else {
                    result.cellularDialerPidValid = true;
                    result.cellularDialerPid = pid;
                    result.cellularDialerPath = selection.dialerPath;
                    result.status = QStringLiteral("%1 已启动，但 %2 在 %3 秒内未获取 IPv4；请检查 DHCP、APN、SIM、天线和信号")
                                        .arg(selection.dialer)
                                        .arg(fallbackInterface)
                                        .arg(kCellularIpv4WaitMs / 1000);
                }
                return result;
            }

            result.cellularDialerPidValid = true;
            result.cellularDialerPid = pid;
            result.cellularDialerPath = selection.dialerPath;
            result.success = true;
            result.status = apn.isEmpty()
                ? QStringLiteral("已识别 %1（%2），%3 已为 %4 获取 IPv4 %5")
                      .arg(selection.displayName, selection.usbId, selection.dialer,
                           fallbackInterface, ipv4Address)
                : QStringLiteral("已识别 %1（%2），%3 使用 APN %4 为 %5 获取 IPv4 %6")
                      .arg(selection.displayName, selection.usbId, selection.dialer, apn,
                           fallbackInterface, ipv4Address);
            return result;
        }

        result.cellularDialerPidValid = true;
        result.cellularDialerPid = pid;
        result.cellularDialerPath = selection.dialerPath;
        result.success = true;
        result.status = apn.isEmpty()
            ? QStringLiteral("已识别 %1（%2），已启动 %3；数据接口将自动发现")
                  .arg(selection.displayName, selection.usbId, selection.dialer)
            : QStringLiteral("已识别 %1（%2），已启动 %3 并传入 APN %4；数据接口将自动发现")
                  .arg(selection.displayName, selection.usbId, selection.dialer, apn);
        return result;
    }

    int exitCode = -1;

    if (!commandPathForHelper(QStringLiteral("nmcli")).isEmpty()) {
        const QRegularExpression interfaceExpression(QStringLiteral("^(wwan|usb|ppp|rmnet)[A-Za-z0-9_.-]{0,31}$"));
        if (!interfaceExpression.match(fallbackInterface).hasMatch()
            || !QFileInfo::exists(networkInterfacePathForHelper(fallbackInterface))) {
            result.status = QStringLiteral("Cellular interface is not currently present: %1").arg(fallbackInterface);
            return result;
        }

        QString output;
        if (apn.isEmpty()) {
            output = runCommandForHelper(QStringLiteral("nmcli"),
                                         QStringList() << QStringLiteral("device") << QStringLiteral("connect") << fallbackInterface,
                                         12000,
                                         &exitCode);
        } else {
            const QString deviceType = runCommandForHelper(QStringLiteral("nmcli"),
                                                            QStringList() << QStringLiteral("-g") << QStringLiteral("GENERAL.TYPE")
                                                                          << QStringLiteral("device") << QStringLiteral("show") << fallbackInterface,
                                                            5000,
                                                            &exitCode).trimmed();
            if (exitCode != 0 || deviceType != QStringLiteral("gsm")) {
                result.status = QStringLiteral("APN configuration requires a NetworkManager GSM device; %1 reports '%2'")
                    .arg(fallbackInterface, deviceType.isEmpty() ? QStringLiteral("unknown") : deviceType);
                return result;
            }
            const QString connectionName = QStringLiteral("boardlab-gsm-%1").arg(fallbackInterface);
            const QString existing = runCommandForHelper(QStringLiteral("nmcli"),
                                                          QStringList() << QStringLiteral("-g") << QStringLiteral("NAME")
                                                                        << QStringLiteral("connection") << QStringLiteral("show") << connectionName,
                                                          5000,
                                                          &exitCode);
            if (exitCode == 0 && existing.trimmed() == connectionName) {
                output = runCommandForHelper(QStringLiteral("nmcli"),
                                             QStringList() << QStringLiteral("connection") << QStringLiteral("modify") << connectionName
                                                           << QStringLiteral("connection.interface-name") << fallbackInterface
                                                           << QStringLiteral("gsm.apn") << apn,
                                             8000,
                                             &exitCode);
            } else {
                output = runCommandForHelper(QStringLiteral("nmcli"),
                                             QStringList() << QStringLiteral("connection") << QStringLiteral("add")
                                                           << QStringLiteral("type") << QStringLiteral("gsm")
                                                           << QStringLiteral("ifname") << fallbackInterface
                                                           << QStringLiteral("con-name") << connectionName
                                                           << QStringLiteral("apn") << apn,
                                             8000,
                                             &exitCode);
            }
            if (exitCode == 0) {
                const QString appliedApn = runCommandForHelper(QStringLiteral("nmcli"),
                                                               QStringList() << QStringLiteral("-g") << QStringLiteral("gsm.apn")
                                                                             << QStringLiteral("connection") << QStringLiteral("show") << connectionName,
                                                               5000,
                                                               &exitCode).trimmed();
                if (exitCode != 0 || appliedApn != apn) {
                    result.status = QStringLiteral("APN verification failed for %1: expected %2, got %3")
                        .arg(connectionName, apn, appliedApn.isEmpty() ? QStringLiteral("<empty>") : appliedApn);
                    return result;
                }
                output = runCommandForHelper(QStringLiteral("nmcli"),
                                             QStringList() << QStringLiteral("connection") << QStringLiteral("up")
                                                           << connectionName << QStringLiteral("ifname") << fallbackInterface,
                                             12000,
                                             &exitCode);
            }
        }
        if (exitCode == 0) {
            result.success = true;
            result.result = fallbackInterface;
            result.status = apn.isEmpty()
                ? QStringLiteral("NetworkManager cellular connect requested: %1").arg(fallbackInterface)
                : QStringLiteral("NetworkManager GSM connection requested: %1 with APN %2").arg(fallbackInterface, apn);
            return result;
        }
        result.status = QStringLiteral("Cellular connect failed: %1").arg(output.left(120));
        return result;
    }

    result.status = QStringLiteral("No cellular manager found; install ModemManager/NetworkManager");
    return result;
}

BackendOperationResult stopCellularWithCommand(const QVariantMap &parameters)
{
    BackendOperationResult result;
    result.refreshNetworkDevices = true;
    const QStringList devices = parameters.value(QStringLiteral("devices")).toStringList();
    const CellularModemSelection selection = selectCellularModemForHelper(
        parameters.value(QStringLiteral("socFamily")).toString());
    result.cellularModemStatus = cellularModemStatusMap(selection, devices);
    result.cellularModemStatusValid = true;

    const qint64 ownedPid = parameters.value(QStringLiteral("ownedDialerPid")).toLongLong();
    const QString ownedPath = parameters.value(QStringLiteral("ownedDialerPath")).toString().trimmed();
    if (ownedPid > 0) {
        if (!processIsAlive(ownedPid)) {
            result.success = true;
            result.clearCellularDialerPid = true;
            result.status = QStringLiteral("已跟踪的蜂窝拨号进程已退出，已完成清理");
            return result;
        }
        if (ownedPath.isEmpty() || !processCommandMatches(ownedPid, QStringList() << ownedPath)) {
            result.status = QStringLiteral("已跟踪的蜂窝拨号进程命令不匹配，拒绝终止未知进程");
            return result;
        }
        const pid_t nativePid = static_cast<pid_t>(ownedPid);
        if (::kill(nativePid, SIGINT) != 0 && errno != ESRCH) {
            result.status = QStringLiteral("向蜂窝拨号进程发送 SIGINT 失败：%1")
                                .arg(QString::fromLocal8Bit(std::strerror(errno)));
            return result;
        }
        if (!processStoppedAfterDelay(ownedPid, 700) && !terminateProcess(ownedPid)) {
            result.status = QStringLiteral("蜂窝拨号进程未能停止（PID %1）").arg(ownedPid);
            return result;
        }
        result.success = true;
        result.clearCellularDialerPid = true;
        result.result = preferredCellularInterfaceFromDiscovery(devices);
        result.status = QStringLiteral("已停止迅为开发板测试平台启动的蜂窝拨号进程（PID %1）").arg(ownedPid);
        return result;
    }

    // A recognized Topeet modem is intentionally managed only through the
    // process BoardLab started and recorded above.  Do not let a later Stop
    // click tear down an external/vendor connection after an application
    // restart or an untracked manual dial-up.
    if (selection.detected) {
        result.success = true;
        result.result = preferredCellularInterfaceFromDiscovery(devices);
        result.status = QStringLiteral("未发现本平台启动的蜂窝拨号进程，未修改现有蜂窝链路");
        return result;
    }

    int exitCode = -1;
    bool attempted = false;
    bool stopped = false;
    QStringList failures;
    const QString iface = parameters.value(QStringLiteral("interface")).toString().trimmed().isEmpty()
        ? parameters.value(QStringLiteral("preferredInterface"), preferredCellularInterfaceFromDiscovery(devices)).toString().trimmed()
        : parameters.value(QStringLiteral("interface")).toString().trimmed();
    if (!commandPathForHelper(QStringLiteral("nmcli")).isEmpty()) {
        attempted = true;
        const QString output = runCommandForHelper(QStringLiteral("nmcli"),
                                                    QStringList() << QStringLiteral("device") << QStringLiteral("disconnect") << iface,
                                                    5000,
                                                    &exitCode);
        if (exitCode == 0) {
            stopped = true;
        } else {
            failures.append(QStringLiteral("nmcli: %1").arg(compactCommandOutput(output)));
        }
    }
    if (!commandPathForHelper(QStringLiteral("ifconfig")).isEmpty()) {
        attempted = true;
        const QString output = runCommandForHelper(QStringLiteral("ifconfig"),
                                                    QStringList() << iface << QStringLiteral("down"),
                                                    5000,
                                                    &exitCode);
        if (exitCode == 0) {
            stopped = true;
        } else {
            failures.append(QStringLiteral("ifconfig: %1").arg(compactCommandOutput(output)));
        }
    }

    result.success = stopped;
    result.result = iface;
    result.status = stopped ? QStringLiteral("Cellular link stopped: %1").arg(iface)
                            : (attempted
                               ? QStringLiteral("Cellular stop failed: %1").arg(failures.join(QStringLiteral("; ")))
                               : QStringLiteral("No cellular stop tool available"));
    return result;
}

BackendOperationResult startCameraTestWithCommand(const QString &mode, const QString &device, const QStringList &allowedDevices)
{
    BackendOperationResult result;
    if (commandPathForHelper(QStringLiteral("v4l2-ctl")).isEmpty()) {
        result.status = QStringLiteral("v4l2-ctl not found; install v4l-utils");
        return result;
    }

    const QString normalizedMode = mode.trimmed().isEmpty() ? QStringLiteral("mipi") : mode.trimmed().toLower();
    if (normalizedMode != QStringLiteral("mipi")
        && normalizedMode != QStringLiteral("usb")
        && normalizedMode != QStringLiteral("hdmi")
        && normalizedMode != QStringLiteral("hdmiin")) {
        result.status = QStringLiteral("Invalid V4L2 capture mode: %1").arg(normalizedMode);
        return result;
    }
    QString videoDevice = device.trimmed();
    if (videoDevice.isEmpty() && !allowedDevices.isEmpty()) {
        videoDevice = allowedDevices.first();
    }
    if (videoDevice.isEmpty()) {
        result.status = normalizedMode == QStringLiteral("hdmiin") || normalizedMode == QStringLiteral("hdmi")
            ? QStringLiteral("No HDMIIN video device detected")
            : QStringLiteral("No camera video device detected");
        return result;
    }
    if (!isVideoNodePath(videoDevice)) {
        result.status = QStringLiteral("Invalid video device path: %1").arg(videoDevice);
        return result;
    }
    if (allowedDevices.isEmpty() && !device.trimmed().isEmpty()) {
        result.status = QStringLiteral("No detected %1 capture list; refusing direct device override %2")
                            .arg(normalizedMode == QStringLiteral("hdmiin") || normalizedMode == QStringLiteral("hdmi")
                                     ? QStringLiteral("HDMIIN")
                                     : QStringLiteral("camera"),
                                 videoDevice);
        return result;
    }
    if (!allowedDevices.isEmpty() && !allowedDevices.contains(videoDevice)) {
        result.status = QStringLiteral("%1 is not in the allowed %2 capture list")
                            .arg(videoDevice,
                                 normalizedMode == QStringLiteral("hdmiin") || normalizedMode == QStringLiteral("hdmi")
                                     ? QStringLiteral("HDMIIN")
                                     : QStringLiteral("camera"));
        return result;
    }

    qint64 pid = 0;
    if (!startDetachedWithTimeoutForHelper(QStringLiteral("v4l2-ctl"),
                                           QStringList() << QStringLiteral("-d") << videoDevice
                                                         << QStringLiteral("--stream-mmap")
                                                         << QStringLiteral("--stream-count=120")
                                                         << QStringLiteral("--stream-to=/dev/null"),
                                           15,
                                           &pid)) {
        result.status = QStringLiteral("V4L2 capture start failed: %1 %2").arg(normalizedMode, videoDevice);
        return result;
    }
    if (pid > 0) {
        QElapsedTimer settle;
        settle.start();
        while (settle.elapsed() < 250) {
            if (!processIsAlive(pid)) {
                result.status = QStringLiteral("V4L2 capture exited immediately: %1 %2").arg(normalizedMode, videoDevice);
                return result;
            }
            QThread::msleep(20);
        }
    }

    result.success = true;
    result.result = QStringLiteral("%1 %2").arg(normalizedMode, videoDevice);
    result.status = QStringLiteral("V4L2 capture smoke started: %1 %2").arg(normalizedMode, videoDevice);
    return result;
}

BackendOperationResult stopCameraTestWithCommand()
{
    BackendOperationResult result;
    if (commandPathForHelper(QStringLiteral("pkill")).isEmpty()) {
        result.status = QStringLiteral("pkill not found; cannot stop V4L2 capture smoke");
        return result;
    }
    int exitCode = -1;
    const QString output = runCommandForHelper(QStringLiteral("pkill"),
                                               QStringList() << QStringLiteral("-f") << QStringLiteral("v4l2-ctl.*--stream-mmap"),
                                               3000,
                                               &exitCode);
    result.success = exitCode == 0;
    result.status = result.success ? QStringLiteral("V4L2 capture smoke stopped")
                                   : QStringLiteral("V4L2 capture smoke stop requested: %1").arg(output.left(96));
    return result;
}

struct GpioLineReference {
    QString requestedName;
    QString chipPath;
    quint32 lineOffset = 0;
    int legacyNumber = -1;

    bool usesCharacterDevice() const { return !chipPath.isEmpty(); }
    bool isValid() const { return usesCharacterDevice() || legacyNumber >= 0; }
};

QString gpioSysfsRootForHelper()
{
    return QDir(boardlab::sysRoot()).filePath(QStringLiteral("class/gpio"));
}

QString configuredGpioName(const QString &pinName, const QVariantMap &aliases)
{
    const QString requested = pinName.trimmed();
    for (auto it = aliases.cbegin(); it != aliases.cend(); ++it) {
        if (it.key().trimmed().compare(requested, Qt::CaseInsensitive) == 0) {
            return it.value().toString().trimmed();
        }
    }
    return requested;
}

QString chipPathForBank(int bank)
{
    const QDir dev(deviceRootForHelper());
    const QFileInfoList chips = dev.entryInfoList({QStringLiteral("gpiochip*")},
                                                   QDir::System | QDir::Files,
                                                   QDir::Name);
    const QRegularExpression bankExpression(
        QStringLiteral("(?:^|[^0-9])gpio%1(?:$|[^0-9])").arg(bank),
        QRegularExpression::CaseInsensitiveOption);
    for (const QFileInfo &chip : chips) {
        const QByteArray path = chip.absoluteFilePath().toLocal8Bit();
        const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        gpiochip_info info;
        std::memset(&info, 0, sizeof(info));
        const bool matched = ::ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0
            && (bankExpression.match(QString::fromLocal8Bit(info.name)).hasMatch()
                || bankExpression.match(QString::fromLocal8Bit(info.label)).hasMatch());
        ::close(fd);
        if (matched) {
            return chip.absoluteFilePath();
        }
    }

    // Many BSPs expose gpiochipN in bank order but omit a useful chip label.
    // This fallback is only used for GPIOx_Ay notation; explicit chip:offset is
    // available when a platform numbers chips differently.
    const QString conventional = dev.filePath(QStringLiteral("gpiochip%1").arg(bank));
    return QFileInfo::exists(conventional) ? conventional : QString();
}

GpioLineReference findNamedGpioLine(const QString &lineName)
{
    GpioLineReference reference;
    reference.requestedName = QStringLiteral("line:%1").arg(lineName);
    const QDir dev(deviceRootForHelper());
    const QFileInfoList chips = dev.entryInfoList({QStringLiteral("gpiochip*")},
                                                   QDir::System | QDir::Files,
                                                   QDir::Name);
    for (const QFileInfo &chip : chips) {
        const QByteArray path = chip.absoluteFilePath().toLocal8Bit();
        const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            continue;
        }
        gpiochip_info chipInfo;
        std::memset(&chipInfo, 0, sizeof(chipInfo));
        if (::ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &chipInfo) == 0) {
            for (quint32 offset = 0; offset < chipInfo.lines; ++offset) {
#if defined(GPIO_V2_GET_LINEINFO_IOCTL)
                gpio_v2_line_info lineInfoV2;
                std::memset(&lineInfoV2, 0, sizeof(lineInfoV2));
                lineInfoV2.offset = offset;
                if (::ioctl(fd, GPIO_V2_GET_LINEINFO_IOCTL, &lineInfoV2) == 0) {
                    if (QString::fromLocal8Bit(lineInfoV2.name).compare(
                            lineName, Qt::CaseSensitive) == 0) {
                        reference.chipPath = chip.absoluteFilePath();
                        reference.lineOffset = offset;
                        ::close(fd);
                        return reference;
                    }
                    continue;
                }
                const int v2Error = errno;
                if (v2Error != ENOTTY && v2Error != EINVAL
                    && v2Error != EOPNOTSUPP) {
                    continue;
                }
#endif
                gpioline_info lineInfo;
                std::memset(&lineInfo, 0, sizeof(lineInfo));
                lineInfo.line_offset = offset;
                if (::ioctl(fd, GPIO_GET_LINEINFO_IOCTL, &lineInfo) == 0
                    && QString::fromLocal8Bit(lineInfo.name).compare(
                           lineName, Qt::CaseSensitive) == 0) {
                    reference.chipPath = chip.absoluteFilePath();
                    reference.lineOffset = offset;
                    ::close(fd);
                    return reference;
                }
            }
        }
        ::close(fd);
    }
    return reference;
}

GpioLineReference gpioLineFromNameForHelper(const QString &pinName,
                                            const QVariantMap &aliases = QVariantMap())
{
    const QString configured = configuredGpioName(pinName, aliases);
    GpioLineReference reference;
    reference.requestedName = pinName.trimmed();

    bool numeric = false;
    const int numericGpio = configured.toInt(&numeric);
    if (numeric && numericGpio >= 0) {
        reference.legacyNumber = numericGpio;
        return reference;
    }

    static const QRegularExpression explicitLine(
        QStringLiteral("^(?:/dev/)?(gpiochip[0-9]+)[:/]([0-9]+)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch explicitMatch = explicitLine.match(configured);
    if (explicitMatch.hasMatch()) {
        bool offsetOk = false;
        const uint offset = explicitMatch.captured(2).toUInt(&offsetOk);
        const QString path = QDir(deviceRootForHelper()).filePath(
            explicitMatch.captured(1).toLower());
        if (offsetOk && QFileInfo::exists(path)) {
            reference.chipPath = path;
            reference.lineOffset = offset;
        }
        return reference;
    }

    if (configured.startsWith(QStringLiteral("line:"), Qt::CaseInsensitive)) {
        const QString lineName = configured.mid(5).trimmed();
        return lineName.isEmpty() ? reference : findNamedGpioLine(lineName);
    }

    static const QRegularExpression bankLine(
        QStringLiteral("^GPIO([0-9]+)_([A-D])([0-7])$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch bankMatch = bankLine.match(configured);
    if (!bankMatch.hasMatch()) {
        return reference;
    }

    const int bank = bankMatch.captured(1).toInt();
    const int groupOffset = (bankMatch.captured(2).toUpper().at(0).unicode()
                             - QChar('A').unicode()) * 8;
    reference.lineOffset = static_cast<quint32>(groupOffset + bankMatch.captured(3).toInt());
    reference.chipPath = chipPathForBank(bank);
    if (reference.chipPath.isEmpty()) {
        // Legacy Rockchip 4.19 BSPs commonly retain the fixed 32-lines-per-bank
        // global sysfs ABI. Newer kernels use the character-device path above.
        reference.legacyNumber = bank * 32 + static_cast<int>(reference.lineOffset);
    }
    return reference;
}

bool operateGpioCharacterLine(const GpioLineReference &reference, bool input,
                              bool high, bool *value, QString *errorText)
{
    const QByteArray chipPath = reference.chipPath.toLocal8Bit();
    const int chipFd = ::open(chipPath.constData(), O_RDONLY | O_CLOEXEC);
    if (chipFd < 0) {
        if (errorText) {
            *errorText = QStringLiteral("%1: %2").arg(
                reference.chipPath, QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

#if defined(GPIO_V2_GET_LINE_IOCTL)
    gpio_v2_line_request requestV2;
    std::memset(&requestV2, 0, sizeof(requestV2));
    requestV2.offsets[0] = reference.lineOffset;
    requestV2.num_lines = 1;
    requestV2.config.flags = input ? GPIO_V2_LINE_FLAG_INPUT
                                   : GPIO_V2_LINE_FLAG_OUTPUT;
    std::strncpy(requestV2.consumer, "boardlab", sizeof(requestV2.consumer) - 1);
    if (!input) {
        requestV2.config.num_attrs = 1;
        requestV2.config.attrs[0].attr.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        requestV2.config.attrs[0].attr.values = high ? 1U : 0U;
        requestV2.config.attrs[0].mask = 1U;
    }
    if (::ioctl(chipFd, GPIO_V2_GET_LINE_IOCTL, &requestV2) == 0) {
        ::close(chipFd);
        gpio_v2_line_values valuesV2;
        std::memset(&valuesV2, 0, sizeof(valuesV2));
        valuesV2.mask = 1U;
        bool success = true;
        if (!input) {
            valuesV2.bits = high ? 1U : 0U;
            success = ::ioctl(requestV2.fd, GPIO_V2_LINE_SET_VALUES_IOCTL,
                              &valuesV2) == 0;
        }
        if (success) {
            std::memset(&valuesV2, 0, sizeof(valuesV2));
            valuesV2.mask = 1U;
            success = ::ioctl(requestV2.fd, GPIO_V2_LINE_GET_VALUES_IOCTL,
                              &valuesV2) == 0;
        }
        if (success && value) {
            *value = (valuesV2.bits & 1U) != 0;
        }
        if (!success && errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        ::close(requestV2.fd);
        return success;
    }
    const int v2RequestError = errno;
    if (v2RequestError != ENOTTY && v2RequestError != EINVAL
        && v2RequestError != EOPNOTSUPP) {
        if (errorText) {
            *errorText = QStringLiteral("%1 line %2: %3")
                             .arg(reference.chipPath)
                             .arg(reference.lineOffset)
                             .arg(QString::fromLocal8Bit(std::strerror(v2RequestError)));
        }
        ::close(chipFd);
        return false;
    }
#endif

    // Linux 4.19 exposes only ABI v1. Newer kernels may retain it as a
    // compatibility option, so use it only when v2 is unavailable.
    gpiohandle_request request;
    std::memset(&request, 0, sizeof(request));
    request.lineoffsets[0] = reference.lineOffset;
    request.lines = 1;
    request.flags = input ? GPIOHANDLE_REQUEST_INPUT : GPIOHANDLE_REQUEST_OUTPUT;
    request.default_values[0] = high ? 1 : 0;
    std::strncpy(request.consumer_label, "boardlab", sizeof(request.consumer_label) - 1);
    if (::ioctl(chipFd, GPIO_GET_LINEHANDLE_IOCTL, &request) < 0) {
        if (errorText) {
            *errorText = QStringLiteral("%1 line %2: %3")
                             .arg(reference.chipPath)
                             .arg(reference.lineOffset)
                             .arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        ::close(chipFd);
        return false;
    }
    ::close(chipFd);

    gpiohandle_data data;
    std::memset(&data, 0, sizeof(data));
    bool success = true;
    if (!input) {
        data.values[0] = high ? 1 : 0;
        success = ::ioctl(request.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) == 0;
    }
    if (success) {
        std::memset(&data, 0, sizeof(data));
        success = ::ioctl(request.fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) == 0;
    }
    if (success && value) {
        *value = data.values[0] != 0;
    }
    if (!success && errorText) {
        *errorText = QString::fromLocal8Bit(std::strerror(errno));
    }
    ::close(request.fd);
    return success;
}

QString gpioReadbackForHelper(int gpio, QString *errorText)
{
    const QString root = gpioSysfsRootForHelper();
    const QString gpioPath = QDir(root).filePath(QStringLiteral("gpio%1").arg(gpio));
    if (!QFileInfo::exists(gpioPath)) {
        if (!writeTextFileForHelper(QDir(root).filePath(QStringLiteral("export")),
                                    QString::number(gpio), errorText)) {
            return QStringLiteral("Unavailable");
        }
    }

    const QString value = readTextFileForHelper(gpioPath + QStringLiteral("/value"));
    if (value == QStringLiteral("1")) {
        return QStringLiteral("HIGH (1)");
    }
    if (value == QStringLiteral("0")) {
        return QStringLiteral("LOW (0)");
    }
    if (errorText) {
        *errorText = QStringLiteral("read failed");
    }
    return QStringLiteral("Unavailable");
}

BackendOperationResult gpioOperationWithCommand(const QString &operation, const QVariantMap &parameters)
{
    BackendOperationResult result;
    const QString pinName = parameters.value(QStringLiteral("pin")).toString().trimmed();
    const GpioLineReference reference = gpioLineFromNameForHelper(
        pinName, parameters.value(QStringLiteral("gpioAliases")).toMap());
    if (!reference.isValid()) {
        result.status = QStringLiteral("Unsupported GPIO selector: %1 (use GPIOx_Ay, gpiochipN:offset, line:name, or a configured alias)")
                            .arg(pinName);
        result.result = QStringLiteral("Unsupported");
        return result;
    }

    const QString normalizedMode = parameters.value(QStringLiteral("mode")).toString().trimmed().toLower();
    const bool inputMode = operation == QStringLiteral("gpio-read")
        || normalizedMode == QStringLiteral("in")
        || normalizedMode == QStringLiteral("input");
    const bool high = parameters.value(QStringLiteral("high")).toBool();
    QString errorText;

    if (reference.usesCharacterDevice()) {
        bool value = false;
        result.success = operateGpioCharacterLine(reference, inputMode, high, &value, &errorText);
        result.result = result.success
            ? (value ? QStringLiteral("HIGH (1)") : QStringLiteral("LOW (0)"))
            : QStringLiteral("Unavailable");
        result.status = result.success
            ? QStringLiteral("%1 %2 via %3 line %4: %5")
                  .arg(pinName, inputMode ? QStringLiteral("input") : QStringLiteral("output"),
                       reference.chipPath)
                  .arg(reference.lineOffset)
                  .arg(result.result)
            : QStringLiteral("GPIO character-device operation failed: %1").arg(errorText);
        return result;
    }

    const QString root = gpioSysfsRootForHelper();
    const QString gpioPath = QDir(root).filePath(
        QStringLiteral("gpio%1").arg(reference.legacyNumber));
    if (!QFileInfo::exists(gpioPath)
        && !writeTextFileForHelper(QDir(root).filePath(QStringLiteral("export")),
                                   QString::number(reference.legacyNumber), &errorText)) {
        result.status = QStringLiteral("Legacy GPIO sysfs export failed: %1").arg(errorText);
        result.result = QStringLiteral("Unavailable");
        return result;
    }

    if (!writeTextFileForHelper(gpioPath + QStringLiteral("/direction"),
                                inputMode ? QStringLiteral("in") : QStringLiteral("out"),
                                &errorText)) {
        result.status = QStringLiteral("GPIO direction failed: %1").arg(errorText);
        result.result = QStringLiteral("Unavailable");
        return result;
    }
    if (!inputMode
        && !writeTextFileForHelper(gpioPath + QStringLiteral("/value"),
                                   high ? QStringLiteral("1") : QStringLiteral("0"),
                                   &errorText)) {
        result.status = QStringLiteral("GPIO value failed: %1").arg(errorText);
        result.result = QStringLiteral("Unavailable");
        return result;
    }

    result.result = inputMode
        ? gpioReadbackForHelper(reference.legacyNumber, &errorText)
        : QStringLiteral("输出已应用: %1").arg(high ? QStringLiteral("HIGH") : QStringLiteral("LOW"));
    result.success = !inputMode
        || result.result == QStringLiteral("HIGH (1)")
        || result.result == QStringLiteral("LOW (0)");
    result.status = result.success
        ? QStringLiteral("%1 %2 via legacy sysfs").arg(pinName, result.result)
        : QStringLiteral("GPIO read failed: %1").arg(errorText);
    return result;
}

BackendOperationResult setBacklightWithCommand(const QVariantMap &parameters)
{
    BackendOperationResult result;
    const int requestedPercent = parameters.value(QStringLiteral("percent")).toInt();
    QDir dir(QStringLiteral("/sys/class/backlight"));
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString path = entry.absoluteFilePath();
        const int maxBrightness = readTextFileForHelper(path + QStringLiteral("/max_brightness")).toInt();
        if (maxBrightness <= 0) {
            continue;
        }

        const int clamped = qBound(1, requestedPercent, 100);
        const int raw = qMax(1, static_cast<int>((maxBrightness * clamped) / 100.0));
        QString errorText;
        if (!writeTextFileForHelper(path + QStringLiteral("/brightness"), QString::number(raw), &errorText)) {
            result.status = QStringLiteral("Backlight write failed: %1").arg(errorText);
            return result;
        }

        const int brightness = qBound(0, readTextFileForHelper(path + QStringLiteral("/brightness")).toInt(), maxBrightness);
        result.success = true;
        result.result = QString::number(qBound(0, static_cast<int>((100.0 * brightness) / maxBrightness), 100));
        result.status = QStringLiteral("Backlight set to %1%").arg(result.result);
        result.backlightValid = true;
        result.backlightName = entry.fileName();
        result.backlightPath = path;
        result.backlightMaxBrightness = maxBrightness;
        result.backlightBrightness = brightness;
        return result;
    }

    result.status = QStringLiteral("No /sys/class/backlight device found");
    return result;
}

BackendOperationResult ledOperationWithFiles(const QString &operation, const QVariantMap &parameters);

BackendOperationResult runHardwareOperationWithCommand(const QString &operation, const QVariantMap &parameters)
{
    if (operation == QStringLiteral("ethernet-dhcp") || operation == QStringLiteral("ethernet-static")) {
        return configureEthernetWithNmcli(operation, parameters);
    }
    if (operation == QStringLiteral("ethernet-refresh")) {
        return refreshEthernetWithCommand(parameters);
    }
    if (operation == QStringLiteral("backlight-set")) {
        return setBacklightWithCommand(parameters);
    }
    if (operation == QStringLiteral("gpio-apply") || operation == QStringLiteral("gpio-read")) {
        return gpioOperationWithCommand(operation, parameters);
    }
    if (operation == QStringLiteral("led-set")
        || operation == QStringLiteral("led-all-on")
        || operation == QStringLiteral("led-all-off")
        || operation == QStringLiteral("led-blink")) {
        return ledOperationWithFiles(operation, parameters);
    }
    if (operation == QStringLiteral("uart-tx")) {
        return sendSerialTestFrameConfiguredWithDevice(parameters);
    }
    if (operation == QStringLiteral("uart-rx")) {
        return readSerialSampleConfiguredWithDevice(parameters);
    }
    if (operation == QStringLiteral("can-open")) {
        return configureCanInterfaceWithCommand(parameters);
    }
    if (operation == QStringLiteral("can-close")) {
        return closeCanInterfaceWithCommand(parameters);
    }
    if (operation == QStringLiteral("can-tx")) {
        return sendCanTestFrameWithSocket(parameters);
    }
    if (operation == QStringLiteral("can-rx")) {
        return readCanSampleWithSocket(parameters);
    }
    if (operation == QStringLiteral("can-loop-start")) {
        return startCanReferenceSenderWithCommand(parameters);
    }
    if (operation == QStringLiteral("can-loop-stop")) {
        return stopCanReferenceSenderWithCommand(parameters);
    }
    if (operation == QStringLiteral("watchdog-status")) {
        return watchdogStatusWithCommand(parameters);
    }
    if (operation == QStringLiteral("watchdog-start")) {
        return startWatchdogReferenceTestWithCommand(parameters);
    }
    if (operation == QStringLiteral("cellular-status")) {
        return cellularStatusReportWithCommand(parameters);
    }
    if (operation == QStringLiteral("cellular-start")) {
        return startCellularWithCommand(parameters);
    }
    if (operation == QStringLiteral("cellular-stop")) {
        return stopCellularWithCommand(parameters);
    }
    if (operation == QStringLiteral("camera-start")) {
        const QString requestedMode = parameters.value(QStringLiteral("mode")).toString().trimmed().toLower();
        if (requestedMode == QStringLiteral("hdmi") || requestedMode == QStringLiteral("hdmiin")) {
            BackendOperationResult result;
            result.status = QStringLiteral("Camera operation does not accept HDMIIN mode");
            return result;
        }
        return startCameraTestWithCommand(requestedMode.isEmpty() ? QStringLiteral("mipi") : requestedMode,
                                          parameters.value(QStringLiteral("device")).toString(),
                                          parameters.value(QStringLiteral("allowedDevices")).toStringList());
    }
    if (operation == QStringLiteral("hdmiin-start")) {
        const QString requestedMode = parameters.value(QStringLiteral("mode")).toString().trimmed().toLower();
        if (!requestedMode.isEmpty()
            && requestedMode != QStringLiteral("hdmi")
            && requestedMode != QStringLiteral("hdmiin")) {
            BackendOperationResult result;
            result.status = QStringLiteral("HDMIIN operation requires hdmiin mode");
            return result;
        }
        return startCameraTestWithCommand(QStringLiteral("hdmiin"),
                                          parameters.value(QStringLiteral("device")).toString(),
                                          parameters.value(QStringLiteral("allowedDevices")).toStringList());
    }
    if (operation == QStringLiteral("camera-stop") || operation == QStringLiteral("hdmiin-stop")) {
        return stopCameraTestWithCommand();
    }

    BackendOperationResult result;
    result.status = QStringLiteral("Unknown hardware operation: %1").arg(operation);
    return result;
}

bool startDetachedWithTimeoutForHelper(const QString &program, const QStringList &arguments, int timeoutSeconds, qint64 *pid)
{
    const QString executable = program.contains(QChar('/')) ? program : commandPathForHelper(program);
    if (executable.isEmpty()) {
        return false;
    }

    QString launchProgram = executable;
    QStringList launchArguments = arguments;
    if (!commandPathForHelper(QStringLiteral("timeout")).isEmpty() && timeoutSeconds > 0) {
        launchProgram = commandPathForHelper(QStringLiteral("timeout"));
        launchArguments.clear();
        launchArguments << QStringLiteral("--kill-after=2s")
                        << QString::number(timeoutSeconds)
                        << executable
                        << arguments;
    }

    // PolicyKit GUI launches may start with a deliberately minimal PATH.
    // QConnectManager V1.6.8 internally shells out to `dhclient`, so make the
    // standard administration directories explicit for every controlled
    // detached helper without relying on the desktop session environment.
    QStringList helperPaths = QString::fromLocal8Bit(qgetenv("PATH"))
                                  .split(QChar(':'), boardlab::kSkipEmptyParts);
    helperPaths << QStringLiteral("/usr/local/sbin")
                << QStringLiteral("/usr/local/bin")
                << QStringLiteral("/usr/sbin")
                << QStringLiteral("/usr/bin")
                << QStringLiteral("/sbin")
                << QStringLiteral("/bin");
    helperPaths.removeDuplicates();
    const QString envProgram = commandPathForHelper(QStringLiteral("env"));
    if (!envProgram.isEmpty()) {
        QStringList environmentArguments;
        environmentArguments << QStringLiteral("PATH=%1").arg(helperPaths.join(QChar(':')))
                             << launchProgram
                             << launchArguments;
        return QProcess::startDetached(envProgram, environmentArguments, QString(), pid);
    }
    return QProcess::startDetached(launchProgram, launchArguments, QString(), pid);
}

QString readTextFileForHelper(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool writeTextFileForHelper(const QString &path, const QString &text, QString *errorText)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorText) {
            *errorText = file.errorString();
        }
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    if (stream.status() != QTextStream::Ok) {
        if (errorText) {
            *errorText = QStringLiteral("write stream error");
        }
        return false;
    }
    return true;
}

QString inputClassRootForHelper()
{
    return boardlab::fixtureRoot("BOARDLAB_INPUT_CLASS_ROOT",
                                 QStringLiteral("/sys/class/input"));
}

QString inputDeviceRootForHelper()
{
    return boardlab::fixtureRoot("BOARDLAB_INPUT_DEVICE_ROOT",
                                 QStringLiteral("/dev/input"));
}

bool inputCapabilityBitSet(const unsigned long *bits, unsigned int bit)
{
    constexpr unsigned int bitsPerLong = sizeof(unsigned long) * 8U;
    return (bits[bit / bitsPerLong] & (1UL << (bit % bitsPerLong))) != 0;
}

bool ueventCapabilityBitSet(const QString &ueventPath, const QString &key,
                            unsigned int bit)
{
    const QRegularExpression expression(
        QStringLiteral("(?:^|\\n)%1=([0-9a-fA-F]+)(?:\\n|$)")
            .arg(QRegularExpression::escape(key)));
    const QRegularExpressionMatch match = expression.match(readTextFileForHelper(ueventPath));
    bool parsed = false;
    const quint64 capabilities = match.hasMatch()
        ? match.captured(1).toULongLong(&parsed, 16)
        : 0;
    return parsed && bit < 64U && (capabilities & (quint64(1) << bit)) != 0;
}

bool inputEventSupportsTone(int fd, const QString &ueventPath)
{
    constexpr unsigned int bitsPerLong = sizeof(unsigned long) * 8U;
    unsigned long eventBits[(EV_MAX + bitsPerLong) / bitsPerLong] = {};
    unsigned long soundBits[(SND_MAX + bitsPerLong) / bitsPerLong] = {};

    if (::ioctl(fd, EVIOCGBIT(0, sizeof(eventBits)), eventBits) >= 0
        && inputCapabilityBitSet(eventBits, EV_SND)
        && ::ioctl(fd, EVIOCGBIT(EV_SND, sizeof(soundBits)), soundBits) >= 0) {
        return inputCapabilityBitSet(soundBits, SND_TONE);
    }

    // A regular file cannot answer EVIOCGBIT, which keeps the runtime probe
    // testable without touching host input devices. Real evdev nodes always
    // use the ioctl path above.
    if (!boardlab::fixtureRootConfigured("BOARDLAB_INPUT_DEVICE_ROOT")) {
        return false;
    }
    return ueventCapabilityBitSet(ueventPath, QStringLiteral("EV"), EV_SND)
        && ueventCapabilityBitSet(ueventPath, QStringLiteral("SND"), SND_TONE);
}

bool isPwmBeeperInputName(const QString &name)
{
    const QString normalized = name.trimmed();
    return normalized.compare(QStringLiteral("pwm-beeper"), Qt::CaseInsensitive) == 0
        || normalized.compare(QStringLiteral("pwm beeper"), Qt::CaseInsensitive) == 0;
}

bool inputEventNumber(const QString &name, quint64 *number)
{
    static const QRegularExpression expression(QStringLiteral("^event([0-9]+)$"));
    const QRegularExpressionMatch match = expression.match(name);
    bool parsed = false;
    const quint64 value = match.hasMatch() ? match.captured(1).toULongLong(&parsed) : 0;

    if (!parsed) {
        return false;
    }
    if (number) {
        *number = value;
    }
    return true;
}

QString discoverPwmBeeperInputEvent(QString *unavailableReason)
{
    if (unavailableReason) {
        unavailableReason->clear();
    }

    const QDir inputClass(inputClassRootForHelper());
    const QFileInfoList entries = inputClass.entryInfoList(
        QStringList() << QStringLiteral("event*"),
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::NoSort);
    bool matchingBeeperFound = false;
    quint64 selectedEventNumber = std::numeric_limits<quint64>::max();
    QString selectedEventPath;
    QString lastFailure;
    for (const QFileInfo &entry : entries) {
        quint64 eventNumber = 0;
        if (!inputEventNumber(entry.fileName(), &eventNumber)) {
            continue;
        }
        const QString name = readTextFileForHelper(entry.absoluteFilePath() + QStringLiteral("/device/name"));
        if (!isPwmBeeperInputName(name)) {
            continue;
        }
        matchingBeeperFound = true;

        const QString eventPath = QDir(inputDeviceRootForHelper()).filePath(entry.fileName());
        const QByteArray encodedPath = eventPath.toLocal8Bit();
        const int fd = ::open(encodedPath.constData(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            lastFailure = QStringLiteral("无法写入 %1: %2").arg(eventPath, QString::fromLocal8Bit(std::strerror(errno)));
            continue;
        }

        const bool supportsTone = inputEventSupportsTone(fd, entry.absoluteFilePath() + QStringLiteral("/device/uevent"));
        ::close(fd);
        if (supportsTone) {
            if (eventNumber < selectedEventNumber) {
                selectedEventNumber = eventNumber;
                selectedEventPath = eventPath;
            }
            continue;
        }
        lastFailure = QStringLiteral("%1 未声明 SND_TONE 能力").arg(eventPath);
    }

    if (!selectedEventPath.isEmpty()) {
        return selectedEventPath;
    }

    if (unavailableReason) {
        if (!lastFailure.isEmpty()) {
            *unavailableReason = lastFailure;
        } else if (!matchingBeeperFound) {
            *unavailableReason = QStringLiteral("未发现 pwm-beeper 输入事件");
        }
    }
    return QString();
}

int toneFrequencyForPercent(int percent)
{
    const int clamped = qBound(0, percent, 100);
    return clamped == 0 ? 0 : 600 + ((clamped * 1800) / 100);
}

bool writeBuzzerToneEvent(const QString &eventPath, int frequencyHz, QString *errorText)
{
    if (eventPath.trimmed().isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("未发现 pwm-beeper 输入事件");
        }
        return false;
    }

    const QByteArray encodedPath = eventPath.toLocal8Bit();
    int openFlags = O_WRONLY | O_NONBLOCK | O_CLOEXEC;
    // A real evdev character device has no file offset. Appending only for a
    // redirected test node preserves every synthetic input_event for checks.
    if (boardlab::fixtureRootConfigured("BOARDLAB_INPUT_DEVICE_ROOT")) {
        openFlags |= O_APPEND;
    }
    const int fd = ::open(encodedPath.constData(), openFlags);
    if (fd < 0) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        return false;
    }

    input_event events[2] = {};
    events[0].type = EV_SND;
    events[0].code = SND_TONE;
    events[0].value = qBound(0, frequencyHz, 20000);
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;

    ssize_t written = -1;
    do {
        written = ::write(fd, events, sizeof(events));
    } while (written < 0 && errno == EINTR);
    const int savedErrno = errno;
    ::close(fd);
    if (written != static_cast<ssize_t>(sizeof(events))) {
        if (errorText) {
            *errorText = written < 0
                ? QString::fromLocal8Bit(std::strerror(savedErrno))
                : QStringLiteral("写入长度不完整");
        }
        return false;
    }
    return true;
}

QVariantList scanLedDevicesForPaths(const QStringList &paths)
{
    QVariantList devices;
    for (const QString &configuredPath : paths) {
        const QString path = QDir::cleanPath(configuredPath.trimmed());
        const QString brightnessPath = path + QStringLiteral("/brightness");
        const QString maxBrightnessPath = path + QStringLiteral("/max_brightness");
        const QFileInfo brightnessInfo(brightnessPath);
        if (path.isEmpty()
            || !brightnessInfo.exists()
            || !QFileInfo::exists(maxBrightnessPath)
            || !brightnessInfo.isWritable()) {
            continue;
        }

        bool maxOk = false;
        bool brightnessOk = false;
        const int maxBrightness = readTextFileForHelper(maxBrightnessPath).toInt(&maxOk);
        const int brightness = readTextFileForHelper(brightnessPath).toInt(&brightnessOk);
        if (!maxOk || !brightnessOk || maxBrightness <= 0) {
            continue;
        }

        QVariantMap item;
        item.insert(QStringLiteral("name"), QFileInfo(path).fileName());
        item.insert(QStringLiteral("path"), path);
        item.insert(QStringLiteral("brightness"), qBound(0, brightness, maxBrightness));
        item.insert(QStringLiteral("maxBrightness"), maxBrightness);
        item.insert(QStringLiteral("percent"), qBound(0, qRound(100.0 * brightness / maxBrightness), 100));
        item.insert(QStringLiteral("writable"), true);
        devices.append(item);
    }
    return devices;
}

QString audioDeviceDescription(const QVariantMap &item)
{
    return QStringList{
        item.value(QStringLiteral("card")).toString(),
        item.value(QStringLiteral("name")).toString(),
        item.value(QStringLiteral("label")).toString(),
        item.value(QStringLiteral("device")).toString()
    }.join(QLatin1Char(' ')).toLower();
}

int audioDevicePreference(const QVariantMap &item)
{
    const QString description = audioDeviceDescription(item);
    // Rockchip images commonly enumerate HDMI before the onboard codec. Keep
    // a real analog codec first so the default never silently tests HDMI.
    if (description.contains(QRegularExpression(QStringLiteral("es8|rt56|rk809|codec|analog|headset|speaker|microphone")))) {
        return 0;
    }
    if (!description.contains(QStringLiteral("hdmi"))) {
        return 1;
    }
    return 2;
}

QString audioCommandDevice(const QVariantMap &item)
{
    const QString commandDevice = item.value(QStringLiteral("commandDevice")).toString().trimmed();
    return commandDevice.isEmpty() ? item.value(QStringLiteral("device")).toString().trimmed() : commandDevice;
}

QString resolveAudioDevice(const QString &requested, const QVariantList &available)
{
    const QString candidate = requested.trimmed();
    if (candidate.isEmpty()) {
        return available.isEmpty() ? QString() : audioCommandDevice(available.first().toMap());
    }
    for (const QVariant &item : available) {
        const QVariantMap row = item.toMap();
        if (row.value(QStringLiteral("device")).toString() == candidate) {
            return audioCommandDevice(row);
        }
    }
    return QStringLiteral("__invalid__");
}

BackendOperationResult ledOperationWithFiles(const QString &operation, const QVariantMap &parameters)
{
    BackendOperationResult result;
    const QStringList allowedPaths = parameters.value(QStringLiteral("allowedPaths")).toStringList();
    if (allowedPaths.isEmpty()) {
        result.status = QStringLiteral("No LED devices are configured for this board profile");
        return result;
    }

    auto resolvePath = [&allowedPaths](const QString &requestedName) {
        const QString name = requestedName.trimmed();
        for (const QString &candidate : allowedPaths) {
            const QString clean = QDir::cleanPath(candidate);
            if (name.isEmpty() || name == clean || name == QFileInfo(clean).fileName()) {
                return clean;
            }
        }
        return QString();
    };

    auto validateLedPath = [](const QString &path, QString *errorText) {
        const QFileInfo brightnessInfo(path + QStringLiteral("/brightness"));
        bool maxOk = false;
        bool brightnessOk = false;
        const int maxBrightness = readTextFileForHelper(path + QStringLiteral("/max_brightness")).toInt(&maxOk);
        readTextFileForHelper(path + QStringLiteral("/brightness")).toInt(&brightnessOk);
        if (!brightnessInfo.exists() || !brightnessInfo.isWritable()) {
            if (errorText) {
                *errorText = QStringLiteral("brightness control is missing or not writable");
            }
            return false;
        }
        if (!maxOk || !brightnessOk || maxBrightness <= 0) {
            if (errorText) {
                *errorText = QStringLiteral("invalid brightness or max_brightness");
            }
            return false;
        }
        return true;
    };

    auto writePercent = [&validateLedPath](const QString &path, int percent, QString *errorText, int *readbackPercent) {
        if (!validateLedPath(path, errorText)) {
            return false;
        }
        bool maxOk = false;
        const int maxBrightness = readTextFileForHelper(path + QStringLiteral("/max_brightness")).toInt(&maxOk);
        if (!maxOk || maxBrightness <= 0) {
            if (errorText) {
                *errorText = QStringLiteral("invalid max_brightness");
            }
            return false;
        }
        const int raw = qBound(0, qRound(maxBrightness * qBound(0, percent, 100) / 100.0), maxBrightness);
        if (!writeTextFileForHelper(path + QStringLiteral("/brightness"), QString::number(raw), errorText)) {
            return false;
        }
        bool readOk = false;
        const int readback = readTextFileForHelper(path + QStringLiteral("/brightness")).toInt(&readOk);
        if (!readOk) {
            if (errorText) {
                *errorText = QStringLiteral("brightness readback failed");
            }
            return false;
        }
        if (readbackPercent) {
            *readbackPercent = qBound(0, qRound(100.0 * readback / maxBrightness), 100);
        }
        if (readback != raw) {
            if (errorText) {
                *errorText = QStringLiteral("brightness readback mismatch (requested %1, got %2)").arg(raw).arg(readback);
            }
            return false;
        }
        return true;
    };

    auto restoreRawBrightness = [](const QString &path, int value, QString *errorText) {
        return writeTextFileForHelper(path + QStringLiteral("/brightness"), QString::number(value), errorText);
    };

    if (operation == QStringLiteral("led-blink")) {
        QVariantMap originalValues;
        QString errorText;
        for (const QString &path : allowedPaths) {
            const QString clean = QDir::cleanPath(path);
            if (!validateLedPath(clean, &errorText)) {
                result.status = QStringLiteral("LED blink is unavailable for %1: %2")
                                    .arg(QFileInfo(clean).fileName(), errorText);
                return result;
            }
            bool originalOk = false;
            const int original = readTextFileForHelper(clean + QStringLiteral("/brightness")).toInt(&originalOk);
            if (!originalOk) {
                result.status = QStringLiteral("LED blink cannot read original brightness for %1")
                                    .arg(QFileInfo(clean).fileName());
                return result;
            }
            originalValues.insert(clean, original);
        }

        auto restoreOriginalValues = [&]() {
            QStringList failures;
            for (const QString &path : allowedPaths) {
                const QString clean = QDir::cleanPath(path);
                QString restoreError;
                if (!restoreRawBrightness(clean, originalValues.value(clean).toInt(), &restoreError)) {
                    failures.append(QStringLiteral("%1: %2").arg(QFileInfo(clean).fileName(), restoreError));
                }
            }
            return failures;
        };
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (const QString &path : allowedPaths) {
                int ignored = 0;
                if (!writePercent(QDir::cleanPath(path), 100, &errorText, &ignored)) {
                    const QStringList restoreFailures = restoreOriginalValues();
                    result.status = QStringLiteral("LED blink failed: %1%2")
                                        .arg(errorText,
                                             restoreFailures.isEmpty()
                                                 ? QStringLiteral("; original brightness restored")
                                                 : QStringLiteral("; restore failed: %1").arg(restoreFailures.join(QStringLiteral(", "))));
                    return result;
                }
            }
            QThread::msleep(180);
            for (const QString &path : allowedPaths) {
                int ignored = 0;
                if (!writePercent(QDir::cleanPath(path), 0, &errorText, &ignored)) {
                    const QStringList restoreFailures = restoreOriginalValues();
                    result.status = QStringLiteral("LED blink failed: %1%2")
                                        .arg(errorText,
                                             restoreFailures.isEmpty()
                                                 ? QStringLiteral("; original brightness restored")
                                                 : QStringLiteral("; restore failed: %1").arg(restoreFailures.join(QStringLiteral(", "))));
                    return result;
                }
            }
            QThread::msleep(180);
        }
        const QStringList restoreFailures = restoreOriginalValues();
        if (!restoreFailures.isEmpty()) {
            result.status = QStringLiteral("LED blink completed but original brightness restore failed: %1")
                                .arg(restoreFailures.join(QStringLiteral(", ")));
            return result;
        }
        result.success = true;
        result.result = QStringLiteral("3 cycles");
        result.status = QStringLiteral("LED blink test completed and original brightness restored");
        return result;
    }

    QStringList targets;
    int percent = qBound(0, parameters.value(QStringLiteral("percent"), 100).toInt(), 100);
    if (operation == QStringLiteral("led-all-on")) {
        targets = allowedPaths;
        percent = 100;
    } else if (operation == QStringLiteral("led-all-off")) {
        targets = allowedPaths;
        percent = 0;
    } else if (operation == QStringLiteral("led-set")) {
        const QString path = resolvePath(parameters.value(QStringLiteral("name")).toString());
        if (!path.isEmpty()) {
            targets.append(path);
        }
    }
    if (targets.isEmpty()) {
        result.status = QStringLiteral("No matching LED device in board profile whitelist");
        return result;
    }

    QString errorText;
    for (const QString &target : targets) {
        if (!validateLedPath(QDir::cleanPath(target), &errorText)) {
            result.status = QStringLiteral("LED operation aborted before writing %1: %2")
                                .arg(QFileInfo(target).fileName(), errorText);
            return result;
        }
    }
    QStringList readbacks;
    for (const QString &target : targets) {
        int readbackPercent = 0;
        if (!writePercent(QDir::cleanPath(target), percent, &errorText, &readbackPercent)) {
            result.status = QStringLiteral("LED write failed for %1: %2").arg(QFileInfo(target).fileName(), errorText);
            return result;
        }
        readbacks.append(QStringLiteral("%1=%2%").arg(QFileInfo(target).fileName()).arg(readbackPercent));
    }
    result.success = true;
    result.result = readbacks.join(QStringLiteral(", "));
    result.status = QStringLiteral("LED brightness applied: %1").arg(result.result);
    return result;
}

BackendOperationResult setAudioVolumeWithCommand(int percent, const QString &playbackDevice)
{
    BackendOperationResult result;
    const int clamped = qBound(0, percent, 100);
    int exitCode = -1;
    const int cardIndex = alsaCardIndexFromDevice(playbackDevice);

    // Rockchip codec cards commonly expose PCM rather than a global Master
    // control.  Target the selected ALSA card before using desktop-session
    // mixers, which may point at HDMI or reject headless board sessions.
    if (cardIndex >= 0 && !commandPathForHelper(QStringLiteral("amixer")).isEmpty()) {
        const QString output = runCommandForHelper(QStringLiteral("amixer"),
                                                   QStringList() << QStringLiteral("-q")
                                                                 << QStringLiteral("-c") << QString::number(cardIndex)
                                                                 << QStringLiteral("sset") << QStringLiteral("PCM")
                                                                 << QStringLiteral("%1%").arg(clamped),
                                                   5000,
                                                   &exitCode);
        if (exitCode == 0) {
            result.success = true;
            result.audioVolumePercent = clamped;
            result.result = QString::number(clamped);
            result.status = QStringLiteral("ALSA card %1 PCM volume set to %2%").arg(cardIndex).arg(clamped);
            return result;
        }
        result.status = QStringLiteral("ALSA card %1 PCM volume failed: %2").arg(cardIndex).arg(output.left(120));
    }

    if (!commandPathForHelper(QStringLiteral("pactl")).isEmpty()) {
        const QString output = runCommandForHelper(QStringLiteral("pactl"),
                                                   QStringList() << QStringLiteral("set-sink-volume")
                                                                 << QStringLiteral("@DEFAULT_SINK@")
                                                                 << QStringLiteral("%1%").arg(clamped),
                                                   5000,
                                                   &exitCode);
        if (exitCode == 0) {
            result.success = true;
            result.audioVolumePercent = clamped;
            result.result = QString::number(clamped);
            result.status = QStringLiteral("Audio volume set to %1%").arg(clamped);
            return result;
        }
        result.status = QStringLiteral("pactl volume failed: %1").arg(output.left(120));
    }

    if (!commandPathForHelper(QStringLiteral("amixer")).isEmpty()) {
        const QString output = runCommandForHelper(QStringLiteral("amixer"),
                                                   QStringList() << QStringLiteral("-q")
                                                                 << QStringLiteral("sset")
                                                                 << QStringLiteral("Master")
                                                                 << QStringLiteral("%1%").arg(clamped),
                                                   5000,
                                                   &exitCode);
        if (exitCode == 0) {
            result.success = true;
            result.audioVolumePercent = clamped;
            result.result = QString::number(clamped);
            result.status = QStringLiteral("ALSA Master volume set to %1%").arg(clamped);
            return result;
        }
        result.status = QStringLiteral("amixer Master volume failed: %1").arg(output.left(120));
        return result;
    }

    if (result.status.isEmpty()) {
        result.status = QStringLiteral("No audio mixer found; install pulseaudio-utils or alsa-utils");
    }
    return result;
}

BackendOperationResult startAudioTestWithCommand(const QVector<qint64> &stalePids,
                                                 const QString &playbackDevice,
                                                 const QString &socFamily)
{
    stopProcessList(stalePids, audioProcessKillTokens());

    BackendOperationResult result;
    const QString samplePath = headphoneSamplePathForSocFamily(socFamily);
    if (samplePath.isEmpty()) {
        result.status = QStringLiteral("未在 %1 中发现可读的 *_headphone/1.wav 测试音频（SoC 仅影响候选目录优先级）")
                            .arg(topeetTestRootForHelper());
        return result;
    }
    const QFileInfo sampleInfo(samplePath);
    if (!sampleInfo.isFile() || !sampleInfo.isReadable()) {
        result.status = QStringLiteral("未找到当前 SoC 的迅为耳机测试音频：%1").arg(samplePath);
        return result;
    }
    if (commandPathForHelper(QStringLiteral("aplay")).isEmpty()) {
        result.status = QStringLiteral("aplay 未安装，无法播放迅为耳机测试音频；请安装 alsa-utils");
        return result;
    }

    qint64 pid = 0;
    QStringList arguments;
    if (!playbackDevice.isEmpty()) {
        arguments << QStringLiteral("-D") << playbackDevice;
    }
    arguments << QStringLiteral("-q") << samplePath;
    if (!startDetachedWithTimeoutForHelper(QStringLiteral("aplay"), arguments, 0, &pid)) {
        result.status = QStringLiteral("启动迅为耳机测试音频失败：%1").arg(samplePath);
        return result;
    }
    if (pid <= 0 || !waitForProcessAlive(pid, 250)) {
        result.status = QStringLiteral("aplay 启动后立即退出：%1；请检查播放设备和 ALSA 驱动").arg(samplePath);
        return result;
    }

    result.audioPids.append(pid);
    result.success = true;
    result.result = samplePath;
    result.status = QStringLiteral("正在播放迅为耳机测试音频%1：%2")
                        .arg(playbackDevice.isEmpty() ? QString() : QStringLiteral("（%1）").arg(playbackDevice),
                             samplePath);
    return result;
}

BackendOperationResult recordAudioSampleWithCommand(const QVector<qint64> &stalePids, const QString &captureDevice)
{
    stopProcessList(stalePids, audioProcessKillTokens());

    BackendOperationResult result;
    if (commandPathForHelper(QStringLiteral("arecord")).isEmpty()) {
        result.status = QStringLiteral("arecord not found; install alsa-utils");
        return result;
    }

    const QString outputPath = QStringLiteral("/tmp/boardlab-audio-test.wav");
    qint64 pid = 0;
    QStringList arguments;
    if (!captureDevice.isEmpty()) {
        arguments << QStringLiteral("-D") << captureDevice;
    }
    arguments << QStringLiteral("-q")
                                                        << QStringLiteral("-d") << QStringLiteral("2")
                                                        << QStringLiteral("-f") << QStringLiteral("S16_LE")
                                                        << QStringLiteral("-r") << QStringLiteral("48000")
                                                        << QStringLiteral("-c") << QStringLiteral("2")
                                                        << outputPath;
    if (startDetachedWithTimeoutForHelper(QStringLiteral("arecord"), arguments,
                                          5,
                                          &pid)) {
        if (pid > 0) {
            result.audioPids.append(pid);
        }
        result.success = true;
        result.result = outputPath;
        result.status = QStringLiteral("Recording 2s sample%1 with timeout: %2").arg(captureDevice.isEmpty() ? QString() : QStringLiteral(" on %1").arg(captureDevice), outputPath);
        return result;
    }

    result.status = QStringLiteral("arecord start failed: %1").arg(outputPath);
    return result;
}

BackendOperationResult playRecordedAudioWithCommand(const QVector<qint64> &stalePids, const QString &playbackDevice)
{
    stopProcessList(stalePids, audioProcessKillTokens());

    BackendOperationResult result;
    if (commandPathForHelper(QStringLiteral("aplay")).isEmpty()) {
        result.status = QStringLiteral("aplay not found; install alsa-utils");
        return result;
    }

    const QString inputPath = QStringLiteral("/tmp/boardlab-audio-test.wav");
    if (!QFileInfo::exists(inputPath)) {
        result.status = QStringLiteral("No recorded audio sample; record first");
        return result;
    }

    qint64 pid = 0;
    QStringList arguments;
    if (!playbackDevice.isEmpty()) {
        arguments << QStringLiteral("-D") << playbackDevice;
    }
    arguments << QStringLiteral("-q") << inputPath;
    if (startDetachedWithTimeoutForHelper(QStringLiteral("aplay"), arguments,
                                          10,
                                          &pid)) {
        if (pid > 0) {
            result.audioPids.append(pid);
        }
        result.success = true;
        result.result = inputPath;
        result.status = QStringLiteral("Playback started%1 with timeout guard: %2").arg(playbackDevice.isEmpty() ? QString() : QStringLiteral(" on %1").arg(playbackDevice), inputPath);
        return result;
    }

    result.status = QStringLiteral("aplay start failed: %1").arg(inputPath);
    return result;
}

BackendOperationResult stopAudioWithCommand(const QVector<qint64> &stalePids)
{
    BackendOperationResult result;
    if (stalePids.isEmpty()) {
        result.status = QStringLiteral("No tracked audio playback or recording process");
        return result;
    }

    bool alreadyExited = false;
    for (const qint64 pid : stalePids) {
        if (!processIsAlive(pid)) {
            alreadyExited = true;
        }
    }
    stopProcessList(stalePids, audioProcessKillTokens());
    result.success = true;
    result.status = alreadyExited ? QStringLiteral("Audio playback/recording already exited; cleanup completed")
                                  : QStringLiteral("Audio playback/recording stopped");
    return result;
}

BackendOperationResult stopBeepWithInputEvent(const QString &eventPath,
                                              const std::shared_ptr<std::atomic_bool> &heartbeatCancelled,
                                              const std::shared_ptr<std::mutex> &toneWriteMutex);

BackendOperationResult stopLateAudioProcessesWithCommand(QVector<qint64> lateAudioPids,
                                                         bool stopBuzzer,
                                                         const std::shared_ptr<std::atomic_bool> &heartbeatCancelled,
                                                         const std::shared_ptr<std::mutex> &toneWriteMutex,
                                                         const QString &inputEventDevice)
{
    const bool hadAudio = !lateAudioPids.isEmpty();
    stopProcessList(lateAudioPids, audioProcessKillTokens());
    const BackendOperationResult beep = stopBuzzer
        ? stopBeepWithInputEvent(inputEventDevice, heartbeatCancelled, toneWriteMutex)
        : BackendOperationResult();

    BackendOperationResult result;
    result.success = !stopBuzzer || beep.success;
    if (hadAudio && stopBuzzer) {
        result.status = QStringLiteral("Pending music and buzzer stop completed: %1").arg(beep.status);
    } else if (hadAudio) {
        result.status = QStringLiteral("Pending music stop completed");
    } else if (stopBuzzer) {
        result.status = beep.status;
    } else {
        result.status = QStringLiteral("Audio stop requested; no late process was left running");
    }
    return result;
}

BackendOperationResult setBeepPercentWithInputEvent(int percent, const QString &eventPath,
                                                     const std::shared_ptr<std::mutex> &toneWriteMutex)
{
    BackendOperationResult result;
    const int clamped = qBound(0, percent, 100);
    const int frequencyHz = toneFrequencyForPercent(clamped);
    QString errorText;
    std::lock_guard<std::mutex> guard(*toneWriteMutex);
    if (!writeBuzzerToneEvent(eventPath, frequencyHz, &errorText)) {
        result.status = QStringLiteral("蜂鸣器输入事件写入失败: %1").arg(errorText);
        return result;
    }

    result.success = true;
    result.result = QString::number(clamped);
    result.status = frequencyHz > 0
        ? QStringLiteral("蜂鸣器已通过 pwm-beeper 输入事件开启: %1 Hz（%2%）").arg(frequencyHz).arg(clamped)
        : QStringLiteral("蜂鸣器已通过 pwm-beeper 输入事件停止");
    return result;
}

BackendOperationResult startBeepHeartbeatWithInputEvent(
    const QString &eventPath,
    const std::shared_ptr<std::atomic_bool> &heartbeatCancelled,
    const std::shared_ptr<std::mutex> &toneWriteMutex)
{
    BackendOperationResult result;
    const int frequencyHz = toneFrequencyForPercent(50);
    QString errorText;
    for (int pulse = 0; pulse < 2; ++pulse) {
        {
            std::lock_guard<std::mutex> guard(*toneWriteMutex);
            if (heartbeatCancelled->load(std::memory_order_acquire)) {
                result.success = true;
                result.status = QStringLiteral("蜂鸣器提示音已取消并停止（pwm-beeper 输入事件）");
                return result;
            }
            if (!writeBuzzerToneEvent(eventPath, frequencyHz, &errorText)) {
                result.status = QStringLiteral("蜂鸣器提示音启动失败: %1").arg(errorText);
                return result;
            }
        }
        QThread::msleep(90);
        {
            std::lock_guard<std::mutex> guard(*toneWriteMutex);
            if (!writeBuzzerToneEvent(eventPath, 0, &errorText)) {
                result.status = QStringLiteral("蜂鸣器提示音停止失败: %1").arg(errorText);
                return result;
            }
        }
        if (pulse == 0) {
            QThread::msleep(120);
        }
    }

    result.success = true;
    result.result = QStringLiteral("2 pulses");
    result.status = QStringLiteral("蜂鸣器双短提示已完成并停止（pwm-beeper 输入事件）");
    return result;
}

BackendOperationResult stopBeepWithInputEvent(
    const QString &eventPath,
    const std::shared_ptr<std::atomic_bool> &heartbeatCancelled,
    const std::shared_ptr<std::mutex> &toneWriteMutex)
{
    BackendOperationResult result;
    QString errorText;
    std::lock_guard<std::mutex> guard(*toneWriteMutex);
    heartbeatCancelled->store(true, std::memory_order_release);
    if (!writeBuzzerToneEvent(eventPath, 0, &errorText)) {
        result.status = QStringLiteral("蜂鸣器停止失败: %1").arg(errorText);
        return result;
    }
    result.success = true;
    result.status = QStringLiteral("蜂鸣器已停止（pwm-beeper 输入事件）");
    return result;
}

BackendOperationResult runAudioOperationWithCommand(const QString &operation,
                                                    int value,
                                                    const QString &playbackDevice,
                                                    const QString &captureDevice,
                                                    const QString &socFamily,
                                                    const QVector<qint64> &audioPids,
                                                    const std::shared_ptr<std::atomic_bool> &heartbeatCancelled,
                                                    const std::shared_ptr<std::mutex> &toneWriteMutex,
                                                    const QString &inputEventDevice)
{
    if (operation == QStringLiteral("play-test")) {
        return startAudioTestWithCommand(audioPids, playbackDevice, socFamily);
    }
    if (operation == QStringLiteral("record")) {
        return recordAudioSampleWithCommand(audioPids, captureDevice);
    }
    if (operation == QStringLiteral("play-recorded")) {
        return playRecordedAudioWithCommand(audioPids, playbackDevice);
    }
    if (operation == QStringLiteral("stop-audio")) {
        return stopAudioWithCommand(audioPids);
    }
    if (operation == QStringLiteral("volume")) {
        return setAudioVolumeWithCommand(value, playbackDevice);
    }
    if (operation == QStringLiteral("beep-apply")) {
        return setBeepPercentWithInputEvent(value, inputEventDevice, toneWriteMutex);
    }
    if (operation == QStringLiteral("beep-heartbeat")) {
        return startBeepHeartbeatWithInputEvent(inputEventDevice, heartbeatCancelled, toneWriteMutex);
    }
    if (operation == QStringLiteral("beep-stop")) {
        return stopBeepWithInputEvent(inputEventDevice, heartbeatCancelled, toneWriteMutex);
    }
    if (operation == QStringLiteral("stop-all")) {
        BackendOperationResult audio = stopAudioWithCommand(audioPids);
        BackendOperationResult beep = stopBeepWithInputEvent(inputEventDevice, heartbeatCancelled, toneWriteMutex);
        BackendOperationResult result;
        result.success = audio.success || beep.success;
        result.status = QStringLiteral("%1; %2").arg(audio.status, beep.status);
        return result;
    }

    BackendOperationResult result;
    result.status = QStringLiteral("Unknown audio operation: %1").arg(operation);
    return result;
}

QString storageTypeForPath(const QString &path, const QString &transport, bool removable)
{
    const QString normalizedPath = path.toLower();
    const QString normalizedTransport = transport.toLower();
    if (normalizedTransport == QStringLiteral("nvme")
        || normalizedPath.startsWith(QStringLiteral("/dev/nvme"))) {
        return QStringLiteral("NVME");
    }
    if (normalizedTransport == QStringLiteral("mmc")
        || normalizedPath.startsWith(QStringLiteral("/dev/mmcblk"))) {
        return removable ? QStringLiteral("SD") : QStringLiteral("EMMC");
    }
    if (normalizedTransport == QStringLiteral("usb")) {
        return QStringLiteral("USB");
    }
    if (normalizedTransport == QStringLiteral("sata")
        || normalizedTransport == QStringLiteral("ata")) {
        return QStringLiteral("SATA");
    }
    if (normalizedTransport == QStringLiteral("ufs")) {
        return QStringLiteral("UFS");
    }
    if (removable) {
        return QStringLiteral("Removable");
    }
    return QStringLiteral("Storage");
}

QString suggestedStorageMountPoint(const QString &device)
{
    QString name = QFileInfo(device).fileName();
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    return QDir(QStringLiteral("/mnt/boardlab")).filePath(name.isEmpty() ? QStringLiteral("storage") : name);
}

bool storageNodeIsProtected(const QString &mountPoint, const QString &fileSystem)
{
    const QString trimmedMount = mountPoint.trimmed();
    const QString normalizedMount = trimmedMount.startsWith(QLatin1Char('/'))
        ? QDir::cleanPath(trimmedMount) : trimmedMount;
    return fileSystem.compare(QStringLiteral("swap"), Qt::CaseInsensitive) == 0
        || normalizedMount == QStringLiteral("[SWAP]")
        || normalizedMount == QStringLiteral("/")
        || normalizedMount == QStringLiteral("/boot")
        || normalizedMount.startsWith(QStringLiteral("/boot/"));
}

bool isBoardLabManagedStorageMount(const QString &mountPoint)
{
    const QString root = QDir::cleanPath(QStringLiteral("/mnt/boardlab"));
    const QString normalizedMount = QDir::cleanPath(mountPoint.trimmed());
    return normalizedMount.startsWith(root + QLatin1Char('/'));
}

bool jsonBoolean(const QJsonValue &value, bool fallback)
{
    if (value.isBool()) {
        return value.toBool();
    }
    if (value.isDouble()) {
        return value.toInt() != 0;
    }
    if (value.isString()) {
        const QString normalized = value.toString().trimmed().toLower();
        if (normalized == QStringLiteral("1") || normalized == QStringLiteral("true")
            || normalized == QStringLiteral("yes")) {
            return true;
        }
        if (normalized == QStringLiteral("0") || normalized == QStringLiteral("false")
            || normalized == QStringLiteral("no")) {
            return false;
        }
    }
    return fallback;
}

QString storageBlockClassRoot()
{
    return QDir(boardlab::sysRoot()).filePath(QStringLiteral("class/block"));
}

QString decodeProcMountToken(QString value)
{
    // /proc/*/mounts uses fstab-style octal escaping for whitespace and '\\'.
    value.replace(QStringLiteral("\\040"), QStringLiteral(" "));
    value.replace(QStringLiteral("\\011"), QStringLiteral("\t"));
    value.replace(QStringLiteral("\\012"), QStringLiteral("\n"));
    value.replace(QStringLiteral("\\134"), QStringLiteral("\\"));
    return value;
}

struct StorageMountInfo {
    QString mountPoint;
    QString fileSystem;
};

struct StorageMountTable {
    QHash<QString, StorageMountInfo> byDeviceNumber;
    QSet<QString> protectedDeviceNumbers;
    QString provider;
    bool parsed = false;
    bool rootObserved = false;
    bool unresolvedProtectedMount = false;
};

QString normalizedDeviceNumber(const QString &value)
{
    const QStringList parts = value.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 2) {
        return QString();
    }
    bool majorOk = false;
    bool minorOk = false;
    const qulonglong majorNumber = parts.at(0).toULongLong(&majorOk);
    const qulonglong minorNumber = parts.at(1).toULongLong(&minorOk);
    if (!majorOk || !minorOk) {
        return QString();
    }
    return QStringLiteral("%1:%2").arg(majorNumber).arg(minorNumber);
}

QString lsblkDeviceNumber(const QJsonObject &node)
{
    return normalizedDeviceNumber(
        node.value(QStringLiteral("maj:min")).toString());
}

void collectLsblkDeviceNumbers(const QJsonArray &nodes, QSet<QString> *deviceNumbers)
{
    if (!deviceNumbers) {
        return;
    }
    for (const QJsonValue &value : nodes) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject node = value.toObject();
        const QString deviceNumber = lsblkDeviceNumber(node);
        if (!deviceNumber.isEmpty()) {
            deviceNumbers->insert(deviceNumber);
        }
        collectLsblkDeviceNumbers(node.value(QStringLiteral("children")).toArray(),
                                  deviceNumbers);
    }
}

bool storageTreeContainsProtectedDevice(const QJsonObject &node,
                                        const QSet<QString> &protectedDeviceNumbers)
{
    if (protectedDeviceNumbers.contains(lsblkDeviceNumber(node))) {
        return true;
    }
    const QJsonArray children = node.value(QStringLiteral("children")).toArray();
    for (const QJsonValue &child : children) {
        if (child.isObject()
            && storageTreeContainsProtectedDevice(child.toObject(),
                                                  protectedDeviceNumbers)) {
            return true;
        }
    }
    return false;
}

QString storageDeviceNumber(const QString &blockPath)
{
    return normalizedDeviceNumber(readTextFileForHelper(
        QDir(blockPath).filePath(QStringLiteral("dev"))));
}

QString deviceNumberFromStat(const QString &path, bool mountedPath)
{
    if (path.isEmpty()) {
        return QString();
    }
    struct stat status {};
    const QByteArray encodedPath = QFile::encodeName(path);
    if (::stat(encodedPath.constData(), &status) != 0) {
        return QString();
    }
    const dev_t deviceNumber = mountedPath ? status.st_dev : status.st_rdev;
    if (!mountedPath && !S_ISBLK(status.st_mode)) {
        return QString();
    }
    return QStringLiteral("%1:%2")
        .arg(static_cast<qulonglong>(major(deviceNumber)))
        .arg(static_cast<qulonglong>(minor(deviceNumber)));
}

QString storageProcRoot()
{
    return boardlab::fixtureRoot("BOARDLAB_PROC_ROOT", QStringLiteral("/proc"));
}

QString storageSourcePath(const QString &source)
{
    QString relativePath;
    if (source.startsWith(QStringLiteral("/dev/"))) {
        relativePath = source.mid(5);
    } else {
        const QList<QPair<QString, QString>> aliases {
            qMakePair(QStringLiteral("UUID="), QStringLiteral("disk/by-uuid")),
            qMakePair(QStringLiteral("LABEL="), QStringLiteral("disk/by-label")),
            qMakePair(QStringLiteral("PARTUUID="), QStringLiteral("disk/by-partuuid")),
            qMakePair(QStringLiteral("PARTLABEL="), QStringLiteral("disk/by-partlabel"))
        };
        for (const auto &alias : aliases) {
            if (source.startsWith(alias.first)) {
                QString value = source.mid(alias.first.size());
                if (value.size() >= 2
                    && ((value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
                        || (value.startsWith(QLatin1Char('\'')) && value.endsWith(QLatin1Char('\''))))) {
                    value = value.mid(1, value.size() - 2);
                }
                relativePath = QDir(alias.second).filePath(value);
                break;
            }
        }
    }

    if (relativePath.isEmpty()) {
        return source.startsWith(QLatin1Char('/')) ? QDir::cleanPath(source) : QString();
    }
    return QDir(deviceRootForHelper()).filePath(relativePath);
}

QString storageSourceDeviceNumber(const QString &source)
{
    const QString sourcePath = storageSourcePath(source);
    QString deviceNumber = deviceNumberFromStat(sourcePath, false);
    if (!deviceNumber.isEmpty()) {
        return deviceNumber;
    }

    const QString canonicalPath = QFileInfo(sourcePath).canonicalFilePath();
    const QString blockName = QFileInfo(canonicalPath.isEmpty() ? sourcePath : canonicalPath).fileName();
    if (blockName.isEmpty()) {
        return QString();
    }
    return storageDeviceNumber(QDir(storageBlockClassRoot()).filePath(blockName));
}

void addStorageMount(StorageMountTable *table,
                     const QString &deviceNumber,
                     const StorageMountInfo &info)
{
    if (!table) {
        return;
    }
    const bool protectedMount = storageNodeIsProtected(info.mountPoint, info.fileSystem);
    if (info.mountPoint == QStringLiteral("/")) {
        table->rootObserved = true;
    }
    if (protectedMount) {
        if (deviceNumber.isEmpty()) {
            table->unresolvedProtectedMount = true;
        } else {
            table->protectedDeviceNumbers.insert(deviceNumber);
        }
    }
    if (deviceNumber.isEmpty()) {
        return;
    }

    const auto existing = table->byDeviceNumber.constFind(deviceNumber);
    if (existing == table->byDeviceNumber.constEnd()
        || (protectedMount
            && !storageNodeIsProtected(existing->mountPoint, existing->fileSystem))) {
        table->byDeviceNumber.insert(deviceNumber, info);
    }
}

bool readStorageMountInfo(StorageMountTable *table)
{
    QFile file(QDir(storageProcRoot()).filePath(QStringLiteral("self/mountinfo")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    bool parsedLine = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              boardlab::kSkipEmptyParts);
        const int separator = fields.indexOf(QStringLiteral("-"));
        if (fields.size() < 10 || separator < 6 || separator + 2 >= fields.size()) {
            continue;
        }
        parsedLine = true;
        StorageMountInfo info;
        info.mountPoint = decodeProcMountToken(fields.at(4));
        info.fileSystem = decodeProcMountToken(fields.at(separator + 1));
        addStorageMount(table, normalizedDeviceNumber(fields.at(2)), info);
    }
    return parsedLine;
}

bool readStorageMountsFallback(StorageMountTable *table)
{
    const QString procRoot = storageProcRoot();
    const QString selfMounts = QDir(procRoot).filePath(QStringLiteral("self/mounts"));
    QFile file(QFileInfo::exists(selfMounts)
                   ? selfMounts : QDir(procRoot).filePath(QStringLiteral("mounts")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }

    bool parsedLine = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const QStringList fields = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              boardlab::kSkipEmptyParts);
        if (fields.size() < 3) {
            continue;
        }
        parsedLine = true;
        const QString source = decodeProcMountToken(fields.at(0));
        StorageMountInfo info;
        info.mountPoint = decodeProcMountToken(fields.at(1));
        info.fileSystem = decodeProcMountToken(fields.at(2));
        QString deviceNumber = storageSourceDeviceNumber(source);
        if (deviceNumber.isEmpty() && storageNodeIsProtected(info.mountPoint, info.fileSystem)
            && !boardlab::testModeEnabled()) {
            deviceNumber = deviceNumberFromStat(info.mountPoint, true);
        }
        addStorageMount(table, deviceNumber, info);
    }
    return parsedLine;
}

StorageMountTable storageMountsByDeviceNumber()
{
    StorageMountTable table;
    if (readStorageMountInfo(&table)) {
        table.parsed = true;
        table.provider = QStringLiteral("mountinfo");
        return table;
    }

    table = StorageMountTable();
    if (readStorageMountsFallback(&table)) {
        table.parsed = true;
        table.provider = QStringLiteral("mounts");
    }
    return table;
}

QString probeStorageFileSystem(const QString &device)
{
    const QString blkidPath = commandPathForHelper(QStringLiteral("blkid"));
    if (blkidPath.isEmpty()) {
        return QString();
    }

    int exitCode = -1;
    const QString output = runCommandForHelper(
        blkidPath,
        QStringList() << QStringLiteral("-o") << QStringLiteral("value")
                      << QStringLiteral("-s") << QStringLiteral("TYPE") << device,
        2500,
        &exitCode).trimmed();
    if (exitCode != 0 || output.isEmpty()
        || !QRegularExpression(QStringLiteral("^[A-Za-z0-9._+-]+$")).match(output).hasMatch()) {
        return QString();
    }
    return output;
}

QString parentBlockName(const QFileInfo &entry, bool partition)
{
    if (!partition) {
        return entry.fileName();
    }

    const QString canonicalPath = entry.canonicalFilePath();
    if (!canonicalPath.isEmpty()) {
        const QString parentName = QFileInfo(canonicalPath).dir().dirName();
        if (!parentName.isEmpty() && parentName != QStringLiteral("block")) {
            return parentName;
        }
    }

    const QString name = entry.fileName();
    const QList<QRegularExpression> patterns {
        QRegularExpression(QStringLiteral("^(mmcblk[0-9]+)p[0-9]+$")),
        QRegularExpression(QStringLiteral("^(nvme[0-9]+n[0-9]+)p[0-9]+$")),
        QRegularExpression(QStringLiteral("^((?:sd|hd|vd|xvd)[a-z]+)[0-9]+$"))
    };
    for (const QRegularExpression &pattern : patterns) {
        const QRegularExpressionMatch match = pattern.match(name);
        if (match.hasMatch()) {
            return match.captured(1);
        }
    }
    return name;
}

QString storageTransport(const QString &blockPath, const QString &deviceName)
{
    QString transport = readTextFileForHelper(
        QDir(blockPath).filePath(QStringLiteral("device/transport"))).trimmed().toLower();
    if (!transport.isEmpty()) {
        return transport;
    }

    const QFileInfo subsystem(QDir(blockPath).filePath(QStringLiteral("device/subsystem")));
    const QString subsystemPath = subsystem.canonicalFilePath().toLower();
    const QString subsystemName = QFileInfo(subsystemPath).fileName().toLower();
    const QString canonicalPath = QFileInfo(blockPath).canonicalFilePath().toLower();
    const QString evidence = subsystemPath + QLatin1Char(' ') + canonicalPath;
    if (evidence.contains(QStringLiteral("/usb"))) {
        return QStringLiteral("usb");
    }
    if (subsystemName == QStringLiteral("mmc")
        || evidence.contains(QStringLiteral("/mmc"))
        || deviceName.startsWith(QStringLiteral("mmcblk"))) {
        return QStringLiteral("mmc");
    }
    if (subsystemName == QStringLiteral("nvme")
        || evidence.contains(QStringLiteral("/nvme"))
        || deviceName.startsWith(QStringLiteral("nvme"))) {
        return QStringLiteral("nvme");
    }
    if (evidence.contains(QStringLiteral("/ufs"))) {
        return QStringLiteral("ufs");
    }
    if (evidence.contains(QStringLiteral("/ata"))
        || evidence.contains(QStringLiteral("/sata"))) {
        return QStringLiteral("sata");
    }
    if (subsystemName == QStringLiteral("virtio")
        || evidence.contains(QStringLiteral("/virtio"))) {
        return QStringLiteral("virtio");
    }
    return subsystemName;
}

qint64 storageSizeBytes(const QString &blockPath)
{
    bool ok = false;
    const qulonglong sectors = readTextFileForHelper(
        QDir(blockPath).filePath(QStringLiteral("size"))).toULongLong(&ok);
    if (!ok || sectors > static_cast<qulonglong>(std::numeric_limits<qint64>::max()) / 512ULL) {
        return 0;
    }
    return static_cast<qint64>(sectors * 512ULL);
}

QVariantList scanStorageDevicesFromSysfs(bool *scanSucceeded, QString *scanStatus)
{
    if (scanSucceeded) {
        *scanSucceeded = false;
    }
    const QString blockRoot = storageBlockClassRoot();
    const QDir directory(blockRoot);
    if (!directory.exists()) {
        if (scanStatus) {
            *scanStatus = QStringLiteral("sysfs block class is unavailable: %1").arg(blockRoot);
        }
        return QVariantList();
    }

    const QFileInfoList entries = directory.entryInfoList(
        QDir::Dirs | QDir::Files | QDir::System | QDir::NoDotAndDotDot,
        QDir::Name);
    const StorageMountTable mounts = storageMountsByDeviceNumber();
    const QString devRoot = deviceRootForHelper();
    const QRegularExpression virtualBlock(QStringLiteral("^(loop|ram|zram)[0-9]+$"));
    QVariantList candidates;
    QSet<QString> rootsWithPartitions;
    QSet<QString> protectedRoots;
    QSet<QString> knownDeviceNumbers;
    QHash<QString, QString> blockNameByDeviceNumber;
    QHash<QString, QString> parentNameByBlockName;

    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (virtualBlock.match(name).hasMatch()) {
            continue;
        }
        const QString blockPath = entry.absoluteFilePath();
        const bool partition = QFileInfo::exists(
            QDir(blockPath).filePath(QStringLiteral("partition")));
        const QString parentName = parentBlockName(entry, partition);
        if (partition) {
            rootsWithPartitions.insert(parentName);
        }
        const QString device = QDir(devRoot).filePath(name);
        const QString deviceNumber = storageDeviceNumber(blockPath);
        if (!deviceNumber.isEmpty()) {
            knownDeviceNumbers.insert(deviceNumber);
            blockNameByDeviceNumber.insert(deviceNumber, name);
        }
        parentNameByBlockName.insert(name, parentName);
        const StorageMountInfo mount = mounts.byDeviceNumber.value(deviceNumber);
        const bool protectedNode = storageNodeIsProtected(mount.mountPoint, mount.fileSystem);
        if (protectedNode) {
            protectedRoots.insert(parentName);
        }

        QVariantMap candidate;
        candidate.insert(QStringLiteral("name"), name);
        candidate.insert(QStringLiteral("device"), device);
        candidate.insert(QStringLiteral("blockPath"), blockPath);
        candidate.insert(QStringLiteral("partition"), partition);
        candidate.insert(QStringLiteral("parentName"), parentName);
        candidate.insert(QStringLiteral("mountPoint"), mount.mountPoint);
        candidate.insert(QStringLiteral("fstype"), mount.fileSystem);
        candidates.append(candidate);
    }

    bool mountAssociationTrusted = mounts.parsed && mounts.rootObserved
        && !mounts.unresolvedProtectedMount;
    QStringList protectedQueue;
    for (const QString &deviceNumber : mounts.protectedDeviceNumbers) {
        const QString blockName = blockNameByDeviceNumber.value(deviceNumber);
        if (!knownDeviceNumbers.contains(deviceNumber) || blockName.isEmpty()) {
            mountAssociationTrusted = false;
        } else {
            protectedQueue.append(blockName);
        }
    }

    // Device-mapper roots expose their physical backing devices through
    // sysfs/slaves. Protect those partitions and their whole parent disks too.
    QSet<QString> visitedProtectedBlocks;
    while (!protectedQueue.isEmpty()) {
        const QString blockName = protectedQueue.takeFirst();
        if (visitedProtectedBlocks.contains(blockName)) {
            continue;
        }
        visitedProtectedBlocks.insert(blockName);
        protectedRoots.insert(parentNameByBlockName.value(blockName, blockName));
        const QDir slaves(QDir(blockRoot).filePath(blockName + QStringLiteral("/slaves")));
        const QFileInfoList slaveEntries = slaves.entryInfoList(
            QDir::Dirs | QDir::Files | QDir::System | QDir::NoDotAndDotDot,
            QDir::Name);
        for (const QFileInfo &slave : slaveEntries) {
            protectedQueue.append(slave.fileName());
        }
    }

    QVariantList storageDevices;
    for (const QVariant &candidateValue : candidates) {
        const QVariantMap candidate = candidateValue.toMap();
        const QString name = candidate.value(QStringLiteral("name")).toString();
        const QString device = candidate.value(QStringLiteral("device")).toString();
        const QString blockPath = candidate.value(QStringLiteral("blockPath")).toString();
        const bool partition = candidate.value(QStringLiteral("partition")).toBool();
        const QString parentName = candidate.value(QStringLiteral("parentName")).toString();
        const QString parentPath = QDir(blockRoot).filePath(parentName);
        const QString physicalDevice = QDir(devRoot).filePath(parentName);
        const QString removableText = readTextFileForHelper(
            QDir(parentPath).filePath(QStringLiteral("removable"))).trimmed();
        const bool removable = removableText == QStringLiteral("1");
        const QString transport = storageTransport(parentPath, parentName);
        const QString type = storageTypeForPath(physicalDevice, transport, removable);
        QString fileSystem = candidate.value(QStringLiteral("fstype")).toString();
        const QString actualMountPoint = candidate.value(QStringLiteral("mountPoint")).toString();
        const bool mounted = !actualMountPoint.isEmpty()
            && actualMountPoint != QStringLiteral("[SWAP]");
        if (fileSystem.isEmpty() && !mounted) {
            fileSystem = probeStorageFileSystem(device);
        }
        const bool protectedNode = !mountAssociationTrusted
            || protectedRoots.contains(parentName)
            || storageNodeIsProtected(actualMountPoint, fileSystem);
        const bool rawWithChildren = !partition && rootsWithPartitions.contains(name)
            && fileSystem.isEmpty() && actualMountPoint.isEmpty();
        const bool internalRawEmmc = !partition && type == QStringLiteral("EMMC")
            && fileSystem.isEmpty() && actualMountPoint.isEmpty();
        if (rawWithChildren || internalRawEmmc) {
            continue;
        }

        const bool managedMount = mounted && isBoardLabManagedStorageMount(actualMountPoint);
        const bool mountable = !protectedNode && !mounted && !fileSystem.isEmpty();
        const qint64 bytes = storageSizeBytes(blockPath);
        const QString suggestedMount = suggestedStorageMountPoint(device);
        QString model = readTextFileForHelper(
            QDir(parentPath).filePath(QStringLiteral("device/model"))).simplified();
        if (model.isEmpty()) {
            model = readTextFileForHelper(
                QDir(parentPath).filePath(QStringLiteral("device/name"))).simplified();
        }

        QVariantMap info;
        info.insert(QStringLiteral("device"), device);
        info.insert(QStringLiteral("parentDevice"), physicalDevice);
        info.insert(QStringLiteral("type"), type);
        info.insert(QStringLiteral("size"), bytes > 0
                    ? QStringLiteral("%1 GB").arg(bytes / 1073741824.0, 0, 'f', 2)
                    : QStringLiteral("Unknown"));
        info.insert(QStringLiteral("sizeBytes"), bytes);
        info.insert(QStringLiteral("fstype"), fileSystem);
        info.insert(QStringLiteral("filesystem"), fileSystem.isEmpty()
                    ? QStringLiteral("未识别") : fileSystem);
        info.insert(QStringLiteral("label"), QString());
        info.insert(QStringLiteral("model"), model);
        info.insert(QStringLiteral("transport"), transport);
        info.insert(QStringLiteral("removable"), removable);
        info.insert(QStringLiteral("mounted"), mounted);
        info.insert(QStringLiteral("actualMountPoint"), actualMountPoint);
        info.insert(QStringLiteral("mountPoint"), mounted ? actualMountPoint : suggestedMount);
        info.insert(QStringLiteral("mountPointHint"), suggestedMount);
        info.insert(QStringLiteral("protected"), protectedNode);
        info.insert(QStringLiteral("managedMount"), managedMount);
        info.insert(QStringLiteral("canMount"), mountable);
        info.insert(QStringLiteral("canUnmount"), mounted && !protectedNode && managedMount);
        info.insert(QStringLiteral("canBenchmark"), mounted && !protectedNode && managedMount);
        info.insert(QStringLiteral("readSpeed"), QString());
        info.insert(QStringLiteral("writeSpeed"), QString());
        info.insert(QStringLiteral("testing"), false);
        info.insert(QStringLiteral("provider"), QStringLiteral("sysfs+%1")
                    .arg(mounts.provider.isEmpty() ? QStringLiteral("unresolved-mounts")
                                                   : mounts.provider));
        storageDevices.append(info);
    }

    if (scanSucceeded) {
        *scanSucceeded = true;
    }
    if (scanStatus) {
        *scanStatus = QStringLiteral("Sysfs inventory contains %1 storage devices")
                          .arg(storageDevices.size());
        if (!mountAssociationTrusted) {
            *scanStatus += QStringLiteral("; root/boot ownership is unresolved, so storage operations are disabled");
        }
    }
    return storageDevices;
}

void appendLsblkStorageNodes(const QJsonArray &nodes,
                             const QString &physicalPath,
                             const QString &physicalModel,
                             const QString &physicalTransport,
                             bool physicalRemovable,
                             bool physicalProtected,
                             const StorageMountTable &mounts,
                             bool mountAssociationTrusted,
                             QVariantList *storageDevices)
{
    if (!storageDevices) {
        return;
    }

    for (const QJsonValue &value : nodes) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject node = value.toObject();
        const QString path = node.value(QStringLiteral("path")).toString().trimmed();
        const QString deviceNumber = lsblkDeviceNumber(node);
        const QString nodeType = node.value(QStringLiteral("type")).toString().trimmed().toLower();
        const StorageMountInfo authoritativeMount = mounts.byDeviceNumber.value(deviceNumber);
        QString fileSystem = node.value(QStringLiteral("fstype")).toString().trimmed();
        if (!authoritativeMount.fileSystem.isEmpty()) {
            fileSystem = authoritativeMount.fileSystem;
        }
        const QString lsblkMountPoint = node.value(
            QStringLiteral("mountpoint")).toString().trimmed();
        const QString actualMountPoint = mountAssociationTrusted
            ? authoritativeMount.mountPoint : lsblkMountPoint;
        const QString nodeModel = node.value(QStringLiteral("model")).toString().simplified();
        const QString nodeTransport = node.value(QStringLiteral("tran")).toString().trimmed();
        const QJsonArray children = node.value(QStringLiteral("children")).toArray();
        const QString rootPath = physicalPath.isEmpty() ? path : physicalPath;
        const QString rootModel = physicalModel.isEmpty() ? nodeModel : physicalModel;
        const QString rootTransport = physicalTransport.isEmpty() ? nodeTransport : physicalTransport;
        const bool rootProtected = physicalPath.isEmpty()
            ? storageTreeContainsProtectedDevice(node, mounts.protectedDeviceNumbers)
            : physicalProtected;
        const bool removable = node.contains(QStringLiteral("rm"))
            ? jsonBoolean(node.value(QStringLiteral("rm")), physicalRemovable)
            : physicalRemovable;

        const bool candidateType = nodeType == QStringLiteral("disk")
            || nodeType == QStringLiteral("part")
            || nodeType == QStringLiteral("lvm")
            || nodeType == QStringLiteral("crypt");
        const bool leafOrMounted = children.isEmpty() || !fileSystem.isEmpty() || !actualMountPoint.isEmpty();
        // Internal eMMC boot and vendor partitions are not customer-testable
        // media. Keep its mounted/formatted partitions for visibility, while
        // still showing an unformatted external USB/TF/NVMe device.
        const QString storageType = storageTypeForPath(
            rootPath.isEmpty() ? path : rootPath, rootTransport, removable);
        const bool internalRawEmmc = storageType == QStringLiteral("EMMC")
            && fileSystem.isEmpty() && actualMountPoint.isEmpty();
        const bool virtualRamBlock = rootPath.startsWith(QStringLiteral("/dev/zram"));
        if (candidateType && leafOrMounted && !path.isEmpty() && !internalRawEmmc && !virtualRamBlock) {
            const bool protectedNode = !mountAssociationTrusted
                || deviceNumber.isEmpty()
                || rootProtected
                || storageNodeIsProtected(actualMountPoint, fileSystem);
            const bool mounted = !actualMountPoint.isEmpty() && actualMountPoint != QStringLiteral("[SWAP]");
            const bool mountable = !protectedNode && !mounted && !fileSystem.isEmpty();
            const bool managedMount = mounted && isBoardLabManagedStorageMount(actualMountPoint);
            const qint64 bytes = static_cast<qint64>(node.value(QStringLiteral("size")).toDouble());
            const QString suggestedMount = suggestedStorageMountPoint(path);
            const QString label = node.value(QStringLiteral("label")).toString().trimmed();

            QVariantMap info;
            info.insert(QStringLiteral("device"), path);
            info.insert(QStringLiteral("parentDevice"), rootPath);
            info.insert(QStringLiteral("type"), storageType);
            info.insert(QStringLiteral("size"), bytes > 0
                        ? QStringLiteral("%1 GB").arg(bytes / 1073741824.0, 0, 'f', 2)
                        : QStringLiteral("Unknown"));
            info.insert(QStringLiteral("sizeBytes"), bytes);
            info.insert(QStringLiteral("fstype"), fileSystem);
            info.insert(QStringLiteral("filesystem"), fileSystem.isEmpty() ? QStringLiteral("未识别") : fileSystem);
            info.insert(QStringLiteral("label"), label);
            info.insert(QStringLiteral("model"), rootModel);
            info.insert(QStringLiteral("transport"), rootTransport);
            info.insert(QStringLiteral("removable"), removable);
            info.insert(QStringLiteral("mounted"), mounted);
            info.insert(QStringLiteral("actualMountPoint"), actualMountPoint);
            info.insert(QStringLiteral("mountPoint"), mounted ? actualMountPoint : suggestedMount);
            info.insert(QStringLiteral("mountPointHint"), suggestedMount);
            info.insert(QStringLiteral("protected"), protectedNode);
            info.insert(QStringLiteral("managedMount"), managedMount);
            info.insert(QStringLiteral("canMount"), mountable);
            info.insert(QStringLiteral("canUnmount"), mounted && !protectedNode && managedMount);
            info.insert(QStringLiteral("canBenchmark"), mounted && !protectedNode && managedMount);
            info.insert(QStringLiteral("readSpeed"), QString());
            info.insert(QStringLiteral("writeSpeed"), QString());
            info.insert(QStringLiteral("testing"), false);
            info.insert(QStringLiteral("provider"), QStringLiteral("lsblk-json+%1")
                        .arg(mounts.provider.isEmpty()
                                 ? QStringLiteral("unresolved-mounts")
                                 : mounts.provider));
            storageDevices->append(info);
        }

        appendLsblkStorageNodes(children,
                                rootPath,
                                rootModel,
                                rootTransport,
                                removable,
                                rootProtected,
                                mounts,
                                mountAssociationTrusted,
                                storageDevices);
    }
}

QVariantList scanStorageDevicesWithCommand(bool *scanSucceeded = nullptr,
                                           QString *scanStatus = nullptr,
                                           bool *inventoryValid = nullptr)
{
    if (scanSucceeded) {
        *scanSucceeded = false;
    }
    if (inventoryValid) {
        *inventoryValid = false;
    }
    if (scanStatus) {
        scanStatus->clear();
    }

    QString enhancedProviderFailure;
    const QString lsblkPath = commandPathForHelper(QStringLiteral("lsblk"));
    if (!lsblkPath.isEmpty()) {
        int exitCode = -1;
        const QString output = runCommandForHelper(
            lsblkPath,
            QStringList() << QStringLiteral("--json")
                          << QStringLiteral("--bytes")
                          << QStringLiteral("--output")
                          << QStringLiteral("NAME,PATH,TYPE,SIZE,FSTYPE,MOUNTPOINT,LABEL,MODEL,TRAN,RM,MAJ:MIN"),
            3500,
            &exitCode);
        if (exitCode == 0) {
            QJsonParseError parseError;
            const QJsonDocument document = QJsonDocument::fromJson(output.toUtf8(), &parseError);
            const QJsonValue blockDevices = document.isObject()
                ? document.object().value(QStringLiteral("blockdevices")) : QJsonValue();
            if (parseError.error == QJsonParseError::NoError && blockDevices.isArray()) {
                const StorageMountTable mounts = storageMountsByDeviceNumber();
                QSet<QString> lsblkDeviceNumbers;
                collectLsblkDeviceNumbers(blockDevices.toArray(), &lsblkDeviceNumbers);
                bool mountAssociationTrusted = mounts.parsed && mounts.rootObserved
                    && !mounts.unresolvedProtectedMount;
                for (const QString &deviceNumber : mounts.protectedDeviceNumbers) {
                    if (!lsblkDeviceNumbers.contains(deviceNumber)) {
                        mountAssociationTrusted = false;
                    }
                }
                QVariantList storageDevices;
                appendLsblkStorageNodes(blockDevices.toArray(),
                                        QString(),
                                        QString(),
                                        QString(),
                                        false,
                                        false,
                                        mounts,
                                        mountAssociationTrusted,
                                        &storageDevices);
                if (scanSucceeded) {
                    *scanSucceeded = true;
                }
                if (inventoryValid) {
                    *inventoryValid = true;
                }
                if (scanStatus) {
                    *scanStatus = QStringLiteral("Found %1 storage devices")
                                      .arg(storageDevices.size());
                    if (!storageDevices.isEmpty() && !mountAssociationTrusted) {
                        *scanStatus += QStringLiteral("; root/boot ownership is unresolved, so storage operations are disabled");
                    }
                }
                return storageDevices;
            }
            enhancedProviderFailure = parseError.error != QJsonParseError::NoError
                ? QStringLiteral("invalid lsblk JSON: %1").arg(parseError.errorString())
                : QStringLiteral("lsblk JSON has no blockdevices array");
        } else {
            enhancedProviderFailure = QStringLiteral("lsblk exited with code %1: %2")
                                          .arg(exitCode)
                                          .arg(compactCommandOutput(output));
        }
    } else {
        enhancedProviderFailure = QStringLiteral("lsblk is not installed");
    }

    bool fallbackSucceeded = false;
    QString fallbackStatus;
    const QVariantList fallback = scanStorageDevicesFromSysfs(
        &fallbackSucceeded, &fallbackStatus);
    if (inventoryValid) {
        *inventoryValid = fallbackSucceeded;
    }
    if (scanStatus) {
        *scanStatus = fallbackSucceeded
            ? QStringLiteral("%1 (degraded provider: %2)")
                  .arg(fallbackStatus, enhancedProviderFailure)
            : QStringLiteral("Storage scan unavailable: %1; %2")
                  .arg(enhancedProviderFailure, fallbackStatus);
    }
    return fallback;
}

BackendOperationResult scanStorageDevicesOperation()
{
    BackendOperationResult result;
    bool inventoryValid = false;
    result.storageDevices = scanStorageDevicesWithCommand(
        &result.success, &result.status, &inventoryValid);
    result.storageDevicesValid = inventoryValid;
    result.result = QString::number(result.storageDevices.size());
    return result;
}

QVariantMap permittedStorageDevice(const QString &device)
{
    const QString requestedDevice = device.trimmed();
    for (const QVariant &entry : scanStorageDevicesWithCommand()) {
        const QVariantMap info = entry.toMap();
        if (info.value(QStringLiteral("device")).toString() == requestedDevice) {
            return info;
        }
    }
    return QVariantMap();
}

BackendOperationResult mountStorageDeviceWithCommand(const QString &device, const QString &mountPoint)
{
    BackendOperationResult result;
    const QString trimmedDevice = device.trimmed();
    const QString trimmedMount = mountPoint.trimmed();
    if (trimmedDevice.isEmpty() || trimmedMount.isEmpty()) {
        result.status = QStringLiteral("Storage mount requires device and mount point");
        return result;
    }

    const QVariantMap deviceInfo = permittedStorageDevice(trimmedDevice);
    if (!deviceInfo.value(QStringLiteral("canMount")).toBool()
        || trimmedMount != deviceInfo.value(QStringLiteral("mountPointHint")).toString()) {
        result.status = QStringLiteral("Storage mount request is not an approved removable partition");
        return result;
    }

    QDir().mkpath(trimmedMount);
    int exitCode = -1;
    const QString output = runCommandForHelper(QStringLiteral("mount"),
                                               QStringList() << trimmedDevice << trimmedMount,
                                               8000,
                                               &exitCode);
    result.success = exitCode == 0;
    result.status = result.success
        ? QStringLiteral("Mounted %1 to %2").arg(trimmedDevice, trimmedMount)
        : QStringLiteral("Failed to mount %1: %2").arg(trimmedDevice, compactCommandOutput(output));
    bool inventoryValid = false;
    result.storageDevices = scanStorageDevicesWithCommand(
        nullptr, nullptr, &inventoryValid);
    result.storageDevicesValid = inventoryValid;
    return result;
}

BackendOperationResult unmountStorageDeviceWithCommand(const QString &mountPoint)
{
    BackendOperationResult result;
    const QString trimmedMount = mountPoint.trimmed();
    if (trimmedMount.isEmpty()) {
        result.status = QStringLiteral("Storage unmount requires mount point");
        return result;
    }

    bool approved = false;
    for (const QVariant &entry : scanStorageDevicesWithCommand()) {
        const QVariantMap info = entry.toMap();
        if (info.value(QStringLiteral("actualMountPoint")).toString() == trimmedMount
            && info.value(QStringLiteral("canUnmount")).toBool()) {
            approved = true;
            break;
        }
    }
    if (!approved) {
        result.status = QStringLiteral("Storage unmount request is not an approved removable partition");
        return result;
    }

    int exitCode = -1;
    const QString output = runCommandForHelper(QStringLiteral("umount"),
                                               QStringList() << trimmedMount,
                                               8000,
                                               &exitCode);
    result.success = exitCode == 0;
    result.status = result.success
        ? QStringLiteral("Unmounted %1").arg(trimmedMount)
        : QStringLiteral("Failed to unmount %1: %2").arg(trimmedMount, compactCommandOutput(output));
    bool inventoryValid = false;
    result.storageDevices = scanStorageDevicesWithCommand(
        nullptr, nullptr, &inventoryValid);
    result.storageDevicesValid = inventoryValid;
    return result;
}

BackendOperationResult testStorageSpeedWithCommand(const QString &mountPoint)
{
    BackendOperationResult result;
    const QString trimmedMount = mountPoint.trimmed();
    if (trimmedMount.isEmpty()) {
        result.status = QStringLiteral("Storage speed test requires a mount point");
        return result;
    }
    if (!QFileInfo(trimmedMount).isDir()) {
        result.status = QStringLiteral("Storage speed test mount point is not a directory: %1").arg(trimmedMount);
        return result;
    }

    bool approved = false;
    for (const QVariant &entry : scanStorageDevicesWithCommand()) {
        const QVariantMap info = entry.toMap();
        if (info.value(QStringLiteral("actualMountPoint")).toString() == trimmedMount
            && info.value(QStringLiteral("canBenchmark")).toBool()) {
            approved = true;
            break;
        }
    }
    if (!approved) {
        result.status = QStringLiteral("Storage speed test requires an approved mounted removable partition");
        return result;
    }

    const QString testFile = QDir(trimmedMount).filePath(QStringLiteral("rk-speed-test.dat"));
    int writeExit = -1;
    const QString writeOutput = runCommandForHelper(QStringLiteral("dd"),
                                                    QStringList() << QStringLiteral("if=/dev/zero")
                                                                  << QStringLiteral("of=%1").arg(testFile)
                                                                  << QStringLiteral("bs=1M")
                                                                  << QStringLiteral("count=100")
                                                                  << QStringLiteral("oflag=direct"),
                                                    35000,
                                                    &writeExit);
    const QString writeSpeed = speedFromDdOutput(writeOutput);

    int readExit = -1;
    QString readOutput;
    QString readSpeed;
    if (writeExit == 0) {
        readOutput = runCommandForHelper(QStringLiteral("dd"),
                                         QStringList() << QStringLiteral("if=%1").arg(testFile)
                                                       << QStringLiteral("of=/dev/null")
                                                       << QStringLiteral("bs=1M")
                                                       << QStringLiteral("iflag=direct"),
                                         35000,
                                         &readExit);
        readSpeed = speedFromDdOutput(readOutput);
    }
    QFile::remove(testFile);

    result.success = writeExit == 0 && readExit == 0 && !writeSpeed.isEmpty() && !readSpeed.isEmpty();
    result.result = QStringLiteral("%1\n%2").arg(readSpeed.isEmpty() ? QStringLiteral("N/A") : readSpeed,
                                                 writeSpeed.isEmpty() ? QStringLiteral("N/A") : writeSpeed);
    if (result.success) {
        result.status = QStringLiteral("Speed test completed: Read %1, Write %2").arg(readSpeed, writeSpeed);
    } else {
        result.status = QStringLiteral("Speed test failed: write[%1] %2; read[%3] %4")
                            .arg(QString::number(writeExit),
                                 compactCommandOutput(writeOutput, 160),
                                 QString::number(readExit),
                                 compactCommandOutput(readOutput, 160));
    }
    return result;
}

BackendOperationResult pingInterfaceWithCommand(const QString &interfaceName, const QString &target)
{
    BackendOperationResult result;
    const QString iface = interfaceName.trimmed();
    const QString host = target.trimmed().isEmpty()
        ? QString::fromLatin1(kDefaultPingTarget)
        : target.trimmed();
    if (!isSafeNetworkInterfaceName(iface)) {
        result.status = QStringLiteral("Invalid network interface name");
        result.result = result.status;
        return result;
    }
    if (!isExistingNetworkInterface(iface)) {
        result.status = QStringLiteral("Network interface is unavailable: %1").arg(iface);
        result.result = result.status;
        return result;
    }
    if (!isSafePingTarget(host)) {
        result.status = QStringLiteral("Invalid ping target. Use a hostname or IPv4 address.");
        result.result = result.status;
        return result;
    }
    if (commandPathForHelper(QStringLiteral("ping")).isEmpty()) {
        result.status = QStringLiteral("ping not found; install iputils-ping");
        result.result = result.status;
        return result;
    }

    int exitCode = -1;
    // The Ethernet workflow presents and configures IPv4.  Force the same
    // protocol for its connectivity check so a dual-stack hostname cannot
    // silently validate an unrelated IPv6 route instead.
    const QString output = runCommandForHelper(QStringLiteral("ping"),
                                               QStringList() << QStringLiteral("-4")
                                                             << QStringLiteral("-n")
                                                             << QStringLiteral("-c") << QString::number(kPingPacketCount)
                                                             << QStringLiteral("-W") << QString::number(kPingReplyTimeoutSeconds)
                                                             << QStringLiteral("-w") << QString::number(kPingDeadlineSeconds)
                                                             << QStringLiteral("-I") << iface
                                                             << host,
                                               kPingProcessTimeoutMs,
                                               &exitCode);
    result.success = exitCode == 0;
    if (result.success) {
        result.status = QStringLiteral("Ping passed: %1 -> %2").arg(iface, host);
    } else if (exitCode == -1) {
        result.status = QStringLiteral("Ping timed out after %1 seconds: %2 -> %3")
                            .arg(kPingDeadlineSeconds).arg(iface, host);
    } else if (output.contains(QStringLiteral("Operation not permitted"), Qt::CaseInsensitive)
               || output.contains(QStringLiteral("Permission denied"), Qt::CaseInsensitive)) {
        result.status = QStringLiteral("Ping permission denied on %1. Install/configure ping capability or run through an authorized desktop account.")
                            .arg(iface);
    } else {
        result.status = QStringLiteral("Ping failed: %1 -> %2 (%3)")
                            .arg(iface)
                            .arg(host)
                            .arg(compactCommandOutput(output, 140));
    }
    result.result = output.isEmpty() ? result.status : output;
    return result;
}

bool containsAny(const QString &text, const QStringList &needles)
{
    for (const QString &needle : needles) {
        if (text.contains(needle)) {
            return true;
        }
    }
    return false;
}

bool processIsAlive(qint64 pid)
{
    if (pid <= 0) {
        return false;
    }
    errno = 0;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

bool waitForProcessAlive(qint64 pid, int settleMs)
{
    if (pid <= 0) {
        return false;
    }

    if (!processIsAlive(pid)) {
        return false;
    }
    if (settleMs > 0) {
        QThread::msleep(static_cast<unsigned long>(settleMs));
    }
    return processIsAlive(pid);
}

bool processStoppedAfterDelay(qint64 pid, unsigned long delayMs)
{
    if (pid <= 0) {
        return true;
    }
    if (delayMs > 0) {
        QThread::msleep(delayMs);
    }
    return !processIsAlive(pid);
}

bool terminateProcess(qint64 pid)
{
    if (pid <= 0 || !processIsAlive(pid)) {
        return false;
    }
    const pid_t nativePid = static_cast<pid_t>(pid);
    if (::kill(nativePid, SIGTERM) != 0 && errno != ESRCH) {
        return false;
    }
    if (processStoppedAfterDelay(pid, 150)) {
        return true;
    }
    if (::kill(nativePid, SIGKILL) != 0 && errno != ESRCH) {
        return false;
    }
    return processStoppedAfterDelay(pid, 150);
}

QString processCommandLine(qint64 pid)
{
    if (pid <= 0) {
        return QString();
    }
    QFile file(QStringLiteral("/proc/%1/cmdline").arg(pid));
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }
    QByteArray data = file.readAll();
    data.replace('\0', ' ');
    return QString::fromLocal8Bit(data).simplified();
}

bool processCommandMatches(qint64 pid, const QStringList &allowedCommandTokens)
{
    if (allowedCommandTokens.isEmpty()) {
        return true;
    }

    const QString commandLine = processCommandLine(pid);
    if (commandLine.isEmpty()) {
        return false;
    }
    for (const QString &token : allowedCommandTokens) {
        if (!token.isEmpty() && commandLine.contains(token, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QStringList audioProcessKillTokens()
{
    return QStringList()
        << QStringLiteral("aplay")
        << QStringLiteral("arecord")
        << QStringLiteral("speaker-test");
}

void stopProcessList(QVector<qint64> pids, const QStringList &allowedCommandTokens)
{
    for (const qint64 pid : pids) {
        if (processIsAlive(pid) && processCommandMatches(pid, allowedCommandTokens)) {
            terminateProcess(pid);
        }
    }
}

bool hasVideoCaptureCapability(const QString &text)
{
    return text.contains(QStringLiteral("video capture"));
}

bool isMemoryToMemoryVideoNode(const QString &text)
{
    return containsAny(text,
                       QStringList()
                           << QStringLiteral("memory-to-memory")
                           << QStringLiteral("mem2mem")
                           << QStringLiteral("m2m")
                           << QStringLiteral("rkvdec")
                           << QStringLiteral("rkvenc")
                           << QStringLiteral("vpu")
                           << QStringLiteral("decoder")
                           << QStringLiteral("encoder"));
}

}

namespace {

constexpr int kKeyMappingVerificationIntervalMs = 1000;

QVariantMap environmentFieldPlaceholder(const QString &unit, const QString &error)
{
    QVariantMap field;
    field.insert(QStringLiteral("path"), QString());
    field.insert(QStringLiteral("channel"), QString());
    field.insert(QStringLiteral("unit"), unit);
    field.insert(QStringLiteral("raw"), QString());
    field.insert(QStringLiteral("value"), QVariant());
    field.insert(QStringLiteral("timestamp"), QString());
    field.insert(QStringLiteral("valid"), false);
    field.insert(QStringLiteral("error"), error.isEmpty() ? QStringLiteral("not sampled") : error);
    return field;
}

QVariantMap flattenEnvironmentStatus(const QVariantMap &rawState, bool inFlight, const QString &mode)
{
    QVariantMap state = rawState;
    const QVariantList sensors = rawState.value(QStringLiteral("sensors")).toList();
    const QVariantMap firstSensor = sensors.isEmpty() ? QVariantMap() : sensors.first().toMap();
    const QVariantMap runtime = firstSensor.value(QStringLiteral("runtime")).toMap();
    const QVariantMap deviceTree = firstSensor.value(QStringLiteral("deviceTree")).toMap();
    const QString overallError = rawState.value(QStringLiteral("error")).toString();
    const QVariantMap temperature = firstSensor.value(QStringLiteral("temperature")).toMap();
    const QVariantMap humidity = firstSensor.value(QStringLiteral("relativeHumidity")).toMap();

    QString source = deviceTree.value(QStringLiteral("fullName")).toString();
    if (source.isEmpty()) {
        source = runtime.value(QStringLiteral("ofFullName")).toString();
    }
    if (source.isEmpty()) {
        source = runtime.value(QStringLiteral("deviceName")).toString();
    }
    if (source.isEmpty()) {
        source = QStringLiteral("unavailable");
    }

    const QString sampledAt = rawState.value(QStringLiteral("sampledAt")).toString();
    const QString discoveredAt = rawState.value(QStringLiteral("discoveredAt")).toString();
    QVariantMap sampling;
    sampling.insert(QStringLiteral("inFlight"), inFlight);
    sampling.insert(QStringLiteral("mode"), inFlight ? mode : QStringLiteral("idle"));
    sampling.insert(QStringLiteral("sequence"), rawState.value(QStringLiteral("samplingSequence")).toString());
    sampling.insert(QStringLiteral("minimumDelayMs"), rawState.value(QStringLiteral("minimumSampleDelayMs")).toInt());

    state.insert(QStringLiteral("available"), !sensors.isEmpty());
    state.insert(QStringLiteral("source"), source);
    state.insert(QStringLiteral("updatedAt"), !sampledAt.isEmpty() ? sampledAt : discoveredAt);
    state.insert(QStringLiteral("temperature"), temperature.isEmpty()
                 ? environmentFieldPlaceholder(QStringLiteral("C"), overallError)
                 : temperature);
    state.insert(QStringLiteral("humidity"), humidity.isEmpty()
                 ? environmentFieldPlaceholder(QStringLiteral("%RH"), overallError)
                 : humidity);
    state.insert(QStringLiteral("relativeHumidity"), humidity.isEmpty()
                 ? environmentFieldPlaceholder(QStringLiteral("%RH"), overallError)
                 : humidity);
    state.insert(QStringLiteral("sampling"), sampling);
    state.insert(QStringLiteral("status"), inFlight
                 ? (mode == QStringLiteral("discovering") ? QStringLiteral("discovering") : QStringLiteral("sampling"))
                 : rawState.value(QStringLiteral("status"), QStringLiteral("unavailable")).toString());
    return state;
}

QString environmentOperationMessage(const QVariantMap &state)
{
    const QString status = state.value(QStringLiteral("status")).toString();
    if (status == QStringLiteral("sampled")) {
        return QStringLiteral("Environment sample completed");
    }
    if (status == QStringLiteral("partial")) {
        return QStringLiteral("Environment sample completed with partial data");
    }
    if (status == QStringLiteral("unavailable")) {
        return QStringLiteral("No paired temperature/humidity IIO device was discovered");
    }
    return state.value(QStringLiteral("error"), QStringLiteral("Environment sample failed")).toString();
}

QVariantList expectedKeysFromCandidates(const QVariantList &candidates)
{
    QVariantList expected;
    QSet<int> seenCodes;
    for (const QVariant &candidateVariant : candidates) {
        const QVariantMap candidate = candidateVariant.toMap();
        const QVariantList keyCodes = candidate.value(QStringLiteral("keyCodes")).toList();
        for (const QVariant &keyVariant : keyCodes) {
            const QVariantMap rawKey = keyVariant.toMap();
            bool ok = false;
            const int code = rawKey.value(QStringLiteral("code")).toInt(&ok);
            if (!ok || seenCodes.contains(code)) {
                continue;
            }
            if (!KeyMonitor::isSafeKeyCode(code)) {
                continue;
            }
            const QString name = KeyMonitor::friendlyKeyName(code);
            seenCodes.insert(code);
            QVariantMap key;
            key.insert(QStringLiteral("code"), code);
            key.insert(QStringLiteral("name"), name);
            key.insert(QStringLiteral("label"), name);
            key.insert(QStringLiteral("complete"), false);
            expected.append(key);
        }
    }
    return expected;
}

QVariantList normalizeKeyCandidates(const QVariantList &candidates)
{
    QVariantList normalized;
    for (const QVariant &candidateVariant : candidates) {
        QVariantMap candidate = candidateVariant.toMap();
        const QString deviceId = candidate.value(QStringLiteral("deviceId"),
                                                 candidate.value(QStringLiteral("dtPath"))).toString().trimmed();
        const QString eventPath = candidate.value(QStringLiteral("eventPath"),
                                                  candidate.value(QStringLiteral("event"))).toString().trimmed();
        if (deviceId.isEmpty() || eventPath.isEmpty()) {
            continue;
        }
        // A select widget needs `id`, but it is always the device-tree ID;
        // `/dev/input/eventN` remains evidence only and is never submitted as
        // a control selector.
        candidate.insert(QStringLiteral("id"), deviceId);
        candidate.insert(QStringLiteral("deviceId"), deviceId);
        candidate.insert(QStringLiteral("dtPath"), deviceId);
        candidate.insert(QStringLiteral("event"), eventPath);
        candidate.insert(QStringLiteral("eventPath"), eventPath);
        candidate.insert(QStringLiteral("eligible"), true);

        QVariantList supported;
        const QVariantList keyCodes = candidate.value(QStringLiteral("keyCodes")).toList();
        for (const QVariant &keyVariant : keyCodes) {
            QVariantMap key = keyVariant.toMap();
            bool ok = false;
            const int code = key.value(QStringLiteral("code")).toInt(&ok);
            if (!ok) {
                continue;
            }
            if (!KeyMonitor::isSafeKeyCode(code)) {
                continue;
            }
            const QString name = KeyMonitor::friendlyKeyName(code);
            key.insert(QStringLiteral("name"), name);
            key.insert(QStringLiteral("label"), name);
            supported.append(key);
        }
        candidate.insert(QStringLiteral("supportedKeys"), supported);
        normalized.append(candidate);
    }
    return normalized;
}

} // namespace

namespace {

QString preferredFileBrowserStartPath()
{
    auto usableDirectory = [](const QString &candidate) -> QString {
        const QString cleaned = QDir::cleanPath(candidate.trimmed());
        if (cleaned.isEmpty() || cleaned == QStringLiteral("/")) {
            return QString();
        }
        QDir dir(cleaned);
        if (!dir.exists() && !QDir().mkpath(cleaned)) {
            return QString();
        }
        QFileInfo info(cleaned);
        if (!info.exists() || !info.isDir()) {
            return QString();
        }
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty() || canonical == QStringLiteral("/")) {
            return QString();
        }
        return canonical;
    };

    const QByteArray uploadRoot = qgetenv("BOARDLAB_UPLOAD_ROOT").trimmed();
    if (!uploadRoot.isEmpty()) {
        const QString path = usableDirectory(QString::fromLocal8Bit(uploadRoot));
        if (!path.isEmpty()) {
            return path;
        }
    }

    const QByteArray dataRoot = qgetenv("BOARDLAB_DATA_ROOT").trimmed();
    if (!dataRoot.isEmpty()) {
        const QString path = usableDirectory(
            QDir(QString::fromLocal8Bit(dataRoot)).filePath(QStringLiteral("uploads")));
        if (!path.isEmpty()) {
            return path;
        }
    }

    const QString productPath = usableDirectory(QStringLiteral("/var/lib/boardlab/uploads"));
    if (!productPath.isEmpty()) {
        return productPath;
    }

    // Ubuntu/Debian desktop users keep browsing under their home directory.
    const QString home = QDir::homePath().trimmed();
    if (!home.isEmpty() && home != QStringLiteral("/") && home != QStringLiteral("/root")) {
        return home;
    }
    // Buildroot root has no useful /root home; start in the product uploads dir
    // even if it only exists after mkpath above failed for permission reasons.
    return QStringLiteral("/var/lib/boardlab/uploads");
}

} // namespace

BoardBackend::BoardBackend(BoardProfile *boardProfile, QObject *parent)
    : QObject(parent)
    , m_currentPath(preferredFileBrowserStartPath())
    , m_boardProfile(boardProfile)
{
    m_gpuBenchmark = new GpuBenchmark(this);
    // Discovery is a local executable lookup only; it does not start a GPU
    // workload or touch a display.  This lets launchers show a truthful
    // availability state without running a benchmark on startup.
    m_gpuBenchmark->discover();
    connect(m_gpuBenchmark, &GpuBenchmark::statusChanged, this, &BoardBackend::changed);
    connect(m_gpuBenchmark, &GpuBenchmark::finished, this,
            [this](bool success, const QVariantMap &state) {
        const QString message = state.value(QStringLiteral("error")).toString().trimmed().isEmpty()
            ? state.value(QStringLiteral("status")).toString().trimmed()
            : state.value(QStringLiteral("error")).toString().trimmed();
        const QString status = message.isEmpty()
            ? QStringLiteral("GPU benchmark finished")
            : message;
        if (m_backendBusy && m_backendBusyOperation == QStringLiteral("gpu-start")) {
            setStatus(status);
            finishAsyncOperation(QStringLiteral("gpu-start"), success,
                                 state.value(QStringLiteral("score")).toString(), status);
        } else {
            setStatus(status);
            emit operationFinished(QStringLiteral("gpu-start"), success,
                                   state.value(QStringLiteral("score")).toString(), status);
        }
    });
    connect(m_gpuBenchmark, &GpuBenchmark::previewFinished, this,
            [this](bool success, const QVariantMap &state) {
        const QString message = state.value(QStringLiteral("previewError")).toString().trimmed().isEmpty()
            ? state.value(QStringLiteral("previewStatus")).toString().trimmed()
            : state.value(QStringLiteral("previewError")).toString().trimmed();
        const QString status = message.isEmpty()
            ? QStringLiteral("Embedded GPU preview finished")
            : message;
        if (m_backendBusy && m_backendBusyOperation == QStringLiteral("gpu-preview-start")) {
            setStatus(status);
            finishAsyncOperation(QStringLiteral("gpu-preview-start"), success,
                                 state.value(QStringLiteral("previewScore")).toString(), status);
        } else {
            setStatus(status);
            emit operationFinished(QStringLiteral("gpu-preview-start"), success,
                                   state.value(QStringLiteral("previewScore")).toString(), status);
        }
    });

    m_webCameraPreview = new WebCameraPreview(this);
    connect(m_webCameraPreview, &WebCameraPreview::changed, this, [this]() {
        // The preview process owns its own lifecycle and must not occupy the
        // generic hardware-operation lock for an entire browser session. Its
        // immutable state snapshot is published through /api/board and
        // /api/camera instead.
        emit changed();
    });

    m_wifiManager = new WifiManager(this);
    connect(m_wifiManager, &WifiManager::stateChanged, this, &BoardBackend::changed);
    connect(m_wifiManager, &WifiManager::accessPointsChanged, this, &BoardBackend::changed);
    connect(m_wifiManager, &WifiManager::busyChanged, this, &BoardBackend::changed);
    connect(m_wifiManager, &WifiManager::operationFinished, this,
            [this](const QString &operation, bool success, const QVariantMap &result) {
        const QString backendOperation = QStringLiteral("wifi-") + operation;
        const QString status = result.value(QStringLiteral("message")).toString().trimmed().isEmpty()
            ? QStringLiteral("Wi-Fi operation finished")
            : result.value(QStringLiteral("message")).toString().trimmed();
        const QString detail = result.value(QStringLiteral("selectedDevice")).toString();
        if (m_backendBusy && m_backendBusyOperation == backendOperation) {
            setStatus(status);
            finishAsyncOperation(backendOperation, success, detail, status);
        } else {
            setStatus(status);
            emit operationFinished(backendOperation, success, detail, status);
        }
    });

    // Discovery only reads topology and metadata; it never reads a DHT value.
    // The temperature/humidity sampling sequence remains explicitly requested
    // and runs asynchronously through refreshEnvironmentAsync().
    m_environmentRawState = EnvironmentMonitor().discover();
    m_environmentState = flattenEnvironmentStatus(m_environmentRawState, false, QString());
    m_keyMonitor = new KeyMonitor(this);
    m_keyMappingVerificationTimer = new QTimer(this);
    m_keyMappingVerificationTimer->setInterval(kKeyMappingVerificationIntervalMs);
    m_keyMappingVerificationTimer->setTimerType(Qt::CoarseTimer);
    connect(m_keyMappingVerificationTimer, &QTimer::timeout, this, [this]() {
        // Keep an open evdev fd bound to the device-tree candidate that was
        // selected at Start. This only re-reads key topology; it does not run
        // the broader board diagnostics and never grabs an input device.
        if (m_keyMonitor && m_keyMonitor->active()) {
            refreshKeyTestDevices();
        }
    });
    const auto publishKeyStatus = [this]() { emit changed(); };
    connect(m_keyMonitor, &KeyMonitor::activeChanged, this, [this, publishKeyStatus]() {
        if (m_keyMappingVerificationTimer) {
            if (m_keyMonitor && m_keyMonitor->active()) {
                m_keyMappingVerificationTimer->start();
            } else {
                m_keyMappingVerificationTimer->stop();
            }
        }
        publishKeyStatus();
    });
    connect(m_keyMonitor, &KeyMonitor::eventsChanged, this, publishKeyStatus);
    connect(m_keyMonitor, &KeyMonitor::stateChanged, this, publishKeyStatus);
    refreshKeyTestDevices();
    refreshFan();
    QTimer::singleShot(250, this, &BoardBackend::refresh);
}

BoardBackend::~BoardBackend()
{
    stopCanLoopSender(nullptr);
    stopWatchdogTest();
    if (m_cellularDialerPid > 0 && !m_cellularDialerPath.isEmpty()) {
        stopProcessList(QVector<qint64>() << m_cellularDialerPid,
                        QStringList() << m_cellularDialerPath);
    }
    m_cellularDialerPid = 0;
    m_cellularDialerPath.clear();
    stopProcessList(m_audioProcessPids, audioProcessKillTokens());
    m_audioProcessPids.clear();
    stopBeep();
    if (m_keyMappingVerificationTimer) {
        m_keyMappingVerificationTimer->stop();
    }
    if (m_keyMonitor) {
        m_keyMonitor->stop();
    }
}

QStringList BoardBackend::serialDevices() const
{
    return m_serialDevices;
}

QVariantList BoardBackend::ethernetInterfaces() const
{
    return m_ethernetInterfaces;
}

QStringList BoardBackend::canInterfaces() const
{
    return m_canInterfaces;
}

QStringList BoardBackend::cellularDevices() const
{
    return m_cellularDevices;
}

QVariantMap BoardBackend::cellularModemStatus() const
{
    QVariantMap status = m_cellularModemStatus;
    const bool running = m_cellularDialerPid > 0
        && !m_cellularDialerPath.isEmpty()
        && processIsAlive(m_cellularDialerPid)
        && processCommandMatches(m_cellularDialerPid, QStringList() << m_cellularDialerPath);
    status.insert(QStringLiteral("dialerRunning"), running);
    status.insert(QStringLiteral("dialerPid"), running ? m_cellularDialerPid : 0);
    if (!status.contains(QStringLiteral("preferredInterface"))) {
        status.insert(QStringLiteral("preferredInterface"), preferredCellularInterfaceFromDiscovery(m_cellularDevices));
    }
    return status;
}

QStringList BoardBackend::videoDevices() const
{
    return m_videoDevices;
}

QStringList BoardBackend::cameraVideoDevices() const
{
    return m_cameraVideoDevices;
}

QStringList BoardBackend::hdmiInVideoDevices() const
{
    return m_hdmiInVideoDevices;
}

QVariantList BoardBackend::cameraCaptureDevices() const
{
    return m_cameraCaptureDevices;
}

QVariantList BoardBackend::hdmiInCaptureDevices() const
{
    return m_hdmiInCaptureDevices;
}

QString BoardBackend::cameraVideoDevice() const
{
    return m_cameraVideoDevice;
}

QString BoardBackend::hdmiInVideoDevice() const
{
    return m_hdmiInVideoDevice;
}
QStringList BoardBackend::watchdogDevices() const
{
    return m_watchdogDevices;
}
QVariantList BoardBackend::fileEntries() const
{
    return m_fileEntries;
}

QString BoardBackend::currentPath() const
{
    return m_currentPath;
}

bool BoardBackend::backlightAvailable() const
{
    return !m_backlight.path.isEmpty() && m_backlight.maxBrightness > 0;
}

int BoardBackend::backlightPercent() const
{
    if (!backlightAvailable()) {
        return 0;
    }
    return qBound(0, static_cast<int>((100.0 * m_backlight.brightness) / m_backlight.maxBrightness), 100);
}

int BoardBackend::audioVolumePercent() const
{
    return m_audioVolumePercent;
}

QVariantList BoardBackend::audioPlaybackDevices() const
{
    return m_audioPlaybackDevices;
}

QVariantList BoardBackend::audioCaptureDevices() const
{
    return m_audioCaptureDevices;
}

bool BoardBackend::buzzerAvailable() const
{
    return m_buzzerAvailable;
}

QString BoardBackend::buzzerAvailabilityStatus() const
{
    return m_buzzerAvailabilityStatus;
}

QString BoardBackend::buzzerControlMethod() const
{
    return m_buzzerControlMethod;
}

QVariantMap BoardBackend::fanStatus() const
{
    QVariantMap status;
    const bool available = !m_fan.path.isEmpty() && !m_fan.ambiguous;
    status.insert(QStringLiteral("available"), available);
    status.insert(QStringLiteral("name"), m_fan.name);
    status.insert(QStringLiteral("path"), m_fan.path);
    status.insert(QStringLiteral("pwm"), available ? m_fan.pwm : 0);
    status.insert(QStringLiteral("pwmMax"), available ? m_fan.pwmMax : 255);
    status.insert(QStringLiteral("percent"), available && m_fan.pwmMax > 0
                 ? qBound(0, qRound(100.0 * m_fan.pwm / m_fan.pwmMax), 100) : 0);
    status.insert(QStringLiteral("tempControl"), m_fan.tempControl);
    status.insert(QStringLiteral("mode"), m_fan.ambiguous ? QStringLiteral("unavailable")
                 : !available ? QStringLiteral("unavailable")
                 : (m_fan.tempControl == 1 ? QStringLiteral("auto")
                    : (m_fan.tempControl == 0 ? QStringLiteral("manual") : QStringLiteral("unsupported"))));
    status.insert(QStringLiteral("message"), m_fan.ambiguous
                 ? QStringLiteral("检测到多个 pwmfan 风扇节点，已停用控制以避免选错设备")
                 : !available ? QStringLiteral("未发现 pwmfan 风扇节点")
                 : (m_fan.tempControl < 0 ? QStringLiteral("已发现 pwmfan，但内核未提供 temp_control")
                                          : QStringLiteral("pwmfan 已就绪")));
    return status;
}

bool BoardBackend::setFanAuto()
{
    refreshFan();
    const QString operation = QStringLiteral("fan-auto");
    QString status;
    bool success = false;
    if (!operationAllowed(operation)) {
        status = operationUnavailableReason(operation);
    } else if (m_fan.ambiguous) {
        status = QStringLiteral("检测到多个 pwmfan 风扇节点，已停用控制以避免选错设备");
    } else if (m_fan.path.isEmpty()) {
        status = QStringLiteral("未发现 pwmfan 风扇节点");
    } else if (m_fan.tempControlPath.isEmpty()) {
        status = QStringLiteral("当前 pwmfan 内核未提供 temp_control，无法切换自动温控");
    } else {
        QString error;
        success = writeTextFile(m_fan.tempControlPath, QStringLiteral("1"), &error);
        status = success ? QStringLiteral("风扇已切换为自动温控")
                         : QStringLiteral("切换自动温控失败: %1").arg(error);
    }
    refreshFan();
    setStatus(status);
    emit operationFinished(operation, success, QString(), status);
    return success;
}

bool BoardBackend::setFanManualPercent(int percent)
{
    refreshFan();
    const QString operation = QStringLiteral("fan-manual");
    QString status;
    bool success = false;
    if (percent < 0 || percent > 100) {
        status = QStringLiteral("风扇转速百分比必须在 0-100 之间");
    } else if (!operationAllowed(operation)) {
        status = operationUnavailableReason(operation);
    } else if (m_fan.ambiguous) {
        status = QStringLiteral("检测到多个 pwmfan 风扇节点，已停用控制以避免选错设备");
    } else if (m_fan.path.isEmpty()) {
        status = QStringLiteral("未发现 pwmfan 风扇节点");
    } else if (m_fan.tempControlPath.isEmpty()) {
        status = QStringLiteral("当前 pwmfan 内核未提供 temp_control，无法切换手动转速");
    } else {
        QString error;
        // The mode switch must precede pwm1: otherwise the next thermal
        // notification can overwrite the value while the application writes.
        success = writeTextFile(m_fan.tempControlPath, QStringLiteral("0"), &error);
        if (success) {
            const int raw = qRound(m_fan.pwmMax * percent / 100.0);
            success = writeTextFile(m_fan.pwmPath, QString::number(raw), &error);
            if (!success) {
                const QString pwmError = error;
                QString restoreError;
                const bool restored = m_fan.tempControl == 0
                    || writeTextFile(m_fan.tempControlPath, QString::number(m_fan.tempControl), &restoreError);
                status = restored
                    ? QStringLiteral("设置风扇手动转速失败: %1；已恢复原温控模式").arg(pwmError)
                    : QStringLiteral("设置风扇手动转速失败: %1；恢复原温控模式失败: %2")
                          .arg(pwmError, restoreError);
            }
        }
        if (success) {
            status = QStringLiteral("风扇手动转速已设为 %1%%").arg(percent);
        } else if (status.isEmpty()) {
            status = QStringLiteral("设置风扇手动转速失败: %1").arg(error);
        }
    }
    refreshFan();
    setStatus(status);
    emit operationFinished(operation, success, QString::number(percent), status);
    return success;
}

QString BoardBackend::requiredCapabilityForOperation(const QString &operation) const
{
    const QString normalized = operation.trimmed().toLower();
    if (normalized.startsWith(QStringLiteral("gpio-"))) {
        return QStringLiteral("gpio");
    }
    if (normalized.startsWith(QStringLiteral("uart-"))) {
        return QStringLiteral("uart");
    }
    if (normalized.startsWith(QStringLiteral("ethernet-")) || normalized == QStringLiteral("ping-interface")) {
        return QStringLiteral("ethernet");
    }
    if (normalized.startsWith(QStringLiteral("can-"))) {
        return QStringLiteral("can");
    }
    if (normalized.startsWith(QStringLiteral("cellular-"))) {
        return QStringLiteral("cellular");
    }
    if (normalized.startsWith(QStringLiteral("camera-"))) {
        return QStringLiteral("camera");
    }
    if (normalized.startsWith(QStringLiteral("hdmiin-"))) {
        return QStringLiteral("hdmiin");
    }
    if (normalized.startsWith(QStringLiteral("watchdog-"))) {
        return QStringLiteral("watchdog");
    }
    if (normalized.startsWith(QStringLiteral("audio-beep-"))) {
        return QStringLiteral("buzzer");
    }
    if (normalized.startsWith(QStringLiteral("audio-"))) {
        return QStringLiteral("music");
    }
    if (normalized.startsWith(QStringLiteral("led-"))) {
        return QStringLiteral("led");
    }
    if (normalized.startsWith(QStringLiteral("fan-"))) {
        return QStringLiteral("fan");
    }
    if (normalized.startsWith(QStringLiteral("backlight-"))) {
        return QStringLiteral("backlight");
    }
    if (normalized.startsWith(QStringLiteral("storage-"))) {
        return QStringLiteral("storage");
    }
    if (normalized.startsWith(QStringLiteral("files-"))) {
        return QStringLiteral("files");
    }
    if (normalized.startsWith(QStringLiteral("environment-"))) {
        return QStringLiteral("temperaturehumidity");
    }
    if (normalized.startsWith(QStringLiteral("keys-"))) {
        return QStringLiteral("keys");
    }
    if (normalized.startsWith(QStringLiteral("gpu-"))) {
        return QStringLiteral("gpu");
    }
    if (normalized.startsWith(QStringLiteral("wifi-"))) {
        return QStringLiteral("wifi");
    }
    return QString();
}

bool BoardBackend::operationAllowed(const QString &operation) const
{
    const QString capability = requiredCapabilityForOperation(operation);
    return capability.isEmpty() || !m_boardProfile || m_boardProfile->hasCapability(capability);
}

QString BoardBackend::operationUnavailableReason(const QString &operation) const
{
    const QString capability = requiredCapabilityForOperation(operation);
    if (capability.isEmpty() || operationAllowed(operation)) {
        return QString();
    }

    static const QHash<QString, QString> displayNames = {
        {QStringLiteral("gpio"), QStringLiteral("GPIO")},
        {QStringLiteral("uart"), QStringLiteral("串口")},
        {QStringLiteral("ethernet"), QStringLiteral("以太网")},
        {QStringLiteral("can"), QStringLiteral("CAN")},
        {QStringLiteral("cellular"), QStringLiteral("4G / 5G")},
        {QStringLiteral("camera"), QStringLiteral("摄像头")},
        {QStringLiteral("hdmiin"), QStringLiteral("HDMI 输入")},
        {QStringLiteral("watchdog"), QStringLiteral("看门狗")},
        {QStringLiteral("music"), QStringLiteral("音乐与录音")},
        {QStringLiteral("buzzer"), QStringLiteral("蜂鸣器")},
        {QStringLiteral("led"), QStringLiteral("板载 LED")},
        {QStringLiteral("fan"), QStringLiteral("风扇")},
        {QStringLiteral("backlight"), QStringLiteral("背光")},
        {QStringLiteral("storage"), QStringLiteral("存储测试")},
        {QStringLiteral("files"), QStringLiteral("文件浏览")},
        {QStringLiteral("temperaturehumidity"), QStringLiteral("温湿度")},
        {QStringLiteral("keys"), QStringLiteral("按键")},
        {QStringLiteral("gpu"), QStringLiteral("GPU 基准")},
        {QStringLiteral("wifi"), QStringLiteral("Wi-Fi")}
    };
    return QStringLiteral("当前板型未启用%1测试").arg(displayNames.value(capability, capability));
}

QString BoardBackend::statusText() const
{
    return m_statusText;
}

bool BoardBackend::backendBusy() const
{
    return m_backendBusy;
}

QString BoardBackend::backendBusyOperation() const
{
    return m_backendBusyOperation;
}

bool BoardBackend::audioOperationBusy() const
{
    return m_audioOperationBusy;
}

QVariantList BoardBackend::ledDevices() const
{
    return m_ledDevices;
}

bool BoardBackend::ledAvailable() const
{
    return !m_ledDevices.isEmpty();
}

QVariantMap BoardBackend::rtcStatus() const
{
    return m_rtcStatus;
}

QVariantMap BoardBackend::npuStatus() const
{
    return m_npuStatus;
}

QVariantMap BoardBackend::wifiStatus() const
{
    return m_wifiStatus;
}

QVariantMap BoardBackend::bluetoothStatus() const
{
    return m_bluetoothStatus;
}

QVariantMap BoardBackend::gpuBenchmarkStatus() const
{
    return m_gpuBenchmark ? m_gpuBenchmark->status() : QVariantMap();
}

QVariantMap BoardBackend::webCameraPreviewStatus() const
{
    if (m_webCameraPreview) {
        return m_webCameraPreview->status();
    }

    QVariantMap unavailable;
    unavailable.insert(QStringLiteral("available"), false);
    unavailable.insert(QStringLiteral("running"), false);
    unavailable.insert(QStringLiteral("active"), false);
    unavailable.insert(QStringLiteral("previewAvailable"), false);
    unavailable.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
    unavailable.insert(QStringLiteral("message"), QStringLiteral("Web 摄像头实时预览服务不可用"));
    return unavailable;
}

QVariantMap BoardBackend::wifiManagerStatus() const
{
    return m_wifiManager ? m_wifiManager->state() : QVariantMap();
}

QVariantList BoardBackend::wifiAccessPoints() const
{
    return m_wifiManager ? m_wifiManager->accessPoints() : QVariantList();
}

bool BoardBackend::wifiManagerBusy() const
{
    return m_wifiManager && m_wifiManager->busy();
}

QVariantList BoardBackend::inputDevices() const
{
    return m_inputDevices;
}

QVariantList BoardBackend::displayOutputs() const
{
    return m_displayOutputs;
}

QVariantMap BoardBackend::environmentStatus() const
{
    return m_environmentState;
}

QVariantMap BoardBackend::environmentState() const
{
    return m_environmentState;
}

bool BoardBackend::environmentRefreshInFlight() const
{
    return m_environmentRefreshInFlight;
}

QVariantMap BoardBackend::keyTestStatus() const
{
    QVariantMap status;
    const QVariantMap monitorState = m_keyMonitor ? m_keyMonitor->state() : QVariantMap();
    const QVariantList events = m_keyMonitor ? m_keyMonitor->events() : QVariantList();
    QVariantList expected = monitorState.value(QStringLiteral("expectedKeys")).toList();
    if (expected.isEmpty()) {
        expected = expectedKeysFromCandidates(m_keyTestDevices);
    }

    int passedCount = 0;
    for (const QVariant &expectedVariant : expected) {
        if (expectedVariant.toMap().value(QStringLiteral("complete")).toBool()) {
            ++passedCount;
        }
    }
    int pressCount = 0;
    int releaseCount = 0;
    int repeatCount = 0;
    for (const QVariant &eventVariant : events) {
        const QString eventState = eventVariant.toMap().value(QStringLiteral("state")).toString();
        if (eventState == QStringLiteral("press")) {
            ++pressCount;
        } else if (eventState == QStringLiteral("release")) {
            ++releaseCount;
        } else if (eventState == QStringLiteral("repeat")) {
            ++repeatCount;
        }
    }

    const bool monitoring = m_keyMonitor && m_keyMonitor->active();
    QString selectedDeviceId = monitorState.value(QStringLiteral("deviceId")).toString().trimmed();
    if (selectedDeviceId.isEmpty() && !m_keyTestDevices.isEmpty()) {
        selectedDeviceId = m_keyTestDevices.first().toMap().value(QStringLiteral("deviceId")).toString();
    }
    QString message = monitorState.value(QStringLiteral("status")).toString().trimmed();
    if (message.isEmpty() || (message == QStringLiteral("Ready") && m_keyTestDevices.isEmpty())) {
        message = m_keyTestDevices.isEmpty()
            ? QStringLiteral("No device-tree user-key device discovered")
            : QStringLiteral("Ready to monitor a board user-key device");
    }

    status.insert(QStringLiteral("available"), !m_keyTestDevices.isEmpty());
    status.insert(QStringLiteral("status"), message);
    status.insert(QStringLiteral("monitoring"), monitoring);
    status.insert(QStringLiteral("selectedDeviceId"), selectedDeviceId);
    status.insert(QStringLiteral("devices"), m_keyTestDevices);
    status.insert(QStringLiteral("expectedKeys"), expected);
    status.insert(QStringLiteral("passedCount"), passedCount);
    status.insert(QStringLiteral("pressCount"), pressCount);
    status.insert(QStringLiteral("releaseCount"), releaseCount);
    status.insert(QStringLiteral("repeatCount"), repeatCount);
    status.insert(QStringLiteral("events"), events);
    status.insert(QStringLiteral("lastEvent"), events.isEmpty() ? QVariantMap() : events.last().toMap());
    status.insert(QStringLiteral("lastError"), monitorState.value(QStringLiteral("lastError")).toString());
    status.insert(QStringLiteral("readOnly"), true);
    return status;
}

QVariantMap BoardBackend::hdmiInStatus() const
{
    return m_hdmiInStatus;
}

bool BoardBackend::discoverGpuBenchmark()
{
    if (!m_gpuBenchmark) {
        return false;
    }
    m_gpuBenchmark->discover();
    emit changed();
    return m_gpuBenchmark->status().value(QStringLiteral("available")).toBool();
}

bool BoardBackend::startGpuBenchmark()
{
    const QString operation = QStringLiteral("gpu-start");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!m_gpuBenchmark) {
        const QString status = QStringLiteral("GPU benchmark service is unavailable");
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }

    m_gpuBenchmark->discover();
    if (!m_gpuBenchmark->status().value(QStringLiteral("available")).toBool()) {
        const QString status = m_gpuBenchmark->status().value(QStringLiteral("error")).toString();
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!beginAsyncOperation(operation)) {
        return false;
    }
    if (!m_gpuBenchmark->start()) {
        const QString status = m_gpuBenchmark->status().value(QStringLiteral("error"),
                                                               QStringLiteral("GPU benchmark could not start")).toString();
        setStatus(status);
        finishAsyncOperation(operation, false, QString(), status);
        return false;
    }

    setStatus(QStringLiteral("GPU benchmark started"));
    emit changed();
    return true;
}

bool BoardBackend::stopGpuBenchmark()
{
    if (!m_gpuBenchmark || !m_gpuBenchmark->running()) {
        setStatus(QStringLiteral("GPU benchmark is not running"));
        return false;
    }
    if (!m_gpuBenchmark->stop()) {
        setStatus(QStringLiteral("GPU benchmark stop was not accepted"));
        return false;
    }

    setStatus(QStringLiteral("GPU benchmark stop requested"));
    emit changed();
    return true;
}

bool BoardBackend::startGpuPreview()
{
    const QString operation = QStringLiteral("gpu-preview-start");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!m_gpuBenchmark) {
        const QString status = QStringLiteral("Embedded GPU preview service is unavailable");
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }

    m_gpuBenchmark->discover();
    const QVariantMap previewStatus = m_gpuBenchmark->status();
    if (!previewStatus.value(QStringLiteral("previewAvailable")).toBool()) {
        const QString status = previewStatus.value(QStringLiteral("previewError"),
                                                    QStringLiteral("Embedded GPU preview is unavailable."))
                                   .toString();
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!beginAsyncOperation(operation)) {
        return false;
    }
    if (!m_gpuBenchmark->startPreview()) {
        const QString status = m_gpuBenchmark->status().value(
            QStringLiteral("previewError"),
            QStringLiteral("Embedded GPU preview could not start")).toString();
        setStatus(status);
        finishAsyncOperation(operation, false, QString(), status);
        return false;
    }

    const QString status = QStringLiteral("Embedded GPU preview started in the Qt GPU test page");
    setStatus(status);
    emit changed();
    return true;
}

bool BoardBackend::stopGpuPreview()
{
    if (!m_gpuBenchmark || !m_gpuBenchmark->previewRunning()) {
        setStatus(QStringLiteral("Embedded GPU preview is not running"));
        return false;
    }
    if (!m_gpuBenchmark->stopPreview()) {
        setStatus(QStringLiteral("Embedded GPU preview stop was not accepted"));
        return false;
    }

    setStatus(QStringLiteral("Embedded GPU preview stop requested"));
    emit changed();
    return true;
}

bool BoardBackend::startWebCameraPreview(const QString &device)
{
    const QString operation = QStringLiteral("camera-preview-start");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!m_webCameraPreview) {
        const QString status = QStringLiteral("Web 摄像头实时预览服务不可用");
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }

    // A page can be opened before the deferred startup discovery has run.
    // Refresh the passive V4L2 inventory once here so a real MIPI mainpath is
    // not rejected solely because its initial scan is still queued.
    if (m_cameraVideoDevices.isEmpty() || m_cameraVideoDevice.isEmpty()) {
        refreshVideoDevices();
    }

    QString selectedDevice = device.trimmed();
    if (selectedDevice.isEmpty()) {
        selectedDevice = m_cameraVideoDevice;
    }
    if (selectedDevice.isEmpty()) {
        const QString status = QStringLiteral("未发现可用于 Web 实时预览的摄像头节点");
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!isVideoNodePath(selectedDevice) || !m_cameraVideoDevices.contains(selectedDevice)) {
        const QString status = QStringLiteral("摄像头节点不在当前发现列表中：%1").arg(selectedDevice);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }

    QString mode = QStringLiteral("mipi");
    for (const QVariant &entry : m_cameraCaptureDevices) {
        const QVariantMap candidate = entry.toMap();
        if (candidate.value(QStringLiteral("path")).toString() == selectedDevice) {
            const QString discoveredMode = candidate.value(QStringLiteral("mode")).toString().trimmed();
            if (!discoveredMode.isEmpty()) {
                mode = discoveredMode;
            }
            break;
        }
    }

    if (!m_webCameraPreview->start(selectedDevice, mode)) {
        const QVariantMap preview = m_webCameraPreview->status();
        const QString status = preview.value(QStringLiteral("error"), preview.value(QStringLiteral("message"))).toString();
        setStatus(status.isEmpty() ? QStringLiteral("Web 摄像头实时预览未能启动") : status);
        emit operationFinished(operation, false, selectedDevice, m_statusText);
        return false;
    }

    const QString status = QStringLiteral("Web 摄像头实时预览启动请求已提交：%1").arg(selectedDevice);
    setStatus(status);
    emit operationFinished(operation, true, selectedDevice, status);
    emit changed();
    return true;
}

bool BoardBackend::stopWebCameraPreview()
{
    const QString operation = QStringLiteral("camera-preview-stop");
    if (!m_webCameraPreview) {
        const QString status = QStringLiteral("Web 摄像头实时预览服务不可用");
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    const QString previewState = m_webCameraPreview->status().value(QStringLiteral("status")).toString();
    const bool hasPreviewSession = m_webCameraPreview->running() || m_webCameraPreview->active()
        || previewState == QStringLiteral("starting")
        || previewState == QStringLiteral("waiting")
        || previewState == QStringLiteral("stopping");
    if (!hasPreviewSession) {
        const QString status = QStringLiteral("Web 摄像头实时预览未在运行");
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!m_webCameraPreview->stop()) {
        const QVariantMap preview = m_webCameraPreview->status();
        const QString status = preview.value(QStringLiteral("error"), preview.value(QStringLiteral("message"))).toString();
        setStatus(status.isEmpty() ? QStringLiteral("Web 摄像头实时预览停止请求未被接受") : status);
        emit operationFinished(operation, false, QString(), m_statusText);
        return false;
    }

    const QString status = QStringLiteral("Web 摄像头实时预览停止请求已提交");
    setStatus(status);
    emit operationFinished(operation, true, QString(), status);
    emit changed();
    return true;
}

QByteArray BoardBackend::webCameraPreviewJpeg() const
{
    return m_webCameraPreview ? m_webCameraPreview->latestJpeg() : QByteArray();
}

bool BoardBackend::refreshWifiAsync()
{
    const QString operation = QStringLiteral("wifi-refresh");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!m_wifiManager || !beginAsyncOperation(operation)) {
        return false;
    }
    if (!m_wifiManager->refresh()) {
        if (m_backendBusy && m_backendBusyOperation == operation) {
            const QString status = m_wifiManager->state().value(QStringLiteral("message"),
                                                                  QStringLiteral("Wi-Fi status read was not accepted")).toString();
            setStatus(status);
            finishAsyncOperation(operation, false, QString(), status);
        }
        return false;
    }
    return true;
}

bool BoardBackend::scanWifiAsync(const QString &interfaceName)
{
    const QString operation = QStringLiteral("wifi-scan");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!m_wifiManager || !beginAsyncOperation(operation)) {
        return false;
    }
    if (!m_wifiManager->scan(interfaceName)) {
        if (m_backendBusy && m_backendBusyOperation == operation) {
            const QString status = m_wifiManager->state().value(QStringLiteral("message"),
                                                                  QStringLiteral("Wi-Fi scan was not accepted")).toString();
            setStatus(status);
            finishAsyncOperation(operation, false, QString(), status);
        }
        return false;
    }
    return true;
}

bool BoardBackend::connectWifiAsync(const QString &interfaceName,
                                    const QString &networkId,
                                    const QString &password)
{
    const QString operation = QStringLiteral("wifi-connect");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!m_wifiManager || !beginAsyncOperation(operation)) {
        return false;
    }
    if (!m_wifiManager->connectToNetwork(interfaceName, networkId, password)) {
        if (m_backendBusy && m_backendBusyOperation == operation) {
            const QString status = m_wifiManager->state().value(QStringLiteral("message"),
                                                                  QStringLiteral("Wi-Fi connection was not accepted")).toString();
            setStatus(status);
            finishAsyncOperation(operation, false, QString(), status);
        }
        return false;
    }
    return true;
}

bool BoardBackend::disconnectWifiAsync(const QString &interfaceName)
{
    const QString operation = QStringLiteral("wifi-disconnect");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!m_wifiManager || !beginAsyncOperation(operation)) {
        return false;
    }
    if (!m_wifiManager->disconnect(interfaceName)) {
        if (m_backendBusy && m_backendBusyOperation == operation) {
            const QString status = m_wifiManager->state().value(QStringLiteral("message"),
                                                                  QStringLiteral("Wi-Fi disconnect was not accepted")).toString();
            setStatus(status);
            finishAsyncOperation(operation, false, QString(), status);
        }
        return false;
    }
    return true;
}

void BoardBackend::refresh()
{
    // Input topology is small and read-only, while an eventN remap can leave
    // a monitor attached to an old input node.  Check it before the broader
    // three-second diagnostics cache so remaps are torn down promptly.
    refreshKeyTestDevices();
    refreshFan();
    if (m_refreshInFlight) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastRefreshMs > 0 && now - m_lastRefreshMs < 3000) {
        if (m_statusText.isEmpty()) {
            setStatus(QStringLiteral("Board backend ready"));
        }
        return;
    }
    m_lastRefreshMs = now;
    m_refreshInFlight = true;
    refreshLedDevices();
    refreshBuzzerAvailability();
    refreshAudioDevices();
    refreshReadOnlyDiagnostics();
    refreshVideoDevicesAsync();
    if (m_statusText.isEmpty()) {
        m_statusText = QStringLiteral("Board backend ready");
    }
    emit changed();
    openPathAsync(m_currentPath.isEmpty() ? QDir::homePath() : m_currentPath);

    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher]() {
        const BackendOperationResult result = watcher->result();
        bool changedData = false;

        if (result.serialDevicesValid && m_serialDevices != result.serialDevices) {
            m_serialDevices = result.serialDevices;
            changedData = true;
        }
        if (result.ethernetInterfacesValid && m_ethernetInterfaces != result.ethernetInterfaces) {
            m_ethernetInterfaces = result.ethernetInterfaces;
            changedData = true;
        }
        if (result.canInterfacesValid && m_canInterfaces != result.canInterfaces) {
            m_canInterfaces = result.canInterfaces;
            changedData = true;
        }
        if (result.cellularDevicesValid && m_cellularDevices != result.cellularDevices) {
            m_cellularDevices = result.cellularDevices;
            changedData = true;
        }
        if (result.cellularModemStatusValid && m_cellularModemStatus != result.cellularModemStatus) {
            m_cellularModemStatus = result.cellularModemStatus;
            changedData = true;
        }
        if (result.watchdogDevicesValid && m_watchdogDevices != result.watchdogDevices) {
            m_watchdogDevices = result.watchdogDevices;
            changedData = true;
        }
        if (result.backlightValid) {
            if (m_backlight.name != result.backlightName
                || m_backlight.path != result.backlightPath
                || m_backlight.maxBrightness != result.backlightMaxBrightness
                || m_backlight.brightness != result.backlightBrightness) {
                changedData = true;
            }
            m_backlight.name = result.backlightName;
            m_backlight.path = result.backlightPath;
            m_backlight.maxBrightness = result.backlightMaxBrightness;
            m_backlight.brightness = result.backlightBrightness;
        }
        if (result.audioVolumePercent >= 0
            && m_audioVolumePercent != qBound(0, result.audioVolumePercent, 100)) {
            m_audioVolumePercent = qBound(0, result.audioVolumePercent, 100);
            changedData = true;
        }

        m_refreshInFlight = false;
        const bool statusChanged = m_statusText != result.status && !result.status.trimmed().isEmpty();
        if (statusChanged) {
            setStatus(result.status);
        } else if (changedData) {
            emit changed();
        }
        watcher->deleteLater();
    });
    const QString socFamily = m_boardProfile ? m_boardProfile->socFamily() : QString();
    watcher->setFuture(QtConcurrent::run(refreshCachedBoardStateWithCommand, socFamily));

    if (m_statusText.isEmpty()) {
        setStatus(QStringLiteral("Board backend ready"));
    } else {
        emit changed();
    }
}

bool BoardBackend::refreshEnvironmentAsync()
{
    const QString operation = QStringLiteral("environment-refresh");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (m_environmentRefreshInFlight) {
        return false;
    }

    m_environmentRefreshInFlight = true;
    m_environmentState = flattenEnvironmentStatus(m_environmentRawState, true, QStringLiteral("sampling"));
    emit changed();

    // Capture the effective root before submitting work.  This keeps the test
    // fixture stable and ensures the worker has no dependency on mutable UI
    // state while it waits for the DHT-safe interval.
    const QString monitorRoot = EnvironmentMonitor::defaultIioRoot();
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, operation]() {
        m_environmentRawState = watcher->result();
        m_environmentRefreshInFlight = false;
        m_environmentState = flattenEnvironmentStatus(m_environmentRawState, false, QString());
        const QString stateName = m_environmentState.value(QStringLiteral("status")).toString();
        const bool success = stateName == QStringLiteral("sampled");
        const QString message = environmentOperationMessage(m_environmentState);
        emit changed();
        emit operationFinished(operation, success,
                               m_environmentState.value(QStringLiteral("updatedAt")).toString(), message);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([monitorRoot]() {
        EnvironmentMonitor monitor(monitorRoot);
        return monitor.sample();
    }));
    return true;
}

bool BoardBackend::startKeyMonitoring(const QString &deviceId)
{
    const QString operation = QStringLiteral("keys-start");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }

    // eventN may change after a reboot, driver reload or USB hotplug. Resolve
    // the stable DT path again immediately before opening an input node.
    refreshKeyTestDevices();
    const QString requestedId = deviceId.trimmed();
    if (requestedId.isEmpty()) {
        const QString status = QStringLiteral("A device-tree key device must be selected before monitoring starts");
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        emit changed();
        return false;
    }
    if (!m_keyMonitor || !m_keyMonitor->start(requestedId, m_keyTestDevices)) {
        const QString status = m_keyMonitor && !m_keyMonitor->lastError().trimmed().isEmpty()
            ? m_keyMonitor->lastError().trimmed()
            : QStringLiteral("Unable to start board key monitoring");
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        emit changed();
        return false;
    }

    const QString status = m_keyMonitor->state().value(QStringLiteral("status")).toString();
    setStatus(status);
    emit operationFinished(operation, true, requestedId, status);
    emit changed();
    return true;
}

void BoardBackend::stopKeyMonitoring()
{
    const QString operation = QStringLiteral("keys-stop");
    if (!m_keyMonitor) {
        return;
    }
    const bool wasMonitoring = m_keyMonitor->active();
    m_keyMonitor->stop();
    const QString status = m_keyMonitor->state().value(QStringLiteral("status")).toString();
    if (wasMonitoring) {
        setStatus(status);
        emit operationFinished(operation, true, m_keyMonitor->activeDeviceId(), status);
    }
    emit changed();
}

void BoardBackend::clearKeyTestEvents()
{
    const QString operation = QStringLiteral("keys-clear");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return;
    }
    if (!m_keyMonitor) {
        return;
    }
    m_keyMonitor->clear();
    const QString status = QStringLiteral("Key test events cleared");
    setStatus(status);
    emit operationFinished(operation, true, QString(), status);
    emit changed();
}

bool BoardBackend::setBacklightPercent(int percent)
{
    refreshBacklight();
    if (!backlightAvailable()) {
        setStatus(QStringLiteral("No /sys/class/backlight device found"));
        return false;
    }

    const int clamped = qBound(1, percent, 100);
    const int raw = qMax(1, static_cast<int>((m_backlight.maxBrightness * clamped) / 100.0));
    QString errorText;
    if (!writeTextFile(m_backlight.path + QStringLiteral("/brightness"), QString::number(raw), &errorText)) {
        setStatus(QStringLiteral("Backlight write failed: %1").arg(errorText));
        return false;
    }

    refreshBacklight();
    setStatus(QStringLiteral("Backlight set to %1%").arg(backlightPercent()));
    return true;
}

bool BoardBackend::setGpioValue(const QString &pinName, bool high)
{
    return setGpioMode(pinName, QStringLiteral("out"), high);
}

bool BoardBackend::setGpioMode(const QString &pinName, const QString &mode, bool high)
{
    const QString operation = QStringLiteral("gpio-apply");
    if (!operationAllowed(operation)) {
        setStatus(operationUnavailableReason(operation));
        return false;
    }
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pin"), pinName);
    parameters.insert(QStringLiteral("mode"), mode);
    parameters.insert(QStringLiteral("high"), high);
    if (m_boardProfile) {
        parameters.insert(QStringLiteral("gpioAliases"),
                          m_boardProfile->configuration().value(
                              QStringLiteral("gpioAliases")).toMap());
    }
    const BackendOperationResult result = gpioOperationWithCommand(operation, parameters);
    setStatus(result.status);
    return result.success;
}

QString BoardBackend::readGpioValue(const QString &pinName)
{
    const QString operation = QStringLiteral("gpio-read");
    if (!operationAllowed(operation)) {
        setStatus(operationUnavailableReason(operation));
        return QStringLiteral("Unavailable");
    }
    QVariantMap parameters;
    parameters.insert(QStringLiteral("pin"), pinName);
    if (m_boardProfile) {
        parameters.insert(QStringLiteral("gpioAliases"),
                          m_boardProfile->configuration().value(
                              QStringLiteral("gpioAliases")).toMap());
    }
    const BackendOperationResult result = gpioOperationWithCommand(operation, parameters);
    setStatus(result.status);
    return result.result;
}

bool BoardBackend::sendSerialTestFrame(const QString &device, int baudRate)
{
    return sendSerialTestFrameConfigured(device, baudRate, 8, QStringLiteral("None"), 1, QStringLiteral("None"));
}

bool BoardBackend::sendSerialTestFrameConfigured(const QString &device, int baudRate, int dataBits, const QString &parity, int stopBits, const QString &flowControl)
{
    if (device.isEmpty() || device.startsWith(QStringLiteral("No serial"))) {
        setStatus(QStringLiteral("No serial device selected"));
        return false;
    }

    const QByteArray devicePath = device.toLocal8Bit();
    const int fd = ::open(devicePath.constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        setStatus(QStringLiteral("Serial open failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    QString error;
    if (!configureSerialPort(fd, baudRate, dataBits, parity, stopBits, flowControl, &error)) {
        ::close(fd);
        setStatus(QStringLiteral("Serial configure failed: %1").arg(error));
        return false;
    }

    const unsigned char frame[] = {0x55, 0xAA, 0x01, 0x00, 0xFE};
    const ssize_t written = ::write(fd, frame, sizeof(frame));
    ::tcdrain(fd);
    ::close(fd);

    if (written != static_cast<ssize_t>(sizeof(frame))) {
        setStatus(QStringLiteral("Serial write failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    setStatus(QStringLiteral("Serial TX %1 @%2 %3%4%5 flow=%6")
                  .arg(device)
                  .arg(baudRate)
                  .arg(dataBits)
                  .arg(parity.trimmed().isEmpty() || parity.trimmed().toLower() == QStringLiteral("none") ? QStringLiteral("N") : parity.trimmed().left(1).toUpper())
                  .arg(stopBits)
                  .arg(flowControl.trimmed().isEmpty() ? QStringLiteral("None") : flowControl.trimmed()));
    return true;
}

QString BoardBackend::readSerialSample(const QString &device, int baudRate)
{
    return readSerialSampleConfigured(device, baudRate, 8, QStringLiteral("None"), 1, QStringLiteral("None"));
}

QString BoardBackend::readSerialSampleConfigured(const QString &device, int baudRate, int dataBits, const QString &parity, int stopBits, const QString &flowControl)
{
    if (device.isEmpty() || device.startsWith(QStringLiteral("No serial"))) {
        setStatus(QStringLiteral("No serial device selected"));
        return QString();
    }

    const QByteArray devicePath = device.toLocal8Bit();
    const int fd = ::open(devicePath.constData(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        setStatus(QStringLiteral("Serial open failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return QString();
    }

    QString error;
    if (!configureSerialPort(fd, baudRate, dataBits, parity, stopBits, flowControl, &error)) {
        ::close(fd);
        setStatus(QStringLiteral("Serial configure failed: %1").arg(error));
        return QString();
    }

    QByteArray data;
    char buffer[128];
    for (int i = 0; i < 5; ++i) {
        const ssize_t bytes = ::read(fd, buffer, sizeof(buffer));
        if (bytes > 0) {
            data.append(buffer, bytes);
            break;
        }
        QThread::msleep(20);
    }
    ::close(fd);

    if (data.isEmpty()) {
        setStatus(QStringLiteral("Serial read: no data available"));
        return QStringLiteral("<no data>");
    }

    const QString text = QString::fromLocal8Bit(data.toHex(' ').toUpper());
    setStatus(QStringLiteral("Serial read %1 byte(s)").arg(data.size()));
    return text;
}

bool BoardBackend::playAudioTest()
{
    if (!m_audioProcessPids.isEmpty()) {
        stopAudioProcessesAsync();
    }

    refreshAudioDevices();
    const QString playbackDevice = resolveAudioDevice(QString(), m_audioPlaybackDevices);
    if (playbackDevice.isEmpty()) {
        setStatus(QStringLiteral("未发现可用 ALSA 播放设备；拒绝回退到系统默认输出"));
        return false;
    }
    const BackendOperationResult result = startAudioTestWithCommand(
        m_audioProcessPids,
        playbackDevice,
        m_boardProfile ? m_boardProfile->socFamily() : QString());
    m_audioProcessPids = result.audioPids;
    setStatus(result.status);
    return result.success;
}

bool BoardBackend::recordAudioSample()
{
    if (!m_audioProcessPids.isEmpty()) {
        stopAudioProcessesAsync();
    }

    refreshAudioDevices();
    const QString captureDevice = resolveAudioDevice(QString(), m_audioCaptureDevices);
    if (captureDevice.isEmpty()) {
        setStatus(QStringLiteral("未发现可用 ALSA 录音设备；拒绝回退到系统默认输入"));
        return false;
    }
    if (commandPath(QStringLiteral("arecord")).isEmpty()) {
        setStatus(QStringLiteral("arecord not found; install alsa-utils"));
        return false;
    }

    const QString outputPath = QStringLiteral("/tmp/boardlab-audio-test.wav");
    const QString arecordPath = commandPath(QStringLiteral("arecord"));
    qint64 pid = 0;
    if (startDetachedWithTimeout(arecordPath,
                                 QStringList() << QStringLiteral("-D") << captureDevice
                                               << QStringLiteral("-q")
                                               << QStringLiteral("-d") << QStringLiteral("2")
                                               << QStringLiteral("-f") << QStringLiteral("S16_LE")
                                               << QStringLiteral("-r") << QStringLiteral("48000")
                                               << QStringLiteral("-c") << QStringLiteral("2")
                                               << outputPath,
                                 5,
                                 &pid)) {
        if (pid > 0) {
            m_audioProcessPids.append(pid);
        }
        setStatus(QStringLiteral("Recording 2s sample on %1 with timeout: %2").arg(captureDevice, outputPath));
        return true;
    }

    setStatus(QStringLiteral("arecord start failed: %1").arg(outputPath));
    return false;
}

bool BoardBackend::playRecordedAudio()
{
    if (!m_audioProcessPids.isEmpty()) {
        stopAudioProcessesAsync();
    }

    refreshAudioDevices();
    const QString playbackDevice = resolveAudioDevice(QString(), m_audioPlaybackDevices);
    if (playbackDevice.isEmpty()) {
        setStatus(QStringLiteral("未发现可用 ALSA 播放设备；拒绝回退到系统默认输出"));
        return false;
    }
    if (commandPath(QStringLiteral("aplay")).isEmpty()) {
        setStatus(QStringLiteral("aplay not found; install alsa-utils"));
        return false;
    }

    const QString inputPath = QStringLiteral("/tmp/boardlab-audio-test.wav");
    if (!QFileInfo::exists(inputPath)) {
        setStatus(QStringLiteral("No recorded audio sample; record first"));
        return false;
    }

    const QString aplayPath = commandPath(QStringLiteral("aplay"));
    qint64 pid = 0;
    if (startDetachedWithTimeout(aplayPath,
                                 QStringList() << QStringLiteral("-D") << playbackDevice
                                               << QStringLiteral("-q") << inputPath,
                                 10,
                                 &pid)) {
        if (pid > 0) {
            m_audioProcessPids.append(pid);
        }
        setStatus(QStringLiteral("Playback started on %1 with timeout guard: %2").arg(playbackDevice, inputPath));
        return true;
    }

    setStatus(QStringLiteral("aplay start failed: %1").arg(inputPath));
    return false;
}

bool BoardBackend::stopAudioPlayback()
{
    if (m_audioProcessPids.isEmpty()) {
        setStatus(QStringLiteral("No tracked audio playback or recording process"));
        return false;
    }

    bool alreadyExited = false;
    for (const qint64 pid : qAsConst(m_audioProcessPids)) {
        if (!processIsAlive(pid)) {
            alreadyExited = true;
        }
    }
    stopAudioProcessesAsync();

    setStatus(alreadyExited ? QStringLiteral("Audio playback/recording already exited; cleanup queued")
                            : QStringLiteral("Audio playback/recording stop queued"));
    return true;
}

void BoardBackend::stopAudioProcessesAsync()
{
    if (m_audioProcessPids.isEmpty()) {
        return;
    }

    const QVector<qint64> pids = m_audioProcessPids;
    m_audioProcessPids.clear();
    static_cast<void>(QtConcurrent::run([pids]() {
        stopProcessList(pids, audioProcessKillTokens());
    }));
}

bool BoardBackend::setAudioVolumePercent(int percent)
{
    const int clamped = qBound(0, percent, 100);
    int exitCode = -1;

    if (!commandPath(QStringLiteral("pactl")).isEmpty()) {
        const QString output = runCommand(QStringLiteral("pactl"),
                                          QStringList() << QStringLiteral("set-sink-volume")
                                                        << QStringLiteral("@DEFAULT_SINK@")
                                                        << QStringLiteral("%1%").arg(clamped),
                                          5000,
                                          &exitCode);
        refreshAudioVolume();
        if (exitCode == 0) {
            m_audioVolumePercent = clamped;
            setStatus(QStringLiteral("Audio volume set to %1%").arg(clamped));
            return true;
        }
        setStatus(QStringLiteral("pactl volume failed: %1").arg(output.left(120)));
        return false;
    }

    if (!commandPath(QStringLiteral("amixer")).isEmpty()) {
        const QString output = runCommand(QStringLiteral("amixer"),
                                          QStringList() << QStringLiteral("-q")
                                                        << QStringLiteral("sset")
                                                        << QStringLiteral("Master")
                                                        << QStringLiteral("%1%").arg(clamped),
                                          5000,
                                          &exitCode);
        refreshAudioVolume();
        if (exitCode == 0) {
            m_audioVolumePercent = clamped;
            setStatus(QStringLiteral("ALSA Master volume set to %1%").arg(clamped));
            return true;
        }
        setStatus(QStringLiteral("amixer volume failed: %1").arg(output.left(120)));
        return false;
    }

    setStatus(QStringLiteral("No audio mixer found; install pulseaudio-utils or alsa-utils"));
    return false;
}

bool BoardBackend::setBeepPercent(int percent)
{
    refreshBuzzerAvailability();
    if (!m_buzzerAvailable) {
        setStatus(m_buzzerAvailabilityStatus);
        return false;
    }
    const BackendOperationResult result =
        setBeepPercentWithInputEvent(percent, m_buzzerInputEventDevice, m_buzzerToneWriteMutex);
    if (!result.success) {
        refreshBuzzerAvailability();
    }
    setStatus(result.status);
    return result.success;
}

bool BoardBackend::startBeepHeartbeat()
{
    refreshBuzzerAvailability();
    if (!m_buzzerAvailable) {
        setStatus(m_buzzerAvailabilityStatus);
        return false;
    }
    m_buzzerHeartbeatCancelled->store(false, std::memory_order_release);
    const BackendOperationResult result =
        startBeepHeartbeatWithInputEvent(m_buzzerInputEventDevice, m_buzzerHeartbeatCancelled,
                                         m_buzzerToneWriteMutex);
    if (!result.success) {
        refreshBuzzerAvailability();
    }
    setStatus(result.status);
    return result.success;
}

bool BoardBackend::stopBeep()
{
    m_beepProcessPids.clear();
    const BackendOperationResult result =
        stopBeepWithInputEvent(m_buzzerInputEventDevice, m_buzzerHeartbeatCancelled,
                               m_buzzerToneWriteMutex);
    if (!result.success) {
        refreshBuzzerAvailability();
    }
    setStatus(result.status);
    return result.success;
}

bool BoardBackend::runAudioOperationAsync(const QString &operation, int value,
                                          const QString &playbackDevice, const QString &captureDevice)
{
    const QString trimmedOperation = operation.trimmed();
    const QString asyncOperation = QStringLiteral("audio-%1").arg(trimmedOperation);
    if (!operationAllowed(asyncOperation)) {
        const QString status = operationUnavailableReason(asyncOperation);
        setStatus(status);
        emit operationFinished(asyncOperation, false, QString(), status);
        return false;
    }
    const bool buzzerOperation = trimmedOperation == QStringLiteral("beep-apply")
        || trimmedOperation == QStringLiteral("beep-heartbeat")
        || trimmedOperation == QStringLiteral("beep-stop");
    if (buzzerOperation) {
        refreshBuzzerAvailability();
        if (!m_buzzerAvailable) {
            setStatus(m_buzzerAvailabilityStatus);
            emit operationFinished(asyncOperation, false, QString(), m_buzzerAvailabilityStatus);
            return false;
        }
    }
    const bool needsPlayback = trimmedOperation == QStringLiteral("play-test")
        || trimmedOperation == QStringLiteral("play-recorded");
    const bool resolvesPlaybackDevice = needsPlayback || trimmedOperation == QStringLiteral("volume");
    const bool needsCapture = trimmedOperation == QStringLiteral("record");
    if ((resolvesPlaybackDevice && m_audioPlaybackDevices.isEmpty())
        || (needsCapture && m_audioCaptureDevices.isEmpty())) {
        refreshAudioDevices();
    }
    const QString selectedPlayback = resolvesPlaybackDevice
        ? resolveAudioDevice(playbackDevice, m_audioPlaybackDevices)
        : QString();
    const QString selectedCapture = needsCapture
        ? resolveAudioDevice(captureDevice, m_audioCaptureDevices)
        : QString();
    if (selectedPlayback == QStringLiteral("__invalid__") || selectedCapture == QStringLiteral("__invalid__")) {
        const QString status = QStringLiteral("Requested ALSA device is not in the current discovery list");
        setStatus(status);
        emit operationFinished(asyncOperation, false, QString(), status);
        return false;
    }
    if ((needsPlayback && selectedPlayback.isEmpty()) || (needsCapture && selectedCapture.isEmpty())) {
        const QString status = needsPlayback
            ? QStringLiteral("未发现可用 ALSA 播放设备；拒绝回退到系统默认输出")
            : QStringLiteral("未发现可用 ALSA 录音设备；拒绝回退到系统默认输入");
        setStatus(status);
        emit operationFinished(asyncOperation, false, QString(), status);
        return false;
    }
    if (trimmedOperation == QStringLiteral("volume")) {
        startAudioVolumeOperation(value, selectedPlayback);
        return true;
    }

    const QString buzzerInputEventDevice = m_buzzerInputEventDevice;
    const auto heartbeatCancelled = m_buzzerHeartbeatCancelled;
    const auto toneWriteMutex = m_buzzerToneWriteMutex;
    const QString audioSocFamily = m_boardProfile ? m_boardProfile->socFamily() : QString();
    if (trimmedOperation == QStringLiteral("beep-stop") && m_audioOperationBusy) {
        const BackendOperationResult result = stopBeepWithInputEvent(
            buzzerInputEventDevice, heartbeatCancelled, toneWriteMutex);
        if (!result.success) {
            refreshBuzzerAvailability();
        }
        setStatus(result.status);
        emit operationFinished(asyncOperation, result.success, result.result, result.status);
        return true;
    }
    if (trimmedOperation == QStringLiteral("stop-audio") || trimmedOperation == QStringLiteral("stop-all")) {
        const bool stopAll = trimmedOperation == QStringLiteral("stop-all");

        if (m_audioOperationBusy) {
            QVector<qint64> immediateAudioPids = m_audioProcessPids;
            m_audioProcessPids.clear();
            BackendOperationResult immediateBuzzerStop;
            if (stopAll) {
                m_beepProcessPids.clear();
                // Do not wait for the heartbeat worker or a PID bookkeeping
                // path: cancel and silence the evdev buzzer immediately.
                immediateBuzzerStop = stopBeepWithInputEvent(
                    buzzerInputEventDevice, heartbeatCancelled, toneWriteMutex);
            }

            m_audioStopPending = true;
            m_pendingAudioStopAll = m_pendingAudioStopAll || stopAll;
            setStatus(stopAll ? QStringLiteral("Stop all audio queued")
                              : QStringLiteral("Stop audio queued"));
            if (!immediateAudioPids.isEmpty()) {
                auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
                connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, asyncOperation]() {
                    const BackendOperationResult result = watcher->result();
                    const bool statusChanged = m_statusText != result.status;
                    setStatus(result.status);
                    if (!statusChanged) {
                        emit changed();
                    }
                    emit operationFinished(asyncOperation, result.success, result.result, result.status);
                    watcher->deleteLater();
                });
                watcher->setFuture(QtConcurrent::run(audioThreadPool(), [immediateAudioPids, stopAll, heartbeatCancelled, toneWriteMutex, buzzerInputEventDevice]() {
                    return stopLateAudioProcessesWithCommand(immediateAudioPids, stopAll, heartbeatCancelled,
                                                             toneWriteMutex, buzzerInputEventDevice);
                }));
            } else {
                const bool success = !stopAll || immediateBuzzerStop.success;
                const QString status = stopAll ? immediateBuzzerStop.status : m_statusText;
                setStatus(status);
                emit operationFinished(asyncOperation, success, QString(), status);
            }
            return true;
        }

        if (m_backendBusy) {
            QVector<qint64> audioPids = m_audioProcessPids;
            m_audioProcessPids.clear();
            if (stopAll) {
                m_beepProcessPids.clear();
                stopBeepWithInputEvent(buzzerInputEventDevice, heartbeatCancelled, toneWriteMutex);
            }

            auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
            connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, asyncOperation]() {
                const BackendOperationResult result = watcher->result();
                const bool statusChanged = m_statusText != result.status;
                setStatus(result.status);
                if (!statusChanged) {
                    emit changed();
                }
                emit operationFinished(asyncOperation, result.success, result.result, result.status);
                watcher->deleteLater();
            });
            watcher->setFuture(QtConcurrent::run(audioThreadPool(), [trimmedOperation, value, selectedPlayback, selectedCapture, audioSocFamily, audioPids, heartbeatCancelled, toneWriteMutex, buzzerInputEventDevice]() {
                return runAudioOperationWithCommand(trimmedOperation, value, selectedPlayback, selectedCapture, audioSocFamily, audioPids,
                                                    heartbeatCancelled, toneWriteMutex, buzzerInputEventDevice);
            }));
            return true;
        }
    }

    if (m_audioOperationBusy) {
        const QString status = QStringLiteral("Audio backend busy");
        setStatus(status);
        emit operationFinished(asyncOperation, false, QString(), status);
        return false;
    }
    if (trimmedOperation == QStringLiteral("beep-heartbeat")) {
        heartbeatCancelled->store(false, std::memory_order_release);
    }
    m_audioOperationBusy = true;
    emit operationStateChanged();

    QVector<qint64> audioPids;
    if (trimmedOperation == QStringLiteral("play-test")
        || trimmedOperation == QStringLiteral("record")
        || trimmedOperation == QStringLiteral("play-recorded")
        || trimmedOperation == QStringLiteral("stop-audio")
        || trimmedOperation == QStringLiteral("stop-all")) {
        audioPids = m_audioProcessPids;
        m_audioProcessPids.clear();
    }
    if (trimmedOperation == QStringLiteral("beep-heartbeat")
        || trimmedOperation == QStringLiteral("beep-stop")
        || trimmedOperation == QStringLiteral("stop-all")) {
        m_beepProcessPids.clear();
    }

    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, asyncOperation, trimmedOperation, buzzerInputEventDevice, heartbeatCancelled, toneWriteMutex]() {
        const BackendOperationResult result = watcher->result();
        for (const qint64 pid : result.audioPids) {
            if (pid > 0) {
                m_audioProcessPids.append(pid);
            }
        }
        for (const qint64 pid : result.beepPids) {
            if (pid > 0) {
                m_beepProcessPids.append(pid);
            }
        }
        if (result.audioVolumePercent >= 0) {
            m_audioVolumePercent = qBound(0, result.audioVolumePercent, 100);
        }
        if ((trimmedOperation.startsWith(QStringLiteral("beep-"))
             || trimmedOperation == QStringLiteral("stop-all"))
            && !result.success) {
            refreshBuzzerAvailability();
        }
        const bool statusChanged = m_statusText != result.status;
        setStatus(result.status);
        if (result.audioVolumePercent >= 0 && !statusChanged) {
            emit changed();
        }
        m_audioOperationBusy = false;
        emit operationStateChanged();
        emit operationFinished(asyncOperation, result.success, result.result, result.status);

        if (m_audioStopPending) {
            const bool stopAll = m_pendingAudioStopAll;
            QVector<qint64> lateAudioPids = m_audioProcessPids;
            m_audioProcessPids.clear();
            if (stopAll) {
                m_beepProcessPids.clear();
            }
            m_audioStopPending = false;
            m_pendingAudioStopAll = false;

            const QString stopOperation = stopAll ? QStringLiteral("audio-stop-all") : QStringLiteral("audio-stop-audio");
            auto *stopWatcher = new QFutureWatcher<BackendOperationResult>(this);
            connect(stopWatcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, stopWatcher, stopOperation]() {
                const BackendOperationResult stopResult = stopWatcher->result();
                const bool stopStatusChanged = m_statusText != stopResult.status;
                setStatus(stopResult.status);
                if (!stopStatusChanged) {
                    emit changed();
                }
                emit operationFinished(stopOperation, stopResult.success, stopResult.result, stopResult.status);
                if (m_pendingAudioVolumePercent >= 0) {
                    const int pendingVolume = m_pendingAudioVolumePercent;
                    const QString pendingPlayback = m_pendingAudioVolumePlaybackDevice;
                    m_pendingAudioVolumePercent = -1;
                    m_pendingAudioVolumePlaybackDevice.clear();
                    startAudioVolumeOperation(pendingVolume, pendingPlayback);
                }
                stopWatcher->deleteLater();
            });
            stopWatcher->setFuture(QtConcurrent::run(audioThreadPool(), [lateAudioPids, stopAll, heartbeatCancelled, toneWriteMutex, buzzerInputEventDevice]() {
                return stopLateAudioProcessesWithCommand(lateAudioPids, stopAll, heartbeatCancelled,
                                                         toneWriteMutex, buzzerInputEventDevice);
            }));
            watcher->deleteLater();
            return;
        }
        if (m_pendingAudioVolumePercent >= 0) {
            const int pendingVolume = m_pendingAudioVolumePercent;
            const QString pendingPlayback = m_pendingAudioVolumePlaybackDevice;
            m_pendingAudioVolumePercent = -1;
            m_pendingAudioVolumePlaybackDevice.clear();
            startAudioVolumeOperation(pendingVolume, pendingPlayback);
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(audioThreadPool(), [trimmedOperation, value, selectedPlayback, selectedCapture, audioSocFamily, audioPids, heartbeatCancelled, toneWriteMutex, buzzerInputEventDevice]() {
        return runAudioOperationWithCommand(trimmedOperation, value, selectedPlayback, selectedCapture, audioSocFamily, audioPids,
                                            heartbeatCancelled, toneWriteMutex, buzzerInputEventDevice);
    }));
    return true;
}

void BoardBackend::startAudioVolumeOperation(int percent, const QString &playbackDevice)
{
    const int clamped = qBound(0, percent, 100);
    if (m_audioOperationBusy) {
        m_pendingAudioVolumePercent = clamped;
        m_pendingAudioVolumePlaybackDevice = playbackDevice;
        return;
    }
    if (m_audioVolumeBusy) {
        m_pendingAudioVolumePercent = clamped;
        m_pendingAudioVolumePlaybackDevice = playbackDevice;
        return;
    }

    m_audioVolumeBusy = true;
    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher]() {
        const BackendOperationResult result = watcher->result();
        if (result.audioVolumePercent >= 0) {
            m_audioVolumePercent = qBound(0, result.audioVolumePercent, 100);
        }
        const bool statusChanged = m_statusText != result.status;
        setStatus(result.status);
        if (result.audioVolumePercent >= 0 && !statusChanged) {
            emit changed();
        }
        emit operationFinished(QStringLiteral("audio-volume"), result.success, result.result, result.status);
        watcher->deleteLater();

        m_audioVolumeBusy = false;
        if (m_pendingAudioVolumePercent >= 0) {
            const int pending = m_pendingAudioVolumePercent;
            const QString pendingPlayback = m_pendingAudioVolumePlaybackDevice;
            m_pendingAudioVolumePercent = -1;
            m_pendingAudioVolumePlaybackDevice.clear();
            startAudioVolumeOperation(pending, pendingPlayback);
        } else {
            m_pendingAudioVolumePercent = -1;
            m_pendingAudioVolumePlaybackDevice.clear();
        }
    });
    watcher->setFuture(QtConcurrent::run(audioThreadPool(), setAudioVolumeWithCommand, clamped, playbackDevice));
}

bool BoardBackend::runHardwareOperationAsync(const QString &operation, const QVariantMap &parameters)
{
    const QString trimmedOperation = operation.trimmed();
    if (!operationAllowed(trimmedOperation)) {
        const QString status = operationUnavailableReason(trimmedOperation);
        setStatus(status);
        emit operationFinished(trimmedOperation, false, QString(), status);
        return false;
    }

    QString cellularInterface;
    if (trimmedOperation == QStringLiteral("cellular-start")
        || trimmedOperation == QStringLiteral("cellular-stop")) {
        // A Quectel dialer can be the operation that creates its usb*/wwan*
        // data interface, so discovery must also accept a verified USB modem.
        // Refresh the passive inventory once for clicks made before the first
        // background scan has completed.
        refreshNetworkDevices();
        const QString requestedInterface = parameters.value(QStringLiteral("interface")).toString().trimmed();
        cellularInterface = requestedInterface.isEmpty() ? preferredCellularInterface() : requestedInterface;
        bool interfaceDiscovered = false;
        for (const QString &entry : m_cellularDevices) {
            if (cellularNetworkInterfaceFromDiscovery(entry) == cellularInterface) {
                interfaceDiscovered = true;
                break;
            }
        }

        const QVariantMap modemStatus = cellularModemStatus();
        const bool supportedModemDetected = modemStatus.value(QStringLiteral("detected")).toBool();
        const bool selectedDialerAvailable = modemStatus.value(QStringLiteral("dialerAvailable")).toBool();
        const bool trackedDialerRunning = modemStatus.value(QStringLiteral("dialerRunning")).toBool();
        const bool interfaceIsSafeExpectedDataPort = requestedInterface.isEmpty()
            || (isSafeNetworkInterfaceName(requestedInterface)
                && isCellularNetworkInterfaceName(requestedInterface));

        if (trimmedOperation == QStringLiteral("cellular-start")
            && supportedModemDetected
            && !selectedDialerAvailable) {
            const QString status = modemStatus.value(QStringLiteral("message")).toString().trimmed().isEmpty()
                ? QStringLiteral("已识别蜂窝模组，但匹配的拨号程序不可用")
                : modemStatus.value(QStringLiteral("message")).toString().trimmed();
            setStatus(status);
            emit operationFinished(trimmedOperation, false, QString(), status);
            return false;
        }

        const bool directDialerMayCreateInterface = trimmedOperation == QStringLiteral("cellular-start")
            && supportedModemDetected
            && selectedDialerAvailable
            && interfaceIsSafeExpectedDataPort;
        const bool trackedDialerMayStopWithoutInterface = trimmedOperation == QStringLiteral("cellular-stop")
            && trackedDialerRunning;
        if (!interfaceDiscovered && !directDialerMayCreateInterface && !trackedDialerMayStopWithoutInterface) {
            const QString status = m_cellularDevices.isEmpty()
                ? QStringLiteral("未发现可用蜂窝网络接口；请插入 4G/5G 模块并刷新设备状态")
                : QStringLiteral("蜂窝网络接口不在当前发现列表中：%1").arg(cellularInterface.isEmpty()
                    ? QStringLiteral("未选择") : cellularInterface);
            setStatus(status);
            emit operationFinished(trimmedOperation, false, QString(), status);
            return false;
        }
    }
    if (trimmedOperation == QStringLiteral("watchdog-start")
        && !parameters.value(QStringLiteral("confirm")).toBool()) {
        const QString status = QStringLiteral("watchdog-start requires confirm=true because this is an intentional board reset test");
        setStatus(status);
        emit operationFinished(trimmedOperation, false, QString(), status);
        return false;
    }
    const bool canBusControl = trimmedOperation == QStringLiteral("can-open")
        || trimmedOperation == QStringLiteral("can-close")
        || trimmedOperation == QStringLiteral("can-tx")
        || trimmedOperation == QStringLiteral("can-loop-start");
    if (canBusControl && !parameters.value(QStringLiteral("confirmCan")).toBool()) {
        const QString status = QStringLiteral("%1 requires confirmCan=true because it controls a real CAN bus")
            .arg(trimmedOperation);
        setStatus(status);
        emit operationFinished(trimmedOperation, false, QString(), status);
        return false;
    }
    if (trimmedOperation == QStringLiteral("can-open")
        && parameters.value(QStringLiteral("fdMode")).toBool()) {
        const QString status = QStringLiteral("当前平台参考帧测试不支持 CAN FD；请使用经典 CAN");
        setStatus(status);
        emit operationFinished(trimmedOperation, false, QString(), status);
        return false;
    }
    if (trimmedOperation.startsWith(QStringLiteral("led-"))) {
        refreshLedDevices();
        if (!ledAvailable()) {
            const QString status = QStringLiteral("未发现可写的板载 LED 控制节点；请检查设备树、驱动和权限");
            setStatus(status);
            emit operationFinished(trimmedOperation, false, QString(), status);
            return false;
        }
    }
    if (!beginAsyncOperation(trimmedOperation)) {
        return false;
    }

    QVariantMap workerParameters = parameters;
    if (trimmedOperation.startsWith(QStringLiteral("gpio-")) && m_boardProfile) {
        workerParameters.insert(QStringLiteral("gpioAliases"),
                                m_boardProfile->configuration().value(
                                    QStringLiteral("gpioAliases")).toMap());
    }
    if (trimmedOperation.startsWith(QStringLiteral("led-"))) {
        // The profile whitelist is authoritative. Some product images expose
        // work1/work2 while others expose user1/user2, so discard only aliases
        // that do not exist on this running image. Keep an existing but
        // read-only path in the list: the worker can then preflight every LED
        // and fail before changing another one. Remote callers still cannot
        // replace this list with arbitrary sysfs paths.
        QStringList existingProfileLedPaths;
        if (m_boardProfile) {
            for (const QString &configuredPath : m_boardProfile->ledPaths()) {
                const QString path = QDir::cleanPath(configuredPath.trimmed());
                if (!path.isEmpty()
                    && QFileInfo::exists(path + QStringLiteral("/brightness"))
                    && QFileInfo::exists(path + QStringLiteral("/max_brightness"))) {
                    existingProfileLedPaths.append(path);
                }
            }
        }
        workerParameters.insert(QStringLiteral("allowedPaths"), existingProfileLedPaths);
    }
    if (trimmedOperation == QStringLiteral("cellular-start") || trimmedOperation == QStringLiteral("cellular-stop")) {
        workerParameters.insert(QStringLiteral("preferredInterface"), cellularInterface);
    }
    if (trimmedOperation == QStringLiteral("cellular-start")
        || trimmedOperation == QStringLiteral("cellular-stop")
        || trimmedOperation == QStringLiteral("cellular-status")) {
        workerParameters.insert(QStringLiteral("devices"), m_cellularDevices);
        workerParameters.insert(QStringLiteral("socFamily"), m_boardProfile ? m_boardProfile->socFamily() : QString());
        // These values are backend-owned. The HTTP and QML layers must never
        // choose a binary, path, model, or an arbitrary PID to terminate.
        workerParameters.insert(QStringLiteral("ownedDialerPid"), m_cellularDialerPid);
        workerParameters.insert(QStringLiteral("ownedDialerPath"), m_cellularDialerPath);
    }
    if (trimmedOperation == QStringLiteral("watchdog-status") || trimmedOperation == QStringLiteral("watchdog-start")) {
        workerParameters.insert(QStringLiteral("devices"), m_watchdogDevices);
    }
    if (trimmedOperation == QStringLiteral("camera-start")) {
        // Device discovery belongs to the backend.  A client supplied
        // allowlist would let an API caller approve an arbitrary /dev/video*
        // node, defeating the runtime discovery gate.
        workerParameters.insert(QStringLiteral("allowedDevices"), m_cameraVideoDevices);
    }
    if (trimmedOperation == QStringLiteral("camera-start") && !workerParameters.contains(QStringLiteral("mode"))) {
        workerParameters.insert(QStringLiteral("mode"), QStringLiteral("mipi"));
    }
    if (trimmedOperation == QStringLiteral("hdmiin-start")) {
        // Keep HDMI input capture on the same server-owned discovery boundary
        // as camera capture; never trust a request-provided allowlist.
        workerParameters.insert(QStringLiteral("allowedDevices"), m_hdmiInVideoDevices);
    }
    if (trimmedOperation == QStringLiteral("hdmiin-start")) {
        workerParameters.insert(QStringLiteral("mode"), QStringLiteral("hdmiin"));
    }
    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, trimmedOperation]() {
        const BackendOperationResult result = watcher->result();
        bool backendDataChanged = false;
        if (result.cellularModemStatusValid && m_cellularModemStatus != result.cellularModemStatus) {
            m_cellularModemStatus = result.cellularModemStatus;
            backendDataChanged = true;
        }
        if (result.clearCellularDialerPid && (m_cellularDialerPid != 0 || !m_cellularDialerPath.isEmpty())) {
            m_cellularDialerPid = 0;
            m_cellularDialerPath.clear();
            backendDataChanged = true;
        }
        if (result.cellularDialerPidValid
            && (m_cellularDialerPid != result.cellularDialerPid
                || m_cellularDialerPath != result.cellularDialerPath)) {
            m_cellularDialerPid = result.cellularDialerPid;
            m_cellularDialerPath = result.cellularDialerPath;
            backendDataChanged = true;
        }
        if (result.refreshNetworkDevices) {
            refreshNetworkDevices();
            backendDataChanged = true;
        }
        if (result.backlightValid) {
            if (m_backlight.name != result.backlightName
                || m_backlight.path != result.backlightPath
                || m_backlight.maxBrightness != result.backlightMaxBrightness
                || m_backlight.brightness != result.backlightBrightness) {
                backendDataChanged = true;
            }
            m_backlight.name = result.backlightName;
            m_backlight.path = result.backlightPath;
            m_backlight.maxBrightness = result.backlightMaxBrightness;
            m_backlight.brightness = result.backlightBrightness;
        }
        if (trimmedOperation.startsWith(QStringLiteral("led-"))) {
            refreshLedDevices();
            backendDataChanged = true;
        }
        const bool statusChanged = m_statusText != result.status;
        setStatus(result.status);
        if (backendDataChanged && !statusChanged) {
            emit changed();
        }
        finishAsyncOperation(trimmedOperation, result.success, result.result, result.status);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(runHardwareOperationWithCommand, trimmedOperation, workerParameters));
    return true;
}

void BoardBackend::startFileBrowseAsync(const QString &operation, const QString &path)
{
    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, operation]() {
        const BackendOperationResult result = watcher->result();
        bool dataChanged = false;
        if (result.fileEntriesValid) {
            dataChanged = m_currentPath != result.currentPath || m_fileEntries != result.fileEntries;
            m_currentPath = result.currentPath;
            m_fileEntries = result.fileEntries;
        }
        const bool statusChanged = m_statusText != result.status;
        setStatus(result.status);
        if (dataChanged && !statusChanged) {
            emit changed();
        }
        emit operationFinished(operation, result.success, result.result, result.status);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(scanFileEntriesWithCommand, path));
}

bool BoardBackend::openPath(const QString &path)
{
    QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        setStatus(QStringLiteral("Not a directory: %1").arg(path));
        return false;
    }
    m_currentPath = info.absoluteFilePath();
    refreshFileEntries();
    setStatus(QStringLiteral("Opened %1").arg(m_currentPath));
    return true;
}

bool BoardBackend::goUp()
{
    QDir dir(m_currentPath);
    if (!dir.cdUp()) {
        setStatus(QStringLiteral("Already at filesystem root"));
        return false;
    }
    m_currentPath = dir.absolutePath();
    refreshFileEntries();
    setStatus(QStringLiteral("Opened %1").arg(m_currentPath));
    return true;
}

void BoardBackend::openPathAsync(const QString &path)
{
    setStatus(QStringLiteral("Opening %1").arg(path.trimmed().isEmpty() ? QDir::homePath() : path.trimmed()));
    startFileBrowseAsync(QStringLiteral("files-open"), path);
}

void BoardBackend::goUpAsync()
{
    QDir dir(m_currentPath.trimmed().isEmpty() ? QDir::homePath() : m_currentPath);
    if (!dir.cdUp()) {
        const QString status = QStringLiteral("Already at filesystem root");
        setStatus(status);
        emit operationFinished(QStringLiteral("files-up"), false, m_currentPath, status);
        return;
    }

    const QString targetPath = dir.absolutePath();
    setStatus(QStringLiteral("Opening %1").arg(targetPath));
    startFileBrowseAsync(QStringLiteral("files-up"), targetPath);
}

QString BoardBackend::gpioPreview(const QString &pinName, bool high) const
{
    const QVariantMap aliases = m_boardProfile
        ? m_boardProfile->configuration().value(QStringLiteral("gpioAliases")).toMap()
        : QVariantMap();
    const GpioLineReference reference = gpioLineFromNameForHelper(pinName, aliases);
    if (!reference.isValid()) {
        return QStringLiteral("Unsupported GPIO selector");
    }
    if (reference.usesCharacterDevice()) {
        return QStringLiteral("%1 -> %2:%3 -> %4")
            .arg(pinName, reference.chipPath)
            .arg(reference.lineOffset)
            .arg(high ? QStringLiteral("HIGH") : QStringLiteral("LOW"));
    }
    return QStringLiteral("%1 -> gpio%2 (legacy sysfs) -> %3")
        .arg(pinName).arg(reference.legacyNumber)
        .arg(high ? QStringLiteral("HIGH") : QStringLiteral("LOW"));
}

bool BoardBackend::configureCanInterface(const QString &interfaceName, int bitrate, int dataBitrate, bool fdMode)
{
    Q_UNUSED(dataBitrate)
    const QString iface = interfaceName.trimmed();
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No CAN"))) {
        setStatus(QStringLiteral("No CAN interface selected"));
        return false;
    }
    if (fdMode) {
        setStatus(QStringLiteral("当前平台参考帧测试不支持 CAN FD；请使用经典 CAN"));
        return false;
    }
    if (commandPath(QStringLiteral("ip")).isEmpty()) {
        setStatus(QStringLiteral("ip command not found; install iproute2"));
        return false;
    }

    const int clampedBitrate = qBound(50000, bitrate, 1000000);
    int exitCode = -1;
    runCommand(QStringLiteral("ip"), QStringList() << QStringLiteral("link") << QStringLiteral("set") << iface << QStringLiteral("down"), 3000, &exitCode);

    QStringList configureArgs;
    configureArgs << QStringLiteral("link") << QStringLiteral("set") << iface << QStringLiteral("type") << QStringLiteral("can")
                  << QStringLiteral("bitrate") << QString::number(clampedBitrate);
    const QString configureOutput = runCommand(QStringLiteral("ip"), configureArgs, 5000, &exitCode);
    if (exitCode != 0) {
        setStatus(QStringLiteral("CAN configure failed: %1").arg(configureOutput.left(120)));
        return false;
    }

    const QString upOutput = runCommand(QStringLiteral("ip"),
                                        QStringList() << QStringLiteral("link") << QStringLiteral("set") << iface << QStringLiteral("up"),
                                        5000,
                                        &exitCode);
    refreshNetworkDevices();
    if (exitCode != 0) {
        setStatus(QStringLiteral("CAN up failed: %1").arg(upOutput.left(120)));
        return false;
    }

    setStatus(QStringLiteral("%1 opened at %2 bps Classic CAN").arg(iface).arg(clampedBitrate));
    return true;
}

bool BoardBackend::closeCanInterface(const QString &interfaceName)
{
    const QString iface = interfaceName.trimmed();
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No CAN"))) {
        setStatus(QStringLiteral("No CAN interface selected"));
        return false;
    }
    if (commandPath(QStringLiteral("ip")).isEmpty()) {
        setStatus(QStringLiteral("ip command not found; install iproute2"));
        return false;
    }

    int exitCode = -1;
    const QString output = runCommand(QStringLiteral("ip"),
                                      QStringList() << QStringLiteral("link") << QStringLiteral("set") << iface << QStringLiteral("down"),
                                      5000,
                                      &exitCode);
    refreshNetworkDevices();
    if (exitCode == 0) {
        setStatus(QStringLiteral("%1 closed").arg(iface));
        return true;
    }

    setStatus(QStringLiteral("CAN close failed: %1").arg(output.left(120)));
    return false;
}

bool BoardBackend::sendCanTestFrame(const QString &interfaceName)
{
    const QString iface = interfaceName.trimmed();
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No CAN"))) {
        setStatus(QStringLiteral("No CAN interface selected"));
        return false;
    }

    QString errorText;
    const int socketFd = openCanSocket(iface, &errorText);
    if (socketFd < 0) {
        setStatus(QStringLiteral("CAN socket open failed: %1").arg(errorText));
        return false;
    }

    QString writeError;
    const bool sentAll = writeReferenceCanFrame(socketFd, &writeError);
    ::close(socketFd);
    if (sentAll) {
        setStatus(QStringLiteral("CAN TX %1 Classic frame: std 0x11 [01 02]").arg(iface));
        return true;
    }

    setStatus(QStringLiteral("CAN TX failed: %1").arg(writeError));
    return false;
}

bool BoardBackend::startCanReferenceSender(const QString &interfaceName, int bitrate)
{
    const BackendOperationResult result = startCanLoopSender(interfaceName, qBound(50000, bitrate, 1000000));
    setStatus(result.status);
    return result.success;
}

bool BoardBackend::stopCanReferenceSender()
{
    QString status;
    const bool stopped = stopCanLoopSender(&status);
    setStatus(status);
    return stopped;
}

QString BoardBackend::readCanSample(const QString &interfaceName)
{
    const QString iface = interfaceName.trimmed();
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No CAN"))) {
        setStatus(QStringLiteral("No CAN interface selected"));
        return QString();
    }

    QString errorText;
    const int socketFd = openCanSocket(iface, &errorText);
    if (socketFd < 0) {
        setStatus(QStringLiteral("CAN socket open failed: %1").arg(errorText));
        return QString();
    }

    QStringList lines;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 1000;
    while (lines.size() < 5 && QDateTime::currentMSecsSinceEpoch() < deadline) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socketFd, &readSet);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 120000;

        const int ready = ::select(socketFd + 1, &readSet, nullptr, nullptr, &timeout);
        if (ready > 0 && FD_ISSET(socketFd, &readSet)) {
            can_frame frame;
            std::memset(&frame, 0, sizeof(frame));
            const ssize_t readBytes = ::read(socketFd, &frame, sizeof(frame));
            if (readBytes == static_cast<ssize_t>(sizeof(frame))) {
                lines << QStringLiteral("%1 %2").arg(iface, formatCanFrame(frame));
            }
        } else if (ready < 0) {
            const QString error = QString::fromLocal8Bit(std::strerror(errno));
            ::close(socketFd);
            setStatus(QStringLiteral("CAN RX failed: %1").arg(error));
            return QString();
        }
    }
    ::close(socketFd);

    if (!lines.isEmpty()) {
        setStatus(QStringLiteral("CAN RX sample captured on %1").arg(iface));
        return lines.join(QChar('\n'));
    }

    setStatus(QStringLiteral("CAN RX: no frames"));
    return QStringLiteral("<no CAN frames>");
}

bool BoardBackend::renewDhcp(const QString &interfaceName)
{
    const QString iface = interfaceName.trimmed();
    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No Ethernet"))) {
        setStatus(QStringLiteral("No Ethernet interface selected"));
        return false;
    }
    if (!isEthernetInterfaceName(iface) || !isExistingNetworkInterface(iface)) {
        setStatus(QStringLiteral("Ethernet interface is unavailable or invalid: %1").arg(iface.left(64)));
        return false;
    }
    if (commandPath(QStringLiteral("nmcli")).isEmpty()) {
        setStatus(QStringLiteral("nmcli not found; install NetworkManager"));
        return false;
    }

    QString connection = nmcliConnectionNameForDevice(iface);
    QString bootstrapOutput;
    if (connection.isEmpty()) {
        int connectExit = -1;
        bootstrapOutput = runCommand(QStringLiteral("nmcli"),
                                     QStringList() << QStringLiteral("-w") << QStringLiteral("12")
                                                   << QStringLiteral("device") << QStringLiteral("connect") << iface,
                                     12000,
                                     &connectExit);
        connection = nmcliConnectionNameForDevice(iface);
    }
    if (connection.isEmpty()) {
        const QString profileName = QStringLiteral("rk-shell-%1").arg(iface);
        int addExit = -1;
        const QString addOutput = runCommand(QStringLiteral("nmcli"),
                                             QStringList() << QStringLiteral("connection") << QStringLiteral("add")
                                                           << QStringLiteral("type") << QStringLiteral("ethernet")
                                                           << QStringLiteral("ifname") << iface
                                                           << QStringLiteral("con-name") << profileName,
                                             8000,
                                             &addExit);
        if (!addOutput.trimmed().isEmpty()) {
            bootstrapOutput = bootstrapOutput.isEmpty() ? addOutput : bootstrapOutput + QStringLiteral("; ") + addOutput;
        }
        connection = nmcliConnectionNameForDevice(iface);
        if (connection.isEmpty() && addExit == 0) {
            connection = profileName;
        }
    }
    if (connection.isEmpty()) {
        setStatus(QStringLiteral("No NetworkManager profile for %1: %2").arg(iface, compactCommandOutput(bootstrapOutput)));
        return false;
    }

    int exitCode = -1;
    QString output = runCommand(QStringLiteral("nmcli"),
                                QStringList() << QStringLiteral("connection") << QStringLiteral("modify") << connection
                                              << QStringLiteral("ipv4.method") << QStringLiteral("auto")
                                              << QStringLiteral("ipv4.addresses") << QString()
                                              << QStringLiteral("ipv4.gateway") << QString()
                                              << QStringLiteral("ipv4.dns") << QString()
                                              << QStringLiteral("ipv4.ignore-auto-dns") << QStringLiteral("no"),
                                8000,
                                &exitCode);
    if (exitCode != 0) {
        setStatus(QStringLiteral("DHCP profile update failed: %1").arg(output.left(120)));
        return false;
    }

    output = runCommand(QStringLiteral("nmcli"),
                        QStringList() << QStringLiteral("-w") << QStringLiteral("20")
                                      << QStringLiteral("connection") << QStringLiteral("up") << connection
                                      << QStringLiteral("ifname") << iface,
                        20000,
                        &exitCode);
    refreshNetworkDevices();
    if (exitCode == 0) {
        setStatus(QStringLiteral("DHCP applied on %1 via %2").arg(iface, connection));
        return true;
    }

    setStatus(QStringLiteral("DHCP request failed: %1").arg(output.left(120)));
    return false;
}

bool BoardBackend::setStaticIp(const QString &interfaceName, const QString &address, const QString &netmask, const QString &gateway)
{
    const QString iface = interfaceName.trimmed();
    const QString ipAddress = address.trimmed();
    const QString mask = netmask.trimmed().isEmpty() ? QStringLiteral("255.255.255.0") : netmask.trimmed();
    const QString gw = gateway.trimmed();

    if (iface.isEmpty() || iface.startsWith(QStringLiteral("No Ethernet"))) {
        setStatus(QStringLiteral("No Ethernet interface selected"));
        return false;
    }
    if (ipAddress.isEmpty()) {
        setStatus(QStringLiteral("Static IP address is empty"));
        return false;
    }
    if (!isEthernetInterfaceName(iface) || !isExistingNetworkInterface(iface)) {
        setStatus(QStringLiteral("Ethernet interface is unavailable or invalid: %1").arg(iface.left(64)));
        return false;
    }
    if (!isValidIpv4AddressForHelper(ipAddress)) {
        setStatus(QStringLiteral("Invalid static IPv4 address: %1").arg(ipAddress.left(64)));
        return false;
    }
    if (!isOptionalIpv4AddressForHelper(gw)) {
        setStatus(QStringLiteral("Invalid IPv4 gateway: %1").arg(gw.left(64)));
        return false;
    }
    if (commandPath(QStringLiteral("nmcli")).isEmpty()) {
        setStatus(QStringLiteral("nmcli not found; install NetworkManager"));
        return false;
    }

    const int prefix = netmaskPrefixLength(mask);
    if (prefix < 0) {
        setStatus(QStringLiteral("Invalid IPv4 netmask or prefix: %1").arg(mask));
        return false;
    }

    QString connection = nmcliConnectionNameForDevice(iface);
    QString bootstrapOutput;
    if (connection.isEmpty()) {
        const QString profileName = QStringLiteral("rk-shell-%1").arg(iface);
        int addExit = -1;
        bootstrapOutput = runCommand(QStringLiteral("nmcli"),
                                     QStringList() << QStringLiteral("connection") << QStringLiteral("add")
                                                   << QStringLiteral("type") << QStringLiteral("ethernet")
                                                   << QStringLiteral("ifname") << iface
                                                   << QStringLiteral("con-name") << profileName,
                                     8000,
                                     &addExit);
        connection = nmcliConnectionNameForDevice(iface);
        if (connection.isEmpty() && addExit == 0) {
            connection = profileName;
        }
    }
    if (connection.isEmpty()) {
        setStatus(QStringLiteral("No NetworkManager profile for %1: %2").arg(iface, compactCommandOutput(bootstrapOutput)));
        return false;
    }

    int exitCode = -1;
    QStringList modifyArgs;
    modifyArgs << QStringLiteral("connection") << QStringLiteral("modify") << connection
               << QStringLiteral("ipv4.method") << QStringLiteral("manual")
               << QStringLiteral("ipv4.addresses") << QStringLiteral("%1/%2").arg(ipAddress).arg(prefix)
               << QStringLiteral("ipv4.gateway") << gw
               << QStringLiteral("ipv4.dns") << QString();
    const QString ipOutput = runCommand(QStringLiteral("nmcli"), modifyArgs, 8000, &exitCode);
    if (exitCode != 0) {
        setStatus(QStringLiteral("Static IP failed: %1").arg(ipOutput.left(120)));
        return false;
    }

    const QString upOutput = runCommand(QStringLiteral("nmcli"),
                                        QStringList() << QStringLiteral("-w") << QStringLiteral("20")
                                                      << QStringLiteral("connection") << QStringLiteral("up") << connection
                                                      << QStringLiteral("ifname") << iface,
                                        20000,
                                        &exitCode);
    if (exitCode != 0) {
        setStatus(QStringLiteral("Static IP apply failed: %1").arg(upOutput.left(120)));
        return false;
    }

    refreshNetworkDevices();
    setStatus(QStringLiteral("%1 static IP set to %2/%3 via %4").arg(iface, ipAddress).arg(prefix).arg(connection));
    return true;
}

QString BoardBackend::pingInterface(const QString &interfaceName, const QString &target)
{
    const QString operation = QStringLiteral("ping-interface");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        return status;
    }
    const BackendOperationResult result = pingInterfaceWithCommand(interfaceName, target);
    setStatus(result.status);
    return result.result;
}

bool BoardBackend::pingInterfaceAsync(const QString &interfaceName, const QString &target)
{
    const QString operation = QStringLiteral("ping-interface");
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    const QString iface = interfaceName.trimmed();
    const QString host = target.trimmed().isEmpty()
        ? QString::fromLatin1(kDefaultPingTarget)
        : target.trimmed();
    if (!isSafeNetworkInterfaceName(iface) || !isExistingNetworkInterface(iface)) {
        const QString status = QStringLiteral("Network interface is unavailable or invalid: %1").arg(iface.left(64));
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!isSafePingTarget(host)) {
        const QString status = QStringLiteral("Invalid ping target. Use a hostname or IPv4 address.");
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (!beginAsyncOperation(operation)) {
        return false;
    }

    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, operation]() {
        const BackendOperationResult result = watcher->result();
        setStatus(result.status);
        finishAsyncOperation(operation, result.success, result.result, result.status);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(pingInterfaceWithCommand, iface, host));
    return true;
}

bool BoardBackend::startCellularConnection()
{
    return startCellularConnectionWithOptions(QString(), QString());
}

bool BoardBackend::startCellularConnectionWithOptions(const QString &apn, const QString &interfaceName)
{
    QVariantMap parameters;
    parameters.insert(QStringLiteral("apn"), apn.trimmed());
    parameters.insert(QStringLiteral("interface"), interfaceName.trimmed());
    parameters.insert(QStringLiteral("preferredInterface"), preferredCellularInterface());
    return runHardwareOperationAsync(QStringLiteral("cellular-start"), parameters);
}

QString BoardBackend::preferredCellularInterface() const
{
    const QString detectedInterface = m_cellularModemStatus.value(QStringLiteral("preferredInterface")).toString().trimmed();
    if (!detectedInterface.isEmpty() && isCellularNetworkInterfaceName(detectedInterface)) {
        return detectedInterface;
    }
    return preferredCellularInterfaceFromDiscovery(m_cellularDevices);
}

QString BoardBackend::cellularStatusReport() const
{
    QVariantMap parameters;
    parameters.insert(QStringLiteral("devices"), m_cellularDevices);
    parameters.insert(QStringLiteral("socFamily"), m_boardProfile ? m_boardProfile->socFamily() : QString());
    return cellularStatusReportWithCommand(parameters).result;
}

bool BoardBackend::stopCellularConnection()
{
    refreshNetworkDevices();
    QVariantMap parameters;
    parameters.insert(QStringLiteral("preferredInterface"), preferredCellularInterface());
    parameters.insert(QStringLiteral("devices"), m_cellularDevices);
    parameters.insert(QStringLiteral("socFamily"), m_boardProfile ? m_boardProfile->socFamily() : QString());
    parameters.insert(QStringLiteral("ownedDialerPid"), m_cellularDialerPid);
    parameters.insert(QStringLiteral("ownedDialerPath"), m_cellularDialerPath);
    const BackendOperationResult result = stopCellularWithCommand(parameters);
    if (result.cellularModemStatusValid) {
        m_cellularModemStatus = result.cellularModemStatus;
    }
    if (result.clearCellularDialerPid) {
        m_cellularDialerPid = 0;
        m_cellularDialerPath.clear();
    }
    refreshNetworkDevices();
    setStatus(result.status);
    return result.success;
}

bool BoardBackend::startCameraTest(const QString &mode, const QString &device)
{
    if (commandPath(QStringLiteral("v4l2-ctl")).isEmpty()) {
        setStatus(QStringLiteral("v4l2-ctl not found; install v4l-utils"));
        return false;
    }

    if (m_videoDevices.isEmpty() && m_cameraVideoDevice.isEmpty() && m_hdmiInVideoDevice.isEmpty()) {
        refreshVideoDevices();
    }

    const QString normalizedMode = mode.trimmed().isEmpty() ? QStringLiteral("mipi") : mode.trimmed().toLower();
    const bool hdmiMode = normalizedMode == QStringLiteral("hdmiin") || normalizedMode == QStringLiteral("hdmi");
    QString videoDevice = device.trimmed();
    if (videoDevice.isEmpty()) {
        videoDevice = hdmiMode ? m_hdmiInVideoDevice : m_cameraVideoDevice;
    }
    if (videoDevice.isEmpty()) {
        setStatus(hdmiMode
                      ? QStringLiteral("No HDMIIN video device detected")
                      : QStringLiteral("No camera video device detected"));
        return false;
    }
    if (!isVideoNodePath(videoDevice)) {
        setStatus(QStringLiteral("Invalid video device path: %1").arg(videoDevice));
        return false;
    }
    const QStringList expectedDevices = hdmiMode ? m_hdmiInVideoDevices : m_cameraVideoDevices;
    if (expectedDevices.isEmpty() && !device.trimmed().isEmpty()) {
        setStatus(QStringLiteral("No detected %1 capture list; refusing direct device override %2")
                      .arg(hdmiMode ? QStringLiteral("HDMIIN") : QStringLiteral("camera"), videoDevice));
        return false;
    }
    if (!expectedDevices.isEmpty() && !expectedDevices.contains(videoDevice)) {
        setStatus(QStringLiteral("%1 is not in the detected %2 capture list")
                      .arg(videoDevice, hdmiMode ? QStringLiteral("HDMIIN") : QStringLiteral("camera")));
        return false;
    }

    qint64 pid = 0;
    if (!startDetachedWithTimeout(QStringLiteral("v4l2-ctl"),
                                  QStringList() << QStringLiteral("-d") << videoDevice
                                                << QStringLiteral("--stream-mmap")
                                                << QStringLiteral("--stream-count=120")
                                                << QStringLiteral("--stream-to=/dev/null"),
                                  15,
                                  &pid)) {
        setStatus(QStringLiteral("V4L2 capture start failed: %1 %2").arg(normalizedMode, videoDevice));
        return false;
    }
    if (pid > 0 && !waitForProcessAlive(pid, 250)) {
        setStatus(QStringLiteral("V4L2 capture exited immediately: %1 %2").arg(normalizedMode, videoDevice));
        return false;
    }

    setStatus(QStringLiteral("V4L2 capture smoke started: %1 %2").arg(normalizedMode, videoDevice));
    return true;
}

bool BoardBackend::stopCameraTest()
{
    if (commandPath(QStringLiteral("pkill")).isEmpty()) {
        setStatus(QStringLiteral("pkill not found; cannot stop V4L2 capture smoke"));
        return false;
    }
    int exitCode = -1;
    const QString output = runCommand(QStringLiteral("pkill"),
                                      QStringList() << QStringLiteral("-f") << QStringLiteral("v4l2-ctl.*--stream-mmap"),
                                      3000,
                                      &exitCode);
    setStatus(exitCode == 0 ? QStringLiteral("V4L2 capture smoke stopped")
                            : QStringLiteral("V4L2 capture smoke stop requested: %1").arg(output.left(96)));
    return exitCode == 0;
}

QString BoardBackend::watchdogStatusReport() const
{
    QStringList lines;
    lines << QStringLiteral("Watchdog status");
    lines << QStringLiteral("Devices: %1").arg(m_watchdogDevices.isEmpty() ? QStringLiteral("none") : m_watchdogDevices.join(QStringLiteral(", ")));
    int exitCode = -1;
    if (!commandPath(QStringLiteral("wdctl")).isEmpty()) {
        lines << QString();
        lines << QStringLiteral("[wdctl]");
        lines << runCommand(QStringLiteral("wdctl"), QStringList(), 5000, &exitCode);
    } else {
        lines << QStringLiteral("wdctl: missing");
    }
    return lines.join(QChar('\n'));
}

bool BoardBackend::startWatchdogReferenceTest(const QString &device, int timeoutSeconds, bool confirmed)
{
    if (!confirmed) {
        setStatus(QStringLiteral("watchdog-start requires explicit confirmation because this is an intentional board reset test"));
        return false;
    }
    const BackendOperationResult result = startWatchdogTest(device, m_watchdogDevices, timeoutSeconds);
    setStatus(result.status);
    return result.success;
}

speed_t BoardBackend::serialSpeed(int baudRate)
{
    switch (baudRate) {
    case 4800:
        return B4800;
    case 9600:
        return B9600;
    case 19200:
        return B19200;
    case 38400:
        return B38400;
    case 57600:
        return B57600;
    case 230400:
#ifdef B230400
        return B230400;
#else
        return 0;
#endif
    case 460800:
#ifdef B460800
        return B460800;
#else
        return 0;
#endif
    case 921600:
#ifdef B921600
        return B921600;
#else
        return 0;
#endif
    case 115200:
    default:
        return B115200;
    }
}

bool BoardBackend::configureSerialPort(int fd, int baudRate, int dataBits, const QString &parity, int stopBits, const QString &flowControl, QString *errorText)
{
    termios options;
    if (::tcgetattr(fd, &options) != 0) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        return false;
    }

    ::cfmakeraw(&options);
    const speed_t speed = serialSpeed(baudRate);
    if (speed == 0) {
        if (errorText) {
            *errorText = QStringLiteral("unsupported baud rate on this platform: %1").arg(baudRate);
        }
        return false;
    }
    ::cfsetispeed(&options, speed);
    ::cfsetospeed(&options, speed);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~CSIZE;
    switch (qBound(5, dataBits, 8)) {
    case 5:
        options.c_cflag |= CS5;
        break;
    case 6:
        options.c_cflag |= CS6;
        break;
    case 7:
        options.c_cflag |= CS7;
        break;
    case 8:
    default:
        options.c_cflag |= CS8;
        break;
    }

    const QString normalizedParity = parity.trimmed().toLower();
    if (normalizedParity == QStringLiteral("even") || normalizedParity == QStringLiteral("e") || normalizedParity == QStringLiteral("偶校验")) {
        options.c_cflag |= PARENB;
        options.c_cflag &= ~PARODD;
    } else if (normalizedParity == QStringLiteral("odd") || normalizedParity == QStringLiteral("o") || normalizedParity == QStringLiteral("奇校验")) {
        options.c_cflag |= PARENB;
        options.c_cflag |= PARODD;
    } else {
        options.c_cflag &= ~PARENB;
        options.c_cflag &= ~PARODD;
    }

    if (stopBits >= 2) {
        options.c_cflag |= CSTOPB;
    } else {
        options.c_cflag &= ~CSTOPB;
    }

    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    const QString normalizedFlow = flowControl.trimmed().toLower();
    if (normalizedFlow == QStringLiteral("software") || normalizedFlow == QStringLiteral("xonxoff") || normalizedFlow == QStringLiteral("xon/xoff")) {
        options.c_iflag |= (IXON | IXOFF | IXANY);
    }
#ifdef CRTSCTS
    if (normalizedFlow == QStringLiteral("hardware") || normalizedFlow == QStringLiteral("rtscts") || normalizedFlow == QStringLiteral("rts/cts")) {
        options.c_cflag |= CRTSCTS;
    } else {
        options.c_cflag &= ~CRTSCTS;
    }
#endif
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 1;

    if (::tcsetattr(fd, TCSANOW, &options) != 0) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        return false;
    }
    return true;
}

int BoardBackend::gpioNumberFromName(const QString &pinName)
{
    const QString trimmed = pinName.trimmed();
    bool numeric = false;
    const int numericGpio = trimmed.toInt(&numeric);
    if (numeric && numericGpio >= 0) {
        return numericGpio;
    }

    static const QRegularExpression expression(QStringLiteral("^GPIO([0-9]+)_([A-D])([0-7])$"),
                                               QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = expression.match(trimmed);
    if (!match.hasMatch()) {
        return -1;
    }

    const int bank = match.captured(1).toInt();
    const QChar group = match.captured(2).toUpper().at(0);
    const int offset = match.captured(3).toInt();
    const int groupOffset = (group.unicode() - QChar('A').unicode()) * 8;
    return bank * 32 + groupOffset + offset;
}

QString BoardBackend::commandPath(const QString &command)
{
    const QString pathEnv = QString::fromLocal8Bit(qgetenv("PATH"));
    QStringList paths = pathEnv.split(QChar(':'), boardlab::kSkipEmptyParts);
    paths << QStringLiteral("/usr/local/sbin")
          << QStringLiteral("/usr/local/bin")
          << QStringLiteral("/usr/sbin")
          << QStringLiteral("/usr/bin")
          << QStringLiteral("/sbin")
          << QStringLiteral("/bin");
    paths.removeDuplicates();
    for (const QString &path : paths) {
        const QString candidate = QDir(path).filePath(command);
        if (QFileInfo(candidate).isExecutable()) {
            return candidate;
        }
    }
    return QString();
}

QString BoardBackend::runCommand(const QString &program, const QStringList &arguments, int timeoutMs, int *exitCode)
{
    QProcess process;
    const QString executable = program.contains(QChar('/')) ? program : commandPath(program);
    process.setProgram(executable.isEmpty() ? program : executable);
    process.setArguments(arguments);
    process.start();
    return runProcessResponsive(&process, timeoutMs, exitCode);
}

bool BoardBackend::startDetachedWithTimeout(const QString &program, const QStringList &arguments, int timeoutSeconds, qint64 *pid)
{
    const QString executable = program.contains(QChar('/')) ? program : commandPath(program);
    if (executable.isEmpty()) {
        return false;
    }

    QStringList wrapperArguments;
    if (!commandPath(QStringLiteral("timeout")).isEmpty() && timeoutSeconds > 0) {
        wrapperArguments << QStringLiteral("--kill-after=2s")
                         << QString::number(timeoutSeconds)
                         << executable
                         << arguments;
        return QProcess::startDetached(commandPath(QStringLiteral("timeout")), wrapperArguments, QString(), pid);
    }

    return QProcess::startDetached(executable, arguments, QString(), pid);
}

QString BoardBackend::readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

bool BoardBackend::writeTextFile(const QString &path, const QString &text, QString *errorText)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (errorText) {
            *errorText = file.errorString();
        }
        return false;
    }
    QTextStream stream(&file);
    stream << text;
    if (stream.status() != QTextStream::Ok) {
        if (errorText) {
            *errorText = QStringLiteral("write stream error");
        }
        return false;
    }
    return true;
}

BoardBackend::VideoDeviceInfo BoardBackend::inspectVideoDevice(const QString &path, const QString &groupName)
{
    VideoDeviceInfo info;
    info.path = path.trimmed();
    info.groupName = groupName.trimmed();
    if (!isVideoNodePath(info.path)) {
        return info;
    }

    const QString nodeName = QFileInfo(info.path).fileName();
    const QString videoClassRoot = QDir(boardlab::sysRoot()).filePath(
        QStringLiteral("class/video4linux"));
    info.sysfsName = readTextFile(QDir(videoClassRoot).filePath(
        nodeName + QStringLiteral("/name")));

    QStringList capabilityDetails;
    const QByteArray devicePath = info.path.toLocal8Bit();
    const int videoFd = ::open(devicePath.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (videoFd >= 0) {
        v4l2_capability capability;
        std::memset(&capability, 0, sizeof(capability));
        if (::ioctl(videoFd, VIDIOC_QUERYCAP, &capability) == 0) {
            const quint32 flags = (capability.capabilities & V4L2_CAP_DEVICE_CAPS)
                ? capability.device_caps : capability.capabilities;
            capabilityDetails.append(QStringLiteral("Driver: %1")
                .arg(QString::fromLocal8Bit(
                    reinterpret_cast<const char *>(capability.driver))));
            capabilityDetails.append(QStringLiteral("Card: %1")
                .arg(QString::fromLocal8Bit(
                    reinterpret_cast<const char *>(capability.card))));
            capabilityDetails.append(QStringLiteral("Bus: %1")
                .arg(QString::fromLocal8Bit(
                    reinterpret_cast<const char *>(capability.bus_info))));
            if (flags & (V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
                capabilityDetails.append(QStringLiteral("Video Capture"));
            }
            if (flags & (V4L2_CAP_VIDEO_M2M | V4L2_CAP_VIDEO_M2M_MPLANE)) {
                capabilityDetails.append(QStringLiteral("Memory-to-Memory"));
            }
            if (flags & V4L2_CAP_STREAMING) {
                capabilityDetails.append(QStringLiteral("Streaming"));
            }

            for (quint32 index = 0; index < 32; ++index) {
                v4l2_input input;
                std::memset(&input, 0, sizeof(input));
                input.index = index;
                if (::ioctl(videoFd, VIDIOC_ENUMINPUT, &input) != 0) {
                    break;
                }
                capabilityDetails.append(QStringLiteral("Input: %1")
                    .arg(QString::fromLocal8Bit(
                        reinterpret_cast<const char *>(input.name))));
            }
        }
        ::close(videoFd);
    }

    if (!commandPath(QStringLiteral("v4l2-ctl")).isEmpty()) {
        int exitCode = -1;
        const QString deviceCaps = runCommand(QStringLiteral("v4l2-ctl"),
                                              QStringList() << QStringLiteral("-D") << QStringLiteral("-d") << info.path,
                                              2000,
                                              &exitCode);
        const QString allCaps = runCommand(QStringLiteral("v4l2-ctl"),
                                           QStringList() << QStringLiteral("--all") << QStringLiteral("-d") << info.path,
                                           2500,
                                           &exitCode);
        capabilityDetails << deviceCaps << allCaps;
    }
    info.detailText = capabilityDetails.join(QChar('\n'));

    const QString nameText = (QStringList()
                                  << info.groupName
                                  << info.sysfsName)
                                 .join(QChar('\n'))
                                 .toLower();
    const QString combined = (QStringList()
                                  << nameText
                                  << info.detailText)
                                 .join(QChar('\n'))
                                 .toLower();

    const bool hasCapabilityReport = !info.detailText.trimmed().isEmpty();
    const bool captureCapability = hasVideoCaptureCapability(combined);
    // V4L2 node numbers are probe-order dependent.  A USB camera cannot be
    // identified from one fixed node number across RK3588, RK3568 or kernel
    // releases.  Classify it only from the discovered device/group name and
    // reported capabilities.
    const bool usbCameraName = containsAny(combined,
                                           QStringList()
                                               << QStringLiteral("uvc")
                                               << QStringLiteral("usb")
                                               << QStringLiteral("webcam"));
    const bool metadataNode = !usbCameraName
                              && containsAny(combined,
                                             QStringList()
                                                 << QStringLiteral("metadata")
                                                 << QStringLiteral("statistics")
                                                 << QStringLiteral("stats")
                                                 << QStringLiteral("params")
                                                 << QStringLiteral("rawrd")
                                                 << QStringLiteral("input-params"));
    const bool mem2memNode = isMemoryToMemoryVideoNode(combined);
    const bool hdmiName = containsAny(combined,
                                      QStringList()
                                          << QStringLiteral("rk_hdmirx")
                                          << QStringLiteral("hdmirx")
                                          << QStringLiteral("hdmi-rx")
                                          << QStringLiteral("hdmi rx")
                                          << QStringLiteral("input: hdmi"));
    const bool preferredCameraGroup = containsAny(nameText,
                                                   QStringList()
                                                       << QStringLiteral("rkisp_mainpath")
                                                       << QStringLiteral("mainpath")
                                                       << QStringLiteral("uvc")
                                                       << QStringLiteral("usb")
                                                       << QStringLiteral("webcam")
                                                       << QStringLiteral("camera"));
    const bool secondaryCameraGroup = containsAny(nameText,
                                                  QStringList()
                                                      << QStringLiteral("rkcif")
                                                      << QStringLiteral("cif")
                                                      << QStringLiteral("mipi")
                                                      << QStringLiteral("lvds"));
    const bool rejectedCameraGroup = containsAny(nameText,
                                                 QStringList()
                                                     << QStringLiteral("rkisp_selfpath")
                                                     << QStringLiteral("selfpath")
                                                     << QStringLiteral("fbcpath")
                                                     << QStringLiteral("iqtool")
                                                     << QStringLiteral("raw")
                                                     << QStringLiteral("rawrd"));
    const bool cameraName = (preferredCameraGroup || secondaryCameraGroup) && !rejectedCameraGroup;

    const bool usableCapture = !metadataNode
                               && !mem2memNode
                               && ((hasCapabilityReport && captureCapability)
                                   || (!hasCapabilityReport && (cameraName || hdmiName)));
    info.hdmiIn = usableCapture && hdmiName;
    // Once the kernel reports a capture capability, vendor/card naming is only
    // a ranking hint. This admits standards-compliant sensors with unfamiliar
    // names while still rejecting metadata and codec mem2mem nodes.
    info.camera = usableCapture && !info.hdmiIn
        && (hasCapabilityReport || cameraName);
    if (info.hdmiIn) {
        info.mode = QStringLiteral("hdmiin");
    } else if (info.camera) {
        info.mode = usbCameraName ? QStringLiteral("usb") : QStringLiteral("mipi");
        // The V4L2 node number only reflects probe order.  In particular,
        // RK3588 exposes the raw rkcif ingress nodes before rkisp_mainpath;
        // the former are useful for bring-up but are not the processed stream
        // expected by the BoardLab live preview.  Rank the discovered device
        // names so the preferred map is stable across kernel releases.
        if (containsAny(nameText,
                        QStringList()
                            << QStringLiteral("rkisp_mainpath")
                            << QStringLiteral("mainpath"))) {
            info.cameraPriority = 300;
        } else if (usbCameraName) {
            info.cameraPriority = 260;
        } else if (preferredCameraGroup) {
            info.cameraPriority = 220;
        } else if (secondaryCameraGroup) {
            info.cameraPriority = 100;
        }
    }
    return info;
}

QVariantMap BoardBackend::videoDeviceInfoToMap(const VideoDeviceInfo &info, const QString &kind, bool preferred)
{
    QVariantMap row;
    const QString name = !info.sysfsName.trimmed().isEmpty()
                             ? info.sysfsName.trimmed()
                             : (!info.groupName.trimmed().isEmpty()
                                    ? info.groupName.trimmed()
                                    : QFileInfo(info.path).fileName());
    const QString group = info.groupName.trimmed();
    QString label = info.path;
    if (!name.isEmpty()) {
        label += QStringLiteral(" - ") + name;
    }

    row.insert(QStringLiteral("path"), info.path);
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("kind"), kind);
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("group"), group);
    row.insert(QStringLiteral("mode"), info.mode.trimmed().isEmpty() ? kind : info.mode.trimmed());
    row.insert(QStringLiteral("preferred"), preferred);
    return row;
}

void BoardBackend::appendDetectedVideoDeviceToResult(const VideoDeviceInfo &info, VideoDeviceScanResult *result)
{
    if (!result || info.path.isEmpty()) {
        return;
    }

    appendUnique(&result->videoDevices, info.path);
    if (info.camera) {
        appendUnique(&result->cameraVideoDevices, info.path);
        const bool preferred = result->cameraVideoDevice.isEmpty();
        result->cameraCaptureDevices.append(videoDeviceInfoToMap(info, QStringLiteral("camera"), preferred));
        if (preferred) {
            result->cameraVideoDevice = info.path;
        }
    }
    if (info.hdmiIn) {
        appendUnique(&result->hdmiInVideoDevices, info.path);
        const bool preferred = result->hdmiInVideoDevice.isEmpty();
        result->hdmiInCaptureDevices.append(videoDeviceInfoToMap(info, QStringLiteral("hdmiin"), preferred));
        if (preferred) {
            result->hdmiInVideoDevice = info.path;
        }
    }
}

BoardBackend::VideoDeviceScanResult BoardBackend::scanVideoDevices()
{
    VideoDeviceScanResult result;
    QList<VideoDeviceInfo> inspectedDevices;
    QDir devDir(deviceRootForHelper());
    const QFileInfoList devices = devDir.entryInfoList(QStringList() << QStringLiteral("video*"),
                                                       QDir::System | QDir::Files,
                                                       QDir::Name);
    for (const QFileInfo &device : devices) {
        const QString path = device.absoluteFilePath();
        if (!isVideoNodePath(path)) {
            continue;
        }
        VideoDeviceInfo info = inspectVideoDevice(path, videoDeviceName(path));
        appendUnique(&result.videoDevices, info.path);
        inspectedDevices.append(info);
    }

    // Preserve the full /dev/video* inventory in its original order while
    // ordering only the usable capture choices.  This makes rkisp_mainpath
    // the default for an RK3588 MIPI camera instead of the earlier raw rkcif
    // node, while retaining rkcif as an explicit advanced/bring-up option.
    std::stable_sort(inspectedDevices.begin(), inspectedDevices.end(),
                     [](const VideoDeviceInfo &left, const VideoDeviceInfo &right) {
        const int leftKind = left.camera ? 2 : (left.hdmiIn ? 1 : 0);
        const int rightKind = right.camera ? 2 : (right.hdmiIn ? 1 : 0);
        if (leftKind != rightKind) {
            return leftKind > rightKind;
        }
        if (left.camera && right.camera && left.cameraPriority != right.cameraPriority) {
            return left.cameraPriority > right.cameraPriority;
        }
        return left.path < right.path;
    });
    for (const VideoDeviceInfo &info : inspectedDevices) {
        appendDetectedVideoDeviceToResult(info, &result);
    }
    return result;
}

QString BoardBackend::interfaceIpv4Address(const QString &interfaceName)
{
    const QByteArray ifaceName = interfaceName.trimmed().toLocal8Bit();
    if (ifaceName.isEmpty()) {
        return QString();
    }

    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return QString();
    }

    ifreq request;
    std::memset(&request, 0, sizeof(request));
    request.ifr_addr.sa_family = AF_INET;
    std::strncpy(request.ifr_name, ifaceName.constData(), IFNAMSIZ - 1);

    QString address;
    if (::ioctl(fd, SIOCGIFADDR, &request) == 0) {
        const auto *socketAddress = reinterpret_cast<sockaddr_in *>(&request.ifr_addr);
        char buffer[INET_ADDRSTRLEN] = {0};
        if (::inet_ntop(AF_INET, &socketAddress->sin_addr, buffer, sizeof(buffer))) {
            address = QString::fromLatin1(buffer);
        }
    }

    ::close(fd);
    return address;
}

QString BoardBackend::nmcliConnectionNameForDevice(const QString &interfaceName)
{
    const QString iface = interfaceName.trimmed();
    if (iface.isEmpty() || commandPath(QStringLiteral("nmcli")).isEmpty()) {
        return QString();
    }

    int exitCode = -1;
    const QString activeOutput = runCommand(QStringLiteral("nmcli"),
                                            QStringList() << QStringLiteral("-t")
                                                          << QStringLiteral("-e") << QStringLiteral("yes")
                                                          << QStringLiteral("-f") << QStringLiteral("NAME,DEVICE")
                                                          << QStringLiteral("connection") << QStringLiteral("show")
                                                          << QStringLiteral("--active"),
                                            5000,
                                            &exitCode);
    const QStringList activeLines = activeOutput.split(QChar('\n'), boardlab::kSkipEmptyParts);
    for (const QString &line : activeLines) {
        const QStringList parts = splitEscapedNmcliLine(line);
        if (parts.value(1).trimmed() == iface) {
            return parts.value(0);
        }
    }

    const QString deviceOutput = runCommand(QStringLiteral("nmcli"),
                                            QStringList() << QStringLiteral("-t")
                                                          << QStringLiteral("-e") << QStringLiteral("yes")
                                                          << QStringLiteral("-f") << QStringLiteral("DEVICE,CONNECTION")
                                                          << QStringLiteral("device") << QStringLiteral("status"),
                                            5000,
                                            &exitCode);
    const QStringList deviceLines = deviceOutput.split(QChar('\n'), boardlab::kSkipEmptyParts);
    for (const QString &line : deviceLines) {
        const QStringList parts = splitEscapedNmcliLine(line);
        if (parts.value(0).trimmed() == iface) {
            const QString connection = parts.value(1).trimmed();
            if (!connection.isEmpty() && connection != QStringLiteral("--")) {
                return connection;
            }
        }
    }

    return QString();
}

int BoardBackend::netmaskPrefixLength(const QString &netmask)
{
    const QString trimmed = netmask.trimmed();
    if (trimmed.isEmpty()) {
        return 24;
    }

    bool prefixOk = false;
    const int directPrefix = trimmed.toInt(&prefixOk);
    if (prefixOk && directPrefix >= 0 && directPrefix <= 32) {
        return directPrefix;
    }

    const QStringList octets = trimmed.split(QChar('.'));
    if (octets.size() != 4) {
        return -1;
    }

    int prefix = 0;
    bool zeroSeen = false;
    for (const QString &octetText : octets) {
        bool ok = false;
        int octet = octetText.toInt(&ok);
        if (!ok || octet < 0 || octet > 255) {
            return -1;
        }
        for (int bit = 7; bit >= 0; --bit) {
            const bool one = (octet & (1 << bit)) != 0;
            if (one && zeroSeen) {
                return -1;
            }
            if (one) {
                ++prefix;
            } else {
                zeroSeen = true;
            }
        }
    }
    return prefix;
}

void BoardBackend::refreshNetworkDevices()
{
    const BackendOperationResult devices = scanCachedBoardDevicesWithCommand(
        m_boardProfile ? m_boardProfile->socFamily() : QString());
    bool changedData = false;
    if (devices.ethernetInterfacesValid && m_ethernetInterfaces != devices.ethernetInterfaces) {
        m_ethernetInterfaces = devices.ethernetInterfaces;
        changedData = true;
    }
    if (devices.canInterfacesValid && m_canInterfaces != devices.canInterfaces) {
        m_canInterfaces = devices.canInterfaces;
        changedData = true;
    }
    if (devices.cellularDevicesValid && m_cellularDevices != devices.cellularDevices) {
        m_cellularDevices = devices.cellularDevices;
        changedData = true;
    }
    if (devices.cellularModemStatusValid && m_cellularModemStatus != devices.cellularModemStatus) {
        m_cellularModemStatus = devices.cellularModemStatus;
        changedData = true;
    }
    if (changedData) {
        emit changed();
    }
}

bool BoardBackend::applyVideoDeviceScan(const VideoDeviceScanResult &result)
{
    const bool changedData = m_videoDevices != result.videoDevices
                             || m_cameraVideoDevices != result.cameraVideoDevices
                             || m_hdmiInVideoDevices != result.hdmiInVideoDevices
                             || m_cameraCaptureDevices != result.cameraCaptureDevices
                             || m_hdmiInCaptureDevices != result.hdmiInCaptureDevices
                             || m_cameraVideoDevice != result.cameraVideoDevice
                             || m_hdmiInVideoDevice != result.hdmiInVideoDevice;
    m_videoDevices = result.videoDevices;
    m_cameraVideoDevices = result.cameraVideoDevices;
    m_hdmiInVideoDevices = result.hdmiInVideoDevices;
    m_cameraCaptureDevices = result.cameraCaptureDevices;
    m_hdmiInCaptureDevices = result.hdmiInCaptureDevices;
    m_cameraVideoDevice = result.cameraVideoDevice;
    m_hdmiInVideoDevice = result.hdmiInVideoDevice;
    return changedData;
}

void BoardBackend::refreshVideoDevices()
{
    if (applyVideoDeviceScan(scanVideoDevices())) {
        emit changed();
    }
}

void BoardBackend::refreshVideoDevicesAsync()
{
    if (m_videoRefreshInFlight) {
        return;
    }
    m_videoRefreshInFlight = true;
    auto *watcher = new QFutureWatcher<VideoDeviceScanResult>(this);
    connect(watcher, &QFutureWatcher<VideoDeviceScanResult>::finished, this, [this, watcher]() {
        const bool changedData = applyVideoDeviceScan(watcher->result());
        m_videoRefreshInFlight = false;
        if (changedData) {
            emit changed();
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(&BoardBackend::scanVideoDevices));
}

void BoardBackend::refreshVideoDevicesFromV4l2(QStringList *detectedDevices)
{
    if (!detectedDevices || commandPath(QStringLiteral("v4l2-ctl")).isEmpty()) {
        return;
    }
    int exitCode = -1;
    const QString output = runCommand(QStringLiteral("v4l2-ctl"),
                                      QStringList() << QStringLiteral("--list-devices"),
                                      3000,
                                      &exitCode);
    if (exitCode != 0) {
        return;
    }
    static const QRegularExpression deviceExpression(QStringLiteral("(/dev/video[0-9]+)"));
    QRegularExpressionMatchIterator it = deviceExpression.globalMatch(output);
    while (it.hasNext()) {
        appendUnique(detectedDevices, it.next().captured(1));
    }
}

void BoardBackend::appendDetectedVideoDevice(const VideoDeviceInfo &info, QStringList *detectedDevices)
{
    if (!detectedDevices || info.path.isEmpty() || (!info.camera && !info.hdmiIn)) {
        return;
    }
    appendUnique(detectedDevices, info.path);
}

void BoardBackend::refreshSerialDevices()
{
    QStringList devices;
    const QStringList patterns = {
        QStringLiteral("ttyS*"),
        QStringLiteral("ttyAMA*"),
        QStringLiteral("ttyTHS*"),
        QStringLiteral("ttySC*"),
        QStringLiteral("ttyLP*"),
        QStringLiteral("ttyFIQ*"),
        QStringLiteral("ttyUSB*"),
        QStringLiteral("ttyACM*")
    };

    QDir devDir(deviceRootForHelper());
    for (const QString &pattern : patterns) {
        const QFileInfoList infos = devDir.entryInfoList(QStringList(pattern), QDir::System | QDir::Files, QDir::Name);
        for (const QFileInfo &info : infos) {
            devices.append(info.absoluteFilePath());
        }
    }

    devices.removeDuplicates();
    m_serialDevices = devices;
}

void BoardBackend::refreshBacklight()
{
    m_backlight = BacklightDevice();
    QDir dir(QStringLiteral("/sys/class/backlight"));
    const QFileInfoList entries = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString path = entry.absoluteFilePath();
        const int maxBrightness = readTextFile(path + QStringLiteral("/max_brightness")).toInt();
        const int brightness = readTextFile(path + QStringLiteral("/brightness")).toInt();
        if (maxBrightness > 0) {
            m_backlight.name = entry.fileName();
            m_backlight.path = path;
            m_backlight.maxBrightness = maxBrightness;
            m_backlight.brightness = qBound(0, brightness, maxBrightness);
            return;
        }
    }
}

void BoardBackend::refreshLedDevices()
{
    m_ledDevices = scanLedDevicesForPaths(m_boardProfile ? m_boardProfile->ledPaths() : QStringList());
}

void BoardBackend::refreshBuzzerAvailability()
{
    bool available = false;
    QString status;
    QString controlMethod = QStringLiteral("unavailable");
    QString inputEventDevice;
    if (!m_boardProfile || !m_boardProfile->hasCapability(QStringLiteral("buzzer"))) {
        status = QStringLiteral("当前板型未声明蜂鸣器");
    } else {
        QString inputProbeFailure;
        inputEventDevice = discoverPwmBeeperInputEvent(&inputProbeFailure);
        if (!inputEventDevice.isEmpty()) {
            available = true;
            controlMethod = QStringLiteral("input-event");
            status = QStringLiteral("pwm-beeper 输入事件已就绪：%1").arg(inputEventDevice);
        } else {
            status = inputProbeFailure.isEmpty()
                ? QStringLiteral("未发现可用的 pwm-beeper 输入事件")
                : inputProbeFailure;
        }
    }

    if (m_buzzerAvailable == available
        && m_buzzerAvailabilityStatus == status
        && m_buzzerControlMethod == controlMethod
        && m_buzzerInputEventDevice == inputEventDevice) {
        return;
    }
    m_buzzerAvailable = available;
    m_buzzerAvailabilityStatus = status;
    m_buzzerControlMethod = controlMethod;
    m_buzzerInputEventDevice = inputEventDevice;
    emit changed();
}

void BoardBackend::refreshFan()
{
    FanDevice discovered;
    const QDir root(boardlab::fixtureRoot("BOARDLAB_HWMON_ROOT",
                                          QStringLiteral("/sys/class/hwmon")));
    // hwmonN entries in /sys/class/hwmon are symlinks to the physical device.
    // Keep the class path instead of canonicalizing it: it follows the link for
    // I/O while remaining valid for the current hwmon numbering.
    const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    bool foundPwmFan = false;
    for (const QFileInfo &entry : entries) {
        if (!entry.isDir()) {
            continue;
        }
        const QString path = entry.absoluteFilePath();
        if (readTextFile(path + QStringLiteral("/name")) != QStringLiteral("pwmfan")) {
            continue;
        }
        if (foundPwmFan) {
            discovered = FanDevice();
            discovered.ambiguous = true;
            break;
        }
        foundPwmFan = true;
        const QString pwmPath = path + QStringLiteral("/pwm1");
        const QFileInfo pwmInfo(pwmPath);
        // Sysfs permission bits are not a reliable capability probe (and can
        // be reported differently through a class symlink).  Discover the
        // node from its attributes; an actual write failure is reported by
        // the control operation instead of hiding an otherwise usable fan.
        if (!pwmInfo.exists()) {
            continue;
        }
        bool maxOk = false;
        int pwmMax = readTextFile(path + QStringLiteral("/pwm1_max")).toInt(&maxOk);
        if (!maxOk || pwmMax <= 0) {
            pwmMax = 255;
        }
        bool pwmOk = false;
        const int pwm = readTextFile(pwmPath).toInt(&pwmOk);
        const QString tempPath = path + QStringLiteral("/temp_control");
        int tempControl = -1;
        if (QFileInfo::exists(tempPath)) {
            bool tempOk = false;
            const int value = readTextFile(tempPath).toInt(&tempOk);
            if (tempOk && (value == 0 || value == 1)) {
                tempControl = value;
            }
        }
        discovered.name = entry.fileName();
        discovered.path = path;
        discovered.pwmPath = pwmPath;
        discovered.pwmMaxPath = path + QStringLiteral("/pwm1_max");
        discovered.tempControlPath = tempControl < 0 ? QString() : tempPath;
        discovered.pwm = pwmOk ? qBound(0, pwm, pwmMax) : 0;
        discovered.pwmMax = pwmMax;
        discovered.tempControl = tempControl;
    }

    const bool changedData = m_fan.name != discovered.name
        || m_fan.path != discovered.path
        || m_fan.pwm != discovered.pwm
        || m_fan.pwmMax != discovered.pwmMax
        || m_fan.tempControl != discovered.tempControl
        || m_fan.ambiguous != discovered.ambiguous;
    m_fan = discovered;
    if (changedData) {
        emit changed();
    }
}

void BoardBackend::refreshAudioVolume()
{
    int exitCode = -1;
    QString output;
    if (!commandPath(QStringLiteral("pactl")).isEmpty()) {
        output = runCommand(QStringLiteral("pactl"),
                            QStringList() << QStringLiteral("get-sink-volume") << QStringLiteral("@DEFAULT_SINK@"),
                            3000,
                            &exitCode);
    }

    if ((output.isEmpty() || exitCode != 0) && !commandPath(QStringLiteral("amixer")).isEmpty()) {
        output = runCommand(QStringLiteral("amixer"),
                            QStringList() << QStringLiteral("get") << QStringLiteral("Master"),
                            3000,
                            &exitCode);
    }

    const QRegularExpression percentExpression(QStringLiteral("([0-9]{1,3})%"));
    const QRegularExpressionMatch match = percentExpression.match(output);
    if (match.hasMatch()) {
        bool ok = false;
        const int percent = match.captured(1).toInt(&ok);
        if (ok) {
            m_audioVolumePercent = qBound(0, percent, 100);
        }
    }
}

void BoardBackend::refreshAudioDevices()
{
    const auto scan = [](const QString &program) {
        QVariantList devices;
        int exitCode = -1;
        const QString output = runCommand(program, QStringList() << QStringLiteral("-l"), 3500, &exitCode);
        if (exitCode != 0) {
            return devices;
        }
        const QRegularExpression deviceExpression(
            QStringLiteral("^card\\s+(\\d+):\\s*([^\\[]+)\\s*\\[([^\\]]+)\\],\\s*device\\s+(\\d+):\\s*([^\\[]+)"),
            QRegularExpression::MultilineOption);
        QRegularExpressionMatchIterator match = deviceExpression.globalMatch(output);
        while (match.hasNext()) {
            const QRegularExpressionMatch itemMatch = match.next();
            const QString device = QStringLiteral("hw:%1,%2").arg(itemMatch.captured(1), itemMatch.captured(4));
            QVariantMap item;
            item.insert(QStringLiteral("device"), device);
            item.insert(QStringLiteral("commandDevice"), QStringLiteral("plughw:%1,%2").arg(itemMatch.captured(1), itemMatch.captured(4)));
            item.insert(QStringLiteral("card"), itemMatch.captured(3).trimmed());
            item.insert(QStringLiteral("name"), itemMatch.captured(5).trimmed());
            item.insert(QStringLiteral("label"), QStringLiteral("%1 · %2").arg(device, itemMatch.captured(3).trimmed()));
            devices.append(item);
        }
        std::stable_sort(devices.begin(), devices.end(), [](const QVariant &left, const QVariant &right) {
            const QVariantMap leftItem = left.toMap();
            const QVariantMap rightItem = right.toMap();
            const int leftPreference = audioDevicePreference(leftItem);
            const int rightPreference = audioDevicePreference(rightItem);
            if (leftPreference != rightPreference) {
                return leftPreference < rightPreference;
            }
            return leftItem.value(QStringLiteral("device")).toString()
                < rightItem.value(QStringLiteral("device")).toString();
        });
        for (int index = 0; index < devices.size(); ++index) {
            QVariantMap item = devices.at(index).toMap();
            item.insert(QStringLiteral("preferred"), index == 0);
            devices[index] = item;
        }
        return devices;
    };

    const QVariantList playback = scan(QStringLiteral("aplay"));
    const QVariantList capture = scan(QStringLiteral("arecord"));
    if (m_audioPlaybackDevices != playback || m_audioCaptureDevices != capture) {
        m_audioPlaybackDevices = playback;
        m_audioCaptureDevices = capture;
    }
}

void BoardBackend::refreshKeyTestDevices()
{
    KeyDeviceDiscovery discovery;
    const QVariantList candidates = normalizeKeyCandidates(discovery.discoverSafeCandidates());
    const QString activeDeviceId = m_keyMonitor ? m_keyMonitor->activeDeviceId() : QString();
    const QString activeEventPath = m_keyMonitor ? m_keyMonitor->activeEventPath() : QString();
    bool activeDeviceIdStillPresent = activeDeviceId.isEmpty();
    bool activeMappingStillPresent = activeDeviceId.isEmpty();
    for (const QVariant &candidateVariant : candidates) {
        const QVariantMap candidate = candidateVariant.toMap();
        if (candidate.value(QStringLiteral("deviceId")).toString() == activeDeviceId) {
            activeDeviceIdStillPresent = true;
            if (candidate.value(QStringLiteral("eventPath")).toString() == activeEventPath) {
                activeMappingStillPresent = true;
                break;
            }
        }
    }

    const bool changedDevices = m_keyTestDevices != candidates;
    m_keyTestDevices = candidates;
    if (m_keyMonitor && m_keyMonitor->active() && !activeMappingStillPresent) {
        // A stable DT ID alone is not enough once an fd is open: a driver
        // reload can assign that node a new eventN.  Do not continue reading
        // the stale fd; require an explicit, freshly discovered Start.
        const QString status = activeDeviceIdStillPresent
            ? QStringLiteral("Key input mapping changed; monitoring stopped. Select the device and start again.")
            : QStringLiteral("Key input device disappeared; monitoring stopped. Rediscover and start again.");
        m_keyMonitor->stop(status);
        setStatus(status);
    }
    if (changedDevices) {
        emit changed();
    }
}

void BoardBackend::refreshReadOnlyDiagnostics()
{
    QVariantMap rtc;
    rtc.insert(QStringLiteral("systemUtc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QString rtcNode;
    QString rtcSysfsUtc;
    const QFileInfoList rtcEntries = QDir(rtcClassRootForHelper()).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : rtcEntries) {
        const QString date = readTextFile(entry.absoluteFilePath() + QStringLiteral("/date"));
        const QString time = readTextFile(entry.absoluteFilePath() + QStringLiteral("/time"));
        if (date.isEmpty() || time.isEmpty()) {
            continue;
        }
        rtcNode = entry.fileName();
        rtcSysfsUtc = QStringLiteral("%1T%2Z").arg(date, time);
        break;
    }

    int rtcExitCode = -1;
    QString rtcOutput;
    if (rtcSysfsUtc.isEmpty()) {
        rtcOutput = runCommand(QStringLiteral("hwclock"),
                               QStringList() << QStringLiteral("-u"),
                               2500,
                               &rtcExitCode);
    }
    const bool rtcAvailable = !rtcSysfsUtc.isEmpty() || rtcExitCode == 0;
    const bool rtcDevicePresent = QFileInfo::exists(QDir(deviceRootForHelper()).filePath(QStringLiteral("rtc")))
        || QFileInfo::exists(QDir(deviceRootForHelper()).filePath(QStringLiteral("rtc0")));
    const bool rtcAuthorizationRequired = !rtcAvailable && rtcDevicePresent && ::geteuid() != 0;
    rtc.insert(QStringLiteral("available"), rtcAvailable);
    rtc.insert(QStringLiteral("hardwareUtc"), !rtcSysfsUtc.isEmpty() ? rtcSysfsUtc
                                                                       : (rtcExitCode == 0 ? rtcOutput : QString()));
    rtc.insert(QStringLiteral("source"), !rtcSysfsUtc.isEmpty() ? QStringLiteral("sysfs")
                                                                   : (rtcExitCode == 0 ? QStringLiteral("hwclock") : QString()));
    rtc.insert(QStringLiteral("authorizationRequired"), rtcAuthorizationRequired);
    rtc.insert(QStringLiteral("status"), !rtcSysfsUtc.isEmpty()
               ? QStringLiteral("Hardware clock read from %1 sysfs").arg(rtcNode)
               : (rtcExitCode == 0
                  ? QStringLiteral("Hardware clock read successfully")
                  : (rtcAuthorizationRequired
                     ? QStringLiteral("Hardware clock exists but requires authorized access")
                     : QStringLiteral("Hardware clock is unavailable: %1").arg(compactCommandOutput(rtcOutput)))));

    QVariantMap npu;
    const QString root = rknpuDebugRootForHelper();
    const QString version = readTextFile(root + QStringLiteral("/version"));
    const QString driverVersion = readTextFile(root + QStringLiteral("/driver_version"));
    const QString load = readTextFile(root + QStringLiteral("/load"));
    const QString power = readTextFile(root + QStringLiteral("/power"));
    npu.insert(QStringLiteral("available"), !version.isEmpty() || !driverVersion.isEmpty() || !load.isEmpty() || !power.isEmpty());
    npu.insert(QStringLiteral("version"), !version.isEmpty() ? version : driverVersion);
    npu.insert(QStringLiteral("load"), load);
    npu.insert(QStringLiteral("power"), power);
    const bool npuExpectedByProfile = m_boardProfile && m_boardProfile->hasCapability(QStringLiteral("npu"));
    const bool npuAuthorizationRequired = !npu.value(QStringLiteral("available")).toBool()
        && QFileInfo::exists(root) && ::geteuid() != 0;
    npu.insert(QStringLiteral("authorizationRequired"), npuAuthorizationRequired);
    npu.insert(QStringLiteral("status"), npu.value(QStringLiteral("available")).toBool()
               ? QStringLiteral("RKNPU diagnostic nodes are readable")
               : (npuAuthorizationRequired
                  ? QStringLiteral("RKNPU diagnostic nodes require authorized access")
                  : (npuExpectedByProfile
                     ? QStringLiteral("RKNPU diagnostic nodes are not exposed by the running kernel")
                     : QStringLiteral("RKNPU diagnostic nodes are unavailable"))));

    // This runs during every passive refresh, including the first desktop
    // launch.  Asking NetworkManager through `nmcli device` looks harmless,
    // but on some vendor images it asks for the Wi-Fi scan polkit permission
    // and puts an authentication dialog over the BoardLab home screen.  A
    // status card must never scan, associate, or request privileges.  Read
    // the kernel-owned interface state instead; explicit network operations
    // continue to use NetworkManager after the operator has chosen them.
    QVariantMap wifi;
    QString wifiDevice;
    QString wifiState;
    bool wifiLinkUp = false;
    QDir networkDirectory(networkClassRootForHelper());
    const QFileInfoList networkEntries = networkDirectory.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                                          QDir::Name);
    for (const QFileInfo &entry : networkEntries) {
        const QString interfaceName = entry.fileName();
        // Do not present the Wi-Fi Direct management interface as the board's
        // normal wireless NIC when both are present.
        if (interfaceName.startsWith(QStringLiteral("p2p-"), Qt::CaseInsensitive)) {
            continue;
        }
        const QString interfacePath = entry.absoluteFilePath();
        const bool wireless = QFileInfo::exists(QDir(interfacePath).filePath(QStringLiteral("wireless")))
            || interfaceName.startsWith(QStringLiteral("wlan"), Qt::CaseInsensitive)
            || interfaceName.startsWith(QStringLiteral("wl"), Qt::CaseInsensitive);
        if (!wireless) {
            continue;
        }

        wifiDevice = interfaceName;
        wifiState = readTextFile(interfacePath + QStringLiteral("/operstate")).trimmed();
        const QString carrier = readTextFile(interfacePath + QStringLiteral("/carrier")).trimmed();
        wifiLinkUp = carrier == QStringLiteral("1")
            || wifiState.compare(QStringLiteral("up"), Qt::CaseInsensitive) == 0;
        break;
    }
    const QString rfkillOutput = runCommand(QStringLiteral("rfkill"),
                                             QStringList() << QStringLiteral("list"),
                                             1200,
                                             nullptr);
    const QRegularExpression wlanBlockExpression(QStringLiteral("Wireless LAN[\\s\\S]*?Soft blocked: (yes|no)[\\s\\S]*?Hard blocked: (yes|no)"),
                                                  QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch wlanBlockMatch = wlanBlockExpression.match(rfkillOutput);
    const bool wifiSoftBlocked = wlanBlockMatch.hasMatch() && wlanBlockMatch.captured(1).compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
    const bool wifiHardBlocked = wlanBlockMatch.hasMatch() && wlanBlockMatch.captured(2).compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0;
    const QString wifiIpv4 = wifiDevice.isEmpty() ? QString() : interfaceIpv4AddressForHelper(wifiDevice);
    wifi.insert(QStringLiteral("available"), !wifiDevice.isEmpty());
    wifi.insert(QStringLiteral("device"), wifiDevice);
    wifi.insert(QStringLiteral("interface"), wifiDevice);
    wifi.insert(QStringLiteral("state"), wifiState);
    wifi.insert(QStringLiteral("linkUp"), wifiLinkUp);
    wifi.insert(QStringLiteral("ipv4"), wifiIpv4);
    wifi.insert(QStringLiteral("ipv4Address"), wifiIpv4);
    wifi.insert(QStringLiteral("ipAddress"), wifiIpv4);
    wifi.insert(QStringLiteral("ipv4Assigned"), !wifiIpv4.isEmpty());
    wifi.insert(QStringLiteral("ipv4Status"), wifiIpv4.isEmpty()
                ? QStringLiteral("not-assigned") : QStringLiteral("assigned"));
    wifi.insert(QStringLiteral("connection"), wifiDevice.isEmpty()
                ? QString()
                : (wifiLinkUp
                   ? (wifiIpv4.isEmpty()
                      ? QStringLiteral("无线链路已连接（未读取 SSID，尚未分配 IPv4）")
                      : QStringLiteral("无线链路已连接，IPv4：%1（未读取 SSID）").arg(wifiIpv4))
                   : QStringLiteral("未连接（未扫描 Wi-Fi）")));
    wifi.insert(QStringLiteral("softBlocked"), wifiSoftBlocked);
    wifi.insert(QStringLiteral("hardBlocked"), wifiHardBlocked);
    // Passive kernel/sysfs reads never need NetworkManager or polkit access.
    wifi.insert(QStringLiteral("authorizationRequired"), false);
    wifi.insert(QStringLiteral("status"), !wifiDevice.isEmpty()
                ? (wifiIpv4.isEmpty()
                   ? QStringLiteral("Wi-Fi 内核状态已读取；未扫描、联网或读取 SSID，尚未获得 IPv4")
                   : QStringLiteral("Wi-Fi 内核状态已读取；IPv4：%1；未扫描或读取 SSID").arg(wifiIpv4))
                : QStringLiteral("No Wi-Fi interface discovered"));

    QVariantMap bluetooth;
    int bluetoothExitCode = -1;
    const QString bluetoothOutput = runCommand(QStringLiteral("bluetoothctl"),
                                               QStringList() << QStringLiteral("show"),
                                               1800,
                                               &bluetoothExitCode);
    const QRegularExpression controllerExpression(QStringLiteral("^Controller ([0-9A-F:]+)"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression poweredExpression(QStringLiteral("^\\s*Powered: (yes|no)"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression pairableExpression(QStringLiteral("^\\s*Pairable: (yes|no)"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression discoveringExpression(QStringLiteral("^\\s*Discovering: (yes|no)"), QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch controllerMatch = controllerExpression.match(bluetoothOutput);
    const QRegularExpressionMatch poweredMatch = poweredExpression.match(bluetoothOutput);
    const QRegularExpressionMatch pairableMatch = pairableExpression.match(bluetoothOutput);
    const QRegularExpressionMatch discoveringMatch = discoveringExpression.match(bluetoothOutput);
    const QRegularExpression bluetoothBlockExpression(QStringLiteral("Bluetooth[\\s\\S]*?Soft blocked: (yes|no)[\\s\\S]*?Hard blocked: (yes|no)"),
                                                       QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch bluetoothBlockMatch = bluetoothBlockExpression.match(rfkillOutput);
    bluetooth.insert(QStringLiteral("available"), controllerMatch.hasMatch());
    bluetooth.insert(QStringLiteral("controller"), controllerMatch.hasMatch() ? controllerMatch.captured(1) : QString());
    bluetooth.insert(QStringLiteral("powered"), poweredMatch.hasMatch() && poweredMatch.captured(1).compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0);
    bluetooth.insert(QStringLiteral("pairable"), pairableMatch.hasMatch() && pairableMatch.captured(1).compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0);
    bluetooth.insert(QStringLiteral("discovering"), discoveringMatch.hasMatch() && discoveringMatch.captured(1).compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0);
    bluetooth.insert(QStringLiteral("softBlocked"), bluetoothBlockMatch.hasMatch() && bluetoothBlockMatch.captured(1).compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0);
    bluetooth.insert(QStringLiteral("hardBlocked"), bluetoothBlockMatch.hasMatch() && bluetoothBlockMatch.captured(2).compare(QStringLiteral("yes"), Qt::CaseInsensitive) == 0);
    bluetooth.insert(QStringLiteral("authorizationRequired"), bluetoothExitCode != 0 && QFileInfo::exists(QStringLiteral("/sys/class/bluetooth")));
    bluetooth.insert(QStringLiteral("status"), controllerMatch.hasMatch()
                     ? QStringLiteral("Bluetooth controller discovered; no scan or pairing was requested")
                     : (bluetoothExitCode != 0
                        ? QStringLiteral("Bluetooth status requires authorized system-bus access: %1").arg(compactCommandOutput(bluetoothOutput))
                        : QStringLiteral("No Bluetooth controller discovered")));

    QVariantList inputs;
    const QString inputDeviceText = readTextFile(QStringLiteral("/proc/bus/input/devices"));
    const QRegularExpression nameExpression(QStringLiteral("^N: Name=\\\"([^\\\"]+)\\\""), QRegularExpression::MultilineOption);
    const QRegularExpression handlerExpression(QStringLiteral("^H: Handlers=(.+)$"), QRegularExpression::MultilineOption);
    for (const QString &block : inputDeviceText.split(QStringLiteral("\n\n"), boardlab::kSkipEmptyParts)) {
        const QRegularExpressionMatch nameMatch = nameExpression.match(block);
        const QRegularExpressionMatch handlerMatch = handlerExpression.match(block);
        if (!nameMatch.hasMatch() || !handlerMatch.hasMatch()) {
            continue;
        }
        const QRegularExpression eventExpression(QStringLiteral("\\bevent[0-9]+\\b"));
        const QRegularExpressionMatch eventMatch = eventExpression.match(handlerMatch.captured(1));
        if (!eventMatch.hasMatch()) {
            continue;
        }
        const QString name = nameMatch.captured(1);
        const QString lowerName = name.toLower();
        const bool external = lowerName.contains(QStringLiteral("usb"))
            || lowerName.contains(QStringLiteral("keyboard"))
            || lowerName.contains(QStringLiteral("mouse"))
            || lowerName.contains(QStringLiteral("gaming"));
        QVariantMap input;
        input.insert(QStringLiteral("name"), name);
        input.insert(QStringLiteral("event"), QStringLiteral("/dev/input/") + eventMatch.captured(0));
        input.insert(QStringLiteral("boardInput"), !external);
        input.insert(QStringLiteral("kind"), lowerName.contains(QStringLiteral("key")) || lowerName.contains(QStringLiteral("button")) ? QStringLiteral("key") : QStringLiteral("input"));
        inputs.append(input);
    }

    QVariantList outputs;
    QDir drmDir(drmClassRootForHelper());
    const QFileInfoList drmEntries = drmDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : drmEntries) {
        const QString statusPath = entry.absoluteFilePath() + QStringLiteral("/status");
        const QString status = readTextFile(statusPath).trimmed();
        if (status.isEmpty()) {
            continue;
        }
        const QString connector = entry.fileName();
        if (!connector.contains(QLatin1Char('-'))) {
            continue;
        }
        QVariantMap output;
        output.insert(QStringLiteral("connector"), connector);
        output.insert(QStringLiteral("status"), status);
        output.insert(QStringLiteral("connected"), status == QStringLiteral("connected"));
        output.insert(QStringLiteral("kind"), connector.contains(QStringLiteral("HDMI"), Qt::CaseInsensitive)
                      ? QStringLiteral("HDMI") : (connector.contains(QStringLiteral("VGA"), Qt::CaseInsensitive)
                                                 ? QStringLiteral("VGA") : QStringLiteral("DRM")));
        outputs.append(output);
    }

    QVariantMap hdmiIn;
    QString hdmiDevice;
    for (const QVariant &deviceVariant : m_hdmiInCaptureDevices) {
        const QVariantMap device = deviceVariant.toMap();
        hdmiDevice = device.value(QStringLiteral("path")).toString();
        if (!hdmiDevice.isEmpty()) {
            break;
        }
    }
    if (hdmiDevice.isEmpty() && !m_hdmiInVideoDevices.isEmpty()) {
        hdmiDevice = m_hdmiInVideoDevices.first();
    }
    if (hdmiDevice.isEmpty()) {
        for (int index = 0; index < 64; ++index) {
            const QString nodeName = readTextFile(QStringLiteral("/sys/class/video4linux/video%1/name").arg(index)).trimmed();
            if (nodeName.contains(QStringLiteral("hdmirx"), Qt::CaseInsensitive)
                || nodeName.contains(QStringLiteral("hdmi-rx"), Qt::CaseInsensitive)) {
                hdmiDevice = QStringLiteral("/dev/video%1").arg(index);
                break;
            }
        }
    }
    int hdmiExitCode = -1;
    const QString hdmiOutput = hdmiDevice.isEmpty() ? QString() : runCommand(QStringLiteral("v4l2-ctl"),
                                                                                QStringList() << QStringLiteral("-d") << hdmiDevice << QStringLiteral("--all"),
                                                                                1800,
                                                                                &hdmiExitCode);
    const QRegularExpression powerExpression(QStringLiteral("power_present[^\\n]*value=0x?([0-9a-f]+)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression audioExpression(QStringLiteral("audio_present[^\\n]*value=([0-9]+)"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch powerMatch = powerExpression.match(hdmiOutput);
    const QRegularExpressionMatch audioMatch = audioExpression.match(hdmiOutput);
    bool powerPresent = false;
    if (powerMatch.hasMatch()) {
        bool ok = false;
        powerPresent = powerMatch.captured(1).toInt(&ok, 0) != 0 && ok;
    }
    hdmiIn.insert(QStringLiteral("available"), !hdmiDevice.isEmpty() && hdmiExitCode == 0);
    hdmiIn.insert(QStringLiteral("device"), hdmiDevice);
    hdmiIn.insert(QStringLiteral("signalPresent"), powerPresent);
    hdmiIn.insert(QStringLiteral("audioPresent"), audioMatch.hasMatch() && audioMatch.captured(1).toInt() != 0);
    hdmiIn.insert(QStringLiteral("status"), hdmiDevice.isEmpty()
                  ? QStringLiteral("No HDMI input capture device discovered")
                  : (hdmiExitCode != 0 ? QStringLiteral("HDMI input node requires V4L2 access")
                     : (powerPresent ? QStringLiteral("HDMI input signal is present") : QStringLiteral("HDMI input node is ready but no source signal is present"))));

    if (m_rtcStatus != rtc || m_npuStatus != npu || m_wifiStatus != wifi || m_bluetoothStatus != bluetooth
        || m_inputDevices != inputs || m_displayOutputs != outputs || m_hdmiInStatus != hdmiIn) {
        m_rtcStatus = rtc;
        m_npuStatus = npu;
        m_wifiStatus = wifi;
        m_bluetoothStatus = bluetooth;
        m_inputDevices = inputs;
        m_displayOutputs = outputs;
        m_hdmiInStatus = hdmiIn;
    }
}

void BoardBackend::refreshFileEntries()
{
    QVariantList entries;
    QDir dir(m_currentPath);
    if (!dir.exists()) {
        m_currentPath = QDir::homePath();
        dir.setPath(m_currentPath);
    }

    const QFileInfoList infos = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
    const int maxEntries = qMin(infos.size(), 24);
    for (int i = 0; i < maxEntries; ++i) {
        const QFileInfo &info = infos.at(i);
        QVariantMap row;
        row.insert(QStringLiteral("name"), info.fileName() + (info.isDir() ? QStringLiteral("/") : QString()));
        row.insert(QStringLiteral("path"), info.absoluteFilePath());
        row.insert(QStringLiteral("directory"), info.isDir());
        row.insert(QStringLiteral("size"), info.isDir() ? QStringLiteral("Folder") : QStringLiteral("%1 KB").arg(qMax<qint64>(1, info.size() / 1024)));
        entries.append(row);
    }

    m_fileEntries = entries;
}

void BoardBackend::setStatus(const QString &status)
{
    if (m_statusText == status) {
        return;
    }
    m_statusText = status;
    emit changed();
}

bool BoardBackend::beginAsyncOperation(const QString &operation)
{
    if (!operationAllowed(operation)) {
        const QString status = operationUnavailableReason(operation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }
    if (m_backendBusy) {
        const QString status = QStringLiteral("Backend busy: %1").arg(m_backendBusyOperation);
        setStatus(status);
        emit operationFinished(operation, false, QString(), status);
        return false;
    }

    m_backendBusy = true;
    m_backendBusyOperation = operation;
    emit operationStateChanged();
    return true;
}

void BoardBackend::finishAsyncOperation(const QString &operation, bool success, const QString &result, const QString &status)
{
    m_backendBusy = false;
    m_backendBusyOperation.clear();
    emit operationStateChanged();
    emit operationFinished(operation, success, result, status);
}

// Storage device functions implementation
QVariantList BoardBackend::storageDevices() const
{
    return m_storageDevices;
}

void BoardBackend::scanStorageDevices()
{
    bool scanSucceeded = false;
    bool inventoryValid = false;
    QString scanStatus;
    const QVariantList scannedDevices = scanStorageDevicesWithCommand(
        &scanSucceeded, &scanStatus, &inventoryValid);
    if (inventoryValid) {
        m_storageDevices = scannedDevices;
        setStatus(scanStatus);
        emit changed();
        return;
    }
    setStatus(scanStatus);
}

bool BoardBackend::scanStorageDevicesAsync()
{
    const QString operation = QStringLiteral("storage-scan");
    if (!beginAsyncOperation(operation)) {
        return false;
    }

    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, operation]() {
        const BackendOperationResult result = watcher->result();
        const QVariantList previousDevices = m_storageDevices;
        if (result.storageDevicesValid) {
            m_storageDevices = result.storageDevices;
        }
        const bool devicesChanged = previousDevices != m_storageDevices;
        const bool statusChanged = m_statusText != result.status;
        setStatus(result.status);
        if (devicesChanged && !statusChanged) {
            emit changed();
        }
        finishAsyncOperation(operation, result.success, result.result, result.status);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(scanStorageDevicesOperation));
    return true;
}

bool BoardBackend::mountStorageDevice(const QString &device, const QString &mountPoint)
{
    const BackendOperationResult result = mountStorageDeviceWithCommand(device, mountPoint);
    if (result.success) {
        setStatus(result.status);
        scanStorageDevices();
        return true;
    }

    setStatus(result.status);
    return false;
}

bool BoardBackend::mountStorageDeviceAsync(const QString &device, const QString &mountPoint)
{
    const QString operation = QStringLiteral("storage-mount");
    if (!beginAsyncOperation(operation)) {
        return false;
    }

    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, operation]() {
        const BackendOperationResult result = watcher->result();
        const QVariantList previousDevices = m_storageDevices;
        if (result.storageDevicesValid) {
            m_storageDevices = result.storageDevices;
        }
        const bool devicesChanged = previousDevices != m_storageDevices;
        const bool statusChanged = m_statusText != result.status;
        setStatus(result.status);
        if (devicesChanged && !statusChanged) {
            emit changed();
        }
        finishAsyncOperation(operation, result.success, result.result, result.status);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([device, mountPoint]() {
        return mountStorageDeviceWithCommand(device, mountPoint);
    }));
    return true;
}

bool BoardBackend::unmountStorageDevice(const QString &mountPoint)
{
    const BackendOperationResult result = unmountStorageDeviceWithCommand(mountPoint);
    if (result.success) {
        setStatus(result.status);
        scanStorageDevices();
        return true;
    }

    setStatus(result.status);
    return false;
}

bool BoardBackend::unmountStorageDeviceAsync(const QString &mountPoint)
{
    const QString operation = QStringLiteral("storage-unmount");
    if (!beginAsyncOperation(operation)) {
        return false;
    }

    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, operation]() {
        const BackendOperationResult result = watcher->result();
        const QVariantList previousDevices = m_storageDevices;
        if (result.storageDevicesValid) {
            m_storageDevices = result.storageDevices;
        }
        const bool devicesChanged = previousDevices != m_storageDevices;
        const bool statusChanged = m_statusText != result.status;
        setStatus(result.status);
        if (devicesChanged && !statusChanged) {
            emit changed();
        }
        finishAsyncOperation(operation, result.success, result.result, result.status);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([mountPoint]() {
        return unmountStorageDeviceWithCommand(mountPoint);
    }));
    return true;
}

bool BoardBackend::testStorageSpeed(const QString &mountPoint)
{
    const QString operation = QStringLiteral("storage-speed");
    if (!beginAsyncOperation(operation)) {
        return false;
    }

    for (int i = 0; i < m_storageDevices.size(); ++i) {
        QVariantMap info = m_storageDevices[i].toMap();
        if (info["mountPoint"].toString() == mountPoint) {
            info["testing"] = true;
            m_storageDevices[i] = info;
            break;
        }
    }

    setStatus(QStringLiteral("Running speed test..."));
    emit changed();

    auto *watcher = new QFutureWatcher<BackendOperationResult>(this);
    connect(watcher, &QFutureWatcher<BackendOperationResult>::finished, this, [this, watcher, operation, mountPoint]() {
        const BackendOperationResult result = watcher->result();
        const QStringList speeds = result.result.split(QChar('\n'));
        const QString readSpeed = speeds.value(0, QStringLiteral("N/A"));
        const QString writeSpeed = speeds.value(1, QStringLiteral("N/A"));
        const QVariantList previousDevices = m_storageDevices;
        for (int i = 0; i < m_storageDevices.size(); ++i) {
            QVariantMap info = m_storageDevices[i].toMap();
            if (info[QStringLiteral("mountPoint")].toString() == mountPoint) {
                info[QStringLiteral("readSpeed")] = readSpeed;
                info[QStringLiteral("writeSpeed")] = writeSpeed;
                info[QStringLiteral("testing")] = false;
                m_storageDevices[i] = info;
                break;
            }
        }
        const bool devicesChanged = previousDevices != m_storageDevices;
        const bool statusChanged = m_statusText != result.status;
        setStatus(result.status);
        if (devicesChanged && !statusChanged) {
            emit changed();
        }
        finishAsyncOperation(operation, result.success, result.result, result.status);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run(testStorageSpeedWithCommand, mountPoint));
    return true;
}
