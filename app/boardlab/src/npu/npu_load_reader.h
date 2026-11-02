#pragma once

#include <QString>
#include <QVariantList>

// Parsed RKNPU utilization. Callers never receive raw kernel text, only
// validated bounded per-core percentages or an aggregate devfreq percentage.
struct NpuLoadReading {
    bool available = false;
    QVariantList cores;
    double averagePercent = -1.0;
    QString source;
    QString message;
};

class NpuLoadReader final
{
public:
    static QVariantList unavailableCores(int expectedCoreCount = 3);

    // Multi-path RKNPU load read for Buildroot root and Ubuntu/Debian non-root:
    // debugfs, NPU devfreq/sysfs, then the installed restricted helper.
    // BOARDLAB_SYS_ROOT confines discovery to a fixture sysfs tree.
    static NpuLoadReading readPrivileged(int expectedCoreCount = 3);

    // Test and restricted-mount-namespace support. Parsed values retain the
    // same contract as the privileged reader and raw debugfs text is not
    // returned in the result.
    static NpuLoadReading readDebugfsFile(const QString &path, int expectedCoreCount = 3);
};
