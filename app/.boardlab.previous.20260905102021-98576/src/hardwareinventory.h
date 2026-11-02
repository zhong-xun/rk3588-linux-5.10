#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>

// A read-only snapshot of standard Linux hardware interfaces. It deliberately
// avoids SoC names and kernel-version checks: BSP-specific code may provide
// extra policy, but ordinary devices are discovered from their kernel ABI.
class HardwareInventory
{
public:
    struct Snapshot {
        QVariantMap capabilities;
        QVariantMap states;
        QStringList ledPaths;
    };

    static Snapshot discover(const QString &sysRoot = QString(),
                             const QString &devRoot = QString());
};
