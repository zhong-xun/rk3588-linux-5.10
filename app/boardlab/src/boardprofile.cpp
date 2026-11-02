#include "boardprofile.h"
#include "hardwareinventory.h"
#include "runtimepaths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QSysInfo>

#include <algorithm>

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

void refineCapabilitiesWithDeviceTree(const QString &root, QVariantMap *capabilities,
                                      QStringList *ledPaths)
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
    // Runtime discovery is intentionally separate from authorization. An
    // unknown carrier can expose perfectly standard kernel endpoints whose
    // use is still disruptive (watchdog, backlight, fan, network or storage),
    // so discovery alone must never enable a hardware action. The inventory
    // remains visible through capabilityStates; a matching local profile opts
    // in to the operations that are valid for that board's wiring and image.
    QVariantMap capabilities;
    for (const QString &name : {
             QStringLiteral("gpio"), QStringLiteral("uart"),
             QStringLiteral("ethernet"), QStringLiteral("can"),
             QStringLiteral("cellular"), QStringLiteral("camera"),
             QStringLiteral("hdmiin"), QStringLiteral("watchdog"),
             QStringLiteral("music"), QStringLiteral("buzzer"),
             QStringLiteral("fan"), QStringLiteral("led"),
             QStringLiteral("backlight"), QStringLiteral("storage"),
             QStringLiteral("keys"), QStringLiteral("temperaturehumidity"),
             QStringLiteral("gpu"), QStringLiteral("wifi"),
             QStringLiteral("bluetooth"), QStringLiteral("hdmiout"),
             QStringLiteral("rtc"), QStringLiteral("npu"),
             QStringLiteral("npuyolodemo"), QStringLiteral("rs485")}) {
        capabilities.insert(name, false);
    }
    capabilities.insert(QStringLiteral("files"), true);
    capabilities.insert(QStringLiteral("web"), true);
    capabilities.insert(QStringLiteral("monitor"), true);
    capabilities.insert(QStringLiteral("diagnostics"), true);
    return capabilities;
}

