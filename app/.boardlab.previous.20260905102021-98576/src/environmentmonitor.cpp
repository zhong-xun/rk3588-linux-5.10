#include "environmentmonitor.h"

#include "qtcompat.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <cmath>

namespace {

constexpr int kMinimumDhtSampleDelayMs = 3000;

struct IioChannelPaths {
    QString typeStem;
    QString channelStem;
    QString channel;
    QString inputPath;
    QString rawPath;
    QString offsetPath;
    QString scalePath;
    QString offsetProvider;
    QString scaleProvider;

    bool isValid() const
    {
        return !inputPath.isEmpty() || !rawPath.isEmpty();
    }

    bool usesProcessedInput() const
    {
        return !inputPath.isEmpty();
    }

    QString primaryPath() const
    {
        return usesProcessedInput() ? inputPath : rawPath;
    }
};

struct IioAttributePath {
    QString path;
    QString provider;
};

bool channelReadable(const IioChannelPaths &channel);

QString isoNow()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
}

bool testModeEnabled()
{
    return qEnvironmentVariable("BOARDLAB_TEST_MODE") == QStringLiteral("1");
}

QString readFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString().trimmed().isEmpty()
                ? QStringLiteral("could not open %1").arg(path)
                : file.errorString().trimmed();
        }
        return QString();
    }

    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError) {
        if (error) {
            *error = file.errorString().trimmed().isEmpty()
                ? QStringLiteral("could not read %1").arg(path)
                : file.errorString().trimmed();
        }
        return QString();
    }
    if (error) {
        error->clear();
    }
    QString value = QString::fromUtf8(bytes);
    value.remove(QChar('\0'));
    return value.trimmed();
}

QByteArray readBytes(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = file.errorString().trimmed().isEmpty()
                ? QStringLiteral("could not open %1").arg(path)
                : file.errorString().trimmed();
        }
        return QByteArray();
    }

    const QByteArray bytes = file.readAll();
    if (file.error() != QFile::NoError) {
        if (error) {
            *error = file.errorString().trimmed().isEmpty()
                ? QStringLiteral("could not read %1").arg(path)
                : file.errorString().trimmed();
        }
        return QByteArray();
    }
    if (error) {
        error->clear();
    }
    return bytes;
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

QString ueventValue(const QString &uevent, const QString &name)
{
    const QString prefix = name + QLatin1Char('=');
    const QStringList lines = uevent.split(QLatin1Char('\n'), boardlab::kSkipEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith(prefix)) {
            return line.mid(prefix.size()).trimmed();
        }
    }
    return QString();
}

bool attributeExists(const QString &path)
{
    const QFileInfo info(path);
    return info.exists() || info.isSymLink();
}

bool isChannelAttribute(const QString &fileName,
                        const QString &typeStem,
                        const QString &suffix)
{
    if (!fileName.startsWith(typeStem) || !fileName.endsWith(suffix)) {
        return false;
    }
    const int middleLength = fileName.size() - typeStem.size() - suffix.size();
    if (middleLength == 0) {
        return true;
    }
    const QChar first = fileName.at(typeStem.size());
    return first == QLatin1Char('_') || first.isDigit();
}

IioAttributePath relatedAttributePath(const QDir &directory,
                                      const QString &channelStem,
                                      const QString &typeStem,
                                      const QStringList &equivalentTypeStems,
                                      const QString &suffix)
{
    const QString channelPath = directory.filePath(channelStem + suffix);
    if (attributeExists(channelPath)) {
        return {channelPath, QStringLiteral("channel")};
    }

    const QString sharedPath = directory.filePath(typeStem + suffix);
    if (attributeExists(sharedPath)) {
        return {sharedPath, QStringLiteral("type-shared")};
    }

    for (const QString &equivalentTypeStem : equivalentTypeStems) {
        if (equivalentTypeStem == typeStem) {
            continue;
        }
        const QString equivalentPath = directory.filePath(equivalentTypeStem + suffix);
        if (attributeExists(equivalentPath)) {
            return {equivalentPath, QStringLiteral("equivalent-type-shared")};
        }
    }
    return {};
}

