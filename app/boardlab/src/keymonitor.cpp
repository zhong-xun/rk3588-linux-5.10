#include "keymonitor.h"

#include <QDateTime>
#include <QRegularExpression>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr int kMaximumEvents = 80;
constexpr int kPollIntervalMs = 20;

QString candidateString(const QVariantMap &candidate, const QStringList &keys)
{
    for (const QString &key : keys) {
        const QString value = candidate.value(key).toString().trimmed();
        if (!value.isEmpty()) {
            return value;
        }
    }
    return QString();
}

bool boolCandidateValue(const QVariantMap &candidate, const QStringList &keys)
{
    for (const QString &key : keys) {
        if (candidate.contains(key) && candidate.value(key).toBool()) {
            return true;
        }
    }
    return false;
}

bool bitIsSet(const QByteArray &bits, int bit)
{
    if (bit < 0) {
        return false;
    }
    const int byteIndex = bit / 8;
    if (byteIndex < 0 || byteIndex >= bits.size()) {
        return false;
    }
    return (static_cast<unsigned char>(bits.at(byteIndex)) & (1U << (bit % 8))) != 0;
}

QString eventValueName(int value)
{
    if (value == 1) {
        return QStringLiteral("press");
    }
    if (value == 0) {
        return QStringLiteral("release");
    }
    if (value == 2) {
        return QStringLiteral("repeat");
    }
    return QStringLiteral("unknown");
}

const QSet<int> &unsafeSystemKeyCodes()
{
    static const QSet<int> codes = []() {
        QSet<int> result;
        result.insert(KEY_POWER);
#ifdef KEY_RESTART
        result.insert(KEY_RESTART);
#endif
#ifdef KEY_POWER2
        result.insert(KEY_POWER2);
#endif
#ifdef KEY_SLEEP
        result.insert(KEY_SLEEP);
#endif
#ifdef KEY_SUSPEND
        result.insert(KEY_SUSPEND);
#endif
#ifdef KEY_WAKEUP
        result.insert(KEY_WAKEUP);
#endif
#ifdef KEY_SYSRQ
        result.insert(KEY_SYSRQ);
#endif
#ifdef KEY_SCREENLOCK
        result.insert(KEY_SCREENLOCK);
#elif defined(KEY_COFFEE)
        result.insert(KEY_COFFEE);
#endif
#ifdef KEY_SCREENSAVER
        result.insert(KEY_SCREENSAVER);
#endif
#ifdef KEY_DISPLAY_OFF
        result.insert(KEY_DISPLAY_OFF);
#endif
#ifdef KEY_DISPLAYTOGGLE
        result.insert(KEY_DISPLAYTOGGLE);
#endif
#ifdef KEY_RFKILL
        result.insert(KEY_RFKILL);
#endif
#ifdef KEY_LOGOFF
        result.insert(KEY_LOGOFF);
#endif
        return result;
    }();
    return codes;
}

bool inputNodeKinds(const QString &path, bool *isCharacterDevice, bool *isFifo, QString *errorText)
{
    if (isCharacterDevice) {
        *isCharacterDevice = false;
    }
    if (isFifo) {
        *isFifo = false;
    }

    struct stat nodeStat = {};
    const QByteArray encodedPath = path.toLocal8Bit();
    if (::stat(encodedPath.constData(), &nodeStat) != 0) {
        if (errorText) {
            *errorText = QString::fromLocal8Bit(std::strerror(errno));
        }
        return false;
    }
    if (isCharacterDevice) {
        *isCharacterDevice = S_ISCHR(nodeStat.st_mode);
    }
    if (isFifo) {
        *isFifo = S_ISFIFO(nodeStat.st_mode);
    }
    return true;
}

} // namespace

KeyMonitor::KeyMonitor(QObject *parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &KeyMonitor::pollInput);
    setStatus(QStringLiteral("Ready"));
}

KeyMonitor::~KeyMonitor()
{
    stop();
}

bool KeyMonitor::active() const
{
    return m_fd >= 0;
}

QString KeyMonitor::activeDeviceId() const
{
    return m_activeDeviceId;
}

QString KeyMonitor::activeEventPath() const
{
    return active() ? candidateEventPath(m_candidate) : QString();
}

QVariantList KeyMonitor::events() const
{
    return m_events;
}

