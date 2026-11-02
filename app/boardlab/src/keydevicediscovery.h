#pragma once

#include <QString>
#include <QVariantList>

// Read-only device-tree-backed discovery for board user-key input devices.
// eventN is deliberately runtime metadata only.  deviceId/dtPath is the
// stable selector emitted to callers and consumed by KeyMonitor.
class KeyDeviceDiscovery
{
public:
    KeyDeviceDiscovery();

    QString inputClassRoot() const;
    QString inputDeviceRoot() const;
    QString deviceTreeRoot() const;

    // Test roots are honored only while BOARDLAB_TEST_MODE=1:
    // BOARDLAB_KEY_INPUT_CLASS_ROOT
    // BOARDLAB_KEY_INPUT_DEVICE_ROOT
    // BOARDLAB_KEY_DEVICE_TREE_ROOT
    static QString defaultInputClassRoot();
    static QString defaultInputDeviceRoot();
    static QString defaultDeviceTreeRoot();

    // Each row contains deviceId/dtPath, eventPath, compatible, driver,
    // keyCodes, runtime input name/bus/path, and explicit safety flags.
    QVariantList discover() const;

    // Applies KeyMonitor's shared static safety policy to discover().
    QVariantList discoverSafeCandidates() const;

private:
    QString m_inputClassRoot;
    QString m_inputDeviceRoot;
    QString m_deviceTreeRoot;
};