IioChannelPaths rawChannelPaths(const QDir &directory,
                                const QFileInfo &rawChannel,
                                const QString &typeStem,
                                const QStringList &equivalentTypeStems)
{
    const QString rawSuffix = QStringLiteral("_raw");
    const QString fileName = rawChannel.fileName();
    const QString channelStem = fileName.left(fileName.size() - rawSuffix.size());
    const IioAttributePath offset = relatedAttributePath(directory, channelStem, typeStem,
                                                         equivalentTypeStems,
                                                         QStringLiteral("_offset"));
    const IioAttributePath scale = relatedAttributePath(directory, channelStem, typeStem,
                                                        equivalentTypeStems,
                                                        QStringLiteral("_scale"));

    IioChannelPaths result;
    result.typeStem = typeStem;
    result.channelStem = channelStem;
    result.channel = fileName;
    result.rawPath = rawChannel.absoluteFilePath();
    result.offsetPath = offset.path;
    result.scalePath = scale.path;
    result.offsetProvider = offset.path.isEmpty() ? QStringLiteral("implicit-zero") : offset.provider;
    result.scaleProvider = scale.path.isEmpty() ? QStringLiteral("implicit-one") : scale.provider;
    return result;
}

IioChannelPaths discoverIioChannel(const QDir &directory,
                                   const QStringList &equivalentTypeStems)
{
    if (equivalentTypeStems.isEmpty()) {
        return {};
    }

    // Prefer a readable processed channel, but retain an unreadable one as a
    // diagnosable fallback when no raw representation exists.
    QFileInfo fallbackInput;
    QString fallbackInputType;
    for (const QString &typeStem : equivalentTypeStems) {
        const QFileInfoList inputs = directory.entryInfoList(
            QStringList() << typeStem + QStringLiteral("*_input"),
            QDir::Files | QDir::System, QDir::Name);
        for (const QFileInfo &input : inputs) {
            if (!isChannelAttribute(input.fileName(), typeStem, QStringLiteral("_input"))) {
                continue;
            }
            if (!fallbackInput.exists()) {
                fallbackInput = input;
                fallbackInputType = typeStem;
            }
            if (!input.isReadable()) {
                continue;
            }
            IioChannelPaths result;
            result.typeStem = typeStem;
            result.channelStem = input.fileName().left(
                input.fileName().size() - QStringLiteral("_input").size());
            result.channel = input.fileName();
            result.inputPath = input.absoluteFilePath();
            result.offsetProvider = QStringLiteral("kernel-processed");
            result.scaleProvider = QStringLiteral("kernel-processed");
            return result;
        }
    }

    IioChannelPaths fallbackRaw;
    for (const QString &typeStem : equivalentTypeStems) {
        const QFileInfoList rawChannels = directory.entryInfoList(
            QStringList() << typeStem + QStringLiteral("*_raw"),
            QDir::Files | QDir::System, QDir::Name);
        for (const QFileInfo &rawChannel : rawChannels) {
            if (!isChannelAttribute(rawChannel.fileName(), typeStem, QStringLiteral("_raw"))) {
                continue;
            }
            const IioChannelPaths result = rawChannelPaths(directory, rawChannel, typeStem,
                                                           equivalentTypeStems);
            if (!fallbackRaw.isValid()) {
                fallbackRaw = result;
            }
            if (channelReadable(result)) {
                return result;
            }
        }
    }
    if (fallbackRaw.isValid()) {
        return fallbackRaw;
    }

    IioChannelPaths result;
    if (fallbackInput.exists()) {
        result.typeStem = fallbackInputType;
        result.channelStem = fallbackInput.fileName().left(
            fallbackInput.fileName().size() - QStringLiteral("_input").size());
        result.channel = fallbackInput.fileName();
        result.inputPath = fallbackInput.absoluteFilePath();
        result.offsetProvider = QStringLiteral("kernel-processed");
        result.scaleProvider = QStringLiteral("kernel-processed");
    }
    return result;
}

bool channelReadable(const IioChannelPaths &channel)
{
    if (channel.usesProcessedInput()) {
        return QFileInfo(channel.inputPath).isReadable();
    }
    return QFileInfo(channel.rawPath).isReadable()
        && (channel.scalePath.isEmpty() || QFileInfo(channel.scalePath).isReadable())
        && (channel.offsetPath.isEmpty() || QFileInfo(channel.offsetPath).isReadable());
}