QVariantMap KeyMonitor::state() const
{
    QVariantMap result;
    result.insert(QStringLiteral("active"), active());
    result.insert(QStringLiteral("deviceId"), m_activeDeviceId);
    result.insert(QStringLiteral("candidateName"), m_candidate.value(QStringLiteral("name")));
    result.insert(QStringLiteral("status"), m_status);
    result.insert(QStringLiteral("lastError"), m_lastError);
    result.insert(QStringLiteral("eventCount"), m_events.size());
    result.insert(QStringLiteral("passed"), testPassed());

    QVariantList keys;
    const QList<int> sortedCodes = m_expectedCodes.values();
    QList<int> orderedCodes = sortedCodes;
    std::sort(orderedCodes.begin(), orderedCodes.end());
    for (int code : orderedCodes) {
        QVariantMap key;
        key.insert(QStringLiteral("code"), code);
        key.insert(QStringLiteral("name"), friendlyKeyName(code));
        key.insert(QStringLiteral("pressed"), m_seenPresses.contains(code));
        key.insert(QStringLiteral("released"), m_seenReleases.contains(code));
        key.insert(QStringLiteral("complete"), m_seenPresses.contains(code) && m_seenReleases.contains(code));
        keys.append(key);
    }
    result.insert(QStringLiteral("expectedKeys"), keys);
    return result;
}

QString KeyMonitor::lastError() const
{
    return m_lastError;
}

QString KeyMonitor::candidateDeviceId(const QVariantMap &candidate)
{
    return candidateString(candidate, {QStringLiteral("deviceId"), QStringLiteral("dtPath")});
}

QString KeyMonitor::candidateEventPath(const QVariantMap &candidate)
{
    return candidateString(candidate, {QStringLiteral("eventPath"), QStringLiteral("event")});
}

QSet<int> KeyMonitor::candidateKeyCodes(const QVariantMap &candidate)
{
    QSet<int> codes;
    const QStringList listKeys = {
        QStringLiteral("keyCodes"),
        QStringLiteral("codes"),
        QStringLiteral("supportedKeys")
    };

    for (const QString &listKey : listKeys) {
        const QVariant value = candidate.value(listKey);
        if (!value.isValid()) {
            continue;
        }
        const QVariantList entries = value.toList();
        for (const QVariant &entry : entries) {
            bool ok = false;
            int code = -1;
            if (entry.canConvert<QVariantMap>()) {
                const QVariantMap map = entry.toMap();
                code = map.value(QStringLiteral("code")).toInt(&ok);
            } else {
                code = entry.toInt(&ok);
            }
            if (ok && code >= 0 && code <= KEY_MAX) {
                codes.insert(code);
            }
        }
    }
    return codes;
}

bool KeyMonitor::isSafeKeyCode(int keyCode)
{
    if (keyCode <= KEY_RESERVED || keyCode > KEY_MAX) {
        return false;
    }
#ifdef KEY_UNKNOWN
    if (keyCode == KEY_UNKNOWN) {
        return false;
    }
#endif
    return !unsafeSystemKeyCodes().contains(keyCode);
}

bool KeyMonitor::hasUnsafeKeyCode(const QSet<int> &codes)
{
    const QSet<int> &unsafeCodes = unsafeSystemKeyCodes();
    for (int code : codes) {
        if (unsafeCodes.contains(code)) {
            return true;
        }
    }
    return false;
}

