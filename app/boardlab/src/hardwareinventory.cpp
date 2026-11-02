#include "hardwareinventory.h"

#include "qtcompat.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

namespace {

QString rootPath(const QString &root, const QString &absolutePath)
{
    const QString normalizedRoot = QDir::cleanPath(root);
    if (normalizedRoot == QStringLiteral("/")) {
        return QDir::cleanPath(absolutePath);
    }
    return QDir(normalizedRoot).filePath(absolutePath.mid(1));
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    QString value = QString::fromUtf8(file.readAll());
    value.remove(QChar('\0'));
    return value.trimmed();
}

QFileInfoList entries(const QString &path, const QStringList &patterns = QStringList())
{
    const QDir directory(path);
    return directory.entryInfoList(patterns,
                                   QDir::Dirs | QDir::Files | QDir::System
                                       | QDir::NoDotAndDotDot,
                                   QDir::Name);
}

void appendEndpoint(QStringList *endpoints, const QString &path)
{
    if (!path.isEmpty() && !endpoints->contains(path)) {
        endpoints->append(path);
    }
}

bool anyWritable(const QStringList &paths)
{
    for (const QString &path : paths) {
        if (QFileInfo(path).isWritable()) {
            return true;
        }
    }
    return false;
}

void publish(HardwareInventory::Snapshot *snapshot, const QString &name,
             const QString &provider, const QStringList &endpoints,
             const QString &unavailableReason = QString(), bool usable = true)
{
    const bool discovered = !endpoints.isEmpty();
    QVariantMap state;
    state.insert(QStringLiteral("discovered"), discovered);
    state.insert(QStringLiteral("usable"), discovered && usable);
    state.insert(QStringLiteral("writable"), discovered && anyWritable(endpoints));
    state.insert(QStringLiteral("provider"), provider);
    state.insert(QStringLiteral("endpoints"), endpoints);
    state.insert(QStringLiteral("reason"), discovered
                 ? (usable ? QString() : unavailableReason)
                 : unavailableReason);
    snapshot->capabilities.insert(name, discovered && usable);
    snapshot->states.insert(name, state);
}

QStringList deviceNodes(const QString &devRoot, const QStringList &patterns)
{
    QStringList result;
    for (const QFileInfo &entry : entries(devRoot, patterns)) {
        appendEndpoint(&result, entry.absoluteFilePath());
    }
    return result;
}

bool isMetadataVideoName(const QString &name)
{
    const QString lower = name.toLower();
    return lower.contains(QStringLiteral("metadata"))
        || lower.contains(QStringLiteral("statistics"))
        || lower.contains(QStringLiteral("stats"))
        || lower.contains(QStringLiteral("params"))
        || lower.contains(QStringLiteral("rawrd"))
        || lower.contains(QStringLiteral("selfpath"));
}

bool isHdmiInputName(const QString &name)
{
    const QString lower = name.toLower();
    return lower.contains(QStringLiteral("hdmirx"))
        || lower.contains(QStringLiteral("hdmi-rx"))
        || lower.contains(QStringLiteral("hdmi rx"));
}

bool isWirelessInterface(const QString &path)
{
    if (QFileInfo::exists(QDir(path).filePath(QStringLiteral("wireless")))) {
        return true;
    }
    return readText(QDir(path).filePath(QStringLiteral("uevent")))
        .contains(QStringLiteral("DEVTYPE=wlan"));
}

bool isPhysicalNetworkInterface(const QString &path)
{
    return QFileInfo::exists(QDir(path).filePath(QStringLiteral("device")))
        || QFileInfo(QDir(path).filePath(QStringLiteral("device"))).isSymLink();
}

bool iioAttributeExists(const QDir &directory, const QString &name)
{
    const QFileInfo attribute(directory.filePath(name));
    return attribute.exists() || attribute.isSymLink();
}

bool hasIioChannel(const QDir &directory, const QString &typeStem)
{
    if (!directory.entryInfoList({typeStem + QStringLiteral("*_input")},
                                 QDir::Files | QDir::System, QDir::Name).isEmpty()) {
        return true;
    }

    const QString rawSuffix = QStringLiteral("_raw");
    const QFileInfoList rawChannels = directory.entryInfoList(
        {typeStem + QStringLiteral("*_raw")}, QDir::Files | QDir::System, QDir::Name);
    for (const QFileInfo &rawChannel : rawChannels) {
        const QString channelStem = rawChannel.fileName().left(
            rawChannel.fileName().size() - rawSuffix.size());
        if (iioAttributeExists(directory, channelStem + QStringLiteral("_scale"))
            || iioAttributeExists(directory, typeStem + QStringLiteral("_scale"))) {
            return true;
        }
    }
    return false;
}

QStringList findIioPairs(const QString &iioRoot)
{
    QStringList endpoints;
    for (const QFileInfo &device : entries(iioRoot, {QStringLiteral("iio:device*")})) {
        const QDir directory(device.absoluteFilePath());
        if (hasIioChannel(directory, QStringLiteral("in_temp"))
            && (hasIioChannel(directory, QStringLiteral("in_humidityrelative"))
                || hasIioChannel(directory, QStringLiteral("in_humidity")))) {
            appendEndpoint(&endpoints, device.absoluteFilePath());
        }
    }
    return endpoints;
}

QStringList findPwmFans(const QString &hwmonRoot)
{
    QStringList endpoints;
    const QRegularExpression pwmExpression(QStringLiteral("^pwm[0-9]+$"));
    for (const QFileInfo &device : entries(hwmonRoot, {QStringLiteral("hwmon*")})) {
        for (const QFileInfo &attribute : entries(device.absoluteFilePath(),
                                                   {QStringLiteral("pwm*")})) {
            if (pwmExpression.match(attribute.fileName()).hasMatch()) {
                appendEndpoint(&endpoints, attribute.absoluteFilePath());
            }
        }
    }
    return endpoints;
}

} // namespace