QVariantMap unsampledField(const IioChannelPaths &paths,
                           const QString &unit,
                           bool dhtProcessedValue,
                           int sampleDelayMs)
{
    const bool processed = paths.usesProcessedInput();
    const double normalizationDivisor = processed && dhtProcessedValue ? 1.0 : 1000.0;
    QVariantMap field;
    field.insert(QStringLiteral("path"), paths.primaryPath());
    field.insert(QStringLiteral("channel"), paths.channel);
    field.insert(QStringLiteral("unit"), unit);
    field.insert(QStringLiteral("provider"), processed
                 ? QStringLiteral("iio-processed-input")
                 : QStringLiteral("iio-raw-offset-scale"));
    field.insert(QStringLiteral("readMode"), processed
                 ? QStringLiteral("input") : QStringLiteral("raw-offset-scale"));
    field.insert(QStringLiteral("inputPath"), paths.inputPath);
    field.insert(QStringLiteral("rawPath"), paths.rawPath);
    field.insert(QStringLiteral("offsetPath"), paths.offsetPath);
    field.insert(QStringLiteral("scalePath"), paths.scalePath);
    field.insert(QStringLiteral("offsetProvider"), paths.offsetProvider);
    field.insert(QStringLiteral("scaleProvider"), paths.scaleProvider);
    field.insert(QStringLiteral("conversionProvider"), processed && dhtProcessedValue
                 ? QStringLiteral("dht-processed-native-units")
                 : QStringLiteral("linux-iio-abi"));
    field.insert(QStringLiteral("dhtProcessedValue"), processed && dhtProcessedValue);
    field.insert(QStringLiteral("nativeFormula"), processed
                 ? QStringLiteral("input")
                 : QStringLiteral("(raw + offset) * scale"));
    field.insert(QStringLiteral("conversionFormula"), normalizationDivisor == 1.0
                 ? QStringLiteral("input")
                 : (processed ? QStringLiteral("input / 1000")
                              : QStringLiteral("((raw + offset) * scale) / 1000")));
    field.insert(QStringLiteral("normalizationDivisor"), normalizationDivisor);
    field.insert(QStringLiteral("sampleDelayMs"), sampleDelayMs);
    field.insert(QStringLiteral("raw"), QString());
    field.insert(QStringLiteral("offset"), processed || paths.offsetPath.isEmpty()
                 ? QVariant(0.0) : QVariant());
    field.insert(QStringLiteral("scale"), processed || paths.scalePath.isEmpty()
                 ? QVariant(1.0) : QVariant());
    field.insert(QStringLiteral("convertedValue"), QVariant());
    field.insert(QStringLiteral("value"), QVariant());
    field.insert(QStringLiteral("timestamp"), QString());
    field.insert(QStringLiteral("valid"), false);
    field.insert(QStringLiteral("error"), QStringLiteral("not sampled"));
    return field;
}

bool readNumericAttribute(const QString &path, const QString &label, QString *text,
                          double *value, QString *error)
{
    QString readError;
    const QString raw = readFile(path, &readError);
    if (text) {
        *text = raw;
    }
    if (!readError.isEmpty()) {
        if (error) {
            *error = readError;
        }
        return false;
    }

    bool numeric = false;
    const double parsed = raw.toDouble(&numeric);
    if (!numeric || !std::isfinite(parsed)) {
        if (error) {
            *error = QStringLiteral("IIO %1 value is not numeric").arg(label);
        }
        return false;
    }
    if (value) {
        *value = parsed;
    }
    if (error) {
        error->clear();
    }
    return true;
}

QVariantMap invalidNumericField(QVariantMap field, const QString &error)
{
    field.insert(QStringLiteral("convertedValue"), QVariant());
    field.insert(QStringLiteral("value"), QVariant());
    field.insert(QStringLiteral("valid"), false);
    field.insert(QStringLiteral("error"), error);
    return field;
}