QString rockchipSocFromCompatible(const QStringList &compatibleValues)
{
    static const QRegularExpression expression(
        QStringLiteral("^rockchip,((?:rk|rv)[0-9]{4}[a-z0-9]*|px[0-9]{2}[a-z0-9]*)(?:-.*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    for (const QString &compatible : compatibleValues) {
        const QRegularExpressionMatch match = expression.match(compatible.trimmed());
        if (match.hasMatch()) {
            return match.captured(1).toUpper();
        }
    }
    return QString();
}

QString configuredBoardProfilePath(bool *explicitPath)
{
    const QString environmentPath = QString::fromLocal8Bit(
        qgetenv("BOARDLAB_BOARD_CONFIG")).trimmed();
    if (explicitPath) {
        *explicitPath = !environmentPath.isEmpty();
    }
    return environmentPath.isEmpty()
        ? QStringLiteral("/etc/boardlab/board-profile.json")
        : QDir::cleanPath(environmentPath);
}

bool loadBoardConfiguration(QVariantMap *configuration, QString *loadedPath,
                            QString *errorText)
{
    bool explicitPath = false;
    const QString path = configuredBoardProfilePath(&explicitPath);
    QFile file(path);
    if (loadedPath) {
        *loadedPath = explicitPath || file.exists()
            ? QFileInfo(file).absoluteFilePath() : QString();
    }
    if (!file.exists()) {
        if (explicitPath && errorText) {
            *errorText = QStringLiteral("Board profile configuration does not exist: %1").arg(path);
        }
        return false;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorText) {
            *errorText = QStringLiteral("Could not read board profile configuration %1: %2")
                             .arg(path, file.errorString());
        }
        return false;
    }
    if (file.size() > 256 * 1024) {
        if (errorText) {
            *errorText = QStringLiteral("Board profile configuration is too large: %1").arg(path);
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (errorText) {
            *errorText = QStringLiteral("Invalid board profile configuration %1: %2")
                             .arg(path, parseError.errorString());
        }
        return false;
    }

    const QJsonObject object = document.object();
    const QSet<QString> allowedRootFields {
        QStringLiteral("schemaVersion"), QStringLiteral("identity"),
        QStringLiteral("match"), QStringLiteral("capabilities"),
        QStringLiteral("gpioAliases"), QStringLiteral("ledPaths"),
        QStringLiteral("npuYolo")
    };
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!allowedRootFields.contains(it.key())) {
            if (errorText) {
                *errorText = QStringLiteral("unknown board profile field %1 in %2")
                                 .arg(it.key(), path);
            }
            return false;
        }
    }

    const QJsonValue schemaValue = object.value(QStringLiteral("schemaVersion"));
    if (!schemaValue.isDouble()) {
        if (errorText) {
            *errorText = QStringLiteral("board profile schemaVersion is required and must be 1 in %1")
                             .arg(path);
        }
        return false;
    }
    const int schemaVersion = schemaValue.toInt(-1);
    if (schemaValue.toDouble() != 1.0 || schemaVersion != 1) {
        if (errorText) {
            *errorText = QStringLiteral("Unsupported board profile schemaVersion %1 in %2")
                             .arg(schemaVersion).arg(path);
        }
        return false;
    }

    const auto validateStringObject = [errorText, &path](const QJsonObject &parent,
                                                         const QString &key,
                                                         const QStringList &fields) {
        const QJsonValue value = parent.value(key);
        if (value.isUndefined()) {
            return true;
        }
        if (!value.isObject()) {
            if (errorText) {
                *errorText = QStringLiteral("board profile %1 must be an object in %2")
                                 .arg(key, path);
            }
            return false;
        }
        const QJsonObject child = value.toObject();
        for (auto it = child.constBegin(); it != child.constEnd(); ++it) {
            if (!fields.contains(it.key())) {
                if (errorText) {
                    *errorText = QStringLiteral("unknown board profile %1.%2 field in %3")
                                     .arg(key, it.key(), path);
                }
                return false;
            }
        }
        for (const QString &field : fields) {
            const QJsonValue fieldValue = child.value(field);
            if (!fieldValue.isUndefined() && !fieldValue.isString()) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile %1.%2 must be a string in %3")
                                     .arg(key, field, path);
                }
                return false;
            }
            if (fieldValue.isString() && fieldValue.toString().trimmed().isEmpty()) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile %1.%2 must not be empty in %3")
                                     .arg(key, field, path);
                }
                return false;
            }
        }
        return true;
    };
    if (!validateStringObject(object, QStringLiteral("identity"),
                              {QStringLiteral("id"), QStringLiteral("displayName"),
                               QStringLiteral("socFamily")})) {
        return false;
    }

    const QJsonValue capabilitiesValue = object.value(QStringLiteral("capabilities"));
    if (!capabilitiesValue.isUndefined()) {
        if (!capabilitiesValue.isObject()) {
            if (errorText) {
                *errorText = QStringLiteral("board profile capabilities must be an object in %1")
                                 .arg(path);
            }
            return false;
        }
        const QJsonObject capabilities = capabilitiesValue.toObject();
        for (auto it = capabilities.constBegin(); it != capabilities.constEnd(); ++it) {
            if (!it.value().isBool()) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile capability %1 must be boolean in %2")
                                     .arg(it.key(), path);
                }
                return false;
            }
        }
    }

    const auto validateStringOrArray = [errorText, &path](const QJsonObject &parent,
                                                          const QString &field) {
        const QJsonValue value = parent.value(field);
        if (value.isUndefined()) {
            return true;
        }
        if (value.isString()) {
            if (value.toString().trimmed().isEmpty()) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile %1 must not be empty in %2")
                                     .arg(field, path);
                }
                return false;
            }
            return true;
        }
        if (!value.isArray()) {
            if (errorText) {
                *errorText = QStringLiteral("board profile %1 must be a string or string array in %2")
                                 .arg(field, path);
            }
            return false;
        }
        const QJsonArray items = value.toArray();
        if (items.isEmpty()) {
            if (errorText) {
                *errorText = QStringLiteral("board profile %1 must not be an empty array in %2")
                                 .arg(field, path);
            }
            return false;
        }
        QSet<QString> seen;
        for (const QJsonValue &item : items) {
            const QString text = item.toString().trimmed();
            if (!item.isString() || text.isEmpty()) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile %1 contains an empty or non-string value in %2")
                                     .arg(field, path);
                }
                return false;
            }
            if (seen.contains(text)) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile %1 contains a duplicate value in %2")
                                     .arg(field, path);
                }
                return false;
            }
            seen.insert(text);
        }
        return true;
    };
    const QJsonValue matchValue = object.value(QStringLiteral("match"));
    if (!matchValue.isObject() || matchValue.toObject().isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("board profile match is required and must contain compatible or modelContains in %1")
                             .arg(path);
        }
        return false;
    }
    const QJsonObject match = matchValue.toObject();
    for (auto it = match.constBegin(); it != match.constEnd(); ++it) {
        if (it.key() != QStringLiteral("compatible")
            && it.key() != QStringLiteral("modelContains")) {
            if (errorText) {
                *errorText = QStringLiteral("unknown board profile match.%1 field in %2")
                                 .arg(it.key(), path);
            }
            return false;
        }
    }
    if (!validateStringOrArray(match, QStringLiteral("compatible"))
        || !validateStringOrArray(match, QStringLiteral("modelContains"))) {
        return false;
    }
    const QJsonValue ledPathsValue = object.value(QStringLiteral("ledPaths"));
    if (!ledPathsValue.isUndefined()) {
        if (!ledPathsValue.isArray()) {
            if (errorText) {
                *errorText = QStringLiteral("board profile ledPaths must be a string array in %1")
                                 .arg(path);
            }
            return false;
        }
        QSet<QString> seenPaths;
        for (const QJsonValue &item : ledPathsValue.toArray()) {
            const QString ledPath = QDir::cleanPath(item.toString().trimmed());
            if (!item.isString()
                || !ledPath.startsWith(QStringLiteral("/sys/class/leds/"))
                || ledPath.count(QLatin1Char('/')) != 4
                || seenPaths.contains(ledPath)) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile ledPaths contains an invalid or duplicate LED class path in %1")
                                     .arg(path);
                }
                return false;
            }
            seenPaths.insert(ledPath);
        }
    }
    const QJsonValue gpioAliasesValue = object.value(QStringLiteral("gpioAliases"));
    if (!gpioAliasesValue.isUndefined()) {
        if (!gpioAliasesValue.isObject()) {
            if (errorText) {
                *errorText = QStringLiteral("board profile gpioAliases must be an object in %1")
                                 .arg(path);
            }
            return false;
        }
        const QJsonObject aliases = gpioAliasesValue.toObject();
        for (auto it = aliases.constBegin(); it != aliases.constEnd(); ++it) {
            if ((!it.value().isString() && !it.value().isDouble())
                || (it.value().isString() && it.value().toString().trimmed().isEmpty())) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile GPIO alias %1 must be a string or integer in %2")
                                     .arg(it.key(), path);
                }
                return false;
            }
            if (it.value().isDouble()
                && (it.value().toDouble() < 0
                    || it.value().toDouble() != static_cast<double>(it.value().toInt(-1)))) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile GPIO alias %1 must be a non-negative integer in %2")
                                     .arg(it.key(), path);
                }
                return false;
            }
        }
    }
    const QJsonValue npuYoloValue = object.value(QStringLiteral("npuYolo"));
    if (!npuYoloValue.isUndefined()) {
        if (!npuYoloValue.isObject()) {
            if (errorText) {
                *errorText = QStringLiteral("board profile npuYolo must be an object in %1")
                                 .arg(path);
            }
            return false;
        }
        const QJsonObject npuYolo = npuYoloValue.toObject();
        const QSet<QString> allowedNpuFields {
            QStringLiteral("platformId"), QStringLiteral("displayName"),
            QStringLiteral("modelFileName"), QStringLiteral("workerCount"),
            QStringLiteral("telemetryCoreCount"), QStringLiteral("pinWorkersToCores")
        };
        for (auto it = npuYolo.constBegin(); it != npuYolo.constEnd(); ++it) {
            if (!allowedNpuFields.contains(it.key())) {
                if (errorText) {
                    *errorText = QStringLiteral("unknown board profile npuYolo.%1 field in %2")
                                     .arg(it.key(), path);
                }
                return false;
            }
        }

        const auto validOptionalString = [&npuYolo, errorText, &path](
                                             const QString &field,
                                             const QRegularExpression &pattern,
                                             int maximumLength) {
            const QJsonValue value = npuYolo.value(field);
            if (value.isUndefined()) {
                return true;
            }
            const QString text = value.toString().trimmed();
            if (!value.isString() || text.isEmpty() || text.size() > maximumLength
                || (!pattern.pattern().isEmpty() && !pattern.match(text).hasMatch())) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile npuYolo.%1 is invalid in %2")
                                     .arg(field, path);
                }
                return false;
            }
            return true;
        };
        if (!validOptionalString(
                QStringLiteral("platformId"),
                QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")), 64)
            || !validOptionalString(QStringLiteral("displayName"), QRegularExpression(), 96)
            || !validOptionalString(
                QStringLiteral("modelFileName"),
                QRegularExpression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,126}\\.rknn$"),
                                   QRegularExpression::CaseInsensitiveOption), 132)) {
            return false;
        }
        const QString displayName = npuYolo.value(QStringLiteral("displayName")).toString();
        for (const QChar character : displayName) {
            if (!character.isPrint()) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile npuYolo.displayName contains a non-printable character in %1")
                                     .arg(path);
                }
                return false;
            }
        }

        const auto integerField = [&npuYolo, errorText, &path](const QString &field,
                                                               int minimum, int maximum,
                                                               int *result) {
            const QJsonValue value = npuYolo.value(field);
            if (value.isUndefined()) {
                return true;
            }
            const double number = value.toDouble(-1.0);
            const int integer = value.toInt(-1);
            if (!value.isDouble() || number != static_cast<double>(integer)
                || integer < minimum || integer > maximum) {
                if (errorText) {
                    *errorText = QStringLiteral("board profile npuYolo.%1 must be an integer from %2 to %3 in %4")
                                     .arg(field).arg(minimum).arg(maximum).arg(path);
                }
                return false;
            }
            if (result) {
                *result = integer;
            }
            return true;
        };
        int workerCount = 1;
        int telemetryCoreCount = 1;
        if (!integerField(QStringLiteral("workerCount"), 1, 8, &workerCount)
            || !integerField(QStringLiteral("telemetryCoreCount"), 1, 16,
                             &telemetryCoreCount)) {
            return false;
        }
        const QJsonValue pinning = npuYolo.value(QStringLiteral("pinWorkersToCores"));
        if (!pinning.isUndefined() && !pinning.isBool()) {
            if (errorText) {
                *errorText = QStringLiteral("board profile npuYolo.pinWorkersToCores must be boolean in %1")
                                 .arg(path);
            }
            return false;
        }
        if (pinning.toBool(false) && npuYolo.contains(QStringLiteral("workerCount"))
            && (workerCount > 3
                || (npuYolo.contains(QStringLiteral("telemetryCoreCount"))
                    && workerCount > telemetryCoreCount))) {
            if (errorText) {
                *errorText = QStringLiteral("board profile npuYolo core pinning requires workerCount <= min(3, telemetryCoreCount) in %1")
                                 .arg(path);
            }
            return false;
        }
    }
    if (configuration) {
        *configuration = object.toVariantMap();
    }
    if (errorText) {
        errorText->clear();
    }
    return true;
}

