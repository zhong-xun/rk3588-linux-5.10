#include "keydevicediscovery.h"

#include "keymonitor.h"
#include "qtcompat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

bool testModeEnabled()
{
    return qEnvironmentVariable("BOARDLAB_TEST_MODE") == QStringLiteral("1");
}

QString testRootOrDefault(const char *environmentName, const QString &fallback)
{
    if (testModeEnabled()) {
        const QString configured = QString::fromLocal8Bit(qgetenv(environmentName)).trimmed();
        if (!configured.isEmpty()) {
            return QDir::cleanPath(configured);
        }
    }
    return fallback;
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    return file.readAll();
}

QString readText(const QString &path)
{
    QByteArray bytes = readBytes(path);
    bytes.replace('\0', ' ');
    return QString::fromUtf8(bytes).simplified();
}

QStringList nulDelimitedStrings(const QByteArray &bytes)
{
    QStringList values;
    const QList<QByteArray> rawValues = bytes.split('\0');
    for (const QByteArray &rawValue : rawValues) {
        const QString value = QString::fromUtf8(rawValue).trimmed();
        if (!value.isEmpty()) {
            values.append(value);
        }
    }
    return values;
}

QString ueventValue(const QString &uevent, const QString &key)
{
    const QString prefix = key + QLatin1Char('=');
    const QStringList lines = uevent.split(QLatin1Char('\n'), boardlab::kSkipEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith(prefix)) {
            return line.mid(prefix.size()).trimmed();
        }
    }
    return QString();
}

QString canonicalOrCleanPath(const QString &path)
{
    const QString canonical = QFileInfo(path).canonicalFilePath();
    return canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
}

QString pathBelowRoot(const QString &path, const QString &root)
{
    const QString cleanPath = QDir::cleanPath(path);
    const QString cleanRoot = QDir::cleanPath(root);
    if (cleanPath == cleanRoot) {
        return QStringLiteral("/");
    }
    const QString prefix = cleanRoot + QLatin1Char('/');
    if (!cleanPath.startsWith(prefix)) {
        return QString();
    }
    return cleanPath.mid(cleanRoot.size());
}

bool hasAllowedCompatible(const QStringList &compatible)
{
    for (const QString &entry : compatible) {
        const QString value = entry.toLower();
        if (value.contains(QStringLiteral("adc-keys")) || value.contains(QStringLiteral("gpio-keys"))) {
            return true;
        }
    }
    return false;
}

int readBigEndianCell(const QByteArray &bytes, int offset)
{
    if (offset < 0 || offset + 4 > bytes.size()) {
        return -1;
    }
    return (static_cast<unsigned char>(bytes.at(offset)) << 24)
        | (static_cast<unsigned char>(bytes.at(offset + 1)) << 16)
        | (static_cast<unsigned char>(bytes.at(offset + 2)) << 8)
        | static_cast<unsigned char>(bytes.at(offset + 3));
}

QVariantList keyCodesFromDeviceTree(const QString &nodePath)
{
    QVariantList codes;
    QSet<int> seen;
    const QDir node(nodePath);
    const QFileInfoList children = node.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                      QDir::Name);
    for (const QFileInfo &child : children) {
        const QByteArray rawCode = readBytes(QDir(child.absoluteFilePath()).filePath(QStringLiteral("linux,code")));
        const int code = readBigEndianCell(rawCode, 0);
        if (code < 0 || code > KEY_MAX || seen.contains(code)) {
            continue;
        }
        seen.insert(code);
        QVariantMap key;
        key.insert(QStringLiteral("code"), code);
        key.insert(QStringLiteral("label"), readText(QDir(child.absoluteFilePath()).filePath(QStringLiteral("label"))));
        key.insert(QStringLiteral("node"), child.fileName());
        codes.append(key);
    }
    return codes;
}