QVariantMap readNumericField(QVariantMap field)
{
    field.insert(QStringLiteral("timestamp"), isoNow());

    const QString readMode = field.value(QStringLiteral("readMode")).toString();
    QString rawText;
    QString readError;
    double rawValue = 0.0;
    if (readMode == QStringLiteral("input")) {
        if (!readNumericAttribute(field.value(QStringLiteral("inputPath")).toString(),
                                  QStringLiteral("input"), &rawText, &rawValue, &readError)) {
            field.insert(QStringLiteral("raw"), rawText);
            return invalidNumericField(field, readError);
        }
        field.insert(QStringLiteral("raw"), rawText);
        field.insert(QStringLiteral("offset"), 0.0);
        field.insert(QStringLiteral("scale"), 1.0);
        field.insert(QStringLiteral("offsetRaw"), QStringLiteral("0"));
        field.insert(QStringLiteral("scaleRaw"), QStringLiteral("1"));
    } else {
        if (!readNumericAttribute(field.value(QStringLiteral("rawPath")).toString(),
                                  QStringLiteral("raw"), &rawText, &rawValue, &readError)) {
            field.insert(QStringLiteral("raw"), rawText);
            return invalidNumericField(field, readError);
        }
        field.insert(QStringLiteral("raw"), rawText);

        double offset = 0.0;
        const QString offsetPath = field.value(QStringLiteral("offsetPath")).toString();
        if (!offsetPath.isEmpty()) {
            QString offsetText;
            if (!readNumericAttribute(offsetPath, QStringLiteral("offset"), &offsetText,
                                      &offset, &readError)) {
                return invalidNumericField(field, readError);
            }
            field.insert(QStringLiteral("offsetRaw"), offsetText);
        } else {
            field.insert(QStringLiteral("offsetRaw"), QStringLiteral("0"));
        }

        double scale = 1.0;
        const QString scalePath = field.value(QStringLiteral("scalePath")).toString();
        if (!scalePath.isEmpty()) {
            QString scaleText;
            if (!readNumericAttribute(scalePath, QStringLiteral("scale"), &scaleText,
                                      &scale, &readError)) {
                return invalidNumericField(field, readError);
            }
            field.insert(QStringLiteral("scaleRaw"), scaleText);
        } else {
            field.insert(QStringLiteral("scaleRaw"), QStringLiteral("1"));
        }
        field.insert(QStringLiteral("offset"), offset);
        field.insert(QStringLiteral("scale"), scale);
    }

    if (readMode == QStringLiteral("input")
        && field.value(QStringLiteral("dhtProcessedValue")).toBool()) {
        // Vendor DHT drivers may return C/%RH directly, while upstream-style
        // implementations follow the IIO milli-unit ABI. Values outside the
        // DHT physical range unambiguously use the latter representation.
        const QString unit = field.value(QStringLiteral("unit")).toString();
        const bool outsideDirectRange = unit == QStringLiteral("%RH")
            ? rawValue < 0.0 || rawValue > 100.0
            : rawValue < -40.0 || rawValue > 80.0;
        if (outsideDirectRange) {
            field.insert(QStringLiteral("normalizationDivisor"), 1000.0);
            field.insert(QStringLiteral("conversionProvider"),
                         QStringLiteral("linux-iio-abi-dht-milli-units"));
            field.insert(QStringLiteral("conversionFormula"), QStringLiteral("input / 1000"));
        }
    }

    const double offset = field.value(QStringLiteral("offset")).toDouble();
    const double scale = field.value(QStringLiteral("scale")).toDouble();
    const double convertedValue = (rawValue + offset) * scale;
    const double normalizationDivisor = field.value(QStringLiteral("normalizationDivisor"), 1.0).toDouble();
    const double value = convertedValue / normalizationDivisor;
    if (!std::isfinite(convertedValue) || !std::isfinite(value)) {
        return invalidNumericField(field, QStringLiteral("IIO conversion produced a non-finite value"));
    }
    field.insert(QStringLiteral("convertedValue"), convertedValue);
    field.insert(QStringLiteral("value"), value);

    // Do not apply a range rule here.  In particular, the literal "0" is a
    // successfully read numeric value and must not become a synthetic error.
    field.insert(QStringLiteral("valid"), true);
    field.insert(QStringLiteral("error"), QString());
    return field;
}

