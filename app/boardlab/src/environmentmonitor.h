#pragma once

#include <QString>
#include <QVariantMap>

// Read-only discovery and sampling for IIO temperature/humidity devices.
//
// The synchronous sample() call waits between the temperature and humidity
// reads only when kernel/device-tree identity confirms a DHT-class sensor. Call
// it from a worker thread when it is connected to a GUI or HTTP request path.
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

    // Returns only IIO devices exposing both temperature and relative-humidity
    // channels. Each channel may use *_input or *_raw; missing raw-channel
    // offset/scale attributes use the IIO identity defaults 0 and 1. Fields
    // remain unsampled during discovery.
    QVariantMap discover() const;

    // Re-discovers devices and performs a read-only sample for every pair. A
    // confirmed DHT sensor gets the required inter-read delay; other IIO
    // devices are read immediately.
    QVariantMap sample() const;

private:
    QString m_iioRoot;
};
