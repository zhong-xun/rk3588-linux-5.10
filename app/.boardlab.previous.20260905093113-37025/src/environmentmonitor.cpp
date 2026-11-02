#include "environmentmonitor.h"

#include "qtcompat.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

namespace {

constexpr int kMinimumDhtSampleDelayMs = 3000;

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

QVariantMap unsampledField(const QString &path, const QString &channel, const QString &unit)
{
    QVariantMap field;
    field.insert(QStringLiteral("path"), path);
    field.insert(QStringLiteral("channel"), channel);
    field.insert(QStringLiteral("unit"), unit);
    field.insert(QStringLiteral("raw"), QString());
    field.insert(QStringLiteral("value"), QVariant());
    field.insert(QStringLiteral("timestamp"), QString());
    field.insert(QStringLiteral("valid"), false);
    field.insert(QStringLiteral("error"), QStringLiteral("not sampled"));
    return field;
}

QVariantMap readNumericField(const QString &path, const QString &channel, const QString &unit)
{
    QVariantMap field;
    field.insert(QStringLiteral("path"), path);
    field.insert(QStringLiteral("channel"), channel);
    field.insert(QStringLiteral("unit"), unit);
    field.insert(QStringLiteral("timestamp"), isoNow());

    QString readError;
    const QString raw = readFile(path, &readError);
    field.insert(QStringLiteral("raw"), raw);
    if (!readError.isEmpty()) {
        field.insert(QStringLiteral("value"), QVariant());
        field.insert(QStringLiteral("valid"), false);
        field.insert(QStringLiteral("error"), readError);
        return field;
    }

    bool numeric = false;
    const double value = raw.toDouble(&numeric);
    if (!numeric) {
        field.insert(QStringLiteral("value"), QVariant());
        field.insert(QStringLiteral("valid"), false);
        field.insert(QStringLiteral("error"), QStringLiteral("IIO value is not numeric"));
        return field;
    }

    // Do not apply a range rule here.  In particular, the literal "0" is a
    // successfully read numeric value and must not become a synthetic error.
    field.insert(QStringLiteral("value"), value);
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
                            const QString &temperaturePath,
                            const QString &humidityPath,
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
    runtime.insert(QStringLiteral("iioRoot"), rootPath);
    runtime.insert(QStringLiteral("deviceNode"), entry.fileName());
    runtime.insert(QStringLiteral("devicePath"), devicePath);
    runtime.insert(QStringLiteral("canonicalDevicePath"), canonicalPath);
    runtime.insert(QStringLiteral("deviceName"), deviceName);
    runtime.insert(QStringLiteral("driver"), driverTarget.isEmpty() ? QString() : QFileInfo(driverTarget).fileName());
    runtime.insert(QStringLiteral("temperaturePath"), temperaturePath);
    runtime.insert(QStringLiteral("relativeHumidityPath"), humidityPath);
    runtime.insert(QStringLiteral("temperatureReadable"), QFileInfo(temperaturePath).isReadable());
    runtime.insert(QStringLiteral("relativeHumidityReadable"), QFileInfo(humidityPath).isReadable());
    runtime.insert(QStringLiteral("uevent"), uevent);
    runtime.insert(QStringLiteral("ofFullName"), ueventValue(uevent, QStringLiteral("OF_FULLNAME")));
    return runtime;
}

QVariantMap unavailableState(const QString &rootPath, const QString &reason)
{
    QVariantMap runtime;
    runtime.insert(QStringLiteral("iioRoot"), rootPath);
    runtime.insert(QStringLiteral("testMode"), testModeEnabled());
    runtime.insert(QStringLiteral("scannedDeviceCount"), 0);
    runtime.insert(QStringLiteral("pairedSensorCount"), 0);

    QVariantMap state;
    state.insert(QStringLiteral("readOnly"), true);
    state.insert(QStringLiteral("samplingSequence"), QStringLiteral("temperature -> wait >= 3000 ms -> relative humidity"));
    state.insert(QStringLiteral("minimumSampleDelayMs"), kMinimumDhtSampleDelayMs);
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
    for (const QFileInfo &entry : entries) {
        const QDir deviceDir(entry.absoluteFilePath());
        const QFileInfoList temperatureFiles = deviceDir.entryInfoList(QStringList() << QStringLiteral("in_temp*_input"),
                                                                        QDir::Files,
                                                                        QDir::Name);
        const QFileInfoList humidityFiles = deviceDir.entryInfoList(QStringList() << QStringLiteral("in_humidityrelative*_input"),
                                                                     QDir::Files,
                                                                     QDir::Name);
        if (temperatureFiles.isEmpty() || humidityFiles.isEmpty()) {
            continue;
        }

        const QString temperaturePath = temperatureFiles.first().absoluteFilePath();
        const QString humidityPath = humidityFiles.first().absoluteFilePath();
        QString nameError;
        const QString deviceName = readFile(deviceDir.filePath(QStringLiteral("name")), &nameError);
        QString ueventError;
        const QString uevent = readFile(deviceDir.filePath(QStringLiteral("uevent")), &ueventError);
        const QVariantMap runtime = runtimeEvidence(rootPath, entry, temperaturePath, humidityPath, deviceName, uevent);
        const QVariantMap deviceTree = deviceTreeEvidence(entry.absoluteFilePath(), uevent);
        const QString ofFullName = runtime.value(QStringLiteral("ofFullName")).toString();
        const QString sensorId = !ofFullName.isEmpty()
            ? QStringLiteral("of:%1").arg(ofFullName)
            : QStringLiteral("iio:%1").arg(runtime.value(QStringLiteral("canonicalDevicePath")).toString());

        QVariantMap sensor;
        sensor.insert(QStringLiteral("id"), sensorId);
        sensor.insert(QStringLiteral("runtime"), runtime);
        sensor.insert(QStringLiteral("deviceTree"), deviceTree);
        sensor.insert(QStringLiteral("temperature"), unsampledField(temperaturePath,
                                                                       temperatureFiles.first().fileName(),
                                                                       QStringLiteral("C")));
        sensor.insert(QStringLiteral("relativeHumidity"), unsampledField(humidityPath,
                                                                            humidityFiles.first().fileName(),
                                                                            QStringLiteral("%RH")));
        sensor.insert(QStringLiteral("status"), QStringLiteral("ready"));
        sensor.insert(QStringLiteral("error"), nameError.isEmpty() && ueventError.isEmpty()
                      ? QString()
                      : (!nameError.isEmpty() ? nameError : ueventError));
        sensors.append(sensor);
    }

    QVariantMap runtime;
    runtime.insert(QStringLiteral("iioRoot"), rootPath);
    runtime.insert(QStringLiteral("testMode"), testModeEnabled());
    runtime.insert(QStringLiteral("scannedDeviceCount"), entries.size());
    runtime.insert(QStringLiteral("pairedSensorCount"), sensors.size());

    QVariantMap state;
    state.insert(QStringLiteral("readOnly"), true);
    state.insert(QStringLiteral("samplingSequence"), QStringLiteral("temperature -> wait >= 3000 ms -> relative humidity"));
    state.insert(QStringLiteral("minimumSampleDelayMs"), kMinimumDhtSampleDelayMs);
    state.insert(QStringLiteral("discoveredAt"), isoNow());
    state.insert(QStringLiteral("sampledAt"), QString());
    state.insert(QStringLiteral("runtime"), runtime);
    state.insert(QStringLiteral("sensors"), sensors);
    state.insert(QStringLiteral("status"), sensors.isEmpty() ? QStringLiteral("unavailable") : QStringLiteral("ready"));
    state.insert(QStringLiteral("error"), sensors.isEmpty()
                 ? QStringLiteral("No IIO device exposes both temperature and relative-humidity input channels")
                 : QString());
    return state;
}

QVariantMap sampleSensor(QVariantMap sensor)
{
    const QVariantMap runtime = sensor.value(QStringLiteral("runtime")).toMap();
    const QVariantMap oldTemperature = sensor.value(QStringLiteral("temperature")).toMap();
    const QVariantMap oldHumidity = sensor.value(QStringLiteral("relativeHumidity")).toMap();
    const QString temperaturePath = runtime.value(QStringLiteral("temperaturePath")).toString();
    const QString humidityPath = runtime.value(QStringLiteral("relativeHumidityPath")).toString();

    const QVariantMap temperature = readNumericField(temperaturePath,
                                                      oldTemperature.value(QStringLiteral("channel")).toString(),
                                                      QStringLiteral("C"));
    // DHT sensors need a fixed inter-read gap; the previous QElapsedTimer started
    // after the temperature read so it never subtracted that cost.
    QThread::msleep(static_cast<unsigned long>(kMinimumDhtSampleDelayMs));
    const QVariantMap humidity = readNumericField(humidityPath,
                                                   oldHumidity.value(QStringLiteral("channel")).toString(),
                                                   QStringLiteral("%RH"));

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