QVariantMap deviceTreeEvidence(const QString &devicePath, const QString &uevent)
{
    QVariantMap evidence;
    const QString ofNodePath = QDir(devicePath).filePath(QStringLiteral("of_node"));
    QFileInfo ofNodeInfo(ofNodePath);
    const bool available = ofNodeInfo.exists() && ofNodeInfo.isDir();
    evidence.insert(QStringLiteral("available"), available);
    evidence.insert(QStringLiteral("path"), ofNodePath);
    evidence.insert(QStringLiteral("canonicalPath"), ofNodeInfo.canonicalFilePath());
    evidence.insert(QStringLiteral("fullName"), ueventValue(uevent, QStringLiteral("OF_FULLNAME")));

    if (!available) {
        evidence.insert(QStringLiteral("name"), QString());
        evidence.insert(QStringLiteral("compatible"), QStringList());
        evidence.insert(QStringLiteral("status"), QStringLiteral("unknown"));
        evidence.insert(QStringLiteral("error"), QStringLiteral("IIO device has no readable of_node"));
        return evidence;
    }

    QString readError;
    const QString name = readFile(QDir(ofNodePath).filePath(QStringLiteral("name")), &readError);
    QString compatibleError;
    const QByteArray compatible = readBytes(QDir(ofNodePath).filePath(QStringLiteral("compatible")), &compatibleError);
    QString statusError;
    const QString status = readFile(QDir(ofNodePath).filePath(QStringLiteral("status")), &statusError);

    evidence.insert(QStringLiteral("name"), name);
    evidence.insert(QStringLiteral("compatible"), nulDelimitedStrings(compatible));
    evidence.insert(QStringLiteral("status"), status.isEmpty() ? QStringLiteral("unspecified") : status);
    evidence.insert(QStringLiteral("error"), !compatibleError.isEmpty()
                    ? compatibleError
                    : (!readError.isEmpty() ? readError : QString()));
    return evidence;
}

QVariantMap runtimeEvidence(const QString &rootPath,
                            const QFileInfo &entry,
                            const IioChannelPaths &temperature,
                            const IioChannelPaths &humidity,
                            const QString &deviceName,
                            const QString &uevent)
{
    const QString devicePath = entry.absoluteFilePath();
    const QString canonicalPath = entry.canonicalFilePath().isEmpty()
        ? devicePath
        : entry.canonicalFilePath();
    const QFileInfo driverInfo(QDir(canonicalPath).filePath(QStringLiteral("../driver")));
    const QString driverTarget = driverInfo.symLinkTarget();

    QVariantMap runtime;
    runtime.insert(QStringLiteral("available"), true);
    runtime.insert(QStringLiteral("provider"), QStringLiteral("linux-iio-sysfs"));
    runtime.insert(QStringLiteral("iioRoot"), rootPath);
    runtime.insert(QStringLiteral("deviceNode"), entry.fileName());
    runtime.insert(QStringLiteral("devicePath"), devicePath);
    runtime.insert(QStringLiteral("canonicalDevicePath"), canonicalPath);
    runtime.insert(QStringLiteral("deviceName"), deviceName);
    runtime.insert(QStringLiteral("driver"), driverTarget.isEmpty() ? QString() : QFileInfo(driverTarget).fileName());
    runtime.insert(QStringLiteral("temperaturePath"), temperature.primaryPath());
    runtime.insert(QStringLiteral("relativeHumidityPath"), humidity.primaryPath());
    runtime.insert(QStringLiteral("temperatureReadMode"), temperature.usesProcessedInput()
                   ? QStringLiteral("input") : QStringLiteral("raw-offset-scale"));
    runtime.insert(QStringLiteral("relativeHumidityReadMode"), humidity.usesProcessedInput()
                   ? QStringLiteral("input") : QStringLiteral("raw-offset-scale"));
    runtime.insert(QStringLiteral("temperatureReadable"), channelReadable(temperature));
    runtime.insert(QStringLiteral("relativeHumidityReadable"), channelReadable(humidity));
    runtime.insert(QStringLiteral("uevent"), uevent);
    runtime.insert(QStringLiteral("ofFullName"), ueventValue(uevent, QStringLiteral("OF_FULLNAME")));
    return runtime;
}