QStringList configurationStrings(const QVariant &value)
{
    QStringList values;
    if (value.userType() == QMetaType::QString) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
        return values;
    }
    for (const QVariant &item : value.toList()) {
        const QString text = item.toString().trimmed();
        if (!text.isEmpty()) {
            values.append(text);
        }
    }
    return values;
}

bool boardConfigurationMatches(const QVariantMap &configuration,
                               const QString &model,
                               const QStringList &compatibleValues,
                               QString *reason)
{
    const QVariantMap match = configuration.value(QStringLiteral("match")).toMap();
    if (match.isEmpty()) {
        return true;
    }

    const QStringList requiredCompatible = configurationStrings(
        match.value(QStringLiteral("compatible")));
    if (!requiredCompatible.isEmpty()) {
        bool compatibleMatched = false;
        for (const QString &required : requiredCompatible) {
            for (const QString &actual : compatibleValues) {
                if (required.compare(actual, Qt::CaseInsensitive) == 0) {
                    compatibleMatched = true;
                    break;
                }
            }
            if (compatibleMatched) {
                break;
            }
        }
        if (!compatibleMatched) {
            if (reason) {
                *reason = QStringLiteral("board profile compatible match does not match the running device tree");
            }
            return false;
        }
    }

    const QStringList modelTokens = configurationStrings(
        match.value(QStringLiteral("modelContains")));
    if (!modelTokens.isEmpty()) {
        bool modelMatched = false;
        for (const QString &token : modelTokens) {
            if (model.contains(token, Qt::CaseInsensitive)) {
                modelMatched = true;
                break;
            }
        }
        if (!modelMatched) {
            if (reason) {
                *reason = QStringLiteral("board profile modelContains match does not match the running device tree");
            }
            return false;
        }
    }
    return true;
}