QString KeyMonitor::friendlyKeyName(int keyCode)
{
    if (keyCode >= KEY_F1 && keyCode <= KEY_F10) {
        return QStringLiteral("F%1").arg(keyCode - KEY_F1 + 1);
    }
    if (keyCode >= KEY_F11 && keyCode <= KEY_F12) {
        return QStringLiteral("F%1").arg(keyCode - KEY_F11 + 11);
    }
#if defined(KEY_F13) && defined(KEY_F24)
    if (keyCode >= KEY_F13 && keyCode <= KEY_F24) {
        return QStringLiteral("F%1").arg(keyCode - KEY_F13 + 13);
    }
#endif
#if defined(BTN_0) && defined(BTN_9)
    if (keyCode >= BTN_0 && keyCode <= BTN_9) {
        return QStringLiteral("按钮 %1").arg(keyCode - BTN_0);
    }
#endif

    switch (keyCode) {
    case KEY_VOLUMEDOWN:
        return QStringLiteral("音量-");
    case KEY_VOLUMEUP:
        return QStringLiteral("音量+");
    case KEY_MENU:
        return QStringLiteral("菜单");
    case KEY_BACK:
        return QStringLiteral("返回");
    case KEY_HOME:
        return QStringLiteral("主页");
    case KEY_ENTER:
        return QStringLiteral("确认");
    case KEY_UP:
        return QStringLiteral("上");
    case KEY_DOWN:
        return QStringLiteral("下");
    case KEY_LEFT:
        return QStringLiteral("左");
    case KEY_RIGHT:
        return QStringLiteral("右");
    case KEY_MUTE:
        return QStringLiteral("静音");
    case KEY_PLAYPAUSE:
        return QStringLiteral("播放/暂停");
    case KEY_PAUSE:
        return QStringLiteral("暂停");
#ifdef KEY_PLAY
    case KEY_PLAY:
        return QStringLiteral("播放");
#endif
    case KEY_NEXTSONG:
        return QStringLiteral("下一曲");
    case KEY_PREVIOUSSONG:
        return QStringLiteral("上一曲");
    case KEY_STOP:
        return QStringLiteral("停止");
    case KEY_CAMERA:
        return QStringLiteral("相机");
#ifdef KEY_CAMERA_FOCUS
    case KEY_CAMERA_FOCUS:
        return QStringLiteral("相机对焦");
#endif
#ifdef KEY_OK
    case KEY_OK:
        return QStringLiteral("确认");
#endif
#ifdef KEY_SELECT
    case KEY_SELECT:
        return QStringLiteral("选择");
#endif
#ifdef KEY_HOMEPAGE
    case KEY_HOMEPAGE:
        return QStringLiteral("主页");
#endif
#ifdef KEY_PROG1
    case KEY_PROG1:
        return QStringLiteral("程序键 1");
#endif
#ifdef KEY_PROG2
    case KEY_PROG2:
        return QStringLiteral("程序键 2");
#endif
#ifdef KEY_PROG3
    case KEY_PROG3:
        return QStringLiteral("程序键 3");
#endif
#ifdef KEY_PROG4
    case KEY_PROG4:
        return QStringLiteral("程序键 4");
#endif
    default:
        return QStringLiteral("KEY_%1").arg(keyCode);
    }
}

bool KeyMonitor::isSafeCandidate(const QVariantMap &candidate, QString *reason)
{
    auto reject = [reason](const QString &message) {
        if (reason) {
            *reason = message;
        }
        return false;
    };

    const QString deviceId = candidateDeviceId(candidate);
    const QString dtPath = candidateString(candidate, {QStringLiteral("dtPath")});
    const QString eventPath = candidateEventPath(candidate);
    const QString source = candidateString(candidate, {QStringLiteral("source")}).toLower();
    const QString compatible = candidateString(candidate, {QStringLiteral("compatible"), QStringLiteral("dtCompatible")}).toLower();
    const QString driver = candidateString(candidate, {QStringLiteral("driver")}).toLower();
    const QString name = candidateString(candidate, {QStringLiteral("name")}).toLower();
    const QString bus = candidateString(candidate, {QStringLiteral("bus"), QStringLiteral("busType")}).toLower();
    const QString physicalPath = candidateString(candidate, {QStringLiteral("physicalPath"), QStringLiteral("sysfsPath")}).toLower();
    const QString evidence = QStringList{deviceId, dtPath, eventPath, name, driver, bus, physicalPath}.join(QLatin1Char(' ')).toLower();
    const bool testMode = candidate.value(QStringLiteral("testMode")).toBool();

    if (deviceId.isEmpty() || dtPath.isEmpty() || !deviceId.startsWith(QLatin1Char('/'))
        || !dtPath.startsWith(QLatin1Char('/')) || deviceId.contains(QStringLiteral(".."))
        || dtPath.contains(QStringLiteral("..")) || deviceId != dtPath) {
        return reject(QStringLiteral("candidate must use one absolute device-tree deviceId/dtPath"));
    }
    if (eventPath.isEmpty()) {
        return reject(QStringLiteral("candidate has no discovered event path"));
    }
    if (source != QStringLiteral("device-tree")) {
        return reject(QStringLiteral("candidate is not backed by device-tree discovery"));
    }
    if (!compatible.contains(QStringLiteral("adc-keys")) && !compatible.contains(QStringLiteral("gpio-keys"))) {
        return reject(QStringLiteral("candidate device-tree compatible is not adc-keys/gpio-keys"));
    }
    if (!driver.isEmpty() && !driver.contains(QStringLiteral("adc_keys"))
        && !driver.contains(QStringLiteral("adc-keys")) && !driver.contains(QStringLiteral("gpio-keys"))) {
        return reject(QStringLiteral("candidate driver is not an approved key driver"));
    }
    if (boolCandidateValue(candidate, {QStringLiteral("isPower"), QStringLiteral("isHeadset"), QStringLiteral("isUsb"), QStringLiteral("isKeyboard")})) {
        return reject(QStringLiteral("candidate is explicitly marked as unsafe input"));
    }
    static const QRegularExpression unsafeInput(QStringLiteral("(?:pwrkey|power|headset|usb|hid|keyboard|mouse|gaming|consumer|remote)"));
    if (unsafeInput.match(evidence).hasMatch()) {
        return reject(QStringLiteral("candidate identifies a power, headset, USB, or keyboard input"));
    }
    if (!testMode) {
        static const QRegularExpression eventPathExpression(QStringLiteral("^/dev/input/event[0-9]+$"));
        if (!eventPathExpression.match(eventPath).hasMatch()) {
            return reject(QStringLiteral("candidate event path is not a canonical input event node"));
        }
    }

    const QSet<int> codes = candidateKeyCodes(candidate);
    if (hasUnsafeKeyCode(codes)) {
        return reject(QStringLiteral("candidate exposes a system lifecycle key"));
    }
    if (!codes.isEmpty()) {
        bool hasSafeKey = false;
        for (int code : codes) {
            hasSafeKey = hasSafeKey || isSafeKeyCode(code);
        }
        if (!hasSafeKey) {
            return reject(QStringLiteral("candidate does not expose a safe user key"));
        }
    }

    if (reason) {
        reason->clear();
    }
    return true;
}