QStringList dhtIdentityEvidence(const QVariantMap &runtime, const QVariantMap &deviceTree)
{
    QStringList candidates;
    candidates << runtime.value(QStringLiteral("deviceName")).toString()
               << runtime.value(QStringLiteral("driver")).toString()
               << runtime.value(QStringLiteral("uevent")).toString()
               << runtime.value(QStringLiteral("ofFullName")).toString()
               << deviceTree.value(QStringLiteral("name")).toString()
               << deviceTree.value(QStringLiteral("fullName")).toString()
               << deviceTree.value(QStringLiteral("compatible")).toStringList();
    const QStringList dhtMarkers = QStringList()
        << QStringLiteral("dht11")
        << QStringLiteral("dht21")
        << QStringLiteral("dht22")
        << QStringLiteral("am2301")
        << QStringLiteral("am2302");

    QStringList matches;
    for (const QString &candidate : candidates) {
        const QString normalized = candidate.trimmed().toLower();
        if (normalized.isEmpty()) {
            continue;
        }
        for (const QString &marker : dhtMarkers) {
            if (normalized.contains(marker)) {
                matches.append(candidate.trimmed());
                break;
            }
        }
    }
    matches.removeDuplicates();
    return matches;
}

QVariantMap unavailableState(const QString &rootPath, const QString &reason)
{
    QVariantMap runtime;
    runtime.insert(QStringLiteral("provider"), QStringLiteral("linux-iio-sysfs"));
    runtime.insert(QStringLiteral("iioRoot"), rootPath);
    runtime.insert(QStringLiteral("testMode"), testModeEnabled());
    runtime.insert(QStringLiteral("scannedDeviceCount"), 0);
    runtime.insert(QStringLiteral("pairedSensorCount"), 0);

    QVariantMap state;
    state.insert(QStringLiteral("readOnly"), true);
    state.insert(QStringLiteral("samplingSequence"), QStringLiteral("temperature -> relative humidity"));
    state.insert(QStringLiteral("minimumSampleDelayMs"), 0);
    state.insert(QStringLiteral("discoveredAt"), isoNow());
    state.insert(QStringLiteral("sampledAt"), QString());
    state.insert(QStringLiteral("runtime"), runtime);
    state.insert(QStringLiteral("sensors"), QVariantList());
    state.insert(QStringLiteral("status"), QStringLiteral("unavailable"));
    state.insert(QStringLiteral("error"), reason);
    return state;
}

