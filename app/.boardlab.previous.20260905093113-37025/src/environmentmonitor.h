#pragma once

#include <QString>
#include <QVariantMap>

// Read-only discovery and sampling for IIO temperature/humidity devices.
//
// The synchronous sample() call intentionally waits at least three seconds
// between the temperature and relative-humidity reads.  Call it from a worker
// thread when it is connected to a GUI or HTTP request path.
class EnvironmentMonitor
{
public:
    explicit EnvironmentMonitor(const QString &iioRoot = QString());

    QString iioRoot() const;

    // Uses BOARDLAB_ENVIRONMENT_IIO_ROOT only when BOARDLAB_TEST_MODE=1.
    // Production always defaults to /sys/bus/iio/devices unless a caller
    // explicitly provides a root to the constructor.
    static QString defaultIioRoot();
    static int minimumDhtSampleDelayMs();

    // Returns only IIO devices exposing both in_temp*_input and
    // in_humidityrelative*_input in the same device directory.  Fields are
    // present but unsampled, so a UI can present discovery before polling.
    QVariantMap discover() const;

    // Re-discovers devices and performs the safe read-only sequence for every
    // discovered pair: temperature -> >= 3 seconds -> relative humidity.
    QVariantMap sample() const;

private:
    QString m_iioRoot;
};