QString driverName(const QString &devicePath)
{
    const QFileInfo driverInfo(QDir(devicePath).filePath(QStringLiteral("driver")));
    const QString canonical = driverInfo.canonicalFilePath();
    if (!canonical.isEmpty()) {
        return QFileInfo(canonical).fileName();
    }
    const QString target = driverInfo.symLinkTarget();
    return target.isEmpty() ? QString() : QFileInfo(target).fileName();
}

QString busName(int busType)
{
    switch (busType) {
    case BUS_USB:
        return QStringLiteral("usb");
    case BUS_BLUETOOTH:
        return QStringLiteral("bluetooth");
    case BUS_I2C:
        return QStringLiteral("i2c");
    case BUS_SPI:
        return QStringLiteral("spi");
    case BUS_HOST:
        return QStringLiteral("host");
    default:
        return busType >= 0 ? QStringLiteral("bus-%1").arg(busType, 0, 16) : QStringLiteral("unknown");
    }
}

int busTypeFromProduct(const QString &uevent)
{
    const QString product = ueventValue(uevent, QStringLiteral("PRODUCT"));
    const QString firstPart = product.section(QLatin1Char('/'), 0, 0).trimmed();
    bool ok = false;
    const int value = firstPart.toInt(&ok, 16);
    return ok ? value : -1;
}

QVariantMap runtimeInputEvidence(const QString &eventPath,
                                 const QString &eventDevicePath,
                                 const QString &inputName,
                                 const QString &uevent)
{
    QVariantMap runtime;
    QString name = inputName;
    int busType = busTypeFromProduct(uevent);
    QString probe = QStringLiteral("sysfs");

    const QByteArray encodedPath = eventPath.toLocal8Bit();
    const int fd = ::open(encodedPath.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (fd >= 0) {
        char evdevName[256] = {};
        input_id inputId = {};
        const bool nameOk = ::ioctl(fd, EVIOCGNAME(sizeof(evdevName)), evdevName) >= 0;
        const bool idOk = ::ioctl(fd, EVIOCGID, &inputId) >= 0;
        if (nameOk && QString::fromLocal8Bit(evdevName).trimmed().size() > 0) {
            name = QString::fromLocal8Bit(evdevName).trimmed();
        }
        if (idOk) {
            busType = inputId.bustype;
        }
        probe = nameOk || idOk ? QStringLiteral("evdev") : QStringLiteral("sysfs");
        ::close(fd);
    }

    runtime.insert(QStringLiteral("inputName"), name);
    runtime.insert(QStringLiteral("inputBus"), busName(busType));
    runtime.insert(QStringLiteral("inputBusType"), busType);
    runtime.insert(QStringLiteral("eventPath"), eventPath);
    runtime.insert(QStringLiteral("eventSysfsPath"), eventDevicePath);
    runtime.insert(QStringLiteral("runtimeProbe"), probe);
    runtime.insert(QStringLiteral("readable"), QFileInfo(eventPath).isReadable());
    return runtime;
}

struct DeviceTreeEvidence {
    QString devicePath;
    QString ofNodePath;
    QString canonicalOfNodePath;
    QString dtPath;
    QStringList compatible;
    QString driver;
};

DeviceTreeEvidence findDeviceTreeEvidence(const QString &inputDevicePath, const QString &deviceTreeRoot)
{
    DeviceTreeEvidence evidence;
    const QString canonicalTreeRoot = canonicalOrCleanPath(deviceTreeRoot);
    QString current = canonicalOrCleanPath(inputDevicePath);
    QSet<QString> seen;

    while (!current.isEmpty() && current != QStringLiteral("/") && !seen.contains(current)) {
        seen.insert(current);
        const QString ofNodePath = QDir(current).filePath(QStringLiteral("of_node"));
        const QFileInfo ofNodeInfo(ofNodePath);
        if (ofNodeInfo.exists()) {
            QString canonicalOfNode = ofNodeInfo.canonicalFilePath();
            QString dtPath = pathBelowRoot(canonicalOfNode, canonicalTreeRoot);
            if (dtPath.isEmpty()) {
                const QString ofFullName = ueventValue(readText(QDir(current).filePath(QStringLiteral("uevent"))),
                                                       QStringLiteral("OF_FULLNAME"));
                if (ofFullName.startsWith(QLatin1Char('/')) && !ofFullName.contains(QStringLiteral(".."))) {
                    const QString candidateOfNode = QDir(canonicalTreeRoot).filePath(ofFullName.mid(1));
                    if (QFileInfo(candidateOfNode).exists()) {
                        canonicalOfNode = canonicalOrCleanPath(candidateOfNode);
                        dtPath = pathBelowRoot(canonicalOfNode, canonicalTreeRoot);
                    }
                }
            }
            if (!dtPath.isEmpty()) {
                evidence.devicePath = current;
                evidence.ofNodePath = ofNodePath;
                evidence.canonicalOfNodePath = canonicalOfNode;
                evidence.dtPath = dtPath;
                evidence.compatible = nulDelimitedStrings(readBytes(QDir(canonicalOfNode).filePath(QStringLiteral("compatible"))));
                evidence.driver = driverName(current);
                return evidence;
            }
        }

        const QString parent = QFileInfo(current).dir().absolutePath();
        if (parent == current) {
            break;
        }
        current = parent;
    }
    return evidence;
}

bool unsafeNameContains(const QString &value, const QString &expression)
{
    return QRegularExpression(expression, QRegularExpression::CaseInsensitiveOption).match(value).hasMatch();
}

} // namespace