QVariantMap discoverFromRoot(const QString &rootPath)
{
    const QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        return unavailableState(rootPath, QStringLiteral("IIO root is not available"));
    }

    QDir root(rootPath);
    const QFileInfoList entries = root.entryInfoList(QStringList() << QStringLiteral("iio:device*"),
                                                     QDir::Dirs | QDir::NoDotAndDotDot,
                                                     QDir::Name);
    QVariantList sensors;
    bool hasDhtSensor = false;
    for (const QFileInfo &entry : entries) {
        const QDir deviceDir(entry.absoluteFilePath());
        const IioChannelPaths temperature = discoverIioChannel(
            deviceDir, QStringList() << QStringLiteral("in_temp"));
        const IioChannelPaths humidity = discoverIioChannel(
            deviceDir, QStringList() << QStringLiteral("in_humidityrelative")
                                     << QStringLiteral("in_humidity"));
        if (!temperature.isValid() || !humidity.isValid()) {
            continue;
        }

        QString nameError;
        const QString deviceName = readFile(deviceDir.filePath(QStringLiteral("name")), &nameError);
        QString ueventError;
        const QString uevent = readFile(deviceDir.filePath(QStringLiteral("uevent")), &ueventError);
        QVariantMap runtime = runtimeEvidence(rootPath, entry, temperature, humidity, deviceName, uevent);
        const QVariantMap deviceTree = deviceTreeEvidence(entry.absoluteFilePath(), uevent);
        const QStringList dhtEvidence = dhtIdentityEvidence(runtime, deviceTree);
        const bool dhtSensor = !dhtEvidence.isEmpty();
        runtime.insert(QStringLiteral("dhtClassSensor"), dhtSensor);
        runtime.insert(QStringLiteral("dhtEvidence"), dhtEvidence);
        runtime.insert(QStringLiteral("minimumSampleDelayMs"), dhtSensor ? kMinimumDhtSampleDelayMs : 0);
        hasDhtSensor = hasDhtSensor || dhtSensor;
        const QString ofFullName = runtime.value(QStringLiteral("ofFullName")).toString();
        const QString sensorId = !ofFullName.isEmpty()
            ? QStringLiteral("of:%1").arg(ofFullName)
            : QStringLiteral("iio:%1").arg(runtime.value(QStringLiteral("canonicalDevicePath")).toString());

        QVariantMap sensor;
        sensor.insert(QStringLiteral("id"), sensorId);
        sensor.insert(QStringLiteral("provider"), QStringLiteral("linux-iio-sysfs"));
        sensor.insert(QStringLiteral("runtime"), runtime);
        sensor.insert(QStringLiteral("deviceTree"), deviceTree);
        sensor.insert(QStringLiteral("dhtClassSensor"), dhtSensor);
        sensor.insert(QStringLiteral("dhtEvidence"), dhtEvidence);
        sensor.insert(QStringLiteral("sampleDelayMs"), dhtSensor ? kMinimumDhtSampleDelayMs : 0);
        sensor.insert(QStringLiteral("minimumSampleDelayMs"), dhtSensor ? kMinimumDhtSampleDelayMs : 0);
        sensor.insert(QStringLiteral("temperature"),
                      unsampledField(temperature, QStringLiteral("C"),
                                     dhtSensor && temperature.usesProcessedInput(), 0));
        sensor.insert(QStringLiteral("relativeHumidity"),
                      unsampledField(humidity, QStringLiteral("%RH"),
                                     dhtSensor && humidity.usesProcessedInput(),
                                     dhtSensor ? kMinimumDhtSampleDelayMs : 0));
        sensor.insert(QStringLiteral("status"), QStringLiteral("ready"));
        sensor.insert(QStringLiteral("error"), nameError.isEmpty() && ueventError.isEmpty()
                      ? QString()
                      : (!nameError.isEmpty() ? nameError : ueventError));
        sensors.append(sensor);
    }

    QVariantMap runtime;
    runtime.insert(QStringLiteral("provider"), QStringLiteral("linux-iio-sysfs"));
    runtime.insert(QStringLiteral("iioRoot"), rootPath);
    runtime.insert(QStringLiteral("testMode"), testModeEnabled());
    runtime.insert(QStringLiteral("scannedDeviceCount"), entries.size());
    runtime.insert(QStringLiteral("pairedSensorCount"), sensors.size());

    QVariantMap state;
    state.insert(QStringLiteral("readOnly"), true);
    state.insert(QStringLiteral("samplingSequence"), hasDhtSensor
                 ? QStringLiteral("temperature -> DHT wait >= 3000 ms -> relative humidity")
                 : QStringLiteral("temperature -> relative humidity"));
    state.insert(QStringLiteral("minimumSampleDelayMs"), hasDhtSensor ? kMinimumDhtSampleDelayMs : 0);
    state.insert(QStringLiteral("discoveredAt"), isoNow());
    state.insert(QStringLiteral("sampledAt"), QString());
    state.insert(QStringLiteral("runtime"), runtime);
    state.insert(QStringLiteral("sensors"), sensors);
    state.insert(QStringLiteral("status"), sensors.isEmpty() ? QStringLiteral("unavailable") : QStringLiteral("ready"));
    state.insert(QStringLiteral("error"), sensors.isEmpty()
                 ? QStringLiteral("No IIO device exposes both temperature and relative-humidity input or raw channels")
                 : QString());
    return state;
}