QStringList configuredLedPaths(const QVariant &value)
{
    QStringList paths;
    const QVariantList values = value.toList();
    for (const QVariant &item : values) {
        const QString path = QDir::cleanPath(item.toString().trimmed());
        if (path.startsWith(QStringLiteral("/sys/class/leds/"))) {
            paths.append(path);
        }
    }
    paths.removeDuplicates();
    return paths;
}

void mergeCapabilityStates(const QVariantMap &configured,
                           const QVariantMap &effective,
                           const QVariantMap &discoveredStates,
                           const QVariantMap &configurationCapabilities,
                           QVariantMap *states)
{
    QSet<QString> names;
    for (auto it = configured.cbegin(); it != configured.cend(); ++it) {
        names.insert(it.key().toLower());
    }
    for (auto it = effective.cbegin(); it != effective.cend(); ++it) {
        names.insert(it.key().toLower());
    }
    for (auto it = discoveredStates.cbegin(); it != discoveredStates.cend(); ++it) {
        names.insert(it.key().toLower());
    }

    QVariantMap merged;
    for (const QString &name : names) {
        QVariantMap state = discoveredStates.value(name).toMap();
        const bool allowed = effective.value(name, false).toBool();
        const bool discovered = state.value(QStringLiteral("discovered"), false).toBool();
        state.insert(QStringLiteral("configured"), configured.value(name, false).toBool());
        state.insert(QStringLiteral("allowed"), allowed);
        state.insert(QStringLiteral("configurationOverride"),
                     configurationCapabilities.contains(name));
        if (!allowed) {
            state.insert(QStringLiteral("usable"), false);
            state.insert(QStringLiteral("reason"),
                         QStringLiteral("Disabled by board policy or local configuration"));
        } else if (!discovered && state.value(QStringLiteral("reason")).toString().isEmpty()) {
            state.insert(QStringLiteral("reason"),
                         QStringLiteral("Allowed, but no runtime endpoint is currently discovered"));
        }
        merged.insert(name, state);
    }
    *states = merged;
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
    const QString deviceTreeRoot = boardlab::deviceTreeRoot();
    m_deviceTreeModel = readDeviceTreeValue(deviceTreeRoot, QStringLiteral("model"));
    const QStringList deviceTreeCompatibleValues = readDeviceTreeStrings(deviceTreeRoot, QStringLiteral("compatible"));
    m_deviceTreeCompatible = deviceTreeCompatibleValues.join(QLatin1Char(' '));

    const QString requestedProfile = QString::fromLocal8Bit(qgetenv("BOARDLAB_BOARD_PROFILE")).trimmed();
    const QString legacyTarget = QString::fromLocal8Bit(qgetenv("BOARDLAB_TARGET")).trimmed();
    const bool allowFixtureOverride = qEnvironmentVariable("BOARDLAB_TEST_MODE")
        == QStringLiteral("1");

    BoardDefinition selected;
    bool matched = false;
    bool verifiedByDeviceTree = false;
    const QList<BoardDefinition> definitions = boardDefinitions();
    const bool hasKnownProductCompatible = std::any_of(
        definitions.cbegin(), definitions.cend(),
        [&deviceTreeCompatibleValues](const BoardDefinition &definition) {
            return !definition.genericFallback
                && definitionMatchesCompatible(definition, deviceTreeCompatibleValues);
        });
    auto selectDefinition = [&](const BoardDefinition &definition, const QString &source,
                                bool deviceTreeVerified = true) {
        selected = definition;
        matched = true;
        verifiedByDeviceTree = deviceTreeVerified;
        m_detectionSource = source;
        return true;
    };
    auto selectProfileId = [&](const QString &profileId, const QString &source) {
        if (profileId.isEmpty()) {
            return false;
        }
        for (const BoardDefinition &definition : definitions) {
            if (profileId.compare(definition.id, Qt::CaseInsensitive) != 0) {
                continue;
            }
            const bool compatibleMatched = definitionMatchesCompatible(
                definition, deviceTreeCompatibleValues);
            const bool hardwareMatched = compatibleMatched
                || (!hasKnownProductCompatible && !definition.genericFallback
                    && definitionMatchesLegacyModel(definition, m_deviceTreeModel));
            if ((hasKnownProductCompatible && !compatibleMatched)
                || (!hardwareMatched && !allowFixtureOverride)) {
                return false;
            }
            // Keep the long-standing public detectionSource value stable for
            // QML/Web clients. Device-tree verification is tracked by the
            // separate internal flag and still gates every real override.
            return selectDefinition(definition, source, hardwareMatched);
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
        // Keep the identity useful when this application is copied into a new
        // Rockchip SDK before a product profile exists. The runtime inventory,
        // rather than this SoC string, decides which standard interfaces exist.
        const QString detectedSoc = rockchipSocFromCompatible(deviceTreeCompatibleValues);
        if (!detectedSoc.isEmpty()) {
            selected.id = QStringLiteral("generic-") + detectedSoc.toLower();
            selected.displayName = m_deviceTreeModel.isEmpty()
                ? QStringLiteral("Generic %1 Board").arg(detectedSoc)
                : m_deviceTreeModel;
            selected.socFamily = detectedSoc;
            selected.genericFallback = true;
            selected.capabilities = genericBoardCapabilities();
            matched = true;
            verifiedByDeviceTree = true;
            m_detectionSource = QStringLiteral("device-tree-generic-rockchip");
        }
    }
    if (!matched) {
        // Legacy target markers are hints only. They can select a profile when
        // the running DT still proves that identity, but cannot authorize a
        // copied image on an unrelated or unknown carrier.
        selectProfileId(legacyTarget, QStringLiteral("BOARDLAB_TARGET"));
    }

    if (!matched) {
        selected.id = QStringLiteral("generic-linux");
        selected.displayName = m_deviceTreeModel.isEmpty() ? QStringLiteral("Generic Linux Board") : m_deviceTreeModel;
        selected.socFamily = QSysInfo::currentCpuArchitecture();
        selected.genericFallback = true;
        selected.capabilities = genericBoardCapabilities();
        verifiedByDeviceTree = false;
        m_detectionSource = m_deviceTreeModel.isEmpty() ? QStringLiteral("runtime-fallback") : QStringLiteral("device-tree-fallback");
    }

    m_id = selected.id;
    m_displayName = selected.displayName;
    m_socFamily = selected.socFamily;
    m_configuredCapabilities = selected.capabilities;
    m_ledPaths = selected.ledPaths;
    if (!selected.genericFallback && verifiedByDeviceTree) {
        refineCapabilitiesWithDeviceTree(deviceTreeRoot, &m_configuredCapabilities,
                                         &m_ledPaths);
    }

    // Generic profiles expose diagnostics but do not authorize hardware
    // actions merely because a kernel endpoint happens to exist.
    m_capabilities = selected.genericFallback
        ? genericBoardCapabilities()
        : m_configuredCapabilities;
    const HardwareInventory::Snapshot inventory = HardwareInventory::discover(
        boardlab::sysRoot(), boardlab::devRoot());
    m_discoveredCapabilities = inventory.capabilities;
    QVariantMap loadedConfiguration;
    if (loadBoardConfiguration(&loadedConfiguration, &m_configurationPath,
                               &m_configurationError)) {
        QString mismatchReason;
        if (boardConfigurationMatches(loadedConfiguration, m_deviceTreeModel,
                                      deviceTreeCompatibleValues, &mismatchReason)) {
            m_configuration = loadedConfiguration;
        } else {
            m_configurationError = QStringLiteral("%1: %2")
                                       .arg(m_configurationPath, mismatchReason);
        }
    }
    const QVariantMap identity = m_configuration.value(QStringLiteral("identity")).toMap();
    const QString configuredId = identity.value(QStringLiteral("id")).toString().trimmed();
    const QString configuredDisplayName = identity.value(
        QStringLiteral("displayName")).toString().trimmed();
    const QString configuredSocFamily = identity.value(
        QStringLiteral("socFamily")).toString().trimmed();
    if (!configuredId.isEmpty()) {
        m_id = configuredId;
    }
    if (!configuredDisplayName.isEmpty()) {
        m_displayName = configuredDisplayName;
    }
    if (!configuredSocFamily.isEmpty()) {
        m_socFamily = configuredSocFamily;
    }

    QVariantMap configurationCapabilities;
    const QVariantMap rawCapabilities = m_configuration.value(
        QStringLiteral("capabilities")).toMap();
    for (auto it = rawCapabilities.cbegin(); it != rawCapabilities.cend(); ++it) {
        if (it.value().userType() != QMetaType::Bool) {
            continue;
        }
        const QString name = it.key().trimmed().toLower();
        if (name.isEmpty()) {
            continue;
        }
        configurationCapabilities.insert(name, it.value().toBool());
        m_configuredCapabilities.insert(name, it.value().toBool());
        m_capabilities.insert(name, it.value().toBool());
    }
    if (m_configuration.contains(QStringLiteral("ledPaths"))) {
        m_ledPaths = configuredLedPaths(m_configuration.value(QStringLiteral("ledPaths")));
    } else if (selected.genericFallback
               && configurationCapabilities.value(QStringLiteral("led"), false).toBool()) {
        m_ledPaths = inventory.ledPaths;
    }
    if (!m_configurationPath.isEmpty() && m_configurationError.isEmpty()) {
        m_detectionSource += QStringLiteral("+board-config");
    }

    // Keep fixture paths isolated from production profiles. The runtime test
    // harness can emulate the two RK3588 LED nodes without granting a test
    // process any access to real /sys paths.
    if (boardlab::testModeEnabled()) {
        const QString testLedRoot = QString::fromLocal8Bit(qgetenv("BOARDLAB_TEST_LED_ROOT")).trimmed();
        if (!testLedRoot.isEmpty() && !m_ledPaths.isEmpty()) {
            QStringList testLedPaths;
            for (const QString &path : m_ledPaths) {
                testLedPaths.append(QDir(testLedRoot).filePath(QFileInfo(path).fileName()));
            }
            m_ledPaths = testLedPaths;
        }
    }
    mergeCapabilityStates(m_configuredCapabilities, m_capabilities,
                          inventory.states, configurationCapabilities,
                          &m_capabilityStates);
}

QString BoardProfile::id() const { return m_id; }
QString BoardProfile::displayName() const { return m_displayName; }
QString BoardProfile::socFamily() const { return m_socFamily; }
QString BoardProfile::deviceTreeModel() const { return m_deviceTreeModel; }
QString BoardProfile::deviceTreeCompatible() const { return m_deviceTreeCompatible; }
QString BoardProfile::detectionSource() const { return m_detectionSource; }
QVariantMap BoardProfile::capabilities() const { return m_capabilities; }
QVariantMap BoardProfile::configuredCapabilities() const { return m_configuredCapabilities; }
QVariantMap BoardProfile::discoveredCapabilities() const { return m_discoveredCapabilities; }
QVariantMap BoardProfile::capabilityStates() const { return m_capabilityStates; }
QStringList BoardProfile::ledPaths() const { return m_ledPaths; }
QString BoardProfile::configurationPath() const { return m_configurationPath; }
QString BoardProfile::configurationError() const { return m_configurationError; }
QVariantMap BoardProfile::configuration() const { return m_configuration; }

bool BoardProfile::hasCapability(const QString &name) const
{
    return m_capabilities.value(name.trimmed().toLower(), false).toBool();
}

bool BoardProfile::isCapabilityDiscovered(const QString &name) const
{
    return m_discoveredCapabilities.value(name.trimmed().toLower(), false).toBool();
}

QVariantMap BoardProfile::capabilityState(const QString &name) const
{
    return m_capabilityStates.value(name.trimmed().toLower()).toMap();
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
    data.insert(QStringLiteral("configuredCapabilities"), m_configuredCapabilities);
    data.insert(QStringLiteral("discoveredCapabilities"), m_discoveredCapabilities);
    data.insert(QStringLiteral("capabilityStates"), m_capabilityStates);
    data.insert(QStringLiteral("ledPaths"), m_ledPaths);
    data.insert(QStringLiteral("configurationPath"), m_configurationPath);
    data.insert(QStringLiteral("configurationError"), m_configurationError);
    data.insert(QStringLiteral("configuration"), m_configuration);
    return data;
}

QJsonObject BoardProfile::toJson() const
{
    return QJsonObject::fromVariantMap(summary());
}