QVariantList KeyMonitor::filterSafeCandidates(const QVariantList &candidates)
{
    QVariantList result;
    for (const QVariant &entry : candidates) {
        const QVariantMap candidate = entry.toMap();
        QString reason;
        if (!isSafeCandidate(candidate, &reason)) {
            continue;
        }
        QVariantMap safeCandidate = candidate;
        safeCandidate.insert(QStringLiteral("eligible"), true);
        safeCandidate.remove(QStringLiteral("blockedReason"));
        result.append(safeCandidate);
    }
    return result;
}

bool KeyMonitor::start(const QString &deviceId, const QVariantList &discoveryCandidates)
{
    const QString requestedId = deviceId.trimmed();
    if (requestedId.isEmpty() || !requestedId.startsWith(QLatin1Char('/')) || requestedId.contains(QStringLiteral(".."))) {
        setError(QStringLiteral("A device-tree deviceId is required to start key monitoring"));
        return false;
    }

    QVariantMap selected;
    int matchCount = 0;
    for (const QVariant &entry : discoveryCandidates) {
        const QVariantMap candidate = entry.toMap();
        QString reason;
        if (!isSafeCandidate(candidate, &reason) || candidateDeviceId(candidate) != requestedId) {
            continue;
        }
        selected = candidate;
        ++matchCount;
    }
    if (matchCount != 1) {
        setError(matchCount == 0
                 ? QStringLiteral("No safe device-tree key candidate matches the requested deviceId")
                 : QStringLiteral("The requested deviceId is ambiguous in the discovery snapshot"));
        return false;
    }

    stop();
    clear();
    m_expectedCodes.clear();
    m_candidate.clear();
    m_activeDeviceId.clear();

    const QString eventPath = candidateEventPath(selected);
    bool characterDevice = false;
    bool fifo = false;
    QString nodeTypeError;
    if (!inputNodeKinds(eventPath, &characterDevice, &fifo, &nodeTypeError)) {
        setError(QStringLiteral("Unable to inspect discovered input node: %1").arg(nodeTypeError));
        return false;
    }
    m_fifoTestDevice = selected.value(QStringLiteral("testMode")).toBool() && fifo;
    if (!m_fifoTestDevice && !characterDevice) {
        setError(QStringLiteral("Discovered input event node is not a character device"));
        return false;
    }

    const QByteArray encodedPath = eventPath.toLocal8Bit();
    m_fd = ::open(encodedPath.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    if (m_fd < 0) {
        setError(QStringLiteral("Unable to open discovered key input: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    if (!m_fifoTestDevice) {
        QString identityError;
        if (!validateRuntimeInput(&identityError)) {
            ::close(m_fd);
            m_fd = -1;
            setError(identityError);
            return false;
        }
    }

    QSet<int> discoveredCodes;
    QString capabilityError;
    const bool capabilityQuerySucceeded = queryEvKeyCapabilities(&discoveredCodes, &capabilityError);
    if (!capabilityQuerySucceeded && !m_fifoTestDevice) {
        ::close(m_fd);
        m_fd = -1;
        setError(capabilityError.isEmpty() ? QStringLiteral("Unable to validate EV_KEY capability") : capabilityError);
        return false;
    }

    const QSet<int> candidateCodes = candidateKeyCodes(selected);
    if (capabilityQuerySucceeded && hasUnsafeKeyCode(discoveredCodes)) {
        ::close(m_fd);
        m_fd = -1;
        setError(QStringLiteral("Discovered input exposes a system lifecycle key and was rejected"));
        return false;
    }
    const QSet<int> availableCodes = capabilityQuerySucceeded ? discoveredCodes : candidateCodes;
    for (int code : availableCodes) {
        if (isSafeKeyCode(code)) {
            m_expectedCodes.insert(code);
        }
    }
    if (m_expectedCodes.isEmpty()) {
        ::close(m_fd);
        m_fd = -1;
        setError(QStringLiteral("Discovered input has no safe user keys"));
        return false;
    }

    m_candidate = selected;
    m_activeDeviceId = requestedId;
    m_lastError.clear();
    setStatus(QStringLiteral("Listening for %1 user keys").arg(m_expectedCodes.size()));
    m_pollTimer->start();
    emit activeChanged();
    emit stateChanged();
    return true;
}

void KeyMonitor::stop(const QString &statusText)
{
    const bool wasActive = active();
    if (m_pollTimer) {
        m_pollTimer->stop();
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_pendingBytes.clear();
    m_pressedCodes.clear();
    m_fifoTestDevice = false;
    if (wasActive) {
        if (!statusText.trimmed().isEmpty()) {
            setStatus(statusText.trimmed());
        } else if (m_lastError.isEmpty()) {
            setStatus(testPassed() ? QStringLiteral("Key test passed") : QStringLiteral("Key monitoring stopped"));
        }
        emit activeChanged();
    }
    emit stateChanged();
}

void KeyMonitor::clear()
{
    const bool hadEvents = !m_events.isEmpty();
    m_events.clear();
    resetProgress();
    m_lastError.clear();
    if (active()) {
        setStatus(QStringLiteral("Listening for %1 user keys").arg(m_expectedCodes.size()));
    } else if (m_status != QStringLiteral("Ready")) {
        setStatus(QStringLiteral("Ready"));
    }
    if (hadEvents) {
        emit eventsChanged();
    }
    emit stateChanged();
}

bool KeyMonitor::queryEvKeyCapabilities(QSet<int> *codes, QString *errorText) const
{
    if (codes) {
        codes->clear();
    }
    if (m_fd < 0) {
        if (errorText) {
            *errorText = QStringLiteral("Input is not open");
        }
        return false;
    }

    QByteArray eventTypes((EV_MAX / 8) + 1, '\0');
    if (::ioctl(m_fd, EVIOCGBIT(0, eventTypes.size()), eventTypes.data()) < 0) {
        if (errorText) {
            *errorText = QStringLiteral("Unable to query input event types: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
    if (!bitIsSet(eventTypes, EV_KEY)) {
        if (errorText) {
            *errorText = QStringLiteral("Discovered input does not support EV_KEY");
        }
        return false;
    }

    QByteArray keyBits((KEY_MAX / 8) + 1, '\0');
    if (::ioctl(m_fd, EVIOCGBIT(EV_KEY, keyBits.size()), keyBits.data()) < 0) {
        if (errorText) {
            *errorText = QStringLiteral("Unable to query EV_KEY capabilities: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
    if (codes) {
        for (int code = 0; code <= KEY_MAX; ++code) {
            if (bitIsSet(keyBits, code)) {
                codes->insert(code);
            }
        }
    }
    return true;
}

bool KeyMonitor::validateRuntimeInput(QString *errorText) const
{
    if (m_fd < 0) {
        if (errorText) {
            *errorText = QStringLiteral("Input is not open");
        }
        return false;
    }

    char inputName[256] = {};
    if (::ioctl(m_fd, EVIOCGNAME(sizeof(inputName)), inputName) < 0) {
        if (errorText) {
            *errorText = QStringLiteral("Unable to query input name: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }
    input_id inputId = {};
    if (::ioctl(m_fd, EVIOCGID, &inputId) < 0) {
        if (errorText) {
            *errorText = QStringLiteral("Unable to query input bus: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
        }
        return false;
    }

    const QString actualName = QString::fromLocal8Bit(inputName).trimmed().toLower();
    static const QRegularExpression unsafeInput(QStringLiteral("(?:pwrkey|power|headset|usb|hid|keyboard|mouse|gaming|consumer|remote)"));
    if (inputId.bustype == BUS_USB || unsafeInput.match(actualName).hasMatch()) {
        if (errorText) {
            *errorText = QStringLiteral("Runtime input identity is not a safe board user-key device");
        }
        return false;
    }
    return true;
}

void KeyMonitor::pollInput()
{
    if (m_fd < 0) {
        return;
    }

    char chunk[sizeof(input_event) * 32];
    for (;;) {
        const ssize_t count = ::read(m_fd, chunk, sizeof(chunk));
        if (count > 0) {
            m_pendingBytes.append(chunk, static_cast<int>(count));
            continue;
        }
        if (count == 0) {
            // A FIFO used by a unit test has no writer between test writes.
            // Real evdev nodes do not use EOF as their idle indication.
            if (!m_fifoTestDevice) {
                setError(QStringLiteral("Key input node closed while monitoring"));
                stop();
            }
            break;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            setError(QStringLiteral("Unable to read key input: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
            stop();
        }
        break;
    }

    const int eventSize = static_cast<int>(sizeof(input_event));
    int offset = 0;
    while (m_pendingBytes.size() - offset >= eventSize) {
        input_event event = {};
        std::memcpy(&event, m_pendingBytes.constData() + offset, sizeof(event));
        offset += eventSize;
        if (event.type == EV_KEY) {
            processInputEvent(event.code, event.value);
        }
    }
    if (offset > 0) {
        m_pendingBytes.remove(0, offset);
    }
}

void KeyMonitor::processInputEvent(int keyCode, int value)
{
    if (!m_expectedCodes.contains(keyCode)) {
        return;
    }
    bool counted = false;
    if (value == 1) {
        m_pressedCodes.insert(keyCode);
        m_seenPresses.insert(keyCode);
        counted = true;
    } else if (value == 0) {
        if (m_pressedCodes.contains(keyCode)) {
            m_pressedCodes.remove(keyCode);
            m_seenReleases.insert(keyCode);
            counted = true;
        }
    }

    appendEvent(keyCode, value, counted);
    if (testPassed()) {
        setStatus(QStringLiteral("All user keys completed press/release validation"));
    } else {
        int completed = 0;
        for (int code : m_expectedCodes) {
            if (m_seenPresses.contains(code) && m_seenReleases.contains(code)) {
                ++completed;
            }
        }
        setStatus(QStringLiteral("Validated %1/%2 user keys").arg(completed).arg(m_expectedCodes.size()));
    }
    emit stateChanged();
}

void KeyMonitor::appendEvent(int keyCode, int value, bool counted)
{
    QVariantMap event;
    event.insert(QStringLiteral("deviceId"), m_activeDeviceId);
    event.insert(QStringLiteral("code"), keyCode);
    event.insert(QStringLiteral("key"), friendlyKeyName(keyCode));
    event.insert(QStringLiteral("state"), eventValueName(value));
    event.insert(QStringLiteral("value"), value);
    event.insert(QStringLiteral("counted"), counted);
    event.insert(QStringLiteral("timestampMs"), QDateTime::currentMSecsSinceEpoch());
    event.insert(QStringLiteral("timestamp"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    m_events.append(event);
    while (m_events.size() > kMaximumEvents) {
        m_events.removeFirst();
    }
    emit eventsChanged();
    emit eventCaptured(event);
}

void KeyMonitor::resetProgress()
{
    m_pressedCodes.clear();
    m_seenPresses.clear();
    m_seenReleases.clear();
}

void KeyMonitor::setError(const QString &errorText)
{
    m_lastError = errorText;
    m_status = errorText;
    emit stateChanged();
}

void KeyMonitor::setStatus(const QString &statusText)
{
    if (m_status == statusText) {
        return;
    }
    m_status = statusText;
    emit stateChanged();
}

bool KeyMonitor::testPassed() const
{
    if (m_expectedCodes.isEmpty()) {
        return false;
    }
    for (int code : m_expectedCodes) {
        if (!m_seenPresses.contains(code) || !m_seenReleases.contains(code)) {
            return false;
        }
    }
    return true;
}