QVariantMap sampleSensor(QVariantMap sensor)
{
    const QVariantMap runtime = sensor.value(QStringLiteral("runtime")).toMap();
    const QVariantMap oldTemperature = sensor.value(QStringLiteral("temperature")).toMap();
    const QVariantMap oldHumidity = sensor.value(QStringLiteral("relativeHumidity")).toMap();
    const QVariantMap temperature = readNumericField(oldTemperature);
    const int sampleDelayMs = sensor.value(
        QStringLiteral("sampleDelayMs"),
        runtime.value(QStringLiteral("minimumSampleDelayMs"))).toInt();
    if (sampleDelayMs > 0) {
        QThread::msleep(static_cast<unsigned long>(sampleDelayMs));
    }
    const QVariantMap humidity = readNumericField(oldHumidity);

    const bool temperatureValid = temperature.value(QStringLiteral("valid")).toBool();
    const bool humidityValid = humidity.value(QStringLiteral("valid")).toBool();
    sensor.insert(QStringLiteral("temperature"), temperature);
    sensor.insert(QStringLiteral("relativeHumidity"), humidity);
    sensor.insert(QStringLiteral("sampledAt"), isoNow());
    if (temperatureValid && humidityValid) {
        sensor.insert(QStringLiteral("status"), QStringLiteral("sampled"));
        sensor.insert(QStringLiteral("error"), QString());
    } else if (temperatureValid || humidityValid) {
        sensor.insert(QStringLiteral("status"), QStringLiteral("partial"));
        sensor.insert(QStringLiteral("error"), QStringLiteral("Only one environment field was read successfully"));
    } else {
        sensor.insert(QStringLiteral("status"), QStringLiteral("read-error"));
        sensor.insert(QStringLiteral("error"), QStringLiteral("Neither environment field was read successfully"));
    }
    return sensor;
}

} // namespace

EnvironmentMonitor::EnvironmentMonitor(const QString &iioRoot)
    : m_iioRoot(iioRoot.trimmed().isEmpty() ? defaultIioRoot() : QDir::cleanPath(iioRoot))
{
}

QString EnvironmentMonitor::iioRoot() const
{
    return m_iioRoot;
}

QString EnvironmentMonitor::defaultIioRoot()
{
    if (testModeEnabled()) {
        const QString testRoot = QString::fromLocal8Bit(qgetenv("BOARDLAB_ENVIRONMENT_IIO_ROOT")).trimmed();
        if (!testRoot.isEmpty()) {
            return QDir::cleanPath(testRoot);
        }
    }
    return QStringLiteral("/sys/bus/iio/devices");
}

int EnvironmentMonitor::minimumDhtSampleDelayMs()
{
    return kMinimumDhtSampleDelayMs;
}

QVariantMap EnvironmentMonitor::discover() const
{
    return discoverFromRoot(m_iioRoot);
}

QVariantMap EnvironmentMonitor::sample() const
{
    QVariantMap state = discoverFromRoot(m_iioRoot);
    QVariantList sensors = state.value(QStringLiteral("sensors")).toList();
    if (sensors.isEmpty()) {
        return state;
    }

    bool allSampled = true;
    bool anyFieldValid = false;
    for (int index = 0; index < sensors.size(); ++index) {
        const QVariantMap sensor = sampleSensor(sensors.at(index).toMap());
        const QString status = sensor.value(QStringLiteral("status")).toString();
        allSampled = allSampled && status == QStringLiteral("sampled");
        const QVariantMap temperature = sensor.value(QStringLiteral("temperature")).toMap();
        const QVariantMap humidity = sensor.value(QStringLiteral("relativeHumidity")).toMap();
        anyFieldValid = anyFieldValid
                        || temperature.value(QStringLiteral("valid")).toBool()
                        || humidity.value(QStringLiteral("valid")).toBool();
        sensors[index] = sensor;
    }

    state.insert(QStringLiteral("sensors"), sensors);
    state.insert(QStringLiteral("sampledAt"), isoNow());
    if (allSampled) {
        state.insert(QStringLiteral("status"), QStringLiteral("sampled"));
        state.insert(QStringLiteral("error"), QString());
    } else if (anyFieldValid) {
        state.insert(QStringLiteral("status"), QStringLiteral("partial"));
        state.insert(QStringLiteral("error"), QStringLiteral("One or more environment fields could not be sampled"));
    } else {
        state.insert(QStringLiteral("status"), QStringLiteral("read-error"));
        state.insert(QStringLiteral("error"), QStringLiteral("No environment field could be sampled"));
    }
    return state;
}