KeyDeviceDiscovery::KeyDeviceDiscovery()
    : m_inputClassRoot(defaultInputClassRoot())
    , m_inputDeviceRoot(defaultInputDeviceRoot())
    , m_deviceTreeRoot(defaultDeviceTreeRoot())
{
}

QString KeyDeviceDiscovery::inputClassRoot() const
{
    return m_inputClassRoot;
}

QString KeyDeviceDiscovery::inputDeviceRoot() const
{
    return m_inputDeviceRoot;
}

QString KeyDeviceDiscovery::deviceTreeRoot() const
{
    return m_deviceTreeRoot;
}

QString KeyDeviceDiscovery::defaultInputClassRoot()
{
    return testRootOrDefault("BOARDLAB_KEY_INPUT_CLASS_ROOT", QStringLiteral("/sys/class/input"));
}

QString KeyDeviceDiscovery::defaultInputDeviceRoot()
{
    return testRootOrDefault("BOARDLAB_KEY_INPUT_DEVICE_ROOT", QStringLiteral("/dev/input"));
}

QString KeyDeviceDiscovery::defaultDeviceTreeRoot()
{
    return testRootOrDefault("BOARDLAB_KEY_DEVICE_TREE_ROOT", QStringLiteral("/sys/firmware/devicetree/base"));
}

