#include "boardprofile.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSet>
#include <QSysInfo>

namespace {

struct BoardDefinition {
    QString id;
    QString displayName;
    QString socFamily;
    QStringList compatibleIds;
    QStringList legacyModelTokens;
    bool genericFallback = false;
    QVariantMap capabilities;
    QStringList ledPaths;
};

QStringList readDeviceTreeStrings(const QString &root, const QString &name)
{
    QFile file(QDir(root).filePath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        return QStringList();
    }
    QStringList values;
    const QList<QByteArray> rawValues = file.readAll().split('\0');
    for (const QByteArray &rawValue : rawValues) {
        const QString value = QString::fromUtf8(rawValue).trimmed();
        if (!value.isEmpty()) {
            values.append(value);
        }
    }
    return values;
}

QString readDeviceTreeValue(const QString &root, const QString &name)
{
    return readDeviceTreeStrings(root, name).join(QLatin1Char(' '));
}

bool deviceTreeNodeAndAncestorsEnabled(const QString &root, const QString &path)
{
    const QString normalizedRoot = QDir(root).absolutePath();
    QString current = QDir(path).absolutePath();
    if (current != normalizedRoot && !current.startsWith(normalizedRoot + QLatin1Char('/'))) {
        return false;
    }

    while (true) {
        const QString status = readDeviceTreeValue(current, QStringLiteral("status")).trimmed();
        if (!status.isEmpty()
            && status.compare(QStringLiteral("okay"), Qt::CaseInsensitive) != 0
            && status.compare(QStringLiteral("ok"), Qt::CaseInsensitive) != 0) {
            return false;
        }
        if (current == normalizedRoot) {
            return true;
        }
        current = QFileInfo(current).dir().absolutePath();
    }
}

QSet<QString> enabledDeviceTreeCompatibleValues(const QString &root)
{
    QSet<QString> values;
    const QDir rootDir(root);
    if (!rootDir.exists()) {
        return values;
    }

    const auto collect = [&values, &root](const QString &nodePath) {
        if (!deviceTreeNodeAndAncestorsEnabled(root, nodePath)) {
            return;
        }
        const QStringList compatibleValues = readDeviceTreeStrings(nodePath, QStringLiteral("compatible"));
        for (const QString &value : compatibleValues) {
            values.insert(value.toLower());
        }
    };

    collect(rootDir.absolutePath());
    QDirIterator iterator(rootDir.absolutePath(), QDir::Dirs | QDir::NoDotAndDotDot,
                           QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        collect(iterator.next());
    }
    return values;
}

bool hasEnabledDeviceTreeCompatible(const QSet<QString> &values, const QStringList &candidates)
{
    for (const QString &candidate : candidates) {
        if (values.contains(candidate.toLower())) {
            return true;
        }
    }
    return false;
}

void refineCapabilitiesWithDeviceTree(const BoardDefinition &definition, const QString &root,
                                      QVariantMap *capabilities, QStringList *ledPaths)
{
    if (!capabilities || !ledPaths) {
        return;
    }

    // A product compatible identifies the carrier board.  The enabled child
    // nodes describe optional/variant peripherals on that carrier, so they
    // can only remove a static capability, never grant a new one.
    const QSet<QString> compatibleValues = enabledDeviceTreeCompatibleValues(root);
    const auto restrict = [capabilities, &compatibleValues](const QString &capability,
                                                              const QStringList &requiredCompatibles) {
        if (capabilities->value(capability).toBool()
            && !hasEnabledDeviceTreeCompatible(compatibleValues, requiredCompatibles)) {
            capabilities->insert(capability, false);
        }
    };

    restrict(QStringLiteral("hdmiin"), QStringList{
        QStringLiteral("rockchip,rk3588-hdmirx-ctrler"),
        QStringLiteral("rockchip,hdmirx-ctrler")
    });
    restrict(QStringLiteral("temperaturehumidity"), QStringList{QStringLiteral("dht11")});
    restrict(QStringLiteral("led"), QStringList{
        QStringLiteral("gpio-leds"),
        QStringLiteral("pwm-leds")
    });
    if (capabilities->value(QStringLiteral("buzzer")).toBool()) {
        restrict(QStringLiteral("buzzer"), QStringList{QStringLiteral("pwm-beeper")});
    }
    if (!capabilities->value(QStringLiteral("led")).toBool()) {
        ledPaths->clear();
    }
}

QVariantMap baseCapabilities()
{
    QVariantMap capabilities;
    capabilities.insert(QStringLiteral("gpio"), true);
    capabilities.insert(QStringLiteral("uart"), true);
    capabilities.insert(QStringLiteral("ethernet"), true);
    capabilities.insert(QStringLiteral("can"), true);
    capabilities.insert(QStringLiteral("cellular"), true);
    capabilities.insert(QStringLiteral("camera"), true);
    capabilities.insert(QStringLiteral("hdmiin"), true);
    capabilities.insert(QStringLiteral("watchdog"), true);
    capabilities.insert(QStringLiteral("music"), true);
    capabilities.insert(QStringLiteral("buzzer"), false);
    capabilities.insert(QStringLiteral("fan"), true);
    capabilities.insert(QStringLiteral("led"), false);
    capabilities.insert(QStringLiteral("backlight"), true);
    capabilities.insert(QStringLiteral("storage"), true);
    capabilities.insert(QStringLiteral("files"), true);
    capabilities.insert(QStringLiteral("web"), true);
    // A fixed RKNN YOLOv8 workload is narrower than general NPU telemetry.
    // Product profiles opt in only after a matching model is bundled.
    capabilities.insert(QStringLiteral("npuyolodemo"), false);
    // These are deliberately enabled only for identified Topeet profiles.
    // Generic SoC matches do not prove that a usable graphics or Wi-Fi stack
    // is present on the carrier board.
    capabilities.insert(QStringLiteral("gpu"), true);
    capabilities.insert(QStringLiteral("wifi"), true);
    return capabilities;
}

QVariantMap genericBoardCapabilities()
{
    // An SoC compatible string alone cannot prove carrier-board wiring. Keep
    // the generic fallback useful for application access while requiring a
    // board-specific profile before it authorizes hardware control surfaces.
    QVariantMap capabilities;
    capabilities.insert(QStringLiteral("files"), true);
    capabilities.insert(QStringLiteral("web"), true);
    capabilities.insert(QStringLiteral("npuyolodemo"), false);
    return capabilities;
}

// Baseline lab surfaces that most identified Topeet carriers share. Board
// helpers below clear features that the product DTS does not enable.
QVariantMap withCommonTopeetLabCapabilities(QVariantMap capabilities)
{
    capabilities.insert(QStringLiteral("gpio"), true);
    capabilities.insert(QStringLiteral("uart"), true);
    capabilities.insert(QStringLiteral("ethernet"), true);
    capabilities.insert(QStringLiteral("camera"), true);
    capabilities.insert(QStringLiteral("watchdog"), true);
    capabilities.insert(QStringLiteral("music"), true);
    capabilities.insert(QStringLiteral("backlight"), true);
    capabilities.insert(QStringLiteral("storage"), true);
    capabilities.insert(QStringLiteral("files"), true);
    capabilities.insert(QStringLiteral("web"), true);
    capabilities.insert(QStringLiteral("keys"), true);
    // USB/SDIO Wi-Fi and BT modules are common on the lab images even when the
    // product DTS has no chip-compatible node. Keep the pages visible; runtime
    // discovery still decides readiness.
    capabilities.insert(QStringLiteral("wifi"), true);
    capabilities.insert(QStringLiteral("bluetooth"), true);
    capabilities.insert(QStringLiteral("hdmiout"), true);
    capabilities.insert(QStringLiteral("rtc"), true);
    capabilities.insert(QStringLiteral("npu"), true);
    capabilities.insert(QStringLiteral("gpu"), true);
    // These are read-only system pages, but are still product-lab surfaces:
    // a generic SoC match must not look like a verified carrier board.
    capabilities.insert(QStringLiteral("monitor"), true);
    capabilities.insert(QStringLiteral("diagnostics"), true);
    // Defaults that many boards override.
    capabilities.insert(QStringLiteral("can"), false);
    capabilities.insert(QStringLiteral("cellular"), false);
    capabilities.insert(QStringLiteral("rs485"), false);
    capabilities.insert(QStringLiteral("hdmiin"), false);
    capabilities.insert(QStringLiteral("buzzer"), false);
    // Fan availability is still determined dynamically from hwmon, but all
    // identified Topeet profiles expose the read-only/unavailable surface.
    capabilities.insert(QStringLiteral("fan"), true);
    capabilities.insert(QStringLiteral("led"), false);
    capabilities.insert(QStringLiteral("temperaturehumidity"), false);
    capabilities.insert(QStringLiteral("npuyolodemo"), false);
    return capabilities;
}

QVariantMap topeetRk3568Capabilities()
{
    // Evidence: rk3568 6.1 product DTS + Linux manual.
    // LED9 trigger/GPIO path is still not BoardLab readback-safe, so led=false.
    QVariantMap capabilities = withCommonTopeetLabCapabilities(QVariantMap());
    capabilities.insert(QStringLiteral("can"), true);
    capabilities.insert(QStringLiteral("rs485"), true);
    capabilities.insert(QStringLiteral("buzzer"), true);
    capabilities.insert(QStringLiteral("temperaturehumidity"), true);
    capabilities.insert(QStringLiteral("hdmiin"), false);
    capabilities.insert(QStringLiteral("led"), false);
    capabilities.insert(QStringLiteral("npuyolodemo"), true);
    return capabilities;
}

QVariantMap topeetRk3568MiniCapabilities()
{
    // Mini DTS drops dht11 and pwm10 beeper vs Standard; keep RS485/CAN/etc.
    QVariantMap capabilities = topeetRk3568Capabilities();
    capabilities.insert(QStringLiteral("buzzer"), false);
    capabilities.insert(QStringLiteral("temperaturehumidity"), false);
    return capabilities;
}

QVariantMap topeetRk3562Capabilities()
{
    // Leaner carrier: single eth, cameras, NPU/GPU, storage, keys. No board
    // CAN/RS485/DHT11/LED/buzzer/HDMI-IN enable in the product DTS.
    QVariantMap capabilities = withCommonTopeetLabCapabilities(QVariantMap());
    capabilities.insert(QStringLiteral("can"), false);
    capabilities.insert(QStringLiteral("rs485"), false);
    capabilities.insert(QStringLiteral("hdmiin"), false);
    capabilities.insert(QStringLiteral("buzzer"), false);
    capabilities.insert(QStringLiteral("led"), false);
    capabilities.insert(QStringLiteral("temperaturehumidity"), false);
    capabilities.insert(QStringLiteral("npuyolodemo"), true);
    return capabilities;
}

QVariantMap topeetRk3576Capabilities()
{
    // Dual eth, CAN0, DHT11, cameras, Type-C/DP default display. No RS485 helper
    // or HDMI-IN in product DTS. The bundled RK3576 model uses RKNN automatic
    // core scheduling rather than the RK3588-specific core-mask API.
    QVariantMap capabilities = withCommonTopeetLabCapabilities(QVariantMap());
    capabilities.insert(QStringLiteral("can"), true);
    capabilities.insert(QStringLiteral("temperaturehumidity"), true);
    capabilities.insert(QStringLiteral("buzzer"), true);
    capabilities.insert(QStringLiteral("led"), true);
    capabilities.insert(QStringLiteral("rs485"), false);
    capabilities.insert(QStringLiteral("hdmiin"), false);
    capabilities.insert(QStringLiteral("npuyolodemo"), true);
    return capabilities;
}

QVariantMap topeetRk3576LgaCapabilities()
{
    // The LGA carrier has one enabled GMAC and extra header UARTs, but no
    // enabled CAN, DHT11 or pwm-beeper node. It retains the PWM work LED.
    QVariantMap capabilities = withCommonTopeetLabCapabilities(QVariantMap());
    capabilities.insert(QStringLiteral("can"), false);
    capabilities.insert(QStringLiteral("temperaturehumidity"), false);
    capabilities.insert(QStringLiteral("buzzer"), false);
    capabilities.insert(QStringLiteral("led"), true);
    capabilities.insert(QStringLiteral("rs485"), false);
    capabilities.insert(QStringLiteral("hdmiin"), false);
    capabilities.insert(QStringLiteral("npuyolodemo"), true);
    return capabilities;
}

QVariantMap topeetRk3588StandardCapabilities()
{
    // Full lab carrier: dual eth, CAN, cellular, HDMI-IN/OUT, DHT11, LEDs,
    // pwm-beeper, cameras, SATA/NVMe. RK3588 and RK3588S intentionally share
    // the validated RK3588-family YOLO model.
    QVariantMap capabilities = withCommonTopeetLabCapabilities(baseCapabilities());
    capabilities.insert(QStringLiteral("can"), true);
    capabilities.insert(QStringLiteral("cellular"), true);
    capabilities.insert(QStringLiteral("hdmiin"), true);
    capabilities.insert(QStringLiteral("buzzer"), true);
    capabilities.insert(QStringLiteral("led"), true);
    capabilities.insert(QStringLiteral("temperaturehumidity"), true);
    capabilities.insert(QStringLiteral("rs485"), false);
    capabilities.insert(QStringLiteral("npuyolodemo"), true);
    return capabilities;
}

QVariantMap topeetRk3588MiniCapabilities()
{
    // Current 5.10 and 6.1 product DTS files expose PWM15 through pwm-beeper.
    QVariantMap capabilities = topeetRk3588StandardCapabilities();
    capabilities.insert(QStringLiteral("buzzer"), true);
    return capabilities;
}

QVariantMap topeetRk3588sCapabilities()
{
    // Single eth, uart5/uart9 (232/485), no DHT11/LEDs/HDMI-IN,
    // CAN disabled in product DTS, no SATA. The YOLO entry intentionally uses
    // the same RK3588-family RKNN model as the RK3588 profile. Current 5.10
    // and 6.1 product DTS files expose PWM5 through pwm-beeper.
    QVariantMap capabilities = withCommonTopeetLabCapabilities(baseCapabilities());
    capabilities.insert(QStringLiteral("can"), false);
    capabilities.insert(QStringLiteral("rs485"), true);
    capabilities.insert(QStringLiteral("hdmiin"), false);
    capabilities.insert(QStringLiteral("buzzer"), true);
    capabilities.insert(QStringLiteral("led"), false);
    capabilities.insert(QStringLiteral("temperaturehumidity"), false);
    capabilities.insert(QStringLiteral("npuyolodemo"), true);
    return capabilities;
}

QList<BoardDefinition> boardDefinitions()
{
    // 3588 Standard/Mini DTS labels work1/work2; current 6.1 lab image also
    // exposes user1/user2. Prefer the live sysfs names used on product images.
    const QStringList rk3588LedPaths{
        QStringLiteral("/sys/class/leds/user1"),
        QStringLiteral("/sys/class/leds/user2"),
        QStringLiteral("/sys/class/leds/work1"),
        QStringLiteral("/sys/class/leds/work2")
    };

    BoardDefinition topeetRk3588;
    topeetRk3588.id = QStringLiteral("topeet-rk3588");
    topeetRk3588.displayName = QStringLiteral("iTOP-RK3588 Standard");
    topeetRk3588.socFamily = QStringLiteral("RK3588");
    // Prefer the exact product compatible first. Keep the older topeet,rk3588
    // alias for images that have not yet switched to *-standard.
    topeetRk3588.compatibleIds = QStringList{
        QStringLiteral("topeet,itop-rk3588-standard"),
        QStringLiteral("topeet,rk3588")
    };
    topeetRk3588.legacyModelTokens = QStringList{QStringLiteral("itop"), QStringLiteral("rk3588")};
    topeetRk3588.capabilities = topeetRk3588StandardCapabilities();
    topeetRk3588.ledPaths = rk3588LedPaths;

    BoardDefinition topeetRk3588Mini;
    topeetRk3588Mini.id = QStringLiteral("topeet-rk3588-mini");
    topeetRk3588Mini.displayName = QStringLiteral("iTOP-RK3588 Mini");
    topeetRk3588Mini.socFamily = QStringLiteral("RK3588");
    topeetRk3588Mini.compatibleIds = QStringList{QStringLiteral("topeet,itop-rk3588-mini")};
    topeetRk3588Mini.capabilities = topeetRk3588MiniCapabilities();
    topeetRk3588Mini.ledPaths = rk3588LedPaths;

    BoardDefinition topeetRk3588s;
    topeetRk3588s.id = QStringLiteral("topeet-rk3588s");
    topeetRk3588s.displayName = QStringLiteral("iTOP-RK3588S");
    topeetRk3588s.socFamily = QStringLiteral("RK3588S");
    topeetRk3588s.compatibleIds = QStringList{QStringLiteral("topeet,itop-rk3588s")};
    topeetRk3588s.legacyModelTokens = QStringList{QStringLiteral("topeet"), QStringLiteral("rk3588s")};
    topeetRk3588s.capabilities = topeetRk3588sCapabilities();
    // No board LEDs in the S product DTS; the buzzer is a pwm-beeper input device.

    BoardDefinition topeetRk3568;
    topeetRk3568.id = QStringLiteral("topeet-rk3568");
    topeetRk3568.displayName = QStringLiteral("iTOP-RK3568 Standard");
    topeetRk3568.socFamily = QStringLiteral("RK3568");
    topeetRk3568.compatibleIds = QStringList{
        QStringLiteral("topeet,itop-rk3568-standard"),
        QStringLiteral("topeet,itop-rk3568")
    };
    topeetRk3568.legacyModelTokens = QStringList{QStringLiteral("itop"), QStringLiteral("rk3568")};
    topeetRk3568.capabilities = topeetRk3568Capabilities();

    BoardDefinition topeetRk3568Mini;
    topeetRk3568Mini.id = QStringLiteral("topeet-rk3568-mini");
    topeetRk3568Mini.displayName = QStringLiteral("iTOP-RK3568 Mini");
    topeetRk3568Mini.socFamily = QStringLiteral("RK3568");
    topeetRk3568Mini.compatibleIds = QStringList{QStringLiteral("topeet,itop-rk3568-mini")};
    topeetRk3568Mini.capabilities = topeetRk3568MiniCapabilities();

    BoardDefinition topeetRk3562;
    topeetRk3562.id = QStringLiteral("topeet-rk3562");
    topeetRk3562.displayName = QStringLiteral("iTOP-RK3562");
    topeetRk3562.socFamily = QStringLiteral("RK3562");
    topeetRk3562.compatibleIds = QStringList{QStringLiteral("topeet,itop-rk3562")};
    topeetRk3562.legacyModelTokens = QStringList{QStringLiteral("topeet"), QStringLiteral("rk3562")};
    topeetRk3562.capabilities = topeetRk3562Capabilities();

    BoardDefinition topeetRk3576;
    topeetRk3576.id = QStringLiteral("topeet-rk3576");
    topeetRk3576.displayName = QStringLiteral("iTOP-RK3576 Standard");
    topeetRk3576.socFamily = QStringLiteral("RK3576");
    topeetRk3576.compatibleIds = QStringList{
        QStringLiteral("topeet,itop-rk3576-standard"),
        QStringLiteral("topeet,itop-rk3576")
    };
    topeetRk3576.legacyModelTokens = QStringList{QStringLiteral("topeet"), QStringLiteral("rk3576")};
    topeetRk3576.capabilities = topeetRk3576Capabilities();
    // RK3576 declares the buzzer as a standard pwm-beeper input device.
    topeetRk3576.ledPaths = QStringList{QStringLiteral("/sys/class/leds/work")};

    BoardDefinition topeetRk3576Lga;
    topeetRk3576Lga.id = QStringLiteral("topeet-rk3576-lga");
    topeetRk3576Lga.displayName = QStringLiteral("iTOP-RK3576 LGA");
    topeetRk3576Lga.socFamily = QStringLiteral("RK3576");
    topeetRk3576Lga.compatibleIds = QStringList{QStringLiteral("topeet,itop-rk3576-lga")};
    topeetRk3576Lga.capabilities = topeetRk3576LgaCapabilities();
    topeetRk3576Lga.ledPaths = QStringList{QStringLiteral("/sys/class/leds/work")};

    BoardDefinition genericRk3588;
    genericRk3588.id = QStringLiteral("generic-rk3588");
    genericRk3588.displayName = QStringLiteral("RK3588 Board");
    genericRk3588.socFamily = QStringLiteral("RK3588");
    genericRk3588.compatibleIds = QStringList{QStringLiteral("rockchip,rk3588")};
    genericRk3588.genericFallback = true;
    genericRk3588.capabilities = genericBoardCapabilities();

    BoardDefinition genericRk3568;
    genericRk3568.id = QStringLiteral("generic-rk3568");
    genericRk3568.displayName = QStringLiteral("RK3568 Board");
    genericRk3568.socFamily = QStringLiteral("RK3568");
    genericRk3568.compatibleIds = QStringList{QStringLiteral("rockchip,rk3568")};
    genericRk3568.genericFallback = true;
    genericRk3568.capabilities = genericBoardCapabilities();

    BoardDefinition genericRk3562;
    genericRk3562.id = QStringLiteral("generic-rk3562");
    genericRk3562.displayName = QStringLiteral("RK3562 Board");
    genericRk3562.socFamily = QStringLiteral("RK3562");
    genericRk3562.compatibleIds = QStringList{QStringLiteral("rockchip,rk3562")};
    genericRk3562.genericFallback = true;
    genericRk3562.capabilities = genericBoardCapabilities();

    BoardDefinition genericRk3576;
    genericRk3576.id = QStringLiteral("generic-rk3576");
    genericRk3576.displayName = QStringLiteral("RK3576 Board");
    genericRk3576.socFamily = QStringLiteral("RK3576");
    genericRk3576.compatibleIds = QStringList{QStringLiteral("rockchip,rk3576")};
    genericRk3576.genericFallback = true;
    genericRk3576.capabilities = genericBoardCapabilities();

    BoardDefinition genericRk3588s;
    genericRk3588s.id = QStringLiteral("generic-rk3588s");
    genericRk3588s.displayName = QStringLiteral("RK3588S Board");
    genericRk3588s.socFamily = QStringLiteral("RK3588S");
    genericRk3588s.compatibleIds = QStringList{QStringLiteral("rockchip,rk3588s")};
    genericRk3588s.genericFallback = true;
    genericRk3588s.capabilities = genericBoardCapabilities();

    // Exact product IDs first, then generic SoC fallbacks.
    return {topeetRk3562, topeetRk3568Mini, topeetRk3568, topeetRk3576Lga, topeetRk3576,
            topeetRk3588Mini, topeetRk3588s, topeetRk3588,
            genericRk3562, genericRk3568, genericRk3576, genericRk3588s, genericRk3588};
}

bool definitionMatchesCompatible(const BoardDefinition &definition, const QStringList &compatibleValues)
{
    for (const QString &candidate : compatibleValues) {
        for (const QString &compatibleId : definition.compatibleIds) {
            if (candidate.compare(compatibleId, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
    }
    return false;
}

bool definitionMatchesLegacyModel(const BoardDefinition &definition, const QString &model)
{
    if (definition.legacyModelTokens.isEmpty() || model.trimmed().isEmpty()) {
        return false;
    }
    for (const QString &token : definition.legacyModelTokens) {
        if (!model.contains(token, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

}

BoardProfile::BoardProfile(QObject *parent)
    : QObject(parent)
{
    const QString deviceTreeRoot = QString::fromLocal8Bit(qgetenv("BOARDLAB_DEVICE_TREE_ROOT")).trimmed().isEmpty()
        ? QStringLiteral("/proc/device-tree")
        : QString::fromLocal8Bit(qgetenv("BOARDLAB_DEVICE_TREE_ROOT")).trimmed();
    m_deviceTreeModel = readDeviceTreeValue(deviceTreeRoot, QStringLiteral("model"));
    const QStringList deviceTreeCompatibleValues = readDeviceTreeStrings(deviceTreeRoot, QStringLiteral("compatible"));
    m_deviceTreeCompatible = deviceTreeCompatibleValues.join(QLatin1Char(' '));

    const QString requestedProfile = QString::fromLocal8Bit(qgetenv("BOARDLAB_BOARD_PROFILE")).trimmed();
    const QString legacyTarget = QString::fromLocal8Bit(qgetenv("BOARDLAB_TARGET")).trimmed();

    BoardDefinition selected;
    bool matched = false;
    const QList<BoardDefinition> definitions = boardDefinitions();
    auto selectDefinition = [&](const BoardDefinition &definition, const QString &source) {
        selected = definition;
        matched = true;
        m_detectionSource = source;
        return true;
    };
    auto selectProfileId = [&](const QString &profileId, const QString &source) {
        if (profileId.isEmpty()) {
            return false;
        }
        for (const BoardDefinition &definition : definitions) {
            if (profileId == definition.id) {
                return selectDefinition(definition, source);
            }
        }
        return false;
    };
    auto selectFromCompatible = [&](bool genericFallback) {
        for (const BoardDefinition &definition : definitions) {
            if (definition.genericFallback == genericFallback
                && definitionMatchesCompatible(definition, deviceTreeCompatibleValues)) {
                return selectDefinition(definition, QStringLiteral("device-tree"));
            }
        }
        return false;
    };
    auto selectFromLegacyModel = [&]() {
        for (const BoardDefinition &definition : definitions) {
            if (!definition.genericFallback && definitionMatchesLegacyModel(definition, m_deviceTreeModel)) {
                return selectDefinition(definition, QStringLiteral("device-tree-legacy-model"));
            }
        }
        return false;
    };

    selectProfileId(requestedProfile, QStringLiteral("BOARDLAB_BOARD_PROFILE"));
    if (!matched) {
        // A board-specific compatible string is the primary identity source.
        // The legacy model check is intentionally narrow because old RK3568
        // Standard and Mini images share an otherwise indistinguishable model.
        selectFromCompatible(false);
    }
    if (!matched) {
        selectFromLegacyModel();
    }
    if (!matched) {
        selectFromCompatible(true);
    }
    if (!matched) {
        // Do not merge a legacy target marker with device-tree evidence: it
        // can be stale after an image swap.
        selectProfileId(legacyTarget, QStringLiteral("BOARDLAB_TARGET"));
    }

    if (!matched) {
        selected.id = QStringLiteral("generic-linux");
        selected.displayName = m_deviceTreeModel.isEmpty() ? QStringLiteral("Generic Linux Board") : m_deviceTreeModel;
        selected.socFamily = QSysInfo::currentCpuArchitecture();
        selected.capabilities = genericBoardCapabilities();
        m_detectionSource = m_deviceTreeModel.isEmpty() ? QStringLiteral("runtime-fallback") : QStringLiteral("device-tree-fallback");
    }

    m_id = selected.id;
    m_displayName = selected.displayName;
    m_socFamily = selected.socFamily;
    m_capabilities = selected.capabilities;
    m_ledPaths = selected.ledPaths;
    // Keep fixture paths isolated from production profiles. The runtime test
    // harness can emulate the two RK3588 LED nodes without granting a test
    // process any access to real /sys paths.
    if (qEnvironmentVariable("BOARDLAB_TEST_MODE") == QStringLiteral("1")) {
        const QString testLedRoot = QString::fromLocal8Bit(qgetenv("BOARDLAB_TEST_LED_ROOT")).trimmed();
        if (!testLedRoot.isEmpty() && !m_ledPaths.isEmpty()) {
            QStringList testLedPaths;
            for (const QString &path : m_ledPaths) {
                testLedPaths.append(QDir(testLedRoot).filePath(QFileInfo(path).fileName()));
            }
            m_ledPaths = testLedPaths;
        }
    }
    if (!selected.genericFallback
        && (m_detectionSource == QStringLiteral("device-tree")
            || m_detectionSource == QStringLiteral("device-tree-legacy-model"))) {
        refineCapabilitiesWithDeviceTree(selected, deviceTreeRoot, &m_capabilities, &m_ledPaths);
    }
}

QString BoardProfile::id() const { return m_id; }
QString BoardProfile::displayName() const { return m_displayName; }
QString BoardProfile::socFamily() const { return m_socFamily; }
QString BoardProfile::deviceTreeModel() const { return m_deviceTreeModel; }
QString BoardProfile::deviceTreeCompatible() const { return m_deviceTreeCompatible; }
QString BoardProfile::detectionSource() const { return m_detectionSource; }
QVariantMap BoardProfile::capabilities() const { return m_capabilities; }
QStringList BoardProfile::ledPaths() const { return m_ledPaths; }

bool BoardProfile::hasCapability(const QString &name) const
{
    return m_capabilities.value(name.trimmed().toLower(), false).toBool();
}

QVariantMap BoardProfile::summary() const
{
    QVariantMap data;
    data.insert(QStringLiteral("id"), m_id);
    data.insert(QStringLiteral("displayName"), m_displayName);
    data.insert(QStringLiteral("socFamily"), m_socFamily);
    data.insert(QStringLiteral("deviceTreeModel"), m_deviceTreeModel);
    data.insert(QStringLiteral("deviceTreeCompatible"), m_deviceTreeCompatible);
    data.insert(QStringLiteral("detectionSource"), m_detectionSource);
    data.insert(QStringLiteral("capabilities"), m_capabilities);
    data.insert(QStringLiteral("ledPaths"), m_ledPaths);
    return data;
}

QJsonObject BoardProfile::toJson() const
{
    return QJsonObject::fromVariantMap(summary());
}