HardwareInventory::Snapshot HardwareInventory::discover(const QString &sysRoot,
                                                        const QString &devRoot)
{
    Snapshot snapshot;
    const QString sys = QDir::cleanPath(sysRoot.isEmpty() ? QStringLiteral("/sys") : sysRoot);
    const QString dev = QDir::cleanPath(devRoot.isEmpty() ? QStringLiteral("/dev") : devRoot);

    const QString gpioClass = rootPath(sys, QStringLiteral("/class/gpio"));
    QStringList gpio = deviceNodes(dev, {QStringLiteral("gpiochip*")});
    if (QFileInfo::exists(QDir(gpioClass).filePath(QStringLiteral("export")))) {
        appendEndpoint(&gpio, QDir(gpioClass).filePath(QStringLiteral("export")));
    }
    publish(&snapshot, QStringLiteral("gpio"), QStringLiteral("linux-gpio"), gpio,
            QStringLiteral("No GPIO character device or legacy sysfs export interface was found"));

    publish(&snapshot, QStringLiteral("uart"), QStringLiteral("tty-device"),
            deviceNodes(dev, {QStringLiteral("ttyS*"), QStringLiteral("ttyAMA*"),
                              QStringLiteral("ttyTHS*"), QStringLiteral("ttySC*"),
                              QStringLiteral("ttyLP*"), QStringLiteral("ttyFIQ*"),
                              QStringLiteral("ttyUSB*"), QStringLiteral("ttyACM*")}),
            QStringLiteral("No supported TTY device node was found"));

    QStringList ethernet;
    QStringList can;
    QStringList wifi;
    QStringList cellular;
    const QString netRoot = rootPath(sys, QStringLiteral("/class/net"));
    for (const QFileInfo &interface : entries(netRoot)) {
        const QString name = interface.fileName();
        if (name == QStringLiteral("lo")) {
            continue;
        }
        const QString path = interface.absoluteFilePath();
        bool typeOk = false;
        const int type = readText(QDir(path).filePath(QStringLiteral("type"))).toInt(&typeOk);
        if (typeOk && type == 280) {
            appendEndpoint(&can, name);
        } else if (isWirelessInterface(path)) {
            appendEndpoint(&wifi, name);
        } else if (typeOk && type == 1 && isPhysicalNetworkInterface(path)) {
            appendEndpoint(&ethernet, name);
        }
        const QString lower = name.toLower();
        if (lower.startsWith(QStringLiteral("wwan"))
            || lower.startsWith(QStringLiteral("rmnet"))
            || lower.startsWith(QStringLiteral("wwp"))
            || lower.startsWith(QStringLiteral("usb"))) {
            appendEndpoint(&cellular, name);
        }
    }
    for (const QString &node : deviceNodes(dev, {QStringLiteral("cdc-wdm*"),
                                                  QStringLiteral("mhi_*"),
                                                  QStringLiteral("wwan*")})) {
        appendEndpoint(&cellular, node);
    }
    publish(&snapshot, QStringLiteral("ethernet"), QStringLiteral("sysfs-net-type"), ethernet,
            QStringLiteral("No physical ARPHRD_ETHER interface was found"));
    publish(&snapshot, QStringLiteral("can"), QStringLiteral("sysfs-net-type"), can,
            QStringLiteral("No ARPHRD_CAN interface was found"));
    publish(&snapshot, QStringLiteral("wifi"), QStringLiteral("sysfs-wireless"), wifi,
            QStringLiteral("No wireless network interface was found"));
    publish(&snapshot, QStringLiteral("cellular"), QStringLiteral("wwan-kernel-abi"), cellular,
            QStringLiteral("No WWAN control or network endpoint was found"));

    QStringList cameras;
    QStringList hdmiInputs;
    const QString videoRoot = rootPath(sys, QStringLiteral("/class/video4linux"));
    for (const QFileInfo &video : entries(videoRoot, {QStringLiteral("video*")})) {
        const QString name = readText(QDir(video.absoluteFilePath()).filePath(QStringLiteral("name")));
        const QString node = QDir(dev).filePath(video.fileName());
        if (isHdmiInputName(name)) {
            appendEndpoint(&hdmiInputs, node);
        } else if (!isMetadataVideoName(name)) {
            appendEndpoint(&cameras, node);
        }
    }
    publish(&snapshot, QStringLiteral("camera"), QStringLiteral("video4linux"), cameras,
            QStringLiteral("No V4L2 capture candidate was found"));
    publish(&snapshot, QStringLiteral("hdmiin"), QStringLiteral("video4linux"), hdmiInputs,
            QStringLiteral("No HDMI receiver capture endpoint was found"));

    publish(&snapshot, QStringLiteral("watchdog"), QStringLiteral("watchdog-device"),
            deviceNodes(dev, {QStringLiteral("watchdog*")}),
            QStringLiteral("No watchdog device node was found"));

    QStringList sound;
    const QString soundClass = rootPath(sys, QStringLiteral("/class/sound"));
    for (const QFileInfo &entry : entries(soundClass, {QStringLiteral("card*")})) {
        appendEndpoint(&sound, entry.absoluteFilePath());
    }
    publish(&snapshot, QStringLiteral("music"), QStringLiteral("alsa-sysfs"), sound,
            QStringLiteral("No ALSA card was found"));

    QStringList buzzers;
    const QString inputClass = rootPath(sys, QStringLiteral("/class/input"));
    for (const QFileInfo &event : entries(inputClass, {QStringLiteral("event*")})) {
        const QString name = readText(QDir(event.absoluteFilePath()).filePath(QStringLiteral("device/name")));
        const QString normalized = name.trimmed().toLower();
        if (normalized == QStringLiteral("pwm-beeper")
            || normalized == QStringLiteral("pwm beeper")) {
            appendEndpoint(&buzzers, QDir(dev).filePath(QStringLiteral("input/") + event.fileName()));
        }
    }
    publish(&snapshot, QStringLiteral("buzzer"), QStringLiteral("evdev-sound"), buzzers,
            QStringLiteral("No pwm-beeper EV_SND endpoint was found"));

    publish(&snapshot, QStringLiteral("fan"), QStringLiteral("hwmon-pwm"),
            findPwmFans(rootPath(sys, QStringLiteral("/class/hwmon"))),
            QStringLiteral("No hwmon PWM control was found"));

    QStringList leds;
    for (const QFileInfo &led : entries(rootPath(sys, QStringLiteral("/class/leds")))) {
        const QString brightness = QDir(led.absoluteFilePath()).filePath(QStringLiteral("brightness"));
        if (QFileInfo::exists(brightness)) {
            appendEndpoint(&leds, led.absoluteFilePath());
            snapshot.ledPaths.append(led.absoluteFilePath());
        }
    }
    publish(&snapshot, QStringLiteral("led"), QStringLiteral("led-class"), leds,
            QStringLiteral("No LED class device was found"));

    QStringList backlights;
    for (const QFileInfo &entry : entries(rootPath(sys, QStringLiteral("/class/backlight")))) {
        const QString brightness = QDir(entry.absoluteFilePath()).filePath(QStringLiteral("brightness"));
        if (QFileInfo::exists(brightness)) {
            appendEndpoint(&backlights, entry.absoluteFilePath());
        }
    }
    publish(&snapshot, QStringLiteral("backlight"), QStringLiteral("backlight-class"), backlights,
            QStringLiteral("No backlight class device was found"));

    QStringList storage;
    const QRegularExpression virtualBlock(QStringLiteral("^(loop|ram|zram)[0-9]+$"));
    for (const QFileInfo &block : entries(rootPath(sys, QStringLiteral("/class/block")))) {
        if (!virtualBlock.match(block.fileName()).hasMatch()) {
            appendEndpoint(&storage, QDir(dev).filePath(block.fileName()));
        }
    }
    publish(&snapshot, QStringLiteral("storage"), QStringLiteral("block-class"), storage,
            QStringLiteral("No non-virtual block device was found"));

    publish(&snapshot, QStringLiteral("temperaturehumidity"), QStringLiteral("iio-channels"),
            findIioPairs(rootPath(sys, QStringLiteral("/bus/iio/devices"))),
            QStringLiteral("No IIO device has both temperature and relative-humidity input or raw/scale channels"));

    QStringList keys;
    for (const QFileInfo &event : entries(inputClass, {QStringLiteral("event*")})) {
        appendEndpoint(&keys, QDir(dev).filePath(QStringLiteral("input/") + event.fileName()));
    }
    publish(&snapshot, QStringLiteral("keys"), QStringLiteral("evdev"), keys,
            QStringLiteral("No evdev input endpoint was found"));

    QStringList gpu = deviceNodes(QDir(dev).filePath(QStringLiteral("dri")),
                                  {QStringLiteral("renderD*"), QStringLiteral("card*")});
    for (const QString &node : deviceNodes(dev, {QStringLiteral("mali*"), QStringLiteral("galcore")})) {
        appendEndpoint(&gpu, node);
    }
    publish(&snapshot, QStringLiteral("gpu"), QStringLiteral("drm-render"), gpu,
            QStringLiteral("No DRM render/card or legacy GPU endpoint was found"));

    publish(&snapshot, QStringLiteral("bluetooth"), QStringLiteral("bluetooth-class"),
            [&sys]() {
                QStringList result;
                for (const QFileInfo &entry : entries(rootPath(sys, QStringLiteral("/class/bluetooth")),
                                                       {QStringLiteral("hci*")})) {
                    appendEndpoint(&result, entry.absoluteFilePath());
                }
                return result;
            }(), QStringLiteral("No Bluetooth HCI controller was found"));

    QStringList displayOutputs;
    for (const QFileInfo &connector : entries(rootPath(sys, QStringLiteral("/class/drm")))) {
        if (connector.fileName().contains(QStringLiteral("HDMI"), Qt::CaseInsensitive)
            && QFileInfo::exists(QDir(connector.absoluteFilePath()).filePath(QStringLiteral("status")))) {
            appendEndpoint(&displayOutputs, connector.absoluteFilePath());
        }
    }
    publish(&snapshot, QStringLiteral("hdmiout"), QStringLiteral("drm-connector"), displayOutputs,
            QStringLiteral("No HDMI DRM connector was found"));

    publish(&snapshot, QStringLiteral("rtc"), QStringLiteral("rtc-class"),
            deviceNodes(dev, {QStringLiteral("rtc*")}),
            QStringLiteral("No RTC device node was found"));

    QStringList npu = deviceNodes(dev, {QStringLiteral("rknpu*"), QStringLiteral("npu*")});
    for (const QFileInfo &entry : entries(rootPath(sys, QStringLiteral("/class/devfreq")))) {
        if (entry.fileName().contains(QStringLiteral("npu"), Qt::CaseInsensitive)) {
            appendEndpoint(&npu, entry.absoluteFilePath());
        }
    }
    publish(&snapshot, QStringLiteral("npu"), QStringLiteral("npu-device"), npu,
            QStringLiteral("No NPU device or devfreq endpoint was found"));

    publish(&snapshot, QStringLiteral("rs485"), QStringLiteral("board-manifest"), {},
            QStringLiteral("RS485 wiring cannot be inferred safely from a generic UART"));
    publish(&snapshot, QStringLiteral("npuyolodemo"), QStringLiteral("rknn-runtime"), {},
            QStringLiteral("The RKNN runtime and a compatible model must be probed together"));

    for (const QString &name : {QStringLiteral("files"), QStringLiteral("web"),
                                QStringLiteral("monitor"), QStringLiteral("diagnostics")}) {
        publish(&snapshot, name, QStringLiteral("application"),
                {QStringLiteral("application:") + name});
    }

    snapshot.ledPaths.removeDuplicates();
    return snapshot;
}