QVariantList KeyDeviceDiscovery::discover() const
{
    QVariantList candidates;
    const QDir inputClass(m_inputClassRoot);
    const QFileInfoList entries = inputClass.entryInfoList(QStringList() << QStringLiteral("event*"),
                                                           QDir::Dirs | QDir::NoDotAndDotDot,
                                                           QDir::Name);
    QSet<QString> seenDeviceIds;

    for (const QFileInfo &entry : entries) {
        static const QRegularExpression eventNameExpression(QStringLiteral("^event[0-9]+$"));
        if (!eventNameExpression.match(entry.fileName()).hasMatch()) {
            continue;
        }

        const QString eventDevicePath = QDir(entry.absoluteFilePath()).filePath(QStringLiteral("device"));
        const QString inputDevicePath = canonicalOrCleanPath(eventDevicePath);
        if (!QFileInfo(inputDevicePath).exists()) {
            continue;
        }
        const DeviceTreeEvidence tree = findDeviceTreeEvidence(inputDevicePath, m_deviceTreeRoot);
        if (tree.dtPath.isEmpty() || !hasAllowedCompatible(tree.compatible) || seenDeviceIds.contains(tree.dtPath)) {
            continue;
        }

        const QString eventPath = QDir(m_inputDeviceRoot).filePath(entry.fileName());
        const QString sysfsName = readText(QDir(inputDevicePath).filePath(QStringLiteral("name")));
        const QString inputUevent = readText(QDir(inputDevicePath).filePath(QStringLiteral("uevent")));
        const QVariantMap runtime = runtimeInputEvidence(eventPath, inputDevicePath, sysfsName, inputUevent);
        const QString inputName = runtime.value(QStringLiteral("inputName")).toString();
        const QString combinedIdentity = QStringList{
            inputName,
            tree.devicePath,
            runtime.value(QStringLiteral("inputBus")).toString()
        }.join(QLatin1Char(' '));

        QVariantMap candidate;
        candidate.insert(QStringLiteral("deviceId"), tree.dtPath);
        candidate.insert(QStringLiteral("dtPath"), tree.dtPath);
        candidate.insert(QStringLiteral("eventName"), entry.fileName());
        candidate.insert(QStringLiteral("eventPath"), eventPath);
        candidate.insert(QStringLiteral("name"), inputName);
        candidate.insert(QStringLiteral("inputName"), inputName);
        candidate.insert(QStringLiteral("kind"), QStringLiteral("key"));
        candidate.insert(QStringLiteral("boardInput"), true);
        candidate.insert(QStringLiteral("source"), QStringLiteral("device-tree"));
        candidate.insert(QStringLiteral("compatible"), tree.compatible.join(QLatin1Char(' ')));
        candidate.insert(QStringLiteral("compatibleList"), tree.compatible);
        candidate.insert(QStringLiteral("driver"), tree.driver);
        candidate.insert(QStringLiteral("keyCodes"), keyCodesFromDeviceTree(tree.canonicalOfNodePath));
        candidate.insert(QStringLiteral("sysfsPath"), tree.devicePath);
        candidate.insert(QStringLiteral("ofNodePath"), tree.canonicalOfNodePath);
        candidate.insert(QStringLiteral("inputSysfsPath"), inputDevicePath);
        candidate.insert(QStringLiteral("runtime"), runtime);
        candidate.insert(QStringLiteral("bus"), runtime.value(QStringLiteral("inputBus")));
        candidate.insert(QStringLiteral("readable"), runtime.value(QStringLiteral("readable")));
        candidate.insert(QStringLiteral("isUsb"), runtime.value(QStringLiteral("inputBus")).toString() == QStringLiteral("usb")
                         || unsafeNameContains(combinedIdentity, QStringLiteral("(?:^|[^a-z])usb(?:[^a-z]|$)|\\bhid\\b")));
        candidate.insert(QStringLiteral("isKeyboard"), unsafeNameContains(combinedIdentity, QStringLiteral("keyboard")));
        candidate.insert(QStringLiteral("isHeadset"), unsafeNameContains(combinedIdentity, QStringLiteral("headset")));
        candidate.insert(QStringLiteral("isPower"), unsafeNameContains(combinedIdentity, QStringLiteral("pwrkey|power")));
        if (testModeEnabled()) {
            candidate.insert(QStringLiteral("testMode"), true);
        }

        // Keep the raw discovery output useful to a UI while still proving
        // that the same row is accepted by the monitor's shared policy.
        QString safetyReason;
        candidate.insert(QStringLiteral("eligible"), KeyMonitor::isSafeCandidate(candidate, &safetyReason));
        candidate.insert(QStringLiteral("blockedReason"), safetyReason);
        candidates.append(candidate);
        seenDeviceIds.insert(tree.dtPath);
    }
    return candidates;
}

QVariantList KeyDeviceDiscovery::discoverSafeCandidates() const
{
    return KeyMonitor::filterSafeCandidates(discover());
}
